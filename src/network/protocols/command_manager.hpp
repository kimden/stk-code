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
#include <queue>


#ifdef ENABLE_SQLITE3
#include <sqlite3.h>
#endif

#include "network/protocols/command_voting.hpp"
#include "network/protocols/command_permissions.hpp"
#include "utils/enum_extended_reader.hpp"
#include "utils/set_typo_fixer.hpp"

class ServerLobby;
class Event;
class STKPeer;

class CommandManager
{
    struct FileResource
    {
        std::string m_file_name;
        uint64_t m_interval;
        uint64_t m_last_invoked;
        std::string m_contents;

        FileResource(std::string file_name = "", uint64_t interval = 0);

        void read();

        std::string get();
    };

    struct AuthResource
    {
        std::string m_secret;
        std::string m_server;
        std::string m_link_format;

        AuthResource(std::string secret = "", std::string server = "",
            std::string link_format = "");

        std::string get(const std::string& username) const;
    };

    static EnumExtendedReader permission_reader;
    static EnumExtendedReader mode_scope_reader;
    static EnumExtendedReader state_scope_reader;

    enum ModeScope: int
    {
        MS_DEFAULT = 1,
        MS_SOCCER_TOURNAMENT = 2
        // add more powers of two if needed
    };

    enum StateScope: int
    {
        SS_LOBBY = 1,
        SS_INGAME = 2,
        SS_ALWAYS = SS_LOBBY | SS_INGAME
    };

    struct Command;

    struct Context
    {
        Event* m_event;

        std::weak_ptr<STKPeer> m_peer;

        std::vector<std::string> m_argv;

        std::string m_cmd;

        std::weak_ptr<Command> m_command;

        int m_user_permissions;

        bool m_voting;

        Context(Event* event, std::shared_ptr<STKPeer> peer):
                m_event(event), m_peer(peer), m_argv(),
                m_cmd(""), m_user_permissions(0), m_voting(false) {}

        Context(Event* event, std::shared_ptr<STKPeer> peer,
            std::vector<std::string>& argv, std::string& cmd,
            int user_permissions, bool voting):
                m_event(event), m_peer(peer), m_argv(argv),
                m_cmd(cmd), m_user_permissions(user_permissions), m_voting(voting) {}
    };

    struct CommandDescription
    {
        std::string m_usage;
        std::string m_permissions;
        std::string m_description;
        CommandDescription(std::string usage = "", std::string permissions = "",
            std::string description = ""): m_usage(usage),
            m_permissions(permissions), m_description(description) {}

        std::string getUsage() const { return "Usage: " + m_usage; }

        std::string getHelp() const
        {
            return "Usage: " + m_usage
                + "\nAvailable to: " + m_permissions
                + "\n" + m_description;
        }
    };

    struct Command
    {
        std::string m_name;

        std::string m_prefix_name;

        int m_permissions;

        void (CommandManager::*m_action)(Context& context);

        int m_mode_scope;

        int m_state_scope;

        bool m_omit_name;

        CommandDescription m_description;

        std::weak_ptr<Command> m_parent;

        std::map<std::string, std::weak_ptr<Command>> m_name_to_subcommand;

        std::vector<std::shared_ptr<Command>> m_subcommands;

        SetTypoFixer m_stf_subcommand_names;

        Command() {}

        Command(std::string name,
                void (CommandManager::*f)(Context& context),
                int permissions = UP_EVERYONE,
                int mode_scope = MS_DEFAULT, int state_scope = SS_ALWAYS);

        void changeFunction(void (CommandManager::*f)(Context& context))
                                                              { m_action = f; }
        void changePermissions(int permissions = UP_EVERYONE,
                                int mode_scope = MS_DEFAULT,
                                int state_scope = SS_ALWAYS);

        std::string getUsage() const       { return m_description.getUsage(); }
        std::string getHelp() const         { return m_description.getHelp(); }
        std::string getFullName() const               { return m_prefix_name; }
    };

private:

    ServerLobby* m_lobby;

    std::vector<std::weak_ptr<Command>> m_all_commands;

    std::map<std::string, std::weak_ptr<Command>> m_full_name_to_command;

    std::shared_ptr<Command> m_root_command;

    std::multiset<std::string> m_users;

    std::map<std::string, std::string> m_text_response;

    std::map<std::string, FileResource> m_file_resources;

    std::map<std::string, AuthResource> m_auth_resources;

    std::map<std::string, CommandVoting> m_votables;

    std::queue<std::string> m_triggered_votables;

    std::map<std::string, std::vector<std::string>> m_user_command_replacements;

    std::map<std::string, bool> m_user_saved_voting;

    std::map<std::string, int> m_user_correct_arguments;

    std::map<std::string, CommandDescription> m_config_descriptions;

    std::map<std::string, std::vector<std::string>> m_aliases;

    SetTypoFixer m_stf_present_users;

    SetTypoFixer m_stf_all_maps;

    SetTypoFixer m_stf_addon_maps;

    std::vector<std::string> m_current_argv;

    // Auxiliary things, should be moved somewhere because they just help
    // in commands but have nothing to do with CM itself

