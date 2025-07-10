//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2018 Joerg Henrichs
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

#ifndef PEER_VOTE_HPP
#define PEER_VOTE_HPP

#include "network/network_string.hpp"
#include "network/packet_types.hpp"

#include "irrString.h"
#include <string>

/** A simple structure to store a vote from a client:
 *  track name, number of laps and reverse or not. */
class PeerVote
{
public:
    core::stringw m_player_name;
    std::string   m_track_name;
    uint8_t       m_num_laps;
    bool          m_reverse;

    // ------------------------------------------------------
    PeerVote() : m_player_name(""), m_track_name(""),
                 m_num_laps(1), m_reverse(false)
    {
    }   // PeerVote()
    // ------------------------------------------------------
    PeerVote(const core::stringw &name,
             const std::string track,
             int laps, bool reverse) : m_player_name(name),
                                       m_track_name(track),
                                       m_num_laps(laps),
                                       m_reverse(reverse)
    {
    }   // PeerVote(name, track, laps, reverse)

    // ------------------------------------------------------
    /** Initialised this object from a data in a network string. */
    PeerVote(const PeerVotePacket& packet)
    {
        m_player_name = packet.player_name;
        m_track_name = packet.track_name;
        m_num_laps = packet.num_laps;
        m_reverse = packet.is_reverse;
    }   // PeerVote(PeerVotePacket &)

    // ------------------------------------------------------
    /** Encodes this vote object into a network string. */
    PeerVotePacket encode()
    {
        PeerVotePacket packet;
        packet.player_name = m_player_name;
        packet.track_name = m_track_name;
        packet.num_laps = m_num_laps;
        packet.is_reverse = m_reverse;
        return packet;
    }   // encode
};   // class PeerVote

#endif // PEER_VOTE_HPP
