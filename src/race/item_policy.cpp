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

#include <iomanip>
#include <iostream>
#include <bitset>

#include "race/item_policy.hpp"

#include "race/race_manager.hpp"
#include "utils/types.hpp"
#include "utils/string_utils.hpp"
#include "modes/world.hpp"
#include "items/item.hpp"
#include "items/powerup.hpp"
#include "items/powerup_manager.hpp"
#include "karts/kart.hpp"
#include "karts/max_speed.hpp"
#include "config/stk_config.hpp"

int ItemPolicy::selectItemFrom(const std::vector<PowerupManager::PowerupType>& types,
                               const std::vector<int>& weights)
{
    if (types.size() != weights.size())
    {
        Log::error("ItemPolicy", "Mismatch in item policy section weights and types lists size");
        return -1;
    }
    int sum_of_weight = 0;

    for (size_t i = 0; i < types.size(); ++i)
        sum_of_weight += weights[i];

    int rnd = rand() % sum_of_weight;
    for (size_t i = 0; i < types.size(); ++i)
    {
        if (rnd < weights[i])
            return i;
        rnd -= weights[i];
    }

    throw std::logic_error("No item selected from weighted list (this code path should be unreachable)");
}   // selectItemFrom
//-----------------------------------------------------------------------------

void ItemPolicy::applySectionRules(
        ItemPolicySection &section, AbstractKart *kart,
        int next_section_start_laps, int current_lap,
        int current_time, int prev_lap_item_amount)
{
    if (section.m_section_type == IP_TIME_BASED)
    {
        Log::error("ItemPolicy", "Time-implemented item policy sections are not implemented yet");
        return;
    }

    int section_start_lap = section.m_section_start;

    int curr_item_amount = kart->getPowerup()->getNum();
    PowerupManager::PowerupType curr_item_type = kart->getPowerup()->getType();

    int16_t rules = section.m_rules;
    bool overwrite = rules & ItemPolicyRules::IPT_OVERWRITE_ITEMS;
    bool linear_add = rules & ItemPolicyRules::IPT_LINEAR;
    bool linear_clear = rules & ItemPolicyRules::IPT_CLEAR;
    bool gradual_add = rules & ItemPolicyRules::IPT_GRADUAL;
    bool gradual_replenish = rules & ItemPolicyRules::IPT_REPLENISH;
    bool progressive_cap = rules & ItemPolicyRules::IPT_PROGRESSIVE_CAP;
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


    PowerupManager::PowerupType new_type = curr_item_type;

    bool item_is_valid = false;

    // If the list of weights is empty, then we take this to mean that any item type is correct.
    bool empty_weights = section.m_weight_distribution.size() == 0;
    if (!empty_weights)
    {
        auto found_item = std::find(section.m_possible_types.begin(),
                                    section.m_possible_types.end(),
                                    curr_item_type);
        item_is_valid = found_item != section.m_possible_types.end();
    } else {
        item_is_valid = true;
    }

    int new_amount = curr_item_amount;

    if (!item_is_valid)
        new_amount = 0;

    if (section_start && linear_clear)
        new_amount = 0;

    new_amount += amount_to_add;
    new_amount += amount_to_add_linear;
    if (progressive_cap && (new_amount > section.m_progressive_cap*remaining_laps))
        new_amount = section.m_progressive_cap*remaining_laps;

    if (!empty_weights)
    {
        bool selecting_item = overwrite || new_amount == 0;
        selecting_item |= (section_start && (linear_clear || new_amount != 0));
        selecting_item |= (!section_start && !item_is_valid && active_role);

        if (selecting_item)
        {
            int index = selectItemFrom(section.m_possible_types, section.m_weight_distribution);
            if (index == -1)
                return;
            new_type = section.m_possible_types[index];
        }
    }

    // If the powerup type is NOTHING, the amount must be 0.
    // If the amount is 0, the powerup type must be NOTHING.
    if (new_amount == 0)
        new_type = PowerupManager::PowerupType::POWERUP_NOTHING;
    if (new_type == PowerupManager::PowerupType::POWERUP_NOTHING)
        new_amount = 0;

    if (new_type == curr_item_type)
    {
        // STK by default will add instead of overwriting items of the same type,
        // so we set it to 0 like this manually if that will happen.
        // Yes, this is stupid, but it's the only way without touching the whole codebase.
        kart->setPowerup(new_type, -curr_item_amount);
    }
    kart->setPowerup(new_type, new_amount);
}   // applySectionRules
//-----------------------------------------------------------------------------

