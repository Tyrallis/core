// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The World seams: what the runtime does at the functions that load the world, on both builds.

#include "wxl/seams/seams.h"

#include "binding/Binding.hpp"
#include "common/Log.hpp"
#include "runtime/Core.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <cstdint>

namespace
{
    /// The world the mods were last told they entered, so that a leave names it. The map id at
    /// Before is already the destination's (CMap::Load runs before World::Preload), so the
    /// function's own state cannot say what is being left.
    bool     g_inWorld = false;
    uint32_t g_mapId   = 0;
}

namespace wxl::seam::World::Preload
{
    void WXL_CALL Before(const wxl_vec3* position, int32_t show_loading)
    {
        if (!show_loading || !position || !g_inWorld) return;
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return;

        wxl_world_args args{};
        args.map_id   = g_mapId;
        args.position = *position;
        WLOG_DEBUG(wxl::core::kTag, "world leave: map %u", args.map_id);
        host->Raise(WXL_EV_WorldLeave, &args);
        g_inWorld = false;
    }

    void WXL_CALL After(const wxl_vec3* position, int32_t show_loading)
    {
        if (!show_loading || !position) return;
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return;

        wxl_world_args args{};
        args.map_id   = static_cast<uint32_t>(wxl::core::binding::map_id());
        args.position = *position;
        g_inWorld     = true;
        g_mapId       = args.map_id;
        WLOG_DEBUG(wxl::core::kTag, "world enter: map %u at %.1f %.1f %.1f", args.map_id, args.position.x, args.position.y, args.position.z);
        host->Raise(WXL_EV_WorldEnter, &args);
    }
}
