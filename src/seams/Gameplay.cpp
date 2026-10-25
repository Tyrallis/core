// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// What the gameplay seams share, on both builds: see Gameplay.hpp.

#include "seams/Gameplay.hpp"

#include "binding/Binding.hpp"
#include "objects/Objects.hpp"
#include "runtime/Core.hpp"

#include <cstring>
#include <vector>

namespace
{
    namespace binding = wxl::core::binding;

    constexpr uint32_t kPlayerFlags = 0x96; ///< PLAYER_FLAGS
    constexpr uint32_t kGhostFlag   = 0x10; ///< PLAYER_FLAGS_GHOST
    constexpr uint8_t  kHarmful     = 0x80; ///< the aura flag OnAuraAdded hands the combat log

    enum class Life : uint8_t
    {
        Unknown,
        Alive,
        Dead,
        Ghost,
    };

    Life g_life = Life::Unknown;

    /// What one interface event of the game's table means for a window: nothing (0), or the
    /// window it opens (kind + 1) or closes (kind + 1, with the high bit).
    constexpr uint8_t kCloses = 0x80;
    std::vector<uint8_t> g_window;

    /// Which windows the mods were told are open, and on whom.
    bool     g_open[16]{};
    uint64_t g_on[16]{};

    struct WindowName
    {
        const char*     name;
        wxl_window_kind kind;
        bool            closes;
    };

    constexpr WindowName kWindows[] = {
        { "GOSSIP_SHOW", WXL_WINDOW_GOSSIP, false },
        { "GOSSIP_CLOSED", WXL_WINDOW_GOSSIP, true },
        { "MERCHANT_SHOW", WXL_WINDOW_MERCHANT, false },
        { "MERCHANT_CLOSED", WXL_WINDOW_MERCHANT, true },
        { "TRAINER_SHOW", WXL_WINDOW_TRAINER, false },
        { "TRAINER_CLOSED", WXL_WINDOW_TRAINER, true },
        { "BANKFRAME_OPENED", WXL_WINDOW_BANK, false },
        { "BANKFRAME_CLOSED", WXL_WINDOW_BANK, true },
        { "AUCTION_HOUSE_SHOW", WXL_WINDOW_AUCTION, false },
        { "AUCTION_HOUSE_CLOSED", WXL_WINDOW_AUCTION, true },
        { "MAIL_SHOW", WXL_WINDOW_MAIL, false },
        { "MAIL_CLOSED", WXL_WINDOW_MAIL, true },
        { "TAXIMAP_OPENED", WXL_WINDOW_TAXI, false },
        { "TAXIMAP_CLOSED", WXL_WINDOW_TAXI, true },
        { "TRADE_SHOW", WXL_WINDOW_TRADE, false },
        { "TRADE_CLOSED", WXL_WINDOW_TRADE, true },
        { "QUEST_GREETING", WXL_WINDOW_QUEST, false },
        { "QUEST_DETAIL", WXL_WINDOW_QUEST, false },
        { "QUEST_PROGRESS", WXL_WINDOW_QUEST, false },
        { "QUEST_COMPLETE", WXL_WINDOW_QUEST, false },
        { "QUEST_FINISHED", WXL_WINDOW_QUEST, true },
        { "GUILDBANKFRAME_OPENED", WXL_WINDOW_GUILD_BANK, false },
        { "GUILDBANKFRAME_CLOSED", WXL_WINDOW_GUILD_BANK, true },
        { "PET_STABLE_SHOW", WXL_WINDOW_STABLE, false },
        { "PET_STABLE_CLOSED", WXL_WINDOW_STABLE, true },
        { "PETITION_SHOW", WXL_WINDOW_PETITION, false },
        { "PETITION_CLOSED", WXL_WINDOW_PETITION, true },
        { "OPEN_TABARD_FRAME", WXL_WINDOW_TABARD, false },
        { "CLOSE_TABARD_FRAME", WXL_WINDOW_TABARD, true },
    };

}

namespace wxl::core::seams::gameplay
{
    wxl::host::ModHost* Listener(std::initializer_list<wxl_event> events)
    {
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return nullptr;
        for (wxl_event event : events)
            if (host->Listening(event)) return host;
        return nullptr;
    }

    bool Field(const void* object, uint32_t index, uint32_t& out)
    {
        const void* block = object ? binding::object_descriptor(object) : nullptr;
        if (!block) return false;
        std::memcpy(&out, static_cast<const char*>(block) + index * 4u, sizeof out);
        return true;
    }

