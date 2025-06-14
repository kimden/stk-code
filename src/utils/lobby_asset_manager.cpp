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

#include "utils/lobby_asset_manager.hpp"

#include "karts/kart_properties.hpp"
#include "karts/kart_properties_manager.hpp"
#include "karts/official_karts.hpp"
#include "network/network_string.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/server_config.hpp"
#include "network/stk_host.hpp"
#include "network/stk_peer.hpp"
#include "tracks/track.hpp"
#include "tracks/track_manager.hpp"
#include "utils/kart_elimination.hpp"
#include "utils/lobby_queues.hpp"
#include "utils/lobby_settings.hpp"
#include "utils/random_generator.hpp"
#include "utils/string_utils.hpp"
#include "utils/tournament.hpp"


void LobbyAssetManager::setupContextUser()
{
    init();
    initAvailableTracks();
    updateAddons();
}   // setupContextUser
//-----------------------------------------------------------------------------

void LobbyAssetManager::init()
{
    std::shared_ptr<TrackManager> track_manager = TrackManager::get();

    std::vector<int> all_k =
        kart_properties_manager->getKartsInGroup("standard");
    std::vector<int> all_t =
        track_manager->getTracksInGroup("standard");
    std::vector<int> all_arenas =
        track_manager->getArenasInGroup("standard", false);
    std::vector<int> all_soccers =
        track_manager->getArenasInGroup("standard", true);
    all_t.insert(all_t.end(), all_arenas.begin(), all_arenas.end());
    all_t.insert(all_t.end(), all_soccers.begin(), all_soccers.end());

    m_official_kts.first = OfficialKarts::getOfficialKarts();
    for (int track : all_t)
    {
        Track* t = track_manager->getTrack(track);
        if (!t->isAddon())
            m_official_kts.second.insert(t->getIdent());
    }

    m_official_karts_play_threshold  = ServerConfig::m_official_karts_play_threshold;
    m_official_tracks_play_threshold = ServerConfig::m_official_tracks_play_threshold;
    m_addon_karts_join_threshold     = ServerConfig::m_addon_karts_join_threshold;
    m_addon_tracks_join_threshold    = ServerConfig::m_addon_tracks_join_threshold;
    m_addon_arenas_join_threshold    = ServerConfig::m_addon_arenas_join_threshold;
    m_addon_soccers_join_threshold   = ServerConfig::m_addon_soccers_join_threshold;
    m_addon_arenas_play_threshold    = ServerConfig::m_addon_arenas_play_threshold;
    m_addon_karts_play_threshold     = ServerConfig::m_addon_karts_play_threshold;
    m_addon_soccers_play_threshold   = ServerConfig::m_addon_soccers_play_threshold;
    m_addon_tracks_play_threshold    = ServerConfig::m_addon_tracks_play_threshold;
    m_official_karts_threshold       = ServerConfig::m_official_karts_threshold;
    m_official_tracks_threshold      = ServerConfig::m_official_tracks_threshold;
}   // init
//-----------------------------------------------------------------------------

void LobbyAssetManager::updateAddons()
{
    std::shared_ptr<TrackManager> track_manager = TrackManager::get();

    m_addon_kts.first.clear();
    m_addon_kts.second.clear();
    m_addon_arenas.clear();
    m_addon_soccers.clear();

    std::set<std::string> total_addons;
    for (unsigned i = 0; i < kart_properties_manager->getNumberOfKarts(); i++)
    {
        const KartProperties* kp =
            kart_properties_manager->getKartById(i);
        if (kp->isAddon())
            total_addons.insert(kp->getIdent());
    }
    for (unsigned i = 0; i < track_manager->getNumberOfTracks(); i++)
    {
        const Track* track = track_manager->getTrack(i);
        if (track->isAddon())
            total_addons.insert(track->getIdent());
    }

    for (auto& addon : total_addons)
    {
        const KartProperties* kp = kart_properties_manager->getKart(addon);
        if (kp && kp->isAddon())
        {
            m_addon_kts.first.insert(kp->getIdent());
            continue;
        }
        Track* t = track_manager->getTrack(addon);
        if (!t || !t->isAddon() || t->isInternal())
            continue;
        if (t->isArena())
            m_addon_arenas.insert(t->getIdent());
        if (t->isSoccer())
            m_addon_soccers.insert(t->getIdent());
        if (!t->isArena() && !t->isSoccer())
            m_addon_kts.second.insert(t->getIdent());
    }

    std::vector<std::string> all_k;
    for (unsigned i = 0; i < kart_properties_manager->getNumberOfKarts(); i++)
    {
        const KartProperties* kp = kart_properties_manager->getKartById(i);
        if (kp->isAddon())
            all_k.push_back(kp->getIdent());
    }
    std::set<std::string> oks = OfficialKarts::getOfficialKarts();
    if (all_k.size() >= 65536 - (unsigned)oks.size())
        all_k.resize(65535 - (unsigned)oks.size());
    for (const std::string& k : oks)
        all_k.push_back(k);
    if (getSettings()->isLivePlayers())
        m_available_kts.first = m_official_kts.first;
    else
        m_available_kts.first = { all_k.begin(), all_k.end() };
    m_entering_kts = m_available_kts;
}   // updateAddons
//-----------------------------------------------------------------------------

