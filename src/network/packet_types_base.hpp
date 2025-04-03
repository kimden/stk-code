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
using widestr16 = irr::core::stringw; // but encodeString16

// temp
constexpr int IDONT_KNOW = 0;

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
    DEFINE_VECTOR_OBJ(PlayerListProfilePacket, all_profiles_size, all_profiles)
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
    DEFINE_VECTOR_OBJ(EncodedSinglePlayerPacket, players_size, all_players)
    DEFINE_FIELD(uint32_t, item_seed)
    DEFINE_FIELD_OPTIONAL(BattleInfoPacket, battle_info, check(0)) // RaceManager::get()->isBattleMode()
    DEFINE_VECTOR_OBJ(KartDataPacket, players_size, players_kart_data)
END_DEFINE_CLASS(LoadWorldPacket)

DEFINE_CLASS(PlacementPacket)
    DEFINE_FIELD(Vec3, xyz)
    DEFINE_FIELD(btQuaternion, rotation)
END_DEFINE_CLASS(PlacementPacket)

DEFINE_CLASS(ItemStatePacket)
    DEFINE_FIELD(uint8_t, type)
    DEFINE_FIELD(uint8_t, original_type)
    DEFINE_FIELD(uint32_t, ticks_till_return)
    DEFINE_FIELD(uint32_t, item_id)
    DEFINE_FIELD(uint32_t, deactive_ticks)
    DEFINE_FIELD(uint32_t, used_up_counter)
    DEFINE_FIELD(PlacementPacket, original_xyz_rotation)
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
    DEFINE_VECTOR_OBJ(ItemCompleteStatePacket, all_items_size, all_items)
END_DEFINE_CLASS(NimCompleteStatePacket)

DEFINE_CLASS(KartInfoPacket)
    DEFINE_FIELD(uint32_t, finished_laps)
    DEFINE_FIELD(uint32_t, ticks_at_last_lap)
    DEFINE_FIELD(uint32_t, lap_start_ticks)
    DEFINE_FIELD(float, estimated_finish)
    DEFINE_FIELD(float, overall_distance)
    DEFINE_FIELD(float, wrong_way_timer)
END_DEFINE_CLASS(KartInfoPacket)

DEFINE_CLASS(TrackSectorPacket)
    DEFINE_FIELD(uint32_t, current_graph_node)
    DEFINE_FIELD(uint32_t, estimated_valid_graph_node)
    DEFINE_FIELD(uint32_t, last_valid_graph_node)
    DEFINE_FIELD(Vec3, current_track_coords)
    DEFINE_FIELD(Vec3, estimated_valid_track_coords)
    DEFINE_FIELD(Vec3, latest_valid_track_coords)
    DEFINE_FIELD(bool, on_road)
    DEFINE_FIELD(uint32_t, last_triggered_checkline)
END_DEFINE_CLASS(TrackSectorPacket)

DEFINE_CLASS(CheckStructureSubPacket)
    DEFINE_FIELD(Vec3, previous_position)
    DEFINE_FIELD(bool, is_active)
END_DEFINE_CLASS(CheckStructureSubPacket)

DEFINE_CLASS(CheckStructurePacket)
    AUX_VAR(uint32_t, karts_count)
    DEFINE_VECTOR_OBJ(CheckStructureSubPacket, karts_count, player_check_state)
END_DEFINE_CLASS(CheckStructurePacket)

DEFINE_CLASS(CheckLineSubPacket)
    DEFINE_FIELD(bool, previous_sign)
END_DEFINE_CLASS(CheckLineSubPacket)

DEFINE_CLASS(CheckLinePacket)
    AUX_VAR(uint32_t, karts_count)
    DEFINE_FIELD_PTR(CheckStructurePacket, check_structure_packet)
    DEFINE_VECTOR_OBJ(CheckLineSubPacket, karts_count, subpackets)
END_DEFINE_CLASS(CheckLinePacket)

