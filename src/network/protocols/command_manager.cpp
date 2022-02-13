//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2021-2022 kimden
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
// #include "config/user_config.hpp"
#include "io/file_manager.hpp"
// #include "items/network_item_manager.hpp"
// #include "items/powerup_manager.hpp"
// #include "items/attachment.hpp"
// #include "karts/controller/player_controller.hpp"
// #include "karts/kart_properties.hpp"
// #include "karts/kart_properties_manager.hpp"
// #include "karts/official_karts.hpp"
// #include "modes/capture_the_flag.hpp"
#include "modes/soccer_world.hpp"
// #include "modes/linear_world.hpp"
#include "network/crypto.hpp"
#include "network/event.hpp"
#include "network/game_setup.hpp"
// #include "network/network.hpp"
// #include "network/network_config.hpp"
#include "network/network_player_profile.hpp"
// #include "network/peer_vote.hpp"
// #include "network/protocol_manager.hpp"
// #include "network/protocols/connect_to_peer.hpp"
#include "network/protocols/command_permissions.hpp"
// #include "network/protocols/command_voting.hpp"
// #include "network/protocols/game_protocol.hpp"
// #include "network/protocols/game_events_protocol.hpp"
#include "network/protocols/server_lobby.hpp"
// #include "network/race_event_manager.hpp"
#include "network/server_config.hpp"
// #include "network/socket_address.hpp"
#include "network/stk_host.hpp"
// #include "network/stk_ipv6.hpp"
#include "network/stk_peer.hpp"
// #include "online/online_profile.hpp"
// #include "online/request_manager.hpp"
// #include "online/xml_request.hpp"
// #include "race/race_manager.hpp"
// #include "tracks/check_manager.hpp"
// #include "tracks/track.hpp"
// #include "tracks/track_manager.hpp"
#include "utils/file_utils.hpp"
#include "utils/log.hpp"
#include "utils/random_generator.hpp"
#include "utils/string_utils.hpp"
// #include "utils/time.hpp"
// #include "utils/translation.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <iterator>

// ========================================================================


CommandManager::FileResource::FileResource(std::string file_name, uint64_t interval)
{
    m_file_name = file_name;
    m_interval = interval;
    m_contents = "";
    m_last_invoked = 0;
    read();
} // FileResource::FileResource
// ========================================================================

void CommandManager::FileResource::read()
{
    if (m_file_name.empty()) // in case it is not properly initialized
        return;
    // idk what to do with absolute or relative paths
    const std::string& path = /*ServerConfig::getConfigDirectory() + "/" + */m_file_name;
    std::ifstream message(FileUtils::getPortableReadingPath(path));
    std::string answer = "";
    if (message.is_open())
    {
        for (std::string line; std::getline(message, line); )
        {
            answer += line;
            answer.push_back('\n');
        }
        if (!answer.empty())
            answer.pop_back();
    }
    m_contents = answer;
    m_last_invoked = StkTime::getMonoTimeMs();
} // FileResource::read
// ========================================================================

std::string CommandManager::FileResource::get()
{
    uint64_t current_time = StkTime::getMonoTimeMs();
    if (m_interval == 0 || current_time < m_interval + m_last_invoked)
        return m_contents;
    read();
    return m_contents;
} // FileResource::get
// ========================================================================


void CommandManager::initCommandsInfo()
{
    const std::string file_name = file_manager->getAsset("commands.xml");
    const XMLNode *root = file_manager->createXMLTree(file_name);
    unsigned int num_nodes = root->getNumNodes();
    uint32_t version = 1;
    root->get("version", &version);
    if (version != 1)
        Log::warn("CommandManager", "command.xml has version %d which is not supported", version);
    for (unsigned int i = 0; i < num_nodes; i++)
    {
        const XMLNode *node = root->getNode(i);
        std::string node_name = node->getName();
        // here the commands go
        std::string name = "";
        std::string text = ""; // for text-command
        std::string file = ""; // for file-command
        uint64_t interval = 0; // for file-command
        std::string usage = "";
        std::string permissions = "";
        std::string description = "";
        // If enabled is not empty, command is added iff the server name is in enabled
        // Otherwise it is added iff the server name is not in disabled
        std::string enabled = "";
        std::string disabled = "";
        node->get("enabled", &enabled);
        node->get("disabled", &disabled);
        std::vector<std::string> enabled_split = StringUtils::split(enabled, ' ');
        std::vector<std::string> disabled_split = StringUtils::split(disabled, ' ');
        bool ok;
        if (!enabled.empty())
        {
            ok = false;
            for (const std::string& s: enabled_split)
                if (s == ServerConfig::m_server_uid)
                    ok = true;
        }
        else
        {
            ok = true;
            for (const std::string& s: disabled_split)
                if (s == ServerConfig::m_server_uid)
                    ok = false;
        }
        if (!ok)
            continue;

        node->get("name", &name);
        node->get("usage", &usage);
        node->get("permissions", &permissions);
        node->get("description", &description);

        if (node_name == "command")
        {
            m_config_descriptions[name] = CommandDescription(usage, permissions, description);
        }
        else if (node_name == "text-command")
        {
            node->get("text", &text);
            m_commands.emplace_back(name, &CommandManager::process_text, UP_EVERYONE, MS_DEFAULT);
            addTextResponse(name, text);
            m_config_descriptions[name] = CommandDescription(usage, permissions, description);
        }
        else if (node_name == "file-command")
        {
            node->get("file", &file);
            node->get("interval", &interval);
            m_commands.emplace_back(name, &CommandManager::process_file, UP_EVERYONE, MS_DEFAULT);
            addFileResource(name, file, interval);
            m_config_descriptions[name] = CommandDescription(usage, permissions, description);
        }
    }
    delete root;
} // initCommandsInfo
// ========================================================================

void CommandManager::initCommands()
{
    initCommandsInfo();
    using CM = CommandManager;
    auto kick_permissions = ((ServerConfig::m_kicks_allowed ? UP_CROWNED : UP_HAMMER) | PE_VOTED_NORMAL);
    auto& v = m_commands;

    v.emplace_back("commands", &CM::process_commands, UP_EVERYONE);
    v.emplace_back("replay", &CM::process_replay, UP_SINGLE, MS_DEFAULT, SS_LOBBY);
    v.emplace_back("start", &CM::process_start, UP_NORMAL | PE_VOTED_NORMAL, MS_DEFAULT, SS_LOBBY);
    if (ServerConfig::m_server_configurable)
        v.emplace_back("config", &CM::process_config, UP_CROWNED | PE_VOTED_NORMAL, MS_DEFAULT, SS_LOBBY);
    v.emplace_back("spectate", &CM::process_spectate, UP_EVERYONE);
    v.emplace_back("addons", &CM::process_addons, UP_EVERYONE);
    v.emplace_back("moreaddons", &CM::process_addons, UP_EVERYONE);
    v.emplace_back("getaddons", &CM::process_addons, UP_EVERYONE);
    v.emplace_back("checkaddon", &CM::process_checkaddon, UP_EVERYONE);
    v.emplace_back("listserveraddon", &CM::process_lsa, UP_EVERYONE);
    v.emplace_back("playerhasaddon", &CM::process_pha, UP_EVERYONE);
    v.emplace_back("kick", &CM::process_kick, kick_permissions);
    v.emplace_back("kickban", &CM::process_kick, UP_HAMMER | PE_VOTED_NORMAL);
    v.emplace_back("unban", &CM::process_unban, UP_HAMMER);
    v.emplace_back("ban", &CM::process_ban, UP_HAMMER);
    v.emplace_back("playeraddonscore", &CM::process_pas, UP_EVERYONE);
    v.emplace_back("serverhasaddon", &CM::process_sha, UP_EVERYONE);
    v.emplace_back("mute", &CM::process_mute, UP_EVERYONE);
    v.emplace_back("unmute", &CM::process_unmute, UP_EVERYONE);
    v.emplace_back("listmute", &CM::process_listmute, UP_EVERYONE);
    v.emplace_back("description", &CM::process_text, UP_EVERYONE);
    v.emplace_back("moreinfo", &CM::process_text, UP_EVERYONE);
    v.emplace_back("gnu", &CM::process_gnu, UP_HAMMER | PE_VOTED_NORMAL, MS_DEFAULT, SS_LOBBY);
    v.emplace_back("nognu", &CM::process_gnu, UP_HAMMER | PE_VOTED_NORMAL, MS_DEFAULT, SS_LOBBY);
    v.emplace_back("tell", &CM::process_tell, UP_EVERYONE);
    v.emplace_back("standings", &CM::process_standings, UP_EVERYONE);
    v.emplace_back("teamchat", &CM::process_teamchat, UP_EVERYONE);
    v.emplace_back("to", &CM::process_to, UP_EVERYONE);
    v.emplace_back("public", &CM::process_public, UP_EVERYONE);
    v.emplace_back("record", &CM::process_record, UP_EVERYONE);
    v.emplace_back("power", &CM::process_power, UP_EVERYONE);
    v.emplace_back("length", &CM::process_length, UP_SINGLE, MS_DEFAULT, SS_LOBBY);
    v.emplace_back("queue", &CM::process_queue, UP_SINGLE, MS_DEFAULT, SS_LOBBY);
    v.emplace_back("allowstart", &CM::process_allowstart, UP_HAMMER);
    v.emplace_back("shuffle", &CM::process_shuffle, UP_HAMMER);
    v.emplace_back("timeout", &CM::process_timeout, UP_HAMMER);
    v.emplace_back("team", &CM::process_team, UP_HAMMER);
    v.emplace_back("resetteams", &CM::process_resetteams, UP_HAMMER);
    v.emplace_back("randomteams", &CM::process_randomteams, UP_HAMMER);
    v.emplace_back("resetgp", &CM::process_resetgp, UP_HAMMER, MS_DEFAULT, SS_LOBBY);
    v.emplace_back("cat+", &CM::process_cat, UP_HAMMER);
    v.emplace_back("cat-", &CM::process_cat, UP_HAMMER);
    v.emplace_back("catshow", &CM::process_cat, UP_HAMMER);
    v.emplace_back("troll", &CM::process_troll, UP_HAMMER, MS_DEFAULT, SS_LOBBY);
    v.emplace_back("hitmsg", &CM::process_hitmsg, UP_HAMMER, MS_DEFAULT, SS_LOBBY);
    v.emplace_back("teamhit", &CM::process_teamhit, UP_HAMMER, MS_DEFAULT, SS_LOBBY);
    v.emplace_back("version", &CM::process_text, UP_EVERYONE);
    v.emplace_back("clear", &CM::process_text, UP_EVERYONE);
    v.emplace_back("register", &CM::process_register, UP_EVERYONE);
#ifdef ENABLE_WEB_SUPPORT
    v.emplace_back("token", &CM::process_token, UP_EVERYONE);
#endif
    v.emplace_back("muteall", &CM::process_muteall, UP_EVERYONE, MS_SOCCER_TOURNAMENT);
    v.emplace_back("game", &CM::process_game, UP_HAMMER, MS_SOCCER_TOURNAMENT, SS_LOBBY);
    v.emplace_back("role", &CM::process_role, UP_HAMMER, MS_SOCCER_TOURNAMENT);
    v.emplace_back("stop", &CM::process_stop, UP_HAMMER, MS_SOCCER_TOURNAMENT, SS_INGAME);
    v.emplace_back("go", &CM::process_go, UP_HAMMER, MS_SOCCER_TOURNAMENT, SS_INGAME);
    v.emplace_back("play", &CM::process_go, UP_HAMMER, MS_SOCCER_TOURNAMENT, SS_INGAME);
    v.emplace_back("resume", &CM::process_go, UP_HAMMER, MS_SOCCER_TOURNAMENT, SS_INGAME);
    v.emplace_back("lobby", &CM::process_lobby, UP_HAMMER, MS_SOCCER_TOURNAMENT, SS_INGAME);
    v.emplace_back("init", &CM::process_init, UP_HAMMER, MS_SOCCER_TOURNAMENT, SS_INGAME);
    v.emplace_back("vote", &CM::special, UP_EVERYONE);
    v.emplace_back("mimiz", &CM::process_mimiz, UP_EVERYONE);
    v.emplace_back("test", &CM::process_test, UP_EVERYONE | PE_VOTED);
    v.emplace_back("help", &CM::process_help, UP_EVERYONE);
// v.emplace_back("1", &CM::special, UP_EVERYONE, MS_DEFAULT);
// v.emplace_back("2", &CM::special, UP_EVERYONE, MS_DEFAULT);
// v.emplace_back("3", &CM::special, UP_EVERYONE, MS_DEFAULT);
// v.emplace_back("4", &CM::special, UP_EVERYONE, MS_DEFAULT);
// v.emplace_back("5", &CM::special, UP_EVERYONE, MS_DEFAULT);
    v.emplace_back("slots", &CM::process_slots, UP_HAMMER | PE_VOTED_NORMAL, MS_DEFAULT, SS_LOBBY);
    v.emplace_back("time", &CM::process_time, UP_EVERYONE);

    v.emplace_back("addondownloadprogress", &CM::special, UP_EVERYONE);
    v.emplace_back("stopaddondownload", &CM::special, UP_EVERYONE);
    v.emplace_back("installaddon", &CM::special, UP_EVERYONE);
    v.emplace_back("uninstalladdon", &CM::special, UP_EVERYONE);
    v.emplace_back("music", &CM::special, UP_EVERYONE);
    v.emplace_back("addonrevision", &CM::special, UP_EVERYONE);
    v.emplace_back("liststkaddon", &CM::special, UP_EVERYONE);
    v.emplace_back("listlocaladdon", &CM::special, UP_EVERYONE);



    addTextResponse("description", ServerConfig::m_motd);
    addTextResponse("moreinfo", StringUtils::wideToUtf8(m_lobby->m_help_message));
    addTextResponse("version", "1.3-rc1 k 210fff beta");
    addTextResponse("clear", std::string(30, '\n'));

    for (Command& command: m_commands) {
        m_stf_command_names.add(command.m_name);
        command.m_description = m_config_descriptions[command.m_name];
    }

    std::sort(m_commands.begin(), m_commands.end(), [](const Command& a, const Command& b) -> bool {
        return a.m_name < b.m_name;
    });
    for (auto& command: m_commands)
        m_name_to_command[command.m_name] = command;

    // m_votables.emplace("replay", 1.0);
    m_votables.emplace("start", 0.81);
    m_votables.emplace("config", 0.6);
    m_votables.emplace("kick", 0.81);
    m_votables.emplace("kickban", 0.81);
    m_votables.emplace("gnu", 0.81);
    m_votables.emplace("slots", CommandVoting::DEFAULT_THRESHOLD);
    m_votables["gnu"].setCustomThreshold("gnu kart", 1.1);
} // initCommands
// ========================================================================

