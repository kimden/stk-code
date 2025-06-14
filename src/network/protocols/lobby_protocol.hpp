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

#ifndef LOBBY_PROTOCOL_HPP
#define LOBBY_PROTOCOL_HPP

#include "network/protocol.hpp"
#include "network/packet_types.hpp"
#include "utils/stk_process.hpp"

class GameSetup;
class NetworkPlayerProfile;
class PeerVote;
class RemoteKartInfo;
class Track;

#include <atomic>
#include <cassert>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

/*!
 * \class LobbyProtocol
 * \brief Base class for both client and server lobby. The lobbies are started
 *  when a server opens a game, or when a client joins a game.
 *  It is used to exchange data about the race settings, like kart selection.
 */
class LobbyProtocol : public Protocol
{
public:
    // Enums were moved to packet_types.hpp

protected:
    const ProcessType m_process_type;
    /** Vote from each peer. The host id is used as a key. Note that
     *  host ids can be non-consecutive, so we cannot use std::vector. */
    std::map<uint32_t, PeerVote> m_peers_votes;

    /** Timer user for voting periods in both lobbies. */
    std::atomic<uint64_t> m_end_voting_period;

    /** The maximum voting time. */
    uint64_t m_max_voting_time;

    std::thread m_start_game_thread;

    static std::weak_ptr<LobbyProtocol> m_lobby[PT_COUNT];

    /** Estimated current started game remaining time,
     *  uint32_t max if not available. */
    std::atomic<uint32_t> m_estimated_remaining_time;

    /** Estimated current started game progress in 0-100%,
      * uint32_t max if not available. */
    std::atomic<uint32_t> m_estimated_progress;

    /** Save the last live join ticks, for physical objects to update current
      * transformation in server, and reset smooth network body in client. */
    int m_last_live_join_util_ticks;

    /** Mutex to protect m_current_track. */
    mutable std::mutex m_current_track_mutex;

    /** Store current playing track in name. */
    std::string m_current_track;

    /** Stores data about the online game to play. */
    GameSetup* m_game_setup;

    // ------------------------------------------------------------------------
    void configRemoteKart(
        const std::vector<std::shared_ptr<NetworkPlayerProfile> >& players,
        int local_player_size) const;
    // ------------------------------------------------------------------------
    void joinStartGameThread()
    {
        if (m_start_game_thread.joinable())
            m_start_game_thread.join();
    }
    // ------------------------------------------------------------------------
    void addLiveJoiningKart(int kart_id, const RemoteKartInfo& rki,
                            int live_join_util_ticks) const;
    // ------------------------------------------------------------------------
    void exitGameState();
public:

    /** Creates either a client or server lobby protocol as a singleton. */
    template<typename Singleton, typename... Types>
        static std::shared_ptr<Singleton> create(Types ...args)
    {
        ProcessType pt = STKProcess::getType();
        assert(m_lobby[pt].expired());
        auto ret = std::make_shared<Singleton>(args...);
        m_lobby[pt] = ret;
        return std::dynamic_pointer_cast<Singleton>(ret);
    }   // create

    // ------------------------------------------------------------------------
    /** Returns the singleton client or server lobby protocol. */
    template<class T> static std::shared_ptr<T> get()
    {
        ProcessType pt = STKProcess::getType();
        if (std::shared_ptr<LobbyProtocol> lp = m_lobby[pt].lock())
        {
            std::shared_ptr<T> new_type = std::dynamic_pointer_cast<T>(lp);
            if (new_type)
                return new_type;
        }
        return nullptr;
    }   // get

    // ------------------------------------------------------------------------
    /** Returns specific singleton client or server lobby protocol. */
    template<class T> static std::shared_ptr<T> getByType(ProcessType pt)
    {
        if (std::shared_ptr<LobbyProtocol> lp = m_lobby[pt].lock())
        {
            std::shared_ptr<T> new_type = std::dynamic_pointer_cast<T>(lp);
            if (new_type)
                return new_type;
        }
        return nullptr;
    }   // get

    // ------------------------------------------------------------------------

             LobbyProtocol();
    virtual ~LobbyProtocol();
    virtual void setup()                = 0;
    virtual void update(int ticks)      = 0;
    virtual void finishedLoadingWorld() = 0;
    virtual void loadWorld();
    virtual bool isRacing() const = 0;
    void startVotingPeriod(float max_time);
    float getRemainingVotingTime();
    bool isVotingOver();
    // ------------------------------------------------------------------------
    /** Returns the maximum floating time in seconds. */
    float getMaxVotingTime() { return m_max_voting_time / 1000.0f; }
    // ------------------------------------------------------------------------
    /** Returns the game setup data structure. */
    GameSetup* getGameSetup() const { return m_game_setup; }
    // ------------------------------------------------------------------------
    /** Returns the number of votes received so far. */
    int getNumberOfVotes() const { return (int)m_peers_votes.size(); }
    // -----------------------------------------------------------------------
    void addVote(uint32_t host_id, const PeerVote &vote);
    // -----------------------------------------------------------------------
    const PeerVote* getVote(uint32_t host_id) const;
    // -----------------------------------------------------------------------
    void resetVotingTime()                   { m_end_voting_period.store(0); }
    // -----------------------------------------------------------------------
    /** Returns all voting data.*/
    const std::map<uint32_t, PeerVote>& getAllVotes() const
                                                     { return m_peers_votes; }
    // -----------------------------------------------------------------------
    std::pair<uint32_t, uint32_t> getGameStartedProgress() const
    {
        return std::make_pair(m_estimated_remaining_time.load(),
            m_estimated_progress.load());
    }
    // ------------------------------------------------------------------------
    void setGameStartedProgress(const std::pair<uint32_t, uint32_t>& p)
    {
        m_estimated_remaining_time.store(p.first);
        m_estimated_progress.store(p.second);
    }
    // ------------------------------------------------------------------------
    void resetGameStartedProgress()
    {
        m_estimated_remaining_time.store(std::numeric_limits<uint32_t>::max());
        m_estimated_progress.store(std::numeric_limits<uint32_t>::max());
    }
    // ------------------------------------------------------------------------
    bool hasLiveJoiningRecently() const;
    // ------------------------------------------------------------------------
    void storePlayingTrack(const std::string& track_ident)
    {
        std::lock_guard<std::mutex> lock(m_current_track_mutex);
        m_current_track = track_ident;
    }
    // ------------------------------------------------------------------------
    std::string getPlayingTrackIdent() const
    {
        std::lock_guard<std::mutex> lock(m_current_track_mutex);
        return m_current_track;
    }
    // ------------------------------------------------------------------------
    Track* getPlayingTrack() const;
};   // class LobbyProtocol

#endif // LOBBY_PROTOCOL_HPP