DEFINE_CLASS(LinearWorldCompleteStatePacket)
    AUX_VAR(uint32_t, karts_count)
    AUX_VAR(uint32_t, track_sectors_count)
    DEFINE_FIELD(uint32_t, fastest_lap_ticks)
    DEFINE_FIELD(float, distance_increase)
    DEFINE_VECTOR_OBJ(PlacementPacket, karts_count, kart_placements)
    DEFINE_VECTOR_OBJ(KartInfoPacket, karts_count, kart_infos)
    DEFINE_VECTOR_OBJ(TrackSectorPacket, track_sectors_count, track_sectors)
    DEFINE_FIELD(uint8_t, check_structure_count)
    DEFINE_VECTOR_OBJ_PTR(CheckStructurePacket, check_structure_count, check_structures)
END_DEFINE_CLASS(LinearWorldCompleteStatePacket)

// 0: peer->getClientCapabilities().find("soccer_fixes") != peer->getClientCapabilities().end()
DEFINE_CLASS(ScorerDataPacket)
    DEFINE_FIELD(uint8_t, id)
    DEFINE_FIELD(uint8_t, correct_goal)
    DEFINE_FIELD(float, time)
    DEFINE_FIELD(std::string, kart)
    DEFINE_FIELD(widestr, player)
    DEFINE_FIELD_OPTIONAL(std::string, country_code, check(0))
    DEFINE_FIELD_OPTIONAL(uint8_t, handicap_level, check(0))
END_DEFINE_CLASS(ScoreerDataPacket)

DEFINE_CLASS(SoccerWorldCompleteStatePacket)
    DEFINE_FIELD(uint32_t, red_scorers_count)
    DEFINE_VECTOR_OBJ(ScorerDataPacket, red_scorers_count, red_scorers)
    DEFINE_FIELD(uint32_t, blue_scorers_count)
    DEFINE_VECTOR_OBJ(ScorerDataPacket, blue_scorers_count, blue_scorers)
    DEFINE_FIELD(uint32_t, reser_ball_ticks)
    DEFINE_FIELD(uint32_t, ticks_back_to_own_goal)
END_DEFINE_CLASS(SoccerWorldCompleteStatePacket)

DEFINE_CLASS(FFAWorldCompleteStatePacket)
    AUX_VAR(uint32_t, karts_count)
    DEFINE_VECTOR(uint32_t, karts_count, scores)
END_DEFINE_CLASS(FFAWorldCompleteStatePacket)

DEFINE_CLASS(CTFWorldCompleteStatePacket)
    DEFINE_FIELD_PTR(FFAWorldCompleteStatePacket, ffa_packet)
    DEFINE_FIELD(uint32_t, red_score)
    DEFINE_FIELD(uint32_t, blue_score)
END_DEFINE_CLASS(CTFWorldCompleteStatePacket)

DEFINE_CLASS(WorldCompleteStatePacket)
    DEFINE_FIELD_OPTIONAL(LinearWorldCompleteStatePacket, linear_packet, check(0))
    DEFINE_FIELD_OPTIONAL(SoccerWorldCompleteStatePacket, soccer_packet, check(1))
    DEFINE_FIELD_OPTIONAL(FFAWorldCompleteStatePacket, ffa_packet, check(2))
    DEFINE_FIELD_OPTIONAL(CTFWorldCompleteStatePacket, ctf_packet, check(3))
END_DEFINE_CLASS(WorldCompleteStatePacket)

DEFINE_CLASS(InsideGameInfoPacket)
    DEFINE_FIELD(uint8_t, players_size)
    DEFINE_VECTOR_OBJ(EncodedSinglePlayerPacket, players_size, all_players)
    DEFINE_VECTOR_OBJ(KartDataPacket, players_size, players_kart_data)
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


DEFINE_CLASS(ChatPacket)
    SYNCHRONOUS(true)
    DEFINE_FIXED_FIELD(uint8_t, type, LE_CHAT)
    DEFINE_FIELD(widestr16, message) // use encodeString16 ! max len is 360
    DEFINE_FIELD_OPTIONAL(KartTeam, kart_team) // KartTeam is uint8_t
    // send with PRM_RELIABLE
END_DEFINE_CLASS(ChatPacket)

DEFINE_CLASS(ChangeTeamPacket)
    DEFINE_FIXED_FIELD(uint8_t, type, LE_CHANGE_TEAM)
    DEFINE_FIELD(uint8_t, local_id)
    // send with PRM_RELIABLE
END_DEFINE_CLASS(ChangeTeamPacket)

