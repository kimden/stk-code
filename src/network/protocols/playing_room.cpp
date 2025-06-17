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

#include "network/protocols/playing_room.hpp"

#include "utils/log.hpp"
#include "network/server_config.hpp"
#include "network/stk_host.hpp"
#include "network/stk_peer.hpp"
#include "network/network_player_profile.hpp"
#include "network/network_string.hpp"
#include "utils/game_info.hpp"
#include "items/network_item_manager.hpp"
#include "network/game_setup.hpp"
#include "utils/lobby_asset_manager.hpp"
#include "config/stk_config.hpp"
#include "utils/lobby_settings.hpp"
#include "karts/abstract_kart.hpp"
#include "modes/linear_world.hpp"
#include "modes/world.hpp"
#include "modes/free_for_all.hpp"
#include "tracks/track_manager.hpp"
#include "network/peer_vote.hpp"
#include "network/protocol_manager.hpp"
#include "tracks/check_manager.hpp"
#include "network/socket_address.hpp"
#include "network/race_event_manager.hpp"
#include "tracks/track.hpp"
#include "network/event.hpp"
#include "network/protocols/command_permissions.hpp"
#include "utils/tournament.hpp"
#include "utils/crown_manager.hpp"
#include "utils/communication.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/protocols/command_manager.hpp"
#include "utils/team_manager.hpp"
#include "utils/lobby_queues.hpp"
#include "utils/kart_elimination.hpp"
#include "network/network_config.hpp"
#include "utils/lobby_gp_manager.hpp"
#include "network/protocols/game_protocol.hpp"
#include "utils/hit_processor.hpp"
#include "modes/capture_the_flag.hpp"
#include "utils/chat_manager.hpp"
#include "network/protocols/game_events_protocol.hpp"

namespace
{
    void encodePlayers(BareNetworkString* bns,
        std::vector<std::shared_ptr<NetworkPlayerProfile> >& players,
        const std::shared_ptr<GenericDecorator>& decorator)
    {
        bns->addUInt8((uint8_t)players.size());
        for (unsigned i = 0; i < players.size(); i++)
        {
            std::shared_ptr<NetworkPlayerProfile>& player = players[i];
            bns->encodeString(player->getDecoratedName(decorator))
                .addUInt32(player->getHostId())
                .addFloat(player->getDefaultKartColor())
                .addUInt32(player->getOnlineId())
                .addUInt8(player->getHandicap())
                .addUInt8(player->getLocalPlayerId())
                .addUInt8(player->getTeam())
                .encodeString(player->getCountryCode());
            bns->encodeString(player->getKartName());
        }
    }   // encodePlayers
    //-------------------------------------------------------------------------

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

    NetworkString* getNetworkString(size_t capacity = 16)
    {
        return new NetworkString(PROTOCOL_LOBBY_ROOM, capacity);
    }   // getNetworkString
    //-------------------------------------------------------------------------

    /** Get a list of current ingame players for live join or spectate.
     */
    std::vector<std::shared_ptr<NetworkPlayerProfile> > getLivePlayers()
    {
        std::vector<std::shared_ptr<NetworkPlayerProfile> > players;
        for (unsigned i = 0; i < RaceManager::get()->getNumPlayers(); i++)
        {
            const RemoteKartInfo& rki = RaceManager::get()->getKartInfo(i);
            std::shared_ptr<NetworkPlayerProfile> player =
                rki.getNetworkPlayerProfile().lock();
            if (!player)
            {
                if (RaceManager::get()->modeHasLaps())
                {
                    player = std::make_shared<NetworkPlayerProfile>(
                        nullptr, rki.getPlayerName(),
                        std::numeric_limits<uint32_t>::max(),
                        rki.getDefaultKartColor(),
                        rki.getOnlineId(), rki.getHandicap(),
                        rki.getLocalPlayerId(), KART_TEAM_NONE,
                        rki.getCountryCode());
                    player->setKartName(rki.getKartName());
                }
                else
                {
                    player = NetworkPlayerProfile::getReservedProfile(
                        RaceManager::get()->getMinorMode() ==
                        RaceManager::MINOR_MODE_FREE_FOR_ALL ?
                        KART_TEAM_NONE : rki.getKartTeam());
                }
            }
            players.push_back(player);
        }
        return players;
    }   // getLivePlayers
    //-------------------------------------------------------------------------

}   // namespace
//=============================================================================

PlayingRoom::PlayingRoom()
{
    m_lobby_players.store(0);
    m_current_ai_count.store(0);
    m_rs_state.store(RS_NONE);
    m_server_owner_id.store(-1);
    m_play_state = WAITING_FOR_START_GAME;
    m_items_complete_state = new BareNetworkString();
    m_difficulty.store(ServerConfig::m_server_difficulty);
    m_game_mode.store(ServerConfig::m_server_mode);
    m_reset_to_default_mode_later.store(false);

    m_game_info = {};
}   // PlayingRoom
//-----------------------------------------------------------------------------

PlayingRoom::~PlayingRoom()
{
    delete m_items_complete_state;
}   // ~PlayingRoom
//-----------------------------------------------------------------------------

[[deprecated("STKHost and GameSetup are used in a room method.")]]
void PlayingRoom::setup()
{
    m_item_seed = 0;
    m_client_starting_time = 0;
    m_ai_count = 0;

    auto players = STKHost::get()->getPlayersForNewGame();
    auto game_setup = getGameSetupFromCtx();
    if (game_setup->isGrandPrix() && !game_setup->isGrandPrixStarted())
    {
        for (auto player : players)
            player->resetGrandPrixData();
    }
    if (!game_setup->isGrandPrix() || !game_setup->isGrandPrixStarted())
    {
        for (auto player : players)
            player->setKartName("");
    }
    if (auto ai = m_ai_peer.lock())
    {
        for (auto player : ai->getPlayerProfiles())
            player->setKartName("");
    }
    for (auto ai : m_ai_profiles)
        ai->setKartName("");

    updateMapsForMode();

    m_server_has_loaded_world.store(false);
    // Initialise the data structures to detect if all clients and 
    // the server are ready:
    resetPeersReady();
    setInfiniteTimeout();
    m_server_started_at = m_server_delay = 0;
    m_game_info = {};

    Log::info("PlayingRoom", "Resetting room to its initial state.");
}   // PlayingRoom::setup()
//-----------------------------------------------------------------------------

