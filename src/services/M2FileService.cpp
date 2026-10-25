// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// The wxl.m2file service: a parsed model's arrays read and pointed the way this client keeps
// them, memory they can reach, and the shadow pass's bones.

#include "services/M2FileService.hpp"

#include "binding/Binding.hpp"
#include "common/Log.hpp"

#include <windows.h>

#include <cstring>
#include <mutex>
#include <vector>

namespace
{
    constexpr char kTag[] = "m2file";

    // --- Memory an array can reach ------------------------------------------------------------
    //
    // On the recompiled client an array is a 32-bit offset from the array itself, so what it
    // points at must lie within 2 GB of it, which the client's allocator does not promise on x64.
    // Chunks are reserved near the anchor asked for and carved front to back; one serves every
    // later request whose anchor it is also near. On the stock client any address is in reach and
    // every chunk serves. Nothing is freed: a model's arrays never are.

    constexpr uintptr_t kReach     = 0x40000000; // 1 GB each way: well inside a 32-bit offset
    constexpr size_t    kChunkSize = 0x100000;   // 1 MB per chunk, unless one request is larger

    struct Chunk
    {
        uint8_t* base;
        size_t   size;
        size_t   used;
    };

    std::mutex         g_memoryLock;
    std::vector<Chunk> g_chunks;

    bool Near(uintptr_t anchor, uintptr_t at, size_t extent)
    {
        if (!wxl::core::services::m2file::kSelfRelative) return true;
        const uintptr_t lo = anchor < at ? anchor : at;
        const uintptr_t hi = anchor < at ? at + extent : anchor;
        return hi - lo < kReach;
    }

    /// Reserves and commits `bytes` (a multiple of the allocation granularity) within kReach of
    /// `anchor`: the free region nearest above it first, then the nearest below.
    uint8_t* ReserveNear(uintptr_t anchor, size_t bytes)
    {
        if (!wxl::core::services::m2file::kSelfRelative)
            return static_cast<uint8_t*>(VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));

        SYSTEM_INFO info;
        GetSystemInfo(&info);
        const uintptr_t granularity = info.dwAllocationGranularity;
        uintptr_t       minimum     = reinterpret_cast<uintptr_t>(info.lpMinimumApplicationAddress);
        uintptr_t       maximum     = reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress);
        if (anchor > minimum + kReach) minimum = anchor - kReach;
        if (anchor + kReach < maximum) maximum = anchor + kReach;

        MEMORY_BASIC_INFORMATION region;
        for (uintptr_t at = anchor; at < maximum && VirtualQuery(reinterpret_cast<void*>(at), &region, sizeof region);)
        {
            const uintptr_t base = reinterpret_cast<uintptr_t>(region.BaseAddress);
            const uintptr_t end  = base + region.RegionSize;
            if (region.State == MEM_FREE)
            {
                const uintptr_t candidate = ((base > at ? base : at) + granularity - 1) & ~(granularity - 1);
                if (candidate + bytes <= end && candidate + bytes <= maximum)
                    if (void* p = VirtualAlloc(reinterpret_cast<void*>(candidate), bytes, MEM_RESERVE | MEM_COMMIT,
                                               PAGE_READWRITE))
                        return static_cast<uint8_t*>(p);
            }
            at = end;
        }
        for (uintptr_t at = anchor; at > minimum && VirtualQuery(reinterpret_cast<void*>(at - 1), &region, sizeof region);)
        {
            const uintptr_t base = reinterpret_cast<uintptr_t>(region.BaseAddress);
            uintptr_t       end  = base + region.RegionSize;
            if (end > at) end = at;
            if (region.State == MEM_FREE && end - base >= bytes)
            {
                const uintptr_t candidate = (end - bytes) & ~(granularity - 1);
                if (candidate >= base && candidate >= minimum)
                    if (void* p = VirtualAlloc(reinterpret_cast<void*>(candidate), bytes, MEM_RESERVE | MEM_COMMIT,
                                               PAGE_READWRITE))
                        return static_cast<uint8_t*>(p);
            }
            at = base;
        }
        return nullptr;
    }

    // --- The table ----------------------------------------------------------------------------

    void* WXL_CALL AllocNear(const void* anchor, uint32_t size)
    {
        if (!anchor || size == 0) return nullptr;
        const uintptr_t at    = reinterpret_cast<uintptr_t>(anchor);
        const size_t    bytes = (static_cast<size_t>(size) + 15) & ~size_t(15);

        std::lock_guard lock(g_memoryLock);
        for (Chunk& chunk : g_chunks)
        {
            if (chunk.size - chunk.used < bytes) continue;
            uint8_t* p = chunk.base + chunk.used;
            if (!Near(at, reinterpret_cast<uintptr_t>(p), bytes)) continue;
            chunk.used += bytes;
            return p; // VirtualAlloc zeroed it, and nothing gave it back
        }

        const size_t reserve = bytes > kChunkSize ? (bytes + 0xFFFF) & ~size_t(0xFFFF) : kChunkSize;
        uint8_t*     base    = ReserveNear(at, reserve);
        if (!base)
        {
            WLOG_ERROR(kTag, "no memory within reach of %p for %u bytes", anchor, size);
            return nullptr;
        }
        g_chunks.push_back(Chunk{base, reserve, bytes});
        return base;
    }

    void WXL_CALL AnimateBones(void* file)
    {
        if (file) wxl::core::binding::m2_file_animate_bones(file);
    }

    // An array is { uint32_t count; uint32_t where; }: an offset from the array itself on the
    // recompiled client, an address on the stock one.
    void* WXL_CALL ArrayData(const void* array)
    {
        if (!array) return nullptr;
        uint32_t words[2];
        std::memcpy(words, array, sizeof words);
        if (!words[0]) return nullptr;
        if constexpr (wxl::core::services::m2file::kSelfRelative)
            return const_cast<uint8_t*>(static_cast<const uint8_t*>(array) + static_cast<int32_t>(words[1]));
        else
            return reinterpret_cast<void*>(static_cast<uintptr_t>(words[1]));
    }

    wxl_result WXL_CALL SetArray(void* array, const void* data, uint32_t count)
    {
        if (!array || (count && !data)) return WXL_E_INVALID;
        uint32_t words[2] = {count, 0};
        if (data)
        {
            if constexpr (wxl::core::services::m2file::kSelfRelative)
            {
                const intptr_t offset = static_cast<const uint8_t*>(data) - static_cast<const uint8_t*>(array);
                if (offset < INT32_MIN || offset > INT32_MAX)
                {
                    WLOG_WARN(kTag, "set_array: %p is out of a 32-bit offset's reach of the array at %p; "
                                    "alloc_near gives memory that is not", data, array);
                    return WXL_E_INVALID;
                }
                words[1] = static_cast<uint32_t>(static_cast<int32_t>(offset));
            }
            else
            {
                words[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
            }
        }
        std::memcpy(array, words, sizeof words);
        return WXL_OK;
    }

    const wxl_m2file_api kApi = {sizeof(wxl_m2file_api), &AllocNear, &AnimateBones, &ArrayData, &SetArray};
}

namespace wxl::core::services
{
    const wxl_m2file_api& M2FileApi() { return kApi; }
}
