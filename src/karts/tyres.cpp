//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2024 Nomagno
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

#include "tyres.hpp"

#include "karts/cached_characteristic.hpp"
#include "karts/controller/controller.hpp"
#include "karts/kart.hpp"
#include "karts/kart_properties.hpp"
#include "karts/kart_properties_manager.hpp"
#include "karts/max_speed.hpp"
#include "tracks/track.hpp"
#include "modes/world.hpp"
#include "network/network_string.hpp"
#include "network/rewind_manager.hpp"
#include "config/user_config.hpp"
#include "config/stk_config.hpp"
#include "network/network_config.hpp"
#include "race/race_manager.hpp"
#include <iostream>
#include <algorithm>

// Despite being declared in base section of the config, it's NOT intended
// to be overridden or edited by difficulty or kart class.
std::shared_ptr<CachedCharacteristic> Tyres::m_colors_characteristic = nullptr;

Tyres::Tyres(Kart *kart) {
    m_kart = kart;
    m_deg_mult = 1;
    m_current_compound = 1; // Placeholder value
    m_current_fuel = 1; // Placeholder value
    m_high_fuel_demand = false;
    m_lap_count = 0;
    m_reset_compound = false;
    m_reset_fuel = false;

    m_speed_fetching_period = 0.3f;
    m_speed_accumulation_limit = 6;

	RaceManager::TyreModRules *tme_rules = RaceManager::get()->getTyreModRules();
    m_kart->m_tyres_queue = tme_rules->tyre_allocation;

    // Boilerplate to initialize all the m_c_xxx constants
    #include "karts/tyres_boilerplate.txt"

    m_c_fuel_rate_base = m_kart->getKartProperties()->getFuelConsumption();
    m_c_fuel_weight_real = m_kart->getKartProperties()->getFuelMassReal();
    m_c_fuel_weight_virtual = m_kart->getKartProperties()->getFuelMassVirtual();

    m_current_fuel = m_c_fuel;
}

float Tyres::correct(float f) {
    return (100.0f*(float)(m_current_compound-1)+(float)(m_current_compound-1))+f;
}

