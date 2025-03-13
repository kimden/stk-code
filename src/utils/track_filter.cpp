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

#include "utils/log.hpp"
#include "utils/random_generator.hpp"
#include "utils/string_utils.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <exception>

std::string Filter::PLACEHOLDER_STRING = ":placeholder";
//-----------------------------------------------------------------------------

TrackFilter::TrackFilter()
{

}   // TrackFilter
//-----------------------------------------------------------------------------

TrackFilter::TrackFilter(std::string input)
{
    // Make sure to update prepareAssetNames too!
    m_initial_string = input;
    if (input == PLACEHOLDER_STRING)
    {
        m_placeholder = true;
        return;
    }
    auto tokens = StringUtils::split(input, ' ');
    bool good = true;
    others = false;
    bool unknown_others = true;
    for (unsigned i = 0; i < tokens.size(); i++)
    {
        if (tokens[i] == "" || tokens[i] == " ")
            continue;
        else if (tokens[i] == "random")
        {
            m_pick_random = true;
            m_random_count = 1;
            int value = -1;
            if (i + 1 < tokens.size()
                    && StringUtils::parseString<int>(tokens[i + 1], &value)
                    && value > 0)
            {
                m_random_count = value;
                ++i;
            }
        }
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
            unknown_others = false;
            others = true;
        }
        else if (tokens[i] == "other:no")
        {
            unknown_others = false;
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
            if (separator != (int)std::string::npos)
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
    if (unknown_others)
    {
        if (!allowed.empty() || !w_allowed.empty())
            others = false;
        else
            others = true;
    }
}   // TrackFilter
//-----------------------------------------------------------------------------

template<typename T>
std::vector<SplitArgument> prepareAssetNames(std::string& input)
{
    // Basically does the same as the constructor but splits the string
    // into some kind of tokens such that some of them have to be checked for
    // being maps and some of them don't have to

    auto tokens = StringUtils::split(input, ' ');
    std::vector<SplitArgument> res;
    if (input == Filter::PLACEHOLDER_STRING)
        return {SplitArgument(input, -1, false)};

    for (unsigned i = 0; i < tokens.size(); i++)
    {
        if (i)
            res.emplace_back(" ", -1, false);
        res.emplace_back(tokens[i], i, false);
    }
    return res;
}   // Filter::prepareAssetNames
//-----------------------------------------------------------------------------

template<>
std::vector<SplitArgument> prepareAssetNames<TrackFilter>(std::string& input)
{
    auto tokens = StringUtils::split(input, ' ');
    std::vector<SplitArgument> res;
    if (input == Filter::PLACEHOLDER_STRING)
        return {SplitArgument(input, -1, false)};

    std::set<std::string> keywords = {
        "", " ", "random", "available", "unavailable", "official", "addon",
        "not", "no", "yes", "ok", "other:yes", "other:no"
    };
    for (unsigned i = 0; i < tokens.size(); i++)
    {
        if (i)
            res.emplace_back(" ", -1, false);

        if (keywords.find(tokens[i]) != keywords.end() || tokens[i][0] == '%')
        {
            res.emplace_back(tokens[i], i, false);
            if (tokens[i] == "random" && i + 1 < tokens.size())
            {
                res.emplace_back(" ", -1, false);
                res.emplace_back(tokens[i + 1], i + 1, false);
                i++;
            }
        }
        else
        {
            int separator = tokens[i].find(':');
            if (separator != (int)std::string::npos)
            {
                std::string track = tokens[i].substr(0, separator);
                std::string rest = tokens[i].substr(separator);
                res.emplace_back(track, i, true);
                res.emplace_back(rest, i, false);
            }
            else
            {
                res.emplace_back(tokens[i], i, true);
            }
        }
    }
    return res;
}   // TrackFilter::prepareAssetNames
//-----------------------------------------------------------------------------

std::string TrackFilter::get(const std::vector<std::string>& vec, int index)
{
    if (index >= 0 && index < (int)vec.size())
        return vec[index];
    if (index < 0 && index >= -(int)vec.size())
        return vec[(int)vec.size() + index];
    return "";
}   // get
//-----------------------------------------------------------------------------

void TrackFilter::apply(FilterContext& context) const
{
    // Does not use username and applied_at_selection_start
    // as they are the parameters for KartFilter
    if (isPlaceholder())
        return;
    std::set<std::string> copy = context.elements;
    context.elements.clear();

    std::set<std::string> names_allowed, names_forbidden;

    for (int x: w_allowed)
    {
        std::string name = get(context.wildcards, x);
        if (!name.empty())
            names_allowed.insert(name);
    }
    for (int x: w_forbidden)
    {
        std::string name = get(context.wildcards, x);
        if (!name.empty())
            names_forbidden.insert(name);
    }

    for (const std::string& s: copy)
    {
        bool addon = (s.length() >= 6 && s.substr(0, 6) == "addon_");
        bool yes = false;
        bool no = false;
        auto it = max_players.find(s);
        if (it != max_players.end() && it->second < context.num_players)
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
        {
            if (others)
                yes = true;
            else
                no = true;
        }
        if (yes)
            context.elements.insert(s);
    }
    if (m_pick_random && (int)context.elements.size() > m_random_count)
    {
        RandomGenerator rg;
        std::vector<int> take(m_random_count, 1);
        take.resize(context.elements.size(), 0);
        // Shuffling the vector like it's not having the form 11..1100..00
        for (unsigned i = 0; i < context.elements.size(); i++)
            std::swap(take[rg.get(i + 1)], take[i]);
        std::set<std::string> result;
        for (std::set<std::string>::iterator it = context.elements.begin();
                it != context.elements.end(); it++)
        {
            if (take.back() == 1)
                result.insert(*it);
            take.pop_back();
        }
        std::swap(result, context.elements);
    }
}   // TrackFilter::apply
//-----------------------------------------------------------------------------

KartFilter::KartFilter()
{

}   // KartFilter(0)
//-----------------------------------------------------------------------------

KartFilter::KartFilter(std::string input)
{
    // TODO: Make sure that username with spaces, quotes, bad chars, etc
    // are handled properly. Currently usernames are enclosed by a random brace
    // to prevent commonly used chars in a typical kart elimination.

    // TODO: Make sure player names never coincide with keywords
    m_initial_string = input;
    if (input == PLACEHOLDER_STRING)
    {
        m_placeholder = true;
        return;
    }
    // TODO: remove <> and replace with "" once the usernames cannot be keywords
    auto tokens = StringUtils::splitQuoted(input, ' ', '<', '>', '\\');
    bool good = true;
    bool mode_karts = true;
    bool unknown_unspecified = true;
    m_ignore_players_input = false;
    m_placeholder = false;
    for (unsigned i = 0; i < tokens.size(); i++)
    {
        if (tokens[i] == "" || tokens[i] == " ")
            continue;
        else if (tokens[i] == "karts")
            mode_karts = true;
        // else if (tokens[i] == "for")
        // {
        //     mode_karts = false;
        //     good = true;
        // }
        // else if (tokens[i] == "except")
        // {
        //     mode_karts = false;
        //     good = false;
        // }
        else if (tokens[i] == "not")
        {
            good = false;
        }
        else if (tokens[i] == "ignore")
        {
            m_ignore_players_input = true;
        }
        else if (tokens[i] == "other:yes")
        {
            unknown_unspecified = false;
            m_allow_unspecified_karts = true;
        }
        else if (tokens[i] == "other:no")
        {
            unknown_unspecified = false;
            m_allow_unspecified_karts = false;
        }
        else if (StringUtils::startsWith(tokens[i], "random("))
        {
            if (!mode_karts)
                continue;
            if (!good) {
                continue;
            }
            std::string karts_together = tokens[i].substr(7, tokens[i].length() - 8);
            auto karts_mentioned = StringUtils::split(karts_together, ',');
            m_random_stuff.push_back(karts_mentioned);
        }
        else
        {
            if (good)
                m_allowed_karts.insert(tokens[i]);
            else
                m_forbidden_karts.insert(tokens[i]);
        }
    }
    // int random_stuff_size = m_random_stuff.size();
    // if (!m_random_stuff.empty())
    // {
    //     for (std::string& kart: m_allowed_karts)
    //     {
    //         m_random_stuff.emplace_back();
    //         m_random_stuff.back().push_back(kart);
    //     }
    // }
    // for (int i = 0; i < random_stuff_size; i++)
    // {
    //     for (std::string& kart: m_random_stuff[i])
    //         m_allowed_karts.push_back();
    // }

    if (unknown_unspecified)
    {
        if (!m_allowed_karts.empty() || !m_random_stuff.empty())
            m_allow_unspecified_karts = false;
        else
            m_allow_unspecified_karts = true;
    }
}   // KartFilter(1)
//-----------------------------------------------------------------------------

void KartFilter::apply(FilterContext& context) const
{
    // Not using wildcards and num_players as they are TrackFilter parameters
    // Not username for now, subject to change later when username-keyword
    // conflicts are resolved

    if (isPlaceholder())
        return;

    // if (m_except_players.count(context.username) > 0 ||
    //     (m_for_players.count(context.username) == 0 && !m_apply_for_unspecified_players))
    // {
    //     return;
    // }

    // Ignoring means that the random of allowed and randoms will be picked afterwards
    // Not ignoring means that the player chooses from allowed
    // and one instance of each random
    std::set<std::string> result;

    // delete forbidden karts
    for (const std::string& kart: context.elements) {
        if (m_allowed_karts.count(kart) > 0)
        {
            result.insert(kart);
        }
        else if (m_forbidden_karts.count(kart) > 0)
        {
            continue;
        }
        else if (m_allow_unspecified_karts)
        {
            result.insert(kart);
        }
    }
    /*
     * KartFilter currently has two ways to allow karts: individually or as a part of
     * *random group*.
     *
     * There are two considerably different cases which this code tries to handle:
     * 1. m_ignore_players_input is FALSE, which means that the filter gives player
     *    all individual karts and a kart from each random group, to choose from them.
     *    If a player has nothing non-forbidden from the group, it is ignored.
     * 2. m_ignore_players_input is TRUE, which means that the filter chooses the
     *    kart ITSELF and only gives a list of all possible karts to player (so that
     *    the player is aware what can be encountered). The server chooses the random kart
     *    in the same way: picks one kart at random from every random group, adds individual
     *    karts and picks randomly from the result. Server chooses the kart when
     *    applied_at_selection_start is FALSE, that is, on the second run which is absent
     *    for (1). In this case, the random is invoked in go_on_race branch or in livejoin
     *    function, not here (for uniformity and because there can be other filters to apply).
     */

    // TODO: maybe would be bad in the future when
    // we try to force karts but anyway forcing should be done before applying the filters
    if ((!m_ignore_players_input && context.applied_at_selection_start)
        || (m_ignore_players_input && !context.applied_at_selection_start))
    {
        for (const auto& array: m_random_stuff)
        {
            std::vector<std::string> available;
            for (const std::string& kart: array)
            {
                if (m_forbidden_karts.count(kart) > 0)
                {
                    continue;
                }
                if (context.elements.count(kart) == 0)
                {
                    continue;
                }
                available.push_back(kart);
            }
            if (available.empty())
                continue;
            RandomGenerator rg;
            std::string s = available[rg.get(available.size())];
            result.insert(s);
        }
    }
    else if ((m_ignore_players_input && context.applied_at_selection_start)
        || (!m_ignore_players_input && !context.applied_at_selection_start))
    {
        for (const auto& array: m_random_stuff)
        {
            for (const std::string& kart: array)
            {
                if (m_forbidden_karts.count(kart) > 0)
                {
                    continue;
                }
                if (context.elements.count(kart) == 0)
                {
                    continue;
                }
                result.insert(kart);
            }
        }
    }
    std::swap(result, context.elements);
}   // KartFilter::apply
//-----------------------------------------------------------------------------
