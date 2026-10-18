// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// The SimpleTop seam: what the runtime does once the interface is drawn.

#include "wxl/seams/seams.h"

#include "runtime/Core.hpp"
#include "runtime/Frame.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

namespace wxl::seam::SimpleTop::OnLayerRender
{
    void WXL_CALL Before(void*) {}

    void WXL_CALL After(void* self)
    {
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host || !self) return;

        // The slot is part of the frame it is drawn in, and carries that frame's index.
        wxl_frame_args args{};
        if (!wxl::core::frame::Current(args)) return;
        host->Raise(WXL_EV_UiRenderEnd, &args);
    }
}
