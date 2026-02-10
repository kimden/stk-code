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

#include "utils/scoring/scoring.hpp"
#include "utils/scoring/exponential_gap_scoring.hpp"
#include "utils/scoring/fixed_scoring.hpp"
#include "utils/scoring/incremental_scoring.hpp"
#include "utils/scoring/linear_gap_scoring.hpp"
#include "utils/scoring/standard_scoring.hpp"
#include "utils/string_utils.hpp"

#include <stdexcept>

namespace
{
    struct ScoringAndDescription
    {
        std::shared_ptr<Scoring> scoring;
        std::string description;
    };

    ScoringAndDescription scoringObjectAndDesc(const std::string& input)
    {
        std::string type;
        std::string description;
        if (input == "")
        {
            type = "standard";
            description = "";
        }
        else
        {
            size_t pos = input.find(' ');
            if (pos == std::string::npos)
                throw std::logic_error("Scoring description should start with type and space");
            
            type = input.substr(0, pos);
            description = input.substr(pos + 1);
        }

        std::shared_ptr<Scoring> res;
        if (type == "standard")
            res = std::make_shared<StandardScoring>();
        else if (type == "linear-gap")
            res = std::make_shared<LinearGapScoring>();
        else if (type == "exp-gap")
            res = std::make_shared<ExponentialGapScoring>();
        else if (type == "fixed")
            res = std::make_shared<FixedScoring>();
        else if (type == "inc")
            res = std::make_shared<IncrementalScoring>();
        else
            throw std::logic_error(StringUtils::insertValues(
                "Unknown scoring type %s", type.c_str()));
        
        return {res, description};
    }   // scoringObjectAndDesc
}

std::shared_ptr<Scoring> Scoring::unknownScoringFromIntParamString(const std::string& input)
{
    auto [res, description] = std::move(scoringObjectAndDesc(input));
    res->fromIntParamString(description);
    return res;    
}

std::shared_ptr<Scoring> Scoring::unknownScoringFromString(const std::string& input)
{
    auto [res, description] = std::move(scoringObjectAndDesc(input));
    res->fromString(description);
    return res;    
}
