// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The Chat seams: what the runtime does as a chat message arrives and is shown, as the local
// player sends one from the interface, and as experience is logged, on both builds. The
// handlers read the message from its cursor, without moving it; the gates read the script's
// arguments off the Lua stack, without moving it. All of them run on the main thread.

#include "wxl/seams/seams.h"

#include "binding/Binding.hpp"
#include "objects/Objects.hpp"
#include "runtime/Core.hpp"
#include "seams/Message.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"
#include "wxl/services/script.h"

#include <cstdint>

namespace
{
    namespace binding = wxl::core::binding;
    using wxl::core::seams::MessageReader;

    /// The chat types the message's layout depends on, as the client numbers them.
    constexpr uint8_t kWhisperForeign   = 8;
    constexpr uint8_t kMonsterSay       = 12;
    constexpr uint8_t kMonsterParty     = 13;
    constexpr uint8_t kMonsterYell      = 14;
    constexpr uint8_t kMonsterWhisper   = 15;
    constexpr uint8_t kMonsterEmote     = 16;
    constexpr uint8_t kChannel          = 17;
    constexpr uint8_t kBgSystemNeutral  = 36;
    constexpr uint8_t kBgSystemAlliance = 37;
    constexpr uint8_t kBgSystemHorde    = 38;
    constexpr uint8_t kRaidBossEmote    = 41;
    constexpr uint8_t kRaidBossWhisper  = 42;
    constexpr uint8_t kBattlenet        = 47;
    constexpr uint8_t kAchievement      = 48;
    constexpr uint8_t kGuildAchievement = 49;

    /// Whether a target guid is one the server follows with a name: the client's own tests.
    bool TargetNamed(uint64_t guid)
    {
        return guid && ((guid & 0xF000000000000000ull) || !(guid & 0xF07FFFFFFFFFFFFFull));
    }

    bool TargetNamedForMonster(uint64_t guid)
    {
        return TargetNamed(guid) && (guid & 0xF0F0000000000000ull) != 0xF040000000000000ull;
    }

    bool Text(MessageReader& in, wxl_str& out)
    {
        const char* text   = nullptr;
        uint32_t    length = 0;
        if (!in.SizedString(text, length)) return false;
        out = wxl_str{text, length};
        return true;
    }

    /// A string argument of the running script function, or empty when it is not one. A
    /// number is read as its text, as the client's lua_tolstring does.
    wxl_str Arg(void* state, int32_t index)
    {
        if (index > binding::script_top(state)) return wxl_str{"", 0};
        const uint32_t type = binding::script_value_type(state, index);
        if (type != WXL_VALUE_STRING && type != WXL_VALUE_NUMBER) return wxl_str{"", 0};
        uint32_t    length = 0;
        const char* text   = binding::script_to_string(state, index, &length);
        return text ? wxl_str{text, length} : wxl_str{"", 0};
    }

    /// Raises ChatSend for the script function running on `state`, with its arguments as
    /// `text`, `type`, `language`/`prefix` and `target` index them; answers whether a handler
    /// suppressed the message.
    bool RaiseSend(void* state, bool addon)
    {
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host || !state) return false;

        wxl_bool           suppressed = WXL_FALSE;
        wxl_chat_send_args args{};
        if (addon)
        {
            args.prefix = Arg(state, 1);
            args.text   = Arg(state, 2);
            args.type   = Arg(state, 3);
            args.target = Arg(state, 4);
        }
        else
        {
            args.text     = Arg(state, 1);
            args.type     = Arg(state, 2);
            args.language = Arg(state, 3);
            args.target   = Arg(state, 4);
        }
        args.addon      = addon ? WXL_TRUE : WXL_FALSE;
        args.suppressed = &suppressed;
        host->Raise(WXL_EV_ChatSend, &args);
        return suppressed != WXL_FALSE;
    }

    /// The message ChatMessageReceived is handling, as it read it, until the client shows it or
    /// drops it. `text` points into the message's own bytes, which no other line shares.
    struct InFlight
    {
        bool          live = false;
        wxl_chat_args args{};
    };

    InFlight g_inFlight;
}

