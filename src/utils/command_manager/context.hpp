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

#ifndef CONTEXT_HPP
#define CONTEXT_HPP

#include <memory>
#include <vector>
#include <string>
#include "network/event.hpp"

struct Command;
class ServerLobby;

struct Context
{
    ServerLobby* m_lobby;

    Event* m_event;

    std::weak_ptr<STKPeer> m_peer;

    std::weak_ptr<STKPeer> m_target_peer;

    std::vector<std::string> m_argv;

    std::string m_cmd;

    std::weak_ptr<Command> m_command;

    int m_user_permissions;

    int m_acting_user_permissions;

    bool m_voting;

    Context(ServerLobby* lobby, Event* event, std::shared_ptr<STKPeer> peer):
            m_lobby(lobby),
            m_event(event), m_peer(peer), m_target_peer(peer), m_argv(),
            m_cmd(""), m_user_permissions(0), m_acting_user_permissions(0), m_voting(false) {}

    Context(ServerLobby* lobby, Event* event, std::shared_ptr<STKPeer> peer,
        std::vector<std::string>& argv, std::string& cmd,
        int user_permissions, int acting_user_permissions, bool voting):
            m_lobby(lobby),
            m_event(event), m_peer(peer), m_target_peer(peer), m_argv(argv),
            m_cmd(cmd), m_user_permissions(user_permissions),
            m_acting_user_permissions(acting_user_permissions), m_voting(voting) {}

    std::shared_ptr<STKPeer> peer();
    std::shared_ptr<STKPeer> peerMaybeNull();
    std::shared_ptr<STKPeer> actingPeer();
    std::shared_ptr<STKPeer> actingPeerMaybeNull();
    std::shared_ptr<Command> command();

    void say(const std::string& s);
};

#endif // CONTEXT_HPP