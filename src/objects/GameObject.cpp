// The reads a game object answers, over the client binding.
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

namespace wxl::core::objects
{
    bool ClientObjects::GameObjectCreatedBy(void* object, wxl_guid& out) const
    {
        out = binding::gameobject_created_by(object);
        return true;
    }

    bool ClientObjects::GameObjectDisplayId(void* object, uint32_t& out) const
    {
        out = binding::gameobject_display_id(object);
        return true;
    }

    bool ClientObjects::GameObjectFlags(void* object, uint32_t& out) const
    {
        out = binding::gameobject_flags(object);
        return true;
    }

    bool ClientObjects::GameObjectDynamicFlags(void* object, uint32_t& out) const
    {
        out = binding::gameobject_dynamic_flags(object);
        return true;
    }

    bool ClientObjects::GameObjectFaction(void* object, uint32_t& out) const
    {
        out = binding::gameobject_faction(object);
        return true;
    }

    bool ClientObjects::GameObjectLevel(void* object, uint32_t& out) const
    {
        out = binding::gameobject_level(object);
        return true;
    }

    bool ClientObjects::GameObjectState(void* object, uint32_t& out) const
    {
        out = static_cast<uint8_t>(binding::gameobject_state(object));
        return true;
    }

    bool ClientObjects::GameObjectType(void* object, uint32_t& out) const
    {
        out = static_cast<uint8_t>(binding::gameobject_type_id(object));
        return true;
    }

    bool ClientObjects::GameObjectArtKit(void* object, uint32_t& out) const
    {
        out = static_cast<uint8_t>(binding::gameobject_art_kit(object));
        return true;
    }
}
