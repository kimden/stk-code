//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2013-2015 SuperTuxKart-Team
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

#include "network/protocols/server_lobby.hpp"

#include "items/network_item_manager.hpp"
#include "items/attachment.hpp"
#include "karts/kart_properties.hpp"
#include "karts/kart_properties_manager.hpp"
#include "karts/official_karts.hpp"
#include "modes/capture_the_flag.hpp"
#include "modes/soccer_world.hpp"
#include "modes/linear_world.hpp"
#include "network/crypto.hpp"
#include "network/database_connector.hpp"
#include "network/event.hpp"
#include "network/game_setup.hpp"
#include "network/network_config.hpp"
#include "network/network_player_profile.hpp"
#include "network/packet_types.hpp"
#include "network/protocol_manager.hpp"
#include "network/protocols/connect_to_peer.hpp"
#include "network/protocols/command_manager.hpp"
#include "network/protocols/game_protocol.hpp"
#include "network/protocols/game_events_protocol.hpp"
#include "network/protocols/ranking.hpp"
#include "network/race_event_manager.hpp"
#include "network/requests.hpp"
#include "network/server_config.hpp"
#include "network/stk_host.hpp"
#include "network/stk_ipv6.hpp"
#include "network/stk_peer.hpp"
#include "online/request_manager.hpp"
#include "tracks/check_manager.hpp"
#include "tracks/track.hpp"
#include "tracks/track_manager.hpp"
#include "utils/chat_manager.hpp"
#include "utils/communication.hpp"
#include "utils/crown_manager.hpp"
#include "utils/kart_elimination.hpp"
#include "utils/game_info.hpp"
#include "utils/hit_processor.hpp"
#include "utils/lobby_asset_manager.hpp"
#include "utils/lobby_gp_manager.hpp"
#include "utils/lobby_settings.hpp"
#include "utils/lobby_queues.hpp"
#include "utils/map_vote_handler.hpp"
#include "utils/name_decorators/generic_decorator.hpp"
#include "utils/team_manager.hpp"
#include "utils/tournament.hpp"
#include "utils/translation.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <iterator> 

// ============================================================================

// Helper functions.
namespace
{
    EncodedSinglePlayerPacket encodePlayer(std::shared_ptr<NetworkPlayerProfile> player,
            const std::shared_ptr<GenericDecorator>& decorator)
    {
        EncodedSinglePlayerPacket packet;
        packet.name            = player->getDecoratedName(decorator);
        packet.host_id         = player->getHostId();
        packet.kart_color      = player->getDefaultKartColor();
        packet.online_id       = player->getOnlineId();
        packet.handicap        = player->getHandicap();
        packet.local_player_id = player->getLocalPlayerId();
        packet.kart_team       = player->getTeam();
        packet.country_code    = player->getCountryCode();
        packet.kart_name       = player->getKartName();
        return packet;
    }
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
} // anonymous namespace

// ============================================================================

// We use max priority for all server requests to avoid downloading of addons
// icons blocking the poll request in all-in-one graphical client server

/** This is the central game setup protocol running in the server. It is
 *  mostly a finite state machine. Note that all nodes in ellipses and light
 *  grey background are actual states; nodes in boxes and white background 
 *  are functions triggered from a state or triggering potentially a state
 *  change.
 \dot
 digraph interaction {
 node [shape=box]; "Server Constructor"; "playerTrackVote"; "connectionRequested"; 
                   "signalRaceStartToClients"; "startedRaceOnClient"; "loadWorld";
 node [shape=ellipse,style=filled,color=lightgrey];

 "Server Constructor" -> "INIT_WAN" [label="If WAN game"]
 "Server Constructor" -> "WAITING_FOR_START_GAME" [label="If LAN game"]
 "INIT_WAN" -> "GETTING_PUBLIC_ADDRESS" [label="GetPublicAddress protocol callback"]
 "GETTING_PUBLIC_ADDRESS" -> "WAITING_FOR_START_GAME" [label="Register server"]
 "WAITING_FOR_START_GAME" -> "connectionRequested" [label="Client connection request"]
 "connectionRequested" -> "WAITING_FOR_START_GAME"
 "WAITING_FOR_START_GAME" -> "SELECTING" [label="Start race from authorised client"]
 "SELECTING" -> "SELECTING" [label="Client selects kart, #laps, ..."]
 "SELECTING" -> "playerTrackVote" [label="Client selected track"]
 "playerTrackVote" -> "SELECTING" [label="Not all clients have selected"]
 "playerTrackVote" -> "LOAD_WORLD" [label="All clients have selected; signal load_world to clients"]
 "LOAD_WORLD" -> "loadWorld"
 "loadWorld" -> "WAIT_FOR_WORLD_LOADED" 
 "WAIT_FOR_WORLD_LOADED" -> "WAIT_FOR_WORLD_LOADED" [label="Client or server loaded world"]
 "WAIT_FOR_WORLD_LOADED" -> "signalRaceStartToClients" [label="All clients and server ready"]
 "signalRaceStartToClients" -> "WAIT_FOR_RACE_STARTED"
 "WAIT_FOR_RACE_STARTED" ->  "startedRaceOnClient" [label="Client has started race"]
 "startedRaceOnClient" -> "WAIT_FOR_RACE_STARTED" [label="Not all clients have started"]
 "startedRaceOnClient" -> "DELAY_SERVER" [label="All clients have started"]
 "DELAY_SERVER" -> "DELAY_SERVER" [label="Not done waiting"]
 "DELAY_SERVER" -> "RACING" [label="Server starts race now"]
 }
 \enddot


 *  It starts with detecting the public ip address and port of this
 *  host (GetPublicAddress).
 */
ServerLobby::ServerLobby() : LobbyProtocol()
{
    m_client_server_host_id.store(0);
    m_lobby_players.store(0);

    m_lobby_context = std::make_shared<LobbyContext>(this, (bool)ServerConfig::m_soccer_tournament);
    m_lobby_context->setup();
    m_context = m_lobby_context.get();
    m_game_setup->setContext(m_context);
    m_context->setGameSetup(m_game_setup);

    m_current_ai_count.store(0);

    m_rs_state.store(RS_NONE);
    m_last_success_poll_time.store(StkTime::getMonoTimeMs() + 30000);
    m_last_unsuccess_poll_time = StkTime::getMonoTimeMs();
    m_server_owner_id.store(-1);
    m_registered_for_once_only = false;
    setHandleDisconnections(true);
    m_state = SET_PUBLIC_ADDRESS;
    if (ServerConfig::m_ranked)
    {
        Log::info("ServerLobby", "This server will submit ranking scores to "
            "the STK addons server. Don't bother hosting one without the "
            "corresponding permissions, as they would be rejected.");

        m_ranking = std::make_shared<Ranking>();
    }
    m_name_decorator = std::make_shared<GenericDecorator>();
    m_nim_complete_state = NimCompleteStatePacket();
    m_server_id_online.store(0);
    m_difficulty.store(ServerConfig::m_server_difficulty);
    m_game_mode.store(ServerConfig::m_server_mode);
    m_reset_to_default_mode_later.store(false);

    m_game_info = {};
}   // ServerLobby

//-----------------------------------------------------------------------------
/** Destructor.
 */
ServerLobby::~ServerLobby()
{
    if (m_server_id_online.load() != 0)
    {
        // For child process the request manager will keep on running
        unregisterServer(m_process_type == PT_MAIN ? true : false/*now*/);
    }
    if (getSettings()->isSavingServerConfig())
        ServerConfig::writeServerConfigToDisk();

#ifdef ENABLE_SQLITE3
    getDbConnector()->destroyDatabase();
#endif
}   // ~ServerLobby

//-----------------------------------------------------------------------------

void ServerLobby::initServerStatsTable()
{
#ifdef ENABLE_SQLITE3
    getDbConnector()->initServerStatsTable();
#endif
}   // initServerStatsTable

//-----------------------------------------------------------------------------
/** Calls the corresponding method from LobbyAssetManager
 *  whenever server is reset or game mode is changed. */
void ServerLobby::updateMapsForMode()
{
    getAssetManager()->updateMapsForMode(
        ServerConfig::getLocalGameMode(m_game_mode.load()).first
    );
}   // updateMapsForMode

//-----------------------------------------------------------------------------
void ServerLobby::setup()
{
    LobbyProtocol::setup();
    m_item_seed = 0;
    m_client_starting_time = 0;
    m_ai_count = 0;
    auto players = STKHost::get()->getPlayersForNewGame();
    if (m_game_setup->isGrandPrix() && !m_game_setup->isGrandPrixStarted())
    {
        for (auto player : players)
            player->resetGrandPrixData();
    }
    if (!m_game_setup->isGrandPrix() || !m_game_setup->isGrandPrixStarted())
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

    StateManager::get()->resetActivePlayers();
    getAssetManager()->onServerSetup();
    getSettings()->onServerSetup();
    updateMapsForMode();

    m_server_has_loaded_world.store(false);

    // Initialise the data structures to detect if all clients and 
    // the server are ready:
    resetPeersReady();
    setInfiniteTimeout();
    m_server_started_at = m_server_delay = 0;
    getCommandManager()->onServerSetup();
    m_game_info = {};
    Log::info("ServerLobby", "Resetting the server to its initial state.");
}   // setup

//-----------------------------------------------------------------------------
bool ServerLobby::notifyEvent(Event* event)
{
    assert(m_game_setup); // assert that the setup exists
    if (event->getType() != EVENT_TYPE_MESSAGE)
        return true;

    NetworkString &data = event->data();
    assert(data.size()); // message not empty
    uint8_t message_type;
    message_type = data.getUInt8();
    Log::info("ServerLobby", "Synchronous message of type %d received.",
              message_type);
    switch (message_type)
    {
    case LE_RACE_FINISHED_ACK:   playerFinishedResult(event);          break;
    case LE_LIVE_JOIN:           liveJoinRequest(event);               break;
    case LE_CLIENT_LOADED_WORLD: finishedLoadingLiveJoinClient(event); break;
    case LE_KART_INFO:           handleKartInfo(event);                break;
    case LE_CLIENT_BACK_LOBBY:   clientInGameWantsToBackLobby(event);  break;
    default:
        Log::error("ServerLobby", "Unknown message of type %d - ignored.",
                        message_type);
             break;
    }   // switch message_type
    return true;
}   // notifyEvent

//-----------------------------------------------------------------------------
void ServerLobby::handleChat(Event* event)
{
    if (!checkDataSize(event, 1) || !getChatManager()->getChat()) return;

    auto peer = event->getPeerSP();
    auto packet = event->getPacket<ChatPacket>();

    core::stringw message = packet.message;
    KartTeam target_team = KART_TEAM_NONE;
    if (packet.kart_team.has_value())
        target_team = packet.kart_team.get_value();

    getChatManager()->handleNormalChatMessage(peer,
            StringUtils::wideToUtf8(message), target_team, m_name_decorator);
}   // handleChat

//-----------------------------------------------------------------------------
void ServerLobby::changeTeam(Event* event)
{
    if (!checkDataSize(event, 1))
        return;

    auto packet = event->getPacket<ChangeTeamPacket>();
    uint8_t local_id = packet.local_id;

    auto& player = event->getPeer()->getPlayerProfiles().at(local_id);
    getTeamManager()->changeTeam(player);
}   // changeTeam

//-----------------------------------------------------------------------------
void ServerLobby::kickHost(Event* event)
{
    if (!checkDataSize(event, 4))
        return;

    auto packet = event->getPacket<KickHostPacket>();
    uint32_t host_id = packet.host_id;
    std::shared_ptr<STKPeer> target = STKHost::get()->findPeerByHostId(host_id);
    auto initiator = event->getPeerSP();

    getSettings()->tryKickingAnotherPeer(initiator, target);
}   // kickHost

//-----------------------------------------------------------------------------
bool ServerLobby::notifyEventAsynchronous(Event* event)
{
    assert(m_game_setup); // assert that the setup exists
    if (event->getType() == EVENT_TYPE_MESSAGE)
    {
        NetworkString &data = event->data();
        assert(data.size()); // message not empty
        uint8_t message_type;
        message_type = data.getUInt8();
        Log::info("ServerLobby", "Message of type %d received.",
                  message_type);
        switch(message_type)
        {
        case LE_CONNECTION_REQUESTED:        connectionRequested(event); break;
        case LE_KART_SELECTION:           kartSelectionRequested(event); break;
        case LE_CLIENT_LOADED_WORLD:  finishedLoadingWorldClient(event); break;
        case LE_VOTE:                           handlePlayerVote(event); break;
        case LE_KICK_HOST:                              kickHost(event); break;
        case LE_CHANGE_TEAM:                          changeTeam(event); break;
        case LE_REQUEST_BEGIN:                    startSelection(event); break;
        case LE_CHAT:                                 handleChat(event); break;
        case LE_CONFIG_SERVER:         handleServerConfiguration(event); break;
        case LE_CHANGE_HANDICAP:                  changeHandicap(event); break;
        case LE_CLIENT_BACK_LOBBY:
                           clientSelectingAssetsWantsToBackLobby(event); break;
        case LE_REPORT_PLAYER:                 writePlayerReport(event); break;
        case LE_ASSETS_UPDATE:                      handleAssets(event); break;
        case LE_COMMAND:                     handleServerCommand(event); break;
        default:
            Log::error("ServerLobby", "Invalid message type %d", message_type);
            break;
        }   // switch
    } // if (event->getType() == EVENT_TYPE_MESSAGE)
    else if (event->getType() == EVENT_TYPE_DISCONNECTED)
    {
        clientDisconnected(event);
    } // if (event->getType() == EVENT_TYPE_DISCONNECTED)
    return true;
}   // notifyEventAsynchronous

