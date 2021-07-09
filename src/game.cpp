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

#include "game.h"

#include "options.h"
#include "position.h"
#include "util.h"

#include <climits>
#include <cstring>
#include <ctime>
#include <iostream>
#include <string>

Game::Game(int rd, int gm)
    : game_rule()
    , round(rd)
    , game(gm)
    , ply()
    , state()
    , board_size()
{}

bool Game::load_fen(std::string_view fen, int *color, const Options *o, size_t curRound)
{
    pos.emplace_back(o->boardSize);

    if (pos[0].apply_opening(fen, o->openingType)) {
        *color = pos[0].get_turn();
    }
    else {
        return false;
    }

    if (o->transform) {
        TransformType transType = (TransformType)(curRound % NB_TRANS);
        pos[0].transform(transType);
    }

    return true;
}

// Applies rules to generate legal moves, and determine the state of the game
int Game::game_apply_rules(move_t lastmove)
{
    bool allow_long_connection = true;
    if (game_rule == GOMOKU_EXACT_FIVE) {
        allow_long_connection = false;
    }
    else if (game_rule == RENJU) {
        if (ColorFromMove(lastmove) == BLACK)
            allow_long_connection = false;
    }

    if (pos[ply].check_five_in_line_lastmove(allow_long_connection)) {
        return STATE_FIVE_CONNECT;
    }
    else if (pos[ply].get_moves_left() == 0) {
        return STATE_DRAW_INSUFFICIENT_SPACE;
    }

    // game does not end
    return STATE_NONE;
}

void Game::gomocup_turn_info_command(const EngineOptions *eo,
                                     const int64_t        timeLeft,
                                     Engine *             engine)
{
    engine->writeln(format("INFO time_left %" PRId64, timeLeft).c_str());
}

void Game::gomocup_game_info_command(const EngineOptions *eo,
                                     const Options *      option,
                                     Engine *             engine)
{
    // game info
    engine->writeln(format("INFO rule %i", option->gameRule).c_str());

    // time control info
    if (eo->timeoutTurn)
        engine->writeln(format("INFO timeout_turn %" PRId64, eo->timeoutTurn).c_str());

    // always send match timeout info (0 means no limit in match time)
    engine->writeln(format("INFO timeout_match %" PRId64, eo->timeoutMatch).c_str());

    if (eo->depth)
        engine->writeln(format("INFO max_depth %i", eo->depth).c_str());

    if (eo->nodes)
        engine->writeln(format("INFO max_node %" PRId64, eo->nodes).c_str());

    // memory limit info
    engine->writeln(format("INFO max_memory %" PRId64, eo->maxMemory).c_str());

    // multi threading info
    if (eo->numThreads > 1)
        engine->writeln(format("INFO thread_num %i", eo->numThreads).c_str());

    // custom info
    std::string left, right;
    for (size_t i = 0; i < eo->options.size(); i++) {
        string_tok(right, string_tok(left, eo->options[i].c_str(), "="), "=");

        engine->writeln(format("INFO %s %s", left, right).c_str());
    }
}

void Game::send_board_command(const Position *pos, Worker *w, Engine *engine)
{
    engine->writeln("BOARD");

    int           moveCnt   = pos->get_move_count();
    const move_t *histMoves = pos->get_hist_moves();

    // make sure last color is 2 according to piskvork protocol
    auto colorToGomocupStoneIdx = [lastColor = ColorFromMove(histMoves[moveCnt - 1])](
                                      Color c) { return c == lastColor ? 2 : 1; };

    for (int i = 0; i < moveCnt; i++) {
        Color color           = ColorFromMove(histMoves[i]);
        int   gomocupColorIdx = colorToGomocupStoneIdx(color);
        Pos   p               = PosFromMove(histMoves[i]);

        engine->writeln(
            format("%i,%i,%i", CoordX(p), CoordY(p), gomocupColorIdx).c_str());
    }

    engine->writeln("DONE");
}

