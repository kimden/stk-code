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

#include "utils/crown_manager.hpp"

#include "network/server_config.hpp"
#include "utils/hourglass_reason.hpp"
#include "network/stk_host.hpp"
#include "network/stk_peer.hpp"
#include "network/protocols/server_lobby.hpp"
#include "utils/lobby_settings.hpp"
#include "utils/lobby_asset_manager.hpp"
#include "utils/team_manager.hpp"
#include "utils/tournament.hpp"

#include <cmath>


namespace
{
    const std::string g_hr_unknown            = "For some reason, server doesn't know about the hourglass status of player %s.";
    const std::string g_hr_none               = "%s can play (but if hourglass is present, there are not enough slots on the server).";
    const std::string g_hr_absent_peer        = "Player %s is not present on the server.";
    const std::string g_hr_not_tournament     = "%s is not a tournament player for this game.";
    const std::string g_hr_limit_spectator    = "Not enough slots to fit %s.";
    const std::string g_hr_no_karts_after     = "After applying all kart filters, %s doesn't have karts to play.";
    const std::string g_hr_no_maps_after      = "After applying all map filters, %s doesn't have maps to play.";
    const std::string g_hr_lack_required_maps = "%s lacks required maps.";
    const std::string g_hr_addon_karts_thr    = "Player %s doesn't have enough addon karts.";
    const std::string g_hr_addon_tracks_thr   = "Player %s doesn't have enough addon tracks.";
    const std::string g_hr_addon_arenas_thr   = "Player %s doesn't have enough addon arenas.";
    const std::string g_hr_addon_fields_thr   = "Player %s doesn't have enough addon fields.";
    const std::string g_hr_off_karts_thr      = "The number of official karts for %s is lower than the threshold.";
    const std::string g_hr_off_tracks_thr     = "The number of official tracks for %s is lower than the threshold.";
    const std::string g_hr_empty              = "";
}   // namespace
//-----------------------------------------------------------------------------

void CrownManager::setupContextUser()
{
    m_only_host_riding = ServerConfig::m_only_host_riding;
    m_owner_less       = ServerConfig::m_owner_less;
    m_sleeping_server  = ServerConfig::m_sleeping_server;
}   // setupContextUser
//-----------------------------------------------------------------------------

std::set<std::shared_ptr<STKPeer>>& CrownManager::getSpectatorsByLimit(bool update)
{
    if (!update)
        return m_spectators_by_limit;

    m_why_peer_cannot_play.clear();
    m_spectators_by_limit.clear();

    auto peers = STKHost::get()->getPeers();

    unsigned player_limit = getSettings()->getServerMaxPlayers();

    // If the server has an in-game player limit lower than the lobby limit, apply it,
    // A value of 0 for this parameter means no limit.
    unsigned current_max_players_in_game = getSettings()->getCurrentMaxPlayersInGame();
    if (current_max_players_in_game > 0)
        player_limit = std::min(player_limit, current_max_players_in_game);

    // only 10 players allowed for FFA and 14 for CTF and soccer
    if (RaceManager::get()->getMinorMode() ==
            RaceManager::MINOR_MODE_FREE_FOR_ALL)
        player_limit = std::min(player_limit, 10u);

    if (RaceManager::get()->getMinorMode() ==
            RaceManager::MINOR_MODE_CAPTURE_THE_FLAG)
        player_limit = std::min(player_limit, 14u);

    if (RaceManager::get()->getMinorMode() ==
            RaceManager::MINOR_MODE_SOCCER)
        player_limit = std::min(player_limit, 14u);

    unsigned ingame_players = 0, waiting_players = 0, total_players = 0;
    STKHost::get()->updatePlayers(&ingame_players, &waiting_players, &total_players);

    for (int i = 0; i < (int)peers.size(); ++i)
    {
        if (!peers[i]->isValidated())
        {
            std::swap(peers[i], peers.back());
            peers.pop_back();
            --i;
        }
    }

    std::sort(peers.begin(), peers.end(),
        [](const std::shared_ptr<STKPeer>& a, const std::shared_ptr<STKPeer>& b)
        {
            return defaultOrderComparator(a, b);
        }
    );

    unsigned player_count = 0;

    bool is_racing = getLobby()->isStateAtLeastRacing();

    if (is_racing)
    {
        for (auto &peer : peers)
            if (peer->isSpectator())
                ingame_players -= (int)peer->getPlayerProfiles().size();
        player_count = ingame_players;
    }

    for (unsigned i = 0; i < peers.size(); i++)
    {
        auto& peer = peers[i];
        if (!peer->isValidated())
            continue;
        bool ignore = false;
        if (!is_racing)
        {
            if (peer->alwaysSpectate() || peer->isWaitingForGame())
                ignore = true;
            else if (!canRace(peer))
            {
                m_spectators_by_limit.insert(peer);
                ignore = true;
            }
        }
        else
        {
            if (!peer->isWaitingForGame())
                ignore = true; // we already counted them properly in ingame_players
        }
        if (!ignore)
        {
            player_count += (unsigned)peer->getPlayerProfiles().size();
            if (player_count > player_limit)
                m_spectators_by_limit.insert(peer);
        }
    }
    return m_spectators_by_limit;
}   // getSpectatorsByLimit

