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
#include <ctype.h>
#include <limits.h>
#include <string.h>
#include "options.h"
#include "util.h"
#include "vec.h"


// Gomocup time control is in format 'matchtime|turntime' or only 'matchtime'
static void options_parse_tc_gomocup(const char *s, EngineOptions *eo)
{
    double matchTime = 0, turnTime = 0, increment = 0;

    // s = left+increment
    scope(str_destroy) str_t left = str_init(), right = str_init();
    str_tok(str_tok(s, &left, "+"), &right, "+");
    increment = atof(right.buf);

    // parse left
    if (strchr(left.buf, '/')) {
        // left = matchtime|turntime
        scope(str_destroy) str_t copy = str_init_from(left);
        str_tok(str_tok(copy.buf, &left, "/"), &right, "/");
        matchTime = atof(left.buf);
        turnTime = atof(right.buf);
    } else {
        // left = matchTime
        matchTime = atof(left.buf);
        // turnTime is the same as match time 
        turnTime = matchTime;
    }

    eo->timeoutMatch = (int64_t)(matchTime * 1000);
    eo->timeoutTurn = (int64_t)(turnTime * 1000);
    eo->increment = (int64_t)(increment * 1000);
}

static int options_parse_eo(int argc, const char **argv, int i, EngineOptions *eo)
{
    while (i < argc && argv[i][0] != '-') {
        const char *tail = NULL;

        if ((tail = str_prefix(argv[i], "cmd="))) {
            str_cpy_c(&eo->cmd, tail);
        } else if ((tail = str_prefix(argv[i], "name="))) {
            str_cpy_c(&eo->name, tail);
        } else if ((tail = str_prefix(argv[i], "tc="))) {
            options_parse_tc_gomocup(tail, eo);
        } else if ((tail = str_prefix(argv[i], "depth="))) {
            eo->depth = atoi(tail);
        } else if ((tail = str_prefix(argv[i], "nodes="))) {
            eo->nodes = atoll(tail);
        } else if ((tail = str_prefix(argv[i], "maxmemory="))) {
            eo->maxMemory = (int64_t)(atof(tail));
        } else if ((tail = str_prefix(argv[i], "thread="))) {
            eo->numThreads = atoi(tail);
        } else if ((tail = str_prefix(argv[i], "tolerance="))) {
            eo->tolerance = (int64_t)(atof(tail) * 1000);
        } else if ((tail = str_prefix(argv[i], "option."))) {
            vec_push(eo->options, str_init_from_c(tail), str_t);  // store "name=value" string
        } else {
            DIE("Illegal syntax '%s'\n", argv[i]);
        }

        i++;
    }

    return i - 1;
}

static int options_parse_openings(int argc, const char **argv, int i, Options *o)
{
    while (i < argc && argv[i][0] != '-') {
        const char *tail = NULL;

        if ((tail = str_prefix(argv[i], "file=")))
            str_cpy_c(&o->openings, tail);
        else if ((tail = str_prefix(argv[i], "type="))) {
            if (!strcmp(tail, "pos"))
                o->openingType = OPENING_POS;
            else if (strcmp(tail, "offset"))
                DIE("Invalid type for -openings: '%s'\n", tail);
        } else if ((tail = str_prefix(argv[i], "order="))) {
            if (!strcmp(tail, "random"))
                o->random = true;
            else if (strcmp(tail, "sequential"))
                DIE("Invalid order for -openings: '%s'\n", tail);
        } else if ((tail = str_prefix(argv[i], "srand=")))
            o->srand = (uint64_t)atoll(tail);
        else
            DIE("Illegal token in -openings: '%s'\n", argv[i]);

        i++;
    }

    return i - 1;
}

static int options_parse_adjudication(int argc, const char **argv, int i, int *count, int *score)
{
    if (i + 1 < argc) {
        *count = atoi(argv[i++]);
        *score = atoi(argv[i]);
    } else {
        DIE("Missing parameter(s) for '%s'\n", argv[i - 1]);
    }

    return i;
}

