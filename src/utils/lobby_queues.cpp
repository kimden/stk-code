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

#include "utils/lobby_queues.hpp"

#include "network/server_config.hpp"
#include "utils/string_utils.hpp"

void LobbyQueues::setupContextUser()
{
    loadTracksQueueFromConfig();
}   // setupContextUser
//-----------------------------------------------------------------------------

void LobbyQueues::loadTracksQueueFromConfig()
{
    std::vector<std::string> tokens;
    m_onetime_tracks_queue.clear();
    m_cyclic_tracks_queue.clear();
    m_onetime_karts_queue.clear();
    m_cyclic_karts_queue.clear();

    tokens = StringUtils::splitQuoted(
            ServerConfig::m_tracks_order, ' ', '{', '}', '\\');
    for (std::string& s: tokens)
    {
        m_onetime_tracks_queue.push_back(std::make_shared<TrackFilter>(s));
        m_cyclic_tracks_queue.push_back(
            std::make_shared<TrackFilter>(TrackFilter::PLACEHOLDER_STRING));
    }

    tokens = StringUtils::splitQuoted(
            ServerConfig::m_cyclic_tracks_order, ' ', '{', '}', '\\');
    for (std::string& s: tokens)
        m_cyclic_tracks_queue.push_back(std::make_shared<TrackFilter>(s));


    tokens = StringUtils::splitQuoted(
            ServerConfig::m_karts_order, ' ', '{', '}', '\\');
    for (std::string& s: tokens)
    {
        m_onetime_karts_queue.push_back(std::make_shared<KartFilter>(s));
        m_cyclic_karts_queue.push_back(
            std::make_shared<KartFilter>(KartFilter::PLACEHOLDER_STRING));
    }

    tokens = StringUtils::splitQuoted(
            ServerConfig::m_cyclic_karts_order, ' ', '{', '}', '\\');
    for (std::string& s: tokens)
        m_cyclic_karts_queue.push_back(std::make_shared<KartFilter>(s));
}   // loadTracksQueueFromConfig
//-----------------------------------------------------------------------------

void LobbyQueues::popOnRaceFinished()
{
    if (!m_onetime_tracks_queue.empty())
        m_onetime_tracks_queue.pop_front();

    if (!m_cyclic_tracks_queue.empty())
    {
        auto item = m_cyclic_tracks_queue.front().get()->getInitialString();
        bool is_placeholder = m_cyclic_tracks_queue.front()->isPlaceholder();
        m_cyclic_tracks_queue.pop_front();
        if (!is_placeholder)
            m_cyclic_tracks_queue.push_back(std::make_shared<TrackFilter>(item));
    }

    if (!m_onetime_karts_queue.empty())
        m_onetime_karts_queue.pop_front();

    if (!m_cyclic_karts_queue.empty())
    {
        auto item = m_cyclic_karts_queue.front().get()->getInitialString();
        bool is_placeholder = m_cyclic_karts_queue.front()->isPlaceholder();
        m_cyclic_karts_queue.pop_front();
        if (!is_placeholder)
            m_cyclic_karts_queue.push_back(std::make_shared<KartFilter>(item));
    }
}   // popOnRaceFinished
//-----------------------------------------------------------------------------

void LobbyQueues::resetToDefaultSettings(const std::set<std::string>& preserved_settings)
{
    if (!preserved_settings.count("queue"))
        m_onetime_tracks_queue.clear();

    if (!preserved_settings.count("qcyclic"))
        m_cyclic_tracks_queue.clear();

    if (!preserved_settings.count("kqueue"))
        m_onetime_karts_queue.clear();

    if (!preserved_settings.count("kcyclic"))
        m_cyclic_karts_queue.clear();
}   // resetToDefaultSettings
//-----------------------------------------------------------------------------

void LobbyQueues::applyFrontMapFilters(FilterContext& context)
{
    if (!m_onetime_tracks_queue.empty())
    {
        m_onetime_tracks_queue.front()->apply(context);
    }
    if (!m_cyclic_tracks_queue.empty())
    {
        m_cyclic_tracks_queue.front()->apply(context);
    }
}   // applyFrontMapFilters
//-----------------------------------------------------------------------------

void LobbyQueues::applyFrontKartFilters(FilterContext& context)
{
    if (!m_onetime_karts_queue.empty())
    {
        m_onetime_karts_queue.front()->apply(context);
    }
    if (!m_cyclic_karts_queue.empty())
    {
        m_cyclic_karts_queue.front()->apply(context);
    }
}   // applyFrontKartFilters
//-----------------------------------------------------------------------------

bool LobbyQueues::areKartFiltersIgnoringKarts() const
{
    if (!m_onetime_karts_queue.empty() &&
            m_onetime_karts_queue.front()->ignoresPlayersInput())
        return true;

    if (!m_cyclic_karts_queue.empty() &&
            m_cyclic_karts_queue.front()->ignoresPlayersInput())
        return true;

    return false;
}   // areKartFiltersIgnoringKarts
//-----------------------------------------------------------------------------
