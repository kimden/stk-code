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

#include "graphics/show_curve.hpp"
#include "graphics/slip_stream.hpp"
#include "items/attachment.hpp"
#include "items/item_manager.hpp"
#include "items/powerup.hpp"
#include "items/projectile_manager.hpp"
#include "karts/kart.hpp"
#include "karts/controller/kart_control.hpp"
#include "karts/controller/ai_properties.hpp"
#include "karts/kart_properties.hpp"
#include "karts/max_speed.hpp"
#include "karts/rescue_animation.hpp"
#include "karts/skidding.hpp"
#include "modes/linear_world.hpp"
#include "modes/profile_world.hpp"
#include "physics/triangle_mesh.hpp"
#include "race/race_manager.hpp"
#include "tracks/drive_graph.hpp"
#include "tracks/track.hpp"
#include "utils/constants.hpp"
#include "utils/log.hpp"
#include "utils/vs.hpp"

#include "karts/controller/race_ai_tme.hpp"

//-----------------------------------------------------------------------------
/** Constructor.
 */
TyreModAI::TyreModAI(Kart *kart) : AIBaseLapController(kart)
{
    m_item_manager = Track::getCurrentTrack()->getItemManager();
    reset();
    setControllerName("TME");
}


//-----------------------------------------------------------------------------
/** Resets the AI when a race is restarted.
 */
void TyreModAI::reset() {
    AIBaseLapController::reset();
    m_track_node               = Graph::UNKNOWN_SECTOR;
    DriveGraph::get()->findRoadSector(m_kart->getXYZ(), &m_track_node);
    if(m_track_node==Graph::UNKNOWN_SECTOR)
    {
        Log::error(getControllerName().c_str(),
                   "Invalid starting position for '%s' - not on track"
                   " - can be ignored.",
                   m_kart->getIdent().c_str());
        m_track_node = DriveGraph::get()->findOutOfRoadSector(m_kart->getXYZ());
    }

    AIBaseLapController::reset();
}

//-----------------------------------------------------------------------------
/** Destructor.
 */
TyreModAI::~TyreModAI() { }

//-----------------------------------------------------------------------------
/** Returns the next sector of the given sector index. This is used
 *  for branches in the quad graph to select which way the AI kart should
 *  go. This is a very simple implementation that always returns the first
 *  successor, but it can be overridden to allow a better selection.
 *  \param index Index of the graph node for which the successor is searched.
 *  \return Returns the successor of this graph node.
 */
unsigned int TyreModAI::getNextSector(unsigned int index)
{
    std::vector<unsigned int> successors;
    DriveGraph::get()->getSuccessors(index, successors);
    return successors[0];
}   // getNextSector


//-----------------------------------------------------------------------------
/** Returns the next sector index of the given sector index. This is used
 *  for branches in the quad graph to select which way the AI kart should
 *  go. This is a very simple implementation that always returns the first
 *  successor, but it can be overridden to allow a better selection.
 *  \param index Index of the graph node for which the successor is searched.
 *  \return Returns the successor of this graph node.
 */
unsigned int TyreModAI::getNextSectorIndex(unsigned int index)
{
    return 0;
}   // getNextSectorIndex


bool TyreModAI::vec3Compare(Vec3 a, Vec3 b) {
    return (std::fabs(a.x()-b.x()) < 0.001) &&
           (std::fabs(a.y()-b.y()) < 0.001) &&
           (std::fabs(a.z()-b.z()) < 0.001) ;
}


