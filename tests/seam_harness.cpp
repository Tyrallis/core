// The seam harness: the seam bodies, driven by hand against a real mod host and a fake client.
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

#include "binding/Binding.hpp"
#include "objects/Objects.hpp"
#include "seams/Table.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"
#include "wxl/seams/seams.h"

#if defined(_M_IX86)
    #include "seams/Detour.hpp"
#endif

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

// --- What Core.cpp and the attachments supply in the DLL -------------------------------------

namespace
{
    std::unique_ptr<wxl::host::ModHost> g_host;
}

namespace wxl::core
{
    wxl::host::ModHost* Host() { return g_host.get(); }
}

// --- A fake client, handed to the binding as a wxl_core_init ---------------------------------

namespace
{
    /// An object as the fake client keeps it; the binding's accessors read these.
    struct FakeObject
    {
        uint64_t guid   = 0;
        uint32_t typeId = 0;
        uint32_t entry  = 0;
        wxl_vec3 position{};
        uint32_t health = 0;
        int32_t  visibleItems[19]{};
    };

    /// A visible item as CGPlayer_C::UpdateInventoryComponent is handed one.
    struct FakeVisibleItem
    {
        int32_t itemId = 0;
    };

    struct FakeArea
    {
        int32_t tileX = 0;
        int32_t tileY = 0;
    };

    int32_t     g_mapId       = -1;
    uint64_t    g_targetGuid  = 0;
    FakeObject* g_localPlayer = nullptr;

    const FakeObject* Obj(const void* object) { return static_cast<const FakeObject*>(object); }

    int32_t WXL_CALL FakeMapId(void) { return g_mapId; }
    const void* WXL_CALL FakeLocalPlayer(void) { return g_localPlayer; }
    uint64_t WXL_CALL FakeTargetGuid(void) { return g_targetGuid; }

    uint64_t WXL_CALL FakeGuid(const void* object) { return Obj(object)->guid; }
    uint32_t WXL_CALL FakeTypeId(const void* object) { return Obj(object)->typeId; }
    uint32_t WXL_CALL FakeEntry(const void* object) { return Obj(object)->entry; }
    uint32_t WXL_CALL FakeHealth(const void* object) { return Obj(object)->health; }

    wxl_bool WXL_CALL FakePosition(const void* object, wxl_vec3* out)
    {
        if (Obj(object)->typeId == WXL_OBJECT_ITEM) return WXL_FALSE;
        *out = Obj(object)->position;
        return WXL_TRUE;
    }

    wxl_bool WXL_CALL FakeVisibleItemId(const void* object, uint32_t slot, int32_t* out)
    {
        if (slot >= 19) return WXL_FALSE;
        *out = Obj(object)->visibleItems[slot];
        return WXL_TRUE;
    }

    int32_t WXL_CALL FakeVisibleItemIdOf(const void* item)
    {
        return static_cast<const FakeVisibleItem*>(item)->itemId;
    }

    int32_t WXL_CALL FakeTileX(const void* area) { return static_cast<const FakeArea*>(area)->tileX; }
    int32_t WXL_CALL FakeTileY(const void* area) { return static_cast<const FakeArea*>(area)->tileY; }

    /// The accessors the fake client hands over: what the seams and the checks below read.
    /// Every other row stays null, which is what a client that hands nothing over looks like.
    wxl_core_init FakeInit()
    {
        wxl_core_init init{};
        init.struct_size            = sizeof init;
        init.map_id                 = &FakeMapId;
        init.local_player_object    = &FakeLocalPlayer;
        init.ui_target_guid         = &FakeTargetGuid;
        init.object_guid            = &FakeGuid;
        init.object_type_id         = &FakeTypeId;
        init.object_entry           = &FakeEntry;
        init.object_position        = &FakePosition;
        init.unit_health            = &FakeHealth;
        init.player_visible_item_id = &FakeVisibleItemId;
        init.visible_item_id        = &FakeVisibleItemIdOf;
        init.map_area_tile_x        = &FakeTileX;
        init.map_area_tile_y        = &FakeTileY;
        return init;
    }
}

