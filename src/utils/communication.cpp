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

#include "utils/communication.hpp"

#include "network/stk_host.hpp"
#include "network/packet_types.hpp"
#include "network/stk_peer.hpp"

// Probably not needed when packets branch is merged
#include "utils/string_utils.hpp"
#include "network/protocol.hpp"

namespace Comm
{

// ----------------------------------------------------------------------------
/** Sends a message to all validated peers in game, encrypt the message if
 *  needed. The message is composed of a 1-byte message (usually the message
 *  type) followed by the actual message.
 *  \param message The actual message content.
 *  \param reliable Whether it should be in a reliable way or not.
*/
[[deprecated("You should send packets to Comm::, it will pass NetString to STKHost,")]]
void sendNetstringToPeers(NetworkString *message,
                        PacketReliabilityMode reliable)
{
    STKHost::get()->sendNetstringToPeers(message, reliable);
}   // sendNetstringToPeers

// ----------------------------------------------------------------------------
/** Sends a message to all validated peers in server, encrypt the message if
 *  needed. The message is composed of a 1-byte message (usually the message
 *  type) followed by the actual message.
 *  \param message The actual message content.
 *  \param reliable Whether it should be in a reliable way or not.
*/
[[deprecated("You should send packets to Comm::, it will pass NetString to STKHost,")]]
void sendNetstringToPeersInServer(NetworkString* message,
                                PacketReliabilityMode reliable)
{
    STKHost::get()->sendNetstringToPeersInServer(message, reliable);
}   // sendNetstringToPeersInServer

// ----------------------------------------------------------------------------
/** Sends a message from a client to the server.
 *  \param message The actual message content.
 *  \param reliable Whether it should be in a reliable way or not.
 */
[[deprecated("You should send packets to Comm::, it will pass NetString to STKHost,")]]
void sendToServer(NetworkString *message,
                  PacketReliabilityMode reliable)
{
    STKHost::get()->sendToServer(message, reliable);
}   // sendMessage
//-----------------------------------------------------------------------------

void sendStringToPeer(std::shared_ptr<STKPeer> peer, const std::string& s)
{
    if (!peer)
    {
        sendStringToPeers(s);
        return;
    }

    ChatPacket packet;
    packet.message = StringUtils::utf8ToWide(s);
    peer->sendPacket(packet);
}   // sendStringToPeer
//-----------------------------------------------------------------------------

void sendStringToPeers(const std::string& s)
{
    ChatPacket packet;
    packet.message = StringUtils::utf8ToWide(s);
    sendPacketToPeers(packet);
}   // sendStringToPeers
//-----------------------------------------------------------------------------
}   // namespace Comm
