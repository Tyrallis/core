// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// The GameUI seams: what the runtime does when the player's target moves, on both builds.

#include "wxl/seams/seams.h"

#include "binding/Binding.hpp"
#include "objects/Objects.hpp"
#include "runtime/Core.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <cstdint>

namespace
{
    /// The target guid the mods were last told about: zero until the first change.
    uint64_t g_announced = 0;

    /// Raises TargetChanged when the client's target static moved since the last announcement.
    void Announce()
    {
        const uint64_t current = wxl::core::binding::ui_target_guid();
        if (current == g_announced) return;

        wxl_target_args args{};
        args.previous = wxl::core::objects::FromGuid(g_announced);
        args.current  = wxl::core::objects::FromGuid(current);
        g_announced   = current;

        if (wxl::host::ModHost* host = wxl::core::Host()) host->Raise(WXL_EV_TargetChanged, &args);
    }
}

namespace wxl::seam::GameUI::SendTarget
{
    void WXL_CALL Before(uint64_t) {}

    void WXL_CALL After(uint64_t) { Announce(); }
}

namespace wxl::seam::GameUI::ClearTarget
{
    void WXL_CALL Before(uint64_t, int32_t) {}

    void WXL_CALL After(uint64_t, int32_t a2)
    {
        if (a2) Announce();
    }
}
