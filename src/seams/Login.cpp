// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The Login seams: what the runtime does as the logon server lists the realms, on both builds.
// Raised on the logon connection's thread; the body reads the message from its cursor,
// without moving it.

#include "wxl/seams/seams.h"

#include "binding/Binding.hpp"
#include "runtime/Core.hpp"
#include "seams/Message.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <cstdint>

namespace
{
    using wxl::core::seams::MessageReader;

    /// The realm flag that says a version follows.
    constexpr uint8_t kFlagVersion = 0x4;

    /// What Login::GetLoginServerType answers for a Battle.net login: its list carries four
    /// dwords where a Grunt logon server's carries the realm id as a byte.
    constexpr int32_t kServerBattlenet = 1;

    /// One realm of the list, read into the args of its event in the layout `battlenet` names;
    /// `host` null reads without raising. False at a read that failed.
    bool Realm(wxl::host::ModHost* host, MessageReader& in, bool battlenet, uint32_t index,
               uint32_t count)
    {
        wxl_realm_args args{};
        const char*    name = nullptr, *address = nullptr;
        uint32_t       nameLength = 0, addressLength = 0;
        uint8_t        type = 0, locked = 0, flags = 0, characters = 0, category = 0;
        if (!in.U8(type) || !in.U8(locked) || !in.U8(flags) || !in.CString(name, nameLength) ||
            !in.CString(address, addressLength) || !in.F32(args.population) || !in.U8(characters) ||
            !in.U8(category))
            return false;
        if (battlenet)
        {
            if (!in.U32(args.region_id) || !in.U32(args.battlegroup_id) || !in.U32(args.realm_id) ||
                !in.U32(args.build))
                return false;
        }
        else
        {
            uint8_t realmId = 0;
            if (!in.U8(realmId)) return false;
            args.realm_id = realmId;
        }
        if (flags & kFlagVersion)
        {
            uint8_t  major = 0, minor = 0, revision = 0;
            uint16_t build = 0;
            if (!in.U8(major) || !in.U8(minor) || !in.U8(revision) || !in.U16(build)) return false;
            args.version_major    = major;
            args.version_minor    = minor;
            args.version_revision = revision;
            args.version_build    = build;
        }
        if (!host) return true;

        args.index      = index;
        args.count      = count;
        args.name       = wxl_str{name, nameLength};
        args.address    = wxl_str{address, addressLength};
        args.type       = type;
        args.flags      = flags;
        args.locked     = locked ? WXL_TRUE : WXL_FALSE;
        args.characters = characters;
        args.category   = category;
        host->Raise(WXL_EV_GlueRealm, &args);
        return true;
    }
}

namespace wxl::seam::Login::RealmListResult
{
    void WXL_CALL Before(void* self, void* msg)
    {
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return;

        // The layout is the login's, as LoginResponse::HandleRealmData decides it: a Grunt logon
        // server's list as it came, or the one BattlenetLogin::BuildRealmListResult wrote.
        const int32_t server = wxl::core::binding::login_server_type(self);
        if (server < 0) return;
        const bool battlenet = server == kServerBattlenet;

        MessageReader in(msg);
        uint16_t      count = 0;
        if (!in.U16(count)) return;

        // The whole list first, so one cut short raises none of it.
        MessageReader probe = in;
        for (uint32_t i = 0; i < count; ++i)
            if (!Realm(nullptr, probe, battlenet, i, count)) return;

        wxl_realm_list_args list{};
        list.count = count;
        host->Raise(WXL_EV_GlueRealmList, &list);
        for (uint32_t i = 0; i < count; ++i) Realm(host, in, battlenet, i, count);
    }
}
