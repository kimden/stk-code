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
#include "tracks/track.hpp"
#include "tracks/track_manager.hpp"
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
#include <utility>

std::vector<std::string> CommandManager::QUEUE_NAMES = {
    "", "mqueue", "mcyclic", "mboth",
    "kqueue", "qregular", "", "",
    "kcyclic", "", "qcyclic", "",
    "kboth", "", "", "qboth"
};

// ========================================================================
EnumExtendedReader CommandManager::mode_scope_reader({
    {"MS_DEFAULT", MS_DEFAULT},
    {"MS_SOCCER_TOURNAMENT", MS_SOCCER_TOURNAMENT}
});
EnumExtendedReader CommandManager::state_scope_reader({
    {"SS_LOBBY", SS_LOBBY},
    {"SS_INGAME", SS_INGAME},
    {"SS_ALWAYS", SS_ALWAYS}
});
EnumExtendedReader CommandManager::permission_reader({
    {"PE_NONE", PE_NONE},
    {"PE_SPECTATOR", PE_SPECTATOR},
    {"PE_USUAL", PE_USUAL},
    {"PE_CROWNED", PE_CROWNED},
    {"PE_SINGLE", PE_SINGLE},
    {"PE_HAMMER", PE_HAMMER},
    {"PE_CONSOLE", PE_CONSOLE},
    {"PE_VOTED_SPECTATOR", PE_VOTED_SPECTATOR},
    {"PE_VOTED_NORMAL", PE_VOTED_NORMAL},
    {"PE_VOTED", PE_VOTED},
    {"UP_CONSOLE", UP_CONSOLE},
    {"UP_HAMMER", UP_HAMMER},
    {"UP_SINGLE", UP_SINGLE},
    {"UP_CROWNED", UP_CROWNED},
    {"UP_NORMAL", UP_NORMAL},
    {"UP_EVERYONE", UP_EVERYONE}
});
// ========================================================================


CommandManager::FileResource::FileResource(std::string file_name, uint64_t interval)
{
    m_file_name = std::move(file_name);
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
    std::string answer;
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

CommandManager::AuthResource::AuthResource(std::string secret, std::string server,
    std::string link_format):
    m_secret(secret), m_server(server), m_link_format(link_format)
{

} // AuthResource::AuthResource
// ========================================================================

std::string CommandManager::AuthResource::get(const std::string& username) const
{
#ifdef ENABLE_CRYPTO_OPENSSL
    std::string header = "{\"alg\":\"HS256\",\"typ\":\"JWT\"}";
    uint64_t timestamp = StkTime::getTimeSinceEpoch();
    uint64_t period = 3600;
    std::string payload = "{\"name\":\"" + username + "\",";
    payload += "\"iat\":\"" + std::to_string(timestamp) + "\",";
    payload += "\"exp\":\"" + std::to_string(timestamp + period) + "\",";
    payload += "\"serv\":\"" + m_server + "\"}";
    header = Crypto::base64url(StringUtils::toUInt8Vector(header));
    payload = Crypto::base64url(StringUtils::toUInt8Vector(payload));
    std::string message = header + "." + payload;
    std::string signature = Crypto::base64url(Crypto::hmac_sha256_array(m_secret, message));
    std::string token = message + "." + signature;
    std::string response = StringUtils::insertValues(m_link_format, token.c_str());
    return response;
#else
    return "This command is currently only supported for OpenSSL";
#endif
} // AuthResource::get
// ========================================================================

CommandManager::Command::Command(std::string name,
                                 void (CommandManager::*f)(Context& context),
                                 int permissions,
                                 int mode_scope,
                                 int state_scope):
        m_name(name), m_action(f), m_permissions(permissions),
        m_mode_scope(mode_scope), m_state_scope(state_scope),
        m_omit_name(false)
{
} // Command::Command(5)
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

    std::function<void(const XMLNode* current, std::shared_ptr<Command> command)> dfs =
            [&](const XMLNode* current, const std::shared_ptr<Command>& command) {
        for (unsigned int i = 0; i < current->getNumNodes(); i++)
        {
            const XMLNode *node = current->getNode(i);
            std::string node_name = node->getName();
            // here the commands go
            std::string name = "";
            std::string text = ""; // for text-command
            std::string file = ""; // for file-command
            uint64_t interval = 0; // for file-command
            std::string usage = "";
            std::string permissions_s = "UP_EVERYONE";
            std::string mode_scope_s = "MS_DEFAULT";
            std::string state_scope_s = "SS_ALWAYS";
            bool omit_name = false;
            int permissions;
            int mode_scope;
            int state_scope;
            std::string permissions_str = "";
            std::string description = "";
            std::string aliases = "";
            std::string secret = ""; // for auth-command
            std::string link_format = ""; // for auth-command
            std::string server = ""; // for auth-command
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
            node->get("permissions", &permissions_s);
            permissions = CommandManager::permission_reader.parse(permissions_s);
            node->get("mode-scope", &mode_scope_s);
            mode_scope = CommandManager::mode_scope_reader.parse(mode_scope_s);
            node->get("state-scope", &state_scope_s);
            state_scope = CommandManager::state_scope_reader.parse(state_scope_s);
            node->get("permissions-verbose", &permissions_str);
            node->get("description", &description);
            node->get("omit-name", &omit_name);
            node->get("aliases", &aliases);
            std::vector<std::string> aliases_split = StringUtils::split(aliases, ' ');

            std::shared_ptr<Command> c;
            if (node_name == "command")
            {
                c = addChildCommand(command, name, &CommandManager::special, permissions, mode_scope, state_scope);
            }
            else if (node_name == "text-command")
            {
                c = addChildCommand(command, name, &CommandManager::process_text, permissions, mode_scope, state_scope);
                node->get("text", &text);
                addTextResponse(c->getFullName(), text);
            }
            else if (node_name == "file-command")
            {
                c = addChildCommand(command, name, &CommandManager::process_file, permissions, mode_scope, state_scope);
                node->get("file", &file);
                node->get("interval", &interval);
                addFileResource(c->getFullName(), file, interval);
            }
            else if (node_name == "auth-command")
            {
                c = addChildCommand(command, name, &CommandManager::process_auth, permissions, mode_scope, state_scope);
                node->get("secret", &secret);
                node->get("server", &server);
                node->get("link-format", &link_format);
                addAuthResource(name, secret, server, link_format);
            }
            c->m_description = CommandDescription(usage, permissions_str, description);
            m_all_commands.emplace_back(c);
            m_full_name_to_command[c->getFullName()] = std::weak_ptr<Command>(c);
            c->m_omit_name = omit_name;
            command->m_stf_subcommand_names.add(name);
            for (const std::string& alias_name: aliases_split)
            {
                command->m_stf_subcommand_names.add(alias_name, name);
                command->m_name_to_subcommand[alias_name] = command->m_name_to_subcommand[name];
            }
            dfs(node, c);
        }
    };
    dfs(root, m_root_command);
    delete root;
} // initCommandsInfo
// ========================================================================

