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

#include "utils/set_typo_fixer.hpp"
#include "utils/string_utils.hpp"

void SetTypoFixer::add(const std::string& key)
{
    m_set.insert(key);
    m_map[key] = key;
} // add
// ========================================================================

void SetTypoFixer::add(const std::string& key, const std::string& value)
{
    m_set.insert(key);
    m_map[key] = value;
} // add
// ========================================================================

void SetTypoFixer::remove(const std::string& key)
{
    m_set.erase(m_set.find(key));
    if (m_set.find(key) == m_set.end())
        m_map.erase(key);
} // remove
// ========================================================================

std::vector<std::pair<std::string, int>> SetTypoFixer::getClosest(
    const std::string& query, int count, bool case_sensitive) const
{
    std::map<std::string, int> ans_map;
    std::vector<std::pair<std::string, int>> ans;
    if (m_set.empty())
        return ans;
    auto it = m_set.find(query);
    if (it != m_set.end())
    {
        ans.emplace_back(m_map.find(query)->second, 0);
        return ans;
    }
    const std::string& query_ref = query;
    for (const std::string& s: m_set)
    {
        int distance = StringUtils::getEditDistance(s,
            query_ref, case_sensitive, '*', '?');

        std::string value = m_map.find(s)->second;
        if (ans_map.count(value))
            ans_map[value] = std::min(ans_map[value], distance);
        else
            ans_map[value] = distance;
    }
    for (const auto& p: ans_map)
        ans.emplace_back(p.first, p.second);
    std::sort(ans.begin(), ans.end(),
        [](const std::pair<std::string, int>& a, const std::pair<std::string, int>& b) -> bool
    {
        if (a.second != b.second)
            return a.second < b.second;
        return a.first < b.first;
    });
    if (ans.size() > count)
        ans.resize(count);
    return ans;
} // getClosest
// ========================================================================
