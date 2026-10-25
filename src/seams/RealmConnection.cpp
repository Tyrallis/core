// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The RealmConnection seams: what the runtime does as the world server answers the login,
// lists the characters, and is asked for one, on both builds. Every body reads the message
// from its cursor, without moving it.

#include "wxl/seams/seams.h"

#include "runtime/Core.hpp"
#include "seams/Message.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <cstddef>
#include <cstdint>

namespace
{
    using wxl::core::seams::MessageReader;

    /// The results that carry a billing block and, for the queue, a position.
    constexpr uint8_t kAuthOk        = 12;
    constexpr uint8_t kAuthWaitQueue = 27;

    /// The most characters a list may hold; the client refuses a longer one as a whole.
    constexpr uint8_t kMaxCharacters = 10;

    /// The equipment slots listed per character, each a display id, a slot type and a visual id.
    constexpr size_t kEquipmentSlots = 23;

    /// One character of the list, read into the args of its event; `host` null reads without
    /// raising. False at a read that failed.
    bool Character(wxl::host::ModHost* host, MessageReader& in, uint32_t index, uint32_t count)
    {
        wxl_character_args args{};
        const char*        name       = nullptr;
        uint32_t           nameLength = 0;
        uint8_t            race = 0, classId = 0, sex = 0, level = 0, firstLogin = 0;
        if (!in.U64(args.guid) || !in.CString(name, nameLength) || !in.U8(race) || !in.U8(classId) ||
            !in.U8(sex) || !in.Skip(5) || !in.U8(level) || !in.U32(args.zone_id) ||
            !in.U32(args.map_id) || !in.F32(args.position.x) || !in.F32(args.position.y) ||
            !in.F32(args.position.z) || !in.U32(args.guild_id) || !in.U32(args.flags) ||
            !in.U32(args.customize_flags) || !in.U8(firstLogin) || !in.Skip(12) ||
            !in.Skip(kEquipmentSlots * 9))
            return false;
        if (!host) return true;

        args.index       = index;
        args.count       = count;
        args.name        = wxl_str{name, nameLength};
        args.race        = race;
        args.class_id    = classId;
        args.sex         = sex;
        args.level       = level;
        args.first_login = firstLogin ? WXL_TRUE : WXL_FALSE;
        host->Raise(WXL_EV_GlueCharacter, &args);
        return true;
    }
}

namespace wxl::seam::RealmConnection::HandleAuthResponse
{
    void WXL_CALL Before(void* self, uint32_t msg_id, uint32_t time, void* msg)
    {
        (void)self;
        (void)msg_id;
        (void)time;

        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return;

        MessageReader        in(msg);
        wxl_auth_result_args args{};
        uint8_t              result = 0;
        if (!in.U8(result)) return;
        args.result = result;

        if (result == kAuthOk || result == kAuthWaitQueue)
        {
            // The billing block when the message holds it, by the client's own test: the bytes
            // left, less the queue's five, in unsigned arithmetic (0x00464699-0x004646A1, `jb`).
            // A queue message with fewer than five left wraps and reads the block anyway.
            const uint32_t left = static_cast<uint32_t>(in.Left()) - (result == kAuthWaitQueue ? 5u : 0u);
            // As the client's reads: one that runs out leaves its value, and every read after it,
            // as it was; the result is answered all the same.
            if (left >= 10)
            {
                uint8_t flags = 0, expansion = 0;
                (void)(in.U32(args.billing_time_remaining) && in.U8(flags) &&
                       in.U32(args.billing_time_rested) && in.U8(expansion));
                args.billing_flags = flags;
                args.expansion     = expansion;
            }
            if (result == kAuthWaitQueue)
            {
                uint8_t migration = 0;
                (void)(in.U32(args.queue_position) && in.U8(migration));
                args.free_character_migration = migration ? WXL_TRUE : WXL_FALSE;
            }
        }
        host->Raise(WXL_EV_GlueAuthResult, &args);
    }
}

namespace wxl::seam::RealmConnection::HandleCharEnum
{
    void WXL_CALL Before(void* self, uint32_t msg_id, uint32_t time, void* msg)
    {
        (void)self;
        (void)msg_id;
        (void)time;

        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return;

        MessageReader in(msg);
        uint8_t       count = 0;
        if (!in.U8(count)) return;

        wxl_character_list_args list{};
        list.count = count;
        list.valid = count <= kMaxCharacters ? WXL_TRUE : WXL_FALSE;
        if (!list.valid)
        {
            host->Raise(WXL_EV_GlueCharacterList, &list);
            return;
        }

        // The whole list first, so one cut short raises none of it.
        MessageReader probe = in;
        for (uint32_t i = 0; i < count; ++i)
            if (!Character(nullptr, probe, i, count)) return;

        host->Raise(WXL_EV_GlueCharacterList, &list);
        for (uint32_t i = 0; i < count; ++i) Character(host, in, i, count);
    }
}

namespace wxl::seam::RealmConnection::RequestCharacterLogin
{
    void WXL_CALL Before(void* self, uint64_t guid, int32_t a2)
    {
        (void)self;
        (void)a2;

        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return;

        wxl_enter_world_args args{};
        args.character_guid = guid;
        host->Raise(WXL_EV_GlueEnterWorld, &args);
    }
}
