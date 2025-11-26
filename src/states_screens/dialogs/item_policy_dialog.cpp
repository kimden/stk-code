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
#include "guiengine/widgets/check_box_widget.hpp"
#include "guiengine/widgets/text_box_widget.hpp"
#include "items/powerup_manager.hpp"
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
    m_current_section = 0;
    m_current_config_tab = 0;

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
            bool success = saveConfig(path, "normal");
            if (success) RaceManager::get()->setNumberOfRuleFiles(RaceManager::get()->getNumberOfRuleFiles()+1);
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

    m_new_section_widget = getWidget<IconButtonWidget>("new-section");
    assert(m_new_section_widget != NULL);
    m_remove_section_widget = getWidget<IconButtonWidget>("remove-section");
    assert(m_remove_section_widget != NULL);


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

    m_config_mode_spinner->addLabel(_("Powerups"));
    m_config_mode_spinner->addLabel(_("Powerup pool"));
    m_config_mode_spinner->addLabel(_("Rules"));
    m_config_mode_spinner->addLabel(_("Fuel & Tyres"));

    m_config_mode_spinner->setValue(0);
    m_current_section_spinner->setValue(0);
    updateMoreOption(0);
    setGUIFromPolicy();
}   // init

// ----------------------------------------------------------------------------
GUIEngine::EventPropagation
    ItemPolicyDialog::processEvent(const std::string& src)
{
    std::string source = src;
    if (source == "options")
        source = getWidget<GUIEngine::RibbonWidget>("options")->getSelectionIDString(PLAYER_ID_GAME_MASTER);
    if (source == "section-control")
        source = getWidget<GUIEngine::RibbonWidget>("section-control")->getSelectionIDString(PLAYER_ID_GAME_MASTER);
    if (source == m_cancel_widget->m_properties[PROP_ID])
    {
        m_save = false;
        m_self_destroy = true;
        return GUIEngine::EVENT_BLOCK;
    } else if (source == m_ok_widget->m_properties[PROP_ID]) {
        m_save = true;
        m_self_destroy = true;
        return GUIEngine::EVENT_BLOCK;
    } else if (source == "new-section") {
        m_item_policy.m_policy_sections.push_back({});
        ItemPolicySection *prev = &m_item_policy.m_policy_sections[m_item_policy.m_policy_sections.size()-2];
        ItemPolicySection *last = &m_item_policy.m_policy_sections[m_item_policy.m_policy_sections.size()-1];
        last->m_section_start = prev->m_section_start+1;
        last->m_rules = 0x0;
        last->m_linear_mult = 0.0f;
        last->m_items_per_lap = 0.0f;
        last->m_progressive_cap = 0.0f;
        last->m_virtual_pace_gaps = 0.0f;
        last->m_deg_mult = 1.0f;
        last->m_fuel_mult = 1.0f;
        last->m_tyre_change_time = 0.0f;
        last->m_possible_types.clear();
        last->m_weight_distribution.clear();

        m_current_section_spinner->setValue(m_item_policy.m_policy_sections.size()-1);
        return GUIEngine::EVENT_BLOCK;
    } else if (source == "remove-section") {
        if (m_item_policy.m_policy_sections.size() > 1) {
            if (m_current_section_spinner->getValue() >= (m_item_policy.m_policy_sections.size()-1)) {
                m_current_section_spinner->setValue(m_item_policy.m_policy_sections.size()-2);
            }
            m_item_policy.m_policy_sections.pop_back();
        }
        return GUIEngine::EVENT_BLOCK;
    } else {
        return GUIEngine::EVENT_LET;
    }
}   // eventCallback

void ItemPolicyDialog::onUpdate(float dt) {
    if (m_self_destroy) {
        if (m_save) {
            saveConfig(m_path, m_item_policy.toString());
            RaceManager::get()->setItemPolicy(m_item_policy.toString());
        }
        ModalDialog::dismiss();
        return;
    }

    if (m_current_section_spinner->getValue() != m_current_section) {
        if (m_current_section_spinner->getValue() >= m_item_policy.m_policy_sections.size())
            m_current_section_spinner->setValue(m_item_policy.m_policy_sections.size()-1);
        else {
            m_current_section = m_current_section_spinner->getValue();
            setGUIFromPolicy();
        }
    }

    if (m_config_mode_spinner->getValue() != m_current_config_tab) {
        m_current_config_tab = m_config_mode_spinner->getValue();
        updateMoreOption(m_current_config_tab);
    }
    computePolicyFromGUI();
}

