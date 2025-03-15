//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2025 kimden
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

/**
 * IMPORTANT!
 * This file has NO ifndef/define guards.
 * This is done on purpose to provide an easy interface for making new
 * types of packets, without the need to mention all fields more than once.
 * Always #include "network/packet_types.hpp" and NOT this file.
 * packet_types.hpp is the ONLY file allowed to include this file.
 */

/**
 * IMPORTANT!
 * The structures in this file have DIRECT relation to STK network protocol.
 * You should NOT modify them unless you know what you are doing, as that
 * could cause the server to not talk the same language as clients!
 */

/**
 * For now, for each non-vector type you have in the packets, you have
 * to define encode<T> and decode<T> for BareNetworkString.
 * 
 * If you need to use a type tbhat includes a comma, such as std::map<A, B>, 
 * you can use either a #define or using = to pass it anyway.
 */

#include "irrString.h"
#include "utils/types.hpp"

#include <memory>
#include <map>
#include <set>
#include <string>

using widestr = irr::core::stringw;

// Note that bools are encoded using int8_t

DEFINE_CLASS(PlayerListProfilePacket)
    DEFINE_FIELD(uint32_t,    host_id)
    DEFINE_FIELD(uint32_t,    online_id)
    DEFINE_FIELD(uint8_t,     local_player_id)
    DEFINE_FIELD(widestr,     profile_name)
    DEFINE_FIELD(uint8_t,     mask)
    DEFINE_FIELD(uint8_t,     handicap)
    DEFINE_FIELD(uint8_t,     kart_team)
    DEFINE_FIELD(std::string, country_code)
END_DEFINE_CLASS(PlayerListProfilePacket)

DEFINE_CLASS(PlayerListPacket)
    SYNCHRONOUS(true)
    DEFINE_FIXED_FIELD(uint8_t, type, LE_UPDATE_PLAYER_LIST)
    DEFINE_FIELD(bool,          game_started)
    DEFINE_FIELD(uint8_t,       all_profiles_size)
    DEFINE_VECTOR(PlayerListProfilePacket, all_profiles_size, all_profiles)
END_DEFINE_CLASS(PlayerListPacket)

// end