#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <sstream>
#include <string>
#include <bitset>
#include "race/race_manager.hpp"
#include "utils/string_utils.hpp"
#include "race/itempolicy.hpp"
#include "items/powerup.hpp"
#include "items/powerup_manager.hpp"
#include "karts/kart.hpp"

int ItemPolicy::select_item_from(std::vector<PowerupManager::PowerupType> types, std::vector<int> weights) {
    if (types.size() != weights.size()) {
        printf("Mismatch in item policy section weights and types lists size\n");
        return PowerupManager::PowerupType::POWERUP_NOTHING;
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
void ItemPolicy::applySectionRules (ItemPolicySection &section, Kart *kart, int next_section_start_laps, int current_lap, int current_time, int prev_lap_item_amount) {
    if (section.m_section_type == IP_TIME_BASED) {
        printf("Time-implemented item policy sections are not implemented yet\n");
        return;
    }

    int curr_item_amount = kart->getPowerup()->getNum();
    PowerupManager::PowerupType curr_item_type = kart->getPowerup()->getType();

    int16_t rules = section.m_rules;
    bool overwrite = rules & ItemPolicyRules::IPT_OVERWRITEITEMS;
    bool linear_add = rules & ItemPolicyRules::IPT_LINEAR;
    bool linear_clear = rules & ItemPolicyRules::IPT_CLEAR;
    bool gradual_add = rules & ItemPolicyRules::IPT_GRADUAL;
    bool gradual_replenish = rules & ItemPolicyRules::IPT_REPLENISH;
    bool progressive_cap = rules & ItemPolicyRules::IPT_PROGRESSIVECAP;
    bool section_start = current_lap == section.m_section_start;

    int amount_to_add = section_start
                                ? section.m_items_per_lap
                                : (prev_lap_item_amount - curr_item_amount);
    if (amount_to_add > section.m_items_per_lap)
        amount_to_add = section.m_items_per_lap;
    if (gradual_add && !gradual_replenish)
        amount_to_add = section.m_items_per_lap;

    int remaining_laps = (next_section_start_laps - section.m_section_start);
    int amount_to_add_linear;
    if (section_start)
        amount_to_add_linear = linear_add ? section.m_linear_mult * remaining_laps : 0;
    else
        amount_to_add_linear = 0;

    int new_amount = curr_item_amount;
    if (section_start && linear_clear) new_amount = 0;
    new_amount += amount_to_add;
    new_amount += amount_to_add_linear;
    if (progressive_cap && new_amount > section.m_progressive_cap*remaining_laps)
        new_amount = section.m_progressive_penalty*remaining_laps;

    PowerupManager::PowerupType new_type = curr_item_type;
    if (section_start || overwrite || new_amount == 0) {
        int index = select_item_from(section.m_possible_types, section.m_weight_distribution);;
        new_type = section.m_possible_types[index];
    }
    if (new_amount == 0) new_type = PowerupManager::PowerupType::POWERUP_NOTHING;
    kart->setPowerup(new_type, new_amount);
}
void ItemPolicy::applyRules(Kart *kart, int current_lap, int current_time) {
    for (int i = 0; i < m_policy_sections.size(); i++) {
        int next_section_start_laps;
        int prev_lap_item_amount = 0;
        if (i+1 == m_policy_sections.size()) {
            next_section_start_laps = RaceManager::get()->getNumLaps(); 
            applySectionRules(m_policy_sections[i], kart, next_section_start_laps, current_lap, current_time, prev_lap_item_amount);
        } else if (m_policy_sections[i].m_section_type == IP_TIME_BASED) {
            printf("Time-implemented item policy sections are not implemented yet\n");
        } else if (m_policy_sections[i].m_section_type == IP_LAPS_BASED) {
            if (m_policy_sections[i+1].m_section_type == IP_TIME_BASED) {
                printf("Time-implemented item policy sections are not implemented yet\n");
            } else if (m_policy_sections[i+1].m_section_type == IP_LAPS_BASED) {
                if (current_lap > m_policy_sections[i].m_section_start &&
                    current_lap < m_policy_sections[i+1].m_section_start)
                {
                    next_section_start_laps = m_policy_sections[i+1].m_section_start;
                    applySectionRules(m_policy_sections[i], kart, next_section_start_laps, current_lap, current_time, prev_lap_item_amount);
                }
            }
        }
    }
}

static std::string fetch(std::vector<std::string> strings, int idx) {
    if (idx >= strings.size()) throw std::logic_error("Out of bounds in item policy parsing");
    return strings[idx];
}

void ItemPolicy::fromString(std::string input) {
    std::string normal_race_preset = "1 0 000000 0 0 0 0 1 nothing 0";
    std::string tt_preset = "1 0 000001 1 0 0 0 1 zipper 1";
    if (input.empty()) {
        fromString(normal_race_preset);
        return;
    }
    if (input == "normal") {
        fromString(normal_race_preset);
        return;
    }
    if (input == "tt" || input == "timetrial") {
        fromString(tt_preset);
        return;
    }
    std::vector<std::string> params = StringUtils::split(input, ' ');
    if (params.empty()) {
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

        StringUtils::fromString(fetch(params, cumlen), tmp.m_progressive_penalty);
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

std::string ItemPolicy::toString() {
    std::stringstream ss;
    ss << std::setprecision(4);
    ss << m_policy_sections.size() << " ";
    for (unsigned i = 0; i < m_policy_sections.size(); i++) {
        if (m_policy_sections[i].m_section_type == IP_TIME_BASED)
            return "TIME BASED SECTIONS NOT SUPPORTED YET. HOW DID YOU EVEN GET THIS INTO MEMORY??";
        ss << m_policy_sections[i].m_section_start << " ";
        std::string bs = std::bitset<8>(m_policy_sections[i].m_rules).to_string();
        ss << bs << " ";
        ss << m_policy_sections[i].m_linear_mult << " ";
        ss << m_policy_sections[i].m_items_per_lap << " ";
        ss << m_policy_sections[i].m_progressive_cap << " ";
        ss << m_policy_sections[i].m_progressive_penalty << " ";
        ss << m_policy_sections[i].m_possible_types.size() << " ";
        for (unsigned j = 0; j < m_policy_sections[i].m_possible_types.size(); j++) {
            ss << PowerupManager::getPowerupAsString(m_policy_sections[i].m_possible_types[j])
               << " ";
            ss << m_policy_sections[i].m_weight_distribution[j] << " ";
        }
    }
    return ss.str();
}