static int options_parse_sprt(int argc, const char **argv, int i, Options *o)
{
    o->sprt = true;

    while (i < argc && argv[i][0] != '-') {
        const char *tail = NULL;

        if ((tail = str_prefix(argv[i], "elo0=")))
            o->sprtParam.elo0 = atof(tail);
        else if ((tail = str_prefix(argv[i], "elo1=")))
            o->sprtParam.elo1 = atof(tail);
        else if ((tail = str_prefix(argv[i], "alpha=")))
            o->sprtParam.alpha = atof(tail);
        else if ((tail = str_prefix(argv[i], "beta=")))
            o->sprtParam.beta = atof(tail);
        else
            DIE("Illegal token in -sprt: '%s'\n", argv[i]);

        i++;
    }

    if (!sprt_validate(&o->sprtParam))
        DIE("Invalid SPRT parameters\n");

    return i - 1;
}

static int options_parse_sample(int argc, const char **argv, int i, Options *o)
{
    while (i < argc && argv[i][0] != '-') {
        const char *tail = NULL;

        if ((tail = str_prefix(argv[i], "freq=")))
            o->sp.freq = atof(tail);
        else if ((tail = str_prefix(argv[i], "file=")))
            str_cpy_c(&o->sp.fileName, tail);
        else if ((tail = str_prefix(argv[i], "format="))) {
            if (!strcmp(tail, "csv"))
                o->sp.bin = false;
            else if (!strcmp(tail, "bin"))
                o->sp.bin = true;
            else if (!strcmp(tail, "bin_lz4"))
                o->sp.bin = o->sp.compress = true;
            else
                DIE("Illegal format in -sample: '%s'\n", tail);
        } else
            DIE("Illegal token in -sample: '%s'\n", argv[i]);

        i++;
    }

    if (!o->sp.fileName.len)
        str_cpy_fmt(&o->sp.fileName, "sample.%s", o->sp.bin ? (o->sp.compress ? "bin.lz4" : "bin") : "csv");

    return i - 1;
}

static void check_rule_code(GameRule gr) {
    bool supported = false;
    for (int i = 0; i < RULES_COUNT; i++) {
        if (ALL_VALID_RULES[i] == gr) {
            supported = true;
            break;
        }
    }
    if (!supported) {
        DIE("Unspported game rule code '%i'!\n", gr);
    }
}

EngineOptions engine_options_init(void)
{
    EngineOptions eo = {0};
    eo.cmd = str_init();
    eo.name = str_init();
    eo.options = vec_init(str_t);

    // init time control info
    eo.timeoutMatch = 0;
    eo.timeoutTurn = 0;
    eo.increment = 0;
    eo.nodes = 0;
    eo.depth = 0;

    // default max memory is set to 350MB (same as Gomocup)
    eo.maxMemory = 367001600;
    // default thread num is 1
    eo.numThreads = 1;
    // default tolerance is 3
    eo.tolerance = 3000;

    return eo;
}

void engine_options_destroy(EngineOptions *eo)
{
    str_destroy_n(&eo->cmd, &eo->name);
    vec_destroy_rec(eo->options, str_destroy);
}

Options options_init(void)
{
    Options o = {0};
    o.openings = str_init();
    o.pgn = str_init();
    o.sgf = str_init();
    o.msg = str_init();
    o.sp = SampleParams { .fileName = str_init(), .freq = 1.0 };

    // non-zero default values
    o.concurrency = 1;
    o.games = o.rounds = 1;
    o.sprtParam.alpha = o.sprtParam.beta = 0.05;
    o.useTURN = true;
    o.boardSize = 15; // default size
    o.gameRule = GOMOKU_FIVE_OR_MORE;
    o.debug = false;

    return o;
}

