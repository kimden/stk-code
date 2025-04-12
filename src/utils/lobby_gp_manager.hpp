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

#ifndef LOBBY_GP_MANAGER_HPP
#define LOBBY_GP_MANAGER_HPP

#include "irrString.h"
#include "network/packet_types.hpp"
#include "utils/track_filter.hpp"
#include "utils/lobby_context.hpp"

#include <memory>
#include <queue>

class NetworkPlayerProfile;
class GPScoring;

struct GPScore
{
    int score = 0;
    double time = 0.;
    bool operator < (const GPScore& rhs) const
    {
        return (score < rhs.score || (score == rhs.score && time > rhs.time));
    }
    bool operator > (const GPScore& rhs) const
    {
        return (score > rhs.score || (score == rhs.score && time < rhs.time));
    }
};

class LobbyGPManager: public LobbyContextComponent
{
public:
    LobbyGPManager(LobbyContext* context): LobbyContextComponent(context) {}
    
    void setupContextUser() OVERRIDE;

    void onStartSelection();

    void setScoresToPlayer(std::shared_ptr<NetworkPlayerProfile> player) const;

    std::string getGrandPrixStandings(bool showIndividual = false, bool showTeam = true) const;

    void resetGrandPrix();

    void shuffleGPScoresWithPermutation(const std::map<int, int>& permutation);

    GPScoresPacket updateGPScores(std::vector<float>& gp_changes);

    bool trySettingGPScoring(const std::string& input);

    void updateWorldScoring() const;

    std::string getScoringAsString() const;

private:
    std::map<std::string, GPScore> m_gp_scores;

    std::map<int, GPScore> m_gp_team_scores;

    std::shared_ptr<GPScoring> m_gp_scoring;

};

#endif // LOBBY_GP_MANAGER_HPP