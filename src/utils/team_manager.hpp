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

#ifndef TEAM_MANAGER_HPP
#define TEAM_MANAGER_HPP

#include "irrString.h"
#include "utils/lobby_context.hpp"
#include "utils/set_with_flip.hpp"
#include "utils/types.hpp"

#include <memory>
#include <map>
#include <set>
#include <string>

enum KartTeam: int8_t;
class NetworkPlayerProfile;
class STKPeer;

class TeamManager: public LobbyContextComponent
{
public:
    TeamManager(LobbyContext* context): LobbyContextComponent(context) {}

    void setupContextUser() OVERRIDE;

    // The functions below set *both* KartTeam and temporary team,
    // depending on game mode; also reset/set ASM_NO_TEAM if needed.
    void setTemporaryTeamInLobby(const std::string& username, int team);
    void setTeamInLobby(std::shared_ptr<NetworkPlayerProfile> profile, KartTeam team);
    void setTemporaryTeamInLobby(std::shared_ptr<NetworkPlayerProfile> profile, int team);
    void applyPermutationToTeams(const std::map<int, int>& permutation);
    std::string getAvailableTeams() const;
    void initCategories();
    void addPlayerToCategory(const std::string& player, const std::string& category);
    void erasePlayerFromCategory(const std::string& player, const std::string& category);
    void makeCategoryVisible(const std::string category, int value);
    bool isCategoryVisible(const std::string category) const;

    std::vector<std::string> getVisibleCategoriesForPlayer(
            const std::string& profile_name) const;

    const std::set<std::string>& getPlayersInCategory(
            const std::string& category) const;

    std::string getInternalAvailableTeams() const { return m_available_teams; }
    void setInternalAvailableTeams(std::string& s)   { m_available_teams = s; }

    void setTeamForUsername(const std::string& name, int team)
                                            { m_team_for_player[name] = team; }

    int getTeamForUsername(const std::string& name);
    void clearTeams()                            { m_team_for_player.clear(); }

    bool hasTeam(const std::string& name)
            { return m_team_for_player.find(name) != m_team_for_player.end(); }

    bool isInHammerWhitelist(const std::string& str) const
           { return m_hammer_whitelist.find(str) != m_hammer_whitelist.end(); }

    void clearTemporaryTeams();
    void shuffleTemporaryTeams(const std::map<int, int>& permutation);
    void changeTeam(std::shared_ptr<NetworkPlayerProfile> player);

    // The functions below reset/set ASM_NO_TEAM if needed by team changing procedure.
    void checkNoTeamSpectator(std::shared_ptr<STKPeer> peer);

private:

    std::string m_available_teams;

    std::map<std::string, std::set<std::string>> m_player_categories;

    SetWithFlip<std::string> m_hidden_categories;

    std::map<std::string, std::set<std::string>> m_categories_for_player;

    std::map<std::string, int> m_team_for_player;

    std::set<std::string> m_hammer_whitelist;

};

#endif // TEAM_MANAGER_HPP