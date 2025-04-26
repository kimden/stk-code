//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2013-2015 SuperTuxKart-Team
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

#include "network/game_setup.hpp"

#include "config/player_manager.hpp"
#include "config/user_config.hpp"
#ifdef DEBUG
#include "network/network_config.hpp"
#endif
#include "network/network_player_profile.hpp"
#include "network/packet_types.hpp"
#include "network/peer_vote.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/server_config.hpp"
#include "network/stk_host.hpp"
#include "race/race_manager.hpp"
#include "utils/file_utils.hpp"
#include "utils/lobby_settings.hpp"
#include "utils/log.hpp"
#include "utils/stk_process.hpp"
#include "utils/string_utils.hpp"

#include <algorithm>
#include <fstream>
#include <random>

//-----------------------------------------------------------------------------
GameSetup::GameSetup()
{
    m_message_of_today = readOrLoadFromFile
        ((std::string) ServerConfig::m_motd);
    const std::string& server_name = ServerConfig::m_server_name;
    m_server_name_utf8 = StringUtils::wideToUtf8
        (StringUtils::xmlDecode(server_name));
    m_extra_server_info = -1;
    m_extra_seconds = 0.0f;
    m_is_grand_prix.store(false);
    reset();
}   // GameSetup

//-----------------------------------------------------------------------------
void GameSetup::loadWorld()
{
    // Notice: for arena (battle / soccer) lap and reverse will be mapped to
    // goals / time limit and random item location
    assert(!m_tracks.empty());
    // Disable accidentally unlocking of a challenge
    if (STKProcess::getType() == PT_MAIN && PlayerManager::getCurrentPlayer())
        PlayerManager::getCurrentPlayer()->setCurrentChallenge("");
    RaceManager::get()->setTimeTarget(0.0f);
    if (RaceManager::get()->isSoccerMode() ||
        RaceManager::get()->isBattleMode())
    {
        const bool is_ctf = RaceManager::get()->getMinorMode() ==
            RaceManager::MINOR_MODE_CAPTURE_THE_FLAG;
        bool prev_val = UserConfigParams::m_random_arena_item;
        if (is_ctf)
            UserConfigParams::m_random_arena_item = false;
        else
            UserConfigParams::m_random_arena_item = m_reverse;

        RaceManager::get()->setRandomItemsIndicator(m_reverse);
        RaceManager::get()->setReverseTrack(false);
        if (RaceManager::get()->isSoccerMode())
        {
            if (isSoccerGoalTarget())
                RaceManager::get()->setMaxGoal(m_laps);
            else
                RaceManager::get()->setTimeTarget((float)m_laps * 60.0f - m_extra_seconds);
        }
        else
        {
            RaceManager::get()->setHitCaptureTime(m_hit_capture_limit,
                m_battle_time_limit - m_extra_seconds);
        }
        RaceManager::get()->startSingleRace(m_tracks.back(), -1,
            false/*from_overworld*/);
        UserConfigParams::m_random_arena_item = prev_val;
    }
    else
    {
        RaceManager::get()->setRandomItemsIndicator(false);
        RaceManager::get()->setReverseTrack(m_reverse);
        RaceManager::get()->startSingleRace(m_tracks.back(), m_laps,
                                      false/*from_overworld*/);
    }
}   // loadWorld

