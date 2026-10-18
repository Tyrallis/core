// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// The named hook points: the table out of Points.def, resolved at compile time.

#include "hook/Points.hpp"

#include "wxl/offsets/site.hpp"

#include <cstring>

namespace wxl::hook
{
    namespace
    {
        // The seams first, under the name their events are announced by, then the points that
        // exist only as detour targets.
        // Each WXL_SITE is its own constant evaluation: one initialiser evaluating every row of
        // functions.def once per seam would exceed the compiler's step limit.
        const Point kPoints[] = {
#define WXL_SEAM(domain, name, symbol, ret, conv, params) \
    { #domain "." #name, WXL_SEAM_SITE(symbol, 0), #ret " __" #conv " " #params },
#define WXL_SEAM_GATE WXL_SEAM
#include "wxl/seams/seams.def"
#undef WXL_SEAM_GATE
#undef WXL_SEAM
#define WXL_HOOK_POINT(name, address, signature) { name, address, signature },
#include "hook/Points.def"
#undef WXL_HOOK_POINT
        };
    }

    std::span<const Point> Points() { return kPoints; }

    const Point* FindPoint(const char* name)
    {
        if (!name) return nullptr;
        for (const Point& point : kPoints)
            if (strcmp(point.name, name) == 0) return &point;
        return nullptr;
    }
}
