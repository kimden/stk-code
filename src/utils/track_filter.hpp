//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2004-2020  Steve Baker <sjbaker1@airmail.net>,
//  Copyright (C) 2004-2020  Ingo Ruhnke <grumbel@gmx.de>
//  Copyright (C) 2006-2020  SuperTuxKart-Team
//  Copyright (C) 2020  kimden
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

#ifndef HEADER_TRACK_FILTER_HPP
#define HEADER_TRACK_FILTER_HPP

#include "utils/types.hpp"
#include <limits>
#include <string>
#include <vector>
#include <sstream>
#include <map>
#include <set>

// A structure to apply requirements to the track set. allowed contains 
// the only tracks to be allowed, forbidden contains the only tracks to be
// forbidden. Putting one track into both vectors produces undefined behaviour
// for now. Works with wildcards (indices are integers: ..., %-1, %0, %1, ...).

struct TrackFilter
{
    bool m_include_available = true;
    bool m_include_unavailable = true;
    bool m_include_official = true;
    bool m_include_addons = true;
    bool m_pick_random = false;
    std::set<std::string> allowed;
    std::set<std::string> forbidden;
    std::vector<int> w_allowed; // wildcards
    std::vector<int> w_forbidden; // wildcards
    std::map<std::string, int> max_players;
    bool others; // whether not specified tracks are allowed
    TrackFilter();
    TrackFilter(std::string input);
    static std::string get(std::vector<std::string>& vec, int index);
    void apply(int num_players, std::set<std::string>& input) const;
    void apply(int num_players, std::set<std::string>& input,
        std::vector<std::string>& wildcards) const;
    bool isPickingRandom() const                      { return m_pick_random; }
};

#endif
