/*
 *  c-gomoku-cli, a command line interface for Gomocup engines. Copyright 2021 Chao Ma.
 *  c-gomoku-cli is derived from c-chess-cli, originally authored by lucasart 2020.
 *
 *  c-gomoku-cli is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 *  c-gomoku-cli is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with this
 * program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "options.h"

#include "util.h"

#include <cassert>
#include <cctype>
#include <climits>
#include <cstring>
#include <iostream>

// Gomocup time control is in format 'matchtime|turntime' or only 'matchtime'
static void options_parse_tc_gomocup(const char *s, EngineOptions &eo)
{
    double matchTime = 0, turnTime = 0, increment = 0;

    // s = left+increment
    std::string left, right;
    string_tok(right, string_tok(left, s, "+"), "+");
    increment = atof(right.c_str());

    // parse left
    if (strchr(left.c_str(), '/')) {
        // left = matchtime|turntime
        std::string copy = left;
        string_tok(right, string_tok(left, copy.c_str(), "/"), "/");
        matchTime = atof(left.c_str());
        turnTime  = atof(right.c_str());
    }
    else {
        // left = matchTime
        matchTime = atof(left.c_str());
        // turnTime is the same as match time
        turnTime = matchTime;
    }

    eo.timeoutMatch = (int64_t)(matchTime * 1000);
    eo.timeoutTurn  = (int64_t)(turnTime * 1000);
    eo.increment    = (int64_t)(increment * 1000);
}

static int options_parse_eo(int argc, const char **argv, int i, EngineOptions &eo)
{
    while (i < argc && argv[i][0] != '-') {
        const char *tail = NULL;

        if ((tail = string_prefix(argv[i], "cmd="))) {
            eo.cmd = tail;
        }
        else if ((tail = string_prefix(argv[i], "name="))) {
            eo.name = tail;
        }
        else if ((tail = string_prefix(argv[i], "tc="))) {
            options_parse_tc_gomocup(tail, eo);
        }
        else if ((tail = string_prefix(argv[i], "depth="))) {
            eo.depth = atoi(tail);
        }
        else if ((tail = string_prefix(argv[i], "nodes="))) {
            eo.nodes = atoll(tail);
        }
        else if ((tail = string_prefix(argv[i], "maxmemory="))) {
            eo.maxMemory = (int64_t)(atof(tail));
        }
        else if ((tail = string_prefix(argv[i], "thread="))) {
            eo.numThreads = atoi(tail);
        }
        else if ((tail = string_prefix(argv[i], "tolerance="))) {
            eo.tolerance = (int64_t)(atof(tail) * 1000);
        }
        else if ((tail = string_prefix(argv[i], "option."))) {
            eo.options.push_back(tail);  // store "name=value" string
        }
        else {
            DIE("Illegal syntax '%s'\n", argv[i]);
        }

        i++;
    }

    return i - 1;
}

static int options_parse_openings(int argc, const char **argv, int i, Options &o)
{
    while (i < argc && argv[i][0] != '-') {
        const char *tail = NULL;

        if ((tail = string_prefix(argv[i], "file=")))
            o.openings = tail;
        else if ((tail = string_prefix(argv[i], "type="))) {
            if (!strcmp(tail, "pos"))
                o.openingType = OPENING_POS;
            else if (strcmp(tail, "offset"))
                DIE("Invalid type for -openings: '%s'\n", tail);
        }
        else if ((tail = string_prefix(argv[i], "order="))) {
            if (!strcmp(tail, "random"))
                o.random = true;
            else if (strcmp(tail, "sequential"))
                DIE("Invalid order for -openings: '%s'\n", tail);
        }
        else if ((tail = string_prefix(argv[i], "srand=")))
            o.srand = (uint64_t)atoll(tail);
        else
            DIE("Illegal token in -openings: '%s'\n", argv[i]);

        i++;
    }

    return i - 1;
}

static int
options_parse_adjudication(int argc, const char **argv, int i, int *count, int *score)
{
    if (i + 1 < argc) {
        *count = atoi(argv[i++]);
        *score = atoi(argv[i]);
    }
    else {
        DIE("Missing parameter(s) for '%s'\n", argv[i - 1]);
    }

    return i;
}

static int options_parse_sprt(int argc, const char **argv, int i, Options &o)
{
    o.sprt = true;

    while (i < argc && argv[i][0] != '-') {
        const char *tail = NULL;

        if ((tail = string_prefix(argv[i], "elo0=")))
            o.sprtParam.elo0 = atof(tail);
        else if ((tail = string_prefix(argv[i], "elo1=")))
            o.sprtParam.elo1 = atof(tail);
        else if ((tail = string_prefix(argv[i], "alpha=")))
            o.sprtParam.alpha = atof(tail);
        else if ((tail = string_prefix(argv[i], "beta=")))
            o.sprtParam.beta = atof(tail);
        else
            DIE("Illegal token in -sprt: '%s'\n", argv[i]);

        i++;
    }

    if (!o.sprtParam.validate())
        DIE("Invalid SPRT parameters\n");

    return i - 1;
}

static int options_parse_sample(int argc, const char **argv, int i, Options &o)
{
    while (i < argc && argv[i][0] != '-') {
        const char *tail = NULL;

        if ((tail = string_prefix(argv[i], "freq=")))
            o.sp.freq = atof(tail);
        else if ((tail = string_prefix(argv[i], "file=")))
            o.sp.fileName = tail;
        else if ((tail = string_prefix(argv[i], "format="))) {
            if (!strcmp(tail, "csv"))
                o.sp.bin = false;
            else if (!strcmp(tail, "bin"))
                o.sp.bin = true;
            else if (!strcmp(tail, "bin_lz4"))
                o.sp.bin = o.sp.compress = true;
            else
                DIE("Illegal format in -sample: '%s'\n", tail);
        }
        else
            DIE("Illegal token in -sample: '%s'\n", argv[i]);

        i++;
    }

    if (o.sp.fileName.empty())
        o.sp.fileName =
            format("sample.%s", o.sp.bin ? (o.sp.compress ? "bin.lz4" : "bin") : "csv");

    return i - 1;
}

static void check_rule_code(GameRule gr)
{
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

void options_parse(int                         argc,
                   const char **               argv,
                   Options &                   o,
                   std::vector<EngineOptions> &eo)
{
    EngineOptions each;
    bool          eachSet = false;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-repeat"))
            o.repeat = true;
        else if (!strcmp(argv[i], "-transform"))
            o.transform = true;
        else if (!strcmp(argv[i], "-gauntlet"))
            o.gauntlet = true;
        else if (!strcmp(argv[i], "-loseonly"))
            o.saveLoseOnly = true;
        else if (!strcmp(argv[i], "-log"))
            o.log = true;
        else if (!strcmp(argv[i], "-concurrency"))
            o.concurrency = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-each")) {
            i       = options_parse_eo(argc, argv, i + 1, each);
            eachSet = true;
        }
        else if (!strcmp(argv[i], "-engine")) {
            EngineOptions newEn;
            i = options_parse_eo(argc, argv, i + 1, newEn);
            eo.push_back(newEn);
        }
        else if (!strcmp(argv[i], "-games"))
            o.games = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-rounds"))
            o.rounds = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-openings"))
            i = options_parse_openings(argc, argv, i + 1, o);
        else if (!strcmp(argv[i], "-pgn"))
            o.pgn = argv[++i];
        else if (!strcmp(argv[i], "-sgf"))
            o.sgf = argv[++i];
        else if (!strcmp(argv[i], "-msg"))
            o.msg = argv[++i];
        else if (!strcmp(argv[i], "-resign"))
            i = options_parse_adjudication(argc,
                                           argv,
                                           i + 1,
                                           &o.resignCount,
                                           &o.resignScore);
        else if (!strcmp(argv[i], "-draw"))
            i = options_parse_adjudication(argc, argv, i + 1, &o.drawCount, &o.drawScore);
        else if (!strcmp(argv[i], "-drawafter"))
            o.forceDrawAfter = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-sprt"))
            i = options_parse_sprt(argc, argv, i + 1, o);
        else if (!strcmp(argv[i], "-sample"))
            i = options_parse_sample(argc, argv, i + 1, o);
        else if (!strcmp(argv[i], "-rule")) {
            o.gameRule = (GameRule)atoi(argv[++i]);
            check_rule_code(o.gameRule);
        }
        else if (!strcmp(argv[i], "-boardsize")) {
            o.boardSize = atoi(argv[i + 1]);
            if (o.boardSize < 5 || o.boardSize > 22) {
                DIE("Only support board size of 5 ~ 22\n");
            }
            i++;
        }
        else if (!strcmp(argv[i], "-debug")) {
            o.debug = true;
            o.log   = true;  // enable log if debug is enabled
        }
        else if (!strcmp(argv[i], "-sendbyboard"))
            o.useTURN = false;
        else if (!strcmp(argv[i], "-fatalerror"))
            o.fatalError = true;
        else {
            DIE("Unknown option '%s'\n", argv[i]);
        }
    }

    if (eachSet) {
        for (size_t i = 0; i < eo.size(); i++) {
            if (!each.cmd.empty())
                eo[i].cmd = each.cmd;

            if (!each.name.empty())
                eo[i].name = each.name;

            for (size_t j = 0; j < each.options.size(); j++)
                eo[i].options.push_back(each.options[j]);

            if (each.timeoutMatch)
                eo[i].timeoutMatch = each.timeoutMatch;

            if (each.timeoutTurn)
                eo[i].timeoutTurn = each.timeoutTurn;

            if (each.increment)
                eo[i].increment = each.increment;

            if (each.nodes)
                eo[i].nodes = each.nodes;

            if (each.depth)
                eo[i].depth = each.depth;

            if (each.maxMemory)
                eo[i].maxMemory = each.maxMemory;

            if (each.numThreads)
                eo[i].numThreads = each.numThreads;

            if (each.tolerance)
                eo[i].tolerance = each.tolerance;
        }
    }

    if (eo.size() < 2)
        DIE("at least 2 engines are needed\n");

    if (eo.size() > 2 && o.sprt)
        DIE("only 2 engines for SPRT\n");

    options_print(o, eo);
}

void options_print(const Options &o, const std::vector<EngineOptions> &eo)
{
    auto openingTypeName = [](OpeningType optype) {
        switch (optype) {
        case OPENING_OFFSET: return "offset";
        case OPENING_POS: return "pos";
        default: return "";
        }
    };

    std::cout << "---------------------------" << std::endl;
    std::cout << "Global Options:" << std::endl;
    std::cout << "openings = " << o.openings << std::endl;
    if (!o.openings.empty())
        std::cout << "openingType = " << openingTypeName(o.openingType) << std::endl;
    std::cout << "boardSize = " << o.boardSize << std::endl;
    std::cout << "gameRule = " << o.gameRule << std::endl;
    std::cout << "pgn = " << o.pgn << std::endl;
    std::cout << "sgf = " << o.sgf << std::endl;
    std::cout << "msg = " << o.msg << std::endl;
    std::cout << "log = " << o.log << std::endl;
    std::cout << "sample = " << o.sp.fileName << std::endl;
    if (!o.sp.fileName.empty())
        std::cout << "sample.freq = " << o.sp.freq << std::endl;
    std::cout << "random = " << o.random << std::endl;
    std::cout << "repeat = " << o.repeat << std::endl;
    std::cout << "transform = " << o.transform << std::endl;
    std::cout << "sprt = " << o.sprt << std::endl;
    std::cout << "gauntlet = " << o.gauntlet << std::endl;
    if (o.gauntlet)
        std::cout << "loseonly = " << o.saveLoseOnly << std::endl;
    std::cout << "concurrency = " << o.concurrency << std::endl;
    std::cout << "games = " << o.games << std::endl;
    std::cout << "rounds = " << o.rounds << std::endl;
    std::cout << "resignCount = " << o.resignCount << std::endl;
    std::cout << "resignScore = " << o.resignScore << std::endl;
    std::cout << "drawCount = " << o.drawCount << std::endl;
    std::cout << "drawScore = " << o.drawScore << std::endl;
    std::cout << "drawAfter = " << o.forceDrawAfter << std::endl;
    std::cout << "fatalerror = " << o.fatalError << std::endl;
    std::cout << "debug = " << o.debug << std::endl;
    std::cout << std::endl;

    size_t engineCnt = eo.size();
    std::cout << "Engine number = " << engineCnt << std::endl;
    for (size_t ei = 0; ei < engineCnt; ei++) {
        const EngineOptions &e1 = eo[ei];
        std::cout << "---------------------------" << std::endl;
        std::cout << "Engine " << ei << " Options:" << std::endl;
        std::cout << "name = " << e1.name << std::endl;
        std::cout << "cmd = " << e1.cmd << std::endl;
        std::cout << "nodes = " << e1.nodes << std::endl;
        std::cout << "depth = " << e1.depth << std::endl;
        std::cout << "timeoutTurn = " << e1.timeoutTurn << std::endl;
        std::cout << "timeoutMatch = " << e1.timeoutMatch << std::endl;
        std::cout << "increment = " << e1.increment << std::endl;
        std::cout << "maxMemory = " << e1.maxMemory << std::endl;
        std::cout << "thread = " << e1.numThreads << std::endl;
        std::cout << "tolerance = " << e1.tolerance << std::endl;
        for (size_t i = 0; i < e1.options.size(); i++) {
            std::cout << "option." << e1.options[i] << std::endl;
        }
    }
    std::cout << "---------------------------" << std::endl;
}