//-----------------------------------------------------------------------------
#ifdef ENABLE_SQLITE3
/* Every 1 minute STK will poll database:
 * 1. Set disconnected time to now for non-exists host.
 * 2. Clear expired player reports if necessary
 * 3. Kick active peer from ban list
 */
void ServerLobby::pollDatabase()
{
    auto db_connector = getDbConnector();
    if (!getSettings()->hasSqlManagement() || !db_connector->hasDatabase())
        return;

    if (!db_connector->isTimeToPoll())
        return;

    db_connector->updatePollTime();

    std::vector<DatabaseConnector::IpBanTableData> ip_ban_list =
            db_connector->getIpBanTableData();
    std::vector<DatabaseConnector::Ipv6BanTableData> ipv6_ban_list =
            db_connector->getIpv6BanTableData();
    std::vector<DatabaseConnector::OnlineIdBanTableData> online_id_ban_list =
            db_connector->getOnlineIdBanTableData();

    for (std::shared_ptr<STKPeer> p : STKHost::get()->getPeers())
    {
        if (p->isAIPeer())
            continue;
        bool is_kicked = false;
        std::string address = "";
        std::string reason = "";
        std::string description = "";

        if (p->getAddress().isIPv6())
        {
            address = p->getAddress().toString(false);
            if (address.empty())
                continue;
            for (auto& item: ipv6_ban_list)
            {
                if (insideIPv6CIDR(item.ipv6_cidr.c_str(), address.c_str()) == 1)
                {
                    is_kicked = true;
                    reason = item.reason;
                    description = item.description;
                    break;
                }
            }
        }
        else
        {
            uint32_t peer_addr = p->getAddress().getIP();
            address = p->getAddress().toString();
            for (auto& item: ip_ban_list)
            {
                if (item.ip_start <= peer_addr && item.ip_end >= peer_addr)
                {
                    is_kicked = true;
                    reason = item.reason;
                    description = item.description;
                    break;
                }
            }
        }
        if (!is_kicked && !p->getPlayerProfiles().empty())
        {
            uint32_t online_id = p->getMainProfile()->getOnlineId();
            for (auto& item: online_id_ban_list)
            {
                if (item.online_id == online_id)
                {
                    is_kicked = true;
                    reason = item.reason;
                    description = item.description;
                    break;
                }
            }
        }
        if (is_kicked)
        {
            Log::info("ServerLobby", "Kick %s, reason: %s, description: %s",
                address.c_str(), reason.c_str(), description.c_str());
            p->kick();
        }
    } // for p in peers

    db_connector->clearOldReports();

    auto peers = STKHost::get()->getPeers();
    std::vector<uint32_t> hosts;
    if (!peers.empty())
    {
        for (auto& peer : peers)
        {
            if (!peer->isValidated())
                continue;
            hosts.push_back(peer->getHostId());
        }
    }
    db_connector->setDisconnectionTimes(hosts);
}   // pollDatabase

#endif
//-----------------------------------------------------------------------------
void ServerLobby::writePlayerReport(Event* event)
{
#ifdef ENABLE_SQLITE3
    auto db_connector = getDbConnector();
    if (!db_connector->hasDatabase() || !db_connector->hasPlayerReportsTable())
        return;
    std::shared_ptr<STKPeer> reporter = event->getPeerSP();
    if (!reporter->hasPlayerProfiles())
        return;
    auto reporter_npp = reporter->getMainProfile();

    auto packet = event->getPacket<ReportRequestPacket>();
    uint32_t reporting_host_id = packet.host_id;
    core::stringw info = packet.info;
    if (info.empty())
        return;

    auto reporting_peer = STKHost::get()->findPeerByHostId(reporting_host_id);
    if (!reporting_peer || !reporting_peer->hasPlayerProfiles())
        return;
    auto reporting_npp = reporting_peer->getMainProfile();

    bool written = db_connector->writeReport(reporter, reporter_npp,
            reporting_peer, reporting_npp, info);
    if (written)
    {
        ReportSuccessPacket packet2;
        packet2.success = 1;
        packet2.reported_name = reporting_npp->getName();
        event->getPeer()->sendPacket(packet2);
    }
#endif
}   // writePlayerReport

//-----------------------------------------------------------------------------
/** Find out the public IP server or poll STK server asynchronously. */
void ServerLobby::asynchronousUpdate()
{
    if (m_rs_state.load() == RS_ASYNC_RESET)
    {
        resetVotingTime();
        resetServer();
        m_rs_state.store(RS_NONE);
    }

    getChatManager()->clearAllExpiredWeakPtrs();

#ifdef ENABLE_SQLITE3
    pollDatabase();
#endif

    // Check if server owner has left
    updateServerOwner();

    if (getSettings()->isRanked() && m_state.load() == WAITING_FOR_START_GAME)
        m_ranking->cleanup();

    if (!getSettings()->isLegacyGPMode() || m_state.load() == WAITING_FOR_START_GAME)
    {
        // Only poll the STK server if server has been registered.
        if (m_server_id_online.load() != 0 &&
            m_state.load() != REGISTER_SELF_ADDRESS)
            checkIncomingConnectionRequests();
        handlePendingConnection();
    }

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

    switch (m_state.load())
    {
    case SET_PUBLIC_ADDRESS:
    {
        // In case of LAN we don't need our public address or register with the
        // STK server, so we can directly go to the accepting clients state.
        if (NetworkConfig::get()->isLAN())
        {
            m_state = WAITING_FOR_START_GAME;
            if (m_reset_to_default_mode_later.exchange(false))
                handleServerConfiguration(NULL);

            updatePlayerList();
            STKHost::get()->startListening();
            return;
        }
        auto ip_type = NetworkConfig::get()->getIPType();
        // Set the IPv6 address first for possible IPv6 only server
        if (isIPv6Socket() && ip_type >= NetworkConfig::IP_V6)
        {
            STKHost::get()->setPublicAddress(AF_INET6);
        }
        if (ip_type == NetworkConfig::IP_V4 ||
            ip_type == NetworkConfig::IP_DUAL_STACK)
        {
            STKHost::get()->setPublicAddress(AF_INET);
        }
        if (STKHost::get()->getPublicAddress().isUnset() &&
            STKHost::get()->getPublicIPv6Address().empty())
        {
            m_state = ERROR_LEAVE;
        }
        else
        {
            STKHost::get()->startListening();
            m_state = REGISTER_SELF_ADDRESS;
        }
        break;
    }
    case REGISTER_SELF_ADDRESS:
    {
        if (m_game_setup->isGrandPrixStarted() || m_registered_for_once_only)
        {
            m_state = WAITING_FOR_START_GAME;
            if (m_reset_to_default_mode_later.exchange(false))
                handleServerConfiguration(NULL);

            updatePlayerList();
            break;
        }
        // Register this server with the STK server. This will block
        // this thread, because there is no need for the protocol manager
        // to react to any requests before the server is registered.
        if (m_server_registering.expired() && m_server_id_online.load() == 0)
            registerServer(true/*first_time*/);

        if (m_server_registering.expired())
        {
            // Finished registering server
            if (m_server_id_online.load() != 0)
            {
                // For non grand prix server we only need to register to stk
                // addons once
                if (!getSettings()->isLegacyGPMode())
                    m_registered_for_once_only = true;
                m_state = WAITING_FOR_START_GAME;
                if (m_reset_to_default_mode_later.exchange(false))
                    handleServerConfiguration(NULL);

                updatePlayerList();
            }
        }
        break;
    }
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
                m_game_setup->isGrandPrixStarted()) &&
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
                !m_game_setup->isGrandPrixStarted())
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
    case ERROR_LEAVE:
    {
        requestTerminate();
        m_state = EXITING;
        STKHost::get()->requestShutdown();
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
            LoadWorldPacket packet = getLoadWorldMessage(players, false/*live_join*/);
            sendPacketToPeers(packet);
            // updatePlayerList so the in lobby players (if any) can see always
            // spectators join the game
            if (has_always_on_spectators || !previous_spectate_mode.empty())
                updatePlayerList();

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

}   // asynchronousUpdate

//-----------------------------------------------------------------------------
LoadWorldPacket ServerLobby::getLoadWorldMessage(
    std::vector<std::shared_ptr<NetworkPlayerProfile> >& players,
    bool live_join) const
{
    LoadWorldPacket packet;
    packet.default_vote = getSettings()->encodeDefaultVote();
    packet.live_join = live_join;
    packet.players_size = players.size();
    for (auto& player: players)
        packet.all_players.push_back(encodePlayer(player, m_name_decorator));
    packet.item_seed = m_item_seed;

    auto& stk_config = STKConfig::get();

    if (RaceManager::get()->isBattleMode())
    {
        BattleInfoPacket battle_packet;
        battle_packet.battle_hit_capture_limit = getSettings()->getBattleHitCaptureLimit();
        battle_packet.battle_time_limit = getSettings()->getBattleTimeLimit();
        battle_packet.flag_return_time = (uint16_t)stk_config->time2Ticks(
                getSettings()->getFlagReturnTimeout());
        battle_packet.flag_deactivated_time = (uint16_t)stk_config->time2Ticks(
                getSettings()->getFlagDeactivatedTime());
        packet.battle_info = battle_packet;
    }
    MultipleKartDataPacket multi_packet;
    for (unsigned i = 0; i < players.size(); i++)
        multi_packet.players_kart_data.push_back(players[i]->getKartData().encode());

    packet.karts_data = multi_packet;

    return packet;
}   // getLoadWorldMessage

//-----------------------------------------------------------------------------
/** Returns true if server can be live joined or spectating
 */
bool ServerLobby::canLiveJoinNow() const
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
void ServerLobby::rejectLiveJoin(std::shared_ptr<STKPeer> peer, BackLobbyReason blr)
{
    BackLobbyPacket packet1;
    packet1.reason = blr;
    peer->sendPacket(packet1);

    updatePlayerList();

    ServerInfoPacket packet2 = m_game_setup->addServerInfo();
    peer->sendPacket(packet2);
}   // rejectLiveJoin

//-----------------------------------------------------------------------------
/** This message is like kartSelectionRequested, but it will send the peer
 *  load world message if he can join the current started game.
 */
void ServerLobby::liveJoinRequest(Event* event)
{
    // I moved some data getters ahead of some returns. This should be fine
    // in general, but you know what caused it if smth goes wrong.

    std::shared_ptr<STKPeer> peer = event->getPeerSP();
    auto packet = event->getPacket<LiveJoinRequestPacket>();
    bool spectator = packet.is_spectator;

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
        setPlayerKarts(packet.player_karts.get_value(), peer);

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

    LoadWorldPacket load_world_packet = getLoadWorldMessage(players, true/*live_join*/);
    peer->sendPacket(load_world_packet);

    peer->updateLastActivity();
}   // liveJoinRequest

//-----------------------------------------------------------------------------
/** Decide where to put the live join player depends on his team and game mode.
 */