void Tyres::computeDegradation(float dt, bool is_on_ground, bool is_skidding, bool is_using_zipper, float slowdown, float brake_amount, float steer_amount, float throttle_amount) {
    m_debug_cycles += 1;
    m_time_elapsed += dt;
    float speed = m_kart->getSpeed();
    // Every 0.3 seconds, up to 6 samples, collect a longitudinal speed sample.
    // Compare the current speed with the 6 past speed samples, and take the lowest acceleration.
    // This is to normalize for weird acceleration peaks, as STK karts have unlimited grip.
    if (fmod(m_time_elapsed, m_speed_fetching_period) < dt) {
        m_previous_speeds.push_back(speed);
        if (m_previous_speeds.size() > m_speed_accumulation_limit ) {
            m_previous_speeds.pop_front();
        }
        if (m_previous_speeds.size() >= 2) {
            m_acceleration = (speed-m_previous_speeds[m_previous_speeds.size()-2])/dt;
            for (unsigned i = 0; i < m_previous_speeds.size()-2; i++) {
                if (std::abs(speed-m_previous_speeds[i])/dt < std::abs(m_acceleration) && std::abs(speed-m_previous_speeds[i])/dt < 2300.0f) { //Empirical, above this generally indicates a crash, so we throw it out.
                    m_acceleration = (speed-m_previous_speeds[i])/dt;
                }
            }
        }
        if (slowdown < 0.5f && !is_using_zipper) {
            m_acceleration = 0.0f; //No fair traction simulation can be achieved with such materials
        }
    }
    
    float turn_radius = 1.0f/steer_amount; // not really the "turn radius" but proportional
    float deg_tur = 0.0f;
    float deg_tra = 0.0f;
    float deg_tur_percent = 0.0f;
    float deg_tra_percent = 0.0f;

    // The weight used to compute degradation, which will be different from the real one

    float effective_mass = m_kart->getMass() - m_c_fuel_weight_real*m_current_fuel + m_c_fuel_weight_virtual*m_current_fuel;
    // Longitudinal force
    m_force_x = m_acceleration*effective_mass;

    // Centripetal force
    m_force_y = ((speed*speed)/turn_radius)*effective_mass;

    // If throttle is below 20% usage, user is "lift and coasting" and fuel consumption is reduced by 12%
    float lift_and_coast_factor = 0;
    if (throttle_amount > 0.20f) {
        m_high_fuel_demand = true;
        lift_and_coast_factor = 1.0f;
    } else {
        m_high_fuel_demand = false;
        lift_and_coast_factor = 0.88f;
    }
    /*The fuel rate factor is in L/km*/
    /*The base rate is immutable, while the regular rate can be modified on the fly by item policy*/
    float fuel_rate_factor = lift_and_coast_factor*m_c_fuel_rate_base*m_c_fuel_rate*0.001f;

    if (m_current_fuel > m_kart->getKartProperties()->getFuelCapacity()) m_current_fuel = m_kart->getKartProperties()->getFuelCapacity();
    if (m_current_fuel < 0.0f) m_current_fuel = 0.0f;

    //Doesn't make much sense to degrade the tyres/consume fuel midair or in reverse or at ridiculously low speeds, now does it?
    if (!is_on_ground || speed < 1.0f) {
        m_high_fuel_demand = false;
        goto LOG_ZONE;
    }

    // Fuel consumption hence is only dependent on travelled distance, and on lift and coasting
    m_current_fuel -= std::abs(speed)*dt*fuel_rate_factor;

    // Apply longitudinal force as degradation to the traction health
    deg_tra = dt*std::abs(m_force_x)/100000.0f;

    // Apply rolling resistance as degradation to the traction health
    deg_tra += dt*std::abs(speed)/50.0f;

    // If braking input is above a certain threshold, multiply degradation by 1/threshold
    if (brake_amount > m_c_brake_threshold) {
        deg_tra *= brake_amount*(1.0f/m_c_brake_threshold);
    }

    // If offroad and slowing down, multiple degradation by the offroad fraction (simulate sliding on low-grip materials)
    if (slowdown < 0.98f && !is_using_zipper) {
        deg_tra *= m_c_offroad_factor;
    }

    // Apply centripetal force as degradation to the turning health
    deg_tur = dt*std::abs(m_force_y)/10000.0f;

    // If skidding, the turning deg is multiplied by the skid factor
    if (is_skidding) {
        deg_tur *= m_c_skid_factor;
    }

    if (m_current_fuel > m_kart->getKartProperties()->getFuelCapacity()) m_current_fuel = m_kart->getKartProperties()->getFuelCapacity();
    if (m_current_fuel < 0.0f) m_current_fuel = 0.0f;

    deg_tra *= m_deg_mult;
    deg_tur *= m_deg_mult;

    deg_tra_percent = deg_tra/m_c_max_life_traction;
    deg_tur_percent = deg_tur/m_c_max_life_turning;

    // Limiting transfer settings: each tyre will be configured in to act differently
    // when the lowest health bar is turning/traction,
    // degrading turning a certain X% for every Y% degraded to traction, or vice versa.
    if(m_current_life_traction < m_current_life_turning) {
        m_current_life_turning -= deg_tra_percent*m_c_limiting_transfer_traction*m_c_max_life_turning;
        m_current_life_traction -= deg_tur_percent*m_c_regular_transfer_turning*m_c_max_life_traction;
    } else {
        m_current_life_turning -= deg_tra_percent*m_c_regular_transfer_traction*m_c_max_life_turning;
        m_current_life_traction -= deg_tur_percent*m_c_limiting_transfer_turning*m_c_max_life_traction;
    }

    // Apply degradation
    m_current_life_traction -= deg_tra;
    m_current_life_turning -= deg_tur;

    if (m_current_life_traction < 0.0f) m_current_life_traction = 0.0f;
    if (m_current_life_turning < 0.0f) m_current_life_turning = 0.0f;

    LOG_ZONE:
    ;

    //printf("(%f; %f; %f)\n", m_kart->getXYZ().getX(), m_kart->getXYZ().getY(), m_kart->getXYZ().getZ());
    //printf("Cycle %20lu || K %s || C %u\n\ttrac: %f%% ||| turn: %f%%\n", m_debug_cycles, m_kart->getIdent().c_str(),
    //m_current_compound, 100.0f*(m_current_life_traction)/m_c_max_life_traction, 100.0f*(m_current_life_turning)/m_c_max_life_turning);
    //printf("\tCenter of gravity: (%f, %f)\n\tTurn: %f || Speed:%f || Brake: %f\n", m_force_x,
    //m_force_y, turn_radius, speed, brake_amount);
    //printf("\tFuel: %f || Weight: %f || RealWeight: %f || TrackLength: %f\n", m_current_fuel, effective_mass, m_kart->getMass(), Track::getCurrentTrack()->getTrackLength());
}

