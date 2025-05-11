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
    PE_NONE                = 0,
    UU_SPECTATOR           = (1 << 0),
    UU_USUAL               = (1 << 1),
    UU_CROWNED             = (1 << 2),
    UU_SINGLE              = (1 << 3),
    UU_HAMMER              = (1 << 4),
    UU_MANIPULATOR         = (1 << 5),
    UU_CONSOLE             = (1 << 6),
    PE_VOTED_SPECTATOR     = (1 << 10),
    PE_VOTED_NORMAL        = (1 << 11),

    UU_OWN_COMMANDS        = (1 << 15),
    UU_OTHERS_COMMANDS     = (1 << 16),

    // If the command allows anyone to invoke it for others,
    // even people with only own commands permission can do it
    PE_ALLOW_ANYONE        = UU_OWN_COMMANDS,

    PE_SPECTATOR           = UU_SPECTATOR   | UU_OWN_COMMANDS,
    PE_USUAL               = UU_USUAL       | UU_OWN_COMMANDS,
    PE_CROWNED             = UU_CROWNED     | UU_OWN_COMMANDS,
    PE_SINGLE              = UU_SINGLE      | UU_OWN_COMMANDS,
    PE_HAMMER              = UU_HAMMER      | UU_OWN_COMMANDS,
    PE_MANIPULATOR         = UU_MANIPULATOR | UU_OWN_COMMANDS | UU_OTHERS_COMMANDS,
    PE_CONSOLE             = UU_CONSOLE     | UU_OWN_COMMANDS | UU_OTHERS_COMMANDS,

    PE_VOTED               = PE_VOTED_SPECTATOR | PE_VOTED_NORMAL,
    UP_CONSOLE             = PE_CONSOLE,
    UP_HAMMER              = UP_CONSOLE | PE_HAMMER,
    UP_SINGLE              = UP_HAMMER  | PE_SINGLE,
    UP_CROWNED             = UP_SINGLE  | PE_CROWNED,
    UP_NORMAL              = UP_CROWNED | PE_USUAL,
    UP_EVERYONE            = UP_NORMAL  | PE_SPECTATOR,

    MASK_MANIPULATION      = UU_OWN_COMMANDS | UU_OTHERS_COMMANDS,
};

#endif // COMMAND_PERMISSIONS_HPP