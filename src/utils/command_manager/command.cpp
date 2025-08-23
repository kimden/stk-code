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

#include "utils/command_manager/command.hpp"
#include "utils/string_utils.hpp"
#include "network/server_config.hpp"
#include "utils/enum_extended_reader.hpp"
#include "utils/log.hpp"

#include "utils/command_manager/auth_resource.hpp"
#include "utils/command_manager/file_resource.hpp"
#include "utils/command_manager/text_resource.hpp"

// This is needed for default actions only for now...
#include "network/protocols/command_manager.hpp"

#include <string_view>

namespace
{
    static EnumExtendedReader mode_scope_reader({
        {"MS_DEFAULT", MS_DEFAULT},
        {"MS_SOCCER_TOURNAMENT", MS_SOCCER_TOURNAMENT}
    });

    static EnumExtendedReader state_scope_reader({
        {"SS_LOBBY", SS_LOBBY},
        {"SS_INGAME", SS_INGAME},
        {"SS_ALWAYS", SS_ALWAYS}
    });

    static EnumExtendedReader permission_reader({
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

    // C++20 compile-time string to class conversion

    // template<std::string_view S>
    // struct XmlNameToClass;

    // template<> struct XmlNameToClass<"command">           { using type = Command; };
    // template<> struct XmlNameToClass<"text-command">      { using type = TextResource; };
    // template<> struct XmlNameToClass<"file-command">      { using type = FileResource; };
    // template<> struct XmlNameToClass<"auth-command">      { using type = AuthResource; };

}   // namespace
//=============================================================================

CommandDescription::CommandDescription(std::string usage,
                                       std::string permissions,
                                       std::string description)
    : m_usage(std::move(usage))
    , m_permissions(std::move(permissions))
    , m_description(std::move(description))
{}
//-----------------------------------------------------------------------------

std::string CommandDescription::getUsage() const
{
    return StringUtils::insertValues("Usage: %s", m_usage.c_str());
}   // getUsage
//-----------------------------------------------------------------------------

std::string CommandDescription::getHelp() const
{
    return StringUtils::insertValues("Usage: %s\nAvailable to: %s\n%s",
        m_usage.c_str(),
        m_permissions.c_str(),
        m_description.c_str()
    );
}   // getHelp
//=============================================================================

void Command::changePermissions(int permissions,
        int mode_scope, int state_scope)
{
    // Handling players who are allowed to run for anyone in any case
    m_permissions = permissions | UU_OTHERS_COMMANDS;
    m_mode_scope = mode_scope;
    m_state_scope = state_scope;
}   // changePermissions
//-----------------------------------------------------------------------------


std::shared_ptr<Command> Command::unknownTypeFromXmlNode(const XMLNode* node)
{
    std::string node_name = node->getName();
    if (node_name == "external-commands-file")
        return {};

    std::shared_ptr<Command> res;
    if (node_name == "command")
        res = std::make_shared<Command>();
    else if (node_name == "text-command")
        res = std::make_shared<TextResource>();
    else if (node_name == "auth-command")
        res = std::make_shared<AuthResource>();
    else if (node_name == "file-command")
        res = std::make_shared<FileResource>();
    else
    {
        Log::error("Command", "Unknown node name %s, treating as normal command.", node_name.c_str());
        res = std::make_shared<Command>();
    }

    try
    {
        res->fromXmlNode(node);
    }
    catch (std::exception& ex)
    {
        Log::error("Command", "unknownTypeFromXmlNode: error while calling fromXmlNode: %s", ex.what());
        return {};
    }

    return res;
}

void Command::fromXmlNode(const XMLNode* node)
{
    std::string name = "";
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

    node->get("name", &name);
    m_name = name;

    // If enabled is not empty, command is added iff the server name is in enabled
    // Otherwise it is added iff the server name is not in disabled
    std::string enabled = "";
    std::string disabled = "";
    node->get("enabled", &enabled);
    node->get("disabled", &disabled);
    bool ok;
    if (!enabled.empty())
    {
        std::vector<std::string> enabled_split = StringUtils::split(enabled, ' ');
        ok = false;
        for (const std::string& s: enabled_split)
            if (s == ServerConfig::m_server_uid)
                ok = true;
    }
    else
    {
        std::vector<std::string> disabled_split = StringUtils::split(disabled, ' ');
        ok = true;
        for (const std::string& s: disabled_split)
            if (s == ServerConfig::m_server_uid)
                ok = false;
    }

    if (!ok)
    {
        throw std::logic_error(StringUtils::insertValues(
            "The command %s is not loaded, as it was disabled in the config.",
            name.c_str()
        ));
    }

    node->get("permissions", &permissions_s);
    node->get("mode-scope", &mode_scope_s);
    node->get("state-scope", &state_scope_s);
    permissions = permission_reader.parse(permissions_s);
    mode_scope = mode_scope_reader.parse(mode_scope_s);
    state_scope = state_scope_reader.parse(state_scope_s);
    changePermissions(permissions, mode_scope, state_scope);

    node->get("usage", &usage);
    node->get("permissions-verbose", &permissions_str);
    node->get("description", &description);
    m_description = CommandDescription(usage, permissions_str, description);

    node->get("aliases", &aliases);
    m_aliases = StringUtils::split(aliases, ' ');

    node->get("omit-name", &omit_name);
    m_omit_name = omit_name;

    // m_action is set in CommandManager, as it knows better about implementation.
    // CM::special is also set from there.
}   // fromXmlNode
//-----------------------------------------------------------------------------

void Command::addChild(const std::shared_ptr<Command>& child)
{
    child->m_parent = shared_from_this();
    const std::string name = child->m_name;

    m_subcommands.push_back(child);
    child->m_prefix_name =
            (m_prefix_name.empty() ? "" : m_prefix_name + " ") + name;

    auto weak = std::weak_ptr<Command>(child);
    m_name_to_subcommand[name] = weak;

    m_stf_subcommand_names.add(name);
    for (const std::string& alias_name: child->getAliases())
    {
        m_stf_subcommand_names.add(alias_name, name);
        m_name_to_subcommand[alias_name] = m_name_to_subcommand[name];
    }
}   // addChild
//-----------------------------------------------------------------------------

void Command::execute(Context& context)
{
    m_action(context);
}   // execute
//-----------------------------------------------------------------------------

std::shared_ptr<Command> Command::findChild(const std::string& name) const
{
    const auto& it = m_name_to_subcommand.find(name);
    if (it == m_name_to_subcommand.end())
        return {};

    return it->second.lock();
}   // findChild
//-----------------------------------------------------------------------------