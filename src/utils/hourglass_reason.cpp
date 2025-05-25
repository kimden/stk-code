//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2024-2025 kimden
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

#include "utils/hourglass_reason.hpp"


namespace
{
    const std::string g_hr_unknown            = "For some reason, server doesn't know about the hourglass status of player %s.";
    const std::string g_hr_none               = "%s can play (but if hourglass is present, there are not enough slots on the server).";
    const std::string g_hr_absent_peer        = "Player %s is not present on the server.";
    const std::string g_hr_not_tournament     = "%s is not a tournament player for this game.";
    const std::string g_hr_limit_spectator    = "Not enough slots to fit %s.";
    const std::string g_hr_no_karts_after     = "After applying all kart filters, %s doesn't have karts to play.";
    const std::string g_hr_no_maps_after      = "After applying all map filters, %s doesn't have maps to play.";
    const std::string g_hr_lack_required_maps = "%s lacks required maps.";
    const std::string g_hr_addon_karts_thr    = "Player %s doesn't have enough addon karts.";
    const std::string g_hr_addon_tracks_thr   = "Player %s doesn't have enough addon tracks.";
    const std::string g_hr_addon_arenas_thr   = "Player %s doesn't have enough addon arenas.";
    const std::string g_hr_addon_fields_thr   = "Player %s doesn't have enough addon fields.";
    const std::string g_hr_off_karts_thr      = "The number of official karts for %s is lower than the threshold.";
    const std::string g_hr_off_tracks_thr     = "The number of official tracks for %s is lower than the threshold.";
    const std::string g_hr_empty              = "";
}   // namespace
//-----------------------------------------------------------------------------

namespace Conversions
{
    std::string hourglassReasonToString(HourglassReason reason)
    {
        switch (reason)
        {
            case HR_UNKNOWN:                        return g_hr_unknown;
            case HR_NONE:                           return g_hr_none;
            case HR_ABSENT_PEER:                    return g_hr_absent_peer;
            case HR_NOT_A_TOURNAMENT_PLAYER:        return g_hr_not_tournament;
            case HR_SPECTATOR_BY_LIMIT:             return g_hr_limit_spectator;
            case HR_NO_KARTS_AFTER_FILTER:          return g_hr_no_karts_after;
            case HR_NO_MAPS_AFTER_FILTER:           return g_hr_no_maps_after;
            case HR_LACKING_REQUIRED_MAPS:          return g_hr_lack_required_maps;
            case HR_ADDON_KARTS_PLAY_THRESHOLD:     return g_hr_addon_karts_thr;
            case HR_ADDON_TRACKS_PLAY_THRESHOLD:    return g_hr_addon_tracks_thr;
            case HR_ADDON_ARENAS_PLAY_THRESHOLD:    return g_hr_addon_arenas_thr;
            case HR_ADDON_FIELDS_PLAY_THRESHOLD:    return g_hr_addon_fields_thr;
            case HR_OFFICIAL_KARTS_PLAY_THRESHOLD:  return g_hr_off_karts_thr;
            case HR_OFFICIAL_TRACKS_PLAY_THRESHOLD: return g_hr_off_tracks_thr;
            default:                                return g_hr_empty;
        }
    }
}