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

#include <iomanip>
#include <iostream>
#include <numbers>
#include <sstream>
#include <string>
#include <bitset>
#include <algorithm>
#include "race/race_manager.hpp"
#include "utils/types.hpp"
#include "utils/string_utils.hpp"
#include "race/itempolicy.hpp"
#include "items/powerup.hpp"
#include "items/powerup_manager.hpp"
#include "karts/kart.hpp"

int ItemPolicy::selectItemFrom(std::vector<PowerupManager::PowerupType>& types, std::vector<int>& weights) {
    if (types.size() != weights.size()) {
        Log::warn("ItemPolicy", "Mismatch in item policy section weights and types lists size\n");
        return -1;
    }
    int sum_of_weight = 0;

    for(int i = 0; i < types.size(); i++) {
        sum_of_weight += weights[i];
    }
    int rnd = rand() % sum_of_weight;
    for(int i=0; i < types.size(); i++) {
        if(rnd < weights[i])
            return i;
        rnd -= weights[i];
    }
    assert(!"This will never be reached\n");
}
//--------------------------------------------------
void ItemPolicy::applySectionRules(ItemPolicySection &section, AbstractKart *kart, int next_section_start_laps, int current_lap, int current_time, int prev_lap_item_amount) {
    if (section.m_section_type == IP_TIME_BASED) {
        Log::warn("ItemPolicy", "Time-implemented item policy sections are not implemented yet\n");
        return;
    }

    int section_start_lap = section.m_section_start;

    int curr_item_amount = kart->getPowerup()->getNum();
    PowerupManager::PowerupType curr_item_type = kart->getPowerup()->getType();

    int16_t rules = section.m_rules;
    bool overwrite = rules & ItemPolicyRules::IPT_OVERWRITEITEMS;
    bool linear_add = rules & ItemPolicyRules::IPT_LINEAR;
    bool linear_clear = rules & ItemPolicyRules::IPT_CLEAR;
    bool gradual_add = rules & ItemPolicyRules::IPT_GRADUAL;
    bool gradual_replenish = rules & ItemPolicyRules::IPT_REPLENISH;
    bool progressive_cap = rules & ItemPolicyRules::IPT_PROGRESSIVECAP;
    bool section_start = current_lap == section_start_lap;

    bool active_role = gradual_add || gradual_replenish;

    int amount_to_add = section_start
                                ? section.m_items_per_lap
                                : (prev_lap_item_amount - curr_item_amount);
    if (amount_to_add > section.m_items_per_lap)
        amount_to_add = section.m_items_per_lap;
    if (gradual_add && !gradual_replenish)
        amount_to_add = section.m_items_per_lap;
    if (!gradual_add) amount_to_add = 0;

    int remaining_laps = next_section_start_laps - current_lap;
    int amount_to_add_linear;
    if (section_start)
        amount_to_add_linear = linear_add ? section.m_linear_mult * remaining_laps : 0;
    else
        amount_to_add_linear = 0;

    int new_amount = curr_item_amount;
    if (section_start && linear_clear) new_amount = 0;
    new_amount += amount_to_add;
    new_amount += amount_to_add_linear;
    if (progressive_cap && (new_amount > section.m_progressive_cap*remaining_laps))
        new_amount = section.m_progressive_cap*remaining_laps;

    PowerupManager::PowerupType new_type = curr_item_type;

    bool item_is_valid = false;

    // If the list of weights is empty, then we take this to mean that any item type is correct.
    bool empty_weights = section.m_weight_distribution.size() == 0;
    if (!empty_weights) {
        auto found_item = std::find(section.m_weight_distribution.begin(),
                                    section.m_weight_distribution.end(),
                                    curr_item_type);
        item_is_valid = found_item != section.m_weight_distribution.end();
    }

    if (!empty_weights)
    {
        if ( ( section_start && (linear_clear || new_amount != 0))||
             (!section_start && !item_is_valid && active_role)    ||
              overwrite                                           ||
              new_amount == 0)
        {
            int index = selectItemFrom(section.m_possible_types, section.m_weight_distribution);
            if (index == -1) return;
            new_type = section.m_possible_types[index];
        }
    }

    // If the powerup type is NOTHING, the amount must be 0.
    // If the amount is 0, the powerup type must be NOTHING.
    if (new_amount == 0)
        new_type = PowerupManager::PowerupType::POWERUP_NOTHING;
    if (new_type == PowerupManager::PowerupType::POWERUP_NOTHING)
        new_amount = 0;

    if (new_type == curr_item_type) {
        // STK by default will add instead of overwriting items of the same type,
        // so we set it to 0 like this manually if that will happen.
        // Yes, this is stupid, but it's the only way without touching the whole codebase.
        kart->setPowerup(new_type, -curr_item_amount);
    }
    kart->setPowerup(new_type, new_amount);
}
//--------------------------------------------------
// Returns the section that was applied. Returns -1 if it tried to appply an unsupported section or if there are no sections
int ItemPolicy::applyRules(AbstractKart *kart, int current_lap, int current_time) {
    if (m_policy_sections.size() == 0) return -1;
    for (int i = 0; i < m_policy_sections.size(); i++) {
        int next_section_start_laps;
        int prev_lap_item_amount = kart->item_amount_last_lap;
        if (i+1 == m_policy_sections.size()) {
            next_section_start_laps = RaceManager::get()->getNumLaps();
            applySectionRules(m_policy_sections[i], kart, next_section_start_laps, current_lap, current_time, prev_lap_item_amount);
            return i;
            break;
        } else if (m_policy_sections[i].m_section_type == IP_TIME_BASED) {
            Log::warn("ItemPolicy", "Time-implemented item policy sections are not implemented yet\n");
            return i;
            break;
        } else if (m_policy_sections[i].m_section_type == IP_LAPS_BASED) {
            if (m_policy_sections[i+1].m_section_type == IP_TIME_BASED) {
                Log::warn("ItemPolicy", "Time-implemented item policy sections are not implemented yet\n");
                return i;
                break;
            } else if (m_policy_sections[i+1].m_section_type == IP_LAPS_BASED) {
                if (current_lap >= m_policy_sections[i].m_section_start &&
                    current_lap < m_policy_sections[i+1].m_section_start)
                {
                    next_section_start_laps = m_policy_sections[i+1].m_section_start;
                    applySectionRules(m_policy_sections[i], kart, next_section_start_laps, current_lap, current_time, prev_lap_item_amount);
                    return i;
                    break;
                } else {
                    ;
                }
            }
        }
    }
    return -1;
}
//--------------------------------------------------
static std::string fetch(std::vector<std::string>& strings, int idx) {
    if (idx >= strings.size())
        throw std::logic_error("Out of bounds in item policy parsing");
    return strings[idx];
}
//--------------------------------------------------
void ItemPolicy::fromString(std::string& input) {
    std::string normal_race_preset = "1 0 0000000000 0 0 0 0 0";
    std::string tt_preset = "1 0 0010000001 1 0 0 0 1 zipper 1";
    if (input.empty()) {
        fromString(normal_race_preset);
        return;
    }
    if (input == "normal" || input == "") {
        fromString(normal_race_preset);
        return;
    }
    if (input == "tt" || input == "timetrial" || input == "time-trial") {
        fromString(tt_preset);
        return;
    }
    std::vector<std::string> params = StringUtils::split(input, ' ');
    // Format can not form a valid policy with less than 10 space-separated parameters:
    // 1 0 0000000000 0 0 0 0 1 1 0
    // 1 section starting on lap 1 with no rules, all data to 0, and a length-0 item vector
    if (params.empty() || params.size() < 8) {
        fromString(normal_race_preset);
        return;
    }
    int cumlen = 0;

    int section_number;
    StringUtils::fromString(fetch(params, 0), section_number);
    if (section_number == 0) return;
    cumlen++;
    m_policy_sections.clear();
    for (int i = 0; i < section_number; i++) {
        ItemPolicySection tmp;
        tmp.m_section_type = IP_LAPS_BASED;
        StringUtils::fromString(fetch(params, cumlen), tmp.m_section_start);
        cumlen++;

        std::string bitstring = fetch(params, cumlen);
        cumlen++;

        tmp.m_rules = 0;
        for (unsigned j = 0; j < bitstring.size(); j++) {
            tmp.m_rules |= (bitstring[j] != '0') << (bitstring.size() - j - 1);
        }

        StringUtils::fromString(fetch(params, cumlen), tmp.m_linear_mult);
        cumlen++;

        StringUtils::fromString(fetch(params, cumlen), tmp.m_items_per_lap);
        cumlen++;

        StringUtils::fromString(fetch(params, cumlen), tmp.m_progressive_cap);
        cumlen++;

        StringUtils::fromString(fetch(params, cumlen), tmp.m_virtualpace_gaps);
        cumlen++;

        unsigned item_vector_length;
        StringUtils::fromString(fetch(params, cumlen), item_vector_length);
        cumlen++;

        for (int j = 0; j < item_vector_length; j++) {
            tmp.m_possible_types.push_back(PowerupManager::getPowerupType(fetch(params, cumlen)));
            cumlen++;

            tmp.m_weight_distribution.push_back(std::stoi(fetch(params, cumlen)));
            cumlen++;
        }
        if (item_vector_length != tmp.m_possible_types.size() ||
            item_vector_length != tmp.m_weight_distribution.size() ||
            tmp.m_possible_types.size() != tmp.m_weight_distribution.size()) {
                throw std::logic_error("Mismatched/wrong length item-weights list during item policy parsing");
        }
        m_policy_sections.push_back(tmp);
    }
}   // fromString
//--------------------------------------------------
std::string ItemPolicy::toString() {
    std::stringstream ss;
    ss << std::setprecision(4);
    ss << m_policy_sections.size() << " ";
    for (unsigned i = 0; i < m_policy_sections.size(); i++) {
        if (m_policy_sections[i].m_section_type == IP_TIME_BASED)
            return "TIME BASED SECTIONS NOT SUPPORTED YET. HOW DID YOU EVEN GET THIS INTO MEMORY??";
        ss << m_policy_sections[i].m_section_start << " ";
        std::string bs = std::bitset<12>(m_policy_sections[i].m_rules).to_string();
        ss << bs << " ";
        ss << m_policy_sections[i].m_linear_mult << " ";
        ss << m_policy_sections[i].m_items_per_lap << " ";
        ss << m_policy_sections[i].m_progressive_cap << " ";
        ss << m_policy_sections[i].m_virtualpace_gaps << " ";
        ss << m_policy_sections[i].m_possible_types.size() << " ";
        for (unsigned j = 0; j < m_policy_sections[i].m_possible_types.size(); j++) {
            ss << PowerupManager::getPowerupAsString(m_policy_sections[i].m_possible_types[j])
               << " ";
            ss << m_policy_sections[i].m_weight_distribution[j] << " ";
        }
    }
    return ss.str();
}
