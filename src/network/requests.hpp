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

#ifndef HEADER_REQUESTS_HPP
#define HEADER_REQUESTS_HPP

#include "online/xml_request.hpp"
#include "online/request_manager.hpp"

#include <memory>

class ProtocolManager;
class ServerLobby;


struct KeyData
{
    std::string m_aes_key;
    std::string m_aes_iv;
    irr::core::stringw m_name;
    std::string m_country_code;
    bool m_tried = false;
};

// ============================================================================
class RegisterServerRequest: public Online::XMLRequest
{
private:
    std::weak_ptr<ServerLobby> m_server_lobby;
    bool m_first_time;

protected:
    virtual void afterOperation() OVERRIDE;

public:
    RegisterServerRequest(std::shared_ptr<ServerLobby> sl, bool first_time)
            : XMLRequest(Online::RequestManager::HTTP_MAX_PRIORITY),
              m_server_lobby(sl),
              m_first_time(first_time) {}

};   // RegisterServerRequest
// ============================================================================

class UnregisterServerRequest : public Online::XMLRequest
{
private:
    std::weak_ptr<ServerLobby> m_server_lobby;

protected:
    virtual void afterOperation() OVERRIDE;

public:
    UnregisterServerRequest(std::weak_ptr<ServerLobby> sl)
            : XMLRequest(Online::RequestManager::HTTP_MAX_PRIORITY),
              m_server_lobby(sl) {}

};   // UnregisterServerRequest
// ============================================================================

class PollServerRequest : public Online::XMLRequest
{
private:
    std::weak_ptr<ServerLobby> m_server_lobby;
    std::weak_ptr<ProtocolManager> m_protocol_manager;

protected:
    virtual void afterOperation() OVERRIDE;

public:
    PollServerRequest(std::shared_ptr<ServerLobby> sl,
                        std::shared_ptr<ProtocolManager> pm)
            : XMLRequest(Online::RequestManager::HTTP_MAX_PRIORITY),
              m_server_lobby(sl),
              m_protocol_manager(pm)
    {
        m_disable_sending_log = true;
    }

};   // PollServerRequest
// ============================================================================

#endif // HEADER_REQUESTS_HPP