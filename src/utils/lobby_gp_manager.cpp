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

#include "utils/lobby_gp_manager.hpp"

#include "karts/abstract_kart.hpp"
#include "modes/linear_world.hpp"
#include "modes/world.hpp"
#include "modes/world_with_rank.hpp"
#include "network/game_setup.hpp"
#include "network/network_player_profile.hpp"
#include "network/network_string.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/server_config.hpp"
#include "utils/gp_scoring.hpp"
#include "utils/string_utils.hpp"
#include "utils/team_manager.hpp"

void LobbyGPManager::setupContextUser()
{
    if (!trySettingGPScoring(ServerConfig::m_gp_scoring))
        m_gp_scoring = {};
}   // setupContextUser
//-----------------------------------------------------------------------------

void LobbyGPManager::onStartSelection()
{
    if (!getGameSetupFromCtx()->isGrandPrixStarted())
    {
        m_gp_scores.clear();
        m_gp_team_scores.clear();
    }
}   // onStartSelection
//-----------------------------------------------------------------------------

void LobbyGPManager::setScoresToPlayer(std::shared_ptr<NetworkPlayerProfile> player) const
{
    std::string username = StringUtils::wideToUtf8(player->getName());
    if (getGameSetupFromCtx()->isGrandPrix())
    {
        auto it = m_gp_scores.find(username);
        if (it != m_gp_scores.end())
        {
            player->setScore(it->second.score);
            player->setOverallTime(it->second.time);
        }
    }
}   // setScoresToPlayer
//-----------------------------------------------------------------------------

std::string LobbyGPManager::getGrandPrixStandings(bool showIndividual, bool showTeam) const
{
    std::stringstream response;
    response << "Grand Prix standings";

    if (!showIndividual && !showTeam)
    {
        if (m_gp_team_scores.empty())
            showIndividual = true;
        else
            showTeam = true;
    }

    auto game_setup = getGameSetupFromCtx();
    int passed = (int)game_setup->getAllTracks().size();
    bool ongoing = false;
    if (getLobby()->isWorldPicked() && !getLobby()->isWorldFinished())
    {
        --passed;
        ongoing = true;
    }
    
    uint8_t total = game_setup->getExtraServerInfo();
    if (passed != 0 || ongoing)
    {
        response << " after " << (int)passed << " of " << (int)total << " games";
        if (ongoing)
            response << " (before this game)";
    }
    else
        response << ", " << (int)total << " games";

    response << ":\n";
    
    if (showIndividual)
    {
        std::vector<std::pair<GPScore, std::string>> results;
        for (auto &p: m_gp_scores)
            results.emplace_back(p.second, p.first);
        std::stable_sort(results.rbegin(), results.rend());
        for (unsigned i = 0; i < results.size(); i++)
        {
            response << (i + 1) << ". ";
            response << "  " << results[i].second;
            response << "  " << results[i].first.score;
            response << "  " << "(" << StringUtils::timeToString(results[i].first.time) << ")";
            response << "\n";
        }
    }

    if (showTeam)
    {
        if (!m_gp_team_scores.empty())
        {
            std::vector<std::pair<GPScore, int>> results2;
            if (showIndividual)
                response << "\n";
            for (auto &p: m_gp_team_scores)
                results2.emplace_back(p.second, p.first);
            std::stable_sort(results2.rbegin(), results2.rend());
            for (unsigned i = 0; i < results2.size(); i++)
            {
                response << (i + 1) << ". ";
                response << "  " << TeamUtils::getTeamByIndex(results2[i].second).getNameWithEmoji();
                response << "  " << results2[i].first.score;
                response << "  " << "(" << StringUtils::timeToString(results2[i].first.time) << ")";
                response << "\n";
            }
        }
    }
    return response.str();
}   // getGrandPrixStandings
//-----------------------------------------------------------------------------

void LobbyGPManager::resetGrandPrix()
{
    m_gp_scores.clear();
    m_gp_team_scores.clear();
    getGameSetupFromCtx()->stopGrandPrix();

    getLobby()->sendServerInfoToEveryone();
    getLobby()->updatePlayerList();
}   // resetGrandPrix
//-----------------------------------------------------------------------------

void LobbyGPManager::shuffleGPScoresWithPermutation(const std::map<int, int>& permutation)
{
    auto old_scores = m_gp_team_scores;
    m_gp_team_scores.clear();
    for (auto& p: old_scores)
    {
        auto it = permutation.find(p.first);
        if (it != permutation.end())
            m_gp_team_scores[it->second] = p.second;
        else
            m_gp_team_scores[p.first] = p.second;
    }
}   // shuffleGPScoresWithPermutation
//-----------------------------------------------------------------------------

