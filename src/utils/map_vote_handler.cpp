//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2024 kimden
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

#include "utils/map_vote_handler.hpp"
#include "utils/string_utils.hpp"
#include "utils/log.hpp"
#include "utils/random_generator.hpp"

MapVoteHandler::MapVoteHandler(): algorithm(0)
{

}   // MapVoteHandler
// ============================================================================


template<typename T>
void MapVoteHandler::findMajorityValue(const std::map<T, unsigned>& choices, unsigned cur_players,
                       T* best_choice, float* rate)
{
    RandomGenerator rg;
    unsigned max_votes = 0;
    auto best_iter = choices.begin();
    unsigned best_iters_count = 1;
    // Among choices with max votes, we need to pick one uniformly,
    // thus we have to keep track of their number
    for (auto iter = choices.begin(); iter != choices.end(); iter++)
    {
        if (iter->second > max_votes)
        {
            max_votes = iter->second;
            best_iter = iter;
            best_iters_count = 1;
        }
        else if (iter->second == max_votes)
        {
            best_iters_count++;
            if (rg.get(best_iters_count) == 0)
            {
                max_votes = iter->second;
                best_iter = iter;
            }
        }
    }
    if (best_iter != choices.end())
    {
        *best_choice = best_iter->first;
        *rate = float(best_iter->second) / cur_players;
    }
}   // findMajorityValue
// ============================================================================

bool MapVoteHandler::handleAllVotes(std::map<uint32_t, PeerVote>& peers_votes,
                    float remaining_time, float max_time, bool is_over,
                    unsigned cur_players, PeerVote* default_vote,
                    PeerVote* winner_vote, uint32_t* winner_peer_id) const
{
    if (algorithm == 0)
        return standard(peers_votes, remaining_time, max_time, is_over, cur_players, default_vote, winner_vote, winner_peer_id);
    else // if (algorithm == 1)
        return random(peers_votes, remaining_time, max_time, is_over, cur_players, default_vote, winner_vote, winner_peer_id);
}   // handleAllVotes
// ============================================================================

bool MapVoteHandler::random(std::map<uint32_t, PeerVote>& peers_votes,
                    float remaining_time, float max_time, bool is_over,
                    unsigned cur_players, PeerVote* default_vote,
                    PeerVote* winner_vote, uint32_t* winner_peer_id) const
{
    if (!is_over)
        return false;

    if (peers_votes.empty())
    {
        *winner_vote = *default_vote;
        return true;
    }

    RandomGenerator rg;
    std::map<uint32_t, PeerVote>::iterator it = peers_votes.begin();
    std::advance(it, rg.get((int)peers_votes.size()));
    *winner_peer_id = it->first;
    *winner_vote = it->second;
    return true;
}   // random
// ============================================================================

bool MapVoteHandler::standard(std::map<uint32_t, PeerVote>& peers_votes,
                    float remaining_time, float max_time, bool is_over,
                    unsigned cur_players, PeerVote* default_vote,
                    PeerVote* winner_vote, uint32_t* winner_peer_id) const
{
    // Determine majority agreement when 35% of voting time remains,
    // reserve some time for kart selection so it's not 50%
    if (remaining_time / max_time > 0.35f)
    {
        return false;
    }

    if (peers_votes.empty())
    {
        if (is_over)
        {
            *winner_vote = *default_vote;
            return true;
        }
        return false;
    }

    std::string top_track = default_vote->m_track_name;
    unsigned top_laps = default_vote->m_num_laps;
    bool top_reverse = default_vote->m_reverse;

    std::map<std::string, unsigned> tracks;
    std::map<unsigned, unsigned> laps;
    std::map<bool, unsigned> reverses;

    // Ratio to determine majority agreement
    float tracks_rate = 0.0f;
    float laps_rate = 0.0f;
    float reverses_rate = 0.0f;

    for (auto& p : peers_votes)
    {
        auto track_vote = tracks.find(p.second.m_track_name);
        if (track_vote == tracks.end())
            tracks[p.second.m_track_name] = 1;
        else
            track_vote->second++;
        auto lap_vote = laps.find(p.second.m_num_laps);
        if (lap_vote == laps.end())
            laps[p.second.m_num_laps] = 1;
        else
            lap_vote->second++;
        auto reverse_vote = reverses.find(p.second.m_reverse);
        if (reverse_vote == reverses.end())
            reverses[p.second.m_reverse] = 1;
        else
            reverse_vote->second++;
    }

    findMajorityValue<std::string>(tracks, cur_players, &top_track, &tracks_rate);
    findMajorityValue<unsigned>(laps, cur_players, &top_laps, &laps_rate);
    findMajorityValue<bool>(reverses, cur_players, &top_reverse, &reverses_rate);

    // End early if there is majority agreement which is all entries rate > 0.5
    auto it = peers_votes.begin();
    if (tracks_rate > 0.5f && laps_rate > 0.5f && reverses_rate > 0.5f)
    {
        while (it != peers_votes.end())
        {
            if (it->second.m_track_name == top_track &&
                it->second.m_num_laps == top_laps &&
                it->second.m_reverse == top_reverse)
                break;
            else
                it++;
        }
        if (it == peers_votes.end())
        {
            // Don't end if no vote matches all majority choices
            Log::warn("ServerLobby",
                "Missing track %s from majority.", top_track.c_str());
            it = peers_votes.begin();
            if (!is_over)
                return false;
        }
        *winner_peer_id = it->first;
        *winner_vote = it->second;
        return true;
    }
    if (is_over)
    {
        // Pick the best lap (or soccer goal / time) from only the top track
        // if no majority agreement from all
        int diff = std::numeric_limits<int>::max();
        auto closest_lap = peers_votes.begin();
        while (it != peers_votes.end())
        {
            if (it->second.m_track_name == top_track &&
                std::abs((int)it->second.m_num_laps - (int)top_laps) < diff)
            {
                closest_lap = it;
                diff = std::abs((int)it->second.m_num_laps - (int)top_laps);
            }
            else
                it++;
        }
        *winner_peer_id = closest_lap->first;
        *winner_vote = closest_lap->second;
        return true;
    }
    return false;
} // standard
// ========================================================================
