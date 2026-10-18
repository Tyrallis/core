// wxl.object on this runtime: the service instance, and the client binding behind it.
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

#pragma once

#include "wxl/runtime/object_service.hpp"

#include <cstdint>

namespace wxl::core::objects
{
    /**
     * @brief The leaf reads of wxl.object, over wxl::core::binding.
     *
     * One class, defined across this folder one file per kind of object (Object.cpp, Unit.cpp,
     * Player.cpp, Item.cpp, Container.cpp, GameObject.cpp, DynamicObject.cpp, Corpse.cpp), so a
     * kind's reads sit together and nothing else is in the file. The same code on both builds:
     * what differs is how the binding under it reads the client.
     */
    class ClientObjects final : public wxl::runtime::ObjectBinding
    {
    public:
        // Object.cpp
        wxl_guid        Guid(void* object) const override;
        wxl_object_type Type(void* object) const override;
        bool            Entry(void* object, uint32_t& out) const override;
        bool            Scale(void* object, float& out) const override;
        bool            Position(void* object, wxl_vec3& out) const override;
        bool            Facing(void* object, float& out) const override;
        bool            Name(void* object, const char*& out, uint32_t& size) const override;
        void*           LocalPlayer() const override;

        // Unit.cpp
        bool UnitHealth(void* object, uint32_t& out) const override;
        bool UnitMaxHealth(void* object, uint32_t& out) const override;
        bool UnitLevel(void* object, uint32_t& out) const override;
        bool UnitTarget(void* object, wxl_guid& out) const override;
        bool UnitPower(void* object, uint32_t index, uint32_t& out) const override;
        bool UnitMaxPower(void* object, uint32_t index, uint32_t& out) const override;
        bool UnitPowerType(void* object, uint32_t& out) const override;
        bool UnitRace(void* object, uint32_t& out) const override;
        bool UnitClass(void* object, uint32_t& out) const override;
        bool UnitSex(void* object, uint32_t& out) const override;
        bool UnitDisplayId(void* object, uint32_t& out) const override;
        bool UnitNativeDisplayId(void* object, uint32_t& out) const override;
        bool UnitMountDisplayId(void* object, uint32_t& out) const override;
        bool UnitFactionTemplate(void* object, uint32_t& out) const override;
        bool UnitFlags(void* object, uint32_t& out) const override;
        bool UnitFlags2(void* object, uint32_t& out) const override;
        bool UnitDynamicFlags(void* object, uint32_t& out) const override;
        bool UnitNpcFlags(void* object, uint32_t& out) const override;
        bool UnitBoundingRadius(void* object, float& out) const override;
        bool UnitCombatReach(void* object, float& out) const override;
        bool UnitCharm(void* object, wxl_guid& out) const override;
        bool UnitSummon(void* object, wxl_guid& out) const override;
        bool UnitCharmedBy(void* object, wxl_guid& out) const override;
        bool UnitSummonedBy(void* object, wxl_guid& out) const override;
        bool UnitCreatedBy(void* object, wxl_guid& out) const override;

        // Player.cpp
        bool PlayerFlags(void* object, uint32_t& out) const override;
        bool PlayerGuildId(void* object, uint32_t& out) const override;
        bool PlayerGuildRank(void* object, uint32_t& out) const override;
        bool PlayerChosenTitle(void* object, uint32_t& out) const override;
        bool PlayerVisibleItem(void* object, uint32_t slot, uint32_t& out) const override;

        // Item.cpp
        bool ItemOwner(void* object, wxl_guid& out) const override;
        bool ItemContainedIn(void* object, wxl_guid& out) const override;
        bool ItemCreator(void* object, wxl_guid& out) const override;
        bool ItemStackCount(void* object, uint32_t& out) const override;
        bool ItemDuration(void* object, uint32_t& out) const override;
        bool ItemFlags(void* object, uint32_t& out) const override;
        bool ItemDurability(void* object, uint32_t& out) const override;
        bool ItemMaxDurability(void* object, uint32_t& out) const override;

        // Container.cpp
        bool ContainerNumSlots(void* object, uint32_t& out) const override;
        bool ContainerSlot(void* object, uint32_t index, wxl_guid& out) const override;

        // GameObject.cpp
        bool GameObjectCreatedBy(void* object, wxl_guid& out) const override;
        bool GameObjectDisplayId(void* object, uint32_t& out) const override;
        bool GameObjectFlags(void* object, uint32_t& out) const override;
        bool GameObjectDynamicFlags(void* object, uint32_t& out) const override;
        bool GameObjectFaction(void* object, uint32_t& out) const override;
        bool GameObjectLevel(void* object, uint32_t& out) const override;
        bool GameObjectState(void* object, uint32_t& out) const override;
        bool GameObjectType(void* object, uint32_t& out) const override;
        bool GameObjectArtKit(void* object, uint32_t& out) const override;

        // DynamicObject.cpp
        bool DynamicCaster(void* object, wxl_guid& out) const override;
        bool DynamicSpellId(void* object, uint32_t& out) const override;
        bool DynamicRadius(void* object, float& out) const override;
        bool DynamicCastTime(void* object, uint32_t& out) const override;

        // Corpse.cpp
        bool CorpseOwner(void* object, wxl_guid& out) const override;
        bool CorpseParty(void* object, wxl_guid& out) const override;
        bool CorpseDisplayId(void* object, uint32_t& out) const override;
        bool CorpseGuild(void* object, uint32_t& out) const override;
        bool CorpseFlags(void* object, uint32_t& out) const override;
        bool CorpseDynamicFlags(void* object, uint32_t& out) const override;
    };

    /// The one object service of this process, built on first use over ClientObjects.
    wxl::runtime::ObjectService& Service();

    /// The table published under WXL_OBJECT_SERVICE.
    const wxl_object_api* Api();

    // What the seams drive: the ObjectMgr seams track an object when the client builds it and
    // untrack it when the client lets it go, so a handle kept past that point reads as absent.
    wxl_handle Track(void* object);
    void       Untrack(void* object);
    void       Untrack(wxl_handle handle);
    bool       IsTracked(void* object);
    wxl_handle HandleOf(void* object);
    wxl_handle FromGuid(uint64_t guid);
}
