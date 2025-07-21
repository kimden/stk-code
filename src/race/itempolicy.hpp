//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 1950-2025 Nomagno
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

#ifndef HEADER_ITEMPOLICY_HPP
#define HEADER_ITEMPOLICY_HPP

#include <vector>
#include <cstdint>
#include <string>
#include "items/powerup_manager.hpp"

class Kart;
class PowerupManager;

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
    IPT_PROGRESSIVECAP = 1 << 4,

    // When there are multiple items, at section start, and whenever items reach 0,
    // there will normally be an invokation of the random number generator with the
    // specified weights to decide the first item, and all items after will be of the same type.
    // If this bit is set to true, instead every lap where something is to be done, an invokation of the RNG will be made.
    IPT_OVERWRITEITEMS = 1 << 5,

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
    IPT_VIRTUALPACE = 1 << 10,

    // The "virtual pace car" procedure will let all karts fully unlap.
    IPT_UNLAPPING  =  1 << 11
};



// An event can have several sections which each run by different rules.
// Item policies are only applied at the start of the race (lap 0), and at the start of every new lap.
struct ItemPolicySection {
    #define IP_TIME_BASED 1
    #define IP_LAPS_BASED 0
    int m_section_type;

    // The key used to decide the current section,
    // can be a time in seconds or a lap
    int m_section_start; 

    // Bitstring of IPT_XX rule bits
    uint16_t m_rules;

    float m_linear_mult;
    float m_items_per_lap;
    float m_progressive_cap;
    float m_virtualpace_gaps;
    float m_deg_mult;

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
    //                   -([code] + 3) + position_in_race*m_virtualpace_gaps[m_leader_section]
    //                  to go back to normal running. This type of code is triggered by the last
    //                  kart when it starts slowing down.
    // code  = -2  : Slow down IMMEDIATELY and indefinitely (on next pass by finish line)
    // code  = -1  : Normal racing, remove all penalties.
    // code >=  0  : Slow down indefinitely when the kart finishes lap [code]
    int m_virtualpace_code;
    // Number of karts that are ready to restart the race. Used to trigger the restart procedure.
    int m_restart_count;

    int selectItemFrom(std::vector<PowerupManager::PowerupType>& types,
                         std::vector<int>& weights);
    void applySectionRules (ItemPolicySection &section, Kart *kart,
                            int next_section_start_laps, int current_lap,
                            int current_time, int prev_lap_item_amount);

    int applyRules(Kart *kart, int current_lap, int current_time);
    void fromString(std::string& str);
    std::string toString();
};

#endif
