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

CommandVoting::CommandVoting(double threshold): m_threshold(threshold)
{

} // CommandVoting
// ========================================================================

int CommandVoting::castVote(std::string player, std::string category, std::string vote)
{
    uncastVote(player, category);
    m_votes_by_player[player][category] = vote;
    m_votes_by_poll[category][vote].insert(player);
    m_need_check = true;
    return m_votes_by_poll[category][vote].size();
} // castVote
// ========================================================================

void CommandVoting::uncastVote(std::string player, std::string category)
{
    auto it = m_votes_by_player[player].find(category);
    if (it != m_votes_by_player[player].end())
    {
        std::string previous_vote = it->second;
        m_votes_by_poll[category][previous_vote].erase(player);
    }
    m_need_check = true;
} // uncastVote
// ========================================================================

std::map<std::string, std::string> CommandVoting::process(std::multiset<std::string>& all_users)
{
    m_need_check = false;
    std::map<std::string, std::string> result;
    int required_number = (int)(all_users.size() * m_threshold);
    for (const auto& p: m_votes_by_poll)
    {
        std::string category = p.first;
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
        for (const auto& q: category_results)
        {
            if (q.second >= required_number)
            {
                result[category] = q.first;
                break;
            }
        }
    }
    for (const auto& p: result)
    {
        std::string category = p.first;
        for (const auto& q: m_votes_by_poll[category])
        {
            std::string vote = q.first;
            for (const auto& player: q.second)
            {
                m_votes_by_player[player].erase(category);
            }
        }
        m_votes_by_poll[category].clear();
    }
    return result;
} // process
// ========================================================================