DEFINE_CLASS(KickHostPacket)
    DEFINE_FIXED_FIELD(uint8_t, type, LE_KICK_HOST)
    DEFINE_FIELD(uint32_t, host_id)
    // send with PRM_RELIABLE
END_DEFINE_CLASS(KickHostPacket)

DEFINE_CLASS(ReportRequestPacket)
    DEFINE_FIXED_FIELD(uint8_t, type, LE_REPORT_PLAYER)
    DEFINE_FIELD(uint32_t, host_id)
    DEFINE_FIELD(widestr16, info)
    // send with PRM_RELIABLE
END_DEFINE_CLASS(ReportRequestPacket)

DEFINE_CLASS(ReportSuccessPacket)
    SYNCHRONOUS(true)
    DEFINE_FIXED_FIELD(uint8_t, type, LE_REPORT_PLAYER)
    DEFINE_FIELD(bool, success)
    DEFINE_FIELD(widestr, reported_name)
    // send with PRM_RELIABLE
END_DEFINE_CLASS(ReportSuccessPacket)

DEFINE_CLASS(ChangeHandicapPacket)
    DEFINE_FIXED_FIELD(uint8_t, type, LE_CHANGE_HANDICAP)
    DEFINE_FIELD(uint8_t, local_id)
    DEFINE_FIELD(uint8_t, handicap)
    // send with PRM_RELIABLE
END_DEFINE_CLASS(ChangeHandicapPacket)

DEFINE_CLASS(BackLobbyPacket)
    SYNCHRONOUS(true)
    DEFINE_FIXED_FIELD(uint8_t, type, LE_BACK_LOBBY)
    DEFINE_FIELD(uint8_t, reason)
    // send with PRM_RELIABLE
END_DEFINE_CLASS(BackLobbyPacket)

DEFINE_CLASS(ServerInfoPacket)
    SYNCHRONOUS(true)
    DEFINE_FIXED_FIELD(uint8_t, type, LE_SERVER_INFO)
    DEFINE_FIELD(uint8_t, placeholder)
    //...kimden: fill this
    // send with PRM_RELIABLE
END_DEFINE_CLASS(ServerInfoPacket)

DEFINE_CLASS(AssetsPacket)
    DEFINE_FIELD(uint16_t, karts_number)
    DEFINE_VECTOR(std::string, karts_number, karts)
    DEFINE_FIELD(uint16_t, maps_number)
    DEFINE_VECTOR(std::string, maps_number, maps)
END_DEFINE_CLASS(AssetsPacket)

DEFINE_CLASS(AssetsPacket2)
    DEFINE_FIELD(uint16_t, karts_number)
    DEFINE_FIELD(uint16_t, maps_number)
    DEFINE_VECTOR(std::string, karts_number, karts)
    DEFINE_VECTOR(std::string, maps_number, maps)
END_DEFINE_CLASS(AssetsPacket2)

DEFINE_CLASS(NewAssetsPacket)
    DEFINE_FIXED_FIELD(uint8_t, type, LE_ASSETS_UPDATE)
    DEFINE_FIELD(AssetsPacket, assets)
END_DEFINE_CLASS(NewAssetsPacket)

DEFINE_CLASS(PlayerKartsPacket)
    DEFINE_FIELD(uint8_t, players_count)
    DEFINE_VECTOR_OBJ(std::string, players_count, karts)

    // I don't care about compilation for now but don't want extra macroses yet either.
    DEFINE_VECTOR_OBJ/*_OPTIONAL*/(KartDataPacket, players_count, kart_data/*, IDONT_KNOW*/) // if has "real_addon_karts" in cap AND anything is sent
END_DEFINE_CLASS(PlayerKartsPacket)

DEFINE_CLASS(KartSelectionRequestPacket)
    DEFINE_FIXED_FIELD(uint8_t, type, LE_KART_SELECTION)
    DEFINE_FIELD(PlayerKartsPacket, karts)
END_DEFINE_CLASS(KartSelectionRequestPacket)

DEFINE_CLASS(LiveJoinRequestPacket)
    DEFINE_FIXED_FIELD(uint8_t, type, LE_LIVE_JOIN)
    DEFINE_FIELD(bool, is_spectator)
    DEFINE_FIELD_OPTIONAL(PlayerKartsPacket, player_karts) // is it optional?
