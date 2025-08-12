//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2020-2025  kimden
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

#include "utils/log.hpp"
#include "utils/string_utils.hpp"
#include "race/race_manager.hpp"
#include "modes/world.hpp"
#include "network/protocols/server_lobby.hpp"
#include "karts/abstract_kart.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

namespace
{
    std::string g_default_kart = "gnu";

    std::string g_start_message_gnu =
            "Gnu Elimination starts now! "
            "Use /standings after each race for results.";

    std::string g_start_message_generic =
            "Gnu Elimination starts now (elimination kart: %s)! "
            "Use /standings after each race for results.";

    std::string g_place_continues = "%s. %s";
    std::string g_place_eliminated = "[%s]. %s";

    std::string g_warning_eliminated =
            "Gnu Elimination is played right now on this server, "
            "you will be forced to use kart %s until it ends.";

    std::string g_warning_continues =
            "Gnu Elimination is played right now on this server "
            "with kart %s. You are not eliminated yet.";

    std::string g_see_standings = "Use /standings to see the results.";

    std::string g_eliminated_alone = "is now eliminated.";
    std::string g_eliminated_plural = "are now eliminated.";

    std::string g_finished_winner =
            "Gnu Elimination has finished! Congratulations to %s !";

    std::string g_elimination_running = "Gnu Elimination is running";
    std::string g_elimination_disabled = "Gnu Elimination is disabled";
    std::string g_before_standings_prefix = "standings";

    std::string g_now_off = "Gnu Elimination is now off";
    std::string g_already_on = "Gnu Elimination mode was already enabled!";
    std::string g_already_off = "Gnu Elimination mode was already off!";
    std::string g_only_racing = "Gnu Elimination is available only with racing modes";

}   // namespace
//-----------------------------------------------------------------------------

void KartElimination::setupContextUser()
{
    m_enabled = false;
    m_remained = 0;
    m_kart = "";
}   // setupContextUser
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
    std::string result = (m_enabled ? g_elimination_running : g_elimination_disabled);

    if (!m_participants.empty())
        result += ", " + g_before_standings_prefix + ":";

    for (int i = 0; i < (int)m_participants.size(); i++)
    {
        std::string line = "\n";
        line += StringUtils::insertValues(
                i < m_remained ? g_place_continues : g_place_eliminated,
                i + 1,
                m_participants[i]
        );
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
    if (m_kart == g_default_kart)
        return g_start_message_gnu;
    else
        return StringUtils::insertValues(g_start_message_generic, m_kart.c_str());
}   // getStartingMessage
//-----------------------------------------------------------------------------

std::string KartElimination::getWarningMessage(bool isEliminated) const
{
    std::string what = (isEliminated ? g_warning_eliminated : g_warning_continues);
    what += " " + g_see_standings;
    return StringUtils::insertValues(what, m_kart.c_str());
}   // getWarningMessage
//-----------------------------------------------------------------------------

std::string KartElimination::onRaceFinished()
{
    World* w = World::getWorld();
    if (!w)
    {
        Log::error("KartElimination", "onRaceFinished aborted: World was not found.");
        return "";
    }

    int player_count = RaceManager::get()->getNumPlayers();
    std::map<std::string, double> order;
    for (int i = 0; i < player_count; i++)
    {
        std::string username = StringUtils::wideToUtf8(RaceManager::get()->getKartInfo(i).getPlayerName());
        if (w->getKart(i)->isEliminated())
            order[username] = KartElimination::INF_TIME;
        else
            order[username] = RaceManager::get()->getKartRaceTime(i);
    }

    if (m_remained == 0)
    {
        Log::error("KartElimination", "onRaceFinished aborted: Number of remained karts was 0.");
        return "";
    }

    if (m_remained < 0)
    {
        m_remained = order.size();
        for (const auto& p: order)
            m_participants.push_back(p.first);
    }

    for (int i = 0; i < m_remained; i++)
        if (order.count(m_participants[i]) == 0)
            order[m_participants[i]] = KartElimination::INF_TIME;

    std::stable_sort(
        m_participants.begin(),
        m_participants.begin() + m_remained,
        [order](const std::string& a, const std::string& b) -> bool {
            auto it1 = order.find(a);
            auto it2 = order.find(b);
            return it1->second < it2->second;
        }
    );

    std::string msg = "";
    msg += m_participants[--m_remained];
    bool alone = true;
    while (m_remained - 1 >= 0 &&
            order[m_participants[m_remained - 1]] == KartElimination::INF_TIME)
    {
        msg += ", " + m_participants[--m_remained];
        alone = false;
    }

    msg += " " + (alone ? g_eliminated_alone : g_eliminated_plural);

    if (m_remained <= 1) {
        m_enabled = false;
        msg += "\n";
        msg += StringUtils::insertValues(g_finished_winner, m_participants[0].c_str());
    }

    return msg;
}   // onRaceFinished
//-----------------------------------------------------------------------------

std::string KartElimination::getNowOffMessage()
{
    return g_now_off;
}   // getNowOffMessage
//-----------------------------------------------------------------------------

std::string KartElimination::getAlreadyEnabledString()
{
    return g_already_on;
}   // getAlreadyEnabledString
//-----------------------------------------------------------------------------

std::string KartElimination::getAlreadyOffString()
{
    return g_already_off;
}   // getAlreadyOffString
//-----------------------------------------------------------------------------

std::string KartElimination::getOnlyRacingString()
{
    return g_only_racing;
}   // getOnlyRacingString
//-----------------------------------------------------------------------------