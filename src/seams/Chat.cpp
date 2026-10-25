// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The Chat seams: what the runtime does as a chat message arrives and is shown, as the local
// player sends one from the interface, and as experience is logged, on both builds. The
// handlers read the message from its cursor, without moving it; the gates read the script's
// arguments off the Lua stack, without moving it. All of them run on the main thread.
//
// A received message is followed through the client's own decisions rather than judged here.
// ChatMessageReceived reads it; AddChatMessage tells whether a call adds it, by the text the
// client hands over; then whatever the client does with that line decides what is raised:
// RecordChat commits it (ChatMessage), CHAT_MSG_ADDON delivers it (ChatAddonMessage),
// QueueChatText holds it, and it is followed again when the client adds the held copy, until
// PENDINGCHAT's destructor says the copy is gone. A line the client drops reaches none of them.

#include "wxl/seams/seams.h"

#include "binding/Binding.hpp"
#include "objects/Objects.hpp"
#include "runtime/Core.hpp"
#include "seams/Chat.hpp"
#include "seams/Message.hpp"
#include "services/ScriptService.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"
#include "wxl/services/script.h"

#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

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

    /// The language of an addon message, which the client delivers rather than shows.
    constexpr uint32_t kAddonLanguage = 0xFFFFFFFFu;

    wxl_str Str(const std::string& text) { return wxl_str{text.data(), static_cast<uint32_t>(text.size())}; }

    wxl_str Str(const char* text)
    {
        return text ? wxl_str{text, static_cast<uint32_t>(std::strlen(text))} : wxl_str{"", 0};
    }

    /// A received message as ChatMessageReceived read it. Its strings point into the message's
    /// own bytes until Keep() copies them, which a message the client holds past the handler
    /// needs.
    struct Message
    {
        wxl_chat_args args{};
        bool          kept = false;
        std::string   senderName, targetName, channel, text;

        void Keep()
        {
            if (kept) return;
            senderName.assign(args.sender_name.data, args.sender_name.size);
            targetName.assign(args.target_name.data, args.target_name.size);
            channel.assign(args.channel.data, args.channel.size);
            text.assign(args.text.data, args.text.size);
            kept = true;
        }

        wxl_chat_args View() const
        {
            wxl_chat_args view = args;
            if (kept)
            {
                view.sender_name = Str(senderName);
                view.target_name = Str(targetName);
                view.channel     = Str(channel);
                view.text        = Str(text);
            }
            return view;
        }
    };

    /// The message ChatMessageReceived is handling, until it hands its text to AddChatMessage.
    /// `args.text` points into the message's own bytes, which no other line shares.
    struct InFlight
    {
        bool    live = false;
        Message message;
    };

    /// A received message the client holds as a PENDINGCHAT, by the copy of its text the
    /// client will hand AddChatMessage when it adds the line again.
    struct Held
    {
        const void* line = nullptr;
        const char* text = nullptr;
        Message     message;
    };

    /// The received message the running AddChatMessage call adds, if it adds one. `done` once
    /// the client committed, delivered or held it: one line, one outcome.
    struct Adding
    {
        bool    live = false;
        bool    done = false;
        Message message;
    };

    InFlight          g_inFlight;
    std::vector<Held> g_held;
    Adding            g_adding;
}

namespace wxl::core::seams::chat
{
    bool AddonDelivered(uint32_t index, const char* format, void* list, wxl_chat_addon_args& out)
    {
        if (!g_adding.live || g_adding.done || g_adding.message.args.language != kAddonLanguage) return false;
        const char* name = wxl::core::services::script::EventName(index);
        if (!name || std::strcmp(name, "CHAT_MSG_ADDON") != 0) return false;
        g_adding.done = true;
        if (!format) return false;

        // The client fires it as "%s%s%s%s": the prefix, the text, the channel, the sender's
        // name (0x0050A0E0..0x0050A0F0 push them in reverse). The list is a copy.
        const char* strings[4] = {};
        uint32_t    count      = 0;
        va_list     args       = static_cast<va_list>(list);
        for (const char* at = format; *at && count < 4; ++at)
        {
            if (at[0] != '%') continue;
            if (at[1] != 's') return false;
            strings[count++] = va_arg(args, const char*);
            ++at;
        }
        if (count != 4) return false;

        const wxl_chat_args& message = g_adding.message.args;
        out             = wxl_chat_addon_args{};
        out.type        = message.type;
        out.sender_guid = message.sender_guid;
        out.sender      = message.sender_guid ? wxl::core::objects::FromGuid(message.sender_guid) : WXL_HANDLE_NONE;
        out.sender_name = Str(strings[3]);
        out.prefix      = Str(strings[0]);
        out.text        = Str(strings[1]);
        out.channel     = Str(strings[2]);
        out.gm          = message.gm;
        return true;
    }
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