/** Called whenever server is reset or game mode is changed. */
void LobbyAssetManager::updateMapsForMode(RaceManager::MinorRaceModeType mode)
{
    std::shared_ptr<TrackManager> track_manager = TrackManager::get();
    
    auto all_t = track_manager->getAllTrackIdentifiers();
    if (all_t.size() >= 65536)
        all_t.resize(65535);
    m_available_kts.second = { all_t.begin(), all_t.end() };
    switch (mode)
    {
        case RaceManager::MINOR_MODE_NORMAL_RACE:
        case RaceManager::MINOR_MODE_TIME_TRIAL:
        case RaceManager::MINOR_MODE_FOLLOW_LEADER:
        {
            auto it = m_available_kts.second.begin();
            while (it != m_available_kts.second.end())
            {
                Track* t = track_manager->getTrack(*it);
                if (t->isArena() || t->isSoccer() || t->isInternal())
                {
                    it = m_available_kts.second.erase(it);
                }
                else
                    it++;
            }
            break;
        }
        case RaceManager::MINOR_MODE_FREE_FOR_ALL:
        case RaceManager::MINOR_MODE_CAPTURE_THE_FLAG:
        {
            auto it = m_available_kts.second.begin();
            while (it != m_available_kts.second.end())
            {
                Track* t = track_manager->getTrack(*it);
                if (RaceManager::get()->getMinorMode() ==
                    RaceManager::MINOR_MODE_CAPTURE_THE_FLAG)
                {
                    if (!t->isCTF() || t->isInternal())
                    {
                        it = m_available_kts.second.erase(it);
                    }
                    else
                        it++;
                }
                else
                {
                    if (!t->isArena() ||  t->isInternal())
                    {
                        it = m_available_kts.second.erase(it);
                    }
                    else
                        it++;
                }
            }
            break;
        }
        case RaceManager::MINOR_MODE_SOCCER:
        {
            auto it = m_available_kts.second.begin();
            while (it != m_available_kts.second.end())
            {
                Track* t = track_manager->getTrack(*it);
                if (!t->isSoccer() || t->isInternal())
                {
                    it = m_available_kts.second.erase(it);
                }
                else
                    it++;
            }
            break;
        }
        default:
            assert(false);
            break;
    }
    m_entering_kts = m_available_kts;
}   // updateMapsForMode
//-----------------------------------------------------------------------------

void LobbyAssetManager::onServerSetup()
{
    // We use maximum 16bit unsigned limit
    auto all_k = kart_properties_manager->getAllAvailableKarts();
    if (all_k.size() >= 65536)
        all_k.resize(65535);
    if (getSettings()->isLivePlayers())
        m_available_kts.first = m_official_kts.first;
    else
        m_available_kts.first = { all_k.begin(), all_k.end() };
}   // onServerSetup
//-----------------------------------------------------------------------------

void LobbyAssetManager::eraseAssetsWithPeers(
        const std::vector<std::shared_ptr<STKPeer>>& peers)
{
    std::set<std::string> karts_erase, maps_erase;
    for (const auto& peer: peers)
    {
        if (peer)
        {
            peer->eraseServerKarts(m_available_kts.first, karts_erase);
            peer->eraseServerTracks(m_available_kts.second, maps_erase);
        }
    }
    for (const std::string& kart_erase : karts_erase)
        m_available_kts.first.erase(kart_erase);

    for (const std::string& map_erase : maps_erase)
        m_available_kts.second.erase(map_erase);

} // eraseAssetsWithPeers
//-----------------------------------------------------------------------------

