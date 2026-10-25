// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The Player seams: what the runtime does when a player's equipment or money changes and when
// the local player levels, on both builds.

#include "wxl/seams/seams.h"

#include "binding/Binding.hpp"
#include "objects/Objects.hpp"
#include "runtime/Core.hpp"
#include "seams/Gameplay.hpp"
#include "seams/Message.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>

namespace
{
    constexpr uint32_t kLevelUpInfo     = 0x1D4; ///< SMSG_LEVEL_UP_INFO
    constexpr uint32_t kItemPushResult  = 0x166; ///< SMSG_ITEM_PUSH_RESULT
    constexpr uint32_t kInventoryFailed = 0x112; ///< SMSG_INVENTORY_CHANGE_FAILURE

    constexpr uint32_t kLootMoneyNotify = 0x163; ///< SMSG_LOOT_MONEY_NOTIFY
    constexpr uint32_t kLootStartRoll   = 0x2A1; ///< SMSG_LOOT_START_ROLL
    constexpr uint32_t kLootRoll        = 0x2A2; ///< SMSG_LOOT_ROLL
    constexpr uint32_t kLootRollWon     = 0x29F; ///< SMSG_LOOT_ROLL_WON
    constexpr uint32_t kLootAllPassed   = 0x29E; ///< SMSG_LOOT_ALL_PASSED

    constexpr uint32_t kQuestList     = 0x185; ///< SMSG_QUESTGIVER_QUEST_LIST
    constexpr uint32_t kQuestDetails  = 0x188; ///< SMSG_QUESTGIVER_QUEST_DETAILS
    constexpr uint32_t kQuestRequest  = 0x18B; ///< SMSG_QUESTGIVER_REQUEST_ITEMS
    constexpr uint32_t kQuestReward   = 0x18D; ///< SMSG_QUESTGIVER_OFFER_REWARD
    constexpr uint32_t kQuestComplete = 0x191; ///< SMSG_QUESTGIVER_QUEST_COMPLETE

    constexpr uint32_t kQuestLogFirst = 0x0A; ///< PLAYER_QUEST_LOG_1_1, in dwords from the player's block
    constexpr uint32_t kQuestLogSlots = 25;
    constexpr uint32_t kUnitEnd       = 0x94; ///< where the player's block starts in the descriptor

    namespace gameplay = wxl::core::seams::gameplay;

    wxl_str Str(const char* text, uint32_t length) { return wxl_str{ text ? text : "", text ? length : 0u }; }

    /// The previous group list's identity and loot rules, for the staleness rule and the change.
    struct LastGroup
    {
        uint64_t group     = 0;
        uint32_t sequence  = 0;
        uint32_t when      = 0;
        bool     battle    = false;
        bool     known     = false;
        uint32_t method    = 0;
        uint64_t looter    = 0;
        uint32_t threshold = 0;
    };

    LastGroup g_group;
    LastGroup g_battleGroup;

    /// What Before decided for the call After closes: the function is not reentrant.
    struct Pending
    {
        void*    self    = nullptr;
        bool     changed = false;
        uint32_t itemId  = 0;
    };

    Pending g_pending;
}

namespace wxl::seam::Player::UpdateInventoryComponent
{
    void WXL_CALL Before(void* self, int32_t slot, const void* old_item)
    {
        g_pending = Pending{};
        if (!self || !old_item || slot < 0) return;

        int32_t current = 0;
        if (wxl::core::binding::player_visible_item_id(self, static_cast<uint32_t>(slot), &current) != WXL_TRUE)
            return;
        const int32_t previous = wxl::core::binding::visible_item_id(old_item);

        // The client's own test: the same id with the same sign is an enchant refresh, not a change.
        g_pending.self    = self;
        g_pending.changed = std::abs(previous) != std::abs(current) || (previous > 0) != (current > 0);
        g_pending.itemId  = static_cast<uint32_t>(std::abs(current));
    }

