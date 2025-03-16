//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2024-2025 kimden
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

#ifndef MAP_VOTE_HANDLER_HPP
#define MAP_VOTE_HANDLER_HPP

#include "irrString.h"
#include "network/peer_vote.hpp"
#include "utils/lobby_context.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

class LobbySettings;

// A class containing different implementations for handling map votes
// in a server lobby.
// It can be extended to store some information about previous choices,
// but currently it doesn't store or use it.

class MapVoteHandler: public LobbyContextComponent
{
    // STANDARD = 0
    // RANDOM = 1
    // ADVANCED = 2
private:
    int algorithm;

    template<typename T>
    static void findMajorityValue(const std::map<T, unsigned>& choices,
            unsigned cur_players, T* best_choice, float* rate);

    bool standard(std::map<uint32_t, PeerVote>& peers_votes,
            float remaining_time, float max_time, bool is_over,
            unsigned cur_players, PeerVote* winner_vote) const;

    bool random(std::map<uint32_t, PeerVote>& peers_votes,
            float remaining_time, float max_time, bool is_over,
            unsigned cur_players, PeerVote* winner_vote) const;
public:
    MapVoteHandler(LobbyContext* context): LobbyContextComponent(context) {}
    
    void setupContextUser() OVERRIDE;
    void setAlgorithm(int x)                                 { algorithm = x; }
    int getAlgorithm() const                              { return algorithm; }

    bool handleAllVotes(std::map<uint32_t, PeerVote>& peers_votes,
            float remaining_time, float max_time, bool is_over,
            unsigned cur_players, PeerVote* winner_vote) const;

};
#endif // MAP_VOTE_HANDLER_HPP