void PlayingRoom::updateRoom(int ticks)
{
    World* w = World::getWorld();
    bool world_started = m_play_state.load() >= WAIT_FOR_WORLD_LOADED &&
        m_play_state.load() <= RACING && m_server_has_loaded_world.load();
    bool all_players_in_world_disconnected = (w != NULL && world_started);
    int sec = getSettings()->getKickIdlePlayerSeconds();
    if (world_started)
    {
        for (unsigned i = 0; i < RaceManager::get()->getNumPlayers(); i++)
        {
            RemoteKartInfo& rki = RaceManager::get()->getKartInfo(i);
            std::shared_ptr<NetworkPlayerProfile> player =
                rki.getNetworkPlayerProfile().lock();
            if (player)
            {
                if (w)
                    all_players_in_world_disconnected = false;
            }
            else
                continue;
            auto peer = player->getPeer();
            if (!peer)
                continue;

            if (peer->idleForSeconds() > 60 && w &&
                w->getKart(i)->isEliminated())
            {
                // Remove loading world too long (60 seconds) live join peer
                Log::info("ServerLobby", "%s hasn't live-joined within"
                    " 60 seconds, remove it.",
                    peer->getAddress().toString().c_str());
                rki.makeReserved();
                continue;
            }
            if (!peer->isAIPeer() &&
                sec > 0 && peer->idleForSeconds() > sec &&
                !peer->isDisconnected() && NetworkConfig::get()->isWAN())
            {
                if (w && w->getKart(i)->hasFinishedRace())
                    continue;
                // Don't kick in game GUI server host so he can idle in game
                if (getLobby()->isChildClientServerHost(peer))
                    continue;
                Log::info("ServerLobby", "%s %s has been idle ingame for more than"
                    " %d seconds, kick.",
                    peer->getAddress().toString().c_str(),
                    StringUtils::wideToUtf8(rki.getPlayerName()).c_str(), sec);
                peer->kick();
            }
            if (getHitProcessor()->isAntiTrollActive() && !peer->isAIPeer())
            {
                // for all human players
                // if they troll, kick them
                LinearWorld *lin_world = dynamic_cast<LinearWorld*>(w);
                if (lin_world) {
                    // check warn level for each player
                    switch(lin_world->getWarnLevel(i))
                    {
                        case 0:
                            break;
                        case 1:
                        {
                            Comm::sendStringToPeer(peer, getSettings()->getTrollWarnMsg());
                            std::string player_name = peer->getMainName();
                            Log::info("ServerLobby-AntiTroll", "Sent WARNING to %s", player_name.c_str());
                            break;
                        }
                        default:
                        {
                            std::string player_name = peer->getMainName();
                            Log::info("ServerLobby-AntiTroll", "KICKING %s", player_name.c_str());
                            peer->kick();
                            break;
                        }
                    }
                }
            }
            getHitProcessor()->punishSwatterHits();
        }
    }
    if (m_play_state.load() == WAITING_FOR_START_GAME) {
        sec = getSettings()->getKickIdleLobbyPlayerSeconds();
        auto peers = STKHost::get()->getPeers();
        for (auto peer: peers)
        {
            if (!peer->isAIPeer() &&
                sec > 0 && peer->idleForSeconds() > sec &&
                !peer->isDisconnected() && NetworkConfig::get()->isWAN())
            {
                // Don't kick in game GUI server host so he can idle in the lobby
                if (getLobby()->isChildClientServerHost(peer))
                    continue;
                std::string peer_name = "";
                if (peer->hasPlayerProfiles())
                    peer_name = peer->getMainName().c_str();
                Log::info("ServerLobby", "%s %s has been idle on the server for "
                        "more than %d seconds, kick.",
                        peer->getAddress().toString().c_str(), peer_name.c_str(), sec);
                peer->kick();
            }
        }
    }

    if (w)
        setGameStartedProgress(w->getGameStartedProgress());
    else
        resetGameStartedProgress();

    if (w && w->getPhase() == World::RACE_PHASE)
        storePlayingTrack(RaceManager::get()->getTrackName());
    else
        storePlayingTrack("");

    // Reset server to initial state if no more connected players
    if (m_rs_state.load() == RS_WAITING)
    {
        if ((RaceEventManager::get() &&
            !RaceEventManager::get()->protocolStopped()) ||
            !GameProtocol::emptyInstance())
            return;

        exitGameState();
        m_rs_state.store(RS_ASYNC_RESET);
    }

    STKHost::get()->updatePlayers();
    if (m_rs_state.load() == RS_NONE &&
        (m_play_state.load() > WAITING_FOR_START_GAME/* ||
        m_game_setup->isGrandPrixStarted()*/) &&
        (STKHost::get()->getPlayersInGame() == 0 ||
        all_players_in_world_disconnected))
    {
        if (RaceEventManager::get() &&
            RaceEventManager::get()->isRunning())
        {
            // Send a notification to all players who may have start live join
            // or spectate to go back to lobby
            NetworkString* back_to_lobby = getNetworkString(2);
            back_to_lobby->setSynchronous(true);
            back_to_lobby->addUInt8(LE_BACK_LOBBY).addUInt8(BLR_NONE);
            Comm::sendMessageToPeersInServer(back_to_lobby, PRM_RELIABLE);
            delete back_to_lobby;

            RaceEventManager::get()->stop();
            RaceEventManager::get()->getProtocol()->requestTerminate();
            GameProtocol::lock()->requestTerminate();
        }
        else if (auto ai = m_ai_peer.lock())
        {
            // Reset AI peer for empty server, which will delete world
            NetworkString* back_to_lobby = getNetworkString(2);
            back_to_lobby->setSynchronous(true);
            back_to_lobby->addUInt8(LE_BACK_LOBBY).addUInt8(BLR_NONE);
            ai->sendPacket(back_to_lobby, PRM_RELIABLE);
            delete back_to_lobby;
        }
        if (all_players_in_world_disconnected)
            m_game_setup->cancelOneRace();
        resetVotingTime();
        // m_game_setup->cancelOneRace();
        //m_game_setup->stopGrandPrix();
        m_rs_state.store(RS_WAITING);
        return;
    }

    if (m_rs_state.load() != RS_NONE)
        return;

    // Reset for ranked server if in kart / track selection has only 1 player
    if (getSettings()->isRanked() &&
        m_play_state.load() == SELECTING &&
        STKHost::get()->getPlayersInGame() == 1)
    {
        NetworkString* back_lobby = getNetworkString(2);
        back_lobby->setSynchronous(true);
        back_lobby->addUInt8(LE_BACK_LOBBY)
            .addUInt8(BLR_ONE_PLAYER_IN_RANKED_MATCH);
        Comm::sendMessageToPeers(back_lobby, PRM_RELIABLE);
        delete back_lobby;
        resetVotingTime();
        // m_game_setup->cancelOneRace();
        //m_game_setup->stopGrandPrix();
        m_rs_state.store(RS_ASYNC_RESET);
    }

    handlePlayerDisconnection();

    switch (m_play_state.load())
    {
    case WAITING_FOR_START_GAME:
    case WAIT_FOR_WORLD_LOADED:
    case WAIT_FOR_RACE_STARTED:
    {
        // Waiting for asynchronousUpdate
        break;
    }
    case SELECTING:
        // The function playerTrackVote will trigger the next state
        // once all track votes have been received.
        break;
    case LOAD_WORLD:
        Log::info("ServerLobby", "Starting the race loading.");
        // This will create the world instance, i.e. load track and karts
        loadWorld();
        getGPManager()->updateWorldScoring();
        getSettings()->updateWorldSettings(m_game_info);
        m_state = WAIT_FOR_WORLD_LOADED;
        break;
    case RACING:
        if (World::getWorld() && RaceEventManager::get() &&
            RaceEventManager::get()->isRunning())
        {
            checkRaceFinished();
        }
        break;
    case WAIT_FOR_RACE_STOPPED:
        if (!RaceEventManager::get()->protocolStopped() ||
            !GameProtocol::emptyInstance())
            return;

        // This will go back to lobby in server (and exit the current race)
        exitGameState();
        // Reset for next state usage
        resetPeersReady();
        // Set the delay before the server forces all clients to exit the race
        // result screen and go back to the lobby
        setTimeoutFromNow(15);
        m_state = RESULT_DISPLAY;
        Comm::sendMessageToPeers(m_result_ns, PRM_RELIABLE);
        delete m_result_ns;
        Log::info("ServerLobby", "End of game message sent");
        break;
    case RESULT_DISPLAY:
        if (checkPeersReady(true/*ignore_ai_peer*/, AFTER_GAME) ||
            isTimeoutExpired())
        {
            // Send a notification to all clients to exit
            // the race result screen
            NetworkString* back_to_lobby = getNetworkString(2);
            back_to_lobby->setSynchronous(true);
            back_to_lobby->addUInt8(LE_BACK_LOBBY).addUInt8(BLR_NONE);
            Comm::sendMessageToPeersInServer(back_to_lobby, PRM_RELIABLE);
            delete back_to_lobby;
            m_rs_state.store(RS_ASYNC_RESET);
        }
        break;
    }
}   // updateRoom
//-----------------------------------------------------------------------------