// ----------------------------------------------------------------------------
/**
 * Reconfigure GUI based on the itempolicy config mode (powerups/poweruppool/rules/fuelandtyres)
 */
void ItemPolicyDialog::updateMoreOption(int config_mode)
{
    switch(config_mode) {
        case 0:
            setVisibilityOfPowerupTab(true);
            setVisibilityOfPowerupPoolTab(false);
            setVisibilityOfRulesTab(false);
            setVisibilityOfFuelAndTyresTab(false);
            break;
        case 1:
            setVisibilityOfPowerupTab(false);
            setVisibilityOfPowerupPoolTab(true);
            setVisibilityOfRulesTab(false);
            setVisibilityOfFuelAndTyresTab(false);
            break;
        case 2:
            setVisibilityOfPowerupTab(false);
            setVisibilityOfPowerupPoolTab(false);
            setVisibilityOfRulesTab(true);
            setVisibilityOfFuelAndTyresTab(false);
            break;
        case 3:
            setVisibilityOfPowerupTab(false);
            setVisibilityOfPowerupPoolTab(false);
            setVisibilityOfRulesTab(false);
            setVisibilityOfFuelAndTyresTab(true);
            break;
        default:
            break;
    }
}   // updateMoreOption

#define _S(__x, __y) ((std::string(__x)+std::string(__y)).c_str())

#define LABEL(__str) getWidget<GUIEngine::LabelWidget> _S(__str, "-label")
#define CHECKBOX(__str) getWidget<GUIEngine::CheckBoxWidget> _S(__str, "-checkbox")
#define SPINNER(__str) getWidget<GUIEngine::SpinnerWidget> _S(__str, "-spinner")
#define TEXTBOX(__str) getWidget<GUIEngine::TextBoxWidget> _S(__str, "-textbox")


#define SETRULE(__x, __y) if (__x) { cs->m_rules |= (ItemPolicyRules::__y); } else { cs->m_rules &= ~(ItemPolicyRules::__y); } 
#define READ_TEXTBOX_FLOAT(__y, __x) { std::string __tmp = StringUtils::wideToUtf8(TEXTBOX(__x)->getText()); try { __y = std::stof(__tmp);  } catch(...) { __y = 0; } } 
#define READ_SPINNER(__y, __x) __y = SPINNER(__x)->getValue(); 

