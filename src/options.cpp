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


/*
// Parse time control. Expects 'mtg/time+inc' or 'time+inc'. Note that time and inc are provided by
// the user in seconds, instead of msec.
static void options_parse_tc(const char *s, EngineOptions *eo)
{
    double time = 0, increment = 0;

    // s = left+increment
    scope(str_destroy) str_t left = str_init(), right = str_init();
    str_tok(str_tok(s, &left, "+"), &right, "+");
    increment = atof(right.buf);

    // parse left
    if (strchr(left.buf, '/')) {
        // left = movestogo/time
        scope(str_destroy) str_t copy = str_init_from(left);
        str_tok(str_tok(copy.buf, &left, "/"), &right, "/");
        eo->movestogo = atoi(left.buf);
        time = atof(right.buf);
    } else
        // left = time
        time = atof(left.buf);

    eo->time = (int64_t)(time * 1000);
    eo->increment = (int64_t)(increment * 1000);
}
*/

// Gomocup time control is in format 'matchtime|turntime' or only 'matchtime'
static void options_parse_tc_gomocup(const char *s, EngineOptions *eo)
{
    //double time = 0, increment = 0;
    double matchTime = 0;
    double turnTime = 0;

    // s = left+increment
    scope(str_destroy) str_t left = str_init(), right = str_init();
    str_tok(str_tok(s, &left, "+"), &right, "+");
    double increment = atof(right.buf);

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
    }

    eo->timeoutMatch = (int64_t)(matchTime * 1000);
    eo->timeoutTurn = (int64_t)(turnTime * 1000);
}

