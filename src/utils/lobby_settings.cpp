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

#include "utils/lobby_settings.hpp"

#include "modes/soccer_world.hpp"
#include "modes/world.hpp"
#include "network/game_setup.hpp"
#include "network/game_setup.hpp"
#include "network/network_config.hpp"
#include "network/network_string.hpp"
#include "network/peer_vote.hpp"
#include "network/server_config.hpp"
#include "network/stk_host.hpp"
#include "network/stk_peer.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/game_setup.hpp"
#include "tracks/track.hpp"
#include "tracks/track_manager.hpp"
#include "utils/communication.hpp"
#include "utils/game_info.hpp"
#include "utils/kart_elimination.hpp"
#include "utils/lobby_asset_manager.hpp"
#include "utils/lobby_queues.hpp"
#include "utils/random_generator.hpp"
#include "utils/string_utils.hpp"
#include "utils/tournament.hpp"


void LobbySettings::setupContextUser()
{

    m_motd = StringUtils::wideToUtf8(
        getGameSetupFromCtx()->readOrLoadFromFile(
            (std::string) ServerConfig::m_motd
        )
    );
    m_help_message = StringUtils::wideToUtf8(
        getGameSetupFromCtx()->readOrLoadFromFile(
            (std::string) ServerConfig::m_help
        )
    );
    m_shuffle_gp = ServerConfig::m_shuffle_gp;
    m_consent_on_replays = false;
    
    m_legacy_gp_mode = false;
    m_legacy_gp_mode_started = false;

    m_last_reset = 0;
    m_save_server_config = true;

    m_fixed_direction = ServerConfig::m_fixed_direction;

    m_current_max_players_in_game = ServerConfig::m_max_players_in_game;

    setDefaultLapRestrictions();

    m_default_vote = new PeerVote();
    m_allowed_to_start = ServerConfig::m_allowed_to_start;

    initAvailableModes();

    loadWhiteList();
    loadPreservedSettings();

    m_live_players = ServerConfig::m_live_players;

    m_ai_anywhere                    = ServerConfig::m_ai_anywhere;
    m_ai_handling                    = ServerConfig::m_ai_handling;
    m_capture_limit                  = ServerConfig::m_capture_limit;
    m_expose_mobile                  = ServerConfig::m_expose_mobile;
    m_firewalled_server              = ServerConfig::m_firewalled_server;
    m_flag_deactivated_time          = ServerConfig::m_flag_deactivated_time;
    m_flag_return_timeout            = ServerConfig::m_flag_return_timeout;
    m_free_teams                     = ServerConfig::m_free_teams;
    m_high_ping_workaround           = ServerConfig::m_high_ping_workaround;
    m_hit_limit                      = ServerConfig::m_hit_limit;
    m_incompatible_advice            = ServerConfig::m_incompatible_advice;
    m_jitter_tolerance               = ServerConfig::m_jitter_tolerance;
    m_kick_idle_lobby_player_seconds = ServerConfig::m_kick_idle_lobby_player_seconds;
    m_kick_idle_player_seconds       = ServerConfig::m_kick_idle_player_seconds;
    m_kicks_allowed                  = ServerConfig::m_kicks_allowed;
    m_max_ping                       = ServerConfig::m_max_ping;
    m_min_start_game_players         = ServerConfig::m_min_start_game_players;
    m_preserve_battle_scores         = ServerConfig::m_preserve_battle_scores;
    m_private_server_password        = ServerConfig::m_private_server_password;
    m_ranked                         = ServerConfig::m_ranked;
    m_real_addon_karts               = ServerConfig::m_real_addon_karts;
    m_record_replays                 = ServerConfig::m_record_replays;
    m_server_configurable            = ServerConfig::m_server_configurable;
    m_server_difficulty              = ServerConfig::m_server_difficulty;
    m_server_max_players             = ServerConfig::m_server_max_players;
    m_server_mode                    = ServerConfig::m_server_mode;
    m_soccer_goal_target             = ServerConfig::m_soccer_goal_target;
    m_sql_management                 = ServerConfig::m_sql_management;
    m_start_game_counter             = ServerConfig::m_start_game_counter;
    m_state_frequency                = ServerConfig::m_state_frequency;
    m_store_results                  = ServerConfig::m_store_results;
    m_strict_players                 = ServerConfig::m_strict_players;
    m_team_choosing                  = ServerConfig::m_team_choosing;
    m_time_limit_ctf                 = ServerConfig::m_time_limit_ctf;
    m_time_limit_ffa                 = ServerConfig::m_time_limit_ffa;
    m_track_kicks                    = ServerConfig::m_track_kicks;
    m_track_voting                   = ServerConfig::m_track_voting;
    m_troll_warn_msg                 = ServerConfig::m_troll_warn_msg;
    m_validating_player              = ServerConfig::m_validating_player;
    m_voting_timeout                 = ServerConfig::m_voting_timeout;
    m_commands_file                  = ServerConfig::m_commands_file;
    m_power_password                 = ServerConfig::m_power_password;
    m_power_password_level_2         = ServerConfig::m_power_password_level_2;
    m_register_table_name            = ServerConfig::m_register_table_name;
    m_lobby_cooldown                 = ServerConfig::m_lobby_cooldown;
}   // setupContextUser
//-----------------------------------------------------------------------------