bool LobbyAssetManager::tryApplyingMapFilters()
{
    std::set<std::string> available_tracks_fallback = m_available_kts.second;
    applyAllMapFilters(m_available_kts.second, true);

   /* auto iter = m_available_kts.second.begin();
    while (iter != m_available_kts.second.end())
    {
        // Initial version which will be brought into a separate fuction
        std::string track = *iter;
        if (getTrackMaxPlayers(track) < max_player)
            iter = m_available_kts.second.erase(iter);
        else
            iter++;
    }*/

    if (m_available_kts.second.empty())
    {
        m_available_kts.second = available_tracks_fallback;
        return false;
    }
    return true;
}   // tryApplyingMapFilters
//-----------------------------------------------------------------------------

std::string LobbyAssetManager::getRandomAvailableMap()
{
    RandomGenerator rg;
    std::set<std::string>::iterator it = m_available_kts.second.begin();
    std::advance(it, rg.get((int)m_available_kts.second.size()));
    return *it;
}   // getRandomAvailableMap
//-----------------------------------------------------------------------------

AssetsPacket2 LobbyAssetManager::encodePlayerKartsAndCommonMaps(
        const std::set<std::string>& all_k)
{
    const auto& all_t = m_available_kts.second;

    AssetsPacket2 packet;
    packet.karts_number = (uint16_t)all_k.size();
    packet.maps_number = (uint16_t)all_t.size();

    for (const std::string& kart : all_k)
        packet.karts.push_back(kart);

    for (const std::string& track : all_t)
        packet.maps.push_back(track);

    return packet;
}   // encodePlayerKartsAndCommonMaps
//-----------------------------------------------------------------------------

bool LobbyAssetManager::handleAssetsForPeer(std::shared_ptr<STKPeer> peer,
        const std::set<std::string>& client_karts,
        const std::set<std::string>& client_tracks)
{
    // Drop this player if he doesn't have at least 1 kart / track the same
    // as server
    float okt = 0.0f;
    float ott = 0.0f;
    int addon_karts = 0;
    int addon_tracks = 0;
    int addon_arenas = 0;
    int addon_soccers = 0;
    for (auto& client_kart : client_karts)
    {
        if (m_official_kts.first.find(client_kart) !=
            m_official_kts.first.end())
            okt += 1.0f;
        if (m_addon_kts.first.find(client_kart) !=
            m_addon_kts.first.end())
            ++addon_karts;
    }
    okt = okt / (float)m_official_kts.first.size();
    for (auto& client_track : client_tracks)
    {
        if (m_official_kts.second.find(client_track) !=
            m_official_kts.second.end())
            ott += 1.0f;
        if (m_addon_kts.second.find(client_track) !=
            m_addon_kts.second.end())
            ++addon_tracks;
        if (m_addon_arenas.find(client_track) !=
            m_addon_arenas.end())
            ++addon_arenas;
        if (m_addon_soccers.find(client_track) !=
            m_addon_soccers.end())
            ++addon_soccers;
    }
    ott = ott / (float)m_official_kts.second.size();

    std::set<std::string> karts_erase, maps_erase;
    for (const std::string& server_kart : m_entering_kts.first)
    {
        if (client_karts.find(server_kart) == client_karts.end())
        {
            karts_erase.insert(server_kart);
        }
    }
    for (const std::string& server_track : m_entering_kts.second)
    {
        if (client_tracks.find(server_track) == client_tracks.end())
        {
            maps_erase.insert(server_track);
        }
    }

    auto settings = getSettings();

    Log::info("LobbyAssetManager", "Player has the following addons: %d/%d(%d) karts,"
        " %d/%d(%d) tracks, %d/%d(%d) arenas, %d/%d(%d) soccer fields.",
        addon_karts, getAddonKartsJoinThreshold(),
                     getAddonKartsPlayThreshold(),
        addon_tracks, getAddonTracksJoinThreshold(),
                      getAddonTracksPlayThreshold(),
        addon_arenas, getAddonArenasJoinThreshold(),
                      getAddonArenasPlayThreshold(),
        addon_soccers, getAddonSoccersJoinThreshold(),
                       getAddonSoccersPlayThreshold());

    bool bad = false;

    bool has_required_tracks = true;
    for (const std::string& required_track : m_must_have_maps)
    {
        if (client_tracks.find(required_track) == client_tracks.end())
        {
            has_required_tracks = false;
            bad = true;
            Log::verbose("LobbyAssetManager", "Player does not have a required track '%s'.", required_track.c_str());
            break;
        }
    }

    peer->addon_karts_count = addon_karts;
    peer->addon_tracks_count = addon_tracks;
    peer->addon_arenas_count = addon_arenas;
    peer->addon_soccers_count = addon_soccers;

    if (karts_erase.size() == m_entering_kts.first.size())
    {
        Log::verbose("LobbyAssetManager", "Bad player: no common karts with server");
        bad = true;
    }

    if (maps_erase.size() == m_entering_kts.second.size())
    {
        Log::verbose("LobbyAssetManager", "Bad player: no common tracks with server");
        bad = true;
    }

    if (okt < getOfficialKartsThreshold())
    {
        Log::verbose("LobbyAssetManager", "Bad player: bad official kart threshold");
        bad = true;
    }

    if (ott < getOfficialTracksThreshold())
    {
        Log::verbose("LobbyAssetManager", "Bad player: bad official track threshold");
        bad = true;
    }

    if (addon_karts < getAddonKartsJoinThreshold())
    {
        Log::verbose("LobbyAssetManager", "Bad player: too little addon karts");
        bad = true;
    }

    if (addon_tracks < getAddonTracksJoinThreshold())
    {
        Log::verbose("LobbyAssetManager", "Bad player: too little addon tracks");
        bad = true;
    }

    if (addon_arenas < getAddonArenasJoinThreshold())
    {
        Log::verbose("LobbyAssetManager", "Bad player: too little addon arenas");
        bad = true;
    }

    if (addon_soccers < getAddonSoccersJoinThreshold())
    {
        Log::verbose("LobbyAssetManager", "Bad player: too little addon soccers");
        bad = true;
    }

    if (!has_required_tracks)
    {
        Log::verbose("LobbyAssetManager", "Bad player: no required tracks");
        bad = true;
    }

    return !bad;
}   // handleAssetsForPeer
//-----------------------------------------------------------------------------

