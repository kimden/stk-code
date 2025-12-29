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

#include "network/network_player_profile.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/remote_kart_info.hpp"
#include "network/server_config.hpp"
#include "network/stk_host.hpp"
#include "network/stk_peer.hpp"
#include "utils/crown_manager.hpp"
#include "utils/lobby_gp_manager.hpp"
#include "utils/lobby_settings.hpp"
#include "utils/string_utils.hpp"
#include "utils/team_utils.hpp"
#include "utils/tournament.hpp"

#include <random>

namespace
{
    static const std::set<std::string> empty_string_set = {};
}

void TeamManager::setupContextUser()
{
    m_available_teams = ServerConfig::m_init_available_teams;
    initCategories();
}   // setupContextUser
//-----------------------------------------------------------------------------

void TeamManager::setTemporaryTeamInLobby(const std::string& username, int team)
{
    irr::core::stringw wide_player_name = StringUtils::utf8ToWide(username);

    // kimden: this code, as well as findPeerByName, assumes (wrongly) that
    // there cannot be two profiles with the same name. Fix that pls.
    std::shared_ptr<STKPeer> player_peer = STKHost::get()->findPeerByName(wide_player_name);
    if (!player_peer)
        return;

    for (auto& profile : player_peer->getPlayerProfiles())
    {
        if (profile->getName() == wide_player_name)
        {
            setTemporaryTeamInLobby(profile, team);
            break;
        }
    }
}   // setTemporaryTeamInLobby (username)
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

    checkNoTeamSpectator(profile->getPeer());
}   // setTeamInLobby
//-----------------------------------------------------------------------------