        args.type                = type;
        args.tag                 = tag;
        args.gm                  = is_gm ? WXL_TRUE : WXL_FALSE;
        g_inFlight.message.args  = args;
        g_inFlight.live          = true;
    }

    void WXL_CALL After(int32_t result, void* msg, uint8_t is_gm)
    {
        (void)result;
        (void)msg;
        (void)is_gm;
        g_inFlight = InFlight{};
        // The message's bytes go with the handler: a line still unresolved here was dropped.
        if (g_adding.live && !g_adding.message.kept) g_adding = Adding{};
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

        // Every call adds one line, and nothing the client does before it commits, holds or
        // delivers that line adds another: the call supersedes whatever the last one left.
        g_adding = Adding{};
        if (!text || !wxl::core::Host()) return;

        if (g_inFlight.live && text == g_inFlight.message.args.text.data &&
            sender_guid == g_inFlight.message.args.sender_guid)
        {
            // The line ChatMessageReceived lets through: the text still points into its bytes.
            g_adding.message = std::move(g_inFlight.message);
            g_inFlight       = InFlight{};
        }
        else
        {
            // A line the client held, added again from its copy: the copy's own pointer.
            auto held = g_held.begin();
            while (held != g_held.end() && !(held->text == text && held->message.args.sender_guid == sender_guid))
                ++held;
            if (held == g_held.end()) return;
            g_adding.message = std::move(held->message);
            g_held.erase(held);
        }

        // The type the client settled on: a foreign whisper is a whisper.
        g_adding.live              = true;
        g_adding.message.args.type = static_cast<uint32_t>(type);
    }
}

namespace wxl::seam::Chat::QueueChatText
{
    void WXL_CALL After(const char* text, int32_t type, const char* sender_name, const char* target_name,
                        uint32_t language, const char* channel_name, const char* flags, int32_t queued,
                        int32_t unused, uint64_t sender_guid, uint32_t sender_unk, uint64_t target_guid,
                        int32_t achievement_id, uint8_t is_gm, const void* conversation)
    {
        (void)text;
        (void)type;
        (void)sender_name;
        (void)target_name;
        (void)language;
        (void)channel_name;
        (void)flags;
        (void)queued;
        (void)unused;
        (void)sender_unk;
        (void)target_guid;
        (void)achievement_id;
        (void)is_gm;
        (void)conversation;

        if (!g_adding.live || g_adding.done) return;
        g_adding.done = true;

        // The line just appended, and the copy of the text the client will add it again with.
        const void* line = wxl::core::binding::chat_pending_last();
        const char* copy = line ? wxl::core::binding::pending_chat_text(line) : nullptr;
        if (!copy || sender_guid != g_adding.message.args.sender_guid) return;

        Held held;
        held.line    = line;
        held.text    = copy;
        held.message = std::move(g_adding.message);
        held.message.Keep();
        g_held.push_back(std::move(held));
        g_adding = Adding{};
    }
}

namespace wxl::seam::Chat::DestroyPendingChat
{
    void WXL_CALL Before(void* self)
    {
        for (auto held = g_held.begin(); held != g_held.end();)
            held = held->line == self ? g_held.erase(held) : held + 1;
    }
}

namespace wxl::seam::Chat::RecordChat
{
    void WXL_CALL Before(const char* text, int32_t type, const char* sender_name, uint64_t sender_guid,
                         uint32_t sender_unk, uint32_t zone_channel_id, const char* channel_name,
                         uint32_t line_id, const void* player)
    {
        (void)sender_unk;
        (void)zone_channel_id;
        (void)channel_name;
        (void)player;

        if (!g_adding.live || g_adding.done) return;
        g_adding.done = true;
        if (sender_guid != g_adding.message.args.sender_guid) return;

        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return;

        // The line as the frames get it: its final type (a say with no text is an emote), the
        // name it shows, its number, its text in the player's languages and filtered.
        wxl_chat_args args = g_adding.message.View();
        args.type          = static_cast<uint32_t>(type);
        if (sender_name) args.sender_name = Str(sender_name);
        args.line_id    = line_id;
        args.shown_text = Str(text);
        args.sender     = args.sender_guid ? wxl::core::objects::FromGuid(args.sender_guid) : WXL_HANDLE_NONE;
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
