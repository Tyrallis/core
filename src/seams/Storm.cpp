// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The Storm seams: Storm's allocator, counted for wxl.footprint while a mod has started it.

#include "wxl/seams/seams.h"

#include "services/FootprintService.hpp"

#include <cstddef>
#include <cstdint>

namespace footprint = wxl::core::services::footprint;

namespace wxl::seam::Storm::SMemAlloc
{
    void WXL_CALL After(void* result, size_t bytes, const char* filename, int32_t, uint32_t)
    {
        footprint::Allocated(result, bytes, filename);
    }
}

namespace wxl::seam::Storm::SMemFree
{
    void WXL_CALL Before(void* ptr, const char*, int32_t, uint32_t)
    {
        footprint::Freeing(ptr);
    }
}

namespace wxl::seam::Storm::SMemReAlloc
{
    void WXL_CALL Before(void* ptr, size_t, const char*, int32_t, uint32_t flags)
    {
        footprint::Reallocating(ptr, flags);
    }

    void WXL_CALL After(void* result, void* ptr, size_t bytes, const char* filename, int32_t, uint32_t)
    {
        footprint::Reallocated(result, ptr, bytes, filename);
    }
}