//-----------------------------------------------------------------------------
ServerInfoPacket GameSetup::addServerInfo()
{
#ifdef DEBUG
    assert(NetworkConfig::get()->isServer());
#endif
    ServerInfoPacket packet;

    packet.server_name = m_server_name_utf8;

    auto sl = LobbyProtocol::get<ServerLobby>();
    assert(sl);
    
    packet.difficulty = (uint8_t)sl->getDifficulty();
    
    // probably getSettings()->
    packet.max_players = (uint8_t)ServerConfig::m_server_max_players;

    packet.extra_spectators_zero = 0;
    packet.game_mode = (uint8_t)sl->getGameMode();

    if (hasExtraServerInfo())
    {
        if (isGrandPrix())
        {
            // has_extra_server_info is used for current track index
            uint8_t cur_track = (uint8_t)m_tracks.size();
            if (!isGrandPrixStarted())
                cur_track = 0;

            packet.has_extra_server_info = 2;
            packet.extra_server_info.push_back(cur_track);
            packet.extra_server_info.push_back(m_extra_server_info);
        }
        else
        {
            // Soccer mode
            packet.has_extra_server_info = 1;
            packet.extra_server_info.push_back(m_extra_server_info);
        }
    }
    else
    {
        packet.has_extra_server_info = 0;
        packet.extra_server_info = {};
    }

    if (ServerConfig::m_owner_less)
    {
        // probably getSettings()-> also
        packet.min_start_game_players = ServerConfig::m_min_start_game_players;
        packet.start_game_counter = std::max<float>(0.0f, getSettings()->getStartGameCounter());
    }
    else
    {
        packet.min_start_game_players = 0;
        packet.start_game_counter = 0.0f;
    }

    packet.motd = m_message_of_today;
    packet.is_configurable = getSettings()->isServerConfigurable();
    packet.has_live_players = getSettings()->isLivePlayers();
    return packet;
}   // addServerInfo

//-----------------------------------------------------------------------------
void GameSetup::sortPlayersForGrandPrix(
    std::vector<std::shared_ptr<NetworkPlayerProfile> >& players,
    bool shuffle_instead) const
{
    if (!isGrandPrix())
        return;

    if (m_tracks.size() == 1 || shuffle_instead)
    {
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(players.begin(), players.end(), g);
        return;
    }

    std::sort(players.begin(), players.end(),
        [](const std::shared_ptr<NetworkPlayerProfile>& a,
        const std::shared_ptr<NetworkPlayerProfile>& b)
        {
            return (a->getScore() < b->getScore()) ||
                (a->getScore() == b->getScore() &&
                a->getOverallTime() > b->getOverallTime());
        });
    if (UserConfigParams::m_gp_most_points_first)
    {
        std::reverse(players.begin(), players.end());
    }
}   // sortPlayersForGrandPrix

//-----------------------------------------------------------------------------
void GameSetup::sortPlayersForGame(
    std::vector<std::shared_ptr<NetworkPlayerProfile> >& players) const
{
    if (!isGrandPrix())
    {
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(players.begin(), players.end(), g);
    }
    if (!RaceManager::get()->teamEnabled() ||
        ServerConfig::m_team_choosing)
        return;
    for (unsigned i = 0; i < players.size(); i++)
    {
        // The same as ServerLobby::setTeamInLobby, but without server lobby.
        // Checks for spectate modes are not needed here.
        players[i]->setTeam((KartTeam)(i % 2));
        players[i]->setTemporaryTeam(TeamUtils::getIndexFromKartTeam(i % 2));
    }
}   // sortPlayersForGame

// ----------------------------------------------------------------------------
void GameSetup::setRace(const PeerVote &vote, float extra_seconds)
{
    m_tracks.push_back(vote.m_track_name);
    m_laps = vote.m_num_laps;
    m_reverse = vote.m_reverse;
    m_extra_seconds = extra_seconds;
}   // setRace
// ----------------------------------------------------------------------------
irr::core::stringw GameSetup::readOrLoadFromFile(std::string value)
{
    const std::string& temp = value;
    irr::core::stringw answer;
    if (temp.find(".txt") != std::string::npos)
    {
        const std::string& path = ServerConfig::getConfigDirectory() + "/" +
            temp;
        std::ifstream message(FileUtils::getPortableReadingPath(path));
        if (message.is_open())
        {
            for (std::string line; std::getline(message, line); )
            {
                answer += StringUtils::utf8ToWide(line).trim() +
                    L"\n";
            }
            // Remove last newline
            answer.erase(answer.size() - 1);
        }
    }
    else if (!temp.empty())
        answer = StringUtils::xmlDecode(temp);
    return answer;
}
