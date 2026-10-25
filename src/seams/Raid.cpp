// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The Raid seams: ready checks, on both builds.

#include "wxl/seams/seams.h"

#include "binding/Binding.hpp"
#include "objects/Objects.hpp"
#include "runtime/Core.hpp"
#include "seams/Gameplay.hpp"
#include "seams/Message.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <cstdint>

namespace
{
    namespace gameplay = wxl::core::seams::gameplay;

    constexpr uint32_t kReadyCheckTime = 30000; ///< what MSG_RAID_READY_CHECK's handler waits, in ms

    /// Who started the ready check in progress: MSG_RAID_READY_CHECK_FINISHED does not say.
    uint64_t g_initiator = 0;
}

namespace wxl::seam::Raid::Packet_MSG_RAID_READY_CHECK
{
    void WXL_CALL Before(void*, uint32_t, uint32_t, void* msg)
    {
        wxl::core::seams::MessageReader in(msg);
        uint64_t initiator = 0;
        if (!in.U64(initiator)) return;
        g_initiator = initiator;
        wxl::host::ModHost* host = gameplay::Listener({ WXL_EV_ReadyCheck });
        if (!host) return;
        wxl_ready_check_args args{};
        args.initiator_guid = initiator;
        args.initiator      = wxl::core::objects::FromGuid(initiator);
        args.duration       = kReadyCheckTime;
        host->Raise(WXL_EV_ReadyCheck, &args);
    }
}

namespace wxl::seam::Raid::Packet_MSG_RAID_READY_CHECK_CONFIRM
{
    void WXL_CALL Before(void*, uint32_t, uint32_t, void* msg)
    {
        wxl::host::ModHost* host = gameplay::Listener({ WXL_EV_ReadyCheckResponse });
        if (!host) return;
        wxl::core::seams::MessageReader in(msg);
        wxl_ready_check_response_args args{};
        uint8_t ready = 0;
        if (!in.U64(args.member_guid)) return;
        // The answer byte may be missing, which the client reads as not ready.
        if (in.Left() && !in.U8(ready)) return;
        args.member = wxl::core::objects::FromGuid(args.member_guid);
        args.ready  = ready ? WXL_TRUE : WXL_FALSE;
        host->Raise(WXL_EV_ReadyCheckResponse, &args);
    }
}

namespace wxl::seam::Raid::Packet_MSG_RAID_READY_CHECK_FINISHED
{
    void WXL_CALL Before(void*, uint32_t, uint32_t, void*)
    {
        const uint64_t initiator = g_initiator;
        g_initiator              = 0;
        wxl::host::ModHost* host = gameplay::Listener({ WXL_EV_ReadyCheckFinished });
        if (!host) return;
        wxl_ready_check_end_args args{};
        args.initiator_guid = initiator;
        host->Raise(WXL_EV_ReadyCheckFinished, &args);
    }
}
