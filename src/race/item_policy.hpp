//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2025 Nomagno
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 3
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

#ifndef HEADER_ITEM_POLICY_HPP
#define HEADER_ITEM_POLICY_HPP

#include <vector>
#include <cstdint>
#include <string>
#include "items/powerup_manager.hpp"
#include "items/item.hpp"

class AbstractKart;
class Kart;
class PowerupManager;


// Note during PR: Please make it prettier.

enum ItemPolicyRules {
    // Give items at section start: m_linear_mult X section_length
    IPT_LINEAR =         1 << 0,

    // Clear items at section start
    IPT_CLEAR =          1 << 1,

    // Give items at every lap of the section: m_items_per_lap
    IPT_GRADUAL =        1 << 2,

    // If previously given gradual items at start of new
    // lap were not fully spent, refill them instead of
    // blindly adding
    IPT_REPLENISH =      1 << 3,

    // For every lap in the section, if the amount
    // of items is above m_progressive_cap X remaining_section_length,
    // then cap them at m_progressive_penalty X remaining_section_length
    // (those two multipliers will usually be the same)
    IPT_PROGRESSIVE_CAP = 1 << 4,

    // When there are multiple items, at section start, and whenever items reach 0,
    // there will normally be an invokation of the random number generator with the
    // specified weights to decide the first item, and all items after will be of the same type.
    // If this bit is set to true, instead every lap where something is to be done, an invokation of the RNG will be made.
    IPT_OVERWRITE_ITEMS = 1 << 5,

    // Prevent cake & bowl hits between lappings and lappers from causing damage.
    // It helps with traffic, like blue flags in real motorsports.
    // The leader's section will be applied for everyone
    IPT_BLUE_FLAGS = 1 << 6,

    // Spawns/despawns bonus boxes.
    // The leader's section will be applied for everyone, or section 0 if it fails.
    IPT_FORBID_BONUSBOX = 1 << 7,

    // Spawns/despawns bananas.
    // The leader's section will be applied for everyone, or section 0 if it fails.
    IPT_FORBID_BANANA = 1 << 8,

    // Spawns/despawns nitro.
    // The leader's section will be applied for everyone, or section 0 if it fails.
    IPT_FORBID_NITRO = 1 << 9,

    // A "virtual pace car" procedure will be initiated at section start
    IPT_VIRTUAL_PACE = 1 << 10,

    // The "virtual pace car" procedure will let all karts fully unlap.
    IPT_UNLAPPING  =  1 << 11,

    // Bonus boxes will instead have their powerup pool draw from the item policy section's current one, rather than the one in powerup.xml.
    IPT_BONUS_BOX_OVERRIDE  =  1 << 12,

    // Provided weights for the powerup pool will be ignored, instead using automatic ones to try to attempt reasonable balance.
    IPT_AUTOMATIC_WEIGHTS =  1 << 13,

};

enum ItemPolicySectionBase
{
    IP_LAPS_BASED = 0,
    IP_TIME_BASED = 1
};

// An event can have several sections which each run by different rules.
// Item policies are only applied at the start of the race (lap 0), and at the start of every new lap.
struct ItemPolicySection {
    int m_section_type;

    // The key used to decide the current section,
    // can be a time in seconds or a lap
    int m_section_start; 

    // Bitstring of IPT_XX rule bits
    uint16_t m_rules;

    float m_linear_mult;
    float m_items_per_lap;
    float m_progressive_cap;
    float m_virtual_pace_gaps;

    // Which items can be handed out
    std::vector<PowerupManager::PowerupType> m_possible_types;
    std::vector<int> m_weight_distribution;
};

// Sections only have their start specified, so for instance if there is a section
// that starts in lap 1, and another that starts in lap 4, both would technically be applicable.
// The way this ambiguity is solved is, the section with the highest index that is still applicable is always used.
struct ItemPolicy {
    std::vector<ItemPolicySection> m_policy_sections;

    // Holds the section the leader is in.
    // -1 if this gamemode doesn't support leaders for some reason
    int m_leader_section;

    // Holds the status of the "virtual pace car" procedure:
    // code <= -3  : All karts must wait until the global clock hits second -([code] + 3) to start
    //                   the restart procedure. Afterwards, they must wait until second
    //                   -([code] + 3) + position_in_race*m_virtual_pace_gaps[m_leader_section]
    //                  to go back to normal running. This type of code is triggered by the last
    //                  kart when it starts slowing down.
    // code  = -2  : Slow down IMMEDIATELY and indefinitely (on next pass by finish line)
    // code  = -1  : Normal racing, remove all penalties.
    // code >=  0  : Slow down indefinitely when the kart finishes lap [code]
    int m_virtual_pace_code;

    // Number of karts that are ready to restart the race. Used to trigger the restart procedure.
    int m_restart_count;

    int selectItemFrom(const std::vector<PowerupManager::PowerupType>& types,
                       const std::vector<int>& weights);

    void applySectionRules (ItemPolicySection &section, AbstractKart *kart,
                            int next_section_start_laps, int current_lap,
                            int current_time, int prev_lap_item_amount);

    int applyRules(AbstractKart *kart, int current_lap, int current_time,
                   int total_laps_of_race);

    bool isHitValid(float sender_distance, float sender_lap, int sender_position, float recv_distance, int recv_position, float recv_lap, float track_length);

    int computeItemTicksTillReturn(ItemState::ItemType orig_type,
                                   ItemState::ItemType curr_type,
                                   int curr_type_respawn_ticks,
                                   int curr_ticks_till_return);

    void enforceVirtualPaceCarRulesForKart(Kart *k);

    void checkAndApplyVirtualPaceCarRules(AbstractKart *kart, int kart_section, int finished_laps);

    void fromString(std::string& str);
    std::string toString();
};

#endif // HEADER_ITEM_POLICY_HPP
