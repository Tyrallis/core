// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The Loot seams: the loot window opened, emptied and closed, on both builds.

#include "wxl/seams/seams.h"

#include "binding/Binding.hpp"
#include "objects/Objects.hpp"
#include "runtime/Core.hpp"
#include "seams/Gameplay.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <cstdint>

namespace
{
    namespace binding  = wxl::core::binding;
    namespace gameplay = wxl::core::seams::gameplay;

    constexpr uint32_t kEntries = 18; ///< CGLootInfo::m_slots

    /// The window SetObject found open: its object and kind, for the LootClosed After raises.
    struct Window
    {
        uint64_t guid = 0;
        uint32_t kind = 0;
    };

    Window g_before;

    /// Entry `index` of the window as an event's members; false for an empty one.
    bool Entry(uint32_t index, wxl_loot_slot_args& out)
    {
        int32_t values[7]{};
        if (binding::loot_slot(index, values) != WXL_TRUE) return false;
        out.item_id            = static_cast<uint32_t>(values[0]);
        out.quantity           = static_cast<uint32_t>(values[1]);
        out.display_id         = static_cast<uint32_t>(values[2]);
        out.random_suffix      = static_cast<uint32_t>(values[3]);
        out.random_property_id = values[4];
        out.slot               = static_cast<uint32_t>(values[5]);
        out.slot_type          = static_cast<uint32_t>(values[6]);
        return true;
    }
}

namespace wxl::seam::Loot::SetObject
{
    void WXL_CALL Before(void* object, int32_t coins, uint32_t loot_acquire)
    {
        (void)object;
        (void)coins;
        (void)loot_acquire;
        g_before.guid = binding::loot_object_guid();
        g_before.kind = binding::loot_kind();
    }

    void WXL_CALL After(void* object, int32_t coins, uint32_t loot_acquire)
    {
        (void)coins;
        (void)loot_acquire;
        const Window before = g_before;
        g_before            = Window{};
        wxl::host::ModHost* host =
            gameplay::Listener({ WXL_EV_LootOpened, WXL_EV_LootSlot, WXL_EV_LootClosed });
        if (!host) return;

        // The window open before is closed whatever comes next, as the client closes it.
        const uint64_t now = binding::loot_object_guid();
        if (before.guid && before.guid != now)
        {
            wxl_loot_args closed{};
            closed.source_guid = before.guid;
            closed.source      = wxl::core::objects::FromGuid(before.guid);
            closed.kind        = before.kind;
            host->Raise(WXL_EV_LootClosed, &closed);
        }
        if (!object || !now) return;

        wxl_loot_slot_args entries[kEntries]{};
        uint32_t           count = 0;
        for (uint32_t i = 0; i < kEntries; ++i)
            if (Entry(i, entries[count])) entries[count].index = count, ++count;

        wxl_loot_args opened{};
        opened.source      = wxl::core::objects::HandleOf(object);
        opened.source_guid = now;
        opened.kind        = binding::loot_kind();
        opened.money       = binding::loot_money();
        opened.count       = count;
        host->Raise(WXL_EV_LootOpened, &opened);
        for (uint32_t i = 0; i < count; ++i) host->Raise(WXL_EV_LootSlot, &entries[i]);
    }
}

namespace wxl::seam::Loot::ClearSlot
{
    void WXL_CALL Before(uint8_t slot)
    {
        wxl::host::ModHost* host = gameplay::Listener({ WXL_EV_LootSlotCleared });
        if (!host) return;
        // The entry ClearSlot empties: the first with an item for that server slot.
        for (uint32_t i = 0; i < kEntries; ++i)
        {
            wxl_loot_slot_args entry{};
            if (!Entry(i, entry) || entry.slot != slot) continue;
            entry.index = i;
            host->Raise(WXL_EV_LootSlotCleared, &entry);
            return;
        }
    }
}
