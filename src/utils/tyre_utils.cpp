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
    std::string getKartFromCompound(unsigned c) {
        if(c < 124) return "ERROR";
        const KartProperties *kp = kart_properties_manager->getKart("tux");
        std::vector<std::string> names = StringUtils::split(kp->getTyresChangeKartMap(), ' ');
        if (names.size() > c - 124) {
            return names[c-124];
        } else {
            return "???";
        }
    }

    std::string getStringFromCompound(unsigned c, bool shortver) {
        if (c == 123) return std::string("FUEL");

        if (c >= 124) return "KART: " + getKartFromCompound(c);

        const KartProperties *kp = kart_properties_manager->getKart("tux");
        std::vector<std::string> names;
        if (shortver) {
            names = StringUtils::split(kp->getTyresNamesShort(), ' ');
        } else {
            names = StringUtils::split(kp->getTyresNamesLong(), ' ');
        }

        // Compounds are 1-indexed
        if (c == 0) {
            std::string ret = "?0";
            return ret;
        } else if (names.size() >= c) {
            return names[c-1];
        } else {
            std::string ret = "?";
            ret = ret + std::to_string(c);
            return ret;
        }
    };

    //Compound, length
    std::vector<std::tuple<unsigned, unsigned>> stringToStints(std::string x) {
        std::vector<std::tuple<unsigned, unsigned>> retval;
        std::vector<std::string> items = StringUtils::split(x, ',');
        for (unsigned i = 0; i < items.size(); i++) {
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
        for (unsigned i = 0; i < x.size(); i++) {
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

    /** Returns a vector that maps each active tyre index to its absolute index */
    const std::vector<unsigned> getAllActiveCompounds(bool exclude_cheat) {
        std::vector<unsigned> retval;

        const KartProperties *kp = kart_properties_manager->getKart("tux");
        const unsigned compound_number = kp->getTyresCompoundNumber();
        auto compound_colors = kp->getTyresDefaultColor();

        for (unsigned i = 0; i < compound_number; i++) {
            bool excluded = exclude_cheat && StringUtils::startsWith(getStringFromCompound(i+1, false /*shortver*/), "CHEAT");
            if (compound_colors[i] > -0.5f && !excluded) {
                retval.push_back(i+1);
            }
        }
        return retval;
    }


    // This pair of functions extracts data from a format in the form:
    // -1 10 0 -1; 1
    // where the numbers before the semicolon are a list of the tyre amount for
    // each of the selectable tyres, and the number after the semicolon
    // is the amount of wildcards
    std::vector<int> stringToAlloc(const std::string &in) {
        std::vector<int> retval = {};

        std::vector<unsigned> tyre_mapping = getAllActiveCompounds();
        const KartProperties *kp = kart_properties_manager->getKart("tux");
        const unsigned compound_number = kp->getTyresCompoundNumber();
        for (unsigned i = 0; i < compound_number; i++) {
            retval.push_back(-1);
        }

        std::vector<std::string> strs = StringUtils::split(in, ';');
        if (strs.size() == 0) return retval;

        std::vector<std::string> nums = StringUtils::split(strs[0], ' ');
        if (nums.size() == 0) return retval;

        // Remove all empty strings from the vector
        nums.erase(std::remove(nums.begin(), nums.end(), ""), nums.end());

        for (unsigned i = 0; i < nums.size(); i++) {
            int val;
            try         { val = std::stoi(nums[i]); }
            catch (...) { val = -1; }

            if (i < tyre_mapping.size()) {
                if (tyre_mapping[i]-1 < retval.size()) {
                    retval[tyre_mapping[i]-1] = val;
                }
            }
        }
        return retval;
    }
    int stringToAllocWildcard(const std::string &in) {
        int retval;

        std::vector<std::string> strs = StringUtils::split(in, ';');
        if (strs.size() <= 1)
            return 0;

        try         { retval = std::stoi(strs[1]); }
        catch (...) { retval = 0; }

        return retval;        
    }

    std::string allocToString(const std::vector<int> &input) {
        std::string retval = "{";
        std::vector<unsigned> tyre_mapping = getAllActiveCompounds();
        for (unsigned i = 0; i < tyre_mapping.size(); i++) {
            if (tyre_mapping[i]-1 < input.size()) {
                retval += std::to_string(input[tyre_mapping[i]-1]);
                retval += " ";
            }
        }
        // remove trailing space if needed
        if (retval.size() > 1)
            retval.pop_back();

        retval += "}";

        return retval;
    }

}