CommandManager::CommandManager(ServerLobby* lobby):
    m_lobby(lobby)
{
    if (!lobby)
        return;
    initCommands();
} // CommandManager
// ========================================================================

void CommandManager::handleCommand(Event* event, std::shared_ptr<STKPeer> peer)
{
    NetworkString& data = event->data();
    std::string language;
    data.decodeString(&language);

    Context context(event, peer);
    auto& argv = context.m_argv;
    auto& cmd = context.m_cmd;
    auto& permissions = context.m_user_permissions;
    auto& voting = context.m_voting;

    data.decodeString(&cmd);
    argv = StringUtils::splitQuoted(cmd, ' ', '"', '"', '\\');
    if (argv.size() == 0)
        return;
    CommandManager::restoreCmdByArgv(cmd, argv, ' ', '"', '"', '\\');

    permissions = m_lobby->getPermissions(peer);
    voting = false;
    std::string action = "invoke";
    std::string username = StringUtils::wideToUtf8(
        peer->getPlayerProfiles()[0]->getName());

    if (argv[0] == "vote")
    {
        if (argv.size() == 1 || argv[1] == "vote")
        {
            std::string msg = "Usage: /vote (a command with arguments)";
            m_lobby->sendStringToPeer(msg, peer);
            return;
        }
        std::reverse(argv.begin(), argv.end());
        argv.pop_back();
        std::reverse(argv.begin(), argv.end());
        CommandManager::restoreCmdByArgv(cmd, argv, ' ', '"', '"', '\\');
        voting = true;
        action = "vote for";
    }
    bool restored = false;
    for (int i = 0; i <= 5; ++i) {
        if (argv[0] == std::to_string(i)) {
            if (i < (int)m_user_command_replacements[username].size() &&
                    !m_user_command_replacements[username][i].empty()) {
                cmd = m_user_command_replacements[username][i];
                argv = StringUtils::splitQuoted(cmd, ' ', '"', '"', '\\');
                if (argv.size() == 0)
                    return;
                CommandManager::restoreCmdByArgv(cmd, argv, ' ', '"', '"', '\\');
                voting = m_user_saved_voting[username];
                restored = true;
                break;
            } else {
                std::string msg = "Pick one of " +
                    std::to_string(-1 + (int)m_user_command_replacements[username].size())
                    + " options using /1, etc., or use /0, or type a different command";
                m_lobby->sendStringToPeer(msg, peer);
                return;
            }
        }
    }
    m_user_command_replacements.erase(username);
    if (!restored)
        m_user_correct_arguments.erase(username);

    if (hasTypo(peer, voting, argv, cmd, 0, m_stf_command_names, 3, false, false))
        return;

    auto command_iterator = m_name_to_command.find(argv[0]);

    const auto& command = command_iterator->second;

    if (!isAvailable(command))
    {
        std::string msg = "You don't have permissions to " + action + " this command";
        m_lobby->sendStringToPeer(msg, peer);
        return;
    }
    int mask = (permissions & command.m_permissions);
    if (mask == 0)
    {
        std::string msg = "You don't have permissions to " + action + " this command";
        m_lobby->sendStringToPeer(msg, peer);
        return;
    }
    int mask_without_voting = (mask & ~PE_VOTED);
    if (mask != PE_NONE && mask_without_voting == PE_NONE)
        voting = true;

    execute(command, context);

    while (!m_triggered_votables.empty())
    {
        std::string votable_name = m_triggered_votables.front();
        m_triggered_votables.pop();
        auto it = m_votables.find(votable_name);
        if (it != m_votables.end() && it->second.needsCheck())
        {
            auto response = it->second.process(m_users);
            int count = response.first;
            std::string msg = username + " voted \"" + cmd + "\", there are " + std::to_string(count) + " such votes";
            m_lobby->sendStringToAllPeers(msg);
            auto res = response.second;
            if (!res.empty())
            {
                for (auto& p: res)
                {
                    std::string new_cmd = p.first + " " + p.second;
                    auto new_argv = StringUtils::splitQuoted(cmd, ' ', '"', '"', '\\');
                    CommandManager::restoreCmdByArgv(new_cmd, new_argv, ' ', '"', '"', '\\');
                    std::string msg = "Command \"" + new_cmd + "\" has been successfully voted";
                    m_lobby->sendStringToAllPeers(msg);
                    Context new_context(event, std::shared_ptr<STKPeer>(nullptr), new_argv, new_cmd, UP_EVERYONE, false);
                    execute(command, new_context);
                }
            }
        }
    }

} // handleCommand
// ========================================================================

int CommandManager::getCurrentModeScope()
{
    int mask = MS_DEFAULT;
    if (ServerConfig::m_soccer_tournament)
        mask |= MS_SOCCER_TOURNAMENT;
    return mask;
} // getCurrentModeScope
// ========================================================================

int CommandManager::getCurrentStateScope()
{
    auto state = m_lobby->m_state.load();
    if (state < ServerLobby::WAITING_FOR_START_GAME
        || state > ServerLobby::RESULT_DISPLAY)
        return 0;
    if (state == ServerLobby::WAITING_FOR_START_GAME)
        return SS_LOBBY;
    return SS_INGAME;
} // getCurrentStateScope
// ========================================================================

bool CommandManager::isAvailable(const Command& c)
{
    return (getCurrentModeScope() & c.m_mode_scope) != 0
        && (getCurrentStateScope() & c.m_state_scope) != 0;
} // getCurrentModeScope
// ========================================================================

void CommandManager::vote(Context& context, std::string category, std::string value)
{
    auto& peer = context.m_peer;
    auto& argv = context.m_argv;
    std::string username = StringUtils::wideToUtf8(
        peer->getPlayerProfiles()[0]->getName());
    auto& votable = m_votables[argv[0]];
    bool neededCheck = votable.needsCheck();
    votable.castVote(username, category, value);
    if (votable.needsCheck() && !neededCheck)
        m_triggered_votables.push(argv[0]);
} // vote
// ========================================================================

void CommandManager::update()
{
    for (auto& votable_pairs: m_votables)
    {
        auto& votable = votable_pairs.second;
        auto response = votable.process(m_users);
        auto res = response.second;
        if (!res.empty())
        {
            for (auto& p: res)
            {
                std::string new_cmd = p.first + " " + p.second;
                auto new_argv = StringUtils::splitQuoted(new_cmd, ' ', '"', '"', '\\');
                CommandManager::restoreCmdByArgv(new_cmd, new_argv, ' ', '"', '"', '\\');
                std::string msg = "Command \"" + new_cmd + "\" has been successfully voted";
                m_lobby->sendStringToAllPeers(msg);
                auto& command = m_name_to_command[new_argv[0]];
                // We don't know the event though it is only needed in
                // ServerLobby::startSelection where it is nullptr when they vote
                Context new_context(nullptr, std::shared_ptr<STKPeer>(nullptr), new_argv, new_cmd, UP_EVERYONE, false);
                execute(command, new_context);
            }
        }
    }
} // update
// ========================================================================

