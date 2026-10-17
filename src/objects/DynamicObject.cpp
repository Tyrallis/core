// The reads a dynamic object answers, over the client binding.
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
    bool ClientObjects::DynamicCaster(void* object, wxl_guid& out) const
    {
        out = binding::dynamic_caster(object);
        return true;
    }

    bool ClientObjects::DynamicSpellId(void* object, uint32_t& out) const
    {
        out = binding::dynamic_spell_id(object);
        return true;
    }

    bool ClientObjects::DynamicRadius(void* object, float& out) const
    {
        out = binding::dynamic_radius(object);
        return true;
    }

    bool ClientObjects::DynamicCastTime(void* object, uint32_t& out) const
    {
        out = binding::dynamic_cast_time(object);
        return true;
    }
}
