//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2024-2025 kimden
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

#include "utils/game_info.hpp"

#include "config/stk_config.hpp"
#include "karts/kart_properties.hpp"
#include "karts/kart_properties_manager.hpp"
#include "karts/official_karts.hpp"
#include "modes/free_for_all.hpp"
#include "modes/linear_world.hpp"
#include "modes/soccer_world.hpp"
#include "modes/world.hpp"
#include "network/database_connector.hpp"
#include "network/network_player_profile.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/race_event_manager.hpp"
#include "network/remote_kart_info.hpp"
#include "network/stk_peer.hpp"
#include "race/race_manager.hpp"
#include "utils/lobby_settings.hpp"
#include "utils/string_utils.hpp"

#include <algorithm>

namespace
{

    // TODO: don't use file names as constants
    const std::string g_default_powerup_string   = "powerup.xml";
    const std::string g_default_kart_char_string = "kart_characteristics.xml";

    //-------------------------------------------------------------------------
    // This function also exists in an anonymous namespace in ServerLobby.
    // I'll decide later what to do with it.

    /** Returns true if world is active for clients to live join, spectate or
     *  going back to lobby live
     */
    bool worldIsActive()
    {
        return World::getWorld() && RaceEventManager::get()->isRunning() &&
            !RaceEventManager::get()->isRaceOver() &&
            World::getWorld()->getPhase() == WorldStatus::RACE_PHASE;
    }   // worldIsActive
    //-------------------------------------------------------------------------

}   // namespace


//-----------------------------------------------------------------------------
void GameInfo::setPowerupString(const std::string&& str)
{
    if (str == g_default_powerup_string)
        m_powerup_string = "";
    else
        m_powerup_string = str;
}   // setPowerupString

//-----------------------------------------------------------------------------
void GameInfo::setKartCharString(const std::string&& str)
{
    if (str == g_default_kart_char_string)
        m_kart_char_string = "";
    else
        m_kart_char_string = str;
}   // setKartCharString
//-----------------------------------------------------------------------------

void GameInfo::fillFromRaceManager()
{
    for (int i = 0; i < (int)RaceManager::get()->getNumPlayers(); i++)
    {
        PlayerInfo info;
        RemoteKartInfo& rki = RaceManager::get()->getKartInfo(i);
        if (!rki.isReserved())
        {
            info = GameInfo::PlayerInfo(false/* reserved */,
                                        false/* game event*/);
            // TODO: I suspected it to be local name, but it's not!
            info.m_username = StringUtils::wideToUtf8(rki.getPlayerName());
            info.m_kart = rki.getKartName();
            //info.m_start_position = i;
            info.m_when_joined = 0;
            info.m_country_code = rki.getCountryCode();
            info.m_online_id = rki.getOnlineId();
            info.m_kart_class = rki.getKartData().m_kart_type;
            info.m_kart_color = rki.getDefaultKartColor();
            info.m_handicap = (uint8_t)rki.getHandicap();
            info.m_team = (int8_t)rki.getKartTeam();
            if (info.m_team == KartTeam::KART_TEAM_NONE)
            {
                auto npp = rki.getNetworkPlayerProfile().lock();
                if (npp)
                    info.m_team = npp->getTemporaryTeam() - 1;
            }
        }
        else
        {
            info = GameInfo::PlayerInfo(true/* reserved */,
                                        false/* game event*/);
        }
        m_player_info.push_back(info);
    }
}   // fillFromRaceManager
//-----------------------------------------------------------------------------

