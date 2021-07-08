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
#include <thread>
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
#include "extern/lz4frame.h"

static Options options;
static EngineOptions *eo;
static Openings *openings;
static JobQueue *jq;
static SeqWriter *pgnSeqWriter;
static SeqWriter *sgfSeqWriter;
static SeqWriter *msgSeqWriter;
static std::vector<Worker*> workers;
static FILE *sampleFile;
static LZ4F_compressionContext_t sampleFileLz4Ctx;

static void main_destroy(void)
{
    for (Worker* worker : workers)
        delete worker;
    workers.clear();

    if (options.sp.fileName.len) {
        if (options.sp.compress) {
            // Flush LZ4 tails and release LZ4 context
            const size_t bufSize = LZ4F_compressBound(0, nullptr);
            char buf[bufSize];
            size_t size = LZ4F_compressEnd(sampleFileLz4Ctx, buf, bufSize, nullptr);
            fwrite(buf, 1, size, sampleFile);
            LZ4F_freeCompressionContext(sampleFileLz4Ctx);
        }
        fclose(sampleFile);
    }

    if (pgnSeqWriter)
        delete pgnSeqWriter;
    if (sgfSeqWriter)
        delete sgfSeqWriter;
    if (msgSeqWriter)
        delete msgSeqWriter;

    delete openings;
    delete jq;
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

    jq = new JobQueue(vec_size(eo), options.rounds, options.games, options.gauntlet);
    openings = new Openings(options.openings.buf, options.random, options.srand);

    if (options.pgn.len)
        pgnSeqWriter = new SeqWriter(options.pgn.buf, FOPEN_APPEND_MODE);

    if (options.sgf.len)
        sgfSeqWriter = new SeqWriter(options.sgf.buf, FOPEN_APPEND_MODE);

    if (options.msg.len)
        msgSeqWriter = new SeqWriter(options.msg.buf, FOPEN_APPEND_MODE);

    if (options.sp.fileName.len) {
        if (options.sp.compress) {
            DIE_IF(0, !(sampleFile = fopen(options.sp.fileName.buf, FOPEN_WRITE_BINARY_MODE)));
            // Init LZ4 context and write file headers
            DIE_IF(0, LZ4F_isError(LZ4F_createCompressionContext(&sampleFileLz4Ctx, LZ4F_VERSION)));
            char buf[LZ4F_HEADER_SIZE_MAX];
            size_t headerSize = LZ4F_compressBegin(sampleFileLz4Ctx, buf, sizeof(buf), nullptr);
            fwrite(buf, sizeof(char), headerSize, sampleFile);
        } else if (options.sp.bin) {
            DIE_IF(0, !(sampleFile = fopen(options.sp.fileName.buf, FOPEN_APPEND_BINARY_MODE)));
        } else {
            DIE_IF(0, !(sampleFile = fopen(options.sp.fileName.buf, FOPEN_APPEND_MODE)));
        }
    }

    // Prepare Workers[]
    for (int i = 0; i < options.concurrency; i++) {
        scope(str_destroy) str_t logName = str_init();

        if (options.log) {
            str_cat_fmt(&logName, "c-gomoku-cli.%i.log", i + 1);
        }

        workers.push_back(new Worker (i, logName.buf));
    }
}