// --- What a mod receives ----------------------------------------------------------------------

namespace
{
    /// One event as a handler saw it: which, and a copy of the bytes it was handed.
    struct Received
    {
        wxl_event         event;
        std::vector<char> args;

        template <class T>
        T As() const
        {
            T typed{};
            if (args.size() >= sizeof typed) memcpy(&typed, args.data(), sizeof typed);
            return typed;
        }
    };

    std::vector<Received> g_received;

    /// The size of the struct each event carries, so the harness can keep a copy of it.
    size_t ArgsSize(wxl_event event)
    {
        switch (event)
        {
        case WXL_EV_Update:            return sizeof(wxl_update_args);
        case WXL_EV_WorldEnter:
        case WXL_EV_WorldLeave:        return sizeof(wxl_world_args);
        case WXL_EV_ObjectCreate:
        case WXL_EV_ObjectDestroy:     return sizeof(wxl_object_args);
        case WXL_EV_UnitDeath:         return sizeof(wxl_unit_args);
        case WXL_EV_UnitAuraChanged:   return sizeof(wxl_unit_aura_args);
        case WXL_EV_EquipmentChanged:  return sizeof(wxl_equipment_args);
        case WXL_EV_TargetChanged:     return sizeof(wxl_target_args);
        case WXL_EV_TerrainChunkBuild: return sizeof(wxl_terrain_chunk_args);
        default:                       return 0;
        }
    }

    /// One tag per event id; a subscription's `user` is the address of its own, which is how a
    /// handler that is only handed `user` and `args` knows which event arrived.
    wxl_event g_tags[WXL_EV_COUNT]{};

    void WXL_CALL OnEvent(void* user, const void* args)
    {
        const wxl_event event = *static_cast<const wxl_event*>(user);
        Received record{ event, {} };
        const size_t size = ArgsSize(event);
        if (args && size)
        {
            record.args.resize(size);
            memcpy(record.args.data(), args, size);
        }
        g_received.push_back(std::move(record));
    }

    int g_failures = 0;

    void Check(bool ok, const char* what)
    {
        printf("%-4s %s\n", ok ? "ok" : "FAIL", what);
        if (!ok) ++g_failures;
    }

    /// How many of an event arrived.
    size_t Count(wxl_event event)
    {
        size_t n = 0;
        for (const Received& record : g_received) n += record.event == event;
        return n;
    }

    /// The one event received, or null when none or more than one arrived.
    const Received* Only(wxl_event event)
    {
        const Received* found = nullptr;
        for (const Received& record : g_received)
        {
            if (record.event != event) continue;
            if (found) return nullptr;
            found = &record;
        }
        return found;
    }
}

// --- The checks -------------------------------------------------------------------------------

namespace
{
    /// Every event the seam table says some body raises is one the host will let a mod subscribe
    /// to. A seam that raises an event the attachment never published is a silent dead event.
    void CheckEveryRaisedEventIsSubscribable()
    {
        for (const wxl_event event : wxl::core::seams::RaisedEvents())
        {
            g_tags[event] = event;
            const wxl_subscription subscription =
                g_host->Events().Subscribe(event, &OnEvent, &g_tags[event], 0);
            char what[128];
            snprintf(what, sizeof what, "a mod can subscribe to event %u", unsigned(event));
            Check(subscription != WXL_SUBSCRIPTION_NONE, what);
        }
    }

    /// Every row declares a return type and a convention the generator knows.
    void CheckEveryRowDeclaresItsAbi()
    {
        for (const wxl::core::seams::Seam& seam : wxl::core::seams::Table())
        {
            const bool known = strcmp(seam.convention, "cdecl") == 0 ||
                               strcmp(seam.convention, "thiscall") == 0 ||
                               strcmp(seam.convention, "stdcall") == 0 ||
                               strcmp(seam.convention, "regparm") == 0;
            char what[160];
            snprintf(what, sizeof what, "%s declares %s %s %s", seam.name, seam.returns,
                     seam.convention, seam.params);
            Check(known && seam.returns[0] != 0, what);
        }
    }

