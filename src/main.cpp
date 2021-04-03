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

#include <iostream>
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include "engine.h"
#include "game.h"
#include "jobs.h"
#include "openings.h"
#include "options.h"
#include "seqwriter.h"
#include "sprt.h"
#include "util.h"
#include "vec.h"
#include "workers.h"

static Options options;
static EngineOptions *eo;
static Openings openings;
static SeqWriter pgnSeqWriter;
static SeqWriter sgfSeqWriter;
static JobQueue jq;

static void main_destroy(void)
{
    for (int i = 0; i < Workers.size(); i++) {
        Workers[i].worker_destroy();
    }
    Workers.clear();

    if (options.pgn.len)
        pgnSeqWriter.seq_writer_destroy();

    if (options.sgf.len)
        sgfSeqWriter.seq_writer_destroy();

    openings.openings_destroy(0);
    jq.job_queue_destroy();
    options_destroy(&options);
    vec_destroy_rec(eo, engine_options_destroy);
}

static void main_init(int argc, const char **argv)
{
    atexit(main_destroy);

    initZobrish();

    eo = vec_init(EngineOptions);
    options = options_init();
    options_parse(argc, argv, &options, &eo);

    jq.job_queue_init(vec_size(eo), options.rounds, options.games, options.gauntlet);
    openings.openings_init(options.openings.buf, options.random, options.srand, 0);

    if (options.pgn.len) {
        pgnSeqWriter.seq_writer_init(options.pgn.buf, "ae");
    }

    if (options.sgf.len) {
        sgfSeqWriter.seq_writer_init(options.sgf.buf, "ae");
    }

    // Prepare Workers[]
    for (int i = 0; i < options.concurrency; i++) {
        scope(str_destroy) str_t logName = str_init();

        if (options.log) {
            str_cat_fmt(&logName, "c-gomoku-cli.%i.log", i + 1);
        }

        Worker wker;
        wker.worker_init(i, logName.buf);
        Workers.push_back(wker);
    }
}

static void *thread_start(void *arg)
{
    Worker *w = (Worker*)arg;
    Engine engines[2] = {0};

    scope(str_destroy) str_t fen = str_init();
    Job job = {0};
    int ei[2] = {-1, -1};  // eo[ei[0]] plays eo[ei[1]]: initialize with invalid values to start
    size_t idx = 0, count = 0;  // game idx and count (shared across workers)

    while (jq.job_queue_pop(&job, &idx, &count)) {
        // Engine stop/start, as needed
        for (int i = 0; i < 2; i++) {
            if (job.ei[i] != ei[i]) {
                if (engines[i].pid) {
                    engines[i].engine_destroy(w);
                }

                ei[i] = job.ei[i];
                engines[i].engine_init(w, eo[ei[i]].cmd.buf, eo[ei[i]].name.buf, eo[ei[i]].options, options.debug);
                jq.job_queue_set_name(ei[i], engines[i].name.buf);
            }
        }
    
        // Choose opening position
        openings.openings_next(&fen, options.repeat ? idx / 2 : idx, w->id);

        // Play 1 game
        Game game;
        game.game_init(job.round, job.game);
        int color = BLACK; // black play first in gomoku/renju by default

        if (!game.game_load_fen(&fen, &color, &options)) {
            DIE("[%d] illegal FEN '%s'\n", w->id, fen.buf);
        }

        const int blackIdx = color ^ job.reverse;

        printf("[%d] Started game %zu of %zu (%s vs %s)\n", w->id, idx + 1, count,
            engines[blackIdx].name.buf, engines[oppositeColor((Color)blackIdx)].name.buf);

        const EngineOptions *eoPair[2] = {&eo[ei[0]], &eo[ei[1]]};
        const int wld = game.game_play(w, &options, engines, eoPair, job.reverse);

        // Write to PGN file
        if (options.pgn.len) {
            int pgnVerbosity = 3;
            scope(str_destroy) str_t pgnText = str_init();
            game.game_export_pgn(pgnVerbosity, &pgnText);
            pgnSeqWriter.seq_writer_push(idx, pgnText);
        }

        // Write to SGF file
        if (options.sgf.len) {
            scope(str_destroy) str_t sgfText = str_init();
            game.game_export_sgf(&sgfText);
            sgfSeqWriter.seq_writer_push(idx, sgfText);
        }

        // Write to stdout a one line summary of the game
        scope(str_destroy) str_t result = str_init(), reason = str_init();
        game.game_decode_state(&result, &reason);

        printf("[%d] Finished game %zu (%s vs %s): %s {%s}\n", w->id, idx + 1,
            engines[blackIdx].name.buf, engines[oppositeColor((Color)blackIdx)].name.buf, result.buf, reason.buf);

        // Pair update
        int wldCount[3] = {0};
        jq.job_queue_add_result(job.pair, wld, wldCount);
        const int n = wldCount[RESULT_WIN] + wldCount[RESULT_LOSS] + wldCount[RESULT_DRAW];
        printf("Score of %s vs %s: %d - %d - %d  [%.3f] %d\n", engines[0].name.buf,
            engines[1].name.buf, wldCount[RESULT_WIN], wldCount[RESULT_LOSS], wldCount[RESULT_DRAW],
            (wldCount[RESULT_WIN] + 0.5 * wldCount[RESULT_DRAW]) / n, n);

        // SPRT update
        if (options.sprt && sprt_done(wldCount, &options.sprtParam)) {
            jq.job_queue_stop();
        }

        // Tournament update
        if (vec_size(eo) > 2) {
            jq.job_queue_print_results((size_t)options.games);
        }

        game.game_destroy();
    }

    for (int i = 0; i < 2; i++) {
        engines[i].engine_destroy(w);
    }

    return NULL;
}

int main(int argc, const char **argv)
{
    main_init(argc, argv);

    // Start threads[]
    pthread_t threads[options.concurrency];

    for (int i = 0; i < options.concurrency; i++) {
        pthread_create(&threads[i], NULL, thread_start, &Workers[i]);
    }

    // Main thread loop: check deadline overdue at regular intervals
    do {
        system_sleep(100);

        // We want some tolerance on small delays here. Given a choice, it's best to wait for the
        // worker thread to notice an overdue deadline, which it will handled nicely by counting the
        // game as lost for the offending engine, and continue. Enforcing deadlines from the master
        // thread is the last resort solution, because it is an unrecovrable error. At this point we
        // are likely to face a completely unresponsive engine, where any attempt at I/O will block
        // the master thread, on top of the already blocked worker. Hence, we must DIE().
        for (int i = 0; i < options.concurrency; i++) {
            if (Workers[i].deadline_overdue() > 1000) {
                DIE("[%d] engine %s is unresponsive\n", Workers[i].id,
                    Workers[i].deadline.engineName.buf);
            }
        }
    } while (!jq.job_queue_done());

    // Join threads[]
    for (int i = 0; i < options.concurrency; i++) {
        pthread_join(threads[i], NULL);
    }

    return 0;
}
