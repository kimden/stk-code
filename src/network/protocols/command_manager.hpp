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
#include "utils/command_manager/auth_resource.hpp"
#include "utils/command_manager/command.hpp"
#include "utils/command_manager/context.hpp"
#include "utils/command_manager/file_resource.hpp"
#include "utils/command_manager/text_resource.hpp"
#include "utils/lobby_context.hpp"
#include "utils/set_typo_fixer.hpp"
#include "utils/team_utils.hpp"
#include "utils/track_filter.hpp"
#include "utils/types.hpp"

class Event;
class KartElimination;
class LobbySettings;
class ServerLobby;
class STKPeer;

class CommandManager: public LobbyContextComponent
{
private:
    std::deque<std::shared_ptr<Filter>>& get_queue(int x) const;

    template<typename T>
    void add_to_queue(int x, int mask, bool to_front, std::string& s) const;

private:

    std::vector<std::weak_ptr<Command>> m_all_commands;

    std::map<std::string, std::weak_ptr<Command>> m_full_name_to_command;

    std::shared_ptr<Command> m_root_command;

    std::multiset<std::string> m_users;

    std::map<std::string, CommandVoting> m_votables;

    std::queue<std::string> m_triggered_votables;

    std::map<std::string, std::vector<std::string>> m_user_command_replacements;

    std::map<std::string, bool> m_user_saved_voting;

    std::map<std::string, std::weak_ptr<STKPeer>> m_user_saved_acting_peer;

    std::map<std::string, std::pair<int, int>> m_user_last_correct_argument;

    std::map<std::string, CommandDescription> m_config_descriptions;

    std::map<std::string, std::vector<std::string>> m_aliases;

    SetTypoFixer m_stf_present_users;

    SetTypoFixer m_stf_all_maps;

    SetTypoFixer m_stf_addon_maps;

    enum TypoFixerType
    {
        TFT_PRESENT_USERS,
        TFT_ALL_MAPS,
        TFT_ADDON_MAPS
    };

    const SetTypoFixer& getFixer(TypoFixerType type);

    std::vector<std::string> m_current_argv;

    void initCommandsInfo();
    void initCommands();

    void initAssets();

    int getCurrentModeScope();

    bool isAvailable(std::shared_ptr<Command> c);

    void vote(Context& context, std::string category, std::string value);
    void update();
    void error(Context& context, bool is_error = false);

    void execute(std::shared_ptr<Command> command, Context& context);

    void process_help(Context& context);
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
    void process_vip(Context& context);
    void process_troll(Context& context);
    void process_troll_assign(Context& context);
    void process_hitmsg(Context& context);
    void process_hitmsg_assign(Context& context);
    void process_teamhit(Context& context);
    void process_teamhit_assign(Context& context);
    void process_scoring(Context& context);
    void process_scoring_assign(Context& context);
    void process_register(Context& context);
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
    void process_history(Context& context);
    void process_history_assign(Context& context);
    void process_voting(Context& context);
    void process_voting_assign(Context& context);
    void process_why_hourglass(Context& context);
    void process_available_teams(Context& context);
    void process_available_teams_assign(Context& context);
    void process_cooldown(Context& context);
    void process_cooldown_assign(Context& context);
    void process_countteams(Context& context);
    void process_net(Context& context);
    void process_everynet(Context& context);

    // Temporary command
    void process_temp250318(Context& context);

    void special(Context& context);

public:
    CommandManager(LobbyContext* context): LobbyContextComponent(context) {}

    void setupContextUser() OVERRIDE;

    void handleCommand(Event* event, std::shared_ptr<STKPeer> peer);

    void addUser(std::string& s);

    void deleteUser(std::string& s);

    static void restoreCmdByArgv(std::string& cmd,
            std::vector<std::string>& argv, char c, char d, char e, char f,
            int from = 0);

    // A simple version of hasTypo for validating simple arguments.
    // Returns the opposite bool value.
    bool validate(Context& ctx, int idx,
            TypoFixerType fixer_type, bool case_sensitive, bool allow_as_is);

    bool hasTypo(std::shared_ptr<STKPeer> acting_peer, std::shared_ptr<STKPeer> peer, bool voting,
        std::vector<std::string>& argv, std::string& cmd, int idx,
        const SetTypoFixer& stf, int top, bool case_sensitive, bool allow_as_is,
        bool dont_replace = false, int subidx = 0, int substr_l = -1, int substr_r = -1);

    void onServerSetup();

    void onStartSelection();

    std::vector<std::string> getCurrentArgv()         { return m_current_argv; }

    // Helper functions, unrelated to CommandManager inner structure
    std::string getAddonPreferredType() const;

    void shift(std::string& cmd, std::vector<std::string>& argv,
        const std::string& username, int count);

    std::function<void(Context&)> getDefaultAction();

};

#endif // COMMAND_MANAGER_HPP
