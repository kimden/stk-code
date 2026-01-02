//  SuperTuxKart - a fun racing game with go-kart
//
//  Copyright (C) 2004-2015 Steve Baker <sjbaker1@airmail.net>
//  Copyright (C) 2006-2015 SuperTuxKart-Team
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

#include "items/item.hpp"

#include "SColor.h"
#include "graphics/irr_driver.hpp"
#include "graphics/lod_node.hpp"
#include "graphics/sp/sp_mesh.hpp"
#include "graphics/sp/sp_mesh_node.hpp"
#include "graphics/stk_text_billboard.hpp"
#include "guiengine/engine.hpp"
#include "items/item_manager.hpp"
#include "karts/kart.hpp"
#include "modes/world.hpp"
#include "network/network_config.hpp"
#include "network/network_string.hpp"
#include "network/rewind_manager.hpp"
#include "tracks/track.hpp"
#include "tracks/track_manager.hpp"
#include "tracks/arena_graph.hpp"
#include "tracks/drive_graph.hpp"
#include "tracks/drive_node.hpp"
#include "utils/constants.hpp"
#include "utils/string_utils.hpp"
#include "utils/tyre_utils.hpp"
#include "utils/kart_tags.hpp"
#include "font/bold_face.hpp"
#include "font/font_manager.hpp"
#include "items/powerup_manager.hpp"

#include <IBillboardSceneNode.h>
#include <IMeshSceneNode.h>
#include <ISceneManager.h>

const float ICON_SIZE = 0.7f;
const int SPARK_AMOUNT = 10;
const float SPARK_SIZE = 0.4f;
const float SPARK_SPEED_H = 1.0f;

// ----------------------------------------------------------------------------
/** Constructor.
 *  \param type Type of the item.
 *  \param owner If not NULL it is the kart that dropped this item; NULL
 *         indicates an item that's part of the track.
 *  \param id Index of this item in the array of all items.
 */
ItemState::ItemState(ItemType type, const Kart *owner, int id)
{
    setType(type);
    m_item_id = id;
    m_previous_owner = owner;
    m_compound = 0;
    m_stop_time = 0;
    m_attached = NULL;
    m_used_up_counter = -1;
    //printf("Constructor for item %u\n", id);
    if (owner)
        setDeactivatedTicks(STKConfig::get()->time2Ticks(1.5f));
    else
        setDeactivatedTicks(0);
}   // ItemState(ItemType)

//-----------------------------------------------------------------------------
/** Constructor to restore item state at current ticks in client for live join
 */
ItemState::ItemState(const BareNetworkString& buffer)
{
    m_type = (ItemType)buffer.getUInt8();
    m_original_type = (ItemType)buffer.getUInt8();
    m_ticks_till_return = buffer.getUInt32();
    m_item_id = buffer.getUInt32();
    m_deactive_ticks = buffer.getUInt32();
    m_used_up_counter = buffer.getUInt32();
    m_xyz_init = buffer.getVec3();
    m_xyz = buffer.getVec3();
    m_original_rotation = buffer.getQuat();
    m_previous_owner = NULL;
    int8_t kart_id = buffer.getUInt8();
    if (kart_id != -1)
        m_previous_owner = World::getWorld()->getKart(kart_id);
    m_compound = buffer.getUInt8();
    m_stop_time = buffer.getUInt8();

    std::string attached = "";
    buffer.decodeString(&attached);

    auto object_manager =  TrackManager::get()->getTrack(RaceManager::get()->getTrackName())->getTrackObjectManager();
    m_attached = NULL;

    std::vector<std::string> attachedl = StringUtils::split(attached, ':');
    if (attachedl.size() == 0) {
      ;
    } else if (attachedl.size() == 1) {
        m_attached = object_manager->getTrackObject("", attachedl[0]);
    } else if (attachedl.size() == 3) { // "a::b" split by ':' ===> "a","","b"
        m_attached = object_manager->getTrackObject(attachedl[0], attachedl[2]);
    }

    //printf("Restored for %u: powerup %u\n", m_item_id, m_compound);
}   // ItemState(const BareNetworkString& buffer)

// ------------------------------------------------------------------------
/** Sets the disappear counter depending on type.  */
void ItemState::setDisappearCounter()
{
    switch (m_type)
    {
    // The Nolok variants are stored as standard variants with special graphics,
    // so this also applies
    case ITEM_BUBBLEGUM:
    case ITEM_BUBBLEGUM_SMALL:
        m_used_up_counter = STKConfig::get()->m_bubblegum_counter; break;
    case ITEM_EASTER_EGG:
        m_used_up_counter = -1; break;
    default:
        m_used_up_counter = -1;
    }   // switch
}   // setDisappearCounter

