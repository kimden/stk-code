//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2015  Supertuxkart-Team
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

#include "network/protocols/game_protocol.hpp"

#include "items/item_manager.hpp"
#include "items/network_item_manager.hpp"
#include "karts/abstract_kart.hpp"
#include "karts/controller/player_controller.hpp"
#include "modes/world.hpp"
#include "network/event.hpp"
#include "network/network_config.hpp"
#include "network/game_setup.hpp"
#include "network/network.hpp"
#include "network/network_config.hpp"
#include "network/network_string.hpp"
#include "network/protocol_manager.hpp"
#include "network/rewind_info.hpp"
#include "network/rewind_manager.hpp"
#include "network/socket_address.hpp"
#include "network/stk_host.hpp"
#include "network/stk_peer.hpp"
#include "tracks/track.hpp"
#include "utils/communication.hpp"
#include "utils/log.hpp"
#include "utils/time.hpp"
#include "main_loop.hpp"

// ============================================================================
std::weak_ptr<GameProtocol> GameProtocol::m_game_protocol[PT_COUNT];
// ============================================================================
std::shared_ptr<GameProtocol> GameProtocol::createInstance()
{
    if (!emptyInstance())
    {
        Log::fatal("GameProtocol", "Create only 1 instance of GameProtocol!");
        return NULL;
    }
    auto gm = std::make_shared<GameProtocol>();
    ProcessType pt = STKProcess::getType();
    m_game_protocol[pt] = gm;
    return gm;
}   // createInstance

//-----------------------------------------------------------------------------
/** Constructor. Allocates the buffer for events to send to the server. */
GameProtocol::GameProtocol()
            : Protocol(PROTOCOL_CONTROLLER_EVENTS)
{
    m_network_item_manager = static_cast<NetworkItemManager*>
        (Track::getCurrentTrack()->getItemManager());
    m_packet_to_send = std::make_shared<BigGameStatesPacket>();
}   // GameProtocol

//-----------------------------------------------------------------------------
GameProtocol::~GameProtocol()
{
}   // ~GameProtocol

//-----------------------------------------------------------------------------
/** Synchronous update - will send all commands collected during the last
 *  frame (and could optional only send messages every N frames).
 */
void GameProtocol::sendActions()
{
    if (m_all_actions.size() == 0) return;   // nothing to do

    // Clear left-over data from previous frame. This way the network
    // string will increase till it reaches maximum size necessary
    if (m_all_actions.size() > 255)
    {
        Log::warn("GameProtocol",
            "Too many actions unsent %d.", (int)m_all_actions.size());
        m_all_actions.resize(255);
    }
    ControllerActionBigPacket big_packet;
    big_packet.count = m_all_actions.size();

    // Add all actions
    for (auto& a : m_all_actions)
    {
        if (Network::m_connection_debug)
        {
            Log::verbose("GameProtocol",
                "Controller action: %d %d %d %d %d %d",
                a.m_ticks, a.m_kart_id, a.m_action, a.m_value, a.m_value_l,
                a.m_value_r);
        }
        const auto& c = compressAction(a);

        ControllerSingleActionPacket packet;
        packet.ticks = a.m_ticks;
        packet.subpacket.kart_id = a.m_kart_id;
        packet.subpacket.compressed_action_0 = std::get<0>(c);
        packet.subpacket.compressed_action_1 = std::get<1>(c);
        packet.subpacket.compressed_action_2 = std::get<2>(c);
        packet.subpacket.compressed_action_3 = std::get<3>(c);
        big_packet.actions.push_back(packet);
    }   // for a in m_all_actions

    Comm::sendPacketToServer(big_packet, PRM_RELIABLE);

    m_packet_to_send = std::make_shared<BigGameStatesPacket>(big_packet);

    m_all_actions.clear();
}   // sendActions

//-----------------------------------------------------------------------------
/** Called when a message from a remote GameProtocol is received.
 */
bool GameProtocol::notifyEventAsynchronous(Event* event)
{
    if(!checkDataSize(event, 1)) return true;

    // Ignore events arriving when client has already exited
    auto lock = acquireWorldDeletingMutex();
    if (!World::getWorld())
        return true;

    NetworkString &data = event->data();
    uint8_t message_type = data.getUInt8();
    switch (message_type)
    {
    case GP_CONTROLLER_ACTION: handleControllerAction(event); break;
    case GP_STATE:             handleState(event);            break;
    case GP_ITEM_CONFIRMATION: handleItemEventConfirmation(event); break;
    case GP_ADJUST_TIME:
    case GP_ITEM_UPDATE:
        break;
    default: Log::error("GameProtocol",
                        "Received unknown message type %d - ignored.",
                        message_type);                        break;
    }   // switch message_type
    return true;
}   // notifyEventAsynchronous

