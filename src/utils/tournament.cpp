//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2024 kimden
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

#include "utils/tournament.hpp"

#include "utils/track_filter.hpp"
#include "network/stk_peer.hpp"
#include "network/network_player_profile.hpp"
#include "network/server_config.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/peer_vote.hpp"
#include "modes/world.hpp"
#include "modes/soccer_world.hpp"
#include "utils/lobby_settings.hpp"
#include "utils/string_utils.hpp"

namespace
{
    static int g_history_limit = 100;
}

Tournament::Tournament(ServerLobby* lobby,
        std::shared_ptr<LobbySettings> settings)
    : m_lobby(lobby), m_lobby_settings(settings)
{
    initTournamentPlayers();
    m_game = 0;
    m_extra_seconds = 0.0f;
}   // Tournament
//-----------------------------------------------------------------------------

void Tournament::applyFiltersForThisGame(FilterContext& track_context)
{
    track_context.wildcards = m_arenas;
    m_track_filters[m_game].apply(track_context);
}   // applyFiltersForThisGame
//-----------------------------------------------------------------------------

std::set<std::string> Tournament::getThoseWhoSeeTeamchats() const
{
    std::set<std::string> sees_teamchats;
    for (const std::string& s: m_referees) {
        sees_teamchats.insert(s);
    }
    return sees_teamchats;
}   // getThoseWhoSeeTeamchats
//-----------------------------------------------------------------------------

bool Tournament::checkSenderInRefsOrPlayers(std::shared_ptr<STKPeer> sender) const
{
    if (!sender)
        return false;
    for (auto& profile: sender->getPlayerProfiles())
    {
        std::string name = StringUtils::wideToUtf8(
            profile->getName());
        if (m_referees.count(name) > 0 ||
            m_red_players.count(name) > 0 ||
            m_blue_players.count(name) > 0)
        {
            return true;
        }
    }
    return false;
}   // checkSenderInRefsOrPlayers
//-----------------------------------------------------------------------------

std::set<std::string> Tournament::getImportantChatPlayers() const
{
    std::set<std::string> important_players;
    if (m_limited_chat)
    {
        for (const std::string& s: m_referees)
            important_players.insert(s);
        for (const std::string& s: m_red_players)
            important_players.insert(s);
        for (const std::string& s: m_blue_players)
            important_players.insert(s);
    }
    for (const std::string& s: m_mutealls)
        important_players.insert(s);
    return important_players;
}   // getImportantChatPlayers
//-----------------------------------------------------------------------------

void Tournament::fillNextArena(const std::string& track_name)
{
    if (m_game >= (int)m_arenas.size())
        m_arenas.resize(m_game + 1, "");
    m_arenas[m_game] = track_name;
}   // fillNextArena
//-----------------------------------------------------------------------------

void Tournament::applyRestrictionsOnDefaultVote(PeerVote* default_vote) const
{
    default_vote->m_num_laps = m_length;
    default_vote->m_reverse = false;
}   // applyRestrictionsOnDefaultVote
//-----------------------------------------------------------------------------

void Tournament::applyRestrictionsOnVote(PeerVote* vote) const
{
    vote->m_reverse = false;
}   // applyRestrictionsOnVote
//-----------------------------------------------------------------------------

void Tournament::onRaceFinished() const
{
    World* w = World::getWorld();
    if (w)
    {
        SoccerWorld *sw = dynamic_cast<SoccerWorld*>(w);
        sw->tellCountIfDiffers();
    }
}   // onRaceFinished
//-----------------------------------------------------------------------------

void Tournament::updateTournamentRole(std::shared_ptr<STKPeer> peer)
{
    if (peer->getPlayerProfiles().empty())
        return;
    std::string utf8_online_name = StringUtils::wideToUtf8(
        peer->getPlayerProfiles()[0]->getName());
    for (auto& player: peer->getPlayerProfiles())
    {
        core::stringw name = player->getName();
        std::string utf8_name = StringUtils::wideToUtf8(name);
        if (m_red_players.count(utf8_online_name))
            m_lobby->setTeamInLobby(player, KART_TEAM_RED);
        else if (m_blue_players.count(utf8_online_name))
            m_lobby->setTeamInLobby(player, KART_TEAM_BLUE);
        else
            m_lobby->setTeamInLobby(player, KART_TEAM_NONE);
        if (hasColorsSwapped())
        {
            if (player->getTeam() == KART_TEAM_BLUE)
                m_lobby->setTeamInLobby(player, KART_TEAM_RED);
            else if (player->getTeam() == KART_TEAM_RED)
                m_lobby->setTeamInLobby(player, KART_TEAM_BLUE);
        }
    }
}   // updateTournamentRole
//-----------------------------------------------------------------------------

