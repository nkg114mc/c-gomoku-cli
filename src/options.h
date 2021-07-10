/*
 *  c-gomoku-cli, a command line interface for Gomocup engines. Copyright 2021 Chao Ma.
 *  c-gomoku-cli is derived from c-chess-cli, originally authored by lucasart 2020.
 *
 *  c-gomoku-cli is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 *  c-gomoku-cli is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with this
 * program. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once
#include "position.h"
#include "sprt.h"
#include "workers.h"

#include <cinttypes>
#include <string>
#include <vector>

struct SampleParams
{
    std::string fileName;
    double      freq     = 1.0;
    bool        bin      = false;
    bool        compress = false;
};

struct Options
{
    std::string  openings, pgn, sgf, msg;
    SampleParams sp;
    SPRTParam    sprtParam   = {.elo0 = 0, .elo1 = 0, .alpha = 0.05, .beta = 0.05};
    uint64_t     srand       = 0;
    int          concurrency = 1;
    int          games = 1, rounds = 1;
    int          resignCount = 0, resignScore = 0;
    int          drawCount = 0, drawScore = 0;
    int          forceDrawAfter = 0;
    int          boardSize      = 15;
    GameRule     gameRule       = GOMOKU_FIVE_OR_MORE;
    OpeningType  openingType    = OPENING_OFFSET;
    bool         useTURN        = true;
    bool         log            = false;
    bool         random         = false;
    bool         repeat         = false;
    bool         transform      = false;
    bool         sprt           = false;
    bool         gauntlet       = false;
    bool         saveLoseOnly   = false;
    bool         debug          = false;
};

struct EngineOptions
{
    std::string              cmd, name;
    std::vector<std::string> options;

    // default time control info
    int64_t timeoutTurn = 0, timeoutMatch = 0, increment = 0;
    int64_t nodes = 0;
    int     depth = 0;

    // default thread num is 1
    int numThreads = 1;

    // default max memory is set to 350MB (same as Gomocup)
    int64_t maxMemory = 367001600;

    // default tolerance is 3
    int64_t tolerance = 3000;
};

void options_parse(int                         argc,
                   const char **               argv,
                   Options &                   o,
                   std::vector<EngineOptions> &eo);
void options_print(const Options &o, const std::vector<EngineOptions> &eo);