int GameInfo::onLiveJoinedPlayer(int id, const RemoteKartInfo& rki, World* w)
{
    if (!m_player_info[id].isReserved())
    {
        Log::error("GameInfo", "While live joining kart %d, "
                "player info was not reserved.", id);
    }
    std::string name = StringUtils::wideToUtf8(rki.getPlayerName());
    auto& info = m_player_info[id];
    info = GameInfo::PlayerInfo(false/* reserved */,
            false/* game event*/);
    info.m_username = StringUtils::wideToUtf8(rki.getPlayerName());
    info.m_kart = rki.getKartName();
    info.m_start_position = w->getStartPosition(id);
    info.m_when_joined = STKConfig::get()->ticks2Time(w->getTicksSinceStart());
    info.m_country_code = rki.getCountryCode();
    info.m_online_id = rki.getOnlineId();
    info.m_kart_class = rki.getKartData().m_kart_type;
    info.m_kart_color = rki.getDefaultKartColor();
    info.m_handicap = (uint8_t)rki.getHandicap();
    info.m_team = (int8_t)rki.getKartTeam();
    if (info.m_team == KartTeam::KART_TEAM_NONE)
    {
        auto npp = rki.getNetworkPlayerProfile().lock();
        if (npp)
            info.m_team = npp->getTemporaryTeam() - 1;
    }
    int points = 0;
    if (RaceManager::get()->isBattleMode())
    {
        if (getSettings()->isPreservingBattleScores())
            points = m_saved_ffa_points[name];

        info.m_result -= points;
    }
    return points;
}   // onLiveJoinedPlayer
//-----------------------------------------------------------------------------

void GameInfo::saveDisconnectingPeerInfo(std::shared_ptr<STKPeer> peer)
{
    if (worldIsActive() && !peer->isWaitingForGame())
    {
        for (const int id : peer->getAvailableKartIDs())
        {
            saveDisconnectingIdInfo(id);
        }
    }
}   // saveDisconnectingPeerInfo

//-----------------------------------------------------------------------------
void GameInfo::saveDisconnectingIdInfo(int id)
{
    World* w = World::getWorld();
    FreeForAll* ffa_world = dynamic_cast<FreeForAll*>(w);
    LinearWorld* linear_world = dynamic_cast<LinearWorld*>(w);
    RemoteKartInfo& rki = RaceManager::get()->getKartInfo(id);
    int points = 0;
    m_player_info.emplace_back(true/* reserved */,
                               false/* game event*/);
    std::swap(m_player_info.back(), m_player_info[id]);
    auto& info = m_player_info.back();
    if (RaceManager::get()->isBattleMode())
    {
        if (ffa_world)
            points = ffa_world->getKartScore(id);
        info.m_result += points;
        if (getSettings()->isPreservingBattleScores())
            m_saved_ffa_points[StringUtils::wideToUtf8(rki.getPlayerName())] = points;
    }
    info.m_when_left = STKConfig::get()->ticks2Time(w->getTicksSinceStart());
    info.m_start_position = w->getStartPosition(id);
    if (RaceManager::get()->isLinearRaceMode())
    {
        info.m_autofinish = 0;
        info.m_fastest_lap = -1;
        info.m_sog_time = -1;
        if (linear_world)
        {
            info.m_fastest_lap = (double)linear_world->getFastestLapForKart(id);
            info.m_sog_time = linear_world->getStartTimeForKart(id);
        }
        info.m_not_full = 1;
        // If a player disconnects before the final screen but finished,
        // don't set him as "quitting"
        if (w->getKart(id)->hasFinishedRace())
            info.m_not_full = 0;
        if (info.m_not_full == 1)
            info.m_result = info.m_when_left;
        else
            info.m_result = RaceManager::get()->getKartRaceTime(id);
    }
}   // saveDisconnectingIdInfo
//-----------------------------------------------------------------------------

