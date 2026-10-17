// The named hook points: the table out of Points.def, resolved at compile time.
// Copyright (C) 2026 WarcraftXL
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

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
    { #domain "." #name, WXL_SITE(symbol, 0), #ret " __" #conv " " #params },
#include "wxl/seams/seams.def"
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
