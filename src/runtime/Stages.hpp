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
     * Nothing outside the runtime knows when the client has finished mounting its archives or
     * built its script context; the seams on those very functions do, and this is where they
     * say so. Both builds call it from the same seam bodies, so a mod is told the same thing on
     * either client.
     *
     * A stage is announced at most once, and never after a later one: the contract says stages
     * run in order. Gaps are left as gaps -- a stage this runtime cannot recognise is simply
     * never reported, which the contract allows and which is the honest answer. Announcing
     * WXL_STAGE_GRAPHICS because WXL_STAGE_SCRIPT arrived would be a lie a mod would act on.
     */
    void Reach(wxl_stage stage);

    /// The furthest stage announced so far.
    wxl_stage Reached();

    /// Forgets what has been announced, for a runtime that starts over. Only Core.cpp calls it.
    void Restart();
}
