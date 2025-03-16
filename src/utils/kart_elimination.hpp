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

#ifndef HEADER_KART_ELIMINATION_HPP
#define HEADER_KART_ELIMINATION_HPP

#include "utils/lobby_context.hpp"
#include "utils/string_utils.hpp"
#include "utils/types.hpp"

#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

// A class that contains Gnu Elimination data

class KartElimination: public LobbyContextComponent
{
private:
    bool m_enabled;
    int m_remained;
    std::string m_kart;
    std::vector<std::string> m_participants;

public:
	static constexpr double INF_TIME = 1e9;

    KartElimination(LobbyContext* context): LobbyContextComponent(context) {}
    
    void setupContextUser() OVERRIDE;
    bool isEliminated(std::string username) const;
    std::string getKart() const                             {  return m_kart; }
    std::set<std::string> getRemainingParticipants() const;
    bool isEnabled() const                               {  return m_enabled; }
    void enable(std::string kart);
    void disable();
    std::string getStandings() const;
    std::string getStartingMessage() const;
    std::string getWarningMessage(bool isEliminated) const;
    std::string update(std::map<std::string, double>& order);
};

#endif