//-----------------------------------------------------------------------------
/** Called from the local kart controller when an action (like steering,
 *  acceleration, ...) was triggered. It sends a message with the new info
 *  to the server and informs the rewind manager to store the event.
 *  \param Kart id that triggered the action.
 *  \param action Which action was triggered.
 *  \param value New value for the given action.
 */
void GameProtocol::controllerAction(int kart_id, PlayerAction action,
                                    int value, int val_l, int val_r)
{
    // Store the action in the list of actions that will be sent to the
    // server next.
    assert(NetworkConfig::get()->isClient());
    Action a;
    a.m_kart_id = kart_id;
    a.m_action  = action;
    a.m_value   = value;
    a.m_value_l = val_l;
    a.m_value_r = val_r;
    a.m_ticks   = World::getWorld()->getTicksSinceStart();

    m_all_actions.push_back(a);
    const auto& c = compressAction(a);
    // Store the event in the rewind manager, which is responsible
    // for freeing the allocated memory

    ControllerActionPacket packet;
    packet.kart_id = kart_id;
    packet.compressed_action_0 = std::get<0>(c);
    packet.compressed_action_1 = std::get<1>(c);
    packet.compressed_action_2 = std::get<2>(c);
    packet.compressed_action_3 = std::get<3>(c);

    RewindManager::get()->addEvent(this, packet, /*confirmed*/true,
                                   World::getWorld()->getTicksSinceStart());
}   // controllerAction

// ----------------------------------------------------------------------------
/** Called when a controller event is received - either on the server from
 *  a client, or on a client from the server. It sorts the event into the
 *  RewindManager's network event queue. The server will also send this 
 *  event immediately to all clients (except to the original sender).
 */
void GameProtocol::handleControllerAction(Event *event)
{
    std::shared_ptr<STKPeer> peer = event->getPeerSP();
    if (NetworkConfig::get()->isServer() && (peer->isWaitingForGame() ||
        peer->getAvailableKartIDs().empty()))
        return;

    auto packet = event->getPacket<ControllerActionBigPacket>();
    uint8_t count = packet.count;
    bool will_trigger_rewind = false;
    //int rewind_delta = 0;
    int cur_ticks = 0;
    const int not_rewound = RewindManager::get()->getNotRewoundWorldTicks();
    for (unsigned int i = 0; i < count; i++)
    {
        auto& action_packet = packet.actions[i];
        cur_ticks = action_packet.ticks;
        // Since this is running in a thread, it might be called during
        // a rewind, i.e. with an incorrect world time. So the event
        // time needs to be compared with the World time independent
        // of any rewinding.
        if (cur_ticks < not_rewound && !will_trigger_rewind)
        {
            will_trigger_rewind = true;
            //rewind_delta = not_rewound - cur_ticks;
        }
        uint8_t kart_id = action_packet.subpacket.kart_id;
        if (NetworkConfig::get()->isServer() &&
            !peer->availableKartID(kart_id))
        {
            Log::warn("GameProtocol", "Wrong kart id %d from %s.",
                kart_id, peer->getAddress().toString().c_str());
            return;
        }

        uint8_t w = action_packet.subpacket.compressed_action_0;
        uint16_t x = action_packet.subpacket.compressed_action_1;
        uint16_t y = action_packet.subpacket.compressed_action_2;
        uint16_t z = action_packet.subpacket.compressed_action_3;
        if (Network::m_connection_debug)
        {
            const auto& a = decompressAction(w, x, y, z);
            Log::verbose("GameProtocol",
                "Controller action: %d %d %d %d %d %d",
                cur_ticks, kart_id, std::get<0>(a), std::get<1>(a),
                std::get<2>(a), std::get<3>(a));
        }
        RewindManager::get()->addNetworkEvent(this, action_packet.subpacket, cur_ticks);
    }

    if (NetworkConfig::get()->isServer())
    {
        // Send update to all clients except the original sender if the event
        // is after the server time
        peer->updateLastActivity();
        if (!will_trigger_rewind)
            Comm::sendPacketExcept(peer, packet, PRM_UNRELIABLE);
    }   // if server

}   // handleControllerAction

// ----------------------------------------------------------------------------
/** Sends a confirmation to the server that all item events up to 'ticks'
 *  have been received.
 *  \param ticks Up to which time in ticks the item events have been received.
 */