void CommandManager::initCommands()
{
    using CM = CommandManager;
    auto& mp = m_full_name_to_command;
    m_root_command = std::make_shared<Command>("", &CM::special);

    initCommandsInfo();

    auto applyFunctionIfPossible = [&](std::string&& name, void (CommandManager::*f)(Context& context)) {
        if (mp.count(name) == 0)
            return;
        std::shared_ptr<Command> command = mp[name].lock();
        if (!command) {
            return;
        }
        command->changeFunction(f);
    };
    // special permissions according to ServerConfig options
    std::shared_ptr<Command> kick_command = mp["kick"].lock();
    if (kick_command) {
        if (ServerConfig::m_kicks_allowed)
            kick_command->m_permissions |= PE_CROWNED;
        else
            kick_command->m_permissions &= ~PE_CROWNED;
    }

    applyFunctionIfPossible("commands", &CM::process_commands);
    applyFunctionIfPossible("replay", &CM::process_replay);
    applyFunctionIfPossible("start", &CM::process_start);
    applyFunctionIfPossible("config", &CM::process_config);
    applyFunctionIfPossible("config =", &CM::process_config_assign);
    applyFunctionIfPossible("spectate", &CM::process_spectate);
    applyFunctionIfPossible("addons", &CM::process_addons);
    applyFunctionIfPossible("moreaddons", &CM::process_addons);
    applyFunctionIfPossible("getaddons", &CM::process_addons);
    applyFunctionIfPossible("checkaddon", &CM::process_checkaddon);
    applyFunctionIfPossible("id", &CM::process_id);
    applyFunctionIfPossible("listserveraddon", &CM::process_lsa);
    applyFunctionIfPossible("playerhasaddon", &CM::process_pha);
    applyFunctionIfPossible("kick", &CM::process_kick); // todo Actual permissions are (kick_permissions)
    applyFunctionIfPossible("kickban", &CM::process_kick);
    applyFunctionIfPossible("unban", &CM::process_unban);
    applyFunctionIfPossible("ban", &CM::process_ban);
    applyFunctionIfPossible("playeraddonscore", &CM::process_pas);
    applyFunctionIfPossible("everypas", &CM::process_everypas);
    applyFunctionIfPossible("serverhasaddon", &CM::process_sha);
    applyFunctionIfPossible("mute", &CM::process_mute);
    applyFunctionIfPossible("unmute", &CM::process_unmute);
    applyFunctionIfPossible("listmute", &CM::process_listmute);
    applyFunctionIfPossible("description", &CM::process_text);
    applyFunctionIfPossible("moreinfo", &CM::process_text);
    applyFunctionIfPossible("gnu", &CM::process_gnu);
    applyFunctionIfPossible("nognu", &CM::process_gnu);
    applyFunctionIfPossible("tell", &CM::process_tell);
    applyFunctionIfPossible("standings", &CM::process_standings);
    applyFunctionIfPossible("teamchat", &CM::process_teamchat);
    applyFunctionIfPossible("to", &CM::process_to);
    applyFunctionIfPossible("public", &CM::process_public);
    applyFunctionIfPossible("record", &CM::process_record);
    applyFunctionIfPossible("power", &CM::process_power);
    applyFunctionIfPossible("length", &CM::process_length);
    applyFunctionIfPossible("length clear", &CM::process_length_clear);
    applyFunctionIfPossible("length =", &CM::process_length_fixed);
    applyFunctionIfPossible("length x", &CM::process_length_multi);
    applyFunctionIfPossible("direction", &CM::process_direction);
    applyFunctionIfPossible("direction =", &CM::process_direction_assign);
    for (int i = 0; i < QUEUE_NAMES.size(); i++)
    {
        const std::string& name = QUEUE_NAMES[i];
        if (name.empty())
            continue;
        applyFunctionIfPossible(name + "", &CM::process_queue);
        applyFunctionIfPossible(name + " pop_front", &CM::process_queue_pop);
        applyFunctionIfPossible(name + " clear", &CM::process_queue_clear);
        applyFunctionIfPossible(name + " shuffle", &CM::process_queue_shuffle);
        applyFunctionIfPossible(name + " pop", &CM::process_queue_pop);
        applyFunctionIfPossible(name + " pop_back", &CM::process_queue_pop);
        // No pushing into kart and track queues at the same time
        if (((i & QM_ALL_KART_QUEUES) > 0) && ((i & QM_ALL_MAP_QUEUES) > 0))
            continue;
        applyFunctionIfPossible(name + " push", &CM::process_queue_push);
        applyFunctionIfPossible(name + " push_back", &CM::process_queue_push);
        applyFunctionIfPossible(name + " push_front", &CM::process_queue_push);
    }
    applyFunctionIfPossible("allowstart", &CM::process_allowstart);
    applyFunctionIfPossible("allowstart =", &CM::process_allowstart_assign);
    applyFunctionIfPossible("shuffle", &CM::process_shuffle);
    applyFunctionIfPossible("shuffle =", &CM::process_shuffle_assign);
    applyFunctionIfPossible("timeout", &CM::process_timeout);
    applyFunctionIfPossible("team", &CM::process_team);
    applyFunctionIfPossible("swapteams", &CM::process_swapteams);
    applyFunctionIfPossible("resetteams", &CM::process_resetteams);
    applyFunctionIfPossible("randomteams", &CM::process_randomteams);
    applyFunctionIfPossible("resetgp", &CM::process_resetgp);
    applyFunctionIfPossible("cat+", &CM::process_cat);
    applyFunctionIfPossible("cat-", &CM::process_cat);
    applyFunctionIfPossible("catshow", &CM::process_cat);
    applyFunctionIfPossible("troll", &CM::process_troll);
    applyFunctionIfPossible("troll =", &CM::process_troll_assign);
    applyFunctionIfPossible("hitmsg", &CM::process_hitmsg);
    applyFunctionIfPossible("hitmsg =", &CM::process_hitmsg_assign);
    applyFunctionIfPossible("teamhit", &CM::process_teamhit);
    applyFunctionIfPossible("teamhit =", &CM::process_teamhit_assign);
    applyFunctionIfPossible("scoring", &CM::process_scoring);
    applyFunctionIfPossible("scoring =", &CM::process_scoring_assign);
    applyFunctionIfPossible("version", &CM::process_text);
    applyFunctionIfPossible("clear", &CM::process_text);
    applyFunctionIfPossible("register", &CM::process_register);
#ifdef ENABLE_WEB_SUPPORT
    applyFunctionIfPossible("token", &CM::process_token);
#endif
    applyFunctionIfPossible("muteall", &CM::process_muteall);
    applyFunctionIfPossible("game", &CM::process_game);
    applyFunctionIfPossible("role", &CM::process_role);
    applyFunctionIfPossible("stop", &CM::process_stop);
    applyFunctionIfPossible("go", &CM::process_go);
    applyFunctionIfPossible("play", &CM::process_go);
    applyFunctionIfPossible("resume", &CM::process_go);
    applyFunctionIfPossible("lobby", &CM::process_lobby);
    applyFunctionIfPossible("init", &CM::process_init);
    applyFunctionIfPossible("vote", &CM::special);
    applyFunctionIfPossible("mimiz", &CM::process_mimiz);
    applyFunctionIfPossible("test", &CM::process_test);
    applyFunctionIfPossible("test test2", &CM::process_test);
    applyFunctionIfPossible("test test3", &CM::process_test);
    applyFunctionIfPossible("help", &CM::process_help);
// applyFunctionIfPossible("1", &CM::special);
// applyFunctionIfPossible("2", &CM::special);
// applyFunctionIfPossible("3", &CM::special);
// applyFunctionIfPossible("4", &CM::special);
// applyFunctionIfPossible("5", &CM::special);
    applyFunctionIfPossible("slots", &CM::process_slots);
    applyFunctionIfPossible("slots =", &CM::process_slots_assign);
    applyFunctionIfPossible("time", &CM::process_time);
    applyFunctionIfPossible("result", &CM::process_result);
    applyFunctionIfPossible("preserve", &CM::process_preserve);
    applyFunctionIfPossible("preserve =", &CM::process_preserve_assign);
    applyFunctionIfPossible("history", &CM::process_history);
    applyFunctionIfPossible("history =", &CM::process_history_assign);

    applyFunctionIfPossible("addondownloadprogress", &CM::special);
    applyFunctionIfPossible("stopaddondownload", &CM::special);
    applyFunctionIfPossible("installaddon", &CM::special);
    applyFunctionIfPossible("uninstalladdon", &CM::special);
    applyFunctionIfPossible("music", &CM::special);
    applyFunctionIfPossible("addonrevision", &CM::special);
    applyFunctionIfPossible("liststkaddon", &CM::special);
    applyFunctionIfPossible("listlocaladdon", &CM::special);

    addTextResponse("description", StringUtils::wideToUtf8(m_lobby->getGameSetup()->readOrLoadFromFile
            ((std::string)ServerConfig::m_motd)));
    addTextResponse("moreinfo", StringUtils::wideToUtf8(m_lobby->m_help_message));
    std::string version = "1.3 k 210fff beta";
#ifdef GIT_VERSION
    version = std::string(GIT_VERSION);
    #ifdef GIT_BRANCH
        version += ", branch " + std::string(GIT_BRANCH);
    #endif
#endif
    addTextResponse("version", version);
    addTextResponse("clear", std::string(30, '\n'));

    // m_votables.emplace("replay", 1.0);
    m_votables.emplace("start", 0.81);
    m_votables.emplace("config", 0.6);
    m_votables.emplace("kick", 0.81);
    m_votables.emplace("kickban", 0.81);
    m_votables.emplace("gnu", 0.81);
    m_votables.emplace("slots", CommandVoting::DEFAULT_THRESHOLD);
    m_votables["gnu"].setCustomThreshold("gnu kart", 1.1);
    m_votables["config"].setMerge(7);
} // initCommands
// ========================================================================

void CommandManager::initAssets()
{
    auto all_t = track_manager->getAllTrackIdentifiers();
    std::map<std::string, int> what_exists;
    for (std::string& s: all_t)
    {
        if (StringUtils::startsWith(s, "addon_"))
            what_exists[s.substr(6)] |= 2;
        else
            what_exists[s] |= 1;
    }
    for (const auto& p: what_exists)
    {
        if (p.second != 3)
        {
            bool is_addon = (p.second == 2);
            std::string value = (is_addon ? "addon_" : "") + p.first;
            m_stf_all_maps.add(p.first, value);
            m_stf_all_maps.add("addon_" + p.first, value);
            if (is_addon)
            {
                m_stf_addon_maps.add(p.first, value);
                m_stf_addon_maps.add("addon_" + p.first, value);
            }
        }
        else
        {
            m_stf_all_maps.add(p.first, p.first);
            m_stf_all_maps.add("addon_" + p.first, "addon_" + p.first);
            m_stf_addon_maps.add(p.first, "addon_" + p.first);
            m_stf_addon_maps.add("addon_" + p.first, "addon_" + p.first);
        }
    }
} // initAssets
// ========================================================================

