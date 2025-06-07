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

#include "utils/chat_manager.hpp"

#include "network/network_player_profile.hpp"
#include "network/network_string.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/remote_kart_info.hpp"
#include "network/server_config.hpp"
#include "network/stk_host.hpp"
#include "network/stk_peer.hpp"
#include "utils/communication.hpp"
#include "utils/string_utils.hpp"
#include "utils/string_utils.hpp"
#include "utils/tournament.hpp"

namespace
{
    const std::string g_red_team     = StringUtils::utf32ToUtf8({0x1f7e5, 0x20});
    const std::string g_blue_team    = StringUtils::utf32ToUtf8({0x1f7e6, 0x20});
    const std::string g_private_chat = StringUtils::utf32ToUtf8({0x1f512, 0x20});
};


void ChatManager::setupContextUser()
{
    m_chat                      = ServerConfig::m_chat;
    m_chat_consecutive_interval = ServerConfig::m_chat_consecutive_interval;
}   // setupContextUser
//-----------------------------------------------------------------------------

void ChatManager::addMutedPlayerFor(std::shared_ptr<STKPeer> peer,
                                      const std::string& name)
{
    m_peers_muted_players[std::weak_ptr<STKPeer>(peer)].insert(name);
}   // addMutedPlayerFor
//-----------------------------------------------------------------------------

bool ChatManager::removeMutedPlayerFor(std::shared_ptr<STKPeer> peer,
                                         const std::string& name)
{
    auto& collection = m_peers_muted_players[std::weak_ptr<STKPeer>(peer)];
    auto it = collection.find(name);
    if (it == collection.end())
        return false;

    collection.erase(it);
    return true;
}   // removeMutedPlayerFor
//-----------------------------------------------------------------------------

bool ChatManager::isMuting(std::shared_ptr<STKPeer> peer,
                             const std::string& name) const
{
    auto it = m_peers_muted_players.find(std::weak_ptr<STKPeer>(peer));
    if (it == m_peers_muted_players.end())
        return false;
    
    return it->second.find(name) != it->second.end();
}   // isMuting
//-----------------------------------------------------------------------------

std::string ChatManager::getMutedPlayersAsString(std::shared_ptr<STKPeer> peer)
{
    std::string response;
    int num_players = 0;
    for (auto& name : m_peers_muted_players[std::weak_ptr<STKPeer>(peer)])
    {
        response += StringUtils::quoteEscape(name, ' ', '"', '"', '\\');
        response += ", ";
        ++num_players;
    }

    if (response.size() >= 2)
    {
        response.resize(response.size() - 2);
        response.push_back(' ');
    }

    if (num_players == 0)
        response = "No player has been muted by you";
    else
    {
        response += (num_players == 1 ? "is" : "are");
        response += StringUtils::insertValues(" muted (total: %s)", num_players);
    }
    return response;
}   // getMutedPlayersAsString
//-----------------------------------------------------------------------------

void ChatManager::addTeamSpeaker(std::shared_ptr<STKPeer> peer)
{
    m_team_speakers.insert(peer);
}   // addTeamSpeaker
//-----------------------------------------------------------------------------

void ChatManager::setMessageReceiversFor(std::shared_ptr<STKPeer> peer,
    const std::vector<std::string>& receivers)
{
    auto& thing = m_message_receivers[peer];
    thing.clear();
    for (unsigned i = 0; i < receivers.size(); ++i)
        thing.insert(receivers[i]);
}   // setMessageReceiversFor
//-----------------------------------------------------------------------------

std::set<std::string> ChatManager::getMessageReceiversFor(
        std::shared_ptr<STKPeer> peer) const
{
    auto it = m_message_receivers.find(peer);
    if (it == m_message_receivers.end())
        return {};

    return it->second;
}   // getMessageReceiversFor
//-----------------------------------------------------------------------------

bool ChatManager::isTeamSpeaker(std::shared_ptr<STKPeer> peer) const
{
    return m_team_speakers.find(peer) != m_team_speakers.end();
}   // isTeamSpeaker
//-----------------------------------------------------------------------------

void ChatManager::makeChatPublicFor(std::shared_ptr<STKPeer> peer)
{
    m_message_receivers[peer].clear();
    m_team_speakers.erase(peer);
}   // makeChatPublicFor
//-----------------------------------------------------------------------------

void ChatManager::clearAllExpiredWeakPtrs()
{
    for (auto it = m_peers_muted_players.begin();
        it != m_peers_muted_players.end();)
    {
        if (it->first.expired())
            it = m_peers_muted_players.erase(it);
        else
            it++;
    }
}   // clearAllExpiredWeakPtrs
//-----------------------------------------------------------------------------

void ChatManager::onPeerDisconnect(std::shared_ptr<STKPeer> peer)
{
    m_message_receivers.erase(peer);
}   // onPeerDisconnect
//-----------------------------------------------------------------------------