    /// Event.DispatchUpdate: Before raises Update carrying the tick's own elapsed_sec.
    void CheckEventDispatchUpdate()
    {
        g_received.clear();
        wxl::seam::Event::DispatchUpdate::Before(nullptr, 1000u, 0.125f);
        wxl::seam::Event::DispatchUpdate::After(nullptr, 1000u, 0.125f);

        const Received* update = Only(WXL_EV_Update);
        Check(update != nullptr, "Event.DispatchUpdate raises Update exactly once");
        if (!update) return;
        Check(update->As<wxl_update_args>().delta == 0.125f,
              "Update carries the seam's elapsed_sec as its delta");
    }

    /// World.Preload: the pair raises WorldEnter on the way in and WorldLeave only once a world
    /// has been entered, and neither when the loading screen is not being shown.
    void CheckWorldPreload()
    {
        const wxl_vec3 where{ 1.0f, 2.0f, 3.0f };
        g_mapId = 571;

        // A preload with no loading screen is a far-sight, cinematic or game-object preload.
        g_received.clear();
        wxl::seam::World::Preload::Before(&where, 0);
        wxl::seam::World::Preload::After(&where, 0);
        Check(g_received.empty(), "World.Preload raises nothing when show_loading is clear");

        // The first real preload enters a world; there is no world to leave yet.
        g_received.clear();
        wxl::seam::World::Preload::Before(&where, 1);
        wxl::seam::World::Preload::After(&where, 1);
        Check(Only(WXL_EV_WorldLeave) == nullptr, "the first world entry raises no WorldLeave");

        const Received* entered = Only(WXL_EV_WorldEnter);
        Check(entered != nullptr, "World.Preload raises WorldEnter once a world is loaded");
        if (entered)
        {
            const wxl_world_args args = entered->As<wxl_world_args>();
            Check(args.map_id == 571u, "WorldEnter carries the map the binding reports");
            Check(args.position.x == 1.0f && args.position.y == 2.0f && args.position.z == 3.0f,
                  "WorldEnter carries the position the seam was called with");
        }

        // The next one leaves the world just entered, naming it, and enters the new one.
        g_mapId = 0;
        g_received.clear();
        wxl::seam::World::Preload::Before(&where, 1);
        const Received* left = Only(WXL_EV_WorldLeave);
        Check(left != nullptr, "the next world entry raises WorldLeave first");
        if (left)
            Check(left->As<wxl_world_args>().map_id == 571u,
                  "WorldLeave names the map being left, not the one arriving");
        wxl::seam::World::Preload::After(&where, 1);
        Check(Only(WXL_EV_WorldEnter) != nullptr, "and WorldEnter after it");
    }

