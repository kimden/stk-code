//
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

#include "utils/public_player_value_storage.hpp"

std::vector<std::shared_ptr<PublicPlayerValue>> PublicPlayerValueStorage::values = {};

// Currently supported only for players with account,
// however, currently we'll show it for local accounts too
// if they have a corresponding name.
std::string PublicPlayerValueStorage::get(const std::string& player_name)
{
    // might add caching later

    std::string res;
    for (const auto& ppv: values)
    {
        const auto item = ppv->get(player_name);
        if (!item.has_value())
            continue;

        std::string value = item.value();
        if (!value.empty())
        {
            if (!res.empty())
                res += ", ";
            res += value;
        }
    }
    return res;
}   // get
//-----------------------------------------------------------------------------

void PublicPlayerValueStorage::tryUpdate()
{
    for (const auto& ppv: values)
        ppv->tryUpdate();
}   // tryUpdate
//-----------------------------------------------------------------------------