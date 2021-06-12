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
#ifndef __MINGW32__
    #include <sys/types.h>
#endif

#include <cinttypes>
#include <cstdbool>
#include <cstdio>
#include "str.h"
#include "workers.h"

// Elements remembered from parsing info lines (for writing PGN comments)
struct Info {
    int score, depth;
    int64_t time;
};

// Engine process
class Engine {
public:
#ifdef __MINGW32__
    long  pid;
    void* hProcess;
#else
    pid_t pid;
#endif

    FILE *in, *out;
    str_t name;
    bool isDebug;
    char pad[3];

    void engine_init(Worker *w, const char *cmd, const char *name, bool debug);
    void engine_destroy(Worker *w);

    void engine_readln(const Worker *w, str_t *line);
    void engine_writeln(const Worker *w, const char *buf);

    void engine_sync(Worker *w);
    void engine_wait_for_ok(Worker *w);
    bool engine_bestmove(Worker *w, int64_t *timeLeft, int64_t maxTurnTime, str_t *best, str_t *pv, Info *info);

    void engine_about(Worker *w, const char* fallbackName);
    // process MESSAGE, UNKNOWN, ERROR, DEBUG messages
    void engine_process_message_ifneeded(const char *line);
    void engine_parse_thinking_messages(const char *line, Info *info);
};