void ItemPolicyDialog::computePolicyFromGUI() {
    ItemPolicySection *cs = &m_item_policy.m_policy_sections[m_current_section];
    cs->m_section_type = IP_LAPS_BASED;

    READ_SPINNER(cs->m_section_start, "current-section");

    READ_SPINNER(cs->m_section_start, "section-start");

    READ_TEXTBOX_FLOAT(cs->m_linear_mult, "give-at-start");
    SETRULE(cs->m_linear_mult > 0.0f, IPT_LINEAR);

    READ_SPINNER(cs->m_items_per_lap, "give-per-lap");
    SETRULE(cs->m_items_per_lap > 0.0f, IPT_GRADUAL);

    SETRULE(CHECKBOX("clear-previous")->getState(), IPT_CLEAR);
    SETRULE(CHECKBOX("overwrite")->getState(), IPT_OVERWRITE_ITEMS);
    SETRULE(CHECKBOX("force-use")->getState(), IPT_REPLENISH);
    SETRULE(CHECKBOX("anti-hoard")->getState(), IPT_PROGRESSIVE_CAP);
    READ_TEXTBOX_FLOAT(cs->m_progressive_cap, "anti-hoard-strength");
    SETRULE(CHECKBOX("automatic-weights")->getState(), IPT_AUTOMATIC_WEIGHTS);
    SETRULE(CHECKBOX("override-gifts")->getState(), IPT_BONUS_BOX_OVERRIDE);

    SETRULE(CHECKBOX("disable-lapping-hits")->getState(), IPT_BLUE_FLAGS);
    SETRULE(CHECKBOX("pace-car")->getState(), IPT_VIRTUAL_PACE);
    SETRULE(CHECKBOX("pace-car-unlap")->getState(), IPT_UNLAPPING);

    SETRULE(CHECKBOX("forbid-bananas")->getState(), IPT_FORBID_BANANA);
    SETRULE(CHECKBOX("forbid-gifts")->getState(), IPT_FORBID_BONUSBOX);
    SETRULE(CHECKBOX("forbid-nitro")->getState(), IPT_FORBID_NITRO);
    SETRULE(CHECKBOX("joker-laps-override")->getState(), IPT_JOKER_OVERRIDE);

    READ_TEXTBOX_FLOAT(cs->m_virtual_pace_gaps, "pace-car-intervals");

    READ_TEXTBOX_FLOAT(cs->m_deg_mult, "deg-mult");
    READ_TEXTBOX_FLOAT(cs->m_fuel_mult, "fuel-mult");

    SETRULE(CHECKBOX("tyre-change-override")->getState(), IPT_TYRE_CHANGE_TIME_OVERRIDE);
    READ_TEXTBOX_FLOAT(cs->m_tyre_change_time, "tyre-change-override-val");

    SETRULE(CHECKBOX("ghost-karts")->getState(), IPT_GHOST_KARTS);

    // Simultaneously rebuild the item policy and set the dependent GUI elements to visible/invisible
    cs->m_possible_types.clear();
    cs->m_weight_distribution.clear();
    for (std::string powerup_name : PowerupManager::powerup_names) {
        if (CHECKBOX("powerup-"+powerup_name)->getState() == true && m_current_config_tab == 1) {
            if (!SPINNER("powerup-"+powerup_name)->isVisible())
                SPINNER("powerup-"+powerup_name)->setVisible(true);
            int new_weight;
            READ_SPINNER(new_weight, "powerup-"+powerup_name);
            cs->m_possible_types.push_back(PowerupManager::getPowerupType(powerup_name));
            cs->m_weight_distribution.push_back(new_weight);

            if (m_current_config_tab == 1)
                LABEL("powerup-"+powerup_name+"-autorefreshed")->setVisible(true);
        } else if (CHECKBOX("powerup-"+powerup_name)->getState() == true) {
            int new_weight;
            READ_SPINNER(new_weight, "powerup-"+powerup_name);
            cs->m_possible_types.push_back(PowerupManager::getPowerupType(powerup_name));
            cs->m_weight_distribution.push_back(new_weight);
        } else {
            SPINNER("powerup-"+powerup_name)->setVisible(false);
            LABEL("powerup-"+powerup_name+"-autorefreshed")->setVisible(false);
        }
    }

    int sum = 0;
    for (int i : cs->m_weight_distribution) {
        sum += i;
    }

    int idx = 0;
    // Autorefresh the labels containing the percent textually, but only if needed
    for (std::string powerup_name : PowerupManager::powerup_names) {
        if (SPINNER("powerup-"+powerup_name)->isVisible() && CHECKBOX("powerup-"+powerup_name)->getState() == true) {
            float percent = (float)cs->m_weight_distribution[idx]/(float)sum * 100.0f;

            std::ostringstream out;
            out.precision(2);
            out << std::fixed << percent;
            std::string percent_display = out.str() + "%";
            LABEL("powerup-"+powerup_name+"-autorefreshed")->setText(StringUtils::utf8ToWide(percent_display), true);
            idx += 1;
        }
    }
}

#define GETRULE(__z) (cs->m_rules & ItemPolicyRules::__z)
#define SET_CHECKBOX_RULE(__x, __y) CHECKBOX(__x)->setState(GETRULE(__y));
#define SET_TEXTBOX_FLOAT(__y, __x) TEXTBOX(__x)->setText(StringUtils::utf8ToWide(std::to_string(__y)));
#define SET_SPINNER(__y, __x) SPINNER(__x)->setValue(__y); 

