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

#ifndef HEADER_TYRES_HPP
#define HEADER_TYRES_HPP

#include "vector"
#include "utils/leak_check.hpp"
#include "utils/interpolation_array.hpp"
#include "utils/no_copy.hpp"
#include "utils/types.hpp"
#include <queue>
#include <memory>

class BareNetworkString;
class Kart;
class ShowCurve;
class CachedCharacteristic;


/**
 * \ingroup karts
 */

#undef SKID_DEBUG

class Tyres
{
friend class KartRewinder;
public:
    LEAK_CHECK();
private:
    float m_center_of_gravity_x;
    float m_center_of_gravity_y;
    float m_time_elapsed;
    float m_acceleration;

    float m_speed_fetching_period;
    unsigned m_speed_accumulation_limit;
    std::deque<float> m_previous_speeds;
    long unsigned m_debug_cycles;

    float m_c_hardness_multiplier;
    InterpolationArray m_c_heat_cycle_hardness_curve;
    InterpolationArray m_c_hardness_penalty_curve;

    float m_c_fuel;
    float m_c_fuel_regen;
    float m_c_fuel_stop;
    float m_c_fuel_weight;
    float m_c_fuel_rate;

    float m_c_mass;
    float m_c_ideal_temp;
    float m_c_min_life_traction;
    float m_c_min_life_turning;
    float m_c_limiting_transfer_traction;
    float m_c_regular_transfer_traction;
    float m_c_limiting_transfer_turning;
    float m_c_regular_transfer_turning;
    bool m_c_do_substractive_traction;
    bool m_c_do_grip_based_turning;
    bool m_c_do_substractive_turning;
    bool m_c_do_substractive_topspeed;
    InterpolationArray m_c_response_curve_traction;
    InterpolationArray m_c_response_curve_turning;
    InterpolationArray m_c_response_curve_topspeed;
    float m_c_initial_bonus_mult_traction;
    float m_c_initial_bonus_add_traction;
    float m_c_initial_bonus_mult_turning;
    float m_c_initial_bonus_add_turning;
    float m_c_initial_bonus_mult_topspeed;
    float m_c_initial_bonus_add_topspeed;
    float m_c_traction_constant;
    float m_c_turning_constant;
    float m_c_topspeed_constant;

    float m_c_offroad_factor;
    float m_c_skid_factor;
    float m_c_brake_threshold;
    float m_c_crash_penalty;


public:
    float m_current_life_traction;
    float m_c_max_life_traction;

    float m_current_life_turning;
    float m_c_max_life_turning;

    float m_current_fuel;
    float m_c_max_fuel;
    bool m_high_fuel_demand;

    unsigned m_current_compound;
    bool m_reset_compound;
    bool m_reset_fuel;

    float m_current_temp;
    float m_heat_cycle_count;
    float m_lap_count;

private:
    /** A read-only pointer to the kart's properties. */
    Kart *m_kart;

    static std::shared_ptr<CachedCharacteristic> m_colors_characteristic;
    static std::shared_ptr<CachedCharacteristic> getColorsCharacteristic();

public:
         Tyres(Kart *kart);
        ~Tyres() { };
    void reset();

    float getFuelWeight(void) { return m_c_fuel_weight; };
    float getFuelStopRatio(void) { return m_c_fuel_stop; };

    bool getGripBasedTurning(void) { return m_c_do_grip_based_turning; };

    float correct(float);
    void computeDegradation(float dt, bool is_on_ground, bool is_skidding, bool is_using_zipper, float slowdown, float brake_force, float steer_amount, float throttle_amount);

    void applyCrashPenalty(void);

    float degEngineForce(float);
    float degTopSpeed(float);
    float degTurnRadius(float);
    void saveState(BareNetworkString *buffer);
    void rewindTo(BareNetworkString *buffer);

    static float getTyreColor(int compound);


};   // Tyres


#endif

/* EOF */

