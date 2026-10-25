// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The Glue seams: what the runtime does when the login screens change, on both builds.

#include "wxl/seams/seams.h"

#include "binding/Binding.hpp"
#include "runtime/Core.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <cstdint>
#include <cstring>

namespace
{
    wxl_str Str(const char* text)
    {
        return wxl_str{text ? text : "", text ? static_cast<uint32_t>(std::strlen(text)) : 0u};
    }
}

namespace wxl::seam::Glue::UpdateCurrentScreen
{
    void WXL_CALL Before(const char* screen)
    {
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host || !screen) return;

        // The static still holds the screen being left: the function copies over it after.
        wxl_glue_screen_args args{};
        args.screen   = Str(screen);
        args.previous = Str(wxl::core::binding::glue_current_screen());
        host->Raise(WXL_EV_GlueScreenChanged, &args);
    }
}