END_DEFINE_CLASS(LiveJoinRequestPacket)

DEFINE_CLASS(FinishedLoadingLiveJoinPacket)
    DEFINE_FIXED_FIELD(uint8_t, type, LE_CLIENT_LOADED_WORLD)
END_DEFINE_CLASS(FinishedLoadingLiveJoinPacket)

DEFINE_CLASS(ConnectionRequestedPacket)
    DEFINE_FIELD(uint32_t, version)
    DEFINE_FIELD(std::string, user_version)
    DEFINE_FIELD(uint16_t, capabilities_count)
    DEFINE_VECTOR(std::string, capabilities_count, capabilities)
    DEFINE_FIELD(AssetsPacket, assets)
    DEFINE_FIELD(uint8_t, players_count)
    DEFINE_FIELD(uint32_t, online_id)
    DEFINE_FIELD(uint32_t, encrypted_size)
    // to be continued
END_DEFINE_CLASS(ConnectionRequestedPacket)

DEFINE_CLASS(KartInfoRequestPacket)
    // check if synch
    DEFINE_FIXED_FIELD(uint8_t, type, LE_KART_INFO)
    DEFINE_FIELD(uint8_t, kart_id)
END_DEFINE_CLASS(KartInfoRequestPacket)

DEFINE_CLASS(KartInfoPacket)
    SYNCHRONOUS(true)
    DEFINE_FIXED_FIELD(uint8_t, type, LE_KART_INFO)
    DEFINE_FIELD(uint32_t, live_join_util_ticks)
    DEFINE_FIELD(uint8_t, kart_id)
    DEFINE_FIELD(widestr, player_name)
    DEFINE_FIELD(uint32_t, host_id)
    DEFINE_FIELD(float, default_kart_color)
    DEFINE_FIELD(uint32_t, online_id)
    DEFINE_FIELD(uint8_t, handicap)
    DEFINE_FIELD(uint8_t, local_player_id)
    DEFINE_FIELD(std::string, kart_name)
    DEFINE_FIELD(std::string, country_code)
    // The field below is present if "real_addon_karts" is in capabilities
    DEFINE_FIELD_OBJ(KartDataPacket, kart_data)
    // send with PRM_RELIABLE
END_DEFINE_CLASS(KartInfoPacket)

DEFINE_CLASS(ConfigServerPacket)
    DEFINE_FIXED_FIELD(uint8_t, type, LE_CONFIG_SERVER)
    DEFINE_FIELD(uint8_t, difficulty)
    DEFINE_FIELD(uint8_t, game_mode)
    DEFINE_FIELD(bool, soccer_goal_target)
END_DEFINE_CLASS(ConfigServerPacket)

DEFINE_CLASS(ConnectionRefusedPacket)
    SYNCHRONOUS(true)
    DEFINE_FIXED_FIELD(uint8_t, type, LE_CONNECTION_REFUSED)
    DEFINE_FIELD(uint8_t, reason)
    DEFINE_FIELD_OPTIONAL(std::string, message);
    // send with PRM_RELIABLE, warning! can be sent unencrypted!
END_DEFINE_CLASS(ConnectionRefusedPacket)

DEFINE_CLASS(StartGamePacket)
    SYNCHRONOUS(true)
    DEFINE_FIXED_FIELD(uint8_t, type, LE_START_RACE)
    DEFINE_FIELD(uint64_t, start_time)
    DEFINE_FIELD(uint8_t, check_count)
    DEFINE_FIELD(ItemCompleteStatePacket, item_complete_state) /* this had operator += instead */
    // send with PRM_RELIABLE
END_DEFINE_CLASS(StartGamePacket)

DEFINE_CLASS(VotePacket) /* vote of a player sent to others */
    SYNCHRONOUS(true)
    DEFINE_FIXED_FIELD(uint8_t, type, LE_VOTE)
    DEFINE_FIELD(uint32_t, host_id)
    DEFINE_FIELD(PeerVotePacket, vote)
END_DEFINE_CLASS(VotePacket)

DEFINE_CLASS(VoteRequestPacket)
    DEFINE_FIXED_FIELD(uint8_t, type, LE_VOTE)
    DEFINE_FIELD(PeerVotePacket, vote)
