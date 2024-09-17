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

#ifndef HEADER_KART_ELIMINATION_HPP
#define HEADER_KART_ELIMINATION_HPP

#include "utils/types.hpp"
#include "utils/string_utils.hpp"
#include <limits>
#include <vector>
#include <set>
#include <map>
#include <string>

// A class that contains Gnu Elimination data

class KartElimination {
private:
    bool m_enabled;
    int m_remained;
    std::string m_kart;
    std::vector<std::string> m_participants;

public:
	static constexpr double INF_TIME = 1e9;
    KartElimination();
    bool isEliminated(std::string username) const;
    std::string getKart() const                              {  return m_kart; }
    std::set<std::string> getRemainingParticipants() const;
    bool isEnabled() const                                {  return m_enabled; }
    void enable(std::string kart); // done
    void disable(); // done
    std::string getStandings() const; // done
    std::string getStartingMessage() const; // done
    std::string getWarningMessage(bool isEliminated) const; // done
    std::string update(std::map<std::string, double>& order);
};

#endif
