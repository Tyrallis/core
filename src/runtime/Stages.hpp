// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// How far the client has booted, told by the seams that know.

#pragma once

#include "wxl/mod.h"

namespace wxl::core::stages
{
    /**
     * @brief Says the client has reached a stage, and tells the mods once.
     *
     * Called by the seams on the functions that bring each subsystem up, the same on both
     * builds. A stage is announced at most once and never after a later one, in the order the
     * contract lists them; a stage this runtime cannot recognise is never reported. Mods are
     * told on the main thread: a call from another thread is ignored, and the main thread's
     * next call announces the stage.
     */
    void Reach(wxl_stage stage);

    /// The furthest stage announced so far.
    wxl_stage Reached();

    /// Forgets what has been announced, for a runtime that starts over. Only Core.cpp calls it.
    void Restart();
}