void ItemState::reset() {
    m_deactive_ticks    = 0;
    m_ticks_till_return = 0;
    setDisappearCounter();
    // If the item was switched:
    if (m_original_type != ITEM_NONE)
    {
        setType(m_original_type);
        m_original_type = ITEM_NONE;
    }

	bool do_preview = RaceManager::get()->getTyreModRules()->do_item_preview;
    if (do_preview && (m_type == ITEM_BONUS_BOX || m_type == ITEM_BANANA)) // bananas also get an initial powerup in case they get switched
        respawnBonusBox(getItemId());
}   // reset


// -----------------------------------------------------------------------
/** Initialises an item.
 *  \param type Type for this item.
 *  \param xyz The position for this item.
 *  \param normal The normal for this item.
 *  \param compound First payload for this item, for tyre changers it's the compound and for bonus boxes it's the powerup type
 *  \param stop_time Second payload for this item, for tyre changers it's the stop time and for bonus boxes it's the powerup cound
 *  \param attached The parameter
 */
void ItemState::initItem(ItemType type, const Vec3& xyz, const Vec3& normal,
                         int compound, int stop_time, const std::string &attached)
{
    auto& stk_config = STKConfig::get();
    World *world = World::getWorld();

    m_xyz_init          = xyz;
    m_xyz               = m_xyz_init;
    std::vector<std::string> attachedl = StringUtils::split(attached, ':');
    auto object_manager =  TrackManager::get()->getTrack(RaceManager::get()->getTrackName())->getTrackObjectManager();
    m_attached = NULL;

    if (attachedl.size() == 0) {
      ;
    } else if (attachedl.size() == 1) {
        m_attached = object_manager->getTrackObject("", attachedl[0]);
    } else if (attachedl.size() == 3) { // "a::b" split by ':' ===> "a","","b"
        m_attached = object_manager->getTrackObject(attachedl[0], attachedl[2]);
    }
    if (m_attached) {
        ;
    } else if (attached != ""){
        Log::warn("Item", "No attachment but it is specified as %s\n", attached.c_str());
    }
    m_original_rotation = shortestArcQuat(Vec3(0, 1, 0), normal);
    m_original_type     = ITEM_NONE;
    m_ticks_till_return = 0;
    m_compound = compound;
    m_stop_time = stop_time;
    //printf("Item init for %u: powerup %u\n", m_item_id, m_compound);
    setDisappearCounter();
}   // initItem

static int getRespawnTicks(ItemState::ItemType type) {
    auto& stk_config = STKConfig::get();
    switch (type)
    {
        case ItemState::ITEM_BONUS_BOX:
            return stk_config->m_bonusbox_item_return_ticks;
            break;
        case ItemState::ITEM_NITRO_BIG:
        case ItemState::ITEM_NITRO_SMALL:
            return stk_config->m_nitro_item_return_ticks;
            break;
        case ItemState::ITEM_BANANA:
            return stk_config->m_banana_item_return_ticks;
            break;
        case ItemState::ITEM_BUBBLEGUM:
        case ItemState::ITEM_BUBBLEGUM_NOLOK:
        case ItemState::ITEM_BUBBLEGUM_SMALL:
        case ItemState::ITEM_BUBBLEGUM_SMALL_NOLOK:
            return stk_config->m_bubblegum_item_return_ticks;
            break;
        case ItemState::ITEM_TYRE_CHANGE:
            return stk_config->m_tyre_change_item_return_ticks;
            break;
        default:
            return stk_config->time2Ticks(2.0f);
            break;
    }
}

// ----------------------------------------------------------------------------
/** Update the state of the item, called once per physics frame.
 *  \param ticks Number of ticks to simulate. While this value is 1 when
 *         called during the normal game loop, during a rewind this value
 *         can be (much) larger than 1.
 */
