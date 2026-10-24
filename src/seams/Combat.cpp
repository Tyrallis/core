// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The Combat seams: what the runtime does as the server reports attacks, damage, heals and
// misses, on both builds. Every body reads the message from its cursor, without moving it.

#include "wxl/seams/seams.h"

#include "objects/Objects.hpp"
#include "runtime/Core.hpp"
#include "seams/Message.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <cstddef>
#include <cstdint>

namespace
{
    namespace objects = wxl::core::objects;
    using wxl::core::seams::MessageReader;

    /// The opcodes the melee handler serves, as the client numbers them.
    constexpr uint32_t kAttackStart            = 0x143; ///< SMSG_ATTACKSTART
    constexpr uint32_t kAttackStop             = 0x144; ///< SMSG_ATTACKSTOP
    constexpr uint32_t kAttackerStateUpdate    = 0x14A; ///< SMSG_ATTACKERSTATEUPDATE
    constexpr uint32_t kEnvironmentalDamageLog = 0x1FC; ///< SMSG_ENVIRONMENTALDAMAGELOG
    constexpr uint32_t kCombatEventFailed      = 0x261; ///< SMSG_COMBAT_EVENT_FAILED

    /// The bits of a melee round's hit flags the body reads.
    constexpr uint32_t kHitMiss     = 0x10;   ///< the round missed
    constexpr uint32_t kHitAbsorbed = 0x60;   ///< an absorbed amount follows per school
    constexpr uint32_t kHitResisted = 0x180;  ///< a resisted amount follows per school
    constexpr uint32_t kHitCrit     = 0x200;  ///< a critical hit
    constexpr uint32_t kHitBlocked  = 0x2000; ///< a blocked amount follows

    /// The bit of a spell's hit flags that marks a critical hit.
    constexpr uint32_t kSpellHitCrit = 0x2;

    /// The victim states of a melee round that mean nothing landed.
    bool VictimEscaped(uint8_t state)
    {
        return state == 2 || state == 3 || state == 6 || state == 7 || state == 8;
    }

    /// The school mask of each environmental cause, in the server's order of causes.
    constexpr uint32_t kEnvironmentSchools[6] = { 1, 1, 1, 4, 8, 4 };

    /// The aura types a periodic log carries an entry for.
    constexpr uint32_t kAuraPeriodicDamage        = 3;
    constexpr uint32_t kAuraPeriodicHeal          = 8;
    constexpr uint32_t kAuraObsModHealth          = 20;
    constexpr uint32_t kAuraPeriodicEnergize      = 21;
    constexpr uint32_t kAuraObsModPower           = 24;
    constexpr uint32_t kAuraPeriodicManaLeech     = 64;
    constexpr uint32_t kAuraPeriodicDamagePercent = 89;

    /// A handle for a guid the server named: the object's when the client holds it.
    wxl_handle HandleFor(uint64_t guid)
    {
        return guid ? objects::FromGuid(guid) : WXL_HANDLE_NONE;
    }

    wxl_combat_attack_args AttackArgs(uint64_t attacker, uint64_t victim)
    {
        wxl_combat_attack_args args{};
        args.attacker_guid = attacker;
        args.attacker      = HandleFor(attacker);
        args.victim_guid   = victim;
        args.victim        = HandleFor(victim);
        return args;
    }

    /// A melee round: the amounts the server totals, the schools and the per-school amounts
    /// summed, then the victim's state and the swing's spell. The rest (rage gained, the
    /// debug block) is not read.
    void RaiseAttackerStateUpdate(wxl::host::ModHost* host, MessageReader& in)
    {
        uint32_t hitInfo  = 0;
        uint64_t attacker = 0;
        uint64_t victim   = 0;
        uint32_t damage   = 0;
        uint32_t overkill = 0;
        uint8_t  schools  = 0;
        if (!in.U32(hitInfo) || !in.PackedGuid(attacker) || !in.PackedGuid(victim) ||
            !in.U32(damage) || !in.U32(overkill) || !in.U8(schools))
            return;

        uint32_t school = 0;
        for (uint8_t i = 0; i < schools; ++i)
        {
            uint32_t mask = 0;
            if (!in.U32(mask) || !in.Skip(8)) return;
            school |= mask;
        }
        uint32_t absorbed = 0;
        if (hitInfo & kHitAbsorbed)
            for (uint8_t i = 0; i < schools; ++i)
            {
                uint32_t amount = 0;
                if (!in.U32(amount)) return;
                absorbed += amount;
            }
        uint32_t resisted = 0;
        if (hitInfo & kHitResisted)
            for (uint8_t i = 0; i < schools; ++i)
            {
                uint32_t amount = 0;
                if (!in.U32(amount)) return;
                resisted += amount;
            }

        uint8_t  victimState = 0;
        uint32_t spare       = 0;
        uint32_t spellId     = 0;
        uint32_t blocked     = 0;
        if (!in.U8(victimState) || !in.U32(spare) || !in.U32(spellId)) return;
        if ((hitInfo & kHitBlocked) && !in.U32(blocked)) return;

        if ((hitInfo & kHitMiss) || VictimEscaped(victimState))
        {
            wxl_combat_miss_args args{};
            args.source_guid = attacker;
            args.source      = HandleFor(attacker);
            args.target_guid = victim;
            args.target      = HandleFor(victim);
            args.spell_id    = spellId;
            args.melee       = WXL_TRUE;
            args.reason      = VictimEscaped(victimState) ? victimState : 0u;
            args.hit_info    = hitInfo;
            host->Raise(WXL_EV_CombatMiss, &args);
            return;
        }

        wxl_combat_damage_args args{};
        args.source_guid = attacker;
        args.source      = HandleFor(attacker);
        args.target_guid = victim;
        args.target      = HandleFor(victim);
        args.spell_id    = spellId;
        args.kind        = 0;
        args.amount      = damage;
        args.overkill    = overkill;
        args.school      = school;
        args.absorbed    = absorbed;
        args.resisted    = resisted;
        args.blocked     = blocked;
        args.crit        = (hitInfo & kHitCrit) ? WXL_TRUE : WXL_FALSE;
        args.hit_info    = hitInfo;
        host->Raise(WXL_EV_CombatDamage, &args);
    }