void ItemPolicyDialog::setGUIFromPolicy(){
    ItemPolicySection *cs = &m_item_policy.m_policy_sections[m_current_section];
    SPINNER("section-start")->setVisible(true);
    SET_SPINNER(cs->m_section_start, "section-start");

    if (GETRULE(IPT_LINEAR)) {
        SET_TEXTBOX_FLOAT(cs->m_linear_mult, "give-at-start");
    } else {
        SET_TEXTBOX_FLOAT(0.0f, "give-at-start");
    }

    if (GETRULE(IPT_GRADUAL)) {
        SET_SPINNER(cs->m_items_per_lap, "give-per-lap");
    } else {
        SET_SPINNER(0.0f, "give-per-lap");
    }

    SET_CHECKBOX_RULE("clear-previous", IPT_CLEAR);

    SET_CHECKBOX_RULE("clear-previous", IPT_CLEAR);
    SET_CHECKBOX_RULE("overwrite", IPT_OVERWRITE_ITEMS);
    SET_CHECKBOX_RULE("force-use", IPT_REPLENISH);

    // Currently this doesn't have the same "if disabled reset to 0 regardless of the previous value" as
    // give-at-start and give-per-lap because it wasn't really designed that way, but it should. It's fine for now.
    SET_CHECKBOX_RULE("anti-hoard", IPT_PROGRESSIVE_CAP);
    SET_TEXTBOX_FLOAT(cs->m_progressive_cap, "anti-hoard-strength");

    SET_CHECKBOX_RULE("automatic-weights", IPT_AUTOMATIC_WEIGHTS);
    SET_CHECKBOX_RULE("override-gifts", IPT_BONUS_BOX_OVERRIDE);

    SET_CHECKBOX_RULE("disable-lapping-hits", IPT_BLUE_FLAGS);
    SET_CHECKBOX_RULE("pace-car", IPT_VIRTUAL_PACE);
    SET_CHECKBOX_RULE("pace-car-unlap", IPT_UNLAPPING);

    SET_CHECKBOX_RULE("forbid-bananas", IPT_FORBID_BANANA);
    SET_CHECKBOX_RULE("forbid-gifts", IPT_FORBID_BONUSBOX);
    SET_CHECKBOX_RULE("forbid-nitro", IPT_FORBID_NITRO);
    SET_CHECKBOX_RULE("joker-laps-override", IPT_JOKER_OVERRIDE);
    //TODO: joker laps override min and max spinners are useless rn

    SET_TEXTBOX_FLOAT(cs->m_virtual_pace_gaps, "pace-car-intervals");

    SET_TEXTBOX_FLOAT(cs->m_deg_mult, "deg-mult");
    SET_TEXTBOX_FLOAT(cs->m_fuel_mult, "fuel-mult");

    // Currently this doesn't have the same "if disabled reset to 0 regardless of the previous value" as
    // give-at-start and give-per-lap because it wasn't really designed that way, but it should. It's fine for now.
    SET_CHECKBOX_RULE("tyre-change-override", IPT_TYRE_CHANGE_TIME_OVERRIDE);
    SET_TEXTBOX_FLOAT(cs->m_tyre_change_time, "tyre-change-override-val");

    SET_CHECKBOX_RULE("ghost-karts", IPT_GHOST_KARTS);

    // Simultaneously rebuild the GUI and set the dependent GUI elements to visible/invisible

    // Set all elements to invisible and then make visible as needed
    for (std::string powerup_name : PowerupManager::powerup_names) {
        CHECKBOX("powerup-"+powerup_name)->setState(false);
        SPINNER("powerup-"+powerup_name)->setValue(0);

        SPINNER("powerup-"+powerup_name)->setVisible(false);
        LABEL("powerup-"+powerup_name+"-autorefreshed")->setVisible(false);
    }

    int idx = 0;
    for (PowerupManager::PowerupType t : cs->m_possible_types) {
        std::string powerup_name = PowerupManager::getPowerupAsString(t);
        CHECKBOX("powerup-"+powerup_name)->setState(true);
        SPINNER("powerup-"+powerup_name)->setValue(cs->m_weight_distribution[idx]);
        // The spinner will be made visible automatically when needed
        // The autorefreshed text will be updated automatically when needed

        idx += 1;
    }
}