    void WXL_CALL After(void* self, int32_t slot, const void*)
    {
        const Pending pending = g_pending;
        g_pending             = Pending{};
        if (!pending.changed || pending.self != self) return;

        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return;

        wxl_equipment_args args{};
        args.unit = wxl::core::objects::HandleOf(self);
        if (args.unit == WXL_HANDLE_NONE) return;
        args.slot    = static_cast<uint32_t>(slot);
        args.item_id = pending.itemId;
        host->Raise(WXL_EV_EquipmentChanged, &args);
    }
}

namespace wxl::seam::Player::OnUpdateMoney
{
    void WXL_CALL Before(uint64_t guid, uint32_t, uint32_t, const void* value, void*)
    {
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host || !value) return;

        // The handler is registered on the local player's object alone, so the field read is
        // the local block's; the previous value is the one the mirror hands over by address.
        wxl_money_args args{};
        args.player = wxl::core::objects::FromGuid(guid);
        std::memcpy(&args.previous, value, sizeof args.previous);
        if (args.player != WXL_HANDLE_NONE)
            if (void* object = wxl::core::objects::Service().Handles().Resolve(args.player))
                args.current = wxl::core::binding::player_money(object);
        host->Raise(WXL_EV_PlayerMoneyChanged, &args);
    }
}

namespace wxl::seam::Player::Packet_Group_9
{
    void ItemPushed(void* msg)
    {
        wxl::host::ModHost* host = gameplay::Listener({ WXL_EV_ItemPushed });
        if (!host) return;
        wxl::core::seams::MessageReader in(msg);
        wxl_item_push_args args{};
        uint32_t received = 0, created = 0, shown = 0, property = 0;
        uint8_t  bag = 0;
        if (!in.U64(args.player_guid) || !in.U32(received) || !in.U32(created) || !in.U32(shown) || !in.U8(bag) ||
            !in.U32(args.slot) || !in.U32(args.item_id) || !in.U32(args.suffix_factor) || !in.U32(property) ||
            !in.U32(args.count) || !in.U32(args.total))
            return;
        args.bag                = bag;
        args.random_property_id = static_cast<int32_t>(property);
        args.received           = received ? WXL_TRUE : WXL_FALSE;
        args.created            = created ? WXL_TRUE : WXL_FALSE;
        args.announced          = shown ? WXL_TRUE : WXL_FALSE;
        host->Raise(WXL_EV_ItemPushed, &args);
    }

    void InventoryFailed(void* msg)
    {
        wxl::host::ModHost* host = gameplay::Listener({ WXL_EV_InventoryError });
        if (!host) return;
        wxl::core::seams::MessageReader in(msg);
        uint8_t result = 0;
        if (!in.U8(result) || !result) return;
        wxl_inventory_error_args args{};
        args.result = result;
        // The client reads the two items and the bag whatever the reason; a message cut short
        // there still says what was refused.
        uint8_t bag = 0;
        if (in.U64(args.item_guid) && in.U64(args.other_guid) && in.U8(bag) && (result == 1 || result == 87))
            if (!in.U32(args.required_level)) args.required_level = 0;
        host->Raise(WXL_EV_InventoryError, &args);
    }

    void WXL_CALL Before(void* param, uint32_t msg_id, uint32_t time, void* msg)
    {
        (void)param;
        (void)time;
        if (msg_id == kItemPushResult) return ItemPushed(msg);
        if (msg_id == kInventoryFailed) return InventoryFailed(msg);
        if (msg_id != kLevelUpInfo) return;

        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return;

        wxl::core::seams::MessageReader in(msg);
        wxl_level_up_args              args{};
        uint32_t                       power[7];
        uint32_t                       stats[5];
        if (!in.U32(args.level) || !in.U32(args.health)) return;
        for (uint32_t& amount : power)
            if (!in.U32(amount)) return;
        for (uint32_t& amount : stats)
            if (!in.U32(amount)) return;

        args.mana        = power[0];
        args.rage        = power[1];
        args.focus       = power[2];
        args.energy      = power[3];
        args.happiness   = power[4];
        args.runes       = power[5];
        args.runic_power = power[6];
        args.strength    = stats[0];
        args.agility     = stats[1];
        args.stamina     = stats[2];
        args.intellect   = stats[3];
        args.spirit      = stats[4];
        host->Raise(WXL_EV_PlayerLevelUp, &args);
    }
}