    /// ObjectMgr: InitObject's After tracks and announces; ObjDelete's Before announces and
    /// untracks, after which the handle a mod kept reads as absent; DestroyRemovedObject
    /// announces only what is still tracked.
    void CheckObjectMgr()
    {
        FakeObject boar;
        boar.guid     = 0xF130000000000123ull;
        boar.typeId   = WXL_OBJECT_UNIT;
        boar.entry    = 113;
        boar.position = { 4.0f, 5.0f, 6.0f };
        boar.health   = 90;

        g_received.clear();
        wxl::seam::ObjectMgr::InitObject::Before(&boar, nullptr, 1000u);
        wxl::seam::ObjectMgr::InitObject::After(&boar, nullptr, 1000u);

        const Received* created = Only(WXL_EV_ObjectCreate);
        Check(created != nullptr, "ObjectMgr.InitObject raises ObjectCreate exactly once");
        if (!created) return;
        const wxl_object_args args = created->As<wxl_object_args>();
        Check(args.object != WXL_HANDLE_NONE, "ObjectCreate carries a handle");
        Check(args.guid == boar.guid, "ObjectCreate carries the object's guid, read through the binding");
        Check(args.type == WXL_OBJECT_UNIT, "ObjectCreate carries the client's type id");

        const wxl_object_api* api = wxl::core::objects::Api();
        Check(api->valid(args.object) == WXL_TRUE, "the handle resolves through wxl.object");
        Check(api->guid(args.object) == boar.guid, "and reads the guid");
        uint32_t health = 0;
        Check(api->unit_health(args.object, &health) == WXL_OK && health == 90,
              "a unit read goes through the binding to the client's object");
        wxl_vec3 position{};
        Check(api->position(args.object, &position) == WXL_OK && position.y == 5.0f,
              "so does a position");
        Check(api->from_guid(boar.guid) == args.object, "the guid resolves back to the handle");

        // Announcing the same object again tracks it once.
        g_received.clear();
        wxl::seam::ObjectMgr::InitObject::After(&boar, nullptr, 1001u);
        Check(Only(WXL_EV_ObjectCreate) != nullptr &&
                  Only(WXL_EV_ObjectCreate)->As<wxl_object_args>().object == args.object,
              "a second InitObject on the same object answers the same handle");

        // ObjDelete: announced while resident, then absent.
        g_received.clear();
        wxl::seam::ObjectMgr::ObjDelete::Before(&boar);
        const Received* destroyed = Only(WXL_EV_ObjectDestroy);
        Check(destroyed != nullptr, "ObjectMgr.ObjDelete raises ObjectDestroy exactly once");
        if (destroyed)
        {
            Check(destroyed->As<wxl_object_args>().object == args.object,
                  "ObjectDestroy carries the handle ObjectCreate did");
            Check(destroyed->As<wxl_object_args>().guid == boar.guid, "and its guid");
        }
        wxl::seam::ObjectMgr::ObjDelete::After(&boar);
        Check(api->valid(args.object) == WXL_FALSE, "after ObjDelete the handle no longer resolves");
        Check(api->unit_health(args.object, &health) == WXL_E_NOT_FOUND,
              "a read through the stale handle is absent, not the object's last value");
        Check(api->from_guid(boar.guid) == WXL_HANDLE_NONE, "and the guid resolves to nothing");

        // DestroyRemovedObject on what ObjDelete already let go: nothing.
        g_received.clear();
        wxl::seam::ObjectMgr::DestroyRemovedObject::Before(&boar);
        wxl::seam::ObjectMgr::DestroyRemovedObject::After(&boar);
        Check(g_received.empty(), "DestroyRemovedObject after ObjDelete raises nothing");

        // DestroyRemovedObject on a tracked object (the DestroyAllObjects path): once.
        FakeObject chest;
        chest.guid   = 0xF110000000000456ull;
        chest.typeId = WXL_OBJECT_GAMEOBJECT;
        wxl::seam::ObjectMgr::InitObject::After(&chest, nullptr, 1002u);
        g_received.clear();
        wxl::seam::ObjectMgr::DestroyRemovedObject::Before(&chest);
        Check(Only(WXL_EV_ObjectDestroy) != nullptr &&
                  Only(WXL_EV_ObjectDestroy)->As<wxl_object_args>().type == WXL_OBJECT_GAMEOBJECT,
              "DestroyRemovedObject on a tracked object raises ObjectDestroy once");
        Check(api->valid(Only(WXL_EV_ObjectDestroy)->As<wxl_object_args>().object) == WXL_FALSE,
              "and the handle is gone after it");
    }

