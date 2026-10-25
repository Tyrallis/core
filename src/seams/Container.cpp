// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The Container seams: the local player's inventory slots and its bags' slots, on both builds.

#include "wxl/seams/seams.h"

#include "binding/Binding.hpp"
#include "objects/Objects.hpp"
#include "runtime/Core.hpp"
#include "seams/Gameplay.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <cstdint>
#include <cstring>

namespace
{
    namespace gameplay = wxl::core::seams::gameplay;

    constexpr uint32_t kUnitEnd       = 0x94; ///< where the player's block starts in the descriptor
    constexpr uint32_t kPlayerInvSlot = 0xB0; ///< PLAYER_FIELD_INV_SLOT_HEAD, in dwords from the player's block
    constexpr uint32_t kBagSlot       = 0x42; ///< CONTAINER_FIELD_SLOT_1, in dwords from the descriptor's start

    /**
     * @brief Raises InventorySlotChanged for one guid field a mirror handler was told changed.
     *
     * @p first is where the container's slots start, in dwords of its descriptor block, and
     * @p at the dword the handler names in the same numbering. The item there now is the
     * descriptor's; the one before is the value the handler was handed by address.
     */
    void SlotChanged(uint64_t guid, uint32_t at, uint32_t first, const void* old_value)
    {
        wxl::host::ModHost* host = gameplay::Listener({ WXL_EV_InventorySlotChanged });
        if (!host || !old_value || at < first || (at - first) % 2u) return;
        void* container = gameplay::ObjectOf(guid);
        if (!container) return;
        wxl_inventory_slot_args args{};
        if (!gameplay::Field64(container, at, args.item_guid)) return;
        std::memcpy(&args.previous_guid, old_value, sizeof args.previous_guid);
        if (args.previous_guid == args.item_guid) return;
        args.container      = wxl::core::objects::HandleOf(container);
        args.container_guid = guid;
        args.slot           = (at - first) / 2u;
        args.item           = args.item_guid ? wxl::core::objects::FromGuid(args.item_guid) : WXL_HANDLE_NONE;
        host->Raise(WXL_EV_InventorySlotChanged, &args);
    }
}

namespace wxl::seam::Container::InvUpdateHandler
{
    void WXL_CALL Before(uint64_t guid, uint32_t field_offset, uint32_t, const void* old_value, void*)
    {
        // The player's handlers name the field from the start of its player block.
        SlotChanged(guid, kUnitEnd + field_offset / 4u, kUnitEnd + kPlayerInvSlot, old_value);
    }
}

namespace wxl::seam::Container::UpdateInvContents
{
    void WXL_CALL Before(uint64_t guid, uint32_t field_offset, uint32_t, const void* old_value, void*)
    {
        SlotChanged(guid, kUnitEnd + field_offset / 4u, kUnitEnd + kPlayerInvSlot, old_value);
    }
}

namespace wxl::seam::Container::UpdateContainerContents
{
    void WXL_CALL Before(uint64_t guid, uint32_t field_offset, uint32_t, const void* old_value, void*)
    {
        // A container's handlers name the field from the start of its container block, which
        // follows the item's: CONTAINER_FIELD_NUM_SLOTS is ITEM_END.
        SlotChanged(guid, kBagSlot - 2u + field_offset / 4u, kBagSlot, old_value);
    }
}
