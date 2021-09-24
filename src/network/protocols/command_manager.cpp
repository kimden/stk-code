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
// #include "config/user_config.hpp"
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

void CommandManager::initCommands()
{
    m_commands.emplace_back("commands",         &CommandManager::process_commands,   UP_EVERYONE,            CS_ALWAYS,                    "/commands", "everyone", "Prints the list of commands available to you. This list may differ depending on server settings and your permissions.");
    m_commands.emplace_back("replay",           &CommandManager::process_replay,     UP_SINGLE,              CS_ALWAYS,                    "/replay [0 | 1]", "hammers, singleplayers", "Toggles whether the replay of the race is recorded.");
    m_commands.emplace_back("start",            &CommandManager::process_start,      UP_EVERYONE,            CS_ALWAYS,                    "/start", "everyone", "Basically clicks on the green “Ready” button.");
    m_commands.emplace_back("config",           &CommandManager::process_config,     UP_CROWNED,             CS_ALWAYS,                    "/config <mode> <difficulty>", "crowns", "Changes game mode and difficulty if allowed.");
    m_commands.emplace_back("spectate",         &CommandManager::process_spectate,   UP_EVERYONE,            CS_ALWAYS,                    "/spectate [0 | 1]", "everyone", "Toggles autospectate mode.");
    m_commands.emplace_back("addons",           &CommandManager::process_addons,     UP_EVERYONE,            CS_ALWAYS,                    "/addons [type]", "everyone", "Lists addons installed for all players that can play, in random order. Limits the list to a certain addon type if specified.");
    m_commands.emplace_back("moreaddons",       &CommandManager::process_addons,     UP_EVERYONE,            CS_ALWAYS,                    "/moreaddons [type]", "everyone", "Lists top 5 addons that are missing for at least one player, sorted by the number of players that don’t have the addon ascending, random for equal number of players. Limits the list to a certain addon type if specified.");
    m_commands.emplace_back("listserveraddon",  &CommandManager::process_lsa,        UP_EVERYONE,            CS_ALWAYS,                    "/listserveraddon [-type] (substring)", "everyone", "Lists addons installed on the server that have a specified substring in their id. Limits the list to a certain addon type if specified.");
    m_commands.emplace_back("playerhasaddon",   &CommandManager::process_pha,        UP_EVERYONE,            CS_ALWAYS,                    "/playerhasaddon (addon) (username)", "everyone", "Checks whether a player has a certain addon.");
    m_commands.emplace_back("kick",             &CommandManager::process_kick,       UP_CROWNED | PE_VOTED,  CS_ALWAYS,                    "/kick (username)", "crowns if server allows, hammers; votable", "Kicks a player from the server.");
    m_commands.emplace_back("kickban",          &CommandManager::process_kick,       UP_HAMMER | PE_VOTED,   CS_ALWAYS,                    "/kickban (username)", "hammers; votable", "Kicks a player from the server and temporarily bans him.");
    m_commands.emplace_back("unban",            &CommandManager::process_unban,      UP_HAMMER,              CS_ALWAYS,                    "/unban (username)", "hammers", "Removes a temporary ban from a player.");
    m_commands.emplace_back("ban",              &CommandManager::process_ban,        UP_HAMMER,              CS_ALWAYS,                    "/ban (username)", "hammers", "Adds a temporary ban to a player without kicking.");
    m_commands.emplace_back("playeraddonscore", &CommandManager::process_pas,        UP_EVERYONE,            CS_ALWAYS,                    "/playeraddonscore (username)", "everyone", "Returns the number (not the percentage!) of addons of different types installed by a player.");
    m_commands.emplace_back("serverhasaddon",   &CommandManager::process_sha,        UP_EVERYONE,            CS_ALWAYS,                    "/serverhasaddon (addon)", "everyone", "Checks whether the server has a certain addon.");
    m_commands.emplace_back("mute",             &CommandManager::process_mute,       UP_EVERYONE,            CS_ALWAYS,                    "/mute (username)", "everyone", "Temporarily blocks player’s messages from reaching you (until the server restart).");
    m_commands.emplace_back("unmute",           &CommandManager::process_unmute,     UP_EVERYONE,            CS_ALWAYS,                    "/unmute (username)", "everyone", "Unblocks player’s messages from reaching you.");
    m_commands.emplace_back("listmute",         &CommandManager::process_listmute,   UP_EVERYONE,            CS_ALWAYS,                    "/listmute", "everyone", "Lists players whom you blocked using /mute command.");
    m_commands.emplace_back("moreinfo",         &CommandManager::process_text,       UP_EVERYONE,            CS_ALWAYS,                    "/moreinfo", "everyone", "Displays an additional server message.");
    m_commands.emplace_back("gnu",              &CommandManager::process_gnu,        UP_CROWNED,             CS_ALWAYS,                    "/gnu [kart]", "crowns", "Starts kart elimination with Gnu (or a specified kart).");
    m_commands.emplace_back("nognu",            &CommandManager::process_nognu,      UP_CROWNED,             CS_ALWAYS,                    "/nognu", "crowns", "Cancels kart elimination.");
    m_commands.emplace_back("tell",             &CommandManager::process_tell,       UP_EVERYONE,            CS_ALWAYS,                    "/tell (report contents)", "everyone", "Makes a report to the server owner (if the server has a database to store the reports).");
    m_commands.emplace_back("standings",        &CommandManager::process_standings,  UP_EVERYONE,            CS_ALWAYS,                    "/standings [gp | gnu]", "everyone", "Displays standings of grand prix or kart elimination, picks whatever of them is played now.");
    m_commands.emplace_back("teamchat",         &CommandManager::process_teamchat,   UP_EVERYONE,            CS_ALWAYS,                    "/teamchat", "everyone", "Limits your future messages to be sent only to your teammates.");
    m_commands.emplace_back("to",               &CommandManager::process_to,         UP_EVERYONE,            CS_ALWAYS,                    "/to (username1) ... (usernameN)", "everyone", "Limits your future messages to be sent only to specified usernames.");
    m_commands.emplace_back("public",           &CommandManager::process_public,     UP_EVERYONE,            CS_ALWAYS,                    "/public", "everyone", "Allows your future messages to be sent to everyone.");
    m_commands.emplace_back("record",           &CommandManager::process_record,     UP_EVERYONE,            CS_ALWAYS,                    "/record (track id) (mode) (direction) (laps)", "everyone", "Receives the server record for the race settings if there is any (and if the server has a database to store the records).");
    m_commands.emplace_back("power",            &CommandManager::process_power,      UP_EVERYONE,            CS_ALWAYS,                    "/power (password)", "everyone", "Enters the hammer mode if the password is correct.");
    m_commands.emplace_back("length",           &CommandManager::process_length,     UP_SINGLE,              CS_ALWAYS,                    "/length (x (float) | = (int) | check | clear)", "hammers, singleplayers", "Manipulates the length of the games, “= n” fixes the number of laps to n, “x k” sets it to k times default number of laps, “check” displays the setting, “clear” allows players to choose it freely.");
    m_commands.emplace_back("queue",            &CommandManager::process_queue,      UP_SINGLE,              CS_ALWAYS,                    "/queue (show | push[_front] (track) | pop[_back])", "hammers, singleplayers", "Manipulates the track queue aka the next played tracks. “show” displays it, “push” adds the track to the end of the queue, “pop” removes a track at the front. You can use “_back” and “_front” to specify on which end of the queue to add or remove the tracks.");
    m_commands.emplace_back("adminstart",       &CommandManager::process_adminstart, UP_HAMMER,              CS_ALWAYS,                    "/adminstart [0 | 1]", "hammers", "Toggles whether the game can be started on the server.");
    m_commands.emplace_back("shuffle",          &CommandManager::process_shuffle,    UP_HAMMER,              CS_ALWAYS,                    "/shuffle [0 | 1]", "hammers", "Toggles whether the Grand Prix grid is shuffled before each race (1), or it corresponds to the standings (0).");
    m_commands.emplace_back("timeout",          &CommandManager::process_timeout,    UP_HAMMER,              CS_ALWAYS,                    "/timeout [positive int x]", "hammers", "Sets the timeout in seconds, whatever it may mean.");
    m_commands.emplace_back("team",             &CommandManager::process_team,       UP_HAMMER,              CS_ALWAYS,                    "/team ([roygbp-]) (username)", "hammers", "Move a player to one of six teams denoted by square emojis (r - red, o - orange, y - yellow, g - green, b - blue, p – purple), or removes the player from the teams (“-”).");
    m_commands.emplace_back("cat+",             &CommandManager::process_cat,        UP_HAMMER,              CS_ALWAYS,                    "/cat+ (category) (username)", "hammers", "Adds the player to a certain category.");
    m_commands.emplace_back("cat-",             &CommandManager::process_cat,        UP_HAMMER,              CS_ALWAYS,                    "/cat- (category) (username)", "hammers", "Removes the player from a certain category.");
    m_commands.emplace_back("cat*",             &CommandManager::process_cat,        UP_HAMMER,              CS_ALWAYS,                    "/cat* (category) (0 | 1)", "hammers", "Toggles whether a category is displayed in the player list.");
    m_commands.emplace_back("troll",            &CommandManager::process_troll,      UP_HAMMER,              CS_ALWAYS,                    "/troll [0 | 1]", "hammers", "Toggles anti-troll system.");
    m_commands.emplace_back("hitmsg",           &CommandManager::process_hitmsg,     UP_HAMMER,              CS_ALWAYS,                    "/hitmsg [0 | 1]", "hammers", "Toggles whether the messages about teammate hits are displayed.");
    m_commands.emplace_back("teamhit",          &CommandManager::process_teamhit,    UP_HAMMER,              CS_ALWAYS,                    "/teamhit [0 | 1]", "hammers", "Toggles whether the teammate hits are punished ingame.");
    m_commands.emplace_back("version",          &CommandManager::process_text,       UP_EVERYONE,            CS_ALWAYS,                    "/version", "everyone", "Displays version.");
    m_commands.emplace_back("clear",            &CommandManager::process_text,       UP_EVERYONE,            CS_ALWAYS,                    "/clear", "everyone", "Puts newlines onto the screen so that the previous messages are not visible.");
    m_commands.emplace_back("register",         &CommandManager::process_register,   UP_EVERYONE,            CS_ALWAYS,                    "/register [info]", "everyone", "The command is used to register to an event (if the server has a database to store the registrations). Players can provide information while registering.");
#ifdef ENABLE_WEB_SUPPORT
    m_commands.emplace_back("token",            &CommandManager::process_token,      UP_EVERYONE,            CS_ALWAYS,                    "/token", "everyone", "Produces a token for a player and stores it in the database (if the server has a database to store tokens). Tokens may be used to authenticate players using their STK accounts.");
#endif
    m_commands.emplace_back("muteall",          &CommandManager::process_muteall,    UP_EVERYONE,            CS_SOCCER_TOURNAMENT,         "/muteall [0 | 1]", "everyone in soccer tournament mode", "Toggles whether a player wants to receive messages from anyone except acting players and referees in soccer tournament mode (this may be forced for acting player and referees using config).");
    m_commands.emplace_back("game",             &CommandManager::process_game,       UP_HAMMER,              CS_SOCCER_TOURNAMENT,         "/game [number] [length]", "soccer tournament referees", "Prepares the server for a certain game, with certain duration or number of goals.");
    m_commands.emplace_back("role",             &CommandManager::process_role,       UP_HAMMER,              CS_SOCCER_TOURNAMENT,         "/role ([rbjsRBJS]) (username | #Category)", "soccer tournament referees", "Assigns a role to a player. Roles include: “r” and “b” - acting players with red and blue colors, respectively; “j” - judge/referee, “s” – spectator. May be done simultaneously for a specified category. Fails if the player doesn’t satisfy the requirements for the role.");
    m_commands.emplace_back("stop",             &CommandManager::process_stop,       UP_HAMMER,              CS_SOCCER_TOURNAMENT,         "/stop", "soccer tournament referees", "Disables goal counting during the game.");
    m_commands.emplace_back("go",               &CommandManager::process_go,         UP_HAMMER,              CS_SOCCER_TOURNAMENT,         "/go", "soccer tournament referees", "Enables goal counting during the game.");
    m_commands.emplace_back("play",             &CommandManager::process_go,         UP_HAMMER,              CS_SOCCER_TOURNAMENT,         "/play", "soccer tournament referees", "Enables goal counting during the game.");
    m_commands.emplace_back("resume",           &CommandManager::process_go,         UP_HAMMER,              CS_SOCCER_TOURNAMENT,         "/resume", "soccer tournament referees", "Enables goal counting during the game.");
    m_commands.emplace_back("lobby",            &CommandManager::process_lobby,      UP_HAMMER,              CS_SOCCER_TOURNAMENT,         "/lobby", "soccer tournament referees", "Forces players to the lobby through the final screen, stoppng the game.");
    m_commands.emplace_back("init",             &CommandManager::process_init,       UP_HAMMER,              CS_SOCCER_TOURNAMENT,         "/init (red count) (blue count)", "soccer tournament referees", "Initializes the match score to start from (red count):(blue count) instead of 0:0.");
    m_commands.emplace_back("vote",             &CommandManager::special,            UP_EVERYONE,            CS_ALWAYS,                    "/vote (command with arguments, without “/”)", "everyone", "Casts a vote for the provided command, if it can be voted. Only useful for those players who can enforce their command but opt to vote instead of forcing; for those players who cannot invoke the command alone, /(command) is equivalent to /vote (command).");
    m_commands.emplace_back("mimiz",            &CommandManager::process_mimiz,      UP_EVERYONE,            CS_ALWAYS,                    "/mimiz [text]", "everyone", "A joke command.");
    m_commands.emplace_back("test",             &CommandManager::process_test,       UP_EVERYONE | PE_VOTED, CS_ALWAYS,                    "/test [poll name] [option name]", "everyone; votable", "A test command.");
    m_commands.emplace_back("help",             &CommandManager::process_help,       UP_EVERYONE,            CS_ALWAYS,                    "/help (command name)", "everyone", "Gives description of a given command.");

    addTextResponse("moreinfo", StringUtils::wideToUtf8(m_lobby->m_help_message));
    addTextResponse("version", "1.3-rc1 k 210fff beta");
    addTextResponse("clear", std::string(30, '\n'));

    std::sort(m_commands.begin(), m_commands.end(), [](const Command& a, const Command& b) -> bool {
        return a.m_name < b.m_name;
    });
    for (auto& command: m_commands)
        m_name_to_command[command.m_name] = command;

    m_votables.emplace("replay", 1.0);
    m_votables.emplace("start", 0.81);
    m_votables.emplace("config", 0.6);
    m_votables.emplace("kick", 0.81);
    m_votables.emplace("kickban", 0.81);

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
    std::string cmd;
    data.decodeString(&cmd);
    auto argv = StringUtils::splitQuoted(cmd, ' ', '"', '\\');
    if (argv.size() == 0)
        return;

    int permissions = m_lobby->getPermissions(peer);
    bool found_command = false;
    bool voting = false;
    std::string action = "invoke";
    if (argv[0] == "vote")
    {
        if (argv.size() == 1 || argv[1] == "vote")
        {
            std::string msg = "Usage: /vote (a command with arguments)";
            m_lobby->sendStringToPeer(msg, peer);
            return;
        }
        std::reverse(cmd.begin(), cmd.end());
        cmd.resize((int)cmd.size() - 4);
        while (!cmd.empty() && cmd.back() == ' ')
            cmd.pop_back();
        std::reverse(cmd.begin(), cmd.end());
        std::reverse(argv.begin(), argv.end());
        argv.pop_back();
        std::reverse(argv.begin(), argv.end());
        voting = true;
        action = "vote for";
    }

    auto command_iterator = m_name_to_command.find(argv[0]);
    found_command = (command_iterator != m_name_to_command.end());

    if (found_command)
    {
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

        Context context(event, peer, argv, cmd, permissions, voting);
        (this->*command.m_action)(context);

        auto it = m_votables.find(argv[0]);
        if (it != m_votables.end() && it->second.needsCheck())
        {
            auto response = it->second.process(m_users);
            int count = response.first;
            std::string username = StringUtils::wideToUtf8(
                peer->getPlayerProfiles()[0]->getName());
            std::string msg = username + " voted \"" + cmd + "\", there are " + std::to_string(count) + " such votes";
            m_lobby->sendStringToAllPeers(msg);
            auto res = response.second;
            if (!res.empty())
            {
                for (auto& p: res)
                {
                    std::string new_cmd = p.first + " " + p.second;
                    std::string msg = "Command \"" + new_cmd + "\" has been successfully voted";
                    m_lobby->sendStringToAllPeers(msg);
                    auto new_argv = StringUtils::splitQuoted(cmd, ' ', '"', '\\');
                    Context new_context(event, std::shared_ptr<STKPeer>(nullptr), new_argv, new_cmd, UP_EVERYONE, false);
                    (this->*command.m_action)(new_context);
                }
            }
        }
        return;
    }

    // TODO: process commands with typos
    std::string msg = "Command " + argv[0] + " not found";
    m_lobby->sendStringToPeer(msg, peer);

    // TODO: add votedness

} // handleCommand
// ========================================================================

