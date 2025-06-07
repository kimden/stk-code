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
#include "utils/lobby_context.hpp"
#include "utils/team_utils.hpp"

#include <memory>

class GameSetup;
class KartElimination;
class LobbyAssetManager;
class LobbyQueues;
class PeerVote;
class STKPeer;
class Tournament;
class Track;
struct GameInfo;
class DefaultVotePacket;

/** @brief A class that manipulates server settings, such as resetting,
 * scoring, goal policies, etc. Might be split into a few parts later,
 * or even merged back into ServerLobby if proved useless. */
class LobbySettings: public LobbyContextComponent
{
public:
    LobbySettings(LobbyContext* context): LobbyContextComponent(context) {}

    void setupContextUser() OVERRIDE;
    ~LobbySettings();

    void initAvailableModes();
    void loadWhiteList();
    void loadPreservedSettings();
    bool hasConsentOnReplays() const           { return m_consent_on_replays; }
    void setConsentOnReplays(bool value)      { m_consent_on_replays = value; }

    std::string getHelpMessage() const               { return m_help_message; }
    std::string getMotd() const                              { return m_motd; }
    bool hasNoLapRestrictions() const;
    bool hasMultiplier() const;
    bool hasFixedLapCount() const;
    int getMultiplier() const;
    int getFixedLapCount() const;
    void setMultiplier(double new_value);
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
    void updateWorldSettings(std::shared_ptr<GameInfo> game_info);
    void onResetToDefaultSettings();
    bool isPreservingMode() const;
    std::string getPreservedSettingsAsString() const;
    void eraseFromPreserved(const std::string& value);
    void insertIntoPreserved(const std::string& value);
    void initializeDefaultVote();
    void applyRestrictionsOnVote(PeerVote* vote, Track* t) const;
    void applyRestrictionsOnWinnerVote(PeerVote* winner_vote) const;
    DefaultVotePacket encodeDefaultVote() const;
    void setDefaultVote(PeerVote winner_vote);
    PeerVote getDefaultVote() const;
    bool isInWhitelist(const std::string& username) const;
    bool isModeAvailable(int mode) const;
    bool isDifficultyAvailable(int difficulty) const;
    void resetWinnerPeerId()
                   { m_winner_peer_id = std::numeric_limits<uint32_t>::max(); }
    void setWinnerPeerId(uint32_t value)          { m_winner_peer_id = value; }

    int getBattleHitCaptureLimit() const { return m_battle_hit_capture_limit; }
    float getBattleTimeLimit() const            { return m_battle_time_limit; }
    // void setBattleHitCaptureLimit(int value)
                                        // { m_battle_hit_capture_limit = value; }
    // void setBattleTimeLimit(float value)       { m_battle_time_limit = value; }
    void setLobbyCooldown(int value)              { m_lobby_cooldown = value; }

    bool isCooldown() const;
    bool isSavingServerConfig() const          { return m_save_server_config; }

    void onServerSetup();
    void onServerConfiguration();

    void tryKickingAnotherPeer(std::shared_ptr<STKPeer> initiator,
                         std::shared_ptr<STKPeer> target) const;

    unsigned getCurrentMaxPlayersInGame() const
                                      { return m_current_max_players_in_game; }
    void setCurrentMaxPlayersInGame(unsigned value)
                                     { m_current_max_players_in_game = value; }

    bool isTempBanned(const std::string& username)
                { return m_temp_banned.find(username) != m_temp_banned.end(); }
    void tempBan(const std::string& username)
                                            { m_temp_banned.insert(username); }
    void tempUnban(const std::string& username)
                                             { m_temp_banned.erase(username); }
    void getLobbyHitCaptureLimit();

