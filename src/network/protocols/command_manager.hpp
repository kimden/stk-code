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

#ifndef COMMAND_MANAGER_HPP
#define COMMAND_MANAGER_HPP

#include "irrString.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <deque>
#include <vector>

#ifdef ENABLE_SQLITE3
#include <sqlite3.h>
#endif

class ServerLobby;
class Event;
class STKPeer;

class CommandManager
{
    struct Context
    {
        Event* m_event;
        std::shared_ptr<STKPeer> m_peer;
        std::vector<std::string> m_argv;
        Context(Event* event, std::shared_ptr<STKPeer> peer, std::vector<std::string>& argv):
            m_event(event), m_peer(peer), m_argv(argv) {}
    };

    struct Command
    {
        std::string m_name;

        int m_permissions;

        void (CommandManager::*m_action)(Context& context);

        Command(std::string name,
            void (CommandManager::*f)(Context& context), int permissions):
                m_name(name), m_permissions(permissions), m_action(f) {}
    };

private:

    ServerLobby* m_lobby;

    std::vector<Command> m_commands;

    void initCommands();

    void process_commands(Context& context);
    void process_replay(Context& context);
    void process_start(Context& context);
    void process_config(Context& context);

public:

    CommandManager(ServerLobby* lobby = nullptr): m_lobby(lobby) { initCommands(); }

    void handleCommand(Event* event, std::shared_ptr<STKPeer> peer);

    bool isInitialized() { return m_lobby != nullptr; }
};

#endif // COMMAND_MANAGER_HPP