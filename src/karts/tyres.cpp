#include "tyres.hpp"

#include "karts/kart_properties.hpp"
#include "karts/kart.hpp"
#include "network/network_string.hpp"
#include "network/rewind_manager.hpp"


Tyres::Tyres(Kart *kart) {
	m_kart = kart;
	m_current_compound = (((int)(m_kart->getKartProperties()->getTyresCompoundNumber())) == 0) ? 0 : 1;
	m_current_life_traction = m_kart->getKartProperties()->getTyresMaxLifeTraction()[m_current_compound];
	m_current_life_turning = m_kart->getKartProperties()->getTyresMaxLifeTurning()[m_current_compound];
	m_heat_cycle_count = 0.0f;
	m_current_temp = m_kart->getKartProperties()->getTyresIdealTemp()[m_current_compound];
	m_center_of_gravity_x = 0.0f;
	m_center_of_gravity_y = 0.0f;
	m_previous_speed = 0.0f;
	m_acceleration = 0.0f;
	m_time_elapsed = 0.0f;
	m_debug_cycles = 0;

	m_c_hardness_multiplier = m_kart->getKartProperties()->getTyresHardnessMultiplier()[m_current_compound];
	m_c_heat_cycle_hardness_curve = m_kart->getKartProperties()->getTyresHeatCycleHardnessCurve();
	m_c_hardness_penalty_curve = m_kart->getKartProperties()->getTyresHardnessPenaltyCurve();

	m_c_mass = m_kart->getKartProperties()->getMass();
	m_c_ideal_temp = m_kart->getKartProperties()->getTyresIdealTemp()[m_current_compound];
	m_c_max_life_traction = m_kart->getKartProperties()->getTyresMaxLifeTraction()[m_current_compound];
	m_c_max_life_turning = m_kart->getKartProperties()->getTyresMaxLifeTurning()[m_current_compound];
	m_c_min_life_traction = m_kart->getKartProperties()->getTyresMinLifeTraction()[m_current_compound];
	m_c_min_life_turning = m_kart->getKartProperties()->getTyresMinLifeTurning()[m_current_compound];
	m_c_limiting_transfer_traction = m_kart->getKartProperties()->getTyresLimitingTransferTraction()[m_current_compound];
	m_c_regular_transfer_traction = m_kart->getKartProperties()->getTyresRegularTransferTraction()[m_current_compound];
	m_c_limiting_transfer_turning = m_kart->getKartProperties()->getTyresLimitingTransferTurning()[m_current_compound];
	m_c_regular_transfer_turning = m_kart->getKartProperties()->getTyresRegularTransferTurning()[m_current_compound];
	m_c_do_substractive_traction = m_kart->getKartProperties()->getTyresDoSubstractiveTraction();
	m_c_do_substractive_turning  = m_kart->getKartProperties()->getTyresDoSubstractiveTurning();
	m_c_do_substractive_topspeed  = m_kart->getKartProperties()->getTyresDoSubstractiveTopspeed();
	m_c_response_curve_traction = m_kart->getKartProperties()->getTyresResponseCurveTraction();
	m_c_response_curve_turning = m_kart->getKartProperties()->getTyresResponseCurveTurning();
	m_c_response_curve_topspeed = m_kart->getKartProperties()->getTyresResponseCurveTopspeed();
	m_c_initial_bonus_mult_traction = m_kart->getKartProperties()->getTyresInitialBonusMultTraction()[m_current_compound];
	m_c_initial_bonus_add_traction = m_kart->getKartProperties()->getTyresInitialBonusAddTraction()[m_current_compound];
	m_c_initial_bonus_mult_turning = m_kart->getKartProperties()->getTyresInitialBonusMultTurning()[m_current_compound];
	m_c_initial_bonus_add_turning = m_kart->getKartProperties()->getTyresInitialBonusAddTurning()[m_current_compound];
	m_c_initial_bonus_mult_topspeed = m_kart->getKartProperties()->getTyresInitialBonusMultTopspeed()[m_current_compound];
	m_c_initial_bonus_add_topspeed = m_kart->getKartProperties()->getTyresInitialBonusAddTopspeed()[m_current_compound];
	m_c_traction_constant = m_kart->getKartProperties()->getTyresTractionConstant()[m_current_compound];
	m_c_turning_constant = m_kart->getKartProperties()->getTyresTurningConstant()[m_current_compound];
	m_c_topspeed_constant = m_kart->getKartProperties()->getTyresTopspeedConstant()[m_current_compound];

}

