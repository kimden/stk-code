//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2009-2015 Marianne Gagnon
//  Copyright (C) 2016 C. Michael Murphey
//  Copyright (C) 2024-2025 Nomagno
//  Copyright (C) 2025 Matahina
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

// Manages includes common to all tyre screens
#include "states_screens/tyre/tyre_common.hpp"

using namespace GUIEngine;

// -----------------------------------------------------------------------------

TyreScreen6::TyreScreen6() : Screen("tyre/tyre6.stkgui")
{
}   // TyreScreen6

// -----------------------------------------------------------------------------

void TyreScreen6::loadedFromFile()
{
}   // loadedFromFile

// -----------------------------------------------------------------------------

void TyreScreen6::eventCallback(Widget* widget, const std::string& name, const int playerID)
{
    if (name == "category")
    {
        
        std::string selection = ((RibbonWidget*)widget)->getSelectionIDString(PLAYER_ID_GAME_MASTER);

        if (selection != "page6")
            TyreCommon::switchTab(selection);
    }
    else if (name == "back")
    {
        StateManager::get()->escapePressed();
    }
}   // eventCallback

// -----------------------------------------------------------------------------

void TyreScreen6::init()
{
    Screen::init();
    RibbonWidget* w = this->getWidget<RibbonWidget>("category");

    if (w != NULL)
    {
        w->setFocusForPlayer(PLAYER_ID_GAME_MASTER);
        w->select( "page6", PLAYER_ID_GAME_MASTER );
    }
}   // init

// -----------------------------------------------------------------------------
