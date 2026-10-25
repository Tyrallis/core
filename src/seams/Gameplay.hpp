// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// What the gameplay seams share: the host when one of their events has a listener, the reads of
// an object's descriptor block, an aura slot as an event's members, the local player's state of
// life and the windows an NPC opens. On both builds.

#pragma once

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <cstdint>
#include <initializer_list>

namespace wxl::core::seams::gameplay
{
    /// The host when a mod listens to at least one of @p events, null otherwise: a seam asks it
    /// first, so a gameplay event nobody listens to costs one load per event.
    wxl::host::ModHost* Listener(std::initializer_list<wxl_event> events);

    /// Dword @p index of @p object's descriptor block (update_fields.def numbering). False for
    /// a null object or one without a block.
    bool Field(const void* object, uint32_t index, uint32_t& out);

    /// The same for a two-dword value, low dword first: a guid.
    bool Field64(const void* object, uint32_t index, uint64_t& out);

    /// The client object of a guid, or null when the client holds none.
    void* ObjectOf(uint64_t guid);

    /// @p aura, an AuraData of @p unit or a copy of one, as an event's members; @p slot is the
    /// one the client names. The caster resolves to a handle only when it is in view. False,
    /// nothing to raise, for a unit the runtime does not track, as for UnitAuraChanged.
    bool Aura(void* unit, const void* aura, uint32_t slot, wxl_aura_args& out);

    /**
     * @brief Works out the local player's state of life and says which event its move is.
     *
     * Asked whenever the client re-tells the interface that state (GameUI.UpdateActivePlayer,
     * Player.UpdateGhostStatus): dead at no health, a ghost with its ghost flag, alive
     * otherwise. Answers WXL_EV_PlayerDied, WXL_EV_PlayerReleased or WXL_EV_PlayerResurrected,
     * with @p out naming the player, when the state differs from the last one seen, and
     * WXL_EV_COUNT otherwise; the first state seen in a world answers WXL_EV_COUNT. The caller
     * raises it.
     */
    wxl_event LifeChanged(wxl_unit_args& out);

    /// Forgets the local player's state of life and the open windows: the world is going.
    void Forget();

    /**
     * @brief Turns the client's interface events that open or close a window on an NPC into
     *        WindowOpened and WindowClosed.
     *
     * @ref Install reads the names of the event table the interface installs. @ref Signal is
     * asked with each event the client fires, one byte read for an event that is not a window's;
     * true when the event opens a window not open yet (@p opened set) or closes one that is,
     * with @p out the window and whom it is on. The caller raises it.
     */
    void Install(const char* const* names, uint32_t count);
    bool Signal(uint32_t index, bool& opened, wxl_window_args& out);
}
