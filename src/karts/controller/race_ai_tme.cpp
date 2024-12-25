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

//#include <iostream>

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

std::array<unsigned, 2> oddTwoOut(unsigned a, unsigned b) {
	a = a % 4;
	b = b % 4;
	std::vector<unsigned> vec = {0, 1, 2, 3};
	vec.erase(std::find(vec.begin(),vec.end(),a));
	vec.erase(std::find(vec.begin(),vec.end(),b));
	return {vec[0], vec[1]};
}



//Return value structure:
// Note that the points on index 2 of each triangle define the shared edge of the triangles. Or they should define it, at least
//First triangle (connected to predecessor):
//  0- Point 1 of shared edge, equivalent index in predecessor
//  1- Point 2 of shared edge, equivalent index in predecessor
//  2- Point not on shared edge, equivalent index in predecessor (-1 since there will be no equivalent index)
//Second triangle (connected to successor):
//  0- Point 1 of shared edge, equivalent index in successor
//  1- Point 2 of shared edge, equivalent index in successor
//  2- Point not on shared edge, equivalent index in successor (-1 since there will be no equivalent index)


std::array<std::array<std::array<int, 2>, 3>, 2> TyreModAI::formTriangles(std::array<Vec3, 4> quad_prev, std::array<Vec3, 4> quad_curr, std::array<Vec3, 4> quad_next){
    bool initialized_1, initialized_2;
    unsigned int intersection_index_1_curr = 0, intersection_index_1_next = 0, intersection_index_2_curr = 0, intersection_index_2_next = 0;
    std::array<std::array<std::array<int, 2>, 3>, 2> retval;
    //unsigned int different_index_curr_first, different_index_curr_second, different_index_next_first, different_index_next_second;

    initialized_1 = false;
    initialized_2 = false;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (vec3Compare(quad_prev[i], quad_curr[j])) {
                if(initialized_1 == false) {
                    intersection_index_1_curr = i;
                    intersection_index_1_next = j;
                    initialized_1 = true;
                } else if (initialized_2 == false) {
                    intersection_index_2_curr = i;
                    intersection_index_2_next = j;
                    initialized_2 = true;
                    break;
                } else {
                    assert(false);
                }
            }
        }
    }
    assert(initialized_1 && initialized_2);

    // We have the two first quads and the edge they share, we form a triangle containing this edge with the latter quad's points
    retval[0][0][0] = intersection_index_1_next;
    retval[0][1][0] = intersection_index_2_next;

    retval[0][0][1] = intersection_index_1_curr;
    retval[0][1][1] = intersection_index_2_curr;


    initialized_1 = false;
    initialized_2 = false;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (vec3Compare(quad_curr[i], quad_next[j])) {
                if(initialized_1 == false) {
                    intersection_index_1_curr = i;
                    intersection_index_1_next = j;
                    initialized_1 = true;
                } else if (initialized_2 == false) {
                    intersection_index_2_curr = i;
                    intersection_index_2_next = j;
                    initialized_2 = true;
                    break;
                } else {
                    assert(false);
                }
            }
        }
    }
    assert(initialized_1 && initialized_2);
    /*
    // This is to get the index of the two points of each quad that aren't part of the shared edge.
    different_index_curr_first = (intersection_index_1_curr^intersection_index_2_curr) % 4;
    different_index_curr_second = (0 + 1 + 2 + 3 - intersection_index_1_curr - intersection_index_2_curr - different_index_curr_first) % 4;

    different_index_next_first = (intersection_index_1_next^intersection_index_2_next) % 4;
    different_index_next_second = (0 + 1 + 2 + 3 - intersection_index_1_next - intersection_index_2_next - different_index_next_first) % 4;

    */

    // Shifted the window forward by one
    // We have the two first quads and the edge they share, we form a triangle containing this edge with the latter quad's points
    retval[1][0][0] = intersection_index_1_curr;
    retval[1][1][0] = intersection_index_2_curr;

    retval[1][0][1] = intersection_index_1_next;
    retval[1][1][1] = intersection_index_2_next;

    //All quads are connected to their successors and predecessors at opposite ends. This is incredibly useful.
    //So basically, we need to look for a diagonal. That is, take one of the edges, and from the other side, look for the smallest angle.
    if ((quad_curr[retval[0][1][0]]-quad_curr[retval[0][0][0]]).angle(quad_curr[retval[1][0][0]]-quad_curr[retval[0][0][0]]) < (quad_curr[retval[0][1][0]]-quad_curr[retval[0][0][0]]).angle(quad_curr[retval[1][1][0]]-quad_curr[retval[0][0][0]])){
        retval[0][2][0] = retval[1][0][0];
        retval[1][2][0] = retval[0][0][0];
    } else {
        retval[0][2][0] = retval[1][1][0];
        retval[1][2][0] = retval[0][0][0];
    }
    retval[0][2][1] = -1;
    retval[1][2][1] = -1;
    return retval;
}

