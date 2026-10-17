// The world seam: the detour that raises WXL_EV_WorldEnter.
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

#include "runtime/WorldSeam.hpp"

#include "common/Log.hpp"
#include "hook/Engine.hpp"
#include "hook/Points.hpp"
#include "runtime/Core.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"
#include "wxl/offsets/site.hpp"

#include <cstdint>

namespace
{
    constexpr char kPoint[] = "World.Preload";

    /// Numeric id of the loaded map, -1 while none. Written by CMap::Load before World::Preload
    /// runs, so it names the map being entered. The Map.cpp one: MinimapFrame.cpp has a static
    /// of the same name.
    constexpr uintptr_t kMapId = WXL_STATIC_AT("s_mapId", 0x00ADFBC4);

    using PreloadFn = void(__cdecl*)(const wxl_vec3* position, int32_t showLoading);

    PreloadFn g_original = nullptr;

    /**
     * @brief Runs the client's preload, then raises WorldEnter for the loading-screen case.
     * @param position     where the world is loaded around: the player's position.
     * @param showLoading  nonzero on the login and teleport path.
     */
    void __cdecl Detour(const wxl_vec3* position, int32_t showLoading)
    {
        g_original(position, showLoading);
        if (!showLoading) return;

        wxl::host::ModHost* host = wxl::core::Host();
        if (!host || !position) return;

        wxl_world_args args{};
        args.map_id   = static_cast<uint32_t>(*reinterpret_cast<const int32_t*>(kMapId));
        args.position = *position;
        WLOG_DEBUG(wxl::core::kTag, "world enter: map %u at %.1f %.1f %.1f", args.map_id,
                   args.position.x, args.position.y, args.position.z);
        host->Raise(WXL_EV_WorldEnter, &args);
    }
}

namespace wxl::core::world
{
    bool Install()
    {
        const wxl::hook::Point* point = wxl::hook::FindPoint(kPoint);
        if (!point) return false;
        return wxl::hook::Attach("core.WorldEnter", point->address, &Detour, &g_original)
            != wxl::hook::kNone;
    }
}
