// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The Reputation seam: the local player's standings with the factions, on both builds.

#include "wxl/seams/seams.h"

#include "binding/Binding.hpp"
#include "objects/Objects.hpp"
#include "runtime/Core.hpp"
#include "seams/Gameplay.hpp"
#include "seams/Message.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <cstdint>

namespace wxl::seam::Reputation::Packet_SMSG_SET_FACTION_STANDING
{
    void WXL_CALL Before(void*, uint32_t, uint32_t, void* msg)
    {
        wxl::host::ModHost* host = wxl::core::seams::gameplay::Listener({ WXL_EV_ReputationChanged });
        if (!host) return;
        wxl::core::seams::MessageReader in(msg);
        float    bonus = 0.0f;
        uint8_t  effect = 0;
        uint32_t count = 0;
        if (!in.F32(bonus) || !in.U8(effect) || !in.U32(count)) return;
        for (uint32_t i = 0; i < count; ++i)
        {
            uint32_t listId = 0, standing = 0;
            if (!in.U32(listId) || !in.U32(standing)) return;
            // What the handler is about to overwrite: the faction's entry before this message.
            int32_t faction = 0, base = 0, before = 0;
            if (wxl::core::binding::faction_standing(listId, &faction, &base, &before) != WXL_TRUE) continue;
            if (static_cast<int32_t>(standing) == before) continue;
            wxl_reputation_args args{};
            args.faction_id = static_cast<uint32_t>(faction);
            args.previous   = base + before;
            args.current    = base + static_cast<int32_t>(standing);
            args.bonus      = bonus;
            host->Raise(WXL_EV_ReputationChanged, &args);
        }
    }
}