// Returns the section that was applied. Returns -1 if it tried to appply an unsupported section or if there are no sections
int ItemPolicy::applyRules(AbstractKart *kart, int current_lap, int current_time, int total_laps_of_race)
{
    if (m_policy_sections.size() == 0) return -1;
    for (unsigned i = 0; i < m_policy_sections.size(); i++)
    {
        int next_section_start_laps = total_laps_of_race;
        int prev_lap_item_amount = kart->m_item_amount_last_lap;
        if (i + 1 == m_policy_sections.size())
        {
            next_section_start_laps = RaceManager::get()->getNumLaps();
            applySectionRules(m_policy_sections[i], kart, next_section_start_laps, current_lap, current_time, prev_lap_item_amount);
            return i;
            break;
        }
        else if (m_policy_sections[i].m_section_type == IP_TIME_BASED)
        {
            Log::error("ItemPolicy", "Time-implemented item policy sections are not implemented yet");
            return i;
            break;
        }
        else if (m_policy_sections[i].m_section_type == IP_LAPS_BASED)
        {
            if (m_policy_sections[i + 1].m_section_type == IP_TIME_BASED) 
            {
                Log::error("ItemPolicy", "Time-implemented item policy sections are not implemented yet");
                return i;
                break;
            }
            else if (m_policy_sections[i + 1].m_section_type == IP_LAPS_BASED)
            {
                if (current_lap >= m_policy_sections[i].m_section_start &&
                    current_lap < m_policy_sections[i + 1].m_section_start)
                {
                    next_section_start_laps = m_policy_sections[i + 1].m_section_start;
                    applySectionRules(m_policy_sections[i], kart, next_section_start_laps, current_lap, current_time, prev_lap_item_amount);
                    return i;
                    break;
                }
                else
                    continue;
            }
        }
    }
    return -1;
}   // applyRules
//-----------------------------------------------------------------------------

static std::string fetch(std::vector<std::string>& strings, unsigned idx)
{
    if (idx >= strings.size())
        throw std::logic_error("Out of bounds in item policy parsing");

    return strings[idx];
}   // fetch
//-----------------------------------------------------------------------------

void ItemPolicy::fromString(std::string& input)
{
    std::string normal_race_preset = "1 0 0000000000 0 0 0 0 0 0 0";
    std::string tt_preset = "1 0 0010000001 1 0 0 0 0 0 1 zipper 1";
    if (input.empty())
    {
        fromString(normal_race_preset);
        return;
    }
    if (input == "normal" || input == "")
    {
        fromString(normal_race_preset);
        return;
    }
    if (input == "tt" || input == "timetrial" || input == "time-trial")
    {
        fromString(tt_preset);
        return;
    }
    std::vector<std::string> params = StringUtils::split(input, ' ');
    // Format can not form a valid policy with less than 10 space-separated parameters:
    // 1 0 0000000000 0 0 0 0 0 0 0 
    // 1 section starting on lap 1 with no rules, all data to 0, no overridden stop time, and a length-0 item vector
    if (params.empty() || params.size() < 8)
    {
        fromString(normal_race_preset);
        return;
    }
    int idx = 0;

    auto retrieve_float = [&idx, &params](float &x)
    {
        StringUtils::fromString(fetch(params, idx), x);
        idx += 1;
    };
    auto retrieve_int = [&idx, &params](int &x)
    {
        StringUtils::fromString(fetch(params, idx), x);
        idx += 1;
    };
    auto retrieve_uint = [&idx, &params](unsigned &x)
    {
        StringUtils::fromString(fetch(params, idx), x);
        idx += 1;
    };


    unsigned section_number = 0;
    retrieve_uint(section_number);
    if (section_number == 0) return;

    m_policy_sections.clear();
    for (unsigned i = 0; i < section_number; i++)
    {
        ItemPolicySection tmp;

        tmp.m_section_type = IP_LAPS_BASED;
        retrieve_int(tmp.m_section_start);

        std::string bitstring = fetch(params, idx);
        idx++;

        tmp.m_rules = 0;
        for (unsigned j = 0; j < bitstring.size(); j++)
        {
            tmp.m_rules |= (bitstring[j] != '0') << (bitstring.size() - j - 1);
        }

        retrieve_float(tmp.m_linear_mult);
        retrieve_float(tmp.m_items_per_lap);
        retrieve_float(tmp.m_progressive_cap);
        retrieve_float(tmp.m_virtual_pace_gaps);

        unsigned item_vector_length = 0;
        retrieve_uint(item_vector_length);

        for (unsigned j = 0; j < item_vector_length; j++)
        {
            tmp.m_possible_types.push_back(PowerupManager::getPowerupType(fetch(params, idx)));
            idx++;

            tmp.m_weight_distribution.push_back(std::stoi(fetch(params, idx)));
            idx++;
        }
        if (item_vector_length != tmp.m_possible_types.size() ||
                item_vector_length != tmp.m_weight_distribution.size() ||
                tmp.m_possible_types.size() != tmp.m_weight_distribution.size())
        {
            throw std::logic_error("Mismatched length of item and weights lists during item policy parsing");
        }
        m_policy_sections.push_back(tmp);
    }
}   // fromString
//-----------------------------------------------------------------------------