void ItemPolicyDialog::setVisibilityOfPowerupTab(bool visible) {
    LABEL("clear-previous")->setVisible(visible);
    CHECKBOX("clear-previous")->setVisible(visible);

    LABEL("give-at-start")->setVisible(visible);
    TEXTBOX("give-at-start")->setVisible(visible);
    LABEL("give-at-start-info")->setVisible(visible);

    LABEL("give-per-lap")->setVisible(visible);
    SPINNER("give-per-lap")->setVisible(visible);

    LABEL("overwrite")->setVisible(visible);
    CHECKBOX("overwrite")->setVisible(visible);

    LABEL("force-use")->setVisible(visible);
    CHECKBOX("force-use")->setVisible(visible);

    LABEL("anti-hoard")->setVisible(visible);
    CHECKBOX("anti-hoard")->setVisible(visible);

    LABEL("anti-hoard-strength")->setVisible(visible);
    TEXTBOX("anti-hoard-strength")->setVisible(visible);

    LABEL("override-gifts")->setVisible(visible);
    CHECKBOX("override-gifts")->setVisible(visible);
}

void ItemPolicyDialog::setVisibilityOfPowerupPoolTab(bool visible) {
    LABEL("automatic-weights")->setVisible(visible);
    CHECKBOX("automatic-weights")->setVisible(visible);
    for (std::string powerup_name : PowerupManager::powerup_names) {
        LABEL("powerup-"+powerup_name)->setVisible(visible);
        CHECKBOX("powerup-"+powerup_name)->setVisible(visible);

        // These two aren't visible by default, they will be made visible by computePolicyFromGUI() if needed
        SPINNER("powerup-"+powerup_name)->setVisible(false);
        LABEL("powerup-"+powerup_name+"-autorefreshed")->setVisible(false);
    }
}

void ItemPolicyDialog::setVisibilityOfRulesTab(bool visible) {
    LABEL("disable-lapping-hits")->setVisible(visible);
    CHECKBOX("disable-lapping-hits")->setVisible(visible);

    LABEL("pace-car")->setVisible(visible);
    CHECKBOX("pace-car")->setVisible(visible);

    LABEL("pace-car-unlap")->setVisible(visible);
    CHECKBOX("pace-car-unlap")->setVisible(visible);

    LABEL("pace-car-intervals")->setVisible(visible);
    TEXTBOX("pace-car-intervals")->setVisible(visible);

    LABEL("forbid-bananas")->setVisible(visible);
    CHECKBOX("forbid-bananas")->setVisible(visible);

    LABEL("forbid-gifts")->setVisible(visible);
    CHECKBOX("forbid-gifts")->setVisible(visible);

    LABEL("forbid-nitro")->setVisible(visible);
    CHECKBOX("forbid-nitro")->setVisible(visible);

    LABEL("joker-laps-override")->setVisible(visible);
    CHECKBOX("joker-laps-override")->setVisible(visible);

    LABEL("joker-laps-min-max")->setVisible(visible);
    SPINNER("joker-laps-min")->setVisible(visible);
    SPINNER("joker-laps-max")->setVisible(visible);
}

void ItemPolicyDialog::setVisibilityOfFuelAndTyresTab(bool visible) {
    LABEL("deg-mult")->setVisible(visible);
    TEXTBOX("deg-mult")->setVisible(visible);

    LABEL("fuel-mult")->setVisible(visible);
    TEXTBOX("fuel-mult")->setVisible(visible);

    LABEL("tyre-change-override")->setVisible(visible);
    CHECKBOX("tyre-change-override")->setVisible(visible);

    LABEL("tyre-change-override-val")->setVisible(visible);
    TEXTBOX("tyre-change-override-val")->setVisible(visible);

    LABEL("ghost-karts")->setVisible(visible);
    CHECKBOX("ghost-karts")->setVisible(visible);
}
