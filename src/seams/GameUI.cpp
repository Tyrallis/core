// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The GameUI seams: what the runtime does when the player's target, zone or mouseover moves,
// and when a click lands in the world, on both builds.

#include "wxl/seams/seams.h"

#include "binding/Binding.hpp"
#include "objects/Objects.hpp"
#include "runtime/Core.hpp"
#include "seams/Gameplay.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <cstdint>
#include <cstring>
#include <string>

namespace
{
    namespace binding = wxl::core::binding;
    namespace objects = wxl::core::objects;

    /// The target guid the mods were last told about: zero until the first change.
    uint64_t g_announced = 0;

    /// Raises TargetChanged when the client's target static moved since the last announcement.
    void Announce()
    {
        const uint64_t current = binding::ui_target_guid();
        if (current == g_announced) return;

        wxl_target_args args{};
        args.previous = objects::FromGuid(g_announced);
        args.current  = objects::FromGuid(current);
        g_announced   = current;

        if (wxl::host::ModHost* host = wxl::core::Host()) host->Raise(WXL_EV_TargetChanged, &args);
    }

    /// The zone the mods were last told about, so a call that changes nothing of it is quiet.
    struct Zone
    {
        bool        known = false;
        int32_t     areaId   = 0;
        int32_t     subzoneId = 0;
        std::string zoneText;
        std::string subzoneText;
        std::string realZoneText;
        bool        inside = false;
    };

    Zone g_zone;

    /// The object under the mouse the mods were last told about: zero for none.
    uint64_t g_mouseover = 0;

    /// The mouse buttons as the client numbers them, to the contract's 0 left, 1 right, 2 middle.
    uint32_t Button(uint32_t client)
    {
        switch (client)
        {
        case 1: return 0;
        case 4: return 1;
        case 2: return 2;
        default: return client;
        }
    }

    wxl_str Str(const char* text)
    {
        return wxl_str{text ? text : "", text ? static_cast<uint32_t>(std::strlen(text)) : 0u};
    }

    wxl_str Str(const std::string& text)
    {
        return wxl_str{text.c_str(), static_cast<uint32_t>(text.size())};
    }
}

namespace wxl::seam::GameUI::SendTarget
{
    void WXL_CALL After(uint64_t) { Announce(); }
}

namespace wxl::seam::GameUI::ClearTarget
{
    void WXL_CALL After(uint64_t, int32_t a2)
    {
        if (a2) Announce();
    }
}

namespace wxl::seam::GameUI::NewZoneFeedback
{
    void WXL_CALL Before(int32_t area_id, int32_t subzone_id, int32_t wmo_group_id, const char* zone_text,
                         const char* subzone_text, const char* real_zone_text, int32_t inside)
    {
        const char* zone     = zone_text ? zone_text : "";
        const char* subzone  = subzone_text ? subzone_text : "";
        const char* realZone = real_zone_text ? real_zone_text : "";
        const bool  indoors  = inside != 0;

        // The ids and the texts the client will adopt, against what was last announced: the
        // same zone again (a re-entry of the same map object, a reverb update) is not a change.
        const bool newArea = !g_zone.known || g_zone.areaId != area_id;
        const bool changed = newArea || g_zone.subzoneId != subzone_id || g_zone.zoneText != zone ||
                             g_zone.subzoneText != subzone || g_zone.realZoneText != realZone ||
                             g_zone.inside != indoors;
        if (!changed) return;

        g_zone.known        = true;
        g_zone.areaId       = area_id;
        g_zone.subzoneId    = subzone_id;
        g_zone.zoneText     = zone;
        g_zone.subzoneText  = subzone;
        g_zone.realZoneText = realZone;
        g_zone.inside       = indoors;

        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return;

        wxl_zone_args args{};
        args.area_id        = static_cast<uint32_t>(area_id);
        args.subzone_id     = static_cast<uint32_t>(subzone_id);
        args.wmo_group_id   = wmo_group_id;
        args.zone_text      = Str(g_zone.zoneText);
        args.subzone_text   = Str(g_zone.subzoneText);
        args.real_zone_text = Str(g_zone.realZoneText);
        args.indoors        = indoors ? WXL_TRUE : WXL_FALSE;
        args.new_area       = newArea ? WXL_TRUE : WXL_FALSE;
        host->Raise(WXL_EV_ZoneChanged, &args);
    }
}

namespace wxl::seam::GameUI::HandleObjectTrackChange
{
    void WXL_CALL Before(uint64_t guid, uint64_t prev_guid)
    {
        (void)prev_guid;
        if (guid == g_mouseover) return;

        wxl_mouseover_args args{};
        args.previous_guid = g_mouseover;
        args.previous      = objects::FromGuid(g_mouseover);
        args.current_guid  = guid;
        args.current       = objects::FromGuid(guid);
        g_mouseover        = guid;

        if (wxl::host::ModHost* host = wxl::core::Host()) host->Raise(WXL_EV_MouseoverChanged, &args);
    }
}

namespace wxl::seam::GameUI::HandleTerrainClick
{
    void WXL_CALL Before(const void* evt)
    {
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host || !evt) return;

        wxl_world_click_args args{};
        args.point.x = binding::terrain_click_x(evt);
        args.point.y = binding::terrain_click_y(evt);
        args.point.z = binding::terrain_click_z(evt);
        args.object  = objects::FromGuid(binding::terrain_click_guid(evt));
        args.button  = Button(binding::terrain_click_button(evt));
        host->Raise(WXL_EV_WorldClick, &args);
    }
}

namespace wxl::seam::GameUI::HandleSpriteClick
{
    void WXL_CALL Before(const void* evt)
    {
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host || !evt) return;

        // The event carries no point: the object's own position stands in for where it was hit.
        wxl_world_click_args args{};
        args.object = objects::FromGuid(binding::sprite_click_guid(evt));
        args.button = Button(binding::sprite_click_button(evt));
        if (args.object != WXL_HANDLE_NONE)
            if (void* object = objects::Service().Handles().Resolve(args.object))
                binding::object_position(object, &args.point);
        host->Raise(WXL_EV_WorldClick, &args);
    }
}

namespace wxl::seam::GameUI::UpdateActivePlayer
{
    void WXL_CALL After()
    {
        wxl_unit_args   args{};
        const wxl_event moved = wxl::core::seams::gameplay::LifeChanged(args);
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host || moved == WXL_EV_COUNT) return;
        if (moved == WXL_EV_PlayerDied) host->Raise(WXL_EV_PlayerDied, &args);
        else if (moved == WXL_EV_PlayerReleased) host->Raise(WXL_EV_PlayerReleased, &args);
        else host->Raise(WXL_EV_PlayerResurrected, &args);
    }
}