std::string ItemPolicy::toString()
{
    std::stringstream ss;
    ss << std::setprecision(4);
    ss << m_policy_sections.size() << " ";
    for (unsigned i = 0; i < m_policy_sections.size(); i++)
    {
        if (m_policy_sections[i].m_section_type == IP_TIME_BASED)
        {
            Log::error("ItemPolicy", "Can't print time based section data because time based sections are not supported yet");
            return "Time based sections not supported yet";
        }
        ss << m_policy_sections[i].m_section_start << " ";
        std::string bs = std::bitset<16>(m_policy_sections[i].m_rules).to_string();
        ss << bs << " ";
        ss << m_policy_sections[i].m_linear_mult << " ";
        ss << m_policy_sections[i].m_items_per_lap << " ";
        ss << m_policy_sections[i].m_progressive_cap << " ";
        ss << m_policy_sections[i].m_virtual_pace_gaps << " ";
        ss << m_policy_sections[i].m_possible_types.size() << " ";
        for (unsigned j = 0; j < m_policy_sections[i].m_possible_types.size(); j++)
        {
            ss << PowerupManager::getPowerupAsString(m_policy_sections[i].m_possible_types[j])
               << " ";
            ss << m_policy_sections[i].m_weight_distribution[j] << " ";
        }
    }
    return ss.str();
}   // toString
//-----------------------------------------------------------------------------

static bool isKartUnderVirtualPaceCarSlowdown(ItemPolicy *self, int position) {
    bool start_of_race_vpc = self->m_leader_section <= -1 && (self->m_policy_sections[0].m_rules & ItemPolicyRules::IPT_VIRTUAL_PACE);
    // Not in a virtual pace car yet, but since it is on the start of the race, this is done to prevent overtaking
    if (start_of_race_vpc)
        return true;

    bool is_restart = self->m_virtual_pace_code <= -3;
    bool did_restart = false;
    if (is_restart) {
        int restart_time = -(self->m_virtual_pace_code + 3);
        float gap = self->m_policy_sections[self->m_leader_section].m_virtual_pace_gaps;
        gap *= position;
        restart_time += gap;
        int current_time = World::getWorld()->getTime();
        if (current_time > restart_time) {
            // Set slowdown time to 0 (disable it) if its time to restart
            did_restart = true;
        }
    }
    if (is_restart && !did_restart)
        return true;
    else
        return false;
}


bool ItemPolicy::isHitValid(float sender_distance, float sender_lap, int sender_position, float recv_distance, int recv_position, float recv_lap, float track_length) {
    int leader_section_idx = m_leader_section;
    // If leader is not in a valid section, allow the hit
    if (leader_section_idx <= -1)
        return true;
    // If blue flags are not enabled, ALSO allow the hit
    if (!(m_policy_sections[leader_section_idx].m_rules & ItemPolicyRules::IPT_BLUE_FLAGS))
        return true;

    // If one of the karts is under a virtual pace car restart, forbid the hit
    if (isKartUnderVirtualPaceCarSlowdown(this, sender_position) || isKartUnderVirtualPaceCarSlowdown(this, recv_position))
        return false;

    //float minimum_distance_empirical = 200.0f;

    // for too short tracks we instead take 1/5th of the track
    //if (track_length < 750.0f)
    //    minimum_distance_empirical = track_length / 5.0f;

    float distance_normal = std::fabs(sender_distance - recv_distance);
    float distance_complimentary = track_length - distance_normal;

    bool across_finish_line;
    bool forwards_throw;
    if (distance_complimentary < distance_normal)
    {
        across_finish_line = true;
        if (sender_distance > recv_distance)
            forwards_throw = true;
        else
            forwards_throw = false;
    }
    else
        across_finish_line = false;

    // if the distance is less than 5% from half the track length,
    // it is nonsense to try to predict if the hit is across the finish line
    if (distance_normal / track_length > 0.45 && distance_normal / track_length < 0.55)
        across_finish_line = false;

    bool hit_is_valid;
    // sender with a 1 lap difference whose distance is less than an empirical number are almost certainly hitting each other across the start/finish line
    if (across_finish_line && forwards_throw)
        hit_is_valid = (recv_lap - sender_lap) == 1;
    else if (across_finish_line && !forwards_throw)
        hit_is_valid = (sender_lap - recv_lap) == 1;
    else
        hit_is_valid = sender_lap == recv_lap;

    return hit_is_valid;
}   // isHitValid
//-----------------------------------------------------------------------------

