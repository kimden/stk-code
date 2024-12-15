//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2024-2024  Nomagno
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

#ifndef HEADER_TYRE_MOD_AI_HPP
#define HEADER_TYRE_MOD_AI_HPP

#include "karts/controller/ai_base_lap_controller.hpp"
#include "race/race_manager.hpp"
#include "tracks/drive_node.hpp"
#include "utils/random_generator.hpp"
#include <line3d.h>

class ItemManager;
class ItemState;
class LinearWorld;
class Track;

namespace irr
{
    namespace scene
    {
        class ISceneNode;
    }
}

class TyreModAI : public AIBaseLapController
{
private:
    ItemManager* m_item_manager;
    bool vec3Compare(Vec3 a, Vec3 b);
    void computeRacingLine(unsigned int current_node);
protected:
    int placeholder2;
public:
     TyreModAI(Kart *kart);
    ~TyreModAI();
    bool canSkid(float f) { return true; };
    virtual void update(int ticks);
    virtual void reset();
    virtual unsigned int getNextSector(unsigned int index);
    virtual unsigned int getNextSectorIndex(unsigned int index);
    virtual const irr::core::stringw& getNamePostfix() const;
};

#endif