void Tyres::applyCrashPenalty(void) {
    m_current_life_traction -= (m_c_crash_penalty/100.0f)*m_c_max_life_traction;
    m_current_life_turning -= (m_c_crash_penalty/100.0f)*m_c_max_life_turning;
}

float Tyres::degEngineForce(float initial_force) {
    float percent = m_current_life_traction/m_c_max_life_traction;
    float factor = m_c_response_curve_traction.get(correct(percent*100.0f))*m_c_traction_constant;
    float bonus_traction = (initial_force+m_c_initial_bonus_add_traction)*m_c_initial_bonus_mult_traction;
    if (m_c_do_substractive_traction) {
        return bonus_traction - factor;
    } else {
        return bonus_traction*factor;
    }
}

float Tyres::degTurnRadius(float initial_radius) {
    float percent = m_current_life_turning/m_c_max_life_turning;
    float factor = m_c_response_curve_turning.get(correct(percent*100.0f))*m_c_turning_constant;
    float bonus_turning = (initial_radius+m_c_initial_bonus_add_turning)*m_c_initial_bonus_mult_turning;
    if (m_c_do_substractive_turning) {
        return bonus_turning - factor;
    } else {
        return bonus_turning*factor;
    }
}

float Tyres::degTopSpeed(float initial_topspeed) {
    float decrease_per_liter = m_kart->getKartProperties()->getFuelMaxSpeedDecrease();
    if (m_c_fuel_weight_virtual < 0.001f && m_c_fuel_weight_real < 0.001f)
        decrease_per_liter = 0.0f; //no fuel or electric mode, fuel does not affect speed
    float percent = m_current_life_traction/m_c_max_life_traction;
    float factor = m_c_response_curve_topspeed.get(correct(percent*100.0f))*m_c_topspeed_constant;
    float bonus_topspeed = (initial_topspeed+m_c_initial_bonus_add_topspeed)*m_c_initial_bonus_mult_topspeed;
    if (m_c_do_substractive_topspeed && m_current_fuel > 0.1f) {
        return bonus_topspeed - factor - decrease_per_liter*m_current_fuel;
    } else if (m_current_fuel > 0.1f) {
        return bonus_topspeed*factor - decrease_per_liter*m_current_fuel;
    } else {
        return 5;
    }
}