CommandManager::CommandManager(ServerLobby* lobby):
    m_lobby(lobby)
{
    if (!lobby)
        return;
    initCommands();
    initAssets();

    m_aux_mode_aliases = {
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
    m_aux_difficulty_aliases = {
            {"d0", "novice", "easy"},
            {"d1", "intermediate", "medium"},
            {"d2", "expert", "hard"},
            {"d3", "supertux", "super", "best"}
    };
    m_aux_goal_aliases = {
            {"tl", "time-limit", "time", "minutes"},
            {"gl", "goal-limit", "goal", "goals"}
    };
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
    if (argv.empty())
        return;
    CommandManager::restoreCmdByArgv(cmd, argv, ' ', '"', '"', '\\');

    permissions = m_lobby->getPermissions(peer);
    voting = false;
    std::string action = "invoke";
    std::string username = "";
    if (peer->hasPlayerProfiles())
        username = StringUtils::wideToUtf8(
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
                if (argv.empty())
                    return;
                CommandManager::restoreCmdByArgv(cmd, argv, ' ', '"', '"', '\\');
                voting = m_user_saved_voting[username];
                restored = true;
                break;
            } else {
                std::string msg;
                if (m_user_command_replacements[username].empty())
                    msg = "This command is for fixing typos. Try typing a different command";
                else
                    msg = "Pick one of " +
                        std::to_string(-1 + (int)m_user_command_replacements[username].size())
                        + " options using /1, etc., or use /0, or type a different command";
                m_lobby->sendStringToPeer(msg, peer);
                return;
            }
        }
    }
    m_user_command_replacements.erase(username);
    if (!restored)
        m_user_last_correct_argument.erase(username);

    std::shared_ptr<Command> current_command = m_root_command;
    std::shared_ptr<Command> executed_command;
    for (int idx = 0; ; idx++)
    {
        bool one_omittable_subcommand = (current_command->m_subcommands.size() == 1
                && current_command->m_subcommands[0]->m_omit_name);

        std::shared_ptr<Command> command;
        if (one_omittable_subcommand)
        {
            command = current_command->m_subcommands[0];
        }
        else
        {
            if (hasTypo(peer, voting, argv, cmd, idx, current_command->m_stf_subcommand_names, 3, false, false))
                return;
            auto command_iterator = current_command->m_name_to_subcommand.find(argv[idx]);
            command = command_iterator->second.lock();
        }

        if (!command)
        {
            // todo change message
            std::string msg = "There is no such command but there should be. Very strange. Please report it.";
            m_lobby->sendStringToPeer(msg, peer);
            return;
        }
        else if (!isAvailable(command))
        {
            std::string msg = "You don't have permissions to " + action + " this command";
            m_lobby->sendStringToPeer(msg, peer);
            return;
        }
        int mask = (permissions & command->m_permissions);
        if (mask == 0)
        {
            std::string msg = "You don't have permissions to " + action + " this command";
            m_lobby->sendStringToPeer(msg, peer);
            return;
        }
        int mask_without_voting = (mask & ~PE_VOTED);
        if (mask != PE_NONE && mask_without_voting == PE_NONE)
            voting = true;

        if (one_omittable_subcommand)
            --idx;
        current_command = command;
        if (idx + 1 == argv.size() || command->m_subcommands.empty()) {
            executed_command = command;
            execute(command, context);
            break;
        }
    }

    while (!m_triggered_votables.empty())
    {
        std::string votable_name = m_triggered_votables.front();
        m_triggered_votables.pop();
        auto it = m_votables.find(votable_name);
        if (it != m_votables.end() && it->second.needsCheck())
        {
            auto response = it->second.process(m_users);
            std::map<std::string, int> counts = response.first;
            std::string msg = username + " voted \"/" + cmd + "\", there are";
            if (counts.size() == 1)
                msg += " " + std::to_string(counts.begin()->second) + " such votes";
            else
            {
                bool first_time = true;
                for (auto& p: counts)
                {
                    if (!first_time)
                        msg += ",";
                    msg += " " + std::to_string(p.second) + " votes for such '" + p.first + "'";
                    first_time = false;
                }
            }
            m_lobby->sendStringToAllPeers(msg);
            auto res = response.second;
            if (!res.empty())
            {
                for (auto& p: res)
                {
                    std::string new_cmd = p.first + " " + p.second;
                    auto new_argv = StringUtils::splitQuoted(new_cmd, ' ', '"', '"', '\\');
                    CommandManager::restoreCmdByArgv(new_cmd, new_argv, ' ', '"', '"', '\\');
                    std::string msg2 = "Command \"/" + new_cmd + "\" has been successfully voted";
                    m_lobby->sendStringToAllPeers(msg2);
                    Context new_context(event, std::shared_ptr<STKPeer>(nullptr), new_argv, new_cmd, UP_EVERYONE, false);
                    execute(executed_command, new_context);
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

bool CommandManager::isAvailable(std::shared_ptr<Command> c)
{
    return (getCurrentModeScope() & c->m_mode_scope) != 0
        && (getCurrentStateScope() & c->m_state_scope) != 0;
} // getCurrentModeScope
// ========================================================================

void CommandManager::vote(Context& context, std::string category, std::string value)
{
    auto peer = context.m_peer.lock();
    auto command = context.m_command.lock();
    if (!peer || !command)
    {
        error(context, true);
        return;
    }
    auto& argv = context.m_argv;
    if (!peer->hasPlayerProfiles())
        return;
    std::string username = StringUtils::wideToUtf8(
        peer->getPlayerProfiles()[0]->getName());
    auto& votable = m_votables[command->m_prefix_name];
    bool neededCheck = votable.needsCheck();
    votable.castVote(username, category, value);
    if (votable.needsCheck() && !neededCheck)
        m_triggered_votables.push(command->m_prefix_name);
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
                std::string msg = "Command \"/" + new_cmd + "\" has been successfully voted";
                m_lobby->sendStringToAllPeers(msg);
                // Happily the name of the votable coincides with the command full name
                std::shared_ptr<Command> command = m_full_name_to_command[votable_pairs.first].lock();
                if (!command)
                {
                    Log::error("CommandManager", "For some reason command \"%s\" was not found??", votable_pairs.first.c_str());
                    continue;
                }
                // We don't know the event though it is only needed in
                // ServerLobby::startSelection where it is nullptr when they vote
                Context new_context(nullptr, std::shared_ptr<STKPeer>(nullptr), new_argv, new_cmd, UP_EVERYONE, false);
                execute(command, new_context);
            }
        }
    }
} // update
// ========================================================================

void CommandManager::error(Context& context, bool is_error)
{
    std::string msg;
    if (is_error)
        Log::error("CommandManager", "An error occurred while invoking %s", context.m_cmd.c_str());
    auto command = context.m_command.lock();
    auto peer = context.m_peer.lock();
    if (!command) {
        Log::error("CommandManager", "CM::error: cannot load command");
        return;
    }
    if (!peer) {
        Log::error("CommandManager", "CM::error: cannot load peer");
        return;
    }
    msg = command->getUsage();
    if (msg.empty())
        msg = "An error occurred while invoking command \"" + command->getFullName() + "\".";
    if (is_error)
        msg += "\n/!\\ Please report this error to the server owner";
    m_lobby->sendStringToPeer(msg, peer);
} // error
// ========================================================================

void CommandManager::execute(std::shared_ptr<Command> command, Context& context)
{
    m_current_argv = context.m_argv;
    context.m_command = command;
    (this->*(command->m_action))(context);
    m_current_argv = {};
} // execute
// ========================================================================

void CommandManager::process_help(Context& context)
{
    auto& argv = context.m_argv;
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    std::shared_ptr<Command> command = m_root_command;
    for (int i = 1; i < argv.size(); ++i) {
        if (hasTypo(peer, context.m_voting, context.m_argv, context.m_cmd, i, command->m_stf_subcommand_names, 3, false, false))
            return;
        auto ptr = command->m_name_to_subcommand[argv[i]].lock();
        if (ptr)
            command = ptr;
        else
            break;
        if (command->m_subcommands.empty())
            break;
    }
    if (command == m_root_command)
    {
        error(context);
        return;
    }
    std::string msg = command->getHelp();
    m_lobby->sendStringToPeer(msg, peer);
} // process_help
// ========================================================================

void CommandManager::process_text(Context& context)
{
    std::string response;
    auto peer = context.m_peer.lock();
    auto command = context.m_command.lock();
    if (!peer || !command)
    {
        error(context, true);
        return;
    }
    auto it = m_text_response.find(command->getFullName());
    if (it == m_text_response.end())
        response = "Error: a text command " + command->getFullName()
            + " is defined without text";
    else
        response = it->second;
    m_lobby->sendStringToPeer(response, peer);
} // process_text
// ========================================================================

void CommandManager::process_file(Context& context)
{
    std::string response;
    auto peer = context.m_peer.lock();
    auto command = context.m_command.lock();
    if (!peer || !command)
    {
        error(context, true);
        return;
    }
    auto it = m_file_resources.find(command->getFullName());
    if (it == m_file_resources.end())
        response = "Error: file not found for a file command "
            + command->getFullName();
    else
        response = it->second.get();
    m_lobby->sendStringToPeer(response, peer);
} // process_text
// ========================================================================

void CommandManager::process_auth(Context& context)
{
    std::string response;
    auto peer = context.m_peer.lock();
    auto command = context.m_command.lock();
    if (!peer || !command)
    {
        error(context, true);
        return;
    }
    auto it = m_auth_resources.find(command->getFullName());
    if (it == m_auth_resources.end())
        response = "Error: auth method not found for a command "
                   + command->getFullName();
    else
    {
        auto profile = peer->getPlayerProfiles()[0];
        std::string username = StringUtils::wideToUtf8(profile->getName());
        int online_id = profile->getOnlineId();
        if (online_id == 0)
            response = "Error: you need to join with an "
                       "online account to use auth methods";
        else
            response = it->second.get(username);
    }
    m_lobby->sendStringToPeer(response, peer);
} // process_text
// ========================================================================

void CommandManager::process_commands(Context& context)
{
    std::string result;
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    auto& argv = context.m_argv;
    std::shared_ptr<Command> command = m_root_command;
    bool valid_prefix = true;
    for (int i = 1; i < argv.size(); ++i) {
        if (hasTypo(peer, context.m_voting, context.m_argv, context.m_cmd, i, command->m_stf_subcommand_names, 3, false, false))
            return;
        auto ptr = command->m_name_to_subcommand[argv[i]].lock();
        if (!ptr)
            break;
        if ((context.m_user_permissions & ptr->m_permissions) != 0
                && isAvailable(ptr))
            command = ptr;
        else
        {
            valid_prefix = false;
            break;
        }
        if (command->m_subcommands.empty())
            break;
    }
    if (!valid_prefix)
    {
        result = "There are no available commands with such prefix";
        m_lobby->sendStringToPeer(result, peer);
        return;
    }
    result = (command == m_root_command ? "Available commands"
            : "Available subcommands for /" + command->getFullName());
    result += ":";
    bool had_any_subcommands = false;
    std::map<std::string, int> res;
    for (std::shared_ptr<Command>& subcommand: command->m_subcommands)
    {
        if ((context.m_user_permissions & subcommand->m_permissions) != 0
                && isAvailable(subcommand))
        {
            bool subcommands_available = false;
            for (auto& c: subcommand->m_subcommands)
            {
                if ((context.m_user_permissions & c->m_permissions) != 0
                        && isAvailable(c))
                    subcommands_available = true;
            }
            res[subcommand->m_name] = subcommands_available;
            if (subcommands_available)
                had_any_subcommands = true;
        }
    }
    for (const auto& p: res)
    {
        result += " " + p.first;
        if (p.second)
            result += "*";
    }
    if (had_any_subcommands)
        result += "\n* has subcommands";
    m_lobby->sendStringToPeer(result, peer);
} // process_commands
// ========================================================================

void CommandManager::process_replay(Context& context)
{
    const auto& argv = context.m_argv;
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
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
        m_lobby->sendStringToPeer(msg, peer);
    }
} // process_replay
// ========================================================================

void CommandManager::process_start(Context& context)
{
    if (!ServerConfig::m_owner_less && (context.m_user_permissions & UP_CROWNED) == 0)
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
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    int difficulty = m_lobby->getDifficulty();
    int mode = m_lobby->getGameMode();
    bool goal_target = (m_lobby->m_game_setup->hasExtraSeverInfo() ? m_lobby->isSoccerGoalTarget() : false);
//    m_aux_goal_aliases[goal_target ? 1 : 0][0]
    std::string msg = "Current config: ";
    auto get_first_if_exists = [&](std::vector<std::string>& v) -> std::string {
        if (v.size() < 2)
            return v[0];
        return v[1];
    };
    msg += " ";
    msg += get_first_if_exists(m_aux_mode_aliases[mode]);
    msg += " ";
    msg += get_first_if_exists(m_aux_difficulty_aliases[difficulty]);
    msg += " ";
    msg += get_first_if_exists(m_aux_goal_aliases[goal_target ? 1 : 0]);
    if (!ServerConfig::m_server_configurable)
        msg += " (not configurable)";
    m_lobby->sendStringToPeer(msg, peer);
} // process_config
// ========================================================================

