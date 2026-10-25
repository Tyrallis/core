// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The Unit seams: what the runtime does at a unit's death and aura changes, on both builds.

#include "wxl/seams/seams.h"

#include "binding/Binding.hpp"
#include "common/Log.hpp"
#include "objects/Objects.hpp"
#include "runtime/Core.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <cstdint>

namespace
{
    /// The movement opcodes that start a movement, and the ones that end it; the rest (run
    /// mode, facing, heartbeat, acks, ...) raise nothing.
    bool IsMoveStart(uint32_t opcode)
    {
        switch (opcode)
        {
        case 181: // MSG_MOVE_START_FORWARD
        case 182: // MSG_MOVE_START_BACKWARD
        case 184: // MSG_MOVE_START_STRAFE_LEFT
        case 185: // MSG_MOVE_START_STRAFE_RIGHT
        case 187: // MSG_MOVE_JUMP
        case 188: // MSG_MOVE_START_TURN_LEFT
        case 189: // MSG_MOVE_START_TURN_RIGHT
        case 191: // MSG_MOVE_START_PITCH_UP
        case 192: // MSG_MOVE_START_PITCH_DOWN
        case 0x359: // MSG_MOVE_START_ASCEND
        case 0x3A7: // MSG_MOVE_START_DESCEND
            return true;
        default: return false;
        }
    }

    bool IsMoveStop(uint32_t opcode)
    {
        switch (opcode)
        {
        case 183: // MSG_MOVE_STOP
        case 186: // MSG_MOVE_STOP_STRAFE
        case 190: // MSG_MOVE_STOP_TURN
        case 193: // MSG_MOVE_STOP_PITCH
        case 201: // MSG_MOVE_FALL_LAND
        case 0x35A: // MSG_MOVE_STOP_ASCEND
            return true;
        default: return false;
        }
    }

    /// Raises UnitAuraChanged for a tracked unit; an untracked one is logged and skipped.
    void AuraChanged(void* self, int32_t spellId, wxl_bool removed)
    {
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return;

        wxl_unit_aura_args args{};
        args.unit = wxl::core::objects::HandleOf(self);
        if (args.unit == WXL_HANDLE_NONE)
        {
            WLOG_DEBUG(wxl::core::kTag, "aura change on an untracked unit; no event");
            return;
        }
        args.spell_id = static_cast<uint32_t>(spellId);
        args.removed  = removed;
        host->Raise(WXL_EV_UnitAuraChanged, &args);
    }
}

namespace wxl::seam::Unit::OnDeath
{
    void WXL_CALL After(void* self)
    {
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return;

        wxl_unit_args args{};
        args.unit = wxl::core::objects::HandleOf(self);
        if (args.unit == WXL_HANDLE_NONE)
        {
            WLOG_DEBUG(wxl::core::kTag, "death of an untracked unit; no event");
            return;
        }
        host->Raise(WXL_EV_UnitDeath, &args);
    }
}

namespace wxl::seam::Unit::OnAuraAdded
{
    void WXL_CALL After(void* self, uint32_t, uint8_t, void*, int32_t spell_id)
    {
        AuraChanged(self, spell_id, WXL_FALSE);
    }
}

namespace wxl::seam::Unit::OnAuraRemoved
{
    void WXL_CALL After(void* self, uint32_t, uint8_t, void*, int32_t spell_id)
    {
        AuraChanged(self, spell_id, WXL_TRUE);
    }
}

namespace wxl::seam::Unit::SendMovementUpdate
{
    void WXL_CALL Before(void* self, uint32_t, uint32_t msg_id, float, uint32_t, uint64_t, uint8_t)
    {
        const bool start = IsMoveStart(msg_id);
        if (!start && !IsMoveStop(msg_id)) return;

        wxl::host::ModHost* host = wxl::core::Host();
        if (!host || !self) return;

        wxl_player_move_args args{};
        args.unit      = wxl::core::objects::HandleOf(self);
        args.unit_guid = wxl::core::binding::object_guid(self);
        args.opcode    = msg_id;
        args.flags     = wxl::core::binding::unit_movement_flags(self);
        wxl::core::binding::object_position(self, &args.position);
        wxl::core::binding::object_facing(self, &args.facing);
        if (start) host->Raise(WXL_EV_PlayerMoveStart, &args);
        else host->Raise(WXL_EV_PlayerMoveStop, &args);
    }
}
