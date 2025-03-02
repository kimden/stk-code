//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2024 kimden
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

#include "utils/hit_processor.hpp"
#include "network/server_config.hpp"
#include "karts/abstract_kart.hpp"
#include "modes/world.hpp"
#include "items/attachment.hpp"
#include "utils/string_utils.hpp"
#include "karts/kart_properties.hpp"
#include "network/protocols/server_lobby.hpp"
#include "utils/lobby_settings.hpp"


HitProcessor::HitProcessor(ServerLobby* lobby, std::shared_ptr<LobbySettings> settings)
    : m_lobby(lobby), m_lobby_settings(settings)
{
    m_troll_active = ServerConfig::m_troll_active;
    m_show_teammate_hits = ServerConfig::m_show_teammate_hits;
    m_teammate_hit_mode = ServerConfig::m_teammate_hit_mode;
    m_last_teammate_hit_msg = 0;
    m_teammate_swatter_punish.clear();

    // This was also present in ServerLobby::setup(), I'm not sure why.
    // Return back if there are problems.
    m_collecting_teammate_hit_info = false;
} // HitProcessor
// ========================================================================


// this is called when collisions of an item are handled
// so we keep track of hits caused by that item
void HitProcessor::setTeammateHitOwner(unsigned int ownerID, uint16_t ticks_since_thrown)
{
    m_teammate_current_item_ownerID = ownerID;
    m_teammate_ticks_since_thrown = ticks_since_thrown;
    m_teammate_karts_hit.clear();
    m_teammate_karts_exploded.clear();
    m_collecting_teammate_hit_info = true;
}   // setTeammateHitOwner
//-----------------------------------------------------------------------------

void HitProcessor::registerTeammateHit(unsigned int kartID)
{
    // only register if we know the item owner and victim is still racing
    if (m_collecting_teammate_hit_info && !World::getWorld()->getKart(kartID)->hasFinishedRace())
        m_teammate_karts_hit.push_back(kartID);
}   // registerTeammateHit
//-----------------------------------------------------------------------------

void HitProcessor::registerTeammateExplode(unsigned int kartID)
{
    // only register if we know the item owner and victim is still racing
    if (m_collecting_teammate_hit_info && !World::getWorld()->getKart(kartID)->hasFinishedRace())
        m_teammate_karts_exploded.push_back(kartID);
}   // registerTeammateExplode
//-----------------------------------------------------------------------------

void HitProcessor::sendTeammateHitMsg(std::string& s)
{
    if (World* w = World::getWorld())
    {
        int ticks = w->getTicksSinceStart();
        if (ticks - m_last_teammate_hit_msg > stk_config->time2Ticks(1.5f))
        {
            m_last_teammate_hit_msg = ticks;
            m_lobby->sendStringToAllPeers(s);
        }
    }
}   // sendTeammateHitMsg
//-----------------------------------------------------------------------------

