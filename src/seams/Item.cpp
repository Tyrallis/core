// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The Item seam: what the runtime does as the local player asks to use an item, on both builds.

#include "wxl/seams/seams.h"

#include "binding/Binding.hpp"
#include "objects/Objects.hpp"
#include "runtime/Core.hpp"
#include "seams/Gameplay.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <cstdint>
#include <cstring>

namespace wxl::seam::Item::UseItem
{
    bool WXL_CALL Gate(int32_t* result, void** self, const uint64_t** target, int32_t* confirmed)
    {
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return false;

        wxl_bool          refused = WXL_FALSE;
        wxl_item_use_args args{};
        args.item        = wxl::core::objects::HandleOf(*self);
        args.item_guid   = wxl::core::binding::object_guid(*self);
        args.target_guid = *target ? **target : 0;
        args.confirmed   = *confirmed ? WXL_TRUE : WXL_FALSE;
        args.refused     = &refused;

        host->Raise(WXL_EV_ItemUseRequest, &args);
        if (!refused) return false;

        // What the client answers for an item it cannot use: 0, and nothing cast.
        *result = 0;
        return true;
    }
}

namespace
{
    namespace gameplay = wxl::core::seams::gameplay;

    constexpr uint32_t kDurability    = 0x3C; ///< ITEM_FIELD_DURABILITY
    constexpr uint32_t kMaxDurability = 0x3D; ///< ITEM_FIELD_MAXDURABILITY
    constexpr uint32_t kStackCount    = 0x0E; ///< ITEM_FIELD_STACK_COUNT
}

namespace wxl::seam::Item::OnUpdateDurability
{
    void WXL_CALL Before(uint64_t guid, uint32_t, uint32_t, const void* old_value, void*)
    {
        wxl::host::ModHost* host = gameplay::Listener({ WXL_EV_ItemDurabilityChanged });
        if (!host || !old_value) return;
        void* item = gameplay::ObjectOf(guid);
        wxl_item_durability_args args{};
        if (!item || !gameplay::Field(item, kDurability, args.current)) return;
        std::memcpy(&args.previous, old_value, sizeof args.previous);
        if (args.previous == args.current) return;
        gameplay::Field(item, kMaxDurability, args.maximum);
        args.item      = wxl::core::objects::HandleOf(item);
        args.item_guid = guid;
        host->Raise(WXL_EV_ItemDurabilityChanged, &args);
    }
}

namespace wxl::seam::Item::OnUpdateStackCount
{
    void WXL_CALL Before(uint64_t guid, uint32_t, uint32_t, const void* old_value, void*)
    {
        wxl::host::ModHost* host = gameplay::Listener({ WXL_EV_ItemStackChanged });
        if (!host || !old_value) return;
        void* item = gameplay::ObjectOf(guid);
        wxl_item_stack_args args{};
        if (!item || !gameplay::Field(item, kStackCount, args.current)) return;
        std::memcpy(&args.previous, old_value, sizeof args.previous);
        if (args.previous == args.current) return;
        args.item      = wxl::core::objects::HandleOf(item);
        args.item_guid = guid;
        host->Raise(WXL_EV_ItemStackChanged, &args);
    }
}
