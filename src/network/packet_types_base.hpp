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
 * If you need to use a type that includes a comma, such as std::map<A, B>, 
 * you can use either a #define or using = to pass it anyway.
 */

#include "irrString.h"
#include "utils/types.hpp"

#include <string>
#include <vector>

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

DEFINE_CLASS(EncodedSinglePlayerPacket)
    DEFINE_FIELD(widestr,     name)
    DEFINE_FIELD(uint32_t,    host_id)
    DEFINE_FIELD(float,       kart_color)
    DEFINE_FIELD(uint32_t,    online_id)
    DEFINE_FIELD(uint8_t,     handicap)
    DEFINE_FIELD(uint8_t,     local_player_id)
    DEFINE_FIELD(uint8_t,     kart_team)
    DEFINE_FIELD(std::string, country_code)
    DEFINE_FIELD(std::string, kart_name)
END_DEFINE_CLASS(EncodedSinglePlayerPacket)

DEFINE_CLASS(EncodedPlayersPacket)
END_DEFINE_CLASS(EncodedPlayersPacket)


DEFINE_CLASS(PeerVotePacket)
    DEFINE_FIELD(widestr, player_name)
    DEFINE_FIELD(std::string, track_name)
    DEFINE_FIELD(uint8_t, num_laps)
    DEFINE_FIELD(bool, is_reverse)
END_DEFINE_CLASS(PeerVotePacket)


DEFINE_CLASS(DefaultVotePacket)
    DEFINE_FIELD(uint32_t, winner_peer_id)
    DEFINE_FIELD(PeerVotePacket, default_vote)
END_DEFINE_CLASS(DefaultVotePacket)

DEFINE_CLASS(BattleInfoPacket)
    DEFINE_FIELD(uint32_t, battle_hit_capture_limit)
    DEFINE_FIELD(float, battle_time_limit)
    DEFINE_FIELD(uint16_t, flag_return_time)
    DEFINE_FIELD(uint16_t, flag_deactivated_time)
END_DEFINE_CLASS(BattleInfoPacket)

DEFINE_CLASS(KartParametersPacket)
    DEFINE_FIELD(float, width)
    DEFINE_FIELD(float, height)
    DEFINE_FIELD(float, length)
    DEFINE_FIELD(Vec3, gravity_shift)
END_DEFINE_CLASS(KartParametersPacket)

DEFINE_CLASS(KartDataPacket)
    DEFINE_FIELD(std::string, kart_type)
    DEFINE_FIELD_OPTIONAL(KartParametersPacket, parameters, !kart_type.empty())
END_DEFINE_CLASS(KartDataPacket)

DEFINE_CLASS(LoadWorldPacket)
    SYNCHRONOUS(true)
    DEFINE_FIXED_FIELD(uint8_t, type, LE_LOAD_WORLD)
    DEFINE_FIELD(DefaultVotePacket, default_vote)
    DEFINE_FIELD(bool, live_join)
    DEFINE_FIELD(uint8_t,       players_size)
    DEFINE_VECTOR(EncodedSinglePlayerPacket, players_size, all_players)
    DEFINE_FIELD(uint32_t, item_seed)
    DEFINE_FIELD_OPTIONAL(BattleInfoPacket, battle_info, check(0)) // RaceManager::get()->isBattleMode()
    DEFINE_VECTOR(KartDataPacket, players_size, players_kart_data)
END_DEFINE_CLASS(LoadWorldPacket)

DEFINE_CLASS(ItemStatePacket)
    DEFINE_FIELD(uint8_t, type)
    DEFINE_FIELD(uint8_t, original_type)
    DEFINE_FIELD(uint32_t, ticks_till_return)
    DEFINE_FIELD(uint32_t, item_id)
    DEFINE_FIELD(uint32_t, deactive_ticks)
    DEFINE_FIELD(uint32_t, used_up_counter)
    DEFINE_FIELD(Vec3, xyz)
    DEFINE_FIELD(btQuaternion, original_rotation)
    DEFINE_FIELD(uint8_t, previous_owner)
END_DEFINE_CLASS(ItemStatePacket)

DEFINE_CLASS(ItemCompleteStatePacket)
    DEFINE_FIELD(bool, has_item)
    DEFINE_FIELD_OPTIONAL(ItemStatePacket, item_state, has_item)
END_DEFINE_CLASS(ItemCompleteStatePacket)

DEFINE_CLASS(NimCompleteStatePacket)
    DEFINE_FIELD(uint32_t, ticks_since_start)
    DEFINE_FIELD(uint32_t, switch_ticks)
    DEFINE_FIELD(uint32_t, all_items_size)
    DEFINE_VECTOR(ItemCompleteStatePacket, all_items_size, all_items)
END_DEFINE_CLASS(NimCompleteStatePacket)

DEFINE_CLASS(WorldCompleteStatePacket)

END_DEFINE_CLASS(WorldCompleteStatePacket)

DEFINE_CLASS(InsideGameInfoPacket)
    DEFINE_FIELD(uint8_t, players_size)
    DEFINE_VECTOR(EncodedSinglePlayerPacket, players_size, all_players)
    DEFINE_VECTOR(KartDataPacket, players_size, players_kart_data)
END_DEFINE_CLASS(InsideGameInfoPacket)

DEFINE_CLASS(LiveJoinPacket)
    SYNCHRONOUS(true)
    DEFINE_FIXED_FIELD(uint8_t, type, LE_LIVE_JOIN_ACK)
    DEFINE_FIELD(uint64_t, client_starting_time)
    DEFINE_FIELD(uint8_t, check_count);
    DEFINE_FIELD(uint64_t, live_join_start_time)
    DEFINE_FIELD(uint32_t, last_live_join_util_ticks)
    DEFINE_FIELD(NimCompleteStatePacket, nim_complete_state)
    DEFINE_FIELD(WorldCompleteStatePacket, world_complete_state)
    DEFINE_FIELD_OPTIONAL(InsideGameInfoPacket, inside_info, check(0)) // RaceManager::get()->supportsLiveJoining()
END_DEFINE_CLASS(LiveJoinPacket)


// end