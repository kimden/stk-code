//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2018 SuperTuxKart-Team
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

#ifndef SERVER_LOBBY_HPP
#define SERVER_LOBBY_HPP

#include "karts/controller/player_controller.hpp"
#include "network/protocols/lobby_protocol.hpp"
#include "network/requests.hpp" // only needed in header as long as KeyData is there
#include "network/server_enums.hpp"
#include "utils/cpp2011.hpp"
#include "utils/hourglass_reason.hpp"
#include "utils/lobby_context.hpp"
#include "utils/team_utils.hpp"
#include "utils/time.hpp"
#include "utils/track_filter.hpp"

#include "irrString.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>

class BareNetworkString;
class CommandManager;
class DatabaseConnector;
class GenericDecorator;
class HitProcessor;
class KartElimination;
class LobbyAssetManager;
class LobbyQueues;
class LobbySettings;
class MapVoteHandler;
class NetworkItemManager;
class NetworkPlayerProfile;
class NetworkString;
class Ranking;
class SocketAddress;
class STKPeer;
class Tournament;
enum AlwaysSpectateMode: uint8_t;
struct GameInfo;

// class PlayingRoom;
#include "network/protocols/playing_room.hpp"

namespace Online
{
    class Request;
}

class ServerLobby : public LobbyProtocol, public LobbyContextUser
{
private:

    std::atomic<ServerInitState> m_init_state;

#ifdef ENABLE_SQLITE3
    void pollDatabase();
#endif

    std::vector<std::shared_ptr<PlayingRoom>> m_rooms;

    bool m_registered_for_once_only;

    std::weak_ptr<Online::Request> m_server_registering;

    std::mutex m_keys_mutex;

    std::map<uint32_t, KeyData> m_keys;

    std::map<std::weak_ptr<STKPeer>,
        std::pair<uint32_t, BareNetworkString>,
        std::owner_less<std::weak_ptr<STKPeer> > > m_pending_connection;

    std::map<std::string, uint64_t> m_pending_peer_connection;

    std::atomic<uint32_t> m_server_id_online;

    std::atomic<uint32_t> m_client_server_host_id;

    std::atomic<uint64_t> m_last_success_poll_time;

    uint64_t m_last_unsuccess_poll_time;
    
    // Other units previously used in ServerLobby
    std::shared_ptr<LobbyContext> m_lobby_context;

    std::shared_ptr<Ranking> m_ranking;

    std::shared_ptr<GenericDecorator> m_name_decorator;

private:

#define Reworking "This method was moved to PlayingRoom but is still used for ServerLobby. This has to be changed,"

public:
    ServerInitState getCurrentInitState() const { return m_init_state.load(); }

    [[deprecated(Reworking)]]
    virtual bool isRacing() const OVERRIDE { return m_rooms[0]->isRacing(); }

    [[deprecated(Reworking)]]
    int getDifficulty() const                   { return m_rooms[0]->getDifficulty(); }

    [[deprecated(Reworking)]]
    int getGameMode() const                      { return m_rooms[0]->getGameMode(); }

    [[deprecated(Reworking)]]
    int getLobbyPlayers() const              { return m_rooms[0]->getLobbyPlayers(); }

    [[deprecated(Reworking)]]
    bool isAIProfile(const std::shared_ptr<NetworkPlayerProfile>& npp) const
    {
        return m_rooms[0]->isAIProfile(npp);
    }

    [[deprecated(Reworking)]]
    bool isWorldPicked() const         { return m_rooms[0]->isWorldPicked(); }

    [[deprecated(Reworking)]]
    bool isWorldFinished() const   { return m_rooms[0]->isWorldFinished(); }

    [[deprecated(Reworking)]]
    bool isStateAtLeastRacing() const      { return m_rooms[0]->isStateAtLeastRacing(); }

    [[deprecated(Reworking)]]
    std::shared_ptr<GameInfo> getGameInfo() const       { return m_rooms[0]->getGameInfo(); }

    [[deprecated(Reworking)]]
    std::shared_ptr<STKPeer> getServerOwner() const
                                              { return m_rooms[0]->getServerOwner(); }

    void doErrorLeave()                         { m_init_state.store(ERROR_LEAVE); }

    [[deprecated(Reworking)]]
    bool isWaitingForStartGame() const { return m_rooms[0]->isWaitingForStartGame(); }

private:

