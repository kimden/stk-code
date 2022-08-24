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

#include "utils/track_filter.hpp"

#include "utils/string_utils.hpp"
#include "utils/log.hpp"
#include "random_generator.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <exception>

TrackFilter::TrackFilter()
{

}   // TrackFilter
//-----------------------------------------------------------------------------

TrackFilter::TrackFilter(std::string input)
{
    auto tokens = StringUtils::split(input, ' ');
    bool good = true;
    others = false;
    if (tokens.size() == 0)
        others = true;
    for (unsigned i = 0; i < tokens.size(); i++)
    {
        if (tokens[i] == "" || tokens[i] == " ")
            continue;
        else if (tokens[i] == "random")
            m_pick_random = true;
        else if (tokens[i] == "available")
            m_include_unavailable = false;
        else if (tokens[i] == "unavailable")
            m_include_available = false;
        else if (tokens[i] == "official")
            m_include_addons = false;
        else if (tokens[i] == "addon")
            m_include_official = false;
        else if (tokens[i] == "not" || tokens[i] == "no")
        {
            good = false;
            if (i == 0)
                others = true;
        }
        else if (tokens[i] == "yes" || tokens[i] == "ok")
        {
            good = true;
        }
        else if (tokens[i] == "other:yes")
        {
            others = true;
        }
        else if (tokens[i] == "other:no")
        {
            others = false;
        }
        else if (tokens[i][0] == '%')
        {
            int index;
            std::string cut = tokens[i].substr(1);
            if (!StringUtils::parseString<int>(cut, &index))
            {
                Log::warn("TrackFilter", "Unable to parse wildcard index "
                    "from \"%s\", omitting it", tokens[i].c_str());
                continue;
            }
            if (good)
                w_allowed.push_back(index);
            else
                w_forbidden.push_back(index);
        }
        else
        {
            int separator = tokens[i].find(':');
            if (separator != std::string::npos)
            {
                std::string track = tokens[i].substr(0, separator);
                std::string params_str = tokens[i].substr(separator + 1);
                int value;
                if (!StringUtils::parseString<int>(params_str, &value))
                {
                    Log::warn("TrackFilter", "Incorrect integer value %s of "
                        "max-players for track %s", params_str.c_str(),
                        track.c_str());
                }
                max_players[track] = value;
                // std::vector<std::string> params = StringUtils::split(
                //     params_str, ',', false);
                // parameters[track] = params;
            }
            else
            {
                if (good)
                    allowed.insert(tokens[i]);
                else
                    forbidden.insert(tokens[i]);
            }
        }
    }
}   // TrackFilter
//-----------------------------------------------------------------------------
std::string TrackFilter::get(std::vector<std::string>& vec, int index)
{
    if (index >= 0 && index < vec.size())
        return vec[index];
    if (index < 0 && index >= -(int)vec.size())
        return vec[(int)vec.size() + index];
    return "";
}   // get
//-----------------------------------------------------------------------------

void TrackFilter::apply(int num_players, std::set<std::string>& input) const
{
    std::vector<std::string> empty;
    apply(num_players, input, empty);
}   // apply
//-----------------------------------------------------------------------------

void TrackFilter::apply(int num_players, std::set<std::string>& input,
    std::vector<std::string>& wildcards) const
{
    std::set<std::string> copy = input;
    input.clear();

    std::set<std::string> names_allowed, names_forbidden;

    for (int x: w_allowed)
    {
        std::string name = get(wildcards, x);
        if (!name.empty())
            names_allowed.insert(name);
    }
    for (int x: w_forbidden)
    {
        std::string name = get(wildcards, x);
        if (!name.empty())
            names_forbidden.insert(name);
    }

    for (const std::string& s: copy)
    {
        bool addon = (s.length() >= 6 && s.substr(6) == "addon_");
        bool yes = false;
        bool no = false;
        auto it = max_players.find(s);
        if (it != max_players.end() && it->second < num_players)
            continue;
        if (names_allowed.count(s) || allowed.count(s))
            yes = true;
        if (names_forbidden.count(s) || forbidden.count(s))
            no = true;
        if ((!addon && !m_include_official)
            || (addon && !m_include_addons))
        {
            yes = false; // regardless of whether it's allowed or not
            no = true;
        }
        if (yes && no)
        {
            Log::warn("TrackFilter", "Track requirements contradict for %s, "
                "please check your config. Force allowing it.", s.c_str());
            no = false;
        }
        if (!yes && !no)
            if (others)
                yes = true;
            else
                no = true;
        if (yes)
            input.insert(s);
    }
    if (m_pick_random && input.size() > 0)
    {
        RandomGenerator rg;
        std::set<std::string>::iterator it = input.begin();
        std::advance(it, rg.get((int)input.size()));
        std::string choice = *it;
        input.clear();
        input.insert(choice);
    }
}   // apply (2)
//-----------------------------------------------------------------------------

/* EOF */