// Used for racing + FFA, where everything can be defined by a temporary team
void TeamManager::setTemporaryTeamInLobby(
        std::shared_ptr<NetworkPlayerProfile> profile, int team)
{
    profile->setTemporaryTeam(team);

    if (RaceManager::get()->teamEnabled())
        profile->setTeam((KartTeam)(TeamUtils::getKartTeamFromIndex(team)));
    else
        profile->setTeam(KART_TEAM_NONE);

    setTeamForUsername(
        StringUtils::wideToUtf8(profile->getName()),
        profile->getTemporaryTeam()
    );

    checkNoTeamSpectator(profile->getPeer());
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
    int level; // Used only when isHammerWhitelisted
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
            level = 1;
            if (s.length() >= 2 && s[1] == '^')
                level = 2;
        }
        else
        {
            if (isHammerWhitelisted)
            {
                m_hammer_whitelist.insert(s);
                if (level >= 2)
                    m_hammer_whitelist_level_2.insert(s);
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

void TeamManager::addPlayerToCategory(
        const std::string& player, const std::string& category)
{
    m_player_categories[category].insert(player);
    m_categories_for_player[player].insert(category);
}   // addPlayerToCategory
//-----------------------------------------------------------------------------

void TeamManager::erasePlayerFromCategory(
        const std::string& player, const std::string& category)
{
    auto& container_for_categ = m_player_categories[category];
    auto& container_for_player = m_categories_for_player[player];

    container_for_categ.erase(player);
    container_for_player.erase(category);

    if (container_for_categ.empty())
        m_player_categories.erase(category);

    if (container_for_player.empty())
        m_categories_for_player.erase(player);
}   // erasePlayerFromCategory
//-----------------------------------------------------------------------------

void TeamManager::makeCategoryVisible(const std::string category, int value)
{
    m_hidden_categories.set(category, value);
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

std::vector<std::string> TeamManager::getVisibleCategoriesForPlayer(
        const std::string& profile_name) const
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

const std::set<std::string>& TeamManager::getPlayersInCategory(const std::string& category) const
{
    auto it = m_player_categories.find(category);
    if (it == m_player_categories.end())
        return empty_string_set;

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

bool TeamManager::isInHammerWhitelist(const std::string& str, int level) const
{
    if (level == 1)
        return m_hammer_whitelist.find(str) != m_hammer_whitelist.end();
    else if (level == 2) 
        return m_hammer_whitelist_level_2.find(str) != m_hammer_whitelist_level_2.end();

    return false;
}   // isInHammerWhitelist
//-----------------------------------------------------------------------------

void TeamManager::clearTemporaryTeams()
{
    clearTeams();

    for (auto& peer : STKHost::get()->getPeers())
        for (auto& profile : peer->getPlayerProfiles())
            setTemporaryTeamInLobby(profile, TeamUtils::NO_TEAM);
}   // clearTemporaryTeams
//-----------------------------------------------------------------------------

void TeamManager::shuffleTemporaryTeams(const std::map<int, int>& permutation)
{
    applyPermutationToTeams(permutation);

    for (auto& peer : STKHost::get()->getPeers())
        for (auto &profile: peer->getPlayerProfiles())
            if (auto it = permutation.find(profile->getTemporaryTeam()); it != permutation.end())
                setTemporaryTeamInLobby(profile, it->second);

    getGPManager()->shuffleGPScoresWithPermutation(permutation);
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
        Log::info("TeamManager", "Team change requested by %s, but tournament forbids it.", player->getName().c_str());
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
}   // changeTeam
//-----------------------------------------------------------------------------

void TeamManager::checkNoTeamSpectator(std::shared_ptr<STKPeer> peer)
{
    if (!peer)
        return;

    if (RaceManager::get()->teamEnabled())
    {
        bool has_teamed = false;
        for (auto& other: peer->getPlayerProfiles())
        {
            if (other->getTeam() != KART_TEAM_NONE)
            {
                has_teamed = true;
                break;
            }
        }

        if (!has_teamed && peer->getAlwaysSpectate() == ASM_NONE)
            getCrownManager()->setSpectateModeProperly(peer, ASM_NO_TEAM);

        if (has_teamed && peer->getAlwaysSpectate() == ASM_NO_TEAM)
            getCrownManager()->setSpectateModeProperly(peer, ASM_NONE);
    }
}   // checkNoTeamSpectator
//-----------------------------------------------------------------------------

bool TeamManager::assignRandomTeams(int intended_number,
        int* final_number, int* final_player_number)
{
    int teams_number = intended_number;
    *final_number = teams_number;
    int player_number = 0;
    int ingame_player_number = 0;
    for (auto& p : STKHost::get()->getPeers())
    {
        if (!getCrownManager()->canRace(p))
            continue;
        if (p->alwaysSpectateButNotNeutral())
            continue;
        
        size_t size = p->getPlayerProfiles().size();
        player_number += size;
        if (!p->isWaitingForGame())
            ingame_player_number += size;
    }
    if (player_number == 0) {
        *final_number = teams_number;
        *final_player_number = player_number;
        return false;
    }
    int max_number_of_teams = TeamUtils::getNumberOfTeams();
    std::string available_colors_string = getAvailableTeams();
    if (available_colors_string.empty())
        return false;
    if (max_number_of_teams > (int)available_colors_string.length())
        max_number_of_teams = (int)available_colors_string.length();
    if (teams_number == -1 || teams_number < 1 || teams_number > max_number_of_teams)
    {
        teams_number = (int)round(sqrt(player_number));
        if (teams_number > max_number_of_teams)
            teams_number = max_number_of_teams;
        if (player_number > 1 && teams_number <= 1 && max_number_of_teams >= 2)
            teams_number = 2;
    }

    *final_number = teams_number;
    *final_player_number = player_number;
    std::vector<int> available_colors;
    std::vector<int> profile_colors;
    for (const char& c: available_colors_string)
        available_colors.push_back(TeamUtils::getIndexByCode(std::string(1, c)));

    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(available_colors.begin(), available_colors.end(), g);
    available_colors.resize(teams_number);

    for (int i = 0; i < player_number; ++i)
        profile_colors.push_back(available_colors[i % teams_number]);

    const auto it = profile_colors.begin() + ingame_player_number;
    std::shuffle(profile_colors.begin(), it, g);
    std::shuffle(it, profile_colors.end(), g);
    std::reverse(profile_colors.begin(), profile_colors.end());

    clearTemporaryTeams();
    for (int group = 0; group < 2; ++group)
    {
        for (auto& p : STKHost::get()->getPeers())
        {
            if (!getCrownManager()->canRace(p))
                continue;
            if (p->alwaysSpectateButNotNeutral())
                continue;
            if ((!p->isWaitingForGame()) ^ (group == 0))
                continue;
            for (auto& profile : p->getPlayerProfiles())
            {
                setTemporaryTeamInLobby(profile, profile_colors.back());
                if (profile_colors.size() > 1) // prevent crash just in case
                    profile_colors.pop_back();
            }
        }
    }
    return true;
}   // assignRandomTeams
//-----------------------------------------------------------------------------

std::string TeamManager::countTeamsAsString()
{
    std::vector<int> counts(TeamUtils::getNumberOfTeams() + 1, 0);
    int cant_play = 0;
    for (auto& p : STKHost::get()->getPeers())
    {
        if (!getCrownManager()->canRace(p))
        {
            ++cant_play;
            continue;
        }
        if (p->alwaysSpectateButNotNeutral())
        {
            ++cant_play;
            continue;
        }
        for (auto& profile : p->getPlayerProfiles())
            ++counts[profile->getTemporaryTeam()];
    }
    std::vector<std::pair<int, int>> sorted;
    for (int i = 0; i < (int)counts.size(); ++i)
        if (counts[i] > 0)
            sorted.emplace_back(-counts[i], i);

    std::sort(sorted.begin(), sorted.end());
    std::string res = "";

    for (int i = 0; i < (int)sorted.size(); ++i)
    {
        if (i)
            res += ", ";

        std::string emoji = TeamUtils::getTeamByIndex(sorted[i].second).getEmoji();
        if (emoji.empty())
            emoji = "_";

        res += StringUtils::insertValues("%s\u00d7%s", emoji.c_str(), -sorted[i].first);
    }

    if (cant_play > 0)
    {
        res += ", can't play ";
        res += StringUtils::insertValues("\u00d7%s", cant_play);
    }

    return res;
}   // countTeamsAsString
//-----------------------------------------------------------------------------

// This command was made specifically for soccer.
void TeamManager::swapRedBlueTeams()
{
    auto peers = STKHost::get()->getPeers();
    for (auto peer : peers)
    {
        if (peer->hasPlayerProfiles())
        {
            // kimden: you assume that only [0] can be checked, but in other places
            // of the code you are somehow not so sure about that... :)
            auto pp = peer->getMainProfile();
            if (pp->getTeam() == KART_TEAM_RED)
                setTeamInLobby(pp, KART_TEAM_BLUE);
            else if (pp->getTeam() == KART_TEAM_BLUE)
                setTeamInLobby(pp, KART_TEAM_RED);
        }
    }
}   // swapRedBlueTeams
//-----------------------------------------------------------------------------