void Game::compute_time_left(const EngineOptions *eo, int64_t *timeLeft)
{
    if (eo->timeoutMatch > 0) {
        // add increment to time left if increment is set
        if (eo->increment > 0)
            *timeLeft += eo->increment;
    }
    else {
        *timeLeft = 2147483647LL;
    }
}

int Game::play(Worker *             w,
               const Options *      o,
               Engine               engines[2],
               const EngineOptions *eo[2],
               bool                 reverse)
// Play a game:
// - engines[reverse] plays the first move (which does not mean white, that depends on the
// FEN)
// - sets state value: see enum STATE_* codes
// - returns RESULT_LOSS/DRAW/WIN from engines[0] pov
{
    // initialize game rule
    this->game_rule  = (GameRule)(o->gameRule);
    this->board_size = o->boardSize;

    for (int color = BLACK; color <= WHITE; color++) {
        names[color] = engines[color ^ pos[0].get_turn() ^ reverse].name;
    }

    for (int i = 0; i < 2; i++) {
        // tell engine to start a new game
        engines[i].writeln(format("START %i", o->boardSize).c_str());
        engines[i].wait_for_ok();

        // send game info
        gomocup_game_info_command(eo[i], o, &engines[i]);
    }

    move_t  played                = NONE_MOVE;
    int     drawPlyCount          = 0;
    int     resignCount[NB_COLOR] = {0};
    int     ei                    = reverse;     // engines[ei] has the move
    int64_t timeLeft[2]           = {0LL, 0LL};  //{eo[0]->time, eo[1]->time};
    bool    canUseTurn[2]         = {false, false};

    // init time control
    timeLeft[0] = eo[0]->timeoutMatch;
    timeLeft[1] = eo[1]->timeoutMatch;

    // the starting position has been added at load_fen()

    for (ply = 0;; ei = (1 - ei), ply++) {
        if (played != NONE_MOVE) {
            Position::pos_move_with_copy(&pos[ply], &pos[ply - 1], played);
        }

        if (o->debug) {
            pos[ply].pos_print();
        }

        state = game_apply_rules(played);
        if (state > STATE_NONE) {
            break;
        }

        // Apply force draw adjudication rule
        if (o->forceDrawAfter && pos[ply].get_move_count() >= o->forceDrawAfter) {
            state = STATE_DRAW_ADJUDICATION;
            break;
        }

        // Prepare timeLeft[ei]
        compute_time_left(eo[ei], &(timeLeft[ei]));

        // output game/turn info
        gomocup_turn_info_command(eo[ei], timeLeft[ei], &engines[ei]);

        // trigger think!
        if (pos[ply].get_move_count() == 0) {
            engines[ei].writeln("BEGIN");
            canUseTurn[ei] = true;
        }
        else {
            if (o->useTURN && canUseTurn[ei]) {  // use TURN to trigger think
                engines[ei].writeln(
                    format("TURN %s", pos[ply].move_to_gomostr(played)).c_str());
            }
            else {  // use BOARD to trigger think
                send_board_command(&(pos[ply]), w, &(engines[ei]));
                canUseTurn[ei] = true;
            }
        }

        std::string bestmove;
        Info        moveInfo {};
        const bool  ok = engines[ei].bestmove(timeLeft[ei],
                                             eo[ei]->timeoutTurn,
                                             bestmove,
                                             moveInfo,
                                             pos[ply].get_move_count() + 1);
        this->info.push_back(moveInfo);

        if (!ok) {  // engine crashed in bestmove()
            std::printf("[%d] engine %s crashed at %d moves after opening\n",
                        w->id,
                        engines[ei].name.c_str(),
                        ply);
            state = STATE_CRASHED;
            break;
        }

        if ((eo[ei]->timeoutTurn || eo[ei]->timeoutMatch || eo[ei]->increment)
            && timeLeft[ei] < 0) {
            std::printf("[%d] engine %s timeout at %d moves after opening\n",
                        w->id,
                        engines[ei].name.c_str(),
                        ply);
            state = STATE_TIME_LOSS;
            break;
        }

        played = pos[ply].gomostr_to_move(bestmove);

        if (!pos[ply].is_legal_move(played)) {
            std::printf(
                "[%d] engine %s output illegal move at %d moves after opening: %s\n",
                w->id,
                engines[ei].name.c_str(),
                ply,
                bestmove.c_str());
            state = STATE_ILLEGAL_MOVE;
            break;
        }

        if (game_rule == RENJU && pos[ply].is_forbidden_move(played)) {
            state = STATE_FORBIDDEN_MOVE;
            break;
        }

        // Apply draw adjudication rule
        if (o->drawCount && abs(moveInfo.score) <= o->drawScore) {
            if (++drawPlyCount >= 2 * o->drawCount) {
                state = STATE_DRAW_ADJUDICATION;
                break;
            }
        }
        else {
            drawPlyCount = 0;
        }

        // Apply resign rule
        if (o->resignCount && moveInfo.score <= -o->resignScore) {
            if (++resignCount[ei] >= o->resignCount) {
                state = STATE_RESIGN;
                break;
            }
        }
        else {
            resignCount[ei] = 0;
        }

        // Write sample: position (compactly encoded) + move
        if (!o->sp.fileName.empty() && prngf(&w->seed) <= o->sp.freq) {
            Sample sample = {
                .pos    = pos[ply],
                .move   = played,
                .result = NB_RESULT  // mark as invalid for now, computed after the game
            };

            // Record sample.
            samples.push_back(sample);
        }

        pos.emplace_back();
    }

    assert(state != STATE_NONE);

    // Signed result from black's pov: -1 (loss), 0 (draw), +1 (win)
    const int wpov = state < STATE_SEPARATOR ? (pos[ply].get_turn() == BLACK
                                                    ? RESULT_LOSS
                                                    : RESULT_WIN)  // lost from turn's pov
                                             : RESULT_DRAW;

    // Fill results in samples
    if (state == STATE_TIME_LOSS || state == STATE_CRASHED
        || state == STATE_ILLEGAL_MOVE) {
        samples.clear();  // discard samples in a time loss/crash/illegal move game
    }
    else {
        for (size_t i = 0; i < samples.size(); i++)
            samples[i].result = samples[i].pos.get_turn() == WHITE ? wpov : 2 - wpov;
    }

    return state < STATE_SEPARATOR
               ? (ei == 0 ? RESULT_LOSS : RESULT_WIN)  // engine on the move has lost
               : RESULT_DRAW;
}

