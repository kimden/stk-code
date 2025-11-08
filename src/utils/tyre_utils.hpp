//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2025  Nomagno
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

#ifndef HEADER_TYRE_UTILS_HPP
#define HEADER_TYRE_UTILS_HPP

#include "utils/types.hpp"
#include <limits>
#include <string>
#include <vector>
#include <sstream>
#include <irrString.h>
#include <IGUIFont.h>
#include <irrTypes.h>
namespace TyreUtils
{
    std::string getStringFromCompound(unsigned c, bool shortver);

    std::vector<std::tuple<unsigned, unsigned>> stringToStints(std::string x);
    std::string stintsToString(std::vector<std::tuple<unsigned, unsigned>> x);


    const std::vector<unsigned> getAllActiveCompounds(void);
    // This pair of functions extracts data from a format in the form:
    // -1 10 0 -1; 1
    // where the numbers before the semicolon are a list of the tyre amount for
    // each of the selectable tyres, and the number after the semicolon
    // is the amount of wildcards
    std::vector<int> stringToAlloc(const std::string &in);
    int stringToAllocWildcard(const std::string &in);

    std::string allocToString(const std::vector<int> &in);
}
#endif