KartTeam Tournament::getTeam(std::string utf8_online_name) const
{
    bool swapped = hasColorsSwapped();

    if (m_red_players.count(utf8_online_name))
    {
        if (swapped)
            return KART_TEAM_BLUE;
        return KART_TEAM_RED;
    }
    
    if (m_blue_players.count(utf8_online_name))
    {
        if (swapped)
            return KART_TEAM_RED;
        return KART_TEAM_BLUE;
    }
    
    return KART_TEAM_NONE;
}   // getTeam
//-----------------------------------------------------------------------------

bool Tournament::hasGoalsLimit() const
{
    return m_game_limits[m_game] == 'G';
}   // hasGoalsLimit
//-----------------------------------------------------------------------------

bool Tournament::hasGoalsLimit(int game) const
{
    return m_game_limits[game] == 'G';
}   // hasGoalsLimit
//-----------------------------------------------------------------------------

bool Tournament::hasColorsSwapped() const
{
    return m_colors[m_game] == 'B';
}   // hasColorsSwapped
//-----------------------------------------------------------------------------

bool Tournament::hasColorsSwapped(int game) const
{
    return m_colors[game] == 'B';
}   // hasColorsSwapped
//-----------------------------------------------------------------------------

void Tournament::initTournamentPlayers()
{
    // Init playing teams
    std::vector<std::string> tokens = StringUtils::split(
        ServerConfig::m_soccer_tournament_match, ' ');
    std::string type = "";
    for (std::string& s: tokens)
    {
        if (s.length() == 1)
            type = s;
        else if (s.empty())
            continue;
        else if (s[0] == '#')
        {
            std::string cat_name = s.substr(1);
            std::set<std::string>& dest = (
                type == "R" ? m_red_players :
                type == "B" ? m_blue_players :
                m_referees);

            auto categories = m_lobby_settings->getCategories();
            for (const std::string& member: categories[cat_name])
                dest.insert(member);
        }
        else if (type == "R") 
            m_red_players.insert(s);
        else if (type == "B")
            m_blue_players.insert(s);
        else if (type == "J")
            m_referees.insert(s);
    }
    for (const std::string& s: m_red_players)
    {
        Log::info("ServerLobby", "SoccerMatchLog: Role of %s is initially set to r",
            s.c_str());
    }
    for (const std::string& s: m_blue_players)
    {
        Log::info("ServerLobby", "SoccerMatchLog: Role of %s is initially set to b",
            s.c_str());
    }
    for (const std::string& s: m_referees)
    {
        Log::info("ServerLobby", "SoccerMatchLog: Role of %s is initially set to j",
            s.c_str());
    }
    m_init_red = m_red_players;
    m_init_blue = m_blue_players;
    m_init_ref = m_referees;

    // Init tournament format
    tokens = StringUtils::split(
        ServerConfig::m_soccer_tournament_rules, ';');
    bool fallback = tokens.size() < 2;
    std::vector<std::string> general;
    if (!fallback)
    {
        general = StringUtils::split(tokens[0], ' ');
        if (general.size() < 5)
            fallback = true;
    }
    if (fallback)
    {
        Log::warn("ServerLobby", "Tournament rules are not complete, fallback to default");
        general.clear();
        general.push_back("nochat");
        general.push_back("10");
        general.push_back("GGGGT");
        general.push_back("RRBBR");
        general.push_back("+++++");
        tokens.clear();
        tokens.push_back("nochat 10 GGGT RRBBR");
        tokens.push_back("");
        tokens.push_back("");
        tokens.push_back("not %0");
        tokens.push_back("not %0" " %1");
        tokens.push_back("");
        ServerConfig::m_soccer_tournament_rules = "nochat 10 TTTTG RRBBR +++++;"
            ";;not %1;"
            "not %1 "
            "%2;;;";
    }
    Log::info("ServerLobby", "SoccerMatchLog: Tournament rules are set to \"%s\"",
        ServerConfig::m_soccer_tournament_rules.c_str());
    m_limited_chat = false;
    m_length = 10;
    if (general[0] == "nochat")
        m_limited_chat = true;
    if (StringUtils::parseString<int>(general[1], &m_length))
    {
        if (m_length <= 0)
            m_length = 10;
    }
    else
        m_length = 10;
    ServerConfig::m_fixed_lap_count = m_length;

    m_game_limits = general[2];
    m_colors = general[3];
    m_max_games = std::min(general[2].length(), general[3].length());
    m_max_games = std::min(m_max_games, (int)tokens.size() - 1);
    m_votability = general[4];
    for (int i = 0; i < m_max_games; i++)
        m_track_filters.emplace_back(tokens[i + 1]);
}   // initTournamentPlayers
//-----------------------------------------------------------------------------

