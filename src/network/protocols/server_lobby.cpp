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
#include "network/protocol_manager.hpp"
#include "network/protocols/connect_to_peer.hpp"
#include "network/protocols/command_manager.hpp"
#include "network/protocols/game_protocol.hpp"
#include "network/protocols/game_events_protocol.hpp"
#include "network/protocols/ranking.hpp"
#include "network/race_event_manager.hpp"
#include "network/server_config.hpp"
#include "network/stk_host.hpp"
#include "network/stk_ipv6.hpp"
#include "network/stk_peer.hpp"
#include "online/request_manager.hpp"
// #include "online/xml_request.hpp"
#include "tracks/check_manager.hpp"
#include "tracks/track.hpp"
#include "tracks/track_manager.hpp"
#include "utils/kart_elimination.hpp"
#include "utils/game_info.hpp"
#include "utils/hit_processor.hpp"
#include "utils/lobby_asset_manager.hpp"
#include "utils/lobby_queues.hpp"
#include "utils/map_vote_handler.hpp"
#include "utils/tournament.hpp"
#include "utils/translation.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <iterator>

int ServerLobby::m_default_fixed_laps = -1;
// ========================================================================

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
    m_help_message = getGameSetup()->readOrLoadFromFile
        ((std::string) ServerConfig::m_help);

    m_command_manager = std::make_shared<CommandManager>(nullptr);
    m_shuffle_gp = ServerConfig::m_shuffle_gp;
    m_current_max_players_in_game.store(ServerConfig::m_max_players_in_game);
    m_consent_on_replays = false;

    m_kart_elimination = std::make_shared<KartElimination>();
    m_map_vote_handler = std::make_shared<MapVoteHandler>();

    m_lobby_queues = std::make_shared<LobbyQueues>(this);

    m_fixed_lap = ServerConfig::m_fixed_lap_count;
    // Server config has better priority than --laps
    // as it is more flexible and was introduced earlier
    if (m_default_fixed_laps != -1) {
        m_fixed_lap = m_default_fixed_laps;
    }
    m_fixed_direction = ServerConfig::m_fixed_direction;

    m_default_lap_multiplier = ServerConfig::m_auto_game_time_ratio;

    m_troll_active = ServerConfig::m_troll_active;

    m_hit_processor = std::make_shared<HitProcessor>(this);

    m_asset_manager = std::make_shared<LobbyAssetManager>(this);

    m_available_teams = ServerConfig::m_init_available_teams;

    m_map_vote_handler->setAlgorithm(ServerConfig::m_map_vote_handling);

    initAvailableModes();

    m_current_ai_count.store(0);
    m_asset_manager->init();
    m_asset_manager->updateAddons();

    initAvailableTracks();

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
    m_default_vote = new PeerVote();
#ifdef ENABLE_SQLITE3
    m_db_connector = new DatabaseConnector();
    m_db_connector->initDatabase();
#endif
    initCategories();
    if (ServerConfig::m_soccer_tournament)
    {
        m_tournament = std::make_shared<Tournament>(this);
    }
    m_allowed_to_start = ServerConfig::m_allowed_to_start;
    m_game_info = nullptr;
    std::string scoring = ServerConfig::m_gp_scoring;
    loadCustomScoring(scoring);
    loadWhiteList();
    loadPreservedSettings();
#ifdef ENABLE_WEB_SUPPORT
    m_token_generation_tries.store(0);
    loadAllTokens();
#endif
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
    delete m_default_vote;

#ifdef ENABLE_SQLITE3
    m_db_connector->destroyDatabase();
    delete m_db_connector;
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
    RaceManager::MinorRaceModeType m =
        ServerConfig::getLocalGameMode(m_game_mode.load()).first;
    m_asset_manager->updateMapsForMode(m);
}   // updateMapsForMode

//-----------------------------------------------------------------------------
void ServerLobby::setup()
{
    LobbyProtocol::setup();
    m_battle_hit_capture_limit = 0;
    m_battle_time_limit = 0.0f;
    m_item_seed = 0;
    m_winner_peer_id = 0;
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
    m_asset_manager->onServerSetup();
    NetworkConfig::get()->setTuxHitboxAddon(ServerConfig::m_live_players);
    updateMapsForMode();

    m_server_has_loaded_world.store(false);

    // Initialise the data structures to detect if all clients and 
    // the server are ready:
    resetPeersReady();
    m_timeout.store(std::numeric_limits<int64_t>::max());
    m_server_started_at = m_server_delay = 0;
    getCommandManager()->onResetServer();
    if (m_game_info)
        delete m_game_info;
    m_game_info = nullptr;
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
    case LE_RACE_FINISHED_ACK: playerFinishedResult(event);   break;
    case LE_LIVE_JOIN:         liveJoinRequest(event);        break;
    case LE_CLIENT_LOADED_WORLD: finishedLoadingLiveJoinClient(event); break;
    case LE_KART_INFO: handleKartInfo(event); break;
    case LE_CLIENT_BACK_LOBBY: clientInGameWantsToBackLobby(event); break;
    default: Log::error("ServerLobby", "Unknown message of type %d - ignored.",
                        message_type);
             break;
    }   // switch message_type
    return true;
}   // notifyEvent

//-----------------------------------------------------------------------------
void ServerLobby::handleChat(Event* event)
{
    if (!checkDataSize(event, 1) || !ServerConfig::m_chat) return;

    // Update so that the peer is not kicked
    event->getPeer()->updateLastActivity();
    const bool sender_in_game = event->getPeer()->isWaitingForGame();

    int64_t last_message = event->getPeer()->getLastMessage();
    int64_t elapsed_time = (int64_t)StkTime::getMonoTimeMs() - last_message;

    // Read ServerConfig for formula and details
    if (ServerConfig::m_chat_consecutive_interval > 0 &&
        elapsed_time < ServerConfig::m_chat_consecutive_interval * 1000)
        event->getPeer()->updateConsecutiveMessages(true);
    else
        event->getPeer()->updateConsecutiveMessages(false);

    if (ServerConfig::m_chat_consecutive_interval > 0 &&
        event->getPeer()->getConsecutiveMessages() >
        ServerConfig::m_chat_consecutive_interval / 2)
    {
        std::string msg = "Spam detected";
        sendStringToPeer(msg, event->getPeerSP());
        return;
    }

    core::stringw message;
    event->data().decodeString16(&message, 360/*max_len*/);

    // Check if the message starts with "(the name of main profile): " to prevent
    // impersonation, see #5121.
    std::string message_utf8 = StringUtils::wideToUtf8(message);
    std::string prefix = StringUtils::wideToUtf8(
        event->getPeer()->getPlayerProfiles()[0]->getName()) + ": ";
    
    if (!StringUtils::startsWith(message_utf8, prefix))
    {
        std::string warn = "Don't try to impersonate others!";
        sendStringToPeer(warn, event->getPeerSP());
        return;
    }

    KartTeam target_team = KART_TEAM_NONE;
    if (event->data().size() > 0)
        target_team = (KartTeam)event->data().getUInt8();

    if (message.size() > 0)
    {
        bool add_red_emoji = false;
        bool add_blue_emoji = false;
        // Red or blue square emoji
        if (target_team == KART_TEAM_RED)
            add_red_emoji = true;
        else if (target_team == KART_TEAM_BLUE)
            add_blue_emoji = true;

        NetworkString* chat = getNetworkString();
        chat->setSynchronous(true);
        const bool game_started = m_state.load() != WAITING_FOR_START_GAME;
        std::shared_ptr<STKPeer> sender = event->getPeerSP();
        auto can_receive = m_message_receivers[sender];
        if (!can_receive.empty())
            message = StringUtils::utf32ToWide({0x1f512, 0x20}) + message;
        bool team_speak = m_team_speakers.find(sender) != m_team_speakers.end();
        team_speak &= (
            RaceManager::get()->getMinorMode() == RaceManager::MINOR_MODE_SOCCER ||
            RaceManager::get()->getMinorMode() == RaceManager::MINOR_MODE_CAPTURE_THE_FLAG
        );
        std::set<KartTeam> teams;
        for (auto& profile: sender->getPlayerProfiles())
            teams.insert(profile->getTeam());
        if (team_speak)
        {
            for (auto &team: teams)
            {
                if (team == KART_TEAM_RED)
                    add_red_emoji = true;
                else if (team == KART_TEAM_BLUE)
                    add_blue_emoji = true;
            }
        }
        if (add_blue_emoji)
            message = StringUtils::utf32ToWide({0x1f7e6, 0x20}) + message;
        if (add_red_emoji)
            message = StringUtils::utf32ToWide({0x1f7e5, 0x20}) + message;
        bool tournament_limit = false;
        if (m_tournament)
            tournament_limit = !m_tournament->checkSenderInRefsOrPlayers(sender);
        std::set<std::string> sees_teamchats;
        if (m_tournament)
            sees_teamchats = m_tournament->getThoseWhoSeeTeamchats();
            
        // Note that mutealls are still spectators
        std::set<std::string> important_players;
        if (m_tournament && tournament_limit)
        {
            important_players = m_tournament->getImportantChatPlayers();
        }
        chat->addUInt8(LE_CHAT).encodeString16(message);
        core::stringw sender_name =
            event->getPeer()->getPlayerProfiles()[0]->getName();

        STKHost::get()->sendPacketToAllPeersWith(
            [game_started, sender_in_game, target_team, can_receive,
                sender, team_speak, teams, tournament_limit,
                important_players, sender_name, sees_teamchats, this](std::shared_ptr<STKPeer> p)
            {
                if (sender == p)
                    return true;
                if (game_started)
                {
                    if (p->isWaitingForGame() && !sender_in_game)
                        return false;
                    if (!p->isWaitingForGame() && sender_in_game)
                        return false;
                    if (tournament_limit)
                    {
                        bool all_are_important = true;
                        for (auto& player : p->getPlayerProfiles())
                        {
                            std::string name = StringUtils::wideToUtf8(
                                player->getName());
                            if (important_players.count(name) == 0)
                            {
                                all_are_important = false;
                                break;
                            }
                        }
                        if (all_are_important)
                            return false;
                    }
                    if (target_team != KART_TEAM_NONE)
                    {
                        if (p->isSpectator())
                            return false;
                        bool someone_good = false;
                        for (auto& player : p->getPlayerProfiles())
                        {
                            if (player->getTeam() == target_team)
                                someone_good = true;
                            std::string name = StringUtils::wideToUtf8(
                                player->getName());
                            if (sees_teamchats.count(name))
                                someone_good = true;
                        }
                        if (!someone_good)
                            return false;
                    }
                }
                for (auto& peer : m_peers_muted_players)
                {
                    if (auto peer_sp = peer.first.lock())
                    {
                        if (peer_sp == p &&
                            peer.second.find(sender_name) != peer.second.end())
                            return false;
                    }
                }
                if (team_speak)
                {
                    bool someone_good = false;
                    for (auto& profile: p->getPlayerProfiles())
                    {
                        if (teams.count(profile->getTeam()) > 0)
                            someone_good = true;
                        std::string name = StringUtils::wideToUtf8(
                            profile->getName());
                        if (sees_teamchats.count(name))
                            someone_good = true;
                    }
                    if (!someone_good)
                        return false;
                }
                if (can_receive.empty())
                    return true;
                for (auto& profile : p->getPlayerProfiles())
                {
                    if (can_receive.find(profile->getName()) !=
                        can_receive.end())
                    {
                        return true;
                    }
                }
                return false;
            }, chat);
        event->getPeer()->updateLastMessage();
        delete chat;
    }
}   // handleChat

