//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2025 Nomagno
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

#include "states_screens/dialogs/item_policy_dialog.hpp"

#include "config/user_config.hpp"
#include "guiengine/widgets/icon_button_widget.hpp"
#include "guiengine/widgets/label_widget.hpp"
#include "guiengine/widgets/ribbon_widget.hpp"
#include "guiengine/widgets/spinner_widget.hpp"
#include "network/network_string.hpp"
#include "network/protocols/lobby_protocol.hpp"
#include "network/stk_host.hpp"
#include "states_screens/state_manager.hpp"
#include "utils/communication.hpp"
#include "utils/string_utils.hpp"
#include "utils/file_utils.hpp"
#include "utils/translation.hpp"
#include "io/file_manager.hpp"
#include <fstream>

using namespace GUIEngine;

ItemPolicyDialog::ItemPolicyDialog(std::string path) : ModalDialog(0.8f, 0.8f) {
    m_self_destroy = false;
    m_save = false;
    m_path = "/rules/"+path;

    std::string policy = loadConfig(m_path, /*create_if_missing*/ true);
    if(policy != "FAILURE")
        m_item_policy.fromString(policy);
    else {
        policy = "normal";
        m_item_policy.fromString(policy);
        m_save = false;
        m_self_destroy = true;
    }


    loadFromFile("online/item_policy_dialog.stkgui");
}

// ----------------------------------------------------------------------------
std::string ItemPolicyDialog::loadConfig(const std::string &path, bool create_if_missing) {

    std::string policy = "normal";

    file_manager->checkAndCreateDirectory(file_manager->getUserConfigFile("/rules/"));
    const std::string filename = file_manager->getUserConfigFile(path);
    auto root = file_manager->createXMLTree(filename);

    if(!root || root->getName() != "item-policy-preset") {
        delete root;
        if (create_if_missing) {
            Log::info("ItemPolicyDialog::loadConfig",
                    "Could not read item policy  file '%s'.  A new file will be created.", filename.c_str());
            saveConfig(path, "normal");
            root = file_manager->createXMLTree(filename);
        } else {
            Log::info("ItemPolicyDialog::loadConfig",
                    "Could not read item policy  file '%s'. Aborting.", filename.c_str());
            policy = "FAILURE";
            return policy;
        }
    }

    bool success = root->get("itempolicy", &policy);
    if (!success) {
        Log::info("ItemPolicyDialog::loadConfig",
                   "Malformed item policy, aborting", filename.c_str());
        policy = "FAILURE";
    }
    return policy;
}

// ----------------------------------------------------------------------------
bool ItemPolicyDialog::saveConfig(const std::string &path, const std::string &policy)
{
    const std::string filename = file_manager->getUserConfigFile(path);
    std::stringstream ss;
    ss << "<?xml version=\"1.0\"?>\n";
    ss << "<item-policy-preset \n\n";
    const std::string quote = "\"";
    ss << "itempolicy=" << quote << policy << quote << ">" << "\n";
    ss << "</item-policy-preset>\n";

    try
    {
        file_manager->checkAndCreateDirectory(file_manager->getUserConfigFile("/rules/"));
        std::string s = ss.str();
        std::ofstream configfile(FileUtils::getPortableWritingPath(filename),
            std::ofstream::out);
        configfile << ss.rdbuf();
        configfile.close();
        RaceManager::get()->setNumberOfRuleFiles(RaceManager::get()->getNumberOfRuleFiles()+1);
        return true;
    }
    catch (std::runtime_error& e)
    {
        Log::error("ItemPolicyDialog::saveConfig", "Failed to write Item Policy Preset to %s, "
            "because %s", filename.c_str(), e.what());
        return false;
    }
}   // saveConfig
// ----------------------------------------------------------------------------
void ItemPolicyDialog::beforeAddingWidgets()
{
    m_current_section_label = getWidget<LabelWidget>("current-section-label");
    assert(m_current_section_label != NULL);
    m_current_section_spinner = getWidget<SpinnerWidget>("current-section-spinner");
    assert(m_current_section_spinner != NULL);

    m_config_mode_label = getWidget<LabelWidget>("config-mode-label");
    assert(m_config_mode_label != NULL);
    m_config_mode_spinner = getWidget<SpinnerWidget>("config-mode-spinner");
    assert(m_config_mode_spinner != NULL);

    m_ok_widget = getWidget<IconButtonWidget>("ok");
    assert(m_ok_widget != NULL);
    m_cancel_widget = getWidget<IconButtonWidget>("cancel");
    assert(m_cancel_widget != NULL);
}   // beforeAddingWidgets

// ----------------------------------------------------------------------------
void ItemPolicyDialog::init()
{
    ModalDialog::init();

    m_config_mode_spinner->setValue(0);
    updateMoreOption(0);
}   // init

// ----------------------------------------------------------------------------
GUIEngine::EventPropagation
    ItemPolicyDialog::processEvent(const std::string& src)
{
    std::string source = src;
    if (source == "options")
        source = getWidget<GUIEngine::RibbonWidget>("options")->getSelectionIDString(PLAYER_ID_GAME_MASTER);
    if (source == m_cancel_widget->m_properties[PROP_ID])
    {
        m_save = false;
        m_self_destroy = true;
        return GUIEngine::EVENT_BLOCK;
    } else if (source == m_ok_widget->m_properties[PROP_ID]) {
        m_save = true;
        m_self_destroy = true;
        return GUIEngine::EVENT_BLOCK;
    } else if (source == m_config_mode_spinner->m_properties[PROP_ID]) {
        const int selection = m_config_mode_spinner->getValue();
        updateMoreOption(selection);
        return GUIEngine::EVENT_LET;
    } else {
        return GUIEngine::EVENT_LET;
    }
}   // eventCallback


// ----------------------------------------------------------------------------
/**
 * Reconfigure GUI based on the itempolicy config mode (powerups/collectible/other)
 */
void ItemPolicyDialog::updateMoreOption(int config_mode)
{

}   // updateMoreOption
