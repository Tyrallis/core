// The reads a unit answers, over the client binding.
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

#include "objects/Objects.hpp"

#include "binding/Binding.hpp"

namespace
{
    /// Byte n of UNIT_FIELD_BYTES_0, from the low end: race, class, sex, power type.
    uint32_t Bytes0(void* object, unsigned byte)
    {
        return (wxl::core::binding::unit_bytes0(object) >> (8u * byte)) & 0xFFu;
    }
}

namespace wxl::core::objects
{
    bool ClientObjects::UnitHealth(void* object, uint32_t& out) const
    {
        out = binding::unit_health(object);
        return true;
    }

    bool ClientObjects::UnitMaxHealth(void* object, uint32_t& out) const
    {
        out = binding::unit_max_health(object);
        return true;
    }

    bool ClientObjects::UnitLevel(void* object, uint32_t& out) const
    {
        out = binding::unit_level(object);
        return true;
    }

    bool ClientObjects::UnitTarget(void* object, wxl_guid& out) const
    {
        out = binding::unit_target(object);
        return true;
    }

    bool ClientObjects::UnitPower(void* object, uint32_t index, uint32_t& out) const
    {
        return binding::unit_power(object, index, &out) == WXL_TRUE;
    }

    bool ClientObjects::UnitMaxPower(void* object, uint32_t index, uint32_t& out) const
    {
        return binding::unit_max_power(object, index, &out) == WXL_TRUE;
    }

    bool ClientObjects::UnitPowerType(void* object, uint32_t& out) const
    {
        out = Bytes0(object, 3);
        return true;
    }

    bool ClientObjects::UnitRace(void* object, uint32_t& out) const
    {
        out = Bytes0(object, 0);
        return true;
    }

    bool ClientObjects::UnitClass(void* object, uint32_t& out) const
    {
        out = Bytes0(object, 1);
        return true;
    }

    bool ClientObjects::UnitSex(void* object, uint32_t& out) const
    {
        out = Bytes0(object, 2);
        return true;
    }

    bool ClientObjects::UnitDisplayId(void* object, uint32_t& out) const
    {
        out = binding::unit_display_id(object);
        return true;
    }

    bool ClientObjects::UnitNativeDisplayId(void* object, uint32_t& out) const
    {
        out = binding::unit_native_display_id(object);
        return true;
    }

    bool ClientObjects::UnitMountDisplayId(void* object, uint32_t& out) const
    {
        out = binding::unit_mount_display_id(object);
        return true;
    }

    bool ClientObjects::UnitFactionTemplate(void* object, uint32_t& out) const
    {
        out = binding::unit_faction_template(object);
        return true;
    }

    bool ClientObjects::UnitFlags(void* object, uint32_t& out) const
    {
        out = binding::unit_flags(object);
        return true;
    }

    bool ClientObjects::UnitFlags2(void* object, uint32_t& out) const
    {
        out = binding::unit_flags2(object);
        return true;
    }

    bool ClientObjects::UnitDynamicFlags(void* object, uint32_t& out) const
    {
        out = binding::unit_dynamic_flags(object);
        return true;
    }

    bool ClientObjects::UnitNpcFlags(void* object, uint32_t& out) const
    {
        out = binding::unit_npc_flags(object);
        return true;
    }

    bool ClientObjects::UnitBoundingRadius(void* object, float& out) const
    {
        out = binding::unit_bounding_radius(object);
        return true;
    }

    bool ClientObjects::UnitCombatReach(void* object, float& out) const
    {
        out = binding::unit_combat_reach(object);
        return true;
    }

    bool ClientObjects::UnitCharm(void* object, wxl_guid& out) const
    {
        out = binding::unit_charm(object);
        return true;
    }

    bool ClientObjects::UnitSummon(void* object, wxl_guid& out) const
    {
        out = binding::unit_summon(object);
        return true;
    }

    bool ClientObjects::UnitCharmedBy(void* object, wxl_guid& out) const
    {
        out = binding::unit_charmed_by(object);
        return true;
    }

    bool ClientObjects::UnitSummonedBy(void* object, wxl_guid& out) const
    {
        out = binding::unit_summoned_by(object);
        return true;
    }

    bool ClientObjects::UnitCreatedBy(void* object, wxl_guid& out) const
    {
        out = binding::unit_created_by(object);
        return true;
    }
}
