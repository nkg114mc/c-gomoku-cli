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
#include <pthread.h>
#include <stdbool.h>
#include "str.h"

// Result for each pair (e1, e2); e1 < e2. Stores count of game outcomes from e1's point of view.
struct Result {
    int ei[2];
    int count[3];
};

// Job: instruction to play a single game
class Job {
public:
    int ei[2], pair;  // ei[0] plays ei[1]
    int round, game;  // round and game number (start at 0)
    bool reverse;  // if true, e1 plays second
};

// Job Queue: consumed by workers to play tournament (thread safe)
class JobQueue {
public:
    pthread_mutex_t mtx;
    Job *jobs;
    size_t idx;  // next job index
    size_t completed;  // number of jobs completed
    str_t *names;
    Result *results;

    void job_queue_init(int engines, int rounds, int games, bool gauntlet);
    void job_queue_destroy();

    bool job_queue_pop(Job *j, size_t *idx, size_t *count);
    void job_queue_add_result(int pair, int outcome, int count[3]);
    bool job_queue_done();
    void job_queue_stop();

    void job_queue_set_name(int ei, const char *name);
    void job_queue_print_results(size_t frequency);
};