void ItemState::update(int ticks)
{
    auto& stk_config = STKConfig::get();
    World *world = World::getWorld();

    if (m_deactive_ticks > 0) m_deactive_ticks -= ticks;
    if (m_ticks_till_return>0)
    {
        m_ticks_till_return -= ticks;
    }   // if collected

    if (m_attached) {
        m_xyz = m_xyz_init;
        ThreeDAnimation *anim = m_attached->getAnimator();
        if (anim) {
            float curr_time = stk_config->ticks2Time(world->getTicksSinceStart());

            // AnimationBase::getAt() 's implementation doesn't actually update position properly.
            // Instead it seemingly applies the position change that would apply to the center.
            // The consequence is we need to calculate the real position of other points ourselves 
            // There is another optional scale parameter to getAt(), but we'll
            // not concern ourselves with dynamic scaling for now

            // We take the initial orientation of the animation to be 0, since it's relative this works fine
            Vec3 hpr_initial(0, 0, 0);
            Vec3 hpr_curr(0, 0, 0);
            Vec3 anim_initial_pos = Vec3(m_attached->getInitXYZ());

            Vec3 obj_pos = Vec3(m_attached->getPosition());

            Vec3 anim_curr_pos;
            anim->getAt(curr_time, &anim_curr_pos, &hpr_curr);
            anim_curr_pos += obj_pos;

            Vec3 hpr = hpr_curr - hpr_initial;
            Vec3 diff = anim_curr_pos - anim_initial_pos;
            Vec3 diff_base = m_xyz_init - anim_initial_pos;

            diff_base = diff_base.rotate(Vec3(1, 0, 0), DEGREE_TO_RAD*hpr.x());
            diff_base = diff_base.rotate(Vec3(0, 1, 0), DEGREE_TO_RAD*hpr.y());
            diff_base = diff_base.rotate(Vec3(0, 0, 1), DEGREE_TO_RAD*hpr.z());
            if (!NetworkConfig::get()->isServer()) {
                // printf("ATTACHMENT %s\nObject without anim: %f %f %f\n", m_attached->getID().c_str(), obj_pos.x(), obj_pos.y(), obj_pos.z());
                // printf("OBJECT POSITIONS: %f %f %f / %f %f %f\n", anim_initial_pos.x(), anim_initial_pos.y(), anim_initial_pos.z(), anim_curr_pos.x(), anim_curr_pos.y(), anim_curr_pos.z());
                // printf("Z: %f DIFFS: %f %f %f / %f %f %f\n\n", m_xyz_init.z(), diff_base.x(), diff_base.y(), diff_base.z(), diff.x(), diff.y(), diff.z());
            }
            m_xyz = anim_initial_pos + diff_base + diff;
        }
    }

    ItemPolicy *policy = RaceManager::get()->getItemPolicy();
    m_ticks_till_return = policy->computeItemTicksTillReturn(m_original_type, m_type, getRespawnTicks(m_type), m_ticks_till_return, m_compound);
}   // update

// ----------------------------------------------------------------------------
/** Called when the item is collected.
 *  \param kart The kart that collected the item.
 */
void ItemState::collected(const Kart *kart)
{
    auto& stk_config = STKConfig::get();

    if (kart->getBody()->getTag() != KART_TAG)
        return;

    if (m_type == ITEM_EASTER_EGG)
    {
        // They will disappear 'forever'
        m_ticks_till_return = stk_config->time2Ticks(99999);
    }
    else if (m_used_up_counter > 0)
    {
        m_used_up_counter--;
        // Deactivates the item for a certain amount of time. It is used to
        // prevent bubble gum from hitting a kart over and over again (in each
        // frame) by giving it time to drive away.
        m_deactive_ticks = stk_config->time2Ticks(0.5f);
        // Set the time till reappear to -1 seconds --> the item will
        // reappear immediately.
        m_ticks_till_return = -1;
    }
    else
    {
        m_ticks_till_return = getRespawnTicks(m_type);
    }

    if (RaceManager::get()->isBattleMode())
    {
        m_ticks_till_return *= 3;
    }
}   // collected

// ----------------------------------------------------------------------------
/** Returns the graphical type of this item should be using (takes nolok into
 *  account). */
Item::ItemType ItemState::getGraphicalType() const
{
    bool nolok_owned = (m_previous_owner && m_previous_owner->getIdent() == "nolok");
    return nolok_owned && getType() == ITEM_BUBBLEGUM       ? ITEM_BUBBLEGUM_NOLOK       :
           nolok_owned && getType() == ITEM_BUBBLEGUM_SMALL ? ITEM_BUBBLEGUM_SMALL_NOLOK :
                                                              getType();
}   // getGraphicalType

//-----------------------------------------------------------------------------
/** Save item state at current ticks in server for live join
 */
