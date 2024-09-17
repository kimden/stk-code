//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2021 kimden
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

#ifndef COMMAND_PERMISSIONS_HPP
#define COMMAND_PERMISSIONS_HPP

enum CommandPermissions : unsigned int
{
    PE_NONE = 0,
    PE_SPECTATOR = 1,
    PE_USUAL = 2,
    PE_CROWNED = 4,
    PE_SINGLE = 8,
    PE_HAMMER = 16,
    PE_CONSOLE = 32,
    PE_VOTED_SPECTATOR = 1024,
    PE_VOTED_NORMAL = 2048,
    PE_VOTED = PE_VOTED_SPECTATOR | PE_VOTED_NORMAL,
    UP_CONSOLE = PE_CONSOLE,
    UP_HAMMER = UP_CONSOLE | PE_HAMMER,
    UP_SINGLE = UP_HAMMER | PE_SINGLE,
    UP_CROWNED = UP_SINGLE | PE_CROWNED,
    UP_NORMAL = UP_CROWNED | PE_USUAL,
    UP_EVERYONE = UP_NORMAL | PE_SPECTATOR
};

#endif // COMMAND_PERMISSIONS_HPP