void HitProcessor::handleTeammateHits()
{
    m_collecting_teammate_hit_info = false;
    // get team of owner of item
    const std::string owner_name = StringUtils::wideToUtf8(
        RaceManager::get()->getKartInfo(m_teammate_current_item_ownerID).getPlayerName());
    const int owner_team = m_lobby_settings->getTeamForUsername(owner_name);

    if (owner_team == 0) // no team, no punishment
        return;

    AbstractKart *owner = World::getWorld()->getKart(m_teammate_current_item_ownerID);

    // if item is too old, it doesn't count
    // currently only bowling balls have their creation time registered
    // so cakes will always count

    if (m_teammate_ticks_since_thrown > stk_config->time2Ticks(MAX_BOWL_TEAMMATE_HIT_TIME))
        return;

    // Show message?
    if (showTeammateHits())
    {
        // prepare string
        int num_victims = 0;
        std::string msg = ServerConfig::m_teammate_hit_msg_prefix;
        std::string victims;
        msg += owner_name;
        msg += " just shot ";

        for (unsigned int i = 0; i < m_teammate_karts_exploded.size(); i++)
        {
            const std::string playername = StringUtils::wideToUtf8(
                RaceManager::get()->getKartInfo(m_teammate_karts_exploded[i]).getPlayerName());
            const int playerTeam = m_lobby_settings->getTeamForUsername(playername);
            if (owner_team == playerTeam)
            {
                // hit teammate
                if (num_victims > 0)
                    victims += " and ";
                victims += StringUtils::wideToUtf8(RaceManager::get()->getKartInfo(m_teammate_karts_exploded[i]).getPlayerName());
                num_victims++;
            }
        }
        if (num_victims > 0) // we found victims, so send message
        {
            msg += (num_victims > 1) ? "teammates " : "teammate ";
            msg += victims;
            sendTeammateHitMsg(msg);
        }
    }

    if (isTeammateHitMode())
    {
        bool punished = false;
        // first check if we exploded at least one teammate
        for (unsigned int i = 0; i < m_teammate_karts_exploded.size() && !punished; i++)
        {
            const std::string playername = StringUtils::wideToUtf8(
                RaceManager::get()->getKartInfo(m_teammate_karts_exploded[i]).getPlayerName());
            const int playerTeam = m_lobby_settings->getTeamForUsername(playername);
            if (owner_team == playerTeam)
            {
                // we did, so punish
                punished = true;
                if(owner->getAttachment()->getType() == Attachment::ATTACH_BOMB)
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
                    int left_over_ticks = 0;
                    // if owner already has an anvil or a parachute, make new anvil last longer
                    if (owner->getAttachment()->getType() == Attachment::ATTACH_ANVIL
                        || owner->getAttachment()->getType() == Attachment::ATTACH_PARACHUTE)
                    {
                        left_over_ticks = owner->getAttachment()->getTicksLeft();
                    }
                    owner->getAttachment()->set(Attachment::ATTACH_ANVIL,
                        stk_config->time2Ticks(owner->getKartProperties()->getAnvilDuration()) + left_over_ticks);
                    owner->adjustSpeed(owner->getKartProperties()->getAnvilSpeedFactor());
                }
            }
        }

        // now check for deshielding teammates
        for (unsigned int i = 0; i < m_teammate_karts_hit.size() && !punished; i++)
        {
            const std::string playername = StringUtils::wideToUtf8(
                RaceManager::get()->getKartInfo(m_teammate_karts_hit[i]).getPlayerName());
            const int playerTeam = m_lobby_settings->getTeamForUsername(playername);
            if (owner_team == playerTeam)
            {
                // we did, so punish
                punished = true;
                if (owner->getAttachment()->getType() == Attachment::ATTACH_BOMB)
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
                    int left_over_ticks = 0;
                    // if owner already has an anvil or a parachute, make new anvil last longer
                    if (owner->getAttachment()->getType() == Attachment::ATTACH_ANVIL
                        || owner->getAttachment()->getType() == Attachment::ATTACH_PARACHUTE)
                    {
                        left_over_ticks = owner->getAttachment()->getTicksLeft();
                    }
                    owner->getAttachment()->set(Attachment::ATTACH_ANVIL,
                        stk_config->time2Ticks(owner->getKartProperties()->getAnvilDuration()) / 2 + left_over_ticks);
                    owner->adjustSpeed(owner->getKartProperties()->getAnvilSpeedFactor() * 2.0f);
                }
            }
        }
    }
}   // handleTeammateHits
//-----------------------------------------------------------------------------

