// The WorldFrame seam: what the runtime does around the world pass.
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