LobbySettings::~LobbySettings()
{
    delete m_default_vote;
}   // ~LobbySettings
//-----------------------------------------------------------------------------

void LobbySettings::initAvailableModes()
{
    std::vector<std::string> statements =
        StringUtils::split(ServerConfig::m_available_modes, ' ', false);

    for (const std::string& s: statements)
    {
        if (s.length() <= 1)
            continue;
        bool difficulty = s[0] == 'd';
        if (difficulty)
        {
            for (unsigned i = 1; i < s.length(); i++)
            {
                m_available_difficulties.insert(s[i] - '0');
            }
        }
        else
        {
            for (unsigned i = 1; i < s.length(); i++)
            {
                m_available_modes.insert(s[i] - '0');
            }
        }
    }
}  // initAvailableModes
//-----------------------------------------------------------------------------

void LobbySettings::loadWhiteList()
{
    std::vector<std::string> tokens = StringUtils::split(
        ServerConfig::m_white_list, ' ');
    for (std::string& s: tokens)
        m_usernames_white_list.insert(s);
}   // loadWhiteList
//-----------------------------------------------------------------------------

void LobbySettings::loadPreservedSettings()
{
    std::vector<std::string> what_to_preserve = StringUtils::split(
            std::string(ServerConfig::m_preserve_on_reset), ' ');
    for (std::string& str: what_to_preserve)
        m_preserve.insert(str);
}   // loadPreservedSettings
//-----------------------------------------------------------------------------

bool LobbySettings::hasNoLapRestrictions() const
{
    return m_default_lap_multiplier < 0. && m_fixed_lap < 0;
}   // hasNoLapRestrictions
//-----------------------------------------------------------------------------

bool LobbySettings::hasMultiplier() const
{
    return m_default_lap_multiplier >= 0.;
}   // hasMultiplier
//-----------------------------------------------------------------------------

bool LobbySettings::hasFixedLapCount() const
{
    return m_fixed_lap >= 0;
}   // hasFixedLapCount
//-----------------------------------------------------------------------------

int LobbySettings::getMultiplier() const
{
    return m_default_lap_multiplier;
}   // getMultiplier
//-----------------------------------------------------------------------------

int LobbySettings::getFixedLapCount() const
{
    return m_fixed_lap;
}   // getFixedLapCount
//-----------------------------------------------------------------------------

void LobbySettings::setMultiplier(double new_value)
{
    m_default_lap_multiplier = new_value;
    m_fixed_lap = -1;
}   // setMultiplier
//-----------------------------------------------------------------------------

