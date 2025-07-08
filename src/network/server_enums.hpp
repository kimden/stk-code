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

#ifndef SERVER_ENUMS_HPP
#define SERVER_ENUMS_HPP

/* The state for a small finite state machine. */
enum ServerPlayState : unsigned int
{
    WAITING_FOR_START_GAME,   // In lobby, waiting for (auto) start game
    SELECTING,                // kart, track, ... selection started
    LOAD_WORLD,               // Server starts loading world
    WAIT_FOR_WORLD_LOADED,    // Wait for clients and server to load world
    WAIT_FOR_RACE_STARTED,    // Wait for all clients to have started the race
    RACING,                   // racing
    WAIT_FOR_RACE_STOPPED,    // Wait server for stopping all race protocols
    RESULT_DISPLAY,           // Show result screen
};

enum ServerInitState : unsigned int
{
    SET_PUBLIC_ADDRESS,       // Waiting to receive its public ip address
    REGISTER_SELF_ADDRESS,    // Register with STK online server
    RUNNING,                  // Normal functioning
    ERROR_LEAVE,              // shutting down server
    EXITING,
};

enum SelectionPhase: unsigned int
{
    BEFORE_SELECTION = 0,
    LOADING_WORLD = 1,
    AFTER_GAME = 2,
};

/* The state used in multiple threads when reseting server. */
enum ResetState : unsigned int
{
    RS_NONE,       // Default state
    RS_WAITING,    // Waiting for reseting finished
    RS_ASYNC_RESET // Finished reseting server in main thread, now async thread
};

#endif // SERVER_ENUMS_HPP