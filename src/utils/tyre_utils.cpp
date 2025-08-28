//  SuperTuxKart - a fun racing game with go-kart
//
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

#include "utils/tyre_utils.hpp"

#include "utils/constants.hpp"
#include "utils/log.hpp"
#include "utils/time.hpp"
#include "utils/translation.hpp"
#include "utils/types.hpp"
#include "utils/utf8.h"
#include "irrArray.h"
#include "utils/string_utils.hpp"
#include "karts/kart_properties.hpp"
#include "karts/kart_properties_manager.hpp"

#include "coreutil.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <exception>
#include <iomanip>
#include <sstream>

namespace TyreUtils
{
    std::string getStringFromCompound(unsigned c, bool shortver) {
        const KartProperties *kp = kart_properties_manager->getKart("tux");

        std::vector<std::string> names;
        if (shortver) {
            // TODO: get this string from STKConfig instead
            names = StringUtils::split(std::string("C1 S M H C5 C6 C7 C8 C9 CHEAT"), ' ');
        } else {
            // TODO: get this string from STKConfig instead
            names = StringUtils::split(std::string("COMPOUND1 SOFT MEDIUM HARD COMPOUND5 COMPOUND6 COMPOUND7 COMPOUND8 COMPOUND9 CHEAT"), ' ');
        }

        // Compounds are 1-indexed
        if (names.size() >= c) return names[c-1];
        else {
            std::string ret = "?";
            ret = ret + std::to_string(c);
            return ret;
        }
    };

    //Compound, length
    std::vector<std::tuple<unsigned, unsigned>> stringToStints(std::string x) {
        std::vector<std::tuple<unsigned, unsigned>> retval;
        std::vector<std::string> items = StringUtils::split(x, ',');
        for (int i = 0; i < items.size(); i++) {
            retval.push_back(std::make_tuple(0, 0));
            if (items[i].at(0) == ' ') items[i].erase(0, 1);
            switch (items[i].at(0)) {
            case 'S':
                std::get<0>(retval[i]) = 2;
                break;
            case 'M':
                std::get<0>(retval[i]) = 3;
                break;
            case 'H':
                std::get<0>(retval[i]) = 4;
                break;
            default:
                std::get<0>(retval[i]) = items[i].at(0) - '0';
                break;
            }
            items[i].erase(0, 2);
            std::get<1>(retval[i]) = stoi(items[i]);
        }
        return retval;
    }

    std::string stintsToString(std::vector<std::tuple<unsigned, unsigned>> x) {
        std::string retval;
        for (int i = 0; i < x.size(); i++) {
            unsigned compval = std::get<0>(x[i]);
            std::string compstr = getStringFromCompound(compval, /*shortversion*/ true);

            retval.append(compstr);
            retval.append(":");
            retval.append(std::to_string(std::get<1>(x[i])));
            if (i < x.size()-1) {
                retval.append(", ");
            }
        }
        return retval;
    }
}