namespace wxl::seam::Player::PH_LOOT_OPCODES
{
    namespace
    {
        /// The head every roll message starts with: the loot's guid, the slot, the item.
        bool RollHead(wxl::core::seams::MessageReader& in, wxl_loot_roll_args& out, bool suffixFirst)
        {
            uint32_t first = 0, second = 0;
            if (!in.U64(out.source_guid) || !in.U32(out.slot) || !in.U32(out.item_id) || !in.U32(first) ||
                !in.U32(second))
                return false;
            // SMSG_LOOT_ROLL and SMSG_LOOT_ROLL_WON carry the suffix first, SMSG_LOOT_ALL_PASSED the
            // property first, as the client reads them.
            out.random_suffix      = suffixFirst ? first : second;
            out.random_property_id = static_cast<int32_t>(suffixFirst ? second : first);
            return true;
        }
    }

    void WXL_CALL Before(void* param, uint32_t msg_id, uint32_t time, void* msg)
    {
        (void)param;
        (void)time;
        wxl::core::seams::MessageReader in(msg);
        switch (msg_id)
        {
        case kLootMoneyNotify:
        {
            wxl::host::ModHost* host = gameplay::Listener({ WXL_EV_LootMoney });
            if (!host) return;
            wxl_loot_money_args args{};
            uint8_t alone = 0;
            if (!in.U32(args.amount) || !in.U8(alone) || !args.amount) return;
            args.alone = alone ? WXL_TRUE : WXL_FALSE;
            host->Raise(WXL_EV_LootMoney, &args);
            return;
        }
        case kLootStartRoll:
        {
            wxl::host::ModHost* host = gameplay::Listener({ WXL_EV_LootRollStart });
            if (!host) return;
            wxl_loot_roll_start_args args{};
            int32_t  map = 0;
            uint32_t property = 0;
            uint8_t  mask = 0;
            if (!in.U64(args.source_guid) || !in.U32(reinterpret_cast<uint32_t&>(map)) || !in.U32(args.slot) ||
                !in.U32(args.item_id) || !in.U32(args.random_suffix) || !in.U32(property) || !in.U32(args.quantity) ||
                !in.U32(args.duration) || !in.U8(mask))
                return;
            // The client keeps a roll for the map its interface shows, and drops the others.
            if (map != wxl::core::binding::ui_map_id()) return;
            args.random_property_id = static_cast<int32_t>(property);
            args.vote_mask          = mask;
            args.roll_id            = wxl::core::binding::loot_next_roll_id();
            host->Raise(WXL_EV_LootRollStart, &args);
            return;
        }
        case kLootRoll:
        {
            wxl::host::ModHost* host = gameplay::Listener({ WXL_EV_LootRoll });
            if (!host) return;
            wxl_loot_roll_args args{};
            uint64_t roller = 0;
            uint32_t item = 0, suffix = 0, property = 0;
            uint8_t  number = 0, type = 0, autoPass = 0;
            uint32_t slot = 0;
            if (!in.U64(args.source_guid) || !in.U32(slot) || !in.U64(roller) || !in.U32(item) || !in.U32(suffix) ||
                !in.U32(property) || !in.U8(number) || !in.U8(type) || !in.U8(autoPass))
                return;
            args.slot               = slot;
            args.player_guid        = roller;
            args.item_id            = item;
            args.random_suffix      = suffix;
            args.random_property_id = static_cast<int32_t>(property);
            args.roll               = number;
            args.roll_type          = type;
            args.auto_pass          = autoPass ? WXL_TRUE : WXL_FALSE;
            host->Raise(WXL_EV_LootRoll, &args);
            return;
        }
        case kLootRollWon:
        {
            wxl::host::ModHost* host = gameplay::Listener({ WXL_EV_LootRollWon });
            if (!host) return;
            wxl_loot_roll_args args{};
            uint8_t number = 0, type = 0;
            if (!RollHead(in, args, true) || !in.U64(args.player_guid) || !in.U8(number) || !in.U8(type)) return;
            args.roll      = number;
            args.roll_type = type;
            host->Raise(WXL_EV_LootRollWon, &args);
            return;
        }
        case kLootAllPassed:
        {
            wxl::host::ModHost* host = gameplay::Listener({ WXL_EV_LootAllPassed });
            if (!host) return;
            wxl_loot_roll_args args{};
            if (!RollHead(in, args, false)) return;
            args.roll_type = WXL_ROLL_PASS;
            host->Raise(WXL_EV_LootAllPassed, &args);
            return;
        }
        default: return;
        }
    }
}

