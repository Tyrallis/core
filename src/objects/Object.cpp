// The reads every object answers, over the client binding.
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

#include <cstring>

namespace wxl::core::objects
{
    wxl_guid ClientObjects::Guid(void* object) const { return binding::object_guid(object); }

    wxl_object_type ClientObjects::Type(void* object) const
    {
        const uint32_t id = binding::object_type_id(object);
        return id <= WXL_OBJECT_CORPSE ? static_cast<wxl_object_type>(id) : WXL_OBJECT_UNKNOWN;
    }

    bool ClientObjects::Entry(void* object, uint32_t& out) const
    {
        out = binding::object_entry(object);
        return true;
    }

    bool ClientObjects::Scale(void* object, float& out) const
    {
        out = binding::object_scale(object);
        return true;
    }

    bool ClientObjects::Position(void* object, wxl_vec3& out) const
    {
        return binding::object_position(object, &out) == WXL_TRUE;
    }

    bool ClientObjects::Facing(void* object, float& out) const
    {
        return binding::object_facing(object, &out) == WXL_TRUE;
    }

    bool ClientObjects::Name(void* object, const char*& out, uint32_t& size) const
    {
        const char* name = binding::object_name(object);
        if (!name) return false;
        out  = name;
        size = static_cast<uint32_t>(std::strlen(name));
        return true;
    }

    void* ClientObjects::LocalPlayer() const
    {
        return const_cast<void*>(binding::local_player_object());
    }
}