void LobbySettings::setFixedLapCount(int new_value)
{
    m_fixed_lap = new_value;
    m_default_lap_multiplier = -1;
}   // setFixedLapCount
//-----------------------------------------------------------------------------

void LobbySettings::resetLapRestrictions()
{
    m_default_lap_multiplier = -1;
    m_fixed_lap = -1;
}   // resetLapRestrictions
//-----------------------------------------------------------------------------

void LobbySettings::setDefaultLapRestrictions()
{
    m_default_lap_multiplier = ServerConfig::m_auto_game_time_ratio;
    m_fixed_lap = ServerConfig::m_fixed_lap_count;
}   // setDefaultLapRestrictions
//-----------------------------------------------------------------------------

std::string LobbySettings::getLapRestrictionsAsString() const
{
    if (hasNoLapRestrictions())
        return "Game length is currently chosen by players";
    
    if (hasMultiplier())
        return StringUtils::insertValues(
            "Game length is %f x default",
            getMultiplier());
    
    if (hasFixedLapCount() > 0)
        return StringUtils::insertValues(
            "Game length is %d", getFixedLapCount());

    return StringUtils::insertValues(
        "An error: game length is both %f x default and %d",
        m_default_lap_multiplier, m_fixed_lap);
}   // getLapRestrictionsAsString
//-----------------------------------------------------------------------------

std::string LobbySettings::getDirectionAsString(bool just_edited) const
{
    std::string msg = "Direction is ";
    if (just_edited)
        msg += "now ";
    if (m_fixed_direction == -1)
        msg += "chosen ingame";
    else
    {
        msg += "set to ";
        msg += (m_fixed_direction == 0 ? "forward" : "reverse");
    }
    return msg;
}   // getDirectionAsString
//-----------------------------------------------------------------------------

bool LobbySettings::setDirection(int x)
{
    if (x < -1 || x > 1)
        return false;

    m_fixed_direction = x;
    return true;
}   // setDirection
//-----------------------------------------------------------------------------

bool LobbySettings::hasFixedDirection() const
{
    return m_fixed_direction != -1;
}   // hasFixedDirection
//-----------------------------------------------------------------------------

int LobbySettings::getDirection() const
{
    return m_fixed_direction;
}   // getDirection
//-----------------------------------------------------------------------------

bool LobbySettings::isAllowedToStart() const
{
    return m_allowed_to_start;
}   // isAllowedToStart
//-----------------------------------------------------------------------------

void LobbySettings::setAllowedToStart(bool value)
{
    m_allowed_to_start = value;
}   // setAllowedToStart
//-----------------------------------------------------------------------------

std::string LobbySettings::getAllowedToStartAsString(bool just_edited) const
{
    std::string prefix = (just_edited ? "Now s" : "S");
    prefix += "tarting the game is ";
    if (isAllowedToStart())
        return prefix + "allowed";
    else
        return prefix + "forbidden";
}   // getAllowedToStartAsString
//-----------------------------------------------------------------------------

bool LobbySettings::isGPGridShuffled() const
{
    return m_shuffle_gp;
}   // isGPGridShuffled
//-----------------------------------------------------------------------------

void LobbySettings::setGPGridShuffled(bool value)
{
    m_shuffle_gp = value;
}   // setGPGridShuffled
//-----------------------------------------------------------------------------

std::string LobbySettings::getWhetherShuffledGPGridAsString(bool just_edited) const
{
    std::string prefix = "The GP grid is ";
    prefix += (just_edited ? "now " : "");
    if (m_shuffle_gp)
        return prefix + "sorted by score";
    else
        return prefix + "shuffled";
}   // getWhetherShuffledGPGridAsString
//-----------------------------------------------------------------------------

