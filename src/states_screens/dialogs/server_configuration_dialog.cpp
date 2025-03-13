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
    m_fuel_spinner->setValue(1000);
    m_fuel_text->setVisible(true);

    m_gp_tracks_text = getWidget<LabelWidget>("gp-label");
    assert(m_gp_tracks_text != NULL);
    m_gp_tracks_spinner = getWidget<SpinnerWidget>("gp-spinner");
    assert(m_gp_tracks_spinner != NULL);
    m_gp_tracks_spinner->setVisible(false);
    m_gp_tracks_spinner->setValue(0);
    m_gp_tracks_text->setVisible(false);

    m_fuel_stop_text = getWidget<LabelWidget>("fuel-stop-label");
    assert(m_fuel_stop_text != NULL);
    m_fuel_stop_spinner = getWidget<SpinnerWidget>("fuel-stop-spinner");
    assert(m_fuel_stop_spinner != NULL);
    m_fuel_stop_spinner->setVisible(true);
    m_fuel_stop_spinner->setValue(0);
    m_fuel_stop_text->setVisible(true);


    m_fuel_weight_text = getWidget<LabelWidget>("fuel-weight-label");
    assert(m_fuel_weight_text != NULL);
    m_fuel_weight_spinner = getWidget<SpinnerWidget>("fuel-weight-spinner");
    assert(m_fuel_weight_spinner != NULL);
    m_fuel_weight_spinner->setVisible(true);
    m_fuel_weight_spinner->setValue(0);
    m_fuel_weight_text->setVisible(true);


    m_fuel_rate_text = getWidget<LabelWidget>("fuel-rate-label");
    assert(m_fuel_rate_text != NULL);
    m_fuel_rate_spinner = getWidget<SpinnerWidget>("fuel-rate-spinner");
    assert(m_fuel_rate_spinner != NULL);
    m_fuel_rate_spinner->setVisible(true);
    m_fuel_rate_spinner->setValue(0);
    m_fuel_rate_text->setVisible(true);


    m_fuel_regen_text = getWidget<LabelWidget>("fuel-regen-label");
    assert(m_fuel_regen_text != NULL);
    m_fuel_regen_spinner = getWidget<SpinnerWidget>("fuel-regen-spinner");
    assert(m_fuel_regen_spinner != NULL);
    m_fuel_regen_spinner->setVisible(true);
    m_fuel_regen_spinner->setValue(0);
    m_fuel_regen_text->setVisible(true);

    m_allowed_compounds_1_text = getWidget<LabelWidget>("allowed-compound-1-label");
    assert(m_allowed_compounds_1_text != NULL);
    m_allowed_compounds_1_spinner = getWidget<SpinnerWidget>("allowed-compound-1-spinner");
    assert(m_allowed_compounds_1_spinner != NULL);
    m_allowed_compounds_1_spinner->setVisible(true);
    m_allowed_compounds_1_spinner->setValue(-1);
    m_allowed_compounds_1_text->setVisible(true);

    m_allowed_compounds_2_text = getWidget<LabelWidget>("allowed-compound-2-label");
    assert(m_allowed_compounds_2_text != NULL);
    m_allowed_compounds_2_spinner = getWidget<SpinnerWidget>("allowed-compound-2-spinner");
    assert(m_allowed_compounds_2_spinner != NULL);
    m_allowed_compounds_2_spinner->setVisible(true);
    m_allowed_compounds_2_spinner->setValue(-1);
    m_allowed_compounds_2_text->setVisible(true);

    m_allowed_compounds_3_text = getWidget<LabelWidget>("allowed-compound-3-label");
    assert(m_allowed_compounds_3_text != NULL);
    m_allowed_compounds_3_spinner = getWidget<SpinnerWidget>("allowed-compound-3-spinner");
    assert(m_allowed_compounds_3_spinner != NULL);
    m_allowed_compounds_3_spinner->setVisible(true);
    m_allowed_compounds_3_spinner->setValue(-1);
    m_allowed_compounds_3_text->setVisible(true);


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
            change.addUInt8(LobbyProtocol::LE_CONFIG_SERVER);
            change.addUInt8((uint8_t)m_difficulty_widget
                ->getSelection(PLAYER_ID_GAME_MASTER));

            change.addFloat(m_fuel_spinner->getValue());
            change.addFloat(m_fuel_regen_spinner->getValue());
            change.addFloat(m_fuel_stop_spinner->getValue());
            change.addFloat(m_fuel_weight_spinner->getValue());
            change.addFloat(m_fuel_rate_spinner->getValue());

            change.addUInt8(m_allowed_compounds_1_spinner->getValue()+1);
            change.addUInt8(m_allowed_compounds_2_spinner->getValue()+1);
            change.addUInt8(m_allowed_compounds_3_spinner->getValue()+1);

            RaceManager::get()->setFuelAndQueueInfo(m_fuel_spinner->getValue(), m_fuel_regen_spinner->getValue(), m_fuel_stop_spinner->getValue(),
                                                    m_fuel_weight_spinner->getValue(), m_fuel_rate_spinner->getValue(),
                                                    m_allowed_compounds_1_spinner->getValue(),
                                                    m_allowed_compounds_2_spinner->getValue(),
                                                    m_allowed_compounds_3_spinner->getValue());

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
            STKHost::get()->sendToServer(&change, PRM_RELIABLE);
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
