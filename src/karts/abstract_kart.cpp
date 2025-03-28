//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2012-2015  Joerg Henrichs
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


#include "karts/abstract_kart.hpp"

#include "config/user_config.hpp"
#include "items/attachment.hpp"
#include "items/powerup.hpp"
#include "karts/abstract_kart_animation.hpp"
#include "karts/kart_model.hpp"
#include "karts/kart_properties.hpp"
#include "karts/kart_properties_manager.hpp"
#include "karts/official_karts.hpp"
#include "network/network_config.hpp"
#include "utils/hit_processor.hpp"
#include "physics/physics.hpp"
#include "utils/log.hpp"
#include "utils/string_utils.hpp"

/** Creates a kart.
 *  \param ident The identifier of the kart.
 *  \param world_kart_id  The world index of this kart.
 *  \param position The start position of the kart (1<=position<=n).
 *  \param init_transform The start position of the kart.
 */
AbstractKart::AbstractKart(const std::string& ident,
                           int world_kart_id, int position,
                           const btTransform& init_transform,
                           HandicapLevel handicap,
                           std::shared_ptr<GE::GERenderInfo> ri)
             : Moveable()
{
    m_world_kart_id   = world_kart_id;
    if (RaceManager::get()->getKartGlobalPlayerId(m_world_kart_id) > -1)
    {
        const RemoteKartInfo& rki = RaceManager::get()->getKartInfo(
            m_world_kart_id);
        loadKartProperties(ident, handicap, ri, rki.getKartData());
    }
    else
        loadKartProperties(ident, handicap, ri);
}   // AbstractKart

// ----------------------------------------------------------------------------
AbstractKart::~AbstractKart()
{
    if (m_kart_animation)
    {
        m_kart_animation->handleResetRace();
        delete m_kart_animation;
    }
}   // ~AbstractKart

// ----------------------------------------------------------------------------
void AbstractKart::reset()
{
    m_live_join_util = 0;
    // important to delete animations before calling reset, as some animations
    // set the kart velocity in their destructor (e.g. cannon) which "reset"
    // can then cancel. See #2738
    if (m_kart_animation)
    {
        m_kart_animation->handleResetRace();
        delete m_kart_animation;
        m_kart_animation = NULL;
    }
    Moveable::reset();
}   // reset

// ----------------------------------------------------------------------------
void AbstractKart::loadKartProperties(const std::string& new_ident,
                                      HandicapLevel handicap,
                                      std::shared_ptr<GE::GERenderInfo> ri,
                                      const KartData& kart_data)
{
    m_kart_properties.reset(new KartProperties());
    KartProperties* tmp_kp = NULL;
    const KartProperties* kp = kart_properties_manager->getKart(new_ident);
    const KartProperties* kp_addon = NULL;
    bool new_hitbox = false;
    Vec3 gravity_shift;
    if (NetworkConfig::get()->isNetworking() &&
        NetworkConfig::get()->useTuxHitboxAddon() && kp && kp->isAddon())
    {
        // For addon kart in network we use the same hitbox (tux) so anyone
        // can use any addon karts with different graphical kart model
        if (!UserConfigParams::m_addon_tux_online)
            kp_addon = kp;
        kp = kart_properties_manager->getKart(std::string("tux"));
    }
    if (kp == NULL)
    {
        bool official_kart = !StringUtils::startsWith(new_ident, "addon_");
        if (!NetworkConfig::get()->isNetworking() ||
            (!NetworkConfig::get()->useTuxHitboxAddon() && !official_kart))
        {
            Log::warn("Abstract_Kart", "Unknown kart %s, fallback to tux",
                new_ident.c_str());
        }
        kp = kart_properties_manager->getKart(std::string("tux"));
        if (NetworkConfig::get()->isNetworking() && official_kart)
        {
            const KartProperties* official_kp = OfficialKarts::getKartByIdent(
                new_ident, &m_kart_width, &m_kart_height, &m_kart_length,
                &gravity_shift);
            if (official_kp)
            {
                kp = official_kp;
                new_hitbox = true;
            }
        }
    }

    if (NetworkConfig::get()->isNetworking() &&
        !kart_data.m_kart_type.empty() &&
        kart_properties_manager->hasKartTypeCharacteristic(
        kart_data.m_kart_type))
    {
        tmp_kp = new KartProperties();
        tmp_kp->copyFrom(kp);
        tmp_kp->initKartWithDifferentType(kart_data.m_kart_type);
        kp = tmp_kp;
        m_kart_width = kart_data.m_width;
        m_kart_height = kart_data.m_height;
        m_kart_length = kart_data.m_length;
        gravity_shift = kart_data.m_gravity_shift;
        new_hitbox = true;
    }

    m_kart_properties->copyForPlayer(kp, handicap);
    if (kp_addon)
        m_kart_properties->adjustForOnlineAddonKart(kp_addon);

    if (new_hitbox)
    {
        m_kart_properties->updateForOnlineKart(new_ident, gravity_shift,
            m_kart_length);
    }
    m_name = m_kart_properties->getName();
    m_handicap = handicap;
    m_kart_animation  = NULL;
    assert(m_kart_properties);

    // We have to take a copy of the kart model, since otherwise
    // the animations will be mixed up (i.e. different instances of
    // the same model will set different animation frames).
    // Technically the mesh in m_kart_model needs to be grab'ed and
    // released when the kart is deleted, but since the original
    // kart_model is stored in the kart_properties all the time,
    // there is no risk of a mesh being deleted too early.
    if (kp_addon)
        m_kart_model.reset(kp_addon->getKartModelCopy(ri));
    else
        m_kart_model.reset(m_kart_properties->getKartModelCopy(ri));
    if (!new_hitbox)
    {
        m_kart_width  = kp->getMasterKartModel().getWidth();
        m_kart_height = kp->getMasterKartModel().getHeight();
        m_kart_length = kp->getMasterKartModel().getLength();
    }
    m_kart_highest_point = m_kart_model->getHighestPoint();
    m_wheel_graphics_position = m_kart_model->getWheelsGraphicsPosition();
    delete tmp_kp;
}   // loadKartProperties