void ItemState::saveCompleteState(BareNetworkString* buffer) const
{
    //TODO: ATTACH FIX: Currently just getting the attached ID, meaning parent libraries aren't
    // supported here for now (and hence not at all)
    // the stupid part is it requires a recursive function to build the full path, which is easy but annoying
    std::string object_name = "";
    if (m_attached) object_name = m_attached->getID();

    buffer->addUInt8((uint8_t)m_type).addUInt8((uint8_t)m_original_type)
        .addUInt32(m_ticks_till_return).addUInt32(m_item_id)
        .addUInt32(m_deactive_ticks).addUInt32(m_used_up_counter)
        .add(m_xyz_init).add(m_xyz).add(m_original_rotation)
        .addUInt8(m_previous_owner ? (int8_t)m_previous_owner->getWorldKartId() : (int8_t)-1)
        .addUInt8(m_compound).addUInt8(m_stop_time)
        .encodeString(object_name);
}   // saveCompleteState

// ============================================================================
/** Constructor for an item.
 *  \param type Type of the item.
 *  \param xyz Location of the item.
 *  \param normal The normal upon which the item is placed (so that it can
 *         be aligned properly with the ground).
 *  \param mesh The mesh to be used for this item.
 *  \param owner 'Owner' of this item, i.e. the kart that drops it. This is
 *         used to deactivate this item for the owner, i.e. avoid that a kart
 *         'collects' its own bubble gum. NULL means no owner, and the item
 *         can be collected immediatley by any kart.
 */
Item::Item(ItemType type, const Vec3& xyz, const Vec3& normal,
           scene::IMesh* mesh, scene::IMesh* lowres_mesh,
           const std::string& icon, const Kart *owner, int compound, int stop_time,
           const std::string &attached)
    : ItemState(type, owner)
{
    m_icon_node = NULL;
    m_powerup_node = NULL;
    m_was_available_previously = true;
    // Prevent appear animation at start
    m_animation_start_ticks = -9999;
    m_distance_2        = ItemManager::getCollectDistanceSquared(type);
    m_grace_percent     = ItemManager::getGracePercentage(type);
    initItem(type, xyz, normal, compound, stop_time, attached);
    m_graphical_type    = getGraphicalType();

    m_node = NULL;
    if (mesh && !GUIEngine::isNoGraphics())
    {
        LODNode* lodnode =
            new LODNode("item", irr_driver->getSceneManager()->getRootSceneNode(),
            irr_driver->getSceneManager());
        scene::ISceneNode* meshnode =
            irr_driver->addMesh(mesh, StringUtils::insertValues("item_%i", (int)type));

        lodnode->add(1, meshnode, true);
        if (lowres_mesh != NULL)
        {
            scene::ISceneNode* meshnode =
                irr_driver->addMesh(lowres_mesh,
                StringUtils::insertValues("item_lo_%i", (int)type));
            lodnode->add(2, meshnode, true);
        }

        // Auto-compute the rendering distance, but use a high scaling factor
        // to ensure that even at low settings, on-track items only become invisible
        // when already quite far.
        lodnode->autoComputeLevel(24); // The distance grows with the square root of the scaling factor
        m_node = lodnode;
        m_appear_anime_node = irr_driver->getSceneManager()->addEmptySceneNode(m_node);
    }
    setType(type);
    handleNewMesh(getGraphicalType());

    if (!m_node)
        return;
#ifdef DEBUG
    std::string debug_name("item: ");
    debug_name += getType();
    m_node->setName(debug_name.c_str());
#endif
    m_node->setAutomaticCulling(scene::EAC_FRUSTUM_BOX);
    m_node->setPosition(xyz.toIrrVector());
    Vec3 hpr;
    hpr.setHPR(getOriginalRotation());
    m_node->setRotation(hpr.toIrrHPR());
    m_node->grab();

    for (int n = 0; n < SPARK_AMOUNT; n++)
    {
        scene::ISceneNode* billboard =
            irr_driver->addBillboard(core::dimension2df(SPARK_SIZE, SPARK_SIZE),
                                     "item_spark.png", m_appear_anime_node);
#ifdef DEBUG
        billboard->setName("spark");
#endif

        billboard->setVisible(true);

        m_spark_nodes.push_back(billboard);
    }
}   // Item(type, xyz, normal, mesh, lowres_mesh)

//-----------------------------------------------------------------------------
/** Initialises the item. Note that m_distance_2 must be defined before calling
 *  this function, since it pre-computes some values based on this.
 *  \param type Type of the item.
 *  \param xyz Position of this item.
 *  \param normal Normal for this item.
 */
