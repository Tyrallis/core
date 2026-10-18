// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// The Player seams: what the runtime does when a player's equipment changes, on both builds.

#include "wxl/seams/seams.h"

#include "binding/Binding.hpp"
#include "objects/Objects.hpp"
#include "runtime/Core.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <cstdint>
#include <cstdlib>

namespace
{
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