int CommandManager::getCurrentScope()
{
    int mask = CS_ALWAYS;
    if (ServerConfig::m_soccer_tournament)
        mask |= CS_SOCCER_TOURNAMENT;
    return mask;
} // getCurrentScope
// ========================================================================

bool CommandManager::isAvailable(const Command& c)
{
    return (getCurrentScope() & c.m_scope) != 0;
} // getCurrentScope
// ========================================================================

void CommandManager::vote(Context& context, std::string category, std::string value)
{
    auto& cmd = context.m_cmd;
    auto& peer = context.m_peer;
    auto& argv = context.m_argv;
    std::string username = StringUtils::wideToUtf8(
        peer->getPlayerProfiles()[0]->getName());
    auto& votable = m_votables[argv[0]];
    votable.castVote(username, category, value);
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
                std::string msg = "Command \"" + new_cmd + "\" has been successfully voted";
                m_lobby->sendStringToAllPeers(msg);
                auto new_argv = StringUtils::splitQuoted(new_cmd, ' ', '"', '\\');
                auto& command = m_name_to_command[new_argv[0]];
                // We don't know the event though it is only needed in
                // ServerLobby::startSelection where it is nullptr when they vote
                Context new_context(nullptr, std::shared_ptr<STKPeer>(nullptr), new_argv, new_cmd, UP_EVERYONE, false);
                (this->*command.m_action)(new_context);
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

void CommandManager::process_help(Context& context)
{
    auto& argv = context.m_argv;
    if (argv.size() < 2)
    {
        error(context);
        return;
    }
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
        if (m_lobby->m_state.load() != m_lobby->WAITING_FOR_START_GAME)
        {
            std::string username = StringUtils::wideToUtf8(
                peer->getPlayerProfiles()[0]->getName());
            if (m_lobby->m_default_always_spectate_peers.count(peer.get()))
                argv.push_back("0");
            else
                argv.push_back("1");
        }
        else
        {
            if (peer->alwaysSpectate())
                argv.push_back("0");
            else
                argv.push_back("1");
        }
    }

    if (argv.size() != 2 || (argv[1] != "0" && argv[1] != "1"))
    {
        error(context);
        return;
    }

    if (m_lobby->m_state.load() != m_lobby->WAITING_FOR_START_GAME)
    {
        std::string username = StringUtils::wideToUtf8(
            peer->getPlayerProfiles()[0]->getName());
        if (argv[1] == "1")
            m_lobby->m_default_always_spectate_peers.insert(peer.get());
        else
            m_lobby->m_default_always_spectate_peers.erase(peer.get());
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
    m_lobby->updatePlayerList();
} // process_spectate
// ========================================================================

void CommandManager::process_addons(Context& context)
{
    auto& argv = context.m_argv;
    bool more = (argv[0] == "moreaddons");
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
        if (!peer || !peer->isValidated() || peer->isWaitingForGame() || !m_lobby->canRace(peer))
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
        if (result.size() > NEXT_ADDONS)
            result.resize(NEXT_ADDONS);
        if (!more)
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
                    response += "\n" + result[i].first + ", missing for "
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
        vote(context, "kick " + player_name, "");
        return;
    }
    if (!peer->isAngryHost() && !ServerConfig::m_kicks_allowed)
    {
        std::string msg = "Kicking players is not allowed on this server";
        m_lobby->sendStringToPeer(msg, peer);
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
    if (m_lobby->m_kart_elimination.isEnabled())
    {
        std::string msg = "Gnu Elimination mode was already enabled!";
        m_lobby->sendStringToPeer(msg, context.m_peer);
        return;
    }
    if (RaceManager::get()->getMinorMode() != RaceManager::MINOR_MODE_NORMAL_RACE &&
        RaceManager::get()->getMinorMode() != RaceManager::MINOR_MODE_TIME_TRIAL)
    {
        std::string msg = "Gnu Elimination is available only with racing modes";
        m_lobby->sendStringToPeer(msg, context.m_peer);
        return;
    }
    auto& argv = context.m_argv;
    if (argv.size() > 1 && m_lobby->m_available_kts.first.count(argv[1]) > 0)
    {
        m_lobby->m_kart_elimination.enable(argv[1]);
    } else {
        m_lobby->m_kart_elimination.enable("gnu");
    }
    std::string msg = m_lobby->m_kart_elimination.getStartingMessage();
    m_lobby->sendStringToAllPeers(msg);
} // process_gnu
// ========================================================================

void CommandManager::process_nognu(Context& context)
{
    if (!m_lobby->m_kart_elimination.isEnabled())
    {
        std::string msg = "Gnu Elimination mode was already off!";
        m_lobby->sendStringToPeer(msg, context.m_peer);
        return;
    }

    m_lobby->m_kart_elimination.disable();
    std::string msg = "Gnu Elimination is now off";
    m_lobby->sendStringToAllPeers(msg);
} // process_nognu
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
    if (argv.size() == 1)
    {
        if (m_lobby->m_game_setup->isGrandPrix())
            argv.push_back("gp");
        else
            argv.push_back("gnu");
    }
    if (argv[1] == "gp")
        msg = m_lobby->getGrandPrixStandings();
    else if (argv[1] == "gnu")
        msg = m_lobby->m_kart_elimination.getStandings();
    else
    {
        error(context);
        return;
    }
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
    }
} // process_queue
// ========================================================================