std::array<std::array<Vec3, 3>, 2> TyreModAI::formTriangles(std::array<Vec3, 4> quad_curr, std::array<Vec3, 4> quad_prev, std::array<Vec3, 4> quad_next){
    bool initialized_1, initialized_2;
    unsigned int intersection_index_1_curr, intersection_index_1_next, intersection_index_2_curr, intersection_index_2_next;
    std::array<std::array<Vec3, 3>, 2> retval;
    unsigned int different_index_curr_first, different_index_curr_second, different_index_next_first, different_index_next_second;

    initialized_1 = false;
    initialized_2 = false;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (vec3Compare(quad_prev[i], quad_curr[j])) {
                if(initialized_1 == false) {
                    intersection_index_1_curr = i;
                    intersection_index_1_next = j;
                } else if (initialized_2 == false) {
                    intersection_index_2_curr = i;
                    intersection_index_2_next = j;
                    break;
                } else {
                    assert(false);
                }
            }
        }
    }

    // We have the two first quads and the edge they share, we form a triangle containing this edge with the latter quad's points
    retval[0][0] = intersection_index_1_next;
    retval[0][1] = intersection_index_2_next;


    initialized_1 = false;
    initialized_2 = false;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (vec3Compare(quad_curr[i], quad_next[j])) {
                if(initialized_1 == false) {
                    intersection_index_1_curr = i;
                    intersection_index_1_next = j;
                } else if (initialized_2 == false) {
                    intersection_index_2_curr = i;
                    intersection_index_2_next = j;
                    break;
                } else {
                    assert(false);
                }
            }
        }
    }
    /*
    // This is to get the index of the two points of each quad that aren't part of the shared edge.
    different_index_curr_first = (intersection_index_1_curr^intersection_index_2_curr) % 4;
    different_index_curr_second = (0 + 1 + 2 + 3 - intersection_index_1_curr - intersection_index_2_curr - different_index_curr_first) % 4;

    different_index_next_first = (intersection_index_1_next^intersection_index_2_next) % 4;
    different_index_next_second = (0 + 1 + 2 + 3 - intersection_index_1_next - intersection_index_2_next - different_index_next_first) % 4;

    */

    // Shifted the window forward by one
    // We have the two first quads and the edge they share, we form a triangle containing this edge with the latter quad's points
    retval[1][0] = intersection_index_1_curr;
    retval[1][1] = intersection_index_2_curr;

    //All quads are connected to their successors and predecessors at opposite ends. This is incredibly useful.
    //So basically, we need to look for a diagonal. That is, take one of the edges, and from the other side, look for the smallest angle.
    if ((retval[0][1]-retval[0][0]).angle(retval[1][0]-retval[0][0]) < (retval[0][1]-retval[0][0]).angle(retval[1][1]-retval[0][0])){
        retval[0][2] = retval[1][0];
        retval[1][2] = retval[0][0];
    } else {
        retval[0][2] = retval[1][1];
        retval[1][2] = retval[0][0];
    }
}

void TyreModAI::computeRacingLine(unsigned int current_node, unsigned int max) {
    std::array<Vec3, 4> quad_curr = DriveGraph::get()->getNode(current_node)->getQuadPoints();
    std::array<Vec3, 4> quad_prev;
    quad_prev[0] = Vec3(0,0,0);
    quad_prev[1] = Vec3(1,0,0) * (quad_curr[1] - quad_curr[0]).length();
    quad_prev[2] = quad_prev[1].rotate(Vec3(0,0,1), PI/2.0f) ;
    quad_prev[3] = quad_prev[1]+quad_prev[2];

    unsigned int next_node = getNextSector(current_node);
    std::array<Vec3, 4> quad_next = DriveGraph::get()->getNode(next_node)->getQuadPoints();

}
 
//-----------------------------------------------------------------------------
/** This is the main entry point for the AI.
 *  It is called once per frame for each AI and determines the behaviour of
 *  the AI, e.g. steering, accelerating/braking, firing.
 */
void TyreModAI::update(int ticks)
{
    float dt = stk_config->ticks2Time(ticks);
    computeRacingLine(m_track_node, 100);

    m_controls->setLookBack(false);
    m_controls->setSteer(0.0f);
    m_controls->setAccel(1.0f);
    m_controls->setSkidControl(KartControl::SC_NONE); // SC_NONE, SC_LEFT, SC_RIGHT
    m_controls->setNitro(false);
    m_controls->setBrake(false);
    m_controls->setRescue(false);

    /*And obviously general kart stuff*/
    AIBaseLapController::update(ticks);
}

//-----------------------------------------------------------------------------
/** Returns a name for the AI.
 *  This is used in profile mode when comparing different AI implementations
 *  to be able to distinguish them from each other.
 */
const irr::core::stringw& TyreModAI::getNamePostfix() const
{
    // Static to avoid returning the address of a temporary string.
    static irr::core::stringw name="(tme)";
    return name;
}   // getNamePostfix
