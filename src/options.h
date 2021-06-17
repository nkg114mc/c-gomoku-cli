/* 
 *  c-gomoku-cli, a command line interface for Gomocup engines. Copyright 2021 Chao Ma.
 *  c-gomoku-cli is derived from c-chess-cli, originally authored by lucasart 2020.
 *  
 *  c-gomoku-cli is free software: you can redistribute it and/or modify it under the terms of the GNU
 *  General Public License as published by the Free Software Foundation, either version 3 of the
 *  License, or (at your option) any later version.
 *  
 *  c-gomoku-cli is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 *  even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *  General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License along with this program. If
 *  not, see <http://www.gnu.org/licenses/>.
 */

#pragma once
#include <inttypes.h>
#include "workers.h"
#include "sprt.h"
#include "str.h"
#include "position.h"

struct SampleParams {
    str_t fileName;
    double freq;
    bool bin, compress;
    char pad[2];
};

struct Options {
    str_t openings, pgn, sgf, msg;
    SPRTParam sprtParam;
    uint64_t srand;
    int concurrency, games, rounds;
    int resignCount, resignScore;
    int drawCount, drawScore;
    int forceDrawAfter;
    bool log, random, repeat, sprt, gauntlet, useTURN, debug, saveLoseOnly;
    int boardSize;
    int gameRule;
    OpeningType openingType;
    SampleParams sp;
};

struct EngineOptions {
    str_t cmd, name, *options;
    int64_t timeoutTurn, timeoutMatch, increment, nodes;
    int depth, numThreads;
    int64_t maxMemory;
};

EngineOptions engine_options_init(void);
void engine_options_destroy(EngineOptions *eo);

Options options_init(void);
void options_parse(int argc, const char **argv, Options *o, EngineOptions **eo);
void options_destroy(Options *o);
void options_print(Options *o, EngineOptions **eo);