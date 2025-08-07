//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2025 kimden
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

#ifndef SET_WITH_FLIP_HPP
#define SET_WITH_FLIP_HPP

#include "utils/log.hpp"

#include <set>

enum Op: int
{
    SWF_OP_ADD = 1,
    SWF_OP_REMOVE = 0,
    SWF_OP_FLIP = -1,
};

// Add several implementations for different cases later
// (e.g. bitset at most 60 elements)
template<typename T>
class SetWithFlip: public std::set<T>
{
public:
    int add(const T& element)
    {
        this->insert(element);
        return SWF_OP_ADD;
    }

    int remove(const T& element)
    {
        this->erase(element);
        return SWF_OP_REMOVE;
    }

    int flip(const T& element)
    {
        auto it = this->find(element);
        if (it != this->end())
        {
            return remove(element);
        }
        return add(element);
    }

    bool has(const T& element)
    {
        return this->find(element) != this->end();
    }

    int set(const T& element, int type)
    {
        switch (type)
        {
            case SWF_OP_ADD: return add(element);
            case SWF_OP_REMOVE: return remove(element);
            case SWF_OP_FLIP: return flip(element);
        }
        
        Log::warn("SetWithFlip", "Invalid type = %d encountered, "
                "defaulting to flip (%d).", type, SWF_OP_FLIP);
        return flip(element);
    }

    // Iterates over all elements, works in O(n log n). Use with caution.
    SetWithFlip<T> operator & (const SetWithFlip& rhs) const
    {
        auto it1 = this->cbegin();
        auto it2 = rhs.cbegin();
        SetWithFlip<T> result;
        while (it1 != this->cend() && it2 != rhs.cend())
        {
            const T& a = *it1;
            const T& b = *it2;
            if (a == b)
            {
                result.insert(a);
                ++it1;
                ++it2;
            }
            else if (a < b)
                ++it1;
            else
                ++it2;
        }
        return result;
    }
};

#endif