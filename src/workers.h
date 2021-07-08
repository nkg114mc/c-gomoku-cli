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
#include <mutex>
#include <cstdio>
#include <functional>
#include <vector>
#include <string>

// Game results
enum {
    RESULT_LOSS,
    RESULT_DRAW,
    RESULT_WIN,
    NB_RESULT
};

// Per thread data
class Worker {
public:
    struct Deadline_t {
        std::mutex mtx;
        int64_t timeLimit;
        std::string engineName;
        std::string description;
        std::function<void()> callback;
        bool set;
        bool called;
    };

    const int id;  // starts at 1 (0 is for main thread)
    Deadline_t deadline;
    uint64_t seed;  // seed for prng()
    FILE *log;

    Worker(int id, const char *logName);
    ~Worker();

    void deadline_set(const char *engineName, int64_t timeLimit, const char *description,
        std::function<void()> callback = nullptr);
    void deadline_clear();
    void deadline_callback_once();
    int64_t deadline_overdue();
};

