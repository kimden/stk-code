//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2025 kimden
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

#include "utils/scoring/exponential_gap_scoring.hpp"
#include "utils/string_utils.hpp"

#include <cmath>
#include <stdexcept>


void ExponentialGapScoring::fromIntParamString(const std::string& input)
{
    auto argv = StringUtils::split(input, ' ');
    m_pole_points        = Scoring::parse<int>(argv, 0);
    m_fastest_lap_points = Scoring::parse<int>(argv, 1);
    m_win_points         = Scoring::parseThousandth(argv, 2);
    m_time_log_unit      = Scoring::parseThousandth(argv, 3);
    m_decrease           = Scoring::parseThousandth(argv, 4);
    m_continuous         = Scoring::parse<int>(argv, 5);

    m_allow_negative = true;
    m_allow_pure_negative = false; // kimden: pay attention
    m_round = true;
}   // fromIntParamString
//-----------------------------------------------------------------------------

void ExponentialGapScoring::fromString(const std::string& input)
{

}   // fromString
//-----------------------------------------------------------------------------

void ExponentialGapScoring::refreshCustomScores(int num_karts,
        std::vector<int>& score_for_position)
{
    score_for_position.clear();
    score_for_position.resize(num_karts, 0);
}   // refreshCustomScores
//-----------------------------------------------------------------------------

int ExponentialGapScoring::getScoreForPosition(int p, float time,
        std::map<int, float>& race_times,
        const std::vector<int>& score_for_position) const
{
    race_times[p] = time;
    double delta = time - race_times[1];
    if (race_times[1] < 1e-6) // just in case
        return 0;

    delta = log(time / race_times[1]) / log(2);
    double points = m_win_points;
    bool continuous = (m_continuous != 0);
    double time_step = m_time_log_unit;
    double decrease = m_decrease;
    delta /= time_step;
    if (!continuous)
        delta = floor(delta);
    points -= delta * decrease;

    if (points < 0.0)
        points = 0.0;
    
    if (m_round)
        points = round(points);

    return points;
}   // getScoreForPosition
//-----------------------------------------------------------------------------

bool ExponentialGapScoring::canGetScoreForPosition(int p, const std::map<int, float>& race_times) const
{
    if (p == 1 || race_times.count(1))
        return true;
    return false;
}   // canGetScoreForPosition
//-----------------------------------------------------------------------------