// The Gx seams: what the runtime does at the present, and as the graphics device goes and comes back.
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

namespace
{
    /// The size the device renders at now. Zero when there is no device to ask.
    wxl_swapchain_args Size()
    {
        wxl_swapchain_args args{};
        wxl::core::binding::gx_window_size(&args.width, &args.height);
        return args;
    }
}

namespace wxl::seam::Gx::ScenePresent
{
    void WXL_CALL Before()
    {
        // Only the frame Scrn.OnPaint opened ends here. The loading screen presents frames of
        // its own, and a paint with nothing to draw into presents anyway; neither opened one.
        wxl_frame_args args{};
        if (!wxl::core::frame::End(args)) return;

        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return;
        host->Raise(WXL_EV_FrameEnd, &args);
    }

    void WXL_CALL After() {}
}

namespace wxl::seam::Gx::ReleaseD3dResources
{
    void WXL_CALL Before(void* self, int32_t)
    {
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host || !self) return;

        // Read before the release: this is the size of what is about to go away.
        const wxl_swapchain_args args = Size();
        host->Raise(WXL_EV_SwapchainLost, &args);
    }

    void WXL_CALL After(void*, int32_t) {}
}

namespace wxl::seam::Gx::NotifyOnDeviceRestored
{
    void WXL_CALL Before(void* self)
    {
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host || !self) return;

        // Read after the reset, which is what reaching this function means: the new size.
        const wxl_swapchain_args args = Size();
        host->Raise(WXL_EV_SwapchainRestored, &args);
    }

    void WXL_CALL After(void*) {}
}
