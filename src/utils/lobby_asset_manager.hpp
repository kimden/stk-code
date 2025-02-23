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

#ifndef LOBBY_ASSET_MANAGER_HPP
#define LOBBY_ASSET_MANAGER_HPP

#include "utils/types.hpp"

#include <memory>
#include <string>
#include <set>
#include <vector>
class ServerLobby;
class STKPeer;

class LobbyAssetManager
{
public:
    LobbyAssetManager(ServerLobby* lobby);

    void init();

    void updateAddons();
    void updateMapsForMode(RaceManager::MinorRaceModeType mode);
    void onServerSetup();
    void eraseAssetsWithPeers(const std::vector<std::shared_ptr<STKPeer>>& peers);
    bool tryApplyingMapFilters();
    std::string getRandomAvailableMap();
    void encodePlayerKartsAndCommonMaps(NetworkString* ns, const std::set<std::string>& all_k);
    bool handleAssetsForPeer(std::shared_ptr<STKPeer> peer,
            const std::set<std::string>& client_karts,
            const std::set<std::string>& client_maps);
    std::array<int, AS_TOTAL> getAddonScores(
        const std::set<std::string>& client_karts,
        const std::set<std::string>& client_maps);
    std::string getAnyMapForVote();
    bool checkIfNoCommonMaps(
            const std::pair<std::set<std::string>, std::set<std::string>>& assets);
    bool isKartAvailable(const std::string& kart) const;
    float officialKartsFraction(const std::set<std::string>& clientKarts) const;
    float officialMapsFraction(const std::set<std::string>& clientMaps) const;


    std::string getRandomMap() const;
    std::string getRandomAddonMap() const;

    std::set<std::string> getAvailableKarts() const
                                              { return k_available_kts.first; }
    void setMustHaveMaps(const std::string& input)
                  { m_must_have_maps = StringUtils::split(input, ' ', false); }

    std::set<std::string> getAddonKarts() const   { return k_addon_kts.first; }
    std::set<std::string> getAddonTracks() const { return k_addon_kts.second; }
    std::set<std::string> getAddonArenas() const     { return k_addon_arenas; }
    std::set<std::string> getAddonSoccers() const   { return k_addon_soccers; }

    bool hasAddonKart(const std::string& id) const
              { return k_addon_kts.first.find(id) != k_addon_kts.first.end(); }
    bool hasAddonTrack(const std::string& id) const
            { return k_addon_kts.second.find(id) != k_addon_kts.second.end(); }
    bool hasAddonArena(const std::string& id) const
                    { return k_addon_arenas.find(id) != k_addon_arenas.end(); }
    bool hasAddonSoccer(const std::string& id) const
                  { return k_addon_soccers.find(id) != k_addon_soccers.end(); }

private:
    ServerLobby* m_lobby;

public:
    /** Official karts and maps available in server. */
    std::pair<std::set<std::string>, std::set<std::string> > k_official_kts;

    /** Addon karts and maps available in server. */
    std::pair<std::set<std::string>, std::set<std::string> > k_addon_kts;

    /** Addon arenas available in server. */
    std::set<std::string> k_addon_arenas;

    /** Addon soccers available in server. */
    std::set<std::string> k_addon_soccers;

    /** Available karts and maps for all clients, this will be initialized
     *  with data in server first. */
    std::pair<std::set<std::string>, std::set<std::string> > k_available_kts;

    /** Available karts and maps for all clients, this will be initialized
     *  with data in server first. */
    std::pair<std::set<std::string>, std::set<std::string> > k_entering_kts;

    std::vector<std::string> m_must_have_maps;
};

#endif