//-----------------------------------------------------------------------------
void ServerLobby::changeTeam(Event* event)
{
    if (!ServerConfig::m_team_choosing ||
        !RaceManager::get()->teamEnabled())
        return;
    if (!checkDataSize(event, 1)) return;
    NetworkString& data = event->data();
    uint8_t local_id = data.getUInt8();
    auto& player = event->getPeer()->getPlayerProfiles().at(local_id);
    auto red_blue = STKHost::get()->getAllPlayersTeamInfo();
    if (m_tournament && !m_tournament->canChangeTeam())
    {
        Log::info("ServerLobby", "Team change requested by %s, but tournament forbids it.", player->getName().c_str());
        return;
    }

    // For now, the change team button will still only work in soccer + CTF.
    // Further logic might be added later, but now it seems to be complicated
    // because there's a restriction of 7 for those modes, and unnecessary
    // because we don't have client changes.

    // At most 7 players on each team (for live join)
    if (player->getTeam() == KART_TEAM_BLUE)
    {
        if (red_blue.first >= 7 && !ServerConfig::m_free_teams)
            return;
        setTeamInLobby(player, KART_TEAM_RED);
    }
    else
    {
        if (red_blue.second >= 7 && !ServerConfig::m_free_teams)
            return;
        setTeamInLobby(player, KART_TEAM_BLUE);
    }
    updatePlayerList();
}   // changeTeam

