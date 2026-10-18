// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// The WorldFrame seam: what the runtime does around the world pass.

#include "wxl/seams/seams.h"

#include "runtime/Core.hpp"
#include "runtime/Frame.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

namespace wxl::seam::WorldFrame::RenderWorld
{
    void WXL_CALL Before(void* param)
    {
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host || !param) return;

        wxl_frame_args args{};
        if (!wxl::core::frame::Current(args)) return;
        host->Raise(WXL_EV_WorldRenderBegin, &args);
    }

    void WXL_CALL After(void* param)
    {
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host || !param) return;

        // The world and the text in it are drawn, and nothing of the interface over it yet.
        wxl_frame_args args{};
        if (!wxl::core::frame::Current(args)) return;
        host->Raise(WXL_EV_WorldRenderEnd, &args);
    }
}