//-----------------------------------------------------------------------------

bool CrownManager::canRace(std::shared_ptr<STKPeer> peer)
{
    auto it = m_why_peer_cannot_play.find(peer);
    if (it != m_why_peer_cannot_play.end())
        return it->second == 0;

    if (!peer || peer->getPlayerProfiles().empty())
    {
        m_why_peer_cannot_play[peer] = HR_ABSENT_PEER;
        return false;
    }
    std::string username = peer->getMainName();
    if (getTournament() && !getTournament()->canPlay(username))
    {
        m_why_peer_cannot_play[peer] = HR_NOT_A_TOURNAMENT_PLAYER;
        return false;
    }
    else if (m_spectators_by_limit.find(peer) != m_spectators_by_limit.end())
    {
        m_why_peer_cannot_play[peer] = HR_SPECTATOR_BY_LIMIT;
        return false;
    }

    if (!getAssetManager()->getMissingAssets(peer).empty())
    {
        m_why_peer_cannot_play[peer] = HR_LACKING_REQUIRED_MAPS;
        return false;
    }

    if (peer->addon_karts_count < getAssetManager()->getAddonKartsPlayThreshold())
    {
        m_why_peer_cannot_play[peer] = HR_ADDON_KARTS_PLAY_THRESHOLD;
        return false;
    }
    if (peer->addon_tracks_count < getAssetManager()->getAddonTracksPlayThreshold())
    {
        m_why_peer_cannot_play[peer] = HR_ADDON_TRACKS_PLAY_THRESHOLD;
        return false;
    }
    if (peer->addon_arenas_count < getAssetManager()->getAddonArenasPlayThreshold())
    {
        m_why_peer_cannot_play[peer] = HR_ADDON_ARENAS_PLAY_THRESHOLD;
        return false;
    }
    if (peer->addon_soccers_count < getAssetManager()->getAddonSoccersPlayThreshold())
    {
        m_why_peer_cannot_play[peer] = HR_ADDON_FIELDS_PLAY_THRESHOLD;
        return false;
    }

    std::set<std::string> maps = peer->getClientAssets().second;
    std::set<std::string> karts = peer->getClientAssets().first;

    float karts_fraction = getAssetManager()->officialKartsFraction(karts);
    float maps_fraction = getAssetManager()->officialMapsFraction(maps);

    if (karts_fraction < getAssetManager()->getOfficialKartsPlayThreshold())
    {
        m_why_peer_cannot_play[peer] = HR_OFFICIAL_KARTS_PLAY_THRESHOLD;
        return false;
    }
    if (maps_fraction < getAssetManager()->getOfficialTracksPlayThreshold())
    {
        m_why_peer_cannot_play[peer] = HR_OFFICIAL_TRACKS_PLAY_THRESHOLD;
        return false;
    }
    
    getAssetManager()->applyAllMapFilters(maps, true);
    getAssetManager()->applyAllKartFilters(username, karts, false);

    if (karts.empty())
    {
        m_why_peer_cannot_play[peer] = HR_NO_KARTS_AFTER_FILTER;
        return false;
    }
    if (maps.empty())
    {
        m_why_peer_cannot_play[peer] = HR_NO_MAPS_AFTER_FILTER;
        return false;
    }

    m_why_peer_cannot_play[peer] = 0;
    return true;
}   // canRace
//-----------------------------------------------------------------------------

