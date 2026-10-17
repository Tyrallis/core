// The reads a player answers, over the client binding.
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

#include <cstdlib>

namespace wxl::core::objects
{
    bool ClientObjects::PlayerFlags(void* object, uint32_t& out) const
    {
        out = binding::player_flags(object);
        return true;
    }

    bool ClientObjects::PlayerGuildId(void* object, uint32_t& out) const
    {
        out = binding::player_guild_id(object);
        return true;
    }

    bool ClientObjects::PlayerGuildRank(void* object, uint32_t& out) const
    {
        out = binding::player_guild_rank(object);
        return true;
    }

    bool ClientObjects::PlayerChosenTitle(void* object, uint32_t& out) const
    {
        out = binding::player_chosen_title(object);
        return true;
    }

    bool ClientObjects::PlayerVisibleItem(void* object, uint32_t slot, uint32_t& out) const
    {
        int32_t id = 0;
        if (binding::player_visible_item_id(object, slot, &id) != WXL_TRUE) return false;
        out = static_cast<uint32_t>(std::abs(id));
        return true;
    }
}
