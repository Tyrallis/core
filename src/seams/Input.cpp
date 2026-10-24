// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The Input seam: what the runtime does at the client's window procedure.

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
