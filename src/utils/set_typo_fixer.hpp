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

#ifndef SET_TYPO_FIXER_HPP
#define SET_TYPO_FIXER_HPP

#include "irrString.h"

#include <algorithm>
#include <functional>
#include <set>
#include <vector>
#include <string>

// A lazy class that stores a set of strings.
// For a query string, it finds exactly the same string in the set
// if it exists, otherwise iterates over the set and suggests to you
// the closest string according to edit distance.

class SetTypoFixer
{
private:
	std::multiset<std::string> m_set;
public:
	void add(const std::string& value);
	void remove(const std::string& value);
	std::vector<std::pair<std::string, int>> getClosest(
		const std::string& query, int count = 3, bool case_sensitive = true) const;
};
#endif // SET_TYPO_FIXER_HPP
