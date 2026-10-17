// The reads an item answers, over the client binding.
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
    bool ClientObjects::ItemOwner(void* object, wxl_guid& out) const
    {
        out = binding::item_owner(object);
        return true;
    }

    bool ClientObjects::ItemContainedIn(void* object, wxl_guid& out) const
    {
        out = binding::item_contained_in(object);
        return true;
    }

    bool ClientObjects::ItemCreator(void* object, wxl_guid& out) const
    {
        out = binding::item_creator(object);
        return true;
    }

    bool ClientObjects::ItemStackCount(void* object, uint32_t& out) const
    {
        out = binding::item_stack_count(object);
        return true;
    }

    bool ClientObjects::ItemDuration(void* object, uint32_t& out) const
    {
        out = binding::item_duration(object);
        return true;
    }

    bool ClientObjects::ItemFlags(void* object, uint32_t& out) const
    {
        out = binding::item_flags(object);
        return true;
    }

    bool ClientObjects::ItemDurability(void* object, uint32_t& out) const
    {
        out = binding::item_durability(object);
        return true;
    }

    bool ClientObjects::ItemMaxDurability(void* object, uint32_t& out) const
    {
        out = binding::item_max_durability(object);
        return true;
    }
}
