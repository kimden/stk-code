//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2024 SuperTuxKart-Team
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

#include "utils/game_info.hpp"

// TODO: don't use file names as constants
const std::string GameInfo::m_default_powerup_string = "powerup.xml";
const std::string GameInfo::m_default_kart_char_string = "kart_characteristics.xml";

//-----------------------------------------------------------------------------
void GameInfo::setPowerupString(const std::string&& str)
{
    if (str == m_default_powerup_string)
        m_powerup_string = "";
    else
        m_powerup_string = str;
}   // setPowerupString

//-----------------------------------------------------------------------------
void GameInfo::setKartCharString(const std::string&& str)
{
    if (str == m_default_kart_char_string)
        m_kart_char_string = "";
    else
        m_kart_char_string = str;
}   // setKartCharString

//-----------------------------------------------------------------------------
