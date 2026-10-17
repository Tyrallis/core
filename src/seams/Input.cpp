// The Input seam: what the runtime does at the client's window procedure.
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

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <cstdint>

namespace wxl::seam::Input::WindowProc
{
    bool WXL_CALL Gate(intptr_t* result, void** window, uint32_t* message, uintptr_t* wparam,
                       intptr_t* lparam)
    {
        (void)window;

        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return false;

        wxl_bool        handled = WXL_FALSE;
        wxl_input_args  args{};
        args.message = *message;
        args.wparam  = static_cast<uint64_t>(*wparam);
        args.lparam  = static_cast<int64_t>(*lparam);
        args.handled = &handled;

        host->Raise(WXL_EV_Input, &args);
        if (!handled) return false;

        // What a window procedure answers for a message it dealt with itself. The client's own
        // procedure never runs, so neither does its default handling of this message.
        *result = 0;
        return true;
    }
}
