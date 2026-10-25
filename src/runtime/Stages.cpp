// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// How far the client has booted: the one place a stage is announced.

#include "runtime/Stages.hpp"

#include "common/Log.hpp"
#include "runtime/Core.hpp"
#include "runtime/Crash.hpp"

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
        // The client sets its own crash filter as it starts: the report goes back in front of it.
        crash::Install();
        host->Stage(stage);
    }

    wxl_stage Reached() { return g_reachedStage.load(std::memory_order_relaxed); }

    const char* ReachedName()
    {
        return g_reached.load(std::memory_order_relaxed) < 0 ? "none" : Name(Reached());
    }

    void Restart()
    {
        g_reached.store(-1);
        g_reachedStage.store(WXL_STAGE_EARLY);
    }
}