GPScoresPacket LobbyGPManager::updateGPScores(std::vector<float>& gp_changes)
{
    // fastest lap
    int fastest_lap =
        static_cast<LinearWorld*>(World::getWorld())->getFastestLapTicks();
    irr::core::stringw fastest_kart_wide =
        static_cast<LinearWorld*>(World::getWorld())
        ->getFastestLapKartName();
    std::string fastest_kart = StringUtils::wideToUtf8(fastest_kart_wide);

    // all gp tracks
    auto game_setup = getGameSetupFromCtx();

    int points_fl = 0;
    // Commented until used to remove the warning
    // int points_pole = 0;
    WorldWithRank *wwr = dynamic_cast<WorldWithRank*>(World::getWorld());
    if (wwr)
    {
        points_fl = wwr->getFastestLapPoints();
        // Commented until used to remove the warning
        // points_pole = wwr->getPolePoints();
    }
    else
    {
        Log::error("LobbyGPManager",
                    "World with scores that is not a WorldWithRank??");
    }

    std::vector<int> last_scores;
    std::vector<int> cur_scores;
    std::vector<float> overall_times;

    for (unsigned i = 0; i < RaceManager::get()->getNumPlayers(); i++)
    {
        int last_score = (World::getWorld()->getKart(i)->isEliminated() ?
                0 : RaceManager::get()->getKartScore(i));
        gp_changes.push_back((float)last_score);
        int cur_score = last_score;
        float overall_time = RaceManager::get()->getOverallTime(i);
        std::string username = StringUtils::wideToUtf8(
            RaceManager::get()->getKartInfo(i).getPlayerName());
        if (username == fastest_kart)
        {
            gp_changes.back() += points_fl;
            cur_score += points_fl;
        }
        int team = getTeamManager()->getTeamForUsername(username);
        if (team > 0)
        {
            auto& item = m_gp_team_scores[team];
            item.score += cur_score;
            item.time += overall_time;
        }
        last_score = m_gp_scores[username].score;
        cur_score += last_score;
        overall_time = overall_time + m_gp_scores[username].time;
        if (auto player =
            RaceManager::get()->getKartInfo(i).getNetworkPlayerProfile().lock())
        {
            player->setScore(cur_score);
            player->setOverallTime(overall_time);
        }
        auto& item = m_gp_scores[username];
        item.score = cur_score;
        item.time = overall_time;
        last_scores.push_back(last_score);
        cur_scores.push_back(cur_score);
        overall_times.push_back(overall_time);    
    }

    GPScoresPacket packet;
    packet.fastest_lap = fastest_lap;
    packet.fastest_kart = fastest_kart_wide;
    packet.total_gp_tracks = (uint8_t)game_setup->getTotalGrandPrixTracks();
    packet.all_tracks_size = (uint8_t)game_setup->getAllTracks().size();

    for (const std::string& gp_track : game_setup->getAllTracks())
        packet.all_tracks.push_back(gp_track);

    packet.num_players = (uint8_t)RaceManager::get()->getNumPlayers();
    for (unsigned i = 0; i < RaceManager::get()->getNumPlayers(); i++)
    {
        GPIndividualScorePacket subpacket;
        subpacket.last_score = last_scores[i];
        subpacket.cur_score = cur_scores[i];
        subpacket.overall_time = overall_times[i];
        packet.scores.push_back(subpacket);
    }
    
    return packet;
}   // updateGPScores
//-----------------------------------------------------------------------------

bool LobbyGPManager::trySettingGPScoring(const std::string& input)
{
    std::shared_ptr<GPScoring> new_scoring;
    
    try
    {
        new_scoring = GPScoring::createFromIntParamString(input);
    }
    catch (std::logic_error& ex)
    {
        Log::warn("Failed to create GP scoring from string (%s): %s",
                input.c_str(), ex.what());
        return false;
    }

    std::swap(m_gp_scoring, new_scoring);
    return true;
}   // trySettingGPScoring
//-----------------------------------------------------------------------------

void LobbyGPManager::updateWorldScoring() const
{
    WorldWithRank *wwr = dynamic_cast<WorldWithRank*>(World::getWorld());
    if (wwr)
        wwr->setCustomScoringSystem(m_gp_scoring);
}   // updateWorldScoring
//-----------------------------------------------------------------------------

std::string LobbyGPManager::getScoringAsString() const
{
    std::string msg = "Current scoring is \"";
    if (m_gp_scoring)
        msg += m_gp_scoring->toString();
    msg += "\"";
    return msg;
}   // getScoringAsString
//-----------------------------------------------------------------------------