void CommandManager::process_adminstart(Context& context)
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
} // process_adminstart
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
    auto it = m_lobby->m_team_name_to_index.find(argv[1]);
    int index = (it == m_lobby->m_team_name_to_index.end() ? 0 : it->second);
    std::string player;
    auto wide_player_name = StringUtils::utf8ToWide(argv[2]);
    std::shared_ptr<STKPeer> player_peer = STKHost::get()->findPeerByWildcard(
        wide_player_name, player);
    // if player not found
    if (!player_peer)
    {
        // don't use if wildcard
        if (wide_player_name.find("*") != -1 || wide_player_name.find("?") != -1)
        {
            msg = "Player not found or not unique";
            m_lobby->sendStringToPeer(msg, context.m_peer);
            return;
        }
        else
        {
            // if no wildcard, reset player name to use for absent players
            player = argv[2];
        }
    }
    for (const auto& pair: m_lobby->m_team_name_to_index)
    {
        if (pair.second < 0)
        {
            if (pair.second == -index)
            {
                m_lobby->m_player_categories[pair.first].insert(player);
                m_lobby->m_categories_for_player[player].insert(pair.first);
            }
            else
            {
                m_lobby->m_player_categories[pair.first].erase(player);
                m_lobby->m_categories_for_player[player].erase(pair.first);
            }
        }
    }
    index = abs(index);
    m_lobby->m_team_for_player[player] = index;
    wide_player_name = StringUtils::utf8ToWide(player);
    if (player_peer)
    {
        for (auto& profile : player_peer.get()->getPlayerProfiles())
        {
            if (profile->getName() == wide_player_name)
            {
                profile->setTemporaryTeam(index - 1);
                break;
            }
        }
    }
    m_lobby->updatePlayerList();
} // process_team
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
        m_lobby->m_player_categories[category].erase(player);
        m_lobby->m_categories_for_player[player].erase(category);
        m_lobby->updatePlayerList();
        return;
    }
    if (argv[0] == "cat*")
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
    std::string role = argv[1];
    std::string username = argv[2];
    bool permanent = (argv.size() >= 4 &&
        (argv[3] == "p" || argv[3] == "permanent"));
    if (role.length() != 1)
        std::swap(role, username);
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
    update();
} // deleteUser
// ========================================================================