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
#include <string>
#include "str.h"

class Worker;

// Elements remembered from parsing info lines (for writing PGN comments)
struct Info {
    int score, depth;
    int64_t time;
};

// Engine process
class Engine {
public:
    str_t name;

    Engine(Worker *worker, bool debug);
    ~Engine();

    void init(const char *cmd, const char *name, int64_t tolerance, str_t *outmsg);
    void destroy();

    bool readln(str_t *line);
    void writeln(const char *buf);

    void wait_for_ok();
    bool bestmove(int64_t *timeLeft, int64_t maxTurnTime, str_t *best, Info *info, int moveply);

    bool is_crashed() const;

private:
    Worker *const w;
    const bool isDebug;

#ifdef __MINGW32__
    long  pid;
    void* hProcess;
#else
    pid_t pid;
#endif

    FILE *in, *out;
    str_t *messages;
    int64_t tolerance;

    void spawn(const char *cwd, const char *run, char **argv, bool readStdErr);
    void parse_about(const char* fallbackName);
    // process MESSAGE, UNKNOWN, ERROR, DEBUG messages
    void process_message_ifneeded(const char *line);
    void parse_thinking_messages(const char *line, Info *info);
};