    // These were used unchanged from ServerConfig
    bool isLivePlayers()                   const { return m_live_players;                   }
    bool canConnectAiAnywhere()            const { return m_ai_anywhere;                    }
    bool hasAiHandling()                   const { return m_ai_handling;                    }
    int getCaptureLimit()                  const { return m_capture_limit;                  }
    bool isExposingMobile()                const { return m_expose_mobile;                  }
    bool isFirewalledServer()              const { return m_firewalled_server;              }
    float getFlagDeactivatedTime()         const { return m_flag_deactivated_time;          }
    float getFlagReturnTimeout()           const { return m_flag_return_timeout;            }
    bool hasFreeTeams()                    const { return m_free_teams;                     }
    bool hasHighPingWorkaround()           const { return m_high_ping_workaround;           }
    int getHitLimit()                      const { return m_hit_limit;                      }
    std::string getIncompatibleAdvice()    const { return m_incompatible_advice;            }
    int getJitterTolerance()               const { return m_jitter_tolerance;               }
    int getKickIdleLobbyPlayerSeconds()    const { return m_kick_idle_lobby_player_seconds; }
    int getKickIdlePlayerSeconds()         const { return m_kick_idle_player_seconds;       }
    bool hasKicksAllowed()                 const { return m_kicks_allowed;                  }
    int getMaxPing()                       const { return m_max_ping;                       }
    int getMinStartGamePlayers()           const { return m_min_start_game_players;         }
    bool isPreservingBattleScores()        const { return m_preserve_battle_scores;         }
    std::string getPrivateServerPassword() const { return m_private_server_password;        }
    bool isRanked()                        const { return m_ranked;                         }
    bool usesRealAddonKarts()              const { return m_real_addon_karts;               }
    bool isRecordingReplays()              const { return m_record_replays;                 }
    bool isServerConfigurable()            const { return m_server_configurable;            }
    int getServerDifficulty()              const { return m_server_difficulty;              }
    int getServerMaxPlayers()              const { return m_server_max_players;             }
    int getServerMode()                    const { return m_server_mode;                    }
    bool isSoccerGoalTargetInConfig()      const { return m_soccer_goal_target;             }
    bool hasSqlManagement()                const { return m_sql_management;                 }
    float getStartGameCounter()            const { return m_start_game_counter;             }
    int getStateFrequency()                const { return m_state_frequency;                }
    bool isStoringResults()                const { return m_store_results;                  }
    bool hasStrictPlayers()                const { return m_strict_players;                 }
    bool hasTeamChoosing()                 const { return m_team_choosing;                  }
    int getTimeLimitCtf()                  const { return m_time_limit_ctf;                 }
    int getTimeLimitFfa()                  const { return m_time_limit_ffa;                 }
    bool isTrackingKicks()                 const { return m_track_kicks;                    }
    bool hasTrackVoting()                  const { return m_track_voting;                   }
    std::string getTrollWarnMsg()          const { return m_troll_warn_msg;                 }
    bool isValidatingPlayer()              const { return m_validating_player;              }
    float getVotingTimeout()               const { return m_voting_timeout;                 }
    std::string getCommandsFile()          const { return m_commands_file;                  }
    std::string getPowerPassword(int level) const;
    std::string getRegisterTableName()     const { return m_register_table_name;            }
    int getLobbyCooldown()                 const { return m_lobby_cooldown;                 }

    // This one might not get into the config (as it originated from official
    // code's GP, where it is useless unless you want to make a private
    // server), however, it might be useful regardless
    bool isLegacyGPMode()                  const { return m_legacy_gp_mode;                 }
    bool isLegacyGPModeStarted()           const { return m_legacy_gp_mode_started;         }

private:

    int m_battle_hit_capture_limit;

    float m_battle_time_limit;

    std::set<std::string> m_config_available_tracks;

    std::map<std::string, std::vector<std::string>> m_config_track_limitations;

    std::string m_motd;

    std::string m_help_message;

    uint64_t m_last_reset;

    std::set<int> m_available_difficulties;

    std::set<int> m_available_modes;

    int m_fixed_lap;

    int m_fixed_direction;

    double m_default_lap_multiplier;

    std::set<std::string> m_usernames_white_list;

    std::set<std::string> m_preserve;

    bool m_allowed_to_start;

    bool m_consent_on_replays;

    bool m_shuffle_gp;

    bool m_live_players;

    bool m_ai_anywhere;
    bool m_ai_handling;
    int m_capture_limit;
    bool m_expose_mobile;
    bool m_firewalled_server;
    float m_flag_deactivated_time;
    float m_flag_return_timeout;
    bool m_free_teams;
    bool m_high_ping_workaround;
    int m_hit_limit;
    std::string m_incompatible_advice;
    int m_jitter_tolerance;
    int m_kick_idle_lobby_player_seconds;
    int m_kick_idle_player_seconds;
    bool m_kicks_allowed;
    int m_max_ping;
    int m_min_start_game_players;
    bool m_preserve_battle_scores;
    std::string m_private_server_password;
    bool m_ranked;
    bool m_real_addon_karts;
    bool m_record_replays;
    bool m_server_configurable;
    int m_server_difficulty;
    int m_server_max_players;
    int m_server_mode;
    bool m_soccer_goal_target;
    bool m_sql_management;
    float m_start_game_counter;
    int m_state_frequency;
    bool m_store_results;
    bool m_strict_players;
    bool m_team_choosing;
    int m_time_limit_ctf;
    int m_time_limit_ffa;
    bool m_track_kicks;
    bool m_track_voting;
    std::string m_troll_warn_msg;
    bool m_validating_player;
    float m_voting_timeout;
    std::string m_commands_file;
    std::string m_power_password;
    std::string m_power_password_level_2;
    std::string m_register_table_name;
    int m_lobby_cooldown;

    bool m_save_server_config;

    // Special, temporarily public
public:

    bool m_legacy_gp_mode;

    // This one corresponds to m_game_setup->isGrandPrixStarted()
    bool m_legacy_gp_mode_started;

    // Special, temporarily public END
private:

    unsigned m_current_max_players_in_game;

    std::set<std::string> m_temp_banned;

// These should be moved to voting manager ====================================

    // Default game settings if no one has ever vote, and save inside here for
    // final vote (for live join)
    PeerVote* m_default_vote;

    uint32_t m_winner_peer_id;

};

#endif // LOBBY_SETTINGS_HPP