void ChatManager::handleNormalChatMessage(std::shared_ptr<STKPeer> peer,
        std::string message, KartTeam target_team,
        const std::shared_ptr<GenericDecorator>& decorator)
{
    // Update so that the peer is not kicked
    peer->updateLastActivity();

    int64_t last_message = peer->getLastMessage();
    int64_t elapsed_time = (int64_t)StkTime::getMonoTimeMs() - last_message;

    int interval = getChatConsecutiveInterval();


    // Read ServerConfig for formula and details
    bool too_fast = interval > 0 && elapsed_time < interval * 1000;
    peer->updateConsecutiveMessages(too_fast);
    
    if (interval > 0 && peer->getConsecutiveMessages() > interval / 2)
    {
        Comm::sendStringToPeer(peer, "Spam detected");
        return;
    }


    // Check if the message starts with "(the name of main profile): " to prevent
    // impersonation, see #5121.
    std::string prefix = peer->getMainName() + ": ";
    
    if (!StringUtils::startsWith(message, prefix))
    {
        Comm::sendStringToPeer(peer, "Don't try to impersonate others!");
        return;
    }

    std::string new_prefix = StringUtils::wideToUtf8(
        peer->getMainProfile()->getDecoratedName(decorator)) + ": ";
    message = new_prefix + message.substr(prefix.length()); 

    if (message.size() == 0)
        return;

    const bool game_started = !getLobby()->isWaitingForStartGame();
    auto can_receive = getMessageReceiversFor(peer);
    if (!can_receive.empty())
        message = g_private_chat + message;

    bool team_mode = (
        RaceManager::get()->getMinorMode() == RaceManager::MINOR_MODE_SOCCER ||
        RaceManager::get()->getMinorMode() == RaceManager::MINOR_MODE_CAPTURE_THE_FLAG
    );

    bool team_speaker = isTeamSpeaker(peer);
    KartTeamSet teams = getTeamsForPeer(peer);
    
    // Add team emojis
    if (target_team == KART_TEAM_RED || (team_speaker && team_mode && teams.has(KART_TEAM_RED)))
        message = g_red_team + message;

    if (target_team == KART_TEAM_BLUE || (team_speaker && team_mode && teams.has(KART_TEAM_BLUE)))
        message = g_blue_team + message;

    ChatPacket packet;
    packet.message = StringUtils::utf8ToWide(message);

    STKHost::get()->sendPacketToAllPeersWith(
        std::bind(&ChatManager::shouldMessageBeSent,
                  this,
                  peer,
                  std::placeholders::_1,
                  game_started,
                  target_team
        ), packet
    );

    peer->updateLastMessage();
}   // handleNormalChatMessage
//-----------------------------------------------------------------------------

bool ChatManager::shouldMessageBeSent(std::shared_ptr<STKPeer> sender,
                                      std::shared_ptr<STKPeer> target,
                                      bool game_started,
                                      KartTeam target_team)
{
    if (sender == target)
        return true;
    if (game_started)
    {
        if (target->isWaitingForGame() ^ sender->isWaitingForGame())
            return false;

        if (target_team != KART_TEAM_NONE)
        {
            if (target->isSpectator())
                return false;

            if (!Tournament::hasProfileFromTeam(target, target_team))
                return false;
        }

        if (isTournament())
        {
            auto tournament = getTournament();
            if (tournament->cannotSendForSureDueToRoles(sender, target))
                return false;

            if (target_team != KART_TEAM_NONE)
            {
                if (!tournament->hasProfileThatSeesTeamchats(target) &&
                    !Tournament::hasProfileFromTeam(target, target_team))
                    return false;
            }
        }
    }
    if (isMuting(target, sender->getMainName()))
        return false;

    if (isTeamSpeaker(sender))
    {
        // this should be moved into a new function for peer,
        // unless all profiles have the same team forcibly rn
        bool someone_good = !(getTeamsForPeer(sender) & getTeamsForPeer(target)).empty();
        if (!someone_good && (!isTournament() || !getTournament()->hasProfileThatSeesTeamchats(target)))
            return false;
    }
    return isInPrivateChatRecipients(sender, target);
}   // lambda
//-----------------------------------------------------------------------------

// Should be called not once per message. Fix later
KartTeamSet ChatManager::getTeamsForPeer(std::shared_ptr<STKPeer> peer) const
{
    KartTeamSet teams;

    for (auto& profile: peer->getPlayerProfiles())
        teams.add(profile->getTeam());

    return teams;
}   // getTeamsForPeer
//-----------------------------------------------------------------------------

bool ChatManager::isInPrivateChatRecipients(std::shared_ptr<STKPeer> sender,
                                            std::shared_ptr<STKPeer> target) const
{
    // shouldn't be called every time, fix later
    std::set<std::string> can_receive = getMessageReceiversFor(sender);

    // If no private chat enabled, send
    if (can_receive.empty())
        return true;

    for (auto& profile : target->getPlayerProfiles())
    {
        if (can_receive.find(StringUtils::wideToUtf8(profile->getName())) !=
            can_receive.end())
        {
            return true;
        }
    }
    return false;
}   // isInPrivateChatRecipients
//-----------------------------------------------------------------------------