void HitProcessor::handleSwatterHit(unsigned int ownerID, unsigned int victimID,
    bool success, bool has_hit_kart, uint16_t ticks_active)
{
    const std::string owner_name = StringUtils::wideToUtf8(
        RaceManager::get()->getKartInfo(ownerID).getPlayerName());
    const int owner_team = m_lobby_settings->getTeamForUsername(owner_name);
    if (owner_team == 0)
        return;

    const std::string victim_name = StringUtils::wideToUtf8(
        RaceManager::get()->getKartInfo(victimID).getPlayerName());
    const int victim_team = m_lobby_settings->getTeamForUsername(victim_name);
    if (victim_team != owner_team)
        return;

    // should we tell the world?
    if (showTeammateHits() && success)
    {
        std::string msg = StringUtils::insertValues(
            "%s%s just swattered teammate %s",
            std::string(ServerConfig::m_teammate_hit_msg_prefix).c_str(),
            owner_name.c_str(),
            victim_name.c_str()
        );
        sendTeammateHitMsg(msg);
    }
    if (isTeammateHitMode())
    {
        // remove swatter
        AbstractKart *owner = World::getWorld()->getKart(ownerID);
        owner->getAttachment()->setTicksLeft(0);
        // if this is the first kart hit and the swatter is in use for less than 3s
        // the attacker also gets an anvil
        if (!has_hit_kart && ticks_active < stk_config->time2Ticks(3.0f) && success)
            // we cannot do this here, will be done in update()
            m_teammate_swatter_punish.push_back(owner);
    }
}   // handleSwatterHit
//-----------------------------------------------------------------------------

void HitProcessor::handleAnvilHit(unsigned int ownerID, unsigned int victimID)
{
    const std::string owner_name = StringUtils::wideToUtf8(
        RaceManager::get()->getKartInfo(ownerID).getPlayerName());
    const int owner_team = m_lobby_settings->getTeamForUsername(owner_name);
    if (owner_team == 0)
        return;

    const std::string victim_name = StringUtils::wideToUtf8(
        RaceManager::get()->getKartInfo(victimID).getPlayerName());
    const int victim_team = m_lobby_settings->getTeamForUsername(victim_name);
    if (victim_team != owner_team)
        return;

    AbstractKart *owner = World::getWorld()->getKart(ownerID);

    // should we tell the world?
    if (showTeammateHits())
    {
        std::string msg = StringUtils::insertValues(
            "%s%s just gave an anchor to teammate %s",
            std::string(ServerConfig::m_teammate_hit_msg_prefix).c_str(),
            owner_name.c_str(),
            victim_name.c_str()
        );
        sendTeammateHitMsg(msg);
    }
    if (isTeammateHitMode())
    {
        if (owner->getAttachment()->getType() == Attachment::ATTACH_BOMB)
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

            // now give anvil to owner
            int left_over_ticks = 0;
            // if owner already has an anvil or a parachute, make new anvil last longer
            if (owner->getAttachment()->getType() == Attachment::ATTACH_ANVIL
                || owner->getAttachment()->getType() == Attachment::ATTACH_PARACHUTE)
            {
                left_over_ticks = owner->getAttachment()->getTicksLeft();
            }
            owner->getAttachment()->set(Attachment::ATTACH_ANVIL,
                                        stk_config->time2Ticks(owner->getKartProperties()->getAnvilDuration()) + left_over_ticks);
            // the powerup anvil is very strong, copy these values (from powerup.cpp)
            owner->adjustSpeed(owner->getKartProperties()->getAnvilSpeedFactor() * 0.5f);
        }
    }
}   // handleAnvilHit
//-----------------------------------------------------------------------------

void HitProcessor::punishSwatterHits()
{
    if (m_teammate_swatter_punish.size() > 0)
    {
        // punish players who swattered teammates
        for (auto& kart : m_teammate_swatter_punish)
        {
            kart->getAttachment()->set(Attachment::ATTACH_ANVIL,
                stk_config->time2Ticks(kart->getKartProperties()->getAnvilDuration()));
            kart->adjustSpeed(kart->getKartProperties()->getAnvilSpeedFactor());
        }
        m_teammate_swatter_punish.clear();
    }
}   // punishSwatterHits
//-----------------------------------------------------------------------------