int ServerLobby::getReservedId(std::shared_ptr<NetworkPlayerProfile>& p,
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
/** Finally put the kart in the world and inform client the current world
 *  status, (including current confirmed item state, kart scores...)
 */
void ServerLobby::finishedLoadingLiveJoinClient(Event* event)
{
    std::shared_ptr<STKPeer> peer = event->getPeerSP();
    auto unused = event->getPacket<FinishedLoadingLiveJoinPacket>();
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

    LiveJoinPacket packet;

    packet.client_starting_time = m_client_starting_time;
    packet.check_count = cc;
    packet.live_join_start_time = live_join_start_time;
    packet.last_live_join_util_ticks = m_last_live_join_util_ticks;

    NetworkItemManager* nim = dynamic_cast<NetworkItemManager*>
        (Track::getCurrentTrack()->getItemManager());
    assert(nim);
    packet.nim_complete_state = nim->saveCompleteState();
    nim->addLiveJoinPeer(peer);

    packet.world_complete_state = std::dynamic_pointer_cast<WorldCompleteStatePacket>
            (w->saveCompleteState(peer));

    if (RaceManager::get()->supportsLiveJoining())
    {
        InsideGameInfoPacket inside_packet;
        // Only needed in non-racing mode as no need players can added after
        // starting of race
        std::vector<std::shared_ptr<NetworkPlayerProfile> > players =
            getLivePlayers();

        inside_packet.players_size = players.size();

        for (auto& player: players)
            inside_packet.all_players.push_back(encodePlayer(player, m_name_decorator));
        for (unsigned i = 0; i < players.size(); i++)
            inside_packet.players_kart_data.push_back(players[i]->getKartData().encode());

        packet.inside_info = inside_packet;
    }

    m_peers_ready[peer] = false;
    peer->setWaitingForGame(false);
    peer->setSpectator(spectator);

    peer->sendPacket(packet);
    updatePlayerList();
    peer->updateLastActivity();
}   // finishedLoadingLiveJoinClient

//-----------------------------------------------------------------------------
/** Simple finite state machine.  Once this
 *  is known, register the server and its address with the stk server so that
 *  client can find it.
 */
void ServerLobby::update(int ticks)
{
    World* w = World::getWorld();
    bool world_started = m_state.load() >= WAIT_FOR_WORLD_LOADED &&
        m_state.load() <= RACING && m_server_has_loaded_world.load();
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
                if (m_process_type == PT_CHILD &&
                    peer->getHostId() == m_client_server_host_id.load())
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
    if (m_state.load() == WAITING_FOR_START_GAME) {
        sec = getSettings()->getKickIdleLobbyPlayerSeconds();
        auto peers = STKHost::get()->getPeers();
        for (auto peer: peers)
        {
            if (!peer->isAIPeer() &&
                sec > 0 && peer->idleForSeconds() > sec &&
                !peer->isDisconnected() && NetworkConfig::get()->isWAN())
            {
                // Don't kick in game GUI server host so he can idle in the lobby
                if (m_process_type == PT_CHILD &&
                    peer->getHostId() == m_client_server_host_id.load())
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
    {
        storePlayingTrack(RaceManager::get()->getTrackName());
    }
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
        (m_state.load() > WAITING_FOR_START_GAME/* ||
        m_game_setup->isGrandPrixStarted()*/) &&
        (STKHost::get()->getPlayersInGame() == 0 ||
        all_players_in_world_disconnected))
    {
        if (RaceEventManager::get() &&
            RaceEventManager::get()->isRunning())
        {
            // Send a notification to all players who may have start live join
            // or spectate to go back to lobby
            BackLobbyPacket packet;
            packet.reason = BLR_NONE;
            sendPacketToPeersInServer(packet);

            RaceEventManager::get()->stop();
            RaceEventManager::get()->getProtocol()->requestTerminate();
            GameProtocol::lock()->requestTerminate();
        }
        else if (auto ai = m_ai_peer.lock())
        {
            // Reset AI peer for empty server, which will delete world
            BackLobbyPacket packet;
            packet.reason = BLR_NONE;
            ai->sendPacket(packet);
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
        m_state.load() == SELECTING &&
        STKHost::get()->getPlayersInGame() == 1)
    {
        BackLobbyPacket packet;
        packet.reason = BLR_ONE_PLAYER_IN_RANKED_MATCH;
        sendPacketToPeers(packet);

        resetVotingTime();
        // m_game_setup->cancelOneRace();
        //m_game_setup->stopGrandPrix();
        m_rs_state.store(RS_ASYNC_RESET);
    }

    handlePlayerDisconnection();

    switch (m_state.load())
    {
    case SET_PUBLIC_ADDRESS:
    case REGISTER_SELF_ADDRESS:
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

        sendPacketToPeers(m_result_packet);

        Log::info("ServerLobby", "End of game message sent");
        break;

    case RESULT_DISPLAY:
        if (checkPeersReady(true/*ignore_ai_peer*/, AFTER_GAME) ||
            isTimeoutExpired())
        {
            // Send a notification to all clients to exit
            // the race result screen
            BackLobbyPacket packet;
            packet.reason = BLR_NONE;
            sendPacketToPeersInServer(packet);

            m_rs_state.store(RS_ASYNC_RESET);
        }
        break;
    case ERROR_LEAVE:
    case EXITING:
        break;
    }
}   // update

//-----------------------------------------------------------------------------
/** Register this server (i.e. its public address) with the STK server
 *  so that clients can find it. It blocks till a response from the
 *  stk server is received (this function is executed from the 
 *  ProtocolManager thread). The information about this client is added
 *  to the table 'server'.
 */
void ServerLobby::registerServer(bool first_time)
{
    auto request = std::make_shared<RegisterServerRequest>(
        std::dynamic_pointer_cast<ServerLobby>(shared_from_this()), first_time);
    NetworkConfig::get()->setServerDetails(request, "create");
    const SocketAddress& addr = STKHost::get()->getPublicAddress();
    request->addParameter("address",      addr.getIP()        );
    request->addParameter("port",         addr.getPort()      );
    request->addParameter("private_port",
                                    STKHost::get()->getPrivatePort()      );
    request->addParameter("name", m_game_setup->getServerNameUtf8());
    request->addParameter("max_players", getSettings()->getServerMaxPlayers());
    int difficulty = m_difficulty.load();
    request->addParameter("difficulty", difficulty);
    int game_mode = m_game_mode.load();
    request->addParameter("game_mode", game_mode);
    const std::string& pw = getSettings()->getPrivateServerPassword();
    request->addParameter("password", (unsigned)(!pw.empty()));
    request->addParameter("version", (unsigned)ServerConfig::m_server_version);

    bool ipv6_only = addr.isUnset();
    if (!ipv6_only)
    {
        Log::info("ServerLobby", "Public IPv4 server address %s",
            addr.toString().c_str());
    }
    if (!STKHost::get()->getPublicIPv6Address().empty())
    {
        request->addParameter("address_ipv6",
            STKHost::get()->getPublicIPv6Address());
        Log::info("ServerLobby", "Public IPv6 server address %s",
            STKHost::get()->getPublicIPv6Address().c_str());
    }
    request->queue();
    m_server_registering = request;
}   // registerServer

//-----------------------------------------------------------------------------
/** Unregister this server (i.e. its public address) with the STK server,
 *  currently when karts enter kart selection screen it will be done or quit
 *  stk.
 */
void ServerLobby::unregisterServer(bool now, std::weak_ptr<ServerLobby> sl)
{
    auto request = std::make_shared<UnregisterServerRequest>(sl);
    NetworkConfig::get()->setServerDetails(request, "stop");

    const SocketAddress& addr = STKHost::get()->getPublicAddress();
    request->addParameter("address", addr.getIP());
    request->addParameter("port", addr.getPort());
    bool ipv6_only = addr.isUnset();
    if (!ipv6_only)
    {
        Log::info("ServerLobby", "Unregister server address %s",
            addr.toString().c_str());
    }
    else
    {
        Log::info("ServerLobby", "Unregister server address %s",
            STKHost::get()->getValidPublicAddress().c_str());
    }

    // No need to check for result as server will be auto-cleared anyway
    // when no polling is done
    if (now)
    {
        request->executeNow();
    }
    else
        request->queue();

}   // unregisterServer

//-----------------------------------------------------------------------------
/** Instructs all clients to start the kart selection. If event is NULL,
 *  the command comes from the owner less server.
 */
void ServerLobby::startSelection(const Event *event)
{
    bool need_to_update = false;
    bool cooldown = getSettings()->isCooldown();
    
    if (event != NULL)
    {
        if (m_state.load() != WAITING_FOR_START_GAME)
        {
            Log::warn("ServerLobby",
                "Received startSelection while being in state %d.",
                m_state.load());
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
                updatePlayerList();
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
                BadTeamPacket packet;
                event->getPeer()->sendPacket(packet);
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

    if (getSettings()->isLegacyGPMode())
    {
        ProtocolManager::lock()->findAndTerminate(PROTOCOL_CONNECTION);
        if (m_server_id_online.load() != 0)
        {
            unregisterServer(false/*now*/,
                std::dynamic_pointer_cast<ServerLobby>(shared_from_this()));
        }
    }

    startVotingPeriod(getSettings()->getVotingTimeout());

    peers = STKHost::get()->getPeers();
    for (auto& peer: peers)
    {
        if (peer->isDisconnected() && !peer->isValidated())
            continue;
        if (!getCrownManager()->canRace(peer) || peer->isWaitingForGame())
            continue; // they are handled below

        // Start selection - must be synchronous since the receiver pushes
        // a new screen, which must be done from the main thread.

        StartSelectionPacket packet;
        packet.voting_timeout = getSettings()->getVotingTimeout();
        packet.no_kart_selection = getSettings()->isLegacyGPMode() && m_game_setup->isGrandPrixStarted();
        packet.fixed_length = !getSettings()->hasNoLapRestrictions();
        packet.track_voting = getSettings()->hasTrackVoting();

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

        packet.assets = getAssetManager()->encodePlayerKartsAndCommonMaps(all_k);
        peer->sendPacket(packet);

        if (getQueues()->areKartFiltersIgnoringKarts())
            Comm::sendStringToPeer(peer, "The server will ignore your kart choice");
    }

    m_state = SELECTING;
    if (need_to_update || !always_spectate_peers.empty())
    {
        BackLobbyPacket packet;
        packet.reason = BLR_SPECTATING_NEXT_GAME;

        sendPacketToAllPeersWith(
            [always_spectate_peers](std::shared_ptr<STKPeer> peer)
            {
                return always_spectate_peers.find(peer) !=
                always_spectate_peers.end();
            }, packet);

        updatePlayerList();
    }

    if (getSettings()->isLegacyGPMode())
    {
        // Drop all pending players and keys if doesn't allow joinning-waiting
        for (auto& p : m_pending_connection)
        {
            if (auto peer = p.first.lock())
                peer->disconnect();
        }
        m_pending_connection.clear();
        std::unique_lock<std::mutex> ul(m_keys_mutex);
        m_keys.clear();
        ul.unlock();
    }

    // Will be changed after the first vote received
    setInfiniteTimeout();

    getGPManager()->onStartSelection();

    getCommandManager()->onStartSelection();
}   // startSelection

//-----------------------------------------------------------------------------
/** Query the STK server for connection requests. For each connection request
 *  start a ConnectToPeer protocol.
 */
void ServerLobby::checkIncomingConnectionRequests()
{
    // First poll every 5 seconds. Return if no polling needs to be done.
    const uint64_t POLL_INTERVAL = 5000;
    static uint64_t last_poll_time = 0;
    if (StkTime::getMonoTimeMs() < last_poll_time + POLL_INTERVAL ||
        StkTime::getMonoTimeMs() > m_last_success_poll_time.load() + 30000)
        return;

    // Keep the port open, it can be sent to anywhere as we will send to the
    // correct peer later in ConnectToPeer.
    if (getSettings()->isFirewalledServer())
    {
        BareNetworkString data;
        data.addUInt8(0);
        const SocketAddress* stun_v4 = STKHost::get()->getStunIPv4Address();
        const SocketAddress* stun_v6 = STKHost::get()->getStunIPv6Address();
        if (stun_v4)
            STKHost::get()->sendRawPacket(data, *stun_v4);
        if (stun_v6)
            STKHost::get()->sendRawPacket(data, *stun_v6);
    }

    // Now poll the stk server
    last_poll_time = StkTime::getMonoTimeMs();

    auto request = std::make_shared<PollServerRequest>(
        std::dynamic_pointer_cast<ServerLobby>(shared_from_this()),
        ProtocolManager::lock());
    NetworkConfig::get()->setServerDetails(request,
        "poll-connection-requests");
    const SocketAddress& addr = STKHost::get()->getPublicAddress();
    request->addParameter("address", addr.getIP()  );
    request->addParameter("port",    addr.getPort());
    request->addParameter("current-players", getLobbyPlayers());
    request->addParameter("current-ai", m_current_ai_count.load());
    request->addParameter("game-started",
        m_state.load() == WAITING_FOR_START_GAME ? 0 : 1);
    std::string current_track = getPlayingTrackIdent();
    if (!current_track.empty())
        request->addParameter("current-track", getPlayingTrackIdent());
    request->queue();

}   // checkIncomingConnectionRequests

//-----------------------------------------------------------------------------
/** Checks if the race is finished, and if so informs the clients and switches
 *  to state RESULT_DISPLAY, during which the race result gui is shown and all
 *  clients can click on 'continue'.
 */
void ServerLobby::checkRaceFinished()
{
    assert(RaceEventManager::get()->isRunning());
    assert(World::getWorld());
    if (!RaceEventManager::get()->isRaceOver()) return;

    if (isTournament())
        getTournament()->onRaceFinished();

    if (RaceManager::get()->getMinorMode() ==
        RaceManager::MINOR_MODE_SOCCER)
        Log::info("ServerLobby", "SoccerMatchLog: The game is considered finished.");
    else
        Log::info("ServerLobby", "The game is considered finished.");
    // notify the network world that it is stopped
    RaceEventManager::get()->stop();
    RaceManager::get()->resetHitProcessor();

    // stop race protocols before going back to lobby (end race)
    RaceEventManager::get()->getProtocol()->requestTerminate();
    GameProtocol::lock()->requestTerminate();

    // Save race result before delete the world
    m_result_packet = RaceFinishedPacket();
    std::vector<float> gp_changes;

    if (RaceManager::get()->modeHasLaps())
    {
        int fastest_lap =
            static_cast<LinearWorld*>(World::getWorld())->getFastestLapTicks();

        // kimden: you shouldn't expose packet field type like that
        m_result_packet.fastest_lap = fastest_lap;
        m_result_packet.fastest_kart_name = static_cast<LinearWorld*>(World::getWorld())
                ->getFastestLapKartName();
    }

    if (m_game_setup->isGrandPrix())
    {
        m_result_packet.gp_scores = getGPManager()->updateGPScores(gp_changes);
    }

    m_result_packet.point_changes_indication = 
            (getSettings()->isRanked() && RaceManager::get()->modeHasLaps()) ||
            m_game_setup->isGrandPrix();

    if (getKartElimination()->isEnabled())
    {
        std::string msg = getKartElimination()->onRaceFinished();
        if (!msg.empty())
            Comm::sendStringToAllPeers(msg);
    }

    if (getSettings()->isStoringResults())
    {
        if (m_game_info)
            m_game_info->fillAndStoreResults();
        else
            Log::warn("ServerLobby", "GameInfo is not accessible??");
    }

    if (getSettings()->isRanked())
    {
        m_result_packet.point_changes = computeNewRankings();
        submitRankingsToAddons();
    }
    else if (m_game_setup->isGrandPrix())
    {
        PointChangesPacket subpacket;

        unsigned player_count = RaceManager::get()->getNumPlayers();
        subpacket.player_count = (uint8_t)player_count;

        for (unsigned i = 0; i < player_count; i++)
            subpacket.changes.push_back(gp_changes[i]);

        m_result_packet.point_changes = subpacket;
    }
    m_state.store(WAIT_FOR_RACE_STOPPED);

    getAssetManager()->gameFinishedOn(RaceManager::get()->getTrackName());

    getQueues()->popOnRaceFinished();
}   // checkRaceFinished

//-----------------------------------------------------------------------------

/** Compute the new player's rankings used in ranked servers
 */
PointChangesPacket ServerLobby::computeNewRankings()
{
    unsigned player_count = RaceManager::get()->getNumPlayers();

    // Empty packet for exceptions - unlikely to have though.
    PointChangesPacket empty_packet;
    empty_packet.player_count = (uint8_t)player_count;
    empty_packet.changes = std::vector<float>(player_count, 0.);

    // No ranking for battle mode
    if (!RaceManager::get()->modeHasLaps())
        return empty_packet;

    World* w = World::getWorld();
    assert(w);

    // If all players quitted the race, we assume something went wrong
    // and skip entirely rating and statistics updates.
    for (unsigned i = 0; i < player_count; i++)
    {
        if (!w->getKart(i)->isEliminated())
            break;
        if ((i + 1) == player_count)
            return empty_packet;
    }
    
    // Fill the results for the rankings to process
    std::vector<RaceResultData> data;
    for (unsigned i = 0; i < player_count; i++)
    {
        RaceResultData entry;
        entry.online_id = RaceManager::get()->getKartInfo(i).getOnlineId();
        entry.is_eliminated = w->getKart(i)->isEliminated();
        entry.time = RaceManager::get()->getKartRaceTime(i);
        entry.handicap = w->getKart(i)->getHandicap();
        data.push_back(entry);
    }

    // "LOLAND" changes, accidentally merged into supertuxkart/master
    // and not noticed for 4 months.
    // for (int i = 0; i < 64; ++i) {
    m_ranking->computeNewRankings(data, RaceManager::get()->isTimeTrialMode());
    // }

    // Used to display rating change at the end of a race
    PointChangesPacket packet;
    packet.player_count = (uint8_t)player_count;
    for (unsigned i = 0; i < player_count; i++)
    {
        const uint32_t id = RaceManager::get()->getKartInfo(i).getOnlineId();
        double change = m_ranking->getDelta(id);
        packet.changes.push_back((float)change);
    }
    return packet;
}   // computeNewRankings
//-----------------------------------------------------------------------------
/** Called when a client disconnects.
 *  \param event The disconnect event.
 */
void ServerLobby::clientDisconnected(Event* event)
{
    auto players_on_peer = event->getPeer()->getPlayerProfiles();
    if (players_on_peer.empty())
        return;

    World* w = World::getWorld();
    std::shared_ptr<STKPeer> peer = event->getPeerSP();
    getChatManager()->onPeerDisconnect(peer);
     // No warnings otherwise, as it could happen during lobby period
    if (m_game_info)
    {
        if (w)
            m_game_info->saveDisconnectingPeerInfo(peer);
    }
    else
        Log::warn("ServerLobby", "GameInfo is not accessible??");

    PlayerDisconnectedPacket packet;
    packet.players_size = (uint8_t)players_on_peer.size();
    packet.host_id = event->getPeer()->getHostId();

    const bool waiting_peer_disconnected =
        event->getPeer()->isWaitingForGame();

    for (auto p : players_on_peer)
    {
        std::string name = StringUtils::wideToUtf8(p->getName());
        packet.names.push_back(name);
        Log::info("ServerLobby", "%s disconnected", name.c_str());
        getCommandManager()->deleteUser(name);
    }

    unsigned players_number;
    STKHost::get()->updatePlayers(NULL, NULL, &players_number);
    if (players_number == 0)
        resetToDefaultSettings();

    // Don't show waiting peer disconnect message to in game player
    sendPacketToAllPeersWith([waiting_peer_disconnected]
        (std::shared_ptr<STKPeer> p)
        {
            if (!p->isValidated())
                return false;
            if (!p->isWaitingForGame() && waiting_peer_disconnected)
                return false;
            return true;
        }, packet);
    updatePlayerList();

#ifdef ENABLE_SQLITE3
    getDbConnector()->writeDisconnectInfoTable(event->getPeerSP());
#endif
}   // clientDisconnected

//-----------------------------------------------------------------------------
    
void ServerLobby::kickPlayerWithReason(std::shared_ptr<STKPeer> peer, const char* reason) const
{
    ConnectionRefusedPacket packet;
    packet.reason = RR_BANNED;
    packet.message = reason;
    peer->sendPacket(packet, PRM_RELIABLE, PEM_UNENCRYPTED);
    
    peer->cleanPlayerProfiles();
    peer->reset();
}   // kickPlayerWithReason
//-----------------------------------------------------------------------------

void ServerLobby::saveIPBanTable(const SocketAddress& addr)
{
#ifdef ENABLE_SQLITE3
    getDbConnector()->saveAddressToIpBanTable(addr);
#endif
}   // saveIPBanTable
//-----------------------------------------------------------------------------

bool ServerLobby::handleAssets(Event* event)
{
    auto packet = event->getPacket<NewAssetsPacket>();
    std::shared_ptr<STKPeer> peer = event->getPeerSP();

    std::set<std::string> client_karts;
    for (const std::string& item: packet.assets.karts)
        client_karts.insert(item);

    std::set<std::string> client_maps;
    for (const std::string& item: packet.assets.maps)
        client_maps.insert(item);

    return handleAssetsAndAddonScores(peer, client_karts, client_maps);
}   // handleAssets
//-----------------------------------------------------------------------------

bool ServerLobby::handleAssetsAndAddonScores(std::shared_ptr<STKPeer> peer,
        const std::set<std::string>& client_karts,
        const std::set<std::string>& client_maps)
{
    if (!getAssetManager()->handleAssetsForPeer(peer, client_karts, client_maps))
    {
        if (peer->isValidated())
        {
            Comm::sendStringToPeer(peer, "You deleted some assets that are required to stay on the server");
            peer->kick();
        }
        else
        {
            ConnectionRefusedPacket packet;
            packet.reason = RR_INCOMPATIBLE_DATA;
            packet.message = getSettings()->getIncompatibleAdvice();
            peer->sendPacket(packet, PRM_RELIABLE, PEM_UNENCRYPTED);

            peer->cleanPlayerProfiles();
            peer->reset();
        }
        Log::verbose("ServerLobby", "Player has incompatible karts / tracks.");
        return false;
    }

    std::array<int, AS_TOTAL> addons_scores = getAssetManager()->getAddonScores(client_karts, client_maps);

    // Save available karts and tracks from clients in STKPeer so if this peer
    // disconnects later in lobby it won't affect current players
    peer->setAvailableKartsTracks(client_karts, client_maps);
    peer->setAddonsScores(addons_scores);

    if (m_process_type == PT_CHILD &&
        peer->getHostId() == m_client_server_host_id.load())
    {
        // Update child process addons list too so player can choose later
        getAssetManager()->updateAddons();
        updateMapsForMode();
    }

    if (isTournament())
        getTournament()->updateTournamentRole(peer);
    updatePlayerList();
    return true;
}   // handleAssets

//-----------------------------------------------------------------------------
void ServerLobby::connectionRequested(Event* event)
{
    std::shared_ptr<STKPeer> peer = event->getPeerSP();
    auto conn_packet = event->getPacket<ConnectionRequestedPacket>();
    // if (!checkDataSize(event, 14)) return;

    peer->cleanPlayerProfiles();

    // can we add the player ?
    if (getSettings()->isLegacyGPMode() &&
        (m_state.load() != WAITING_FOR_START_GAME /*||
        m_game_setup->isGrandPrixStarted()*/))
    {
        ConnectionRefusedPacket packet;
        packet.reason = RR_BUSY;
        // send only to the peer that made the request and disconnect it now
        peer->sendPacket(packet, PRM_RELIABLE, PEM_UNENCRYPTED);

        peer->reset();
        Log::verbose("ServerLobby", "Player refused: selection started");
        return;
    }

    // Check server version
    int version = conn_packet.server_version;

    auto& stk_config = STKConfig::get();
    
    if (version < stk_config->m_min_server_version ||
        version > stk_config->m_max_server_version)
    {
        ConnectionRefusedPacket packet;
        packet.reason = RR_INCOMPATIBLE_DATA;
        peer->sendPacket(packet, PRM_RELIABLE, PEM_UNENCRYPTED);

        peer->reset();
        Log::verbose("ServerLobby", "Player refused: wrong server version");
        return;
    }
    std::string user_version = conn_packet.user_agent;
    event->getPeer()->setUserVersion(user_version);

    unsigned list_caps = conn_packet.capabilities_size;
    std::set<std::string> caps;
    for (unsigned i = 0; i < list_caps; i++)
    {
        std::string cap = conn_packet.capabilities[i];
        caps.insert(cap);
    }
    event->getPeer()->setClientCapabilities(caps);

    std::set<std::string> client_karts;
    for (const std::string& item: conn_packet.assets.karts)
        client_karts.insert(item);

    std::set<std::string> client_maps;
    for (const std::string& item: conn_packet.assets.maps)
        client_maps.insert(item);

    if (!handleAssetsAndAddonScores(event->getPeerSP(), client_karts, client_maps))
        return;

    unsigned player_count = conn_packet.player_count;
    uint32_t online_id = 0;
    uint32_t encrypted_size = 0;
    online_id = conn_packet.id;
    encrypted_size = conn_packet.encrypted_size;

    // Will be disconnected if banned by IP
    testBannedForIP(peer);
    if (peer->isDisconnected())
        return;

    testBannedForIPv6(peer);
    if (peer->isDisconnected())
        return;

    if (online_id != 0)
        testBannedForOnlineId(peer, online_id);

    if (peer->isDisconnected())
        return;

    unsigned total_players = 0;
    STKHost::get()->updatePlayers(NULL, NULL, &total_players);
    if (total_players + player_count + m_ai_profiles.size() >
        (unsigned)getSettings()->getServerMaxPlayers())
    {
        ConnectionRefusedPacket packet;
        packet.reason = RR_TOO_MANY_PLAYERS;
        peer->sendPacket(packet, PRM_RELIABLE, PEM_UNENCRYPTED);

        peer->reset();
        Log::verbose("ServerLobby", "Player refused: too many players");
        return;
    }

    // Reject non-validated player joinning if WAN server and not disabled
    // enforcement of validation, unless it's player from localhost or lan
    // And no duplicated online id or split screen players in ranked server
    // AIPeer only from lan and only 1 if ai handling
    std::set<uint32_t> all_online_ids =
        STKHost::get()->getAllPlayerOnlineIds();
    bool duplicated_ranked_player =
        all_online_ids.find(online_id) != all_online_ids.end();

    bool failed_validation =
            (encrypted_size == 0 || online_id == 0) &&
            !(peer->getAddress().isPublicAddressLocalhost() || peer->getAddress().isLAN()) &&
            NetworkConfig::get()->isWAN() &&
            getSettings()->isValidatingPlayer();

    bool failed_strictness =
            getSettings()->hasStrictPlayers() &&
            (player_count != 1 || online_id == 0 || duplicated_ranked_player);

    bool failed_anywhere_ai =
            peer->isAIPeer() &&
            !peer->getAddress().isLAN() &&
            !getSettings()->canConnectAiAnywhere();

    bool failed_unhandled_ai =
            peer->isAIPeer() &&
            getSettings()->hasAiHandling() &&
            !m_ai_peer.expired();

    if (failed_validation || failed_strictness || failed_anywhere_ai || failed_unhandled_ai)
    {
        ConnectionRefusedPacket packet;
        packet.reason = RR_INVALID_PLAYER;
        peer->sendPacket(packet, PRM_RELIABLE, PEM_UNENCRYPTED);

        peer->reset();
        Log::verbose("ServerLobby", "Player refused: invalid player");
        return;
    }

    if (getSettings()->hasAiHandling() && peer->isAIPeer())
        m_ai_peer = peer;

    // Second condition was not present before
    if (encrypted_size != 0 && conn_packet.player_info_encrypted.has_value())
    {
        NetworkString* ns = new NetworkString();
        auto encrypted = conn_packet.player_info_encrypted.get_value();
        encrypted.toNetworkString(ns);
        m_pending_connection[peer] = std::make_pair(online_id, *(BareNetworkString*)ns);
    }
    else
    {
        core::stringw online_name;
        if (online_id > 0 && /* ??? */ conn_packet.player_name.has_value())
            online_name = conn_packet.player_name.get_value();
        handleUnencryptedConnection(peer, conn_packet.player_info_unencrypted, online_id, online_name,
            false/*is_pending_connection*/);
    }
}   // connectionRequested

//-----------------------------------------------------------------------------
void ServerLobby::handleUnencryptedConnection(std::shared_ptr<STKPeer> peer,
    const Optional<RestConnectionRequestPacket>& opt, uint32_t online_id,
    const core::stringw& online_name, bool is_pending_connection,
    std::string country_code)
{
    if (!opt.has_value())
        return;

    RestConnectionRequestPacket packet = opt.get_value();

    // Check for password
    std::string password;
    password = packet.private_server_password;
    const std::string& server_pw = getSettings()->getPrivateServerPassword();
    if (online_id > 0)
    {
        std::string username = StringUtils::wideToUtf8(online_name);
        if (getSettings()->isTempBanned(username))
        {
            ConnectionRefusedPacket packet;
            packet.reason = RR_BANNED;
            packet.message = std::string("Please behave well next time.");
            peer->sendPacket(packet, PRM_RELIABLE, PEM_UNENCRYPTED);

            peer->reset();
            Log::verbose("ServerLobby", "Player refused: invalid player");
            return;
        }
        if (getSettings()->isInWhitelist(username))
            password = server_pw;
    }
    if (password != server_pw)
    {
        ConnectionRefusedPacket packet;
        packet.reason = RR_INCORRECT_PASSWORD;
        peer->sendPacket(packet, PRM_RELIABLE, PEM_UNENCRYPTED);

        peer->reset();
        Log::verbose("ServerLobby", "Player refused: incorrect password");
        return;
    }

    // Check again max players and duplicated player in ranked server,
    // if this is a pending connection
    unsigned total_players = 0;
    unsigned player_count = packet.player_count;

    if (is_pending_connection)
    {
        STKHost::get()->updatePlayers(NULL, NULL, &total_players);
        if (total_players + player_count >
            (unsigned)getSettings()->getServerMaxPlayers())
        {
            ConnectionRefusedPacket packet;
            packet.reason = RR_TOO_MANY_PLAYERS;
            peer->sendPacket(packet, PRM_RELIABLE, PEM_UNENCRYPTED);

            peer->reset();
            Log::verbose("ServerLobby", "Player refused: too many players");
            return;
        }

        std::set<uint32_t> all_online_ids =
            STKHost::get()->getAllPlayerOnlineIds();
        bool duplicated_ranked_player =
            all_online_ids.find(online_id) != all_online_ids.end();
        if (getSettings()->isRanked() && duplicated_ranked_player)
        {
            ConnectionRefusedPacket packet;
            packet.reason = RR_INVALID_PLAYER;
            peer->sendPacket(packet, PRM_RELIABLE, PEM_UNENCRYPTED);

            peer->reset();
            Log::verbose("ServerLobby", "Player refused: invalid player");
            return;
        }
    }

#ifdef ENABLE_SQLITE3
    if (country_code.empty() && !peer->getAddress().isIPv6())
        country_code = getDbConnector()->ip2Country(peer->getAddress());
    if (country_code.empty() && peer->getAddress().isIPv6())
        country_code = getDbConnector()->ipv62Country(peer->getAddress());
#endif

    auto red_blue = STKHost::get()->getAllPlayersTeamInfo();
    std::string utf8_online_name = StringUtils::wideToUtf8(online_name);
    for (unsigned i = 0; i < player_count; i++)
    {
        ConnectingPlayerPacket player_packet = packet.players[i];

        core::stringw name = player_packet.name;
        // 30 to make it consistent with stk-addons max user name length
        if (name.empty())
            name = L"unnamed";
        else if (name.size() > 30)
            name = name.subString(0, 30);

        std::string utf8_name = StringUtils::wideToUtf8(name);
        float default_kart_color = player_packet.default_kart_color;
        HandicapLevel handicap = (HandicapLevel)player_packet.handicap;
        auto player = std::make_shared<NetworkPlayerProfile>
            (peer, i == 0 && !online_name.empty() && !peer->isAIPeer() ?
            online_name : name,
            peer->getHostId(), default_kart_color, i == 0 ? online_id : 0,
            handicap, (uint8_t)i, KART_TEAM_NONE,
            country_code);

        int previous_team = -1;
        std::string username = StringUtils::wideToUtf8(player->getName());
        
        // kimden: I'm not sure why the check is double
        if (getTeamManager()->hasTeam(username))
            previous_team = getTeamManager()->getTeamForUsername(username);

        bool can_change_teams = true;
        if (getTournament() && !getTournament()->canChangeTeam())
            can_change_teams = false;
        if (getSettings()->hasTeamChoosing() && can_change_teams)
        {
            KartTeam cur_team = KART_TEAM_NONE;

            if (RaceManager::get()->teamEnabled())
            {
                if (red_blue.first > red_blue.second)
                {
                    cur_team = KART_TEAM_BLUE;
                    red_blue.second++;
                }
                else
                {
                    cur_team = KART_TEAM_RED;
                    red_blue.first++;
                }
            }
            if (cur_team != KART_TEAM_NONE)
                getTeamManager()->setTeamInLobby(player, cur_team);
        }
        if (isTournament())
        {
            KartTeam team = getTournament()->getTeam(utf8_online_name);
            if (team != KART_TEAM_NONE)
                getTeamManager()->setTeamInLobby(player, team);
        }
        getCommandManager()->addUser(username);
        getGPManager()->setScoresToPlayer(player);
        if (!RaceManager::get()->teamEnabled())
        {
            if (previous_team != -1)
            {
                getTeamManager()->setTemporaryTeamInLobby(player, previous_team);
            }
        }
        peer->addPlayer(player);

        // As it sets spectating mode for peer based on which players it has,
        // we need to set the team once again to the same thing
        getTeamManager()->setTemporaryTeamInLobby(player, player->getTemporaryTeam());
    }

    peer->setValidated(true);

    // send a message to the one that asked to connect
    peer->sendPacket(m_game_setup->addServerInfo());

    const bool game_started = m_state.load() != WAITING_FOR_START_GAME;

    auto& stk_config = STKConfig::get();
    ConnectionAcceptedPacket ack_packet;

    // connection success -- return the host id of peer
    float auto_start_timer = getTimeUntilExpiration();
    ack_packet.host_id = peer->getHostId();
    ack_packet.server_version = ServerConfig::m_server_version;
    ack_packet.capabilities_size = (uint16_t)stk_config->m_network_capabilities.size();
    for (const std::string& cap : stk_config->m_network_capabilities)
        ack_packet.capabilities.push_back(cap);
    
    ack_packet.auto_start_timer = auto_start_timer;
    ack_packet.state_frequency = getSettings()->getStateFrequency();
    ack_packet.chat_allowed = getChatManager()->getChat();
    ack_packet.reports_allowed = playerReportsTableExists();

    peer->setSpectator(false);

    // The 127.* or ::1/128 will be in charged for controlling AI
    if (m_ai_profiles.empty() && peer->getAddress().isLoopback())
    {
        unsigned ai_add = NetworkConfig::get()->getNumFixedAI();
        unsigned max_players = getSettings()->getServerMaxPlayers();
        // We need to reserve at least 1 slot for new player
        if (player_count + ai_add + 1 > max_players)
        {
            if (max_players >= player_count + 1)
                ai_add = max_players - player_count - 1;
            else
                ai_add = 0;
        }
        for (unsigned i = 0; i < ai_add; i++)
        {
#ifdef SERVER_ONLY
            core::stringw name = L"Bot";
#else
            core::stringw name = _("Bot");
#endif
            name += core::stringw(" ") + StringUtils::toWString(i + 1);
            
            m_ai_profiles.push_back(std::make_shared<NetworkPlayerProfile>
                (peer, name, peer->getHostId(), 0.0f, 0, HANDICAP_NONE,
                player_count + i, KART_TEAM_NONE, ""));
        }
    }

    if (game_started)
    {
        peer->setWaitingForGame(true);
        updatePlayerList();
        peer->sendPacket(ack_packet);
    }
    else
    {
        peer->setWaitingForGame(false);
        m_peers_ready[peer] = false;
        if (!getSettings()->hasSqlManagement())
        {
            for (std::shared_ptr<NetworkPlayerProfile>& npp :
                peer->getPlayerProfiles())
            {
                Log::info("ServerLobby",
                    "New player %s with online id %u from %s with %s.",
                    StringUtils::wideToUtf8(npp->getName()).c_str(),
                    npp->getOnlineId(), peer->getAddress().toString().c_str(),
                    peer->getUserVersion().c_str());
            }
        }
        updatePlayerList();
        peer->sendPacket(ack_packet);

        if (getSettings()->isRanked())
        {
            getRankingForPlayer(peer->getMainProfile());
        }
    }

#ifdef ENABLE_SQLITE3
    getDbConnector()->onPlayerJoinQueries(peer, online_id, player_count, country_code);
#endif
    if (getKartElimination()->isEnabled())
    {
        bool hasEliminatedPlayer = false;
        for (unsigned i = 0; i < peer->getPlayerProfiles().size(); ++i)
        {
            std::string name = StringUtils::wideToUtf8(
                    peer->getPlayerProfiles()[i]->getName());
            if (getKartElimination()->isEliminated(name))
            {
                hasEliminatedPlayer = true;
                break;
            }
        }

        // This ns packet wasn't replaced with function immediately, I could mess up then... 
        sendStringToPeer(peer, getKartElimination()->getWarningMessage(hasEliminatedPlayer));
    }
    if (getSettings()->isRecordingReplays())
    {
        std::string msg;
        if (getSettings()->hasConsentOnReplays())
            msg = "Recording ghost replays is enabled. "
                "The crowned player can change that "
                "using /replay 0 (to disable) or /replay 1 (to enable). "
                "Do not race under this feature if you don't want to be recorded.";
        else
            msg = "Recording ghost replays is disabled. "
                "The crowned player can change that "
                "using /replay 0 (to disable) or /replay 1 (to enable). ";
        Comm::sendStringToPeer(peer, msg);
    }
    getMessagesFromHost(peer, online_id);

    if (isTournament())
        getTournament()->updateTournamentRole(peer);
}   // handleUnencryptedConnection

//-----------------------------------------------------------------------------
/** Called when any players change their setting (team for example), or
 *  connection / disconnection, it will use the game_started parameter to
 *  determine if this should be send to all peers in server or just in game.
 *  \param update_when_reset_server If true, this message will be sent to
 *  all peers.
 */
void ServerLobby::updatePlayerList(bool update_when_reset_server)
{
    const bool game_started = m_state.load() != WAITING_FOR_START_GAME &&
        !update_when_reset_server;

    auto all_profiles = STKHost::get()->getAllPlayerProfiles();
    size_t all_profiles_size = all_profiles.size();
    for (auto& profile : all_profiles)
    {
        if (profile->getPeer()->alwaysSpectate())
            all_profiles_size--;
    }

    auto& spectators_by_limit = getCrownManager()->getSpectatorsByLimit(true);

    // N - 1 AI
    auto ai_instance = m_ai_peer.lock();
    if (supportsAI())
    {
        if (ai_instance)
        {
            auto ai_profiles = ai_instance->getPlayerProfiles();
            if (m_state.load() == WAITING_FOR_START_GAME ||
                update_when_reset_server)
            {
                if (all_profiles_size > ai_profiles.size())
                    ai_profiles.clear();
                else if (all_profiles_size != 0)
                {
                    ai_profiles.resize(
                        ai_profiles.size() - all_profiles_size + 1);
                }
            }
            else
            {
                // Use fixed number of AI calculated when started game
                ai_profiles.resize(m_ai_count);
            }
            all_profiles.insert(all_profiles.end(), ai_profiles.begin(),
                ai_profiles.end());
            m_current_ai_count.store((int)ai_profiles.size());
        }
        else if (!m_ai_profiles.empty())
        {
            all_profiles.insert(all_profiles.end(), m_ai_profiles.begin(),
                m_ai_profiles.end());
            m_current_ai_count.store((int)m_ai_profiles.size());
        }
    }
    else
        m_current_ai_count.store(0);

    m_lobby_players.store((int)all_profiles.size());

    // No need to update player list (for started grand prix currently)
    if (getSettings()->isLegacyGPMode() &&
        m_state.load() > WAITING_FOR_START_GAME && !update_when_reset_server)
        return;

    PlayerListPacket list_packet;
    list_packet.game_started = game_started;
    list_packet.all_profiles_size = all_profiles.size();

    for (auto profile : all_profiles)
    {
        PlayerListProfilePacket packet;
        auto profile_name = profile->getDecoratedName(m_name_decorator);

        // get OS information
        auto version_os = StringUtils::extractVersionOS(profile->getPeer()->getUserVersion());
        int angry_host = profile->getPeer()->hammerLevel();
        std::string os_type_str = version_os.second;
        std::string utf8_profile_name = StringUtils::wideToUtf8(profile_name);

        // Add a Mobile emoji for mobile OS
        if (getSettings()->isExposingMobile() &&
                (os_type_str == "iOS" || os_type_str == "Android"))
            profile_name = StringUtils::utf32ToWide({0x1F4F1}) + profile_name;

        // Add an hourglass emoji for players waiting because of the player limit
        if (spectators_by_limit.find(profile->getPeer()) != spectators_by_limit.end())
            profile_name = StringUtils::utf32ToWide({ 0x231B }) + profile_name;

        // Add a hammer emoji for angry host
        for (int i = angry_host; i > 0; --i)
            profile_name = StringUtils::utf32ToWide({0x1F528}) + profile_name;

        std::string prefix = "";
        for (const std::string& category: getTeamManager()->getVisibleCategoriesForPlayer(utf8_profile_name))
        {
            prefix += category + ", ";
        }
        if (!prefix.empty())
        {
            prefix.resize((int)prefix.size() - 2);
            prefix = "[" + prefix + "] ";
        }
        int team = profile->getTemporaryTeam();
        if (team != 0 && !RaceManager::get()->teamEnabled()) {
            prefix = TeamUtils::getTeamByIndex(team).getEmoji() + " " + prefix;
        }

        profile_name = StringUtils::utf8ToWide(prefix) + profile_name;

        packet.host_id         = profile->getHostId();
        packet.online_id       = profile->getOnlineId();
        packet.local_player_id = profile->getLocalPlayerId();
        packet.profile_name    = profile_name;

        std::shared_ptr<STKPeer> p = profile->getPeer();
        uint8_t boolean_combine = 0;
        if (p && p->isWaitingForGame())
            boolean_combine |= 1;
        if (p && (p->isSpectator() ||
            ((m_state.load() == WAITING_FOR_START_GAME ||
            update_when_reset_server) && p->alwaysSpectateButNotNeutral())))
            boolean_combine |= (1 << 1);
        if (p && m_server_owner_id.load() == p->getHostId())
            boolean_combine |= (1 << 2);
        if (getCrownManager()->isOwnerLess() && !game_started &&
            m_peers_ready.find(p) != m_peers_ready.end() &&
            m_peers_ready.at(p))
            boolean_combine |= (1 << 3);
        if ((p && p->isAIPeer()) || isAIProfile(profile))
            boolean_combine |= (1 << 4);
        packet.mask = boolean_combine;
        packet.handicap = profile->getHandicap();
        if (getSettings()->hasTeamChoosing())
            packet.kart_team = profile->getTeam();
        else
            packet.kart_team = KART_TEAM_NONE;
        packet.country_code = profile->getCountryCode();
        list_packet.all_profiles.push_back(packet);
    }

    // Don't send this message to in-game players
    sendPacketToAllPeersWith([game_started]
        (std::shared_ptr<STKPeer> p)
        {
            if (!p->isValidated())
                return false;
            if (!p->isWaitingForGame() && game_started)
                return false;
            return true;
        }, list_packet);
}   // updatePlayerList
//-----------------------------------------------------------------------------

void ServerLobby::updateServerOwner(bool force)
{
    ServerState state = m_state.load();
    if (state < WAITING_FOR_START_GAME || state > RESULT_DISPLAY)
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
        ServerOwnershipPacket packet;
        owner->sendPacket(packet);
    }
    m_server_owner = owner;
    m_server_owner_id.store(owner->getHostId());
    updatePlayerList();
}   // updateServerOwner

//-----------------------------------------------------------------------------
/*! \brief Called when a player asks to select karts.
 *  \param event : Event providing the information.
 */
void ServerLobby::kartSelectionRequested(Event* event)
{
    if (m_state != SELECTING /*|| m_game_setup->isGrandPrixStarted()*/)
    {
        Log::warn("ServerLobby", "Received kart selection while in state %d.",
                  m_state.load());
        return;
    }

    if (!checkDataSize(event, 1) ||
        event->getPeer()->getPlayerProfiles().empty())
        return;

    auto packet = event->getPacket<KartSelectionRequestPacket>();
    std::shared_ptr<STKPeer> peer = event->getPeerSP();
    setPlayerKarts(packet.karts, peer);
}   // kartSelectionRequested

//-----------------------------------------------------------------------------
/*! \brief Called when a player votes for track(s), it will auto correct client
 *         data if it sends some invalid data.
 *  \param event : Event providing the information.
 */
void ServerLobby::handlePlayerVote(Event* event)
{
    if (m_state != SELECTING || !getSettings()->hasTrackVoting())
    {
        Log::warn("ServerLobby", "Received track vote while in state %d.",
                  m_state.load());
        return;
    }

    if (!checkDataSize(event, 4) ||
        event->getPeer()->getPlayerProfiles().empty() ||
        event->getPeer()->isWaitingForGame())
        return;

    if (isVotingOver())  return;

    if (!canVote(event->getPeerSP())) return;

    auto packet = event->getPacket<VoteRequestPacket>();
    PeerVote vote(packet.vote);
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
    vote.m_player_name = event->getPeer()->getMainProfile()->getDecoratedName(m_name_decorator);

    // Now inform all clients about the vote
    VotePacket vote_packet;
    vote_packet.host_id = event->getPeer()->getHostId();
    vote_packet.vote = vote.encode();
    sendPacketToPeers(vote_packet);

}   // handlePlayerVote

// ----------------------------------------------------------------------------
/** Select the track to be used based on all votes being received.
 * \param winner_vote The PeerVote that was picked.
 *  \return True if race can go on, otherwise wait.
 */
bool ServerLobby::handleAllVotes(PeerVote* winner_vote)
{
    // First remove all votes from disconnected hosts
    auto it = m_peers_votes.begin();
    while (it != m_peers_votes.end())
    {
        auto peer = STKHost::get()->findPeerByHostId(it->first);
        if (peer == nullptr)
        {
            it = m_peers_votes.erase(it);
        }
        else
            it++;
    }

    // Count number of players
    unsigned cur_players = 0;
    auto peers = STKHost::get()->getPeers();
    for (auto peer : peers)
    {
        if (peer->isAIPeer())
            continue;
        if (!canVote(peer))
            continue;
        if (peer->hasPlayerProfiles() && !peer->isWaitingForGame())
            cur_players++;
    }

    return getMapVoteHandler()->handleAllVotes(
        m_peers_votes,
        getRemainingVotingTime(),
        getMaxVotingTime(),
        isVotingOver(),
        cur_players,
        winner_vote
    );
}   // handleAllVotes

// ----------------------------------------------------------------------------
/** Called from the RaceManager of the server when the world is loaded. Marks
 *  the server to be ready to start the race.
 */
void ServerLobby::finishedLoadingWorld()
{
    for (auto p : m_peers_ready)
    {
        if (auto peer = p.first.lock())
            peer->updateLastActivity();
    }
    m_server_has_loaded_world.store(true);
}   // finishedLoadingWorld;

//-----------------------------------------------------------------------------
/** Called when a client notifies the server that it has loaded the world.
 *  When all clients and the server are ready, the race can be started.
 */
void ServerLobby::finishedLoadingWorldClient(Event *event)
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
void ServerLobby::playerFinishedResult(Event *event)
{
    if (m_rs_state.load() == RS_ASYNC_RESET ||
        m_state.load() != RESULT_DISPLAY)
        return;
    std::shared_ptr<STKPeer> peer = event->getPeerSP();
    m_peers_ready.at(peer) = true;
}   // playerFinishedResult

//-----------------------------------------------------------------------------
bool ServerLobby::waitingForPlayers() const
{
    if (getSettings()->isLegacyGPMode() && getSettings()->isLegacyGPModeStarted())
        return false;
    return m_state.load() >= WAITING_FOR_START_GAME;
}   // waitingForPlayers

//-----------------------------------------------------------------------------
void ServerLobby::handlePendingConnection()
{
    std::lock_guard<std::mutex> lock(m_keys_mutex);

    for (auto it = m_pending_connection.begin();
         it != m_pending_connection.end();)
    {
        auto peer = it->first.lock();
        if (!peer)
        {
            it = m_pending_connection.erase(it);
        }
        else
        {
            const uint32_t online_id = it->second.first;
            auto key = m_keys.find(online_id);
            if (key != m_keys.end() && key->second.m_tried == false)
            {
                try
                {
                    if (decryptConnectionRequest(peer, it->second.second,
                        key->second.m_aes_key, key->second.m_aes_iv, online_id,
                        key->second.m_name, key->second.m_country_code))
                    {
                        it = m_pending_connection.erase(it);
                        m_keys.erase(online_id);
                        continue;
                    }
                    else
                        key->second.m_tried = true;
                }
                catch (std::exception& e)
                {
                    Log::error("ServerLobby",
                        "handlePendingConnection error: %s", e.what());
                    key->second.m_tried = true;
                }
            }
            it++;
        }
    }
}   // handlePendingConnection

//-----------------------------------------------------------------------------
bool ServerLobby::decryptConnectionRequest(std::shared_ptr<STKPeer> peer,
    BareNetworkString& data, const std::string& key, const std::string& iv,
    uint32_t online_id, const core::stringw& online_name,
    const std::string& country_code)
{
    auto crypto = std::unique_ptr<Crypto>(new Crypto(
        Crypto::decode64(key), Crypto::decode64(iv)));
    if (crypto->decryptConnectionRequest(data))
    {
        peer->setCrypto(std::move(crypto));

        RestConnectionRequestPacket packet;
        NetworkString* ns = new NetworkString();
        ns->operator+=(data);
        try
        {
            packet.fromNetworkString(ns);
            delete ns;
        }
        catch (...)
        {
            return false;
        }
        Optional<RestConnectionRequestPacket> opt;
        opt = packet;
        Log::info("ServerLobby", "%s validated",
            StringUtils::wideToUtf8(online_name).c_str());
        handleUnencryptedConnection(peer, opt, online_id,
            online_name, true/*is_pending_connection*/, country_code);
        return true;
    }
    return false;
}   // decryptConnectionRequest

//-----------------------------------------------------------------------------
void ServerLobby::getRankingForPlayer(std::shared_ptr<NetworkPlayerProfile> p)
{
    int priority = Online::RequestManager::HTTP_MAX_PRIORITY;
    auto request = std::make_shared<Online::XMLRequest>(priority);
    NetworkConfig::get()->setUserDetails(request, "get-ranking");

    const uint32_t id = p->getOnlineId();
    request->addParameter("id", id);
    request->executeNow();

    const XMLNode* result = request->getXMLData();
    std::string rec_success;

    bool success = false;
    if (result->get("success", &rec_success))
        if (rec_success == "yes")
            success = true;

    if (!success)
    {
        Log::error("ServerLobby", "No ranking info found for player %s.",
            StringUtils::wideToUtf8(p->getName()).c_str());
        // Kick the player to avoid his score being reset in case
        // connection to stk addons is broken
        auto peer = p->getPeer();
        if (peer)
        {
            peer->kick();
            return;
        }
    }
    m_ranking->fill(id, result, p);
}   // getRankingForPlayer

//-----------------------------------------------------------------------------
void ServerLobby::submitRankingsToAddons()
{
    // No ranking for battle mode
    if (!RaceManager::get()->modeHasLaps())
        return;

    for (unsigned i = 0; i < RaceManager::get()->getNumPlayers(); i++)
    {
        const uint32_t id = RaceManager::get()->getKartInfo(i).getOnlineId();
        const RankingEntry& scores = m_ranking->getScores(id);
        auto request = std::make_shared<SubmitRankingRequest>
            (scores, RaceManager::get()->getKartInfo(i).getCountryCode());
        NetworkConfig::get()->setUserDetails(request, "submit-ranking");
        Log::info("ServerLobby", "Submitting ranking for %s (%d) : %lf, %lf %d",
            StringUtils::wideToUtf8(
            RaceManager::get()->getKartInfo(i).getPlayerName()).c_str(), id,
            scores.score, scores.max_score, scores.races);
        request->queue();
    }
}   // submitRankingsToAddons

//-----------------------------------------------------------------------------
/** This function is called when all clients have loaded the world and
 *  are therefore ready to start the race. It determine the start time in
 *  network timer for client and server based on pings and then switches state
 *  to WAIT_FOR_RACE_STARTED.
 */
void ServerLobby::configPeersStartTime()
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

    StartGamePacket packet;
    packet.start_time = start_time;
    packet.check_count = (uint8_t)Track::getCurrentTrack()->getCheckManager()->getCheckStructureCount();
    packet.nim_complete_state = m_nim_complete_state; // was operator +=
    sendPacketToPeers(packet);

    m_client_starting_time = start_time;

    const unsigned jitter_tolerance = getSettings()->getJitterTolerance();
    Log::info("ServerLobby", "Max ping from peers: %d, jitter tolerance: %d",
        max_ping, jitter_tolerance);
    // Delay server for max ping / 2 from peers and jitter tolerance.
    m_server_delay = (uint64_t)(max_ping / 2) + (uint64_t)jitter_tolerance;
    start_time += m_server_delay;
    m_server_started_at = start_time;
    m_state = WAIT_FOR_RACE_STARTED;

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
            m_state.store(RACING);
        });
}   // configPeersStartTime

//-----------------------------------------------------------------------------
void ServerLobby::addWaitingPlayersToGame()
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
        uint32_t online_id = profile->getOnlineId();
        if (getSettings()->isRanked() && !m_ranking->has(online_id))
        {
            getRankingForPlayer(peer->getMainProfile());
        }
    }
    // Re-activiate the ai
    if (auto ai = m_ai_peer.lock())
        ai->setValidated(true);
}   // addWaitingPlayersToGame

