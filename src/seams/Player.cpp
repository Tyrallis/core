// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The Player seams: what the runtime does when a player's equipment or money changes and when
// the local player levels, on both builds.

#include "wxl/seams/seams.h"

#include "binding/Binding.hpp"
#include "objects/Objects.hpp"
#include "runtime/Core.hpp"
#include "seams/Message.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace
{
    constexpr uint32_t kLevelUpInfo = 0x1D4; ///< SMSG_LEVEL_UP_INFO

    /// What Before decided for the call After closes: the function is not reentrant.
    struct Pending
    {
        void*    self    = nullptr;
        bool     changed = false;
        uint32_t itemId  = 0;
    };

    Pending g_pending;
}

namespace wxl::seam::Player::UpdateInventoryComponent
{
    void WXL_CALL Before(void* self, int32_t slot, const void* old_item)
    {
        g_pending = Pending{};
        if (!self || !old_item || slot < 0) return;

        int32_t current = 0;
        if (wxl::core::binding::player_visible_item_id(self, static_cast<uint32_t>(slot), &current) != WXL_TRUE)
            return;
        const int32_t previous = wxl::core::binding::visible_item_id(old_item);

        // The client's own test: the same id with the same sign is an enchant refresh, not a change.
        g_pending.self    = self;
        g_pending.changed = std::abs(previous) != std::abs(current) || (previous > 0) != (current > 0);
        g_pending.itemId  = static_cast<uint32_t>(std::abs(current));
    }

    void WXL_CALL After(void* self, int32_t slot, const void*)
    {
        const Pending pending = g_pending;
        g_pending             = Pending{};
        if (!pending.changed || pending.self != self) return;

        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return;

        wxl_equipment_args args{};
        args.unit = wxl::core::objects::HandleOf(self);
        if (args.unit == WXL_HANDLE_NONE) return;
        args.slot    = static_cast<uint32_t>(slot);
        args.item_id = pending.itemId;
        host->Raise(WXL_EV_EquipmentChanged, &args);
    }
}

namespace wxl::seam::Player::OnUpdateMoney
{
    void WXL_CALL Before(uint64_t guid, uint32_t, uint32_t, const void* value, void*)
    {
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host || !value) return;

        // The handler is registered on the local player's object alone, so the field read is
        // the local block's; the previous value is the one the mirror hands over by address.
        wxl_money_args args{};
        args.player = wxl::core::objects::FromGuid(guid);
        std::memcpy(&args.previous, value, sizeof args.previous);
        if (args.player != WXL_HANDLE_NONE)
            if (void* object = wxl::core::objects::Service().Handles().Resolve(args.player))
                args.current = wxl::core::binding::player_money(object);
        host->Raise(WXL_EV_PlayerMoneyChanged, &args);
    }
}

namespace wxl::seam::Player::Packet_Group_9
{
    void WXL_CALL Before(void* param, uint32_t msg_id, uint32_t time, void* msg)
    {
        (void)param;
        (void)time;
        if (msg_id != kLevelUpInfo) return;

        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return;

        wxl::core::seams::MessageReader in(msg);
        wxl_level_up_args              args{};
        uint32_t                       power[7];
        uint32_t                       stats[5];
        if (!in.U32(args.level) || !in.U32(args.health)) return;
        for (uint32_t& amount : power)
            if (!in.U32(amount)) return;
        for (uint32_t& amount : stats)
            if (!in.U32(amount)) return;

        args.mana        = power[0];
        args.rage        = power[1];
        args.focus       = power[2];
        args.energy      = power[3];
        args.happiness   = power[4];
        args.runes       = power[5];
        args.runic_power = power[6];
        args.strength    = stats[0];
        args.agility     = stats[1];
        args.stamina     = stats[2];
        args.intellect   = stats[3];
        args.spirit      = stats[4];
        host->Raise(WXL_EV_PlayerLevelUp, &args);
    }
}