void GameProtocol::sendItemEventConfirmation(int ticks)
{
    assert(NetworkConfig::get()->isClient());

    ItemConfirmationPacket packet;
    packet.ticks = ticks;

    Comm::sendPacketToServer(packet);
}   // sendItemEventConfirmation

// ----------------------------------------------------------------------------
/** Handles an item even confirmation from a client. Once it has been confirmed
 *  that all clients have received certain events, those can be deleted and
 *  do not need to be sent again.
 *  \param event The data from the client.
 */
void GameProtocol::handleItemEventConfirmation(Event *event)
{
    assert(NetworkConfig::get()->isServer());
    auto packet = event->getPacket<ItemConfirmationPacket>();
    int ticks = packet.ticks;
    m_network_item_manager->setItemConfirmationTime(event->getPeerSP(), ticks);
}   // handleItemEventConfirmation

// ----------------------------------------------------------------------------
/** Called by the server before assembling a new message containing the full
 *  state of the race to be sent to a client.
 */
void GameProtocol::startNewState()
{
    assert(NetworkConfig::get()->isServer());
    m_packet_to_send = std::make_shared<BigGameStatesPacket>();

    GameEventStatePacket packet;
    packet.ticks_since_start = World::getWorld()->getTicksSinceStart();
    m_packet_to_send->state = packet;
}   // startNewState

// ----------------------------------------------------------------------------
/** Called by a server to add data to the current state. The data in buffer
 *  is copied, so the data can be freed after this call/.
 *  \param buffer Adds the data in the buffer to the current state.
 */
void GameProtocol::addState(TheRestOfBgsPacket packet)
{
    assert(NetworkConfig::get()->isServer());

    m_packet_to_send->the_rest.push_back(std::move(packet));
}   // addState

// ----------------------------------------------------------------------------
/** Called by a server to finalize the current state, which add updated
 *  names of rewinder using to the beginning of state buffer
 *  \param cur_rewinder List of current rewinder using.
 */
void GameProtocol::finalizeState(const std::vector<ProjectilePacket>& cur_rewinder)
{
    assert(NetworkConfig::get()->isServer());

    m_packet_to_send->rewinders_size = cur_rewinder.size();
    m_packet_to_send->rewinders = cur_rewinder; // they should be after time
}   // finalizeState

// ----------------------------------------------------------------------------
/** Called when the last state information has been added and the message
 *  can be sent to the clients.
 */
void GameProtocol::sendState()
{
    assert(NetworkConfig::get()->isServer());
    Comm::sendPacketToPeers(*m_packet_to_send, PRM_UNRELIABLE);
}   // sendState

// ----------------------------------------------------------------------------
/** Called when a new full state is received form the server.
 */
void GameProtocol::handleState(Event *event)
{
    if (!NetworkConfig::get()->isClient())
        return;
    
    auto packet = event->getPacket<BigGameStatesPacket>();

    int ticks          = packet.ticks;

    // Check for updated rewinder using
    unsigned rewinder_size = packet.rewinders_size;

    // The memory for bns will be handled in the RewindInfoState object
    RewindInfoState* ris = new RewindInfoState(ticks, packet.rewinders, packet.the_rest);
    RewindManager::get()->addNetworkRewindInfo(ris);
}   // handleState

// ----------------------------------------------------------------------------
/** Called from the RewindManager when rolling back.
 *  \param buffer Pointer to the saved state information.
 */
void GameProtocol::undo(BareNetworkString *buffer)
{

}   // undo

// ----------------------------------------------------------------------------
/** Called from the RewindManager after a rollback to replay the stored
 *  events.
 *  \param buffer Pointer to the saved state information.
 */
void GameProtocol::rewind(const ControllerActionPacket& packet)
{
    // kimden: I think the packet type is correct.
    // I will likely have to change it to shared_ptr<Packet> though.
    int kart_id = packet.kart_id;
    uint8_t w = packet.compressed_action_0;
    uint16_t x = packet.compressed_action_1;
    uint16_t y = packet.compressed_action_2;
    uint16_t z = packet.compressed_action_3;
    const auto& a = decompressAction(w, x, y, z);
    Controller *c = World::getWorld()->getKart(kart_id)->getController();
    PlayerController *pc = dynamic_cast<PlayerController*>(c);
    // This can be endcontroller when finishing the race
    if (pc)
    {
        pc->actionFromNetwork(std::get<0>(a), std::get<1>(a), std::get<2>(a),
            std::get<3>(a));
    }
}   // rewind

// ----------------------------------------------------------------------------
void GameProtocol::update(int ticks)
{
    if (!World::getWorld())
        ProtocolManager::lock()->findAndTerminate(PROTOCOL_CONTROLLER_EVENTS);
}   // update
