//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2024-2025 kimden
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

#ifndef TEAM_UTILS_HPP
#define TEAM_UTILS_HPP

#include "irrString.h"
#include "utils/singleton.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

// A set of classes that will manipulate teams, their names
// and representations such as emojis, and probably something else
// in the future. Those teams currently have no relation to soccer
// or CTF teams, but some kind of synchronization would be nice.

// A class describing team properties. Properties are supposed to be
// initialized and remain unchanged for now.
class CustomTeam
{
private:
    // Codes currently should start with different chars
    // as e.g. swapteams in CommandManager breaks otherwise
    std::vector<std::string> m_codes; // Must contain at least 1 (primary) code
    std::vector<std::string> m_names; // Must contain at least 1 (primary) name
    float m_color;                      // One color only for now
    std::string m_emoji;              // Currently only one emoji
    std::string m_circle;
public:
    CustomTeam(std::vector<std::string>& codes,
               std::vector<std::string>& names,
               float color, std::string& emoji,
               const std::string& circle):
        m_codes(codes), m_names(names), m_color(color), m_emoji(emoji),
        m_circle(circle) {}

    CustomTeam(std::string& code, std::string& name,
               float color, std::string& emoji, const std::string& circle):
        m_codes(1, code), m_names(1, name), m_color(color), m_emoji(emoji),
        m_circle(circle) {}

    std::string getPrimaryCode() const                   { return m_codes[0]; }
    std::string getPrimaryName() const                   { return m_names[0]; }
    std::string getNameWithEmoji() const { return m_emoji + " " + m_names[0]; }
    float getColor() const                                  { return m_color; }
    std::string getEmoji() const                            { return m_emoji; }
    std::string getCircle() const                          { return m_circle; }
    void addCode(const std::string& code)          { m_codes.push_back(code); }
    void addName(const std::string& name)          { m_names.push_back(name); }
};

class TeamsStorage
{
private:
    std::vector<CustomTeam> m_teams;
    std::map<std::string, int> m_finder_by_code;
    std::map<std::string, int> m_finder_by_name;
    void addTeam(std::string hardcoded_code, std::string hardcoded_name,
                 float hardcoded_color,
                 std::string hardcoded_emoji,
                 std::string hardcoded_circle);
    void addCode(int idx, std::string hardcoded_code);
    void addName(int idx, std::string hardcoded_name);
public:
    TeamsStorage();
    // Teams are indexed from 1 to N, 0 means no team set (or undefined).
    // Previously the code had 4-way inconsistent indexation
    // but hopefully it's fixed now
    const CustomTeam& operator[](int idx) const;
    int getIndexByCode(const std::string& code) const;
    int getIndexByName(const std::string& name) const;
    int getNumberOfTeams() const
                              { return (int)m_teams.size() - 1; }
};

class TeamUtils: public Singleton<TeamsStorage>
{
public:
    static const int NO_TEAM = 0;
    static const CustomTeam& getTeamByIndex(int idx)
                                              { return (*getInstance())[idx]; }
    static int getIndexByCode(const std::string& code)
                                { return getInstance()->getIndexByCode(code); }
    static int getIndexByName(const std::string& name)
                                { return getInstance()->getIndexByName(name); }
    static int getNumberOfTeams()
                             { return (int)getInstance()->getNumberOfTeams(); }
    static int getIndexFromKartTeam(int8_t team)           { return team + 1; }
    static int8_t getKartTeamFromIndex(int team)
                           { return (team >= 0 && team <= 2 ? team - 1 : -1); }

    static int getClosestIndexByColor(float color,
            bool only_emojis = false, bool only_circles = false);
};


#endif // TEAM_UTILS_HPP
