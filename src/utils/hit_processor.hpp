//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2021 Heuchi1, 2025 kimden
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

#include <memory>
#include <string>
#include <vector>

class Kart;
class LobbySettings;
class ServerLobby;

// A class that incapsulates most of the logic for processing kart hits.
// Contains the code originally written by Heuchi1, but it's moved to a
// separate file to not include ServerLobby from item source files.

// Note that this class currently (March 2025) processes *team* hits.
// It will be modified to process all hits later.

class HitProcessor
{
public:
    HitProcessor(ServerLobby* lobby, std::shared_ptr<LobbySettings> settings);

private:
    ServerLobby* m_lobby = nullptr;
    std::shared_ptr<LobbySettings> m_lobby_settings;

    bool m_troll_active; // Whether the anti-troll system is active.

    bool m_show_hits; // Whether to show messages about team hits.

    bool m_hit_mode;  // Whether to anvil the team hitters.
    
    int m_last_hit_msg; // Last tick when the message was shown.


    // We have to keep track of the karts affected by a hit
    // We store IDs, because we need to find the team by name (by ID)
    // m_collecting_hit_info is set to true if we are processing a
    // cake or bowl hit, so we make sure we never fill the vectors with
    // unneeded data.
    bool m_collecting_hit_info;
    unsigned int m_current_item_owner_id;
    uint16_t m_ticks_since_thrown;
    std::vector<unsigned int> m_karts_hit;
    std::vector<unsigned int> m_karts_exploded;

    // Store karts to punish for swattering a teammate
    std::vector<Kart*> m_swatter_punish;

private:

    void processHitMessage(const std::string& owner_name, int owner_team);
    void processTeammateHit(Kart* owner, const std::string& owner_name, int owner_team);
    void punishKart(Kart* kart, float value = 1.0f, float value2 = 1.0f);
    
public:
    bool isAntiTrollActive() const                   { return m_troll_active; }
    bool isTeammateHitMode() const                       { return m_hit_mode; }
    bool showTeammateHits() const                       { return m_show_hits; }
    void setAntiTroll(bool value)                   { m_troll_active = value; }
    void setTeammateHitMode(bool value)                 { m_hit_mode = value; }
    void setShowTeammateHits(bool value)               { m_show_hits = value; }

    void setTeammateHitOwner(unsigned int ownerID,
            uint16_t ticks_since_thrown = 0);

    void registerTeammateHit(unsigned int kartID);
    void registerTeammateExplode(unsigned int kartID);
    void handleTeammateHits();

    void handleSwatterHit(unsigned int ownerID, unsigned int victimID,
            bool success, bool has_hit_kart, uint16_t ticks_active);

    void handleAnvilHit(unsigned int ownerID, unsigned int victimID);

    void sendTeammateHitMsg(std::string& s);
    void punishSwatterHits();
    void resetLastHits()                                { m_last_hit_msg = 0; }
};

#endif // HIT_PROCESSOR_HPP