[[deprecated("Ranking should not be handled by a single room.")]]
void PlayingRoom::asynchronousUpdateRoom()
{
    if (m_rs_state.load() == RS_ASYNC_RESET)
    {
        resetVotingTime();
        resetServer();
        m_rs_state.store(RS_NONE);
    }

    getChatManager()->clearAllExpiredWeakPtrs();

    // Check if server owner has left
    updateServerOwner();

    // Should be done in SL.
    if (getSettings()->isRanked() && m_play_state.load() == WAITING_FOR_START_GAME)
        m_ranking->cleanup();

    // Should be done in SL.
    if (!getSettings()->isLegacyGPMode() || m_play_state.load() == WAITING_FOR_START_GAME)
    {
        // Only poll the STK server if server has been registered.
        if (m_server_id_online.load() != 0 &&
                m_play_state.load() != REGISTER_SELF_ADDRESS)
            checkIncomingConnectionRequests();
        handlePendingConnection();
    }

    // Should be done in SL.
    if (m_server_id_online.load() != 0 &&
        !getSettings()->isLegacyGPMode() &&
        StkTime::getMonoTimeMs() > m_last_unsuccess_poll_time &&
        StkTime::getMonoTimeMs() > m_last_success_poll_time.load() + 30000)
    {
        Log::warn("ServerLobby", "Trying auto server recovery.");
        // For auto server recovery wait 3 seconds for next try
        m_last_unsuccess_poll_time = StkTime::getMonoTimeMs() + 3000;
        registerServer(false/*first_time*/);
    }

    switch (m_play_state.load())
    {
        case WAITING_FOR_START_GAME:
        {
            if (getCrownManager()->isOwnerLess())
            {
                // Ensure that a game can auto-start if the server meets the config's starting limit or if it's already full.
                int starting_limit = std::min((int)getSettings()->getMinStartGamePlayers(), (int)getSettings()->getServerMaxPlayers());
                unsigned current_max_players_in_game = getSettings()->getCurrentMaxPlayersInGame();
                if (current_max_players_in_game > 0) // 0 here means it's not the limit
                    starting_limit = std::min(starting_limit, (int)current_max_players_in_game);

                unsigned players = 0;
                STKHost::get()->updatePlayers(&players);
                if (((int)players >= starting_limit ||
                    getGameSetupFromCtx()->isGrandPrixStarted()) &&
                    isInfiniteTimeout())
                {
                    if (getSettings()->getStartGameCounter() >= -1e-5)
                    {
                        setTimeoutFromNow(getSettings()->getStartGameCounter());
                    }
                    else
                    {
                        setInfiniteTimeout();
                    }
                }
                else if ((int)players < starting_limit &&
                    !getGameSetupFromCtx()->isGrandPrixStarted())
                {
                    resetPeersReady();
                    if (!isInfiniteTimeout())
                        updatePlayerList();

                    setInfiniteTimeout();
                }
                bool forbid_starting = false;
                if (isTournament() && getTournament()->forbidStarting())
                    forbid_starting = true;
                
                bool timer_finished = (!forbid_starting && isTimeoutExpired());
                bool players_ready = (checkPeersReady(true/*ignore_ai_peer*/, BEFORE_SELECTION)
                        && (int)players >= starting_limit);

                if (timer_finished || (players_ready && !getSettings()->isCooldown()))
                {
                    resetPeersReady();
                    startSelection();
                    return;
                }
            }
            break;
        }
        case WAIT_FOR_WORLD_LOADED:
        {
            // For WAIT_FOR_WORLD_LOADED and SELECTING make sure there are enough
            // players to start next game, otherwise exiting and let main thread
            // reset

            // maybe this is not the best place for this?
            getHitProcessor()->resetLastHits();

            if (m_end_voting_period.load() == 0)
                return;

            unsigned player_in_game = 0;
            STKHost::get()->updatePlayers(&player_in_game);
            // Reset lobby will be done in main thread
            if ((player_in_game == 1 && getSettings()->isRanked()) ||
                player_in_game == 0)
            {
                resetVotingTime();
                return;
            }

            // m_server_has_loaded_world is set by main thread with atomic write
            if (m_server_has_loaded_world.load() == false)
                return;
            if (!checkPeersReady(
                getSettings()->hasAiHandling() && m_ai_count == 0/*ignore_ai_peer*/, LOADING_WORLD))
                return;
            // Reset for next state usage
            resetPeersReady();
            configPeersStartTime();
            break;
        }
        case SELECTING:
        {
            if (m_end_voting_period.load() == 0)
                return;
            unsigned player_in_game = 0;
            STKHost::get()->updatePlayers(&player_in_game);
            if ((player_in_game == 1 && getSettings()->isRanked()) ||
                player_in_game == 0)
            {
                resetVotingTime();
                return;
            }

            PeerVote winner_vote;
            getSettings()->resetWinnerPeerId();
            bool go_on_race = false;
            if (getSettings()->hasTrackVoting())
                go_on_race = handleAllVotes(&winner_vote);
            else if (/*m_game_setup->isGrandPrixStarted() || */isVotingOver())
            {
                winner_vote = getSettings()->getDefaultVote();
                go_on_race = true;
            }
            if (go_on_race)
            {
                getSettings()->applyRestrictionsOnWinnerVote(&winner_vote);
                getSettings()->setDefaultVote(winner_vote);
                m_item_seed = (uint32_t)StkTime::getTimeSinceEpoch();
                ItemManager::updateRandomSeed(m_item_seed);
                float extra_seconds = 0.0f;
                if (isTournament())
                    extra_seconds = getTournament()->getExtraSeconds();
                m_game_setup->setRace(winner_vote, extra_seconds);

                // For spectators that don't have the track, remember their
                // spectate mode and don't load the track
                std::string track_name = winner_vote.m_track_name;
                if (isTournament())
                    getTournament()->fillNextArena(track_name);
                
                auto peers = STKHost::get()->getPeers();
                std::map<std::shared_ptr<STKPeer>,
                        AlwaysSpectateMode> previous_spectate_mode;
                for (auto peer : peers)
                {
                    if (peer->alwaysSpectate() && (!peer->alwaysSpectateForReal() ||
                        peer->getClientAssets().second.count(track_name) == 0))
                    {
                        previous_spectate_mode[peer] = peer->getAlwaysSpectate();
                        peer->setAlwaysSpectate(ASM_NONE);
                        peer->setWaitingForGame(true);
                        m_peers_ready.erase(peer);
                    }
                }
                bool has_always_on_spectators = false;
                auto players = STKHost::get()
                    ->getPlayersForNewGame(&has_always_on_spectators);
                for (auto& p: previous_spectate_mode)
                    if (p.first)
                        p.first->setAlwaysSpectate(p.second);
                auto ai_instance = m_ai_peer.lock();
                if (supportsAI())
                {
                    if (ai_instance)
                    {
                        auto ai_profiles = ai_instance->getPlayerProfiles();
                        if (m_ai_count > 0)
                        {
                            ai_profiles.resize(m_ai_count);
                            players.insert(players.end(), ai_profiles.begin(),
                                ai_profiles.end());
                        }
                    }
                    else if (!m_ai_profiles.empty())
                    {
                        players.insert(players.end(), m_ai_profiles.begin(),
                            m_ai_profiles.end());
                    }
                }
                m_game_setup->sortPlayersForGrandPrix(players, getSettings()->isGPGridShuffled());
                m_game_setup->sortPlayersForGame(players);
                for (unsigned i = 0; i < players.size(); i++)
                {
                    std::shared_ptr<NetworkPlayerProfile>& player = players[i];
                    std::shared_ptr<STKPeer> peer = player->getPeer();
                    if (peer)
                        peer->clearAvailableKartIDs();
                }
                for (unsigned i = 0; i < players.size(); i++)
                {
                    std::shared_ptr<NetworkPlayerProfile>& player = players[i];
                    std::shared_ptr<STKPeer> peer = player->getPeer();
                    if (peer)
                        peer->addAvailableKartID(i);
                }
                getSettings()->getLobbyHitCaptureLimit();

                // Add placeholder players for live join
                addLiveJoinPlaceholder(players);
                // If player chose random / hasn't chose any kart
                for (unsigned i = 0; i < players.size(); i++)
                {
                    std::string current_kart = players[i]->getKartName();
                    if (!players[i]->getPeer().get())
                        continue;
                    if (getQueues()->areKartFiltersIgnoringKarts())
                        current_kart = "";
                    std::string name = StringUtils::wideToUtf8(players[i]->getName());
                    // Note 1: setKartName also resets KartData, and should be called
                    // only if current kart name is not suitable.
                    // Note 2: filters only support standard karts for now, so GKFBKC
                    // cannot return an addon; when addons are supported, this part of
                    // code will also have to provide kart data (or setKartName has to
                    // set the correct hitbox itself).
                    std::string new_kart = getAssetManager()->getKartForBadKartChoice(
                            players[i]->getPeer(), name, current_kart);
                    if (new_kart != current_kart)
                    {
                        // Filters only support standard karts for now, but when they
                        // start supporting addons, probably type should not be empty
                        players[i]->setKartName(new_kart);
                        KartData kart_data;
                        setKartDataProperly(kart_data, new_kart, players[i], "");
                        players[i]->setKartData(kart_data);
                    }
                }

                auto& stk_config = STKConfig::get();

                NetworkString* load_world_message = getLoadWorldMessage(players,
                    false/*live_join*/);
                m_game_setup->setHitCaptureTime(getSettings()->getBattleHitCaptureLimit(),
                    getSettings()->getBattleTimeLimit());
                uint16_t flag_return_time = (uint16_t)stk_config->time2Ticks(
                    getSettings()->getFlagReturnTimeout());
                RaceManager::get()->setHitProcessor(getHitProcessor());
                RaceManager::get()->setFlagReturnTicks(flag_return_time);
                if (getSettings()->isRecordingReplays() && getSettings()->hasConsentOnReplays() &&
                    (RaceManager::get()->getMinorMode() == RaceManager::MINOR_MODE_TIME_TRIAL ||
                    RaceManager::get()->getMinorMode() == RaceManager::MINOR_MODE_NORMAL_RACE))
                    RaceManager::get()->setRecordRace(true);
                uint16_t flag_deactivated_time = (uint16_t)STKConfig::get()->time2Ticks(
                    getSettings()->getFlagDeactivatedTime());
                RaceManager::get()->setFlagDeactivatedTicks(flag_deactivated_time);
                configRemoteKart(players, 0);

                // Reset for next state usage
                resetPeersReady();

                m_state = LOAD_WORLD;
                Comm::sendMessageToPeers(load_world_message);
                // updatePlayerList so the in lobby players (if any) can see always
                // spectators join the game
                if (has_always_on_spectators || !previous_spectate_mode.empty())
                    updatePlayerList();
                delete load_world_message;

                if (RaceManager::get()->getMinorMode() ==
                    RaceManager::MINOR_MODE_SOCCER)
                {
                    for (unsigned i = 0; i < RaceManager::get()->getNumPlayers(); i++)
                    {
                        if (auto player =
                            RaceManager::get()->getKartInfo(i).getNetworkPlayerProfile().lock())
                        {
                            std::string username = StringUtils::wideToUtf8(player->getName());
                            if (username.empty())
                                continue;
                            Log::info("ServerLobby", "SoccerMatchLog: There is a player %s.",
                                username.c_str());
                        }
                    }
                }
                m_game_info = std::make_shared<GameInfo>();
                m_game_info->setContext(m_lobby_context.get());
                m_game_info->fillFromRaceManager();
            }
            break;
        }
        default:
            break;
    }
}   // asynchronousUpdateRoom
//-----------------------------------------------------------------------------

/** Calls the corresponding method from LobbyAssetManager
 *  whenever server is reset or game mode is changed. */
[[deprecated("Asset managers should be separate for different rooms.")]]
void PlayingRoom::updateMapsForMode()
{
    getAssetManager()->updateMapsForMode(
        ServerConfig::getLocalGameMode(m_game_mode.load()).first
    );
}   // updateMapsForMode
//-----------------------------------------------------------------------------

NetworkString* PlayingRoom::getLoadWorldMessage(
    std::vector<std::shared_ptr<NetworkPlayerProfile> >& players,
    bool live_join) const
{
    NetworkString* load_world_message = getNetworkString();
    load_world_message->setSynchronous(true);
    load_world_message->addUInt8(LE_LOAD_WORLD);
    getSettings()->encodeDefaultVote(load_world_message);
    load_world_message->addUInt8(live_join ? 1 : 0);
    encodePlayers(load_world_message, players, getLobby()->getNameDecorator());
    load_world_message->addUInt32(m_item_seed);
    if (RaceManager::get()->isBattleMode())
    {
        auto& stk_config = STKConfig::get();

        load_world_message->addUInt32(getSettings()->getBattleHitCaptureLimit())
            .addFloat(getSettings()->getBattleTimeLimit());
        uint16_t flag_return_time = (uint16_t)stk_config->time2Ticks(
            getSettings()->getFlagReturnTimeout());
        load_world_message->addUInt16(flag_return_time);
        uint16_t flag_deactivated_time = (uint16_t)stk_config->time2Ticks(
            getSettings()->getFlagDeactivatedTime());
        load_world_message->addUInt16(flag_deactivated_time);
    }
    for (unsigned i = 0; i < players.size(); i++)
        players[i]->getKartData().encode(load_world_message);
    return load_world_message;
}   // getLoadWorldMessage

//-----------------------------------------------------------------------------
/** Returns true if server can be live joined or spectating
 */
bool PlayingRoom::canLiveJoinNow() const
{
    bool live_join = getSettings()->isLivePlayers() && worldIsActive();
    if (!live_join)
        return false;
    if (RaceManager::get()->modeHasLaps())
    {
        // No spectate when fastest kart is nearly finish, because if there
        // is endcontroller the spectating remote may not be knowing this
        // on time
        LinearWorld* w = dynamic_cast<LinearWorld*>(World::getWorld());
        if (!w)
            return false;
        AbstractKart* fastest_kart = NULL;
        for (unsigned i = 0; i < w->getNumKarts(); i++)
        {
            fastest_kart = w->getKartAtPosition(i + 1);
            if (fastest_kart && !fastest_kart->isEliminated())
                break;
        }
        if (!fastest_kart)
            return false;
        float leader_distance = w->getOverallDistance(
            fastest_kart->getWorldKartId());
        float total_distance =
            Track::getCurrentTrack()->getTrackLength() *
            (float)RaceManager::get()->getNumLaps();
        // standard version uses (leader_distance / total_distance > 0.9f)
        // TODO: allow switching
        if (total_distance - leader_distance < 250.0)
            return false;
    }
    return live_join;
}   // canLiveJoinNow

//-----------------------------------------------------------------------------
/** \ref STKPeer peer will be reset back to the lobby with reason
 *  \ref BackLobbyReason blr
 */