void options_parse(int argc, const char **argv, Options *o, EngineOptions **eo)
{

    scope(engine_options_destroy) EngineOptions each = engine_options_init();
    bool eachSet = false;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-repeat"))
            o->repeat = true;
        else if (!strcmp(argv[i], "-transform"))
            o->transform = true;
        else if (!strcmp(argv[i], "-gauntlet"))
            o->gauntlet = true;
        else if (!strcmp(argv[i], "-loseonly"))
            o->saveLoseOnly = true;
        else if (!strcmp(argv[i], "-log"))
            o->log = true;
        else if (!strcmp(argv[i], "-concurrency"))
            o->concurrency = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-each")) {
            i = options_parse_eo(argc, argv, i + 1, &each);
            eachSet = true;
        } else if (!strcmp(argv[i], "-engine")) {
            EngineOptions newEn = engine_options_init();
            i = options_parse_eo(argc, argv, i + 1, &newEn);
            vec_push(*eo, newEn, EngineOptions);  // new gets moved here
        } else if (!strcmp(argv[i], "-games"))
            o->games = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-rounds"))
            o->rounds = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-openings"))
            i = options_parse_openings(argc, argv, i + 1, o);
        else if (!strcmp(argv[i], "-pgn")) {
            str_cpy_c(&o->pgn, argv[++i]);
        } else if (!strcmp(argv[i], "-sgf")) {
            str_cpy_c(&o->sgf, argv[++i]);
        } else if (!strcmp(argv[i], "-msg"))
            str_cpy_c(&o->msg, argv[++i]);
        else if (!strcmp(argv[i], "-resign"))
            i = options_parse_adjudication(argc, argv, i + 1, &o->resignCount, &o->resignScore);
        else if (!strcmp(argv[i], "-draw"))
            i = options_parse_adjudication(argc, argv, i + 1, &o->drawCount, &o->drawScore);
        else if (!strcmp(argv[i], "-drawafter"))
            o->forceDrawAfter = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-sprt"))
            i = options_parse_sprt(argc, argv, i + 1, o);
        else if (!strcmp(argv[i], "-sample"))
            i = options_parse_sample(argc, argv, i + 1, o);
        else if (!strcmp(argv[i], "-rule")) {
            o->gameRule = (GameRule)(atoi(argv[i + 1]));
            i++;
            check_rule_code((GameRule)o->gameRule);
        } else if (!strcmp(argv[i], "-boardsize")) {
            o->boardSize = atoi(argv[i + 1]);
            if (o->boardSize < 5 || o->boardSize > 22) {
                DIE("Only support board size of 5 ~ 22\n");
            }
            i++;
        } else if (!strcmp(argv[i], "-debug")) {
            o->debug = true;
            o->log = true; // enable log if debug is enabled
        } else if (!strcmp(argv[i], "-sendbyboard")) {
            o->useTURN = false;
        } else {
            DIE("Unknown option '%s'\n", argv[i]);
        }
    }

    if (eachSet) {
        for (size_t i = 0; i < vec_size(*eo); i++) {
            if (each.cmd.len)
                str_cpy(&(*eo)[i].cmd, each.cmd);

            if (each.name.len)
                str_cpy(&(*eo)[i].name, each.name);

            for (size_t j = 0; j < vec_size(each.options); j++)
                vec_push((*eo)[i].options, str_init_from(each.options[j]), str_t);

            if (each.timeoutMatch)
                (*eo)[i].timeoutMatch = each.timeoutMatch;

            if (each.timeoutTurn)
                (*eo)[i].timeoutTurn = each.timeoutTurn;

            if (each.increment)
                (*eo)[i].increment = each.increment;

            if (each.nodes)
                (*eo)[i].nodes = each.nodes;

            if (each.depth)
                (*eo)[i].depth = each.depth;

            if (each.maxMemory)
                (*eo)[i].maxMemory = each.maxMemory;

            if (each.numThreads)
                (*eo)[i].numThreads = each.numThreads;
        }
    }

    if (vec_size(*eo) < 2)
        DIE("at least 2 engines are needed\n");

    if (vec_size(*eo) > 2 && o->sprt)
        DIE("only 2 engines for SPRT\n");

    options_print(o, eo);
}

