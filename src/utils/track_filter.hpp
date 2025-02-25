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

struct SplitArgument {
    std::string value;
    int index;
    bool is_map;

    SplitArgument(const std::string& value, int index, bool is_map):
        value(value), index(index), is_map(is_map) {}
};

struct FilterContext {
    std::string username;
    std::set<std::string> elements;
    int num_players;
    std::vector<std::string> wildcards;
    bool applied_at_selection_start;
};

class Filter {
public:
    std::string m_initial_string;
    std::string toString() const { return "{ " + m_initial_string + " }"; }   // toString

    bool m_placeholder = false;
    bool isPlaceholder() const                        { return m_placeholder; }

    Filter() {}
    virtual ~Filter() {}
    Filter(std::string input) {}
    std::string getInitialString() const { return m_initial_string; }
    virtual void apply(FilterContext& context) const = 0;
    virtual bool ignoresPlayersInput() const { return false; }
    static std::string PLACEHOLDER_STRING;
};

class TrackFilter: public Filter
{
public:
    bool m_include_available = true;
    bool m_include_unavailable = true;
    bool m_include_official = true;
    bool m_include_addons = true;
    bool m_pick_random = false;
    int m_random_count = 0;
    std::set<std::string> allowed;
    std::set<std::string> forbidden;
    std::vector<int> w_allowed; // wildcards
    std::vector<int> w_forbidden; // wildcards
    std::map<std::string, int> max_players;
    bool others; // whether not specified tracks are allowed
    TrackFilter();
    TrackFilter(std::string input);
    static std::string get(const std::vector<std::string>& vec, int index);
    void apply(FilterContext& context) const override;
    bool isPickingRandom() const                      { return m_pick_random; }
};

// A structure to apply requirements to the kart set. Currently
// supports a narrow set of filters, like forcing all players except
// to take a kart or a random kart set.
// It should be also used by Kart Elimination, even though for now
// Kart Elimination will NOT use KartFilter, as KartFilter is yet to
// support custom player sets to be included/excluded from the filter.
// Technically there are no issues with adding custom player sets,
// EXCEPT that it is completely unusable if local player names coincide
// with keywords; I would like to handle that later and VERY CAREFULLY

// Currently works only for standard karts, as addons are probably shown
// for the player anyway. If addons are forbidden by the filter but chosen,
// there is an additional check while the world is loading anyway.

// Currently KartFilter does not support wildcards.

class KartFilter: public Filter
{
private:
    bool m_ignore_players_input = false;
    std::set<std::string> m_allowed_karts;
    std::set<std::string> m_forbidden_karts;
    std::vector<std::vector<std::string>> m_random_stuff;
    bool m_allow_unspecified_karts;
    // std::vector<std::string> m_for_players;
    // std::vector<std::string> m_except_players;
    // bool m_apply_for_unspecified_players;
public:
    KartFilter();
    KartFilter(std::string input);

    bool ignoresPlayersInput() const override { return m_ignore_players_input; }
    // apply is called when the selection starts just like for maps,
    // while applyAfterwards is called when the selection ends to determine the
    // random kart for a player who selected nothing (or a random kart,
    // or if the filter ignores player's input.
    void apply(FilterContext& context) const override;
    bool isPlaceholder() const                        { return m_placeholder; }
};



// TODO: prepareMapNames ONLY WORKS FOR MAPS, THERE IS NO TYPO FIXING
// FOR KARTS. Maybe this should be changed but idk how yet.
template<typename T>
std::vector<SplitArgument> prepareAssetNames(std::string& input);

#endif