void GameInfo::fillAndStoreResults()
{
    World* w = World::getWorld();
    if (!w)
        return;

    RaceManager* rm = RaceManager::get();
    m_timestamp = StkTime::getLogTimeFormatted("%Y-%m-%d %H:%M:%S");
    m_venue = rm->getTrackName();
    m_reverse = (rm->getReverseTrack() ||
            rm->getRandomItemsIndicator() ? "reverse" : "normal");
    m_mode = rm->getMinorModeName();
    m_value_limit = 0;
    m_time_limit = rm->getTimeTarget();
    m_difficulty = getLobby()->getDifficulty();
    setPowerupString(powerup_manager->getFileName());
    setKartCharString(kart_properties_manager->getFileName());

    if (rm->getMinorMode() == RaceManager::MINOR_MODE_CAPTURE_THE_FLAG)
    {
        m_value_limit = rm->getHitCaptureLimit();
        m_flag_return_timeout = getSettings()->getFlagReturnTimeout();
        m_flag_deactivated_time = getSettings()->getFlagDeactivatedTime();
    }
    else if (rm->getMinorMode() == RaceManager::MINOR_MODE_FREE_FOR_ALL)
    {
        m_value_limit = rm->getHitCaptureLimit(); // TODO doesn't work
        m_flag_return_timeout = 0;
        m_flag_deactivated_time = 0;
    }
    else if (rm->getMinorMode() == RaceManager::MINOR_MODE_SOCCER)
    {
        m_value_limit = rm->getMaxGoal(); // TODO doesn't work
        m_flag_return_timeout = 0;
        m_flag_deactivated_time = 0;
    }
    else
    {
        m_value_limit = rm->getNumLaps();
        m_flag_return_timeout = 0;
        m_flag_deactivated_time = 0;
    }
    // m_timestamp = TODO;

    bool racing_mode = false;
    bool soccer = false;
    bool ffa = false;
    bool ctf = false;
    auto minor_mode = RaceManager::get()->getMinorMode();
    racing_mode |= minor_mode == RaceManager::MINOR_MODE_NORMAL_RACE;
    racing_mode |= minor_mode == RaceManager::MINOR_MODE_TIME_TRIAL;
    racing_mode |= minor_mode == RaceManager::MINOR_MODE_LAP_TRIAL;
    ffa |= minor_mode == RaceManager::MINOR_MODE_FREE_FOR_ALL;
    ctf |= minor_mode == RaceManager::MINOR_MODE_CAPTURE_THE_FLAG;
    soccer |= minor_mode == RaceManager::MINOR_MODE_SOCCER;

    // Kart class for standard karts
    // Goal scoring policy?
    // Do we really need round()/int for ingame timestamps?

    FreeForAll* ffa_world = dynamic_cast<FreeForAll*>(World::getWorld());
    LinearWorld* linear_world = dynamic_cast<LinearWorld*>(World::getWorld());

    bool record_fetched = false;
    bool record_exists = false;
    double best_result = 0.0;
    std::string best_user = "";

    if (racing_mode)
    {
#ifdef ENABLE_SQLITE3
        record_fetched = getDbConnector()->getBestResult(*this, &record_exists, &best_user, &best_result);
#endif
    }

    int best_cur_player_idx = -1;
    std::string best_cur_player_name = "";
    double best_cur_time = 1e18;

    double current_game_timestamp = STKConfig::get()->ticks2Time(w->getTicksSinceStart());

    auto& vec = m_player_info;

    // Set game duration for all items, including game events
    // and reserved PlayerInfos (even if those will be removed)
    for (unsigned i = 0; i < vec.size(); i++)
    {
        auto& info = vec[i];
        info.m_game_duration = current_game_timestamp;
        if (!info.isReserved() && info.m_kart_class.empty())
        {
            float width, height, length;
            Vec3 gravity_shift;
            const KartProperties* kp = OfficialKarts::getKartByIdent(info.m_kart,
                    &width, &height, &length, &gravity_shift);
            if (kp)
                info.m_kart_class = kp->getKartType();
        }
    }
    // For those players inside the game, set some variables
    for (unsigned i = 0; i < RaceManager::get()->getNumPlayers(); i++)
    {
        auto& info = vec[i];
        if (info.isReserved())
            continue;
        info.m_when_left = current_game_timestamp;
        info.m_start_position = w->getStartPosition(i);
        if (ffa || ctf)
        {
            int points = 0;
            if (ffa_world)
                points = ffa_world->getKartScore(i);
            else
                Log::error("GameInfo", "storeResults: battle mode but FFAWorld is not found");
            info.m_result += points;
            // I thought it would make sense to set m_autofinish to 1 if
            // someone wins before time expires. However, it's not hard
            // to check after reading the database I suppose...
            info.m_autofinish = 0;
        }
        else if (racing_mode)
        {
            info.m_fastest_lap = -1;
            info.m_sog_time = -1;
            info.m_autofinish = -1;
            info.m_result = rm->getKartRaceTime(i);
            float finish_timeout = std::numeric_limits<float>::max();
            if (linear_world)
            {
                info.m_fastest_lap = (double)linear_world->getFastestLapForKart(i);
                finish_timeout = linear_world->getWorstFinishTime();
                info.m_sog_time = linear_world->getStartTimeForKart(i);
                info.m_autofinish = (info.m_result < finish_timeout ? 0 : 1);
            }
            else
                Log::error("GameInfo", "storeResults: racing mode but LinearWorld is not found");
            info.m_not_full = 0;
        }
        else if (soccer)
        {
            // Soccer's m_result is handled in SoccerWorld
            info.m_autofinish = 0;
        }
    }
    // If the mode is not racing (in which case we set it separately),
    // set m_not_full to false iff he was present all the time
    if (!racing_mode)
    {
        for (unsigned i = 0; i < vec.size(); i++)
        {
            if (vec[i].m_reserved == 0 && vec[i].m_game_event == 0)
                vec[i].m_not_full = (vec[i].m_when_joined == 0 &&
                        vec[i].m_when_left == current_game_timestamp ? 0 : 1);
        }
    }
    // Remove reserved PlayerInfos. Note that the first getNumPlayers() items
    // no longer correspond to same ID in RaceManager (if they exist even)
    for (int i = 0; i < (int)vec.size(); i++)
    {
        if (vec[i].isReserved())
        {
            std::swap(vec[i], vec.back());
            vec.pop_back();
            --i;
        }
    }
    bool sort_asc = !racing_mode;
    std::sort(vec.begin(), vec.end(), [sort_asc]
            (GameInfo::PlayerInfo& lhs, GameInfo::PlayerInfo& rhs) -> bool {
        if (lhs.m_game_event != rhs.m_game_event)
            return lhs.m_game_event < rhs.m_game_event;
        if (lhs.m_when_joined != rhs.m_when_joined)
            return lhs.m_when_joined < rhs.m_when_joined;
        if (lhs.m_result != rhs.m_result)
            return (lhs.m_result > rhs.m_result) ^ sort_asc;
        return false;
    });
    if (soccer || ctf)
    {
        for (unsigned i = 1; i < vec.size(); i++)
        {
            if (vec[i].m_game_event == 1)
            {
                double previous = 0;
                if (vec[i - 1].m_game_event == 1)
                    previous = vec[i - 1].m_when_joined;
                vec[i].m_sog_time = vec[i].m_when_joined - previous;
            }
        }
    }
    // For racing, find who won and possibly beaten the record
    if (racing_mode)
    {
        for (unsigned i = 0; i < vec.size(); i++)
        {
            if (vec[i].m_reserved == 0 && vec[i].m_game_event == 0 &&
                    vec[i].m_not_full == 0 &&
                    (best_cur_player_idx == -1 || vec[i].m_result < best_cur_time))
            {
                best_cur_player_idx = i;
                best_cur_time = vec[i].m_result;
                best_cur_player_name = vec[i].m_username;
            }
        }
    }

    // Note that before, when online_id was 0, "* " was added to the beginning
    // of the name. I'm not sure it's really needed now that online_id is saved.

    // Note that before, string was used to write the result to take into
    // account increased precision for racing. Examples:
    // racing: elapsed_string << std::setprecision(4) << std::fixed << score;
    // ffa: elapsed_string << std::setprecision(0) << std::fixed << score;

    m_saved_ffa_points.clear();

#ifdef ENABLE_SQLITE3
    getDbConnector()->insertManyResults(*this);
#endif
    // end of insertions
    if (record_fetched && best_cur_player_idx != -1) // implies racing_mode
    {
        std::string message;
        if (!record_exists)
        {
            message = StringUtils::insertValues(
                "%s has just set a server record: %s\nThis is the first time set.",
                best_cur_player_name, StringUtils::timeToString(best_cur_time));
        }
        else if (best_result > best_cur_time)
        {
            message = StringUtils::insertValues(
                "%s has just beaten a server record: %s\nPrevious record: %s by %s",
                best_cur_player_name, StringUtils::timeToString(best_cur_time),
                StringUtils::timeToString(best_result), best_user);
        }
        if (!message.empty())
            getLobby()->sendStringToAllPeers(message);
    }

}   // fillAndStoreResults
//-----------------------------------------------------------------------------