void PlayingRoom::rejectLiveJoin(std::shared_ptr<STKPeer> peer, BackLobbyReason blr)
{
    NetworkString* reset = getNetworkString(2);
    reset->setSynchronous(true);
    reset->addUInt8(LE_BACK_LOBBY).addUInt8(blr);
    peer->sendPacket(reset, PRM_RELIABLE);
    delete reset;

    getLobby()->updatePlayerList();

    NetworkString* server_info = getNetworkString();
    server_info->setSynchronous(true);
    server_info->addUInt8(LE_SERVER_INFO);
    getGameSetupFromCtx()->addServerInfo(server_info);
    peer->sendPacket(server_info, PRM_RELIABLE);
    delete server_info;

    peer->updateLastActivity();
}   // rejectLiveJoin

//-----------------------------------------------------------------------------
/** This message is like kartSelectionRequested, but it will send the peer
 *  load world message if he can join the current started game.
 */
void PlayingRoom::liveJoinRequest(Event* event)
{
    // I moved some data getters ahead of some returns. This should be fine
    // in general, but you know what caused it if smth goes wrong.

    std::shared_ptr<STKPeer> peer = event->getPeerSP();
    const NetworkString& data = event->data();
    bool spectator = data.getUInt8() == 1;

    if (!canLiveJoinNow())
    {
        rejectLiveJoin(peer, BLR_NO_GAME_FOR_LIVE_JOIN);
        return;
    }
    if (RaceManager::get()->modeHasLaps() && !spectator)
    {
        // No live join for linear race
        rejectLiveJoin(peer, BLR_NO_GAME_FOR_LIVE_JOIN);
        return;
    }

    peer->clearAvailableKartIDs();
    if (!spectator)
    {
        auto& spectators_by_limit = getCrownManager()->getSpectatorsByLimit();
        setPlayerKarts(data, peer);

        std::vector<int> used_id;
        for (unsigned i = 0; i < peer->getPlayerProfiles().size(); i++)
        {
            int id = getReservedId(peer->getPlayerProfiles()[i], i);
            if (id == -1)
                break;
            used_id.push_back(id);
        }
        if ((used_id.size() != peer->getPlayerProfiles().size()) ||
            (spectators_by_limit.find(peer) != spectators_by_limit.end()))
        {
            for (unsigned i = 0; i < peer->getPlayerProfiles().size(); i++)
                peer->getPlayerProfiles()[i]->setKartName("");
            for (unsigned i = 0; i < used_id.size(); i++)
            {
                RemoteKartInfo& rki = RaceManager::get()->getKartInfo(used_id[i]);
                rki.makeReserved();
            }
            Log::info("ServerLobby", "Too many players (%d) try to live join",
                (int)peer->getPlayerProfiles().size());
            rejectLiveJoin(peer, BLR_NO_PLACE_FOR_LIVE_JOIN);
            return;
        }

        for (int id : used_id)
        {
            Log::info("ServerLobby", "%s live joining with reserved kart id %d.",
                peer->getAddress().toString().c_str(), id);
            peer->addAvailableKartID(id);
        }
    }
    else
    {
        Log::info("ServerLobby", "%s spectating now.",
            peer->getAddress().toString().c_str());
    }

    std::vector<std::shared_ptr<NetworkPlayerProfile> > players =
        getLivePlayers();
    NetworkString* load_world_message = getLoadWorldMessage(players,
        true/*live_join*/);
    peer->sendPacket(load_world_message, PRM_RELIABLE);
    delete load_world_message;
    peer->updateLastActivity();
}   // liveJoinRequest

//-----------------------------------------------------------------------------
/** Finally put the kart in the world and inform client the current world
 *  status, (including current confirmed item state, kart scores...)
 */
void PlayingRoom::finishedLoadingLiveJoinClient(Event* event)
{
    std::shared_ptr<STKPeer> peer = event->getPeerSP();
    if (!canLiveJoinNow())
    {
        rejectLiveJoin(peer, BLR_NO_GAME_FOR_LIVE_JOIN);
        return;
    }
    bool live_joined_in_time = true;
    for (const int id : peer->getAvailableKartIDs())
    {
        const RemoteKartInfo& rki = RaceManager::get()->getKartInfo(id);
        if (rki.isReserved())
        {
            live_joined_in_time = false;
            break;
        }
    }
    if (!live_joined_in_time)
    {
        Log::warn("ServerLobby", "%s can't live-join in time.",
            peer->getAddress().toString().c_str());
        rejectLiveJoin(peer, BLR_NO_GAME_FOR_LIVE_JOIN);
        return;
    }
    World* w = World::getWorld();
    assert(w);

    uint64_t live_join_start_time = STKHost::get()->getNetworkTimer();

    auto& stk_config = STKConfig::get();

    // Instead of using getTicksSinceStart we caculate the current world ticks
    // only from network timer, because if the server hangs in between the
    // world ticks may not be up to date
    // 2000 is the time for ready set, remove 3 ticks after for minor
    // correction (make it more looks like getTicksSinceStart if server has no
    // hang
    int cur_world_ticks = stk_config->time2Ticks(
        (live_join_start_time - m_server_started_at - 2000) / 1000.f) - 3;
    // Give 3 seconds for all peers to get new kart info
    m_last_live_join_util_ticks =
        cur_world_ticks + stk_config->time2Ticks(3.0f);
    live_join_start_time -= m_server_delay;
    live_join_start_time += 3000;

    bool spectator = false;
    for (const int id : peer->getAvailableKartIDs())
    {
        const RemoteKartInfo& rki = RaceManager::get()->getKartInfo(id);
        int points = 0;

        if (m_game_info)
            points = m_game_info->onLiveJoinedPlayer(id, rki, w);
        else
            Log::warn("ServerLobby", "GameInfo is not accessible??");

        // If the mode is not battle/CTF, points are 0.
        // I assume it's fine like that for now
        World::getWorld()->addReservedKart(id, points);
        addLiveJoiningKart(id, rki, m_last_live_join_util_ticks);
        Log::info("ServerLobby", "%s succeeded live-joining with kart id %d.",
            peer->getAddress().toString().c_str(), id);
    }
    if (peer->getAvailableKartIDs().empty())
    {
        Log::info("ServerLobby", "%s spectating succeeded.",
            peer->getAddress().toString().c_str());
        spectator = true;
    }

    const uint8_t cc = (uint8_t)Track::getCurrentTrack()->getCheckManager()->getCheckStructureCount();
    NetworkString* ns = getNetworkString(10);
    ns->setSynchronous(true);
    ns->addUInt8(LE_LIVE_JOIN_ACK).addUInt64(m_client_starting_time)
        .addUInt8(cc).addUInt64(live_join_start_time)
        .addUInt32(m_last_live_join_util_ticks);

    NetworkItemManager* nim = dynamic_cast<NetworkItemManager*>
        (Track::getCurrentTrack()->getItemManager());
    assert(nim);
    nim->saveCompleteState(ns);
    nim->addLiveJoinPeer(peer);

    w->saveCompleteState(ns, peer);
    if (RaceManager::get()->supportsLiveJoining())
    {
        // Only needed in non-racing mode as no need players can added after
        // starting of race
        std::vector<std::shared_ptr<NetworkPlayerProfile> > players =
            getLivePlayers();
        encodePlayers(ns, players, getLobby()->getNameDecorator());
        for (unsigned i = 0; i < players.size(); i++)
            players[i]->getKartData().encode(ns);
    }

    m_peers_ready[peer] = false;
    peer->setWaitingForGame(false);
    peer->setSpectator(spectator);

    peer->sendPacket(ns, PRM_RELIABLE);
    delete ns;
    getLobby()->updatePlayerList();
    peer->updateLastActivity();
}   // finishedLoadingLiveJoinClient

//-----------------------------------------------------------------------------
/** Instructs all clients to start the kart selection. If event is NULL,
 *  the command comes from the owner less server.
 */
