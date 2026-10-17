// The reads a corpse answers, over the client binding.
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
    bool ClientObjects::CorpseOwner(void* object, wxl_guid& out) const
    {
        out = binding::corpse_owner(object);
        return true;
    }

    bool ClientObjects::CorpseParty(void* object, wxl_guid& out) const
    {
        out = binding::corpse_party(object);
        return true;
    }

    bool ClientObjects::CorpseDisplayId(void* object, uint32_t& out) const
    {
        out = binding::corpse_display_id(object);
        return true;
    }

    bool ClientObjects::CorpseGuild(void* object, uint32_t& out) const
    {
        out = binding::corpse_guild(object);
        return true;
    }

    bool ClientObjects::CorpseFlags(void* object, uint32_t& out) const
    {
        out = binding::corpse_flags(object);
        return true;
    }

    bool ClientObjects::CorpseDynamicFlags(void* object, uint32_t& out) const
    {
        out = binding::corpse_dynamic_flags(object);
        return true;
    }
}