//-----------------------------------------------------------------------------
void ServerLobby::resetServer()
{
    addWaitingPlayersToGame();
    resetPeersReady();
    updatePlayerList(true/*update_when_reset_server*/);

    sendPacketToPeersInServer(m_game_setup->addServerInfo());
    
    setup();
    m_state = NetworkConfig::get()->isLAN() ?
        WAITING_FOR_START_GAME : REGISTER_SELF_ADDRESS;

    if (m_state.load() == WAITING_FOR_START_GAME)
        if (m_reset_to_default_mode_later.exchange(false))
            handleServerConfiguration(NULL);

    updatePlayerList();
}   // resetServer

//-----------------------------------------------------------------------------
void ServerLobby::testBannedForIP(std::shared_ptr<STKPeer> peer) const
{
#ifdef ENABLE_SQLITE3
    auto db_connector = getDbConnector();
    if (!db_connector->hasDatabase() || !db_connector->hasIpBanTable())
        return;

    // Test for IPv4
    if (peer->getAddress().isIPv6())
        return;

    bool is_banned = false;
    uint32_t ip_start = 0;
    uint32_t ip_end = 0;

    std::vector<DatabaseConnector::IpBanTableData> ip_ban_list =
            db_connector->getIpBanTableData(peer->getAddress().getIP());
    if (!ip_ban_list.empty())
    {
        is_banned = true;
        ip_start = ip_ban_list[0].ip_start;
        ip_end = ip_ban_list[0].ip_end;
        int row_id = ip_ban_list[0].row_id;
        std::string reason = ip_ban_list[0].reason;
        std::string description = ip_ban_list[0].description;
        Log::info("ServerLobby", "%s banned by IP: %s "
                "(rowid: %d, description: %s).",
                peer->getAddress().toString().c_str(), reason.c_str(), row_id, description.c_str());
        kickPlayerWithReason(peer, reason.c_str());
    }
    if (is_banned)
        db_connector->increaseIpBanTriggerCount(ip_start, ip_end);
#endif
}   // testBannedForIP

