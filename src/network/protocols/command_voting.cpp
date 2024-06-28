//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2021 kimden
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

#include "network/protocols/command_voting.hpp"
#include "utils/random_generator.hpp"

const double CommandVoting::DEFAULT_THRESHOLD = 0.500001;

CommandVoting::CommandVoting(double threshold): m_threshold(threshold)
{

} // CommandVoting
// ========================================================================

void CommandVoting::castVote(std::string player, std::string category, std::string vote)
{
    uncastVote(player, category);
    m_votes_by_player[player][category] = vote;
    m_votes_by_poll[category][vote].insert(player);
    m_need_check = true;
    m_selected_options[category] = vote;
} // castVote
// ========================================================================

void CommandVoting::uncastVote(std::string player, std::string category)
{
    std::string previous_vote = "";
    auto it = m_votes_by_player[player].find(category);
    if (it != m_votes_by_player[player].end())
    {
        previous_vote = it->second;
        m_votes_by_poll[category][previous_vote].erase(player);
        m_votes_by_player[player].erase(it);
    }
    m_need_check = true;
    // We add category to selected options because:
    // - we need to see the number of votes even if the player unvoted
    // - uncast is called inside cast, so we cannot clear the map entirely
    m_selected_options[category] = "";
} // uncastVote
// ========================================================================

std::pair<std::map<std::string, int>, std::map<std::string, std::string>>
CommandVoting::process(std::multiset<std::string>& all_users)
{
    std::map<std::string, std::string> result;
    std::map<std::string, int> num_votes;
    int required_number;
    for (const auto& p: m_votes_by_poll)
    {
        std::string category = p.first;
        double category_threshold = m_threshold;
        auto it = m_custom_thresholds.find(category);
        if (it != m_custom_thresholds.end())
            category_threshold = it->second;
        required_number = std::max<int>(1, (int)ceil((double)all_users.size() * category_threshold));
        std::map<std::string, int> category_results;
        for (const std::string& user: all_users)
        {
            auto it = m_votes_by_player.find(user);
            if (it == m_votes_by_player.end())
                continue;
            auto it2 = it->second.find(category);
            if (it2 == it->second.end())
                continue;
            ++category_results[it2->second];
        }
        auto it4 = m_selected_options.find(category);
        for (const auto& q: category_results)
        {
            if (it4 != m_selected_options.end() && q.first == it4->second)
            {
                num_votes[category] = q.second;
            }
            if (q.second >= required_number)
            {
                result[category] = q.first;
                break;
            }
        }
    }
    m_need_check = false;
    m_selected_options.clear();
    for (const auto& p: result)
    {
        std::string category = p.first;
        reset(category);
    }
    if (m_merge != -1 && !result.empty())
    {
        std::string new_category = "";
        std::string new_command = "";
        for (auto& p: result) {
            if (new_command.empty())
            {
                new_category = p.first;
                new_command += p.second;
            }
            else
            {
                new_command += " " + p.first.substr(m_merge) + " " + p.second;
            }
        }
        result.clear();
        result[new_category] = new_command;
    }
    return make_pair(num_votes, result);
} // process
// ========================================================================

std::string CommandVoting::getAnyBest(std::string category)
{
    int best = 0;
    std::vector<std::string> best_options;
    for (auto& vote_to_set: m_votes_by_poll[category])
    {
        unsigned int count = vote_to_set.second.size();
        std::string option = vote_to_set.first;
        if (count > best)
        {
            best_options.clear();
            best_options.push_back(option);
        }
        else if (count == best)
        {
            best_options.push_back(option);
        }
    }
    if (best_options.empty())
        return ""; // shouldn't happen mainly
    RandomGenerator rg;
    std::vector<std::string>::iterator it = best_options.begin();
    std::advance(it, rg.get((int)best_options.size()));
    return *it;
} // getAnyBest
// ========================================================================

void CommandVoting::reset(std::string category)
{
    for (auto& vote_to_set: m_votes_by_poll[category])
    {
        std::string vote = vote_to_set.first;
        auto& players = vote_to_set.second;
        for (const std::string& player: players)
        {
            m_votes_by_player[player].erase(category);
        }
    }
    m_votes_by_poll[category].clear();
} // reset
// ========================================================================

void CommandVoting::resetAllVotes()
{
    m_votes_by_poll.clear();
    m_votes_by_player.clear();
} // resetAllVotes
// ========================================================================