void options_print(Options *o, EngineOptions **eo) {

    auto openingTypeName = [](OpeningType optype) {
        switch (optype) {
        case OPENING_OFFSET: return "offset";
        case OPENING_POS: return "pos";
        default: return "";
        }
    };

    std::cout << "---------------------------" << std::endl;
    std::cout << "Global Options:" << std::endl;
    std::cout << "openings = " << o->openings.buf << std::endl;
    if (o->openings.len)
        std::cout << "openingType = " << openingTypeName(o->openingType) << std::endl;
    std::cout << "boardSize = " << o->boardSize << std::endl;
    std::cout << "gameRule = " << o->gameRule << std::endl;
    std::cout << "pgn = " << o->pgn.buf << std::endl;
    std::cout << "sgf = " << o->sgf.buf << std::endl;
    std::cout << "msg = " << o->msg.buf << std::endl;
    std::cout << "log = " << o->log << std::endl;
    std::cout << "sample = " << o->sp.fileName.buf << std::endl;
    if (o->sp.fileName.len)
        std::cout << "sample.freq = " << o->sp.freq << std::endl;
    std::cout << "random = " << o->random << std::endl;
    std::cout << "repeat = " << o->repeat << std::endl;
    std::cout << "transform = " << o->transform << std::endl;
    std::cout << "sprt = " << o->sprt << std::endl;
    std::cout << "gauntlet = " << o->gauntlet << std::endl;
    if (o->gauntlet)
        std::cout << "loseonly = " << o->saveLoseOnly << std::endl;
    std::cout << "concurrency = " << o->concurrency << std::endl;
    std::cout << "games = " << o->games << std::endl;
    std::cout << "rounds = " << o->rounds << std::endl;
    std::cout << "resignCount = " << o->resignCount << std::endl;
    std::cout << "resignScore = " << o->resignScore << std::endl;
    std::cout << "drawCount = " << o->drawCount << std::endl;
    std::cout << "drawScore = " << o->drawScore << std::endl;
    std::cout << "drawAfter = " << o->forceDrawAfter << std::endl;
    std::cout << "debug = " << o->debug << std::endl;
    std::cout << std::endl;

    int engineCnt = vec_size(*eo);
    std::cout << "Engine number = " << engineCnt << std::endl;
    for (int i = 0 ; i < engineCnt; i++) {
        EngineOptions *e1;
        e1 = &((*eo)[i]);
        std::cout << "---------------------------" << std::endl;
        std::cout << "Engine " << i << " Options:" << std::endl;
        std::cout << "name = " << e1->name.buf << std::endl;
        std::cout << "cmd = " << e1->cmd.buf << std::endl;
        std::cout << "nodes = " << e1->nodes << std::endl;
        std::cout << "depth = " << e1->depth << std::endl;
        std::cout << "timeoutTurn = " << e1->timeoutTurn << std::endl;
        std::cout << "timeoutMatch = " << e1->timeoutMatch << std::endl;
        std::cout << "increment = " << e1->increment << std::endl;
        std::cout << "maxMemory = " << e1->maxMemory << std::endl;
        std::cout << "thread = " << e1->numThreads << std::endl;
        std::cout << "tolerance = " << e1->tolerance << std::endl;
        for (size_t i = 0; i < vec_size(e1->options); i++) {
            std::cout << "option." << e1->options[i].buf << std::endl;
        }
    }
    std::cout << "---------------------------" << std::endl;
}

void options_destroy(Options *o)
{
    str_destroy_n(&o->openings, &o->pgn, &o->sgf);
}