void PlayingRoom::startSelection(const Event *event)
{
    bool need_to_update = false;
    bool cooldown = getSettings()->isCooldown();
    
    if (event != NULL)
    {
        if (m_play_state.load() != WAITING_FOR_START_GAME)
        {
            Log::warn("ServerLobby",
                "Received startSelection while being in state %d.",
                m_play_state.load());
            return;
        }
        if (getCrownManager()->isSleepingServer())
        {
            Log::warn("ServerLobby",
                "An attempt to start a race on a sleeping server.");
            return;
        }
        auto peer = event->getPeerSP();
        if (getCrownManager()->isOwnerLess())
        {
            if (!getSettings()->isAllowedToStart())
            {
                Comm::sendStringToPeer(peer, "Starting the game is forbidden by server owner");
                return;
            }
            if (!getCrownManager()->canRace(peer))
            {
                Comm::sendStringToPeer(peer, "You cannot play so pressing ready has no action");
                return;
            }
            else
            {
                m_peers_ready.at(event->getPeerSP()) =
                    !m_peers_ready.at(event->getPeerSP());
                getLobby()->updatePlayerList();
                return;
            }
        }
        if (!getSettings()->isAllowedToStart())
        {
            Comm::sendStringToPeer(peer, "Starting the game is forbidden by server owner");
            return;
        }
        if (!hasHostRights(peer))
        {
            auto argv = getCommandManager()->getCurrentArgv();
            if (argv.empty() || argv[0] != "start") {
                Log::warn("ServerLobby",
                          "Client %d is not authorised to start selection.",
                          event->getPeer()->getHostId());
                return;
            }
        }
        if (cooldown)
        {
            Comm::sendStringToPeer(peer, "Starting the game is forbidden by server cooldown");
            return;
        }
    } else {
        // Even if cooldown is bigger than server timeout, start happens upon
        // timeout expiration. If all players clicked before timeout, no start
        // happens - it's handled in another place
        if (!getSettings()->isAllowedToStart())
        {
            // Produce no log spam
            return;
        }
    }

    if (!getCrownManager()->isOwnerLess() && getSettings()->hasTeamChoosing() &&
        !getSettings()->hasFreeTeams() && RaceManager::get()->teamEnabled())
    {
        auto red_blue = STKHost::get()->getAllPlayersTeamInfo();
        if ((red_blue.first == 0 || red_blue.second == 0) &&
            red_blue.first + red_blue.second != 1)
        {
            Log::warn("ServerLobby", "Bad team choosing.");
            if (event)
            {
                NetworkString* bt = getNetworkString();
                bt->setSynchronous(true);
                bt->addUInt8(LE_BAD_TEAM);
                event->getPeer()->sendPacket(bt, PRM_RELIABLE);
                delete bt;
            }
            return;
        }
    }

    unsigned max_player = 0;
    STKHost::get()->updatePlayers(&max_player);
    auto peers = STKHost::get()->getPeers();
    std::set<std::shared_ptr<STKPeer>> always_spectate_peers;

    // Set late coming player to spectate if too many players
    auto& spectators_by_limit = getCrownManager()->getSpectatorsByLimit();
    if (spectators_by_limit.size() == peers.size())
    {
        // produce no log spam for now
        // Log::error("ServerLobby", "Too many players and cannot set "
        //     "spectate for late coming players!");
        return;
    }
    for (auto &peer : spectators_by_limit)
    {
        peer->setAlwaysSpectate(ASM_FULL);
        peer->setWaitingForGame(true);
        always_spectate_peers.insert(peer);
    }

    // Remove karts / tracks from server that are not supported on all clients
    std::vector<std::shared_ptr<STKPeer>> erasingPeers;
    bool has_peer_plays_game = false;
    for (auto peer : peers)
    {
        if (!peer->isValidated() || peer->isWaitingForGame())
            continue;
        bool can_race = getCrownManager()->canRace(peer);
        if (!can_race && !peer->alwaysSpectate())
        {
            peer->setAlwaysSpectate(ASM_FULL);
            peer->setWaitingForGame(true);
            m_peers_ready.erase(peer);
            need_to_update = true;
            always_spectate_peers.insert(peer);
            continue;
        }
        else if (peer->alwaysSpectate())
        {
            always_spectate_peers.insert(peer);
            continue;
        }
        // I might introduce an extra use for a peer that leaves at the same moment. Please investigate later.
        erasingPeers.push_back(peer);
        if (!peer->isAIPeer())
            has_peer_plays_game = true;
    }

    // kimden thinks if someone wants to race he should disable spectating
    // // Disable always spectate peers if no players join the game
    if (!has_peer_plays_game)
    {
        if (event)
        {
            // inside if to not produce log spam for ownerless
            Log::warn("ServerLobby",
                "An attempt to start a game while no one can play.");
            Comm::sendStringToPeer(event->getPeerSP(), "No one can play!");
        }
        addWaitingPlayersToGame();
        return;
        // for (std::shared_ptr<STKPeer> peer : always_spectate_peers)
        //     peer->setAlwaysSpectate(ASM_NONE);
        // always_spectate_peers.clear();
    }
    else
    {
        // We make those always spectate peer waiting for game so it won't
        // be able to vote, this will be reset in STKHost::getPlayersForNewGame
        // This will also allow a correct number of in game players for max
        // arena players handling
        for (std::shared_ptr<STKPeer> peer : always_spectate_peers)
            peer->setWaitingForGame(true);
    }

    getAssetManager()->eraseAssetsWithPeers(erasingPeers);

    max_player = 0;
    STKHost::get()->updatePlayers(&max_player);
    if (auto ai = m_ai_peer.lock())
    {
        if (supportsAI())
        {
            unsigned total_ai_available =
                (unsigned)ai->getPlayerProfiles().size();
            m_ai_count = max_player > total_ai_available ?
                0 : total_ai_available - max_player + 1;
            // Disable ai peer for this game
            if (m_ai_count == 0)
                ai->setValidated(false);
            else
                ai->setValidated(true);
        }
        else
        {
            ai->setValidated(false);
            m_ai_count = 0;
        }
    }
    else
        m_ai_count = 0;

    if (!getAssetManager()->tryApplyingMapFilters())
    {
        Log::error("ServerLobby", "No tracks for playing!");
        return;
    }

    getSettings()->initializeDefaultVote();

    getLobby()->unregisterServerForLegacyGPMode();

    startVotingPeriod(getSettings()->getVotingTimeout());

    peers = STKHost::get()->getPeers();
    for (auto& peer: peers)
    {
        if (peer->isDisconnected() && !peer->isValidated())
            continue;
        if (!getCrownManager()->canRace(peer) || peer->isWaitingForGame())
            continue; // they are handled below

        NetworkString *ns = getNetworkString(1);
        // Start selection - must be synchronous since the receiver pushes
        // a new screen, which must be done from the main thread.
        ns->setSynchronous(true);
        ns->addUInt8(LE_START_SELECTION)
           .addFloat(getSettings()->getVotingTimeout())
           .addUInt8(/*getGameSetupFromCtx()->isGrandPrixStarted() ? 1 : */0)
           .addUInt8((!getSettings()->hasNoLapRestrictions() ? 1 : 0))
           .addUInt8(getSettings()->hasTrackVoting() ? 1 : 0);


        std::set<std::string> all_k = peer->getClientAssets().first;
        std::string username = peer->getMainName();
        // std::string username = StringUtils::wideToUtf8(profile->getName());
        getAssetManager()->applyAllKartFilters(username, all_k);

        if (!getKartElimination()->getRemainingParticipants().empty() && getKartElimination()->getRemainingParticipants().count(username) == 0)
        {
            if (all_k.count(getKartElimination()->getKart()))
                all_k = {getKartElimination()->getKart()};
            else
                all_k = {};
        }

        getAssetManager()->encodePlayerKartsAndCommonMaps(ns, all_k);

        peer->sendPacket(ns, PRM_RELIABLE);
        delete ns;

        if (getQueues()->areKartFiltersIgnoringKarts())
            Comm::sendStringToPeer(peer, "The server will ignore your kart choice");
    }

    m_play_state = SELECTING;
    if (need_to_update || !always_spectate_peers.empty())
    {
        NetworkString* back_lobby = getNetworkString(2);
        back_lobby->setSynchronous(true);
        back_lobby->addUInt8(LE_BACK_LOBBY).addUInt8(BLR_SPECTATING_NEXT_GAME);
        STKHost::get()->sendPacketToAllPeersWith(
            [always_spectate_peers](std::shared_ptr<STKPeer> peer)
            {
                return always_spectate_peers.find(peer) !=
                always_spectate_peers.end();
            }, back_lobby, PRM_RELIABLE);
        delete back_lobby;
        getLobby()->updatePlayerList();
    }

    getLobby()->dropPendingConnectionsForLegacyGPMode();

    // Will be changed after the first vote received
    setInfiniteTimeout();

    getGPManager()->onStartSelection();

    getCommandManager()->onStartSelection();
}   // startSelection

//-----------------------------------------------------------------------------
/*! \brief Called when a player votes for track(s), it will auto correct client
 *         data if it sends some invalid data.
 *  \param event : Event providing the information.
 */
void PlayingRoom::handlePlayerVote(Event* event)
{
    if (m_play_state != SELECTING || !getSettings()->hasTrackVoting())
    {
        Log::warn("ServerLobby", "Received track vote while in state %d.",
                  m_play_state.load());
        return;
    }

    if (!checkDataSize(event, 4) ||
        event->getPeer()->getPlayerProfiles().empty() ||
        event->getPeer()->isWaitingForGame())
        return;

    if (isVotingOver())  return;

    if (!canVote(event->getPeerSP())) return;

    NetworkString& data = event->data();
    PeerVote vote(data);
    Log::debug("ServerLobby",
        "Vote from client: host %d, track %s, laps %d, reverse %d.",
        event->getPeer()->getHostId(), vote.m_track_name.c_str(),
        vote.m_num_laps, vote.m_reverse);

    Track* t = TrackManager::get()->getTrack(vote.m_track_name);
    if (!t)
    {
        vote.m_track_name = getAssetManager()->getAnyMapForVote();
        t = TrackManager::get()->getTrack(vote.m_track_name);
        assert(t);
    }

    // Remove / adjust any invalid settings
    if (isTournament())
    {
        getTournament()->applyRestrictionsOnVote(&vote);
    }
    else
    {
        getSettings()->applyRestrictionsOnVote(&vote, t);
    }

    // Store vote:
    vote.m_player_name = event->getPeer()->getMainProfile()->getName();
    addVote(event->getPeer()->getHostId(), vote);

    // After adding the vote, show decorated name instead
    vote.m_player_name = event->getPeer()->getMainProfile()->getDecoratedName(getLobby()->getNameDecorator());

    // Now inform all clients about the vote
    NetworkString other = NetworkString(PROTOCOL_LOBBY_ROOM);
    other.setSynchronous(true);
    other.addUInt8(LE_VOTE);
    other.addUInt32(event->getPeer()->getHostId());
    vote.encode(&other);
    Comm::sendMessageToPeers(&other);

}   // handlePlayerVote

//-----------------------------------------------------------------------------
/** Called when a client notifies the server that it has loaded the world.
 *  When all clients and the server are ready, the race can be started.
 */
void PlayingRoom::finishedLoadingWorldClient(Event *event)
{
    std::shared_ptr<STKPeer> peer = event->getPeerSP();
    peer->updateLastActivity();
    m_peers_ready.at(peer) = true;
    Log::info("ServerLobby", "Peer %d has finished loading world at %lf",
        peer->getHostId(), StkTime::getRealTime());
}   // finishedLoadingWorldClient

//-----------------------------------------------------------------------------
/** Called when a client clicks on 'ok' on the race result screen.
 *  If all players have clicked on 'ok', go back to the lobby.
 */
void PlayingRoom::playerFinishedResult(Event *event)
{
    if (m_rs_state.load() == RS_ASYNC_RESET ||
        m_play_state.load() != RESULT_DISPLAY)
        return;
    std::shared_ptr<STKPeer> peer = event->getPeerSP();
    m_peers_ready.at(peer) = true;
}   // playerFinishedResult

//-----------------------------------------------------------------------------
/** Tell the client \ref RemoteKartInfo of a player when some player joining
 *  live.
 */
