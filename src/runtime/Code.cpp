// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The code the client accepts: one range per executable section of every registered module,
// read without a lock on the client's hottest path, written under one on the main thread.

#include "runtime/Code.hpp"

#include "common/Log.hpp"

#include "wxl/host/guard.hpp"

#include <windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace
{
    constexpr size_t kSlots = 128;

    /**
     * @brief One executable section of a registered module.
     *
     * A slot holds a range while begin < end. A writer fills begin before end, and empties a
     * slot by its end alone, leaving begin as it was; a reader loads end first, so a slot read
     * while it is filled or emptied answers its old range or none.
     */
    struct Slot
    {
        std::atomic<uintptr_t> begin{0};
        std::atomic<uintptr_t> end{0};
        const void*            module = nullptr; ///< written and read under the lock only
    };

    Slot                   g_slots[kSlots];
    std::atomic<size_t>    g_used{0}; ///< slots ever filled: readers scan up to it
    std::atomic<uintptr_t> g_low{UINTPTR_MAX};
    std::atomic<uintptr_t> g_high{0};
    SRWLOCK                g_lock = SRWLOCK_INIT;

    struct Exclusive
    {
        Exclusive() { AcquireSRWLockExclusive(&g_lock); }
        ~Exclusive() { ReleaseSRWLockExclusive(&g_lock); }
        Exclusive(const Exclusive&)            = delete;
        Exclusive& operator=(const Exclusive&) = delete;
    };

    /// Recomputes the box every range lies in. Under the lock.
    void Rebox()
    {
        uintptr_t low = UINTPTR_MAX, high = 0;
        const size_t used = g_used.load(std::memory_order_relaxed);
        for (size_t i = 0; i < used; ++i)
        {
            const uintptr_t b = g_slots[i].begin.load(std::memory_order_relaxed);
            const uintptr_t e = g_slots[i].end.load(std::memory_order_relaxed);
            if (b >= e) continue;
            if (b < low) low = b;
            if (e > high) high = e;
        }
        g_low.store(low, std::memory_order_release);
        g_high.store(high, std::memory_order_release);
    }

    bool Registered(const void* module)
    {
        const size_t used = g_used.load(std::memory_order_relaxed);
        for (size_t i = 0; i < used; ++i)
            if (g_slots[i].module == module) return true;
        return false;
    }

    /// A free slot, or null when the table is full. Under the lock.
    Slot* FreeSlot()
    {
        const size_t used = g_used.load(std::memory_order_relaxed);
        for (size_t i = 0; i < used; ++i)
            if (!g_slots[i].module) return &g_slots[i];
        if (used == kSlots) return nullptr;
        g_used.store(used + 1, std::memory_order_release);
        return &g_slots[used];
    }

    const char* NameOf(const void* module, char* buffer, DWORD capacity)
    {
        const DWORD n = GetModuleFileNameA(static_cast<HMODULE>(const_cast<void*>(module)), buffer, capacity);
        if (n == 0 || n >= capacity) return "(a module)";
        const char* name = buffer;
        for (const char* at = buffer; *at; ++at)
            if (*at == '\\' || *at == '/') name = at + 1;
        return name;
    }

    class CodeLoader final : public wxl::host::LibraryLoader
    {
    public:
        void* Open(const std::filesystem::path& path, std::string& error) override
        {
            void* library = wxl::host::PlatformLoader().Open(path, error);
            if (library) wxl::core::code::AddModule(library);
            return library;
        }

        void* Symbol(void* library, const char* name) override
        {
            return wxl::host::PlatformLoader().Symbol(library, name);
        }

        void Close(void* library) override
        {
            wxl::core::code::RemoveModule(library);
            wxl::host::PlatformLoader().Close(library);
        }
    };
}

namespace wxl::core::code
{
    bool AddModule(const void* module)
    {
        if (!module) return false;
        const auto* base = static_cast<const uint8_t*>(module);
        const auto* dos  = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

        char       name[MAX_PATH];
        Exclusive  lock;
        if (Registered(module)) return true;

        const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
        unsigned                    ranges  = 0;
        size_t                      bytes   = 0;
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
        {
            if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
            const DWORD size = section->Misc.VirtualSize ? section->Misc.VirtualSize : section->SizeOfRawData;
            if (size == 0) continue;
            Slot* slot = FreeSlot();
            if (!slot)
            {
                WLOG_ERROR(kTag, "no room left to register the code of %s (%zu ranges); a function of it "
                                 "handed to the client would end the client",
                           NameOf(module, name, sizeof name), kSlots);
                Rebox();
                return false;
            }
            const uintptr_t begin = reinterpret_cast<uintptr_t>(base) + section->VirtualAddress;
            slot->module          = module;
            slot->begin.store(begin, std::memory_order_relaxed);
            slot->end.store(begin + size, std::memory_order_release);
            ++ranges;
            bytes += size;
        }
        Rebox();
        WLOG_DEBUG(kTag, "code of %s accepted by the client: %u range(s), %zu bytes at %p",
                   NameOf(module, name, sizeof name), ranges, bytes, module);
        return ranges != 0;
    }

    void RemoveModule(const void* module)
    {
        if (!module) return;
        Exclusive    lock;
        const size_t used = g_used.load(std::memory_order_relaxed);
        bool         found = false;
        for (size_t i = 0; i < used; ++i)
        {
            Slot& slot = g_slots[i];
            if (slot.module != module) continue;
            slot.end.store(0, std::memory_order_release);
            slot.module = nullptr;
            found       = true;
        }
        if (found) Rebox();
    }

    bool AddOwnImage()
    {
        HMODULE self = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR>(&AddOwnImage), &self))
            return false;
        return AddModule(self);
    }

    bool Accepts(const void* pointer)
    {
        const auto at = reinterpret_cast<uintptr_t>(pointer);
        if (at < g_low.load(std::memory_order_acquire) || at >= g_high.load(std::memory_order_acquire)) return false;
        const size_t used = g_used.load(std::memory_order_acquire);
        for (size_t i = 0; i < used; ++i)
        {
            const uintptr_t end = g_slots[i].end.load(std::memory_order_acquire);
            if (at < end && at >= g_slots[i].begin.load(std::memory_order_relaxed)) return true;
        }
        return false;
    }

    wxl::host::LibraryLoader& HostLoader()
    {
        static CodeLoader loader;
        return loader;
    }

    bool Handable(const void* function, const char* what)
    {
        if (Accepts(function)) return true;
        char where[192];
        wxl::host::DescribeAddress(reinterpret_cast<uintptr_t>(function), where, sizeof where);
        WLOG_ERROR(kTag, "%s: the function at %s is neither in WarcraftXL.dll nor in a binary the mod host "
                         "loaded, so the client would end at its first call; it is not handed over",
                   what ? what : "a native function", where);
        return false;
    }

    void Reset()
    {
        Exclusive    lock;
        const size_t used = g_used.load(std::memory_order_relaxed);
        for (size_t i = 0; i < used; ++i)
        {
            g_slots[i].end.store(0, std::memory_order_release);
            g_slots[i].module = nullptr;
        }
        Rebox();
    }
}
