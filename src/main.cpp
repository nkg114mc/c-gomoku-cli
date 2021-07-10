/*
 *  c-gomoku-cli, a command line interface for Gomocup engines. Copyright 2021
 * Chao Ma. c-gomoku-cli is derived from c-chess-cli, originally authored by
 * lucasart 2020.
 *
 *  c-gomoku-cli is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 *  c-gomoku-cli is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 *  You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "engine.h"
#include "extern/lz4frame.h"
#include "game.h"
#include "jobs.h"
#include "openings.h"
#include "options.h"
#include "seqwriter.h"
#include "sprt.h"
#include "util.h"
#include "workers.h"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <thread>

static Options                    options;
static std::vector<EngineOptions> eo;
static Openings *                 openings;
static JobQueue *                 jq;
static SeqWriter *                pgnSeqWriter;
static SeqWriter *                sgfSeqWriter;
static SeqWriter *                msgSeqWriter;
static std::vector<Worker *>      workers;
static FILE *                     sampleFile;
static LZ4F_compressionContext_t  sampleFileLz4Ctx;

static void main_destroy(void)
{
    for (Worker *worker : workers)
        delete worker;
    workers.clear();

    if (sampleFile) {
        if (options.sp.compress) {
            // Flush LZ4 tails and release LZ4 context
            const size_t bufSize = LZ4F_compressBound(0, nullptr);
            char         buf[bufSize];
            size_t       size = LZ4F_compressEnd(sampleFileLz4Ctx, buf, bufSize, nullptr);
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
}

static void main_init(int argc, const char **argv)
{
    atexit(main_destroy);

    initZobrish();

    options_parse(argc, argv, options, eo);

    jq = new JobQueue((int)eo.size(), options.rounds, options.games, options.gauntlet);
    openings = new Openings(options.openings.c_str(), options.random, options.srand);

    if (!options.pgn.empty())
        pgnSeqWriter = new SeqWriter(options.pgn.c_str(), FOPEN_APPEND_MODE);

    if (!options.sgf.empty())
        sgfSeqWriter = new SeqWriter(options.sgf.c_str(), FOPEN_APPEND_MODE);

    if (!options.msg.empty())
        msgSeqWriter = new SeqWriter(options.msg.c_str(), FOPEN_APPEND_MODE);

    if (!options.sp.fileName.empty()) {
        if (options.sp.compress) {
            DIE_IF(0,
                   !(sampleFile =
                         fopen(options.sp.fileName.c_str(), FOPEN_WRITE_BINARY_MODE)));
            // Init LZ4 context and write file headers
            DIE_IF(0,
                   LZ4F_isError(
                       LZ4F_createCompressionContext(&sampleFileLz4Ctx, LZ4F_VERSION)));
            char   buf[LZ4F_HEADER_SIZE_MAX];
            size_t headerSize =
                LZ4F_compressBegin(sampleFileLz4Ctx, buf, sizeof(buf), nullptr);
            fwrite(buf, sizeof(char), headerSize, sampleFile);
        }
        else if (options.sp.bin) {
            DIE_IF(0,
                   !(sampleFile =
                         fopen(options.sp.fileName.c_str(), FOPEN_APPEND_BINARY_MODE)));
        }
        else {
            DIE_IF(0,
                   !(sampleFile = fopen(options.sp.fileName.c_str(), FOPEN_APPEND_MODE)));
        }
    }

    // Prepare Workers[]
    for (int i = 0; i < options.concurrency; i++) {
        std::string logName;

        if (options.log) {
            logName = format("c-gomoku-cli.%i.log", i + 1);
        }

        workers.push_back(new Worker(i, logName.c_str()));
    }
}

static void thread_start(Worker *w)
{
    std::string opening_str, messages;
    Job         job   = {};
    Engine engines[2] = {{w, options.debug, !options.msg.empty() ? &messages : nullptr},
                         {w, options.debug, !options.msg.empty() ? &messages : nullptr}};
    int    ei[2]      = {-1, -1};  // eo[ei[0]] plays eo[ei[1]]: initialize with invalid
                                   // values to start
    size_t idx = 0, count = 0;     // game idx and count (shared across workers)

    while (jq->pop(job, idx, count)) {
        // Clear all previous engine messages and write game index
        if (!options.msg.empty()) {
            messages = "------------------------------\n";
            messages += format("Game ID: %zu\n", idx + 1);
        }

        // Engine stop/start, as needed
        for (int i = 0; i < 2; i++) {
            if (job.ei[i] != ei[i]) {
                ei[i] = job.ei[i];
                engines[i].terminate();
                engines[i].start(eo[ei[i]].cmd.c_str(),
                                 eo[ei[i]].name.c_str(),
                                 eo[ei[i]].tolerance);
                jq->set_name(ei[i], engines[i].name);
            }
            // Re-init engine if it crashed/timeout previously
            else if (!engines[i].is_ok() || engines[i].is_crashed()) {
                engines[i].terminate();
                engines[i].start(eo[ei[i]].cmd.c_str(),
                                 eo[ei[i]].name.c_str(),
                                 eo[ei[i]].tolerance);
            }
        }

        // Choose opening position
        size_t openingRound =
            openings->next(opening_str, options.repeat ? idx / 2 : idx, w->id);

        // Play 1 game
        Game  game(job.round, job.game, w);
        Color color = BLACK;  // black play first in gomoku/renju by default

        if (!game.load_opening(opening_str, options, openingRound, color)) {
            DIE("[%d] illegal OPENING '%s'\n", w->id, opening_str.c_str());
        }

        const int blackIdx = color ^ job.reverse;
        const int whiteIdx = oppositeColor((Color)blackIdx);

        printf("[%d] Started game %zu of %zu (%s vs %s)\n",
               w->id,
               idx + 1,
               count,
               engines[blackIdx].name.c_str(),
               engines[whiteIdx].name.c_str());

        if (!options.msg.empty())
            messages += format("Engines: %s x %s\n",
                               engines[blackIdx].name,
                               engines[whiteIdx].name);

        const EngineOptions *eoPair[2] = {&eo[ei[0]], &eo[ei[1]]};
        const int            wld       = game.play(options, engines, eoPair, job.reverse);

        if (!options.gauntlet || !options.saveLoseOnly || wld == RESULT_LOSS) {
            // Write to PGN file
            if (pgnSeqWriter) {
                const int pgnVerbosity = 0;
                pgnSeqWriter->push(idx, game.export_pgn(idx + 1, pgnVerbosity));
            }

            // Write to SGF file
            if (sgfSeqWriter)
                sgfSeqWriter->push(idx, game.export_sgf(idx + 1));

            // Write engine messages to TXT file
            if (msgSeqWriter)
                msgSeqWriter->push(idx, messages);

            // Write to Sample file
            if (sampleFile)
                game.export_samples(sampleFile, options.sp.bin, sampleFileLz4Ctx);
        }

        // Write to stdout a one line summary of the game
        const char *ResultTxt[3] = {"0-1", "1/2-1/2", "1-0"};  // Black-White
        std::string result, reason;
        game.decode_state(result, reason, ResultTxt);

        printf("[%d] Finished game %zu (%s vs %s): %s {%s}\n",
               w->id,
               idx + 1,
               engines[blackIdx].name.c_str(),
               engines[whiteIdx].name.c_str(),
               result.c_str(),
               reason.c_str());

        // Pair update
        int wldCount[3] = {0};
        jq->add_result(job.pair, wld, wldCount);
        const int n =
            wldCount[RESULT_WIN] + wldCount[RESULT_LOSS] + wldCount[RESULT_DRAW];
        printf("Score of %s vs %s: %d - %d - %d  [%.3f] %d\n",
               engines[0].name.c_str(),
               engines[1].name.c_str(),
               wldCount[RESULT_WIN],
               wldCount[RESULT_LOSS],
               wldCount[RESULT_DRAW],
               (wldCount[RESULT_WIN] + 0.5 * wldCount[RESULT_DRAW]) / n,
               n);

        // SPRT update
        if (options.sprt && options.sprtParam.done(wldCount)) {
            jq->stop();
        }

        // Tournament update
        if (eo.size() > 2) {
            jq->print_results((size_t)options.games);
        }
    }

    for (int i = 0; i < 2; i++) {
        engines[i].terminate();
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

        // We want some tolerance on small delays here. Given a choice, it's
        // best to wait for the worker thread to notice an overdue deadline,
        // which it will handled nicely by counting the game as lost for the
        // offending engine, and continue. Enforcing deadlines from the master
        // thread is the last resort solution, because it is an unrecovrable
        // error. At this point we are likely to face a completely unresponsive
        // engine, where any attempt at I/O will block the master thread, on top
        // of the already blocked worker. Hence, we must DIE().
        for (int i = 0; i < options.concurrency; i++) {
            int64_t overdue = workers[i]->deadline_overdue();
            if (overdue > 0) {
                workers[i]->deadline_callback_once();
            }
            else if (overdue > 1000) {
                DIE("[%d] engine %s is unresponsive to [%s]\n",
                    workers[i]->id,
                    workers[i]->deadline.engineName.c_str(),
                    workers[i]->deadline.description.c_str());
            }
        }
    } while (!jq->done());

    // Join threads[]
    for (std::thread &th : threads) {
        th.join();
    }

    return 0;
}