void CommandManager::error(Context& context)
{
    std::string command = context.m_argv[0];
    // Here we assume that the command argv[0] exists,
    // as we intend to invoke error() from process_* functions
    std::string msg = m_name_to_command[command].getUsage();
    if (msg.empty())
        msg = "An error occurred while invoking command \"" + context.m_argv[0] + "\".";
    m_lobby->sendStringToPeer(msg, context.m_peer);
} // error
// ========================================================================

void CommandManager::execute(const Command& command, Context& context)
{
    m_current_argv = context.m_argv;
    (this->*command.m_action)(context);
    m_current_argv = {};
} // execute
// ========================================================================

void CommandManager::process_help(Context& context)
{
    auto& argv = context.m_argv;
    if (argv.size() < 2)
    {
        error(context);
        return;
    }

    if (hasTypo(context.m_peer, context.m_voting, context.m_argv, context.m_cmd, 1, m_stf_command_names, 3, false, false))
        return;

    std::string msg;
    auto it = m_name_to_command.find(argv[1]);
    if (it == m_name_to_command.end())
        msg = "Unknown command \"" + argv[1] + "\"";
    else
        msg = it->second.getHelp();

    m_lobby->sendStringToPeer(msg, context.m_peer);
} // process_commands
// ========================================================================

void CommandManager::process_text(Context& context)
{
    std::string response;
    auto it = m_text_response.find(context.m_argv[0]);
    if (it == m_text_response.end())
        response = "Error: a text command " + context.m_argv[0]
            + " is defined without text";
    else
        response = it->second;
    m_lobby->sendStringToPeer(response, context.m_peer);
} // process_text
// ========================================================================

void CommandManager::process_file(Context& context)
{
    std::string response;
    auto it = m_file_resources.find(context.m_argv[0]);
    if (it == m_file_resources.end())
        response = "Error: file not found for a file command "
            + context.m_argv[0];
    else
        response = it->second.get();
    m_lobby->sendStringToPeer(response, context.m_peer);
} // process_text
// ========================================================================

void CommandManager::process_commands(Context& context)
{
    std::string result = "Available commands:";
    for (const Command& c: m_commands)
    {
        if ((context.m_user_permissions & c.m_permissions) != 0
            && isAvailable(c))
            result += " " + c.m_name;
    }

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
    if (!ServerConfig::m_owner_less && (context.m_user_permissions & UP_HAMMER) == 0)
    {
        context.m_voting = true;
    }
    if (context.m_voting)
    {
        vote(context, "start", "");
        return;
    }
    m_lobby->startSelection(context.m_event);
} // process_start
// ========================================================================

void CommandManager::process_config(Context& context)
{
    const auto& argv = context.m_argv;
    int difficulty = m_lobby->getDifficulty();
    int mode = m_lobby->getGameMode();
    bool goal_target = m_lobby->isSoccerGoalTarget();
    // bool gp = false;
    std::vector<std::vector<std::string>> mode_aliases = {
        {"m0"},
        {"m1"},
        {"m2"},
        {"m3", "normal", "normal-race", "race"},
        {"m4", "tt", "time-trial", "trial"},
        {"m5"},
        {"m6", "soccer", "football"},
        {"m7", "ffa", "free-for-all", "free", "for", "all"},
        {"m8", "ctf", "capture-the-flag", "capture", "the", "flag"}
    };
    std::vector<std::vector<std::string>> difficulty_aliases = {
        {"d0", "novice", "easy"},
        {"d1", "intermediate", "medium"},
        {"d2", "expert", "hard"},
        {"d3", "supertux", "super", "best"}
    };
    std::vector<std::vector<std::string>> goal_aliases = {
        {"tl", "time-limit", "time", "minutes"},
        {"gl", "goal-limit", "goal", "goals"}
    };
    for (unsigned i = 1; i < argv.size(); i++)
    {
        for (unsigned j = 0; j < mode_aliases.size(); ++j) {
            if (j <= 2 || j == 5) {
                // Switching to GP or modes 2, 5 is not supported yet
                continue;
            }
            for (std::string& alias: mode_aliases[j]) {
                if (argv[i] == alias) {
                    mode = j;
                }
            }
        }
        for (unsigned j = 0; j < difficulty_aliases.size(); ++j) {
            for (std::string& alias: difficulty_aliases[j]) {
                if (argv[i] == alias) {
                    difficulty = j;
                }
            }
        }
        for (unsigned j = 0; j < goal_aliases.size(); ++j) {
            for (std::string& alias: goal_aliases[j]) {
                if (argv[i] == alias) {
                    goal_target = (bool)j;
                }
            }
        }
    }
    // if (mode != 6) {
    //     goal_target = false;
    // }
    if (!m_lobby->isDifficultyAvailable(difficulty)
        || !m_lobby->isModeAvailable(mode))
    {
        std::string response = "Mode or difficulty are not permitted on this server";
        m_lobby->sendStringToPeer(response, context.m_peer);
        return;
    }
    if (context.m_voting)
    {
        // Definitely not the best format as there are extra words
        // but I'll think how to resolve it
        vote(context, "config mode", mode_aliases[mode][0]);
        vote(context, "config difficulty", difficulty_aliases[difficulty][0]);
        vote(context, "config target", goal_aliases[goal_target ? 1 : 0][0]);
        return;
    }
    m_lobby->handleServerConfiguration(context.m_peer, difficulty, mode, goal_target);
} // process_config
// ========================================================================

void CommandManager::process_spectate(Context& context)
{
    std::string response = "";
    auto& argv = context.m_argv;
    auto& peer = context.m_peer;

    if (ServerConfig::m_soccer_tournament || ServerConfig::m_only_host_riding)
        response = "All spectators already have auto spectate ability";

    if (/*m_game_setup->isGrandPrix() || */!ServerConfig::m_live_players)
        response = "Server doesn't support spectating";

    if (!response.empty())
    {
        m_lobby->sendStringToPeer(response, peer);
        return;
    }

    if (argv.size() == 1)
    {
        if (peer->isCommandSpectator())
            argv.push_back("0");
        else
            argv.push_back("1");
    }

    if (argv.size() < 2 || (argv[1] != "0" && argv[1] != "1"))
    {
        error(context);
        return;
    }

    if (argv[1] == "1")
    {
        if (m_lobby->m_process_type == PT_CHILD &&
            peer->getHostId() == m_lobby->m_client_server_host_id.load())
        {
            std::string msg = "Graphical client server cannot spectate";
            m_lobby->sendStringToPeer(msg, peer);
            return;
        }
        peer->setAlwaysSpectate(ASM_COMMAND);
    }
    else
        peer->setAlwaysSpectate(ASM_NONE);
    m_lobby->updateServerOwner(true);
    m_lobby->updatePlayerList();
} // process_spectate
// ========================================================================

void CommandManager::process_addons(Context& context)
{
    auto& argv = context.m_argv;
    bool more = (argv[0] == "moreaddons");
    bool more_own = (argv[0] == "getaddons");
    if (argv.size() == 1)
    {
        argv.push_back("");
        switch (m_lobby->m_game_mode.load())
        {
            case 0:
            case 1:
            case 3:
            case 4:
                argv[1] = "track";
                break;
            case 6:
                argv[1] = "soccer";
                break;
            case 7:
            case 8:
                argv[1] = "arena";
                break;
        }
    }
    const std::set<std::string>& from =
        (argv[1] == "kart" ? m_lobby->m_addon_kts.first :
        (argv[1] == "track" ? m_lobby->m_addon_kts.second :
        (argv[1] == "arena" ? m_lobby->m_addon_arenas :
        /*argv[1] == "soccer" ?*/ m_lobby->m_addon_soccers
    )));
    std::vector<std::pair<std::string, std::vector<std::string>>> result;
    for (const std::string& s: from)
        result.push_back({s, {}});

    auto peers = STKHost::get()->getPeers();
    int num_players = 0;
    for (auto peer : peers)
    {
        if (!peer || !peer->isValidated() || peer->isWaitingForGame()
            || !m_lobby->canRace(peer) || peer->isCommandSpectator())
            continue;
        ++num_players;
        std::string username = StringUtils::wideToUtf8(
                peer->getPlayerProfiles()[0]->getName());
        const auto& kt = peer->getClientAssets();
        const auto& container = (argv[1] == "kart" ? kt.first : kt.second);
        for (auto& p: result)
            if (container.find(p.first) == container.end())
                p.second.push_back(username);
    }
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(result.begin(), result.end(), g);
    std::stable_sort(result.begin(), result.end(),
        [](const std::pair<std::string, std::vector<std::string>>& a,
            const std::pair<std::string, std::vector<std::string>>& b) -> bool {
            if (a.second.size() != b.second.size())
                return a.second.size() > b.second.size();
            return false;
        });
    std::string response;
    const int NEXT_ADDONS = 5;
    std::vector<std::string> all_have;
    while (!result.empty() && (int)result.back().second.size() == 0)
    {
        all_have.push_back(result.back().first);
        result.pop_back();
    }
    if (num_players > 0) {
        response = "Found " + std::to_string(all_have.size()) + " asset(s)";
        std::reverse(result.begin(), result.end());
        if (more_own)
        {
            auto result2 = result;
            result.clear();
            std::string asking_username = StringUtils::wideToUtf8(
                    context.m_peer->getPlayerProfiles()[0]->getName());
            for (int i = 0; i < result2.size(); ++i)
            {
                bool present = false;
                for (int j = 0; j < result2[i].second.size(); ++j)
                {
                    if (result2[i].second[j] == asking_username)
                    {
                        present = true;
                        break;
                    }
                }
                if (present)
                    result.push_back(result2[i]);
            }
        }
        if (result.size() > NEXT_ADDONS)
            result.resize(NEXT_ADDONS);
        if (!more && !more_own)
        {
            bool nothing = true;
            for (const std::string& s: all_have)
            {
                if (s.length() < 6 || s.substr(0, 6) != "addon_")
                    continue;
                response.push_back(nothing ? ':' : ',');
                nothing = false;
                response.push_back(' ');
                response += s.substr(6);
            }
            if (response.length() > 100)
                response += "\nTotal: " + std::to_string(all_have.size());
        }
        else
        {
            if (result.empty())
                response += "\nNothing more to install!";
            else
            {
                response += ". More addons to install:";
                for (unsigned i = 0; i < result.size(); ++i)
                {
                    response += "\n/installaddon " + result[i].first.substr(6) + " , missing for "
                        + std::to_string(result[i].second.size())
                        + " player(s):";
                    std::sort(result[i].second.begin(), result[i].second.end());
                    for (unsigned j = 0; j < result[i].second.size(); ++j)
                    {
                        response += " " + result[i].second[j];
                    }
                }
            }
        }
    } else {
        response = "No one in the lobby can play. Found "
            + std::to_string(all_have.size()) + " assets on the server.";
    }
    m_lobby->sendStringToPeer(response, context.m_peer);
} // process_addons
// ========================================================================