void Tyres::reset() {
    if (m_reset_compound) {
        m_deg_mult = 1;
        const unsigned c = m_kart->getStartingTyre();
        if (c == 0) { /*0 -> random tyre*/
            m_current_compound = ((int)rand() % 3) + 2; /*Should be modulo the compound number, but at the moment some compounds are not finished*/
        } else {
            m_current_compound = ((int)(c-1) % (int)m_kart->getKartProperties()->getTyresCompoundNumber()) + 1;
        }

        if (!(NetworkConfig::get()->isServer())){
            std::wstring namew(m_kart->getController()->getName().c_str());
            std::string name( namew.begin(), namew.end() );
            Log::info("[RunRecord]", "S %s %s %s %s", name.c_str(), m_kart->getIdent().c_str(), RaceManager::get()->getTrackName().c_str(), std::to_string(m_current_compound).c_str());
        }
    }

    bool change_color = UserConfigParams::m_override_kart_color_with_tyre;
    if (change_color && getTyreColor(m_current_compound) > -0.5f) {
        const float tyre_hue = getTyreColor(m_current_compound) / 100.0f;
        m_kart->setKartColor(tyre_hue);
    }

    // Boilerplate to initialize all the m_c_xxx constants
    #include "karts/tyres_boilerplate.txt"

    // This simply inits the constants into their corresponding variables
    if (m_reset_compound) { // Only set these unconditionally at race start
        m_c_fuel_rate_base = m_kart->getKartProperties()->getFuelConsumption();
        m_c_fuel_weight_real = m_kart->getKartProperties()->getFuelMassReal();
        m_c_fuel_weight_virtual = m_kart->getKartProperties()->getFuelMassVirtual();
    }

    if (m_reset_fuel) {
        if (World::getWorld()->raceHasLaps()) {
            m_c_fuel_rate = 0; // Will be set to the appropiate one by itempolicy on pass by start/finish line
        } else {
            m_c_fuel_rate = 1; // Always 1 for now, because in non-race it's not controlled by itempolicy
        }
		RaceManager::TyreModRules *tme_rules = RaceManager::get()->getTyreModRules();
        int fuel_mode = tme_rules->fuel_mode;
        switch (fuel_mode) {
        case 0: // Fuel off
            m_c_fuel_rate_base = 0;
            m_c_fuel_weight_real = 0;
            m_c_fuel_weight_virtual = 0;
            break;
        case 1: // Weightless fuel, doesn't impact kart performance
            m_c_fuel_weight_real = 0;
            m_c_fuel_weight_virtual = 0;
            break;
        case 2: // Fuel on
            break;
        default:
            break;
        }
    }

    if (m_reset_fuel) {
    	RaceManager::TyreModRules *tme_rules = RaceManager::get()->getTyreModRules();
        m_kart->m_tyres_queue = tme_rules->tyre_allocation;
        m_kart->m_wildcards = tme_rules->wildcards;
        m_current_fuel = m_c_fuel;
        m_high_fuel_demand = false;
    }

    m_lap_count = 0;

    m_current_life_traction = m_kart->getKartProperties()->getTyresMaxLifeTraction()[m_current_compound-1];
    m_current_life_turning = m_kart->getKartProperties()->getTyresMaxLifeTurning()[m_current_compound-1];
    m_force_x = 0.0f;
    m_force_y = 0.0f;
    m_previous_speeds.clear();
    m_acceleration = 0.0f;
    m_time_elapsed = 0.0f;
    m_debug_cycles = 0;


    // Deduct starting tyre from allocation for kart if possible
    if (m_reset_compound && m_kart->m_tyres_queue.size() >= m_current_compound ) {
        if (m_kart->m_tyres_queue[m_current_compound-1] > 0) {
            m_kart->m_tyres_queue[m_current_compound-1] -= 1;
        } else if (m_kart->m_tyres_queue[m_current_compound-1] == 0) { // Tried to start with forbidden tyre
            if (m_kart->m_wildcards > 0) { // One wildcard was spent, but do not penalize
                m_kart->m_wildcards -= 1;
            } else { // No wildcards available, penalize
                m_current_life_traction *= 0.05;
                m_current_life_turning *= 0.05;
                m_kart->m_wildcards = 0;
            }
        } else { // Tyre alloc is <= -1, which means infinite, nothing to do
            ;
        }
    }
}


void Tyres::saveState(BareNetworkString *buffer)
{
    buffer->addFloat(m_current_life_traction);
    buffer->addFloat(m_current_life_turning);
    buffer->addFloat(m_current_fuel);
    buffer->addFloat(m_kart->m_target_refuel);
    buffer->addUInt8(m_current_compound);
    buffer->addUInt8(m_lap_count);
    buffer->addUInt8(m_kart->m_wildcards);
    buffer->addUInt8(m_kart->m_tyres_queue.size());
    for (long unsigned i = 0; i < m_kart->m_tyres_queue.size(); i++) {
        buffer->addInt8(m_kart->m_tyres_queue[i]);
    }
}

void Tyres::rewindTo(BareNetworkString *buffer)
{
    m_current_life_traction = buffer->getFloat();
    m_current_life_turning = buffer->getFloat();
    m_current_fuel = buffer->getFloat();
    m_kart->m_target_refuel = buffer->getFloat();
    m_current_compound = buffer->getUInt8();
    m_lap_count = buffer->getUInt8();
    m_kart->m_wildcards = buffer->getUInt8();
    unsigned queue_size = buffer->getUInt8();
    std::vector<int> tmpvec;
    for (unsigned i = 0; i < queue_size; i++) {
        int tmpint = buffer->getInt8();
        tmpvec.push_back(tmpint);
    }
    m_kart->m_tyres_queue = tmpvec;
}

//-----------------------------------------------------------------------------

std::shared_ptr<CachedCharacteristic> Tyres::getColorsCharacteristic()
{
    if (!m_colors_characteristic)
    {
        auto characteristic = kart_properties_manager->getBaseCharacteristic();
        m_colors_characteristic = std::make_shared<CachedCharacteristic>(characteristic);
    }
    return m_colors_characteristic;
}   // getColorsCharacteristic
//-----------------------------------------------------------------------------

