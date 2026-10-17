// How far the client has booted: the one place a stage is announced.
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

#include "runtime/Stages.hpp"

#include "common/Log.hpp"
#include "runtime/Core.hpp"

#include "wxl/host/mod_host.hpp"

namespace
{
    /// The furthest stage announced. Stages are announced from the client's own thread, in the
    /// order the client reaches them, so there is nothing here to guard.
    wxl_stage g_reached = WXL_STAGE_EARLY;
    bool      g_started = false;

    const char* Name(wxl_stage stage)
    {
        switch (stage)
        {
        case WXL_STAGE_EARLY:    return "early";
        case WXL_STAGE_STORAGE:  return "storage";
        case WXL_STAGE_GRAPHICS: return "graphics";
        case WXL_STAGE_SCRIPT:   return "script";
        case WXL_STAGE_READY:    return "ready";
        case WXL_STAGE_SHUTDOWN: return "shutdown";
        }
        return "?";
    }
}

namespace wxl::core::stages
{
    void Reach(wxl_stage stage)
    {
        if (g_started && stage <= g_reached) return;

        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return;

        g_started = true;
        g_reached = stage;
        WLOG_INFO(kTag, "stage %s", Name(stage));
        host->Stage(stage);
    }

    wxl_stage Reached() { return g_reached; }

    void Restart()
    {
        g_reached = WXL_STAGE_EARLY;
        g_started = false;
    }
}
