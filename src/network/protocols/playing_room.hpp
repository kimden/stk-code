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

#ifndef PLAYING_ROOM_HPP
#define PLAYING_ROOM_HPP

#include "network/server_enums.hpp"
#include "utils/lobby_context.hpp"
#include "network/packet_types.hpp"

#include <algorithm>
#include <atomic>
#include <map>
#include <vector>
#include <memory>

class STKPeer;
class NetworkPlayerProfile;
class NetworkString;
class BareNetworkString;
class GameInfo;
class NetworkItemManager;


class PlayingRoom: public LobbyContextUser
{
public:

private:
    std::atomic<ServerState> m_state;

    std::atomic<ResetState> m_rs_state;

    /** Hold the next connected peer for server owner if current one expired
     * (disconnected). */
    std::weak_ptr<STKPeer> m_server_owner;

    /** AI peer which holds the list of reserved AI for dedicated server. */
    std::weak_ptr<STKPeer> m_ai_peer;

    /** AI profiles for all-in-one graphical client server, this will be a
     *  fixed count thorough the live time of server, which its value is
     *  configured in NetworkConfig. */
    std::vector<std::shared_ptr<NetworkPlayerProfile> > m_ai_profiles;

    std::atomic<uint32_t> m_server_owner_id;

    /** Keeps track of the server state. */
    std::atomic_bool m_server_has_loaded_world;

    /** Counts how many peers have finished loading the world. */
    std::map<std::weak_ptr<STKPeer>, bool,
        std::owner_less<std::weak_ptr<STKPeer> > > m_peers_ready;

    /** Timeout counter for various state. */
    std::atomic<int64_t> m_timeout;

    /* Saved the last game result */
    NetworkString* m_result_ns;

    /* Used to make sure clients are having same item list at start */
    BareNetworkString* m_items_complete_state;

    std::atomic<int> m_difficulty;

    std::atomic<int> m_game_mode;

    std::atomic<int> m_lobby_players;

    std::atomic<int> m_current_ai_count;

    uint64_t m_server_started_at;
    
    uint64_t m_server_delay;

    unsigned m_item_seed;

    uint64_t m_client_starting_time;

    // Calculated before each game started
    unsigned m_ai_count;

    std::shared_ptr<GameInfo> m_game_info;

    std::atomic<bool> m_reset_to_default_mode_later;

private:
    void resetPeersReady()
    {
        for (auto it = m_peers_ready.begin(); it != m_peers_ready.end();)
        {
            if (it->first.expired())
            {
                it = m_peers_ready.erase(it);
            }
            else
            {
                it->second = false;
                it++;
            }
        }
    }

private:
    void updateMapsForMode();
    NetworkString* getLoadWorldMessage(
        std::vector<std::shared_ptr<NetworkPlayerProfile> >& players,
        bool live_join) const;

    void rejectLiveJoin(std::shared_ptr<STKPeer> peer, BackLobbyReason blr);
    bool canLiveJoinNow() const;
    bool canVote(std::shared_ptr<STKPeer> peer) const;
    bool hasHostRights(std::shared_ptr<STKPeer> peer) const;
    bool checkPeersReady(bool ignore_ai_peer, SelectionPhase phase);
    bool supportsAI();
    void setPlayerKarts(const NetworkString& ns, std::shared_ptr<STKPeer> peer) const;
    int getReservedId(std::shared_ptr<NetworkPlayerProfile>& p,
                      unsigned local_id);
    void resetServer();
    void addWaitingPlayersToGame();

public:
    PlayingRoom();
    ~PlayingRoom();
    void setup();
    ServerState getCurrentState() const { return m_state.load(); }
    bool isRacing() const                  { return m_state.load() == RACING; }
    int getDifficulty() const                   { return m_difficulty.load(); }
    int getGameMode() const                      { return m_game_mode.load(); }
    int getLobbyPlayers() const              { return m_lobby_players.load(); }
    bool isAIProfile(const std::shared_ptr<NetworkPlayerProfile>& npp) const
    {
        return std::find(m_ai_profiles.begin(), m_ai_profiles.end(), npp) !=
            m_ai_profiles.end();
    }
    void erasePeerReady(std::shared_ptr<STKPeer> peer)
                                                 { m_peers_ready.erase(peer); }
    bool isWorldPicked() const         { return m_state.load() >= LOAD_WORLD; }
    bool isWorldFinished() const   { return m_state.load() >= RESULT_DISPLAY; }
    bool isStateAtLeastRacing() const      { return m_state.load() >= RACING; }
    std::shared_ptr<GameInfo> getGameInfo() const       { return m_game_info; }
    std::shared_ptr<STKPeer> getServerOwner() const
                                              { return m_server_owner.lock(); }
    void doErrorLeave()                         { m_state.store(ERROR_LEAVE); }
    bool isWaitingForStartGame() const
                           { return m_state.load() == WAITING_FOR_START_GAME; }

public: // were public before and SL doesn't call them

    void setTimeoutFromNow(int seconds);
    void setInfiniteTimeout();
    bool isInfiniteTimeout() const;
    bool isTimeoutExpired() const;
    float getTimeUntilExpiration() const;
    void onSpectatorStatusChange(const std::shared_ptr<STKPeer>& peer);
    int getPermissions(std::shared_ptr<STKPeer> peer) const;
    int getCurrentStateScope();
    void resetToDefaultSettings();
    void saveInitialItems(std::shared_ptr<NetworkItemManager> nim);
    bool waitingForPlayers() const;
    void setKartDataProperly(KartData& kart_data, const std::string& kart_name,
                             std::shared_ptr<NetworkPlayerProfile> player,
                             const std::string& type) const;

public: // SL needs to call them
    void playerFinishedResult(Event *event);
    void liveJoinRequest(Event* event);
    void finishedLoadingLiveJoinClient(Event *event);
    void handleKartInfo(Event* event);
    void clientInGameWantsToBackLobby(Event* event);
    void finishedLoadingWorldClient(Event *event);
    void clientSelectingAssetsWantsToBackLobby(Event* event);
    void handlePlayerVote(Event *event);
    void startSelection(const Event *event=NULL); // was public already
    void kartSelectionRequested(Event* event);
};

#endif // PLAYING_ROOM_HPP