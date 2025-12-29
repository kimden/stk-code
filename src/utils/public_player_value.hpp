//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2018 SuperTuxKart-Team
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

#ifndef PUBLIC_PLAYER_VALUE_HPP
#define PUBLIC_PLAYER_VALUE_HPP

#include <functional>
#include <string>
#include <optional>

/**
 * A class that allows to show various (currently file-based) stats for players.
 */
class PublicPlayerValue
{
    using Handler = std::function<std::optional<std::string>(const std::string&)>;
    using Updater = std::function<void()>;
private:
    Handler m_method;
    Updater m_updater;

public:
    PublicPlayerValue(Handler method, Updater updater)
        : m_method(method), m_updater(updater)
    {}

    std::optional<std::string> get(const std::string& player_name) const
                                              { return m_method(player_name); }

    void tryUpdate() const { m_updater(); }
};

#endif // PUBLIC_PLAYER_VALUE_HPP