void Game::decode_state(std::string &result,
                        std::string &reason,
                        const char * restxt[3]) const
{
    result = restxt[RESULT_DRAW];
    reason.clear();

    // Note: pos.get_turn() returns next side to move, so when pos is a win position
    // and next side to move is <color>, then the side of win is opponent(<color>),
    // which is last moved side

    bool isBlackTurn = pos[ply].get_turn() == BLACK;

    if (state == STATE_NONE) {
        result = "*";
        reason = "Unterminated";
    }
    else if (state == STATE_FIVE_CONNECT) {
        result = isBlackTurn ? restxt[RESULT_LOSS] : restxt[RESULT_WIN];
        reason =
            isBlackTurn ? "White win by five connection" : "Black win by five connection";
    }
    else if (state == STATE_DRAW_INSUFFICIENT_SPACE)
        reason = "Draw by fullfilled board";
    else if (state == STATE_ILLEGAL_MOVE) {
        result = isBlackTurn ? restxt[RESULT_LOSS] : restxt[RESULT_WIN];
        reason = isBlackTurn ? "White win by opponent illegal move"
                             : "Black win by opponent illegal move";
    }
    else if (state == STATE_FORBIDDEN_MOVE) {
        assert(isBlackTurn);
        result = restxt[RESULT_LOSS];
        reason = "Black play on forbidden position";
    }
    else if (state == STATE_DRAW_ADJUDICATION)
        reason = "Draw by adjudication";
    else if (state == STATE_RESIGN) {
        result = isBlackTurn ? restxt[RESULT_LOSS] : restxt[RESULT_WIN];
        reason = isBlackTurn ? "White win by adjudication" : "Black win by adjudication";
    }
    else if (state == STATE_TIME_LOSS) {
        result = isBlackTurn ? restxt[RESULT_LOSS] : restxt[RESULT_WIN];
        reason = isBlackTurn ? "White win by time forfeit" : "Black win by time forfeit";
    }
    else if (state == STATE_CRASHED) {
        result = isBlackTurn ? restxt[RESULT_LOSS] : restxt[RESULT_WIN];
        reason =
            isBlackTurn ? "White win by opponent crash" : "Black win by opponent crash";
    }
    else
        assert(false);
}

