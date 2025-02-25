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

#include "utils/tournament_role.hpp"

KartTeam Conversions::roleToTeam(TournamentRole role)
{
    switch (role)
    {
        case TR_RED_PLAYER:
            return KART_TEAM_RED;
        case TR_BLUE_PLAYER:
            return KART_TEAM_BLUE;
        case TR_JUDGE:
            return KART_TEAM_NONE;
        case TR_SPECTATOR:
            return KART_TEAM_NONE;
    }
} // roleToTeam

std::string Conversions::roleToString(TournamentRole role)
{
    switch (role)
    {
        case TR_RED_PLAYER:
            return "red player";
        case TR_BLUE_PLAYER:
            return "blue player";
        case TR_JUDGE:
            return "referee";
        case TR_SPECTATOR:
            return "spectator";
    }
} // roleToString

char Conversions::roleToChar(TournamentRole role)
{
    switch (role)
    {
        case TR_RED_PLAYER:
            return 'r';
        case TR_BLUE_PLAYER:
            return 'b';
        case TR_JUDGE:
            return 'j';
        case TR_SPECTATOR:
            return 's';
    }
} // roleToChar

TournamentRole Conversions::charToRole(char c)
{
    switch (c)
    {
        case 'r':
            return TR_RED_PLAYER;
        case 'b':
            return TR_BLUE_PLAYER;
        case 'j':
            return TR_JUDGE;
        case 's':
            return TR_SPECTATOR;
        default:
            return TR_SPECTATOR;
    }
} // charToRole

std::string Conversions::roleCharToString(char c)
{
    return roleToString(charToRole(c));
} // roleCharToString