void LobbySettings::updateWorldSettings(std::shared_ptr<GameInfo> game_info)
{
    World::getWorld()->setGameInfo(game_info);
    SoccerWorld *sw = dynamic_cast<SoccerWorld*>(World::getWorld());
    if (sw)
    {
        std::string policy = ServerConfig::m_soccer_goals_policy;
        if (policy == "standard")
            sw->setGoalScoringPolicy(0);
        else if (policy == "no-own-goals")
            sw->setGoalScoringPolicy(1);
        else if (policy == "advanced")
            sw->setGoalScoringPolicy(2);
        else
            Log::warn("LobbySettings", "Soccer goals policy %s "
                    "does not exist", policy.c_str());
    }
}   // updateWorldSettings
//-----------------------------------------------------------------------------

void LobbySettings::onResetToDefaultSettings()
{
    getQueues()->resetToDefaultSettings(m_preserve);

    if (!m_preserve.count("elim"))
        getKartElimination()->disable();

    if (!m_preserve.count("laps"))
    {
        // We don't reset restrictions, but set them to those in config
        setDefaultLapRestrictions();
    }

    if (!m_preserve.count("direction"))
        m_fixed_direction = ServerConfig::m_fixed_direction;

    if (!m_preserve.count("replay"))
        setConsentOnReplays(false);
}   // onResetToDefaultSettings
//-----------------------------------------------------------------------------

bool LobbySettings::isPreservingMode() const
{
    return m_preserve.find("mode") != m_preserve.end();
}   // isPreservingMode
//-----------------------------------------------------------------------------

std::string LobbySettings::getPreservedSettingsAsString() const
{
    std::string msg = "Preserved settings:";
    for (const std::string& str: m_preserve)
        msg += " " + str;
    return msg;
}   // getPreservedSettingsAsString
//-----------------------------------------------------------------------------

void LobbySettings::eraseFromPreserved(const std::string& value)
{
    m_preserve.erase(value);
}   // eraseFromPreserved
//-----------------------------------------------------------------------------

void LobbySettings::insertIntoPreserved(const std::string& value)
{
    m_preserve.insert(value);
}   // insertIntoPreserved
//-----------------------------------------------------------------------------

void LobbySettings::initializeDefaultVote()
{
    m_default_vote->m_track_name = getAssetManager()->getRandomAvailableMap();
    RandomGenerator rg;
    switch (RaceManager::get()->getMinorMode())
    {
        case RaceManager::MINOR_MODE_NORMAL_RACE:
        case RaceManager::MINOR_MODE_TIME_TRIAL:
        case RaceManager::MINOR_MODE_FOLLOW_LEADER:
        {
            Track* t = TrackManager::get()->getTrack(m_default_vote->m_track_name);
            assert(t);
            m_default_vote->m_num_laps = t->getDefaultNumberOfLaps();
            if (ServerConfig::m_auto_game_time_ratio > 0.0f)
            {
                m_default_vote->m_num_laps =
                    (uint8_t)(fmaxf(1.0f, (float)t->getDefaultNumberOfLaps() *
                    ServerConfig::m_auto_game_time_ratio));
            }
            else if (hasFixedLapCount())
                m_default_vote->m_num_laps = getFixedLapCount();
            m_default_vote->m_reverse = rg.get(2) == 0;

            if (hasFixedDirection())
                m_default_vote->m_reverse = (getDirection() == 1);
            break;
        }
        case RaceManager::MINOR_MODE_FREE_FOR_ALL:
        {
            m_default_vote->m_num_laps = 0;
            m_default_vote->m_reverse = rg.get(2) == 0;
            break;
        }
        case RaceManager::MINOR_MODE_CAPTURE_THE_FLAG:
        {
            m_default_vote->m_num_laps = 0;
            m_default_vote->m_reverse = 0;
            break;
        }
        case RaceManager::MINOR_MODE_SOCCER:
        {
            if (isTournament())
            {
                getTournament()->applyRestrictionsOnDefaultVote(m_default_vote);
            }
            else
            {
                if (getGameSetupFromCtx()->isSoccerGoalTarget())
                {
                    m_default_vote->m_num_laps =
                        (uint8_t)(UserConfigParams::m_num_goals);
                    if (m_default_vote->m_num_laps > 10)
                        m_default_vote->m_num_laps = (uint8_t)5;
                }
                else
                {
                    m_default_vote->m_num_laps =
                        (uint8_t)(UserConfigParams::m_soccer_time_limit);
                    if (m_default_vote->m_num_laps > 15)
                        m_default_vote->m_num_laps = (uint8_t)7;
                }
                m_default_vote->m_reverse = rg.get(2) == 0;
            }
            break;
        }
        default:
            assert(false);
            break;
    }
}   // initializeDefaultVote
//-----------------------------------------------------------------------------