    std::vector<std::vector<std::string>> m_aux_mode_aliases;
    std::vector<std::vector<std::string>> m_aux_difficulty_aliases;
    std::vector<std::vector<std::string>> m_aux_goal_aliases;

    // End of auxiliary things

    void initCommandsInfo();
    void initCommands();

    void initAssets();

    int getCurrentModeScope();
    int getCurrentStateScope();

    bool isAvailable(std::shared_ptr<Command> c);

    void vote(Context& context, std::string category, std::string value);
    void update();
    void error(Context& context, bool is_error = false);

    void execute(std::shared_ptr<Command> command, Context& context);

    void process_help(Context& context);
    void process_text(Context& context);
    void process_file(Context& context);
    void process_auth(Context& context);
    void process_commands(Context& context);
    void process_replay(Context& context);
    void process_start(Context& context);
    void process_config(Context& context);
    void process_config_assign(Context& context);
    void process_spectate(Context& context);
    void process_addons(Context& context);
    void process_checkaddon(Context& context);
    void process_id(Context& context);
    void process_lsa(Context& context);
    void process_pha(Context& context);
    void process_kick(Context& context);
    void process_unban(Context& context);
    void process_ban(Context& context);
    void process_pas(Context& context);
    void process_everypas(Context& context);
    void process_sha(Context& context);
    void process_mute(Context& context);
    void process_unmute(Context& context);
    void process_listmute(Context& context);
    void process_gnu(Context& context);
    void process_tell(Context& context);
    void process_standings(Context& context);
    void process_teamchat(Context& context);
    void process_to(Context& context);
    void process_public(Context& context);
    void process_record(Context& context);
    void process_power(Context& context);
    void process_length(Context& context);
    void process_length_multi(Context& context);
    void process_length_fixed(Context& context);
    void process_length_clear(Context& context);
    void process_direction(Context& context);
    void process_direction_assign(Context& context);
    void process_queue(Context& context);
    void process_queue_push(Context& context);
    void process_queue_pf(Context& context);
    void process_queue_pop(Context& context);
    void process_queue_clear(Context& context);
    void process_queue_shuffle(Context& context);
    void process_allowstart(Context& context);
    void process_allowstart_assign(Context& context);
    void process_shuffle(Context& context);
    void process_shuffle_assign(Context& context);
    void process_timeout(Context& context);
    void process_team(Context& context);
    void process_swapteams(Context& context);
    void process_resetteams(Context& context);
    void process_randomteams(Context& context);
    void process_resetgp(Context& context);
    void process_cat(Context& context);
    void process_troll(Context& context);
    void process_troll_assign(Context& context);
    void process_hitmsg(Context& context);
    void process_hitmsg_assign(Context& context);
    void process_teamhit(Context& context);
    void process_teamhit_assign(Context& context);
    void process_scoring(Context& context);
    void process_scoring_assign(Context& context);
    void process_register(Context& context);
#ifdef ENABLE_WEB_SUPPORT
    void process_token(Context& context);
#endif
    // soccer tournament commands
    void process_muteall(Context& context);
    void process_game(Context& context);
    void process_role(Context& context);
    void process_stop(Context& context);
    void process_go(Context& context);
    void process_lobby(Context& context);
    void process_init(Context& context);
    void process_mimiz(Context& context);
    void process_test(Context& context);
    void process_slots(Context& context);
    void process_slots_assign(Context& context);
    void process_time(Context& context);
    void process_result(Context& context);
    void process_preserve(Context& context);
    void process_preserve_assign(Context& context);
    void special(Context& context);

    std::string getRandomMap() const;
    std::string getRandomAddonMap() const;

public:

    CommandManager(ServerLobby* lobby = nullptr);

    void handleCommand(Event* event, std::shared_ptr<STKPeer> peer);

    bool isInitialized() { return m_lobby != nullptr; }

    template<typename T>
    void addTextResponse(std::string key, T&& value)
                                              { m_text_response[key] = value; }

    void addFileResource(std::string key, std::string file, uint64_t interval)
                      { m_file_resources[key] = FileResource(file, interval); }

    void addAuthResource(std::string key, std::string secret, std::string server, std::string link_format)
         { m_auth_resources[key] = AuthResource(secret, server, link_format); }

    void addUser(std::string& s);

    void deleteUser(std::string& s);

    static void restoreCmdByArgv(std::string& cmd,
            std::vector<std::string>& argv, char c, char d, char e, char f,
            int from = 0);

    bool hasTypo(std::shared_ptr<STKPeer> peer, bool voting,
        std::vector<std::string>& argv, std::string& cmd, int idx,
        SetTypoFixer& stf, int top, bool case_sensitive, bool allow_as_is,
        bool dont_replace = false);

    void onResetServer();

    void onStartSelection();

    std::vector<std::string> getCurrentArgv()         { return m_current_argv; }

    std::shared_ptr<Command> addChildCommand(std::shared_ptr<Command> target, std::string name,
             void (CommandManager::*f)(Context& context), int permissions = UP_EVERYONE,
             int mode_scope = MS_DEFAULT, int state_scope = SS_ALWAYS);
};

#endif // COMMAND_MANAGER_HPP