//-----------------------------------------------------------------------------
void ServerLobby::getMessagesFromHost(std::shared_ptr<STKPeer> peer, int online_id)
{
#ifdef ENABLE_SQLITE3
    auto db_connector = getDbConnector();
    std::vector<DatabaseConnector::ServerMessage> messages =
            db_connector->getServerMessages(online_id);
    // in case there will be more than one later
    for (const auto& message: messages)
    {
        Log::info("ServerLobby", "A message from server was delivered");
        Comm::sendStringToPeer(peer, "A message from the server (" +
            std::string(message.timestamp) + "):\n" + std::string(message.message));
        db_connector->deleteServerMessage(message.row_id);
    }
#endif
}   // getMessagesFromHost

//-----------------------------------------------------------------------------
void ServerLobby::testBannedForIPv6(std::shared_ptr<STKPeer> peer) const
{
#ifdef ENABLE_SQLITE3
    auto db_connector = getDbConnector();
    if (!db_connector->hasDatabase() || !db_connector->hasIpv6BanTable())
        return;

    // Test for IPv6
    if (!peer->getAddress().isIPv6())
        return;

    bool is_banned = false;
    std::string ipv6_cidr = "";

    std::vector<DatabaseConnector::Ipv6BanTableData> ipv6_ban_list =
            db_connector->getIpv6BanTableData(peer->getAddress().toString(false));

    if (!ipv6_ban_list.empty())
    {
        is_banned = true;
        ipv6_cidr = ipv6_ban_list[0].ipv6_cidr;
        int row_id = ipv6_ban_list[0].row_id;
        std::string reason = ipv6_ban_list[0].reason;
        std::string description = ipv6_ban_list[0].description;
        Log::info("ServerLobby", "%s banned by IPv6: %s "
                "(rowid: %d, description: %s).",
                peer->getAddress().toString(false).c_str(), reason.c_str(), row_id, description.c_str());
        kickPlayerWithReason(peer, reason.c_str());
    }
    if (is_banned)
        db_connector->increaseIpv6BanTriggerCount(ipv6_cidr);
#endif
}   // testBannedForIPv6