std::array<int, AS_TOTAL> LobbyAssetManager::getAddonScores(
        const std::set<std::string>& client_karts,
        const std::set<std::string>& client_tracks)
{
    std::array<int, AS_TOTAL> addons_scores = {{ -1, -1, -1, -1 }};
    size_t addon_kart = 0;
    size_t addon_track = 0;
    size_t addon_arena = 0;
    size_t addon_soccer = 0;

    for (auto& kart : m_addon_kts.first)
        if (client_karts.find(kart) != client_karts.end())
            addon_kart++;

    for (auto& track : m_addon_kts.second)
        if (client_tracks.find(track) != client_tracks.end())
            addon_track++;

    for (auto& arena : m_addon_arenas)
        if (client_tracks.find(arena) != client_tracks.end())
            addon_arena++;

    for (auto& soccer : m_addon_soccers)
        if (client_tracks.find(soccer) != client_tracks.end())
            addon_soccer++;

    if (!m_addon_kts.first.empty())
        addons_scores[AS_KART] = addon_kart;

    if (!m_addon_kts.second.empty())
        addons_scores[AS_TRACK] = addon_track;

    if (!m_addon_arenas.empty())
        addons_scores[AS_ARENA] = addon_arena;

    if (!m_addon_soccers.empty())
        addons_scores[AS_SOCCER] = addon_soccer;

    return addons_scores;
}   // getAddonScores
//-----------------------------------------------------------------------------

std::string LobbyAssetManager::getAnyMapForVote()
{
    return *m_available_kts.second.begin();
}   // getAnyMapForVote
//-----------------------------------------------------------------------------

bool LobbyAssetManager::checkIfNoCommonMaps(
        const std::pair<std::set<std::string>, std::set<std::string>>& assets)
{
    std::set<std::string> maps_erase;
    for (const std::string& server_track : m_available_kts.second)
    {
        if (assets.second.find(server_track) == assets.second.end())
        {
            maps_erase.insert(server_track);
        }
    }
    return maps_erase.size() == m_available_kts.second.size();
}   // checkIfNoCommonMaps
//-----------------------------------------------------------------------------