void CommandManager::process_checkaddon(Context& context)
{
    auto& argv = context.m_argv;
    if (argv.size() < 2)
    {
        error(context);
        return;
    }
    std::string id = "addon_" + argv[1];
    const unsigned HAS_KART = 1;
    const unsigned HAS_MAP = 2;

    unsigned server_status = 0;
    std::vector<std::string> players[4];

    if (m_lobby->m_addon_kts.first.count(id))
        server_status |= HAS_KART;
    if (m_lobby->m_addon_kts.second.count(id))
        server_status |= HAS_MAP;
    if (m_lobby->m_addon_arenas.count(id))
        server_status |= HAS_MAP;
    if (m_lobby->m_addon_soccers.count(id))
        server_status |= HAS_MAP;

    auto peers = STKHost::get()->getPeers();
    unsigned total_players = 0;
    for (auto peer : peers)
    {
        if (!peer || !peer->isValidated() || peer->isWaitingForGame()
            || !m_lobby->canRace(peer) || peer->isCommandSpectator())
            continue;
        std::string username = StringUtils::wideToUtf8(
                peer->getPlayerProfiles()[0]->getName());
        const auto& kt = peer->getClientAssets();
        unsigned status = 0;
        if (kt.first.find(id) != kt.first.end())
            status |= HAS_KART;
        if (kt.second.find(id) != kt.second.end())
            status |= HAS_MAP;
        players[status].push_back(username);
        ++total_players;
    }

    std::string response = "";
    std::string item_name[3];
    bool needed[3];
    item_name[HAS_KART] = "kart";
    item_name[HAS_MAP] = "map";
    needed[HAS_KART] = (players[HAS_KART].size() + players[HAS_MAP | HAS_KART].size() > 0);
    needed[HAS_MAP] = (players[HAS_MAP].size() + players[HAS_MAP | HAS_KART].size() > 0);
    if (server_status & HAS_KART)
        needed[HAS_KART] = true;
    if (server_status & HAS_MAP)
        needed[HAS_MAP] = true;

    if (!needed[HAS_KART] && !needed[HAS_MAP])
    {
        response = "Neither server nor clients have addon " + argv[1] + " installed";
    }
    else
    {
        std::random_device rd;
        std::mt19937 g(rd());
        std::string installed_text[2] = {"Not installed", "Installed"};
        for (unsigned item = 1; item <= 2; ++item)
        {
            if (!needed[item])
                continue;
            response += "Server ";
            if (server_status & item)
                response += "has";
            else
                response += "doesn't have";
            response += " " + item_name[item] + " " + argv[1] + "\n";

            std::vector<std::string> categories[2];
            for (unsigned status = 0; status < 4; ++status)
            {
                for (const std::string& s: players[status])
                    categories[(status & item ? 1 : 0)].push_back(s);
            }
            for (int i = 0; i < 2; ++i)
                shuffle(categories[i].begin(), categories[i].end(), g);
            if (categories[0].empty())
                response += "Everyone who can play has this " + item_name[item] + "\n";
            else if (categories[1].empty())
                response += "No one of those who can play has this " + item_name[item] + "\n";
            else
            {
                for (int i = 1; i >= 0; --i)
                {
                    response += installed_text[i] + " for ";
                    response += std::to_string(categories[i].size()) + " player(s): ";
                    for (int j = 0; j < 5 && j < (int)categories[i].size(); ++j)
                    {
                        if (j)
                            response += ", ";
                        response += categories[i][j];
                    }
                    if (categories[i].size() > 5)
                        response += ", ...";
                    response += "\n";
                }
            }
        }
        response.pop_back();
    }
    m_lobby->sendStringToPeer(response, context.m_peer);
} // process_checkaddon
// ========================================================================

void CommandManager::process_lsa(Context& context)
{
    std::string response = "";
    auto& argv = context.m_argv;
    bool has_options = argv.size() > 1 &&
        (argv[1].compare("-track") == 0 ||
        argv[1].compare("-arena") == 0 ||
        argv[1].compare("-kart") == 0 ||
        argv[1].compare("-soccer") == 0);
    if (argv.size() == 1 || argv.size() > 3 || argv[1].size() < 3 ||
        (argv.size() == 2 && (argv[1].size() < 3 || has_options)) ||
        (argv.size() == 3 && (!has_options || argv[2].size() < 3)))
    {
        error(context);
        return;
    }
    std::string type = "";
    std::string text = "";
    if (argv.size() > 1)
    {
        if (argv[1].compare("-track") == 0 ||
            argv[1].compare("-arena") == 0 ||
            argv[1].compare("-kart") == 0 ||
            argv[1].compare("-soccer") == 0)
            type = argv[1].substr(1);
        if ((argv.size() == 2 && type.empty()) || argv.size() == 3)
            text = argv[argv.size() - 1];
    }

    std::set<std::string> total_addons;
    if (type.empty() || // not specify addon type
       (!type.empty() && type.compare("kart") == 0)) // list kart addon
    {
        total_addons.insert(m_lobby->m_addon_kts.first.begin(), m_lobby->m_addon_kts.first.end());
    }
    if (type.empty() || // not specify addon type
       (!type.empty() && type.compare("track") == 0))
    {
        total_addons.insert(m_lobby->m_addon_kts.second.begin(), m_lobby->m_addon_kts.second.end());
    }
    if (type.empty() || // not specify addon type
       (!type.empty() && type.compare("arena") == 0))
    {
        total_addons.insert(m_lobby->m_addon_arenas.begin(), m_lobby->m_addon_arenas.end());
    }
    if (type.empty() || // not specify addon type
       (!type.empty() && type.compare("soccer") == 0))
    {
        total_addons.insert(m_lobby->m_addon_soccers.begin(), m_lobby->m_addon_soccers.end());
    }
    std::string msg = "";
    for (auto& addon : total_addons)
    {
        // addon_ (6 letters)
        if (!text.empty() && addon.find(text, 6) == std::string::npos)
            continue;

        msg += addon.substr(6);
        msg += ", ";
    }
    if (msg.empty())
        response = "Addon not found";
    else
    {
        msg = msg.substr(0, msg.size() - 2);
        response = "Server's addons: " + msg;
    }
    m_lobby->sendStringToPeer(response, context.m_peer);
} // process_lsa
// ========================================================================

void CommandManager::process_pha(Context& context)
{
    std::string response = "";
    auto& argv = context.m_argv;
    if (argv.size() < 3)
    {
        error(context);
        return;
    }
    if (hasTypo(context.m_peer, context.m_voting, context.m_argv, context.m_cmd,
        2, m_stf_present_users, 3, false, false))
        return;

    std::string addon_id = argv[1];
    std::string player_name = argv[2];
    std::shared_ptr<STKPeer> player_peer = STKHost::get()->findPeerByName(
        StringUtils::utf8ToWide(player_name));
    if (player_name.empty() || !player_peer || addon_id.empty())
    {
        error(context);
        return;
    }

    std::string addon_id_test = Addon::createAddonId(addon_id);
    bool found = false;
    const auto& kt = player_peer->getClientAssets();
    for (auto& kart : kt.first)
    {
        if (kart == addon_id_test)
        {
            found = true;
            break;
        }
    }
    if (!found)
    {
        for (auto& track : kt.second)
        {
            if (track == addon_id_test)
            {
                found = true;
                break;
            }
        }
    }
    if (found)
    {
        response = player_name + " has addon " + addon_id;
    }
    else
    {
        response = player_name + " has no addon " + addon_id;
    }
    m_lobby->sendStringToPeer(response, context.m_peer);
} // process_pha
// ========================================================================

void CommandManager::process_kick(Context& context)
{
    auto& peer = context.m_peer;
    auto& argv = context.m_argv;

    if (argv.size() < 2)
    {
        error(context);
        return;
    }

    if (hasTypo(context.m_peer, context.m_voting, context.m_argv, context.m_cmd,
        1, m_stf_present_users, 3, false, false))
        return;

    std::string player_name = argv[1];

    std::shared_ptr<STKPeer> player_peer = STKHost::get()->findPeerByName(
        StringUtils::utf8ToWide(player_name));
    if (player_name.empty() || !player_peer || player_peer->isAIPeer())
    {
        error(context);
        return;
    }
    if (player_peer->isAngryHost())
    {
        std::string msg = "This player is the owner of this server, "
            "and is protected from your actions now";
        m_lobby->sendStringToPeer(msg, context.m_peer);
        return;
    }
    if (context.m_voting)
    {
        vote(context, argv[0] + " " + player_name, "");
        return;
    }
    Log::info("CommandManager", "%s kicks %s", (peer.get() ? "Crown player" : "Vote"), player_name.c_str());
    player_peer->kick();
    if (ServerConfig::m_track_kicks)
    {
        std::string auto_report = "[ Auto report caused by kick ]";
        m_lobby->writeOwnReport(player_peer.get(), peer.get(), auto_report);
    }
    if (argv[0] == "kickban")
    {
        Log::info("CommandManager", "%s is now banned", player_name.c_str());
        m_lobby->m_temp_banned.insert(player_name);
        std::string msg = StringUtils::insertValues(
            "%s is now banned", player_name.c_str());
        m_lobby->sendStringToPeer(msg, peer);
    }
} // process_kick
// ========================================================================

void CommandManager::process_unban(Context& context)
{
    auto& argv = context.m_argv;
    if (argv.size() < 2)
    {
        error(context);
        return;
    }
    std::string player_name = argv[1];
    if (player_name.empty())
    {
        error(context);
        return;
    }
    Log::info("CommandManager", "%s is now unbanned", player_name.c_str());
    m_lobby->m_temp_banned.erase(player_name);
    std::string msg = StringUtils::insertValues(
        "%s is now unbanned", player_name.c_str());
    m_lobby->sendStringToPeer(msg, context.m_peer);
} // process_unban
// ========================================================================

void CommandManager::process_ban(Context& context)
{
    std::string player_name;
    auto& argv = context.m_argv;
    if (argv.size() < 2)
    {
        error(context);
        return;
    }
    player_name = argv[1];
    if (player_name.empty())
    {
        error(context);
        return;
    }
    Log::info("CommandManager", "%s is now banned", player_name.c_str());
    m_lobby->m_temp_banned.insert(player_name);
    std::string msg = StringUtils::insertValues(
        "%s is now banned", player_name.c_str());
    m_lobby->sendStringToPeer(msg, context.m_peer);
} // process_ban
// ========================================================================

void CommandManager::process_pas(Context& context)
{
    std::string response;
    std::string player_name;
    auto& argv = context.m_argv;
    if (argv.size() < 2)
    {
        error(context);
        return;
    }

    if (hasTypo(context.m_peer, context.m_voting, context.m_argv, context.m_cmd,
        1, m_stf_present_users, 3, false, false))
        return;

    player_name = argv[1];
    std::shared_ptr<STKPeer> player_peer = STKHost::get()->findPeerByName(
        StringUtils::utf8ToWide(player_name));
    if (player_name.empty() || !player_peer)
    {
        error(context);
        return;
    }
    auto& scores = player_peer->getAddonsScores();
    if (scores[AS_KART] == -1 && scores[AS_TRACK] == -1 &&
        scores[AS_ARENA] == -1 && scores[AS_SOCCER] == -1)
    {
        response = player_name + " has no addons";
    }
    else
    {
        std::string msg = player_name;
        msg += " addons:";
        if (scores[AS_KART] != -1)
            msg += " karts: " + StringUtils::toString(scores[AS_KART]) + ",";
        if (scores[AS_TRACK] != -1)
            msg += " tracks: " + StringUtils::toString(scores[AS_TRACK]) + ",";
        if (scores[AS_ARENA] != -1)
            msg += " arenas: " + StringUtils::toString(scores[AS_ARENA]) + ",";
        if (scores[AS_SOCCER] != -1)
            msg += " fields: " + StringUtils::toString(scores[AS_SOCCER]) + ",";
        msg.pop_back();
        response = msg;
    }
    m_lobby->sendStringToPeer(response, context.m_peer);
} // process_pas
// ========================================================================

