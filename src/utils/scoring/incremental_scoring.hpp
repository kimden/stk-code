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

#ifndef INCREMENTAL_SCORING_HPP
#define INCREMENTAL_SCORING_HPP

#include "utils/scoring/scoring.hpp"

class IncrementalScoring: public Scoring
{
public:
    void fromIntParamString(const std::string& input) final;

    void fromString(const std::string& input) final;

    bool isStandard() const final { return false; }

    void refreshCustomScores(int num_karts, std::vector<int>& score_for_position) final;

    int getScoreForPosition(int p, float time,
            std::map<int, float>& race_times,
            const std::vector<int>& score_for_position) const final;

    bool canGetScoreForPosition(int p, const std::map<int, float>& race_times) const final;

    std::string toString() const;
};


#endif // INCREMENTAL_SCORING_HPP