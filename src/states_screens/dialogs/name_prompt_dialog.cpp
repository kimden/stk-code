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

#include "states_screens/dialogs/name_prompt_dialog.hpp"

#include "config/user_config.hpp"
#include "guiengine/widgets/icon_button_widget.hpp"
#include "guiengine/widgets/label_widget.hpp"
#include "guiengine/widgets/ribbon_widget.hpp"
#include "guiengine/widgets/spinner_widget.hpp"
#include "guiengine/widgets/text_box_widget.hpp"
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
#include <functional>

using namespace GUIEngine;

NamePromptDialog::NamePromptDialog(const irr::core::stringw &prompt, std::function<void(std::string)> continuation) : ModalDialog(0.8f, 0.4f) {
    m_prompt = prompt;
    m_continuation = continuation;

    m_self_destroy = false;
    m_save = false;

    loadFromFile("name_prompt_dialog.stkgui");
}

// ----------------------------------------------------------------------------
void NamePromptDialog::beforeAddingWidgets()
{
    m_prompt_label = getWidget<LabelWidget>("prompt-label");
    assert(m_prompt_label != NULL);
    m_input_box = getWidget<TextBoxWidget>("input-box");
    assert(m_input_box != NULL);

    m_ok_widget = getWidget<IconButtonWidget>("ok");
    assert(m_ok_widget != NULL);
    m_cancel_widget = getWidget<IconButtonWidget>("cancel");
    assert(m_cancel_widget != NULL);
}   // beforeAddingWidgets

// ----------------------------------------------------------------------------
void NamePromptDialog::init()
{
    ModalDialog::init();
    m_prompt_label->setText(m_prompt.c_str(), true);
}   // init

// ----------------------------------------------------------------------------
GUIEngine::EventPropagation
    NamePromptDialog::processEvent(const std::string& src)
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
    } else {
        return GUIEngine::EVENT_LET;
    }
}   // eventCallback


void NamePromptDialog::onUpdate(float dt) {
    bool do_continue = false;
    if (m_self_destroy) {
        if (m_save) {
            std::wstring tmp1 = m_input_box->getText().c_str();
            std::string tmp2( tmp1.begin(), tmp1.end() );
            ModalDialog::dismiss();
            m_continuation(tmp2);
        } else {
            ModalDialog::dismiss();
        }
    }
}