//-----------------------------------------------------------------------------
void ServerLobby::testBannedForOnlineId(std::shared_ptr<STKPeer> peer,
                                        uint32_t online_id) const
{
#ifdef ENABLE_SQLITE3
    auto db_connector = getDbConnector();
    if (!db_connector->hasDatabase() || !db_connector->hasOnlineIdBanTable())
        return;

    bool is_banned = false;
    std::vector<DatabaseConnector::OnlineIdBanTableData> online_id_ban_list =
            db_connector->getOnlineIdBanTableData(online_id);

    if (!online_id_ban_list.empty())
    {
        is_banned = true;
        int row_id = online_id_ban_list[0].row_id;
        std::string reason = online_id_ban_list[0].reason;
        std::string description = online_id_ban_list[0].description;
        Log::info("ServerLobby", "%s banned by online id: %s "
                "(online id: %u, rowid: %d, description: %s).",
                peer->getAddress().toString().c_str(), reason.c_str(), online_id, row_id, description.c_str());
        kickPlayerWithReason(peer, reason.c_str());
    }
    if (is_banned)
        db_connector->increaseOnlineIdBanTriggerCount(online_id);
#endif
}   // testBannedForOnlineId

//-----------------------------------------------------------------------------
void ServerLobby::listBanTable()
{
#ifdef ENABLE_SQLITE3
    getDbConnector()->listBanTable();
#endif
}   // listBanTable