bool Tournament::canPlay(const std::string& username) const
{
    return m_red_players.count(username) > 0 ||
        m_blue_players.count(username) > 0;
}   // canPlay
//-----------------------------------------------------------------------------

bool Tournament::canVote(std::shared_ptr<STKPeer> peer) const
{
    std::string username = StringUtils::wideToUtf8(
            peer->getPlayerProfiles()[0]->getName());
    
    bool first = m_red_players.count(username) > 0;
    bool second = m_blue_players.count(username) > 0;
    if (!first && !second)
        return false;
    char votable = m_votability[m_game];
    if (votable == '+')
        return true;
    return (votable == 'F' && first) || (votable == 'S' && second);
}   // canVote
//-----------------------------------------------------------------------------

bool Tournament::hasHostRights(std::shared_ptr<STKPeer> peer)
{
    std::string username = StringUtils::wideToUtf8(
        peer->getPlayerProfiles()[0]->getName());
    return m_referees.count(username) > 0;
}   // hasHostRights
//-----------------------------------------------------------------------------

bool Tournament::hasHammerRights(std::shared_ptr<STKPeer> peer)
{
    std::string username = StringUtils::wideToUtf8(
        peer->getPlayerProfiles()[0]->getName());
    return m_referees.count(username) > 0;
}   // hasHammerRights
//-----------------------------------------------------------------------------

int Tournament::editMuteall(const std::string& username, int type)
{
    return m_mutealls.set(username, type);
}   // editMuteall
//-----------------------------------------------------------------------------


int Tournament::getNextGameNumber() const
{
    int res = m_game + 1;
    if (res == m_max_games)
        res = 0;
    return res;
}   // getNextGameNumber
//-----------------------------------------------------------------------------

int Tournament::getDefaultDuration() const
{
    return ServerConfig::m_fixed_lap_count;
}   // getDefaultDuration
//-----------------------------------------------------------------------------

bool Tournament::isValidGameCmdInput(int new_game_number, int new_duration, int addition) const
{
    return new_game_number >= 0
        && new_game_number < m_max_games
        && new_duration >= 0
        && addition >= 0
        && addition <= 59;
}   // isValidGameCmdInput
//-----------------------------------------------------------------------------

void Tournament::getGameCmdInput(int& game_number, int& duration, int& addition) const
{
    game_number = m_game;
    duration = m_length;
    addition = m_extra_seconds;
}   // getGameCmdInput
//-----------------------------------------------------------------------------

void Tournament::setGameCmdInput(int new_game_number, int new_duration, int addition)
{
    m_game = new_game_number;
    m_length = new_duration;
    m_extra_seconds = 0.0f;
    if (addition > 0) {
        m_extra_seconds = 60.0f - addition;
    }
}   // setGameCmdInput
//-----------------------------------------------------------------------------

void Tournament::setTeam(KartTeam team, const std::string& username, bool permanent)
{
    if (team == KART_TEAM_RED)
    {
        m_red_players.insert(username);
        if (permanent)
            m_init_red.insert(username);
    }
    else if (team == KART_TEAM_BLUE)
    {
        m_blue_players.insert(username);
        if (permanent)
            m_init_blue.insert(username);
    }
}   // setTeam
//-----------------------------------------------------------------------------

void Tournament::setReferee(const std::string& username, bool permanent)
{
    m_referees.insert(username);
    if (permanent)
        m_init_ref.insert(username);
}   // setReferee
//-----------------------------------------------------------------------------

void Tournament::eraseFromAllTournamentCategories(const std::string& username, bool permanent)
{
    m_red_players.erase(username);
    m_blue_players.erase(username);
    m_referees.erase(username);
    if (permanent)
    {
        m_init_red.erase(username);
        m_init_blue.erase(username);
        m_init_ref.erase(username);
    }
}   // eraseFromAllTournamentCategories
//-----------------------------------------------------------------------------

std::vector<std::string> Tournament::getMapHistory() const
{
    return m_arenas;
}   // getMapHistory
//-----------------------------------------------------------------------------

bool Tournament::assignToHistory(int index, const std::string& map_id)
{
    if (index >= g_history_limit)
        return false;
    if (index >= (int)m_arenas.size())
        m_arenas.resize(index + 1, "");
    m_arenas[index] = map_id;
    return true;
}   // assignToHistory
//-----------------------------------------------------------------------------