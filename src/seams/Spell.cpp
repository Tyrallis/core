// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// The Spell seams: what the runtime does as the server reports casts and as the local player
// asks for one, on both builds.

#include "wxl/seams/seams.h"

#include "binding/Binding.hpp"
#include "objects/Objects.hpp"
#include "runtime/Core.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{
    namespace binding = wxl::core::binding;
    namespace objects = wxl::core::objects;

    /// The two opcodes one handler serves, as the client numbers them.
    constexpr uint32_t kSpellStart = 0x131; ///< SMSG_SPELL_START
    constexpr uint32_t kSpellGo    = 0x132; ///< SMSG_SPELL_GO

    /// The bits of the server's target flags that put an object target, as a packed guid,
    /// first in the target block: a unit, a unit's minipet, a game object, a corpse.
    constexpr uint32_t kObjectTargetFlags = 0x18A02;

    /// The miss reason the server follows with a reflect result byte.
    constexpr uint8_t kMissReflect = 11;

    /**
     * @brief Reads a message's body in order, from the store's cursor onwards.
     *
     * The store itself is never moved: what this reads is what the client's own handler is
     * about to read, and the handler must find the cursor where the dispatcher left it. A read
     * past the end fails and every read after it fails too, so a truncated message raises
     * nothing rather than something half read.
     */
    class Reader
    {
    public:
        explicit Reader(void* store)
        {
            if (!store) return;
            const uint8_t* data = static_cast<const uint8_t*>(binding::message_data(store));
            const uint32_t size = binding::message_size(store);
            const uint32_t read = binding::message_read(store);
            if (!data || read > size) return;
            at_  = data + read;
            end_ = data + size;
        }

        bool U8(uint8_t& out) { return Take(&out, sizeof out); }
        bool U32(uint32_t& out) { return Take(&out, sizeof out); }

        bool Skip(size_t count)
        {
            if (!at_ || static_cast<size_t>(end_ - at_) < count)
            {
                at_ = nullptr;
                return false;
            }
            at_ += count;
            return true;
        }

        /// A guid as the server packs it: a mask byte, then one byte per bit set, lowest first.
        bool PackedGuid(uint64_t& out)
        {
            uint8_t mask = 0;
            out         = 0;
            if (!U8(mask)) return false;
            for (uint32_t bit = 0; bit < 8; ++bit)
            {
                if (!(mask & (1u << bit))) continue;
                uint8_t byte = 0;
                if (!U8(byte)) return false;
                out |= static_cast<uint64_t>(byte) << (bit * 8);
            }
            return true;
        }

    private:
        bool Take(void* out, size_t count)
        {
            if (!Skip(count)) return false;
            std::memcpy(out, at_ - count, count);
            return true;
        }

        const uint8_t* at_  = nullptr;
        const uint8_t* end_ = nullptr;
    };

    /// The target block both cast messages end with: the flags, then the object target when
    /// the flags name one. What follows (item, source, destination, string) is not read.
    bool ReadTarget(Reader& in, wxl_spell_cast_args& args)
    {
        if (!in.U32(args.target_flags)) return false;
        if (!(args.target_flags & kObjectTargetFlags)) return true;
        return in.PackedGuid(args.target_guid);
    }

    /// A handle for a guid the server named: the object's when the client holds it.
    wxl_handle HandleFor(uint64_t guid)
    {
        return guid ? objects::FromGuid(guid) : WXL_HANDLE_NONE;
    }
}