//-----------------------------------------------------------------------------
float ServerLobby::getStartupBoostOrPenaltyForKart(uint32_t ping,
                                                   unsigned kart_id)
{
    AbstractKart* k = World::getWorld()->getKart(kart_id);
    if (k->getStartupBoost() != 0.0f)
        return k->getStartupBoost();
    uint64_t now = STKHost::get()->getNetworkTimer();
    uint64_t client_time = now - ping / 2;
    uint64_t server_time = client_time + m_server_delay;

    auto& stk_config = STKConfig::get();

    int ticks = stk_config->time2Ticks(
        (float)(server_time - m_server_started_at) / 1000.0f);
    if (ticks < stk_config->time2Ticks(1.0f))
    {
        PlayerController* pc =
            dynamic_cast<PlayerController*>(k->getController());
        pc->displayPenaltyWarning();
        return -1.0f;
    }
    float f = k->getStartupBoostFromStartTicks(ticks);
    k->setStartupBoost(f);
    return f;
}   // getStartupBoostOrPenaltyForKart

//-----------------------------------------------------------------------------

void ServerLobby::handleServerConfiguration(std::shared_ptr<STKPeer> peer,
    int difficulty, int mode, bool soccer_goal_target)
{
    if (m_state != WAITING_FOR_START_GAME)
    {
        Log::warn("ServerLobby",
            "Received handleServerConfiguration while being in state %d.",
            m_state.load());
        return;
    }
    if (!getSettings()->isServerConfigurable())
    {
        Log::warn("ServerLobby", "server-configurable is not enabled.");
        return;
    }
    bool teams_before = RaceManager::get()->teamEnabled();
    unsigned total_players = 0;
    STKHost::get()->updatePlayers(NULL, NULL, &total_players);
    bool bad_mode = !getSettings()->isModeAvailable(mode);
    bool bad_difficulty = !getSettings()->isDifficultyAvailable(difficulty);
    if (bad_mode || bad_difficulty)
    {
        // It remains just in case, but kimden thinks that
        // this is already covered in command manager (?)
        Log::error("ServerLobby", "Mode %d and/or difficulty %d are not permitted.",
                   difficulty, mode);
        std::string msg = "";
        if (bad_mode && bad_difficulty)
            msg = "Both mode and difficulty are not permitted on this server";
        else if (bad_mode)
            msg = "This mode is not permitted on this server";
        else
            msg = "This difficulty is not permitted on this server";
        Comm::sendStringToPeer(peer, msg);
        return;
    }
    auto modes = ServerConfig::getLocalGameMode(mode);
    if (modes.second == RaceManager::MAJOR_MODE_GRAND_PRIX)
    {
        Log::warn("ServerLobby", "Grand prix is used for new mode.");
        return;
    }

    RaceManager::get()->setMinorMode(modes.first);
    RaceManager::get()->setMajorMode(modes.second);
    RaceManager::get()->setDifficulty(RaceManager::Difficulty(difficulty));
    m_game_setup->resetExtraServerInfo();
    if (RaceManager::get()->getMinorMode() == RaceManager::MINOR_MODE_SOCCER)
        m_game_setup->setSoccerGoalTarget(soccer_goal_target);

    bool teams_after = RaceManager::get()->teamEnabled();

    if (NetworkConfig::get()->isWAN() &&
        (m_difficulty.load() != difficulty ||
        m_game_mode.load() != mode))
    {
        Log::info("ServerLobby", "Updating server info with new "
            "difficulty: %d, game mode: %d to stk-addons.", difficulty,
            mode);
        int priority = Online::RequestManager::HTTP_MAX_PRIORITY;
        auto request = std::make_shared<Online::XMLRequest>(priority);
        NetworkConfig::get()->setServerDetails(request, "update-config");
        const SocketAddress& addr = STKHost::get()->getPublicAddress();
        request->addParameter("address", addr.getIP());
        request->addParameter("port", addr.getPort());
        request->addParameter("new-difficulty", difficulty);
        request->addParameter("new-game-mode", mode);
        request->queue();
    }
    m_difficulty.store(difficulty);
    m_game_mode.store(mode);
    updateMapsForMode();

    getSettings()->onServerConfiguration();

    auto peers = STKHost::get()->getPeers();
    for (auto& peer : peers)
    {
        auto assets = peer->getClientAssets();
        if (!peer->isValidated() || assets.second.empty()) // this check will fail hard when I introduce vavriable limits
            continue;

        if (getAssetManager()->checkIfNoCommonMaps(assets))
        {
            ConnectionRefusedPacket packet;
            packet.reason = RR_INCOMPATIBLE_DATA;
            peer->sendPacket(packet, PRM_RELIABLE);

            peer->cleanPlayerProfiles();
            peer->reset();
            Log::verbose("ServerLobby",
                "Player has incompatible tracks for new game mode.");
        }
    }

    if (teams_before ^ teams_after)
    {
        if (teams_after)
        {
            int final_number, players_number;
            getTeamManager()->clearTeams();
            getTeamManager()->assignRandomTeams(2, &final_number, &players_number);
        }
        else
        {
            getTeamManager()->clearTemporaryTeams();
        }
        for (auto& peer : STKHost::get()->getPeers())
        {
            if (teams_before)
                peer->resetNoTeamSpectate();
            for (auto &profile: peer->getPlayerProfiles())
            {
                // updates KartTeam
                getTeamManager()->setTemporaryTeamInLobby(profile, profile->getTemporaryTeam());
            }
        }
    }

    sendServerInfoToEveryone();

    updatePlayerList();

    if (getKartElimination()->isEnabled() &&
        RaceManager::get()->getMinorMode() != RaceManager::MINOR_MODE_NORMAL_RACE &&
        RaceManager::get()->getMinorMode() != RaceManager::MINOR_MODE_TIME_TRIAL)
    {
        getKartElimination()->disable();
        Comm::sendStringToAllPeers("Gnu Elimination is disabled because of non-racing mode");
    }
}   // handleServerConfiguration
//-----------------------------------------------------------------------------
/*! \brief Called when the server owner request to change game mode or
 *         difficulty.
 *  \param event : Event providing the information.
 *
 *  Format of the data :
 *  Byte 0            1            2
 *       -----------------------------------------------
 *  Size |     1      |     1     |         1          |
 *  Data | difficulty | game mode | soccer goal target |
 *       -----------------------------------------------
 */
void ServerLobby::handleServerConfiguration(Event* event)
{
    if (event != NULL && event->getPeerSP() != m_server_owner.lock())
    {
        Log::warn("ServerLobby",
            "Client %d is not authorised to config server.",
            event->getPeer()->getHostId());
        return;
    }
    int new_difficulty = getSettings()->getServerDifficulty();
    int new_game_mode = getSettings()->getServerMode();
    bool new_soccer_goal_target = getSettings()->isSoccerGoalTargetInConfig();
    if (event != NULL)
    {
        auto packet = event->getPacket<ConfigServerPacket>();
        new_difficulty = packet.difficulty;
        new_game_mode = packet.game_mode;
        new_soccer_goal_target = packet.soccer_goal_target;
    }
    handleServerConfiguration(
        (event ? event->getPeerSP() : std::shared_ptr<STKPeer>()),
        new_difficulty, new_game_mode, new_soccer_goal_target);
}   // handleServerConfiguration

//-----------------------------------------------------------------------------
/*! \brief Called when a player want to change his handicap
 *  \param event : Event providing the information.
 *
 *  Format of the data :
 *  Byte 0                 1
 *       ----------------------------------
 *  Size |       1         |       1      |
 *  Data | local player id | new handicap |
 *       ----------------------------------
 */
void ServerLobby::changeHandicap(Event* event)
{
    auto packet = event->getPacket<ChangeHandicapPacket>();

    if (m_state.load() != WAITING_FOR_START_GAME &&
        !event->getPeer()->isWaitingForGame())
    {
        Log::warn("ServerLobby", "Set handicap at wrong time.");
        return;
    }

    uint8_t local_id = packet.local_id;
    uint8_t handicap_id = packet.handicap;
    auto& player = event->getPeer()->getPlayerProfiles().at(local_id);
    if (handicap_id >= HANDICAP_COUNT)
    {
        Log::warn("ServerLobby", "Wrong handicap %d.", handicap_id);
        return;
    }

    HandicapLevel h = (HandicapLevel)handicap_id;
    player->setHandicap(h);
    updatePlayerList();
}   // changeHandicap

//-----------------------------------------------------------------------------
/** Update and see if any player disconnects, if so eliminate the kart in
 *  world, so this function must be called in main thread.
 */