void PlayingRoom::handleKartInfo(Event* event)
{
    World* w = World::getWorld();
    if (!w)
        return;

    std::shared_ptr<STKPeer> peer = event->getPeerSP();
    const NetworkString& data = event->data();
    uint8_t kart_id = data.getUInt8();
    if (kart_id > RaceManager::get()->getNumPlayers())
        return;

    AbstractKart* k = w->getKart(kart_id);
    int live_join_util_ticks = k->getLiveJoinUntilTicks();

    const RemoteKartInfo& rki = RaceManager::get()->getKartInfo(kart_id);

    NetworkString* ns = getNetworkString(1);
    ns->setSynchronous(true);
    ns->addUInt8(LE_KART_INFO).addUInt32(live_join_util_ticks)
        .addUInt8(kart_id) .encodeString(rki.getPlayerName())
        .addUInt32(rki.getHostId()).addFloat(rki.getDefaultKartColor())
        .addUInt32(rki.getOnlineId()).addUInt8(rki.getHandicap())
        .addUInt8((uint8_t)rki.getLocalPlayerId())
        .encodeString(rki.getKartName()).encodeString(rki.getCountryCode());
    if (peer->getClientCapabilities().find("real_addon_karts") !=
        peer->getClientCapabilities().end())
        rki.getKartData().encode(ns);
    peer->sendPacket(ns, PRM_RELIABLE);
    delete ns;

    FreeForAll* ffa_world = dynamic_cast<FreeForAll*>(World::getWorld());
    if (ffa_world)
        ffa_world->notifyAboutScoreIfNonzero(kart_id);
}   // handleKartInfo

//-----------------------------------------------------------------------------
/** Client if currently in-game (including spectator) wants to go back to
 *  lobby.
 */
void PlayingRoom::clientInGameWantsToBackLobby(Event* event)
{
    World* w = World::getWorld();
    std::shared_ptr<STKPeer> peer = event->getPeerSP();

    if (!w || !worldIsActive() || peer->isWaitingForGame())
    {
        Log::warn("ServerLobby", "%s try to leave the game at wrong time.",
            peer->getAddress().toString().c_str());
        return;
    }

    if (getLobby()->isChildClientServerHost(event->getPeer()))
    {
        // For child server the remaining client cannot go on player when the
        // server owner quited the game (because the world will be deleted), so
        // we reset all players
        auto pm = ProtocolManager::lock();
        if (RaceEventManager::get())
        {
            RaceEventManager::get()->stop();
            pm->findAndTerminate(PROTOCOL_GAME_EVENTS);
        }
        auto gp = GameProtocol::lock();
        if (gp)
        {
            auto lock = gp->acquireWorldDeletingMutex();
            pm->findAndTerminate(PROTOCOL_CONTROLLER_EVENTS);
            exitGameState();
        }
        else
            exitGameState();
        NetworkString* back_to_lobby = getNetworkString(2);
        back_to_lobby->setSynchronous(true);
        back_to_lobby->addUInt8(LE_BACK_LOBBY)
            .addUInt8(BLR_SERVER_ONWER_QUITED_THE_GAME);
        Comm::sendMessageToPeersInServer(back_to_lobby, PRM_RELIABLE);
        delete back_to_lobby;
        m_rs_state.store(RS_ASYNC_RESET);
        return;
    }

    if (m_game_info)
        m_game_info->saveDisconnectingPeerInfo(peer);
    else
        Log::warn("ServerLobby", "GameInfo is not accessible??");

    for (const int id : peer->getAvailableKartIDs())
    {
        RemoteKartInfo& rki = RaceManager::get()->getKartInfo(id);
        if (rki.getHostId() == peer->getHostId())
        {
            Log::info("ServerLobby", "%s left the game with kart id %d.",
                peer->getAddress().toString().c_str(), id);
            rki.setNetworkPlayerProfile(
                std::shared_ptr<NetworkPlayerProfile>());
        }
        else
        {
            Log::error("ServerLobby", "%s doesn't exist anymore in server.",
                peer->getAddress().toString().c_str());
        }
    }
    NetworkItemManager* nim = dynamic_cast<NetworkItemManager*>
        (Track::getCurrentTrack()->getItemManager());
    assert(nim);
    nim->erasePeerInGame(peer);
    m_peers_ready.erase(peer);
    peer->setWaitingForGame(true);
    peer->setSpectator(false);

    NetworkString* reset = getNetworkString(2);
    reset->setSynchronous(true);
    reset->addUInt8(LE_BACK_LOBBY).addUInt8(BLR_NONE);
    peer->sendPacket(reset, PRM_RELIABLE);
    delete reset;
    getLobby()->updatePlayerList();
    NetworkString* server_info = getNetworkString();
    server_info->setSynchronous(true);
    server_info->addUInt8(LE_SERVER_INFO);
    getGameSetupFromCtx()->addServerInfo(server_info);
    peer->sendPacket(server_info, PRM_RELIABLE);
    delete server_info;

    peer->updateLastActivity();
}   // clientInGameWantsToBackLobby

//-----------------------------------------------------------------------------
/** Client if currently select assets wants to go back to lobby.
 */
void PlayingRoom::clientSelectingAssetsWantsToBackLobby(Event* event)
{
    std::shared_ptr<STKPeer> peer = event->getPeerSP();

    if (m_play_state.load() != SELECTING || peer->isWaitingForGame())
    {
        Log::warn("ServerLobby",
            "%s try to leave selecting assets at wrong time.",
            peer->getAddress().toString().c_str());
        return;
    }

    if (getLobby()->isChildClientServerHost(event->getPeerSP()))
    {
        NetworkString* back_to_lobby = getNetworkString(2);
        back_to_lobby->setSynchronous(true);
        back_to_lobby->addUInt8(LE_BACK_LOBBY)
            .addUInt8(BLR_SERVER_ONWER_QUITED_THE_GAME);
        Comm::sendMessageToPeersInServer(back_to_lobby, PRM_RELIABLE);
        delete back_to_lobby;
        resetVotingTime();
        resetServer();
        m_rs_state.store(RS_NONE);
        return;
    }

    m_peers_ready.erase(peer);
    peer->setWaitingForGame(true);
    peer->setSpectator(false);

    NetworkString* reset = getNetworkString(2);
    reset->setSynchronous(true);
    reset->addUInt8(LE_BACK_LOBBY).addUInt8(BLR_NONE);
    peer->sendPacket(reset, PRM_RELIABLE);
    delete reset;
    getLobby()->updatePlayerList();
    NetworkString* server_info = getNetworkString();
    server_info->setSynchronous(true);
    server_info->addUInt8(LE_SERVER_INFO);
    getGameSetupFromCtx()->addServerInfo(server_info);
    peer->sendPacket(server_info, PRM_RELIABLE);
    delete server_info;

    peer->updateLastActivity();
}   // clientSelectingAssetsWantsToBackLobby

//-----------------------------------------------------------------------------
void PlayingRoom::saveInitialItems(std::shared_ptr<NetworkItemManager> nim)
{
    m_items_complete_state->getBuffer().clear();
    m_items_complete_state->reset();
    nim->saveCompleteState(m_items_complete_state);
}   // saveInitialItems

//-----------------------------------------------------------------------------
bool PlayingRoom::supportsAI()
{
    return getGameMode() == 3 || getGameMode() == 4;
}   // supportsAI

//-----------------------------------------------------------------------------
bool PlayingRoom::checkPeersReady(bool ignore_ai_peer, SelectionPhase phase)
{
    bool all_ready = true;
    bool someone_races = false;
    for (auto p : m_peers_ready)
    {
        auto peer = p.first.lock();
        if (!peer)
            continue;
        if (phase == BEFORE_SELECTION && peer->alwaysSpectate())
            continue;
        if (phase == AFTER_GAME && peer->isSpectator())
            continue;
        if (ignore_ai_peer && peer->isAIPeer())
            continue;
        if (phase == BEFORE_SELECTION && !getCrownManager()->canRace(peer))
            continue;
        someone_races = true;
        all_ready = all_ready && p.second;
        if (!all_ready)
            return false;
    }
    return someone_races;
}   // checkPeersReady

//-----------------------------------------------------------------------------

void PlayingRoom::resetToDefaultSettings()
{
    if (getSettings()->isServerConfigurable() && !getSettings()->isPreservingMode())
    {
        if (m_play_state == WAITING_FOR_START_GAME)
            handleServerConfiguration(NULL);
        else
            m_reset_to_default_mode_later.store(true);
    }

    getSettings()->onResetToDefaultSettings();
}  // resetToDefaultSettings
//-----------------------------------------------------------------------------

bool PlayingRoom::canVote(std::shared_ptr<STKPeer> peer) const
{
    if (!peer || peer->getPlayerProfiles().empty())
        return false;

    if (!isTournament())
        return true;

    return getTournament()->canVote(peer);
}   // canVote
//-----------------------------------------------------------------------------

bool PlayingRoom::hasHostRights(std::shared_ptr<STKPeer> peer) const
{
    if (!peer || peer->getPlayerProfiles().empty())
        return false;

    if (peer == m_server_owner.lock())
        return true;

    if (peer->hammerLevel() > 0)
        return true;

    if (isTournament())
        return getTournament()->hasHostRights(peer);

    return false;
}   // hasHostRights
//-----------------------------------------------------------------------------

int PlayingRoom::getPermissions(std::shared_ptr<STKPeer> peer) const
{
    int mask = 0;
    if (!peer)
        return mask;
    bool isSpectator = (peer->alwaysSpectate());
    if (isSpectator)
    {
        mask |= CommandPermissions::PE_SPECTATOR;
        mask |= CommandPermissions::PE_VOTED_SPECTATOR;
    }
    else
    {
        mask |= CommandPermissions::PE_USUAL;
        mask |= CommandPermissions::PE_VOTED_NORMAL;
    }
    if (peer == m_server_owner.lock())
    {
        mask |= CommandPermissions::PE_CROWNED;
        if (getCrownManager()->hasOnlyHostRiding())
            mask |= CommandPermissions::PE_SINGLE;
    }
    int hammer_level = peer->hammerLevel();
    if (hammer_level >= 1)
    {
        mask |= CommandPermissions::PE_HAMMER;
        if (hammer_level >= 2)
            mask |= CommandPermissions::PE_MANIPULATOR;
    }
    else if (getTournament() && getTournament()->hasHammerRights(peer))
    {
        mask |= CommandPermissions::PE_HAMMER;
    }
    return mask;
}   // getPermissions
//-----------------------------------------------------------------------------