void CommandManager::process_config_assign(Context& context)
{
    auto peer = context.m_peer.lock();
    if (!ServerConfig::m_server_configurable)
    {
        std::string msg = "Server is not configurable, this command cannot be invoked.";
        m_lobby->sendStringToPeer(msg, peer);
        return;
    }
    const auto& argv = context.m_argv;
    int difficulty = m_lobby->getDifficulty();
    int mode = m_lobby->getGameMode();
    bool goal_target = (m_lobby->m_game_setup->hasExtraSeverInfo() ? m_lobby->isSoccerGoalTarget() : false);
    bool user_chose_difficulty = false;
    bool user_chose_mode = false;
    bool user_chose_target = false;
    // bool gp = false;
    for (unsigned i = 1; i < argv.size(); i++)
    {
        for (unsigned j = 0; j < m_aux_mode_aliases.size(); ++j) {
            if (j <= 2 || j == 5) {
                // Switching to GP or modes 2, 5 is not supported yet
                continue;
            }
            for (std::string& alias: m_aux_mode_aliases[j]) {
                if (argv[i] == alias) {
                    mode = j;
                    user_chose_mode = true;
                }
            }
        }
        for (unsigned j = 0; j < m_aux_difficulty_aliases.size(); ++j) {
            for (std::string& alias: m_aux_difficulty_aliases[j]) {
                if (argv[i] == alias) {
                    difficulty = j;
                    user_chose_difficulty = true;
                }
            }
        }
        for (unsigned j = 0; j < m_aux_goal_aliases.size(); ++j) {
            for (std::string& alias: m_aux_goal_aliases[j]) {
                if (argv[i] == alias) {
                    goal_target = (bool)j;
                    user_chose_target = true;
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
        m_lobby->sendStringToPeer(response, peer);
        return;
    }
    if (context.m_voting)
    {
        // Definitely not the best format as there are extra words,
        // but I'll think how to resolve it
        if (user_chose_mode)
            vote(context, "config mode", m_aux_mode_aliases[mode][0]);
        if (user_chose_difficulty)
            vote(context, "config difficulty", m_aux_difficulty_aliases[difficulty][0]);
        if (user_chose_target)
            vote(context, "config target", m_aux_goal_aliases[goal_target ? 1 : 0][0]);
        return;
    }
    m_lobby->handleServerConfiguration(peer, difficulty, mode, goal_target);
} // process_config_assign
// ========================================================================

void CommandManager::process_spectate(Context& context)
{
    std::string response = "";
    auto& argv = context.m_argv;
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }

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

    bool selection_started = (m_lobby->m_state.load() >= ServerLobby::SELECTING);
    bool no_racing_yet = (m_lobby->m_state.load() < ServerLobby::RACING);
//    if (selection_started)
//        m_lobby->erasePeerReady(peer);
    if (argv[1] == "1")
    {
        if (m_lobby->m_process_type == PT_CHILD &&
            peer->getHostId() == m_lobby->m_client_server_host_id.load())
        {
            std::string msg = "Graphical client server cannot spectate";
            m_lobby->sendStringToPeer(msg, peer);
            return;
        }
        peer->setDefaultAlwaysSpectate(ASM_COMMAND);
        if (!selection_started || !no_racing_yet)
            peer->setAlwaysSpectate(ASM_COMMAND);
    }
    else
    {
        peer->setDefaultAlwaysSpectate(ASM_NONE);
        if (!selection_started || !no_racing_yet)
            peer->setAlwaysSpectate(ASM_NONE);
        else
        {
            m_lobby->erasePeerReady(peer);
            peer->setAlwaysSpectate(ASM_NONE);
            peer->setWaitingForGame(true);
        }
    }
    m_lobby->updateServerOwner(true);
    m_lobby->updatePlayerList();
} // process_spectate
// ========================================================================

void CommandManager::process_addons(Context& context)
{
    auto& argv = context.m_argv;
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    bool more = (argv[0] == "moreaddons");
    bool more_own = (argv[0] == "getaddons");
    bool apply_filters = false;
    if (argv.size() == 1)
    {
        argv.push_back("");
        apply_filters = true;
        argv[1] = getAddonPreferredType();
    }
    // removed const reference so that I can modify `from`
    // without changing the original container, we copy everything anyway
    std::set<std::string> from =
        (argv[1] == "kart" ? m_lobby->m_addon_kts.first :
        (argv[1] == "track" ? m_lobby->m_addon_kts.second :
        (argv[1] == "arena" ? m_lobby->m_addon_arenas :
        /*argv[1] == "soccer" ?*/ m_lobby->m_addon_soccers
    )));
    if (apply_filters)
        m_lobby->applyAllFilters(from, false); // happily the type is never karts in this line
    std::vector<std::pair<std::string, std::vector<std::string>>> result;
    for (const std::string& s: from)
        result.push_back({s, {}});

    auto peers = STKHost::get()->getPeers();
    int num_players = 0;
    for (auto p : peers)
    {
        if (!p || !p->isValidated())
            continue;
        if ((!more_own || p != peer) && (p->isWaitingForGame()
            || !m_lobby->canRace(p) || p->isCommandSpectator()))
            continue;
        if (!p->hasPlayerProfiles())
            continue;
        ++num_players;
        std::string username = StringUtils::wideToUtf8(
                p->getPlayerProfiles()[0]->getName());
        const auto& kt = p->getClientAssets();
        const auto& container = (argv[1] == "kart" ? kt.first : kt.second);
        for (auto& pr: result)
            if (container.find(pr.first) == container.end())
                pr.second.push_back(username);
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
            std::string asking_username = "";
            if (peer->hasPlayerProfiles())
                asking_username = StringUtils::wideToUtf8(
                    peer->getPlayerProfiles()[0]->getName());
            for (unsigned i = 0; i < result2.size(); ++i)
            {
                bool present = false;
                for (unsigned j = 0; j < result2[i].second.size(); ++j)
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
    m_lobby->sendStringToPeer(response, peer);
} // process_addons
// ========================================================================

void CommandManager::process_checkaddon(Context& context)
{
    auto& argv = context.m_argv;
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    if (argv.size() < 2)
    {
        error(context);
        return;
    }
    if (hasTypo(peer, context.m_voting, context.m_argv, context.m_cmd,
        1, m_stf_addon_maps, 3, false, true))
        return;
    std::string id = argv[1];
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
    for (auto p : peers)
    {
        if (!p || !p->isValidated() || p->isWaitingForGame()
            || !m_lobby->canRace(p) || p->isCommandSpectator()
            || !p->hasPlayerProfiles())
            continue;
        std::string username = StringUtils::wideToUtf8(
                p->getPlayerProfiles()[0]->getName());
        const auto& kt = p->getClientAssets();
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
    m_lobby->sendStringToPeer(response, peer);
} // process_checkaddon
// ========================================================================

void CommandManager::process_id(Context& context)
{
    auto& argv = context.m_argv;
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    if (argv.size() < 2)
    {
        error(context);
        return;
    }
    if (hasTypo(peer, context.m_voting, context.m_argv, context.m_cmd,
                1, m_stf_all_maps, 3, false, true))
        return;
    std::string id = argv[1];
    std::string response = "Server knows this map, copy it below:\n" + id;
    m_lobby->sendStringToPeer(response, peer);
} // process_id
// ========================================================================

void CommandManager::process_lsa(Context& context)
{
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
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
    m_lobby->sendStringToPeer(response, peer);
} // process_lsa
// ========================================================================

void CommandManager::process_pha(Context& context)
{
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    std::string response = "";
    auto& argv = context.m_argv;
    if (argv.size() < 3)
    {
        error(context);
        return;
    }

    if (hasTypo(peer, context.m_voting, context.m_argv, context.m_cmd,
        1, m_stf_addon_maps, 3, false, true))
        return;
    if (hasTypo(peer, context.m_voting, context.m_argv, context.m_cmd,
        2, m_stf_present_users, 3, false, false))
        return;

    std::string addon_id = argv[1];
    if (StringUtils::startsWith(addon_id, "addon_"))
        addon_id = addon_id.substr(6);
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
    m_lobby->sendStringToPeer(response, peer);
} // process_pha
// ========================================================================

void CommandManager::process_kick(Context& context)
{
    auto& argv = context.m_argv;
    auto peer = context.m_peer.lock();
    if (argv.size() < 2)
    {
        error(context);
        return;
    }

    if (hasTypo(peer, context.m_voting, context.m_argv, context.m_cmd,
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
        m_lobby->sendStringToPeer(msg, peer);
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
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
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
    m_lobby->sendStringToPeer(msg, peer);
} // process_unban
// ========================================================================

void CommandManager::process_ban(Context& context)
{
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
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
    m_lobby->sendStringToPeer(msg, peer);
} // process_ban
// ========================================================================

void CommandManager::process_pas(Context& context)
{
    std::string response;
    std::string player_name;
    auto& argv = context.m_argv;
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }

    if (argv.size() < 2)
    {
        if (peer->getPlayerProfiles().empty())
        {
            Log::warn("CommandManager", "pas: no existing player profiles??");
            error(context);
            return;
        }
        player_name = StringUtils::wideToUtf8(
                peer->getPlayerProfiles()[0]->getName());
    }
    else
    {
        if (hasTypo(peer, context.m_voting, context.m_argv, context.m_cmd,
                1, m_stf_present_users, 3, false, false))
            return;
        player_name = argv[1];
    }
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
        msg += "'s addon score:";
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
    m_lobby->sendStringToPeer(response, peer);
} // process_pas
// ========================================================================

void CommandManager::process_everypas(Context& context)
{
    auto peer = context.m_peer.lock();
    auto argv = context.m_argv;
    if (!peer)
    {
        error(context, true);
        return;
    }
    std::string sorting_type = getAddonPreferredType();
    std::string sorting_direction = "desc";
    if (argv.size() > 1)
        sorting_type = argv[1];
    if (argv.size() > 2)
        sorting_direction = argv[2];
    std::string response = "Addon scores:";
    using Pair = std::pair<std::string, std::vector<int>>;
    std::vector<Pair> result;
    for (const auto& p: STKHost::get()->getPeers())
    {
        if (p->isAIPeer())
            continue;
        if (!p->hasPlayerProfiles())
            continue;
        std::string player_name = StringUtils::wideToUtf8(
                p->getPlayerProfiles()[0]->getName());
        auto &scores = p->getAddonsScores();
        std::vector<int> overall;
        for (int item = 0; item < AS_TOTAL; item++)
            overall.push_back(scores[item]);
        result.emplace_back(player_name, overall);
    }
    int sorting_idx = -1;
    if (sorting_type == "kart" || sorting_type == "karts")
        sorting_idx = 0;
    if (sorting_type == "track" || sorting_type == "tracks")
        sorting_idx = 1;
    if (sorting_type == "arena" || sorting_type == "arenas")
        sorting_idx = 2;
    if (sorting_type == "soccer" || sorting_type == "soccers")
        sorting_idx = 3;
    if (sorting_idx != -1)
    {
        if (sorting_direction == "asc")
            std::sort(result.begin(), result.end(), [sorting_idx]
                    (const Pair& lhs, const Pair& rhs) -> bool {
                int diff = lhs.second[sorting_idx] - rhs.second[sorting_idx];
                return (diff < 0 || (diff == 0 && lhs.first < rhs.first));
            });
        else
            std::sort(result.begin(), result.end(), [sorting_idx]
                    (const Pair& lhs, const Pair& rhs) -> bool {
                int diff = lhs.second[sorting_idx] - rhs.second[sorting_idx];
                return (diff > 0 || (diff == 0 && lhs.first < rhs.first));
            });
    }
    // I don't really know if it should be soccer or field, both are used
    // in different situations
    std::vector<std::string> desc = { "karts", "tracks", "arenas", "fields" };
    for (auto& row: result)
    {
        response += "\n" + row.first;
        bool negative = true;
        for (int item = 0; item < AS_TOTAL; item++)
            negative &= row.second[item] == -1;
        if (negative)
            response += " has no addons";
        else
        {
            std::string msg = "'s addon score:";
            for (int i = 0; i < AS_TOTAL; i++)
                if (row.second[i] != -1)
                    msg += " " + desc[i] + ": " + StringUtils::toString(row.second[i]) + ",";
            msg.pop_back();
            response += msg;
        }
    }
    m_lobby->sendStringToPeer(response, peer);
} // process_everypas
// ========================================================================

void CommandManager::process_sha(Context& context)
{
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
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
    m_lobby->sendStringToPeer(response, peer);
} // process_sha
// ========================================================================

void CommandManager::process_mute(Context& context)
{
    std::shared_ptr<STKPeer> player_peer;
    auto& argv = context.m_argv;
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    std::string result_msg;
    core::stringw player_name;

    if (argv.size() != 2 || argv[1].empty())
    {
        error(context);
        return;
    }

    if (hasTypo(peer, context.m_voting, context.m_argv, context.m_cmd,
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
    m_lobby->sendStringToPeer(result_msg, peer);
} // process_mute
// ========================================================================

void CommandManager::process_unmute(Context& context)
{
    std::shared_ptr<STKPeer> player_peer;
    auto& argv = context.m_argv;
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
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
            m_lobby->sendStringToPeer(result_msg, peer);
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
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
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
    auto peer = context.m_peer.lock();
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
        m_lobby->sendStringToPeer(msg, peer);
        return;
    }
    if (!turn_on && !m_lobby->m_kart_elimination.isEnabled())
    {
        std::string msg = "Gnu Elimination mode was already off!";
        m_lobby->sendStringToPeer(msg, peer);
        return;
    }
    if (turn_on &&
        RaceManager::get()->getMinorMode() != RaceManager::MINOR_MODE_NORMAL_RACE &&
        RaceManager::get()->getMinorMode() != RaceManager::MINOR_MODE_TIME_TRIAL)
    {
        std::string msg = "Gnu Elimination is available only with racing modes";
        m_lobby->sendStringToPeer(msg, peer);
        return;
    }
    std::string kart;
    if (!turn_on)
    {
        kart = "off";
    }
    else
    {
        if (peer)
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
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
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
    m_lobby->writeOwnReport(peer.get(), peer.get(), ans);
} // process_tell
// ========================================================================

void CommandManager::process_standings(Context& context)
{
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    std::string msg;
    auto& argv = context.m_argv;
    bool isGP = false;
    bool isGnu = false;
    bool isGPTeams = false;
    bool isGPPlayers = false;
    for (unsigned i = 1; i < argv.size(); ++i)
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
    m_lobby->sendStringToPeer(msg, peer);
} // process_standings
// ========================================================================

void CommandManager::process_teamchat(Context& context)
{
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    m_lobby->m_team_speakers.insert(peer.get());
    std::string msg = "Your messages are now addressed to team only";
    m_lobby->sendStringToPeer(msg, peer);
} // process_teamchat
// ========================================================================

void CommandManager::process_to(Context& context)
{
    auto& argv = context.m_argv;
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    if (argv.size() == 1)
    {
        error(context);
        return;
    }
    m_lobby->m_message_receivers[peer.get()].clear();
    for (unsigned i = 1; i < argv.size(); ++i)
    {
        if (hasTypo(peer, context.m_voting, context.m_argv, context.m_cmd,
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
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
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
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
#ifdef ENABLE_SQLITE3
    if (argv.size() < 5)
    {
        error(context);
        return;
    }
    bool error = false;
    if (hasTypo(peer, context.m_voting, context.m_argv, context.m_cmd,
                1, m_stf_all_maps, 3, false, true))
        return;
    // todo replace with available aliases?
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
    m_lobby->sendStringToPeer(response, peer);
} // process_record
// ========================================================================

void CommandManager::process_power(Context& context)
{
    auto& argv = context.m_argv;
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
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
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    std::string msg;
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
    m_lobby->sendStringToPeer(msg, peer);
} // process_length
// ========================================================================
void CommandManager::process_length_multi(Context& context)
{
    auto& argv = context.m_argv;
    double temp_double = -1.0;
    if (argv.size() < 3 ||
        !StringUtils::parseString<double>(argv[2], &temp_double))
    {
        error(context);
        return;
    }
    m_lobby->m_default_lap_multiplier = std::max<double>(0.0, temp_double);
    m_lobby->m_fixed_lap = -1;
    std::string msg = StringUtils::insertValues(
            "Game length is now %f x default",
            m_lobby->m_default_lap_multiplier);
    m_lobby->sendStringToAllPeers(msg);
} // process_length_multi
// ========================================================================
void CommandManager::process_length_fixed(Context& context)
{
    auto& argv = context.m_argv;
    int temp_int = -1;
    if (argv.size() < 3 ||
        !StringUtils::parseString<int>(argv[2], &temp_int))
    {
        error(context);
        return;
    }
    m_lobby->m_fixed_lap = std::max<int>(0, temp_int);
    m_lobby->m_default_lap_multiplier = -1.0;
    std::string msg = StringUtils::insertValues(
            "Game length is now %d", m_lobby->m_fixed_lap);
    m_lobby->sendStringToAllPeers(msg);
} // process_length_fixed
// ========================================================================
void CommandManager::process_length_clear(Context& context)
{
    m_lobby->m_default_lap_multiplier = -1.0;
    m_lobby->m_fixed_lap = -1;
    std::string msg = "Game length will be chosen by players";
    m_lobby->sendStringToAllPeers(msg);
} // process_length_clear
// ========================================================================

void CommandManager::process_direction(Context& context)
{
    std::string msg = "Direction is ";
    if (m_lobby->m_fixed_direction == -1)
    {
        msg += "chosen ingame";
    } else
    {
        msg += "set to ";
        msg += (m_lobby->m_fixed_direction == 0 ? "forward" : "reverse");
    }
    m_lobby->sendStringToAllPeers(msg);
} // process_direction
// ========================================================================

void CommandManager::process_direction_assign(Context& context)
{
    auto& argv = context.m_argv;
    std::string msg;
    if (argv.size() < 2)
    {
        error(context);
        return;
    }
    int temp_int = -1;
    if (!StringUtils::parseString<int>(argv[1], &temp_int))
    {
        error(context);
        return;
    }
    if (temp_int < -1 || temp_int > 1)
    {
        error(context);
        return;
    }
    m_lobby->m_fixed_direction = temp_int;
    msg = "Direction is now ";
    if (temp_int == -1)
    {
        msg += "chosen ingame";
    } else
    {
        msg += "set to ";
        msg += (temp_int == 0 ? "forward" : "reverse");
    }
    m_lobby->sendStringToAllPeers(msg);
} // process_direction_assign
// ========================================================================

void CommandManager::process_queue(Context& context)
{
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    std::string msg = "";
    int mask = get_queue_mask(context.m_argv[0]);
    for (int x = QM_START; x < QM_END; x <<= 1)
    {
        if (mask & x)
        {
            auto& queue = get_queue(x);
            msg += StringUtils::insertValues("%s (size = %d):",
                get_queue_name(x), (int)queue.size());
            for (std::shared_ptr<Filter>& s: queue)
                msg += " " + s->toString();
            msg += "\n";
        }
    }
    msg.pop_back();
    m_lobby->sendStringToPeer(msg, peer);
} // process_queue
// ========================================================================

void CommandManager::process_queue_push(Context& context)
{
    auto& argv = context.m_argv;
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }

    if (argv.size() < 3)
    {
        error(context);
        return;
    }
    int mask = get_queue_mask(argv[0]);
    bool to_front = (argv[1] == "push_front");

    if (argv.size() == 3 && argv[2] == "-") // kept until there's filter-type replacement
        argv[2] = getRandomMap();
    else if (argv.size() == 3 && argv[2] == "-addon") // kept until there's filter-type replacement
        argv[2] = getRandomAddonMap();

    std::string filter_text = "";
    CommandManager::restoreCmdByArgv(filter_text, argv, ' ', '"', '"', '\\', 2);

    // Fix typos only if track queues are used (majority of cases anyway)
    // TODO: I don't know how to fix typos for both karts and tracks
    // (there's no m_stf_all_karts anyway yet) but maybe it should be done
    if ((mask & QM_ALL_KART_QUEUES) == 0)
    {
        std::vector<SplitArgument> items = prepareAssetNames<TrackFilter>(filter_text);

        int last_index = -1;
        int prefix_length = 0;
        int suffix_length = 0;
        int subidx = 0;
        for (SplitArgument& p: items)
        {
            if (!p.is_map)
                continue;
            if (p.index != -1 && last_index != p.index)
            {
                last_index = p.index;
                prefix_length = 0;
                subidx = 0;
            }
            auto& cell = context.m_argv[2 + p.index];
            suffix_length = cell.length() - p.value.length() - prefix_length;
            if (hasTypo(peer, context.m_voting, context.m_argv, context.m_cmd,
                        2 + p.index, m_stf_all_maps, 3, false, false, false, subidx,
                        prefix_length, cell.length() - suffix_length))
                return;
            p.value = cell.substr(prefix_length, cell.length() - suffix_length - prefix_length);
            prefix_length += cell.length() - suffix_length;
        }
        filter_text = "";
        for (auto& p: items)
            filter_text += p.value;
    }

    std::string msg = "";

    for (int x = QM_START; x < QM_END; x <<= 1)
    {
        if (mask & x)
        {
            if (mask & QM_ALL_KART_QUEUES)
            {
                // TODO Make sure to update the next branch too; unite them somehow?
                add_to_queue<KartFilter>(x, mask, to_front, filter_text);
            }
            else
            {
                // TODO Make sure to update the previous branch too; unite them somehow?
                add_to_queue<TrackFilter>(x, mask, to_front, filter_text);
            }

            msg += "Pushed { " + filter_text + " }"
                            + " to the " + (to_front ? "front" : "back")
                            + " of " + get_queue_name(x) + ", current queue size: "
                            + std::to_string(get_queue(x).size()) + "\n";
        }
    }

    msg.pop_back();
    m_lobby->sendStringToAllPeers(msg);
    m_lobby->updatePlayerList();
} // process_queue_push
// ========================================================================

void CommandManager::process_queue_pop(Context& context)
{
    std::string msg = "";
    auto& argv = context.m_argv;
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    int mask = get_queue_mask(argv[0]);
    bool from_back = (argv[1] == "pop_back");

    for (int x = QM_START; x < QM_END; x <<= 1)
    {
        if (mask & x)
        {
            int another = another_cyclic_queue(x);
            if (get_queue(x).empty()) {
                msg = "The " + get_queue_name(x) + " was empty before.";
            }
            else
            {
                auto object = (from_back ? get_queue(x).back() : get_queue(x).front());
                msg += "Popped " + object->toString()
                    + " from the " + (from_back ? "back" : "front") + " of the "
                    + get_queue_name(x) + ",";
                if (from_back)
                {
                    get_queue(x).pop_back();
                }
                else
                {
                    get_queue(x).pop_front();
                }
                if (another >= QM_START && !(mask & another))
                {
                    // here you have to pop from FRONT and not back because it
                    // was pushed to the front - see process_queue_push
                    auto& q = get_queue(another);
                    if (!q.empty() && q.front()->isPlaceholder())
                        q.pop_front();
                }
                msg += " current queue size: "
                    + std::to_string(get_queue(x).size());
                msg += "\n";
            }
        }
    }
    msg.pop_back();
    m_lobby->sendStringToAllPeers(msg);
    m_lobby->updatePlayerList();
} // process_queue_pop
// ========================================================================

void CommandManager::process_queue_clear(Context& context)
{
    int mask = get_queue_mask(context.m_argv[0]);
    std::string msg = "";
    for (int x = QM_START; x < QM_END; x <<= 1)
    {
        if (mask & x)
        {
            int another = another_cyclic_queue(x);
            msg += StringUtils::insertValues(
                "The " + get_queue_name(x) + " is now empty (previous size: %d)",
                (int)get_queue(x).size()) + "\n";
            get_queue(x).clear();

            if (another >= QM_START && !(mask & another))
            {
                auto& q = get_queue(another);
                while (!q.empty() && q.front()->isPlaceholder())
                    q.pop_front();
            }
        }
    }
    msg.pop_back();
    m_lobby->sendStringToAllPeers(msg);
    m_lobby->updatePlayerList();
} // process_queue_clear
// ========================================================================

void CommandManager::process_queue_shuffle(Context& context)
{
    std::random_device rd;
    std::mt19937 g(rd());

    int mask = get_queue_mask(context.m_argv[0]);
    std::string msg = "";

    // Note that as the size of this queue is not changed,
    // we don't have to do anything with placeholders for the corresponding
    // cyclic queue, BUT we have to not shuffle the placeholders in the
    // current queue itself
    for (int x = QM_START; x < QM_END; x <<= 1)
    {
        if (mask & x)
        {
            auto& queue = get_queue(x);
            // As the placeholders can be only at the start of a queue,
            // let's just do a binary search - in case there are many placeholders
            int L = -1;
            int R = queue.size();
            int mid;
            while (R - L > 1)
            {
                mid = (L + R) / 2;
                if (queue[mid]->isPlaceholder())
                    L = mid;
                else
                    R = mid;
            }
            std::shuffle(queue.begin() + R, queue.end(), g);
            msg += "The " + get_queue_name(x) + " is now shuffled\n";
        }
    }
    msg.pop_back();
    m_lobby->sendStringToAllPeers(msg);
    m_lobby->updatePlayerList();
} // process_queue_shuffle
// ========================================================================

void CommandManager::process_allowstart(Context& context)
{
    std::string msg;
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    if (m_lobby->m_allowed_to_start)
        msg = "Starting the game is allowed";
    else
        msg = "Starting the game is forbidden";
    m_lobby->sendStringToPeer(msg, peer);
} // process_allowstart
// ========================================================================

void CommandManager::process_allowstart_assign(Context& context)
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
        msg = "Now starting a game is forbidden";
    }
    else
    {
        m_lobby->m_allowed_to_start = true;
        msg = "Now starting a game is allowed";
    }
    m_lobby->sendStringToAllPeers(msg);
} // process_allowstart_assign
// ========================================================================

void CommandManager::process_shuffle(Context& context)
{
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    std::string msg;
    if (m_lobby->m_shuffle_gp)
        msg = "The GP grid is sorted by score";
    else
        msg = "The GP grid is shuffled";
    m_lobby->sendStringToPeer(msg, peer);
} // process_shuffle
// ========================================================================

void CommandManager::process_shuffle_assign(Context& context)
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
    m_lobby->sendStringToAllPeers(msg);
} // process_shuffle_assign
// ========================================================================

void CommandManager::process_timeout(Context& context)
{
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
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
    m_lobby->sendStringToPeer(msg, peer);
} // process_timeout
// ========================================================================

void CommandManager::process_team(Context& context)
{
    std::string msg;
    auto& argv = context.m_argv;
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    if (argv.size() != 3)
    {
        error(context);
        return;
    }
    if (hasTypo(peer, context.m_voting, context.m_argv, context.m_cmd,
                2, m_stf_present_users, 3, false, true))
        return;
    std::string player = argv[2];
    m_lobby->setTemporaryTeam(player, argv[1]);

    m_lobby->updatePlayerList();
} // process_team
// ========================================================================

void CommandManager::process_swapteams(Context& context)
{
    auto& argv = context.m_argv;
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    if (argv.size() != 2)
    {
        error(context);
        return;
    }
    // todo move list of teams and checking teams to another unit later,
    // it is awful now
    std::map<char, char> permutation_map;
    std::map<int, int> permutation_map_int;
    for (char c: argv[1])
    {
        // todo remove that link to first char, it is awful
        // on the other hand, I'd better have roygbpcms than r,o,y,g,b,words
        // so let it stay like that for now
        std::string type(1, c);
        int index = TeamUtils::getIndexByCode(type);
        if (index == 0)
            continue;
        char c1 = TeamUtils::getTeamByIndex(index).getPrimaryCode()[0];
        permutation_map[c1] = 0;
    }
    std::string permutation;
    for (auto& p: permutation_map)
        permutation.push_back(p.first);
    std::string msg = "Shuffled some teams: " + permutation + " -> ";
    if (permutation.size() == 2)
        swap(permutation[0], permutation[1]);
    else
    {
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(permutation.begin(), permutation.end(), g);
    }
    msg += permutation;
    auto it = permutation_map.begin();
    for (unsigned i = 0; i < permutation.size(); i++, it++)
    {
        it->second = permutation[i];
    }
    for (auto& p: permutation_map)
    {
        int from = TeamUtils::getIndexByCode(std::string(1, p.first));
        int to = TeamUtils::getIndexByCode(std::string(1, p.second));
        permutation_map_int[from] = to;
    }
    m_lobby->shuffleTemporaryTeams(permutation_map_int);
    m_lobby->sendStringToPeer(msg, peer); // todo make public?
    m_lobby->updatePlayerList();
} // process_swapteams
// ========================================================================

void CommandManager::process_resetteams(Context& context)
{
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    std::string msg = "Teams are reset now";
    m_lobby->clearTemporaryTeams();
    m_lobby->sendStringToPeer(msg, peer);
    m_lobby->updatePlayerList();
} // process_resetteams
// ========================================================================

void CommandManager::process_randomteams(Context& context)
{
    auto& argv = context.m_argv;
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    int players_number = 0;
    for (auto& p : STKHost::get()->getPeers())
    {
        if (!m_lobby->canRace(p))
            continue;
        if (p->alwaysSpectate())
            continue;
        players_number += p->getPlayerProfiles().size();
        for (auto& profile : p->getPlayerProfiles())
            profile->setTemporaryTeam(0);
    }
    if (players_number == 0) {
        std::string msg = "No one can play!";
        m_lobby->sendStringToPeer(msg, peer);
        return;
    }
    int teams_number = -1;
    int max_number_of_teams = TeamUtils::getNumberOfTeams();
    if (argv.size() < 2 || !StringUtils::parseString(argv[1], &teams_number)
        || teams_number < 1 || teams_number > max_number_of_teams)
    {
        teams_number = (int)round(sqrt(players_number));
        if (teams_number > max_number_of_teams)
            teams_number = max_number_of_teams;
        if (players_number > 1 && teams_number <= 1)
            teams_number = 2;
    }

    std::string msg = StringUtils::insertValues(
            "Created %d teams for %d players", teams_number, players_number);
    std::vector<int> available_colors;
    std::vector<int> profile_colors;
    for (int i = 1; i <= max_number_of_teams; ++i)
        available_colors.push_back(i);

    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(available_colors.begin(), available_colors.end(), g);
    available_colors.resize(teams_number);

    for (int i = 0; i < players_number; ++i)
        profile_colors.push_back(available_colors[i % teams_number]);

    std::shuffle(profile_colors.begin(), profile_colors.end(), g);

    m_lobby->clearTemporaryTeams();
    for (auto& p : STKHost::get()->getPeers())
    {
        if (!m_lobby->canRace(p))
            continue;
        if (p->alwaysSpectate())
            continue;
        for (auto& profile : p->getPlayerProfiles()) {
            std::string name = StringUtils::wideToUtf8(profile->getName());
            std::string color = TeamUtils::getTeamByIndex(profile_colors.back()).getPrimaryCode();
            m_lobby->setTemporaryTeam(name, color);
            if (profile_colors.size() > 1) // prevent crash just in case
                profile_colors.pop_back();
        }
    }

    m_lobby->sendStringToPeer(msg, peer);
    m_lobby->updatePlayerList();
} // process_randomteams
// ========================================================================

void CommandManager::process_resetgp(Context& context)
{
    std::string msg = "GP is now reset";
    auto& argv = context.m_argv;
    if (argv.size() >= 2) {
        int number_of_games;
        if (!StringUtils::parseString(argv[1], &number_of_games)
            || number_of_games <= 0)
        {
            error(context);
            return;
        }
        m_lobby->getGameSetup()->setGrandPrixTrack(number_of_games);
    }
    m_lobby->resetGrandPrix();
    m_lobby->sendStringToAllPeers(msg);
} // process_resetgp
// ========================================================================

void CommandManager::process_cat(Context& context)
{
    std::string msg;
    auto& argv = context.m_argv;
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    if (argv[0] == "cat+")
    {
        if (argv.size() != 3)
        {
            error(context);
            return;
        }
        std::string category = argv[1];
        if (hasTypo(peer, context.m_voting, context.m_argv, context.m_cmd,
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
        if (hasTypo(peer, context.m_voting, context.m_argv, context.m_cmd,
            2, m_stf_present_users, 3, false, true))
            return;
        player = argv[2];
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
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    if (m_lobby->m_troll_active)
        msg = "Trolls will be kicked";
    else
        msg = "Trolls can stay";
    m_lobby->sendStringToPeer(msg, peer);
} // process_troll
// ========================================================================

void CommandManager::process_troll_assign(Context& context)
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
    m_lobby->sendStringToAllPeers(msg);
} // process_troll_assign
// ========================================================================

void CommandManager::process_hitmsg(Context& context)
{
    std::string msg;
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    if (m_lobby->m_show_teammate_hits)
        msg = "Teammate hits are sent to all players";
    else
        msg = "Teammate hits are not sent";
    m_lobby->sendStringToPeer(msg, peer);
} // process_hitmsg
// ========================================================================

void CommandManager::process_hitmsg_assign(Context& context)
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
} // process_hitmsg_assign
// ========================================================================

void CommandManager::process_teamhit(Context& context)
{
    std::string msg;
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    if (m_lobby->m_teammate_hit_mode)
        msg = "Teammate hits are punished";
    else
        msg = "Teammate hits are not punished";
    m_lobby->sendStringToPeer(msg, peer);
} // process_teamhit
// ========================================================================

void CommandManager::process_teamhit_assign(Context& context)
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
} // process_teamhit_assign
// ========================================================================

void CommandManager::process_scoring(Context& context)
{
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    std::string msg = "Current scoring is \"" + m_lobby->m_scoring_type;
    for (int param: m_lobby->m_scoring_int_params)
        msg += StringUtils::insertValues(" %d", param);
    msg += "\"";
    m_lobby->sendStringToPeer(msg, peer);
} // process_scoring
// ========================================================================

void CommandManager::process_scoring_assign(Context& context)
{
    std::string msg;
    auto& argv = context.m_argv;
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    std::string cmd2;
    CommandManager::restoreCmdByArgv(cmd2, argv, ' ', '"', '"', '\\', 1);
    if (m_lobby->loadCustomScoring(cmd2))
    {
        msg = "Scoring set to \"" + cmd2 + "\"";
        m_lobby->sendStringToAllPeers(msg);
    }
    else
    {
        msg = "Scoring could not be parsed from \"" + cmd2 + "\"";
        m_lobby->sendStringToPeer(msg, peer);
    }
} // process_scoring_assign
// ========================================================================

void CommandManager::process_register(Context& context)
{
    auto& argv = context.m_argv;
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    if (!peer->hasPlayerProfiles())
        return;
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
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    if (!peer->hasPlayerProfiles())
        return;
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
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    if (!peer->hasPlayerProfiles())
        return;
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
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    if (!peer->hasPlayerProfiles())
        return;
    std::string peer_username = StringUtils::wideToUtf8(
        peer->getPlayerProfiles()[0]->getName());
    int old_game = m_lobby->m_tournament_game;
    int addition = 0;
    if (argv.size() < 2)
    {
        ++m_lobby->m_tournament_game;
        if (m_lobby->m_tournament_game == m_lobby->m_tournament_max_games)
            m_lobby->m_tournament_game = 0;
        m_lobby->m_fixed_lap = ServerConfig::m_fixed_lap_count;
    } else {
        int new_game_number;
        int new_length = m_lobby->m_tournament_length;
        if (!StringUtils::parseString(argv[1], &new_game_number)
            || new_game_number < 0
            || new_game_number >= m_lobby->m_tournament_max_games)
        {
            std::string msg = "Please specify a correct number. "
                "Format: /game [number 0.."
                + std::to_string(m_lobby->m_tournament_max_games - 1) + "] [length]";
            m_lobby->sendStringToPeer(msg, peer);
            return;
        }
        if (argv.size() >= 3)
        {
            bool ok = StringUtils::parseString(argv[2], &new_length);
            if (!ok || new_length < 0)
            {
                error(context);
                return;
            }
        }
        if (argv.size() >= 4)
        {
            bool ok = StringUtils::parseString(argv[3], &addition);
            if (!ok || addition < 0 || addition > 59)
            {
                std::string msg = "Please specify a correct number. "
                                  "Format: /game [number] [length] [0..59 additional seconds]";
                m_lobby->sendStringToPeer(msg, peer);
                return;
            }
            m_lobby->m_extra_seconds = 0.0f;
            if (addition > 0) {
                m_lobby->m_extra_seconds = 60.0f - addition;
            }
        } else {
            m_lobby->m_extra_seconds = 0.0f;
        }
        m_lobby->m_tournament_game = new_game_number;
        m_lobby->m_fixed_lap = new_length;
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
    if (!m_lobby->tournamentGoalsLimit(m_lobby->m_tournament_game) && addition > 0)
    {
        msg += " " + std::to_string(addition) + " seconds";
        ++m_lobby->m_fixed_lap;
    }
    m_lobby->sendStringToAllPeers(msg);
    Log::info("CommandManager", "SoccerMatchLog: Game number changed from %d to %d",
        old_game, m_lobby->m_tournament_game);
} // process_game
// ========================================================================

void CommandManager::process_role(Context& context)
{
    auto& argv = context.m_argv;
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    if (!peer->hasPlayerProfiles())
        return;
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
    if (hasTypo(peer, context.m_voting, context.m_argv, context.m_cmd,
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
    for (const std::string& u: changed_usernames)
    {
        m_lobby->m_tournament_red_players.erase(u);
        m_lobby->m_tournament_blue_players.erase(u);
        m_lobby->m_tournament_referees.erase(u);
        if (permanent)
        {
            m_lobby->m_tournament_init_red.erase(u);
            m_lobby->m_tournament_init_blue.erase(u);
            m_lobby->m_tournament_init_ref.erase(u);
        }
        std::string role_changed = "The referee has updated your role - you are now %s";
        std::shared_ptr<STKPeer> player_peer = STKHost::get()->findPeerByName(
            StringUtils::utf8ToWide(u));
        std::vector<std::string> missing_assets;
        if (player_peer)
            missing_assets = m_lobby->getMissingAssets(player_peer);
        bool fail = false;
        switch (role[0])
        {
            case 'R':
            case 'r':
            {
                if (!missing_assets.empty())
                {
                    fail = true;
//                    break;
                }
                if (m_lobby->tournamentColorsSwapped(m_lobby->m_tournament_game))
                {
                    m_lobby->m_tournament_blue_players.insert(u);
                    if (permanent)
                        m_lobby->m_tournament_init_blue.insert(u);
                }
                else
                {
                    m_lobby->m_tournament_red_players.insert(u);
                    if (permanent)
                        m_lobby->m_tournament_init_red.insert(u);
                }
                if (player_peer)
                {
                    role_changed = StringUtils::insertValues(role_changed, "red player");
                    if (player_peer->hasPlayerProfiles())
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
//                    break;
                }
                if (m_lobby->tournamentColorsSwapped(m_lobby->m_tournament_game))
                {
                    m_lobby->m_tournament_red_players.insert(u);
                    if (permanent)
                        m_lobby->m_tournament_init_red.insert(u);
                }
                else
                {
                    m_lobby->m_tournament_blue_players.insert(u);
                    if (permanent)
                        m_lobby->m_tournament_init_blue.insert(u);
                }
                if (player_peer)
                {
                    role_changed = StringUtils::insertValues(role_changed, "blue player");
                    if (player_peer->hasPlayerProfiles())
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
//                    break;
                }
                m_lobby->m_tournament_referees.insert(u);
                if (permanent)
                    m_lobby->m_tournament_init_ref.insert(u);
                if (player_peer)
                {
                    role_changed = StringUtils::insertValues(role_changed, "referee");
                    if (player_peer->hasPlayerProfiles())
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
                    if (player_peer->hasPlayerProfiles())
                        player_peer->getPlayerProfiles()[0]->setTeam(KART_TEAM_NONE);
                    m_lobby->sendStringToPeer(role_changed, player_peer);
                }
                break;
            }
        }
        std::string msg;
        if (!fail)
        {
            msg = StringUtils::insertValues(
                "Successfully changed role to %s for %s", role, u);
            Log::info("CommandManager", "SoccerMatchLog: Role of %s changed to %s",
                u.c_str(), role.c_str());
        }
        else
        {
            msg = StringUtils::insertValues(
                "Successfully changed role to %s for %s, but there are missing assets:",
                role, u);
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
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    if (!peer->hasPlayerProfiles())
        return;
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
    sw->tellCountToEveryoneInGame();
} // process_init
// ========================================================================

void CommandManager::process_mimiz(Context& context)
{
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
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
    auto peer = context.m_peer.lock();
    if (argv.size() == 1)
    {
        std::string msg = "/test is now deprecated. Use /test *2 [something] [something]";
        m_lobby->sendStringToPeer(msg, peer);
        return;
    }
    argv.resize(4, "");
    if (argv[2] == "no" && argv[3] == "u")
    {
        error(context);
        return;
    }
    if (context.m_voting)
    {
        vote(context, "test " + argv[1] + " " + argv[2], argv[3]);
        return;
    }
    std::string username = "Vote";
    if (peer.get() && peer->hasPlayerProfiles())
    {
        username = StringUtils::wideToUtf8(
            peer->getPlayerProfiles()[0]->getName());
    }
    username = "{" + argv[1].substr(4) + "} " + username;
    std::string msg = username + ", " + argv[2] + ", " + argv[3];
    m_lobby->sendStringToAllPeers(msg);
} // process_test
// ========================================================================

void CommandManager::process_slots(Context& context)
{
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    int current = m_lobby->m_current_max_players_in_game.load();
    std::string msg = "Number of slots is currently " + std::to_string(current);
    m_lobby->sendStringToPeer(msg, peer);
} // process_slots
// ========================================================================

void CommandManager::process_slots_assign(Context& context)
{
    if (ServerConfig::m_only_host_riding)
    {
        std::string msg = "Changing slots is not possible in the singleplayer mode";
        auto peer = context.m_peer.lock(); // may be nullptr, here we don't care
        m_lobby->sendStringToPeer(msg, peer);
        return;
    }
    auto& argv = context.m_argv;
    bool fail = false;
    int number = 0;
    if (argv.size() < 2 || !StringUtils::parseString<int>(argv[1], &number))
        fail = true;
    else if (number <= 0 || number > ServerConfig::m_server_max_players)
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
    m_lobby->m_current_max_players_in_game.store((unsigned)number);
    m_lobby->updatePlayerList();
    std::string msg = "Number of playable slots is now " + argv[1];
    m_lobby->sendStringToAllPeers(msg);
} // process_slots_assign
// ========================================================================

void CommandManager::process_time(Context& context)
{
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    std::string msg = "Server time: " + StkTime::getLogTime();
    m_lobby->sendStringToPeer(msg, peer);
} // process_time
// ========================================================================

void CommandManager::process_result(Context& context)
{
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    std::string msg = "";
    if (RaceManager::get()->getMinorMode() == RaceManager::MINOR_MODE_SOCCER)
    {
        SoccerWorld *sw = dynamic_cast<SoccerWorld*>(World::getWorld());
        if (sw)
        {
            sw->tellCount(peer);
            return;
        }
        else
            msg = "No game to show the score!";
    }
    else
        msg = "This command is not yet supported for this game mode";
    m_lobby->sendStringToPeer(msg, peer);
} // process_result
// ========================================================================

void CommandManager::process_preserve(Context& context)
{
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    std::string msg = "Preserved settings:";
    for (const std::string& str: m_lobby->m_preserve)
        msg += " " + str;
    m_lobby->sendStringToPeer(msg, peer);
} // process_preserve
// ========================================================================

void CommandManager::process_preserve_assign(Context& context)
{
    auto& argv = context.m_argv;
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    std::string msg = "";
    if (argv.size() != 3)
    {
        error(context);
        return;
    }
    if (argv[2] == "0")
    {
        msg = StringUtils::insertValues(
            "'%s' isn't preserved on server reset anymore", argv[1].c_str());
        m_lobby->m_preserve.erase(argv[1]);
    }
    else
    {
        msg = StringUtils::insertValues(
            "'%s' is now preserved on server reset", argv[1].c_str());
        m_lobby->m_preserve.insert(argv[1]);
    }
    m_lobby->sendStringToAllPeers(msg);
} // process_preserve_assign
// ========================================================================

void CommandManager::process_history(Context& context)
{
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    std::string msg = "Map history:";
    for (unsigned i = 0; i < m_lobby->m_tournament_arenas.size(); i++)
        msg += StringUtils::insertValues(" [%d]: ", i) + m_lobby->m_tournament_arenas[i];
    m_lobby->sendStringToPeer(msg, peer);
} // process_history
// ========================================================================

void CommandManager::process_history_assign(Context& context)
{
    auto& argv = context.m_argv;
    auto peer = context.m_peer.lock();
    if (!peer)
    {
        error(context, true);
        return;
    }
    std::string msg = "";
    if (argv.size() != 3)
    {
        error(context);
        return;
    }
    int index;
    if (!StringUtils::fromString(argv[1], index) || index < 0)
    {
        error(context);
        return;
    }
    if (hasTypo(peer, context.m_voting, context.m_argv, context.m_cmd,
        2, m_stf_all_maps, 3, false, false))
        return;
    std::string id = argv[2];
    if (index >= m_lobby->m_tournament_arenas.size())
        m_lobby->m_tournament_arenas.resize(index + 1, "");
    m_lobby->m_tournament_arenas[index] = id;

    msg = StringUtils::insertValues("Assigned [%d] to %s in the map history", index, id.c_str());
    m_lobby->sendStringToPeer(msg, peer);
} // process_history_assign
// ========================================================================

void CommandManager::special(Context& context)
{
    auto peer = context.m_peer.lock();
    auto command = context.m_command.lock();
    auto cmd = context.m_cmd;
    if (!peer || !command)
    {
        error(context, true);
        return;
    }
    // This function is used as a function for /vote and possibly several
    // other future special functions that are never executed "as usual"
    // but need to be displayed in /commands output. So, in fact, this
    // function is only a placeholder and should be never executed.
    Log::warn("CommandManager", "Command %s was invoked "
        "but not implemented or unavailable for this server",
        command->getFullName().c_str());
    std::string msg = "This command (%s) is not implemented, or "
        "not available for this server. "
        "If you believe that is a bug, please report it. Full input:\n"
        "/%s";
    msg = StringUtils::insertValues(msg, command->getFullName(), cmd);
    m_lobby->sendStringToPeer(msg, peer);
} // special
// ========================================================================

std::string CommandManager::getRandomMap() const
{
    std::set<std::string> items;
    for (const std::string& s: m_lobby->m_entering_kts.second) {
        items.insert(s);
    }
    m_lobby->applyAllFilters(items, false);
    if (items.empty())
        return "";
    RandomGenerator rg;
    std::set<std::string>::iterator it = items.begin();
    std::advance(it, rg.get((int)items.size()));
    return *it;
} // getRandomMap
// ========================================================================

std::string CommandManager::getRandomAddonMap() const
{
    std::set<std::string> items;
    for (const std::string& s: m_lobby->m_entering_kts.second) {
        Track* t = track_manager->getTrack(s);
        if (t->isAddon())
            items.insert(s);
    }
    m_lobby->applyAllFilters(items, false);
    if (items.empty())
        return "";
    RandomGenerator rg;
    std::set<std::string>::iterator it = items.begin();
    std::advance(it, rg.get((int)items.size()));
    return *it;
} // getRandomAddonMap
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

void CommandManager::restoreCmdByArgv(std::string& cmd,
        std::vector<std::string>& argv, char c, char d, char e, char f,
        int from)
{
    cmd.clear();
    for (unsigned i = from; i < argv.size(); ++i) {
        bool quoted = false;
        if (argv[i].find(c) != std::string::npos || argv[i].empty()) {
            quoted = true;
        }
        if (i > from) {
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
    SetTypoFixer& stf, int top, bool case_sensitive, bool allow_as_is,
    bool dont_replace, int subidx, int substr_l, int substr_r)
{
    if (!peer.get()) // voted
        return false;
    std::string username = "";
    if (peer->hasPlayerProfiles())
        username = StringUtils::wideToUtf8(
            peer->getPlayerProfiles()[0]->getName());
    if (std::make_pair(idx, subidx) < m_user_last_correct_argument[username])
        return false;
    std::string text = argv[idx];
    std::string prefix = "";
    std::string suffix = "";
    if (substr_r != -1)
    {
        prefix = text.substr(0, substr_l);
        suffix = text.substr(substr_r);
        text = text.substr(substr_l, substr_r - substr_l);
    }
    auto closest_commands = stf.getClosest(text, top, case_sensitive);
    if (closest_commands.empty())
    {
        std::string msg = "Command " + cmd + " not found";
        m_lobby->sendStringToPeer(msg, peer);
        return true;
    }
    bool no_zeros = closest_commands[0].second != 0;
    bool at_least_two_zeros = closest_commands.size() > 1 && closest_commands[1].second == 0;
    bool there_is_only_one = closest_commands.size() == 1;
    if ((no_zeros || at_least_two_zeros) && !(there_is_only_one && !allow_as_is))
    {
        m_user_command_replacements[username].clear();
        m_user_last_correct_argument[username] = {idx, subidx};
        std::string initial_argument = argv[idx];
        std::string response = "";
        if (idx == 0)
            response += "There is no command \"" + text + "\"";
        else
            response += "Argument \"" + text + "\" may be invalid";

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
            argv[idx] = prefix + closest_commands[i].first + suffix;
            CommandManager::restoreCmdByArgv(cmd, argv, ' ', '"', '"', '\\');
            m_user_command_replacements[username].push_back(cmd);
            response += "\ntype /" + std::to_string(i + 1) + " to choose \"" + closest_commands[i].first + "\"";
        }
        argv[idx] = initial_argument;
        CommandManager::restoreCmdByArgv(cmd, argv, ' ', '"', '"', '\\');
        m_lobby->sendStringToPeer(response, peer);
        return true;
    }

    if (!dont_replace)
    {
        argv[idx] = prefix + closest_commands[0].first + suffix; // converts case or regex
        CommandManager::restoreCmdByArgv(cmd, argv, ' ', '"', '"', '\\');
    }
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
std::shared_ptr<CommandManager::Command> CommandManager::addChildCommand(std::shared_ptr<Command> target,
        std::string name, void (CommandManager::*f)(Context& context),
        int permissions, int mode_scope, int state_scope)
{
    std::shared_ptr<Command> child = std::make_shared<Command>(name, f,
                                   permissions, mode_scope, state_scope);
    target->m_subcommands.push_back(child);
    child->m_parent = std::weak_ptr<Command>(target);
    if (target->m_prefix_name.empty())
        child->m_prefix_name = name;
    else
        child->m_prefix_name = target->m_prefix_name + " " + name;
    target->m_name_to_subcommand[name] = std::weak_ptr<Command>(child);
    return child;
} // addChildCommand
// ========================================================================

void CommandManager::Command::changePermissions(int permissions,
        int mode_scope, int state_scope)
{
    m_permissions = permissions;
    m_mode_scope = mode_scope;
    m_state_scope = state_scope;
} // changePermissions
// ========================================================================

std::string CommandManager::getAddonPreferredType() const
{
    int mode = m_lobby->m_game_mode.load();
    if (0 <= mode && mode <= 4)
        return "track";
    if (mode == 6)
        return "soccer";
    if (7 <= mode && mode <= 8)
        return "arena";
    return "track"; // default choice
} // getAddonPreferredType
// ========================================================================

int CommandManager::get_queue_mask(std::string a)
{
    for (int i = 0; i < QUEUE_NAMES.size(); i++)
        if (a == QUEUE_NAMES[i])
            return i;
    return QM_NONE;
} // getAddonPreferredType
// ========================================================================

std::deque<std::shared_ptr<Filter>>& CommandManager::get_queue(int x) const
{
    if (x == QM_MAP_ONETIME)
        return m_lobby->m_onetime_tracks_queue;
    if (x == QM_MAP_CYCLIC)
        return m_lobby->m_cyclic_tracks_queue;
    if (x == QM_KART_ONETIME)
        return m_lobby->m_onetime_karts_queue;
    if (x == QM_KART_CYCLIC)
        return m_lobby->m_cyclic_karts_queue;
    Log::error("CommandManager",
               "Unknown queue mask %d, revert to map onetime", x);
    return m_lobby->m_onetime_tracks_queue;

} // get_queue
// ========================================================================

std::string CommandManager::get_queue_name(int x)
{
    if (x == QM_MAP_ONETIME)
        return "regular map queue";
    if (x == QM_MAP_CYCLIC)
        return "cyclic map queue";
    if (x == QM_KART_ONETIME)
       return "regular kart queue";
    if (x == QM_KART_CYCLIC)
       return "cyclic kart queue";
    return StringUtils::insertValues(
        "[Error QN%d: please report with /tell about it] queue", x);
} // get_queue_name
// ========================================================================

int CommandManager::another_cyclic_queue(int x)
{
    if (x == QM_MAP_ONETIME)
        return QM_MAP_CYCLIC;
    // if (x == QM_KART_ONETIME)
    //    return QM_KART_CYCLIC;
    return QM_NONE;
} // get_queue_name
// ========================================================================

template<typename T>
void CommandManager::add_to_queue(int x, int mask, bool to_front, std::string& s) const
{
    int another = another_cyclic_queue(x);
    if (to_front)
    {
        get_queue(x).push_front(std::make_shared<T>(s));
        if (another >= QM_START && !(mask & another))
        {
            get_queue(another).push_front(std::make_shared<T>(T::PLACEHOLDER_STRING));
        }
    }
    else
    {
        get_queue(x).push_back(std::make_shared<T>(s));
        // here you have to push to FRONT and not back because onetime
        // queue should be invoked strictly before
        if (another >= QM_START && !(mask & another))
        {
            get_queue(another).push_front(std::make_shared<T>(T::PLACEHOLDER_STRING));
        }
    }
} // add_to_queue
// ========================================================================