namespace wxl::seam::Player::OnUpdateQuest
{
    void WXL_CALL Before(uint64_t guid, uint32_t offset, uint32_t count, const void* value, void* param)
    {
        (void)count;
        (void)param;
        wxl::host::ModHost* host =
            gameplay::Listener({ WXL_EV_QuestAccepted, WXL_EV_QuestRemoved, WXL_EV_QuestObjectiveChanged,
                                 WXL_EV_QuestCompleted, WXL_EV_QuestFailed });
        if (!host || !value || (offset & 3u)) return;
        const uint32_t dword = offset / 4u;
        if (dword < kQuestLogFirst || (dword - kQuestLogFirst) % 5u != 0) return;
        const uint32_t slot = (dword - kQuestLogFirst) / 5u;
        if (slot >= kQuestLogSlots) return;
        const void* player = gameplay::ObjectOf(guid);
        if (!player) return;

        // The entry as it was, handed over by address, and as the update left it.
        uint32_t before[5];
        std::memcpy(before, value, sizeof before);
        uint32_t now[5];
        for (uint32_t i = 0; i < 5; ++i)
            if (!gameplay::Field(player, kUnitEnd + dword + i, now[i])) return;

        const uint32_t oldQuest = before[0], newQuest = now[0];
        if (oldQuest && oldQuest != newQuest)
        {
            wxl_quest_args args{ oldQuest, slot };
            host->Raise(WXL_EV_QuestRemoved, &args);
        }
        // The client's own test for a quest new in the log: another quest, or a failed one
        // given back (its failed bit cleared).
        if (newQuest && (newQuest != oldQuest || (!(now[1] & 2u) && (before[1] & 2u))))
        {
            wxl_quest_args args{ newQuest, slot };
            host->Raise(WXL_EV_QuestAccepted, &args);
        }
        if (!newQuest || newQuest != oldQuest) return;

        for (uint32_t objective = 0; objective < 4; ++objective)
        {
            const uint32_t was = (before[2 + objective / 2] >> (16 * (objective % 2))) & 0xFFFFu;
            const uint32_t is  = (now[2 + objective / 2] >> (16 * (objective % 2))) & 0xFFFFu;
            if (was == is) continue;
            wxl_quest_objective_args args{ newQuest, slot, objective, was, is };
            host->Raise(WXL_EV_QuestObjectiveChanged, &args);
        }
        if ((now[1] & 1u) && !(before[1] & 1u))
        {
            wxl_quest_args args{ newQuest, slot };
            host->Raise(WXL_EV_QuestCompleted, &args);
        }
        if ((now[1] & 2u) && !(before[1] & 2u))
        {
            wxl_quest_args args{ newQuest, slot };
            host->Raise(WXL_EV_QuestFailed, &args);
        }
        return;
    }
}

