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

#include "network/stk_peer.hpp"
#include "race/race_manager.hpp"
#include "utils/lobby_context.hpp"
#include "utils/track_filter.hpp"
#include "utils/types.hpp"

#include <memory>
#include <set>
#include <string>
#include <vector>

class ServerLobby;
class STKPeer;

class LobbyAssetManager: public LobbyContextComponent
{
public:
    LobbyAssetManager(LobbyContext* context): LobbyContextComponent(context) {}
    
    void setupContextUser() OVERRIDE;

    void init();
    void initAvailableTracks();
    void updateAddons();
    void updateMapsForMode(RaceManager::MinorRaceModeType mode);
    void onServerSetup();
    void eraseAssetsWithPeers(const std::vector<std::shared_ptr<STKPeer>>& peers);
    bool tryApplyingMapFilters();
    std::string getRandomAvailableMap();

    AssetsPacket2 encodePlayerKartsAndCommonMaps(
            const std::set<std::string>& all_k);

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
                                              { return m_available_kts.first; }
    void setMustHaveMaps(const std::string& input);
    void gameFinishedOn(const std::string& map_name);

    // kimden: default -1 should be called by its own name
    void applyAllMapFilters(std::set<std::string>& maps, bool use_history, int known_number = -1) const;
    void applyAllKartFilters(const std::string& username, std::set<std::string>& karts, bool afterSelection = false) const;

    void applyGlobalFilter(FilterContext& map_context) const;
    void applyGlobalKartsFilter(FilterContext& kart_context) const;

    int checkCanPlay(std::shared_ptr<STKPeer> peer, int known_number);

    std::string getKartForBadKartChoice(
            std::shared_ptr<STKPeer> peer,
            const std::string& username,
            const std::string& check_choice) const;

    std::set<std::string> getAddonKarts() const   { return m_addon_kts.first; }
    std::set<std::string> getAddonTracks() const { return m_addon_kts.second; }
    std::set<std::string> getAddonArenas() const     { return m_addon_arenas; }
    std::set<std::string> getAddonSoccers() const   { return m_addon_soccers; }

    bool hasAddonKart(const std::string& id) const
              { return m_addon_kts.first.find(id) != m_addon_kts.first.end(); }
    bool hasAddonTrack(const std::string& id) const
            { return m_addon_kts.second.find(id) != m_addon_kts.second.end(); }
    bool hasAddonArena(const std::string& id) const
                    { return m_addon_arenas.find(id) != m_addon_arenas.end(); }
    bool hasAddonSoccer(const std::string& id) const
                  { return m_addon_soccers.find(id) != m_addon_soccers.end(); }

    std::vector<std::string> getMissingAssets(std::shared_ptr<STKPeer> peer) const;

    float getOfficialKartsPlayThreshold()  const { return m_official_karts_play_threshold;  }
    float getOfficialTracksPlayThreshold() const { return m_official_tracks_play_threshold; }
    int getAddonKartsJoinThreshold()       const { return m_addon_karts_join_threshold;     }
    int getAddonTracksJoinThreshold()      const { return m_addon_tracks_join_threshold;    }
    int getAddonArenasJoinThreshold()      const { return m_addon_arenas_join_threshold;    }
    int getAddonSoccersJoinThreshold()     const { return m_addon_soccers_join_threshold;   }
    int getAddonKartsPlayThreshold()       const { return m_addon_karts_play_threshold;     }
    int getAddonTracksPlayThreshold()      const { return m_addon_tracks_play_threshold;    }
    int getAddonArenasPlayThreshold()      const { return m_addon_arenas_play_threshold;    }
    int getAddonSoccersPlayThreshold()     const { return m_addon_soccers_play_threshold;   }
    float getOfficialKartsThreshold()      const { return m_official_karts_threshold;       }
    float getOfficialTracksThreshold()     const { return m_official_tracks_threshold;      }

public:
    /** Official karts and maps available in server. */
    std::pair<std::set<std::string>, std::set<std::string> > m_official_kts;

    /** Addon karts and maps available in server. */
    std::pair<std::set<std::string>, std::set<std::string> > m_addon_kts;

    /** Addon arenas available in server. */
    std::set<std::string> m_addon_arenas;

    /** Addon soccers available in server. */
    std::set<std::string> m_addon_soccers;

    /** Available karts and maps for all clients, this will be initialized
     *  with data in server first. */
    std::pair<std::set<std::string>, std::set<std::string> > m_available_kts;

    /** Available karts and maps for all clients, this will be initialized
     *  with data in server first. */
    std::pair<std::set<std::string>, std::set<std::string> > m_entering_kts;

    std::vector<std::string> m_must_have_maps;

    std::vector<std::string> m_map_history;

    TrackFilter m_global_filter;

    KartFilter m_global_karts_filter;

    std::vector<std::string> m_play_requirement_tracks;

    float m_official_karts_play_threshold;
    float m_official_tracks_play_threshold;
    int m_addon_karts_join_threshold;
    int m_addon_tracks_join_threshold;
    int m_addon_arenas_join_threshold;
    int m_addon_soccers_join_threshold;
    int m_addon_arenas_play_threshold;
    int m_addon_karts_play_threshold;
    int m_addon_soccers_play_threshold;
    int m_addon_tracks_play_threshold;
    float m_official_karts_threshold;
    float m_official_tracks_threshold;
};

#endif // LOBBY_ASSET_MANAGER_HPP
