#include "tyres.hpp"

#include "karts/kart_properties.hpp"
#include "karts/kart.hpp"


Tyres::Tyres(Kart *kart) {
	m_kart = kart;
	m_current_life_traction = m_kart->getKartProperties()->getTyresMaxLifeTraction();
	m_current_life_turning = m_kart->getKartProperties()->getTyresMaxLifeTurning();
	m_heat_cycle_count = 0.0f;
	m_heat_accumulator = 0;
	m_current_temp = m_kart->getKartProperties()->getTyresIdealTemp();
	m_maximum_temp = m_current_temp;
	m_minimum_temp = m_current_temp;
	m_center_of_gravity_x = 0.0f;
	m_center_of_gravity_y = 0.0f;
	m_previous_speed = 0.0f;
	m_acceleration = 0.0f;
	m_time_elapsed = 0.0f;
	m_debug_cycles = 0;
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
	float mass = m_kart->getKartProperties()->getMass();
	float current_hardness = m_kart->getKartProperties()->getTyresHardnessMultiplier()*m_kart->getKartProperties()->getTyresHeatCycleHardnessCurve().get(m_heat_cycle_count);

	m_center_of_gravity_x = m_acceleration*mass;
	m_center_of_gravity_y = ((speed*speed)/turn_radius)*mass;
	if (!is_on_ground) return;

	float deg_tra = dt*std::abs(m_center_of_gravity_x)*std::abs(speed)*current_hardness/1000.0f;
	deg_tra += std::abs(speed/20000000.0f)/dt;

	if (brake_amount > 0.2f) {
		deg_tra *= brake_amount*5.0f;
	}
	if (wreck_tyres) {
		deg_tra *= 5.0f;
	}

	float deg_tur = dt*std::abs(m_center_of_gravity_y)*std::abs(speed)*current_hardness/100000.0f;
	deg_tur += std::abs(m_center_of_gravity_y)/mass/100000000.0f/dt;

	if (is_skidding) {
		deg_tur *= 3.0f;
	}

	float deg_tra_percent = deg_tra/m_kart->getKartProperties()->getTyresMaxLifeTraction();
	float deg_tur_percent = deg_tur/m_kart->getKartProperties()->getTyresMaxLifeTurning();

	if(m_current_life_traction < m_current_life_turning) {
		m_current_life_turning -= deg_tra_percent*m_kart->getKartProperties()->getTyresLimitingTransferTraction()*m_kart->getKartProperties()->getTyresMaxLifeTurning();
		m_current_life_traction -= deg_tur_percent*m_kart->getKartProperties()->getTyresRegularTransferTurning()*m_kart->getKartProperties()->getTyresMaxLifeTraction();
	} else {
		m_current_life_turning -= deg_tra_percent*m_kart->getKartProperties()->getTyresRegularTransferTraction()*m_kart->getKartProperties()->getTyresMaxLifeTurning();
		m_current_life_traction -= deg_tur_percent*m_kart->getKartProperties()->getTyresLimitingTransferTurning()*m_kart->getKartProperties()->getTyresMaxLifeTraction();
	}

	m_current_life_traction -= deg_tra;
	m_current_life_turning -= deg_tur;
	if (m_current_life_traction < 0.0f) m_current_life_traction = 0.0f;
	if (m_current_life_turning < 0.0f) m_current_life_turning = 0.0f;

/*
	float old_temp = m_current_temp;
	if (m_acceleration >= 10) m_current_temp += deg_tra/current_hardness/current_hardness;
	m_current_temp -= dt*m_kart->getKartProperties()->getTyresHeatTransferCurve().get(m_current_temp);
	if (m_current_temp > m_maximum_temp) m_maximum_temp = m_current_temp;
	if (m_current_temp < m_minimum_temp) m_minimum_temp = m_current_temp;
	m_heat_accumulator += m_current_temp - old_temp;
	if ((old_temp < m_kart->getKartProperties()->getTyresIdealTemp() && m_current_temp > m_kart->getKartProperties()->getTyresIdealTemp()) || (old_temp > m_kart->getKartProperties()->getTyresIdealTemp() && m_current_temp < m_kart->getKartProperties()->getTyresIdealTemp()) || (m_maximum_temp - m_minimum_temp > 15)) {
		m_heat_cycle_count += std::abs(m_heat_accumulator);
		m_heat_accumulator = 0;
		m_maximum_temp = m_kart->getKartProperties()->getTyresIdealTemp();
		m_minimum_temp = m_maximum_temp;
	}
*/
   	float hardness_deviation = (current_hardness - m_kart->getKartProperties()->getTyresHardnessMultiplier()) / m_kart->getKartProperties()->getTyresHardnessMultiplier();
   	float hardness_penalty = current_hardness *  m_kart->getKartProperties()->getTyresHardnessPenaltyCurve().get(hardness_deviation*100);

	printf("Cycle %20lu || DT: %f\n\ttrac: %f%% ||| turn: %f%%\n", m_debug_cycles, dt, 100.0f*(m_current_life_traction)/m_kart->getKartProperties()->getTyresMaxLifeTraction(), 100.0f*(m_current_life_turning)/m_kart->getKartProperties()->getTyresMaxLifeTurning());
	printf("\tCenter of gravity: (%f, %f)\n\tRadius: %f || Speed:%f || Brake: %f\n", m_center_of_gravity_x, m_center_of_gravity_y, turn_radius, speed, brake_amount);
	printf("\tGround: %b || Skid: %b || Wreck: %b\n\tHard: %f || Pen: %f || Temp: %f\n", is_on_ground, is_skidding, wreck_tyres, current_hardness, hardness_penalty, m_current_temp);
	printf("\tSubstractive: (Tra %b || Tur %b || Top %b)\n", m_kart->getKartProperties()->getTyresDoSubstractiveTraction(), m_kart->getKartProperties()->getTyresDoSubstractiveTurning(), m_kart->getKartProperties()->getTyresDoSubstractiveTopspeed());
}

    float Tyres::degEngineForce(float initial_force) {
    	float current_hardness = m_kart->getKartProperties()->getTyresHardnessMultiplier()*m_kart->getKartProperties()->getTyresHeatCycleHardnessCurve().get(m_heat_cycle_count);
    	float hardness_deviation = (current_hardness - m_kart->getKartProperties()->getTyresHardnessMultiplier()) / m_kart->getKartProperties()->getTyresHardnessMultiplier();
    	float hardness_penalty = current_hardness *  m_kart->getKartProperties()->getTyresHardnessPenaltyCurve().get(hardness_deviation*100);
	   	float percent = m_current_life_traction/m_kart->getKartProperties()->getTyresMaxLifeTraction();
    	float factor = m_kart->getKartProperties()->getTyresResponseCurveTraction().get(percent*100.0f)*m_kart->getKartProperties()->getTyresTractionConstant();
    	float bonus_traction = (initial_force+m_kart->getKartProperties()->getTyresInitialBonusAddTraction())*m_kart->getKartProperties()->getTyresInitialBonusMultTraction();
    	if (m_kart->getKartProperties()->getTyresDoSubstractiveTraction()) {
    		return bonus_traction - hardness_penalty*factor;
    	} else {
    		return bonus_traction*hardness_penalty*factor;
    	}
    }
    float Tyres::degTurnRadius(float initial_radius) {
    	float current_hardness = m_kart->getKartProperties()->getTyresHardnessMultiplier()*m_kart->getKartProperties()->getTyresHeatCycleHardnessCurve().get(m_heat_cycle_count);
    	float hardness_deviation = (current_hardness - m_kart->getKartProperties()->getTyresHardnessMultiplier()) / m_kart->getKartProperties()->getTyresHardnessMultiplier();
    	float hardness_penalty = current_hardness *  m_kart->getKartProperties()->getTyresHardnessPenaltyCurve().get(hardness_deviation*100);
	   	float percent = m_current_life_turning/m_kart->getKartProperties()->getTyresMaxLifeTurning();
    	float factor = m_kart->getKartProperties()->getTyresResponseCurveTurning().get(percent*100.0f)*m_kart->getKartProperties()->getTyresTurningConstant();
    	float bonus_turning = (initial_radius+m_kart->getKartProperties()->getTyresInitialBonusAddTurning())*m_kart->getKartProperties()->getTyresInitialBonusMultTurning();
    	if (m_kart->getKartProperties()->getTyresDoSubstractiveTurning()) {
    		return bonus_turning - hardness_penalty*factor;
    	} else {
    		return bonus_turning*hardness_penalty*factor;
    	}
    }
    float Tyres::degTopSpeed(float initial_topspeed) {
    	float current_hardness = m_kart->getKartProperties()->getTyresHardnessMultiplier()*m_kart->getKartProperties()->getTyresHeatCycleHardnessCurve().get(m_heat_cycle_count);
    	float hardness_deviation = (current_hardness - m_kart->getKartProperties()->getTyresHardnessMultiplier()) / m_kart->getKartProperties()->getTyresHardnessMultiplier();
    	float hardness_penalty = current_hardness *  m_kart->getKartProperties()->getTyresHardnessPenaltyCurve().get(hardness_deviation*100);
    	float percent = m_current_life_traction/m_kart->getKartProperties()->getTyresMaxLifeTraction();
		float factor = m_kart->getKartProperties()->getTyresResponseCurveTopspeed().get(percent*100.0f)*m_kart->getKartProperties()->getTyresTopspeedConstant();
    	float bonus_topspeed = (initial_topspeed+m_kart->getKartProperties()->getTyresInitialBonusAddTopspeed())*m_kart->getKartProperties()->getTyresInitialBonusMultTopspeed();
    	if (m_kart->getKartProperties()->getTyresDoSubstractiveTopspeed()) {
    		return bonus_topspeed - hardness_penalty*factor;
    	} else {
    		return bonus_topspeed*hardness_penalty*factor;
    	}
    }


void Tyres::reset() {
	m_current_life_traction = m_kart->getKartProperties()->getTyresMaxLifeTraction();
	m_current_life_turning = m_kart->getKartProperties()->getTyresMaxLifeTurning();
	m_heat_cycle_count = 0.0f;
	m_heat_accumulator = 0.0f;
	m_current_temp = m_kart->getKartProperties()->getTyresIdealTemp();
	m_maximum_temp = m_current_temp;
	m_minimum_temp = m_current_temp;
	m_center_of_gravity_x = 0.0f;
	m_center_of_gravity_y = 0.0f;
	m_previous_speed = 0.0f;
	m_acceleration = 0.0f;
	m_time_elapsed = 0.0f;
	m_debug_cycles = 0;
}