void Item::initItem(ItemType type, const Vec3 &xyz, const Vec3&normal,
                    int compound, int stop_time, const std::string &attached)
{
    ItemState::initItem(type, xyz, normal, compound, stop_time, attached);
    // Now determine in which quad this item is, and its distance
    // from the center within this quad.
    m_graph_node = Graph::UNKNOWN_SECTOR;
    m_distance_from_center = 9999.9f;
    m_avoidance_points[0] = NULL;
    m_avoidance_points[1] = NULL;

    // Check that Graph exist (it might not in battle mode without navmesh)
    if (Graph::get())
    {
        Graph::get()->findRoadSector(xyz, &m_graph_node);
    }
    if (DriveGraph::get() && m_graph_node != Graph::UNKNOWN_SECTOR)
    {
        // Item is on drive graph. Pre-compute the distance from center
        // of this item, which is used by the AI (mostly for avoiding items)
        Vec3 distances;
        DriveGraph::get()->spatialToTrack(&distances, getXYZ(), m_graph_node);
        m_distance_from_center = distances.getX();
        const DriveNode* dn = DriveGraph::get()->getNode(m_graph_node);
        const Vec3& right = dn->getRightUnitVector();
        // Give it 10% more space, since the kart will not always come
        // parallel to the drive line.
        Vec3 delta = right * sqrt(m_distance_2) * 1.3f;
        m_avoidance_points[0] = new Vec3(getXYZ() + delta);
        m_avoidance_points[1] = new Vec3(getXYZ() - delta);
    }

}   // initItem

//-----------------------------------------------------------------------------
void Item::setMesh(scene::IMesh* mesh, scene::IMesh* lowres_mesh)
{
#ifndef SERVER_ONLY
    if (m_node == NULL)
        return;

    unsigned i = 0;
    for (auto* node : m_node->getAllNodes())
    {
        scene::IMesh* m = i == 0 ? mesh : lowres_mesh;
        if (m == NULL)
        {
            continue;
        }
        SP::SPMeshNode* spmn = dynamic_cast<SP::SPMeshNode*>(node);
        if (spmn)
        {
            spmn->setMesh(static_cast<SP::SPMesh*>(m));
        }
        else
        {
            ((scene::IMeshSceneNode*)node)->setMesh(m);
        }
        i++;
    }
#endif
}   // setMesh

//-----------------------------------------------------------------------------
/** Removes an item.
 */
Item::~Item()
{
    if (m_node != NULL)
    {
        for (auto* node : m_spark_nodes)
            m_appear_anime_node->removeChild(node);
        if (m_icon_node)
            m_appear_anime_node->removeChild(m_icon_node);
        
        m_node->removeChild(m_appear_anime_node);
        m_node->removeChild(m_powerup_node);

        irr_driver->removeNode(m_node);
        m_node->drop();
    }
    if(m_avoidance_points[0])
        delete m_avoidance_points[0];
    if(m_avoidance_points[1])
        delete m_avoidance_points[1];
}   // ~Item

//-----------------------------------------------------------------------------
/** Resets before a race (esp. if a race is restarted).
 */
void Item::reset()
{
    m_was_available_previously = true;
    m_animation_start_ticks = -9999;
    ItemState::reset();


	bool do_preview = RaceManager::get()->getTyreModRules()->do_item_preview;
    if (do_preview && !GUIEngine::isNoGraphics() && getType() == ITEM_BONUS_BOX) {
        if (m_powerup_node) {
            m_graphical_powerup = -1;
            m_node->removeChild(m_powerup_node);
        }
        m_powerup_node = NULL;
        //printf("Graphical Init from reset with c%d\n", m_compound);
        auto powerup_icon = powerup_manager->getIcon(m_compound);
        if (powerup_icon)
        {
            m_graphical_powerup = m_compound;
            m_powerup_node = irr_driver->addBillboard(core::dimension2df(1.0f, 1.0f),
                                            powerup_icon, m_node);

            m_powerup_node->setPosition(core::vector3df(0.0f, 1.5f, 0.0f));
            m_powerup_node->setVisible(true);
        } else {
            m_graphical_powerup = (int)PowerupManager::PowerupType::POWERUP_NOTHING;
        }
    }

    if (m_node != NULL)
    {
        m_node->setScale(core::vector3df(1,1,1));
        m_node->setVisible(true);
    }

}   // reset

