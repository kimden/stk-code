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

#ifndef LOBBY_CONTEXT_HPP
#define LOBBY_CONTEXT_HPP

#include "utils/cpp2011.hpp"

#include <memory>

class ChatManager;
class CommandManager;
class HitProcessor;
class KartElimination;
class LobbyAssetManager;
class LobbyQueues;
class LobbySettings;
class MapVoteHandler;
class ServerLobby;
class TeamManager;
class Tournament;

class LobbyContext
{
private:
    ServerLobby* m_lobby;
    std::shared_ptr<HitProcessor>      m_hit_processor;
    std::shared_ptr<LobbyAssetManager> m_asset_manager;
    std::shared_ptr<Tournament>        m_tournament;
    std::shared_ptr<LobbyQueues>       m_lobby_queues;
    std::shared_ptr<LobbySettings>     m_lobby_settings;
    std::shared_ptr<KartElimination>   m_kart_elimination;
    std::shared_ptr<MapVoteHandler>    m_map_vote_handler;
    std::shared_ptr<CommandManager>    m_command_manager;
    std::shared_ptr<ChatManager>       m_chat_manager;
    std::shared_ptr<TeamManager>       m_team_manager;

public:

    LobbyContext(ServerLobby* lobby, bool make_tournament);

    void setup();

    ServerLobby*                       getLobby()           const { return m_lobby; }
    std::shared_ptr<HitProcessor>      getHitProcessor()    const { return m_hit_processor; }
    std::shared_ptr<LobbyAssetManager> getAssetManager()    const { return m_asset_manager; }
    bool                               isTournament()       const { return m_tournament.get() != nullptr; }
    std::shared_ptr<Tournament>        getTournament()      const { return m_tournament; }
    std::shared_ptr<LobbyQueues>       getQueues()          const { return m_lobby_queues; }
    std::shared_ptr<LobbySettings>     getSettings()        const { return m_lobby_settings; }
    std::shared_ptr<KartElimination>   getKartElimination() const { return m_kart_elimination; }
    std::shared_ptr<MapVoteHandler>    getMapVoteHandler()  const { return m_map_vote_handler; }
    std::shared_ptr<CommandManager>    getCommandManager()  const { return m_command_manager; }
    std::shared_ptr<ChatManager>       getChatManager()     const { return m_chat_manager; }
    std::shared_ptr<TeamManager>       getTeamManager()     const { return m_team_manager; }
};

class LobbyContextUser
{
protected:
    LobbyContext* m_context;
    ServerLobby*                       getLobby()           const { return m_context->getLobby(); }
    std::shared_ptr<HitProcessor>      getHitProcessor()    const { return m_context->getHitProcessor(); }
    std::shared_ptr<LobbyAssetManager> getAssetManager()    const { return m_context->getAssetManager(); }
    bool                               isTournament()       const { return m_context->isTournament(); }
    std::shared_ptr<Tournament>        getTournament()      const { return m_context->getTournament(); }
    std::shared_ptr<LobbyQueues>       getQueues()          const { return m_context->getQueues(); }
    std::shared_ptr<LobbySettings>     getSettings()        const { return m_context->getSettings(); }
    std::shared_ptr<KartElimination>   getKartElimination() const { return m_context->getKartElimination(); }
    std::shared_ptr<MapVoteHandler>    getMapVoteHandler()  const { return m_context->getMapVoteHandler(); }
    std::shared_ptr<CommandManager>    getCommandManager()  const { return m_context->getCommandManager(); }
    std::shared_ptr<ChatManager>       getChatManager()     const { return m_context->getChatManager(); }
    std::shared_ptr<TeamManager>       getTeamManager()     const { return m_context->getTeamManager(); }

public:
    void setContext(LobbyContext* context)             { m_context = context; }
};

class LobbyContextComponent: public LobbyContextUser
{
public:

    LobbyContextComponent(LobbyContext* context)
    {
        m_context = context;
    }
    virtual void setupContextUser() = 0;
};

#endif // LOBBY_CONTEXT_HPP
