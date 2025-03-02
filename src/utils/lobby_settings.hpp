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

#ifndef LOBBY_SETTINGS_HPP
#define LOBBY_SETTINGS_HPP

#include "irrString.h"

// kimden: probably should be removed later
#include "utils/track_filter.hpp"
#include "utils/team_utils.hpp"

#include <memory>
class GameSetup;
class LobbyQueues;
class KartElimination;
class STKPeer;
class PeerVote;
class NetworkString;
class LobbyAssetManager;
class GameInfo;
class Track;
class Tournament;

/** @brief A class that manipulates server settings, such as resetting,
 * scoring, goal policies, etc. Might be split into a few parts later,
 * or even merged back into ServerLobby if proved useless. */
class LobbySettings
{
public:
    LobbySettings(GameSetup* game_setup,
            std::shared_ptr<LobbyQueues> queues,
            std::shared_ptr<KartElimination> elim,
            std::shared_ptr<LobbyAssetManager> asset_manager,
            std::shared_ptr<Tournament> tournament);
    ~LobbySettings();

    void initCategories();
    void initAvailableTracks();
    void initAvailableModes();
    bool loadCustomScoring(std::string& scoring);
    void loadWhiteList();
    void loadPreservedSettings();

    bool hasConsentOnReplays() const           { return m_consent_on_replays; }
    void setConsentOnReplays(bool value)      { m_consent_on_replays = value; }
    std::string getInternalAvailableTeams() const { return m_available_teams; }
    void setInternalAvailableTeams(std::string& s)   { m_available_teams = s; }
    void setTeamForUsername(const std::string& name, int team)
                                            { m_team_for_player[name] = team; }
    int getTeamForUsername(const std::string& name)
    {
        auto it = m_team_for_player.find(name);
        if (it == m_team_for_player.end())
            return TeamUtils::NO_TEAM;
        return it->second;
    }
    void clearTeams()                            { m_team_for_player.clear(); }
    bool hasTeam(const std::string& name)
            { return m_team_for_player.find(name) != m_team_for_player.end(); }
    std::map<std::string, std::set<std::string>> getCategories() const
                                                { return m_player_categories; }

    std::string getHelpMessage() const               { return m_help_message; }
    std::string getMotd() const                              { return m_motd; }
    void addMutedPlayerFor(std::shared_ptr<STKPeer> peer, const irr::core::stringw& name);
    bool removeMutedPlayerFor(std::shared_ptr<STKPeer> peer, const irr::core::stringw& name);
    bool isMuting(std::shared_ptr<STKPeer> peer, const irr::core::stringw& name) const;
    std::string getMutedPlayersAsString(std::shared_ptr<STKPeer> peer);
    void addTeamSpeaker(std::shared_ptr<STKPeer> peer);
    void setMessageReceiversFor(std::shared_ptr<STKPeer> peer, const std::vector<std::string>& receivers);
    std::set<irr::core::stringw> getMessageReceiversFor(std::shared_ptr<STKPeer> peer) const;
    bool isTeamSpeaker(std::shared_ptr<STKPeer> peer) const;
    void makeChatPublicFor(std::shared_ptr<STKPeer> peer);
    bool hasNoLapRestrictions() const;
    bool hasMultiplier() const;
    bool hasFixedLapCount() const;
    int getMultiplier() const;
    int getFixedLapCount() const;
    void setMultiplier(int new_value);
    void setFixedLapCount(int new_value);
    void resetLapRestrictions();
    void setDefaultLapRestrictions();
    std::string getLapRestrictionsAsString() const;
    std::string getDirectionAsString(bool just_edited = false) const;
    bool setDirection(int x = -1);
    bool hasFixedDirection() const;
    int getDirection() const;
    bool isAllowedToStart() const;
    void setAllowedToStart(bool value);
    std::string getAllowedToStartAsString(bool just_edited = false) const;
    bool isGPGridShuffled() const;
    void setGPGridShuffled(bool value);
    std::string getWhetherShuffledGPGridAsString(bool just_edited = false) const;
    std::vector<std::string> getMissingAssets(std::shared_ptr<STKPeer> peer) const;
    void updateWorldSettings(std::shared_ptr<GameInfo> game_info);
    void onResetToDefaultSettings();
    bool isPreservingMode() const;
    std::string getScoringAsString() const;
    void addPlayerToCategory(const std::string& player, const std::string& category);
    void erasePlayerFromCategory(const std::string& player, const std::string& category);
    void makeCategoryVisible(const std::string category, bool value);
    bool isCategoryVisible(const std::string category) const;
    std::vector<std::string> getVisibleCategoriesForPlayer(const std::string& profile_name) const;
    std::set<std::string> getPlayersInCategory(const std::string& category) const;
    std::string getPreservedSettingsAsString() const;
    void eraseFromPreserved(const std::string& value);
    void insertIntoPreserved(const std::string& value);
    void clearAllExpiredWeakPtrs();
    void initializeDefaultVote();
    void applyGlobalFilter(FilterContext& map_context) const;
    void applyGlobalKartsFilter(FilterContext& kart_context) const;
    void applyRestrictionsOnVote(PeerVote* vote, Track* t) const;
    void encodeDefaultVote(NetworkString* ns) const;
    void setDefaultVote(PeerVote winner_vote);
    PeerVote getDefaultVote() const;
    void onPeerDisconnect(std::shared_ptr<STKPeer> peer);
    bool isInWhitelist(const std::string& username) const;
    bool isModeAvailable(int mode) const;
    bool isDifficultyAvailable(int difficulty) const;
    void applyPermutationToTeams(const std::map<int, int>& permutation);
    void resetWinnerPeerId()
                   { m_winner_peer_id = std::numeric_limits<uint32_t>::max(); }
    void setWinnerPeerId(uint32_t value)          { m_winner_peer_id = value; }