int PlayingRoom::getCurrentStateScope()
{
    auto state = m_play_state.load();

    if (state < WAITING_FOR_START_GAME || state > RESULT_DISPLAY)
        return 0;

    if (state == WAITING_FOR_START_GAME)
        return CommandManager::StateScope::SS_LOBBY;

    return CommandManager::StateScope::SS_INGAME;
}   // getCurrentStateScope
//-----------------------------------------------------------------------------
/*! \brief Called when a player asks to select karts.
 *  \param event : Event providing the information.
 */
void PlayingRoom::kartSelectionRequested(Event* event)
{
    if (m_play_state != SELECTING /*|| m_game_setup->isGrandPrixStarted()*/)
    {
        Log::warn("PlayingRoom", "Received kart selection while in state %d.",
                  m_play_state.load());
        return;
    }

    if (!checkDataSize(event, 1) ||
        event->getPeer()->getPlayerProfiles().empty())
        return;

    const NetworkString& data = event->data();
    std::shared_ptr<STKPeer> peer = event->getPeerSP();
    setPlayerKarts(data, peer);
}   // kartSelectionRequested

//-----------------------------------------------------------------------------
void PlayingRoom::setPlayerKarts(const NetworkString& ns, std::shared_ptr<STKPeer> peer) const
{
    unsigned player_count = ns.getUInt8();
    player_count = std::min(player_count, (unsigned)peer->getPlayerProfiles().size());
    for (unsigned i = 0; i < player_count; i++)
    {
        std::string kart;
        ns.decodeString(&kart);
        std::string username = StringUtils::wideToUtf8(
            peer->getPlayerProfiles()[i]->getName());
        if (getKartElimination()->isEliminated(username))
        {
            peer->getPlayerProfiles()[i]->setKartName(getKartElimination()->getKart());
            continue;
        }
        std::string current_kart = kart;
        if (kart.find("randomkart") != std::string::npos ||
                (kart.find("addon_") == std::string::npos &&
                !getAssetManager()->isKartAvailable(kart)))
        {
            current_kart = "";
        }
        if (getQueues()->areKartFiltersIgnoringKarts())
            current_kart = "";
        std::string name = StringUtils::wideToUtf8(peer->getPlayerProfiles()[i]->getName());
        peer->getPlayerProfiles()[i]->setKartName(
                getAssetManager()->getKartForBadKartChoice(peer, name, current_kart));
    }
    if (peer->getClientCapabilities().find("real_addon_karts") ==
        peer->getClientCapabilities().end() || ns.size() == 0)
        return;
    for (unsigned i = 0; i < player_count; i++)
    {
        KartData kart_data(ns);
        std::string type = kart_data.m_kart_type;
        auto& player = peer->getPlayerProfiles()[i];
        const std::string& kart_id = player->getKartName();
        setKartDataProperly(kart_data, kart_id, player, type);
    }
}   // setPlayerKarts

//-----------------------------------------------------------------------------
/** Decide where to put the live join player depends on his team and game mode.
 */
int PlayingRoom::getReservedId(std::shared_ptr<NetworkPlayerProfile>& p,
                               unsigned local_id)
{
    const bool is_ffa =
        RaceManager::get()->getMinorMode() == RaceManager::MINOR_MODE_FREE_FOR_ALL;
    int red_count = 0;
    int blue_count = 0;
    for (unsigned i = 0; i < RaceManager::get()->getNumPlayers(); i++)
    {
        RemoteKartInfo& rki = RaceManager::get()->getKartInfo(i);
        if (rki.isReserved())
            continue;
        bool disconnected = rki.disconnected();
        if (RaceManager::get()->getKartInfo(i).getKartTeam() == KART_TEAM_RED &&
            !disconnected)
            red_count++;
        else if (RaceManager::get()->getKartInfo(i).getKartTeam() ==
            KART_TEAM_BLUE && !disconnected)
            blue_count++;
    }
    KartTeam target_team = red_count > blue_count ? KART_TEAM_BLUE :
        KART_TEAM_RED;

    for (unsigned i = 0; i < RaceManager::get()->getNumPlayers(); i++)
    {
        RemoteKartInfo& rki = RaceManager::get()->getKartInfo(i);
        std::shared_ptr<NetworkPlayerProfile> player =
            rki.getNetworkPlayerProfile().lock();
        if (!player)
        {
            if (is_ffa)
            {
                rki.copyFrom(p, local_id);
                return i;
            }
            if (getSettings()->hasTeamChoosing())
            {
                if ((p->getTeam() == KART_TEAM_RED &&
                    rki.getKartTeam() == KART_TEAM_RED) ||
                    (p->getTeam() == KART_TEAM_BLUE &&
                    rki.getKartTeam() == KART_TEAM_BLUE))
                {
                    rki.copyFrom(p, local_id);
                    return i;
                }
            }
            else
            {
                if (rki.getKartTeam() == target_team)
                {
                    getTeamManager()->setTeamInLobby(p, target_team);
                    rki.copyFrom(p, local_id);
                    return i;
                }
            }
        }
    }
    return -1;
}   // getReservedId

//-----------------------------------------------------------------------------

void PlayingRoom::setKartDataProperly(KartData& kart_data, const std::string& kart_name,
         std::shared_ptr<NetworkPlayerProfile> player,
         const std::string& type) const
{
    // This should set kart data for kart name at least in the following cases:
    // 1. useTuxHitboxAddon() is true
    // 2. kart_name is installed on the server
    // (for addons; standard karts are not processed here it seems)
    // 3. kart type is fine
    // Maybe I'm mistaken and then it should be fixed.
    // I extracted this into a separate function because if kart_name is set
    // by the server (for random addon kart, or due to a filter), kart data
    // has to be set in another place than default one.
    if (NetworkConfig::get()->useTuxHitboxAddon() &&
        StringUtils::startsWith(kart_name, "addon_") &&
        kart_properties_manager->hasKartTypeCharacteristic(type))
    {
        const KartProperties* real_addon =
            kart_properties_manager->getKart(kart_name);
        if (getSettings()->usesRealAddonKarts() && real_addon)
        {
            kart_data = KartData(real_addon);
        }
        else
        {
            const KartProperties* tux_kp =
                kart_properties_manager->getKart("tux");
            kart_data = KartData(tux_kp);
            kart_data.m_kart_type = type;
        }
        player->setKartData(kart_data);
    }
}   // setKartDataProperly
//-----------------------------------------------------------------------------
void PlayingRoom::addWaitingPlayersToGame()
{
    auto all_profiles = STKHost::get()->getAllPlayerProfiles();
    for (auto& profile : all_profiles)
    {
        auto peer = profile->getPeer();
        if (!peer || !peer->isValidated())
            continue;

        peer->resetAlwaysSpectateFull();
        peer->setWaitingForGame(false);
        peer->setSpectator(false);
        if (m_peers_ready.find(peer) == m_peers_ready.end())
        {
            m_peers_ready[peer] = false;
            if (!getSettings()->hasSqlManagement())
            {
                Log::info("ServerLobby",
                    "New player %s with online id %u from %s with %s.",
                    StringUtils::wideToUtf8(profile->getName()).c_str(),
                    profile->getOnlineId(),
                    peer->getAddress().toString().c_str(),
                    peer->getUserVersion().c_str());
            }
        }
        getLobby()->addWaitingPlayersToRanking(profile);
    }
    // Re-activiate the ai
    if (auto ai = m_ai_peer.lock())
        ai->setValidated(true);
}   // addWaitingPlayersToGame

//-----------------------------------------------------------------------------

void PlayingRoom::resetServer()
{
    addWaitingPlayersToGame();
    resetPeersReady();
    updatePlayerList(true/*update_when_reset_server*/);

    NetworkString* server_info = getNetworkString();
    server_info->setSynchronous(true);
    server_info->addUInt8(LE_SERVER_INFO);
    getGameSetupFromCtx()->addServerInfo(server_info);
    Comm::sendMessageToPeersInServer(server_info);
    delete server_info;

    for (auto p : m_peers_ready)
    {
        if (auto peer = p.first.lock())
            peer->updateLastActivity();
    }

    setup();

    // kimden: Before, the state was unified and it was set to REGISTER_SELF_ADDRESS
    // for WAN server, and to WAITING_FOR_START_GAME for LAN. For now, I'm not really
    // sure why. Some issues might arise.
    
    m_play_state = WAITING_FOR_START_GAME;
    getLobby()->resetServerToRSA();

    // The above also means I always call handleServerConfiguration() now,
    // and not only for LAN servers.
    if (m_reset_to_default_mode_later.exchange(false))
        handleServerConfiguration(NULL);

    updatePlayerList();
}   // resetServer
//-----------------------------------------------------------------------------

/** Update and see if any player disconnects, if so eliminate the kart in
 *  world, so this function must be called in main thread.
 */