//-----------------------------------------------------------------------------
void ServerLobby::kickHost(Event* event)
{
    if (m_server_owner.lock() != event->getPeerSP())
        return;
    if (!ServerConfig::m_kicks_allowed)
    {
        std::string msg = "Kicking players is not allowed on this server";
        auto crown = event->getPeerSP();
        sendStringToPeer(msg, crown);
        return;
    }
    if (!checkDataSize(event, 4)) return;
    NetworkString& data = event->data();
    uint32_t host_id = data.getUInt32();
    std::shared_ptr<STKPeer> peer = STKHost::get()->findPeerByHostId(host_id);
    // Ignore kicking ai peer if ai handling is on
    if (peer && (!ServerConfig::m_ai_handling || !peer->isAIPeer()))
    {
        if (peer->isAngryHost())
        {
            std::string msg = "This player is the owner of this server, "
                "and is protected from your actions now";
            auto crown = event->getPeerSP();
            sendStringToPeer(msg, crown);
            return;
        }
        if (!peer->hasPlayerProfiles())
        {
            Log::info("ServerLobby", "Crown player kicks a player");
        }
        else
        {
            auto npp = peer->getPlayerProfiles()[0];
            std::string player_name = StringUtils::wideToUtf8(npp->getName());
            Log::info("ServerLobby", "Crown player kicks %s", player_name.c_str());
        }
        peer->kick();
        if (ServerConfig::m_track_kicks)
        {
            std::string auto_report = "[ Auto report caused by kick ]";
            writeOwnReport(peer, event->getPeerSP(), auto_report);
        }
    }
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
        case LE_CONNECTION_REQUESTED: connectionRequested(event); break;
        case LE_KART_SELECTION: kartSelectionRequested(event);    break;
        case LE_CLIENT_LOADED_WORLD: finishedLoadingWorldClient(event); break;
        case LE_VOTE: handlePlayerVote(event);                    break;
        case LE_KICK_HOST: kickHost(event);                       break;
        case LE_CHANGE_TEAM: changeTeam(event);                   break;
        case LE_REQUEST_BEGIN: startSelection(event);             break;
        case LE_CHAT: handleChat(event);                          break;
        case LE_CONFIG_SERVER: handleServerConfiguration(event);  break;
        case LE_CHANGE_HANDICAP: changeHandicap(event);           break;
        case LE_CLIENT_BACK_LOBBY:
            clientSelectingAssetsWantsToBackLobby(event);         break;
        case LE_REPORT_PLAYER: writePlayerReport(event);          break;
        case LE_ASSETS_UPDATE:
            handleAssets(event->data(), event->getPeerSP());      break;
        case LE_COMMAND:
            handleServerCommand(event, event->getPeerSP());       break;
        default:                                                  break;
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
    if (!ServerConfig::m_sql_management || !m_db_connector->hasDatabase())
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

    for (auto it = m_peers_muted_players.begin();
        it != m_peers_muted_players.end();)
    {
        if (it->first.expired())
            it = m_peers_muted_players.erase(it);
        else
            it++;
    }

#ifdef ENABLE_SQLITE3
    pollDatabase();
#endif

    // Check if server owner has left
    updateServerOwner();

    if (ServerConfig::m_ranked && m_state.load() == WAITING_FOR_START_GAME)
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
        if (ServerConfig::m_owner_less)
        {
            // Ensure that a game can auto-start if the server meets the config's starting limit or if it's already full.
            int starting_limit = std::min((int)ServerConfig::m_min_start_game_players, (int)ServerConfig::m_server_max_players);
            unsigned current_max_players_in_game = m_current_max_players_in_game.load();
            if (current_max_players_in_game > 0) // 0 here means it's not the limit
                starting_limit = std::min(starting_limit, (int)current_max_players_in_game);

            unsigned players = 0;
            STKHost::get()->updatePlayers(&players);
            if (((int)players >= starting_limit ||
                m_game_setup->isGrandPrixStarted()) &&
                m_timeout.load() == std::numeric_limits<int64_t>::max())
            {
                if (ServerConfig::m_start_game_counter >= -1e-5)
                {
                    m_timeout.store((int64_t)StkTime::getMonoTimeMs() +
                        (int64_t)
                        (ServerConfig::m_start_game_counter * 1000.0f));
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
            if (m_tournament && m_tournament->forbidStarting())
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
        if ((player_in_game == 1 && ServerConfig::m_ranked) ||
            player_in_game == 0)
        {
            resetVotingTime();
            return;
        }

        // m_server_has_loaded_world is set by main thread with atomic write
        if (m_server_has_loaded_world.load() == false)
            return;
        if (!checkPeersReady(
            ServerConfig::m_ai_handling && m_ai_count == 0/*ignore_ai_peer*/, LOADING_WORLD))
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
        if ((player_in_game == 1 && ServerConfig::m_ranked) ||
            player_in_game == 0)
        {
            resetVotingTime();
            return;
        }

        PeerVote winner_vote;
        m_winner_peer_id = std::numeric_limits<uint32_t>::max();
        bool go_on_race = false;
        if (ServerConfig::m_track_voting)
            go_on_race = handleAllVotes(&winner_vote, &m_winner_peer_id);
        else if (/*m_game_setup->isGrandPrixStarted() || */isVotingOver())
        {
            winner_vote = *m_default_vote;
            go_on_race = true;
        }
        if (go_on_race)
        {
            if (m_fixed_lap >= 0)
            {
                winner_vote.m_num_laps = m_fixed_lap;
                Log::info("ServerLobby", "Enforcing %d lap race", (int)m_fixed_lap);
            }
            if (m_fixed_direction >= 0)
            {
                winner_vote.m_reverse = (m_fixed_direction == 1);
                Log::info("ServerLobby", "Enforcing direction %d", (int)m_fixed_direction);
            }
            *m_default_vote = winner_vote;
            m_item_seed = (uint32_t)StkTime::getTimeSinceEpoch();
            ItemManager::updateRandomSeed(m_item_seed);
            float extra_seconds = 0.0f;
            if (m_tournament)
                extra_seconds = m_tournament->getExtraSeconds();
            m_game_setup->setRace(winner_vote, extra_seconds);

            // For spectators that don't have the track, remember their
            // spectate mode and don't load the track
            std::string track_name = winner_vote.m_track_name;
            if (m_tournament)
                m_tournament->fillNextArena(track_name);
            
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
            m_game_setup->sortPlayersForGrandPrix(players, m_shuffle_gp);
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
                if (m_lobby_queues->areKartFiltersIgnoringKarts())
                    current_kart = "";
                std::string name = StringUtils::wideToUtf8(players[i]->getName());
                // Note 1: setKartName also resets KartData, and should be called
                // only if current kart name is not suitable.
                // Note 2: filters only support standard karts for now, so GKFBKC
                // cannot return an addon; when addons are supported, this part of
                // code will also have to provide kart data (or setKartName has to
                // set the correct hitbox itself).
                std::string new_kart = getKartForBadKartChoice(
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
            m_game_setup->setHitCaptureTime(m_battle_hit_capture_limit,
                m_battle_time_limit);
            uint16_t flag_return_time = (uint16_t)stk_config->time2Ticks(
                ServerConfig::m_flag_return_timeout);
            RaceManager::get()->setFlagReturnTicks(flag_return_time);
            if (ServerConfig::m_record_replays && m_consent_on_replays &&
                (RaceManager::get()->getMinorMode() == RaceManager::MINOR_MODE_TIME_TRIAL ||
                RaceManager::get()->getMinorMode() == RaceManager::MINOR_MODE_NORMAL_RACE))
                RaceManager::get()->setRecordRace(true);
            uint16_t flag_deactivated_time = (uint16_t)stk_config->time2Ticks(
                ServerConfig::m_flag_deactivated_time);
            RaceManager::get()->setFlagDeactivatedTicks(flag_deactivated_time);
            configRemoteKart(players, 0);

            // Reset for next state usage
            resetPeersReady();

            m_state = LOAD_WORLD;
            sendMessageToPeers(load_world_message);
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
            m_game_info = new GameInfo();
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
void ServerLobby::encodePlayers(BareNetworkString* bns,
        std::vector<std::shared_ptr<NetworkPlayerProfile> >& players) const
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
            .addUInt8(player->getLocalPlayerId())
            .addUInt8(player->getTeam())
            .encodeString(player->getCountryCode());
        bns->encodeString(player->getKartName());
    }
}   // encodePlayers

//-----------------------------------------------------------------------------
NetworkString* ServerLobby::getLoadWorldMessage(
    std::vector<std::shared_ptr<NetworkPlayerProfile> >& players,
    bool live_join) const
{
    NetworkString* load_world_message = getNetworkString();
    load_world_message->setSynchronous(true);
    load_world_message->addUInt8(LE_LOAD_WORLD);
    load_world_message->addUInt32(m_winner_peer_id);
    m_default_vote->encode(load_world_message);
    load_world_message->addUInt8(live_join ? 1 : 0);
    encodePlayers(load_world_message, players);
    load_world_message->addUInt32(m_item_seed);
    if (RaceManager::get()->isBattleMode())
    {
        load_world_message->addUInt32(m_battle_hit_capture_limit)
            .addFloat(m_battle_time_limit);
        uint16_t flag_return_time = (uint16_t)stk_config->time2Ticks(
            ServerConfig::m_flag_return_timeout);
        load_world_message->addUInt16(flag_return_time);
        uint16_t flag_deactivated_time = (uint16_t)stk_config->time2Ticks(
            ServerConfig::m_flag_deactivated_time);
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
    bool live_join = ServerConfig::m_live_players && worldIsActive();
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
/** Returns true if world is active for clients to live join, spectate or
 *  going back to lobby live
 */
bool ServerLobby::worldIsActive() const
{
    return World::getWorld() && RaceEventManager::get()->isRunning() &&
        !RaceEventManager::get()->isRaceOver() &&
        World::getWorld()->getPhase() == WorldStatus::RACE_PHASE;
}   // worldIsActive

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
    std::shared_ptr<STKPeer> peer = event->getPeerSP();
    const NetworkString& data = event->data();

    if (!canLiveJoinNow())
    {
        rejectLiveJoin(peer, BLR_NO_GAME_FOR_LIVE_JOIN);
        return;
    }
    bool spectator = data.getUInt8() == 1;
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
            (spectators_by_limit.find(event->getPeerSP()) != spectators_by_limit.end()))
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
/** Get a list of current ingame players for live join or spectate.
 */
std::vector<std::shared_ptr<NetworkPlayerProfile> >
                                            ServerLobby::getLivePlayers() const
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
            if (ServerConfig::m_team_choosing)
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
                    setTeamInLobby(p, target_team);
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
            info.m_team = (int8_t)rki.getKartTeam();
            if (info.m_team == KartTeam::KART_TEAM_NONE)
            {
                auto npp = rki.getNetworkPlayerProfile().lock();
                if (npp)
                    info.m_team = npp->getTemporaryTeam() - 1;
            }
            if (RaceManager::get()->isBattleMode())
            {
                if (ServerConfig::m_preserve_battle_scores)
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
    int sec = ServerConfig::m_kick_idle_player_seconds;
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
            if (m_troll_active && !peer->isAIPeer())
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
                            std::string msg = ServerConfig::m_troll_warn_msg;
                            sendStringToPeer(msg, peer);
                            std::string player_name = StringUtils::wideToUtf8(peer->getPlayerProfiles()[0]->getName());
                            Log::info("ServerLobby-AntiTroll", "Sent WARNING to %s", player_name.c_str());
                            break;
                        }
                        default:
                        {
                            std::string player_name = StringUtils::wideToUtf8(peer->getPlayerProfiles()[0]->getName());
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
        sec = ServerConfig::m_kick_idle_lobby_player_seconds;
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
                    peer_name = StringUtils::wideToUtf8(
                            peer->getPlayerProfiles()[0]->getName()).c_str();
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
    if (ServerConfig::m_ranked &&
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
        Log::info("ServerLobbyRoom", "Starting the race loading.");
        // This will create the world instance, i.e. load track and karts
        loadWorld();
        updateWorldSettings();
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
    // ========================================================================
    class RegisterServerRequest : public Online::XMLRequest
    {
    private:
        std::weak_ptr<ServerLobby> m_server_lobby;
        bool m_first_time;
    protected:
        virtual void afterOperation()
        {
            Online::XMLRequest::afterOperation();
            const XMLNode* result = getXMLData();
            std::string rec_success;
            auto sl = m_server_lobby.lock();
            if (!sl)
                return;

            if (result->get("success", &rec_success) &&
                rec_success == "yes")
            {
                const XMLNode* server = result->getNode("server");
                assert(server);
                const XMLNode* server_info = server->getNode("server-info");
                assert(server_info);
                unsigned server_id_online = 0;
                server_info->get("id", &server_id_online);
                assert(server_id_online != 0);
                bool is_official = false;
                server_info->get("official", &is_official);
                if (!is_official && ServerConfig::m_ranked)
                {
                    Log::fatal("ServerLobby", "You don't have permission to "
                        "host a ranked server.");
                }
                Log::info("ServerLobby",
                    "Server %d is now online.", server_id_online);
                sl->m_server_id_online.store(server_id_online);
                sl->m_last_success_poll_time.store(StkTime::getMonoTimeMs());
                return;
            }
            Log::error("ServerLobby", "%s",
                StringUtils::wideToUtf8(getInfo()).c_str());
            // Exit now if failed to register to stk addons for first time
            if (m_first_time)
                sl->m_state.store(ERROR_LEAVE);
        }
    public:
        RegisterServerRequest(std::shared_ptr<ServerLobby> sl, bool first_time)
        : XMLRequest(Online::RequestManager::HTTP_MAX_PRIORITY),
        m_server_lobby(sl), m_first_time(first_time) {}
    };   // RegisterServerRequest

    auto request = std::make_shared<RegisterServerRequest>(
        std::dynamic_pointer_cast<ServerLobby>(shared_from_this()), first_time);
    NetworkConfig::get()->setServerDetails(request, "create");
    const SocketAddress& addr = STKHost::get()->getPublicAddress();
    request->addParameter("address",      addr.getIP()        );
    request->addParameter("port",         addr.getPort()      );
    request->addParameter("private_port",
                                    STKHost::get()->getPrivatePort()      );
    request->addParameter("name", m_game_setup->getServerNameUtf8());
    request->addParameter("max_players", ServerConfig::m_server_max_players);
    int difficulty = m_difficulty.load();
    request->addParameter("difficulty", difficulty);
    int game_mode = m_game_mode.load();
    request->addParameter("game_mode", game_mode);
    const std::string& pw = ServerConfig::m_private_server_password;
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
    // ========================================================================
    class UnRegisterServerRequest : public Online::XMLRequest
    {
    private:
        std::weak_ptr<ServerLobby> m_server_lobby;
    protected:
        virtual void afterOperation()
        {
            Online::XMLRequest::afterOperation();
            const XMLNode* result = getXMLData();
            std::string rec_success;

            if (result->get("success", &rec_success) &&
                rec_success == "yes")
            {
                // Clear the server online for next register
                // For grand prix server
                if (auto sl = m_server_lobby.lock())
                    sl->m_server_id_online.store(0);
                return;
            }
            Log::error("ServerLobby", "%s",
                StringUtils::wideToUtf8(getInfo()).c_str());
        }
    public:
        UnRegisterServerRequest(std::weak_ptr<ServerLobby> sl)
        : XMLRequest(Online::RequestManager::HTTP_MAX_PRIORITY),
        m_server_lobby(sl) {}
    };   // UnRegisterServerRequest
    auto request = std::make_shared<UnRegisterServerRequest>(sl);
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
        if (ServerConfig::m_sleeping_server)
        {
            Log::warn("ServerLobby",
                "An attempt to start a race on a sleeping server. Lol.");
            return;
        }
        auto peer = event->getPeerSP();
        if (ServerConfig::m_owner_less)
        {
            if (!m_allowed_to_start)
            {
                std::string msg = "Starting the game is forbidden by server owner";
                sendStringToPeer(msg, peer);
                return;
            }
            if (!canRace(peer))
            {
                std::string msg = "You cannot play so pressing ready has no action";
                sendStringToPeer(msg, peer);
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
        if (!m_allowed_to_start)
        {
            std::string msg = "Starting the game is forbidden by server owner";
            sendStringToPeer(msg, peer);
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
        if (!m_allowed_to_start)
        {
            // Produce no log spam
            return;
        }
    }

    if (!ServerConfig::m_owner_less && ServerConfig::m_team_choosing &&
        !ServerConfig::m_free_teams && RaceManager::get()->teamEnabled())
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
            std::string msg = "No one can play!";
            sendStringToPeer(msg, event->getPeerSP());
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

    m_asset_manager->eraseAssetsWithPeers(erasingPeers);

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

    if (!m_asset_manager->tryApplyingMapFilters())
    {
        Log::error("ServerLobby", "No tracks for playing!");
        return;
    }

    m_default_vote->m_track_name = m_asset_manager->getRandomAvailableMap();
    RandomGenerator rg;
    switch (RaceManager::get()->getMinorMode())
    {
        case RaceManager::MINOR_MODE_NORMAL_RACE:
        case RaceManager::MINOR_MODE_TIME_TRIAL:
        case RaceManager::MINOR_MODE_FOLLOW_LEADER:
        {
            Track* t = track_manager->getTrack(m_default_vote->m_track_name);
            assert(t);
            m_default_vote->m_num_laps = t->getDefaultNumberOfLaps();
            if (ServerConfig::m_auto_game_time_ratio > 0.0f)
            {
                m_default_vote->m_num_laps =
                    (uint8_t)(fmaxf(1.0f, (float)t->getDefaultNumberOfLaps() *
                    ServerConfig::m_auto_game_time_ratio));
            }
            else if (m_fixed_lap >= 0)
                m_default_vote->m_num_laps = m_fixed_lap;
            m_default_vote->m_reverse = rg.get(2) == 0;
            if (m_fixed_direction >= 0)
                m_default_vote->m_reverse = (m_fixed_direction == 1);
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
            if (m_tournament)
            {
                m_tournament->applyRestrictionsOnDefaultVote(m_default_vote);
            }
            else
            {
                if (m_game_setup->isSoccerGoalTarget())
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

    if (!allowJoinedPlayersWaiting())
    {
        ProtocolManager::lock()->findAndTerminate(PROTOCOL_CONNECTION);
        if (m_server_id_online.load() != 0)
        {
            unregisterServer(false/*now*/,
                std::dynamic_pointer_cast<ServerLobby>(shared_from_this()));
        }
    }

    startVotingPeriod(ServerConfig::m_voting_timeout);

    std::string ignored_choice_string = "The server will ignore your kart choice";

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
           .addFloat(ServerConfig::m_voting_timeout)
           .addUInt8(/*m_game_setup->isGrandPrixStarted() ? 1 : */0)
           .addUInt8((m_fixed_lap >= 0
                || m_default_lap_multiplier > 0.0f) ? 1 : 0)
           .addUInt8(ServerConfig::m_track_voting ? 1 : 0);


        std::set<std::string> all_k = peer->getClientAssets().first;
        std::string username = StringUtils::wideToUtf8(peer->getPlayerProfiles()[0]->getName());
        // std::string username = StringUtils::wideToUtf8(profile->getName());
        applyAllKartFilters(username, all_k);

        if (!m_kart_elimination->getRemainingParticipants().empty() && m_kart_elimination->getRemainingParticipants().count(username) == 0)
        {
            if (all_k.count(m_kart_elimination->getKart()))
                all_k = {m_kart_elimination->getKart()};
            else
                all_k = {};
        }

        m_asset_manager->encodePlayerKartsAndCommonMaps(ns, all_k);

        peer->sendPacket(ns, PRM_RELIABLE);
        delete ns;

        if (m_lobby_queues->areKartFiltersIgnoringKarts())
            sendStringToPeer(ignored_choice_string, peer);
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
    if (!m_game_setup->isGrandPrixStarted())
    {
        m_gp_scores.clear();
        m_gp_team_scores.clear();
    }

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
    if (ServerConfig::m_firewalled_server)
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

    // ========================================================================
    class PollServerRequest : public Online::XMLRequest
    {
    private:
        std::weak_ptr<ServerLobby> m_server_lobby;
        std::weak_ptr<ProtocolManager> m_protocol_manager;
    protected:
        virtual void afterOperation()
        {
            Online::XMLRequest::afterOperation();
            const XMLNode* result = getXMLData();
            std::string success;

            if (!result->get("success", &success) || success != "yes")
            {
                Log::error("ServerLobby", "Poll server request failed: %s",
                    StringUtils::wideToUtf8(getInfo()).c_str());
                return;
            }

            // Now start a ConnectToPeer protocol for each connection request
            const XMLNode * users_xml = result->getNode("users");
            std::map<uint32_t, KeyData> keys;
            auto sl = m_server_lobby.lock();
            if (!sl)
                return;
            sl->m_last_success_poll_time.store(StkTime::getMonoTimeMs());
            if (sl->m_state.load() != WAITING_FOR_START_GAME &&
                !sl->allowJoinedPlayersWaiting())
            {
                sl->replaceKeys(keys);
                return;
            }

            sl->removeExpiredPeerConnection();
            for (unsigned int i = 0; i < users_xml->getNumNodes(); i++)
            {
                uint32_t addr, id;
                uint16_t port;
                std::string ipv6;
                users_xml->getNode(i)->get("ip", &addr);
                users_xml->getNode(i)->get("ipv6", &ipv6);
                users_xml->getNode(i)->get("port", &port);
                users_xml->getNode(i)->get("id", &id);
                users_xml->getNode(i)->get("aes-key", &keys[id].m_aes_key);
                users_xml->getNode(i)->get("aes-iv", &keys[id].m_aes_iv);
                users_xml->getNode(i)->get("username", &keys[id].m_name);
                users_xml->getNode(i)->get("country-code",
                    &keys[id].m_country_code);
                keys[id].m_tried = false;
                if (ServerConfig::m_firewalled_server)
                {
                    SocketAddress peer_addr(addr, port);
                    if (!ipv6.empty())
                        peer_addr.init(ipv6, port);
                    peer_addr.convertForIPv6Socket(isIPv6Socket());
                    std::string peer_addr_str = peer_addr.toString();
                    if (sl->m_pending_peer_connection.find(peer_addr_str) !=
                        sl->m_pending_peer_connection.end())
                    {
                        continue;
                    }
                    auto ctp = std::make_shared<ConnectToPeer>(peer_addr);
                    if (auto pm = m_protocol_manager.lock())
                        pm->requestStart(ctp);
                    sl->addPeerConnection(peer_addr_str);
                }
            }
            sl->replaceKeys(keys);
        }
    public:
        PollServerRequest(std::shared_ptr<ServerLobby> sl,
                          std::shared_ptr<ProtocolManager> pm)
        : XMLRequest(Online::RequestManager::HTTP_MAX_PRIORITY),
        m_server_lobby(sl), m_protocol_manager(pm)
        {
            m_disable_sending_log = true;
        }
    };   // PollServerRequest
    // ========================================================================

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

    if (m_tournament)
        m_tournament->onRaceFinished();

    if (RaceManager::get()->getMinorMode() ==
        RaceManager::MINOR_MODE_SOCCER)
        Log::info("ServerLobby", "SoccerMatchLog: The game is considered finished.");
    else
        Log::info("ServerLobby", "The game is considered finished.");
    // notify the network world that it is stopped
    RaceEventManager::get()->stop();

    // stop race protocols before going back to lobby (end race)
    RaceEventManager::get()->getProtocol()->requestTerminate();
    GameProtocol::lock()->requestTerminate();

    // Save race result before delete the world
    m_result_ns->clear();
    m_result_ns->addUInt8(LE_RACE_FINISHED);
    std::vector<float> gp_changes;
    if (m_game_setup->isGrandPrix())
    {
        // fastest lap
        int fastest_lap =
            static_cast<LinearWorld*>(World::getWorld())->getFastestLapTicks();
        m_result_ns->addUInt32(fastest_lap);
        irr::core::stringw fastest_kart_wide =
            static_cast<LinearWorld*>(World::getWorld())
            ->getFastestLapKartName();
        m_result_ns->encodeString(fastest_kart_wide);
        std::string fastest_kart = StringUtils::wideToUtf8(fastest_kart_wide);

        int points_fl = 0;
        // Commented until used to remove the warning
        // int points_pole = 0;
        WorldWithRank *wwr = dynamic_cast<WorldWithRank*>(World::getWorld());
        if (wwr)
        {
            points_fl = wwr->getFastestLapPoints();
            // Commented until used to remove the warning
            // points_pole = wwr->getPolePoints();
        }
        else
        {
            Log::error("ServerLobby",
                       "World with scores that is not a WorldWithRank??");
        }

        // all gp tracks
        m_result_ns->addUInt8((uint8_t)m_game_setup->getTotalGrandPrixTracks())
            .addUInt8((uint8_t)m_game_setup->getAllTracks().size());
        for (const std::string& gp_track : m_game_setup->getAllTracks())
            m_result_ns->encodeString(gp_track);

        // each kart score and total time
        m_result_ns->addUInt8((uint8_t)RaceManager::get()->getNumPlayers());
        for (unsigned i = 0; i < RaceManager::get()->getNumPlayers(); i++)
        {
            int last_score = (World::getWorld()->getKart(i)->isEliminated() ?
                    0 : RaceManager::get()->getKartScore(i));
            gp_changes.push_back((float)last_score);
            int cur_score = last_score;
            float overall_time = RaceManager::get()->getOverallTime(i);
            std::string username = StringUtils::wideToUtf8(
                RaceManager::get()->getKartInfo(i).getPlayerName());
            if (username == fastest_kart)
            {
                gp_changes.back() += points_fl;
                cur_score += points_fl;
            }
            int team = m_team_for_player[username];
            if (team > 0)
            {
                m_gp_team_scores[team].score += cur_score;
                m_gp_team_scores[team].time += overall_time;
            }
            last_score = m_gp_scores[username].score;
            cur_score += last_score;
            overall_time = overall_time + m_gp_scores[username].time;
            if (auto player =
                RaceManager::get()->getKartInfo(i).getNetworkPlayerProfile().lock())
            {
                player->setScore(cur_score);
                player->setOverallTime(overall_time);
            }
            m_gp_scores[username].score = cur_score;
            m_gp_scores[username].time = overall_time;
            m_result_ns->addUInt32(last_score).addUInt32(cur_score)
                .addFloat(overall_time);            
        }
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
    if (ServerConfig::m_ranked && RaceManager::get()->modeHasLaps())
        ranking_changes_indication = 1;
    if (m_game_setup->isGrandPrix())
        ranking_changes_indication = 1;
    m_result_ns->addUInt8(ranking_changes_indication);

    if (m_kart_elimination->isEnabled())
    {
        // ServerLobby's function because we need to take
        // the list of players from somewhere
        updateGnuElimination();
    }

    if (ServerConfig::m_store_results)
    {
        storeResults();
    }

    if (ServerConfig::m_ranked)
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

    m_map_history.push_back(RaceManager::get()->getTrackName());

    m_lobby_queues->popOnRaceFinished();
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
    m_message_receivers.erase(peer);
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
bool ServerLobby::handleAssets(const NetworkString& ns, std::shared_ptr<STKPeer> peer)
{
    std::set<std::string> client_karts, client_tracks;
    const unsigned kart_num = ns.getUInt16();
    const unsigned track_num = ns.getUInt16();
    for (unsigned i = 0; i < kart_num; i++)
    {
        std::string kart;
        ns.decodeString(&kart);
        client_karts.insert(kart);
    }
    for (unsigned i = 0; i < track_num; i++)
    {
        std::string track;
        ns.decodeString(&track);
        client_tracks.insert(track);
    }

    if (!m_asset_manager->handleAssetsForPeer(peer, client_karts, client_tracks))
    {
        if (peer->isValidated())
        {
            std::string msg = "You deleted some assets that are required to stay on the server";
            sendStringToPeer(msg, peer);
            peer->kick();
        }
        else
        {
            NetworkString *message = getNetworkString(2);
            message->setSynchronous(true);
            message->addUInt8(LE_CONNECTION_REFUSED)
                    .addUInt8(RR_INCOMPATIBLE_DATA);

            std::string advice = ServerConfig::m_incompatible_advice;
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


    std::array<int, AS_TOTAL> addons_scores = m_asset_manager->getAddonScores(client_karts, client_tracks);

    // Save available karts and tracks from clients in STKPeer so if this peer
    // disconnects later in lobby it won't affect current players
    peer->setAvailableKartsTracks(client_karts, client_tracks);
    peer->setAddonsScores(addons_scores);

    if (m_process_type == PT_CHILD &&
        peer->getHostId() == m_client_server_host_id.load())
    {
        // Update child process addons list too so player can choose later
        m_asset_manager->updateAddons();
        updateMapsForMode();
    }

    if (m_tournament)
        m_tournament->updateTournamentRole(peer);
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
    if (!handleAssets(data, event->getPeerSP()))
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
        (unsigned)ServerConfig::m_server_max_players)
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
        ServerConfig::m_validating_player) ||
        (ServerConfig::m_strict_players &&
        (player_count != 1 || online_id == 0 || duplicated_ranked_player)) ||
        (peer->isAIPeer() && !peer->getAddress().isLAN() &&!ServerConfig::m_ai_anywhere) ||
        (peer->isAIPeer() &&
        ServerConfig::m_ai_handling && !m_ai_peer.expired()))
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

    if (ServerConfig::m_ai_handling && peer->isAIPeer())
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
    const std::string& server_pw = ServerConfig::m_private_server_password;
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
        if (m_usernames_white_list.count(username) > 0)
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
            (unsigned)ServerConfig::m_server_max_players)
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
        if (ServerConfig::m_ranked && duplicated_ranked_player)
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
        auto player = std::make_shared<NetworkPlayerProfile>
            (peer, i == 0 && !online_name.empty() && !peer->isAIPeer() ?
            online_name : name,
            peer->getHostId(), default_kart_color, i == 0 ? online_id : 0,
            handicap, (uint8_t)i, KART_TEAM_NONE,
            country_code);

        int previous_team = -1;
        std::string username = StringUtils::wideToUtf8(player->getName());
        auto it2 = m_team_for_player.find(username);
        if (it2 != m_team_for_player.end())
            previous_team = it2->second;

        bool can_change_teams = true;
        if (m_tournament && !m_tournament->canChangeTeam())
            can_change_teams = false;
        if (ServerConfig::m_team_choosing && can_change_teams)
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
                setTeamInLobby(player, cur_team);
        }
        if (m_tournament)
        {
            KartTeam team = m_tournament->getTeam(utf8_online_name);
            if (team != KART_TEAM_NONE)
                setTeamInLobby(player, team);
        }
        getCommandManager()->addUser(username);
        if (m_game_setup->isGrandPrix())
        {
            auto it = m_gp_scores.find(username);
            if (it != m_gp_scores.end())
            {
                player->setScore(it->second.score);
                player->setOverallTime(it->second.time);
            }
        }
        if (!RaceManager::get()->teamEnabled())
        {
            if (previous_team != -1)
            {
                setTemporaryTeamInLobby(player, previous_team);
            }
        }
        peer->addPlayer(player);

        // As it sets spectating mode for peer based on which players it has,
        // we need to set the team once again to the same thing
        setTemporaryTeamInLobby(player, player->getTemporaryTeam());
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
        .addUInt32(ServerConfig::m_state_frequency)
        .addUInt8(ServerConfig::m_chat ? 1 : 0)
        .addUInt8(playerReportsTableExists() ? 1 : 0);

    peer->setSpectator(false);

    // The 127.* or ::1/128 will be in charged for controlling AI
    if (m_ai_profiles.empty() && peer->getAddress().isLoopback())
    {
        unsigned ai_add = NetworkConfig::get()->getNumFixedAI();
        unsigned max_players = ServerConfig::m_server_max_players;
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
        peer->sendPacket(message_ack);
        delete message_ack;
    }
    else
    {
        peer->setWaitingForGame(false);
        m_peers_ready[peer] = false;
        if (!ServerConfig::m_sql_management)
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

        if (ServerConfig::m_ranked)
        {
            getRankingForPlayer(peer->getPlayerProfiles()[0]);
        }
    }

#ifdef ENABLE_SQLITE3
    m_db_connector->onPlayerJoinQueries(peer, online_id, player_count, country_code);
#endif
    if (m_kart_elimination->isEnabled())
    {
        bool hasEliminatedPlayer = false;
        for (unsigned i = 0; i < peer->getPlayerProfiles().size(); ++i)
        {
            std::string name = StringUtils::wideToUtf8(
                    peer->getPlayerProfiles()[i]->getName());
            if (m_kart_elimination->isEliminated(name))
            {
                hasEliminatedPlayer = true;
                break;
            }
        }
        NetworkString* chat = getNetworkString();
        chat->addUInt8(LE_CHAT);
        chat->setSynchronous(true);
        std::string warning = m_kart_elimination->getWarningMessage(hasEliminatedPlayer);
        chat->encodeString16(StringUtils::utf8ToWide(warning));
        peer->sendPacket(chat, PRM_RELIABLE);
        delete chat;
    }
    if (ServerConfig::m_record_replays)
    {
        std::string msg;
        if (m_consent_on_replays)
            msg = "Recording ghost replays is enabled. "
                "The crowned player can change that "
                "using /replay 0 (to disable) or /replay 1 (to enable). "
                "Do not race under this feature if you don't want to be recorded.";
        else
            msg = "Recording ghost replays is disabled. "
                "The crowned player can change that "
                "using /replay 0 (to disable) or /replay 1 (to enable). ";
        sendStringToPeer(msg, peer);
    }
    getMessagesFromHost(peer, online_id);

    if (m_tournament)
        m_tournament->updateTournamentRole(peer);
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

    NetworkString* pl = getNetworkString();
    pl->setSynchronous(true);
    pl->addUInt8(LE_UPDATE_PLAYER_LIST)
        .addUInt8((uint8_t)(game_started ? 1 : 0))
        .addUInt8((uint8_t)all_profiles.size());
    for (auto profile : all_profiles)
    {
        auto profile_name = profile->getName();

        // get OS information
        auto version_os = StringUtils::extractVersionOS(profile->getPeer()->getUserVersion());
        bool angry_host = profile->getPeer()->isAngryHost();
        std::string os_type_str = version_os.second;
        std::string utf8_profile_name = StringUtils::wideToUtf8(profile_name);
        // Add a Mobile emoji for mobile OS
        if (ServerConfig::m_expose_mobile && 
            (os_type_str == "iOS" || os_type_str == "Android"))
            profile_name = StringUtils::utf32ToWide({0x1F4F1}) + profile_name;

        // Add an hourglass emoji for players waiting because of the player limit
        if (spectators_by_limit.find(profile->getPeer()) != spectators_by_limit.end())
            profile_name = StringUtils::utf32ToWide({ 0x231B }) + profile_name;

        // Add a hammer emoji for angry host
        if (angry_host)
            profile_name = StringUtils::utf32ToWide({0x1F528}) + profile_name;

        std::string prefix = "";
        for (const std::string& category: m_categories_for_player[utf8_profile_name])
        {
            if (!m_hidden_categories.count(category))
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

        pl->addUInt32(profile->getHostId()).addUInt32(profile->getOnlineId())
            .addUInt8(profile->getLocalPlayerId())
            .encodeString(profile_name);

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
        if (ServerConfig::m_owner_less && !game_started &&
            m_peers_ready.find(p) != m_peers_ready.end() &&
            m_peers_ready.at(p))
            boolean_combine |= (1 << 3);
        if ((p && p->isAIPeer()) || isAIProfile(profile))
            boolean_combine |= (1 << 4);
        pl->addUInt8(boolean_combine);
        pl->addUInt8(profile->getHandicap());
        if (ServerConfig::m_team_choosing)
            pl->addUInt8(profile->getTeam());
        else
            pl->addUInt8(KART_TEAM_NONE);
        pl->encodeString(profile->getCountryCode());
    }

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
        ServerConfig::m_owner_less)
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
    if (m_state != SELECTING || !ServerConfig::m_track_voting)
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

    Track* t = track_manager->getTrack(vote.m_track_name);
    if (!t)
    {
        vote.m_track_name = m_asset_manager->getAnyMapForVote();
        t = track_manager->getTrack(vote.m_track_name);
        assert(t);
    }

    // Remove / adjust any invalid settings
    if (m_tournament)
    {
        m_tournament->applyRestrictionsOnVote(&vote);
    }
    else if (RaceManager::get()->modeHasLaps())
    {
        if (m_default_lap_multiplier > 0.0f)
        {
            vote.m_num_laps =
                (uint8_t)(fmaxf(1.0f, (float)t->getDefaultNumberOfLaps() *
                m_default_lap_multiplier));
        }
        else if (vote.m_num_laps == 0 || vote.m_num_laps > 20)
            vote.m_num_laps = (uint8_t)3;
        if (!t->reverseAvailable() && vote.m_reverse)
            vote.m_reverse = false;
    }
    else if (RaceManager::get()->isSoccerMode())
    {
        if (m_game_setup->isSoccerGoalTarget())
        {
            if (m_default_lap_multiplier > 0.0f)
            {
                vote.m_num_laps = (uint8_t)(m_default_lap_multiplier *
                                            UserConfigParams::m_num_goals);
            }
            else if (vote.m_num_laps > 10)
                vote.m_num_laps = (uint8_t)5;
        }
        else
        {
            if (m_default_lap_multiplier > 0.0f)
            {
                vote.m_num_laps = (uint8_t)(m_default_lap_multiplier *
                                            UserConfigParams::m_soccer_time_limit);
            }
            else if (vote.m_num_laps > 15)
                vote.m_num_laps = (uint8_t)7;
        }
    }
    else if (RaceManager::get()->getMinorMode() ==
        RaceManager::MINOR_MODE_FREE_FOR_ALL)
    {
        vote.m_num_laps = 0;
    }
    else if (RaceManager::get()->getMinorMode() ==
        RaceManager::MINOR_MODE_CAPTURE_THE_FLAG)
    {
        vote.m_num_laps = 0;
        vote.m_reverse = false;
    }
    if (m_fixed_lap >= 0)
    {
        vote.m_num_laps = m_fixed_lap;
    }
    if (m_fixed_direction >= 0)
        vote.m_reverse = (m_fixed_direction == 1);

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
bool ServerLobby::handleAllVotes(PeerVote* winner_vote,
                                 uint32_t* winner_peer_id)
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

    return m_map_vote_handler->handleAllVotes(
        m_peers_votes,
        getRemainingVotingTime(),
        getMaxVotingTime(),
        isVotingOver(),
        cur_players,
        m_default_vote,
        winner_vote, winner_peer_id
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
        if (ServerConfig::m_capture_limit > 0)
            hit_capture_limit = ServerConfig::m_capture_limit;
        if (ServerConfig::m_time_limit_ctf > 0)
            time_limit = (float)ServerConfig::m_time_limit_ctf;
    }
    else
    {
        if (ServerConfig::m_hit_limit > 0)
            hit_capture_limit = ServerConfig::m_hit_limit;
        if (ServerConfig::m_time_limit_ffa > 0.0f)
            time_limit = (float)ServerConfig::m_time_limit_ffa;
    }
    m_battle_hit_capture_limit = hit_capture_limit;
    m_battle_time_limit = time_limit;
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
    const unsigned max_ping_from_peers = ServerConfig::m_max_ping;
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
    if ((ServerConfig::m_high_ping_workaround && peer_exceeded_max_ping) ||
        (ServerConfig::m_live_players && RaceManager::get()->supportsLiveJoining()))
    {
        Log::info("ServerLobby", "Max ping to ServerConfig::m_max_ping for "
            "live joining or high ping workaround.");
        max_ping = ServerConfig::m_max_ping;
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

    const unsigned jitter_tolerance = ServerConfig::m_jitter_tolerance;
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
            if (!ServerConfig::m_sql_management)
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
        if (ServerConfig::m_ranked && !m_ranking->has(online_id))
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
        std::string msg = "A message from the server (" +
            std::string(message.timestamp) + "):\n" + std::string(message.message);
        sendStringToPeer(msg, peer);
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
float ServerLobby::getStartupBoostOrPenaltyForKart(uint32_t ping,
                                                   unsigned kart_id)
{
    AbstractKart* k = World::getWorld()->getKart(kart_id);
    if (k->getStartupBoost() != 0.0f)
        return k->getStartupBoost();
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
    if (!ServerConfig::m_server_configurable)
    {
        Log::warn("ServerLobby", "server-configurable is not enabled.");
        return;
    }
    bool teams_before = RaceManager::get()->teamEnabled();
    unsigned total_players = 0;
    STKHost::get()->updatePlayers(NULL, NULL, &total_players);
    bool bad_mode = (m_available_modes.count(mode) == 0);
    bool bad_difficulty = (m_available_difficulties.count(difficulty) == 0);
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
        sendStringToPeer(msg, peer);
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

    auto peers = STKHost::get()->getPeers();
    for (auto& peer : peers)
    {
        auto assets = peer->getClientAssets();
        if (!peer->isValidated() || assets.second.empty()) // this check will fail hard when I introduce vavriable limits
            continue;
        if (m_asset_manager->checkIfNoCommonMaps(assets))
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
            m_team_for_player.clear();
            m_command_manager->assignRandomTeams(2, &final_number, &players_number);
        }
        else
        {
            clearTemporaryTeams();
        }
        for (auto& peer : STKHost::get()->getPeers())
        {
            if (teams_before)
                peer->resetNoTeamSpectate();
            for (auto &profile: peer->getPlayerProfiles())
            {
                // updates KartTeam
                setTemporaryTeamInLobby(profile, profile->getTemporaryTeam());
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

    if (m_kart_elimination->isEnabled() &&
        RaceManager::get()->getMinorMode() != RaceManager::MINOR_MODE_NORMAL_RACE &&
        RaceManager::get()->getMinorMode() != RaceManager::MINOR_MODE_TIME_TRIAL)
    {
        m_kart_elimination->disable();
        NetworkString* chat = getNetworkString();
        chat->addUInt8(LE_CHAT);
        chat->setSynchronous(true);
        chat->encodeString16(
                L"Gnu Elimination is disabled because of non-racing mode");
        sendMessageToPeers(chat);
        delete chat;
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
    int new_difficulty = ServerConfig::m_server_difficulty;
    int new_game_mode = ServerConfig::m_server_mode;
    bool new_soccer_goal_target = ServerConfig::m_soccer_goal_target;
    if (event != NULL)
    {
        NetworkString& data = event->data();
        new_difficulty = data.getUInt8();
        new_game_mode = data.getUInt8();
        new_soccer_goal_target = data.getUInt8() == 1;
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

        if (!m_game_info)
            Log::warn("ServerLobby", "GameInfo is not accessible??");
        else
        {
            saveDisconnectingIdInfo(i);
        }

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
            if (ServerConfig::m_ranked)
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
    if (!ServerConfig::m_live_players &&
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
    if (!ServerConfig::m_live_players || !RaceManager::get()->supportsLiveJoining())
        return;
    if (RaceManager::get()->getMinorMode() == RaceManager::MINOR_MODE_FREE_FOR_ALL)
    {
        Track* t = track_manager->getTrack(m_game_setup->getCurrentTrack());
        assert(t);
        int max_players = std::min((int)ServerConfig::m_server_max_players,
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
        if (m_kart_elimination->isEliminated(username))
        {
            peer->getPlayerProfiles()[i]->setKartName(m_kart_elimination->getKart());
            continue;
        }
        std::string current_kart = kart;
        if (kart.find("randomkart") != std::string::npos ||
                (kart.find("addon_") == std::string::npos &&
                !m_asset_manager->isKartAvailable(kart)))
        {
            current_kart = "";
        }
        if (m_lobby_queues->areKartFiltersIgnoringKarts())
            current_kart = "";
        std::string name = StringUtils::wideToUtf8(peer->getPlayerProfiles()[i]->getName());
        peer->getPlayerProfiles()[i]->setKartName(getKartForBadKartChoice(peer, name, current_kart));
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

    unsigned player_limit = ServerConfig::m_server_max_players;
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
void ServerLobby::handleServerCommand(Event* event,
                                      std::shared_ptr<STKPeer> peer)
{
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
    std::string msg = m_kart_elimination->update(order);
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
        m_game_info->m_flag_return_timeout = ServerConfig::m_flag_return_timeout;
        m_game_info->m_flag_deactivated_time = ServerConfig::m_flag_deactivated_time;
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
void ServerLobby::initAvailableModes()
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
void ServerLobby::resetToDefaultSettings()
{
    if (ServerConfig::m_server_configurable && !m_preserve.count("mode"))
        handleServerConfiguration(NULL);

    if (!m_preserve.count("elim"))
        m_kart_elimination->disable();

    if (!m_preserve.count("laps"))
    {
        m_default_lap_multiplier = -1.0;
        m_fixed_lap = -1;
    }

    if (!m_preserve.count("direction"))
        m_fixed_direction = ServerConfig::m_fixed_direction;

    m_lobby_queues->resetToDefaultSettings(m_preserve);

    if (!m_preserve.count("replay"))
        setConsentOnReplays(false);
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
void ServerLobby::initCategories()
{
    std::vector<std::string> tokens = StringUtils::split(
        ServerConfig::m_categories, ' ');
    std::string category = "";
    bool isTeam = false;
    for (std::string& s: tokens)
    {
        if (s.empty())
            continue;
        else if (s[0] == '#')
        {
            isTeam = false;
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
            category = s.substr(1);
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
}   // initCategories
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
                setTeamInLobby(pp, KART_TEAM_BLUE);
            }
            else if (pp->getTeam() == KART_TEAM_BLUE)
            {
                setTeamInLobby(pp, KART_TEAM_RED);
            }
        }
    }
    updatePlayerList();
}   // changeColors
//-----------------------------------------------------------------------------
void ServerLobby::sendStringToPeer(std::string& s, std::shared_ptr<STKPeer> peer)
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
void ServerLobby::sendStringToAllPeers(std::string& s)
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
    std::string username = StringUtils::wideToUtf8(
        peer->getPlayerProfiles()[0]->getName());
    if (m_tournament && !m_tournament->canPlay(username))
    {
        m_why_peer_cannot_play[peer] = HR_NOT_A_TOURNAMENT_PLAYER;
        return false;
    }
    else if (m_spectators_by_limit.find(peer) != m_spectators_by_limit.end())
    {
        m_why_peer_cannot_play[peer] = HR_SPECTATOR_BY_LIMIT;
        return false;
    }

    if (!m_play_requirement_tracks.empty())
        for (const std::string& track: m_play_requirement_tracks)
            if (peer->getClientAssets().second.count(track) == 0)
            {
                m_why_peer_cannot_play[peer] = HR_LACKING_REQUIRED_MAPS;
                return false;
            }

    if (peer->addon_karts_count < ServerConfig::m_addon_karts_play_threshold)
    {
        m_why_peer_cannot_play[peer] = HR_ADDON_KARTS_PLAY_THRESHOLD;
        return false;
    }
    if (peer->addon_tracks_count < ServerConfig::m_addon_tracks_play_threshold)
    {
        m_why_peer_cannot_play[peer] = HR_ADDON_TRACKS_PLAY_THRESHOLD;
        return false;
    }
    if (peer->addon_arenas_count < ServerConfig::m_addon_arenas_play_threshold)
    {
        m_why_peer_cannot_play[peer] = HR_ADDON_ARENAS_PLAY_THRESHOLD;
        return false;
    }
    if (peer->addon_soccers_count < ServerConfig::m_addon_soccers_play_threshold)
    {
        m_why_peer_cannot_play[peer] = HR_ADDON_FIELDS_PLAY_THRESHOLD;
        return false;
    }

    std::set<std::string> maps = peer->getClientAssets().second;
    std::set<std::string> karts = peer->getClientAssets().first;

    float karts_fraction = m_asset_manager->officialKartsFraction(karts);
    float maps_fraction = m_asset_manager->officialMapsFraction(maps);

    if (karts_fraction < ServerConfig::m_official_karts_play_threshold)
    {
        m_why_peer_cannot_play[peer] = HR_OFFICIAL_KARTS_PLAY_THRESHOLD;
        return false;
    }
    if (maps_fraction < ServerConfig::m_official_tracks_play_threshold)
    {
        m_why_peer_cannot_play[peer] = HR_OFFICIAL_TRACKS_PLAY_THRESHOLD;
        return false;
    }
    
    applyAllFilters(maps, true);
    applyAllKartFilters(username, karts, false);

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

    if (!m_tournament)
        return true;

    return m_tournament->canVote(peer);
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
    if (m_tournament)
        return m_tournament->hasHostRights(peer);

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
        if (ServerConfig::m_only_host_riding)
            mask |= CommandPermissions::PE_SINGLE;
    }
    if (peer->isAngryHost())
    {
        mask |= CommandPermissions::PE_HAMMER;
    }
    else if (m_tournament && m_tournament->hasHammerRights(peer))
    {
        mask |= CommandPermissions::PE_HAMMER;
    }
    return mask;
}   // getPermissions
//-----------------------------------------------------------------------------
std::string ServerLobby::getGrandPrixStandings(bool showIndividual, bool showTeam) const
{
    std::stringstream response;
    response << "Grand Prix standings";

    if (!showIndividual && !showTeam)
    {
        if (m_gp_team_scores.empty())
            showIndividual = true;
        else
            showTeam = true;
    }

    uint8_t passed = (uint8_t)m_game_setup->getAllTracks().size();
    uint8_t total = m_game_setup->getExtraServerInfo();
    if (passed != 0)
        response << " after " << (int)passed << " of " << (int)total << " games:\n";
    else
        response << ", " << (int)total << " games:\n";

    if (showIndividual)
    {
        std::vector<std::pair<GPScore, std::string>> results;
        for (auto &p: m_gp_scores)
            results.emplace_back(p.second, p.first);
        std::stable_sort(results.rbegin(), results.rend());
        for (unsigned i = 0; i < results.size(); i++)
        {
            response << (i + 1) << ". ";
            response << "  " << results[i].second;
            response << "  " << results[i].first.score;
            response << "  " << "(" << StringUtils::timeToString(results[i].first.time) << ")";
            response << "\n";
        }
    }

    if (showTeam)
    {
        if (!m_gp_team_scores.empty())
        {
            std::vector<std::pair<GPScore, int>> results2;
            if (showIndividual)
                response << "\n";
            for (auto &p: m_gp_team_scores)
                results2.emplace_back(p.second, p.first);
            std::stable_sort(results2.rbegin(), results2.rend());
            for (unsigned i = 0; i < results2.size(); i++)
            {
                response << (i + 1) << ". ";
                response << "  " << TeamUtils::getTeamByIndex(results2[i].second).getNameWithEmoji();
                response << "  " << results2[i].first.score;
                response << "  " << "(" << StringUtils::timeToString(results2[i].first.time) << ")";
                response << "\n";
            }
        }
    }
    return response.str();
}   // getGrandPrixStandings
//-----------------------------------------------------------------------------
bool ServerLobby::loadCustomScoring(std::string& scoring)
{
    std::set<std::string> available_scoring_types = {
            "standard", "default", "", "inc", "fixed", "linear-gap", "exp-gap"
    };
    auto previous_params = m_scoring_int_params;
    auto previous_type = m_scoring_type;
    m_scoring_int_params.clear();
    m_scoring_type = "";
    if (!scoring.empty())
    {
        std::vector<std::string> params = StringUtils::split(scoring, ' ');
        if (params.empty())
        {
            m_scoring_type = "";
            return true;
        }
        m_scoring_type = params[0];
        if (available_scoring_types.count(m_scoring_type) == 0)
        {
            Log::warn("ServerLobby", "Unknown scoring type %s, fallback.", m_scoring_type.c_str());
            m_scoring_int_params = previous_params;
            m_scoring_type = previous_type;
            return false;
        }
        for (unsigned i = 1; i < params.size(); i++)
        {
            int param;
            if (!StringUtils::fromString(params[i], param))
            {
                Log::warn("ServerLobby", "Unable to parse integer from custom scoring data, fallback.");
                m_scoring_int_params = previous_params;
                m_scoring_type = previous_type;
                return false;
            }
            m_scoring_int_params.push_back(param);
        }
    }
    return true;
}   // loadCustomScoring
//-----------------------------------------------------------------------------   
void ServerLobby::updateWorldSettings()
{
    World::getWorld()->setGameInfo(m_game_info);
    WorldWithRank *wwr = dynamic_cast<WorldWithRank*>(World::getWorld());
    if (wwr)
    {
        wwr->setCustomScoringSystem(m_scoring_type, m_scoring_int_params);
    }
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
            Log::warn("ServerLobby", "Soccer goals policy %s does not exist", policy.c_str());
    }
}   // updateWorldSettings
//-----------------------------------------------------------------------------   
void ServerLobby::loadWhiteList()
{
    std::vector<std::string> tokens = StringUtils::split(
        ServerConfig::m_white_list, ' ');
    for (std::string& s: tokens)
        m_usernames_white_list.insert(s);
}   // loadWhiteList
//-----------------------------------------------------------------------------
void ServerLobby::loadPreservedSettings()
{
    std::vector<std::string> what_to_preserve =
            StringUtils::split(std::string(ServerConfig::m_preserve_on_reset), ' ');
    for (std::string& str: what_to_preserve)
        m_preserve.insert(str);
}   // loadWhiteList
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
    NetworkString* server_info = getNetworkString();
    server_info->setSynchronous(true);
    server_info->addUInt8(LE_SERVER_INFO);
    m_game_setup->addServerInfo(server_info);
    sendMessageToPeers(server_info);
    delete server_info;
    updatePlayerList();
}   // changeLimitForTournament
//-----------------------------------------------------------------------------
void ServerLobby::initAvailableTracks()
{
    m_global_filter = TrackFilter(ServerConfig::m_only_played_tracks_string);
    m_global_karts_filter = KartFilter(ServerConfig::m_only_played_karts_string);
    m_asset_manager->setMustHaveMaps(ServerConfig::m_must_have_tracks_string);
    m_play_requirement_tracks = StringUtils::split(
            ServerConfig::m_play_requirement_tracks_string, ' ', false);
}   // initAvailableTracks
//-----------------------------------------------------------------------------
std::vector<std::string> ServerLobby::getMissingAssets(std::shared_ptr<STKPeer> peer) const
{
    std::vector<std::string> ans;
    for (const std::string& required_track : m_play_requirement_tracks)
        if (peer->getClientAssets().second.count(required_track) == 0)
            ans.push_back(required_track);
    return ans;
}   // getMissingAssets
//-----------------------------------------------------------------------------

std::shared_ptr<CommandManager> ServerLobby::getCommandManager()
{
    if (!m_command_manager->isInitialized())
    {
        m_command_manager = std::make_shared<CommandManager>(this);
    }
    return m_command_manager;
}   // getCommandManager
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

#ifdef ENABLE_WEB_SUPPORT

void ServerLobby::loadAllTokens()
{
#ifdef ENABLE_SQLITE3
    std::vector<std::string> tokens;
    if (!m_db_connector->getAllTokens(tokens))
    {
        Log::warn("ServerLobby", "Could not make a query to retrieve tokens.");
    }
    else
    {
        Log::info("ServerLobby", "Successfully loaded %d tokens.", (int)tokens.size());
        for (std::string& s: tokens)
            m_web_tokens.insert(s);
    }
#endif
}   // loadAllTokens
//-----------------------------------------------------------------------------

std::string ServerLobby::getToken()
{
    int tries = m_token_generation_tries.load();
    m_token_generation_tries.store(tries + 1);
    std::mt19937 mt(time(nullptr) + tries);
    std::string token;
    for (int i = 0; i < 16; ++i)
    {
        int z = mt() % 36;
        if (z < 26)
            token.push_back('a' + z);
        else
            token.push_back('0' + z - 26);
        if ((i & 3) == 3)
            token.push_back(' ');
    }
    token.pop_back();
    return token;
}   // getToken
//-----------------------------------------------------------------------------
#endif // ENABLE_WEB_SUPPORT

bool ServerLobby::isSoccerGoalTarget() const
{
    return m_game_setup->isSoccerGoalTarget();
}   // isSoccerGoalTarget
//-----------------------------------------------------------------------------

// This should be moved later to another unit.
void ServerLobby::setTemporaryTeamInLobby(const std::string& username, int team)
{
    irr::core::stringw wide_player_name = StringUtils::utf8ToWide(username);
    std::shared_ptr<STKPeer> player_peer = STKHost::get()->findPeerByName(
            wide_player_name);
    if (player_peer)
    {
        for (auto& profile : player_peer.get()->getPlayerProfiles())
        {
            if (profile->getName() == wide_player_name)
            {
                setTemporaryTeamInLobby(profile, team);
                break;
            }
        }
    }
}   // setTemporaryTeamInLobby (username)
//-----------------------------------------------------------------------------

// todo This should be moved later to another unit.
void ServerLobby::clearTemporaryTeams()
{
    m_team_for_player.clear();

    for (auto& peer : STKHost::get()->getPeers())
    {
        for (auto& profile : peer->getPlayerProfiles())
        {
            setTemporaryTeamInLobby(profile, TeamUtils::NO_TEAM);
        }
    }
}   // clearTemporaryTeams
//-----------------------------------------------------------------------------

// todo This should be moved later to another unit.
void ServerLobby::shuffleTemporaryTeams(const std::map<int, int>& permutation)
{
    for (auto& p: m_team_for_player)
    {
        auto it = permutation.find(p.second);
        if (it != permutation.end())
            p.second = it->second;
    }
    for (auto& peer : STKHost::get()->getPeers())
    {
        for (auto &profile: peer->getPlayerProfiles())
        {
            auto it = permutation.find(profile->getTemporaryTeam());
            if (it != permutation.end())
            {
                setTemporaryTeamInLobby(profile, it->second);
            }
        }
    }
    auto old_scores = m_gp_team_scores;
    m_gp_team_scores.clear();
    for (auto& p: old_scores)
    {
        auto it = permutation.find(p.first);
        if (it != permutation.end())
            m_gp_team_scores[it->second] = p.second;
        else
            m_gp_team_scores[p.first] = p.second;
    }
}   // shuffleTemporaryTeams
//-----------------------------------------------------------------------------

void ServerLobby::resetGrandPrix()
{
    m_gp_scores.clear();
    m_gp_team_scores.clear();
    m_game_setup->stopGrandPrix();

    NetworkString* server_info = getNetworkString();
    server_info->setSynchronous(true);
    server_info->addUInt8(LE_SERVER_INFO);
    m_game_setup->addServerInfo(server_info);
    sendMessageToPeers(server_info);
    delete server_info;
    updatePlayerList();
}   // resetGrandPrix
//-----------------------------------------------------------------------------

void ServerLobby::applyAllFilters(std::set<std::string>& maps, bool use_history) const
{
    unsigned max_player = 0;
    STKHost::get()->updatePlayers(&max_player);
    if (RaceManager::get()->getMinorMode() == RaceManager::MINOR_MODE_FREE_FOR_ALL)
    {
        auto it = maps.begin();
        while (it != maps.end())
        {
            Track* t = track_manager->getTrack(*it);
            if (t && t->getMaxArenaPlayers() < max_player)
            {
                it = maps.erase(it);
            }
            else
                it++;
        }
    }

    // Please note that use_history refers to using queue filters too -
    // calls with false only get map sets, etc
    FilterContext map_context;
    map_context.username = ""; // unused
    map_context.num_players = max_player;
    map_context.wildcards = m_map_history;
    map_context.applied_at_selection_start = true;
    map_context.elements = maps;
    m_global_filter.apply(map_context);
    if (use_history)
    {
        if (m_tournament)
            m_tournament->applyFiltersForThisGame(map_context);
        map_context.wildcards = m_map_history;
        m_lobby_queues->applyFrontMapFilters(map_context);
    }
    swap(maps, map_context.elements);
}   // applyAllFilters
//-----------------------------------------------------------------------------

void ServerLobby::applyAllKartFilters(const std::string& username, std::set<std::string>& karts, bool afterSelection) const
{

    FilterContext kart_context;
    kart_context.username = username;
    kart_context.num_players = 0; // unused
    kart_context.wildcards = {}; // unused
    kart_context.applied_at_selection_start = !afterSelection;
    kart_context.elements = karts;

    m_global_karts_filter.apply(kart_context);
    m_lobby_queues->applyFrontKartFilters(kart_context);
    swap(karts, kart_context.elements);
}   // applyAllKartFilters
//-----------------------------------------------------------------------------
std::string ServerLobby::getKartForBadKartChoice(std::shared_ptr<STKPeer> peer, const std::string& username, const std::string& check_choice) const
{
    std::set<std::string> karts = (peer->isAIPeer() ? m_asset_manager->getAvailableKarts() : peer->getClientAssets().first);
    applyAllKartFilters(username, karts, true);
    if (m_kart_elimination->isEliminated(username)
        && karts.count(m_kart_elimination->getKart()))
    {
        return m_kart_elimination->getKart();
    }
    if (!check_choice.empty() && karts.count(check_choice)) // valid choice
        return check_choice;
    // choice is invalid, roll the random
    RandomGenerator rg;
    std::set<std::string>::iterator it = karts.begin();
    std::advance(it, rg.get((int)karts.size()));
    return *it;
}   // getKartForRandomKartChoice
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
        if (ServerConfig::m_real_addon_karts && real_addon)
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
        if (ServerConfig::m_preserve_battle_scores)
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
std::string ServerLobby::getAvailableTeams() const
{
    if (RaceManager::get()->teamEnabled())
        return "rb";

    return m_available_teams;
}   // getAvailableTeams

//-----------------------------------------------------------------------------
void ServerLobby::setTeamInLobby(std::shared_ptr<NetworkPlayerProfile> profile, KartTeam team)
{
    // Used for soccer+CTF, where everything can be defined by KartTeam
    profile->setTeam(team);
    profile->setTemporaryTeam(TeamUtils::getIndexFromKartTeam(team));
    m_team_for_player[StringUtils::wideToUtf8(profile->getName())] = profile->getTemporaryTeam();

    checkNoTeamSpectator(profile->getPeer());
}   // setTeamInLobby

//-----------------------------------------------------------------------------
void ServerLobby::setTemporaryTeamInLobby(std::shared_ptr<NetworkPlayerProfile> profile, int team)
{
    // Used for racing+FFA, where everything can be defined by a temporary team
    profile->setTemporaryTeam(team);
    if (RaceManager::get()->teamEnabled())
        profile->setTeam((KartTeam)(TeamUtils::getKartTeamFromIndex(team)));
    else
        profile->setTeam(KART_TEAM_NONE);
    m_team_for_player[StringUtils::wideToUtf8(profile->getName())] = profile->getTemporaryTeam();

    checkNoTeamSpectator(profile->getPeer());
}   // setTemporaryTeamInLobby

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
        {
            setSpectateModeProperly(peer, ASM_NO_TEAM);
        }
        if (has_teamed && peer->getAlwaysSpectate() == ASM_NO_TEAM)
        {
            setSpectateModeProperly(peer, ASM_NONE);
        }
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