void CommandManager::process_sha(Context& context)
{
    std::string response;
    auto& argv = context.m_argv;
    if (argv.size() != 2)
    {
        error(context);
        return;
    }
    std::set<std::string> total_addons;
    total_addons.insert(m_lobby->m_addon_kts.first.begin(), m_lobby->m_addon_kts.first.end());
    total_addons.insert(m_lobby->m_addon_kts.second.begin(), m_lobby->m_addon_kts.second.end());
    total_addons.insert(m_lobby->m_addon_arenas.begin(), m_lobby->m_addon_arenas.end());
    total_addons.insert(m_lobby->m_addon_soccers.begin(), m_lobby->m_addon_soccers.end());
    std::string addon_id_test = Addon::createAddonId(argv[1]);
    bool found = total_addons.find(addon_id_test) != total_addons.end();
    if (found)
    {
        response = "Server has addon " + argv[1];
    }
    else
    {
        response = "Server has no addon " + argv[1];
    }
    m_lobby->sendStringToPeer(response, context.m_peer);
} // process_sha
// ========================================================================

void CommandManager::process_mute(Context& context)
{
    std::shared_ptr<STKPeer> player_peer;
    auto& argv = context.m_argv;
    auto& peer = context.m_peer;
    std::string result_msg;
    core::stringw player_name;

    if (argv.size() != 2 || argv[1].empty())
    {
        error(context);
        return;
    }

    if (hasTypo(context.m_peer, context.m_voting, context.m_argv, context.m_cmd,
        1, m_stf_present_users, 3, false, false))
        return;

    player_name = StringUtils::utf8ToWide(argv[1]);
    player_peer = STKHost::get()->findPeerByName(player_name);

    if (!player_peer || player_peer == peer)
    {
        error(context);
        return;
    }

    m_lobby->m_peers_muted_players[peer].insert(player_name);
    result_msg = "Muted player " + argv[1];
    m_lobby->sendStringToPeer(result_msg, context.m_peer);
} // process_mute
// ========================================================================

void CommandManager::process_unmute(Context& context)
{
    std::shared_ptr<STKPeer> player_peer;
    auto& argv = context.m_argv;
    auto& peer = context.m_peer;
    std::string result_msg;
    core::stringw player_name;

    if (argv.size() != 2 || argv[1].empty())
    {
        error(context);
        return;
    }

    player_name = StringUtils::utf8ToWide(argv[1]);
    for (auto it = m_lobby->m_peers_muted_players[peer].begin();
        it != m_lobby->m_peers_muted_players[peer].end();)
    {
        if (*it == player_name)
        {
            it = m_lobby->m_peers_muted_players[peer].erase(it);
            result_msg = "Unmuted player " + argv[1];
            m_lobby->sendStringToPeer(result_msg, context.m_peer);
            return;
        }
        else
        {
            it++;
        }
    }

    error(context);
} // process_unmute
// ========================================================================

void CommandManager::process_listmute(Context& context)
{
    auto& peer = context.m_peer;
    std::string response;
    int num_players = 0;
    for (auto& name : m_lobby->m_peers_muted_players[peer])
    {
        response += StringUtils::wideToUtf8(name);
        response += " ";
        ++num_players;
    }
    if (num_players == 0)
        response = "No player has been muted by you";
    else
    {
        response += (num_players == 1 ? "is" : "are");
        response += " muted (total: " + std::to_string(num_players) + ")";
    }

    m_lobby->sendStringToPeer(response, peer);
} // process_listmute
// ========================================================================

void CommandManager::process_gnu(Context& context)
{
    auto& argv = context.m_argv;
    if (argv[0] != "gnu")
    {
        argv[0] = "gnu";
        if (argv.size() < 2) {
            argv.resize(2);
        }
        argv[1] = "off";
    }
    // "nognu" and "gnu off" are equivalent
    bool turn_on = (argv.size() < 2 || argv[1] != "off");
    if (turn_on && m_lobby->m_kart_elimination.isEnabled())
    {
        std::string msg = "Gnu Elimination mode was already enabled!";
        m_lobby->sendStringToPeer(msg, context.m_peer);
        return;
    }
    if (!turn_on && !m_lobby->m_kart_elimination.isEnabled())
    {
        std::string msg = "Gnu Elimination mode was already off!";
        m_lobby->sendStringToPeer(msg, context.m_peer);
        return;
    }
    if (turn_on &&
        RaceManager::get()->getMinorMode() != RaceManager::MINOR_MODE_NORMAL_RACE &&
        RaceManager::get()->getMinorMode() != RaceManager::MINOR_MODE_TIME_TRIAL)
    {
        std::string msg = "Gnu Elimination is available only with racing modes";
        m_lobby->sendStringToPeer(msg, context.m_peer);
        return;
    }
    std::string kart;
    if (!turn_on)
    {
        kart = "off";
    }
    else
    {
        if (context.m_peer)
        {
            kart = "gnu";
            if (argv.size() > 1 && m_lobby->m_available_kts.first.count(argv[1]) > 0)
            {
                kart = argv[1];
            }
        }
        else // voted
        {
            kart = m_votables["gnu"].getAnyBest("gnu kart");
        }
    }
    if (context.m_voting)
    {
        if (kart != "off")
        {
            vote(context, "gnu", "on");
            vote(context, "gnu kart", kart);
        }
        else
        {
            vote(context, "gnu", "off");
        }
        return;
    }
    m_votables["gnu"].reset("gnu kart");
    if (kart == "off")
    {
        m_lobby->m_kart_elimination.disable();
        std::string msg = "Gnu Elimination is now off";
        m_lobby->sendStringToAllPeers(msg);
    }
    else
    {
        m_lobby->m_kart_elimination.enable(kart);
        std::string msg = m_lobby->m_kart_elimination.getStartingMessage();
        m_lobby->sendStringToAllPeers(msg);
    }
} // process_gnu
// ========================================================================

void CommandManager::process_tell(Context& context)
{
    auto& argv = context.m_argv;
    if (argv.size() == 1)
    {
        error(context);
        return;
    }
    std::string ans;
    for (unsigned i = 1; i < argv.size(); ++i)
    {
        if (i > 1)
            ans.push_back(' ');
        ans += argv[i];
    }
    m_lobby->writeOwnReport(context.m_peer.get(), context.m_peer.get(), ans);
} // process_tell
// ========================================================================

void CommandManager::process_standings(Context& context)
{
    std::string msg;
    auto& argv = context.m_argv;
    bool isGP = false;
    bool isGnu = false;
    bool isGPTeams = false;
    bool isGPPlayers = false;
    for (int i = 1; i < argv.size(); ++i)
    {
        if (argv[i] == "gp")
            isGP = true;
        else if (argv[i] == "gnu")
            isGnu = true;
        else if (argv[i] == "team" || argv[i] == "teams")
            isGPTeams = true;
        else if (argv[i] == "player" || argv[i] == "players")
            isGPPlayers = true;
        else if (argv[i] == "both" || argv[i] == "all")
            isGPPlayers = isGPTeams = true;
    }
    if (!isGP && !isGnu)
    {
        if (m_lobby->m_game_setup->isGrandPrix())
            isGP = true;
        else
            isGnu = true;
    }
    if (isGP)
    {
        // the function will decide itself what to show if nothing is specified:
        // if there are teams, teams will be shown, otherwise players
        msg = m_lobby->getGrandPrixStandings(isGPPlayers, isGPTeams);
    }
    else if (isGnu)
        msg = m_lobby->m_kart_elimination.getStandings();
    m_lobby->sendStringToPeer(msg, context.m_peer);
} // process_standings
// ========================================================================

void CommandManager::process_teamchat(Context& context)
{
    m_lobby->m_team_speakers.insert(context.m_peer.get());
    std::string msg = "Your messages are now addressed to team only";
    m_lobby->sendStringToPeer(msg, context.m_peer);
} // process_teamchat
// ========================================================================

void CommandManager::process_to(Context& context)
{
    auto& argv = context.m_argv;
    if (argv.size() == 1)
    {
        error(context);
        return;
    }
    auto& peer = context.m_peer;
    m_lobby->m_message_receivers[peer.get()].clear();
    for (unsigned i = 1; i < argv.size(); ++i)
    {
        if (hasTypo(context.m_peer, context.m_voting, context.m_argv, context.m_cmd,
            i, m_stf_present_users, 3, false, true))
            return;
        m_lobby->m_message_receivers[peer.get()].insert(
            StringUtils::utf8ToWide(argv[i]));
    }
    std::string msg = "Successfully changed chat settings";
    m_lobby->sendStringToPeer(msg, peer);
} // process_to
// ========================================================================

void CommandManager::process_public(Context& context)
{
    auto& peer = context.m_peer;
    m_lobby->m_message_receivers[peer.get()].clear();
    m_lobby->m_team_speakers.erase(peer.get());
    std::string s = "Your messages are now public";
    m_lobby->sendStringToPeer(s, peer);
} // process_public
// ========================================================================

void CommandManager::process_record(Context& context)
{
    std::string response;
    auto& argv = context.m_argv;
#ifdef ENABLE_SQLITE3
    if (argv.size() < 5)
    {
        error(context);
        return;
    }
    bool error = false;
    std::string track_name = argv[1];
    std::string mode_name = (argv[2] == "t" || argv[2] == "tt"
        || argv[2] == "time-trial" || argv[2] == "timetrial" ?
        "time-trial" : "normal");
    std::string reverse_name = (argv[3] == "r" ||
        argv[3] == "rev" || argv[3] == "reverse" ? "reverse" :
        "normal");
    int laps_count = -1;
    if (!StringUtils::parseString<int>(argv[4], &laps_count))
        error = true;
    if (!error && laps_count < 0)
        error = true;
    if (error)
    {
        response = "Invalid lap count";
    }
    else
    {
        response = m_lobby->getRecord(track_name, mode_name, reverse_name, laps_count);
    }
#else
    response = "This command is not supported.";
#endif
    m_lobby->sendStringToPeer(response, context.m_peer);
} // process_record
// ========================================================================

