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

#ifndef BRACKET_HPP
#define BRACKET_HPP

#include "utils/types.hpp"

#include <memory>
#include <string>
#include <vector>

namespace Competitions
{
    class SomeKindOfResult;

    struct GamePlayer
    {

    };

    struct Game;

    struct PromisedGamePlayer: public GamePlayer
    {
        std::weak_ptr<Game> game;
        int index;
    };

    struct Game
    {
        std::weak_ptr<GamePlayer> players;
        
        bool tryResolveAllPlayers();
        
        std::weak_ptr<GamePlayer> evaluatePlayer(int index);
    };

    class Bracket
    {
    protected:
        std::vector<Game> m_games;
    
    public:
        virtual void accept(SomeKindOfResult) = 0;
        virtual std::vector<Game> getNextGames() = 0;
    };

}

#endif // BRACKET_HPP
