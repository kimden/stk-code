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
// #include "online/xml_request.hpp"
#include "tracks/check_manager.hpp"
#include "tracks/track.hpp"
#include "tracks/track_manager.hpp"
#include "utils/chat_manager.hpp"
#include "utils/kart_elimination.hpp"
#include "utils/game_info.hpp"
#include "utils/hit_processor.hpp"
#include "utils/lobby_asset_manager.hpp"
#include "utils/lobby_gp_manager.hpp"
#include "utils/lobby_settings.hpp"
#include "utils/lobby_queues.hpp"
#include "utils/map_vote_handler.hpp"
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
    void encodePlayers(BareNetworkString* bns,
            std::vector<std::shared_ptr<NetworkPlayerProfile> >& players)
    {
        bns->addUInt8((uint8_t)players.size());
        for (unsigned i = 0; i < players.size(); i++)
        {
            std::shared_ptr<NetworkPlayerProfile>& player = players[i];
            bns->encodeString(player->getName())
                .addUInt32(player->getHostId())
                .addFloat(player->getDefaultKartColor())
                .addUInt32(player->getOnlineId())
                .addUInt8(player->getHandicap())
                .addUInt8(player->getStartingTyre())
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
                        rki.getOnlineId(), rki.getHandicap(), rki.getStartingTyre(),
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

    void getClientAssetsFromNetworkString(const NetworkString& ns,
            std::set<std::string>& client_karts,
            std::set<std::string>& client_maps)
    {
        client_karts.clear();
        client_maps.clear();
        const unsigned kart_num = ns.getUInt16();
        const unsigned maps_num = ns.getUInt16();
        for (unsigned i = 0; i < kart_num; i++)
        {
            std::string kart;
            ns.decodeString(&kart);
            client_karts.insert(kart);
        }
        for (unsigned i = 0; i < maps_num; i++)
        {
            std::string map;
            ns.decodeString(&map);
            client_maps.insert(map);
        }
    }   // getClientAssetsFromNetworkString
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

    m_current_max_players_in_game.store(ServerConfig::m_max_players_in_game);

    m_lobby_context = std::make_shared<LobbyContext>(this, (bool)ServerConfig::m_soccer_tournament);
    m_lobby_context->setup();
    m_context = m_lobby_context.get();
    m_game_setup->setContext(m_context);

    m_current_ai_count.store(0);

    m_rs_state.store(RS_NONE);
    m_last_success_poll_time.store(StkTime::getMonoTimeMs() + 30000);
    m_last_unsuccess_poll_time = StkTime::getMonoTimeMs();
    m_server_owner_id.store(-1);
    m_registered_for_once_only = false;
    setHandleDisconnections(true);
    m_state = SET_PUBLIC_ADDRESS;
    m_save_server_config = true;
    if (ServerConfig::m_ranked)
    {
        Log::info("ServerLobby", "This server will submit ranking scores to "
            "the STK addons server. Don't bother hosting one without the "
            "corresponding permissions, as they would be rejected.");

        m_ranking = std::make_shared<Ranking>();
    }
    m_result_ns = getNetworkString();
    m_result_ns->setSynchronous(true);
    m_items_complete_state = new BareNetworkString();
    m_server_id_online.store(0);
    m_difficulty.store(ServerConfig::m_server_difficulty);
    m_game_mode.store(ServerConfig::m_server_mode);

#ifdef ENABLE_SQLITE3
    m_db_connector = std::make_shared<DatabaseConnector>();
    m_db_connector->initDatabase();
#endif

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
    delete m_result_ns;
    delete m_items_complete_state;
    if (m_save_server_config)
        ServerConfig::writeServerConfigToDisk();

#ifdef ENABLE_SQLITE3
    m_db_connector->destroyDatabase();
#endif
}   // ~ServerLobby

//-----------------------------------------------------------------------------

void ServerLobby::initServerStatsTable()
{
#ifdef ENABLE_SQLITE3
    m_db_connector->initServerStatsTable();
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
    m_timeout.store(std::numeric_limits<int64_t>::max());
    m_server_started_at = m_server_delay = 0;
    getCommandManager()->onResetServer();
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

    core::stringw message;
    event->data().decodeString16(&message, 360/*max_len*/);

    KartTeam target_team = KART_TEAM_NONE;
    if (event->data().size() > 0)
        target_team = (KartTeam)event->data().getUInt8();

    getChatManager()->handleNormalChatMessage(peer,
            StringUtils::wideToUtf8(message), target_team);
}   // handleChat

//-----------------------------------------------------------------------------
void ServerLobby::changeTeam(Event* event)
{
    if (!checkDataSize(event, 1))
        return;

    NetworkString& data = event->data();
    uint8_t local_id = data.getUInt8();
    auto& player = event->getPeer()->getPlayerProfiles().at(local_id);

    getTeamManager()->changeTeam(player);
}   // changeTeam

//-----------------------------------------------------------------------------
void ServerLobby::kickHost(Event* event)
{
    if (!checkDataSize(event, 4))
        return;

    NetworkString& data = event->data();
    uint32_t host_id = data.getUInt32();
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
        case LE_CHANGE_HANDICAP_AND_TYRE: changeHandicapAndTyre(event); break;
        case LE_CHANGE_COLOR:                       changeColor(event); break;
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
    if (!getSettings()->hasSqlManagement() || !m_db_connector->hasDatabase())
        return;

    if (!m_db_connector->isTimeToPoll())
        return;

    m_db_connector->updatePollTime();

    std::vector<DatabaseConnector::IpBanTableData> ip_ban_list =
            m_db_connector->getIpBanTableData();
    std::vector<DatabaseConnector::Ipv6BanTableData> ipv6_ban_list =
            m_db_connector->getIpv6BanTableData();
    std::vector<DatabaseConnector::OnlineIdBanTableData> online_id_ban_list =
            m_db_connector->getOnlineIdBanTableData();

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
            uint32_t online_id = p->getPlayerProfiles()[0]->getOnlineId();
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

    m_db_connector->clearOldReports();

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
    m_db_connector->setDisconnectionTimes(hosts);
}   // pollDatabase

#endif
//-----------------------------------------------------------------------------
void ServerLobby::writePlayerReport(Event* event)
{
#ifdef ENABLE_SQLITE3
    if (!m_db_connector->hasDatabase() || !m_db_connector->hasPlayerReportsTable())
        return;
    std::shared_ptr<STKPeer> reporter = event->getPeerSP();
    if (!reporter->hasPlayerProfiles())
        return;
    auto reporter_npp = reporter->getPlayerProfiles()[0];

    uint32_t reporting_host_id = event->data().getUInt32();
    core::stringw info;
    event->data().decodeString16(&info);
    if (info.empty())
        return;

    auto reporting_peer = STKHost::get()->findPeerByHostId(reporting_host_id);
    if (!reporting_peer || !reporting_peer->hasPlayerProfiles())
        return;
    auto reporting_npp = reporting_peer->getPlayerProfiles()[0];

    bool written = m_db_connector->writeReport(reporter, reporter_npp,
            reporting_peer, reporting_npp, info);
    if (written)
    {
        NetworkString* success = getNetworkString();
        success->setSynchronous(true);
        success->addUInt8(LE_REPORT_PLAYER).addUInt8(1)
            .encodeString(reporting_npp->getName());
        event->getPeer()->sendPacket(success, PRM_RELIABLE);
        delete success;
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

    if (allowJoinedPlayersWaiting() /*|| (m_game_setup->isGrandPrix() &&
        m_state.load() == WAITING_FOR_START_GAME)*/)
    {
        // Only poll the STK server if server has been registered.
        if (m_server_id_online.load() != 0 &&
            m_state.load() != REGISTER_SELF_ADDRESS)
            checkIncomingConnectionRequests();
        handlePendingConnection();
    }

    if (m_server_id_online.load() != 0 &&
        allowJoinedPlayersWaiting() &&
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
                if (allowJoinedPlayersWaiting())
                    m_registered_for_once_only = true;
                m_state = WAITING_FOR_START_GAME;
                updatePlayerList();
            }
        }
        break;
    }
    case WAITING_FOR_START_GAME:
    {
        if (getSettings()->isOwnerLess())
        {
            // Ensure that a game can auto-start if the server meets the config's starting limit or if it's already full.
            int starting_limit = std::min((int)getSettings()->getMinStartGamePlayers(), (int)getSettings()->getServerMaxPlayers());
            unsigned current_max_players_in_game = m_current_max_players_in_game.load();
            if (current_max_players_in_game > 0) // 0 here means it's not the limit
                starting_limit = std::min(starting_limit, (int)current_max_players_in_game);

            unsigned players = 0;
            STKHost::get()->updatePlayers(&players);
            if (((int)players >= starting_limit ||
                m_game_setup->isGrandPrixStarted()) &&
                m_timeout.load() == std::numeric_limits<int64_t>::max())
            {
                if (getSettings()->getStartGameCounter() >= -1e-5)
                {
                    m_timeout.store((int64_t)StkTime::getMonoTimeMs() +
                        (int64_t)
                        (getSettings()->getStartGameCounter() * 1000.0f));
                }
                else
                {
                    m_timeout.store(std::numeric_limits<int64_t>::max());
                }
            }
            else if ((int)players < starting_limit &&
                !m_game_setup->isGrandPrixStarted())
            {
                resetPeersReady();
                if (m_timeout.load() != std::numeric_limits<int64_t>::max())
                    updatePlayerList();
                m_timeout.store(std::numeric_limits<int64_t>::max());
            }
            bool forbid_starting = false;
            if (getTournament() && getTournament()->forbidStarting())
                forbid_starting = true;
            
            bool timer_finished = (!forbid_starting && m_timeout.load() < (int64_t)StkTime::getMonoTimeMs());
            bool players_ready = (checkPeersReady(true/*ignore_ai_peer*/, BEFORE_SELECTION) && (int)players >= starting_limit);

            if (timer_finished || players_ready)
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
            if (getSettings()->hasFixedLapCount())
            {
                winner_vote.m_num_laps = getSettings()->getFixedLapCount();
                Log::info("ServerLobby", "Enforcing %d lap race", getSettings()->getFixedLapCount());
            }
            if (getSettings()->hasFixedDirection())
            {
                winner_vote.m_reverse = (getSettings()->getDirection() == 1);
                Log::info("ServerLobby", "Enforcing direction %d", (int)getSettings()->getDirection());
            }
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
            std::map<std::shared_ptr<STKPeer>, AlwaysSpectateMode> bad_spectators;
            for (auto peer : peers)
            {
                if (peer->alwaysSpectate() && (!peer->alwaysSpectateForReal() ||
                    peer->getClientAssets().second.count(track_name) == 0))
                {
                    bad_spectators[peer] = peer->getAlwaysSpectate();
                    peer->setAlwaysSpectate(ASM_NONE);
                    peer->setWaitingForGame(true);
                    m_peers_ready.erase(peer);
                }
            }
            bool has_always_on_spectators = false;
            auto players = STKHost::get()
                ->getPlayersForNewGame(&has_always_on_spectators);
            for (auto& p: bad_spectators)
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
            getHitCaptureLimit();

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
            uint16_t flag_deactivated_time = (uint16_t)stk_config->time2Ticks(
                getSettings()->getFlagDeactivatedTime());
            RaceManager::get()->setFlagDeactivatedTicks(flag_deactivated_time);
            configRemoteKart(players, 0);

            // Reset for next state usage
            resetPeersReady();

            m_state = LOAD_WORLD;
            sendMessageToPeers(load_world_message);
            // updatePlayerList so the in lobby players (if any) can see always
            // spectators join the game
            if (has_always_on_spectators)
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
            for (int i = 0; i < (int)RaceManager::get()->getNumPlayers(); i++)
            {
                GameInfo::PlayerInfo info;
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
                    info.m_starting_tyre = (uint8_t)rki.getStartingTyre();
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
                m_game_info->m_player_info.push_back(info);
            }
        }
        break;
    }
    default:
        break;
    }

}   // asynchronousUpdate

