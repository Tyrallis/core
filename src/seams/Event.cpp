// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// The Event seams: what the runtime does at the scheduler's tick, on both builds.

#include "wxl/seams/seams.h"

#include "runtime/Core.hpp"
#include "runtime/Stages.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <cstdint>

namespace wxl::seam::Event::DispatchUpdate
{
    void WXL_CALL Before(void*, uint32_t, float elapsed_sec)
    {
        // The scheduler is dispatching, so the client is running its own loop: the first
        // tick is the last moment that still counts as before the first frame.
        wxl::core::stages::Reach(WXL_STAGE_READY);

        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return;

        wxl_update_args args{};
        args.delta = elapsed_sec;
        host->Raise(WXL_EV_Update, &args);
    }

    void WXL_CALL After(void*, uint32_t, float)
    {
        // The tick's fact is that it started; the dispatch below this seam is the client's own
        // handlers, which a mod does not observe.
    }
}
