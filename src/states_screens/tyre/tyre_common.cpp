 //  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2009-2015 Marianne Gagnon
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

#include "states_screens/tyre/tyre_common.hpp"

#include "guiengine/screen.hpp"

namespace TyreCommon
{
    void switchTab(std::string selected_tab)
    {
        GUIEngine::Screen *screen = NULL;
        if (selected_tab == "page1")
            screen = TyreScreen1::getInstance();
        else if (selected_tab == "page2")
            screen = TyreScreen2::getInstance();
        else if (selected_tab == "page3")
            screen = TyreScreen3::getInstance();
        else if (selected_tab == "page4")
            screen = TyreScreen4::getInstance();
        else if (selected_tab == "page5")
            screen = TyreScreen5::getInstance();
        else if (selected_tab == "page6")
            screen = TyreScreen6::getInstance();
        else if (selected_tab == "page7")
            screen = TyreScreen7::getInstance();
        else if (selected_tab == "page8")
            screen = TyreScreen8::getInstance();
        if(screen)
            StateManager::get()->replaceTopMostScreen(screen);
    }
}