void CommandManager::process_power(Context& context)
{
    auto& peer = context.m_peer;
    auto& argv = context.m_argv;
    if (peer->isAngryHost())
    {
        peer->setAngryHost(false);
        std::string msg = "You are now a normal player";
        m_lobby->sendStringToPeer(msg, peer);
        m_lobby->updatePlayerList();
        return;
    }
    std::string password = ServerConfig::m_power_password;
    if (password.empty() || argv.size() <= 1 || argv[1] != password)
    {
        std::string msg = "You need to provide the password to have the power";
        m_lobby->sendStringToPeer(msg, peer);
        return;
    }
    peer->setAngryHost(true);
    std::string msg = "Now you finally have the power!";
    m_lobby->sendStringToPeer(msg, peer);
    m_lobby->updatePlayerList();
} // process_power
// ========================================================================

void CommandManager::process_length(Context& context)
{
    auto& argv = context.m_argv;
    std::string msg;
    if (argv.size() < 2)
    {
        error(context);
        return;
    }
    if (argv[1] == "check")
    {
        if (m_lobby->m_default_lap_multiplier < 0 && m_lobby->m_fixed_lap < 0)
            msg = "Game length is currently chosen by players";
        else if (m_lobby->m_default_lap_multiplier > 0)
            msg = StringUtils::insertValues(
                "Game length is %f x default",
                m_lobby->m_default_lap_multiplier);
        else if (m_lobby->m_fixed_lap > 0)
            msg = StringUtils::insertValues(
                "Game length is %d", m_lobby->m_fixed_lap);
        else
            msg = StringUtils::insertValues(
                "An error: game length is both %f x default and %d",
                m_lobby->m_default_lap_multiplier, m_lobby->m_fixed_lap);
        m_lobby->sendStringToPeer(msg, context.m_peer);
        return;
    }
    if (argv[1] == "clear")
    {
        m_lobby->m_default_lap_multiplier = -1.0;
        m_lobby->m_fixed_lap = -1;
        msg = "Game length will be chosen by players";
        m_lobby->sendStringToAllPeers(msg);
        return;
    }
    double temp_double = -1.0;
    int temp_int = -1;
    if (argv[1] == "x" && argv.size() >= 3 &&
        StringUtils::parseString<double>(argv[2], &temp_double))
    {
        m_lobby->m_default_lap_multiplier = std::max<double>(0.0, temp_double);
        m_lobby->m_fixed_lap = -1;
        msg = StringUtils::insertValues(
            "Game length is now %f x default",
            m_lobby->m_default_lap_multiplier);
        m_lobby->sendStringToAllPeers(msg);
        return;
    }
    if (argv[1] == "=" && argv.size() >= 3 &&
        StringUtils::parseString<int>(argv[2], &temp_int))
    {
        m_lobby->m_fixed_lap = std::max<int>(0, temp_int);
        m_lobby->m_default_lap_multiplier = -1.0;
        msg = StringUtils::insertValues(
                "Game length is now %d", m_lobby->m_fixed_lap);
        m_lobby->sendStringToAllPeers(msg);
        return;
    }

    error(context);
} // process_length
// ========================================================================

void CommandManager::process_queue(Context& context)
{
    std::string msg;
    auto& argv = context.m_argv;
    if (argv.size() < 2)
    {
        error(context);
        return;
    }
    if (argv[1] == "show")
    {
        msg = "Queue:";
        for (const std::string& s: m_lobby->m_tracks_queue) {
            msg += " " + s;
        }
        m_lobby->sendStringToPeer(msg, context.m_peer);
        return;
    }
    if (argv[1] == "push" || argv[1] == "push_back")
    {
        if (argv.size() < 3)
        {
            error(context);
            return;
        }
        m_lobby->m_tracks_queue.push_back(argv[2]);
        std::string msg = "Pushed " + argv[2]
            + " to the back of queue, current queue size: "
            + std::to_string(m_lobby->m_tracks_queue.size());
        m_lobby->sendStringToPeer(msg, context.m_peer);
        m_lobby->updatePlayerList();
    }
    else if (argv[1] == "push_front")
    {
        if (argv.size() < 3)
        {
            error(context);
            return;
        }
        m_lobby->m_tracks_queue.push_front(argv[2]);
        std::string msg = "Pushed " + argv[2]
            + " to the front of queue, current queue size: "
            + std::to_string(m_lobby->m_tracks_queue.size());
        m_lobby->sendStringToPeer(msg, context.m_peer);
        m_lobby->updatePlayerList();
    }
    else if (argv[1] == "pop" || argv[1] == "pop_front")
    {
        msg = "";
        if (m_lobby->m_tracks_queue.empty()) {
            msg = "Queue was empty before.";
        }
        else
        {
            msg = "Popped " + m_lobby->m_tracks_queue.front()
                + " from the front of the queue,";
            m_lobby->m_tracks_queue.pop_front();
            msg += " current queue size: "
                + std::to_string(m_lobby->m_tracks_queue.size());
        }
        m_lobby->sendStringToPeer(msg, context.m_peer);
        m_lobby->updatePlayerList();
    }
    else if (argv[1] == "pop_back")
    {
        msg = "";
        if (m_lobby->m_tracks_queue.empty()) {
            msg = "Queue was empty before.";
        }
        else
        {
            msg = "Popped " + m_lobby->m_tracks_queue.back()
                + " from the back of the queue,";
            m_lobby->m_tracks_queue.pop_back();
            msg += " current queue size: "
                + std::to_string(m_lobby->m_tracks_queue.size());
        }
        m_lobby->sendStringToPeer(msg, context.m_peer);
        m_lobby->updatePlayerList();
    }
} // process_queue
// ========================================================================

void CommandManager::process_allowstart(Context& context)
{
    std::string msg;
    auto& argv = context.m_argv;
    if (argv.size() == 1 || !(argv[1] == "0" || argv[1] == "1"))
    {
        error(context);
        return;
    }
    if (argv[1] == "0")
    {
        m_lobby->m_allowed_to_start = false;
        msg = "Now starting a race is forbidden";
    }
    else
    {
        m_lobby->m_allowed_to_start = true;
        msg = "Now starting a race is allowed";
    }
    m_lobby->sendStringToPeer(msg, context.m_peer);
} // process_allowstart
// ========================================================================

void CommandManager::process_shuffle(Context& context)
{
    std::string msg;
    auto& argv = context.m_argv;
    if (argv.size() == 1 || !(argv[1] == "0" || argv[1] == "1"))
    {
        error(context);
        return;
    }
    if (argv[1] == "0")
    {
        m_lobby->m_shuffle_gp = false;
        msg = "Now the GP grid is sorted by score";
    } else {
        m_lobby->m_shuffle_gp = true;
        msg = "Now the GP grid is shuffled";
    }
    m_lobby->sendStringToPeer(msg, context.m_peer);
} // process_shuffle
// ========================================================================

void CommandManager::process_timeout(Context& context)
{
    std::string msg;
    int seconds;
    auto& argv = context.m_argv;
    if (argv.size() < 2 || !StringUtils::parseString(argv[1], &seconds) || seconds <= 0)
    {
        error(context);
        return;
    }
    m_lobby->m_timeout.store((int64_t)StkTime::getMonoTimeMs() +
            (int64_t)(seconds * 1000.0f));
    m_lobby->updatePlayerList();
    msg = "Successfully changed timeout";
    m_lobby->sendStringToPeer(msg, context.m_peer);
} // process_timeout
// ========================================================================

void CommandManager::process_team(Context& context)
{
    std::string msg;
    auto& argv = context.m_argv;
    if (argv.size() != 3)
    {
        error(context);
        return;
    }
    if (hasTypo(context.m_peer, context.m_voting, context.m_argv, context.m_cmd,
                2, m_stf_present_users, 3, false, true))
        return;
    std::string player = argv[2];
    m_lobby->setTemporaryTeam(player, argv[1]);

    m_lobby->updatePlayerList();
} // process_team
// ========================================================================

void CommandManager::process_resetteams(Context& context)
{
    std::string msg = "Teams are reset now";
    m_lobby->clearTemporaryTeams();
    m_lobby->sendStringToPeer(msg, context.m_peer);
    m_lobby->updatePlayerList();
} // process_resetteams
// ========================================================================

void CommandManager::process_randomteams(Context& context)
{
    auto& argv = context.m_argv;
    int players_number = 0;
    for (auto& peer : STKHost::get()->getPeers())
    {
        if (peer->alwaysSpectate())
            continue;
        players_number += peer->getPlayerProfiles().size();
        for (auto& profile : peer->getPlayerProfiles())
            profile->setTemporaryTeam(-1);
    }

    int teams_number = -1;
    if (argv.size() < 2 || !StringUtils::parseString(argv[1], &teams_number)
        || teams_number < 1 || teams_number > 6)
    {
        teams_number = (int)round(sqrt(players_number));
        if (teams_number > 6)
            teams_number = 6;
        if (players_number > 1 && teams_number <= 1)
            teams_number = 2;
    }

    std::string msg = StringUtils::insertValues(
            "Created %d teams for %d players", teams_number, players_number);
    std::vector<int> available_colors;
    std::vector<int> profile_colors;
    for (int i = 1; i <= 6; ++i)
        available_colors.push_back(i);

    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(available_colors.begin(), available_colors.end(), g);
    available_colors.resize(teams_number);

    for (int i = 0; i < players_number; ++i)
        profile_colors.push_back(available_colors[i % teams_number]);

    std::shuffle(profile_colors.begin(), profile_colors.end(), g);

    m_lobby->clearTemporaryTeams();
    for (auto& peer : STKHost::get()->getPeers())
    {
        if (peer->alwaysSpectate())
            continue;
        for (auto& profile : peer->getPlayerProfiles()) {
            std::string name = StringUtils::wideToUtf8(profile->getName());
            std::string color = m_lobby->m_team_default_names[profile_colors.back()];
            m_lobby->setTemporaryTeam(name, color);
            if (profile_colors.size() > 1) // prevent crash just in case
                profile_colors.pop_back();
        }
    }

    m_lobby->sendStringToPeer(msg, context.m_peer);
    m_lobby->updatePlayerList();
} // process_randomteams
// ========================================================================

void CommandManager::process_resetgp(Context& context)
{
    std::string msg = "GP is now reset";
    m_lobby->resetGrandPrix();
    m_lobby->sendStringToAllPeers(msg);
} // process_resetgp
// ========================================================================