float Tyres::correct(float f) {
	return (100.0f*(float)m_current_compound+(float)m_current_compound)+f;
}

void Tyres::computeDegradation(float dt, bool is_on_ground, bool is_skidding, bool wreck_tyres, float brake_amount, float speed, float steer_amount) {
	m_debug_cycles += 1;
	m_time_elapsed += dt;
	if (fmod(m_time_elapsed, 0.3f) < 0.01f ) {
		//printf("Acceleration cycle has elapsed!\n");
		m_acceleration = speed-m_previous_speed;
		m_previous_speed = speed;
	}
	float turn_radius = 1.0f/steer_amount; // not really the "turn radius" but proportional
	float current_hardness = m_c_hardness_multiplier*m_c_heat_cycle_hardness_curve.get((m_heat_cycle_count));
	float deg_tur = 0.0f;
	float deg_tra = 0.0f;
	float deg_tur_percent = 0.0f;
	float deg_tra_percent = 0.0f;

	m_center_of_gravity_x = m_acceleration*m_c_mass;
	m_center_of_gravity_y = ((speed*speed)/turn_radius)*m_c_mass;
	if (!is_on_ground) goto LOG_ZONE;

	deg_tra = dt*std::abs(m_center_of_gravity_x)*std::abs(speed)*current_hardness/1000.0f;
	deg_tra += std::abs(speed/20000000.0f)/dt;

	if (brake_amount > 0.2f) {
		deg_tra *= brake_amount*5.0f;
	}
	if (wreck_tyres) {
		deg_tra *= 5.0f;
	}

	deg_tur = dt*std::abs(m_center_of_gravity_y)*std::abs(speed)*current_hardness/100000.0f;
	deg_tur += std::abs(m_center_of_gravity_y)/m_c_mass/100000000.0f/dt;

	if (is_skidding) {
		deg_tur *= 3.0f;
	}

	deg_tra_percent = deg_tra/m_c_max_life_traction;
	deg_tur_percent = deg_tur/m_c_max_life_turning;

	if(m_current_life_traction < m_current_life_turning) {
		m_current_life_turning -= deg_tra_percent*m_c_limiting_transfer_traction*m_c_max_life_turning;
		m_current_life_traction -= deg_tur_percent*m_c_regular_transfer_turning*m_c_max_life_traction;
	} else {
		m_current_life_turning -= deg_tra_percent*m_c_regular_transfer_traction*m_c_max_life_turning;
		m_current_life_traction -= deg_tur_percent*m_c_limiting_transfer_turning*m_c_max_life_traction;
	}

	m_current_life_traction -= deg_tra;
	m_current_life_turning -= deg_tur;
	if (m_current_life_traction < 0.0f) m_current_life_traction = 0.0f;
	if (m_current_life_turning < 0.0f) m_current_life_turning = 0.0f;

	LOG_ZONE:

	printf("Cycle %20lu || Kart %s || Time: %f\n\ttrac: %f%% ||| turn: %f%%\n", m_debug_cycles, m_kart->getIdent().c_str(),
	m_time_elapsed, 100.0f*(m_current_life_traction)/m_c_max_life_traction, 100.0f*(m_current_life_turning)/m_c_max_life_turning);

	printf("\tCenter of gravity: (%f, %f)\n\tRadius: %f || Speed:%f || Brake: %f\n", m_center_of_gravity_x,
	m_center_of_gravity_y, turn_radius, speed, brake_amount);
	printf("\tGround: %b || Skid: %b || Wreck: %b\n", is_on_ground, is_skidding, wreck_tyres);
	printf("\tSubstractive: (Tra %b || Tur %b || Top %b)\n", m_c_do_substractive_traction, m_c_do_substractive_turning,
	m_c_do_substractive_topspeed);
	printf("\tCompound: %u\n", m_current_compound);
}

    float Tyres::degEngineForce(float initial_force) {
    	float current_hardness = m_c_hardness_multiplier*m_c_heat_cycle_hardness_curve.get((m_heat_cycle_count));
    	float hardness_deviation = (current_hardness - m_c_hardness_multiplier) / m_c_hardness_multiplier;
    	float hardness_penalty = current_hardness *  m_c_hardness_penalty_curve.get((hardness_deviation*100));
	   	float percent = m_current_life_traction/m_c_max_life_traction;
    	float factor = m_c_response_curve_traction.get(correct(percent*100.0f))*m_c_traction_constant;
    	float bonus_traction = (initial_force+m_c_initial_bonus_add_traction)*m_c_initial_bonus_mult_traction;
    	if (m_c_do_substractive_traction) {
    		return bonus_traction - hardness_penalty*factor;
    	} else {
    		return bonus_traction*hardness_penalty*factor;
    	}
    }

    float Tyres::degTurnRadius(float initial_radius) {
    	float current_hardness = m_c_hardness_multiplier*m_c_heat_cycle_hardness_curve.get((m_heat_cycle_count));
    	float hardness_deviation = (current_hardness - m_c_hardness_multiplier) / m_c_hardness_multiplier;
    	float hardness_penalty = current_hardness *  m_c_hardness_penalty_curve.get((hardness_deviation*100));
	   	float percent = m_current_life_turning/m_c_max_life_turning;
    	float factor = m_c_response_curve_turning.get(correct(percent*100.0f))*m_c_turning_constant;
    	float bonus_turning = (initial_radius+m_c_initial_bonus_add_turning)*m_c_initial_bonus_mult_turning;
    	if (m_c_do_substractive_turning) {
    		return bonus_turning - hardness_penalty*factor;
    	} else {
    		return bonus_turning*hardness_penalty*factor;
    	}
    }

    float Tyres::degTopSpeed(float initial_topspeed) {
    	float current_hardness = m_c_hardness_multiplier*m_c_heat_cycle_hardness_curve.get((m_heat_cycle_count));
    	float hardness_deviation = (current_hardness - m_c_hardness_multiplier) / m_c_hardness_multiplier;
    	float hardness_penalty = current_hardness *  m_c_hardness_penalty_curve.get((hardness_deviation*100));
	   	float percent = m_current_life_traction/m_c_max_life_traction;
    	float factor = m_c_response_curve_topspeed.get(correct(percent*100.0f))*m_c_topspeed_constant;
    	float bonus_topspeed = (initial_topspeed+m_c_initial_bonus_add_topspeed)*m_c_initial_bonus_mult_topspeed;
    	if (m_c_do_substractive_topspeed) {
    		return bonus_topspeed - hardness_penalty*factor;
    	} else {
    		return bonus_topspeed*hardness_penalty*factor;
    	}
    }


