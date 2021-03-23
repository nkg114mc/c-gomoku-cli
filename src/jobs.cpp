/*
 * c-chess-cli, a command line interface for UCI chess engines. Copyright 2020 lucasart.
 *
 * c-chess-cli is free software: you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * c-chess-cli is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program. If
 * not, see <http://www.gnu.org/licenses/>.
*/
#include "jobs.h"
#include "vec.h"
#include "workers.h"
#include <stdio.h>

static void job_queue_init_pair(int games, int e1, int e2, int pair, int *added, int round, Job **jobs)
{
    for (int g = 0; g < games; g++) {
        const Job j = {
            .ei = {e1, e2},
            .pair = pair,
            .round = round, .game = (*added)++,
            .reverse = (bool)(g % 2)
        };
        vec_push(*jobs, j, Job);
    }
}

void JobQueue::job_queue_init(int engines, int rounds, int games, bool gauntlet)
{
    assert(engines >= 2 && rounds >= 1 && games >= 1);

    pthread_mutex_init(&mtx, NULL);

    jobs = vec_init(Job);
    results = vec_init(Result);
    names = vec_init(str_t);

    // Prepare engine names: blank for now, will be discovered at run time (concurrently)
    for (int i = 0; i < engines; i++)
        vec_push(names, str_init(), str_t);

    if (gauntlet) {
        // Gauntlet: N-1 pairs (0, e2) with 0 < e2
        for (int e2 = 1; e2 < engines; e2++) {
            const Result r = {.ei = {0, e2}, .count = {0}, {0}};
            vec_push(results, r, Result);
        }

        for (int r = 0; r < rounds; r++) {
            int added = 0;  // number of games already added to the current round

            for (int e2 = 1; e2 < engines; e2++)
                job_queue_init_pair(games, 0, e2, e2 - 1, &added, r, &jobs);
        }
    } else {
        // Round robin: N(N-1)/2 pairs (e1, e2) with e1 < e2
        for (int e1 = 0; e1 < engines - 1; e1++)
            for (int e2 = e1 + 1; e2 < engines; e2++) {
                const Result r = {.ei = {e1, e2}, .count = {0}, {0}};
                vec_push(results, r, Result);
            }

        for (int r = 0; r < rounds; r++) {
            int pair = 0;  // enumerate pairs in order
            int added = 0;  // number of games already added to the current round

            for (int e1 = 0; e1 < engines - 1; e1++)
                for (int e2 = e1 + 1; e2 < engines; e2++)
                    job_queue_init_pair(games, e1, e2, pair++, &added, r, &jobs);
        }
    }
}

void JobQueue::job_queue_destroy()
{
    vec_destroy(results);
    vec_destroy(jobs);
    vec_destroy_rec(names, str_destroy);
    pthread_mutex_destroy(&mtx);
}

bool JobQueue::job_queue_pop(Job *j, size_t *idx_in, size_t *count)
{
    pthread_mutex_lock(&mtx);
    const bool ok = this->idx < vec_size(this->jobs);

    if (ok) {
        *j = this->jobs[this->idx];
        *idx_in = this->idx++;
        *count = vec_size(this->jobs);
    }

    pthread_mutex_unlock(&mtx);
    return ok;
}

// Add game outcome, and return updated totals
void JobQueue::job_queue_add_result(int pair, int outcome, int count[3])
{
    pthread_mutex_lock(&mtx);
    results[pair].count[outcome]++;
    completed++;

    for (size_t i = 0; i < 3; i++)
        count[i] = results[pair].count[i];

    pthread_mutex_unlock(&mtx);
}

bool JobQueue::job_queue_done()
{
    pthread_mutex_lock(&mtx);
    assert(idx <= vec_size(jobs));
    const bool done = idx == vec_size(jobs);
    pthread_mutex_unlock(&mtx);
    return done;
}

void JobQueue::job_queue_stop()
{
    pthread_mutex_lock(&mtx);
    idx = vec_size(jobs);
    pthread_mutex_unlock(&mtx);
}

void JobQueue::job_queue_set_name(int ei, const char *name)
{
    pthread_mutex_lock(&mtx);

    if (!names[ei].len)
        str_cpy_c(&names[ei], name);

    pthread_mutex_unlock(&mtx);
}

void JobQueue::job_queue_print_results(size_t frequency)
{
    pthread_mutex_lock(&mtx);

    if (completed && completed % frequency == 0) {
        scope(str_destroy) str_t out = str_init_from_c("Tournament update:\n");

        for (size_t i = 0; i < vec_size(results); i++) {
            const Result r = results[i];
            const int n = r.count[RESULT_WIN] + r.count[RESULT_LOSS] + r.count[RESULT_DRAW];

            if (n) {
                char score[8] = "";
                sprintf(score, "%.3f", (r.count[RESULT_WIN] + 0.5 * r.count[RESULT_DRAW]) / n);
                str_cat_fmt(&out, "%S vs %S: %i - %i - %i  [%s] %i\n", names[r.ei[0]],
                    names[r.ei[1]], r.count[RESULT_WIN], r.count[RESULT_LOSS],
                    r.count[RESULT_DRAW], score, n);
            }
        }

        fputs(out.buf, stdout);
    }

    pthread_mutex_unlock(&mtx);
}