    // connection management
    void clientDisconnected(Event* event);
    void connectionRequested(Event* event);
    // kart selection
    // void kartSelectionRequested(Event* event);
    // Track(s) votes
    // void handlePlayerVote(Event *event);
    // void playerFinishedResult(Event *event);
    void registerServer(bool first_time);
    // void finishedLoadingWorldClient(Event *event);
    // void finishedLoadingLiveJoinClient(Event *event);
    void kickHost(Event* event);
    void changeTeam(Event* event);
    void handleChat(Event* event);
    void unregisterServer(bool now,
        std::weak_ptr<ServerLobby> sl = std::weak_ptr<ServerLobby>());

public: // I'll see if it should be private later
    void updatePlayerList(bool update_when_reset_server = false);
    void updateServerOwner(bool force = false);

private:
    void handleServerConfiguration(Event* event);

public: // I'll see if it should be private later
    void handleServerConfiguration(std::shared_ptr<STKPeer> peer,
        int difficulty, int mode, bool soccer_goal_target);

private:
    // bool checkPeersReady(bool ignore_ai_peer, SelectionPhase phase);
    void handlePendingConnection();
    void handleUnencryptedConnection(std::shared_ptr<STKPeer> peer,
                                     BareNetworkString& data,
                                     uint32_t online_id,
                                     const irr::core::stringw& online_name,
                                     bool is_pending_connection,
                                     std::string country_code = "");
    bool decryptConnectionRequest(std::shared_ptr<STKPeer> peer,
                                  BareNetworkString& data,
                                  const std::string& key,
                                  const std::string& iv,
                                  uint32_t online_id,
                                  const irr::core::stringw& online_name,
                                  const std::string& country_code);
    bool handleAllVotes(PeerVote* winner);
    void getRankingForPlayer(std::shared_ptr<NetworkPlayerProfile> p);
    void submitRankingsToAddons();
    void computeNewRankings(NetworkString* ns);
    void checkRaceFinished();
    void configPeersStartTime();
    // void resetServer();
    // void addWaitingPlayersToGame();
    void changeHandicap(Event* event);
    // void handlePlayerDisconnection() const;
    // void addLiveJoinPlaceholder(
    //     std::vector<std::shared_ptr<NetworkPlayerProfile> >& players) const;
    // void setPlayerKarts(const NetworkString& ns, std::shared_ptr<STKPeer> peer) const;
    bool handleAssets(Event* event);

    bool handleAssetsAndAddonScores(std::shared_ptr<STKPeer> peer,
            const std::set<std::string>& client_karts,
            const std::set<std::string>& client_maps);
    
    void handleServerCommand(Event* event);
    // void rejectLiveJoin(std::shared_ptr<STKPeer> peer, BackLobbyReason blr);
    // bool canLiveJoinNow() const;
    // int getReservedId(std::shared_ptr<NetworkPlayerProfile>& p,
    //                   unsigned local_id);
    // void handleKartInfo(Event* event);
    // void clientInGameWantsToBackLobby(Event* event);
    // void clientSelectingAssetsWantsToBackLobby(Event* event);
    void kickPlayerWithReason(std::shared_ptr<STKPeer> peer, const char* reason) const;
    void testBannedForIP(std::shared_ptr<STKPeer> peer) const;
    void testBannedForIPv6(std::shared_ptr<STKPeer> peer) const;
    void testBannedForOnlineId(std::shared_ptr<STKPeer> peer, uint32_t online_id) const;
    void getMessagesFromHost(std::shared_ptr<STKPeer> peer, int online_id);
    void writePlayerReport(Event* event);
    // bool supportsAI();
    void initTournamentPlayers();
public:
    void changeLimitForTournament(bool goal_target);
private:
    // bool canVote(std::shared_ptr<STKPeer> peer) const;
    // bool hasHostRights(std::shared_ptr<STKPeer> peer) const;

public:
             ServerLobby();
    virtual ~ServerLobby();

    virtual bool notifyEventAsynchronous(Event* event) OVERRIDE;
    virtual bool notifyEvent(Event* event) OVERRIDE;
    virtual void setup() OVERRIDE;
    virtual void update(int ticks) OVERRIDE;
    virtual void asynchronousUpdate() OVERRIDE;

