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

#ifndef CHAT_MANAGER_HPP
#define CHAT_MANAGER_HPP

#include "irrString.h"
#include "utils/lobby_context.hpp"
#include "utils/types.hpp"

#include <map>
#include <memory>
#include <set>

class STKPeer;
enum KartTeam : int8_t;
struct KartTeamSet;

class ChatManager: public LobbyContextComponent
{
public:
    ChatManager(LobbyContext* context): LobbyContextComponent(context) {}

    void setupContextUser() OVERRIDE;

private:
    std::map<std::weak_ptr<STKPeer>, std::set<std::string>,
        std::owner_less<std::weak_ptr<STKPeer> > > m_peers_muted_players;

    std::map<std::shared_ptr<STKPeer>, std::set<std::string>> m_message_receivers;

    std::set<std::shared_ptr<STKPeer>> m_team_speakers;

    bool m_chat;
    
    int m_chat_consecutive_interval;

public:
    void addMutedPlayerFor(std::shared_ptr<STKPeer> peer,
                           const std::string& name);

    bool removeMutedPlayerFor(std::shared_ptr<STKPeer> peer,
                           const std::string& name);

    bool isMuting(std::shared_ptr<STKPeer> peer,
                           const std::string& name) const;

    std::string getMutedPlayersAsString(std::shared_ptr<STKPeer> peer);
    void addTeamSpeaker(std::shared_ptr<STKPeer> peer);

    void setMessageReceiversFor(std::shared_ptr<STKPeer> peer,
                                    const std::vector<std::string>& receivers);

    std::set<std::string> getMessageReceiversFor(
                                          std::shared_ptr<STKPeer> peer) const;

    bool isTeamSpeaker(std::shared_ptr<STKPeer> peer) const;
    void makeChatPublicFor(std::shared_ptr<STKPeer> peer);
    void clearAllExpiredWeakPtrs();
    void onPeerDisconnect(std::shared_ptr<STKPeer> peer);
    bool getChat()                   const { return m_chat;                      }
    int getChatConsecutiveInterval() const { return m_chat_consecutive_interval; }

    void handleNormalChatMessage(std::shared_ptr<STKPeer> peer,
            std::string message, KartTeam target_team);

    bool shouldMessageBeSent(std::shared_ptr<STKPeer> sender,
                             std::shared_ptr<STKPeer> target,
                             bool game_started,
                             KartTeam target_team);

    KartTeamSet getTeamsForPeer(std::shared_ptr<STKPeer> peer) const;
    bool isInPrivateChatRecipients(std::shared_ptr<STKPeer> sender,
                                   std::shared_ptr<STKPeer> target) const;
};

#endif // CHAT_MANAGER_HPP