bool LobbyAssetManager::isKartAvailable(const std::string& kart) const
{
    return m_available_kts.first.find(kart) != m_available_kts.first.end();
}   // isKartAvailable
//-----------------------------------------------------------------------------

float LobbyAssetManager::officialKartsFraction(const std::set<std::string>& clientKarts) const
{
    int karts_count = 0;
    for (auto& kart : clientKarts)
    {
        if (m_official_kts.first.find(kart) != m_official_kts.first.end())
            karts_count += 1;
    }
    return karts_count / (float)m_official_kts.first.size();
}   // officialKartsFraction
//-----------------------------------------------------------------------------

float LobbyAssetManager::officialMapsFraction(const std::set<std::string>& clientMaps) const
{
    int maps_count = 0;
    for (auto& map : clientMaps)
    {
        if (m_official_kts.second.find(map) != m_official_kts.second.end())
            maps_count += 1;
    }
    return maps_count / (float)m_official_kts.second.size();
}   // officialMapsFraction
//-----------------------------------------------------------------------------

std::string LobbyAssetManager::getRandomMap() const
{
    std::set<std::string> items;
    for (const std::string& s: m_entering_kts.second) {
        items.insert(s);
    }
    applyAllMapFilters(items, false);
    if (items.empty())
        return "";
    RandomGenerator rg;
    std::set<std::string>::iterator it = items.begin();
    std::advance(it, rg.get((int)items.size()));
    return *it;
}   // getRandomMap
//-----------------------------------------------------------------------------

std::string LobbyAssetManager::getRandomAddonMap() const
{
    std::set<std::string> items;
    for (const std::string& s: m_entering_kts.second) {
        Track* t = TrackManager::get()->getTrack(s);
        if (t->isAddon())
            items.insert(s);
    }
    applyAllMapFilters(items, false);
    if (items.empty())
        return "";
    RandomGenerator rg;
    std::set<std::string>::iterator it = items.begin();
    std::advance(it, rg.get((int)items.size()));
    return *it;
}   // getRandomAddonMap
//-----------------------------------------------------------------------------

void LobbyAssetManager::setMustHaveMaps(const std::string& input)
{
    m_must_have_maps = StringUtils::split(input, ' ', false);
}   // setMustHaveMaps
//-----------------------------------------------------------------------------

void LobbyAssetManager::gameFinishedOn(const std::string& map_name)
{
    m_map_history.push_back(map_name);
}   // gameFinishedOn
//-----------------------------------------------------------------------------

void LobbyAssetManager::applyAllMapFilters(std::set<std::string>& maps, bool use_history, int known_number) const
{
    unsigned max_player = 0;
    if (known_number >= 0) // Careful with unsigned if you edit
        max_player = known_number;
    else
        STKHost::get()->updatePlayers(&max_player);

    if (RaceManager::get()->getMinorMode() == RaceManager::MINOR_MODE_FREE_FOR_ALL)
    {
        auto it = maps.begin();
        while (it != maps.end())
        {
            Track* t = TrackManager::get()->getTrack(*it);
            if (t && t->getMaxArenaPlayers() < max_player)
            {
                it = maps.erase(it);
            }
            else
                it++;
        }
    }

    // Please note that use_history refers to using queue filters too -
    // calls with false only get map sets, etc
    FilterContext map_context;
    map_context.username = ""; // unused
    map_context.num_players = max_player;
    map_context.wildcards = m_map_history;
    map_context.applied_at_selection_start = true;
    map_context.elements = maps;
    applyGlobalFilter(map_context);
    
    if (use_history)
    {
        if (isTournament())
            getTournament()->applyFiltersForThisGame(map_context);
        
        getQueues()->applyFrontMapFilters(map_context);
    }
    std::swap(maps, map_context.elements);
}   // applyAllMapFilters
//-----------------------------------------------------------------------------

void LobbyAssetManager::applyAllKartFilters(const std::string& username, std::set<std::string>& karts, bool afterSelection) const
{
    FilterContext kart_context;
    kart_context.username = username;
    kart_context.num_players = 0; // unused
    kart_context.wildcards = {}; // unused
    kart_context.applied_at_selection_start = !afterSelection;
    kart_context.elements = karts;

    applyGlobalKartsFilter(kart_context);
    getQueues()->applyFrontKartFilters(kart_context);
    swap(karts, kart_context.elements);
}   // applyAllKartFilters
//-----------------------------------------------------------------------------