// ----------------------------------------------------------------------------
void Item::handleNewMesh(ItemType type)
{
#ifndef SERVER_ONLY
    if (m_node == NULL)
        return;
    setMesh(ItemManager::getItemModel(type),
        ItemManager::getItemLowResolutionModel(type));
    for (auto* node : m_node->getAllNodes())
    {
        SP::SPMeshNode* spmn = dynamic_cast<SP::SPMeshNode*>(node);
        if (spmn)
            spmn->setGlowColor(ItemManager::getGlowColor(type));
    }
    Vec3 hpr;
    hpr.setHPR(getOriginalRotation());
    m_node->setRotation(hpr.toIrrHPR());

    if (m_icon_node)
        m_appear_anime_node->removeChild(m_icon_node);
    m_icon_node = NULL;
    auto icon = ItemManager::getIcon(type);

    if (!icon.empty())
    {
        m_icon_node = irr_driver->addBillboard(core::dimension2df(1.0f, 1.0f),
                                        icon, m_appear_anime_node);

        m_icon_node->setPosition(core::vector3df(0.0f, 0.5f, 0.0f));
        m_icon_node->setVisible(false);
        ((scene::IBillboardSceneNode*)m_icon_node)
            ->setColor(ItemManager::getGlowColor(type).toSColor());
    }

    if (GUIEngine::isNoGraphics())
        return;

    BoldFace* bold_face = font_manager->getFont<BoldFace>();
    if (type == ItemType::ITEM_TYRE_CHANGE) {
        m_tb =
            new STKTextBillboard(
            GUIEngine::getSkin()->getColor("font::bottom"),
            GUIEngine::getSkin()->getColor("font::top"),
            m_node, irr_driver->getSceneManager(), -1,
            core::vector3df(0.0f, 2.0f, 0.0f),
            core::vector3df(0.5f, 0.5f, 0.5f));
        m_tb->init(StringUtils::utf8ToWide(TyreUtils::getStringFromCompound(m_compound, false)), bold_face);
    } else {

    }

#endif
}   // handleNewMesh

// ------------------------------------------------------------------------
static int simplePRNG(const unsigned seed, const unsigned time, const unsigned item_id, const unsigned position, Vec3 pos)
{
    const unsigned c = 12345*(1+2*time); // This is always an odd number

    const unsigned a = 1103515245;
    int rand = a*(seed + c + item_id)+(unsigned)(pos.x()*2.0+1)*(unsigned)(pos.y()*2.0+2)*(unsigned)(pos.z()*2.0+3);

    return rand;
} // simplePRNG
// ------------------------------------------------------------------------
void ItemState::respawnBonusBox(unsigned itemid)
{

    unsigned int n=1;
    PowerupManager::PowerupType new_powerup;
    World *world = World::getWorld();

    // Determine a 'random' number based on time, index of the item,
    // and position of the kart ([TME: position fixed to 1]). The idea is that this process is
    // randomly enough to get the right distribution of the powerups,
    // does not involve additional network communication to keep 
    // client and server in sync, and is not exploitable.
    const int time = world->getTicksSinceStart() / 60;
    int random_number = 0;

    // Random_number is in the range 0-32767 
    random_number = simplePRNG(powerup_manager->getRandomSeed(), time, itemid, 1, m_xyz);

    new_powerup = powerup_manager->getRandomPowerup(1, &n, random_number);

    auto& stk_config = STKConfig::get();

    // Do not spawn big boosts at start
    if (time == 0 &&
        (new_powerup == PowerupManager::PowerupType::POWERUP_ZIPPER ||
        new_powerup == PowerupManager::PowerupType::POWERUP_SUDO ||
        new_powerup == PowerupManager::PowerupType::POWERUP_ELECTRO))
    {
        new_powerup = PowerupManager::PowerupType::POWERUP_PLUNGER;
        n = 1;
    }

    // Do not spawn switches at start
    if (time == 0 && new_powerup == PowerupManager::PowerupType::POWERUP_SWITCH) {
        new_powerup = PowerupManager::PowerupType::POWERUP_BOWLING;
        n = 1;
    }


    m_compound = new_powerup;
    m_stop_time = n;

    if (NetworkConfig::get()->isServer()) {
        //printf("Server respawn for %u: %d %d, result: powerup %u\n", itemid, time, powerup_manager->getRandomSeed(), m_compound);
    }

    if (!NetworkConfig::get()->isServer()) {
        //printf("Client respawn for %u: %d %d, result: powerup %u\n", itemid, time, powerup_manager->getRandomSeed(), m_compound);
    }

    // TODO: [TME] remove or rework other collection modes
}

// ----------------------------------------------------------------------------
/** Updated the item - rotates it, takes care of items coming back into
 *  the game after it has been collected.
 *  \param ticks Number of physics time steps - should be 1.
 */
