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

#include "utils/team_manager.hpp"

#include "utils/team_utils.hpp"
#include "network/remote_kart_info.hpp"
#include "network/network_player_profile.hpp"
#include "network/protocols/server_lobby.hpp"
#include "utils/string_utils.hpp"
#include "network/server_config.hpp"
#include "network/stk_host.hpp"
#include "network/stk_peer.hpp"
#include "utils/lobby_settings.hpp"
#include "utils/tournament.hpp"
#include "network/protocols/server_lobby.hpp"

void TeamManager::setupContextUser()
{
    m_available_teams = ServerConfig::m_init_available_teams;
    initCategories();
}   // setupContextUser
//-----------------------------------------------------------------------------

void TeamManager::setTeamInLobby(std::shared_ptr<NetworkPlayerProfile> profile, KartTeam team)
{
    // Used for soccer+CTF, where everything can be defined by KartTeam
    profile->setTeam(team);
    profile->setTemporaryTeam(TeamUtils::getIndexFromKartTeam(team));
    setTeamForUsername(
        StringUtils::wideToUtf8(profile->getName()),
        profile->getTemporaryTeam()
    );

    getLobby()->checkNoTeamSpectator(profile->getPeer());
}   // setTeamInLobby

//-----------------------------------------------------------------------------
void TeamManager::setTemporaryTeamInLobby(std::shared_ptr<NetworkPlayerProfile> profile, int team)
{
    // Used for racing+FFA, where everything can be defined by a temporary team
    profile->setTemporaryTeam(team);
    if (RaceManager::get()->teamEnabled())
        profile->setTeam((KartTeam)(TeamUtils::getKartTeamFromIndex(team)));
    else
        profile->setTeam(KART_TEAM_NONE);
    setTeamForUsername(
        StringUtils::wideToUtf8(profile->getName()),
        profile->getTemporaryTeam()
    );

    getLobby()->checkNoTeamSpectator(profile->getPeer());
}   // setTemporaryTeamInLobby

//-----------------------------------------------------------------------------

void TeamManager::applyPermutationToTeams(const std::map<int, int>& permutation)
{
    for (auto& p: m_team_for_player)
    {
        auto it = permutation.find(p.second);
        if (it != permutation.end())
            p.second = it->second;
    }
}   // applyPermutationToTeams
//-----------------------------------------------------------------------------

std::string TeamManager::getAvailableTeams() const
{
    if (RaceManager::get()->teamEnabled())
        return "rb";

    return m_available_teams;
}   // getAvailableTeams
//-----------------------------------------------------------------------------

void TeamManager::initCategories()
{
    std::vector<std::string> tokens = StringUtils::split(
        ServerConfig::m_categories, ' ');
    std::string category = "";
    bool isTeam = false;
    bool isHammerWhitelisted = false;
    for (std::string& s: tokens)
    {
        if (s.empty())
            continue;
        else if (s[0] == '#')
        {
            isTeam = false;
            isHammerWhitelisted = false;
            if (s.length() > 1 && s[1] == '#')
            {
                category = s.substr(2);
                m_hidden_categories.insert(category);
            }
            else
                category = s.substr(1);
        }
        else if (s[0] == '$')
        {
            isTeam = true;
            isHammerWhitelisted = false;
            category = s.substr(1);
        }
        else if (s[0] == '^')
        {
            isHammerWhitelisted = true;
        }
        else
        {
            if (isHammerWhitelisted)
            {
                m_hammer_whitelist.insert(s);
            }
            else
            {
                if (!isTeam) {
                    m_player_categories[category].insert(s);
                    m_categories_for_player[s].insert(category);
                }
                else
                {
                    m_team_for_player[s] = category[0] - '0' + 1;
                }
            }
        }
    }
}   // initCategories
//-----------------------------------------------------------------------------

void TeamManager::addPlayerToCategory(const std::string& player, const std::string& category)
{
    m_player_categories[category].insert(player);
    m_categories_for_player[player].insert(category);
}   // addPlayerToCategory
//-----------------------------------------------------------------------------