void PlayingRoom::handlePlayerDisconnection() const
{
    if (!World::getWorld() ||
        World::getWorld()->getPhase() < WorldStatus::MUSIC_PHASE)
    {
        return;
    }

    int red_count = 0;
    int blue_count = 0;
    unsigned total = 0;
    for (unsigned i = 0; i < RaceManager::get()->getNumPlayers(); i++)
    {
        RemoteKartInfo& rki = RaceManager::get()->getKartInfo(i);
        if (rki.isReserved())
            continue;
        bool disconnected = rki.disconnected();
        if (RaceManager::get()->getKartInfo(i).getKartTeam() == KART_TEAM_RED &&
            !disconnected)
            red_count++;
        else if (RaceManager::get()->getKartInfo(i).getKartTeam() ==
            KART_TEAM_BLUE && !disconnected)
            blue_count++;

        if (!disconnected)
        {
            total++;
            continue;
        }

        if (m_game_info)
            m_game_info->saveDisconnectingIdInfo(i);
        else
            Log::warn("ServerLobby", "GameInfo is not accessible??");

        rki.makeReserved();

        AbstractKart* k = World::getWorld()->getKart(i);
        if (!k->isEliminated() && !k->hasFinishedRace())
        {
            CaptureTheFlag* ctf = dynamic_cast<CaptureTheFlag*>
                (World::getWorld());
            if (ctf)
                ctf->loseFlagForKart(k->getWorldKartId());

            World::getWorld()->eliminateKart(i,
                false/*notify_of_elimination*/);
            if (getSettings()->isRanked())
            {
                // Handle disconnection earlier to prevent cheating by joining
                // another ranked server
                // Real score will be submitted later in computeNewRankings
                const uint32_t id =
                    RaceManager::get()->getKartInfo(i).getOnlineId();
                RankingEntry penalized = m_ranking->getTemporaryPenalizedScores(id);
                auto request = std::make_shared<SubmitRankingRequest>
                    (penalized,
                    RaceManager::get()->getKartInfo(i).getCountryCode());
                NetworkConfig::get()->setUserDetails(request,
                    "submit-ranking");
                request->queue();
            }
            k->setPosition(
                World::getWorld()->getCurrentNumKarts() + 1);
            k->finishedRace(World::getWorld()->getTime(), true/*from_server*/);
        }
    }

    // If live players is enabled, don't end the game if unfair team
    if (!getSettings()->isLivePlayers() &&
        total != 1 && World::getWorld()->hasTeam() &&
        (red_count == 0 || blue_count == 0))
        World::getWorld()->setUnfairTeam(true);

}   // handlePlayerDisconnection
//-----------------------------------------------------------------------------

/** Add reserved players for live join later if required.
 */
void PlayingRoom::addLiveJoinPlaceholder(
    std::vector<std::shared_ptr<NetworkPlayerProfile> >& players) const
{
    if (!getSettings()->isLivePlayers() || !RaceManager::get()->supportsLiveJoining())
        return;
    if (RaceManager::get()->getMinorMode() == RaceManager::MINOR_MODE_FREE_FOR_ALL)
    {
        Track* t = TrackManager::get()->getTrack(getGameSetupFromCtx()->getCurrentTrack());
        assert(t);
        int max_players = std::min((int)getSettings()->getServerMaxPlayers(),
            (int)t->getMaxArenaPlayers());
        int add_size = max_players - (int)players.size();
        assert(add_size >= 0);
        for (int i = 0; i < add_size; i++)
        {
            players.push_back(
                NetworkPlayerProfile::getReservedProfile(KART_TEAM_NONE));
        }
    }
    else
    {
        // CTF or soccer, reserve at most 7 players on each team
        int red_count = 0;
        int blue_count = 0;
        for (unsigned i = 0; i < players.size(); i++)
        {
            if (players[i]->getTeam() == KART_TEAM_RED)
                red_count++;
            else
                blue_count++;
        }
        red_count = red_count >= 7 ? 0 : 7 - red_count;
        blue_count = blue_count >= 7 ? 0 : 7 - blue_count;
        for (int i = 0; i < red_count; i++)
        {
            players.push_back(
                NetworkPlayerProfile::getReservedProfile(KART_TEAM_RED));
        }
        for (int i = 0; i < blue_count; i++)
        {
            players.push_back(
                NetworkPlayerProfile::getReservedProfile(KART_TEAM_BLUE));
        }
    }
}   // addLiveJoinPlaceholder
//-----------------------------------------------------------------------------

/** This function is called when all clients have loaded the world and
 *  are therefore ready to start the race. It determine the start time in
 *  network timer for client and server based on pings and then switches state
 *  to WAIT_FOR_RACE_STARTED.
 */
void PlayingRoom::configPeersStartTime()
{
    uint32_t max_ping = 0;
    const unsigned max_ping_from_peers = getSettings()->getMaxPing();
    bool peer_exceeded_max_ping = false;
    for (auto p : m_peers_ready)
    {
        auto peer = p.first.lock();
        // Spectators don't send input so we don't need to delay for them
        if (!peer || peer->alwaysSpectate())
            continue;
        if (peer->getAveragePing() > max_ping_from_peers)
        {
            Log::warn("ServerLobby",
                "Peer %s cannot catch up with max ping %d.",
                peer->getAddress().toString().c_str(), max_ping);
            peer_exceeded_max_ping = true;
            continue;
        }
        max_ping = std::max(peer->getAveragePing(), max_ping);
    }
    if ((getSettings()->hasHighPingWorkaround() && peer_exceeded_max_ping) ||
        (getSettings()->isLivePlayers() && RaceManager::get()->supportsLiveJoining()))
    {
        Log::info("ServerLobby", "Max ping to ServerConfig::m_max_ping for "
            "live joining or high ping workaround.");
        max_ping = getSettings()->getMaxPing();
    }
    // Start up time will be after 2500ms, so even if this packet is sent late
    // (due to packet loss), the start time will still ahead of current time
    uint64_t start_time = STKHost::get()->getNetworkTimer() + (uint64_t)2500;
    powerup_manager->setRandomSeed(start_time);
    NetworkString* ns = getNetworkString(10);
    ns->setSynchronous(true);
    ns->addUInt8(LE_START_RACE).addUInt64(start_time);
    const uint8_t cc = (uint8_t)Track::getCurrentTrack()->getCheckManager()->getCheckStructureCount();
    ns->addUInt8(cc);
    *ns += *m_items_complete_state;
    m_client_starting_time = start_time;
    Comm::sendMessageToPeers(ns, PRM_RELIABLE);

    const unsigned jitter_tolerance = getSettings()->getJitterTolerance();
    Log::info("ServerLobby", "Max ping from peers: %d, jitter tolerance: %d",
        max_ping, jitter_tolerance);
    // Delay server for max ping / 2 from peers and jitter tolerance.
    m_server_delay = (uint64_t)(max_ping / 2) + (uint64_t)jitter_tolerance;
    start_time += m_server_delay;
    m_server_started_at = start_time;
    delete ns;
    m_play_state = WAIT_FOR_RACE_STARTED;

    World::getWorld()->setPhase(WorldStatus::SERVER_READY_PHASE);
    // Different stk process thread may have different stk host
    STKHost* stk_host = STKHost::get();
    joinStartGameThread();
    m_start_game_thread = std::thread([start_time, stk_host, this]()
        {
            const uint64_t cur_time = stk_host->getNetworkTimer();
            assert(start_time > cur_time);
            int sleep_time = (int)(start_time - cur_time);
            //Log::info("ServerLobby", "Start game after %dms", sleep_time);
            StkTime::sleep(sleep_time);
            //Log::info("ServerLobby", "Started at %lf", StkTime::getRealTime());
            m_play_state.store(RACING);
        });
}   // configPeersStartTime
//-----------------------------------------------------------------------------

void PlayingRoom::updateServerOwner(bool force)
{
    ServerInitState state = m_init_state.load();
    if (state != RUNNING)
        return;

    if (getCrownManager()->isOwnerLess())
        return;

    if (!force && !m_server_owner.expired())
        return;

    auto peers = STKHost::get()->getPeers();

    if (m_process_type != PT_MAIN)
    {
        auto id = m_client_server_host_id.load();
        for (unsigned i = 0; i < peers.size(); )
        {
            const auto& peer = peers[i];
            if (peer->getHostId() != id)
            {
                std::swap(peers[i], peers.back());
                peers.pop_back();
                continue;
            }
            ++i;
        }
    }

    for (unsigned i = 0; i < peers.size(); )
    {
        const auto& peer = peers[i];
        if (!peer->isValidated() || peer->isAIPeer())
        {
            std::swap(peers[i], peers.back());
            peers.pop_back();
            continue;
        }
        ++i;
    }

    if (peers.empty())
        return;

    std::shared_ptr<STKPeer> owner = getCrownManager()->getFirstInCrownOrder(peers);
    if (m_server_owner.expired() || m_server_owner.lock() != owner)
    {
        NetworkString* ns = getNetworkString();
        ns->setSynchronous(true);
        ns->addUInt8(LE_SERVER_OWNERSHIP);
        owner->sendPacket(ns);
        delete ns;
    }
    m_server_owner = owner;
    m_server_owner_id.store(owner->getHostId());
    updatePlayerList();
}   // updateServerOwner
//-----------------------------------------------------------------------------
/// ...
/// ...
/// ...
/// ...
/// ...
/// ...
/// ...
/// ...
/// ...
/// ...
/// ...
/// ...
/// ...
/// ...
//-----------------------------------------------------------------------------

void PlayingRoom::setTimeoutFromNow(int seconds)
{
    m_timeout.store((int64_t)StkTime::getMonoTimeMs() +
            (int64_t)(seconds * 1000.0f));
}   // setTimeoutFromNow
//-----------------------------------------------------------------------------

void PlayingRoom::setInfiniteTimeout()
{
    m_timeout.store(std::numeric_limits<int64_t>::max());
}   // setInfiniteTimeout
//-----------------------------------------------------------------------------

bool PlayingRoom::isInfiniteTimeout() const
{
    return m_timeout.load() == std::numeric_limits<int64_t>::max();
}   // isInfiniteTimeout
//-----------------------------------------------------------------------------

bool PlayingRoom::isTimeoutExpired() const
{
    return m_timeout.load() < (int64_t)StkTime::getMonoTimeMs();
}   // isTimeoutExpired
//-----------------------------------------------------------------------------

float PlayingRoom::getTimeUntilExpiration() const
{
    int64_t timeout = m_timeout.load();
    if (timeout == std::numeric_limits<int64_t>::max())
        return std::numeric_limits<float>::max();

    return (timeout - (int64_t)StkTime::getMonoTimeMs()) / 1000.0f;
}   // getTimeUntilExpiration
//-----------------------------------------------------------------------------

void PlayingRoom::onSpectatorStatusChange(const std::shared_ptr<STKPeer>& peer)
{
    auto state = m_play_state.load();
    if (state >= ServerPlayState::SELECTING && state < ServerPlayState::RACING)
    {
        erasePeerReady(peer);
        peer->setWaitingForGame(true);
    }
}   // onSpectatorStatusChange
//-----------------------------------------------------------------------------