void LobbySettings::applyRestrictionsOnVote(PeerVote* vote, Track* t) const
{
    if (RaceManager::get()->modeHasLaps())
    {
        if (hasMultiplier())
        {
            vote->m_num_laps =
                (uint8_t)(fmaxf(1.0f, (float)t->getDefaultNumberOfLaps() *
                getMultiplier()));
        }
        else if (vote->m_num_laps == 0 || vote->m_num_laps > 20)
            vote->m_num_laps = (uint8_t)3;
        if (!t->reverseAvailable() && vote->m_reverse)
            vote->m_reverse = false;
    }
    else if (RaceManager::get()->isSoccerMode())
    {
        if (getGameSetupFromCtx()->isSoccerGoalTarget())
        {
            if (hasMultiplier())
            {
                vote->m_num_laps = (uint8_t)(getMultiplier() *
                                            UserConfigParams::m_num_goals);
            }
            else if (vote->m_num_laps > 10)
                vote->m_num_laps = (uint8_t)5;
        }
        else
        {
            if (hasMultiplier())
            {
                vote->m_num_laps = (uint8_t)(getMultiplier() *
                                            UserConfigParams::m_soccer_time_limit);
            }
            else if (vote->m_num_laps > 15)
                vote->m_num_laps = (uint8_t)7;
        }
    }
    else if (RaceManager::get()->getMinorMode() ==
        RaceManager::MINOR_MODE_FREE_FOR_ALL)
    {
        vote->m_num_laps = 0;
    }
    else if (RaceManager::get()->getMinorMode() ==
        RaceManager::MINOR_MODE_CAPTURE_THE_FLAG)
    {
        vote->m_num_laps = 0;
        vote->m_reverse = false;
    }
    if (hasFixedLapCount())
    {
        vote->m_num_laps = getFixedLapCount();
    }
    if (hasFixedDirection())
        vote->m_reverse = (getDirection() == 1);
}   // applyRestrictionsOnVote
//-----------------------------------------------------------------------------

void LobbySettings::applyRestrictionsOnWinnerVote(PeerVote* winner_vote) const
{
    if (hasFixedLapCount())
    {
        winner_vote->m_num_laps = getFixedLapCount();
        Log::info("LobbySettings", "Enforcing %d lap race", getFixedLapCount());
    }
    if (hasFixedDirection())
    {
        winner_vote->m_reverse = (getDirection() == 1);
        Log::info("LobbySettings", "Enforcing direction %d", (int)getDirection());
    }
}   // applyRestrictionsOnWinnerVote
//-----------------------------------------------------------------------------

DefaultVotePacket LobbySettings::encodeDefaultVote() const
{
    DefaultVotePacket packet;
    packet.winner_peer_id = m_winner_peer_id;
    packet.default_vote = m_default_vote->encode();
    return packet;
}   // encodeDefaultVote
//-----------------------------------------------------------------------------


void LobbySettings::setDefaultVote(PeerVote winner_vote)
{
    *m_default_vote = winner_vote;
}   // setDefaultVote
//-----------------------------------------------------------------------------

PeerVote LobbySettings::getDefaultVote() const
{
    return *m_default_vote;
}   // getDefaultVote
//-----------------------------------------------------------------------------

