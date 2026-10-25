// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The wxl.patch service: checked, journaled byte writes into the client's image.

#include "services/PatchService.hpp"

#include "common/Log.hpp"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace
{
    constexpr char kTag[] = "patch";

    /// One applied write and what it replaced.
    struct Entry
    {
        wxl_patch            id;
        std::string          name;
        uintptr_t            address;
        std::vector<uint8_t> before;
        bool                 live;
    };

    std::vector<Entry>& Journal()
    {
        static std::vector<Entry> journal;
        return journal;
    }

    std::mutex& Lock()
    {
        static std::mutex lock;
        return lock;
    }

    wxl_patch g_next = WXL_PATCH_NONE;

    /// Formats up to 16 bytes as hex for a log line.
    void Hex(const uint8_t* bytes, uint32_t size, char* out, size_t cap)
    {
        size_t at = 0;
        for (uint32_t i = 0; i < size && i < 16 && at + 4 < cap; ++i)
            at += static_cast<size_t>(snprintf(out + at, cap - at, "%s%02X", i ? " " : "", bytes[i]));
        if (size > 16 && at + 4 < cap) snprintf(out + at, cap - at, " ..");
    }

    /// Whether [address, address+size) is committed, readable memory.
    bool Readable(uintptr_t address, uint32_t size)
    {
        MEMORY_BASIC_INFORMATION info{};
        uintptr_t at = address;
        while (at < address + size)
        {
            if (!VirtualQuery(reinterpret_cast<void*>(at), &info, sizeof info)) return false;
            if (info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
                return false;
            at = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
        }
        return true;
    }

    /**
     * @brief Writes bytes with the protection lifted and put back, and the code cache flushed.
     * @return false when the protection cannot be changed.
     */
    bool Write(uintptr_t address, const void* bytes, uint32_t size)
    {
        void* at = reinterpret_cast<void*>(address);
        DWORD old = 0;
        if (!VirtualProtect(at, size, PAGE_EXECUTE_READWRITE, &old)) return false;
        memcpy(at, bytes, size);
        DWORD ignored = 0;
        VirtualProtect(at, size, old, &ignored);
        FlushInstructionCache(GetCurrentProcess(), at, size);
        return true;
    }

    wxl_patch WXL_CALL Apply(const char* name, uintptr_t address, const void* expected,
                             const void* bytes, uint32_t size)
    {
        if (!name) name = "?";
        if (!address || !expected || !bytes || size == 0)
        {
            WLOG_ERROR(kTag, "'%s': refused, null argument or zero size", name);
            return WXL_PATCH_NONE;
        }
        if (!Readable(address, size))
        {
            WLOG_ERROR(kTag, "'%s' @0x%08X: refused, %u byte(s) not readable", name,
                       static_cast<unsigned>(address), size);
            return WXL_PATCH_NONE;
        }

        std::lock_guard<std::mutex> guard(Lock());
        const uint8_t* found = reinterpret_cast<const uint8_t*>(address);
        if (memcmp(found, expected, size) != 0)
        {
            char have[64], want[64];
            Hex(found, size, have, sizeof have);
            Hex(static_cast<const uint8_t*>(expected), size, want, sizeof want);
            WLOG_ERROR(kTag, "'%s' @0x%08X: refused, found [%s], expected [%s]", name,
                       static_cast<unsigned>(address), have, want);
            return WXL_PATCH_NONE;
        }

        Entry entry{++g_next, name, address, std::vector<uint8_t>(found, found + size), true};
        if (!Write(address, bytes, size))
        {
            WLOG_ERROR(kTag, "'%s' @0x%08X: refused, cannot change protection (win32=%lu)", name,
                       static_cast<unsigned>(address), GetLastError());
            return WXL_PATCH_NONE;
        }
        Journal().push_back(std::move(entry));
        WLOG_INFO(kTag, "applied '%s' @0x%08X (%u byte(s))", name, static_cast<unsigned>(address),
                  size);
        return g_next;
    }

    wxl_result WXL_CALL Restore(wxl_patch patch)
    {
        std::lock_guard<std::mutex> guard(Lock());
        for (Entry& entry : Journal())
        {
            if (entry.id != patch || !entry.live) continue;
            if (!Write(entry.address, entry.before.data(), static_cast<uint32_t>(entry.before.size())))
            {
                WLOG_ERROR(kTag, "restore '%s' @0x%08X failed (win32=%lu)", entry.name.c_str(),
                           static_cast<unsigned>(entry.address), GetLastError());
                return WXL_E_FAILED;
            }
            entry.live = false;
            WLOG_INFO(kTag, "restored '%s' @0x%08X (%zu byte(s))", entry.name.c_str(),
                      static_cast<unsigned>(entry.address), entry.before.size());
            return WXL_OK;
        }
        return WXL_E_NOT_FOUND;
    }

    wxl_bool WXL_CALL Matches(uintptr_t address, const void* expected, uint32_t size)
    {
        if (!address || !expected || size == 0 || !Readable(address, size)) return WXL_FALSE;
        return memcmp(reinterpret_cast<const void*>(address), expected, size) == 0 ? WXL_TRUE
                                                                                   : WXL_FALSE;
    }

    uintptr_t WXL_CALL Find(const void* pattern, uint32_t size, uintptr_t after)
    {
        if (!pattern || size == 0) return 0;

        const auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
        const auto* dos  = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt   = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
        const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);

        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        {
            const DWORD flags = sec[i].Characteristics;
            if (!(flags & IMAGE_SCN_CNT_INITIALIZED_DATA)) continue;
            if (flags & (IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_EXECUTE)) continue;

            const uint8_t* begin = base + sec[i].VirtualAddress;
            const uint8_t* end   = begin + sec[i].Misc.VirtualSize;
            if (end - begin < static_cast<ptrdiff_t>(size)) continue;
            if (after && reinterpret_cast<uintptr_t>(begin) <= after)
                begin = reinterpret_cast<const uint8_t*>(after) + 1;

            for (const uint8_t* at = begin; at + size <= end; ++at)
                if (memcmp(at, pattern, size) == 0) return reinterpret_cast<uintptr_t>(at);
        }
        return 0;
    }

    const wxl_patch_api g_patch = {
        sizeof(wxl_patch_api),
        &Apply,
        &Restore,
        &Matches,
        &Find,
    };
}

namespace wxl::core::services
{
    const wxl_patch_api& PatchApi() { return g_patch; }
}