void GameInfo::onFlagCaptured(bool red_team_scored, const irr::core::stringw& name,
        int kart_id, unsigned start_pos, float time_since_start)
{
    m_player_info.emplace_back(false/* reserved */,
                                          true/* game event*/);
    auto& info = m_player_info.back();
    RemoteKartInfo& rki = RaceManager::get()->getKartInfo(kart_id);
    info.m_username = StringUtils::wideToUtf8(name);
    info.m_result = (red_team_scored ? 1 : -1);
    info.m_kart = rki.getKartName();
    info.m_kart_class = rki.getKartData().m_kart_type;
    info.m_kart_color = rki.getDefaultKartColor();
    info.m_team = (int8_t)rki.getKartTeam();
    if (info.m_team == KartTeam::KART_TEAM_NONE)
    {
        auto npp = rki.getNetworkPlayerProfile().lock();
        if (npp)
            info.m_team = npp->getTemporaryTeam() - 1;
    }
    info.m_handicap = (uint8_t)rki.getHandicap();
    info.m_start_position = start_pos;
    info.m_online_id = rki.getOnlineId();
    info.m_country_code = rki.getCountryCode();
    info.m_when_joined = time_since_start;
    info.m_when_left = info.m_when_joined;
}   // onFlagCaptured
//-----------------------------------------------------------------------------