//#include <cfenv>
void TyreModAI::computeRacingLine(unsigned int current_node, unsigned int max) {
    std::vector<std::array<Vec3, 4>> flattened_nodes;


    std::array<Vec3, 4> quad_curr = DriveGraph::get()->getNode(current_node)->getQuadPoints();

	//This ensures we get a dummy first flattened quad that is on the XY plane... maybe
    std::array<Vec3, 4> quad_prev_flat;
    quad_prev_flat[0] = Vec3(0,0,0);
    quad_prev_flat[1] = Vec3(1,0,0) * (quad_curr[1] - quad_curr[0]).length();
    quad_prev_flat[2] = quad_prev_flat[1].rotate(Vec3(0,0,1), PI/2.0f) ;
    quad_prev_flat[3] = quad_prev_flat[1]+quad_prev_flat[2];
    flattened_nodes.push_back(quad_prev_flat);

	//This ensures we get a dummy first quad that plays nice
    std::array<Vec3, 4> quad_prev;
    quad_prev[2] = quad_curr[0];
    quad_prev[3] = quad_curr[1];
    quad_prev[0] = quad_prev[2]-(quad_curr[2]-quad_curr[0]);
    quad_prev[1] = quad_prev[3]-(quad_curr[3]-quad_curr[0]);


    unsigned int next_node = getNextSector(current_node);
    std::array<Vec3, 4> quad_next = DriveGraph::get()->getNode(next_node)->getQuadPoints();


    //std::cout << "[flat] START OF COMPUTED SURFACE" << std::endl;
    for(unsigned i = 1; i < max; i++) {
        std::array<std::array<std::array<int, 2>, 3>, 2> aux_indexes = formTriangles(quad_prev, quad_curr, quad_next);
        unsigned triangle_diagonal_p1 = aux_indexes[0][2][0];
        unsigned triangle_diagonal_p2 = aux_indexes[1][2][0];
        assert(triangle_diagonal_p1 != triangle_diagonal_p2);
        unsigned triangle1_tip = oddTwoOut(triangle_diagonal_p1, triangle_diagonal_p2)[0];
        unsigned triangle2_tip = oddTwoOut(triangle_diagonal_p1, triangle_diagonal_p2)[1];
        Vec3 triangle_common_axis = quad_curr[triangle_diagonal_p2] - quad_curr[triangle_diagonal_p1];
        Vec3 triangle1_normal = triangle_common_axis.cross(quad_curr[triangle1_tip] - quad_curr[triangle_diagonal_p1]);
        Vec3 triangle2_normal = triangle_common_axis.cross(quad_curr[triangle2_tip] - quad_curr[triangle_diagonal_p1]);
        float angle_between_tris = triangle1_normal.angle(triangle2_normal);

        std::array<Vec3, 4> quad_flattened;
        quad_flattened[triangle_diagonal_p1] = quad_curr[triangle_diagonal_p1];
        quad_flattened[triangle_diagonal_p2] = quad_curr[triangle_diagonal_p2];
        quad_flattened[triangle1_tip] = quad_curr[triangle1_tip];
        quad_flattened[triangle2_tip] = quad_curr[triangle_diagonal_p1] + ((quad_curr[triangle2_tip] - quad_curr[triangle_diagonal_p1]).rotate(triangle_common_axis, -angle_between_tris));

        unsigned quad_shared_p1_curr = aux_indexes[0][0][0];
        unsigned quad_shared_p1_prev = aux_indexes[0][0][1];
        unsigned quad_shared_p2_curr = aux_indexes[0][1][0];
        unsigned quad_shared_p2_prev = aux_indexes[0][1][1];

        //The edge that is not shared with the predecessor. It's shared with the successor though, of course
        unsigned quad_nonshared_p1_curr = aux_indexes[1][0][0];
        unsigned quad_nonshared_p1_prev = aux_indexes[1][0][1];
        unsigned quad_nonshared_p2_curr = aux_indexes[1][1][0];
        unsigned quad_nonshared_p2_prev = aux_indexes[1][1][1];

        //Now we move point 1 of this flattened quad to its equivalent index in the predecessor, rotate the point 2 to be connected.
        Vec3 displacement_vector = (flattened_nodes.back())[quad_shared_p1_prev] - quad_flattened[quad_shared_p1_curr];

        quad_flattened[0] += displacement_vector;
        quad_flattened[1] += displacement_vector;
        quad_flattened[2] += displacement_vector;
        quad_flattened[3] += displacement_vector;

        Vec3 rotation_axis_initial_alignment = (quad_flattened[quad_shared_p2_curr] - quad_flattened[quad_shared_p1_curr]).cross((flattened_nodes.back())[quad_shared_p2_prev] - (flattened_nodes.back())[quad_shared_p1_prev]);
        float angle_between_edges = (quad_flattened[quad_shared_p2_curr] - quad_flattened[quad_shared_p1_curr]).angle((flattened_nodes.back())[quad_shared_p2_prev] - (flattened_nodes.back())[quad_shared_p1_prev]);

        quad_flattened[quad_shared_p1_curr] = quad_flattened[quad_shared_p1_curr];
        quad_flattened[quad_shared_p2_curr] = quad_flattened[quad_shared_p1_curr] + (quad_flattened[quad_shared_p2_curr] - quad_flattened[quad_shared_p1_curr]).rotate(rotation_axis_initial_alignment, -angle_between_edges);
        quad_flattened[quad_nonshared_p1_curr] = quad_flattened[quad_shared_p1_curr] + (quad_flattened[quad_nonshared_p1_curr] - quad_flattened[quad_shared_p1_curr]).rotate(rotation_axis_initial_alignment, -angle_between_edges);
        quad_flattened[quad_nonshared_p2_curr] = quad_flattened[quad_shared_p1_curr] + (quad_flattened[quad_nonshared_p2_curr] - quad_flattened[quad_shared_p1_curr]).rotate(rotation_axis_initial_alignment, -angle_between_edges);

        //Now rotate the other two points so that the quads' normal vectors are aligned.
        //Or rather, simply align everything to the vector (0, 0, 1) since we're ensuring this by making the first quad forcibly be in the canonical 2D base.
        //This is a useful simplification.
        Vec3 quad_normal = (quad_flattened[quad_shared_p2_curr]-quad_flattened[quad_shared_p1_curr]).cross(quad_flattened[quad_nonshared_p1_curr]-quad_flattened[quad_shared_p1_curr]);
        Vec3 rotation_axis_final = (quad_flattened[quad_shared_p2_curr]-quad_flattened[quad_shared_p1_curr]);
        float angle_between_quads = quad_normal.angle(Vec3(0,0,1));
        quad_flattened[quad_nonshared_p1_curr] = quad_flattened[quad_shared_p1_curr] + (quad_flattened[quad_nonshared_p1_curr] - quad_flattened[quad_shared_p1_curr]).rotate(rotation_axis_final, -angle_between_quads);
        quad_flattened[quad_nonshared_p2_curr] = quad_flattened[quad_shared_p1_curr] + (quad_flattened[quad_nonshared_p2_curr] - quad_flattened[quad_shared_p1_curr]).rotate(rotation_axis_final, -angle_between_quads);

		/*
        std::cout << "[flat] ";
        for (int i = 0; i < 4; i++) {
            std::cout << "(" << quad_flattened[i].x() << ", " << quad_flattened[i].y() << ", " << quad_flattened[i].z() << ")-";
        }
        std::cout << ";";
        std::cout << std::endl;
        */

        flattened_nodes.push_back(quad_flattened);

        quad_prev = quad_curr;
        quad_curr = quad_next;

        next_node = getNextSector(next_node);
        quad_next = DriveGraph::get()->getNode(next_node)->getQuadPoints();
    }
    //std::cout << "[flat] END OF COMPUTED SURFACE" << std::endl;

}
 
//-----------------------------------------------------------------------------
/** This is the main entry point for the AI.
 *  It is called once per frame for each AI and determines the behaviour of
 *  the AI, e.g. steering, accelerating/braking, firing.
 */
void TyreModAI::update(int ticks)
{
    //feenableexcept(FE_INVALID | FE_OVERFLOW);
    float dt = stk_config->ticks2Time(ticks);
    computeRacingLine(m_track_node, 100);

    m_controls->setLookBack(false);
    m_controls->setSteer(0.0f);
    m_controls->setAccel(1.0f);
    m_controls->setSkidControl(KartControl::SC_NONE); // SC_NONE, SC_LEFT, SC_RIGHT
    m_controls->setNitro(false);
    m_controls->setBrake(false);
    m_controls->setRescue(false);

    //fedisableexcept(FE_INVALID | FE_OVERFLOW);
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