namespace wxl::seam::Player::Packet_Group_12
{
    void WXL_CALL Before(void* param, uint32_t msg_id, uint32_t time, void* msg)
    {
        (void)param;
        (void)time;
        wxl::core::seams::MessageReader in(msg);
        if (msg_id == kQuestComplete)
        {
            wxl::host::ModHost* host = gameplay::Listener({ WXL_EV_QuestTurnedIn });
            if (!host) return;
            wxl_quest_reward_args args{};
            uint32_t money = 0;
            if (!in.U32(args.quest_id) || !in.U32(args.xp) || !in.U32(money) || !in.U32(args.honor) ||
                !in.U32(args.talents) || !in.U32(args.arena_points))
                return;
            args.money = static_cast<int32_t>(money);
            host->Raise(WXL_EV_QuestTurnedIn, &args);
            return;
        }
        if (msg_id != kQuestList && msg_id != kQuestDetails && msg_id != kQuestRequest && msg_id != kQuestReward)
            return;
        wxl::host::ModHost* host = gameplay::Listener(
            { WXL_EV_QuestGiverGreeting, WXL_EV_QuestGiverDetail, WXL_EV_QuestGiverProgress, WXL_EV_QuestGiverReward });
        if (!host) return;

        wxl_quest_giver_args args{};
        const char* title = nullptr;
        const char* text  = nullptr;
        uint32_t    titleLength = 0, textLength = 0;
        if (!in.U64(args.giver_guid)) return;
        args.title = Str(nullptr, 0);
        if (msg_id == kQuestList)
        {
            // The greeting, the emote's delay and id, then the quests' count.
            uint32_t delay = 0, emote = 0;
            uint8_t  count = 0;
            if (!in.CString(text, textLength) || !in.U32(delay) || !in.U32(emote) || !in.U8(count)) return;
            args.text  = Str(text, textLength);
            args.count = count;
        }
        else
        {
            if (msg_id == kQuestDetails && !in.U64(args.shared_by)) return;
            if (!in.U32(args.quest_id) || !in.CString(title, titleLength) || !in.CString(text, textLength)) return;
            args.title = Str(title, titleLength);
            args.text  = Str(text, textLength);
        }
        args.giver = wxl::core::objects::FromGuid(args.giver_guid);
        switch (msg_id)
        {
        case kQuestList: host->Raise(WXL_EV_QuestGiverGreeting, &args); break;
        case kQuestDetails: host->Raise(WXL_EV_QuestGiverDetail, &args); break;
        case kQuestRequest: host->Raise(WXL_EV_QuestGiverProgress, &args); break;
        default: host->Raise(WXL_EV_QuestGiverReward, &args); break;
        }
    }
}

namespace wxl::seam::Player::Packet_SMSG_GROUP_INVITE
{
    bool WXL_CALL Gate(int32_t* result, void** param, uint32_t* msg_id, uint32_t* time, void** msg)
    {
        (void)param;
        (void)msg_id;
        (void)time;
        wxl::host::ModHost* host = gameplay::Listener({ WXL_EV_GroupInvite });
        if (!host) return false;
        wxl::core::seams::MessageReader in(*msg);
        uint8_t     canAccept = 0;
        const char* name      = nullptr;
        uint32_t    length    = 0;
        if (!in.U8(canAccept) || !in.CString(name, length)) return false;

        wxl_bool              accept = WXL_FALSE, decline = WXL_FALSE;
        wxl_group_invite_args args{};
        args.inviter    = Str(name, length);
        args.can_accept = canAccept ? WXL_TRUE : WXL_FALSE;
        args.accept     = &accept;
        args.decline    = &decline;
        host->Raise(WXL_EV_GroupInvite, &args);
        // An invitation the player cannot take is only told; there is nothing to answer.
        if (!canAccept || (!accept && !decline)) return false;
        wxl::core::binding::group_invite_answer(decline ? WXL_FALSE : WXL_TRUE);
        *result = 1;
        return true;
    }
}

