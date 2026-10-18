// How far the client has booted, told by the seams that know.
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
