//  SuperTuxKart - a fun racing game with go-kart
//
//  Copyright (C) 2024 kimden
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

#ifndef HEADER_RANKING_HPP
#define HEADER_RANKING_HPP

#include <vector>
#include <algorithm>
#include <bitset>
#include <cmath>
#include "utils/log.hpp"
#include "utils/types.hpp"

// I am sorry for not putting m_ before all the variable names

/* Ranking related variables */
// If updating the base points, update the base points distribution in DB
const double BASE_RANKING_POINTS    = 4000.0; // Given to a new player on 1st connection to a ranked server
const double BASE_RATING_DEVIATION  = 1000.0; // Given to a new player on 1st connection to a ranked server
const double MIN_RATING_DEVIATION   = 100.0; // A server cron job makes RD go up if a player is inactive
const double BASE_RD_PER_DISCONNECT = 15.0;
const double VAR_RD_PER_DISCONNECT  = 3.0;
const double MAX_SCALING_TIME       = 360.0;
const double BASE_POINTS_PER_SECOND = 0.18;
const double HANDICAP_OFFSET        = 2000.0;

struct RankingPartialState {
    unsigned player_count;
    std::vector<int> ids;
    std::vector<double> raw_scores;
    std::vector<double> shown_scores;
    std::vector<double> deviations;
    std::vector<uint64_t> masks;
    std::vector<unsigned> num_games;
    std::vector<double> max_scores;
    RankingPartialState(unsigned player_count, std::vector<int>& ids,
                        std::vector<double>& raw_scores,
                        std::vector<double>& shown_scores,
                        std::vector<double>& deviations,
                        std::vector<uint64_t>& masks,
                        std::vector<unsigned>& num_games,
                        std::vector<double>& max_scores):
        player_count(player_count),
        ids(ids),
        raw_scores(raw_scores),
        shown_scores(shown_scores),
        deviations(deviations),
        masks(masks),
        num_games(num_games),
        max_scores(max_scores)
        {}
};

struct RaceResult {
    bool is_time_trial;
    unsigned player_count;
    std::vector<int> ids;
    std::vector<bool> is_eliminated;
    std::vector<bool> is_handicapped;
    std::vector<double> race_times;
    RaceResult(bool is_time_trial, unsigned player_count,
                        std::vector<int>& ids,
                        std::vector<bool>& is_eliminated,
                        std::vector<bool>& is_handicapped,
                        std::vector<double>& race_times):
        is_time_trial(is_time_trial),
        player_count(player_count),
        ids(ids),
        is_eliminated(is_eliminated),
        is_handicapped(is_handicapped),
        race_times(race_times) {}
};

void updateRankingState(RankingPartialState& state, const RaceResult& result);
double getModeFactor(bool is_time_trial);
double getModeSpread(bool is_time_trial);
double getTimeSpread(double time);
double getUncertaintySpread(uint32_t online_id);
double scalingValueForTime(double time);
double computeH2HResult(double player1_time, double player2_time);
double computeDataAccuracy(double player1_rd, double player2_rd,
                            double player1_scores, double player2_scores,
                            int player_count, bool handicap_used);


#endif

