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
    PE_USUAL = 1,
    PE_VOTED = 2,
    PE_CROWNED = 4,
    PE_SINGLE = 8,
    PE_HAMMER = 16,
    UP_HAMMER = PE_HAMMER,
    UP_SINGLE = UP_HAMMER | PE_SINGLE,
    UP_CROWNED = UP_SINGLE | PE_CROWNED,
    UP_EVERYONE = UP_CROWNED | PE_USUAL
};

#endif // COMMAND_PERMISSIONS_HPP