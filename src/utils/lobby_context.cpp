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

#include "utils/lobby_context.hpp"

#include "network/protocols/command_manager.hpp"
#include "utils/chat_manager.hpp"
#include "utils/hit_processor.hpp"
#include "utils/kart_elimination.hpp"
#include "utils/lobby_asset_manager.hpp"
#include "utils/lobby_queues.hpp"
#include "utils/lobby_settings.hpp"
#include "utils/map_vote_handler.hpp"
#include "utils/team_manager.hpp"
#include "utils/tournament.hpp"
#include "utils/lobby_gp_manager.hpp"
#include "utils/gp_scoring.hpp"
#include "utils/crown_manager.hpp"
#include "network/game_setup.hpp"

LobbyContext::LobbyContext(ServerLobby* lobby, bool make_tournament)
        : m_lobby(lobby)
{
    // Nothing for ServerLobby, as we create LC inside SL
    m_asset_manager    = std::make_shared<LobbyAssetManager>(this);
    m_hit_processor    = std::make_shared<HitProcessor>(this);
    m_kart_elimination = std::make_shared<KartElimination>(this);
    m_lobby_queues     = std::make_shared<LobbyQueues>(this);
    m_lobby_settings   = std::make_shared<LobbySettings>(this);
    m_map_vote_handler = std::make_shared<MapVoteHandler>(this);
    m_command_manager  = std::make_shared<CommandManager>(this);
    m_chat_manager     = std::make_shared<ChatManager>(this);
    m_team_manager     = std::make_shared<TeamManager>(this);
    m_gp_manager       = std::make_shared<LobbyGPManager>(this);
    m_crown_manager    = std::make_shared<CrownManager>(this);
    
    if (make_tournament)
        m_tournament   = std::make_shared<Tournament>(this);
}   // LobbyContext
//-----------------------------------------------------------------------------

void LobbyContext::setup()
{
    // Nothing for ServerLobby
    m_asset_manager->setupContextUser();
    m_hit_processor->setupContextUser();
    m_kart_elimination->setupContextUser();
    m_lobby_queues->setupContextUser();
    m_lobby_settings->setupContextUser();
    m_map_vote_handler->setupContextUser();
    m_chat_manager->setupContextUser();
    m_team_manager->setupContextUser();
    m_command_manager->setupContextUser();
    m_gp_manager->setupContextUser();
    m_crown_manager->setupContextUser();
    
    if (m_tournament)
        m_tournament->setupContextUser();
}   // setup
//-----------------------------------------------------------------------------