float Tyres::getTyreColor(int compound)
{
    auto res = getColorsCharacteristic()->getTyresDefaultColor();
    int idx = compound - 1;
    if (idx < 0 || idx >= res.size())
        return 0.0f;
    return res[idx];
}   // getTyreColor
//-----------------------------------------------------------------------------
void Tyres::commandLap(int ticks) {
    if (!(NetworkConfig::get()->isServer())) {
        auto& stk_config = STKConfig::get();
        std::wstring namew(m_kart->getController()->getName().c_str());
        std::string name( namew.begin(), namew.end() );
        Log::info("[RunRecord]", "L %s %s %s", name.c_str(), RaceManager::get()->getTrackName().c_str(), std::to_string(stk_config->ticks2Time(ticks)).c_str());
    }
    m_lap_count += 1;
}
void Tyres::commandEnd(void) {
    if (!(NetworkConfig::get()->isServer())){
        std::wstring namew(m_kart->getController()->getName().c_str());
        std::string name( namew.begin(), namew.end() );
        Log::info("[RunRecord]", "E %s %s", name.c_str(), RaceManager::get()->getTrackName().c_str());
    }
    auto stint = m_kart->getStints();
    if ((std::get<0>(stint[0]) == 0) && (std::get<1>(stint[0]) == 0)) {
        stint.erase(stint.begin());
    }

    //In theory, the lap age will be incremented after
    // we need it to be incremented because of the position
    // in the call tree of the linear_world.cpp module,
    // so just add one here
    std::tuple<unsigned int, unsigned int> tmp_tuple = std::make_tuple(m_current_compound, m_lap_count+1);
    stint.push_back(tmp_tuple);
    m_kart->setStints(stint);
    m_lap_count = 0;
}

