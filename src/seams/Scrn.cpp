// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// The Scrn seam: what the runtime does as the client paints a frame.

#include "wxl/seams/seams.h"

#include "binding/Binding.hpp"
#include "runtime/Core.hpp"
#include "runtime/Frame.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <cstdint>

namespace wxl::seam::Scrn::OnPaint
{
    void WXL_CALL Before(const void*, void*)
    {
        // The test the paint itself makes before it draws. A client minimised, or whose device
        // is lost, paints nothing, and a frame announced then would be a mod drawing without one.
        if (!wxl::core::binding::gx_has_context() || !wxl::core::binding::gx_window_visible())
            return;

        wxl_frame_args args{};
        if (!wxl::core::frame::Begin(args)) return;

        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return;
        host->Raise(WXL_EV_FrameBegin, &args);
    }
}
