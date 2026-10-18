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

#include <atomic>

namespace
{
    /// Where a stage falls in the boot: the order the contract lists them in, which a stage
    /// appended later need not follow with its number. -1 for a value outside the contract.
    int Ordinal(wxl_stage stage)
    {
        switch (stage)
        {
        case WXL_STAGE_EARLY:    return 0;
        case WXL_STAGE_STORAGE:  return 1;
        case WXL_STAGE_GRAPHICS: return 2;
        case WXL_STAGE_SCRIPT:   return 3;
        case WXL_STAGE_READY:    return 4;
        case WXL_STAGE_SHUTDOWN: return 5;
        }
        return -1;
    }

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

    /// The ordinal of the furthest stage announced, -1 before the first. Written on the main
    /// thread only; read from any thread, since the file seam runs on several.
    std::atomic<int>       g_reached{-1};
    std::atomic<wxl_stage> g_reachedStage{WXL_STAGE_EARLY};
}

namespace wxl::core::stages
{
    void Reach(wxl_stage stage)
    {
        const int ordinal = Ordinal(stage);
        if (ordinal <= g_reached.load(std::memory_order_acquire)) return;

        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return;

        // Mods are told on the main thread. A seam that reaches a stage on another thread
        // leaves it to the main thread's next pass through a seam that reaches it.
        if (!host->OnMainThread()) return;

        g_reached.store(ordinal, std::memory_order_release);
        g_reachedStage.store(stage, std::memory_order_relaxed);
        WLOG_INFO(kTag, "stage %s", Name(stage));
        host->Stage(stage);
    }

    wxl_stage Reached() { return g_reachedStage.load(std::memory_order_relaxed); }

    void Restart()
    {
        g_reached.store(-1);
        g_reachedStage.store(WXL_STAGE_EARLY);
    }
}