void Tyres::commandChange(int compound, int time) {
    auto& stk_config = STKConfig::get();
    if (compound == 123) {
        // 123 is the code for a refueling
        if (time <= 2.0)
            time = 2.0;
        m_kart->m_max_speed->setSlowdown(MaxSpeed::MS_DECREASE_STOP, 0.1f, stk_config->time2Ticks(0.1f), stk_config->time2Ticks(time));
        m_kart->m_is_refueling = true;
        return;
    }

    if (time > 0) {
        m_kart->m_max_speed->setSlowdown(MaxSpeed::MS_DECREASE_STOP, 0.1f, stk_config->time2Ticks(0.1f), stk_config->time2Ticks(time));
    }


    if (compound >= 124 && compound <= 126) {
        std::string kart_to_change_to;
        switch (compound) {
        case 124:
            kart_to_change_to = "kiki";
            break;
        case 125:
            kart_to_change_to = "tux";
            break;
        case 126:
            kart_to_change_to = "pidgin";
            break;
        default:
            kart_to_change_to = "tux";
            break;
        }
        m_kart->changeKartMidRace(kart_to_change_to, m_kart->getHandicap(), m_current_compound /*will be ignored*/, m_kart->getKartModel()->getRenderInfo());
        return;
    }

    if (compound == 125) {
        // 125 is the code for a change to medium kart
        m_kart->m_max_speed->setSlowdown(MaxSpeed::MS_DECREASE_STOP, 0.1f, stk_config->time2Ticks(0.1f), stk_config->time2Ticks(time));
        m_kart->changeKartMidRace("tux",
                         m_kart->getHandicap(), m_current_compound /*parameter will be more or less ignored*/,
                         m_kart->getKartModel()->getRenderInfo());
        return;
    }

    if (compound == 126) {
        // 126 is the code for a change to heavy kart
        m_kart->m_max_speed->setSlowdown(MaxSpeed::MS_DECREASE_STOP, 0.1f, stk_config->time2Ticks(0.1f), stk_config->time2Ticks(time));
        m_kart->changeKartMidRace("pidgin",
                         m_kart->getHandicap(), m_current_compound /*parameter will be more or less ignored*/,
                         m_kart->getKartModel()->getRenderInfo());
        return;
    }


    auto stint = m_kart->getStints();
    if ((std::get<0>(stint[0]) == 0) && (std::get<1>(stint[0]) == 0)) {
        stint.erase(stint.begin());
    }

    std::tuple<unsigned int, unsigned int> tmp_tuple = std::make_tuple(m_current_compound, m_lap_count);
    stint.push_back(tmp_tuple);
    m_kart->setStints(stint);
    m_lap_count = 0;

    unsigned prev_compound = m_current_compound;
    float prev_trac = m_current_life_traction/m_c_max_life_traction;
    float prev_tur = m_current_life_turning/m_c_max_life_turning;
    
    if (compound >= 1) {
        m_current_compound = ((compound-1) % (int)m_kart->getKartProperties()->getTyresCompoundNumber())+1;
    } else {
        m_current_compound = rand() % (int)m_kart->getKartProperties()->getTyresCompoundNumber();
    }
    if (!(NetworkConfig::get()->isServer())){
        std::wstring namew(m_kart->getController()->getName().c_str());
        std::string name( namew.begin(), namew.end() );

        // Logs will be clutter unless this check is ran
        if (!m_kart->isGhostKart()) {
            Log::info("[RunRecord]", "C %s %s %s %s", name.c_str(), RaceManager::get()->getTrackName().c_str(), std::to_string(m_current_compound).c_str(), std::to_string(time).c_str());
        }
    }

    m_reset_compound = false;
    m_reset_fuel = false;
    Tyres::reset();

    if (/*m_kart->m_is_under_tme_ruleset*/ true) {
        if (!m_kart->m_tyres_queue.empty() && m_kart->m_tyres_queue.size() >= m_current_compound) { /*Empty queue just means it wasn't initialized*/
            bool pitting_for_same = prev_compound == m_current_compound;
            bool old_tyres_were_fresh = prev_trac > 0.98f && prev_tur > 0.98f;
            bool new_tyre_is_available = m_kart->m_tyres_queue[m_current_compound-1] != 0;
            bool new_tyre_is_infinite = m_kart->m_tyres_queue[m_current_compound-1] == -1;
            bool prev_tyre_is_infinite = m_kart->m_tyres_queue[prev_compound-1] == -1;


            // This system is extremely lenient, it will never take away a tyre from the user unless it's clearly breaking the rules and it can't be "corrected for the user".
            // TODO: This system adds a bit of spice by penalizing if you mess up, but instead the stop could just never happen if the tyre isn't available.
            //       That should be made configurable in the game settings.

            // Accidental pitstop for the same compound while it is unused is not punished (see below)
            bool same_pitstop_twice = (pitting_for_same && old_tyres_were_fresh);

            // Penalize the player if there is no new tyre, except when trying to make the same pitstop again
            bool should_disqualify = (!new_tyre_is_available && !same_pitstop_twice);

            // Take one compound if it existed, except when trying to make the same pitstop again
            bool reduce_current = (new_tyre_is_available && !same_pitstop_twice);

            // Return the compound if it was unused (e.g. just pitted before) but we select another one
            bool return_old = (!pitting_for_same && old_tyres_were_fresh);

            // This just checks if it's safe to write/read from the corresponding index
            bool prev_compound_has_alloc = m_kart->m_tyres_queue.size() >= prev_compound;
    
            if (prev_compound_has_alloc && return_old && !prev_tyre_is_infinite) {
                m_kart->m_tyres_queue[prev_compound-1] += 1;
            }

            if (reduce_current && !new_tyre_is_infinite) {
                m_kart->m_tyres_queue[m_current_compound-1] -= 1;
            }

            if (should_disqualify) {
                if (m_kart->m_wildcards > 0) { // Spend a wildcard and don't penalize
                    ;
                } else { /*Penalty for pitting with no available compound*/ 
                    m_kart->m_is_disqualified = true;
                    m_current_life_turning *= 0.5;
                    m_current_life_traction *= 0.5;
                }
            }
        }
    }
}

std::vector<uint8_t> Tyres::encodeStints(std::vector<std::tuple<unsigned, unsigned>>& s) {
    std::vector<uint8_t> r;
    r.push_back(1); // version
    if (s.size() > 255) {
        r[0] = 0;
        return r;
    }
    r.push_back(s.size());
    for (unsigned i = 0; i < s.size(); i++) {
        r.push_back(std::get<0>(s[i])); // compound number
        r.push_back(std::get<1>(s[i])); // compound length
    }
    return r;
}

std::vector<std::tuple<unsigned, unsigned>> Tyres::decodeStints(std::vector<uint8_t>& a) {
    std::vector<std::tuple<unsigned, unsigned>> r;
    unsigned version = a[0];
    if (version != 1) {
        r.push_back(std::make_tuple(-1, -1));
        return r;
    }
    unsigned count = a[1];
    for (unsigned i = 0; i < count; i++){
        r.push_back(std::make_tuple(a[2+2*i], a[2+2*i+1]));
    }
    return r;
}
