// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// wxl.point on the recompiled client: the byte each WXL_POINT_<n> line of the fork tests, and
// the entry those lines call while it is set.

#include "points/Points.hpp"

#include "common/Log.hpp"

#include "wxl/core/points.h"

#include <cstdio>
#include <cstring>

namespace
{
    constexpr char kTag[] = "point";

    /// The client's bytes, one per point, in its own image: what the head of each point's
    /// function tests without calling into the runtime.
    volatile uint8_t* g_armed = nullptr;
}

wxl_result WXL_CALL WarcraftXLPoints(const wxl_core_points* points)
{
    using namespace wxl::core::points;
    if (!points || points->struct_size < sizeof(wxl_core_points) || !points->rows || !points->armed)
    {
        WLOG_ERROR(kTag, "WarcraftXLPoints: the client's point table is %s; no point can be attached",
                   points ? "incomplete" : "null");
        return WXL_E_INVALID;
    }
    if (points->count != kCount)
    {
        WLOG_ERROR(kTag, "WarcraftXLPoints: the client lists %u point(s) and this runtime %u: they were built from "
                         "different point tables, so no point can be attached. Rebuild both from one tree.",
                   points->count, static_cast<unsigned>(kCount));
        return WXL_E_INVALID;
    }
    for (uint32_t i = 0; i < kCount; ++i)
    {
        char mine[256];
        snprintf(mine, sizeof mine, "%s: %s", RowOf(i).name, RowOf(i).lowered);
        if (!points->rows[i] || strcmp(points->rows[i], mine) != 0)
        {
            WLOG_ERROR(kTag, "WarcraftXLPoints: point %u is `%s` in the client and `%s` here: they were built from "
                             "different point tables, so no point can be attached. Rebuild both from one tree.",
                       i + 1, points->rows[i] ? points->rows[i] : "(null)", mine);
            return WXL_E_INVALID;
        }
    }
    g_armed = points->armed;
    WLOG_INFO(kTag, "the client tests %u point(s) at their functions' heads", static_cast<unsigned>(kCount));
    return WXL_OK;
}

void WXL_CALL wxl_core_point_enter(uint32_t point, void* const* args, void* result, wxl_core_point_original original)
{
    wxl::core::points::Enter(point, args, result, original);
}

namespace wxl::core::points
{
    bool Has(uint32_t index, const char** why)
    {
        if (index >= kCount) return false;
        if (!(RowOf(index).flags & WXL_POINT_ON_X64))
        {
            if (why) *why = "x86 only: the recompiled client's source has no such function";
            return false;
        }
        if (!g_armed)
        {
            if (why) *why = "the client handed the runtime no point table (WarcraftXLPoints)";
            return false;
        }
        return true;
    }

    bool Arm(uint32_t index, char* why, size_t capacity)
    {
        if (!Has(index, nullptr))
        {
            snprintf(why, capacity, "the client handed no point table");
            return false;
        }
        g_armed[index] = 1;
        return true;
    }

    void Disarm(uint32_t index)
    {
        if (g_armed && index < kCount) g_armed[index] = 0;
    }
}