void Item::updateGraphics(float dt)
{
    if (m_node == NULL)
        return;

    if (m_graphical_type != getGraphicalType())
    {
        handleNewMesh(getGraphicalType());
        m_graphical_type = getGraphicalType();
    }

    auto& stk_config = STKConfig::get();

    float time_till_return = stk_config->ticks2Time(getTicksTillReturn());
    bool is_visible = isAvailable() || time_till_return <= 1.0f ||
                      (isBubblegum() &&
                       getOriginalType() == ITEM_NONE && !isUsedUp());

    m_node->setVisible(is_visible);
    m_node->setPosition(getXYZ().toIrrVector());


	bool do_preview = RaceManager::get()->getTyreModRules()->do_item_preview;

    if (do_preview && getType() == ITEM_BONUS_BOX && isAvailable()) {
        if (m_powerup_node && m_compound == m_graphical_type)
            m_powerup_node->setVisible(true);
        else { // If the powerup for item preview doesn't exist or is mismatched, redraw
            if (m_powerup_node) {
                m_graphical_powerup = (int)PowerupManager::PowerupType::POWERUP_NOTHING;
                m_node->removeChild(m_powerup_node);
            }

            auto powerup_icon = powerup_manager->getIcon((PowerupManager::PowerupType)m_compound);
            if (powerup_icon) {
                m_graphical_powerup = m_compound;
                m_powerup_node = irr_driver->addBillboard(core::dimension2df(1.0f, 1.0f),
                                                powerup_icon, m_node);
                m_powerup_node->setPosition(core::vector3df(0.0f, 1.5f, 0.0f));
                m_powerup_node->setVisible(true);
            } else {
                m_graphical_powerup = (int)PowerupManager::PowerupType::POWERUP_NOTHING;
            }
       }
    } else {
        if (m_powerup_node)
            m_powerup_node->setVisible(false);
    }

    if (time_till_return > 0.1f) {
        if (getType() == ITEM_BONUS_BOX) {
            if (m_powerup_node)
                m_powerup_node->setVisible(false);
        }
    }

    if (!m_was_available_previously && isAvailable())
    {
        // Play animation when item respawns
        m_animation_start_ticks = World::getWorld()->getTicksSinceStart();
        m_node->setScale(core::vector3df(0.0f, 0.0f, 0.0f));

        if (do_preview && getType() == ITEM_BONUS_BOX) {
            if (m_powerup_node) {
                m_graphical_powerup = (int)PowerupManager::PowerupType::POWERUP_NOTHING;
                m_node->removeChild(m_powerup_node);
            }
            m_powerup_node = NULL;

            //printf("Graphical Init from updateGraphics with c%d\n", m_compound);
            auto powerup_icon = powerup_manager->getIcon((PowerupManager::PowerupType)m_compound);
            if (powerup_icon)
            {
                m_graphical_powerup = m_compound;
                m_powerup_node = irr_driver->addBillboard(core::dimension2df(1.0f, 1.0f),
                                                powerup_icon, m_node);
                m_powerup_node->setPosition(core::vector3df(0.0f, 1.5f, 0.0f));
                m_powerup_node->setVisible(true);
            } else {
                m_graphical_powerup = (int)PowerupManager::PowerupType::POWERUP_NOTHING;
            }
        }
    }

    float time_since_return = stk_config->ticks2Time(
        World::getWorld()->getTicksSinceStart() - m_animation_start_ticks);

    // Scale width and length
    float scale_factor = (getType() == ITEM_BUBBLEGUM_SMALL ? 0.75f : 1.0f);

    if (is_visible)
    {
        if (!isAvailable() && !(isBubblegum() &&
                getOriginalType() == ITEM_NONE && !isUsedUp()))
        {
            // Keep it visible so particles work, but hide the model
            m_node->setScale(core::vector3df(0.0f, 1.0f, 0.0f));
        }
        else if (time_since_return <= 1.0f && m_animation_start_ticks)
        {
            float p = time_since_return, f = (1.0f - time_since_return);
            float factor_v = sin(-13.0f * M_PI_2 * (p + 1.0f))
                            * pow(2.0f, -10.0f * p) + 1.0f;
            float factor_h = 1.0f - (f * f * f * f * f - f * f * f * sin(f * M_PI));

            m_node->setScale(core::vector3df(factor_h * scale_factor, factor_v, factor_h * scale_factor));
        }
        else
        {
            m_node->setScale(core::vector3df(scale_factor, 1.0f, scale_factor));
        }

        // Handle rotation of the item
        Vec3 hpr;
        if (rotating())
        {
            // have it rotate
            float angle =
                fmodf((float)World::getWorld()->getTicksSinceStart() / (float)stk_config->time2Ticks(0.33334f),
                M_PI * 2);

            btMatrix3x3 m;
            m.setRotation(getOriginalRotation());
            btQuaternion r = btQuaternion(m.getColumn(1), angle) *
                getOriginalRotation();
            hpr.setHPR(r);
        }
        else
            hpr.setHPR(getOriginalRotation());
        m_node->setRotation(hpr.toIrrHPR());
    } // if item is available

    bool is_in_appear_anime = time_since_return < 1.0f
                             || (!isAvailable() && time_till_return < 1.0f);
    m_appear_anime_node->setVisible(is_in_appear_anime);

    if (is_in_appear_anime)
    {
        for (size_t i = 0; i < SPARK_AMOUNT; i++)
        {
            float t = time_since_return + 0.5f;
            float t2 = time_since_return + 1.0f;

            float node_angle = !rotating() ? 0.0f :
                    fmodf((float)World::getWorld()->getTicksSinceStart() / 40.0f,
                    M_PI * 2);

            float x = sin(float(i) / float(SPARK_AMOUNT) * 2.0f * M_PI - node_angle)
                        * t * SPARK_SPEED_H;
            float y = 2.0f * t2 - t2 * t2 - 0.5f;
            float z = cos(float(i) / float(SPARK_AMOUNT) * 2.0f * M_PI - node_angle)
                        * t * SPARK_SPEED_H;

            m_spark_nodes[i]->setPosition(core::vector3df(x, y, z));

            float factor = std::max(0.0f, 1.0f - t / 2.0f);

            m_spark_nodes[i]->setVisible(true);

            ((scene::IBillboardSceneNode*)m_spark_nodes[i])
                    ->setSize(core::dimension2df(factor * SPARK_SIZE,
                                                factor * SPARK_SIZE));
        }

        if (m_icon_node)
        {
            if (!isAvailable())
            {
                m_icon_node->setVisible(true);
                float size = 1.0f / (pow(6.0f, -time_till_return - 0.2f) *
                            (-time_till_return - 0.2f)) + 7.0f;

                ((scene::IBillboardSceneNode*)m_icon_node)
                        ->setSize(core::dimension2df(size * ICON_SIZE,
                                                    size * ICON_SIZE));
            }
            else
            {
                m_icon_node->setVisible(false);
            }
        }
    }

    m_was_available_previously = isAvailable();
}   // updateGraphics