// ----------------------------------------------------------------------------
void AbstractKart::changeKart(const std::string& new_ident,
                              HandicapLevel handicap,
                              std::shared_ptr<GE::GERenderInfo> ri,
                              const KartData& kart_data)
{
    // Reset previous kart (including delete old animation above)
    reset();
    // Remove kart body
    Physics::get()->removeKart(this);
    loadKartProperties(new_ident, handicap, ri, kart_data);
}   // changeKart

// ----------------------------------------------------------------------------
/** Returns a unique identifier for this kart (name of the directory the
 *  kart was loaded from). */
const std::string& AbstractKart::getIdent() const
{
    return m_kart_properties->getIdent();
}   // getIdent
// ----------------------------------------------------------------------------
bool AbstractKart::isWheeless() const
{
    return m_kart_model->getWheelModel(0)==NULL;
}   // isWheeless

// ----------------------------------------------------------------------------
/** Sets a new kart animation. This function should either be called to
 *  remove an existing kart animation (ka=NULL), or to set a new kart
 *  animation, in which case the current kart animation must be NULL.
 *  \param ka The new kart animation, or NULL if the current kart animation
 *            is to be stopped.
 */
void AbstractKart::setKartAnimation(AbstractKartAnimation *ka)
{
#ifdef DEBUG
    if( ( (ka!=NULL) ^ (m_kart_animation!=NULL) ) ==0)
    {
        if(ka) Log::debug("Abstract_Kart", "Setting kart animation to '%s'.",
                          ka->getName().c_str());
        else   Log::debug("Abstract_Kart", "Setting kart animation to NULL.");
        if(m_kart_animation) Log::info("Abstract_Kart", "Current kart"
                                       "animation is '%s'.",
                                        m_kart_animation->getName().c_str());
        else   Log::debug("Abstract_Kart", "Current kart animation is NULL.");
    }
#endif
    if (ka != NULL && m_kart_animation != NULL)
    {
        delete m_kart_animation;
        m_kart_animation = NULL;
    }

    // Make sure that the either the current animation is NULL and a new (!=0)
    // is set, or there is a current animation, then it must be set to 0. This
    // makes sure that the calling logic of this function is correct.
    assert( (ka!=NULL) ^ (m_kart_animation!=NULL) );
    m_kart_animation = ka;
}   // setKartAnimation

// ----------------------------------------------------------------------------
/** Returns the time at which the kart was at a given distance.
 * Returns -1.0f if none */
float AbstractKart::getTimeForDistance(float distance)
{
    return -1.0f;
}   // getTimeForDistance

// ----------------------------------------------------------------------------
/** Moves the current physical transform into this kart's position.
 */
void AbstractKart::kartIsInRestNow()
{
    // Update the kart transforms with the newly computed position
    // after all karts are reset
    m_starting_transform = getBody()->getWorldTransform();
    setTrans(m_starting_transform);
}   // kartIsInRest

// ------------------------------------------------------------------------
/** Called before go phase to make sure all karts start at the same
 *  position in case there is a slope. */
void AbstractKart::makeKartRest()
{
    btTransform t = m_starting_transform;
    if (m_live_join_util != 0)
    {
        t.setOrigin(t.getOrigin() +
            m_starting_transform.getBasis().getColumn(1) * 3.0f);
    }

    btRigidBody *body = getBody();
    body->clearForces();
    body->setLinearVelocity(Vec3(0.0f));
    body->setAngularVelocity(Vec3(0.0f));
    body->proceedToTransform(t);
    setTrans(t);
}   // makeKartRest

// ----------------------------------------------------------------------------
std::shared_ptr<HitProcessor> AbstractKart::getHitProcessor() const
{
    return RaceManager::get()->getHitProcessor();
}   // getHitProcessor
