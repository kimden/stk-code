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

#include "utils/kart_elimination.hpp"

#include "utils/string_utils.hpp"
#include "utils/log.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <exception>


//-----------------------------------------------------------------------------
KartElimination::KartElimination()
{
    m_enabled = false;
    m_remained = 0;
    m_kart = "";
}   // KartElimination
//-----------------------------------------------------------------------------
bool KartElimination::isEliminated(std::string username) const
{
    if (!m_enabled || m_remained < 0)
        return false;
    for (int i = 0; i < m_remained; i++)
        if (m_participants[i] == username)
            return false;
    return true;
}   // isEliminated
//-----------------------------------------------------------------------------
std::set<std::string> KartElimination::getRemainingParticipants() const
{
    std::set<std::string> ans;
    if (!m_enabled)
        return ans;
    for (int i = 0; i < m_remained; i++)
        ans.insert(m_participants[i]);
    return ans;
}   // getRemainingParticipants
//-----------------------------------------------------------------------------
std::string KartElimination::getStandings() const
{
    std::string result = "Gnu Elimination ";
    if (m_enabled)
        result += "is running";
    else
        result += "is disabled";
    if (!m_participants.empty())
        result += ", standings:";
    for (int i = 0; i < (int)m_participants.size(); i++)
    {
        std::string line = "\n" + (i < m_remained ?
            std::to_string(i + 1) : "[" + std::to_string(i + 1) + "]");
        line += ". " + m_participants[i];
        result += line;
    }
    return result;
}   // getStandings
//-----------------------------------------------------------------------------
void KartElimination::disable()
{
    m_kart = "";
    m_enabled = false;
    m_remained = 0;
    m_participants.clear();
}   // disable
//-----------------------------------------------------------------------------
void KartElimination::enable(std::string kart)
{
    m_kart = kart;
    m_enabled = true;
    m_remained = -1;
    m_participants.clear();
}   // enable
//-----------------------------------------------------------------------------
std::string KartElimination::getStartingMessage() const
{
    if (m_kart == "gnu")
        return "Gnu Elimination starts now! Use /standings "
            "after each race for results.";
    else
        return StringUtils::insertValues("Gnu Elimination starts now "
            "(elimination kart: %s)! Use /standings "
            "after each race for results.", m_kart.c_str());
}   // getStartingMessage
//-----------------------------------------------------------------------------
std::string KartElimination::getWarningMessage(bool isEliminated) const
{
    std::string what = "Gnu Elimination is played right now on this server";
    what += (isEliminated ?
        ", you will be forced to use kart %s until it ends." :
        " with kart %s. You are not eliminated yet.");
    what += " Use /standings to see the results.";
    return StringUtils::insertValues(what, m_kart.c_str());
}   // getWarningMessage
//-----------------------------------------------------------------------------

std::string KartElimination::update(
    std::map<std::string, double>& order)
{
    assert(m_remained != 0);
    if (m_remained < 0)
    {
        m_remained = order.size();
        for (const auto& p: order)
            m_participants.push_back(p.first);
    }
    for (int i = 0; i < m_remained; i++)
    {
        if (order.count(m_participants[i]) == 0)
            order[m_participants[i]] = KartElimination::INF_TIME;
    }
    std::stable_sort(m_participants.begin(), m_participants.begin() + m_remained,
        [order](const std::string& a, const std::string& b) -> bool {
            auto it1 = order.find(a);
            auto it2 = order.find(b);
            return it1->second < it2->second;
    });
    std::string msg = "";
    msg += m_participants[--m_remained];
    bool alone = true;
    while (m_remained - 1 >= 0 && order[m_participants[m_remained - 1]] == KartElimination::INF_TIME)
    {
        msg += ", " + m_participants[--m_remained];
        alone = false;
    }
    if (alone)
        msg += " is now eliminated.";
    else
        msg += " are now eliminated.";
    if (m_remained <= 1) {
        m_enabled = false;
        msg += "\nGnu Elimination has finished! Congratulations to " + m_participants[0] + " !";
    }
    return msg;
}   // update
//-----------------------------------------------------------------------------

/* EOF */
