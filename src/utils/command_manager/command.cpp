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

#include "utils/command_manager/command.hpp"




Command::Command(std::string name,
                 std::function<void(Context&)> f,
                 int permissions,
                 int mode_scope,
                 int state_scope)
    : m_name(std::move(name))
    , m_action(std::move(f))
    , m_permissions(permissions)
    , m_mode_scope(mode_scope)
    , m_state_scope(state_scope)
    , m_omit_name(false)
{
    // Handling players who are allowed to run for anyone in any case
    m_permissions |= UU_OTHERS_COMMANDS;
} // Command::Command(5)
// ========================================================================

void Command::changePermissions(int permissions,
        int mode_scope, int state_scope)
{
    // Handling players who are allowed to run for anyone in any case
    m_permissions = permissions | UU_OTHERS_COMMANDS;
    m_mode_scope = mode_scope;
    m_state_scope = state_scope;
} // changePermissions
// ========================================================================