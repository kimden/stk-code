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

#ifndef LOBBY_QUEUES_HPP
#define LOBBY_QUEUES_HPP

#include "irrString.h"
#include "utils/track_filter.hpp"
#include "utils/lobby_context.hpp"

#include <memory>
#include <queue>

class LobbyQueues: public LobbyContextComponent
{
public:
    LobbyQueues(LobbyContext* context): LobbyContextComponent(context) {}
    
    void setupContextUser() OVERRIDE;

    void loadTracksQueueFromConfig();
    
    void popOnRaceFinished();

    void resetToDefaultSettings(const std::set<std::string>& preserved_settings);

    void applyFrontMapFilters(FilterContext& context);

    void applyFrontKartFilters(FilterContext& context);

    bool areKartFiltersIgnoringKarts() const;

    using Queue = std::deque<std::shared_ptr<Filter>>;

private:
    Queue m_onetime_tracks_queue;
    Queue m_cyclic_tracks_queue;
    Queue m_onetime_karts_queue;
    Queue m_cyclic_karts_queue;

// Temporary reference getters
public:
    Queue& getOnetimeTracksQueue()           { return m_onetime_tracks_queue; }
    Queue& getCyclicTracksQueue()             { return m_cyclic_tracks_queue; }
    Queue& getOnetimeKartsQueue()             { return m_onetime_karts_queue; }
    Queue& getCyclicKartsQueue()               { return m_cyclic_karts_queue; }

};

#endif