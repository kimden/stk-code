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
#include "io/file_manager.hpp"
#include "modes/soccer_world.hpp"
#include "network/crypto.hpp"
#include "network/database_connector.hpp"
#include "network/event.hpp"
#include "network/game_setup.hpp"
#include "network/network_player_profile.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/server_config.hpp"
#include "network/stk_host.hpp"
#include "network/stk_peer.hpp"
#include "tracks/track.hpp"
#include "tracks/track_manager.hpp"
#include "utils/chat_manager.hpp"
#include "utils/communication.hpp"
#include "utils/crown_manager.hpp"
#include "utils/file_utils.hpp"
#include "utils/hit_processor.hpp"
#include "utils/hourglass_reason.hpp"
#include "utils/kart_elimination.hpp"
#include "utils/lobby_asset_manager.hpp"
#include "utils/lobby_context.hpp"
#include "utils/lobby_gp_manager.hpp"
#include "utils/lobby_settings.hpp"
#include "utils/lobby_queues.hpp"
#include "utils/log.hpp"
#include "utils/map_vote_handler.hpp"
#include "utils/random_generator.hpp"
#include "utils/string_utils.hpp"
#include "utils/team_manager.hpp"
#include "utils/tournament.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <iterator>
#include <utility>

// TODO: kimden: should decorators use acting_peer?

namespace
{
    static const std::string g_addon_prefix = "addon_";

    static const std::string g_type_kart   = "kart";
    static const std::string g_type_map    = "map";

    static const std::string g_type_track  = "track";
    static const std::string g_type_arena  = "arena";
    static const std::string g_type_soccer = "soccer";

    const std::vector<std::string> g_queue_names = {
        "", "mqueue", "mcyclic", "mboth",
        "kqueue", "qregular", "", "",
        "kcyclic", "", "qcyclic", "",
        "kboth", "", "", "qboth"
    };

    enum QueueMask: int {
        QM_NONE = -1,
        QM_MAP_ONETIME = 1,
        QM_MAP_CYCLIC = 2,
        QM_KART_ONETIME = 4,
        QM_KART_CYCLIC = 8,
        QM_ALL_MAP_QUEUES = QM_MAP_ONETIME | QM_MAP_CYCLIC,
        QM_ALL_KART_QUEUES = QM_KART_ONETIME | QM_KART_CYCLIC,
        QM_START = 1,
        QM_END = 16
    }; // enum QueueMask

    int get_queue_mask(std::string a)
    {
        for (int i = 0; i < (int)g_queue_names.size(); i++)
            if (a == g_queue_names[i])
                return i;
        return QueueMask::QM_NONE;
    } // get_queue_mask
    // ====================================================================

    std::string get_queue_name(int x)
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
    // ====================================================================

    int another_cyclic_queue(int x)
    {
        if (x == QM_MAP_ONETIME)
            return QM_MAP_CYCLIC;
        // if (x == QM_KART_ONETIME)
        //    return QM_KART_CYCLIC;
        return QM_NONE;
    } // another_cyclic_queue
    // ====================================================================
    

    // Auxiliary things, should be moved somewhere because they just help
    // in commands but have nothing to do with CM itself
    
