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

#include "utils/scoring/incremental_scoring.hpp"


void GPScoring::refreshCustomScores(int num_karts,
        std::vector<int>& score_for_position)
{
    // Testing indicates that number of parameters is not validated.
    // Do it when splitting into separate classes.
    score_for_position.clear();
    for (unsigned i = 2; i < m_params.size(); i++)
        score_for_position.push_back(m_params[i]);
    score_for_position.resize(num_karts, 0);
    std::sort(score_for_position.begin(), score_for_position.end());
    for (unsigned i = 1; i < score_for_position.size(); i++)
        score_for_position[i] += score_for_position[i - 1];
    std::reverse(score_for_position.begin(), score_for_position.end());
}   // refreshCustomScores


int GPScoring::getScoreForPosition(int p, float time,
        std::map<int, float>& race_times,
        const std::vector<int>& score_for_position) const
{
    race_times[p] = time;
    return score_for_position[p - 1];
}   // getScoreForPosition
//-----------------------------------------------------------------------------

bool GPScoring::canGetScoreForPosition(int p, const std::map<int, float>& race_times) const
{
    return true;
}   // canGetScoreForPosition
//-----------------------------------------------------------------------------