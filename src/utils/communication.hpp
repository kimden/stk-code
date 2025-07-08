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

#ifndef COMMUNICATION_HPP
#define COMMUNICATION_HPP

#include "network/network_string.hpp"
#include "utils/constants.hpp"

/** A namespace with useful communication functions,
 *  previously located in server_lobby.cpp */

namespace Comm
{
    void sendNetstringToPeers(NetworkString *message,
                            PacketReliabilityMode reliable = PRM_RELIABLE);
    void sendNetstringToPeersInServer(NetworkString *message,
                                    PacketReliabilityMode reliable = PRM_RELIABLE);
    void sendToServer(NetworkString *message,
                      PacketReliabilityMode reliable = PRM_RELIABLE);

    // Uses PROTOCOL_LOBBY_ROOM as chat is sent from there usually.
    // Using in non-lobby classes would be strange but seemingly ok.
    void sendStringToPeer(std::shared_ptr<STKPeer> peer, const std::string& s);

    // Uses PROTOCOL_LOBBY_ROOM as chat is sent from there usually.
    // Using in non-lobby classes would be strange but seemingly ok.
    void sendStringToPeers(const std::string& s);

    // ----------------------------------------------------------------------------
    template<typename T>
    typename std::enable_if<std::is_base_of<Packet, T>::value, void>::type
    sendPacketToPeers(const T& packet,
            PacketReliabilityMode reliable = PRM_RELIABLE)
    {
        std::shared_ptr<T> ptr1 = std::make_shared<T>(packet);
        std::shared_ptr<Packet> ptr2 = std::dynamic_pointer_cast<Packet>(ptr1);
        return STKHost::get()->sendPacketPtrToPeers(ptr2, reliable);
    }

    template<typename T>
    typename std::enable_if<std::is_base_of<Packet, T>::value, void>::type
    sendPacketToPeersInServer(const T& packet,
            PacketReliabilityMode reliable = PRM_RELIABLE)
    {
        std::shared_ptr<T> ptr1 = std::make_shared<T>(packet);
        std::shared_ptr<Packet> ptr2 = std::dynamic_pointer_cast<Packet>(ptr1);
        return STKHost::get()->sendPacketPtrToPeersInServer(ptr2, reliable);
    }

    template<typename T>
    typename std::enable_if<std::is_base_of<Packet, T>::value, void>::type
    sendPacketToServer(const T& packet,
            PacketReliabilityMode reliable = PRM_RELIABLE)
    {
        std::shared_ptr<T> ptr1 = std::make_shared<T>(packet);
        std::shared_ptr<Packet> ptr2 = std::dynamic_pointer_cast<Packet>(ptr1);
        return STKHost::get()->sendPacketPtrToServer(ptr2, reliable);
    }

    template<typename T>
    typename std::enable_if<std::is_base_of<Packet, T>::value, void>::type
    sendPacketExcept(std::shared_ptr<STKPeer> peer, const T& packet,
            PacketReliabilityMode reliable = PRM_RELIABLE)
    {
        std::shared_ptr<T> ptr1 = std::make_shared<T>(packet);
        std::shared_ptr<Packet> ptr2 = std::dynamic_pointer_cast<Packet>(ptr1);
        return STKHost::get()->sendPacketPtrExcept(peer, ptr2, reliable);
    }

    template<typename T>
    typename std::enable_if<std::is_base_of<Packet, T>::value, void>::type
    sendPacketToPeersWith(std::function<bool(std::shared_ptr<STKPeer>)> predicate,
            const T& packet, PacketReliabilityMode reliable = PRM_RELIABLE)
    {
        std::shared_ptr<T> ptr1 = std::make_shared<T>(packet);
        std::shared_ptr<Packet> ptr2 = std::dynamic_pointer_cast<Packet>(ptr1);
        return STKHost::get()->sendPacketPtrToPeersWith(predicate, ptr2, reliable);
    }
    // ------------------------------------------------------------------------
}


#endif // COMMUNICATION_HPP