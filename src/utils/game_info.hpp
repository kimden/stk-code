//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2024 kimden
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

#ifndef GAME_INFO_HPP
#define GAME_INFO_HPP

#include <iostream>
#include <map>
#include <string>
#include <vector>

/** A simple structure to store the info about the game.
 *  It includes game settings, the results of individual players, as well as
 *   important team game events like flag captures or goals.
 */
struct GameInfo
{
    struct PlayerInfo
    {
        bool m_reserved;
        int m_game_event;
        std::string m_username = "";
        double m_result = 0.0f;
        std::string m_kart = "";
        float m_kart_color = 0.0f;
        std::string m_kart_class = "";
        int m_not_full = 0;
        int m_online_id = 0;
        int m_autofinish = 0;
        int m_team = -1;
        double m_when_joined = 0;
        double m_when_left = 0;
        double m_fastest_lap = -1.0;
        double m_sog_time = -1.0;
        int m_start_position = -1;
        double m_game_duration = -1;
        int m_handicap = 0;
        std::string m_country_code = "";

        std::string m_other_info = "";

        PlayerInfo(bool reserved = false, bool game_event = false):
            m_reserved(reserved),
            m_game_event(game_event ? 1 : 0) {}

        bool isReserved() const                          { return m_reserved; }
    };

    static const std::string m_default_powerup_string;
    static const std::string m_default_kart_char_string;
    std::string m_powerup_string;
    std::string m_kart_char_string;
    std::string m_venue;
    std::string m_reverse;
    std::string m_mode;
    std::string m_server_version;
    int m_value_limit;
    double m_time_limit;
    int m_flag_return_timeout;
    int m_flag_deactivated_time;
    int m_difficulty;
    std::string m_timestamp;

    // First RaceManager->getNumPlayers() elements in m_player_info
    // correspond to RaceManager->m_player_karts with the same index
    // for convenience. The rest of elements correspond to profiles
    // who left the game.
    std::vector<PlayerInfo> m_player_info;
    std::map<std::string, int> m_saved_ffa_points;

    void setPowerupString(const std::string&& str);
    void setKartCharString(const std::string&& str);
    std::string getPowerupString() const           { return m_powerup_string; }
    std::string getKartCharString() const        { return m_kart_char_string; }
};

#endif