//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2021 kimden
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

#include "utils/enum_extended_reader.hpp"
#include "utils/log.hpp"
#include "utils/string_utils.hpp"

// ========================================================================

int EnumExtendedReader::parse(std::string& text)
{
    std::string no_whitespaces = StringUtils::removeWhitespaces(text);
    std::vector<std::string> items = StringUtils::split(no_whitespaces, '|');
    int value = 0;
    for (const std::string& key: items) {
        auto it = values.find(key);
        if (it == values.end())
            Log::warn("EnumExtendedReader", "Key \"%s\" not found - ignored.", key.c_str());
        else
            value |= it->second;
    }
    return value;
} // parse
// ========================================================================