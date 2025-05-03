//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2021 Heuchi1, 2025 kimden
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

#include "items/attachment.hpp"
#include "karts/kart.hpp"
#include "karts/kart_properties.hpp"
#include "modes/world.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/server_config.hpp"
#include "utils/hit_processor.hpp"
#include "utils/lobby_settings.hpp"
#include "utils/string_utils.hpp"
#include "utils/team_manager.hpp"


namespace
{

    // Time after which a bowling ball hit doesn't trigger anything
    const float MAX_BOWL_TEAMMATE_HIT_TIME = 2.0f;

    Attachment::AttachmentType getAttachment(Kart* kart)
    {
        return kart->getAttachment()->getType();
    }

    const float g_hit_message_delay = 1.5f;

}   // namespace
//-----------------------------------------------------------------------------

void HitProcessor::setupContextUser()
{
    m_troll_active = ServerConfig::m_troll_active;
    m_show_hits = ServerConfig::m_show_teammate_hits;
    m_hit_mode = ServerConfig::m_teammate_hit_mode;
    m_message_prefix = ServerConfig::m_teammate_hit_msg_prefix;
    m_last_hit_msg = 0;
    m_swatter_punish.clear();

    // This was also present in ServerLobby::setup(), I'm not sure why.
    // Return back if there are problems.
    m_collecting_hit_info = false;
}   // HitProcessor
//-----------------------------------------------------------------------------

// This is called when collisions of an item are handled,
// so we keep track of hits caused by that item
void HitProcessor::setTeammateHitOwner(unsigned int ownerID,
        uint16_t ticks_since_thrown)
{
    m_current_item_owner_id = ownerID;
    m_ticks_since_thrown = ticks_since_thrown;
    m_karts_hit.clear();
    m_karts_exploded.clear();
    m_collecting_hit_info = true;
}   // setTeammateHitOwner
//-----------------------------------------------------------------------------

void HitProcessor::registerTeammateHit(unsigned int kartID)
{
    // only register if we know the item owner and victim is still racing
    if (m_collecting_hit_info &&
            !World::getWorld()->getKart(kartID)->hasFinishedRace())
        m_karts_hit.push_back(kartID);
}   // registerTeammateHit
//-----------------------------------------------------------------------------

void HitProcessor::registerTeammateExplode(unsigned int kartID)
{
    // only register if we know the item owner and victim is still racing
    if (m_collecting_hit_info &&
            !World::getWorld()->getKart(kartID)->hasFinishedRace())
        m_karts_exploded.push_back(kartID);
}   // registerTeammateExplode
//-----------------------------------------------------------------------------

void HitProcessor::sendTeammateHitMsg(std::string& s)
{
    World* w = World::getWorld();
    if (!w)
        return;

    int ticks = w->getTicksSinceStart();
    if (ticks - m_last_hit_msg > STKConfig::get()->time2Ticks(g_hit_message_delay))
    {
        m_last_hit_msg = ticks;
        getLobby()->sendStringToAllPeers(s);
    }
}   // sendTeammateHitMsg
//-----------------------------------------------------------------------------

void HitProcessor::handleTeammateHits()
{
    m_collecting_hit_info = false;

    // Get team of item owner
    auto kart_info = RaceManager::get()->getKartInfo(m_current_item_owner_id);
    const std::string owner_name = StringUtils::wideToUtf8(kart_info.getPlayerName());
    const int owner_team = getTeamManager()->getTeamForUsername(owner_name);

    if (owner_team == TeamUtils::NO_TEAM)
        return;

    Kart *owner = World::getWorld()->getKart(m_current_item_owner_id);

    // if item is too old, it doesn't count
    // currently only bowling balls have their creation time registered
    // so cakes will always count

    if (m_ticks_since_thrown > STKConfig::get()->time2Ticks(MAX_BOWL_TEAMMATE_HIT_TIME))
        return;

    if (showTeammateHits())
        processHitMessage(owner_name, owner_team);

    if (isTeammateHitMode())
        processTeammateHit(owner, owner_name, owner_team);
}   // handleTeammateHits
//-----------------------------------------------------------------------------

void HitProcessor::processHitMessage(const std::string& owner_name, int owner_team)
{
    // prepare string
    int num_victims = 0;
    std::string msg = m_message_prefix;
    std::string victims;
    msg += owner_name;
    msg += " just shot ";

    for (unsigned int i = 0; i < m_karts_exploded.size(); i++)
    {
        const std::string player_name = StringUtils::wideToUtf8(
            RaceManager::get()->getKartInfo(m_karts_exploded[i]).getPlayerName());
        const int playerTeam = getTeamManager()->getTeamForUsername(player_name);
        if (owner_team != playerTeam)
            continue;

        if (num_victims > 0)
            victims += " and ";
        victims += player_name;
        num_victims++;
    }
    if (num_victims > 0)
    {
        msg += (num_victims > 1) ? "teammates " : "teammate ";
        msg += victims;
        sendTeammateHitMsg(msg);
    }
}   // processHitMessage
//-----------------------------------------------------------------------------