// Returns the amount of ticks till return to set for the current situation
int ItemPolicy::computeItemTicksTillReturn(
        ItemState::ItemType orig_type,
        ItemState::ItemType curr_type,
        int curr_type_respawn_ticks, int curr_ticks_till_return)
{
    int current_section = m_leader_section;

    if (current_section <= -1)
        current_section = 0;

    uint16_t rules_curr = m_policy_sections[current_section].m_rules;
    uint16_t rules_prev;
    if (current_section > 0)
        rules_prev = m_policy_sections[current_section-1].m_rules;
    else
        rules_prev = rules_curr;

    // Note during PR: This is ridiculously ugly.
    // Please come up with a better way as I don't want to.
    bool was_gum = (orig_type==ItemState::ItemType::ITEM_BUBBLEGUM) || (curr_type==ItemState::ItemType::ITEM_BUBBLEGUM_NOLOK);

    bool is_nitro = (curr_type==ItemState::ItemType::ITEM_NITRO_SMALL) || (curr_type==ItemState::ItemType::ITEM_NITRO_BIG);
    bool was_nitro = (orig_type==ItemState::ItemType::ITEM_NITRO_SMALL) || (orig_type==ItemState::ItemType::ITEM_NITRO_BIG);

    bool forbid_prev = ((rules_prev & ItemPolicyRules::IPT_FORBID_BONUSBOX) && curr_type==ItemState::ItemType::ITEM_BONUS_BOX) ||
                      ((rules_prev & ItemPolicyRules::IPT_FORBID_BANANA) && curr_type==ItemState::ItemType::ITEM_BANANA)      ||
                      ((rules_prev & ItemPolicyRules::IPT_FORBID_NITRO) && (is_nitro || was_nitro));

    bool forbid_curr = ((rules_curr & ItemPolicyRules::IPT_FORBID_BONUSBOX) && curr_type==ItemState::ItemType::ITEM_BONUS_BOX) ||
                      ((rules_curr & ItemPolicyRules::IPT_FORBID_BANANA) && curr_type==ItemState::ItemType::ITEM_BANANA)      ||
                      ((rules_curr & ItemPolicyRules::IPT_FORBID_NITRO) && (is_nitro || was_nitro));


    auto& stk_config = STKConfig::get();
    int new_ticks_till_return = curr_ticks_till_return;
    // There's redundant cases here, but it is like this for maintainability
    if (forbid_prev && forbid_curr)
        new_ticks_till_return = stk_config->time2Ticks(99999);
    else if (!forbid_prev && forbid_curr)
        new_ticks_till_return = stk_config->time2Ticks(99999);
    else if (forbid_prev && !forbid_curr)
    {
        int respawn_ticks = curr_type_respawn_ticks;
        // If the ticks till return are abnormally high, set them back to normal.
        // If we don't do it like this, it will set the ticks till return perpetually
        // when transitioning from a section without to a section with this item type allowed.
        if (curr_ticks_till_return > 10 * respawn_ticks)
            new_ticks_till_return = respawn_ticks;
    }
    else if (!forbid_prev && !forbid_curr)
    {
        // Nothing to do
        // This wouldn't be needed normally, but we do it in case of switched items
        int respawn_ticks = curr_type_respawn_ticks;
        if (curr_ticks_till_return > 10 * respawn_ticks && curr_type != ItemState::ItemType::ITEM_EASTER_EGG)
            new_ticks_till_return = respawn_ticks;
    }    // Gums that were switched into nitro are NEVER forbidden

    bool instant = (was_gum && is_nitro);
    if (instant)
        new_ticks_till_return = 0;

    return new_ticks_till_return;
}   // computeItemTicksTillReturn
//-----------------------------------------------------------------------------