namespace wxl::seam::Chat::ChatMessageReceived
{
    void WXL_CALL Before(void* msg, uint8_t is_gm)
    {
        g_inFlight = InFlight{};
        if (!wxl::core::Host()) return;

        MessageReader in(msg);
        wxl_chat_args args{};
        uint8_t       type = 0;
        uint32_t      senderUnk = 0;
        uint8_t       tag = 0;
        if (!in.U8(type) || !in.U32(args.language) || !in.U64(args.sender_guid) || !in.U32(senderUnk))
            return;

        switch (type)
        {
        case kMonsterSay:
        case kMonsterYell:
        case kRaidBossEmote:
        case kRaidBossWhisper:
        case kMonsterEmote:
        case kMonsterWhisper:
        case kMonsterParty:
        case kBattlenet:
            if (!Text(in, args.sender_name) || !in.U64(args.target_guid)) return;
            if (TargetNamedForMonster(args.target_guid) && !Text(in, args.target_name)) return;
            if (!Text(in, args.text) || !in.U8(tag)) return;
            break;
        case kWhisperForeign:
            if (!Text(in, args.sender_name) || !in.U64(args.target_guid) || !Text(in, args.text) ||
                !in.U8(tag))
                return;
            break;
        case kBgSystemNeutral:
        case kBgSystemAlliance:
        case kBgSystemHorde:
            if (!in.U64(args.target_guid)) return;
            if (TargetNamed(args.target_guid) && !Text(in, args.target_name)) return;
            if (!Text(in, args.text) || !in.U8(tag)) return;
            break;
        case kAchievement:
        case kGuildAchievement:
            if (!in.U64(args.target_guid) || !Text(in, args.text) || !in.U8(tag) ||
                !in.U32(args.achievement_id))
                return;
            break;
        default:
        {
            if (is_gm && !Text(in, args.sender_name)) return;
            if (type == kChannel)
            {
                const char* channel = nullptr;
                uint32_t    length  = 0;
                if (!in.CString(channel, length)) return;
                args.channel = wxl_str{channel, length};
            }
            if (!in.U64(args.target_guid) || !Text(in, args.text) || !in.U8(tag)) return;
            break;
        }
        }

        args.type        = type;
        args.tag         = tag;
        args.gm          = is_gm ? WXL_TRUE : WXL_FALSE;
        g_inFlight.args  = args;
        g_inFlight.live  = true;
    }

    void WXL_CALL After(int32_t result, void* msg, uint8_t is_gm)
    {
        (void)result;
        (void)msg;
        (void)is_gm;
        g_inFlight = InFlight{};
    }
}

namespace wxl::seam::Chat::AddChatMessage
{
    void WXL_CALL Before(const char* text, int32_t type, const char* sender_name, uint32_t language,
                         const char* channel_name, const char* target_name, const char* flags,
                         uint64_t sender_guid, uint32_t sender_unk, uint64_t target_guid,
                         int32_t achievement_id, uint8_t is_gm, const void* conversation)
    {
        (void)sender_name;
        (void)language;
        (void)channel_name;
        (void)target_name;
        (void)flags;
        (void)sender_unk;
        (void)target_guid;
        (void)achievement_id;
        (void)is_gm;
        (void)conversation;

        // Only the line ChatMessageReceived lets through for the message it read: the text
        // still points into that message's bytes.
        if (!g_inFlight.live || !text || text != g_inFlight.args.text.data ||
            sender_guid != g_inFlight.args.sender_guid)
            return;
        wxl_chat_args args = g_inFlight.args;
        g_inFlight         = InFlight{};

        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return;

        // The type the client settled on: a foreign whisper is shown as a whisper.
        args.type   = static_cast<uint32_t>(type);
        args.sender = args.sender_guid ? wxl::core::objects::FromGuid(args.sender_guid) : WXL_HANDLE_NONE;
        host->Raise(WXL_EV_ChatMessage, &args);
    }
}

namespace wxl::seam::Chat::Script_SendChatMessage
{
    bool WXL_CALL Gate(int32_t* result, void** state)
    {
        if (!RaiseSend(*state, false)) return false;

        // What the function answers for a message it did not send: no result.
        *result = 0;
        return true;
    }
}

namespace wxl::seam::Chat::Script_SendAddonMessage
{
    bool WXL_CALL Gate(int32_t* result, void** state)
    {
        if (!RaiseSend(*state, true)) return false;
        *result = 0;
        return true;
    }
}

namespace wxl::seam::Chat::Packet_SMSG_LOG_XPGAIN
{
    void WXL_CALL Before(void* param, uint32_t msg_id, uint32_t time, void* msg)
    {
        (void)param;
        (void)msg_id;
        (void)time;

        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return;

        MessageReader    in(msg);
        wxl_xp_gain_args args{};
        uint8_t          kind = 0, referAFriend = 0;
        args.group_multiplier = 1.0f;
        if (!in.U64(args.source_guid) || !in.U32(args.amount) || !in.U8(kind)) return;
        if (!kind && (!in.U32(args.bonus) || !in.F32(args.group_multiplier))) return;
        if (!in.U8(referAFriend)) return;

        args.source         = args.source_guid ? wxl::core::objects::FromGuid(args.source_guid) : WXL_HANDLE_NONE;
        args.kind           = kind;
        args.refer_a_friend = referAFriend ? WXL_TRUE : WXL_FALSE;
        host->Raise(WXL_EV_PlayerXpGain, &args);
    }
}