    // void startSelection(const Event *event=NULL);
    void checkIncomingConnectionRequests();
    void finishedLoadingWorld() OVERRIDE;
    void updateBanList();
    bool waitingForPlayers() const;
    float getStartupBoostOrPenaltyForKart(uint32_t ping, unsigned kart_id);
    // void saveInitialItems(std::shared_ptr<NetworkItemManager> nim);
    void saveIPBanTable(const SocketAddress& addr);
    void listBanTable();
    void initServerStatsTable();
    uint32_t getServerIdOnline() const           { return m_server_id_online; }
    void setClientServerHostId(uint32_t id)   { m_client_server_host_id = id; }
    // void resetToDefaultSettings();
    void writeOwnReport(std::shared_ptr<STKPeer> reporter, std::shared_ptr<STKPeer> reporting,
        const std::string& info);
    bool writeOnePlayerReport(std::shared_ptr<STKPeer> reporter, const std::string& table,
        const std::string& info);
    // int getTrackMaxPlayers(std::string& name) const;

    // TODO: When using different decorators for everyone, you would need
    // a structure to store "player profile" placeholders in a string, so that
    // you can apply decorators at the very last moment inside sendStringToAllPeers
    // and similar functions.
    std::string encodeProfileNameForPeer(
        std::shared_ptr<NetworkPlayerProfile> npp,
        STKPeer* peer);

    std::shared_ptr<GenericDecorator> getNameDecorator() { return m_name_decorator; }

    void unregisterServerForLegacyGPMode();

    void dropPendingConnectionsForLegacyGPMode();

    void addWaitingPlayersToRanking(
            const std::shared_ptr<NetworkPlayerProfile>& npp);

    // int getPermissions(std::shared_ptr<STKPeer> peer) const;
    bool isSoccerGoalTarget() const;

#ifdef ENABLE_SQLITE3
    std::string getRecord(std::string& track, std::string& mode,
        std::string& direction, int laps);
#endif

    bool areKartFiltersIgnoringKarts() const;

    // void setKartDataProperly(KartData& kart_data, const std::string& kart_name,
    //                          std::shared_ptr<NetworkPlayerProfile> player,
    //                          const std::string& type) const;

    bool playerReportsTableExists() const;

    void sendServerInfoToEveryone() const;

    bool isLegacyGPMode() const;
    // int getCurrentStateScope();
    bool isChildProcess()                { return m_process_type == PT_CHILD; }

    bool isClientServerHost(const std::shared_ptr<STKPeer>& peer);
    bool canIgnoreControllerEvents() const;
    bool isPeerInGame(const std::shared_ptr<STKPeer>& peer) const;
    bool hasAnyGameStarted() const;
    bool isPastRegistrationPhase() const;

    std::shared_ptr<PlayingRoom> getRoomForPeer(const std::shared_ptr<STKPeer>& peer) const;
    std::shared_ptr<PlayingRoom> getRoom(int idx) const;

    int getPermissions(std::shared_ptr<STKPeer> peer) const;
    void resetServerToRSA();

    //-------------------------------------------------------------------------
    // More auxiliary functions
    //-------------------------------------------------------------------------

    // Functions that arose from requests separation
    void setServerOnlineId(uint32_t id)       { m_server_id_online.store(id); }
    void resetSuccessPollTime()
                  { m_last_success_poll_time.store(StkTime::getMonoTimeMs()); }

    bool hasPendingPeerConnection(const std::string& peer_addr_str) const
                        { return m_pending_peer_connection.find(peer_addr_str)
                              != m_pending_peer_connection.end(); }

    void addPeerConnection(const std::string& addr_str)
    {
        m_pending_peer_connection[addr_str] = StkTime::getMonoTimeMs();
    }
    void removeExpiredPeerConnection()
    {
        // Remove connect to peer protocol running more than a 45 seconds
        // (from stk addons poll server request),
        for (auto it = m_pending_peer_connection.begin();
             it != m_pending_peer_connection.end();)
        {
            if (StkTime::getMonoTimeMs() - it->second > 45000)
                it = m_pending_peer_connection.erase(it);
            else
                it++;
        }
    }
    void replaceKeys(std::map<uint32_t, KeyData>& new_keys)
    {
        std::lock_guard<std::mutex> lock(m_keys_mutex);
        std::swap(m_keys, new_keys);
    }
    // end of functions that arose from requests separation
};   // class ServerLobby

#endif // SERVER_LOBBY_HPP
