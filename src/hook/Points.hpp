// The named hook points: what a mod attaches to by name through wxl.hook.
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

#pragma once

#include <cstdint>
#include <span>

namespace wxl::hook
{
    /// One point core names, from Points.def.
    struct Point
    {
        const char* name;
        uintptr_t   address;
        const char* signature; ///< the detour's C signature, as text
    };

    /// Every named point, in table order.
    std::span<const Point> Points();

    /// The point carrying a name, or null.
    const Point* FindPoint(const char* name);
}
