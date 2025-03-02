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

#ifndef TOURNAMENT_HPP
#define TOURNAMENT_HPP

#include "utils/types.hpp"
#include "utils/set_with_flip.hpp"
#include "utils/tournament_role.hpp"

#include <memory>
#include <string>
#include <set>
#include <vector>
struct FilterContext;
class PeerVote;
class TrackFilter;
class ServerLobby;
class STKPeer;
class LobbySettings;

/** This class combines things that were previously located in server lobby
 *  (m_tournament_*) and were related to Soccer Tournaments.
 *  The current plan is to make it contain generic tournament things, while
 *  specific things like format, modes, etc would be in other new classes.
 */
class Tournament
{
public:
    Tournament(ServerLobby* lobby, std::shared_ptr<LobbySettings> settings);
    void initTournamentPlayers();
    void applyFiltersForThisGame(FilterContext& track_context);
    std::set<std::string> getThoseWhoSeeTeamchats() const;
    bool checkSenderInRefsOrPlayers(std::shared_ptr<STKPeer> sender) const;
    std::set<std::string> getImportantChatPlayers() const;
    void fillNextArena(const std::string& track_name);
    void applyRestrictionsOnDefaultVote(PeerVote* default_vote) const;
    void applyRestrictionsOnVote(PeerVote* vote) const;
    void onRaceFinished() const;
    void updateTournamentRole(std::shared_ptr<STKPeer> peer);

    KartTeam getTeam(std::string utf8_online_name) const;
    bool canPlay(const std::string& username) const;
    bool canVote(std::shared_ptr<STKPeer> peer) const;

    bool hasHostRights(std::shared_ptr<STKPeer> peer);
    bool hasHammerRights(std::shared_ptr<STKPeer> peer);

    int editMuteall(const std::string& username, int type);

    bool hasGoalsLimit() const;
    bool hasGoalsLimit(int game) const;
    bool hasColorsSwapped() const;
    bool hasColorsSwapped(int game) const;
    bool canChangeTeam() const { return false; }
    bool forbidStarting() const { return true; }
    int getNextGameNumber() const;
    int getDefaultDuration() const;
    bool isValidGameNumber(int number) const;
    int minGameNumber() const { return 0; }
    int maxGameNumber() const { return m_tournament_max_games - 1; }
    bool isValidGameCmdInput(int new_game_number, int new_duration, int addition) const;
    void getGameCmdInput(int& game_number, int& duration, int& addition) const;
    void setGameCmdInput(int new_game_number, int new_duration, int addition);
    void setTeam(KartTeam team, const std::string& username, bool permanent);
    void setReferee(const std::string& username, bool permanent);
    void eraseFromAllTournamentCategories(const std::string& username, bool permanent);
    std::vector<std::string> getMapHistory() const;
    bool assignToHistory(int index, const std::string& map_id);

    float getExtraSeconds() const { return m_extra_seconds; }

private:
    ServerLobby* m_lobby;

    std::shared_ptr<LobbySettings> m_lobby_settings;

    std::set<std::string> m_tournament_red_players;

    std::set<std::string> m_tournament_blue_players;
    
    std::set<std::string> m_tournament_referees;

    SetWithFlip<std::string> m_tournament_mutealls;

    std::set<std::string> m_tournament_init_red;

    std::set<std::string> m_tournament_init_blue;

    std::set<std::string> m_tournament_init_ref;

    bool m_tournament_limited_chat;

    int m_tournament_length;

    int m_tournament_max_games;

    std::string m_tournament_game_limits;

    std::string m_tournament_colors;

    std::string m_tournament_votability;

    std::vector<std::string> m_tournament_arenas;

    std::vector<TrackFilter> m_tournament_track_filters;

    int m_tournament_game;

    std::vector<std::string> m_tournament_must_have_tracks;

    float m_extra_seconds;
};

#endif