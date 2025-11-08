//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2018 SuperTuxKart-Team
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

#include "states_screens/dialogs/server_configuration_dialog.hpp"

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
#include "utils/tyre_utils.hpp"
#include "utils/string_utils.hpp"
#include "utils/translation.hpp"

using namespace GUIEngine;

// ----------------------------------------------------------------------------
void ServerConfigurationDialog::beforeAddingWidgets()
{
    m_more_options_text = getWidget<LabelWidget>("more-options");
    assert(m_more_options_text != NULL);
    m_more_options_spinner = getWidget<SpinnerWidget>("more-options-spinner");
    assert(m_more_options_spinner != NULL);

    m_fuel_text = getWidget<LabelWidget>("fuel-label");
    assert(m_fuel_text != NULL);
    m_fuel_spinner = getWidget<SpinnerWidget>("fuel-spinner");
    assert(m_fuel_spinner != NULL);
    m_fuel_spinner->setVisible(true);
    m_fuel_spinner->setValue(0);
    m_fuel_text->setVisible(true);

    m_gp_tracks_text = getWidget<LabelWidget>("gp-label");
    assert(m_gp_tracks_text != NULL);
    m_gp_tracks_spinner = getWidget<SpinnerWidget>("gp-spinner");
    assert(m_gp_tracks_spinner != NULL);
    m_gp_tracks_spinner->setVisible(false);
    m_gp_tracks_spinner->setValue(0);
    m_gp_tracks_text->setVisible(false);

    m_allowed_selection_text = getWidget<LabelWidget>("allowed-selection-label");
    assert(m_allowed_selection_text != NULL);
    m_allowed_selection_spinner = getWidget<SpinnerWidget>("allowed-selection-spinner");
    assert(m_allowed_selection_spinner != NULL);
    m_allowed_selection_spinner->setVisible(true);
    m_allowed_selection_text->setVisible(true);

    m_allowed_value_text = getWidget<LabelWidget>("allowed-value-label");
    assert(m_allowed_value_text != NULL);
    m_allowed_value_spinner = getWidget<SpinnerWidget>("allowed-value-spinner");
    assert(m_allowed_value_spinner != NULL);
    m_allowed_value_spinner->setVisible(true);
    m_allowed_value_text->setVisible(true);

    m_allowed_wildcards_text = getWidget<LabelWidget>("allowed-wildcards-label");
    assert(m_allowed_wildcards_text != NULL);
    m_allowed_wildcards_spinner = getWidget<SpinnerWidget>("allowed-wildcards-spinner");
    assert(m_allowed_wildcards_spinner != NULL);
    m_allowed_wildcards_spinner->setVisible(true);
    m_allowed_wildcards_text->setVisible(true);

    m_item_preview_text = getWidget<LabelWidget>("item-preview-label");
    assert(m_item_preview_text != NULL);
    m_item_preview_spinner = getWidget<SpinnerWidget>("item-preview-spinner");
    assert(m_item_preview_spinner != NULL);
    m_item_preview_spinner->setVisible(true);
    m_item_preview_text->setVisible(true);


    m_options_widget = getWidget<RibbonWidget>("options");
    assert(m_options_widget != NULL);
    m_game_mode_widget = getWidget<RibbonWidget>("gamemode");
    assert(m_game_mode_widget != NULL);
    m_difficulty_widget = getWidget<RibbonWidget>("difficulty");
    assert(m_difficulty_widget != NULL);
    m_ok_widget = getWidget<IconButtonWidget>("ok");
    assert(m_ok_widget != NULL);
    m_cancel_widget = getWidget<IconButtonWidget>("cancel");
    assert(m_cancel_widget != NULL);
}   // beforeAddingWidgets

// ----------------------------------------------------------------------------
void ServerConfigurationDialog::init()
{
    ModalDialog::init();

    RibbonWidget* difficulty = getWidget<RibbonWidget>("difficulty");
    assert(difficulty != NULL);
    difficulty->setSelection((int)RaceManager::get()->getDifficulty(),
        PLAYER_ID_GAME_MASTER);

    RibbonWidget* gamemode = getWidget<RibbonWidget>("gamemode");
    assert(gamemode != NULL);
    gamemode->setSelection(m_prev_mode, PLAYER_ID_GAME_MASTER);

    updateMoreOption(m_prev_mode);
    m_options_widget->setFocusForPlayer(PLAYER_ID_GAME_MASTER);
    m_options_widget->select("cancel", PLAYER_ID_GAME_MASTER);

    RaceManager::TyreModRules *tme_rules = RaceManager::get()->getTyreModRules();
    std::vector<unsigned> tyre_mapping = TyreUtils::getAllActiveCompounds();

    m_allowed_wildcards_spinner->setValue(0);
    m_item_preview_spinner->setValue((unsigned)tme_rules->do_item_preview);
    m_fuel_spinner->setValue(tme_rules->fuel_mode);
    m_previous_tyre_selection_value = 0;
    m_tyre_alloc = tme_rules->tyre_allocation;

    bool first_value = true;
    for (unsigned i = 0; i < tyre_mapping.size(); i++) {
        std::string name = TyreUtils::getStringFromCompound(tyre_mapping[i], false);
        irr::core::stringw label = _("%s", name.c_str());
        m_allowed_selection_spinner->addLabel(label);

        if (first_value == true) {
            first_value = false;
            m_allowed_selection_spinner->setValue(label);
            m_allowed_value_spinner->setValue(tme_rules->tyre_allocation[tyre_mapping[i]-1]);
        }
    }


}   // init

