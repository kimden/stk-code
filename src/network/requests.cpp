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

#include "network/requests.hpp"
#include "io/xml_node.hpp"
#include "network/server_config.hpp"
#include "network/stk_ipv6.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/socket_address.hpp"
#include "network/protocols/connect_to_peer.hpp"
#include "network/protocol_manager.hpp"

void RegisterServerRequest::afterOperation()
{
    Online::XMLRequest::afterOperation();
    const XMLNode* result = getXMLData();
    std::string rec_success;
    auto sl = m_server_lobby.lock();
    if (!sl)
        return;

    if (result->get("success", &rec_success) && rec_success == "yes")
    {
        const XMLNode* server = result->getNode("server");
        assert(server);
        const XMLNode* server_info = server->getNode("server-info");
        assert(server_info);
        unsigned server_id_online = 0;
        server_info->get("id", &server_id_online);
        assert(server_id_online != 0);
        bool is_official = false;
        server_info->get("official", &is_official);
        if (!is_official && ServerConfig::m_ranked)
        {
            Log::fatal("RegisterServerRequest",
                "You don't have permission to host a ranked server.");
        }
        Log::info("RegisterServerRequest", "Server %d is now online.", server_id_online);
        sl->setServerOnlineId(server_id_online);
        sl->resetSuccessPollTime();
        return;
    }
    Log::error("RegisterServerRequest", "%s", 
            StringUtils::wideToUtf8(getInfo()).c_str());

    // Exit now if failed to register to stk addons for first time
    if (m_first_time)
        sl->doErrorLeave();
}   // RegisterServerRequest::afterOperation
// ========================================================================

void UnregisterServerRequest::afterOperation()
{
    Online::XMLRequest::afterOperation();
    const XMLNode* result = getXMLData();
    std::string rec_success;

    if (result->get("success", &rec_success) &&
        rec_success == "yes")
    {
        // Clear the server online for next register
        // For grand prix server
        if (auto sl = m_server_lobby.lock())
            sl->setServerOnlineId(0);
        return;
    }
    Log::error("UnregisterServerRequest", "%s",
        StringUtils::wideToUtf8(getInfo()).c_str());
}   // UnregisterServerRequest::afterOperation
// ========================================================================

void PollServerRequest::afterOperation()
{
    Online::XMLRequest::afterOperation();
    const XMLNode* result = getXMLData();
    std::string success;

    if (!result->get("success", &success) || success != "yes")
    {
        Log::error("PollServerRequest", "Poll server request failed: %s",
            StringUtils::wideToUtf8(getInfo()).c_str());
        return;
    }

    // Now start a ConnectToPeer protocol for each connection request
    const XMLNode * users_xml = result->getNode("users");
    std::map<uint32_t, KeyData> keys;
    auto sl = m_server_lobby.lock();
    if (!sl)
        return;
    sl->resetSuccessPollTime();
    if (!sl->isWaitingForStartGame() &&
        !sl->allowJoinedPlayersWaiting())
    {
        sl->replaceKeys(keys);
        return;
    }

    sl->removeExpiredPeerConnection();
    for (unsigned int i = 0; i < users_xml->getNumNodes(); i++)
    {
        uint32_t addr, id;
        uint16_t port;
        std::string ipv6;
        const XMLNode* node = users_xml->getNode(i);
        node->get("ip", &addr);
        node->get("ipv6", &ipv6);
        node->get("port", &port);
        node->get("id", &id);
        node->get("aes-key", &keys[id].m_aes_key);
        node->get("aes-iv", &keys[id].m_aes_iv);
        node->get("username", &keys[id].m_name);
        node->get("country-code", &keys[id].m_country_code);
        keys[id].m_tried = false;
        if (ServerConfig::m_firewalled_server)
        {
            SocketAddress peer_addr(addr, port);
            if (!ipv6.empty())
                peer_addr.init(ipv6, port);
            peer_addr.convertForIPv6Socket(isIPv6Socket());
            std::string peer_addr_str = peer_addr.toString();
            if (sl->hasPendingPeerConnection(peer_addr_str))
                continue;

            auto ctp = std::make_shared<ConnectToPeer>(peer_addr);
            if (auto pm = m_protocol_manager.lock())
                pm->requestStart(ctp);
            sl->addPeerConnection(peer_addr_str);
        }
    }
    sl->replaceKeys(keys);
}   // PollServerRequest::afterOperation
// ========================================================================