END_DEFINE_CLASS(VoteRequestPacket)

DEFINE_CLASS(ServerOwnershipPacket)
    SYNCHRONOUS(true)
    DEFINE_FIXED_FIELD(uint8_t, type, LE_SERVER_OWNERSHIP)
END_DEFINE_CLASS(ServerOwnershipPacket)

DEFINE_CLASS(ConnectionAcceptedPacket)
    SYNCHRONOUS(true)
    DEFINE_FIXED_FIELD(uint8_t, type, LE_CONNECTION_ACCEPTED)
    DEFINE_FIELD(uint32_t, host_id)
    DEFINE_FIELD(uint32_t, server_version)
    DEFINE_FIELD(uint16_t, capabilities_size)
    DEFINE_VECTOR(std::string, capabilities_size, capabilities)
    DEFINE_FIELD(float, auto_start_timer)
    DEFINE_FIELD(uint32_t, state_frequency)
    DEFINE_FIELD(bool, chat_allowed)
    DEFINE_FIELD(bool, reports_allowed)
END_DEFINE_CLASS(ConnectionAcceptedPacket)

DEFINE_CLASS(PlayerDisconnectedPacket)
    SYNCHRONOUS(true)
    DEFINE_FIXED_FIELD(uint8_t, type, LE_PLAYER_DISCONNECTED)
    DEFINE_FIELD(uint8_t, players_size)
    DEFINE_FIELD(uint32_t, host_id)
    DEFINE_VECTOR(std::string, players_size, names)
END_DEFINE_CLASS(PlayerDisconnectedPacket)

DEFINE_CLASS(PointChangesPacket)
    DEFINE_FIELD(uint8_t, player_count)
    DEFINE_VECTOR(float, player_count, changes)
END_DEFINE_CLASS(PointChangesPacket)

DEFINE_CLASS(StartSelectionPacket)
    SYNCHRONOUS(true)
    DEFINE_FIXED_FIELD(uint8_t, type, LE_START_SELECTION)
    DEFINE_FIELD(float, voting_timeout)
    DEFINE_FIELD(bool, no_kart_selection)
    DEFINE_FIELD(bool, fixed_length)
    DEFINE_FIELD(bool, track_voting)
    DEFINE_FIELD(AssetsPacket2, assets)
    // send with PRM_RELIABLE
END_DEFINE_CLASS(StartSelectionPacket)

DEFINE_CLASS(BadTeamPacket)
    SYNCHRONOUS(true)
    DEFINE_FIXED_FIELD(uint8_t, type, LE_BAD_TEAM)
    // send with PRM_RELIABLE
END_DEFINE_CLASS(BadTeamPacket)

DEFINE_CLASS(RaceFinishedPacket)
    SYNCHRONOUS(true)
    DEFINE_FIXED_FIELD(uint8_t, type, LE_RACE_FINISHED)
    DEFINE_FIELD_OPTIONAL(uint32_t, fastest_lap, check(0)) /* if linear (incl. gp) */
    DEFINE_FIELD_OPTIONAL(widestr, fastest_kart_name, check(0)) /* if linear (incl. gp) */
    DEFINE_FIELD_OPTIONAL(GPScoresPacket, gp_scores, check(1)) /* if gp */
    DEFINE_FIELD(bool, point_changes_indication)
    DEFINE_FIELD(PointChangesPacket, point_changes)
    // send with PRM_RELIABLE
END_DEFINE_CLASS(RaceFinishedPacket)

DEFINE_CLASS(GPIndividualScorePacket)
    DEFINE_FIELD(uint32_t, last_score)
    DEFINE_FIELD(uint32_t, cur_score)
    DEFINE_FIELD(float, overall_time)
END_DEFINE_CLASS(GPIndividualScorePacket)

DEFINE_CLASS(GPScoresPacket)
    DEFINE_FIELD(uint8_t, total_gp_tracks)
    DEFINE_FIELD(uint8_t, all_tracks_size)
    DEFINE_VECTOR(std::string, all_tracks_size, all_tracks)
    DEFINE_FIELD(uint8_t, num_players)
    DEFINE_VECTOR(GPIndividualScorePacket, num_players, scores)
END_DEFINE_CLASS(GPScoresPacket)

// end