void CommandManager::process_cat(Context& context)
{
    std::string msg;
    auto& argv = context.m_argv;
    if (argv[0] == "cat+")
    {
        if (argv.size() != 3)
        {
            error(context);
            return;
        }
        std::string category = argv[1];
        if (hasTypo(context.m_peer, context.m_voting, context.m_argv, context.m_cmd,
            2, m_stf_present_users, 3, false, true))
            return;
        std::string player = argv[2];
        m_lobby->m_player_categories[category].insert(player);
        m_lobby->m_categories_for_player[player].insert(category);
        m_lobby->updatePlayerList();
        return;
    }
    if (argv[0] == "cat-")
    {
        if (argv.size() != 3)
        {
            error(context);
            return;
        }
        std::string category = argv[1];
        std::string player = argv[2];
        if (hasTypo(context.m_peer, context.m_voting, context.m_argv, context.m_cmd,
            2, m_stf_present_users, 3, false, true))
            return;
        m_lobby->m_player_categories[category].erase(player);
        m_lobby->m_categories_for_player[player].erase(category);
        m_lobby->updatePlayerList();
        return;
    }
    if (argv[0] == "catshow")
    {
        int displayed;
        if (argv.size() != 3 || !StringUtils::parseString(argv[2], &displayed)
                || displayed < 0 || displayed > 1)
        {
            error(context);
            return;
        }
        std::string category = argv[1];
        if (displayed) {
            m_lobby->m_hidden_categories.erase(category);
        } else {
            m_lobby->m_hidden_categories.insert(category);
        }
        m_lobby->updatePlayerList();
        return;
    }
} // process_cat
// ========================================================================

void CommandManager::process_troll(Context& context)
{
    std::string msg;
    auto& argv = context.m_argv;
    if (argv.size() == 1 || !(argv[1] == "0" || argv[1] == "1"))
    {
        error(context);
        return;
    }
    if (argv[1] == "0")
    {
        m_lobby->m_troll_active = false;
        msg = "Trolls can stay";
    } else {
        m_lobby->m_troll_active = true;
        msg = "Trolls will be kicked";
    }
    m_lobby->sendStringToPeer(msg, context.m_peer);
} // process_troll
// ========================================================================

void CommandManager::process_hitmsg(Context& context)
{
    std::string msg;
    auto& argv = context.m_argv;
    if (argv.size() == 1 || !(argv[1] == "0" || argv[1] == "1"))
    {
        error(context);
        return;
    }
    if (argv[1] == "0")
    {
        m_lobby->m_show_teammate_hits = false;
        msg = "Teammate hits will not be sent";
    } else {
        m_lobby->m_show_teammate_hits = true;
        msg = "Teammate hits will be sent to all players";
    }
    m_lobby->sendStringToAllPeers(msg);
} // process_hitmsg
// ========================================================================

void CommandManager::process_teamhit(Context& context)
{
    std::string msg;
    auto& argv = context.m_argv;
    if (argv.size() == 1 || !(argv[1] == "0" || argv[1] == "1"))
    {
        error(context);
        return;
    }
    if (argv[1] == "0")
    {
        m_lobby->m_teammate_hit_mode = false;
        msg = "Teammate hits are not punished now";
    }
    else
    {
        m_lobby->m_teammate_hit_mode = true;
        msg = "Teammate hits are punished now";
    }
    m_lobby->sendStringToAllPeers(msg);
} // process_teamhit
// ========================================================================

void CommandManager::process_register(Context& context)
{
    auto& argv = context.m_argv;
    auto& peer = context.m_peer;
    int online_id = peer->getPlayerProfiles()[0]->getOnlineId();
    if (online_id <= 0)
    {
        std::string msg = "Please join with a valid online STK account.";
        m_lobby->sendStringToPeer(msg, peer);
        return;
    }
    std::string ans = "";
    for (unsigned i = 1; i < argv.size(); ++i)
    {
        if (i > 1)
            ans.push_back(' ');
        ans += argv[i];
    }
    std::string message_ok = "Your registration request is being processed";
    std::string message_wrong = "Sorry, an error occurred. Please try again.";
    if (m_lobby->writeOnePlayerReport(peer.get(), ServerConfig::m_register_table_name,
        ans))
        m_lobby->sendStringToPeer(message_ok, peer);
    else
        m_lobby->sendStringToPeer(message_wrong, peer);
} // process_register
// ========================================================================

#ifdef ENABLE_WEB_SUPPORT
void CommandManager::process_token(Context& context)
{
    auto& peer = context.m_peer;
    int online_id = peer->getPlayerProfiles()[0]->getOnlineId();
    if (online_id <= 0)
    {
        std::string msg = "Please join with a valid online STK account.";
        m_lobby->sendStringToPeer(msg, peer);
        return;
    }
    std::string username = StringUtils::wideToUtf8(
        peer->getPlayerProfiles()[0]->getName());
    std::string token = m_lobby->getToken();
    while (m_lobby->m_web_tokens.count(token))
        token = m_lobby->getToken();
    m_lobby->m_web_tokens.insert(token);
    std::string msg = "Your token is " + token;
#ifdef ENABLE_SQLITE3
    std::string tokens_table_name = ServerConfig::m_tokens_table;
    std::string query = StringUtils::insertValues(
        "INSERT INTO %s (username, token) "
        "VALUES (\"%s\", \"%s\");",
        tokens_table_name.c_str(), username.c_str(), token.c_str()
    );
    // TODO fix injection!!
    if (m_lobby->easySQLQuery(query))
        msg += "\nRetype it on the website to connect your STK account. ";
    else
        msg = "An error occurred, please try again.";
#else
    msg += "\nThough it is useless...";
#endif
    m_lobby->sendStringToPeer(msg, peer);
} // process_token
#endif
// ========================================================================

void CommandManager::process_muteall(Context& context)
{
    auto& argv = context.m_argv;
    auto& peer = context.m_peer;
    std::string peer_username = StringUtils::wideToUtf8(
        peer->getPlayerProfiles()[0]->getName());
    if (argv.size() >= 2 && argv[1] == "0")
    {
        m_lobby->m_tournament_mutealls.erase(peer_username);
    }
    else if (argv.size() >= 2 && argv[1] == "1")
    {
        m_lobby->m_tournament_mutealls.insert(peer_username);
    }
    else
    {
        if (m_lobby->m_tournament_mutealls.count(peer_username))
            m_lobby->m_tournament_mutealls.erase(peer_username);
        else
            m_lobby->m_tournament_mutealls.insert(peer_username);
    }
    std::string msg;
    if (m_lobby->m_tournament_mutealls.count(peer_username))
        msg = "You are now receiving messages only from players and referees";
    else
        msg = "You are now receiving messages from spectators too";
    m_lobby->sendStringToPeer(msg, peer);
} // process_muteall
// ========================================================================

void CommandManager::process_game(Context& context)
{
    auto& argv = context.m_argv;
    auto& peer = context.m_peer;
    std::string peer_username = StringUtils::wideToUtf8(
        peer->getPlayerProfiles()[0]->getName());
    int old_game = m_lobby->m_tournament_game;
    if (argv.size() < 2)
    {
        ++m_lobby->m_tournament_game;
        if (m_lobby->m_tournament_game == m_lobby->m_tournament_max_games)
            m_lobby->m_tournament_game = 0;
        m_lobby->m_fixed_lap = ServerConfig::m_fixed_lap_count;
    } else {
        if (!StringUtils::parseString(argv[1], &m_lobby->m_tournament_game)
            || m_lobby->m_tournament_game < 0
            || m_lobby->m_tournament_game >= m_lobby->m_tournament_max_games)
        {
            std::string msg = "Please specify a correct number. "
                "Format: /game [number 0.."
                + std::to_string(m_lobby->m_tournament_max_games - 1) + "] [length]";
            m_lobby->sendStringToPeer(msg, peer);
            return;
        }
        int length = 10;
        if (argv.size() >= 3)
        {
            bool ok = StringUtils::parseString(argv[2], &length);
            if (!ok || length <= 0)
            {
                error(context);
                return;
            }
        }
        m_lobby->m_fixed_lap = length;
    }
    if (m_lobby->tournamentColorsSwapped(m_lobby->m_tournament_game)
        ^ m_lobby->tournamentColorsSwapped(old_game))
        m_lobby->changeColors();
    if (m_lobby->tournamentGoalsLimit(m_lobby->m_tournament_game)
        ^ m_lobby->tournamentGoalsLimit(old_game))
        m_lobby->changeLimitForTournament(m_lobby->tournamentGoalsLimit(m_lobby->m_tournament_game));
    std::string msg = StringUtils::insertValues(
        "Ready to start game %d for %d ", m_lobby->m_tournament_game, m_lobby->m_fixed_lap)
        + (m_lobby->tournamentGoalsLimit(m_lobby->m_tournament_game) ? "goals" : "minutes");
    m_lobby->sendStringToAllPeers(msg);
    Log::info("CommandManager", "SoccerMatchLog: Game number changed from %d to %d",
        old_game, m_lobby->m_tournament_game);
} // process_game
// ========================================================================

void CommandManager::process_role(Context& context)
{
    auto& argv = context.m_argv;
    auto& peer = context.m_peer;
    std::string peer_username = StringUtils::wideToUtf8(
        peer->getPlayerProfiles()[0]->getName());
    if (argv.size() < 3)
    {
        error(context);
        return;
    }
    if (argv[1].length() > argv[2].length())
    {
        swap(argv[1], argv[2]);
    }
    if (hasTypo(context.m_peer, context.m_voting, context.m_argv, context.m_cmd,
        2, m_stf_present_users, 3, false, true))
        return;
    std::string role = argv[1];
    std::string username = argv[2];
    bool permanent = (argv.size() >= 4 &&
        (argv[3] == "p" || argv[3] == "permanent"));
    if (role.length() != 1)
    {
        error(context);
        return;
    }
    if (role[0] >= 'A' && role[0] <= 'Z')
        role[0] += 'a' - 'A';
    std::vector<std::string> changed_usernames;
    if (!username.empty())
    {
        if (username[0] == '#')
        {
            std::string category = username.substr(1);
            auto it = m_lobby->m_player_categories.find(category);
            if (it != m_lobby->m_player_categories.end())
            {
                for (const std::string& s: it->second)
                {
                    changed_usernames.push_back(s);
                }
            }
        }
        else
        {
            changed_usernames.push_back(username);
        }
    }
    for (const std::string& username: changed_usernames)
    {
        Log::info("CommandManager", "SoccerMatchLog: Role of %s changed to %s",
             username.c_str(), role.c_str());
        m_lobby->m_tournament_red_players.erase(username);
        m_lobby->m_tournament_blue_players.erase(username);
        m_lobby->m_tournament_referees.erase(username);
        if (permanent)
        {
            m_lobby->m_tournament_init_red.erase(username);
            m_lobby->m_tournament_init_blue.erase(username);
            m_lobby->m_tournament_init_ref.erase(username);
        }
        std::string role_changed = "The referee has updated your role - you are now %s";
        std::shared_ptr<STKPeer> player_peer = STKHost::get()->findPeerByName(
            StringUtils::utf8ToWide(username));
        std::vector<std::string> missing_assets;
        if (player_peer)
            missing_assets = m_lobby->getMissingTournamentAssets(player_peer);
        bool fail = false;
        switch (role[0])
        {
            case 'R':
            case 'r':
            {
                if (!missing_assets.empty())
                {
                    fail = true;
                    break;
                }
                if (m_lobby->tournamentColorsSwapped(m_lobby->m_tournament_game))
                {
                    m_lobby->m_tournament_blue_players.insert(username);
                    if (permanent)
                        m_lobby->m_tournament_init_blue.insert(username);
                }
                else
                {
                    m_lobby->m_tournament_red_players.insert(username);
                    if (permanent)
                        m_lobby->m_tournament_init_red.insert(username);
                }
                if (player_peer)
                {
                    role_changed = StringUtils::insertValues(role_changed, "red player");
                    player_peer->getPlayerProfiles()[0]->setTeam(KART_TEAM_RED);
                    m_lobby->sendStringToPeer(role_changed, player_peer);
                }
                break;
            }
            case 'B':
            case 'b':
            {
                if (!missing_assets.empty())
                {
                    fail = true;
                    break;
                }
                if (m_lobby->tournamentColorsSwapped(m_lobby->m_tournament_game))
                {
                    m_lobby->m_tournament_red_players.insert(username);
                    if (permanent)
                        m_lobby->m_tournament_init_red.insert(username);
                }
                else
                {
                    m_lobby->m_tournament_blue_players.insert(username);
                    if (permanent)
                        m_lobby->m_tournament_init_blue.insert(username);
                }
                if (player_peer)
                {
                    role_changed = StringUtils::insertValues(role_changed, "blue player");
                    player_peer->getPlayerProfiles()[0]->setTeam(KART_TEAM_BLUE);
                    m_lobby->sendStringToPeer(role_changed, player_peer);
                }
                break;
            }
            case 'J':
            case 'j':
            {
                if (!missing_assets.empty())
                {
                    fail = true;
                    break;
                }
                m_lobby->m_tournament_referees.insert(username);
                if (permanent)
                    m_lobby->m_tournament_init_ref.insert(username);
                if (player_peer)
                {
                    role_changed = StringUtils::insertValues(role_changed, "referee");
                    player_peer->getPlayerProfiles()[0]->setTeam(KART_TEAM_NONE);
                    m_lobby->sendStringToPeer(role_changed, player_peer);
                }
                break;
            }
            case 'S':
            case 's':
            {
                if (player_peer)
                {
                    role_changed = StringUtils::insertValues(role_changed, "spectator");
                    player_peer->getPlayerProfiles()[0]->setTeam(KART_TEAM_NONE);
                    m_lobby->sendStringToPeer(role_changed, player_peer);
                }
                break;
            }
        }
        std::string msg;
        if (!fail)
            msg = StringUtils::insertValues(
                "Successfully changed role to %s for %s", role, username);
        else
        {
            msg = StringUtils::insertValues(
                "Failed to change role to %s for %s - missing assets:", role, username);
            for (unsigned i = 0; i < missing_assets.size(); i++)
            {
                if (i)
                    msg.push_back(',');
                msg += " " + missing_assets[i];
            }
        }
        m_lobby->sendStringToPeer(msg, peer);
    }
    m_lobby->updatePlayerList();
} // process_role
// ========================================================================

