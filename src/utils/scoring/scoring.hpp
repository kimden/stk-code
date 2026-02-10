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

#ifndef SCORING_HPP
#define SCORING_HPP

#include <string>
#include <map>
#include <vector>
#include <memory>

class Scoring
{
protected:
    double m_pole_points        = 0.;
    double m_fastest_lap_points = 0.;
    double m_quit_points        = 0.;
    double m_absence_points     = 0.;

    bool m_allow_negative = true;
    bool m_allow_pure_negative = false;
    bool m_round = true;

public:
    static std::shared_ptr<Scoring> unknownScoringFromIntParamString(const std::string& input);
    static std::shared_ptr<Scoring> unknownScoringFromString(const std::string& input);

    virtual void fromIntParamString(const std::string& input) = 0;
    virtual void fromString(const std::string& input) = 0;
    virtual bool isStandard() const = 0;
    virtual void refreshCustomScores(int num_karts, std::vector<int>& score_for_position) = 0;
    virtual int getScoreForPosition(int p, float time,
            std::map<int, float>& race_times,
            const std::vector<int>& score_for_position) const = 0;
    virtual bool canGetScoreForPosition(int p, const std::map<int, float>& race_times) const = 0;
    virtual std::string toString() const = 0;

    double getPolePoints()       const { return m_pole_points; }
    double getFastestLapPoints() const { return m_fastest_lap_points; }
    double getQuitPoints()       const { return m_quit_points; }
    double getAbsencePoints()    const { return m_absence_points; }

    static double parseThousandth(std::vector<std::string>& argv, int idx)
    {
        int temp;
        if (!StringUtils::parseString<int>(argv[idx], &temp))
            throw std::domain_error(StringUtils::insertValues(
                "Argument %s is not a valid integer", idx));
        
        return temp * 0.001;
    }

    template<typename T>
    static T parse(std::vector<std::string>& argv, T idx)
    {
        T temp;
        if (!StringUtils::parseString<T>(argv[idx], &temp))
            throw std::domain_error(StringUtils::insertValues(
                "Argument %s cannot be parsed", idx));
        
        return temp;
    }
};


#endif // SCORING_HPP