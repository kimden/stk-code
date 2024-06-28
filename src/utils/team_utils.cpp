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

#include "utils/string_utils.hpp"
#include "utils/team_utils.hpp"


void TeamManager::addTeam(std::string hardcoded_code,
                        std::string hardcoded_name,
                        float hardcoded_color, std::string hardcoded_emoji)
{
    int index = m_teams.size();
    m_teams.emplace_back(hardcoded_code, hardcoded_name,
                         hardcoded_color, hardcoded_emoji);
    m_finder_by_code[hardcoded_code] = index;
    m_finder_by_name[hardcoded_name] = index;
} // addTeam
// ========================================================================

void TeamManager::addCode(int idx, std::string hardcoded_code)
{
    m_teams[idx].addCode(hardcoded_code);
    m_finder_by_code[hardcoded_code] = idx;
} // addCode
// ========================================================================

void TeamManager::addName(int idx, std::string hardcoded_name)
{
    m_teams[idx].addName(hardcoded_name);
    m_finder_by_name[hardcoded_name] = idx;
} // addName
// ========================================================================

const CustomTeam& TeamManager::operator[](int idx) const
{
    return m_teams[idx];
} // getTeamByIndex
// ========================================================================

int TeamManager::getIndexByCode(const std::string& code) const
{
    auto it = m_finder_by_code.find(code);
    if (it == m_finder_by_code.end())
        // I am not sure I shouldn't return -1 but I guess for now it
        // breaks something. Check out the neighboring comment too
        return 0;
    return it->second;
} // getIndexByCode
// ========================================================================

int TeamManager::getIndexByName(const std::string& name) const
{
    auto it = m_finder_by_name.find(name);
    if (it == m_finder_by_name.end())
        // I am not sure I shouldn't return -1 but I guess for now it
        // breaks something. Check out the neighboring comment too
        return 0;
    return it->second;
} // getIndexByName
// ========================================================================

TeamManager::TeamManager()
{
    addTeam("-", "none",    0.00, "");
    addTeam("r", "red",     1.00, StringUtils::utf32ToUtf8({0x1f7e5})); // 1
    addTeam("o", "orange",  0.05, StringUtils::utf32ToUtf8({0x1f7e7})); // 2
    addTeam("y", "yellow",  0.16, StringUtils::utf32ToUtf8({0x1f7e8})); // 3
    addTeam("g", "green",   0.33, StringUtils::utf32ToUtf8({0x1f7e9})); // 4
    addTeam("b", "blue",    0.66, StringUtils::utf32ToUtf8({0x1f7e6})); // 5
    addTeam("p", "purple",  0.78, StringUtils::utf32ToUtf8({0x1f7ea})); // 6
    addTeam("c", "cyan",    0.46, StringUtils::utf32ToUtf8({0x1f5fd})); // 7
    addTeam("m", "magenta", 0.94, StringUtils::utf32ToUtf8({0x1f338})); // 8
    addTeam("s", "sky",     0.58, StringUtils::utf32ToUtf8({ 0x2604})); // 9
    addCode(6, "v");
    addName(6, "violet");
    addName(8, "pink");
} // TeamManager
// ========================================================================