std::string LobbyAssetManager::getKartForBadKartChoice(
            std::shared_ptr<STKPeer> peer,
            const std::string& username,
            const std::string& check_choice) const
{
    std::set<std::string> karts;
    if (peer->isAIPeer())
        karts = getAvailableKarts();
    else
        karts = peer->getClientAssets().first;

    applyAllKartFilters(username, karts, true);

    auto kart_elimination = getKartElimination();
    if (kart_elimination->isEliminated(username)
        && karts.count(kart_elimination->getKart()))
    {
        return kart_elimination->getKart();
    }

    if (!check_choice.empty() && karts.count(check_choice)) // valid choice
        return check_choice;

    // choice is invalid, roll the random
    RandomGenerator rg;
    std::set<std::string>::iterator it = karts.begin();
    std::advance(it, rg.get((int)karts.size()));
    return *it;
}   // getKartForRandomKartChoice
//-----------------------------------------------------------------------------

void LobbyAssetManager::applyGlobalFilter(FilterContext& map_context) const
{
    m_global_filter.apply(map_context);
}   // applyGlobalFilter
//-----------------------------------------------------------------------------

void LobbyAssetManager::applyGlobalKartsFilter(FilterContext& kart_context) const
{
    m_global_karts_filter.apply(kart_context);
}   // applyGlobalKartsFilter
//-----------------------------------------------------------------------------

int LobbyAssetManager::checkCanPlay(std::shared_ptr<STKPeer> peer, int known_number)
{
    if (!getMissingAssets(peer).empty())
        return HR_LACKING_REQUIRED_MAPS;

    if (peer->addon_karts_count < getAddonKartsPlayThreshold())
        return HR_ADDON_KARTS_PLAY_THRESHOLD;

    if (peer->addon_tracks_count < getAddonTracksPlayThreshold())
        return HR_ADDON_TRACKS_PLAY_THRESHOLD;

    if (peer->addon_arenas_count < getAddonArenasPlayThreshold())
        return HR_ADDON_ARENAS_PLAY_THRESHOLD;

    if (peer->addon_soccers_count < getAddonSoccersPlayThreshold())
        return HR_ADDON_FIELDS_PLAY_THRESHOLD;

    std::set<std::string> karts = peer->getClientAssets().first;
    std::set<std::string> maps = peer->getClientAssets().second;

    float karts_fraction = officialKartsFraction(karts);
    if (karts_fraction < getOfficialKartsPlayThreshold())
        return HR_OFFICIAL_KARTS_PLAY_THRESHOLD;

    float maps_fraction = officialMapsFraction(maps);
    if (maps_fraction < getOfficialTracksPlayThreshold())
        return HR_OFFICIAL_TRACKS_PLAY_THRESHOLD;

    applyAllKartFilters(peer->getMainName(), karts, false);
    if (karts.empty())
        return HR_NO_KARTS_AFTER_FILTER;

    applyAllMapFilters(maps, true, known_number);
    if (maps.empty())
        return HR_NO_MAPS_AFTER_FILTER;

    return HR_NONE;
}   // checkCanPlay
//-----------------------------------------------------------------------------

void LobbyAssetManager::initAvailableTracks()
{
    m_global_filter = TrackFilter(ServerConfig::m_only_played_tracks_string);
    m_global_karts_filter = KartFilter(ServerConfig::m_only_played_karts_string);
    setMustHaveMaps(ServerConfig::m_must_have_tracks_string);
    m_play_requirement_tracks = StringUtils::split(
            ServerConfig::m_play_requirement_tracks_string, ' ', false);
}   // initAvailableTracks
//-----------------------------------------------------------------------------

std::vector<std::string> LobbyAssetManager::getMissingAssets(
        std::shared_ptr<STKPeer> peer) const
{
    if (m_play_requirement_tracks.empty())
        return {};

    std::vector<std::string> ans;
    for (const std::string& required_track : m_play_requirement_tracks)
        if (peer->getClientAssets().second.count(required_track) == 0)
            ans.push_back(required_track);
    return ans;
}   // getMissingAssets
//-----------------------------------------------------------------------------