std::string CrownManager::getWhyPeerCannotPlayAsString(
            const std::shared_ptr<STKPeer>& player_peer) const
{
    auto it = m_why_peer_cannot_play.find(player_peer);
    std::string response;
    if (it == m_why_peer_cannot_play.end())
    {
        Log::error("LobbySettings", "Hourglass status undefined for a player!");
        return g_hr_unknown;
    }
    
    switch (it->second)
    {
        case HR_NONE:                           return g_hr_none;
        case HR_ABSENT_PEER:                    return g_hr_absent_peer;
        case HR_NOT_A_TOURNAMENT_PLAYER:        return g_hr_not_tournament;
        case HR_SPECTATOR_BY_LIMIT:             return g_hr_limit_spectator;
        case HR_NO_KARTS_AFTER_FILTER:          return g_hr_no_karts_after;
        case HR_NO_MAPS_AFTER_FILTER:           return g_hr_no_maps_after;
        case HR_LACKING_REQUIRED_MAPS:          return g_hr_lack_required_maps;
        case HR_ADDON_KARTS_PLAY_THRESHOLD:     return g_hr_addon_karts_thr;
        case HR_ADDON_TRACKS_PLAY_THRESHOLD:    return g_hr_addon_tracks_thr;
        case HR_ADDON_ARENAS_PLAY_THRESHOLD:    return g_hr_addon_arenas_thr;
        case HR_ADDON_FIELDS_PLAY_THRESHOLD:    return g_hr_addon_fields_thr;
        case HR_OFFICIAL_KARTS_PLAY_THRESHOLD:  return g_hr_off_karts_thr;
        case HR_OFFICIAL_TRACKS_PLAY_THRESHOLD: return g_hr_off_tracks_thr;
        default:                                return g_hr_empty;
    }
}   // getWhyPeerCannotPlayAsString
//-----------------------------------------------------------------------------

void CrownManager::setSpectateModeProperly(std::shared_ptr<STKPeer> peer, AlwaysSpectateMode mode)
{
    peer->setDefaultAlwaysSpectate(mode);
    peer->setAlwaysSpectate(mode);
    getLobby()->onSpectatorStatusChange(peer);
    if (mode == ASM_NONE)
        getTeamManager()->checkNoTeamSpectator(peer);
}   // setSpectateModeProperly
//-----------------------------------------------------------------------------

// This was a sorting comparator in original code.
// Peers are guaranteed to already be validated and non-AI,
// and eligible for crown (theoretically)
std::shared_ptr<STKPeer> CrownManager::getFirstInCrownOrder(
        const std::vector<std::shared_ptr<STKPeer>>& peers)
{
    if (peers.empty()) // Shouldn't happen but just in case
        return {};

    unsigned best = 0;
    for (unsigned i = 1; i < peers.size(); ++i)
        if (defaultCrownComparator(peers[i], peers[best]))
            best = i;

    return peers[best];
}   // getFirstInCrownOrder
//-----------------------------------------------------------------------------

bool CrownManager::defaultCrownComparator(
        const std::shared_ptr<STKPeer> a,
        const std::shared_ptr<STKPeer> b)
{
    if (a->isCommandSpectator() ^ b->isCommandSpectator())
        return b->isCommandSpectator();

    return defaultOrderComparator(a, b);
}   // defaultCrownComparator
//-----------------------------------------------------------------------------

bool CrownManager::defaultOrderComparator(
        const std::shared_ptr<STKPeer> a,
        const std::shared_ptr<STKPeer> b)
{
    return a->getRejoinTime() < b->getRejoinTime();
}   // defaultOrderComparator
//-----------------------------------------------------------------------------