bool LobbySettings::isInWhitelist(const std::string& username) const
{
    return m_usernames_white_list.find(username) != m_usernames_white_list.end();
}   // isInWhitelist
//-----------------------------------------------------------------------------

bool LobbySettings::isModeAvailable(int mode) const
{
    return m_available_modes.find(mode) != m_available_modes.end();
}   // isModeAvailable
//-----------------------------------------------------------------------------

bool LobbySettings::isDifficultyAvailable(int difficulty) const
{
    return m_available_difficulties.find(difficulty) != m_available_difficulties.end();
}   // isDifficultyAvailable
//-----------------------------------------------------------------------------

void LobbySettings::onServerSetup()
{
    m_battle_hit_capture_limit = 0;
    m_battle_time_limit = 0.0f;
    m_winner_peer_id = 0;

    m_last_reset = StkTime::getMonoTimeMs();

    NetworkConfig::get()->setTuxHitboxAddon(m_live_players);
}   // onServerSetup
//-----------------------------------------------------------------------------

void LobbySettings::onServerConfiguration()
{
    m_last_reset = StkTime::getMonoTimeMs();
}   // onServerConfiguration
//-----------------------------------------------------------------------------

void LobbySettings::tryKickingAnotherPeer(std::shared_ptr<STKPeer> initiator,
                                    std::shared_ptr<STKPeer> target) const
{
    // Should probably include hammer permissions, but this function is
    // originated from the ingame button press. Unify all such functions later.
    if (getLobby()->getServerOwner() != initiator)
        return;

    if (!hasKicksAllowed())
    {
        Comm::sendStringToPeer(initiator, "Kicking players is not allowed on this server");
        return;
    }

    // Ignore kicking ai peer if ai handling is on
    if (target && (!hasAiHandling() || !target->isAIPeer()))
    {
        if (target->hammerLevel() > 0)
        {
            Comm::sendStringToPeer(initiator, "This player holds admin rights of this server, "
                "and is protected from your actions now");
            return;
        }
        if (!target->hasPlayerProfiles())
        {
            Log::info("ServerLobby", "Crown player kicks a player");
        }
        else
        {
            std::string player_name = target->getMainName();
            Log::info("ServerLobby", "Crown player kicks %s", player_name.c_str());
        }
        target->kick();
        if (isTrackingKicks())
        {
            std::string auto_report = "[ Auto report caused by kick ]";
            getLobby()->writeOwnReport(target, initiator, auto_report);
        }
    }
}   // tryKickingAnotherPeer
//-----------------------------------------------------------------------------

bool LobbySettings::isCooldown() const
{
    int64_t passed_since_reset = (int64_t)StkTime::getMonoTimeMs() - m_last_reset;
    return passed_since_reset < 1000 * m_lobby_cooldown;
}   // isCooldown
//-----------------------------------------------------------------------------

void LobbySettings::getLobbyHitCaptureLimit()
{
    int hit_capture_limit = std::numeric_limits<int>::max();
    float time_limit = 0.0f;
    if (RaceManager::get()->getMinorMode() ==
        RaceManager::MINOR_MODE_CAPTURE_THE_FLAG)
    {
        if (m_capture_limit > 0)
            hit_capture_limit = m_capture_limit;
        if (m_time_limit_ctf > 0)
            time_limit = (float)m_time_limit_ctf;
    }
    else
    {
        if (m_hit_limit > 0)
            hit_capture_limit = m_hit_limit;
        if (m_time_limit_ffa > 0.0f)
            time_limit = (float)m_time_limit_ffa;
    }
    m_battle_hit_capture_limit = hit_capture_limit;
    m_battle_time_limit = time_limit;
}   // getLobbyHitCaptureLimit
// ----------------------------------------------------------------------------

std::string LobbySettings::getPowerPassword(int level) const
{
    if (level == 1)
        return m_power_password;
    if (level == 2)
        return m_power_password_level_2;
    
    Log::error("LobbySettings", "Invoked getPowerPassword with level = %d", level);
    return "";
}