std::string Game::export_pgn(size_t gameIdx, int verbosity) const
{
    // Record game id as event name for each game
    std::string out = format("[Event \"%zu\"]\n", gameIdx);

    time_t     rawtime;
    struct tm *timeinfo;
    char       timeBuffer[128];
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(timeBuffer, sizeof(timeBuffer), "[Date \"%Y.%m.%d %H:%M:%S\"]", timeinfo);
    out += format("%s\n", timeBuffer);

    out += format("[Round \"%i.%i\"]\n", round + 1, game + 1);
    out += format("[Round \"%i.%i\"]\n", round + 1, game + 1);
    out += format("[Black \"%s\"]\n", names[BLACK]);
    out += format("[White \"%s\"]\n", names[WHITE]);

    // Result in PGN format "1-0", "0-1", "1/2-1/2" (from white pov)
    const char *ResultTxt[3] = {"1-0", "1/2-1/2", "0-1"};
    std::string result, reason;
    decode_state(result, reason, ResultTxt);
    out += format("[Result \"%s\"]\n", result);
    out += format("[Termination \"%s\"]\n", reason);
    out += format("[PlyCount \"%i\"]\n", ply);

    if (verbosity > 0) {
        // Print the moves
        out.push_back('\n');

        const std::string dummyMovesStr1 = "1. d4 Nf6 2. c4 e6 3. Nf3 d5 4. Nc3 Bb4";
        const std::string dummyMovesStr2 =
            "1. d4 Nf6 2. c4 e6 3. Nf3 d5 4. Nc3 Bb4 5. Bg5";

        out += format("%s ", (this->ply) % 2 == 0 ? dummyMovesStr1 : dummyMovesStr2);
    }

    out += result;
    out += "\n\n";

    return out;
}