    static std::vector<std::vector<std::string>> g_aux_mode_aliases = {
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
    static std::vector<std::vector<std::string>> g_aux_difficulty_aliases = {
            {"d0", "novice", "easy"},
            {"d1", "intermediate", "medium"},
            {"d2", "expert", "hard"},
            {"d3", "supertux", "super", "best"}
    };
    static std::vector<std::vector<std::string>> g_aux_goal_aliases = {
            {"tl", "time-limit", "time", "minutes"},
            {"gl", "goal-limit", "goal", "goals"}
    };

    // End of auxiliary things
} // namespace

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
    {"UU_SPECTATOR", UU_SPECTATOR},
    {"UU_USUAL", UU_USUAL},
    {"UU_CROWNED", UU_CROWNED},
    {"UU_SINGLE", UU_SINGLE},
    {"UU_HAMMER", UU_HAMMER},
    {"UU_MANIPULATOR", UU_MANIPULATOR},
    {"UU_CONSOLE", UU_CONSOLE},
    {"PE_SPECTATOR", PE_SPECTATOR},
    {"PE_USUAL", PE_USUAL},
    {"PE_CROWNED", PE_CROWNED},
    {"PE_SINGLE", PE_SINGLE},
    {"PE_HAMMER", PE_HAMMER},
    {"PE_MANIPULATOR", PE_MANIPULATOR},
    {"PE_CONSOLE", PE_CONSOLE},
    {"UU_OWN_COMMANDS", UU_OWN_COMMANDS},
    {"UU_OTHERS_COMMANDS", UU_OTHERS_COMMANDS},
    {"PE_ALLOW_ANYONE", PE_ALLOW_ANYONE},
    {"PE_VOTED_SPECTATOR", PE_VOTED_SPECTATOR},
    {"PE_VOTED_NORMAL", PE_VOTED_NORMAL},
    {"PE_VOTED", PE_VOTED},
    {"UP_CONSOLE", UP_CONSOLE},
    {"UP_MANIPULATOR", UP_MANIPULATOR},
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

std::string CommandManager::AuthResource::get(const std::string& username, int online_id) const
{
#ifdef ENABLE_CRYPTO_OPENSSL
    std::string header = "{\"alg\":\"HS256\",\"typ\":\"JWT\"}";
    uint64_t timestamp = StkTime::getTimeSinceEpoch();
    std::string payload = "{\"sub\":\"" + username + "/" + std::to_string(online_id) + "\",";
    payload += "\"iat\":\"" + std::to_string(timestamp) + "\",";
    payload += "\"iss\":\"" + m_server + "\"}";
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
    // Handling players who are allowed to run for anyone in any case
    m_permissions |= UU_OTHERS_COMMANDS;
} // Command::Command(5)
// ========================================================================

const SetTypoFixer& CommandManager::getFixer(TypoFixerType type)
{
    switch (type)
    {
        case TFT_PRESENT_USERS: return m_stf_present_users;
        case TFT_ALL_MAPS:      return m_stf_all_maps;
        case TFT_ADDON_MAPS:    return m_stf_addon_maps;
    }
    throw std::logic_error("Invalid TypoFixerType " + std::to_string(type));
}   // getFixer
//-----------------------------------------------------------------------------

// From the result perspective, it works in the same way as
// ServerConfig - just as there, there can be two files, one of them
// overriding another. However, I'm right now lazy to make them use the
// same "abstract mechanism" for that, which could be good in case other
// settings can be generalized that way. Also I don't exclude the
// possibility that commands.xml and ServerConfig could be united (apart
// from the fact there are default commands and no default config).
// (Also commands.xml has nested things and config doesn't)

// So the implementation is different from ServerConfig for simplicity.
// The *custom* config is loaded first, and then a generic one if the
// custom one includes it. If a generic one tries to load a *top-level*
// command already defined in *custom* one, it's ignored. It means that
// to override command's behaviour, you need to specify its full block.

void CommandManager::initCommandsInfo()
{
    // "commands.xml"
    const std::string file_name = file_manager->getAsset(getSettings()->getCommandsFile());
    const XMLNode *root = file_manager->createXMLTree(file_name);
    uint32_t version = 1;
    root->get("version", &version);
    if (version != 1)
        Log::warn("CommandManager", "command.xml has version %d which is not supported", version);
    std::string external_commands;

    XMLNode* root2 = nullptr;
    auto external_node = root->getNode("external-commands-file");
    if (external_node && external_node->get("value", &external_commands))
    {
        const std::string file_name_2 = file_manager->getAsset(external_commands);
        root2 = file_manager->createXMLTree(file_name_2);
    }

    std::set<std::string> used_commands;

    std::function<void(const XMLNode* current, std::shared_ptr<Command> command)> dfs =
            [&](const XMLNode* current, const std::shared_ptr<Command>& command) {
        for (unsigned int i = 0; i < current->getNumNodes(); i++)
        {
            const XMLNode *node = current->getNode(i);
            std::string node_name = node->getName();
            if (node_name == "external-commands-file")
                continue;
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

            // Name is read before enabled/disabled property, because we want
            // to disable commands in "default" config that are present in
            // "custom" config, regardless of server name
            node->get("name", &name);
            if (current == root2 && command == m_root_command
                    && used_commands.find(name) != used_commands.end())
            {
                continue;
            }
            else if (current == root)
            {
                used_commands.insert(name);
            }

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
    if (root2)
        dfs(root2, m_root_command);
    delete root;
    delete root2;
} // initCommandsInfo
//-----------------------------------------------------------------------------

void CommandManager::initCommands()
{
    using CM = CommandManager;
    auto& mp = m_full_name_to_command;
    m_root_command = std::make_shared<Command>("", &CM::special);

    initCommandsInfo();

    auto applyFunctionIfPossible = [&](std::string&& name, void (CM::*f)(Context& context)) {
        auto it = mp.find(name);
        if (it == mp.end())
            return;

        std::shared_ptr<Command> command = it->second.lock();
        if (!command)
            return;

        command->changeFunction(f);
    };
    // special permissions according to ServerConfig options
    std::shared_ptr<Command> kick_command = mp["kick"].lock();
    if (kick_command) {
        if (getSettings()->hasKicksAllowed())
            kick_command->m_permissions |= UU_CROWNED;
        else
            kick_command->m_permissions &= ~UU_CROWNED;
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
    applyFunctionIfPossible("power2", &CM::process_power);
    applyFunctionIfPossible("length", &CM::process_length);
    applyFunctionIfPossible("length clear", &CM::process_length_clear);
    applyFunctionIfPossible("length =", &CM::process_length_fixed);
    applyFunctionIfPossible("length x", &CM::process_length_multi);
    applyFunctionIfPossible("direction", &CM::process_direction);
    applyFunctionIfPossible("direction =", &CM::process_direction_assign);
    for (int i = 0; i < (int)g_queue_names.size(); i++)
    {
        const std::string& name = g_queue_names[i];
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
    applyFunctionIfPossible("as", &CM::special);
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
    applyFunctionIfPossible("voting", &CM::process_voting);
    applyFunctionIfPossible("voting =", &CM::process_voting_assign);
    applyFunctionIfPossible("whyhourglass", &CM::process_why_hourglass);
    applyFunctionIfPossible("availableteams", &CM::process_available_teams);
    applyFunctionIfPossible("availableteams =", &CM::process_available_teams_assign);
    applyFunctionIfPossible("cooldown", &CM::process_cooldown);
    applyFunctionIfPossible("cooldown =", &CM::process_cooldown_assign);
    applyFunctionIfPossible("countteams", &CM::process_countteams);
    applyFunctionIfPossible("network", &CM::process_net);
    applyFunctionIfPossible("everynet", &CM::process_everynet);
    applyFunctionIfPossible("temp", &CM::process_temp250318);

    applyFunctionIfPossible("addondownloadprogress", &CM::special);
    applyFunctionIfPossible("stopaddondownload", &CM::special);
    applyFunctionIfPossible("installaddon", &CM::special);
    applyFunctionIfPossible("uninstalladdon", &CM::special);
    applyFunctionIfPossible("music", &CM::special);
    applyFunctionIfPossible("addonrevision", &CM::special);
    applyFunctionIfPossible("liststkaddon", &CM::special);
    applyFunctionIfPossible("listlocaladdon", &CM::special);

    addTextResponse("description", getSettings()->getMotd());
    addTextResponse("moreinfo", getSettings()->getHelpMessage());
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
    m_votables.emplace("randomteams", 0.6);
    m_votables.emplace("slots", CommandVoting::DEFAULT_THRESHOLD);
    m_votables["gnu"].setCustomThreshold("gnu kart", 1.1);
    m_votables["config"].setMerge(7);
} // initCommands
// ========================================================================

void CommandManager::initAssets()
{
    auto all_t = TrackManager::get()->getAllTrackIdentifiers();
    std::map<std::string, int> what_exists;
    for (std::string& s: all_t)
    {
        if (StringUtils::startsWith(s, g_addon_prefix))
            what_exists[s.substr(g_addon_prefix.size())] |= 2;
        else
            what_exists[s] |= 1;
    }
    for (const auto& p: what_exists)
    {
        if (p.second != 3)
        {
            bool is_addon = (p.second == 2);
            std::string value = (is_addon ? g_addon_prefix : "") + p.first;
            m_stf_all_maps.add(p.first, value);
            m_stf_all_maps.add(g_addon_prefix + p.first, value);
            if (is_addon)
            {
                m_stf_addon_maps.add(p.first, value);
                m_stf_addon_maps.add(g_addon_prefix + p.first, value);
            }
        }
        else
        {
            m_stf_all_maps.add(p.first, p.first);
            m_stf_all_maps.add(g_addon_prefix + p.first, g_addon_prefix + p.first);
            m_stf_addon_maps.add(p.first, g_addon_prefix + p.first);
            m_stf_addon_maps.add(g_addon_prefix + p.first, g_addon_prefix + p.first);
        }
    }
} // initAssets
// ========================================================================

void CommandManager::setupContextUser()
{
    initCommands();
    initAssets();
} // CommandManager
// ========================================================================

void CommandManager::handleCommand(Event* event, std::shared_ptr<STKPeer> peer)
{
    auto packet = event->getPacket<CommandPacket>();
    std::string language = packet.language;

    Context context(getLobby(), event, peer);
    auto& argv = context.m_argv;
    auto& cmd = context.m_cmd;
    auto& permissions = context.m_user_permissions;
    auto& acting_permissions = context.m_acting_user_permissions;
    auto& voting = context.m_voting;
    auto& target_peer = context.m_target_peer;
    std::shared_ptr<STKPeer> target_peer_strong = context.m_target_peer.lock();

    cmd = packet.command;
    argv = StringUtils::splitQuoted(cmd, ' ', '"', '"', '\\');
    if (argv.empty())
        return;
    CommandManager::restoreCmdByArgv(cmd, argv, ' ', '"', '"', '\\');

    permissions = getLobby()->getPermissions(peer);
    voting = false;
    std::string action = "invoke";

    acting_permissions = getLobby()->getPermissions(target_peer_strong);

    std::string username = "";
    std::string acting_username = "";

    username = (peer->hasPlayerProfiles() ? peer->getMainName() : "");
    acting_username = (target_peer_strong && target_peer_strong->hasPlayerProfiles() ? target_peer_strong->getMainName() : "");

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
                target_peer = m_user_saved_acting_peer[username];
                target_peer_strong = target_peer.lock();
                acting_username = (target_peer_strong && target_peer_strong->hasPlayerProfiles() ? target_peer_strong->getMainName() : "");
                acting_permissions = getLobby()->getPermissions(target_peer_strong);
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
                context.say(msg);
                return;
            }
        }
    }

    m_user_command_replacements.erase(username);
    if (!restored)
        m_user_last_correct_argument.erase(username);

    while (!argv.empty())
    {
        if (argv[0] == "as")
        {
            std::shared_ptr<STKPeer> new_target_peer = {};
            if (argv.size() >= 2)
            {
                if (hasTypo(target_peer.lock(), peer, voting, argv, cmd, 1, getFixer(TFT_PRESENT_USERS), 3, false, false))
                    return;

                new_target_peer = STKHost::get()->findPeerByName(StringUtils::utf8ToWide(argv[1]));
            }

            if (!new_target_peer || argv.size() <= 2)
            {
                context.say("Usage: /as (username) (another command with arguments)");
                return;
            }
            target_peer = new_target_peer;
            target_peer_strong = new_target_peer;
            acting_username = (target_peer_strong && target_peer_strong->hasPlayerProfiles() ? target_peer_strong->getMainName() : "");
            acting_permissions = getLobby()->getPermissions(target_peer_strong);
            shift(cmd, argv, username, 2);
            continue;
        }
        else if (argv[0] == "vote")
        {
            if (argv.size() == 1 || argv[1] == "vote")
            {
                // kimden: all error strings in this function should be done in error(context) way
                context.say("Usage: /vote (a command with arguments)");
                return;
            }
            shift(cmd, argv, username, 1);
            voting = true;
            action = "vote for";
            continue;
        }
        break;
    }

    if (peer && target_peer.expired())
    {
        // kimden: save username before player leaves?
        context.say(StringUtils::insertValues(
                "The person from whose name you tried to invoke "
                "the command has left or rejoined. The command was:\n/%s",
                cmd.c_str()
        ));
        return;
    }

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
            if (hasTypo(target_peer_strong, peer, voting, argv, cmd, idx, current_command->m_stf_subcommand_names, 3, false, false))
                return;
            auto command_iterator = current_command->m_name_to_subcommand.find(argv[idx]);
            command = command_iterator->second.lock();
        }

        if (!command)
        {
            // todo change message
            context.say("There is no such command but there should be. Very strange. Please report it.");
            return;
        }
        else if (!isAvailable(command))
        {
            context.say("You don't have permissions to " + action + " this command");
            return;
        }

        // Note that we use caller's permissions to determine if the command can be invoked,
        // and during its invocation, we use acting peer's permissions.
        int mask = ((permissions & command->m_permissions) & (~MASK_MANIPULATION));
        if (mask == 0)
        {
            context.say("You don't have permissions to " + action + " this command");
            return;
        }
        // kimden: both might be nullptr, which means different things
        if (target_peer.lock() != peer)
        {
            int mask_manip = (permissions & command->m_permissions & MASK_MANIPULATION);
            if (mask_manip == 0)
            {
                context.say("You don't have permissions to " + action
                        + " this command for another person");
                return;
            }
        }
        int mask_without_voting = (mask & ~PE_VOTED);
        if (mask != PE_NONE && mask_without_voting == PE_NONE)
            voting = true;

        if (one_omittable_subcommand)
            --idx;
        current_command = command;
        if (idx + 1 == (int)argv.size() || command->m_subcommands.empty()) {
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
            std::string msg = acting_username + " voted \"/" + cmd + "\", there are";
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
            Comm::sendStringToAllPeers(msg);
            auto res = response.second;
            if (!res.empty())
            {
                for (auto& p: res)
                {
                    std::string new_cmd = p.first + " " + p.second;
                    auto new_argv = StringUtils::splitQuoted(new_cmd, ' ', '"', '"', '\\');
                    CommandManager::restoreCmdByArgv(new_cmd, new_argv, ' ', '"', '"', '\\');
                    std::string msg2 = StringUtils::insertValues(
                            "Command \"/%s\" has been successfully voted",
                            new_cmd.c_str());
                    Comm::sendStringToAllPeers(msg2);
                    Context new_context(getLobby(), event, std::shared_ptr<STKPeer>(nullptr), new_argv, new_cmd, UP_EVERYONE, UP_EVERYONE, false);
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
    if (isTournament())
        mask |= MS_SOCCER_TOURNAMENT;
    return mask;
} // getCurrentModeScope
// ========================================================================

bool CommandManager::isAvailable(std::shared_ptr<Command> c)
{
    return (getCurrentModeScope() & c->m_mode_scope) != 0
        && (getLobby()->getCurrentStateScope() & c->m_state_scope) != 0;
} // getCurrentModeScope
// ========================================================================

void CommandManager::vote(Context& context, std::string category, std::string value)
{
    auto peer = context.peer();
    auto acting_peer = context.actingPeer();
    auto command = context.command();

    if (!acting_peer->hasPlayerProfiles())
        return;
    std::string username = acting_peer->getMainName();
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
                std::string msg = StringUtils::insertValues(
                        "Command \"/%s\" has been successfully voted",
                        new_cmd.c_str());
                Comm::sendStringToAllPeers(msg);
                // Happily the name of the votable coincides with the command full name
                std::shared_ptr<Command> command = m_full_name_to_command[votable_pairs.first].lock();
                if (!command)
                {
                    Log::error("CommandManager", "For some reason command \"%s\" was not found??", votable_pairs.first.c_str());
                    continue;
                }
                // We don't know the event though it is only needed in
                // ServerLobby::startSelection where it is nullptr when they vote
                Context new_context(getLobby(), nullptr, std::shared_ptr<STKPeer>(nullptr), new_argv, new_cmd, UP_EVERYONE, UP_EVERYONE, false);
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
        Log::error("CommandManager", "CM::error: cannot load peer to send error");
        return;
    }
    msg = command->getUsage();
    if (msg.empty())
        msg = StringUtils::insertValues("An error occurred "
                "while invoking command \"%s\".",
                command->getFullName().c_str());

    if (is_error)
        msg += "\n/!\\ Please report this error to the server owner";
    context.say(msg);
} // error
// ========================================================================

void CommandManager::execute(std::shared_ptr<Command> command, Context& context)
{
    m_current_argv = context.m_argv;
    context.m_command = command;
    try
    {
        (this->*(command->m_action))(context);
    }
    catch (std::exception& ex)
    {
        // auto peer = context.m_peer.lock();
        error(context, true);

        // // kimden: make error message better + add log
        // context.say(StringUtils::insertValues(
        //     "An error happened: %s",
        //     ex.what()
        // ));
    }
    m_current_argv = {};
} // execute
// ========================================================================

void CommandManager::process_help(Context& context)
{
    auto& argv = context.m_argv;
    auto acting_peer = context.actingPeer();
    auto peer = context.peer();

    std::shared_ptr<Command> command = m_root_command;
    for (int i = 1; i < (int)argv.size(); ++i) {
        if (hasTypo(acting_peer, peer, context.m_voting, context.m_argv, context.m_cmd, i, command->m_stf_subcommand_names, 3, false, false))
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
    context.say(command->getHelp());
} // process_help
// ========================================================================

void CommandManager::process_text(Context& context)
{
    std::string response;
    auto command = context.command();
    auto it = m_text_response.find(command->getFullName());
    if (it == m_text_response.end())
        response = StringUtils::insertValues(
                "Error: a text command %s is defined without text",
                command->getFullName().c_str());
    else
        response = it->second;
    context.say(response);
} // process_text
// ========================================================================

void CommandManager::process_file(Context& context)
{
    std::string response;
    auto command = context.command();

    auto it = m_file_resources.find(command->getFullName());
    if (it == m_file_resources.end())
        response = StringUtils::insertValues(
                "Error: file not found for a file command %s",
                command->getFullName().c_str());
    else
        response = it->second.get();
    context.say(response);
} // process_text
// ========================================================================

void CommandManager::process_auth(Context& context)
{
    std::string response;
    auto acting_peer = context.actingPeer();
    auto command = context.command();

    auto it = m_auth_resources.find(command->getFullName());
    if (it == m_auth_resources.end())
        response = StringUtils::insertValues(
                "Error: auth method not found for a command %s",
                command->getFullName().c_str());
    else
    {
        auto profile = acting_peer->getMainProfile();
        std::string username = StringUtils::wideToUtf8(profile->getName());
        int online_id = profile->getOnlineId();
        if (online_id == 0)
            response = "Error: you need to join with an "
                       "online account to use auth methods";
        else
            response = it->second.get(username, online_id);
    }
    context.say(response);
} // process_text
// ========================================================================

void CommandManager::process_commands(Context& context)
{
    std::string result;
    auto acting_peer = context.actingPeer();
    auto peer = context.peer();

    auto& argv = context.m_argv;
    std::shared_ptr<Command> command = m_root_command;
    bool valid_prefix = true;
    for (int i = 1; i < (int)argv.size(); ++i) {
        if (hasTypo(acting_peer, peer, context.m_voting, context.m_argv, context.m_cmd, i, command->m_stf_subcommand_names, 3, false, false))
            return;
        auto ptr = command->m_name_to_subcommand[argv[i]].lock();
        if (!ptr)
            break;
        if ((context.m_acting_user_permissions & ptr->m_permissions) != 0
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
        context.say("There are no available commands with such prefix");
        return;
    }
    result = (command == m_root_command ? "Available commands"
            : "Available subcommands for /" + command->getFullName());
    result += ":";
    bool had_any_subcommands = false;
    std::map<std::string, int> res;
    for (std::shared_ptr<Command>& subcommand: command->m_subcommands)
    {
        if ((context.m_acting_user_permissions & subcommand->m_permissions) != 0
                && isAvailable(subcommand))
        {
            bool subcommands_available = false;
            for (auto& c: subcommand->m_subcommands)
            {
                if ((context.m_acting_user_permissions & c->m_permissions) != 0
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
    context.say(result);
} // process_commands
// ========================================================================

void CommandManager::process_replay(Context& context)
{
    const auto& argv = context.m_argv;

    if (getSettings()->isRecordingReplays())
    {
        bool current_state = getSettings()->hasConsentOnReplays();
        if (argv.size() >= 2 && argv[1] == "0")
            current_state = false;
        else if (argv.size() >= 2 && argv[1] == "1")
            current_state = true;
        else
            current_state ^= 1;

        getSettings()->setConsentOnReplays(current_state);
        Comm::sendStringToAllPeers(std::string("Recording ghost replays is now ")
                + (current_state ? "on" : "off"));
    }
    else
    {
        context.say("This server doesn't allow recording replays");
    }
} // process_replay
// ========================================================================

void CommandManager::process_start(Context& context)
{
    if (!getCrownManager()->isOwnerLess() && (context.m_acting_user_permissions & UP_CROWNED) == 0)
    {
        context.m_voting = true;
    }
    if (context.m_voting)
    {
        vote(context, "start", "");
        return;
    }
    getLobby()->startSelection(context.m_event);
} // process_start
// ========================================================================

void CommandManager::process_config(Context& context)
{
    int difficulty = getLobby()->getDifficulty();
    int mode = getLobby()->getGameMode();
    bool goal_target = (getGameSetupFromCtx()->hasExtraServerInfo() ? getLobby()->isSoccerGoalTarget() : false);
//    g_aux_goal_aliases[goal_target ? 1 : 0][0]
    std::string msg = "Current config: ";
    auto get_first_if_exists = [&](std::vector<std::string>& v) -> std::string {
        if (v.size() < 2)
            return v[0];
        return v[1];
    };
    msg += " ";
    msg += get_first_if_exists(g_aux_mode_aliases[mode]);
    msg += " ";
    msg += get_first_if_exists(g_aux_difficulty_aliases[difficulty]);
    msg += " ";
    msg += get_first_if_exists(g_aux_goal_aliases[goal_target ? 1 : 0]);
    if (!getSettings()->isServerConfigurable())
        msg += " (not configurable)";
    context.say(msg);
} // process_config
// ========================================================================

void CommandManager::process_config_assign(Context& context)
{
    auto acting_peer = context.actingPeerMaybeNull();
    if (!getSettings()->isServerConfigurable())
    {
        context.say("Server is not configurable, this command cannot be invoked.");
        return;
    }
    const auto& argv = context.m_argv;
    int difficulty = getLobby()->getDifficulty();
    int mode = getLobby()->getGameMode();
    bool goal_target = (getGameSetupFromCtx()->hasExtraServerInfo() ? getLobby()->isSoccerGoalTarget() : false);
    bool user_chose_difficulty = false;
    bool user_chose_mode = false;
    bool user_chose_target = false;
    // bool gp = false;
    for (unsigned i = 1; i < argv.size(); i++)
    {
        for (unsigned j = 0; j < g_aux_mode_aliases.size(); ++j) {
            if (j <= 2 || j == 5) {
                // Switching to GP or modes 2, 5 is not supported yet
                continue;
            }
            for (std::string& alias: g_aux_mode_aliases[j]) {
                if (argv[i] == alias) {
                    mode = j;
                    user_chose_mode = true;
                }
            }
        }
        for (unsigned j = 0; j < g_aux_difficulty_aliases.size(); ++j) {
            for (std::string& alias: g_aux_difficulty_aliases[j]) {
                if (argv[i] == alias) {
                    difficulty = j;
                    user_chose_difficulty = true;
                }
            }
        }
        for (unsigned j = 0; j < g_aux_goal_aliases.size(); ++j) {
            for (std::string& alias: g_aux_goal_aliases[j]) {
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
    if (!getSettings()->isDifficultyAvailable(difficulty)
        || !getSettings()->isModeAvailable(mode))
    {
        context.say("Mode or difficulty are not permitted on this server");
        return;
    }
    if (context.m_voting)
    {
        // Definitely not the best format as there are extra words,
        // but I'll think how to resolve it
        if (user_chose_mode)
            vote(context, "config mode", g_aux_mode_aliases[mode][0]);
        if (user_chose_difficulty)
            vote(context, "config difficulty", g_aux_difficulty_aliases[difficulty][0]);
        if (user_chose_target)
            vote(context, "config target", g_aux_goal_aliases[goal_target ? 1 : 0][0]);
        return;
    }
    getLobby()->handleServerConfiguration(acting_peer, difficulty, mode, goal_target);
} // process_config_assign
// ========================================================================

void CommandManager::process_spectate(Context& context)
{
    std::string response = "";
    auto& argv = context.m_argv;
    auto acting_peer = context.actingPeer();

    if (getSettings()->isLegacyGPMode() || !getSettings()->isLivePlayers())
        response = "Server doesn't support spectating";

    if (!response.empty())
    {
        context.say(response);
        return;
    }

    if (argv.size() == 1)
    {
        if (acting_peer->isCommandSpectator())
            argv.push_back("0");
        else
            argv.push_back("1");
    }

    int value = -1;
    if (argv.size() < 2 || !StringUtils::fromString(argv[1], value)
            || value < 0 || value > 2)
    {
        error(context);
        return;
    }
    if (value >= 1)
    {
        if (getLobby()->isChildProcess() &&
            getLobby()->isClientServerHost(acting_peer))
        {
            context.say("Graphical client server cannot spectate");
            return;
        }
        AlwaysSpectateMode type = (value == 2 ? ASM_COMMAND_ABSENT : ASM_COMMAND);
        getCrownManager()->setSpectateModeProperly(acting_peer, type);
    }
    else
    {
        getCrownManager()->setSpectateModeProperly(acting_peer, ASM_NONE);
    }
    getLobby()->updateServerOwner(true);
    getLobby()->updatePlayerList();
} // process_spectate
// ========================================================================

void CommandManager::process_addons(Context& context)
{
    auto& argv = context.m_argv;
    auto acting_peer = context.actingPeer();

    bool more = (argv[0] == "moreaddons");
    bool more_own = (argv[0] == "getaddons");
    bool apply_filters = false;
    if (argv.size() == 1)
    {
        argv.push_back("");
        apply_filters = true;
        argv[1] = getAddonPreferredType();
    }
    auto asset_manager = getAssetManager();
    // removed const reference so that I can modify `from`
    // without changing the original container, we copy everything anyway
    std::set<std::string> from =
        (argv[1] == g_type_kart ? asset_manager->getAddonKarts() :
        (argv[1] == g_type_track ? asset_manager->getAddonTracks() :
        (argv[1] == g_type_arena ? asset_manager->getAddonArenas() :
        /*argv[1] == g_type_soccer ?*/ asset_manager->getAddonSoccers()
    )));
    if (apply_filters)
        getAssetManager()->applyAllMapFilters(from, false); // happily the type is never karts in this line
    std::vector<std::pair<std::string, std::vector<std::shared_ptr<NetworkPlayerProfile>>>> result;
    for (const std::string& s: from)
        result.push_back({s, {}});

    auto peers = STKHost::get()->getPeers();
    int num_players = 0;
    for (auto p : peers)
    {
        if (!p || !p->isValidated())
            continue;
        if ((!more_own || p != acting_peer) && (p->isWaitingForGame()
            || !getCrownManager()->canRace(p) || p->isCommandSpectator()))
            continue;
        if (!p->hasPlayerProfiles())
            continue;
        ++num_players;
        const auto& kt = p->getClientAssets();
        const auto& container = (argv[1] == g_type_kart ? kt.first : kt.second);
        for (auto& pr: result)
            if (container.find(pr.first) == container.end())
                pr.second.push_back(p->getMainProfile());
    }
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(result.begin(), result.end(), g);
    std::stable_sort(result.begin(), result.end(),
        [](const std::pair<std::string, std::vector<std::shared_ptr<NetworkPlayerProfile>>>& a,
           const std::pair<std::string, std::vector<std::shared_ptr<NetworkPlayerProfile>>>& b) -> bool {
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
            std::shared_ptr<NetworkPlayerProfile> asker = {};
            if (acting_peer->hasPlayerProfiles())
                asker = acting_peer->getMainProfile();
            for (unsigned i = 0; i < result2.size(); ++i)
            {
                bool present = false;
                for (unsigned j = 0; j < result2[i].second.size(); ++j)
                {
                    if (result2[i].second[j] == asker)
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
                if (s.length() < g_addon_prefix.size() ||
                        s.substr(0, g_addon_prefix.size()) != g_addon_prefix)
                    continue;
                response.push_back(nothing ? ':' : ',');
                nothing = false;
                response.push_back(' ');
                response += s.substr(g_addon_prefix.size());
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
                    response += "\n";
                    response += StringUtils::insertValues(
                            "/installaddon %s , missing for %d player(s):",
                            result[i].first.substr(g_addon_prefix.size()).c_str(),
                            result[i].second.size()
                    );

                    std::sort(result[i].second.begin(), result[i].second.end());
                    for (unsigned j = 0; j < result[i].second.size(); ++j)
                    {
                        response += " " + getLobby()->encodeProfileNameForPeer(result[i].second[j], acting_peer.get());
                    }
                }
            }
        }
    } else {
        response = StringUtils::insertValues(
                "No one in the lobby can play. "
                "Found %d assets on the server.",
                all_have.size());
    }
    context.say(response);
} // process_addons
// ========================================================================

void CommandManager::process_checkaddon(Context& context)
{
    auto& argv = context.m_argv;
    auto acting_peer = context.actingPeer();

    if (argv.size() < 2)
    {
        error(context);
        return;
    }
    if (!validate(context, 1, TFT_ADDON_MAPS, false, true))
        return;
    std::string id = argv[1];
    const unsigned HAS_KART = 1;
    const unsigned HAS_MAP = 2;

    unsigned server_status = 0;
    std::vector<std::shared_ptr<NetworkPlayerProfile>> players[4];

    auto asset_manager = getAssetManager();
    if (asset_manager->hasAddonKart(id))
        server_status |= HAS_KART;
    if (asset_manager->hasAddonTrack(id))
        server_status |= HAS_MAP;
    if (asset_manager->hasAddonArena(id))
        server_status |= HAS_MAP;
    if (asset_manager->hasAddonSoccer(id))
        server_status |= HAS_MAP;

    auto peers = STKHost::get()->getPeers();
    for (auto p : peers)
    {
        if (!p || !p->isValidated() || p->isWaitingForGame()
            || !getCrownManager()->canRace(p) || p->isCommandSpectator()
            || !p->hasPlayerProfiles())
            continue;

        const auto& kt = p->getClientAssets();
        unsigned status = 0;
        if (kt.first.find(id) != kt.first.end())
            status |= HAS_KART;
        if (kt.second.find(id) != kt.second.end())
            status |= HAS_MAP;
        players[status].push_back(p->getMainProfile());
    }

    std::string response = "";
    std::string item_name[3];
    bool needed[3];
    item_name[HAS_KART] = g_type_kart;
    item_name[HAS_MAP] = g_type_map;
    needed[HAS_KART] = (players[HAS_KART].size() + players[HAS_MAP | HAS_KART].size() > 0);
    needed[HAS_MAP] = (players[HAS_MAP].size() + players[HAS_MAP | HAS_KART].size() > 0);
    if (server_status & HAS_KART)
        needed[HAS_KART] = true;
    if (server_status & HAS_MAP)
        needed[HAS_MAP] = true;

    if (!needed[HAS_KART] && !needed[HAS_MAP])
    {
        response = StringUtils::insertValues(
                "Neither server nor clients have addon %s installed",
                argv[1].c_str()
        );
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

            std::vector<std::shared_ptr<NetworkPlayerProfile>> categories[2];
            for (unsigned status = 0; status < 4; ++status)
            {
                for (const std::shared_ptr<NetworkPlayerProfile>& s: players[status])
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
                        response += getLobby()->encodeProfileNameForPeer(categories[i][j], acting_peer.get());
                    }
                    if (categories[i].size() > 5)
                        response += ", ...";
                    response += "\n";
                }
            }
        }
        response.pop_back();
    }
    context.say(response);
} // process_checkaddon
// ========================================================================

void CommandManager::process_id(Context& context)
{
    auto& argv = context.m_argv;

    if (argv.size() < 2)
    {
        error(context);
        return;
    }
    if (!validate(context, 1, TFT_ALL_MAPS, false, true))
        return;

    context.say("Server knows this map, copy it below:\n" + argv[1]);
} // process_id
// ========================================================================

void CommandManager::process_lsa(Context& context)
{
    std::string response = "";
    auto& argv = context.m_argv;

    // "-" left from the 'vanilla' code. Not sure if it's worth it.
    bool has_options = argv.size() > 1 && (
        argv[1].compare("-" + g_type_track) == 0 ||
        argv[1].compare("-" + g_type_arena) == 0 ||
        argv[1].compare("-" + g_type_kart) == 0 ||
        argv[1].compare("-" + g_type_soccer) == 0
    );

    // kimden: what is this, remove
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
        if (argv[1].compare("-" + g_type_track) == 0 ||
            argv[1].compare("-" + g_type_arena) == 0 ||
            argv[1].compare("-" + g_type_kart) == 0 ||
            argv[1].compare("-" + g_type_soccer) == 0)
            type = argv[1].substr(1);
        if ((argv.size() == 2 && type.empty()) || argv.size() == 3)
            text = argv[argv.size() - 1];
    }

    std::set<std::string> total_addons;
    auto asset_manager = getAssetManager();
    if (type.empty() || type.compare(g_type_kart) == 0)
    {
        const auto& collection = asset_manager->getAddonKarts();
        total_addons.insert(collection.begin(), collection.end());
    }
    if (type.empty() || type.compare(g_type_track) == 0)
    {
        const auto& collection = asset_manager->getAddonTracks();
        total_addons.insert(collection.begin(), collection.end());
    }
    if (type.empty() || type.compare(g_type_arena) == 0)
    {
        const auto& collection = asset_manager->getAddonArenas();
        total_addons.insert(collection.begin(), collection.end());
    }
    if (type.empty() || type.compare(g_type_soccer) == 0)
    {
        const auto& collection = asset_manager->getAddonSoccers();
        total_addons.insert(collection.begin(), collection.end());
    }
    std::string msg = "";
    for (auto& addon : total_addons)
    {
        if (!text.empty() && addon.find(text, g_addon_prefix.size()) == std::string::npos)
            continue;

        msg += addon.substr(g_addon_prefix.size());
        msg += ", ";
    }
    if (msg.empty())
        response = "Addon not found";
    else
    {
        msg = msg.substr(0, msg.size() - 2);
        response = "Server's addons: " + msg;
    }
    context.say(response);
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

    if (!validate(context, 1, TFT_ADDON_MAPS, false, true))
        return;
    if (!validate(context, 2, TFT_PRESENT_USERS, false, false))
        return;

    std::string addon_id = argv[1];
    if (StringUtils::startsWith(addon_id, g_addon_prefix))
        addon_id = addon_id.substr(g_addon_prefix.size());
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
    context.say(player_name +
            " has " + (found ? "" : "no ") + "addon " + addon_id);
} // process_pha
// ============================================================================

void CommandManager::process_kick(Context& context)
{
    auto& argv = context.m_argv;
    auto acting_peer = context.actingPeerMaybeNull();
    if (argv.size() < 2)
    {
        error(context);
        return;
    }

    if (!validate(context, 1, TFT_PRESENT_USERS, false, false))
        return;

    std::string player_name = argv[1];

    std::shared_ptr<STKPeer> player_peer = STKHost::get()->findPeerByName(
        StringUtils::utf8ToWide(player_name));
    if (player_name.empty() || !player_peer || player_peer->isAIPeer())
    {
        error(context);
        return;
    }
    if (player_peer->hammerLevel() > 0)
    {
        context.say("This player is the owner of "
            "this server, and is protected from your actions now");
        return;
    }
    if (context.m_voting)
    {
        vote(context, argv[0] + " " + player_name, "");
        return;
    }
    Log::info("CommandManager", "%s kicks %s", (acting_peer.get() ? "Crown player" : "Vote"), player_name.c_str());
    player_peer->kick();
    if (getSettings()->isTrackingKicks())
    {
        std::string auto_report = "[ Auto report caused by kick ]";
        getLobby()->writeOwnReport(player_peer, acting_peer, auto_report);
    }
    if (argv[0] == "kickban")
    {
        Log::info("CommandManager", "%s is now banned", player_name.c_str());
        getSettings()->tempBan(player_name);
        context.say(StringUtils::insertValues(
                "%s is now banned", player_name.c_str()));
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
    getSettings()->tempUnban(player_name);
    context.say(StringUtils::insertValues(
            "%s is now unbanned", player_name.c_str()));
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
    getSettings()->tempBan(player_name);
    context.say(StringUtils::insertValues(
            "%s is now banned", player_name.c_str()));
} // process_ban
// ========================================================================

void CommandManager::process_pas(Context& context)
{
    std::string response;
    std::string player_name;
    auto& argv = context.m_argv;
    auto acting_peer = context.actingPeer();

    if (argv.size() < 2)
    {
        if (acting_peer->getPlayerProfiles().empty())
        {
            Log::warn("CommandManager", "pas: no existing player profiles??");
            error(context);
            return;
        }
        player_name = acting_peer->getMainName();
    }
    else
    {
        if (!validate(context, 1, TFT_PRESENT_USERS, false, false))
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
        std::string msg = StringUtils::insertValues("%s's addon score:",
                player_name.c_str());
        if (scores[AS_KART] != -1)
            msg += StringUtils::insertValues(" karts: %d,", scores[AS_KART]);
        if (scores[AS_TRACK] != -1)
            msg += StringUtils::insertValues(" tracks: %d,", scores[AS_TRACK]);
        if (scores[AS_ARENA] != -1)
            msg += StringUtils::insertValues(" arenas: %d,", scores[AS_ARENA]);
        if (scores[AS_SOCCER] != -1)
            msg += StringUtils::insertValues(" fields: %d,", scores[AS_SOCCER]);
        msg.pop_back();
        response = msg;
    }
    context.say(response);
} // process_pas
// ========================================================================

void CommandManager::process_everypas(Context& context)
{
    auto acting_peer = context.actingPeer();
    auto argv = context.m_argv;

    std::string sorting_type = getAddonPreferredType();
    std::string sorting_direction = "desc";
    if (argv.size() > 1)
        sorting_type = argv[1];
    if (argv.size() > 2)
        sorting_direction = argv[2];
    std::string response = "Addon scores:";
    using Pair = std::pair<std::shared_ptr<NetworkPlayerProfile>, std::vector<int>>;
    std::vector<Pair> result;
    for (const auto& p: STKHost::get()->getPeers())
    {
        if (p->isAIPeer())
            continue;
        if (!p->hasPlayerProfiles())
            continue;
        auto &scores = p->getAddonsScores();
        std::vector<int> overall;
        for (int item = 0; item < AS_TOTAL; item++)
            overall.push_back(scores[item]);
        result.emplace_back(p->getMainProfile(), overall);
    }
    int sorting_idx = -1;
    if (sorting_type == g_type_kart || sorting_type == g_type_kart + "s")
        sorting_idx = 0;
    if (sorting_type == g_type_track || sorting_type == g_type_track + "s")
        sorting_idx = 1;
    if (sorting_type == g_type_arena || sorting_type == g_type_arena + "s")
        sorting_idx = 2;
    if (sorting_type == g_type_soccer || sorting_type == g_type_soccer + "s")
        sorting_idx = 3;
    if (sorting_idx != -1)
    {
        // Sorting order for equal players WILL DEPEND ON NAME DECORATOR!
        // This sorting is clearly bad because we ask lobby every time. Change it later.
        auto lobby = getLobby();
        std::stable_sort(result.begin(), result.end(), [lobby, acting_peer](const Pair& lhs, const Pair& rhs) -> bool {
            return lobby->encodeProfileNameForPeer(lhs.first, acting_peer.get())
                < lobby->encodeProfileNameForPeer(rhs.first, acting_peer.get());
        });
        if (sorting_direction == "asc")
            std::sort(result.begin(), result.end(), [sorting_idx]
                    (const Pair& lhs, const Pair& rhs) -> bool {
                int diff = lhs.second[sorting_idx] - rhs.second[sorting_idx];
                return diff < 0;
            });
        else
            std::stable_sort(result.begin(), result.end(), [sorting_idx]
                    (const Pair& lhs, const Pair& rhs) -> bool {
                int diff = lhs.second[sorting_idx] - rhs.second[sorting_idx];
                return diff > 0;
            });
    }
    // I don't really know if it should be soccer or field, both are used
    // in different situations
    std::vector<std::string> desc = {g_type_kart, g_type_track, g_type_arena, g_type_soccer};
    for (auto& row: result)
    {
        response += "\n";
        std::string decorated_name = getLobby()->encodeProfileNameForPeer(row.first, acting_peer.get());

        bool negative = true;
        for (int item = 0; item < AS_TOTAL; item++)
            negative &= row.second[item] == -1;
        if (negative)
            response += StringUtils::insertValues("%s has no addons", decorated_name.c_str());
        else
        {
            std::string msg = StringUtils::insertValues("%s's addon score:", decorated_name.c_str());
            for (int i = 0; i < AS_TOTAL; i++)
                if (row.second[i] != -1)
                    msg += StringUtils::insertValues(" %ss: %d,", desc[i].c_str(), row.second[i]);
            msg.pop_back();
            response += msg;
        }
    }
    context.say(response);
} // process_everypas
// ========================================================================

void CommandManager::process_sha(Context& context)
{
    auto& argv = context.m_argv;
    if (argv.size() != 2)
    {
        error(context);
        return;
    }
    std::set<std::string> total_addons;

    auto asset_manager = getAssetManager();
    const auto all_karts = asset_manager->getAddonKarts();
    const auto all_tracks = asset_manager->getAddonTracks();
    const auto all_arenas = asset_manager->getAddonArenas();
    const auto all_soccers = asset_manager->getAddonSoccers();
    total_addons.insert(all_karts.begin(), all_karts.end());
    total_addons.insert(all_tracks.begin(), all_tracks.end());
    total_addons.insert(all_arenas.begin(), all_arenas.end());
    total_addons.insert(all_soccers.begin(), all_soccers.end());
    std::string addon_id_test = Addon::createAddonId(argv[1]);
    bool found = total_addons.find(addon_id_test) != total_addons.end();
    context.say(std::string("Server has ") +
            (found ? "" : "no ") + "addon " + argv[1]);
} // process_sha
// ========================================================================

void CommandManager::process_mute(Context& context)
{
    std::shared_ptr<STKPeer> player_peer;
    auto& argv = context.m_argv;
    auto acting_peer = context.actingPeer();

    std::string result_msg;

    if (argv.size() != 2 || argv[1].empty())
    {
        error(context);
        return;
    }

    if (!validate(context, 1, TFT_PRESENT_USERS, false, true))
        return;

    std::string player_name = argv[1];
    getChatManager()->addMutedPlayerFor(acting_peer, player_name);
    context.say(StringUtils::insertValues(
            "Muted player %s", player_name.c_str()));
} // process_mute
// ========================================================================

void CommandManager::process_unmute(Context& context)
{
    std::shared_ptr<STKPeer> player_peer;
    auto& argv = context.m_argv;
    auto acting_peer = context.actingPeer();

    if (argv.size() != 2 || argv[1].empty())
    {
        error(context);
        return;
    }

    // Should also iterate over those names muted - but later.
    if (!validate(context, 1, TFT_PRESENT_USERS, false, true))
        return;

    std::string player_name = argv[1];
    std::string msg;

    if (getChatManager()->removeMutedPlayerFor(acting_peer, player_name))
        msg = "Unmuted player %s";
    else
        msg = "Player %s was already unmuted";

    context.say(
            StringUtils::insertValues(msg, player_name.c_str()));
} // process_unmute
// ========================================================================

void CommandManager::process_listmute(Context& context)
{
    auto acting_peer = context.actingPeer();
    context.say(getChatManager()->getMutedPlayersAsString(acting_peer));
} // process_listmute
// ========================================================================

void CommandManager::process_gnu(Context& context)
{
    auto& argv = context.m_argv;
    auto acting_peer = context.actingPeerMaybeNull();
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
    auto kart_elimination = getKartElimination();
    if (turn_on && kart_elimination->isEnabled())
    {
        context.say("Gnu Elimination mode was already enabled!");
        return;
    }
    if (!turn_on && !kart_elimination->isEnabled())
    {
        context.say("Gnu Elimination mode was already off!");
        return;
    }
    if (turn_on &&
        RaceManager::get()->getMinorMode() != RaceManager::MINOR_MODE_NORMAL_RACE &&
        RaceManager::get()->getMinorMode() != RaceManager::MINOR_MODE_TIME_TRIAL)
    {
        context.say("Gnu Elimination is available only with racing modes");
        return;
    }
    std::string kart;
    if (!turn_on)
    {
        kart = "off";
    }
    else
    {
        if (acting_peer)
        {
            kart = "gnu";
            if (argv.size() > 1 && getAssetManager()->isKartAvailable(argv[1]))
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
        kart_elimination->disable();
        Comm::sendStringToAllPeers("Gnu Elimination is now off");
    }
    else
    {
        kart_elimination->enable(kart);
        Comm::sendStringToAllPeers(kart_elimination->getStartingMessage());
    }
} // process_gnu
// ========================================================================

void CommandManager::process_tell(Context& context)
{
    auto& argv = context.m_argv;
    auto acting_peer = context.actingPeer();

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
    getLobby()->writeOwnReport(acting_peer, acting_peer, ans);
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
        if (getGameSetupFromCtx()->isGrandPrix())
            isGP = true;
        else
            isGnu = true;
    }
    if (isGP)
    {
        // the function will decide itself what to show if nothing is specified:
        // if there are teams, teams will be shown, otherwise players
        msg = getGPManager()->getGrandPrixStandings(isGPPlayers, isGPTeams);
    }
    else if (isGnu)
        msg = getKartElimination()->getStandings();
    context.say(msg);
} // process_standings
// ========================================================================

void CommandManager::process_teamchat(Context& context)
{
    auto acting_peer = context.actingPeer();

    getChatManager()->addTeamSpeaker(acting_peer);
    context.say("Your messages are now addressed to team only");
} // process_teamchat
// ========================================================================

void CommandManager::process_to(Context& context)
{
    auto& argv = context.m_argv;
    auto acting_peer = context.actingPeer();

    if (argv.size() == 1)
    {
        error(context);
        return;
    }
    std::vector<std::string> receivers;
    for (unsigned i = 1; i < argv.size(); ++i)
    {
        if (!validate(context, i, TFT_PRESENT_USERS, false, true))
            return;
        receivers.push_back(argv[i]);
    }
    getChatManager()->setMessageReceiversFor(acting_peer, receivers);
    context.say("Successfully changed chat settings");
} // process_to
// ========================================================================

void CommandManager::process_public(Context& context)
{
    auto acting_peer = context.actingPeer();

    getChatManager()->makeChatPublicFor(acting_peer);
    context.say("Your messages are now public");
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

    if (!validate(context, 1, TFT_ALL_MAPS, false, true))
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
        response = getLobby()->getRecord(track_name, mode_name, reverse_name, laps_count);
    }
#else
    response = "This command is not supported.";
#endif
    context.say(response);
} // process_record
// ========================================================================

void CommandManager::process_power(Context& context)
{
    auto& argv = context.m_argv;
    auto acting_peer = context.actingPeer();

    if (acting_peer->hammerLevel() > 0)
    {
        acting_peer->setHammerLevel(0);
        context.say("You are now a normal player");
        getLobby()->updatePlayerList();
        return;
    }
    int new_level = 1;
    if (argv[0] == "power2")
        new_level = 2;

    std::string username = "";
    uint32_t online_id = 0;
    const auto& profiles = acting_peer->getPlayerProfiles();
    if (!profiles.empty())
    {
        username = StringUtils::wideToUtf8(profiles[0]->getName());
        online_id = profiles[0]->getOnlineId();
    }

    std::string password = getSettings()->getPowerPassword(new_level);
    bool bad_password = (password.empty() || argv.size() <= 1 || argv[1] != password);
    bool good_player = (getTeamManager()->isInHammerWhitelist(username, new_level)
            && online_id != 0);
    if (bad_password && !good_player)
    {
        context.say("You need to provide the password to have the power");
        return;
    }
    acting_peer->setHammerLevel(new_level);
    context.say("Now you have the power!");
    getLobby()->updatePlayerList();
} // process_power
// ========================================================================
void CommandManager::process_length(Context& context)
{
    context.say(getSettings()->getLapRestrictionsAsString());
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
    double value = std::max<double>(0.0, temp_double);
    getSettings()->setMultiplier(value);
    Comm::sendStringToAllPeers(StringUtils::insertValues(
                "Game length is now %f x default", value));
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
    int value = std::max<int>(0, temp_int);
    getSettings()->setFixedLapCount(value);
    Comm::sendStringToAllPeers(StringUtils::insertValues(
                "Game length is now %d", value));
} // process_length_fixed
// ========================================================================
void CommandManager::process_length_clear(Context& context)
{
    getSettings()->resetLapRestrictions();
    Comm::sendStringToAllPeers("Game length will be chosen by players");
} // process_length_clear
// ========================================================================

void CommandManager::process_direction(Context& context)
{
    Comm::sendStringToAllPeers(getSettings()->getDirectionAsString());
} // process_direction
// ========================================================================

void CommandManager::process_direction_assign(Context& context)
{
    auto& argv = context.m_argv;
    if (argv.size() < 2)
    {
        error(context);
        return;
    }
    int temp_int = -1;
    if (!StringUtils::parseString<int>(argv[1], &temp_int) || !getSettings()->setDirection(temp_int))
    {
        error(context);
        return;
    }
    Comm::sendStringToAllPeers(getSettings()->getDirectionAsString(true));
} // process_direction_assign
// ========================================================================

void CommandManager::process_queue(Context& context)
{
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
    context.say(msg);
} // process_queue
// ========================================================================

void CommandManager::process_queue_push(Context& context)
{
    auto& argv = context.m_argv;
    auto acting_peer = context.actingPeer();
    auto peer = context.peer();

    if (argv.size() < 3)
    {
        error(context);
        return;
    }
    int mask = get_queue_mask(argv[0]);
    bool to_front = (argv[1] == "push_front");

    auto asset_manager = getAssetManager();
    if (argv.size() == 3 && argv[2] == "-") // kept until there's filter-type replacement
        argv[2] = asset_manager->getRandomMap();
    else if (argv.size() == 3 && argv[2] == "-addon") // kept until there's filter-type replacement
        argv[2] = asset_manager->getRandomAddonMap();

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

            // kimden: this looks horrendous but I remember it was worth it back then
            if (hasTypo(acting_peer, peer, context.m_voting, context.m_argv, context.m_cmd,
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

            msg += StringUtils::insertValues(
                    "Pushed { %s } to the %s of %s, current queue size: %d",
                    filter_text.c_str(),
                    (to_front ? "front" : "back"),
                    get_queue_name(x).c_str(),
                    get_queue(x).size()
            );
            msg += "\n";
        }
    }

    msg.pop_back();
    Comm::sendStringToAllPeers(msg);
    getLobby()->updatePlayerList();
} // process_queue_push
// ========================================================================

void CommandManager::process_queue_pop(Context& context)
{
    std::string msg = "";
    auto& argv = context.m_argv;

    int mask = get_queue_mask(argv[0]);
    bool from_back = (argv[1] == "pop_back");

    for (int x = QM_START; x < QM_END; x <<= 1)
    {
        if (mask & x)
        {
            int another = another_cyclic_queue(x);
            if (get_queue(x).empty()) {
                msg += "The " + get_queue_name(x) + " was empty before.\n";
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
    Comm::sendStringToAllPeers(msg);
    getLobby()->updatePlayerList();
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
    Comm::sendStringToAllPeers(msg);
    getLobby()->updatePlayerList();
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
    Comm::sendStringToAllPeers(msg);
    getLobby()->updatePlayerList();
} // process_queue_shuffle
// ========================================================================

void CommandManager::process_allowstart(Context& context)
{
    context.say(getSettings()->getAllowedToStartAsString());
} // process_allowstart
// ========================================================================

void CommandManager::process_allowstart_assign(Context& context)
{
    auto& argv = context.m_argv;
    // kimden thinks the parts 2-3 should be moved into validation part
    // of the settings. Please do it later. In this function it's not done
    // because of an extra cast.
    if (argv.size() == 1 || !(argv[1] == "0" || argv[1] == "1"))
    {
        error(context);
        return;
    }
    getSettings()->setAllowedToStart(argv[1] != "0");
    Comm::sendStringToAllPeers(getSettings()->getAllowedToStartAsString(true));
} // process_allowstart_assign
// ========================================================================

void CommandManager::process_shuffle(Context& context)
{
    context.say(getSettings()->getWhetherShuffledGPGridAsString());
} // process_shuffle
// ========================================================================

void CommandManager::process_shuffle_assign(Context& context)
{
    auto& argv = context.m_argv;
    // Move validation to lobby settings.
    if (argv.size() == 1 || !(argv[1] == "0" || argv[1] == "1"))
    {
        error(context);
        return;
    }
    getSettings()->setGPGridShuffled(argv[1] != "0");
    Comm::sendStringToAllPeers(getSettings()->getWhetherShuffledGPGridAsString(true));
} // process_shuffle_assign
// ========================================================================

void CommandManager::process_timeout(Context& context)
{
    int seconds;
    auto& argv = context.m_argv;
    if (argv.size() < 2 || !StringUtils::parseString(argv[1], &seconds) || seconds <= 0)
    {
        error(context);
        return;
    }
    getLobby()->setTimeoutFromNow(seconds);
    getLobby()->updatePlayerList();
    context.say("Successfully changed timeout");
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
    if (!validate(context, 2, TFT_PRESENT_USERS, false, true))
        return;
    std::string player = argv[2];

    bool allowed_color = false;
    if (!argv[1].empty())
    {
        std::string temp(1, argv[1][0]);
        if (getTeamManager()->getAvailableTeams().find(temp) != std::string::npos)
            allowed_color = true;
    }
    int team = TeamUtils::getIndexByCode(argv[1]);
    // Resetting should be allowed anyway
    if (!allowed_color && team != TeamUtils::NO_TEAM)
    {
        context.say(StringUtils::insertValues(
                "Color %s is not allowed", argv[1]));
        return;
    }
    getTeamManager()->setTemporaryTeamInLobby(player, team);

    getLobby()->updatePlayerList();
} // process_team
// ========================================================================

void CommandManager::process_swapteams(Context& context)
{
    auto& argv = context.m_argv;

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
    getTeamManager()->shuffleTemporaryTeams(permutation_map_int);
    context.say(msg); // todo make public?
    getLobby()->updatePlayerList();
} // process_swapteams
// ========================================================================

void CommandManager::process_resetteams(Context& context)
{
    getTeamManager()->clearTemporaryTeams();
    context.say("Teams are reset now");
    getLobby()->updatePlayerList();
} // process_resetteams
// ========================================================================

void CommandManager::process_randomteams(Context& context)
{
    auto& argv = context.m_argv;
    int teams_number = -1;
    int final_number = -1;
    int players_number = -1;
    if (argv.size() >= 2)
        StringUtils::parseString(argv[1], &teams_number);
    if (context.m_voting)
    {
        if (argv.size() > 1)
            vote(context, "randomteams", argv[1]);
        else
            vote(context, "randomteams", "");
        return;
    }
    if (!getTeamManager()->assignRandomTeams(teams_number, &final_number, &players_number))
    {
        std::string msg;
        if (players_number == 0)
            msg = "No one can play!";
        else
            msg = "Teams are currently not allowed";
        context.say(msg);
        return;
    }
    context.say(StringUtils::insertValues(
            "Created %d teams for %d players", final_number, players_number));
    getLobby()->updatePlayerList();
} // process_randomteams
// ========================================================================

void CommandManager::process_resetgp(Context& context)
{
    auto& argv = context.m_argv;
    if (argv.size() >= 2) {
        int number_of_games;
        if (!StringUtils::parseString(argv[1], &number_of_games)
            || number_of_games <= 0)
        {
            error(context);
            return;
        }
        getGameSetupFromCtx()->setGrandPrixTrack(number_of_games);
    }
    getGPManager()->resetGrandPrix();
    Comm::sendStringToAllPeers("GP is now reset");
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
        if (!validate(context, 2, TFT_PRESENT_USERS, false, true))
            return;
        std::string player = argv[2];
        getTeamManager()->addPlayerToCategory(player, category);
        getLobby()->updatePlayerList();
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
        if (!validate(context, 2, TFT_PRESENT_USERS, false, true))
            return;
        player = argv[2];
        getTeamManager()->erasePlayerFromCategory(player, category);
        getLobby()->updatePlayerList();
        return;
    }
    if (argv[0] == "catshow")
    {
        int displayed = -1;
        // Validation should happen in LS
        if (argv.size() >= 2)
        {
            if (argv.size() != 3 || !StringUtils::parseString(argv[2], &displayed)
                    || displayed < -1 || displayed > 1)
            {
                error(context);
                return;
            }
        }
        std::string category = argv[1];
        getTeamManager()->makeCategoryVisible(category, displayed);
        getLobby()->updatePlayerList();
        return;
    }
} // process_cat
// ========================================================================

void CommandManager::process_troll(Context& context)
{
    std::string msg;

    auto hit_processor = getHitProcessor();
    if (hit_processor->isAntiTrollActive())
        msg = "Trolls will be kicked";
    else
        msg = "Trolls can stay";
    context.say(msg);
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
    auto hit_processor = getHitProcessor();
    if (argv[1] == "0")
    {
        hit_processor->setAntiTroll(false);
        msg = "Trolls can stay";
    } else {
        hit_processor->setAntiTroll(true);
        msg = "Trolls will be kicked";
    }
    Comm::sendStringToAllPeers(msg);
} // process_troll_assign
// ========================================================================

void CommandManager::process_hitmsg(Context& context)
{
    std::string msg;

    auto hit_processor = getHitProcessor();
    if (hit_processor->showTeammateHits())
        msg = "Teammate hits are sent to all players";
    else
        msg = "Teammate hits are not sent";
    context.say(msg);
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
    auto hit_processor = getHitProcessor();
    if (argv[1] == "0")
    {
        hit_processor->setShowTeammateHits(false);
        msg = "Teammate hits will not be sent";
    } else {
        hit_processor->setShowTeammateHits(true);
        msg = "Teammate hits will be sent to all players";
    }
    Comm::sendStringToAllPeers(msg);
} // process_hitmsg_assign
// ========================================================================

void CommandManager::process_teamhit(Context& context)
{
    std::string msg;

    auto hit_processor = getHitProcessor();
    if (hit_processor->isTeammateHitMode())
        msg = "Teammate hits are punished";
    else
        msg = "Teammate hits are not punished";
    context.say(msg);
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
    auto hit_processor = getHitProcessor();
    if (argv[1] == "0")
    {
        hit_processor->setTeammateHitMode(false);
        msg = "Teammate hits are not punished now";
    }
    else
    {
        hit_processor->setTeammateHitMode(true);
        msg = "Teammate hits are punished now";
    }
    Comm::sendStringToAllPeers(msg);
} // process_teamhit_assign
// ========================================================================

void CommandManager::process_scoring(Context& context)
{
    context.say(getGPManager()->getScoringAsString());
} // process_scoring
// ========================================================================

void CommandManager::process_scoring_assign(Context& context)
{
    std::string msg;
    auto& argv = context.m_argv;

    std::string cmd2;
    CommandManager::restoreCmdByArgv(cmd2, argv, ' ', '"', '"', '\\', 1);
    if (getGPManager()->trySettingGPScoring(cmd2))
        Comm::sendStringToAllPeers("Scoring set to \"" + cmd2 + "\"");
    else
        context.say("Scoring could not be parsed from \"" + cmd2 + "\"");
} // process_scoring_assign
// ========================================================================

void CommandManager::process_register(Context& context)
{
    auto& argv = context.m_argv;
    auto acting_peer = context.actingPeer();

    if (!acting_peer->hasPlayerProfiles())
        return;
    int online_id = acting_peer->getMainProfile()->getOnlineId();
    if (online_id <= 0)
    {
        context.say("Please join with a valid online STK account.");
        return;
    }
    std::string ans = "";
    for (unsigned i = 1; i < argv.size(); ++i)
    {
        if (i > 1)
            ans.push_back(' ');
        ans += argv[i];
    }
    if (getLobby()->writeOnePlayerReport(acting_peer, getSettings()->getRegisterTableName(),
        ans))
        context.say("Your registration request is being processed");
    else
        context.say("Sorry, an error occurred. Please try again.");
} // process_register
// ========================================================================

void CommandManager::process_muteall(Context& context)
{
    auto& argv = context.m_argv;
    auto acting_peer = context.actingPeer();
    auto tournament = getTournament();
    if (!tournament)
    {
        error(context, true);
        return;
    }
    if (!acting_peer->hasPlayerProfiles())
        return;
    std::string peer_username = acting_peer->getMainName();

    int op = SWF_OP_FLIP;
    if (argv.size() >= 2 && argv[1] == "0")
        op = SWF_OP_REMOVE;
    else if (argv.size() >= 2 && argv[1] == "1")
        op = SWF_OP_ADD;

    int status = tournament->editMuteall(peer_username, op);

    std::string msg;
    if (status)
        msg = "You are now receiving messages only from players and referees";
    else
        msg = "You are now receiving messages from spectators too";
    context.say(msg);
} // process_muteall
// ========================================================================

void CommandManager::process_game(Context& context)
{
    auto& argv = context.m_argv;
    auto tournament = getTournament();
    if (!tournament)
    {
        error(context, true);
        return;
    }

    int old_game_number;
    int old_duration;
    int old_addition;
    tournament->getGameCmdInput(old_game_number, old_duration, old_addition);
    int new_game_number = tournament->getNextGameNumber();
    int new_duration = tournament->getDefaultDuration(); // Was m_length for argv.size >= 2
    int new_addition = 0;

    if (1 < argv.size())
    {
        // What's the difference between m_length and ServerConfig::fixedlap ??
        bool bad = false;

        if (!StringUtils::parseString(argv[1], &new_game_number))
            bad = true;
        
        if (!bad && argv.size() >= 3 && !StringUtils::parseString(argv[2], &new_duration))
            bad = true;

        if (!bad && argv.size() >= 4 && !StringUtils::parseString(argv[3], &new_addition))
            bad = true;
        
        if (!bad && !tournament->isValidGameCmdInput(new_game_number, new_duration, new_addition))
        {
            bad = true;
        }

        if (bad)
        {
            // error(context) ?
            context.say(StringUtils::insertValues(
                "Please specify a correct number. "
                "Format: /game [number %d..%d] [length in minutes] [0..59 additional seconds]",
                tournament->minGameNumber(),
                tournament->maxGameNumber()));
            return;
        }
    }
    tournament->setGameCmdInput(new_game_number, new_duration, new_addition);
    getSettings()->setFixedLapCount(new_duration);

    if (tournament->hasColorsSwapped(new_game_number) ^ tournament->hasColorsSwapped(old_game_number))
    {
        getTeamManager()->changeColors();
        getLobby()->updatePlayerList();
    }

    if (tournament->hasGoalsLimit(new_game_number) ^ tournament->hasGoalsLimit(old_game_number))
        getLobby()->changeLimitForTournament(tournament->hasGoalsLimit());

    std::string msg = StringUtils::insertValues(
        "Ready to start game %d for %d %s", new_game_number, new_duration,
        (tournament->hasGoalsLimit() ? "goals" : "minutes"));

    if (!tournament->hasGoalsLimit() && new_addition > 0)
    {
        msg += StringUtils::insertValues(" %d seconds", new_addition);
        getSettings()->setFixedLapCount(getSettings()->getFixedLapCount() + 1);
    }
    Comm::sendStringToAllPeers(msg);
    Log::info("CommandManager", "SoccerMatchLog: Game number changed from %d to %d",
        old_game_number, new_game_number);
} // process_game
// ========================================================================

void CommandManager::process_role(Context& context)
{
    auto& argv = context.m_argv;
    auto tournament = getTournament();
    if (!tournament)
    {
        error(context, true);
        return;
    }
    if (argv.size() < 3)
    {
        error(context);
        return;
    }
    if (argv[1].length() > argv[2].length())
    {
        swap(argv[1], argv[2]);
    }
    if (!validate(context, 2, TFT_PRESENT_USERS, false, true))
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
    char role_char = role[0];
    if (role_char >= 'A' && role_char <= 'Z')
        role_char += 'a' - 'A';
    std::set<std::string> changed_usernames;
    if (!username.empty())
    {
        if (username[0] == '#')
            changed_usernames = getTeamManager()->getPlayersInCategory(username.substr(1));
        else
            changed_usernames.insert(username);
    }
    for (const std::string& u: changed_usernames)
    {
        tournament->eraseFromAllTournamentCategories(u, permanent);
        std::string role_changed = "The referee has updated your role - you are now %s";
        std::shared_ptr<STKPeer> player_peer = STKHost::get()->findPeerByName(
            StringUtils::utf8ToWide(u));
        std::vector<std::string> missing_assets;
        if (player_peer)
            missing_assets = getAssetManager()->getMissingAssets(player_peer);
        bool fail = false;
        switch (role_char)
        {
            case 'r':
            {
                fail = !missing_assets.empty();
                if (tournament->hasColorsSwapped())
                {
                    tournament->setTeam(KART_TEAM_BLUE, u, permanent);
                }
                else
                {
                    tournament->setTeam(KART_TEAM_RED, u, permanent);
                }
                if (player_peer)
                {
                    if (player_peer->hasPlayerProfiles())
                        getTeamManager()->setTeamInLobby(player_peer->getMainProfile(), KART_TEAM_RED);
                    Comm::sendStringToPeer(player_peer,
                            StringUtils::insertValues(role_changed, Conversions::roleCharToString(role_char)));
                }
                break;
            }
            case 'b':
            {
                fail = !missing_assets.empty();
                if (tournament->hasColorsSwapped())
                {
                    tournament->setTeam(KART_TEAM_RED, u, permanent);
                }
                else
                {
                    tournament->setTeam(KART_TEAM_BLUE, u, permanent);
                }
                if (player_peer)
                {
                    if (player_peer->hasPlayerProfiles())
                        getTeamManager()->setTeamInLobby(player_peer->getMainProfile(), KART_TEAM_BLUE);
                    Comm::sendStringToPeer(player_peer,
                            StringUtils::insertValues(role_changed, Conversions::roleCharToString(role_char)));
                }
                break;
            }
            case 'j':
            {
                fail = !missing_assets.empty();
                tournament->setReferee(u, permanent);
                if (player_peer)
                {
                    if (player_peer->hasPlayerProfiles())
                        getTeamManager()->setTeamInLobby(player_peer->getMainProfile(), KART_TEAM_NONE);
                    Comm::sendStringToPeer(player_peer,
                            StringUtils::insertValues(role_changed, Conversions::roleCharToString(role_char)));
                }
                break;
            }
            case 's':
            {
                if (player_peer)
                {
                    if (player_peer->hasPlayerProfiles())
                        getTeamManager()->setTeamInLobby(player_peer->getMainProfile(), KART_TEAM_NONE);
                    Comm::sendStringToPeer(player_peer,
                            StringUtils::insertValues(role_changed, Conversions::roleCharToString(role_char)));
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
        context.say(msg);
    }
    getLobby()->updatePlayerList();
} // process_role
// ========================================================================

void CommandManager::process_stop(Context& context)
{
    if (!getTournament())
    {
        error(context, true);
        return;
    }
    World* w = World::getWorld();
    if (!w)
        return;
    SoccerWorld *sw = dynamic_cast<SoccerWorld*>(w);
    sw->stop();
    Comm::sendStringToAllPeers("The game is stopped.");
    Log::info("CommandManager", "SoccerMatchLog: The game is stopped");
} // process_stop
// ========================================================================

void CommandManager::process_go(Context& context)
{
    if (!getTournament())
    {
        error(context, true);
        return;
    }
    World* w = World::getWorld();
    if (!w)
        return;
    SoccerWorld *sw = dynamic_cast<SoccerWorld*>(w);
    sw->resume();
    Comm::sendStringToAllPeers("The game is resumed.");
    Log::info("CommandManager", "SoccerMatchLog: The game is resumed");
} // process_go
// ========================================================================

void CommandManager::process_lobby(Context& context)
{
    if (!getTournament())
    {
        error(context, true);
        return;
    }
    World* w = World::getWorld();
    if (!w)
        return;
    SoccerWorld *sw = dynamic_cast<SoccerWorld*>(w);
    sw->allToLobby();
    Comm::sendStringToAllPeers("The game will be restarted.");
} // process_lobby
// ========================================================================

void CommandManager::process_init(Context& context)
{
    auto& argv = context.m_argv;
    if (!getTournament())
    {
        error(context, true);
        return;
    }
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
        context.say("Please set the count "
            "when the karts are ready. Setting the initial count "
            "in the lobby is not implemented yet, sorry.");
        return;
    }
    SoccerWorld *sw = dynamic_cast<SoccerWorld*>(w);
    sw->setInitialCount(red, blue);
    sw->tellCountToEveryoneInGame();
} // process_init
// ========================================================================

void CommandManager::process_mimiz(Context& context)
{
    auto& argv = context.m_argv;
    auto& cmd = context.m_cmd;
    std::string msg;
    if (argv.size() == 1)
        msg = "please provide text";
    else
        msg = cmd.substr(argv[0].length() + 1);
    context.say(msg);
} // process_mimiz
// ========================================================================

void CommandManager::process_test(Context& context)
{
    auto& argv = context.m_argv;
    auto acting_peer = context.actingPeerMaybeNull();
    if (argv.size() == 1)
    {
        context.say("/test is now deprecated. Use /test *2 [something] [something]");
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
    if (acting_peer.get() && acting_peer->hasPlayerProfiles())
    {
        username = getLobby()->encodeProfileNameForPeer(
            acting_peer->getMainProfile(), acting_peer.get());
    }
    username = "{" + argv[1].substr(4) + "} " + username;
    Comm::sendStringToAllPeers(username + ", " + argv[2] + ", " + argv[3]);
} // process_test
// ========================================================================

void CommandManager::process_slots(Context& context)
{
    int current = getSettings()->getCurrentMaxPlayersInGame();
    context.say(StringUtils::insertValues(
            "Number of slots is currently %d",
            current));
} // process_slots
// ========================================================================

void CommandManager::process_slots_assign(Context& context)
{
    if (getCrownManager()->hasOnlyHostRiding())
    {
        context.say("Changing slots is not possible in the singleplayer mode");
        return;
    }
    auto& argv = context.m_argv;
    bool fail = false;
    int number = 0;
    if (argv.size() < 2 || !StringUtils::parseString<int>(argv[1], &number))
        fail = true;
    else if (number <= 0 || number > getSettings()->getServerMaxPlayers())
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
    getSettings()->setCurrentMaxPlayersInGame((unsigned)number);
    getLobby()->updatePlayerList();
    Comm::sendStringToAllPeers(StringUtils::insertValues(
            "Number of playable slots is now %s",
            number));
} // process_slots_assign
// ========================================================================

void CommandManager::process_time(Context& context)
{
    context.say(StringUtils::insertValues(
            "Server time: %s",
            StkTime::getLogTime().c_str()));
} // process_time
// ========================================================================

void CommandManager::process_result(Context& context)
{
    // Note that for soccer world we need the peer that used the command,
    // not the one under whose name it's done
    auto peer = context.peer();

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
    context.say(msg);
} // process_result
// ========================================================================

void CommandManager::process_preserve(Context& context)
{
    context.say(getSettings()->getPreservedSettingsAsString());
} // process_preserve
// ========================================================================

void CommandManager::process_preserve_assign(Context& context)
{
    auto& argv = context.m_argv;

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
        getSettings()->eraseFromPreserved(argv[1]);
    }
    else
    {
        msg = StringUtils::insertValues(
            "'%s' is now preserved on server reset", argv[1].c_str());
        getSettings()->insertIntoPreserved(argv[1]);
    }
    Comm::sendStringToAllPeers(msg);
} // process_preserve_assign
// ========================================================================

void CommandManager::process_history(Context& context)
{
    auto tournament = getTournament();
    if (!tournament)
    {
        error(context, true);
        return;
    }
    std::string msg = "Map history:";
    std::vector<std::string> arenas = tournament->getMapHistory();
    for (unsigned i = 0; i < arenas.size(); i++)
        msg += StringUtils::insertValues(" [%d]: %s", i, arenas[i].c_str());
    context.say(msg);
} // process_history
// ========================================================================

void CommandManager::process_history_assign(Context& context)
{
    auto& argv = context.m_argv;
    auto tournament = getTournament();
    if (!tournament)
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
    if (!validate(context, 2, TFT_ALL_MAPS, false, false))
        return;
    std::string id = argv[2];
    if (!tournament->assignToHistory(index, id))
    {
        error(context);
        return;
    }

    context.say(StringUtils::insertValues(
            "Assigned [%d] to %s in the map history", index, id.c_str()));
} // process_history_assign
// ========================================================================

void CommandManager::process_voting(Context& context)
{
    context.say(StringUtils::insertValues("Voting method: %d",
            getMapVoteHandler()->getAlgorithm()));
} // process_voting
// ========================================================================

void CommandManager::process_voting_assign(Context& context)
{
    auto& argv = context.m_argv;

    std::string msg = "";
    if (argv.size() < 2)
    {
        error(context);
        return;
    }
    int value;
    if (!StringUtils::fromString(argv[1], value) || value < 0 || value > 1)
    {
        error(context);
        return;
    }
    getMapVoteHandler()->setAlgorithm(value);
    context.say(StringUtils::insertValues(
            "Set voting method to %s", value));
} // process_voting_assign
// ========================================================================

void CommandManager::process_why_hourglass(Context& context)
{
    std::string response;
    std::string player_name;
    auto& argv = context.m_argv;
    auto acting_peer = context.actingPeer();

    if (argv.size() < 2)
    {
        if (acting_peer->getPlayerProfiles().empty())
        {
            Log::warn("CommandManager", "whyhourglass: no existing player profiles??");
            error(context);
            return;
        }
        player_name = acting_peer->getMainName();
    }
    else
    {
        if (!validate(context, 1, TFT_PRESENT_USERS, false, false))
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

    std::string encoded_name = getLobby()->encodeProfileNameForPeer(
        player_peer->getMainProfile(), acting_peer.get());

    context.say(StringUtils::insertValues(
            getCrownManager()->getWhyPeerCannotPlayAsString(player_peer),
            encoded_name));
} // process_why_hourglass
// ========================================================================

void CommandManager::process_available_teams(Context& context)
{
    context.say(StringUtils::insertValues(
            "Currently available teams: \"%s\"",
            getTeamManager()->getInternalAvailableTeams().c_str()));
} // process_available_teams
// ========================================================================

void CommandManager::process_available_teams_assign(Context& context)
{
    auto& argv = context.m_argv;

    if (argv.size() < 2)
    {
        error(context);
        return;
    }
    std::string value = "";
    std::set<char> value_set;
    std::string ignored = "";
    std::set<char> ignored_set;
    std::string msg = "";
    if (argv[1] == "all")
    {
        for (int i = 1; i <= TeamUtils::getNumberOfTeams(); i++)
            value_set.insert(TeamUtils::getTeamByIndex(i).getPrimaryCode()[0]);
    }
    else
    {
        for (char& c: argv[1])
        {
            std::string temp(1, c);
            if (TeamUtils::getIndexByCode(temp) == TeamUtils::NO_TEAM)
                ignored_set.insert(c);
            else
                value_set.insert(c);
        }
    }
    for (char c: ignored_set)
        ignored.push_back(c);
    for (char c: value_set)
        value.push_back(c);
    getTeamManager()->setInternalAvailableTeams(value);
    msg = StringUtils::insertValues("Set available teams to \"%s\"", value);
    if (!ignored.empty())
        msg += StringUtils::insertValues(
                ", but teams \"%s\" were not recognized", ignored);
    context.say(msg);
} // process_available_teams_assign
// ========================================================================

void CommandManager::process_cooldown(Context& context)
{
    context.say(StringUtils::insertValues(
            "Cooldown for starting the game: %d",
            getSettings()->getLobbyCooldown()));
} // process_cooldown
// ========================================================================

void CommandManager::process_cooldown_assign(Context& context)
{
    auto& argv = context.m_argv;

    if (argv.size() < 2)
    {
        error(context);
        return;
    }
    int new_cooldown = -1;
    // kimden: figure out what's with epsilons in STK
    if (!StringUtils::parseString<int>(argv[1], &new_cooldown) || new_cooldown < 0)
    {
        error(context);
        return;
    }
    getSettings()->setLobbyCooldown(new_cooldown);

    context.say(StringUtils::insertValues(
            "Set cooldown for starting the game to %d",
            new_cooldown));
} // process_cooldown_assign
// ========================================================================

void CommandManager::process_countteams(Context& context)
{
    context.say(StringUtils::insertValues("Teams composition: %s",
            getTeamManager()->countTeamsAsString().c_str()));
} // process_cooldown
// ========================================================================

void CommandManager::process_net(Context& context)
{
    std::string response;
    std::string player_name;
    auto& argv = context.m_argv;
    auto acting_peer = context.actingPeer();

    if (argv.size() < 2)
    {
        if (acting_peer->getPlayerProfiles().empty())
        {
            Log::warn("CommandManager", "net: no existing player profiles??");
            error(context);
            return;
        }
        player_name = acting_peer->getMainName();
    }
    else
    {
        if (!validate(context, 1, TFT_PRESENT_USERS, false, false))
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

    auto enet_peer = player_peer->getENetPeer();
    std::string decorated_name = getLobby()->encodeProfileNameForPeer(
            player_peer->getMainProfile(), acting_peer.get());

    context.say(StringUtils::insertValues(
        "%s's network: ping: %s, packet loss: %s",
        decorated_name.c_str(),
        player_peer->getAveragePing(),
        player_peer->getPacketLoss()
    ));
} // process_net
// ========================================================================

void CommandManager::process_everynet(Context& context)
{
    auto acting_peer = context.actingPeer();
    auto argv = context.m_argv;

    std::string response = "Network stats:";
    using Pair = std::pair<std::shared_ptr<NetworkPlayerProfile>, std::vector<int>>;
    std::vector<Pair> result;
    for (const auto& p: STKHost::get()->getPeers())
    {
        // if (p->isAIPeer())
        //     continue;
        if (!p->hasPlayerProfiles())
            continue;
        
        auto enet_peer = p->getENetPeer();
        std::vector<int> overall;
        overall.push_back(p->getAveragePing());
        overall.push_back(p->getPacketLoss());
        
        result.emplace_back(p->getMainProfile(), overall);
    }

    // Sorting order for equal players WILL DEPEND ON NAME DECORATOR!
    // This sorting is clearly bad because we ask lobby every time. Change it later.
    auto lobby = getLobby();
    std::stable_sort(result.begin(), result.end(), [lobby, acting_peer](const Pair& lhs, const Pair& rhs) -> bool {
        return lobby->encodeProfileNameForPeer(lhs.first, acting_peer.get())
            < lobby->encodeProfileNameForPeer(rhs.first, acting_peer.get());
    });
    std::sort(result.begin(), result.end(), [](const Pair& lhs, const Pair& rhs) -> bool {
        int diff = lhs.second[0] - rhs.second[0];
        return diff > 0;
    });
    for (auto& row: result)
    {
        response += "\n";
        std::string decorated_name = getLobby()->encodeProfileNameForPeer(row.first, acting_peer.get());

        std::string msg = StringUtils::insertValues(
            "%s's network: ping: %s, packet loss: %s",
            decorated_name.c_str(),
            row.second[0],
            row.second[1]
        );

        response += msg;
    }
    context.say(response);
} // process_everynet
// ========================================================================

void CommandManager::process_temp250318(Context& context)
{
    auto& argv = context.m_argv;

    int value = 0;
    if (argv.size() < 2 || !StringUtils::parseString<int>(argv[1], &value))
    {
        error(context);
        return;
    }
    auto settings = getSettings();
    settings->m_legacy_gp_mode         = ((value >> 1) & 1);
    settings->m_legacy_gp_mode_started = ((value >> 0) & 1);
    context.say(StringUtils::insertValues(
            "ok value = %d", value));
} // process_temp250318
// ========================================================================

void CommandManager::special(Context& context)
{
    auto command = context.command();
    auto cmd = context.m_cmd;

    // This function is used as a function for /vote and possibly several
    // other future special functions that are never executed "as usual"
    // but need to be displayed in /commands output. So, in fact, this
    // function is only a placeholder and should be never executed.
    Log::warn("CommandManager", "Command %s was invoked "
        "but not implemented or unavailable for this server",
        command->getFullName().c_str());

    context.say(StringUtils::insertValues(
        "This command (%s) is not implemented, or "
        "not available for this server. "
        "If you believe that is a bug, please report it. Full input:\n"
        "/%s", command->getFullName(), cmd));
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

void CommandManager::restoreCmdByArgv(std::string& cmd,
        std::vector<std::string>& argv, char c, char d, char e, char f,
        int from)
{
    cmd.clear();
    for (int i = from; i < (int)argv.size(); ++i) {
        if (i > from) {
            cmd.push_back(c);
        }
        cmd += StringUtils::quoteEscape(argv[i], c, d, e, f);
    }
}   // restoreCmdByArgv
// ========================================================================

bool CommandManager::validate(Context& ctx, int idx,
    TypoFixerType fixer_type, bool case_sensitive, bool allow_as_is)
{
    const SetTypoFixer& stf = getFixer(fixer_type);

    // We show 3 options by default
    return !hasTypo(ctx.actingPeerMaybeNull(), ctx.peerMaybeNull(), ctx.m_voting, ctx.m_argv, ctx.m_cmd, idx,
            stf, 3, case_sensitive, allow_as_is);
}   // validate
//-----------------------------------------------------------------------------

bool CommandManager::hasTypo(std::shared_ptr<STKPeer> acting_peer, std::shared_ptr<STKPeer> peer, bool voting,
    std::vector<std::string>& argv, std::string& cmd, int idx,
    const SetTypoFixer& stf, int top, bool case_sensitive, bool allow_as_is,
    bool dont_replace, int subidx, int substr_l, int substr_r)
{
    if (!acting_peer.get()) // voted
        return false;
    std::string username = "";
    if (peer->hasPlayerProfiles())
        username = peer->getMainName();
    auto it = m_user_last_correct_argument.find(username);
    if (it != m_user_last_correct_argument.end() &&
            std::make_pair(idx, subidx) <= it->second)
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
        // kimden: HERE IT SHOULD BE SENT TO ANOTHER PEER
        Comm::sendStringToPeer(peer, "Command " + cmd + " not found");
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
        m_user_saved_acting_peer[username] = acting_peer;

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
        Comm::sendStringToPeer(peer, response);
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

void CommandManager::onServerSetup()
{
    update();
} // onServerSetup
// ========================================================================

void CommandManager::onStartSelection()
{
    m_votables["start"].resetAllVotes();
    m_votables["config"].resetAllVotes();
    m_votables["gnu"].resetAllVotes();
    m_votables["slots"].resetAllVotes();
    m_votables["randomteams"].resetAllVotes();
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

std::shared_ptr<STKPeer> CommandManager::Context::peer()
{
    if (m_peer.expired())
        throw std::logic_error("Peer is expired");

    auto peer = m_peer.lock();
    if (!peer)
        throw std::logic_error("Peer is invalid");

    return peer;
}   // peer
//-----------------------------------------------------------------------------

std::shared_ptr<STKPeer> CommandManager::Context::peerMaybeNull()
{
    // if (m_peer.expired())
    //     throw std::logic_error("Peer is expired");

    auto peer = m_peer.lock();
    return peer;
}   // peerMaybeNull
//-----------------------------------------------------------------------------

std::shared_ptr<STKPeer> CommandManager::Context::actingPeer()
{
    if (m_target_peer.expired())
        throw std::logic_error("Target peer is expired");

    auto acting_peer = m_target_peer.lock();
    if (!acting_peer)
        throw std::logic_error("Target peer is invalid");

    return acting_peer;
}   // actingPeer
//-----------------------------------------------------------------------------

std::shared_ptr<STKPeer> CommandManager::Context::actingPeerMaybeNull()
{
    // if (m_target_peer.expired())
    //     throw std::logic_error("Target peer is expired");

    auto acting_peer = m_target_peer.lock();
    return acting_peer;
}   // actingPeerMaybeNull
//-----------------------------------------------------------------------------

std::shared_ptr<CommandManager::Command> CommandManager::Context::command()
{
    if (m_command.expired())
        throw std::logic_error("Command is expired");

    auto command = m_command.lock();
    if (!command)
        throw std::logic_error("Command is invalid");

    return command;
}   // command
//-----------------------------------------------------------------------------

void CommandManager::Context::say(const std::string& s)
{
    if (m_peer.expired())
        throw std::logic_error("Context::say: Peer has expired");

    auto peer = m_peer.lock();
    Comm::sendStringToPeer(peer, s);
}   // say
//-----------------------------------------------------------------------------

void CommandManager::Command::changePermissions(int permissions,
        int mode_scope, int state_scope)
{
    // Handling players who are allowed to run for anyone in any case
    m_permissions = permissions | UU_OTHERS_COMMANDS;
    m_mode_scope = mode_scope;
    m_state_scope = state_scope;
} // changePermissions
// ========================================================================

std::string CommandManager::getAddonPreferredType() const
{
    int mode = getLobby()->getGameMode();
    if (0 <= mode && mode <= 4)
        return g_type_track;
    if (mode == 6)
        return g_type_soccer;
    if (7 <= mode && mode <= 8)
        return g_type_arena;
    return g_type_track; // default choice
} // getAddonPreferredType
// ========================================================================

std::deque<std::shared_ptr<Filter>>& CommandManager::get_queue(int x) const
{
    auto queues = getQueues();
    if (x == QM_MAP_ONETIME)
        return queues->getOnetimeTracksQueue();
    if (x == QM_MAP_CYCLIC)
        return queues->getCyclicTracksQueue();
    if (x == QM_KART_ONETIME)
        return queues->getOnetimeKartsQueue();
    if (x == QM_KART_CYCLIC)
        return queues->getCyclicKartsQueue();
    Log::error("CommandManager",
               "Unknown queue mask %d, revert to map onetime", x);
    return queues->getOnetimeTracksQueue();

} // get_queue
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

void CommandManager::shift(std::string& cmd, std::vector<std::string>& argv,
        const std::string& username, int count)
{
    std::reverse(argv.begin(), argv.end());
    argv.resize(argv.size() - count);
    std::reverse(argv.begin(), argv.end());

    // auto it = m_user_last_correct_argument.find(username);
    // if (it != m_user_last_correct_argument.end())
    //     it->second.first -= count;

    m_user_last_correct_argument[username].first -= count;

    CommandManager::restoreCmdByArgv(cmd, argv, ' ', '"', '"', '\\');
}   // shift
//-----------------------------------------------------------------------------