void CommandManager::process_stop(Context& context)
{
    World* w = World::getWorld();
    if (!w)
        return;
    SoccerWorld *sw = dynamic_cast<SoccerWorld*>(w);
    sw->stop();
    std::string msg = "The game is stopped.";
    m_lobby->sendStringToAllPeers(msg);
    Log::info("CommandManager", "SoccerMatchLog: The game is stopped");
} // process_stop
// ========================================================================

void CommandManager::process_go(Context& context)
{
    World* w = World::getWorld();
    if (!w)
        return;
    SoccerWorld *sw = dynamic_cast<SoccerWorld*>(w);
    sw->resume();
    std::string msg = "The game is resumed.";
    m_lobby->sendStringToAllPeers(msg);
    Log::info("CommandManager", "SoccerMatchLog: The game is resumed");
} // process_go
// ========================================================================

void CommandManager::process_lobby(Context& context)
{
    World* w = World::getWorld();
    if (!w)
        return;
    SoccerWorld *sw = dynamic_cast<SoccerWorld*>(w);
    sw->allToLobby();
    std::string msg = "The game will be restarted.";
    m_lobby->sendStringToAllPeers(msg);
} // process_lobby
// ========================================================================

void CommandManager::process_init(Context& context)
{
    auto& argv = context.m_argv;
    auto& peer = context.m_peer;
    std::string peer_username = StringUtils::wideToUtf8(
        peer->getPlayerProfiles()[0]->getName());
    int red, blue;
    if (argv.size() < 3 ||
        !StringUtils::parseString<int>(argv[1], &red) ||
        !StringUtils::parseString<int>(argv[2], &blue))
    {
        error(context);
        return;
    }
    World* w = World::getWorld();
    if (!w)
    {
        std::string msg = "Please set the count when the karts "
            "are ready. Setting the initial count in lobby is "
            "not implemented yet, sorry.";
        m_lobby->sendStringToPeer(msg, peer);
        return;
    }
    SoccerWorld *sw = dynamic_cast<SoccerWorld*>(w);
    sw->setInitialCount(red, blue);
    sw->tellCount();
} // process_init
// ========================================================================

void CommandManager::process_mimiz(Context& context)
{
    auto& peer = context.m_peer;
    auto& argv = context.m_argv;
    auto& cmd = context.m_cmd;
    std::string msg;
    if (argv.size() == 1)
        msg = "please provide text";
    else
        msg = cmd.substr(argv[0].length() + 1);
    m_lobby->sendStringToPeer(msg, peer);
} // process_mimiz
// ========================================================================

void CommandManager::process_test(Context& context)
{
    auto& argv = context.m_argv;
    argv.resize(3, "");
    auto& peer = context.m_peer;
    if (context.m_voting)
    {
        vote(context, "test " + argv[1], argv[2]);
        return;
    }
    std::string username = "Vote";
    if (peer.get())
    {
        username = StringUtils::wideToUtf8(
            peer->getPlayerProfiles()[0]->getName());
    }
    std::string msg = username + ", " + argv[1] + ", " + argv[2];
    m_lobby->sendStringToAllPeers(msg);
} // process_test
// ========================================================================

void CommandManager::process_slots(Context& context)
{
    auto& argv = context.m_argv;
    bool fail = false;
    unsigned number = 0;
    if (argv.size() < 2 || !StringUtils::parseString<unsigned>(argv[1], &number))
        fail = true;
    else if (number <= 0 || (int)number > ServerConfig::m_server_max_players)
        fail = true;
    if (fail)
    {
        error(context);
        return;
    }
    if (context.m_voting)
    {
        vote(context, "slots", argv[1]);
        return;
    }
    m_lobby->m_current_max_players_in_game.store(number);
    m_lobby->updatePlayerList();
    std::string msg = "Number of playable slots is now " + argv[1];
    m_lobby->sendStringToAllPeers(msg);
} // process_slots
// ========================================================================

void CommandManager::process_time(Context& context)
{
    auto& peer = context.m_peer;
    std::string msg = "Server time: " + StkTime::getLogTime();
    m_lobby->sendStringToPeer(msg, peer);
} // process_time
// ========================================================================

void CommandManager::special(Context& context)
{
    // This function is used as a function for /vote and possibly several
    // other future special functions that are never executed "as usual"
    // but need to be displayed in /commands output. So, in fact, this
    // function is only a placeholder and should be never executed.
} // special
// ========================================================================

void CommandManager::addUser(std::string& s)
{
    m_users.insert(s);
    m_stf_present_users.add(s);
    update();
} // addUser
// ========================================================================

void CommandManager::deleteUser(std::string& s)
{
    auto it = m_users.find(s);
    if (it == m_users.end())
    {
        Log::error("CommandManager", "No user %s in user list!", s.c_str());
        return;
    }
    m_users.erase(it);
    m_stf_present_users.remove(s);
    update();
} // deleteUser
// ========================================================================

void CommandManager::restoreCmdByArgv(std::string& cmd, std::vector<std::string>& argv, char c, char d, char e, char f)
{
    cmd.clear();
    for (unsigned i = 0; i < argv.size(); ++i) {
        bool quoted = false;
        if (argv[i].find(c) != std::string::npos || argv[i].empty()) {
            quoted = true;
        }
        if (i > 0) {
            cmd.push_back(c);
        }
        if (quoted) {
            cmd.push_back(d);
        }
        for (unsigned j = 0; j < argv[i].size(); ++j) {
            if (argv[i][j] == d || argv[i][j] == e || argv[i][j] == f) {
                cmd.push_back(f);
            }
            cmd.push_back(argv[i][j]);
        }
        if (quoted) {
            cmd.push_back(e);
        }
    }
} // restoreCmdByArgv
// ========================================================================

bool CommandManager::hasTypo(std::shared_ptr<STKPeer> peer, bool voting,
    std::vector<std::string>& argv, std::string& cmd, int idx,
    SetTypoFixer& stf, int top, bool case_sensitive, bool allow_as_is)
{
    if (!peer.get()) // voted
        return false;
    std::string username = StringUtils::wideToUtf8(
        peer->getPlayerProfiles()[0]->getName());
    if (idx < m_user_correct_arguments[username])
        return false;
    auto closest_commands = stf.getClosest(argv[idx], top, case_sensitive);
    if (closest_commands.empty())
    {
        std::string msg = "Command " + cmd + " not found";
        m_lobby->sendStringToPeer(msg, peer);
        return true;
    }
    if (closest_commands[0].second != 0 || (closest_commands[0].second == 0
        && closest_commands.size() > 1 && closest_commands[1].second == 0))
    {
        m_user_command_replacements[username].clear();
        m_user_correct_arguments[username] = idx + 1;
        std::string initial_argument = argv[idx];
        std::string response = "";
        if (idx == 0)
            response += "There is no command \"" + argv[idx] + "\"";
        else
            response += "Argument \"" + argv[idx] + "\" may be invalid";

        m_user_saved_voting[username] = voting;

        if (allow_as_is)
        {
            response += ". Type /0 to continue, or choose a fix:";
            m_user_command_replacements[username].push_back(cmd);
        }
        else
        {
            response += ". Choose a fix or ignore:";
            m_user_command_replacements[username].push_back("");
        }
        for (unsigned i = 0; i < closest_commands.size(); ++i) {
            argv[idx] = closest_commands[i].first;
            CommandManager::restoreCmdByArgv(cmd, argv, ' ', '"', '"', '\\');
            m_user_command_replacements[username].push_back(cmd);
            response += "\ntype /" + std::to_string(i + 1) + " to choose \"" + argv[idx] + "\"";
        }
        argv[idx] = initial_argument;
        CommandManager::restoreCmdByArgv(cmd, argv, ' ', '"', '"', '\\');
        m_lobby->sendStringToPeer(response, peer);
        return true;
    }

    argv[idx] = closest_commands[0].first; // converts case or regex
    CommandManager::restoreCmdByArgv(cmd, argv, ' ', '"', '"', '\\');
    return false;

} // hasTypo
// ========================================================================

void CommandManager::onResetServer()
{
    update();
} // onResetServer
// ========================================================================

void CommandManager::onStartSelection()
{
    m_votables["start"].resetAllVotes();
    m_votables["config"].resetAllVotes();
    m_votables["gnu"].resetAllVotes();
    m_votables["slots"].resetAllVotes();
    update();
} // onStartSelection
// ========================================================================