    /// Unit: OnDeath's After raises UnitDeath for a tracked unit; the aura pair raises
    /// UnitAuraChanged with the spell and the direction; an untracked unit raises nothing.
    void CheckUnit()
    {
        FakeObject wolf;
        wolf.guid   = 0xF130000000000789ull;
        wolf.typeId = WXL_OBJECT_UNIT;
        wxl::seam::ObjectMgr::InitObject::After(&wolf, nullptr, 2000u);
        const wxl_handle handle = wxl::core::objects::HandleOf(&wolf);

        g_received.clear();
        wxl::seam::Unit::OnDeath::Before(&wolf);
        wxl::seam::Unit::OnDeath::After(&wolf);
        Check(Only(WXL_EV_UnitDeath) != nullptr &&
                  Only(WXL_EV_UnitDeath)->As<wxl_unit_args>().unit == handle,
              "Unit.OnDeath raises UnitDeath once with the unit's handle");

        g_received.clear();
        wxl::seam::Unit::OnAuraAdded::Before(&wolf, 3u, 0, nullptr, 8921);
        wxl::seam::Unit::OnAuraAdded::After(&wolf, 3u, 0, nullptr, 8921);
        const Received* added = Only(WXL_EV_UnitAuraChanged);
        Check(added != nullptr, "Unit.OnAuraAdded raises UnitAuraChanged once");
        if (added)
        {
            const wxl_unit_aura_args args = added->As<wxl_unit_aura_args>();
            Check(args.unit == handle && args.spell_id == 8921u && args.removed == WXL_FALSE,
                  "with the unit, the spell, and removed clear");
        }

        g_received.clear();
        wxl::seam::Unit::OnAuraRemoved::Before(&wolf, 3u, 0, nullptr, 8921);
        wxl::seam::Unit::OnAuraRemoved::After(&wolf, 3u, 0, nullptr, 8921);
        const Received* removed = Only(WXL_EV_UnitAuraChanged);
        Check(removed != nullptr && removed->As<wxl_unit_aura_args>().removed == WXL_TRUE,
              "Unit.OnAuraRemoved raises UnitAuraChanged once with removed set");

        FakeObject stranger;
        stranger.guid   = 0xF13000000000ABCDull;
        stranger.typeId = WXL_OBJECT_UNIT;
        g_received.clear();
        wxl::seam::Unit::OnDeath::After(&stranger);
        wxl::seam::Unit::OnAuraAdded::After(&stranger, 0u, 0, nullptr, 1);
        Check(g_received.empty(), "a unit the runtime never tracked raises nothing");

        wxl::seam::ObjectMgr::ObjDelete::Before(&wolf);
    }

    /// Player: UpdateInventoryComponent raises EquipmentChanged when the slot's item id moved,
    /// with the new id, and nothing for an enchant-only refresh.
    void CheckPlayer()
    {
        FakeObject me;
        me.guid            = 0x0000000000000042ull;
        me.typeId          = WXL_OBJECT_PLAYER;
        me.visibleItems[15] = 19019;
        wxl::seam::ObjectMgr::InitObject::After(&me, nullptr, 3000u);
        const wxl_handle handle = wxl::core::objects::HandleOf(&me);

        FakeVisibleItem old;
        old.itemId = 0;
        g_received.clear();
        wxl::seam::Player::UpdateInventoryComponent::Before(&me, 15, &old);
        wxl::seam::Player::UpdateInventoryComponent::After(&me, 15, &old);
        const Received* equipped = Only(WXL_EV_EquipmentChanged);
        Check(equipped != nullptr, "Player.UpdateInventoryComponent raises EquipmentChanged once when the slot filled");
        if (equipped)
        {
            const wxl_equipment_args args = equipped->As<wxl_equipment_args>();
            Check(args.unit == handle && args.slot == 15u && args.item_id == 19019u,
                  "with the player's handle, the slot and the new item id");
        }

        old.itemId = 19019;
        g_received.clear();
        wxl::seam::Player::UpdateInventoryComponent::Before(&me, 15, &old);
        wxl::seam::Player::UpdateInventoryComponent::After(&me, 15, &old);
        Check(g_received.empty(), "the same id again (an enchant refresh) raises nothing");

        me.visibleItems[15] = 0;
        g_received.clear();
        wxl::seam::Player::UpdateInventoryComponent::Before(&me, 15, &old);
        wxl::seam::Player::UpdateInventoryComponent::After(&me, 15, &old);
        Check(Only(WXL_EV_EquipmentChanged) != nullptr &&
                  Only(WXL_EV_EquipmentChanged)->As<wxl_equipment_args>().item_id == 0u,
              "clearing the slot raises EquipmentChanged with an item id of 0");

        wxl::seam::ObjectMgr::ObjDelete::Before(&me);
    }

