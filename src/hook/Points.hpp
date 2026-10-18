// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// The named hook points: what a mod attaches to by name through wxl.hook.

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
