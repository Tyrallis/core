// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The Achievement seam: achievements earned, on both builds.

#include "wxl/seams/seams.h"

#include "binding/Binding.hpp"
#include "objects/Objects.hpp"
#include "runtime/Core.hpp"
#include "seams/Gameplay.hpp"
#include "seams/Message.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <cstdint>

namespace wxl::seam::Achievement::Packet_SMSG_ACHIEVEMENT_EARNED
{
    void WXL_CALL Before(void*, uint32_t, uint32_t, void* msg)
    {
        wxl::host::ModHost* host = wxl::core::seams::gameplay::Listener({ WXL_EV_AchievementEarned });
        if (!host) return;
        wxl::core::seams::MessageReader in(msg);
        wxl_achievement_args args{};
        uint32_t silent = 0;
        if (!in.PackedGuid(args.player_guid) || !in.U32(args.achievement_id) || !in.U32(args.date) ||
            !in.U32(silent))
            return;
        args.player = wxl::core::objects::FromGuid(args.player_guid);
        args.silent = silent ? WXL_TRUE : WXL_FALSE;
        host->Raise(WXL_EV_AchievementEarned, &args);
    }
}
