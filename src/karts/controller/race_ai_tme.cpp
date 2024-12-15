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


void TyreModAI::computeRacingLine(unsigned int current_node) {
    std::array<Vec3, 4> quad_curr = DriveGraph::get()->getNode(current_node)->getQuadPoints();
    Vec3 quad_curr_normal = (quad_curr[0] - quad_curr[2]).cross(quad_curr[1] - quad_curr[3]);

    unsigned int next_node_index = getNextSectorIndex(current_node); // The successor index
    unsigned int next_node = getNextSector(current_node);
    std::array<Vec3, 4> quad_next = DriveGraph::get()->getNode(next_node)->getQuadPoints();
    Vec3 quad_next_normal = (quad_next[0] - quad_next[2]).cross(quad_next[1] - quad_next[3]);
    float angle = quad_curr_normal.angle(quad_next_normal);


    bool initialized_1 = false, initialized_2 = false;
    Vec3 intersection1, intersection2;
    unsigned int intersection_index_1_curr, intersection_index_1_next, intersection_index_2_curr, intersection_index_2_next;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (vec3Compare(quad_curr[i], quad_next[j])) {
                if(initialized_1 == false) {
                    intersection1 = quad_curr[i];
                    intersection_index_1_curr = i;
                    intersection_index_1_next = j;
                } else if (initialized_2 == false) {
                    intersection2 = quad_curr[i];
                    intersection_index_2_curr = i;
                    intersection_index_2_next = j;
                    break;
                } else {
                    assert(false);
                }
            }
        }
    }
    // We have the two first quads and the edge they share, now we must
    // detemine the vertical angle between them in order to get them on
    // the same plane for 2D racing line calculations.
    // Once we do this, we keep following the same process recursively
    // to get all quads on the same plane until one of the following occur:
    // 1- We reached a quad with index equal to the original, AKA we completely mapped the lap (unlikely)
    // 2- We reached a point where, if we were to make the next quad coplanary to the rest, it would overlap with some of them.
    // 3- We reached the predetermined limit of quads to use for the calculation.

    Vec3 edgeVector = intersection1 - intersection2;

    // This is to get the index of the two points of each quad that aren't part of the shared edge.
    unsigned int different_index_curr_first = (intersection_index_1_curr^intersection_index_2_curr) % 4;
    unsigned int different_index_curr_second = (0 + 1 + 2 + 3 - intersection_index_1_curr - intersection_index_2_curr - different_index_curr_first) % 4;

    unsigned int different_index_next_first = (intersection_index_1_next^intersection_index_2_next) % 4;
    unsigned int different_index_next_second = (0 + 1 + 2 + 3 - intersection_index_1_next - intersection_index_2_next - different_index_next_first) % 4;

    std::array<Vec3, 4> flattened_next;
    /*
        One edge of the quad is the rotation axis. I want to rotate the other two points around it.
        So what I do is take the vectors from the two vertices of the quad that are in the axis, to the other two points, and rotate THESE.
        And then I add them back to the coordinates of the original axis vertices.

        Unrelated but this proves the non-necessity of a stack:
         Ok so, what you say is calculate relative angle with unrotated version,
         move points so that it's connected again, and re-rotate to match old angle?
         AHHH, you are have genius.
         (Thanks kimden for helping me with the math for this whole thing lmao)
    */

    // Don't change the points on the rotation axis
    flattened_next[intersection_index_1_next] = quad_next[intersection_index_1_next];
    flattened_next[intersection_index_2_next] = quad_next[intersection_index_2_next];

    //rotate around axis
    flattened_next[different_index_next_first] = quad_next[different_index_next_first]+(quad_next[intersection_index_1_next] - quad_next[different_index_next_first]).rotate(edgeVector, -angle);
    flattened_next[different_index_next_second] = quad_next[different_index_next_second] + (quad_next[intersection_index_2_next] - quad_next[different_index_next_second]).rotate(edgeVector, -angle);

    
    // Once this has happened, we have obtained our 2D surface, which we will now use to compute the ideal racing line.

    // The racing line will be composed exclusively of circumference arcs (the straight line being a
    // degenerate case with radius=infinity), which will have their length maximized within the possibilities
    // of the minimization of the racing line length, which must always take priority.
    // These arcs will then be mapped onto each quad.
    // After the circumference arcs have been successfully split and re-defined in function
    // of the quads they go through, we can re-apply the entire rotation chain backwards to obtain our 3D quad surface that contains the racing line.
    // When a kart is going over a quad, we can then simply observe whether we need to increase, decrease or maintain the steering and speed,
    // to follow the plan. Skidding is, of course, an option that is also very good, though it must be released periodically to keep the speed boost.
    // Skidding does also impose a maximum turn radius, though. In general, it must be said that it is preferrable to impose the maximum turn radius
    // of the skid into the optimization algorithm, in order to successfully encode STK "snaking" mechanics. Another option is to simply overlay at 
    // every point the widest possible drifting arc in order to check if it's possible to do it instead of going in a straight line. Only reserved for zones entirely composed of straight lines, which will have their own marker since "infinity radius" isn't a real number. It should also have checks to see if it should go to the appropiate part of the track to take a turn, so it doesn't enter corners suboptimally.
}
 
//-----------------------------------------------------------------------------
/** This is the main entry point for the AI.
 *  It is called once per frame for each AI and determines the behaviour of
 *  the AI, e.g. steering, accelerating/braking, firing.
 */
void TyreModAI::update(int ticks)
{
    float dt = stk_config->ticks2Time(ticks);
    computeRacingLine(m_track_node);

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
