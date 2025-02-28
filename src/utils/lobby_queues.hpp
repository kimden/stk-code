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

#include <queue>
#include <memory>
#include "utils/track_filter.hpp"

class LobbyQueues
{
public:
    LobbyQueues();

    void loadTracksQueueFromConfig();
    
    void popOnRaceFinished();

    void resetToDefaultSettings(const std::set<std::string>& preserved_settings);

    void applyFrontMapFilters(FilterContext& context);

    void applyFrontKartFilters(FilterContext& context);

    bool areKartFiltersIgnoringKarts() const;

private:
    std::deque<std::shared_ptr<Filter>> m_onetime_tracks_queue;

    std::deque<std::shared_ptr<Filter>> m_cyclic_tracks_queue;

    std::deque<std::shared_ptr<Filter>> m_onetime_karts_queue;

    std::deque<std::shared_ptr<Filter>> m_cyclic_karts_queue;

// Temporary reference getters
public:
    std::deque<std::shared_ptr<Filter>>& getOnetimeTracksQueue() { return m_onetime_tracks_queue; }
    std::deque<std::shared_ptr<Filter>>& getCyclicTracksQueue() { return m_cyclic_tracks_queue; }
    std::deque<std::shared_ptr<Filter>>& getOnetimeKartsQueue() { return m_onetime_karts_queue; }
    std::deque<std::shared_ptr<Filter>>& getCyclicKartsQueue() { return m_cyclic_karts_queue; }

};

#endif