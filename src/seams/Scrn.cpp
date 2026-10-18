// The Scrn seam: what the runtime does as the client paints a frame.
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

    void WXL_CALL After(int32_t, const void*, void*)
    {
        // Nothing: the frame closes at the present, before the image is shown, not here.
    }
}
