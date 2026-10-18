// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// The thunks that let a seam stand in front of a function whose arguments arrive in registers.

#pragma once

namespace wxl::core::seams::regparm
{
    /**
     * @brief A seam whose function takes its arguments in registers, and the thunk written for it.
     *
     * A generated detour reads its arguments from the stack and leaves the registers as its own
     * body left them, so it can serve none of these. Each thunk is written by hand against the
     * disassembly of its function and callers: it saves the registers, calls Before, puts them
     * back, runs the original and calls After. The seam harness drives each one on x86.
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
