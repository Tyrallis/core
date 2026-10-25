// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The Duel seams: challenges, countdowns and outcomes, on both builds.

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

    wxl_str Str(const char* text, uint32_t length) { return wxl_str{ text ? text : "", text ? length : 0u }; }
}

namespace wxl::seam::Duel::Packet_SMSG_DUEL_REQUESTED
{
    bool WXL_CALL Gate(int32_t* result, void** param, uint32_t* msg_id, uint32_t* time, void** msg)
    {
        (void)param;
        (void)msg_id;
        (void)time;
        wxl::host::ModHost* host = gameplay::Listener({ WXL_EV_DuelRequested });
        if (!host) return false;
        wxl::core::seams::MessageReader in(*msg);
        uint64_t arbiter = 0, challenger = 0;
        if (!in.U64(arbiter) || !in.U64(challenger)) return false;
        // The client answers a challenge of the player's own at once; that is not a request.
        const void* self = wxl::core::binding::local_player_object();
        if (self && wxl::core::binding::object_guid(self) == challenger) return false;

        wxl_bool              accept = WXL_FALSE, decline = WXL_FALSE;
        wxl_duel_request_args args{};
        args.challenger_guid = challenger;
        args.challenger      = wxl::core::objects::FromGuid(challenger);
        args.arbiter_guid    = arbiter;
        args.accept          = &accept;
        args.decline         = &decline;
        host->Raise(WXL_EV_DuelRequested, &args);
        if (!accept && !decline) return false;
        wxl::core::binding::duel_answer(arbiter, challenger, decline ? WXL_FALSE : WXL_TRUE);
        *result = 1;
        return true;
    }
}

namespace wxl::seam::Duel::Packet_SMSG_DUEL_COUNTDOWN
{
    void WXL_CALL Before(void*, uint32_t, uint32_t, void* msg)
    {
        wxl::host::ModHost* host = gameplay::Listener({ WXL_EV_DuelCountdown });
        if (!host) return;
        wxl::core::seams::MessageReader in(msg);
        wxl_duel_countdown_args args{};
        if (!in.U32(args.duration)) return;
        host->Raise(WXL_EV_DuelCountdown, &args);
    }
}

namespace wxl::seam::Duel::Packet_SMSG_DUEL_WINNER
{
    void WXL_CALL Before(void*, uint32_t, uint32_t, void* msg)
    {
        wxl::host::ModHost* host = gameplay::Listener({ WXL_EV_DuelWon });
        if (!host) return;
        wxl::core::seams::MessageReader in(msg);
        uint8_t     fled = 0;
        const char* winner = nullptr;
        const char* loser  = nullptr;
        uint32_t    winnerLength = 0, loserLength = 0;
        if (!in.U8(fled) || !in.CString(winner, winnerLength) || !in.CString(loser, loserLength)) return;
        wxl_duel_won_args args{};
        args.winner = Str(winner, winnerLength);
        args.loser  = Str(loser, loserLength);
        args.fled   = fled ? WXL_TRUE : WXL_FALSE;
        host->Raise(WXL_EV_DuelWon, &args);
    }
}

namespace wxl::seam::Duel::Packet_SMSG_DUEL_COMPLETE
{
    void WXL_CALL Before(void*, uint32_t, uint32_t, void* msg)
    {
        wxl::host::ModHost* host = gameplay::Listener({ WXL_EV_DuelFinished });
        if (!host) return;
        wxl::core::seams::MessageReader in(msg);
        uint8_t completed = 0;
        if (!in.U8(completed)) return;
        wxl_duel_finished_args args{};
        args.completed = completed ? WXL_TRUE : WXL_FALSE;
        host->Raise(WXL_EV_DuelFinished, &args);
    }
}
