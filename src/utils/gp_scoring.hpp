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

#ifndef GP_SCORING_HPP
#define GP_SCORING_HPP

#include "irrString.h"

#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>

class GPScoring
{
public:
    static std::shared_ptr<GPScoring> createFromIntParamString(const std::string& input);
    bool isStandard() const;
    void refreshCustomScores(int num_karts,
            std::vector<int>& score_for_position);
    int getPolePoints() const;
    int getFastestLapPoints() const;
    int getScoreForPosition(int p, float time,
            std::map<int, float>& race_times,
            const std::vector<int>& score_for_position) const;
    bool canGetScoreForPosition(int p, const std::map<int, float>& race_times) const;
    std::string toString() const;

private:
    std::vector<int> m_params;
    std::string      m_type;
};

#endif // GP_SCORING_HPP