void HitProcessor::processTeammateHit(Kart* owner,
        const std::string& owner_name, int owner_team)
{
    bool punished = false;
    // first check if we exploded at least one teammate
    for (unsigned int i = 0; i < m_karts_exploded.size() && !punished; i++)
    {
        const std::string player_name = StringUtils::wideToUtf8(
            RaceManager::get()->getKartInfo(m_karts_exploded[i]).getPlayerName());
        const int playerTeam = getTeamManager()->getTeamForUsername(player_name);
        if (owner_team != playerTeam)
            continue;

        punished = true;
        if (getAttachment(owner) == Attachment::ATTACH_BOMB)
        {
            // make bomb explode
            owner->getAttachment()->update(10000);
        }
        else if (owner->isShielded())
        {
            // if owner is shielded, take away shield
            owner->decreaseShieldTime();
        }
        else
            punishKart(owner, 1.0f, 1.0f);
    }

    // now check for deshielding teammates
    for (unsigned int i = 0; i < m_karts_hit.size() && !punished; i++)
    {
        const std::string player_name = StringUtils::wideToUtf8(
            RaceManager::get()->getKartInfo(m_karts_hit[i]).getPlayerName());
        const int playerTeam = getTeamManager()->getTeamForUsername(player_name);
        if (owner_team != playerTeam)
            continue;

        // we did, so punish
        punished = true;
        if (getAttachment(owner) == Attachment::ATTACH_BOMB)
        {
            // make bomb explode
            owner->getAttachment()->update(10000);
        }
        else if (owner->isShielded())
        {
            // if owner is shielded, take away shield
            owner->decreaseShieldTime();
        }
        else
        {
            // since teammate didn't explode, make anvil less severe
            punishKart(owner, 0.5f, 0.5f);
        }
    }
}   // processTeammateHit
//-----------------------------------------------------------------------------

void HitProcessor::handleSwatterHit(unsigned int ownerID, unsigned int victimID,
    bool success, bool has_hit_kart, uint16_t ticks_active)
{
    const std::string owner_name = StringUtils::wideToUtf8(
        RaceManager::get()->getKartInfo(ownerID).getPlayerName());
    const int owner_team = getTeamManager()->getTeamForUsername(owner_name);
    if (owner_team == TeamUtils::NO_TEAM)
        return;

    const std::string victim_name = StringUtils::wideToUtf8(
        RaceManager::get()->getKartInfo(victimID).getPlayerName());
    const int victim_team = getTeamManager()->getTeamForUsername(victim_name);
    if (victim_team != owner_team)
        return;

    // should we tell the world?
    if (showTeammateHits() && success)
    {
        std::string msg = StringUtils::insertValues(
            "%s%s just swattered teammate %s",
            m_message_prefix.c_str(),
            owner_name.c_str(),
            victim_name.c_str()
        );
        sendTeammateHitMsg(msg);
    }
    if (isTeammateHitMode())
    {
        // remove swatter
        Kart *owner = World::getWorld()->getKart(ownerID);
        owner->getAttachment()->setTicksLeft(0);
        
        // if this is the first kart hit and the swatter is in use
        // for less than 3s, the attacker also gets an anvil
        if (!has_hit_kart
                && ticks_active < STKConfig::get()->time2Ticks(3.0f)
                && success)
        {
            // we cannot do this here, will be done in update()
            m_swatter_punish.push_back(owner);
        }
    }
}   // handleSwatterHit
//-----------------------------------------------------------------------------

void HitProcessor::handleAnvilHit(unsigned int ownerID, unsigned int victimID)
{
    const std::string owner_name = StringUtils::wideToUtf8(
        RaceManager::get()->getKartInfo(ownerID).getPlayerName());
    const int owner_team = getTeamManager()->getTeamForUsername(owner_name);
    if (owner_team == TeamUtils::NO_TEAM)
        return;

    const std::string victim_name = StringUtils::wideToUtf8(
        RaceManager::get()->getKartInfo(victimID).getPlayerName());
    const int victim_team = getTeamManager()->getTeamForUsername(victim_name);
    if (victim_team != owner_team)
        return;

    Kart *owner = World::getWorld()->getKart(ownerID);

    // should we tell the world?
    if (showTeammateHits())
    {
        std::string msg = StringUtils::insertValues(
            "%s%s just gave an anchor to teammate %s",
            m_message_prefix.c_str(),
            owner_name.c_str(),
            victim_name.c_str()
        );
        sendTeammateHitMsg(msg);
    }
    if (isTeammateHitMode())
    {
        if (getAttachment(owner) == Attachment::ATTACH_BOMB)
        {
            // make bomb explode
            owner->getAttachment()->update(10000);
        }
        else
        {
            if (owner->isShielded())
            {
                // if owner is shielded, take away shield
                // since the anvil will also destroy the shield of the victim
                // we also punish this severely
                owner->decreaseShieldTime();
            }

            punishKart(owner, 1.0f, 2.0f);
        }
    }
}   // handleAnvilHit
//-----------------------------------------------------------------------------

void HitProcessor::punishSwatterHits()
{
    if (m_swatter_punish.size() > 0)
    {
        // punish players who swattered teammates
        for (auto& kart : m_swatter_punish)
            punishKart(kart, 1.0f, 1.0f);

        m_swatter_punish.clear();
    }
}   // punishSwatterHits
//-----------------------------------------------------------------------------

void HitProcessor::punishKart(Kart* kart, float value, float value2)
{
    int leftover_ticks = 0;

    // if owner already has an anvil or a parachute,
    // make new anvil last longer
    if (getAttachment(kart) == Attachment::ATTACH_ANVIL ||
        getAttachment(kart) == Attachment::ATTACH_PARACHUTE)
    {
        leftover_ticks = kart->getAttachment()->getTicksLeft();
    }

    auto time_used = kart->getKartProperties()->getAnvilDuration();
    kart->getAttachment()->set(Attachment::ATTACH_ANVIL,
        STKConfig::get()->time2Ticks(time_used) * value + leftover_ticks);

    auto factor = kart->getKartProperties()->getAnvilSpeedFactor();
    kart->adjustSpeed(factor / value);
}   // punishKart
//-----------------------------------------------------------------------------