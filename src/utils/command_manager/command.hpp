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

#ifndef COMMAND_HPP
#define COMMAND_HPP

#include <string>
#include <memory>
#include <map>
#include <vector>
#include <functional>

#include "network/protocols/command_permissions.hpp"
#include "utils/set_typo_fixer.hpp"
#include "io/xml_node.hpp"
#include "utils/command_manager/context.hpp"

struct Context;

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

struct CommandDescription
{
    std::string m_usage;
    std::string m_permissions;
    std::string m_description;
    CommandDescription(std::string usage = "",
                       std::string permissions = "",
                       std::string description = "");

    std::string getUsage() const;

    std::string getHelp() const;
};

struct Command: public std::enable_shared_from_this<Command>
{
private:
    std::string m_name;
    std::string m_prefix_name;
    std::function<void(Context&)> m_action = nullptr;
    int m_permissions = UP_EVERYONE;
    int m_mode_scope = MS_DEFAULT;
    int m_state_scope = SS_ALWAYS;
    bool m_omit_name;
    std::vector<std::string> m_aliases;

    CommandDescription m_description;
    std::weak_ptr<Command> m_parent;
    std::map<std::string, std::weak_ptr<Command>> m_name_to_subcommand;
    std::vector<std::shared_ptr<Command>> m_subcommands;
    SetTypoFixer m_stf_subcommand_names;

protected:
    virtual void fromXmlNode(const XMLNode* node);

public:
    virtual bool needsFunction() const { return true; }

    Command() {}

    // Command(std::string name,
    //         std::function<void(Context&)> f,
    //         int permissions = UP_EVERYONE,
    //         int mode_scope = MS_DEFAULT, int state_scope = SS_ALWAYS);

    static std::shared_ptr<Command> unknownTypeFromXmlNode(const XMLNode* node);

    void setFunction(std::function<void(Context&)> f)
                                               { m_action = std::move(f); }

    void changePermissions(int permissions = UP_EVERYONE,
                            int mode_scope = MS_DEFAULT,
                            int state_scope = SS_ALWAYS);

    std::string getUsage() const       { return m_description.getUsage(); }
    std::string getHelp() const         { return m_description.getHelp(); }
    std::string getFullName() const               { return m_prefix_name; }
    std::string getShortName() const                     { return m_name; }
    int getPermissions() const                    { return m_permissions; }
    int getModeScope() const                       { return m_mode_scope; }
    int getStateScope() const                     { return m_state_scope; }
    bool omittedName() const                        { return m_omit_name; }
    bool hasFunction() const                { return m_action != nullptr; }

    const SetTypoFixer& subcommandNamesTypoFixer() const
                                         { return m_stf_subcommand_names; }

    std::shared_ptr<Command> findChild(const std::string& name) const;

    const std::vector<std::shared_ptr<Command>>& getSubcommands() const
                                                  { return m_subcommands; }

    void addChild(const std::shared_ptr<Command>& child);

    const std::vector<std::string>& getAliases() const { return m_aliases; }

    virtual void execute(Context& context);
};

#endif // COMMAND_HPP