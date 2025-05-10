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

#ifndef CROWN_MANAGER_HPP
#define CROWN_MANAGER_HPP

#include "irrString.h"
#include "utils/lobby_context.hpp"
#include "utils/types.hpp"

#include <memory>
#include <set>
#include <map>
#include <string>

class STKPeer;
enum AlwaysSpectateMode : uint8_t;

class CrownManager: public LobbyContextComponent
{
public:
    CrownManager(LobbyContext* context): LobbyContextComponent(context) {}

    void setupContextUser() OVERRIDE;

    std::set<std::shared_ptr<STKPeer>>& getSpectatorsByLimit(bool update = false);

    bool canRace(std::shared_ptr<STKPeer> peer);
    bool hasOnlyHostRiding()               const { return m_only_host_riding; }
    bool isOwnerLess()                     const { return m_owner_less;       }
    bool isSleepingServer()                const { return m_sleeping_server;  }

    std::string getWhyPeerCannotPlayAsString(
            const std::shared_ptr<STKPeer>& player_peer) const;

    void setSpectateModeProperly(std::shared_ptr<STKPeer> peer, AlwaysSpectateMode mode);

    std::shared_ptr<STKPeer> getFirstInCrownOrder(
            const std::vector<std::shared_ptr<STKPeer>>& peers);

    static bool defaultCrownComparator(const std::shared_ptr<STKPeer> a,
                                const std::shared_ptr<STKPeer> b);

    static bool defaultOrderComparator(const std::shared_ptr<STKPeer> a,
                                const std::shared_ptr<STKPeer> b);

private:
    bool m_only_host_riding;
    bool m_owner_less;
    bool m_sleeping_server;

    std::set<std::shared_ptr<STKPeer>> m_spectators_by_limit;

    std::map<std::shared_ptr<STKPeer>, int> m_why_peer_cannot_play;

};

#endif // CROWN_MANAGER_HPP