void TeamManager::erasePlayerFromCategory(const std::string& player, const std::string& category)
{
    m_player_categories[category].erase(player);
    m_categories_for_player[player].erase(category);
}   // erasePlayerFromCategory
//-----------------------------------------------------------------------------

void TeamManager::makeCategoryVisible(const std::string category, bool value)
{
    if (value) {
        m_hidden_categories.erase(category);
    } else {
        m_hidden_categories.insert(category);
    }
}   // makeCategoryVisible
//-----------------------------------------------------------------------------

bool TeamManager::isCategoryVisible(const std::string category) const
{
    return m_hidden_categories.find(category) == m_hidden_categories.end();
}   // isCategoryVisible
//-----------------------------------------------------------------------------

std::vector<std::string> TeamManager::getVisibleCategoriesForPlayer(const std::string& profile_name) const
{
    auto it = m_categories_for_player.find(profile_name);
    if (it == m_categories_for_player.end())
        return {};
    
    std::vector<std::string> res;
    for (const std::string& category: it->second)
        if (isCategoryVisible(category))
            res.push_back(category);
    
    return res;
}   // getVisibleCategoriesForPlayer
//-----------------------------------------------------------------------------


std::set<std::string> TeamManager::getPlayersInCategory(const std::string& category) const
{
    auto it = m_player_categories.find(category);
    if (it == m_player_categories.end())
        return {};

    return it->second;
}   // getPlayersInCategory
//-----------------------------------------------------------------------------

int TeamManager::getTeamForUsername(const std::string& name)
{
    auto it = m_team_for_player.find(name);
    if (it == m_team_for_player.end())
        return TeamUtils::NO_TEAM;
    return it->second;
}   // getTeamForUsername
//-----------------------------------------------------------------------------

void TeamManager::clearTemporaryTeams()
{
    clearTeams();

    for (auto& peer : STKHost::get()->getPeers())
    {
        for (auto& profile : peer->getPlayerProfiles())
        {
            setTemporaryTeamInLobby(profile, TeamUtils::NO_TEAM);
        }
    }
}   // clearTemporaryTeams
//-----------------------------------------------------------------------------

void TeamManager::shuffleTemporaryTeams(const std::map<int, int>& permutation)
{
    applyPermutationToTeams(permutation);
    for (auto& peer : STKHost::get()->getPeers())
    {
        for (auto &profile: peer->getPlayerProfiles())
        {
            auto it = permutation.find(profile->getTemporaryTeam());
            if (it != permutation.end())
            {
                setTemporaryTeamInLobby(profile, it->second);
            }
        }
    }
    getLobby()->shuffleGPScoresWithPermutation(permutation);
}   // shuffleTemporaryTeams
//-----------------------------------------------------------------------------

void TeamManager::changeTeam(std::shared_ptr<NetworkPlayerProfile> player)
{
    if (!getSettings()->hasTeamChoosing() ||
        !RaceManager::get()->teamEnabled())
        return;

    auto red_blue = STKHost::get()->getAllPlayersTeamInfo();
    if (isTournament() && !getTournament()->canChangeTeam())
    {
        Log::info("ServerLobby", "Team change requested by %s, but tournament forbids it.", player->getName().c_str());
        return;
    }

    // For now, the change team button will still only work in soccer + CTF.
    // Further logic might be added later, but now it seems to be complicated
    // because there's a restriction of 7 for those modes, and unnecessary
    // because we don't have client changes.

    // At most 7 players on each team (for live join)
    if (player->getTeam() == KART_TEAM_BLUE)
    {
        if (red_blue.first >= 7 && !getSettings()->hasFreeTeams())
            return;
        setTeamInLobby(player, KART_TEAM_RED);
    }
    else
    {
        if (red_blue.second >= 7 && !getSettings()->hasFreeTeams())
            return;
        setTeamInLobby(player, KART_TEAM_BLUE);
    }
    getLobby()->updatePlayerList();
}