    int getBattleHitCaptureLimit() const { return m_battle_hit_capture_limit; }
    float getBattleTimeLimit() const            { return m_battle_time_limit; }
    void setBattleHitCaptureLimit(int value)
                                        { m_battle_hit_capture_limit = value; }
    void setBattleTimeLimit(float value)       { m_battle_time_limit = value; }
    std::string getAvailableTeams() const;

private:
    GameSetup* m_game_setup;

    std::shared_ptr<LobbyQueues> m_lobby_queues;

    std::shared_ptr<KartElimination> m_kart_elimination;

    std::shared_ptr<LobbyAssetManager> m_asset_manager;

    std::shared_ptr<Tournament> m_tournament;

// These are fine here ===============================================================================

    int m_battle_hit_capture_limit;

    float m_battle_time_limit;

    std::vector<std::string> m_play_requirement_tracks;

    std::set<std::string> m_config_available_tracks;

    std::map<std::string, std::vector<std::string>> m_config_track_limitations;

    std::string m_motd;

    std::string m_help_message;

    std::set<int> m_available_difficulties;

    std::set<int> m_available_modes;

    TrackFilter m_global_filter;

    KartFilter m_global_karts_filter;

    int m_fixed_lap;

    int m_fixed_direction;

    double m_default_lap_multiplier;

    std::vector<int> m_scoring_int_params;

    std::string m_scoring_type;

    std::set<std::string> m_usernames_white_list;

    std::set<std::string> m_preserve;

    bool m_allowed_to_start;

    bool m_consent_on_replays;

    bool m_shuffle_gp;

    std::string m_available_teams;


// These should be moved to voting manager ===========================================================

    // Default game settings if no one has ever vote, and save inside here for
    // final vote (for live join)
    PeerVote* m_default_vote;

    uint32_t m_winner_peer_id;


// These should be moved to chat handler =============================================================

    std::map<std::weak_ptr<STKPeer>, std::set<irr::core::stringw>,
        std::owner_less<std::weak_ptr<STKPeer> > > m_peers_muted_players;

    std::map<std::shared_ptr<STKPeer>, std::set<irr::core::stringw>> m_message_receivers;

    std::set<std::shared_ptr<STKPeer>> m_team_speakers;


// These should be moved to category and team manager ================================================

    std::map<std::string, std::set<std::string>> m_player_categories;

    std::set<std::string> m_hidden_categories;

    std::set<std::string> m_special_categories;

    std::map<std::string, std::set<std::string>> m_categories_for_player;

    std::map<std::string, int> m_team_for_player;

};

#endif