// Note that the signature is a bit different in the first bool
void GameInfo::onGoalScored(bool correct_goal, const irr::core::stringw& name,
        int kart_id, unsigned start_pos, float time_since_start)
{
    int sz = m_player_info.size();
    m_player_info.emplace_back(false/* reserved */,
                               true/* game event*/);

    Log::info("SoccerWorld", "player info size before: %d, after: %d",
            sz, (int)m_player_info.size());

    auto& info = m_player_info.back();
    RemoteKartInfo& rki = RaceManager::get()->getKartInfo(kart_id);
    info.m_username = StringUtils::wideToUtf8(name);
    info.m_result = (correct_goal ? 1 : -1);
    info.m_kart = rki.getKartName();
    info.m_kart_class = rki.getKartData().m_kart_type;
    info.m_kart_color = rki.getDefaultKartColor();
    info.m_team = (int8_t)rki.getKartTeam();
    if (info.m_team == KartTeam::KART_TEAM_NONE)
    {
        auto npp = rki.getNetworkPlayerProfile().lock();
        if (npp)
            info.m_team = npp->getTemporaryTeam() - 1;
    }
    info.m_handicap = (uint8_t)rki.getHandicap();
    info.m_start_position = start_pos;
    info.m_online_id = rki.getOnlineId();
    info.m_country_code = rki.getCountryCode();
    info.m_when_joined = time_since_start;
    info.m_when_left = info.m_when_joined;
    if (rki.isReserved())
    {
        // Unfortunately it's unknown which ID the corresponding player
        // has. Maybe the placement of items for disconnected players
        // should be changed in GameInfo::m_player_info. I have no idea
        // right now...
        info.m_not_full = 1;
    }
    else
    {
        info.m_not_full = 0;
        m_player_info[kart_id].m_result += info.m_result;
    }
}   // onGoalScored
//-----------------------------------------------------------------------------