    /// GameUI: the target is announced once per change, with the previous and current handles,
    /// through SendTarget after Target's internal clear, and through a standalone ClearTarget.
    void CheckGameUI()
    {
        FakeObject rat;
        rat.guid   = 0xF130000000000AAAull;
        rat.typeId = WXL_OBJECT_UNIT;
        FakeObject bat;
        bat.guid   = 0xF130000000000BBBull;
        bat.typeId = WXL_OBJECT_UNIT;
        wxl::seam::ObjectMgr::InitObject::After(&rat, nullptr, 4000u);
        wxl::seam::ObjectMgr::InitObject::After(&bat, nullptr, 4001u);
        const wxl_handle ratHandle = wxl::core::objects::HandleOf(&rat);
        const wxl_handle batHandle = wxl::core::objects::HandleOf(&bat);

        // CGGameUI::Target(rat): ClearTarget(old, 0), the static written, SendTarget(rat).
        g_received.clear();
        wxl::seam::GameUI::ClearTarget::Before(0, 0);
        wxl::seam::GameUI::ClearTarget::After(0, 0);
        g_targetGuid = rat.guid;
        wxl::seam::GameUI::SendTarget::Before(rat.guid);
        wxl::seam::GameUI::SendTarget::After(rat.guid);
        const Received* first = Only(WXL_EV_TargetChanged);
        Check(first != nullptr, "targeting from nothing raises TargetChanged once");
        if (first)
            Check(first->As<wxl_target_args>().previous == WXL_HANDLE_NONE &&
                      first->As<wxl_target_args>().current == ratHandle,
                  "from the null handle to the target's");

        // CGGameUI::Target(bat): the internal clear is not announced, the change is, once.
        g_received.clear();
        g_targetGuid = 0;
        wxl::seam::GameUI::ClearTarget::Before(rat.guid, 0);
        wxl::seam::GameUI::ClearTarget::After(rat.guid, 0);
        Check(g_received.empty(), "Target's own pre-clear (a2 clear) announces nothing");
        g_targetGuid = bat.guid;
        wxl::seam::GameUI::SendTarget::Before(bat.guid);
        wxl::seam::GameUI::SendTarget::After(bat.guid);
        const Received* second = Only(WXL_EV_TargetChanged);
        Check(second != nullptr, "retargeting raises TargetChanged once");
        if (second)
            Check(second->As<wxl_target_args>().previous == ratHandle &&
                      second->As<wxl_target_args>().current == batHandle,
                  "from the previous target's handle to the new one's");

        // The summon-friend script: SendTarget with the static unchanged announces nothing.
        g_received.clear();
        wxl::seam::GameUI::SendTarget::Before(rat.guid);
        wxl::seam::GameUI::SendTarget::After(rat.guid);
        Check(g_received.empty(), "SendTarget without a change of the static announces nothing");

        // A standalone ClearTarget (a2 set): announced, to the null handle.
        g_received.clear();
        g_targetGuid = 0;
        wxl::seam::GameUI::ClearTarget::Before(bat.guid, 1);
        wxl::seam::GameUI::ClearTarget::After(bat.guid, 1);
        const Received* cleared = Only(WXL_EV_TargetChanged);
        Check(cleared != nullptr, "a standalone ClearTarget raises TargetChanged once");
        if (cleared)
            Check(cleared->As<wxl_target_args>().previous == batHandle &&
                      cleared->As<wxl_target_args>().current == WXL_HANDLE_NONE,
                  "from the target's handle to the null handle");

        wxl::seam::ObjectMgr::ObjDelete::Before(&rat);
        wxl::seam::ObjectMgr::ObjDelete::Before(&bat);
    }