// ----------------------------------------------------------------------------
GUIEngine::EventPropagation
    ServerConfigurationDialog::processEvent(const std::string& source)
{
    if (source == m_options_widget->m_properties[PROP_ID])
    {
        const std::string& selection =
            m_options_widget->getSelectionIDString(PLAYER_ID_GAME_MASTER);
        if (selection == m_cancel_widget->m_properties[PROP_ID])
        {
            m_self_destroy = true;
            return GUIEngine::EVENT_BLOCK;
        }
        else if (selection == m_ok_widget->m_properties[PROP_ID])
        {
            m_self_destroy = true;
            NetworkString change(PROTOCOL_LOBBY_ROOM);
            change.addUInt8(LobbyEvent::LE_CONFIG_SERVER);
            change.addUInt8((uint8_t)m_difficulty_widget
                ->getSelection(PLAYER_ID_GAME_MASTER));

            change.addUInt8(m_fuel_spinner->getValue());

            change.addUInt8(m_tyre_alloc.size());
            for (unsigned i = 0; i < m_tyre_alloc.size(); i++) {
                change.addInt8(m_tyre_alloc[i]);
            }

            change.addUInt8(m_allowed_wildcards_spinner->getValue());
            change.addUInt8(m_item_preview_spinner->getValue());

            RaceManager::get()->setTyreModRules(m_fuel_spinner->getValue(),
                                                    m_tyre_alloc,
                                                    m_allowed_wildcards_spinner->getValue(),
                                                    m_item_preview_spinner->getValue());

            switch (m_game_mode_widget->getSelection(PLAYER_ID_GAME_MASTER))
            {
                case 0:
                {  
                    unsigned v = m_gp_tracks_spinner->getValue();
                    if (v > 0)
                        change.addUInt8(0).addUInt8(0).addUInt8(v);
                    else
                        change.addUInt8(3).addUInt8(0).addUInt8(v);
                    break;
                }
                case 1:
                {
                    unsigned v = m_gp_tracks_spinner->getValue();
                    if (v > 0)
                        change.addUInt8(1).addUInt8(0).addUInt8(v);
                    else
                        change.addUInt8(4).addUInt8(0).addUInt8(v);
                    break;
                }
                case 2:
                {
                    unsigned v = m_more_options_spinner->getValue();
                    unsigned v2 = m_gp_tracks_spinner->getValue();
                    if (v == 0)
                        change.addUInt8(7).addUInt8(0).addUInt8(v2);
                    else
                        change.addUInt8(8).addUInt8(0).addUInt8(v2);
                    break;
                }
                case 3:
                {
                    int v = m_more_options_spinner->getValue();
                    unsigned v2 = m_gp_tracks_spinner->getValue();
                    change.addUInt8(6).addUInt8((uint8_t)v).addUInt8(v2);
                    break;
                }
                default:
                {
                    break;
                }
            }
            Comm::sendToServer(&change, PRM_RELIABLE);
            return GUIEngine::EVENT_BLOCK;
        }
    }
    else if (source == m_game_mode_widget->m_properties[PROP_ID])
    {
        const int selection =
            m_game_mode_widget->getSelection(PLAYER_ID_GAME_MASTER);
        m_prev_value = 0;
        updateMoreOption(selection);
        m_prev_mode = selection;
        return GUIEngine::EVENT_BLOCK;
    }
    return GUIEngine::EVENT_LET;
}   // eventCallback

// ----------------------------------------------------------------------------
void ServerConfigurationDialog::updateMoreOption(int game_mode)
{
    // Disable for some modes? not sure yet
    m_gp_tracks_text->setVisible(true);
    m_gp_tracks_spinner->setVisible(true);
    switch (game_mode)
    {
        case 0:
        case 1:
        {
            m_more_options_text->setVisible(false);
            m_more_options_spinner->setVisible(false);
            break;
        }
        case 2:
        {
            m_more_options_text->setVisible(true);
            m_more_options_spinner->setVisible(true);
            m_more_options_spinner->clearLabels();
            m_more_options_text->setText(_("Battle mode"), false);
            m_more_options_spinner->setVisible(true);
            m_more_options_spinner->clearLabels();
            m_more_options_spinner->addLabel(_("Free-For-All"));
            m_more_options_spinner->addLabel(_("Capture The Flag"));
            m_more_options_spinner->setValue(m_prev_value);
            break;
        }
        case 3:
        {
            m_more_options_text->setVisible(true);
            m_more_options_spinner->setVisible(true);
            m_more_options_spinner->clearLabels();
            m_more_options_text->setText(_("Soccer game type"), false);
            m_more_options_spinner->setVisible(true);
            m_more_options_spinner->clearLabels();
            m_more_options_spinner->addLabel(_("Time limit"));
            m_more_options_spinner->addLabel(_("Goals limit"));
            m_more_options_spinner->setValue(m_prev_value);
            break;
        }
        default:
        {
            m_more_options_text->setVisible(false);
            m_more_options_spinner->setVisible(false);
            break;
        }
    }
}   // updateMoreOption

void ServerConfigurationDialog::onUpdate(float t) {
        std::vector<unsigned> tyre_mapping = TyreUtils::getAllActiveCompounds();
        int prev = m_previous_tyre_selection_value;
        int curr = m_allowed_selection_spinner->getValue();
        // If the selection spinner changed, update the value spinner
        if (curr != prev) {
            m_allowed_value_spinner->setValue(m_tyre_alloc[tyre_mapping[prev]-1]);
            m_previous_tyre_selection_value = curr;
        }

        if (tyre_mapping[curr]-1 < m_tyre_alloc.size()) {
            m_tyre_alloc[tyre_mapping[curr]-1] = m_allowed_value_spinner->getValue();
        }


        if (m_self_destroy)
            ModalDialog::dismiss();
}