    bool Field64(const void* object, uint32_t index, uint64_t& out)
    {
        uint32_t low = 0, high = 0;
        if (!Field(object, index, low) || !Field(object, index + 1, high)) return false;
        out = (static_cast<uint64_t>(high) << 32) | low;
        return true;
    }

    void* ObjectOf(uint64_t guid)
    {
        return guid ? const_cast<void*>(binding::object_by_guid(guid)) : nullptr;
    }

    bool Aura(void* unit, const void* aura, uint32_t slot, wxl_aura_args& out)
    {
        out.unit = wxl::core::objects::HandleOf(unit);
        if (out.unit == WXL_HANDLE_NONE) return false;
        out.unit_guid = binding::object_guid(unit);
        out.slot      = slot;
        if (!aura) return true;
        out.spell_id        = static_cast<uint32_t>(binding::aura_spell_id(aura));
        out.caster_guid     = binding::aura_caster(aura);
        out.caster          = out.caster_guid ? wxl::core::objects::FromGuid(out.caster_guid) : WXL_HANDLE_NONE;
        out.stacks          = binding::aura_stacks(aura);
        out.previous_stacks = out.stacks;
        out.level           = binding::aura_level(aura);
        out.flags           = binding::aura_flags(aura);
        out.harmful         = (out.flags & kHarmful) ? WXL_TRUE : WXL_FALSE;
        const int32_t duration = binding::aura_duration(aura);
        const int32_t end      = binding::aura_end_time(aura);
        out.duration           = duration > 0 ? static_cast<uint32_t>(duration) : 0u;
        if (end > 0 && out.duration)
        {
            // The end time is OsGetAsyncTimeMs's clock; past it, nothing is left.
            const int32_t left = static_cast<int32_t>(static_cast<uint32_t>(end) - binding::os_time_ms());
            out.remaining      = left > 0 ? static_cast<uint32_t>(left) : 0u;
        }
        return true;
    }

    wxl_event LifeChanged(wxl_unit_args& out)
    {
        const void* player = binding::local_player_object();
        if (!player) return WXL_EV_COUNT;
        uint32_t flags = 0;
        Field(player, kPlayerFlags, flags);
        const Life now = binding::unit_health(player) <= 0 ? Life::Dead
                         : (flags & kGhostFlag)            ? Life::Ghost
                                                           : Life::Alive;
        const Life before = g_life;
        g_life            = now;
        if (before == Life::Unknown || before == now) return WXL_EV_COUNT;

        out.unit = wxl::core::objects::HandleOf(const_cast<void*>(player));
        if (now == Life::Dead) return WXL_EV_PlayerDied;
        return now == Life::Ghost ? WXL_EV_PlayerReleased : WXL_EV_PlayerResurrected;
    }

    void Forget()
    {
        g_life = Life::Unknown;
        for (bool& open : g_open) open = false;
    }

    void Install(const char* const* names, uint32_t count)
    {
        g_window.assign(count, 0);
        if (!names) return;
        for (uint32_t i = 0; i < count; ++i)
        {
            if (!names[i]) continue;
            for (const WindowName& window : kWindows)
                if (std::strcmp(names[i], window.name) == 0)
                    g_window[i] = static_cast<uint8_t>((window.kind + 1) | (window.closes ? kCloses : 0));
        }
    }

    bool Signal(uint32_t index, bool& opened, wxl_window_args& out)
    {
        if (index >= g_window.size() || !g_window[index]) return false;
        const uint8_t         entry  = g_window[index];
        const wxl_window_kind kind   = static_cast<wxl_window_kind>((entry & ~kCloses) - 1);
        const bool            closes = (entry & kCloses) != 0;
        if (closes ? !g_open[kind] : g_open[kind]) return false;

        uint64_t on = 0;
        if (closes) on = g_on[kind];
        else on = kind == WXL_WINDOW_TRADE ? binding::trade_partner() : binding::ui_interact_target();
        g_open[kind] = !closes;
        g_on[kind]   = on;

        opened       = !closes;
        out          = wxl_window_args{};
        out.kind     = kind;
        out.npc_guid = on;
        out.npc      = on ? wxl::core::objects::FromGuid(on) : WXL_HANDLE_NONE;
        return true;
    }
}