//-----------------------------------------------------------------------------
NetworkString* ServerLobby::getLoadWorldMessage(
    std::vector<std::shared_ptr<NetworkPlayerProfile> >& players,
    bool live_join) const
{
    NetworkString* load_world_message = getNetworkString();
    load_world_message->setSynchronous(true);
    load_world_message->addUInt8(LE_LOAD_WORLD);
    getSettings()->encodeDefaultVote(load_world_message);
    load_world_message->addUInt8(live_join ? 1 : 0);
    encodePlayers(load_world_message, players);
    load_world_message->addUInt32(m_item_seed);
    if (RaceManager::get()->isBattleMode())
    {
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
        Kart* fastest_kart = NULL;
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
    NetworkString* reset = getNetworkString(2);
    reset->setSynchronous(true);
    reset->addUInt8(LE_BACK_LOBBY).addUInt8(blr);
    peer->sendPacket(reset, PRM_RELIABLE);
    delete reset;
    updatePlayerList();
    NetworkString* server_info = getNetworkString();
    server_info->setSynchronous(true);
    server_info->addUInt8(LE_SERVER_INFO);
    m_game_setup->addServerInfo(server_info);
    peer->sendPacket(server_info, PRM_RELIABLE);
    delete server_info;
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
        auto& spectators_by_limit = getSpectatorsByLimit();
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
        std::string name = StringUtils::wideToUtf8(rki.getPlayerName());
        int points = 0;

        if (m_game_info)
        {

            if (!m_game_info->m_player_info[id].isReserved())
            {
                Log::error("ServerLobby", "While live joining kart %d, "
                        "player info was not reserved.", id);
            }
            auto& info = m_game_info->m_player_info[id];
            info = GameInfo::PlayerInfo(false/* reserved */,
                    false/* game event*/);
            info.m_username = StringUtils::wideToUtf8(rki.getPlayerName());
            info.m_kart = rki.getKartName();
            info.m_start_position = w->getStartPosition(id);
            info.m_when_joined = stk_config->ticks2Time(w->getTicksSinceStart());
            info.m_country_code = rki.getCountryCode();
            info.m_online_id = rki.getOnlineId();
            info.m_kart_class = rki.getKartData().m_kart_type;
            info.m_kart_color = rki.getDefaultKartColor();
            info.m_handicap = (uint8_t)rki.getHandicap();
            info.m_starting_tyre = rki.getStartingTyre();
            info.m_team = (int8_t)rki.getKartTeam();
            if (info.m_team == KartTeam::KART_TEAM_NONE)
            {
                auto npp = rki.getNetworkPlayerProfile().lock();
                if (npp)
                    info.m_team = npp->getTemporaryTeam() - 1;
            }
            if (RaceManager::get()->isBattleMode())
            {
                if (getSettings()->isPreservingBattleScores())
                    points = m_game_info->m_saved_ffa_points[name];
                info.m_result -= points;
            }
        }
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
        encodePlayers(ns, players);
        for (unsigned i = 0; i < players.size(); i++)
            players[i]->getKartData().encode(ns);
    }

    m_peers_ready[peer] = false;
    peer->setWaitingForGame(false);
    peer->setSpectator(spectator);

    peer->sendPacket(ns, PRM_RELIABLE);
    delete ns;
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
                            sendStringToPeer(peer, getSettings()->getTrollWarnMsg());
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
            NetworkString* back_to_lobby = getNetworkString(2);
            back_to_lobby->setSynchronous(true);
            back_to_lobby->addUInt8(LE_BACK_LOBBY).addUInt8(BLR_NONE);
            sendMessageToPeersInServer(back_to_lobby, PRM_RELIABLE);
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
        m_state.load() == SELECTING &&
        STKHost::get()->getPlayersInGame() == 1)
    {
        NetworkString* back_lobby = getNetworkString(2);
        back_lobby->setSynchronous(true);
        back_lobby->addUInt8(LE_BACK_LOBBY)
            .addUInt8(BLR_ONE_PLAYER_IN_RANKED_MATCH);
        sendMessageToPeers(back_lobby, PRM_RELIABLE);
        delete back_lobby;
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
        m_timeout.store((int64_t)StkTime::getMonoTimeMs() + 15000);
        m_state = RESULT_DISPLAY;
        sendMessageToPeers(m_result_ns, PRM_RELIABLE);
        Log::info("ServerLobby", "End of game message sent");
        break;
    case RESULT_DISPLAY:
        if (checkPeersReady(true/*ignore_ai_peer*/, AFTER_GAME) ||
            (int64_t)StkTime::getMonoTimeMs() > m_timeout.load())
        {
            // Send a notification to all clients to exit
            // the race result screen
            NetworkString* back_to_lobby = getNetworkString(2);
            back_to_lobby->setSynchronous(true);
            back_to_lobby->addUInt8(LE_BACK_LOBBY).addUInt8(BLR_NONE);
            sendMessageToPeersInServer(back_to_lobby, PRM_RELIABLE);
            delete back_to_lobby;
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
    if (event != NULL)
    {
        if (m_state.load() != WAITING_FOR_START_GAME)
        {
            Log::warn("ServerLobby",
                "Received startSelection while being in state %d.",
                m_state.load());
            return;
        }
        if (getSettings()->isSleepingServer())
        {
            Log::warn("ServerLobby",
                "An attempt to start a race on a sleeping server.");
            return;
        }
        auto peer = event->getPeerSP();
        if (getSettings()->isOwnerLess())
        {
            if (!getSettings()->isAllowedToStart())
            {
                sendStringToPeer(peer, "Starting the game is forbidden by server owner");
                return;
            }
            if (!canRace(peer))
            {
                sendStringToPeer(peer, "You cannot play so pressing ready has no action");
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
            sendStringToPeer(peer, "Starting the game is forbidden by server owner");
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
    } else {
        if (!getSettings()->isAllowedToStart())
        {
            // Produce no log spam
            return;
        }
    }

    if (!getSettings()->isOwnerLess() && getSettings()->hasTeamChoosing() &&
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
    auto& spectators_by_limit = getSpectatorsByLimit();
    if (spectators_by_limit.size() == peers.size())
    {
        Log::error("ServerLobby", "Too many players and cannot set "
            "spectate for late coming players!");
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
        bool can_race = canRace(peer);
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
            sendStringToPeer(event->getPeerSP(), "No one can play!");
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

    if (!allowJoinedPlayersWaiting())
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
        if (!canRace(peer) || peer->isWaitingForGame())
            continue; // they are handled below

        NetworkString *ns = getNetworkString(1);
        // Start selection - must be synchronous since the receiver pushes
        // a new screen, which must be done from the main thread.
        ns->setSynchronous(true);
        ns->addUInt8(LE_START_SELECTION)
           .addFloat(getSettings()->getVotingTimeout())
           .addUInt8(/*m_game_setup->isGrandPrixStarted() ? 1 : */0)
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
            sendStringToPeer(peer, "The server will ignore your kart choice");
    }

    m_state = SELECTING;
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
        updatePlayerList();
    }

    if (!allowJoinedPlayersWaiting())
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
    m_timeout.store(std::numeric_limits<int64_t>::max());

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
    m_result_ns->clear();
    m_result_ns->addUInt8(LE_RACE_FINISHED);
    std::vector<float> gp_changes;
    if (m_game_setup->isGrandPrix())
    {
        getGPManager()->updateGPScores(gp_changes, m_result_ns);
    }
    else if (RaceManager::get()->modeHasLaps())
    {
        int fastest_lap =
            static_cast<LinearWorld*>(World::getWorld())->getFastestLapTicks();
        m_result_ns->addUInt32(fastest_lap);
        m_result_ns->encodeString(static_cast<LinearWorld*>(World::getWorld())
            ->getFastestLapKartName());
    }

    uint8_t ranking_changes_indication = 0;
    if (getSettings()->isRanked() && RaceManager::get()->modeHasLaps())
        ranking_changes_indication = 1;
    if (m_game_setup->isGrandPrix())
        ranking_changes_indication = 1;
    m_result_ns->addUInt8(ranking_changes_indication);

    if (getKartElimination()->isEnabled())
    {
        // ServerLobby's function because we need to take
        // the list of players from somewhere
        updateGnuElimination();
    }

    if (getSettings()->isStoringResults())
    {
        storeResults();
    }

    if (getSettings()->isRanked())
    {
        computeNewRankings();
        submitRankingsToAddons();
    }
    else if (m_game_setup->isGrandPrix())
    {
        unsigned player_count = RaceManager::get()->getNumPlayers();
        m_result_ns->addUInt8((uint8_t)player_count);
        for (unsigned i = 0; i < player_count; i++)
        {
            m_result_ns->addFloat(gp_changes[i]);
        }
    }
    m_state.store(WAIT_FOR_RACE_STOPPED);

    getAssetManager()->gameFinishedOn(RaceManager::get()->getTrackName());

    getQueues()->popOnRaceFinished();
}   // checkRaceFinished

//-----------------------------------------------------------------------------

/** Compute the new player's rankings used in ranked servers
 */
void ServerLobby::computeNewRankings()
{
    // No ranking for battle mode
    if (!RaceManager::get()->modeHasLaps())
        return;

    World* w = World::getWorld();
    assert(w);

    unsigned player_count = RaceManager::get()->getNumPlayers();

    // If all players quitted the race, we assume something went wrong
    // and skip entirely rating and statistics updates.
    for (unsigned i = 0; i < player_count; i++)
    {
        if (!w->getKart(i)->isEliminated())
            break;
        if ((i + 1) == player_count)
            return;
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

    for (int i = 0; i < 64; ++i) {
        m_ranking->computeNewRankings(data, RaceManager::get()->isTimeTrialMode());
    }

    // Used to display rating change at the end of a race
    m_result_ns->addUInt8((uint8_t)player_count);
    for (unsigned i = 0; i < player_count; i++)
    {
        const uint32_t id = RaceManager::get()->getKartInfo(i).getOnlineId();
        double change = m_ranking->getDelta(id);
        m_result_ns->addFloat((float)change);
    }
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
    if (w && m_game_info)
        saveDisconnectingPeerInfo(peer);

    NetworkString* msg = getNetworkString(2);
    const bool waiting_peer_disconnected =
        event->getPeer()->isWaitingForGame();
    msg->setSynchronous(true);
    msg->addUInt8(LE_PLAYER_DISCONNECTED);
    msg->addUInt8((uint8_t)players_on_peer.size())
        .addUInt32(event->getPeer()->getHostId());
    for (auto p : players_on_peer)
    {
        std::string name = StringUtils::wideToUtf8(p->getName());
        msg->encodeString(name);
        Log::info("ServerLobby", "%s disconnected", name.c_str());
        getCommandManager()->deleteUser(name);
    }

    unsigned players_number;
    STKHost::get()->updatePlayers(NULL, NULL, &players_number);
    if (players_number == 0)
        resetToDefaultSettings();

    // Don't show waiting peer disconnect message to in game player
    STKHost::get()->sendPacketToAllPeersWith([waiting_peer_disconnected]
        (std::shared_ptr<STKPeer> p)
        {
            if (!p->isValidated())
                return false;
            if (!p->isWaitingForGame() && waiting_peer_disconnected)
                return false;
            return true;
        }, msg);
    updatePlayerList();
    delete msg;

#ifdef ENABLE_SQLITE3
    m_db_connector->writeDisconnectInfoTable(event->getPeerSP());
#endif
}   // clientDisconnected

//-----------------------------------------------------------------------------
    
void ServerLobby::kickPlayerWithReason(std::shared_ptr<STKPeer> peer, const char* reason) const
{
    NetworkString *message = getNetworkString(2);
    message->setSynchronous(true);
    message->addUInt8(LE_CONNECTION_REFUSED).addUInt8(RR_BANNED);
    message->encodeString(std::string(reason));
    peer->cleanPlayerProfiles();
    peer->sendPacket(message, PRM_RELIABLE, PEM_UNENCRYPTED);
    peer->reset();
    delete message;
}   // kickPlayerWithReason

//-----------------------------------------------------------------------------
void ServerLobby::saveIPBanTable(const SocketAddress& addr)
{
#ifdef ENABLE_SQLITE3
    m_db_connector->saveAddressToIpBanTable(addr);
#endif
}   // saveIPBanTable
//-----------------------------------------------------------------------------

bool ServerLobby::handleAssets(Event* event)
{
    const NetworkString& ns = event->data();
    std::shared_ptr<STKPeer> peer = event->getPeerSP();

    std::set<std::string> client_karts, client_maps;
    getClientAssetsFromNetworkString(ns, client_karts, client_maps);

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
            sendStringToPeer(peer, "You deleted some assets that are required to stay on the server");
            peer->kick();
        }
        else
        {
            NetworkString *message = getNetworkString(2);
            message->setSynchronous(true);
            message->addUInt8(LE_CONNECTION_REFUSED)
                    .addUInt8(RR_INCOMPATIBLE_DATA);

            std::string advice = getSettings()->getIncompatibleAdvice();
            if (!advice.empty()) {
                NetworkString *incompatible_reason = getNetworkString();
                incompatible_reason->addUInt8(LE_CHAT);
                incompatible_reason->setSynchronous(true);
                incompatible_reason->encodeString16(
                        StringUtils::utf8ToWide(advice));
                peer->sendPacket(incompatible_reason,
                                 PRM_RELIABLE, PEM_UNENCRYPTED);
                Log::info("ServerLobby", "Sent advice");
                delete incompatible_reason;
            }

            peer->cleanPlayerProfiles();
            peer->sendPacket(message, PRM_RELIABLE, PEM_UNENCRYPTED);
            peer->reset();
            delete message;
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
    NetworkString& data = event->data();
    if (!checkDataSize(event, 14)) return;

    peer->cleanPlayerProfiles();

    // can we add the player ?
    if (!allowJoinedPlayersWaiting() &&
        (m_state.load() != WAITING_FOR_START_GAME /*||
        m_game_setup->isGrandPrixStarted()*/))
    {
        NetworkString *message = getNetworkString(2);
        message->setSynchronous(true);
        message->addUInt8(LE_CONNECTION_REFUSED).addUInt8(RR_BUSY);
        // send only to the peer that made the request and disconnect it now
        peer->sendPacket(message, PRM_RELIABLE, PEM_UNENCRYPTED);
        peer->reset();
        delete message;
        Log::verbose("ServerLobby", "Player refused: selection started");
        return;
    }

    // Check server version
    int version = data.getUInt32();
    if (version < stk_config->m_min_server_version ||
        version > stk_config->m_max_server_version)
    {
        NetworkString *message = getNetworkString(2);
        message->setSynchronous(true);
        message->addUInt8(LE_CONNECTION_REFUSED)
                .addUInt8(RR_INCOMPATIBLE_DATA);
        peer->sendPacket(message, PRM_RELIABLE, PEM_UNENCRYPTED);
        peer->reset();
        delete message;
        Log::verbose("ServerLobby", "Player refused: wrong server version");
        return;
    }
    std::string user_version;
    data.decodeString(&user_version);
    event->getPeer()->setUserVersion(user_version);

    unsigned list_caps = data.getUInt16();
    std::set<std::string> caps;
    for (unsigned i = 0; i < list_caps; i++)
    {
        std::string cap;
        data.decodeString(&cap);
        caps.insert(cap);
    }
    event->getPeer()->setClientCapabilities(caps);

    std::set<std::string> client_karts, client_maps;
    getClientAssetsFromNetworkString(data, client_karts, client_maps);
    if (!handleAssetsAndAddonScores(event->getPeerSP(), client_karts, client_maps))
        return;

    unsigned player_count = data.getUInt8();
    uint32_t online_id = 0;
    uint32_t encrypted_size = 0;
    online_id = data.getUInt32();
    encrypted_size = data.getUInt32();

    // Will be disconnected if banned by IP
    testBannedForIP(peer);
    if (peer->isDisconnected())
        return;

    testBannedForIPv6(peer);
    if (peer->isDisconnected())
        return;

    if (online_id != 0)
        testBannedForOnlineId(peer, online_id);
    // Will be disconnected if banned by online id
    if (peer->isDisconnected())
        return;

    unsigned total_players = 0;
    STKHost::get()->updatePlayers(NULL, NULL, &total_players);
    if (total_players + player_count + m_ai_profiles.size() >
        (unsigned)getSettings()->getServerMaxPlayers())
    {
        NetworkString *message = getNetworkString(2);
        message->setSynchronous(true);
        message->addUInt8(LE_CONNECTION_REFUSED).addUInt8(RR_TOO_MANY_PLAYERS);
        peer->sendPacket(message, PRM_RELIABLE, PEM_UNENCRYPTED);
        peer->reset();
        delete message;
        Log::verbose("ServerLobby", "Player refused: too many players");
        return;
    }

    // Reject non-valiated player joinning if WAN server and not disabled
    // encforement of validation, unless it's player from localhost or lan
    // And no duplicated online id or split screen players in ranked server
    // AIPeer only from lan and only 1 if ai handling
    std::set<uint32_t> all_online_ids =
        STKHost::get()->getAllPlayerOnlineIds();
    bool duplicated_ranked_player =
        all_online_ids.find(online_id) != all_online_ids.end();

    if (((encrypted_size == 0 || online_id == 0) &&
        !(peer->getAddress().isPublicAddressLocalhost() ||
        peer->getAddress().isLAN()) &&
        NetworkConfig::get()->isWAN() &&
        getSettings()->isValidatingPlayer()) ||
        (getSettings()->hasStrictPlayers() &&
        (player_count != 1 || online_id == 0 || duplicated_ranked_player)) ||
        (peer->isAIPeer() && !peer->getAddress().isLAN() && !getSettings()->canConnectAiAnywhere()) ||
        (peer->isAIPeer() &&
        getSettings()->hasAiHandling() && !m_ai_peer.expired()))
    {
        NetworkString* message = getNetworkString(2);
        message->setSynchronous(true);
        message->addUInt8(LE_CONNECTION_REFUSED).addUInt8(RR_INVALID_PLAYER);
        peer->sendPacket(message, PRM_RELIABLE, PEM_UNENCRYPTED);
        peer->reset();
        delete message;
        Log::verbose("ServerLobby", "Player refused: invalid player");
        return;
    }

    if (getSettings()->hasAiHandling() && peer->isAIPeer())
        m_ai_peer = peer;

    if (encrypted_size != 0)
    {
        m_pending_connection[peer] = std::make_pair(online_id,
            BareNetworkString(data.getCurrentData(), encrypted_size));
    }
    else
    {
        core::stringw online_name;
        if (online_id > 0)
            data.decodeStringW(&online_name);
        handleUnencryptedConnection(peer, data, online_id, online_name,
            false/*is_pending_connection*/);
    }
}   // connectionRequested

//-----------------------------------------------------------------------------
void ServerLobby::handleUnencryptedConnection(std::shared_ptr<STKPeer> peer,
    BareNetworkString& data, uint32_t online_id,
    const core::stringw& online_name, bool is_pending_connection,
    std::string country_code)
{
    if (data.size() < 2) return;

    // Check for password
    std::string password;
    data.decodeString(&password);
    const std::string& server_pw = getSettings()->getPrivateServerPassword();
    if (online_id > 0)
    {
        std::string username = StringUtils::wideToUtf8(online_name);
        if (m_temp_banned.count(username))
        {
            NetworkString* message = getNetworkString(2);
            message->setSynchronous(true);
            message->addUInt8(LE_CONNECTION_REFUSED)
                .addUInt8(RR_BANNED);
            std::string tempban = "Please behave well next time.";
            message->encodeString(tempban);
            peer->sendPacket(message, PRM_RELIABLE, PEM_UNENCRYPTED);
            peer->reset();
            delete message;
            Log::verbose("ServerLobby", "Player refused: invalid player");
            return;
        }
        if (getSettings()->isInWhitelist(username))
            password = server_pw;
    }
    if (password != server_pw)
    {
        NetworkString *message = getNetworkString(2);
        message->setSynchronous(true);
        message->addUInt8(LE_CONNECTION_REFUSED)
                .addUInt8(RR_INCORRECT_PASSWORD);
        peer->sendPacket(message, PRM_RELIABLE, PEM_UNENCRYPTED);
        peer->reset();
        delete message;
        Log::verbose("ServerLobby", "Player refused: incorrect password");
        return;
    }

    // Check again max players and duplicated player in ranked server,
    // if this is a pending connection
    unsigned total_players = 0;
    unsigned player_count = data.getUInt8();

    if (is_pending_connection)
    {
        STKHost::get()->updatePlayers(NULL, NULL, &total_players);
        if (total_players + player_count >
            (unsigned)getSettings()->getServerMaxPlayers())
        {
            NetworkString *message = getNetworkString(2);
            message->setSynchronous(true);
            message->addUInt8(LE_CONNECTION_REFUSED)
                .addUInt8(RR_TOO_MANY_PLAYERS);
            peer->sendPacket(message, PRM_RELIABLE, PEM_UNENCRYPTED);
            peer->reset();
            delete message;
            Log::verbose("ServerLobby", "Player refused: too many players");
            return;
        }

        std::set<uint32_t> all_online_ids =
            STKHost::get()->getAllPlayerOnlineIds();
        bool duplicated_ranked_player =
            all_online_ids.find(online_id) != all_online_ids.end();
        if (getSettings()->isRanked() && duplicated_ranked_player)
        {
            NetworkString* message = getNetworkString(2);
            message->setSynchronous(true);
            message->addUInt8(LE_CONNECTION_REFUSED)
                .addUInt8(RR_INVALID_PLAYER);
            peer->sendPacket(message, PRM_RELIABLE, PEM_UNENCRYPTED);
            peer->reset();
            delete message;
            Log::verbose("ServerLobby", "Player refused: invalid player");
            return;
        }
    }

#ifdef ENABLE_SQLITE3
    if (country_code.empty() && !peer->getAddress().isIPv6())
        country_code = m_db_connector->ip2Country(peer->getAddress());
    if (country_code.empty() && peer->getAddress().isIPv6())
        country_code = m_db_connector->ipv62Country(peer->getAddress());
#endif

    auto red_blue = STKHost::get()->getAllPlayersTeamInfo();
    std::string utf8_online_name = StringUtils::wideToUtf8(online_name);
    for (unsigned i = 0; i < player_count; i++)
    {
        core::stringw name;
        data.decodeStringW(&name);
        // 30 to make it consistent with stk-addons max user name length
        if (name.empty())
            name = L"unnamed";
        else if (name.size() > 30)
            name = name.subString(0, 30);

        std::string utf8_name = StringUtils::wideToUtf8(name);
        float default_kart_color = data.getFloat();
        HandicapLevel handicap = (HandicapLevel)data.getUInt8();
        unsigned starting_tyre = data.getUInt8();
        auto player = std::make_shared<NetworkPlayerProfile>
            (peer, i == 0 && !online_name.empty() && !peer->isAIPeer() ?
            online_name : name,
            peer->getHostId(), default_kart_color, i == 0 ? online_id : 0,
            handicap, starting_tyre, (uint8_t)i, KART_TEAM_NONE,
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
    NetworkString* server_info = getNetworkString();
    server_info->setSynchronous(true);
    server_info->addUInt8(LE_SERVER_INFO);
    m_game_setup->addServerInfo(server_info);
    peer->sendPacket(server_info);
    delete server_info;

    const bool game_started = m_state.load() != WAITING_FOR_START_GAME;
    NetworkString* message_ack = getNetworkString(4);
    message_ack->setSynchronous(true);
    // connection success -- return the host id of peer
    float auto_start_timer = 0.0f;
    if (m_timeout.load() == std::numeric_limits<int64_t>::max())
        auto_start_timer = std::numeric_limits<float>::max();
    else
    {
        auto_start_timer =
            (m_timeout.load() - (int64_t)StkTime::getMonoTimeMs()) / 1000.0f;
    }
    message_ack->addUInt8(LE_CONNECTION_ACCEPTED).addUInt32(peer->getHostId())
        .addUInt32(ServerConfig::m_server_version);

    message_ack->addUInt16(
        (uint16_t)stk_config->m_network_capabilities.size());
    for (const std::string& cap : stk_config->m_network_capabilities)
        message_ack->encodeString(cap);

    message_ack->addFloat(auto_start_timer)
        .addUInt32(getSettings()->getStateFrequency())
        .addUInt8(getChatManager()->getChat() ? 1 : 0)
        .addUInt8(playerReportsTableExists() ? 1 : 0);

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
                (peer, name, peer->getHostId(), 0.0f, 0, HANDICAP_NONE, 2,
                player_count + i, KART_TEAM_NONE, ""));
        }
    }

    if (game_started)
    {
        peer->setWaitingForGame(true);
        updatePlayerList();
        peer->sendPacket(message_ack);
        delete message_ack;
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
        peer->sendPacket(message_ack);
        delete message_ack;

        if (getSettings()->isRanked())
        {
            getRankingForPlayer(peer->getPlayerProfiles()[0]);
        }
    }

#ifdef ENABLE_SQLITE3
    m_db_connector->onPlayerJoinQueries(peer, online_id, player_count, country_code);
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
        NetworkString* chat = getNetworkString();
        chat->addUInt8(LE_CHAT);
        chat->setSynchronous(true);
        std::string warning = getKartElimination()->getWarningMessage(hasEliminatedPlayer);
        chat->encodeString16(StringUtils::utf8ToWide(warning));
        peer->sendPacket(chat, PRM_RELIABLE);
        delete chat;
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
        sendStringToPeer(peer, msg);
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

    auto& spectators_by_limit = getSpectatorsByLimit(true);

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
    if (!allowJoinedPlayersWaiting() &&
        m_state.load() > WAITING_FOR_START_GAME && !update_when_reset_server)
        return;

    PlayerListPacket list_packet;
    list_packet.game_started = game_started;
    list_packet.all_profiles_size = all_profiles.size();

    for (auto profile : all_profiles)
    {
        PlayerListProfilePacket packet;
        auto profile_name = profile->getName();

        // get OS information
        auto version_os = StringUtils::extractVersionOS(profile->getPeer()->getUserVersion());
        bool angry_host = profile->getPeer()->isAngryHost();
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
        if (angry_host)
            profile_name = StringUtils::utf32ToWide({0x1F528}) + profile_name;

        profile_name = StringUtils::utf8ToWide(profile->getTyreCircle()) + profile_name;

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
        if (getSettings()->isOwnerLess() && !game_started &&
            m_peers_ready.find(p) != m_peers_ready.end() &&
            m_peers_ready.at(p))
            boolean_combine |= (1 << 3);
        if ((p && p->isAIPeer()) || isAIProfile(profile))
            boolean_combine |= (1 << 4);
        packet.mask = boolean_combine;
        packet.handicap = profile->getHandicap();
        packet.starting_tyre = profile->getStartingTyre();
        if (getSettings()->hasTeamChoosing())
            packet.kart_team = profile->getTeam();
        else
            packet.kart_team = KART_TEAM_NONE;
        packet.country_code = profile->getCountryCode();
        list_packet.all_profiles.push_back(packet);
    }

    NetworkString* pl = getNetworkString();
    list_packet.toNetworkString(pl);

    // Don't send this message to in-game players
    STKHost::get()->sendPacketToAllPeersWith([game_started]
        (std::shared_ptr<STKPeer> p)
        {
            if (!p->isValidated())
                return false;
            if (!p->isWaitingForGame() && game_started)
                return false;
            return true;
        }, pl);
    delete pl;
}   // updatePlayerList

//-----------------------------------------------------------------------------
void ServerLobby::updateServerOwner(bool force)
{
    if (m_state.load() < WAITING_FOR_START_GAME ||
        m_state.load() > RESULT_DISPLAY ||
        getSettings()->isOwnerLess())
        return;
    if (!force && !m_server_owner.expired())
        return;
    auto peers = STKHost::get()->getPeers();
    if (peers.empty())
        return;
    std::sort(peers.begin(), peers.end(), [](const std::shared_ptr<STKPeer> a,
        const std::shared_ptr<STKPeer> b)->bool
        {
            if (a->isCommandSpectator() ^ b->isCommandSpectator())
                return b->isCommandSpectator();
            return a->getRejoinTime() < b->getRejoinTime();
        });

    std::shared_ptr<STKPeer> owner;
    for (auto peer: peers)
    {
        // Only matching host id can be server owner in case of
        // graphics-client-server
        if (peer->isValidated() && !peer->isAIPeer() &&
            (m_process_type == PT_MAIN ||
            peer->getHostId() == m_client_server_host_id.load()))
        {
            owner = peer;
            break;
        }
    }
    if (owner)
    {
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
    }
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

    const NetworkString& data = event->data();
    std::shared_ptr<STKPeer> peer = event->getPeerSP();
    setPlayerKarts(data, peer);
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
    vote.m_player_name = event->getPeer()->getPlayerProfiles()[0]->getName();
    addVote(event->getPeer()->getHostId(), vote);

    // Now inform all clients about the vote
    NetworkString other = NetworkString(PROTOCOL_LOBBY_ROOM);
    other.setSynchronous(true);
    other.addUInt8(LE_VOTE);
    other.addUInt32(event->getPeer()->getHostId());
    vote.encode(&other);
    sendMessageToPeers(&other);

}   // handlePlayerVote

// ----------------------------------------------------------------------------
/** Select the track to be used based on all votes being received.
 * \param winner_vote The PeerVote that was picked.
 * \param winner_peer_id The host id of winner (unchanged if no vote).
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
void ServerLobby::getHitCaptureLimit()
{
    int hit_capture_limit = std::numeric_limits<int>::max();
    float time_limit = 0.0f;
    if (RaceManager::get()->getMinorMode() ==
        RaceManager::MINOR_MODE_CAPTURE_THE_FLAG)
    {
        if (getSettings()->getCaptureLimit() > 0)
            hit_capture_limit = getSettings()->getCaptureLimit();
        if (getSettings()->getTimeLimitCtf() > 0)
            time_limit = (float)getSettings()->getTimeLimitCtf();
    }
    else
    {
        if (getSettings()->getHitLimit() > 0)
            hit_capture_limit = getSettings()->getHitLimit();
        if (getSettings()->getTimeLimitFfa() > 0.0f)
            time_limit = (float)getSettings()->getTimeLimitFfa();
    }
    getSettings()->setBattleHitCaptureLimit(hit_capture_limit);
    getSettings()->setBattleTimeLimit(time_limit);
}   // getHitCaptureLimit

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
    // if (m_game_setup->isGrandPrix() && m_game_setup->isGrandPrixStarted())
    //     return false;
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
        Log::info("ServerLobby", "%s validated",
            StringUtils::wideToUtf8(online_name).c_str());
        handleUnencryptedConnection(peer, data, online_id,
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
    NetworkString* ns = getNetworkString(10);
    ns->setSynchronous(true);
    ns->addUInt8(LE_START_RACE).addUInt64(start_time);
    const uint8_t cc = (uint8_t)Track::getCurrentTrack()->getCheckManager()->getCheckStructureCount();
    ns->addUInt8(cc);
    *ns += *m_items_complete_state;
    m_client_starting_time = start_time;
    sendMessageToPeers(ns, PRM_RELIABLE);

    const unsigned jitter_tolerance = getSettings()->getJitterTolerance();
    Log::info("ServerLobby", "Max ping from peers: %d, jitter tolerance: %d",
        max_ping, jitter_tolerance);
    // Delay server for max ping / 2 from peers and jitter tolerance.
    m_server_delay = (uint64_t)(max_ping / 2) + (uint64_t)jitter_tolerance;
    start_time += m_server_delay;
    m_server_started_at = start_time;
    delete ns;
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
bool ServerLobby::allowJoinedPlayersWaiting() const
{
    return true; //!m_game_setup->isGrandPrix();
}   // allowJoinedPlayersWaiting

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
            getRankingForPlayer(peer->getPlayerProfiles()[0]);
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
    NetworkString* server_info = getNetworkString();
    server_info->setSynchronous(true);
    server_info->addUInt8(LE_SERVER_INFO);
    m_game_setup->addServerInfo(server_info);
    sendMessageToPeersInServer(server_info);
    delete server_info;
    setup();
    m_state = NetworkConfig::get()->isLAN() ?
        WAITING_FOR_START_GAME : REGISTER_SELF_ADDRESS;
    updatePlayerList();
}   // resetServer

//-----------------------------------------------------------------------------
void ServerLobby::testBannedForIP(std::shared_ptr<STKPeer> peer) const
{
#ifdef ENABLE_SQLITE3
    if (!m_db_connector->hasDatabase() || !m_db_connector->hasIpBanTable())
        return;

    // Test for IPv4
    if (peer->getAddress().isIPv6())
        return;

    bool is_banned = false;
    uint32_t ip_start = 0;
    uint32_t ip_end = 0;

    std::vector<DatabaseConnector::IpBanTableData> ip_ban_list =
            m_db_connector->getIpBanTableData(peer->getAddress().getIP());
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
        m_db_connector->increaseIpBanTriggerCount(ip_start, ip_end);
#endif
}   // testBannedForIP

//-----------------------------------------------------------------------------
void ServerLobby::getMessagesFromHost(std::shared_ptr<STKPeer> peer, int online_id)
{
#ifdef ENABLE_SQLITE3
    std::vector<DatabaseConnector::ServerMessage> messages =
            m_db_connector->getServerMessages(online_id);
    // in case there will be more than one later
    for (const auto& message: messages)
    {
        Log::info("ServerLobby", "A message from server was delivered");
        sendStringToPeer(peer, "A message from the server (" +
            std::string(message.timestamp) + "):\n" + std::string(message.message));
        m_db_connector->deleteServerMessage(message.row_id);
    }
#endif
}   // getMessagesFromHost

//-----------------------------------------------------------------------------
void ServerLobby::testBannedForIPv6(std::shared_ptr<STKPeer> peer) const
{
#ifdef ENABLE_SQLITE3
    if (!m_db_connector->hasDatabase() || !m_db_connector->hasIpv6BanTable())
        return;

    // Test for IPv6
    if (!peer->getAddress().isIPv6())
        return;

    bool is_banned = false;
    std::string ipv6_cidr = "";

    std::vector<DatabaseConnector::Ipv6BanTableData> ipv6_ban_list =
            m_db_connector->getIpv6BanTableData(peer->getAddress().toString(false));

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
        m_db_connector->increaseIpv6BanTriggerCount(ipv6_cidr);
#endif
}   // testBannedForIPv6

//-----------------------------------------------------------------------------
void ServerLobby::testBannedForOnlineId(std::shared_ptr<STKPeer> peer,
                                        uint32_t online_id) const
{
#ifdef ENABLE_SQLITE3
    if (!m_db_connector->hasDatabase() || !m_db_connector->hasOnlineIdBanTable())
        return;

    bool is_banned = false;
    std::vector<DatabaseConnector::OnlineIdBanTableData> online_id_ban_list =
            m_db_connector->getOnlineIdBanTableData(online_id);

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
        m_db_connector->increaseOnlineIdBanTriggerCount(online_id);
#endif
}   // testBannedForOnlineId

//-----------------------------------------------------------------------------
void ServerLobby::listBanTable()
{
#ifdef ENABLE_SQLITE3
    m_db_connector->listBanTable();
#endif
}   // listBanTable

//-----------------------------------------------------------------------------
uint8_t ServerLobby::getStartupBoostOrPenaltyForKart(uint32_t ping,
                                                   unsigned kart_id)
{
    // boost-level 0 corresponds to a start penalty
    // boost-level 1 corresponds to a start without boost or penalty
    // boost-level 2 or more corresponds to a start with boost

    Kart* k = World::getWorld()->getKart(kart_id);
    // If a boost already exists, return it
    if (k->getStartupBoostLevel() >= 2)
        return k->getStartupBoostLevel();
    uint64_t now = STKHost::get()->getNetworkTimer();
    uint64_t client_time = now - ping / 2;
    uint64_t server_time = client_time + m_server_delay;
    int ticks = stk_config->time2Ticks(
        (float)(server_time - m_server_started_at) / 1000.0f);
    if (ticks < stk_config->time2Ticks(1.0f))
    {
        PlayerController* pc =
            dynamic_cast<PlayerController*>(k->getController());
        pc->displayPenaltyWarning();
        return 0;
    }
    k->setStartupBoostFromStartTicks(ticks);
    return k->getStartupBoostLevel();
}   // getStartupBoostOrPenaltyForKart

//-----------------------------------------------------------------------------

void ServerLobby::handleServerConfiguration(std::shared_ptr<STKPeer> peer,
    int difficulty, int mode, bool soccer_goal_target, int gp_track_count)
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
        sendStringToPeer(peer, msg);
        return;
    }

    // Kimden version:
    auto modes = ServerConfig::getLocalGameMode(mode);

    if (gp_track_count > 0)
    {
        ServerConfig::m_gp_track_count = gp_track_count;
        Log::warn("ServerLobby", "Grand prix is used for new mode.");
        //return;
    }

    RaceManager::get()->setMinorMode(modes.first);
    RaceManager::get()->setMajorMode(modes.second);
    RaceManager::get()->setDifficulty(RaceManager::Difficulty(difficulty));
    ServerConfig::m_gp_track_count = gp_track_count;
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

    auto peers = STKHost::get()->getPeers();
    for (auto& peer : peers)
    {
        auto assets = peer->getClientAssets();
        if (!peer->isValidated() || assets.second.empty()) // this check will fail hard when I introduce vavriable limits
            continue;
        if (getAssetManager()->checkIfNoCommonMaps(assets))
        {
            NetworkString *message = getNetworkString(2);
            message->setSynchronous(true);
            message->addUInt8(LE_CONNECTION_REFUSED)
                .addUInt8(RR_INCOMPATIBLE_DATA);
            peer->cleanPlayerProfiles();
            peer->sendPacket(message, PRM_RELIABLE);
            peer->reset();
            delete message;
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
            getCommandManager()->assignRandomTeams(2, &final_number, &players_number);
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

    NetworkString* server_info = getNetworkString();
    server_info->setSynchronous(true);
    server_info->addUInt8(LE_SERVER_INFO);
    m_game_setup->addServerInfo(server_info);
    sendMessageToPeers(server_info);
    delete server_info;
    updatePlayerList();

    if (getKartElimination()->isEnabled() &&
        RaceManager::get()->getMinorMode() != RaceManager::MINOR_MODE_NORMAL_RACE &&
        RaceManager::get()->getMinorMode() != RaceManager::MINOR_MODE_TIME_TRIAL)
    {
        getKartElimination()->disable();
        sendStringToAllPeers("Gnu Elimination is disabled because of non-racing mode");
    }
    if (gp_track_count > 0) {
        getGameSetup()->setGrandPrixTrack(gp_track_count);
        getGPManager()->resetGrandPrix();
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
    bool change_config = false;
    int new_difficulty = getSettings()->getServerDifficulty();
    int new_game_mode = getSettings()->getServerMode();
    int new_gp_track_count = ServerConfig::m_gp_track_count;
    bool new_soccer_goal_target = getSettings()->isSoccerGoalTargetInConfig();
    if (event != NULL)
    {
        NetworkString& data = event->data();

        int received_new_difficulty = data.getUInt8();
        if (received_new_difficulty != new_difficulty) {
            change_config = true;
            new_difficulty = received_new_difficulty;
        }

        
        float fuel_info[5];
        fuel_info[0] = data.getFloat();
        fuel_info[1] = data.getFloat();
        fuel_info[2] = data.getFloat();
        fuel_info[3] = data.getFloat();
        fuel_info[4] = data.getFloat();

        int compound_amount[3];
        compound_amount[0] = data.getUInt8()-1;
        compound_amount[1] = data.getUInt8()-1;
        compound_amount[2] = data.getUInt8()-1;

        RaceManager::get()->setFuelAndQueueInfo(fuel_info[0], fuel_info[1], fuel_info[2], fuel_info[3], fuel_info[4], compound_amount[0], compound_amount[1], compound_amount[2]);

        int received_new_game_mode = data.getUInt8();
        if (received_new_game_mode != new_game_mode) {
            change_config = true;
            new_game_mode = received_new_game_mode;
        }

        int received_new_soccer_goal_target = data.getUInt8() == 1;
        if (received_new_soccer_goal_target != new_soccer_goal_target) {
            change_config = true;
            new_soccer_goal_target = received_new_soccer_goal_target;
        }

        int received_gp_track_count = data.getUInt8();
        if (received_gp_track_count != new_gp_track_count) {
            change_config = true;
            new_gp_track_count = received_gp_track_count;
        }
    }
    if (change_config) {
        handleServerConfiguration(
            (event ? event->getPeerSP() : std::shared_ptr<STKPeer>()),
            new_difficulty, new_game_mode, new_soccer_goal_target, new_gp_track_count);
    }
}   // handleServerConfiguration

//-----------------------------------------------------------------------------
/*! \brief Called when a player want to change his handicap and starting rtyre
 *  \param event : Event providing the information.
 *
 *  Format of the data :
 *  Byte 0                 1
 *       -------------------------------------------------
 *  Size |       1         |       1      |       1      |
 *  Data | local player id | new handicap | new tyre     |
 *       -------------------------------------------------
 */
void ServerLobby::changeHandicapAndTyre(Event* event)
{
    NetworkString& data = event->data();
    if (m_state.load() != WAITING_FOR_START_GAME &&
        !event->getPeer()->isWaitingForGame())
    {
        Log::warn("ServerLobby", "Set handicap at wrong time.");
        return;
    }
    uint8_t local_id = data.getUInt8();
    auto& player = event->getPeer()->getPlayerProfiles().at(local_id);
    uint8_t handicap_id = data.getUInt8();
    if (handicap_id >= HANDICAP_COUNT)
    {
        Log::warn("ServerLobby", "Wrong handicap %d.", handicap_id);
        return;
    }
    HandicapLevel h = (HandicapLevel)handicap_id;
    player->setHandicap(h);

    uint8_t tyre_id = data.getUInt8();
    unsigned t = tyre_id;
    player->setStartingTyre(t);
    printf("Server received this, handicap, tyre: %u, %u\n", handicap_id, t);

    updatePlayerList();
}   // changeHandicap

void ServerLobby::changeColor(Event* event)
{
    NetworkString& data = event->data();
    if (m_state.load() != WAITING_FOR_START_GAME &&
        !event->getPeer()->isWaitingForGame())
    {
        Log::warn("ServerLobby", "Set color at wrong time.");
        return;
    }
    uint8_t local_id = data.getUInt8();
    auto& player = event->getPeer()->getPlayerProfiles().at(local_id);
    float hue = data.getFloat();

    player->setDefaultKartColor(hue);
    updatePlayerList();
}   // changeColor

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

        if (!m_game_info)
            Log::warn("ServerLobby", "GameInfo is not accessible??");
        else
        {
            saveDisconnectingIdInfo(i);
        }

        rki.makeReserved();

        Kart* k = World::getWorld()->getKart(i);
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
void ServerLobby::setPlayerKarts(const NetworkString& ns, std::shared_ptr<STKPeer> peer) const
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
/** Tell the client \ref RemoteKartInfo of a player when some player joining
 *  live.
 */
void ServerLobby::handleKartInfo(Event* event)
{
    World* w = World::getWorld();
    if (!w)
        return;

    std::shared_ptr<STKPeer> peer = event->getPeerSP();
    const NetworkString& data = event->data();
    uint8_t kart_id = data.getUInt8();
    if (kart_id > RaceManager::get()->getNumPlayers())
        return;

    Kart* k = w->getKart(kart_id);
    int live_join_util_ticks = k->getLiveJoinUntilTicks();

    const RemoteKartInfo& rki = RaceManager::get()->getKartInfo(kart_id);

    NetworkString* ns = getNetworkString(1);
    ns->setSynchronous(true);
    ns->addUInt8(LE_KART_INFO).addUInt32(live_join_util_ticks)
        .addUInt8(kart_id) .encodeString(rki.getPlayerName())
        .addUInt32(rki.getHostId()).addFloat(rki.getDefaultKartColor())
        .addUInt32(rki.getOnlineId()).addUInt8(rki.getHandicap())
        .addUInt8(rki.getStartingTyre()).addUInt8((uint8_t)rki.getLocalPlayerId())
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
        NetworkString* back_to_lobby = getNetworkString(2);
        back_to_lobby->setSynchronous(true);
        back_to_lobby->addUInt8(LE_BACK_LOBBY)
            .addUInt8(BLR_SERVER_ONWER_QUITED_THE_GAME);
        sendMessageToPeersInServer(back_to_lobby, PRM_RELIABLE);
        delete back_to_lobby;
        m_rs_state.store(RS_ASYNC_RESET);
        return;
    }

    if (!m_game_info)
        Log::warn("ServerLobby", "GameInfo is not accessible??");
    else
        saveDisconnectingPeerInfo(peer);

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
    updatePlayerList();
    NetworkString* server_info = getNetworkString();
    server_info->setSynchronous(true);
    server_info->addUInt8(LE_SERVER_INFO);
    m_game_setup->addServerInfo(server_info);
    peer->sendPacket(server_info, PRM_RELIABLE);
    delete server_info;
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
        NetworkString* back_to_lobby = getNetworkString(2);
        back_to_lobby->setSynchronous(true);
        back_to_lobby->addUInt8(LE_BACK_LOBBY)
            .addUInt8(BLR_SERVER_ONWER_QUITED_THE_GAME);
        sendMessageToPeersInServer(back_to_lobby, PRM_RELIABLE);
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
    updatePlayerList();
    NetworkString* server_info = getNetworkString();
    server_info->setSynchronous(true);
    server_info->addUInt8(LE_SERVER_INFO);
    m_game_setup->addServerInfo(server_info);
    peer->sendPacket(server_info, PRM_RELIABLE);
    delete server_info;
}   // clientSelectingAssetsWantsToBackLobby

//-----------------------------------------------------------------------------
std::set<std::shared_ptr<STKPeer>>& ServerLobby::getSpectatorsByLimit(bool update)
{
    if (!update)
        return m_spectators_by_limit;

    m_why_peer_cannot_play.clear();
    m_spectators_by_limit.clear();

    auto peers = STKHost::get()->getPeers();

    unsigned player_limit = getSettings()->getServerMaxPlayers();
    // If the server has an in-game player limit lower than the lobby limit, apply it,
    // A value of 0 for this parameter means no limit.
    unsigned current_max_players_in_game = m_current_max_players_in_game.load();
    if (current_max_players_in_game > 0)
        player_limit = std::min(player_limit, (unsigned)current_max_players_in_game);

    // only 10 players allowed for FFA and 14 for CTF and soccer
    if (RaceManager::get()->getMinorMode() ==
            RaceManager::MINOR_MODE_FREE_FOR_ALL)
        player_limit = std::min(player_limit, 10u);

    if (RaceManager::get()->getMinorMode() ==
            RaceManager::MINOR_MODE_CAPTURE_THE_FLAG)
        player_limit = std::min(player_limit, 14u);

    if (RaceManager::get()->getMinorMode() ==
            RaceManager::MINOR_MODE_SOCCER)
        player_limit = std::min(player_limit, 14u);

    unsigned ingame_players = 0, waiting_players = 0, total_players = 0;
    STKHost::get()->updatePlayers(&ingame_players, &waiting_players, &total_players);

    for (int i = 0; i < (int)peers.size(); ++i)
    {
        if (!peers[i]->isValidated())
        {
            swap(peers[i], peers.back());
            peers.pop_back();
            --i;
        }
    }

    std::sort(peers.begin(), peers.end(),
        [](const std::shared_ptr<STKPeer>& a,
            const std::shared_ptr<STKPeer>& b)
        { return a->getRejoinTime() < b->getRejoinTime(); });

    unsigned player_count = 0;
    if (m_state.load() >= RACING)
    {
        for (auto &peer : peers)
            if (peer->isSpectator())
                ingame_players -= (int)peer->getPlayerProfiles().size();
        player_count = ingame_players;
    }

    for (unsigned i = 0; i < peers.size(); i++)
    {
        auto& peer = peers[i];
        if (!peer->isValidated())
            continue;
        bool ignore = false;
        if (m_state.load() < RACING)
        {
            if (peer->alwaysSpectate() || peer->isWaitingForGame())
                ignore = true;
            else if (!canRace(peer))
            {
                m_spectators_by_limit.insert(peer);
                ignore = true;
            }
        }
        else
        {
            if (!peer->isWaitingForGame())
                ignore = true; // we already counted them properly in ingame_players
        }
        if (!ignore)
        {
            player_count += (unsigned)peer->getPlayerProfiles().size();
            if (player_count > player_limit)
                m_spectators_by_limit.insert(peer);
        }
    }
    return m_spectators_by_limit;
}   // getSpectatorsByLimit

//-----------------------------------------------------------------------------
void ServerLobby::saveInitialItems(std::shared_ptr<NetworkItemManager> nim)
{
    m_items_complete_state->getBuffer().clear();
    m_items_complete_state->reset();
    nim->saveCompleteState(m_items_complete_state);
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
        if (phase == BEFORE_SELECTION && !canRace(peer))
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
void ServerLobby::updateGnuElimination()
{
    World* w = World::getWorld();
    assert(w);
    int player_count = RaceManager::get()->getNumPlayers();
    std::map<std::string, double> order;
    for (int i = 0; i < player_count; i++)
    {
        std::string username = StringUtils::wideToUtf8(RaceManager::get()->getKartInfo(i).getPlayerName());
        if (w->getKart(i)->isEliminated())
            order[username] = KartElimination::INF_TIME;
        else
            order[username] = RaceManager::get()->getKartRaceTime(i);
    }
    std::string msg = getKartElimination()->update(order);
    if (!msg.empty())
        sendStringToAllPeers(msg);
}  // updateGnuElimination
//-----------------------------------------------------------------------------
void ServerLobby::storeResults()
{
#ifdef ENABLE_SQLITE3
    World* w = World::getWorld();
    if (!w)
        return;
    if (!m_game_info)
    {
        Log::warn("ServerLobby", "GameInfo is not accessible??");
        return;
    }

    RaceManager* rm = RaceManager::get();
    m_game_info->m_timestamp = StkTime::getLogTimeFormatted("%Y-%m-%d %H:%M:%S");
    m_game_info->m_venue = rm->getTrackName();
    m_game_info->m_reverse = (rm->getReverseTrack() ||
            rm->getRandomItemsIndicator() ? "reverse" : "normal");
    m_game_info->m_mode = rm->getMinorModeName();
    m_game_info->m_value_limit = 0;
    m_game_info->m_time_limit = rm->getTimeTarget();
    m_game_info->m_difficulty = getDifficulty();
    m_game_info->setPowerupString(powerup_manager->getFileName());
    m_game_info->setKartCharString(kart_properties_manager->getFileName());

    if (rm->getMinorMode() == RaceManager::MINOR_MODE_CAPTURE_THE_FLAG)
    {
        m_game_info->m_value_limit = rm->getHitCaptureLimit();
        m_game_info->m_flag_return_timeout = getSettings()->getFlagReturnTimeout();
        m_game_info->m_flag_deactivated_time = getSettings()->getFlagDeactivatedTime();
    }
    else if (rm->getMinorMode() == RaceManager::MINOR_MODE_FREE_FOR_ALL)
    {
        m_game_info->m_value_limit = rm->getHitCaptureLimit(); // TODO doesn't work
        m_game_info->m_flag_return_timeout = 0;
        m_game_info->m_flag_deactivated_time = 0;
    }
    else if (rm->getMinorMode() == RaceManager::MINOR_MODE_SOCCER)
    {
        m_game_info->m_value_limit = rm->getMaxGoal(); // TODO doesn't work
        m_game_info->m_flag_return_timeout = 0;
        m_game_info->m_flag_deactivated_time = 0;
    }
    else
    {
        m_game_info->m_value_limit = rm->getNumLaps();
        m_game_info->m_flag_return_timeout = 0;
        m_game_info->m_flag_deactivated_time = 0;
    }

    // m_game_info->m_timestamp = TODO;
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
        record_fetched = m_db_connector->getBestResult(*m_game_info, &record_exists, &best_user, &best_result);
    }

    int best_cur_player_idx = -1;
    std::string best_cur_player_name = "";
    double best_cur_time = 1e18;

    double current_game_timestamp = stk_config->ticks2Time(w->getTicksSinceStart());

    auto& vec = m_game_info->m_player_info;

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
                Log::error("ServerLobby", "storeResults: battle mode but FFAWorld is not found");
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
                Log::error("ServerLobby", "storeResults: racing mode but LinearWorld is not found");
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

    m_game_info->m_saved_ffa_points.clear();
    m_db_connector->insertManyResults(*m_game_info);
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
            sendStringToAllPeers(message);
    }
#endif
}  // storeResults
//-----------------------------------------------------------------------------
void ServerLobby::resetToDefaultSettings()
{
    if (getSettings()->isServerConfigurable() && !getSettings()->isPreservingMode())
        handleServerConfiguration(NULL);

    getSettings()->onResetToDefaultSettings();
}  // resetToDefaultSettings
//-----------------------------------------------------------------------------
void ServerLobby::writeOwnReport(std::shared_ptr<STKPeer> reporter, std::shared_ptr<STKPeer> reporting, const std::string& info)
{
#ifdef ENABLE_SQLITE3
    if (!m_db_connector->hasDatabase() || !m_db_connector->hasPlayerReportsTable())
        return;
    if (!reporting)
        reporting = reporter;
    if (!reporter->hasPlayerProfiles())
        return;
    auto reporter_npp = reporter->getPlayerProfiles()[0];

    if (info.empty())
        return;
    auto info_w = StringUtils::utf8ToWide(info);

    if (!reporting->hasPlayerProfiles())
        return;
    auto reporting_npp = reporting->getPlayerProfiles()[0];

    bool written = m_db_connector->writeReport(reporter, reporter_npp,
            reporting, reporting_npp, info_w);
    if (written)
    {
        NetworkString* success = getNetworkString();
        success->setSynchronous(true);
        if (reporter == reporting)
            success->addUInt8(LE_REPORT_PLAYER).addUInt8(1)
                .encodeString(m_game_setup->getServerNameUtf8());
        else
            success->addUInt8(LE_REPORT_PLAYER).addUInt8(1)
                .encodeString(reporting_npp->getName());
        reporter->sendPacket(success, PRM_RELIABLE);
        delete success;
    }
#endif
}   // writeOwnReport
//-----------------------------------------------------------------------------
void ServerLobby::changeColors()
{
    // We assume here that it's soccer, as it's only called
    // from tournament command
    auto peers = STKHost::get()->getPeers();
    for (auto peer : peers)
    {
        if (peer->hasPlayerProfiles())
        {
            auto pp = peer->getPlayerProfiles()[0];
            if (pp->getTeam() == KART_TEAM_RED)
            {
                getTeamManager()->setTeamInLobby(pp, KART_TEAM_BLUE);
            }
            else if (pp->getTeam() == KART_TEAM_BLUE)
            {
                getTeamManager()->setTeamInLobby(pp, KART_TEAM_RED);
            }
        }
    }
    updatePlayerList();
}   // changeColors
//-----------------------------------------------------------------------------

void ServerLobby::sendStringToPeer(std::shared_ptr<STKPeer> peer, const std::string& s)
{
    if (!peer)
    {
        sendStringToAllPeers(s);
        return;
    }
    NetworkString* chat = getNetworkString();
    chat->addUInt8(LE_CHAT);
    chat->setSynchronous(true);
    chat->encodeString16(StringUtils::utf8ToWide(s));
    peer->sendPacket(chat, PRM_RELIABLE);
    delete chat;
}   // sendStringToPeer
//-----------------------------------------------------------------------------

void ServerLobby::sendStringToAllPeers(const std::string& s)
{
    NetworkString* chat = getNetworkString();
    chat->addUInt8(LE_CHAT);
    chat->setSynchronous(true);
    chat->encodeString16(StringUtils::utf8ToWide(s));
    sendMessageToPeers(chat, PRM_RELIABLE);
    delete chat;
}   // sendStringToAllPeers
//-----------------------------------------------------------------------------

bool ServerLobby::canRace(std::shared_ptr<STKPeer> peer)
{
    auto it = m_why_peer_cannot_play.find(peer);
    if (it != m_why_peer_cannot_play.end())
        return it->second == 0;

    if (!peer || peer->getPlayerProfiles().empty())
    {
        m_why_peer_cannot_play[peer] = HR_ABSENT_PEER;
        return false;
    }
    std::string username = peer->getMainName();
    if (getTournament() && !getTournament()->canPlay(username))
    {
        m_why_peer_cannot_play[peer] = HR_NOT_A_TOURNAMENT_PLAYER;
        return false;
    }
    else if (m_spectators_by_limit.find(peer) != m_spectators_by_limit.end())
    {
        m_why_peer_cannot_play[peer] = HR_SPECTATOR_BY_LIMIT;
        return false;
    }

    if (!getSettings()->getMissingAssets(peer).empty())
    {
        m_why_peer_cannot_play[peer] = HR_LACKING_REQUIRED_MAPS;
        return false;
    }

    if (peer->addon_karts_count < getSettings()->getAddonKartsPlayThreshold())
    {
        m_why_peer_cannot_play[peer] = HR_ADDON_KARTS_PLAY_THRESHOLD;
        return false;
    }
    if (peer->addon_tracks_count < getSettings()->getAddonTracksPlayThreshold())
    {
        m_why_peer_cannot_play[peer] = HR_ADDON_TRACKS_PLAY_THRESHOLD;
        return false;
    }
    if (peer->addon_arenas_count < getSettings()->getAddonArenasPlayThreshold())
    {
        m_why_peer_cannot_play[peer] = HR_ADDON_ARENAS_PLAY_THRESHOLD;
        return false;
    }
    if (peer->addon_soccers_count < getSettings()->getAddonSoccersPlayThreshold())
    {
        m_why_peer_cannot_play[peer] = HR_ADDON_FIELDS_PLAY_THRESHOLD;
        return false;
    }

    std::set<std::string> maps = peer->getClientAssets().second;
    std::set<std::string> karts = peer->getClientAssets().first;

    float karts_fraction = getAssetManager()->officialKartsFraction(karts);
    float maps_fraction = getAssetManager()->officialMapsFraction(maps);

    if (karts_fraction < getSettings()->getOfficialKartsPlayThreshold())
    {
        m_why_peer_cannot_play[peer] = HR_OFFICIAL_KARTS_PLAY_THRESHOLD;
        return false;
    }
    if (maps_fraction < getSettings()->getOfficialTracksPlayThreshold())
    {
        m_why_peer_cannot_play[peer] = HR_OFFICIAL_TRACKS_PLAY_THRESHOLD;
        return false;
    }
    
    getAssetManager()->applyAllFilters(maps, true);
    getAssetManager()->applyAllKartFilters(username, karts, false);

    if (karts.empty())
    {
        m_why_peer_cannot_play[peer] = HR_NO_KARTS_AFTER_FILTER;
        return false;
    }
    if (maps.empty())
    {
        m_why_peer_cannot_play[peer] = HR_NO_MAPS_AFTER_FILTER;
        return false;
    }

    m_why_peer_cannot_play[peer] = 0;
    return true;
}   // canRace
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
    if (peer->isAngryHost())
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
        if (getSettings()->hasOnlyHostRiding())
            mask |= CommandPermissions::PE_SINGLE;
    }
    if (peer->isAngryHost())
    {
        mask |= CommandPermissions::PE_HAMMER;
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
    if (!m_db_connector->hasDatabase())
        return false;
    if (!m_db_connector->hasPlayerReportsTable())
        return false;
    if (!reporter->hasPlayerProfiles())
        return false;
    auto reporter_npp = reporter->getPlayerProfiles()[0];
    auto info_w = StringUtils::utf8ToWide(info);

    bool written = m_db_connector->writeReport(reporter, reporter_npp,
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

    record_fetched = m_db_connector->getBestResult(temp_info, &record_exists, &best_user, &best_result);

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
    return m_db_connector->hasPlayerReportsTable();
#else
    return false;
#endif
}   // playerReportsTableExists

//-----------------------------------------------------------------------------
void ServerLobby::saveDisconnectingPeerInfo(std::shared_ptr<STKPeer> peer) const
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
void ServerLobby::saveDisconnectingIdInfo(int id) const
{
    World* w = World::getWorld();
    FreeForAll* ffa_world = dynamic_cast<FreeForAll*>(w);
    LinearWorld* linear_world = dynamic_cast<LinearWorld*>(w);
    RemoteKartInfo& rki = RaceManager::get()->getKartInfo(id);
    int points = 0;
    m_game_info->m_player_info.emplace_back(true/* reserved */,
                                            false/* game event*/);
    std::swap(m_game_info->m_player_info.back(), m_game_info->m_player_info[id]);
    auto& info = m_game_info->m_player_info.back();
    if (RaceManager::get()->isBattleMode())
    {
        if (ffa_world)
            points = ffa_world->getKartScore(id);
        info.m_result += points;
        if (getSettings()->isPreservingBattleScores())
            m_game_info->m_saved_ffa_points[StringUtils::wideToUtf8(rki.getPlayerName())] = points;
    }
    info.m_when_left = stk_config->ticks2Time(w->getTicksSinceStart());
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

void ServerLobby::checkNoTeamSpectator(std::shared_ptr<STKPeer> peer)
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
            setSpectateModeProperly(peer, ASM_NO_TEAM);

        if (has_teamed && peer->getAlwaysSpectate() == ASM_NO_TEAM)
            setSpectateModeProperly(peer, ASM_NONE);
    }
}   // checkNoTeamSpectator

//-----------------------------------------------------------------------------
void ServerLobby::setSpectateModeProperly(std::shared_ptr<STKPeer> peer, AlwaysSpectateMode mode)
{
    auto state = m_state.load();
    bool selection_started = (state >= ServerLobby::SELECTING);
    bool no_racing_yet = (state < ServerLobby::RACING);

    peer->setDefaultAlwaysSpectate(mode);
    if (!selection_started || !no_racing_yet)
        peer->setAlwaysSpectate(mode);
    else
    {
        erasePeerReady(peer);
        peer->setAlwaysSpectate(mode);
        peer->setWaitingForGame(true);
    }
    if (mode == ASM_NONE)
        checkNoTeamSpectator(peer);
}   // setSpectateModeProperly
//-----------------------------------------------------------------------------

void ServerLobby::sendServerInfoToEveryone() const
{
    NetworkString* server_info = getNetworkString();
    server_info->setSynchronous(true);
    server_info->addUInt8(LE_SERVER_INFO);
    m_game_setup->addServerInfo(server_info);
    sendMessageToPeers(server_info);
    delete server_info;
}   // sendServerInfoToEveryone
//-----------------------------------------------------------------------------
