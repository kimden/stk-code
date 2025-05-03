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

#include "utils/gp_scoring.hpp"

#include "network/server_config.hpp"
#include "utils/string_utils.hpp"
#include "utils/log.hpp"

#include <cmath>
#include <algorithm>


std::shared_ptr<GPScoring> GPScoring::createFromIntParamString(const std::string& input)
{
    std::shared_ptr<GPScoring> new_scoring = std::make_shared<GPScoring>();

    std::set<std::string> available_scoring_types = {
            "standard", "default", "", "inc", "fixed", "linear-gap", "exp-gap"
    };
    if (!input.empty())
    {
        std::vector<std::string> params = StringUtils::split(input, ' ');
        if (params.empty())
        {
            new_scoring->m_type = "";
            return new_scoring;
        }
        new_scoring->m_type = params[0];
        if (available_scoring_types.count(params[0]) == 0)
            throw std::logic_error("Unknown scoring type " + params[0]);

        for (unsigned i = 1; i < params.size(); i++)
        {
            int param;
            if (!StringUtils::fromString(params[i], param))
                throw std::logic_error("Unable to parse integer from custom scoring data");

            new_scoring->m_params.push_back(param);
        }
    }
    return new_scoring;
}   // createFromIntParamString
//-----------------------------------------------------------------------------

bool GPScoring::isStandard() const
{
    return m_type == ""
        || m_type == "standard"
        || m_type == "default";
}   // isStandard
//-----------------------------------------------------------------------------

void GPScoring::refreshCustomScores(int num_karts,
        std::vector<int>& score_for_position)
{
    if (m_type == "inc")
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
    }
    else if (m_type == "fixed")
    {
        score_for_position.clear();
        for (unsigned i = 2; i < m_params.size(); i++)
            score_for_position.push_back(m_params[i]);
        score_for_position.resize(num_karts, 0);
    }
    else if (m_type == "linear-gap"
        || m_type == "exp-gap")
    {
        score_for_position.clear();
        score_for_position.resize(num_karts, 0);
    }
}   // refreshCustomScores
//-----------------------------------------------------------------------------

int GPScoring::getPolePoints() const
{
    return m_params[0];
}   // getPolePoints
//-----------------------------------------------------------------------------

int GPScoring::getFastestLapPoints() const
{
    return m_params[1];
}   // getFastestLapPoints
//-----------------------------------------------------------------------------

int GPScoring::getScoreForPosition(int p, float time,
        std::map<int, float>& race_times,
        const std::vector<int>& score_for_position) const
{
    race_times[p] = time;
    if (m_type == "inc" || m_type == "fixed")
        return score_for_position[p - 1];
    if (m_type == "linear-gap"
        || m_type == "exp-gap")
    {
        double delta = time - race_times[1];
        if (m_type == "exp-gap")
        {
            if (race_times[1] < 1e-6) // just in case
                return 0;
            delta = log(time / race_times[1]) / log(2);
        }
        double points = m_params[2] * 0.001;
        bool continuous = (m_params[5] != 0);
        double time_step = m_params[3] * 0.001;
        double decrease = m_params[4] * 0.001;
        delta /= time_step;
        if (!continuous)
            delta = floor(delta);
        points -= delta * decrease;
        if (points < 0.0)
            points = 0.0;
        return round(points);
    }
    Log::error("GPScoring", "Unknown scoring type: %s. Giving 0 points", m_type.c_str());
    return 0;
}   // getScoreForPosition
//-----------------------------------------------------------------------------

bool GPScoring::canGetScoreForPosition(int p, const std::map<int, float>& race_times) const
{
    if (m_type == "linear-gap"
        || m_type == "exp-gap")
    {
        if (p == 1 || race_times.count(1))
            return true;
        return false;
    }
    return true;
}   // canGetScoreForPosition
//-----------------------------------------------------------------------------

std::string GPScoring::toString() const
{
    std::string res = m_type;
    for (int param: m_params)
        res += StringUtils::insertValues(" %d", param);
    return res;
}   // toString
//-----------------------------------------------------------------------------