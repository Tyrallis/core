// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The Trade seam: the steps of a trade, on both builds.

#include "wxl/seams/seams.h"

#include "binding/Binding.hpp"
#include "objects/Objects.hpp"
#include "runtime/Core.hpp"
#include "seams/Gameplay.hpp"
#include "seams/Message.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <cstdint>

namespace wxl::seam::Trade::Packet_SMSG_TRADE_STATUS
{
    void WXL_CALL Before(void*, uint32_t, uint32_t, void* msg)
    {
        constexpr uint32_t kProposed = 1; ///< TRADE_STATUS_PROPOSED: the proposer's guid follows
        wxl::host::ModHost* host = wxl::core::seams::gameplay::Listener({ WXL_EV_TradeStatus });
        if (!host) return;
        wxl::core::seams::MessageReader in(msg);
        wxl_trade_args args{};
        uint32_t status = 0;
        if (!in.U32(status)) return;
        if (status == kProposed && !in.U64(args.partner_guid)) return;
        args.status = status;
        host->Raise(WXL_EV_TradeStatus, &args);
    }
}