void Tyres::reset() {
	m_current_life_traction = m_kart->getKartProperties()->getTyresMaxLifeTraction()[m_current_compound];
	m_current_life_turning = m_kart->getKartProperties()->getTyresMaxLifeTurning()[m_current_compound];
	m_heat_cycle_count = 0.0f;
	m_current_temp = m_kart->getKartProperties()->getTyresIdealTemp()[m_current_compound];
	m_center_of_gravity_x = 0.0f;
	m_center_of_gravity_y = 0.0f;
	m_previous_speed = 0.0f;
	m_acceleration = 0.0f;
	m_time_elapsed = 0.0f;
	m_debug_cycles = 0;

	m_c_hardness_multiplier = m_kart->getKartProperties()->getTyresHardnessMultiplier()[m_current_compound];
	m_c_heat_cycle_hardness_curve = m_kart->getKartProperties()->getTyresHeatCycleHardnessCurve();
	m_c_hardness_penalty_curve = m_kart->getKartProperties()->getTyresHardnessPenaltyCurve();

	m_c_mass = m_kart->getKartProperties()->getMass();
	m_c_ideal_temp = m_kart->getKartProperties()->getTyresIdealTemp()[m_current_compound];
	m_c_max_life_traction = m_kart->getKartProperties()->getTyresMaxLifeTraction()[m_current_compound];
	m_c_max_life_turning = m_kart->getKartProperties()->getTyresMaxLifeTurning()[m_current_compound];
	m_c_min_life_traction = m_kart->getKartProperties()->getTyresMinLifeTraction()[m_current_compound];
	m_c_min_life_turning = m_kart->getKartProperties()->getTyresMinLifeTurning()[m_current_compound];
	m_c_limiting_transfer_traction = m_kart->getKartProperties()->getTyresLimitingTransferTraction()[m_current_compound];
	m_c_regular_transfer_traction = m_kart->getKartProperties()->getTyresRegularTransferTraction()[m_current_compound];
	m_c_limiting_transfer_turning = m_kart->getKartProperties()->getTyresLimitingTransferTurning()[m_current_compound];
	m_c_regular_transfer_turning = m_kart->getKartProperties()->getTyresRegularTransferTurning()[m_current_compound];
	m_c_do_substractive_traction = m_kart->getKartProperties()->getTyresDoSubstractiveTraction();
	m_c_do_substractive_turning  = m_kart->getKartProperties()->getTyresDoSubstractiveTurning();
	m_c_do_substractive_topspeed  = m_kart->getKartProperties()->getTyresDoSubstractiveTopspeed();
	m_c_response_curve_traction = m_kart->getKartProperties()->getTyresResponseCurveTraction();
	m_c_response_curve_turning = m_kart->getKartProperties()->getTyresResponseCurveTurning();
	m_c_response_curve_topspeed = m_kart->getKartProperties()->getTyresResponseCurveTopspeed();
	m_c_initial_bonus_mult_traction = m_kart->getKartProperties()->getTyresInitialBonusMultTraction()[m_current_compound];
	m_c_initial_bonus_add_traction = m_kart->getKartProperties()->getTyresInitialBonusAddTraction()[m_current_compound];
	m_c_initial_bonus_mult_turning = m_kart->getKartProperties()->getTyresInitialBonusMultTurning()[m_current_compound];
	m_c_initial_bonus_add_turning = m_kart->getKartProperties()->getTyresInitialBonusAddTurning()[m_current_compound];
	m_c_initial_bonus_mult_topspeed = m_kart->getKartProperties()->getTyresInitialBonusMultTopspeed()[m_current_compound];
	m_c_initial_bonus_add_topspeed = m_kart->getKartProperties()->getTyresInitialBonusAddTopspeed()[m_current_compound];
	m_c_traction_constant = m_kart->getKartProperties()->getTyresTractionConstant()[m_current_compound];
	m_c_turning_constant = m_kart->getKartProperties()->getTyresTurningConstant()[m_current_compound];
	m_c_topspeed_constant = m_kart->getKartProperties()->getTyresTopspeedConstant()[m_current_compound];

}

void Tyres::saveState(BareNetworkString *buffer)
{
    buffer->addFloat(m_current_life_traction);
    buffer->addFloat(m_current_life_turning);
    buffer->addFloat(m_current_temp);
    buffer->addFloat(m_heat_cycle_count);
    buffer->addFloat(m_heat_cycle_count);
    buffer->addUInt8(m_current_compound);
}

void Tyres::rewindTo(BareNetworkString *buffer)
{
    m_current_life_traction = buffer->getFloat();
    m_current_life_turning = buffer->getFloat();
    m_current_temp = buffer->getFloat();
    m_heat_cycle_count = buffer->getFloat();
	m_current_compound = buffer->getUInt8();
}