namespace wxl::seam::Player::Packet_SMSG_GROUP_LIST
{
    void WXL_CALL Before(void* param, uint32_t msg_id, uint32_t time, void* msg)
    {
        (void)param;
        (void)msg_id;
        (void)time;
        wxl::host::ModHost* host =
            gameplay::Listener({ WXL_EV_GroupUpdated, WXL_EV_GroupMember, WXL_EV_GroupLootMethodChanged });
        if (!host) return;
        wxl::core::seams::MessageReader in(msg);
        uint8_t  type = 0, subgroup = 0, assignments = 0, roles = 0, lfgFlags = 0;
        uint32_t lfgSlot = 0, sequence = 0, count = 0;
        wxl_group_args group{};
        if (!in.U8(type) || !in.U8(subgroup) || !in.U8(assignments) || !in.U8(roles)) return;
        if ((type & 0x8) && (!in.U8(lfgFlags) || !in.U32(lfgSlot))) return;
        if (!in.U64(group.group_guid) || !in.U32(sequence) || !in.U32(count)) return;

        // The client's rule: the same group, a sequence no newer, within a minute, is dropped.
        LastGroup& last = (type & 0x1) ? g_battleGroup : g_group;
        const uint32_t now = static_cast<uint32_t>(::time(nullptr));
        if (group.group_guid && sequence)
        {
            if (last.group == group.group_guid && last.when && last.sequence >= sequence &&
                static_cast<int32_t>(now - last.when - 60) < 0)
                return;
            last.group    = group.group_guid;
            last.sequence = sequence;
            last.when     = now;
        }

        // The members are read before anything is raised: a list cut short raises nothing.
        struct Member
        {
            const char* name;
            uint32_t    length;
            uint64_t    guid;
            uint8_t     online, subgroup, flags, roles;
        };
        std::vector<Member> members;
        members.reserve(count < 40 ? count : 40);
        for (uint32_t i = 0; i < count; ++i)
        {
            Member m{};
            if (!in.CString(m.name, m.length) || !in.U64(m.guid) || !in.U8(m.online) || !in.U8(m.subgroup) ||
                !in.U8(m.flags) || !in.U8(m.roles))
                return;
            members.push_back(m);
        }
        if (!in.U64(group.leader_guid)) return;
        uint8_t method = 0, threshold = 0, dungeon = 0, raid = 0, dynamic = 0;
        if (count && (!in.U8(method) || !in.U64(group.looter_guid) || !in.U8(threshold) || !in.U8(dungeon) ||
                      !in.U8(raid) || !in.U8(dynamic)))
            return;

        group.raid               = (type & 0x2) ? WXL_TRUE : WXL_FALSE;
        group.battleground       = (type & 0x1) ? WXL_TRUE : WXL_FALSE;
        group.count              = count;
        group.subgroup           = subgroup;
        group.loot_method        = method;
        group.loot_threshold     = threshold;
        group.dungeon_difficulty = dungeon;
        group.raid_difficulty    = raid;
        host->Raise(WXL_EV_GroupUpdated, &group);

        for (uint32_t i = 0; i < members.size(); ++i)
        {
            const Member&         m = members[i];
            wxl_group_member_args args{};
            args.index       = i;
            args.count       = count;
            args.member_guid = m.guid;
            args.member      = wxl::core::objects::FromGuid(m.guid);
            args.name        = Str(m.name, m.length);
            args.online      = m.online ? WXL_TRUE : WXL_FALSE;
            args.subgroup    = m.subgroup;
            args.flags       = m.flags;
            args.roles       = m.roles;
            host->Raise(WXL_EV_GroupMember, &args);
        }

        if (!count) return;
        const bool changed = !last.known || last.method != method || last.looter != group.looter_guid ||
                             last.threshold != threshold;
        last.known     = true;
        last.method    = method;
        last.looter    = group.looter_guid;
        last.threshold = threshold;
        if (!changed) return;
        wxl_group_loot_args loot{};
        loot.loot_method    = method;
        loot.looter_guid    = group.looter_guid;
        loot.loot_threshold = threshold;
        host->Raise(WXL_EV_GroupLootMethodChanged, &loot);
    }
}