    /// MapArea: PrepareChunk's Before raises TerrainChunkBuild with the map, the tile and the chunk.
    void CheckMapArea()
    {
        FakeArea area;
        area.tileX = 32;
        area.tileY = 48;
        g_mapId    = 0;

        g_received.clear();
        wxl::seam::MapArea::PrepareChunk::Before(&area, 3, 9);
        wxl::seam::MapArea::PrepareChunk::After(nullptr, &area, 3, 9);
        const Received* built = Only(WXL_EV_TerrainChunkBuild);
        Check(built != nullptr, "MapArea.PrepareChunk raises TerrainChunkBuild once");
        if (!built) return;
        const wxl_terrain_chunk_args args = built->As<wxl_terrain_chunk_args>();
        Check(args.map_id == 0u && args.tile_x == 32 && args.tile_y == 48,
              "with the map and the tile index read through the binding");
        Check(args.chunk_x == 3 && args.chunk_y == 9, "and the chunk the call named");
    }

#if defined(_M_IX86)
    // --- The generated detour, on the architecture it is generated for --------------------------

    /// A client-shaped class: a non-static member, `this` in ECX, two stack arguments, a result.
    struct Thing
    {
        int32_t base = 100;
        int32_t __thiscall Add(int32_t a, int32_t b) { return base + a + b; }
    };

    int32_t g_shimBefore = 0;
    int32_t g_shimAfter  = 0;
    void*   g_shimSelf   = nullptr;

    void WXL_CALL ShimBefore(void* self, int32_t a, int32_t b)
    {
        g_shimSelf   = self;
        g_shimBefore = a * 10 + b;
    }

    void WXL_CALL ShimAfter(int32_t result, void* self, int32_t a, int32_t b)
    {
        g_shimAfter = self == g_shimSelf ? result + a + b : -1;
    }

    using ShimSeam = wxl::core::seams::DetourOf<wxl::seam::detail::conv_thiscall,
                                                int32_t(void*, int32_t, int32_t)>;
    using ShimLink = ShimSeam::Link<&ShimBefore, &ShimAfter>;

    /// Calls the generated __fastcall detour the way the hook engine's caller would: as the
    /// __thiscall it stands in for, so a wrong shim corrupts the stack here rather than in a
    /// running client.
    void CheckThiscallShim()
    {
        Thing thing;
        auto  member        = &Thing::Add;
        ShimLink::original  = reinterpret_cast<ShimSeam::Fn>(*reinterpret_cast<void**>(&member));

        using AsThiscall = int32_t(__thiscall*)(Thing*, int32_t, int32_t);
        const AsThiscall detour = reinterpret_cast<AsThiscall>(&ShimLink::Detour);
        const int32_t result = detour(&thing, 5, 7);

        Check(result == 112, "the thiscall shim returns the original's result (100 + 5 + 7)");
        Check(g_shimSelf == &thing && g_shimBefore == 57, "Before received `this` and the arguments");
        Check(g_shimAfter == 112 + 5 + 7, "After received the result first, then `this` and the arguments");
    }
#endif
}

int main()
{
    wxl::core::binding::Bind(FakeInit());

    wxl::host::HostConfig config;
    config.runtime_id   = "harness";
    config.runtime_name = "seam harness";
    config.events       = wxl::core::seams::RaisedEvents();
    g_host              = std::make_unique<wxl::host::ModHost>(std::move(config));

    printf("seam harness: %zu seam(s), %zu raisable event(s), %zu binding row(s) left unbound by the fake\n",
           wxl::core::seams::Table().size(), wxl::core::seams::RaisedEvents().size(),
           wxl::core::binding::Unbound().size());

    CheckEveryRaisedEventIsSubscribable();
    CheckEveryRowDeclaresItsAbi();
    CheckEventDispatchUpdate();
    CheckWorldPreload();
    CheckObjectMgr();
    CheckUnit();
    CheckPlayer();
    CheckGameUI();
    CheckMapArea();
#if defined(_M_IX86)
    CheckThiscallShim();
#endif

    printf("\n%d failure(s)\n", g_failures);
    g_host.reset();
    return g_failures ? 1 : 0;
}