std::string Game::export_sgf(size_t gameIdx) const
{
    const int   movePerline = 8;
    std::string out         = "(;FF[4]GM[4]";  // common info

    // Record game id as game name for each game
    out += format("GN[%zu]", gameIdx);
    // Record engine pair as event name for each game
    out += format("EV[%s x %s]", names[BLACK], names[WHITE]);

    time_t     rawtime;
    struct tm *timeinfo;
    char       timeBuffer[128];
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(timeBuffer, sizeof(timeBuffer), "DT[%Y.%m.%d %H:%M:%S]", timeinfo);
    out += timeBuffer;

    out += format("RO[%i.%i]", round + 1, game + 1);
    out += format("RU[%i]", game_rule);
    out += format("SZ[%i]", board_size);
    // out += format("TM[%s]", "0000");
    out += format("PB[%s]", names[BLACK]);
    out += format("PW[%s]", names[WHITE]);

    // Result in SGF format "W+score", "0", "B+score"
    const char *ResultTxt[3] = {"W+1", "0", "B+1"};
    std::string result, reason;
    decode_state(result, reason, ResultTxt);
    out += format("RE[%s]", result);
    out += format("TE[%s]", reason);
    out.push_back('\n');

    // Print the moves
    const Position &lastPos = pos[ply];

    // openning moves
    int openingMoveCnt = lastPos.get_move_count() - ply;

    // played moves
    int           moveCnt  = 0;
    const move_t *histMove = lastPos.get_hist_moves();
    for (int j = 0; j < lastPos.get_move_count(); j++) {
        int thinkPly = j - openingMoveCnt;
        if (openingMoveCnt > 0 && thinkPly == 0) {
            out.push_back('\n');
        }
        if (moveCnt >= movePerline) {
            out.push_back('\n');
            moveCnt = 0;
        }
        out.push_back(';');

        Color color = ColorFromMove(histMove[j]);
        Pos   p     = PosFromMove(histMove[j]);

        char coord[3];
        coord[0] = (char)(CoordX(p) + 'a');
        coord[1] = (char)(CoordY(p) + 'a');
        coord[2] = '\0';
        if (color == BLACK) {
            out += format("B[%s]", coord);
        }
        else if (color == WHITE) {
            out += format("W[%s]", coord);
        }

        if (j < openingMoveCnt) {
            out += "C[opening move]";
        }
        else {
            // const int dep = this->info[thinkPly].depth;
            // const int scr = this->info[thinkPly].score;
            const int64_t tim = this->info[thinkPly].time;
            // str_cat_fmt(out, "C[%i/%i %Ims]", scr, dep, tim);
            out += format("C[%" PRId64 "ms]", tim);

            moveCnt++;
        }
    }

    out += ")\n\n";

    return out;
}

void Game::export_samples_csv(FILE *out) const
{
    for (size_t i = 0; i < samples.size(); i++) {
        std::string pos_str = samples[i].pos.to_opening_str(OPENING_POS);
        std::string move_str =
            samples[i].pos.move_to_opening_str(samples[i].move, OPENING_POS);
        fprintf(out, "%s,%s,%d\n", pos_str.c_str(), move_str.c_str(), samples[i].result);
    }
}

void Game::export_samples_bin(FILE *out, LZ4F_compressionContext_t lz4Ctx) const
{
    struct Entry
    {
        struct EntryHead
        {
            uint16_t boardsize : 5;  // board size
            uint16_t ply : 9;        // current number of stones on board
            uint16_t result : 2;     // final game result: 0=loss, 1=draw, 2=win
            uint16_t move;           // move output by the engine
        } head;
        uint16_t position[1024];  // move sequence that representing a position
    } e;
    const size_t bufSize = LZ4F_compressBound(sizeof(Entry), nullptr);
    char         buf[bufSize];

    for (size_t i = 0; i < samples.size(); i++) {
        int           moveply    = samples[i].pos.get_move_count();
        const move_t *hist_moves = samples[i].pos.get_hist_moves();
        assert(moveply < 1024);

        e.head.boardsize = samples[i].pos.get_size();
        e.head.ply       = moveply;
        e.head.result    = samples[i].result;
        e.head.move      = samples[i].move;
        for (int iMove = 0; iMove < moveply; iMove++) {
            e.position[iMove] = PosFromMove(hist_moves[iMove]);
        }

        const size_t entrySize = sizeof(Entry::EntryHead) + sizeof(uint16_t) * moveply;
        if (lz4Ctx) {
            size_t size =
                LZ4F_compressUpdate(lz4Ctx, buf, bufSize, &e, entrySize, nullptr);
            fwrite(buf, size, 1, out);
        }
        else {
            fwrite(&e, entrySize, 1, out);
        }
    }
}

void Game::export_samples(FILE *out, bool bin, LZ4F_compressionContext_t lz4Ctx) const
{
#ifdef __MINGW32__
    _lock_file(out);
#else
    flockfile(out);
#endif

    if (bin)
        export_samples_bin(out, lz4Ctx);
    else
        export_samples_csv(out);

#ifdef __MINGW32__
    _unlock_file(out);
#else
    funlockfile(out);
#endif
}