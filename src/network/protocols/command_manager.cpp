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

#include "network/protocols/command_manager.hpp"

#include "addons/addon.hpp"
#include "config/user_config.hpp"
#include "items/network_item_manager.hpp"
#include "items/powerup_manager.hpp"
#include "items/attachment.hpp"
#include "karts/controller/player_controller.hpp"
#include "karts/kart_properties.hpp"
#include "karts/kart_properties_manager.hpp"
#include "karts/official_karts.hpp"
#include "modes/capture_the_flag.hpp"
#include "modes/soccer_world.hpp"
#include "modes/linear_world.hpp"
#include "network/crypto.hpp"
#include "network/event.hpp"
#include "network/game_setup.hpp"
#include "network/network.hpp"
#include "network/network_config.hpp"
#include "network/network_player_profile.hpp"
#include "network/peer_vote.hpp"
#include "network/protocol_manager.hpp"
#include "network/protocols/connect_to_peer.hpp"
#include "network/protocols/command_permissions.hpp"
#include "network/protocols/game_protocol.hpp"
#include "network/protocols/game_events_protocol.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/race_event_manager.hpp"
#include "network/server_config.hpp"
#include "network/socket_address.hpp"
#include "network/stk_host.hpp"
#include "network/stk_ipv6.hpp"
#include "network/stk_peer.hpp"
#include "online/online_profile.hpp"
#include "online/request_manager.hpp"
#include "online/xml_request.hpp"
#include "race/race_manager.hpp"
#include "tracks/check_manager.hpp"
#include "tracks/track.hpp"
#include "tracks/track_manager.hpp"
#include "utils/log.hpp"
#include "utils/random_generator.hpp"
#include "utils/string_utils.hpp"
#include "utils/time.hpp"
#include "utils/translation.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <iterator>

// ========================================================================

void CommandManager::initCommands()
{
    m_commands.emplace_back("commands", &CommandManager::process_commands, UP_EVERYONE);
    m_commands.emplace_back("replay", &CommandManager::process_replay, UP_SINGLE);
    m_commands.emplace_back("start", &CommandManager::process_start, UP_EVERYONE);
    m_commands.emplace_back("config", &CommandManager::process_config, UP_CROWNED);
} // initCommands
// ========================================================================

void CommandManager::handleCommand(Event* event, std::shared_ptr<STKPeer> peer)
{
    NetworkString& data = event->data();
    std::string language;
    data.decodeString(&language);
    std::string cmd;
    data.decodeString(&cmd);
    auto argv = StringUtils::split(cmd, ' ');
    if (argv.size() == 0)
        return;

    int permissions = m_lobby->getPermissions(peer);
    bool found_command = false;

    for (const auto& command: m_commands)
    {
        if (argv[0] == command.m_name)
        {
            found_command = true;
            if ((permissions & command.m_permissions) == 0)
            {
                std::string msg = "You don't have permissions to invoke this command";
                m_lobby->sendStringToPeer(msg, peer);
            }
            else
            {
                Context context(event, peer, argv);
                (this->*command.m_action)(context);
            }
        }
    }

    if (!found_command)
    {
        std::string msg = "Command " + argv[0] + " not found";
        m_lobby->sendStringToPeer(msg, peer);
    }

    // TODO: process commands with typos
    // TODO: add votedness

} // handleCommand
// ========================================================================

void CommandManager::process_commands(Context& context)
{
    std::string result = "";
    for (const Command& c: m_commands)
    {
        result += c.m_name + " ";
    }

    if (!result.empty())
        result.pop_back();

    m_lobby->sendStringToPeer(result, context.m_peer);
} // process_commands
// ========================================================================

void CommandManager::process_replay(Context& context)
{
    const auto& argv = context.m_argv;
    if (ServerConfig::m_record_replays)
    {
        bool current_state = m_lobby->hasConsentOnReplays();
        if (argv.size() >= 2 && argv[1] == "0")
            current_state = false;
        else if (argv.size() >= 2 && argv[1] == "1")
            current_state = true;
        else
            current_state ^= 1;

        m_lobby->setConsentOnReplays(current_state);
        std::string msg = "Recording ghost replays is now ";
        msg += (current_state ? "on" : "off");
        m_lobby->sendStringToAllPeers(msg);
    }
    else
    {
        std::string msg = "This server doesn't allow recording replays";
        m_lobby->sendStringToPeer(msg, context.m_peer);
    }
} // process_replay
// ========================================================================

void CommandManager::process_start(Context& context)
{
    m_lobby->startSelection(context.m_event);
} // process_start
// ========================================================================

void CommandManager::process_config(Context& context)
{
    const auto& argv = context.m_argv;
    int difficulty = m_lobby->getDifficulty();
    int mode = m_lobby->getGameMode();
    bool goal_target = m_lobby->isSoccerGoalTarget();
    bool gp = false;
    for (unsigned i = 1; i < argv.size(); i++)
    {
        if (argv[i] == "tt" || argv[i] == "time-trial"||
            argv[i] == "trial" || argv[i] == "m4")
            mode = 4;
        if (argv[i] == "normal" || argv[i] == "normal-race" ||
            argv[i] == "race" || argv[i] == "m3")
            mode = 3;
        if (argv[i] == "soccer" || argv[i] == "football" ||
            argv[i] == "m6")
            mode = 6;
        if (argv[i] == "ffa" || argv[i] == "free-for-all" ||
            argv[i] == "free" || argv[i] == "for" ||
            argv[i] == "all" || argv[i] == "m7")
            mode = 7;
        if (argv[i] == "ctf" || argv[i] == "capture-the-flag"
            || argv[i] == "capture" || argv[i] == "the" ||
            argv[i] == "flag" || argv[i] == "m8")
            mode = 8;
        // if (argv[i] == "gp" || argv[i] == "grand-prix")
        //     gp = true;
        if (argv[i] == "d0" || argv[i] == "novice" || argv[i] == "easy")
            difficulty = 0;
        if (argv[i] == "d1" || argv[i] == "intermediate" || argv[i] == "medium")
            difficulty = 1;
        if (argv[i] == "d2" || argv[i] == "expert" || argv[i] == "hard")
            difficulty = 2;
        if (argv[i] == "d3" || argv[i] == "supertux" || argv[i] == "super" || argv[i] == "best")
            difficulty = 3;
        if (argv[i] == "goal-limit" || argv[i] == "gl" || argv[i] == "goal" || argv[i] == "goals")
            goal_target = true;
        if (argv[i] == "time-limit" || argv[i] == "tl" || argv[i] == "time" || argv[i] == "minutes")
            goal_target = false;
    }
    // if (gp && (mode == 3 || mode == 4))
    //     mode -= 3;
    m_lobby->handleServerConfiguration(context.m_peer, difficulty, mode, goal_target);
} // process_config
// ========================================================================