namespace
{
    void GroupLeft(uint32_t reason)
    {
        g_group.known       = false;
        g_battleGroup.known = false;
        wxl::host::ModHost* host = gameplay::Listener({ WXL_EV_GroupLeft });
        if (!host) return;
        wxl_group_left_args args{};
        args.reason = reason;
        host->Raise(WXL_EV_GroupLeft, &args);
    }
}

namespace wxl::seam::Player::Packet_SMSG_GROUP_UNINVITE
{
    void WXL_CALL Before(void*, uint32_t, uint32_t, void*) { GroupLeft(WXL_GROUP_REMOVED); }
}

namespace wxl::seam::Player::Packet_SMSG_GROUP_DESTROYED
{
    void WXL_CALL Before(void*, uint32_t, uint32_t, void*) { GroupLeft(WXL_GROUP_DISBANDED); }
}

namespace wxl::seam::Player::Packet_SMSG_RESURRECT_REQUEST
{
    bool WXL_CALL Gate(int32_t* result, void** param, uint32_t* msg_id, uint32_t* time, void** msg)
    {
        (void)param;
        (void)msg_id;
        (void)time;
        wxl::host::ModHost* host = gameplay::Listener({ WXL_EV_ResurrectRequest });
        if (!host) return false;
        wxl::core::seams::MessageReader in(*msg);
        uint64_t    offerer = 0;
        const char* name    = nullptr;
        uint32_t    length  = 0;
        uint8_t     sickness = 0, useTimer = 0;
        if (!in.U64(offerer) || !in.SizedString(name, length) || !in.U8(sickness) || !in.U8(useTimer)) return false;

        wxl_bool                   accept = WXL_FALSE, decline = WXL_FALSE;
        wxl_resurrect_request_args args{};
        args.offerer_guid = offerer;
        args.offerer      = wxl::core::objects::FromGuid(offerer);
        args.name         = Str(name, length);
        args.sickness     = sickness ? WXL_TRUE : WXL_FALSE;
        args.accept       = &accept;
        args.decline      = &decline;
        host->Raise(WXL_EV_ResurrectRequest, &args);
        if (!accept && !decline) return false;
        wxl::core::binding::resurrect_answer(offerer, sickness, useTimer, decline ? WXL_FALSE : WXL_TRUE);
        *result = 1;
        return true;
    }
}

namespace wxl::seam::Player::Packet_SMSG_EMOTE
{
    void WXL_CALL Before(void* param, uint32_t msg_id, uint32_t time, void* msg)
    {
        (void)param;
        (void)msg_id;
        (void)time;
        wxl::host::ModHost* host = gameplay::Listener({ WXL_EV_Emote });
        if (!host) return;
        wxl::core::seams::MessageReader in(msg);
        wxl_emote_args args{};
        if (!in.U32(args.emote_id) || !in.U64(args.unit_guid)) return;
        args.unit = wxl::core::objects::FromGuid(args.unit_guid);
        host->Raise(WXL_EV_Emote, &args);
    }
}

namespace wxl::seam::Player::UpdateGhostStatus
{
    void WXL_CALL After(void* self)
    {
        (void)self;
        wxl_unit_args   args{};
        const wxl_event moved = gameplay::LifeChanged(args);
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host || moved == WXL_EV_COUNT) return;
        if (moved == WXL_EV_PlayerDied) host->Raise(WXL_EV_PlayerDied, &args);
        else if (moved == WXL_EV_PlayerReleased) host->Raise(WXL_EV_PlayerReleased, &args);
        else host->Raise(WXL_EV_PlayerResurrected, &args);
    }
}
