// The process image: whether it is the client build the offset tables describe, and whether the
// patcher gave it the entry trampoline.
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

namespace wxl::core::image
{
    /// The client build every address in the offset tables belongs to.
    constexpr uint32_t kBuild = 12340;

    /**
     * @brief Whether the process executable is Wow.exe build 12340.
     *
     * Decided on the PE header's link timestamp, which the patcher does not touch: the named
     * hook points and the core's own detours are armed only when this holds, since their
     * addresses mean nothing in any other image.
     */
    bool IsClient();

    /// kBuild when IsClient, else 0.
    uint32_t Build();

    /**
     * @brief Whether the client executable carries the patcher's entry-point trampoline, which
     *        calls WarcraftXLEntry before the client's own entry point.
     *
     * Reads the executable's headers in memory: safe under the loader lock.
     */
    bool HasEntryTrampoline();
}