    void RaiseEnvironmentalDamage(wxl::host::ModHost* host, MessageReader& in)
    {
        uint64_t guid     = 0;
        uint8_t  cause    = 0;
        uint32_t damage   = 0;
        uint32_t absorbed = 0;
        uint32_t resisted = 0;
        if (!in.U64(guid) || !in.U8(cause) || !in.U32(damage) || !in.U32(absorbed) ||
            !in.U32(resisted))
            return;

        wxl_combat_damage_args args{};
        args.target_guid = guid;
        args.target      = HandleFor(guid);
        args.kind        = 3;
        args.environment = cause;
        args.amount      = damage;
        args.school      = cause < 6 ? kEnvironmentSchools[cause] : 0u;
        args.absorbed    = absorbed;
        args.resisted    = resisted;
        host->Raise(WXL_EV_CombatDamage, &args);
    }

    /// One entry of a periodic log, read into the args of the event it is; false at an aura
    /// type the client stops at, or a read that failed. `host` null reads without raising.
    bool PeriodicEntry(wxl::host::ModHost* host, MessageReader& in, uint64_t caster, uint64_t target,
                       uint32_t spellId)
    {
        uint32_t auraType = 0;
        if (!in.U32(auraType)) return false;
        switch (auraType)
        {
        case kAuraPeriodicDamage:
        case kAuraPeriodicDamagePercent:
        {
            wxl_combat_damage_args args{};
            uint8_t                crit = 0;
            if (!in.U32(args.amount) || !in.U32(args.overkill) || !in.U32(args.school) ||
                !in.U32(args.absorbed) || !in.U32(args.resisted) || !in.U8(crit))
                return false;
            if (!host) return true;
            args.source_guid = caster;
            args.source      = HandleFor(caster);
            args.target_guid = target;
            args.target      = HandleFor(target);
            args.spell_id    = spellId;
            args.kind        = 2;
            args.crit        = crit ? WXL_TRUE : WXL_FALSE;
            host->Raise(WXL_EV_CombatDamage, &args);
            return true;
        }
        case kAuraPeriodicHeal:
        case kAuraObsModHealth:
        {
            wxl_combat_heal_args args{};
            uint8_t              crit = 0;
            if (!in.U32(args.amount) || !in.U32(args.overheal) || !in.U32(args.absorbed) ||
                !in.U8(crit))
                return false;
            if (!host) return true;
            args.source_guid = caster;
            args.source      = HandleFor(caster);
            args.target_guid = target;
            args.target      = HandleFor(target);
            args.spell_id    = spellId;
            args.crit        = crit ? WXL_TRUE : WXL_FALSE;
            args.periodic    = WXL_TRUE;
            host->Raise(WXL_EV_CombatHeal, &args);
            return true;
        }
        case kAuraPeriodicEnergize:
        case kAuraObsModPower: return in.Skip(8);
        case kAuraPeriodicManaLeech: return in.Skip(12);
        default: return false;
        }
    }
}

