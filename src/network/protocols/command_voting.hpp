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

#ifndef COMMAND_VOTING_HPP
#define COMMAND_VOTING_HPP

#include "irrString.h"

#include <algorithm>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <deque>
#include <vector>

class CommandVoting
{
	double m_threshold;
	bool m_need_check = false;
	std::string m_selected_category = "";
	std::string m_selected_option = "";
private:
	std::map<std::string, std::map<std::string, std::set<std::string>>> m_votes_by_poll;
	std::map<std::string, std::map<std::string, std::string>> m_votes_by_player;
public:
	CommandVoting(double threshold = 0.500001);
	bool needsCheck() { return m_need_check; }
	void castVote(std::string player, std::string category, std::string vote);
	void uncastVote(std::string player, std::string category);
	std::pair<int, std::map<std::string, std::string>> process(std::multiset<std::string>& all_users);
};

#endif // COMMAND_VOTING_HPP