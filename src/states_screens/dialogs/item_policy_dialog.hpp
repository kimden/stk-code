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

#ifndef HEADER_ITEM_POLICY_DIALOG_HPP
#define HEADER_ITEM_POLICY_DIALOG_HPP

#include "guiengine/modaldialog.hpp"
#include "race/item_policy.hpp"
#include "race/race_manager.hpp"

namespace GUIEngine
{
    class SpinnerWidget;
    class LabelWidget;
    class RibbonWidget;
    class IconButtonWidget;
}

class ItemPolicyDialog : public GUIEngine::ModalDialog
{
private:
    bool m_self_destroy;
    bool m_save;

    int m_current_section;
    int m_current_config_tab;

    ItemPolicy m_item_policy;
    std::string m_path;
    XMLNode *m_root;

    GUIEngine::SpinnerWidget* m_current_section_spinner;
    GUIEngine::LabelWidget* m_current_section_label;
    GUIEngine::IconButtonWidget* m_new_section_widget;
    GUIEngine::IconButtonWidget* m_remove_section_widget;

    GUIEngine::SpinnerWidget* m_section_start_spinner;
    GUIEngine::LabelWidget* m_section_start_label;

    GUIEngine::SpinnerWidget* m_config_mode_spinner;
    GUIEngine::LabelWidget* m_config_mode_label;

    GUIEngine::IconButtonWidget* m_ok_widget;
    GUIEngine::IconButtonWidget* m_cancel_widget;

    void computePolicyFromGUI();
    void setGUIFromPolicy();
    void setVisibilityOfPowerupTab(bool visible);
    void setVisibilityOfPowerupPoolTab(bool visible);
    void setVisibilityOfRulesTab(bool visible);
    void setVisibilityOfFuelAndTyresTab(bool visible);
    void setVisibilityOfInputTab(bool visible);
    void updateMoreOption(int game_mode);
public:
    ItemPolicyDialog(std::string path);
    // ------------------------------------------------------------------------
    void beforeAddingWidgets();
    // ------------------------------------------------------------------------
    GUIEngine::EventPropagation processEvent(const std::string& source);
    // ------------------------------------------------------------------------
    void init();
    // ------------------------------------------------------------------------
    void onEnterPressedInternal()                    { m_self_destroy = true; }
    // ------------------------------------------------------------------------
    bool onEscapePressed() {
        m_self_destroy = true; 
        m_save = false;
        return false;
    }
    // ------------------------------------------------------------------------
    void onUpdate(float dt) OVERRIDE;
    static std::string loadConfig(const std::string &path, bool create_if_missing);
    static bool saveConfig(const std::string &path, const std::string &policy);
};   // class ServerConfigurationDialog

#endif