namespace wxl::seam::Spell::Packet_Group_30
{
    void WXL_CALL Before(void* param, uint32_t msg_id, uint32_t time, void* msg)
    {
        (void)param;
        (void)time;

        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return;

        const bool start = msg_id == kSpellStart;
        if (!start && msg_id != kSpellGo) return;

        // The header both messages share.
        Reader              in(msg);
        wxl_spell_cast_args args{};
        uint8_t             count = 0;
        if (!in.PackedGuid(args.cast_item) || !in.PackedGuid(args.caster_guid) || !in.U8(count) ||
            !in.U32(args.spell_id) || !in.U32(args.cast_flags))
            return;
        args.cast_count = count;

        if (start)
        {
            if (!in.U32(args.cast_time)) return;
        }
        else
        {
            // The server's time, the hits as full guids, the misses as a full guid and a
            // reason each, with a reflect result after a reflected one.
            uint32_t serverTime = 0;
            uint8_t  hits       = 0;
            uint8_t  misses     = 0;
            if (!in.U32(serverTime) || !in.U8(hits) || !in.Skip(hits * 8u) || !in.U8(misses))
                return;
            for (uint8_t i = 0; i < misses; ++i)
            {
                uint8_t reason = 0;
                if (!in.Skip(8) || !in.U8(reason)) return;
                if (reason == kMissReflect && !in.Skip(1)) return;
            }
            args.hit_count  = hits;
            args.miss_count = misses;
        }
        if (!ReadTarget(in, args)) return;

        args.caster = HandleFor(args.caster_guid);
        args.target = HandleFor(args.target_guid);
        if (start) host->Raise(WXL_EV_SpellCastStart, &args);
        else host->Raise(WXL_EV_SpellCastGo, &args);
    }
}

namespace wxl::seam::Spell::Packet_SMSG_SPELL_FAILURE
{
    void WXL_CALL Before(void* param, uint32_t msg_id, uint32_t time, void* msg)
    {
        (void)param;
        (void)msg_id;
        (void)time;

        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return;

        Reader              in(msg);
        wxl_spell_fail_args args{};
        uint8_t             count  = 0;
        uint8_t             reason = 0;
        if (!in.PackedGuid(args.caster_guid) || !in.U8(count) || !in.U32(args.spell_id) ||
            !in.U8(reason))
            return;
        args.cast_count   = count;
        args.reason       = reason;
        args.custom_error = -1;
        args.custom_arg   = -1;
        args.caster       = HandleFor(args.caster_guid);
        host->Raise(WXL_EV_SpellCastInterrupted, &args);
    }
}

namespace wxl::seam::Spell::SpellFailed
{
    void WXL_CALL Before(void* cast, const void* spell, int32_t reason, int32_t misc1,
                         int32_t misc2, uint8_t reset_cooldown)
    {
        (void)spell;
        (void)reset_cooldown;

        wxl::host::ModHost* host = wxl::core::Host();
        if (!host || !cast) return;

        wxl_spell_fail_args args{};
        args.caster_guid  = binding::pending_cast_caster(cast);
        args.caster       = HandleFor(args.caster_guid);
        args.spell_id     = binding::pending_cast_spell_id(cast);
        args.cast_count   = binding::pending_cast_count(cast);
        args.reason       = static_cast<uint32_t>(reason);
        args.custom_error = misc1;
        args.custom_arg   = misc2;
        host->Raise(WXL_EV_SpellCastFailed, &args);
    }
}

namespace wxl::seam::Spell::CastSpell
{
    bool WXL_CALL Gate(uint8_t* result, void** caster, int32_t* spell_id, const void** item,
                       uint64_t* target, void** cast, uint8_t* confirmed)
    {
        (void)cast;

        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return false;

        wxl_bool               refused = WXL_FALSE;
        wxl_spell_request_args args{};
        args.caster      = *caster ? objects::HandleOf(*caster) : WXL_HANDLE_NONE;
        args.spell_id    = static_cast<uint32_t>(*spell_id);
        args.item        = *item ? objects::HandleOf(const_cast<void*>(*item)) : WXL_HANDLE_NONE;
        args.target_guid = *target;
        args.confirmed   = *confirmed ? WXL_TRUE : WXL_FALSE;
        args.refused     = &refused;

        host->Raise(WXL_EV_SpellCastRequest, &args);
        if (!refused) return false;

        // What the client answers for a spell it cannot cast: false, nothing sent, nothing
        // pending.
        *result = 0;
        return true;
    }
}
