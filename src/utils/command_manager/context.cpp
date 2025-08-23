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

#include "utils/command_manager/context.hpp"
#include "utils/communication.hpp"
#include "utils/command_manager/command.hpp"
#include "utils/string_utils.hpp"
#include "utils/log.hpp"

std::shared_ptr<STKPeer> Context::peer()
{
    if (m_peer.expired())
        throw std::logic_error("Peer is expired");

    auto peer = m_peer.lock();
    if (!peer)
        throw std::logic_error("Peer is invalid");

    return peer;
}   // peer
//-----------------------------------------------------------------------------

std::shared_ptr<STKPeer> Context::peerMaybeNull()
{
    // if (m_peer.expired())
    //     throw std::logic_error("Peer is expired");

    auto peer = m_peer.lock();
    return peer;
}   // peerMaybeNull
//-----------------------------------------------------------------------------

std::shared_ptr<STKPeer> Context::actingPeer()
{
    if (m_target_peer.expired())
        throw std::logic_error("Target peer is expired");

    auto acting_peer = m_target_peer.lock();
    if (!acting_peer)
        throw std::logic_error("Target peer is invalid");

    return acting_peer;
}   // actingPeer
//-----------------------------------------------------------------------------

std::shared_ptr<STKPeer> Context::actingPeerMaybeNull()
{
    // if (m_target_peer.expired())
    //     throw std::logic_error("Target peer is expired");

    auto acting_peer = m_target_peer.lock();
    return acting_peer;
}   // actingPeerMaybeNull
//-----------------------------------------------------------------------------

std::shared_ptr<Command> Context::command()
{
    if (m_command.expired())
        throw std::logic_error("Command is expired");

    auto command = m_command.lock();
    if (!command)
        throw std::logic_error("Command is invalid");

    return command;
}   // command
//-----------------------------------------------------------------------------

void Context::say(const std::string& s)
{
    if (m_peer.expired())
        throw std::logic_error("Context::say: Peer has expired");

    auto peer = m_peer.lock();
    Comm::sendStringToPeer(peer, s);
}   // say
//-----------------------------------------------------------------------------

void Context::error(bool is_error)
{
    std::string msg;
    if (is_error)
        Log::error("CMContext", "An error occurred while invoking %s", m_cmd.c_str());

    auto command = m_command.lock();
    auto peer = m_peer.lock();
    if (!command) {
        Log::error("CMContext", "CM::error: cannot load command");
        return;
    }
    if (!peer) {
        Log::error("CMContext", "CM::error: cannot load peer to send error");
        return;
    }
    msg = command->getUsage();
    if (msg.empty())
        msg = StringUtils::insertValues("An error occurred "
                "while invoking command \"%s\".",
                command->getFullName().c_str());

    if (is_error)
        msg += "\n/!\\ Please report this error to the server owner";

    say(msg);
}   // error
//-----------------------------------------------------------------------------