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

#ifndef HEADER_NAME_PROMPT_DIALOG_HPP
#define HEADER_NAME_PROMPT_DIALOG_HPP

#include "guiengine/modaldialog.hpp"
#include <functional>

namespace GUIEngine
{
    class SpinnerWidget;
    class LabelWidget;
    class RibbonWidget;
    class IconButtonWidget;
    class TextBoxWidget;
}

class NamePromptDialog : public GUIEngine::ModalDialog
{
private:
    bool m_self_destroy;
    bool m_save;
    std::string m_value;
    irr::core::stringw m_prompt;
    std::function<void(std::string)> m_continuation;


    GUIEngine::LabelWidget* m_prompt_label;
    GUIEngine::TextBoxWidget* m_input_box;

    GUIEngine::IconButtonWidget* m_ok_widget;
    GUIEngine::IconButtonWidget* m_cancel_widget;

public:
    NamePromptDialog(const irr::core::stringw &prompt, std::function<void(std::string)> continuation);
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
    void onUpdate(float dt);
};   // class ServerConfigurationDialog

#endif
