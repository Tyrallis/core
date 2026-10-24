// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The Item seam: what the runtime does as the local player asks to use an item, on both builds.

#include "wxl/seams/seams.h"

#include "binding/Binding.hpp"
#include "objects/Objects.hpp"
#include "runtime/Core.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <cstdint>

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
