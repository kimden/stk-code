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

#ifndef ABSTRACT_RESOURCE_HPP
#define ABSTRACT_RESOURCE_HPP

#include <string>
#include "io/xml_node.hpp"

/**
 * AbstractResource represents a generic externally produced resource
 * (that is, typically not produced inside the game, or produced inside the game,
 * but further processed elsewhere).
 */
class AbstractResource
{
public:

    virtual void fromXmlNode(const XMLNode* node) = 0;

    // virtual std::string process(Context& context) = 0;
};


#endif // ABSTRACT_RESOURCE_HPP