static void thread_start(void *arg)
{
    Worker *w = (Worker*)arg;
    Engine engines[2] = {{w, options.debug}, {w, options.debug}};

    scope(str_destroy) str_t fen = str_init();
    scope(str_destroy) str_t messages = str_init();
    str_t *msg = options.msg.len ? &messages : nullptr;
    Job job = {0};
    int ei[2] = {-1, -1};  // eo[ei[0]] plays eo[ei[1]]: initialize with invalid values to start
    size_t idx = 0, count = 0;  // game idx and count (shared across workers)

    while (jq->pop(&job, &idx, &count)) {
        // Clear all previous engine messages and write game index
        if (msg) {
            str_cpy_c(msg, "------------------------------\n");
            str_cat_fmt(msg, "Game ID: %I\n", idx + 1);
        }

        // Engine stop/start, as needed
        for (int i = 0; i < 2; i++) {
            if (job.ei[i] != ei[i]) {
                ei[i] = job.ei[i];
                engines[i].destroy();
                engines[i].init(eo[ei[i]].cmd.buf, eo[ei[i]].name.buf, eo[ei[i]].tolerance, msg);
                jq->set_name(ei[i], engines[i].name.buf);
            } 
            // Re-init engine if it crashed previously
            else if (engines[i].is_crashed()) {
                engines[i].destroy();
                engines[i].init(eo[ei[i]].cmd.buf, eo[ei[i]].name.buf, eo[ei[i]].tolerance, msg);
            }
        }
    
        // Choose opening position
        size_t openingRound = openings->next(&fen, options.repeat ? idx / 2 : idx, w->id);

        // Play 1 game
        Game game(job.round, job.game);
        int color = BLACK; // black play first in gomoku/renju by default

        if (!game.load_fen(&fen, &color, &options, openingRound)) {
            DIE("[%d] illegal FEN '%s'\n", w->id, fen.buf);
        }

        const int blackIdx = color ^ job.reverse;
        const int whiteIdx = oppositeColor((Color)blackIdx);

        printf("[%d] Started game %zu of %zu (%s vs %s)\n", w->id, idx + 1, count,
            engines[blackIdx].name.buf, engines[whiteIdx].name.buf);

        if (msg)
            str_cat_fmt(msg, "Engines: %S x %S\n", engines[blackIdx].name, engines[whiteIdx].name);

        const EngineOptions *eoPair[2] = {&eo[ei[0]], &eo[ei[1]]};
        const int wld = game.play(w, &options, engines, eoPair, job.reverse);

        if (!options.gauntlet || !options.saveLoseOnly || wld == RESULT_LOSS) {
            // Write to PGN file
            if (pgnSeqWriter) {
                const int pgnVerbosity = 0;
                scope(str_destroy) str_t pgnText = str_init();
                game.export_pgn(idx + 1, pgnVerbosity, &pgnText);
                pgnSeqWriter->push(idx, pgnText);
            }

            // Write to SGF file
            if (sgfSeqWriter) {
                scope(str_destroy) str_t sgfText = str_init();
                game.export_sgf(idx + 1, &sgfText);
                sgfSeqWriter->push(idx, sgfText);
            }

            // Write engine messages to TXT file
            if (msgSeqWriter)
                msgSeqWriter->push(idx, messages);

            // Write to Sample file
            if (options.sp.fileName.len)
                game.export_samples(sampleFile, options.sp.bin, sampleFileLz4Ctx);
        }

        // Write to stdout a one line summary of the game
        const char* ResultTxt[3] = { "0-1", "1/2-1/2", "1-0" }; // Black-White
        scope(str_destroy) str_t result = str_init(), reason = str_init();
        game.decode_state(&result, &reason, ResultTxt);

        printf("[%d] Finished game %zu (%s vs %s): %s {%s}\n", w->id, idx + 1,
            engines[blackIdx].name.buf, engines[whiteIdx].name.buf, result.buf, reason.buf);

        // Pair update
        int wldCount[3] = {0};
        jq->add_result(job.pair, wld, wldCount);
        const int n = wldCount[RESULT_WIN] + wldCount[RESULT_LOSS] + wldCount[RESULT_DRAW];
        printf("Score of %s vs %s: %d - %d - %d  [%.3f] %d\n", engines[0].name.buf,
            engines[1].name.buf, wldCount[RESULT_WIN], wldCount[RESULT_LOSS], wldCount[RESULT_DRAW],
            (wldCount[RESULT_WIN] + 0.5 * wldCount[RESULT_DRAW]) / n, n);

        // SPRT update
        if (options.sprt && sprt_done(wldCount, &options.sprtParam)) {
            jq->stop();
        }

        // Tournament update
        if (vec_size(eo) > 2) {
            jq->print_results((size_t)options.games);
        }
    }

    for (int i = 0; i < 2; i++) {
        engines[i].destroy();
    }
}

int main(int argc, const char **argv)
{
    main_init(argc, argv);

    // Start threads[]
    std::vector<std::thread> threads;

    for (int i = 0; i < options.concurrency; i++) {
        threads.emplace_back(thread_start, workers[i]);
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
            int64_t overdue = workers[i]->deadline_overdue();
            if (overdue > 0) {
                workers[i]->deadline_callback_once();
            } else if (overdue > 1000) {
                DIE("[%d] engine %s is unresponsive to [%s]\n", workers[i]->id,
                    workers[i]->deadline.engineName.c_str(), workers[i]->deadline.description.c_str());
            }
        }
    } while (!jq->done());

    // Join threads[]
    for (std::thread& th : threads) {
        th.join();
    }

    return 0;
}