void ItemPolicy::enforceVirtualPaceCarRulesForKart(Kart *kart) {
    auto& stk_config = STKConfig::get();

    bool start_of_race_vpc = m_leader_section <= -1 && (m_policy_sections[0].m_rules & ItemPolicyRules::IPT_VIRTUAL_PACE);
    // Not in a virtual pace car yet, but since it is on the start of the race, this is done to prevent overtaking
    if (start_of_race_vpc) {
        kart->setSlowdown(MaxSpeed::MS_DECREASE_BUBBLE, 0.1f, stk_config->time2Ticks(0.1f), -1);
        return;
    }

    bool is_restart = m_virtual_pace_code <= -3;
    bool did_restart = false;
    if (is_restart) {
        // Reaffirm the penalty in case someone tried to be funny and hit a gum in the middle of a safety car restart to shorten their penalty
        kart->setSlowdown(MaxSpeed::MS_DECREASE_BUBBLE, 0.1f, stk_config->time2Ticks(0.1f), -1);
        int restart_time = -(m_virtual_pace_code + 3);
        float gap = m_policy_sections[m_leader_section].m_virtual_pace_gaps;
        gap *= kart->getPosition();
        restart_time += gap;
        int current_time = World::getWorld()->getTime();
        if (current_time > restart_time)
        {
            // Set slowdown time to 0 (disable it) if its time to restart
            kart->setSlowdown(MaxSpeed::MS_DECREASE_BUBBLE, 0.1f, stk_config->time2Ticks(0.1f), stk_config->time2Ticks(0));
            did_restart = true;
        }
    }

    bool is_last = (unsigned)kart->getPosition() == RaceManager::get()->getNumberOfKarts();

    if (is_last && did_restart)
    {
        m_virtual_pace_code = -1;
        m_restart_count = -1;
    }

    // the only reason such a ridiculous infinite gum penalty (-1) can be given is if it's a virtual pace car restart
    // plainly, the only reason this exists is because first place won't get its penalty overturned if for some reason
    if (m_virtual_pace_code == -1 && kart->m_max_speed->getSpeedDecreaseTicksLeft(MaxSpeed::MS_DECREASE_BUBBLE) == -1)
        kart->setSlowdown(MaxSpeed::MS_DECREASE_BUBBLE, 0.1f, stk_config->time2Ticks(0.1f), stk_config->time2Ticks(0));

}   // enforceVirtualPaceCarRulesForKart
//-----------------------------------------------------------------------------

void ItemPolicy::checkAndApplyVirtualPaceCarRules(
        AbstractKart *kart, int kart_section, int finished_laps)
{
    if (kart->getPosition() == 1)
    {
        m_leader_section = kart_section;
        int start_lap = m_policy_sections[kart_section].m_section_start;
        int16_t rules = m_policy_sections[kart_section].m_rules;
        bool do_virtual_pace = rules & ItemPolicyRules::IPT_VIRTUAL_PACE;
        bool do_unlapping = rules & ItemPolicyRules::IPT_UNLAPPING;
        if (do_virtual_pace && start_lap == finished_laps)
        {
            m_restart_count = 0;
            m_virtual_pace_code = do_unlapping
                ? start_lap // Lappings must slow down when they reach the lead lap
                : -2;       // Lappings must slow down as soon as possible
        }
    }

    bool slowed_down = false;
    if (m_virtual_pace_code == finished_laps || m_virtual_pace_code == -2)
    {
        auto& stk_config = STKConfig::get();
        m_restart_count += 1;
        kart->setSlowdown(MaxSpeed::MS_DECREASE_BUBBLE, 0.1f,
                          stk_config->time2Ticks(0.1f),
                        stk_config->time2Ticks(99999));
        slowed_down = true;
    }

    bool is_last = m_restart_count == (int)RaceManager::get()->getNumberOfKarts();
    // Technically there could be ghosts but you can't get into that situation. Probably.
    // If the kart is last, also fire the virtual pace car restart procedure
    if (slowed_down && is_last)
    {
        int time = World::getWorld()->getTime();
        time = (-time) - 3;
        // code till be added to 3 and inverted to get time
        m_virtual_pace_code = time;
    }
}   // checkAndApplyVirtualPaceCarRules
//-----------------------------------------------------------------------------