namespace wxl::seam::Combat::Packet_Group_29
{
    void WXL_CALL Before(void* param, uint32_t msg_id, uint32_t time, void* msg)
    {
        (void)param;
        (void)time;

        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return;

        MessageReader in(msg);
        switch (msg_id)
        {
        case kAttackStart:
        {
            uint64_t attacker = 0;
            uint64_t victim   = 0;
            if (!in.U64(attacker) || !in.U64(victim)) return;
            const wxl_combat_attack_args args = AttackArgs(attacker, victim);
            host->Raise(WXL_EV_CombatAttackStart, &args);
            return;
        }
        case kAttackStop:
        case kCombatEventFailed:
        {
            uint64_t attacker   = 0;
            uint64_t victim     = 0;
            uint32_t faceTarget = 0;
            if (!in.PackedGuid(attacker) || !in.PackedGuid(victim) || !in.U32(faceTarget)) return;
            const wxl_combat_attack_args args = AttackArgs(attacker, victim);
            host->Raise(WXL_EV_CombatAttackStop, &args);
            return;
        }
        case kAttackerStateUpdate: RaiseAttackerStateUpdate(host, in); return;
        case kEnvironmentalDamageLog: RaiseEnvironmentalDamage(host, in); return;
        default: return;
        }
    }
}

namespace wxl::seam::Combat::HandleSpellLogMiss
{
    void WXL_CALL Before(void* msg, int32_t time_offset)
    {
        (void)time_offset;

        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return;

        MessageReader in(msg);
        uint32_t      spellId = 0;
        uint64_t      caster  = 0;
        uint8_t       debug   = 0;
        uint32_t      count   = 0;
        if (!in.U32(spellId) || !in.U64(caster) || !in.U8(debug) || !in.U32(count)) return;

        // The whole list first, so a list cut short raises none of it.
        const size_t entry = debug ? 17 : 9;
        if (in.Left() / entry < count) return;

        wxl_combat_miss_args args{};
        args.source_guid = caster;
        args.source      = HandleFor(caster);
        args.spell_id    = spellId;
        for (uint32_t i = 0; i < count; ++i)
        {
            uint8_t reason = 0;
            if (!in.U64(args.target_guid) || !in.U8(reason) || (debug && !in.Skip(8))) return;
            args.target = HandleFor(args.target_guid);
            args.reason = reason;
            host->Raise(WXL_EV_CombatMiss, &args);
        }
    }
}

namespace wxl::seam::Combat::HandleSpellNonMeleeDamageLog
{
    void WXL_CALL Before(void* msg, int32_t time_offset)
    {
        (void)time_offset;

        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return;

        MessageReader          in(msg);
        wxl_combat_damage_args args{};
        uint8_t                school   = 0;
        uint8_t                periodic = 0;
        uint8_t                spare    = 0;
        if (!in.PackedGuid(args.target_guid) || !in.PackedGuid(args.source_guid) ||
            !in.U32(args.spell_id) || !in.U32(args.amount) || !in.U32(args.overkill) ||
            !in.U8(school) || !in.U32(args.absorbed) || !in.U32(args.resisted) || !in.U8(periodic) ||
            !in.U8(spare) || !in.U32(args.blocked) || !in.U32(args.hit_info))
            return;

        args.source = HandleFor(args.source_guid);
        args.target = HandleFor(args.target_guid);
        args.kind   = periodic ? 2 : 1;
        args.school = school;
        args.crit   = (args.hit_info & kSpellHitCrit) ? WXL_TRUE : WXL_FALSE;
        host->Raise(WXL_EV_CombatDamage, &args);
    }
}

namespace wxl::seam::Combat::HandleSpellHealLog
{
    void WXL_CALL Before(void* msg, int32_t time_offset)
    {
        (void)time_offset;

        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return;

        MessageReader        in(msg);
        wxl_combat_heal_args args{};
        uint8_t              crit = 0;
        if (!in.PackedGuid(args.target_guid) || !in.PackedGuid(args.source_guid) ||
            !in.U32(args.spell_id) || !in.U32(args.amount) || !in.U32(args.overheal) ||
            !in.U32(args.absorbed) || !in.U8(crit))
            return;

        args.source = HandleFor(args.source_guid);
        args.target = HandleFor(args.target_guid);
        args.crit   = crit ? WXL_TRUE : WXL_FALSE;
        host->Raise(WXL_EV_CombatHeal, &args);
    }
}

namespace wxl::seam::Combat::HandlePeriodicAuraLog
{
    void WXL_CALL Before(void* msg, int32_t time_offset)
    {
        (void)time_offset;

        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return;

        MessageReader in(msg);
        uint64_t      target  = 0;
        uint64_t      caster  = 0;
        uint32_t      spellId = 0;
        uint32_t      count   = 0;
        if (!in.PackedGuid(target) || !in.PackedGuid(caster) || !in.U32(spellId) || !in.U32(count))
            return;

        // The entries the client would read, counted on a copy first: up to an aura type it
        // stops at, and none when one is cut short.
        MessageReader probe = in;
        uint32_t      whole = 0;
        for (; whole < count; ++whole)
        {
            if (PeriodicEntry(nullptr, probe, caster, target, spellId)) continue;
            if (!probe.At()) return; // cut short inside an entry
            break;                   // an aura type the client stops at
        }
        for (uint32_t i = 0; i < whole; ++i) PeriodicEntry(host, in, caster, target, spellId);
    }
}