static int options_parse_eo(int argc, const char **argv, int i, EngineOptions *eo)
{

    while (i < argc && argv[i][0] != '-') {
        const char *tail = NULL;

        if ((tail = str_prefix(argv[i], "cmd="))) {
            str_cpy_c(&eo->cmd, tail);
        } else if ((tail = str_prefix(argv[i], "name="))) {
            str_cpy_c(&eo->name, tail);
        } else if ((tail = str_prefix(argv[i], "option."))) {
            vec_push(eo->options, str_init_from_c(tail), str_t);  // store "name=value" string
        } else if ((tail = str_prefix(argv[i], "depth="))) {
            eo->depth = atoi(tail);
        } else if ((tail = str_prefix(argv[i], "nodes="))) {
            eo->nodes = atoll(tail);
        } else if ((tail = str_prefix(argv[i], "movetime="))) {
            eo->movetime = (int64_t)(atof(tail) * 1000);
        } else if ((tail = str_prefix(argv[i], "maxmemory="))) {
            eo->maxMemory = (int64_t)(atof(tail));
        } else if ((tail = str_prefix(argv[i], "tc="))) {
            options_parse_tc_gomocup(tail, eo);
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
        else if ((tail = str_prefix(argv[i], "order="))) {
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

EngineOptions engine_options_init(void)
{
    EngineOptions eo = {0};
    eo.cmd = str_init();
    eo.name = str_init();
    eo.options = vec_init(str_t);

    eo.maxMemory = 0;
    eo.timeoutMatch = 0;
    eo.timeoutTurn = 0;

    // all others set to zero
    eo.time = 0;
    eo.increment = 0;
    eo.movetime = 0;
    eo.nodes = 0;
    eo.depth = 0;
    eo.movestogo = 0;

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

    // non-zero default values
    o.concurrency = 1;
    o.games = o.rounds = 1;
    o.sprtParam.alpha = o.sprtParam.beta = 0.05;
    o.useTURN = false;
    o.boardSize = 15; // default size
    o.gameRule = GOMOKU_FIVE_OR_MORE;
    o.debug = false;

    return o;
}

void check_rule_code(GameRule gr) {
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

void options_parse(int argc, const char **argv, Options *o, EngineOptions **eo)
{

    scope(engine_options_destroy) EngineOptions each = engine_options_init();
    bool eachSet = false;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-repeat"))
            o->repeat = true;
        else if (!strcmp(argv[i], "-gauntlet"))
            o->gauntlet = true;
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
        } else if (!strcmp(argv[i], "-resign"))
            i = options_parse_adjudication(argc, argv, i + 1, &o->resignCount, &o->resignScore);
        else if (!strcmp(argv[i], "-draw"))
            i = options_parse_adjudication(argc, argv, i + 1, &o->drawCount, &o->drawScore);
        else if (!strcmp(argv[i], "-sprt"))
            i = options_parse_sprt(argc, argv, i + 1, o);
        else if (!strcmp(argv[i], "-rule")) {
            o->gameRule = (GameRule)(atoi(argv[i + 1]));
            i++;
        } else if (!strcmp(argv[i], "-boardsize")) {
            o->boardSize = atoi(argv[i + 1]);
            if (o->boardSize < 5 || o->boardSize > 25) {
                DIE("Only support board size of 5 ~ 25\n");
            }
            i++;
        } else if (!strcmp(argv[i], "-debug")) {
            o->debug = true;
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

            if (each.time)
                (*eo)[i].time = each.time;

            if (each.increment)
                (*eo)[i].increment = each.increment;

            if (each.movetime)
                (*eo)[i].movetime = each.movetime;

            if (each.nodes)
                (*eo)[i].nodes = each.nodes;

            if (each.depth)
                (*eo)[i].depth = each.depth;

            if (each.movestogo)
                (*eo)[i].movestogo = each.movestogo;

            if (each.timeoutMatch)
                (*eo)[i].timeoutMatch = each.timeoutMatch;

            if (each.timeoutTurn)
                (*eo)[i].timeoutTurn = each.timeoutTurn;

            if (each.maxMemory)
                (*eo)[i].maxMemory = each.maxMemory;
        }
    }

    if (vec_size(*eo) < 2)
        DIE("at least 2 engines are needed\n");

    if (vec_size(*eo) > 2 && o->sprt)
        DIE("only 2 engines for SPRT\n");
    
/*
    o->rounds = 1;
    o->log = true;
    //o->boardSize = 12;
    o->gameRule = 0;
    str_cpy_c(&(o->pgn), "tmp.pgn");

    EngineOptions engine1 = engine_options_init();
    str_cpy_c(&engine1.cmd, "./exmple-engine/pbrain-rapfi1");
    //str_cpy_c(&engine1.cmd, "./exmple-engine/pbrain-wine");
    str_cpy_c(&engine1.name, "Rapfi");
    //engine1.maxMemory = 100 * 1000 * 1000;
    engine1.timeoutMatch = 180 * 1000;
    engine1.timeoutTurn = 10 * 1000;
    vec_push(*eo, engine1, EngineOptions);

    EngineOptions engine2 = engine_options_init();
    //str_cpy_c(&engine2.cmd, "./exmple-engine/pbrain-wine");
    str_cpy_c(&engine2.cmd, "./exmple-engine/pbrain-rapfi1");
    str_cpy_c(&engine2.name, "Wine");
    engine2.maxMemory = 100 * 1000 * 1000;
    engine2.timeoutMatch = 180 * 1000;
    engine2.timeoutTurn = 10 * 1000;
    vec_push(*eo, engine2, EngineOptions);
*/

    options_print(o, eo);
}

void options_print(Options *o, EngineOptions **eo) {

    std::cout << "---------------------------" << std::endl;
    std::cout << "Global Options:" << std::endl;
    std::cout << "openingss = " << o->openings.buf << std::endl;
    std::cout << "boardSize = " << o->boardSize << std::endl;
    std::cout << "gameRule = " << o->gameRule << std::endl;
    std::cout << "pgn = " << o->pgn.buf << std::endl;
    std::cout << "sgf = " << o->sgf.buf << std::endl;
    std::cout << "log = " << o->log << std::endl;
    std::cout << "random = " << o->random << std::endl;
    std::cout << "repeat = " << o->repeat << std::endl;
    std::cout << "sprt = " << o->sprt << std::endl;
    std::cout << "gauntlet = " << o->gauntlet << std::endl;
    std::cout << "concurrency = " << o->concurrency << std::endl;
    std::cout << "games = " << o->games << std::endl;
    std::cout << "rounds = " << o->rounds << std::endl;
    std::cout << "resignCount = " << o->resignCount << std::endl;
    std::cout << "resignScore = " << o->resignScore << std::endl;
    std::cout << "drawCount = " << o->drawCount << std::endl;
    std::cout << "drawScore = " << o->drawScore << std::endl;
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
        std::cout << "maxMemory = " << e1->maxMemory << std::endl;
    }
    std::cout << "---------------------------" << std::endl;
}

void options_destroy(Options *o)
{
    str_destroy_n(&o->openings, &o->pgn, &o->sgf);
}