void ServerLobby::handlePlayerDisconnection() const
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
void ServerLobby::addLiveJoinPlaceholder(
    std::vector<std::shared_ptr<NetworkPlayerProfile> >& players) const
{
    if (!getSettings()->isLivePlayers() || !RaceManager::get()->supportsLiveJoining())
        return;
    if (RaceManager::get()->getMinorMode() == RaceManager::MINOR_MODE_FREE_FOR_ALL)
    {
        Track* t = TrackManager::get()->getTrack(m_game_setup->getCurrentTrack());
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
void ServerLobby::setPlayerKarts(const PlayerKartsPacket& packet, std::shared_ptr<STKPeer> peer) const
{
    unsigned player_count = packet.players_count;
    player_count = std::min(player_count, (unsigned)peer->getPlayerProfiles().size());
    for (unsigned i = 0; i < player_count; i++)
    {
        std::string kart = packet.karts[i];
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
    /* packet.kart_data should be a pointer later*/
    if (peer->getClientCapabilities().find("real_addon_karts") ==
        peer->getClientCapabilities().end() || packet.kart_data.empty())
        return;
    for (unsigned i = 0; i < player_count; i++)
    {
        KartData kart_data(packet.kart_data[i]);
        std::string type = kart_data.m_kart_type;
        auto& player = peer->getPlayerProfiles()[i];
        const std::string& kart_id = player->getKartName();
        setKartDataProperly(kart_data, kart_id, player, type);
    }
}   // setPlayerKarts

//-----------------------------------------------------------------------------
/** Tell the client \ref RemoteKartInfo of a player when some player joining
 *  live.
 */
void ServerLobby::handleKartInfo(Event* event)
{
    World* w = World::getWorld();
    if (!w)
        return;

    std::shared_ptr<STKPeer> peer = event->getPeerSP();
    auto request_packet = event->getPacket<KartInfoRequestPacket>();
    uint8_t kart_id = request_packet.kart_id;
    if (kart_id > RaceManager::get()->getNumPlayers())
        return;

    AbstractKart* k = w->getKart(kart_id);
    int live_join_util_ticks = k->getLiveJoinUntilTicks();

    const RemoteKartInfo& rki = RaceManager::get()->getKartInfo(kart_id);

    KartInfoPacket packet;
    packet.live_join_util_ticks = live_join_util_ticks;
    packet.kart_id = kart_id;
    packet.player_name = rki.getPlayerName();
    packet.host_id = rki.getHostId();
    packet.default_kart_color = rki.getDefaultKartColor();
    packet.online_id = rki.getOnlineId();
    packet.handicap = rki.getHandicap();
    packet.local_player_id = (uint8_t)rki.getLocalPlayerId();
    packet.kart_name = rki.getKartName();
    packet.country_code = rki.getCountryCode();

    if (peer->getClientCapabilities().find("real_addon_karts") !=
        peer->getClientCapabilities().end())
        packet.kart_data = rki.getKartData().encode();

    peer->sendPacket(packet);


    FreeForAll* ffa_world = dynamic_cast<FreeForAll*>(World::getWorld());
    if (ffa_world)
        ffa_world->notifyAboutScoreIfNonzero(kart_id);
}   // handleKartInfo

//-----------------------------------------------------------------------------
/** Client if currently in-game (including spectator) wants to go back to
 *  lobby.
 */
void ServerLobby::clientInGameWantsToBackLobby(Event* event)
{
    World* w = World::getWorld();
    std::shared_ptr<STKPeer> peer = event->getPeerSP();

    if (!w || !worldIsActive() || peer->isWaitingForGame())
    {
        Log::warn("ServerLobby", "%s try to leave the game at wrong time.",
            peer->getAddress().toString().c_str());
        return;
    }

    if (m_process_type == PT_CHILD &&
        event->getPeer()->getHostId() == m_client_server_host_id.load())
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

        BackLobbyPacket packet;
        packet.reason = BLR_SERVER_OWNER_QUIT_THE_GAME;
        sendPacketToPeersInServer(packet);

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

    BackLobbyPacket packet;
    packet.reason = BLR_NONE;
    peer->sendPacket(packet, PRM_RELIABLE);

    updatePlayerList();

    peer->sendPacket(m_game_setup->addServerInfo());
}   // clientInGameWantsToBackLobby

//-----------------------------------------------------------------------------
/** Client if currently select assets wants to go back to lobby.
 */
void ServerLobby::clientSelectingAssetsWantsToBackLobby(Event* event)
{
    std::shared_ptr<STKPeer> peer = event->getPeerSP();

    if (m_state.load() != SELECTING || peer->isWaitingForGame())
    {
        Log::warn("ServerLobby",
            "%s try to leave selecting assets at wrong time.",
            peer->getAddress().toString().c_str());
        return;
    }

    if (m_process_type == PT_CHILD &&
        event->getPeer()->getHostId() == m_client_server_host_id.load())
    {
        BackLobbyPacket packet;
        packet.reason = BLR_SERVER_OWNER_QUIT_THE_GAME;
        sendPacketToPeersInServer(packet);

        resetVotingTime();
        resetServer();
        m_rs_state.store(RS_NONE);
        return;
    }

    m_peers_ready.erase(peer);
    peer->setWaitingForGame(true);
    peer->setSpectator(false);

    BackLobbyPacket packet;
    packet.reason = BLR_NONE;
    peer->sendPacket(packet);

    updatePlayerList();

    peer->sendPacket(m_game_setup->addServerInfo());
}   // clientSelectingAssetsWantsToBackLobby

//-----------------------------------------------------------------------------
void ServerLobby::saveInitialItems(std::shared_ptr<NetworkItemManager> nim)
{
    // there was m_nim_complete_state->getBuffer().clear(); here
    m_nim_complete_state = nim->saveCompleteState();
}   // saveInitialItems

//-----------------------------------------------------------------------------
bool ServerLobby::supportsAI()
{
    return getGameMode() == 3 || getGameMode() == 4;
}   // supportsAI

//-----------------------------------------------------------------------------
bool ServerLobby::checkPeersReady(bool ignore_ai_peer, SelectionPhase phase)
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
void ServerLobby::handleServerCommand(Event* event)
{
    std::shared_ptr<STKPeer> peer = event->getPeerSP();

    if (peer.get())
        peer->updateLastActivity();
    
    getCommandManager()->handleCommand(event, peer);
}   // handleServerCommand
//-----------------------------------------------------------------------------

void ServerLobby::resetToDefaultSettings()
{
    if (getSettings()->isServerConfigurable() && !getSettings()->isPreservingMode())
    {
        if (m_state == WAITING_FOR_START_GAME)
            handleServerConfiguration(NULL);
        else
            m_reset_to_default_mode_later.store(true);
    }

    getSettings()->onResetToDefaultSettings();
}  // resetToDefaultSettings
//-----------------------------------------------------------------------------

void ServerLobby::writeOwnReport(std::shared_ptr<STKPeer> reporter, std::shared_ptr<STKPeer> reporting, const std::string& info)
{
#ifdef ENABLE_SQLITE3
    auto db_connector = getDbConnector();
    if (!db_connector->hasDatabase() || !db_connector->hasPlayerReportsTable())
        return;
    if (!reporting)
        reporting = reporter;
    if (!reporter->hasPlayerProfiles())
        return;
    auto reporter_npp = reporter->getMainProfile();

    if (info.empty())
        return;
    auto info_w = StringUtils::utf8ToWide(info);

    if (!reporting->hasPlayerProfiles())
        return;
    auto reporting_npp = reporting->getMainProfile();

    bool written = db_connector->writeReport(reporter, reporter_npp,
            reporting, reporting_npp, info_w);
    if (written)
    {
        ReportSuccessPacket packet;
        packet.success = 1;
        if (reporter == reporting)
            packet.reported_name = StringUtils::utf8ToWide(m_game_setup->getServerNameUtf8());
        else
            packet.reported_name = reporting_npp->getName();

        reporter->sendPacket(packet);
    }
#endif
}   // writeOwnReport
//-----------------------------------------------------------------------------

void ServerLobby::sendStringToPeer(std::shared_ptr<STKPeer> peer, const std::string& s)
{
    if (!peer)
    {
        sendStringToAllPeers(s);
        return;
    }
    ChatPacket packet;
    packet.message = StringUtils::utf8ToWide(s);
    peer->sendPacket(packet);
}   // sendStringToPeer
//-----------------------------------------------------------------------------

void ServerLobby::sendStringToAllPeers(const std::string& s)
{
    ChatPacket packet;
    packet.message = StringUtils::utf8ToWide(s);
    sendPacketToPeers(packet);
}   // sendStringToAllPeers
//-----------------------------------------------------------------------------

std::string ServerLobby::encodeProfileNameForPeer(
    std::shared_ptr<NetworkPlayerProfile> npp,
    STKPeer* peer)
{
    if (npp)
        return StringUtils::wideToUtf8(npp->getDecoratedName(m_name_decorator));
    return "";
}   // encodeProfileNameForPeer
//-----------------------------------------------------------------------------

bool ServerLobby::canVote(std::shared_ptr<STKPeer> peer) const
{
    if (!peer || peer->getPlayerProfiles().empty())
        return false;

    if (!isTournament())
        return true;

    return getTournament()->canVote(peer);
}   // canVote
//-----------------------------------------------------------------------------

bool ServerLobby::hasHostRights(std::shared_ptr<STKPeer> peer) const
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

int ServerLobby::getPermissions(std::shared_ptr<STKPeer> peer) const
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

bool ServerLobby::writeOnePlayerReport(std::shared_ptr<STKPeer> reporter,
    const std::string& table, const std::string& info)
{
#ifdef ENABLE_SQLITE3
    auto db_connector = getDbConnector();
    if (!db_connector->hasDatabase())
        return false;
    if (!db_connector->hasPlayerReportsTable())
        return false;
    if (!reporter->hasPlayerProfiles())
        return false;
    auto reporter_npp = reporter->getMainProfile();
    auto info_w = StringUtils::utf8ToWide(info);

    bool written = db_connector->writeReport(reporter, reporter_npp,
            reporter, reporter_npp, info_w);
    return written;
#else
    return false;
#endif
}   // writeOnePlayerReport
//-----------------------------------------------------------------------------   
void ServerLobby::changeLimitForTournament(bool goal_target)
{
    m_game_setup->setSoccerGoalTarget(goal_target);
    sendServerInfoToEveryone();
    updatePlayerList();
}   // changeLimitForTournament
//-----------------------------------------------------------------------------

#ifdef ENABLE_SQLITE3
std::string ServerLobby::getRecord(std::string& track, std::string& mode,
    std::string& direction, int laps)
{
    std::string powerup_string = powerup_manager->getFileName();
    std::string kc_string = kart_properties_manager->getFileName();
    GameInfo temp_info;
    temp_info.m_venue = track;
    temp_info.m_reverse = direction;
    temp_info.m_mode = mode;
    temp_info.m_value_limit = laps;
    temp_info.m_time_limit = 0;
    temp_info.m_difficulty = getDifficulty();
    temp_info.setPowerupString(powerup_manager->getFileName());
    temp_info.setKartCharString(kart_properties_manager->getFileName());

    bool record_fetched = false;
    bool record_exists = false;
    double best_result = 0.0;
    std::string best_user = "";

    record_fetched = getDbConnector()->getBestResult(temp_info, &record_exists, &best_user, &best_result);

    if (!record_fetched)
        return "Failed to make a query";
    if (!record_exists)
        return "No time set yet. Or there is a typo.";

    std::string message = StringUtils::insertValues(
        "The record is %s by %s",
        StringUtils::timeToString(best_result),
        best_user);
    return message;
}   // getRecord
#endif
//-----------------------------------------------------------------------------

bool ServerLobby::isSoccerGoalTarget() const
{
    return m_game_setup->isSoccerGoalTarget();
}   // isSoccerGoalTarget
//-----------------------------------------------------------------------------

void ServerLobby::setKartDataProperly(KartData& kart_data, const std::string& kart_name,
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

bool ServerLobby::playerReportsTableExists() const
{
#ifdef ENABLE_SQLITE3
    return getDbConnector()->hasPlayerReportsTable();
#else
    return false;
#endif
}   // playerReportsTableExists

//-----------------------------------------------------------------------------

void ServerLobby::sendServerInfoToEveryone() const
{
    sendPacketToPeers(m_game_setup->addServerInfo());
}   // sendServerInfoToEveryone
//-----------------------------------------------------------------------------

bool ServerLobby::isLegacyGPMode() const
{
    return getSettings()->isLegacyGPMode();
}   // isLegacyGPMode
//-----------------------------------------------------------------------------

int ServerLobby::getCurrentStateScope()
{
    auto state = m_state.load();
    if (state < WAITING_FOR_START_GAME
        || state > RESULT_DISPLAY)
        return 0;
    if (state == WAITING_FOR_START_GAME)
        return CommandManager::StateScope::SS_LOBBY;
    return CommandManager::StateScope::SS_INGAME;
}   // getCurrentStateScope
//-----------------------------------------------------------------------------

bool ServerLobby::isClientServerHost(const std::shared_ptr<STKPeer>& peer)
{
    return peer->getHostId() == m_client_server_host_id.load();
}   // isClientServerHost
//-----------------------------------------------------------------------------

void ServerLobby::setTimeoutFromNow(int seconds)
{
    m_timeout.store((int64_t)StkTime::getMonoTimeMs() +
            (int64_t)(seconds * 1000.0f));
}   // setTimeoutFromNow
//-----------------------------------------------------------------------------

void ServerLobby::setInfiniteTimeout()
{
    m_timeout.store(std::numeric_limits<int64_t>::max());
}   // setInfiniteTimeout
//-----------------------------------------------------------------------------

bool ServerLobby::isInfiniteTimeout() const
{
    return m_timeout.load() == std::numeric_limits<int64_t>::max();
}   // isInfiniteTimeout
//-----------------------------------------------------------------------------

bool ServerLobby::isTimeoutExpired() const
{
    return m_timeout.load() < (int64_t)StkTime::getMonoTimeMs();
}   // isTimeoutExpired
//-----------------------------------------------------------------------------

float ServerLobby::getTimeUntilExpiration() const
{
    int64_t timeout = m_timeout.load();
    if (timeout == std::numeric_limits<int64_t>::max())
        return std::numeric_limits<float>::max();

    return (timeout - (int64_t)StkTime::getMonoTimeMs()) / 1000.0f;
}   // getTimeUntilExpiration
//-----------------------------------------------------------------------------

void ServerLobby::onSpectatorStatusChange(const std::shared_ptr<STKPeer>& peer)
{
    auto state = m_state.load();
    if (state >= ServerLobby::SELECTING && state < ServerLobby::RACING)
    {
        erasePeerReady(peer);
        peer->setWaitingForGame(true);
    }
}   // onSpectatorStatusChange
//-----------------------------------------------------------------------------
