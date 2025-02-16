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

#ifndef HIT_PROCESSOR_HPP
#define HIT_PROCESSOR_HPP

#include "utils/types.hpp"

#include <vector>
#include <string>
class AbstractKart;
class ServerLobby;

// A class that incapsulates most of the logic for processing kart hits.
// Contains the code originally written by Heuchi1, but it's moved to a
// separate file to not include ServerLobby from item source files.

class HitProcessor
{
public:
    HitProcessor(ServerLobby* lobby);

public: // feb16 remove todo revert to private
    // feb16 remove
    ServerLobby* m_lobby = nullptr;

    bool m_show_teammate_hits; // Whether to show messages about team hits.
    bool m_teammate_hit_mode; // Whether to anvil the team hitters.
    // time index of last team mate hit
    // make sure not to send too many of them
    int m_last_teammate_hit_msg;
    // we have to keep track of the karts affected by a hit
    // we store IDs, because we need to find the team by name (by ID)
    // m_collecting_teammate_hit_info is set to true if we are processing a
    // cake or bowl hit, so we make sure we never fill the vectors with
    // unneeded data.
    bool m_collecting_teammate_hit_info;
    unsigned int m_teammate_current_item_ownerID;
    uint16_t m_teammate_ticks_since_thrown;
    std::vector<unsigned int> m_teammate_karts_hit;
    std::vector<unsigned int> m_teammate_karts_exploded;

    std::vector<AbstractKart*> m_teammate_swatter_punish; // store karts to punish for swattering a teammate

    // after a certain time a bowl can be avoided and doesn't
    // trigger teammate hits anymore
    const float MAX_BOWL_TEAMMATE_HIT_TIME = 2.0f;
    
public:
    // handle cakes and bowls
    void setTeamMateHitOwner(unsigned int ownerID, uint16_t ticks_since_thrown = 0);
    void registerTeamMateHit(unsigned int kartID);
    void registerTeamMateExplode(unsigned int kartID);
    void handleTeamMateHits();

    // handle swatters
    void handleSwatterHit(unsigned int ownerID, unsigned int victimID, bool success, bool has_hit_kart, uint16_t ticks_active);
    // handle anvil
    void handleAnvilHit(unsigned int ownerID, unsigned int victimID);

    void sendTeamMateHitMsg(std::string& s);
    bool showTeamMateHits() const              { return m_show_teammate_hits; }
    bool useTeamMateHitMode() const             { return m_teammate_hit_mode; }
};


#endif // HIT_PROCESSOR_HPP