// ------------------------------------------------------------------------
/** Returns true if the Kart is close enough to hit this item, the item is
 *  not deactivated anymore, and it wasn't placed by this kart (this is
 *  e.g. used to avoid that a kart hits a bubble gum it just dropped).
 *  \param kart Kart to test.
 *  \param xyz Location of kart (avoiding to use kart->getXYZ() so that
 *         kart.hpp does not need to be included here).
 */
bool Item::hitKart(const Vec3 &xyz, const Kart *kart) const
{
    if (getPreviousOwner() == kart && getDeactivatedTicks() > 0)
        return false;

    // Only catch bubblegums when driving on the ground
    if (isBubblegum() && !kart->isOnGround())
        return false;

    // Set the coordinates in the kart's frame of reference
    // in order to properly account for kart's width and length
    Vec3 lc = quatRotate(kart->getVisualRotation().inverse(), xyz - getXYZ());

    // Since we only care about the length of the vector,
    // we can flip the sign of its components without issue
    if (lc.getX() < 0.0f)
        lc.setX(-lc.getX());
    if (lc.getZ() < 0.0f)
        lc.setZ(-lc.getZ());

    // Substract half the kart width, multiplied by the grace percentage, the from the sideways component
    lc.setX(lc.getX() - kart->getKartWidth() * m_grace_percent * 0.5f);
    if (lc.getX() < 0.0f)
        lc.setX(0.0f);
    // Substract half the kart height, multiplied by the grace percentage, the from the forward component
    lc.setZ(lc.getZ() - kart->getKartLength()* m_grace_percent * 0.5f);
    if (lc.getZ() < 0.0f)
        lc.setZ(0.0f);

    // Don't be too strict if the kart is a bit above the item
    // TODO : have a per-item height value
    if (lc.getY() < 0.0f)
        lc.setY(-lc.getY());
    lc.setY(lc.getY() - 1.0f);
    if (lc.getY() < 0.0f)
        lc.setY(0.0f);

    return lc.length2() < m_distance_2;
}   // hitKart

