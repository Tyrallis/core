// The thunks that let a seam stand in front of a function whose arguments arrive in registers.
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

namespace wxl::core::seams::regparm
{
    /**
     * @brief A seam whose function takes its arguments in registers, and the thunk written for it.
     *
     * A generated detour cannot serve one of these. Its signature says the arguments are on the
     * stack, so the arguments it reads are not the function's, and -- the part that crashes the
     * client -- the registers the function expects are whatever the detour's own body left in
     * them. The thunks below are written by hand, one per seam, against the disassembly of that
     * seam's function and of its callers: they keep the registers, call the seam's Before, hand
     * the registers back, run the original, and call After.
     */
    struct Row
    {
        const char* name;     ///< "<domain>.<name>", as the seam table and the log spell it
        void*       detour;   ///< the thunk
        void**      original; ///< where the hook engine writes the trampoline
    };

    /// The thunk written for a seam, or null when none has been written for it.
    const Row* Find(const char* name);
}
