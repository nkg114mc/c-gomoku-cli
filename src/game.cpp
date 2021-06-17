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

#include <limits.h>
#include <cstring>
#include <string>
#include <ctime>
#include <iostream>
#include "game.h"
#include "util.h"
#include "vec.h"
#include "options.h"
#include "position.h"

// Applies rules to generate legal moves, and determine the state of the game
static int game_apply_rules(const Game *g, move_t lastmove)
{
    Position *pos = &g->pos[g->ply];

    bool allow_long_connection = true;
    if (g->game_rule == GOMOKU_EXACT_FIVE) {
        allow_long_connection = false;
    }
    else if (g->game_rule == RENJU) {
        if (ColorFromMove(lastmove) == BLACK)
            allow_long_connection = false;
    }

    if (pos->check_five_in_line_lastmove(allow_long_connection)) {
        return STATE_FIVE_CONNECT;
    } else if (pos->get_moves_left() == 0) {
        return STATE_DRAW_INSUFFICIENT_SPACE;
    }

    // game does not end
    return STATE_NONE;
}

void Game::game_init(int rd, int gm)
{
    this->round = rd;
    this->game = gm;

    this->names[BLACK] = str_init();
    this->names[WHITE] = str_init();

    this->pos = vec_init(Position);
    this->info = vec_init(Info);
    this->samples = vec_init(Sample);
}

bool Game::game_load_fen(str_t *fen, int *color, const Options *o)
{
    //vec_push(pos, (Position){0}, Position);
    Position p0(o->boardSize);
    vec_push(pos, p0, Position);

    if (pos[0].apply_opening(*fen, o->openingType)) {
        *color = pos[0].get_turn();
        return true;
    } else {
        return false;
    }
}

void Game::game_destroy()
{
    vec_destroy(samples);
    vec_destroy(info);
    vec_destroy(pos);

    str_destroy_n(&names[BLACK], &names[WHITE]);
}

void Game::gomocup_turn_info_command(const EngineOptions *eo, 
                                     const int64_t timeLeft, 
                                     Worker *w, 
                                     Engine *engine)
{
    scope(str_destroy) str_t cmd = str_init();

    str_cpy_c(&cmd, "");
    str_cat_fmt(&cmd, "INFO time_left %I", timeLeft);
    engine->engine_writeln(w, cmd.buf);
}

void Game::gomocup_game_info_command(const EngineOptions *eo,
                                     const Options *option, 
                                     Worker *w, 
                                     Engine *engine)
{
    scope(str_destroy) str_t cmd = str_init();

    // game info
    str_cpy_c(&cmd, "");
    str_cat_fmt(&cmd, "INFO rule %i", option->gameRule);
    engine->engine_writeln(w, cmd.buf);

    // time control info
    if (eo->timeoutTurn) {
        str_cpy_c(&cmd, "");
        str_cat_fmt(&cmd, "INFO timeout_turn %I", eo->timeoutTurn);
        engine->engine_writeln(w, cmd.buf);
    }

    // always send match timeout info (0 means no limit in match time)
    str_cpy_c(&cmd, "");
    str_cat_fmt(&cmd, "INFO timeout_match %I", eo->timeoutMatch);
    engine->engine_writeln(w, cmd.buf);

    if (eo->depth) {
        str_cpy_c(&cmd, "");
        str_cat_fmt(&cmd, "INFO max_depth %i", eo->depth);
        engine->engine_writeln(w, cmd.buf);
    }

    if (eo->nodes) {
        str_cpy_c(&cmd, "");
        str_cat_fmt(&cmd, "INFO max_node %I", eo->nodes);
        engine->engine_writeln(w, cmd.buf);
    }

    // memory limit info
    str_cpy_c(&cmd, "");
    str_cat_fmt(&cmd, "INFO max_memory %I", eo->maxMemory);
    engine->engine_writeln(w, cmd.buf);

    // multi threading info
    if (eo->numThreads > 1) {
        str_cpy_c(&cmd, "");
        str_cat_fmt(&cmd, "INFO thread_num %i", eo->numThreads);
        engine->engine_writeln(w, cmd.buf);
    }

    // custom info
    scope(str_destroy) str_t left = str_init(), right = str_init();
    for (size_t i = 0; i < vec_size(eo->options); i++) {
        str_tok(str_tok(eo->options[i].buf, &left, "="), &right, "=");

        str_cpy_c(&cmd, "");
        str_cat_fmt(&cmd, "INFO %S %S", left, right);
        engine->engine_writeln(w, cmd.buf);
    }
}

void Game::send_board_command(Position *pos, Worker *w, Engine *engine)
{
    engine->engine_writeln(w, "BOARD");

    int moveCnt = pos->get_move_count();
    move_t *histMoves = pos->get_hist_moves();

    // make sure last color is 2 according to piskvork protocol
    auto colorToGomocupStoneIdx = [lastColor = ColorFromMove(histMoves[moveCnt-1])](Color c) {
        return c == lastColor ? 2 : 1;
    };

    for (int i = 0; i < moveCnt; i++) {
        Color color = ColorFromMove(histMoves[i]);
        int gomocupColorIdx = colorToGomocupStoneIdx(color);
        Pos p = PosFromMove(histMoves[i]);
        scope(str_destroy) str_t cmd = str_init();
        str_cpy_c(&cmd, "");
        str_cat_fmt(&cmd, "%i,%i,%i", CoordX(p), CoordY(p), gomocupColorIdx);
        engine->engine_writeln(w, cmd.buf);
    }

    engine->engine_writeln(w, "DONE");
}

void Game::compute_time_left(const EngineOptions *eo, int64_t *timeLeft) {
    if (eo->timeoutMatch > 0) {
        // add increment to time left if increment is set
        if (eo->increment > 0)
            *timeLeft += eo->increment;
    } else {
        *timeLeft = 2147483647LL;
    }
}

int Game::game_play(Worker *w, const Options *o, Engine engines[2],
    const EngineOptions *eo[2], bool reverse)
// Play a game:
// - engines[reverse] plays the first move (which does not mean white, that depends on the FEN)
// - sets state value: see enum STATE_* codes
// - returns RESULT_LOSS/DRAW/WIN from engines[0] pov
{
    // initialize game rule
    this->game_rule = (GameRule)(o->gameRule);
    this->board_size = o->boardSize;

    for (int color = BLACK; color <= WHITE; color++) {
        str_cpy(&names[color], engines[color ^ pos[0].get_turn() ^ reverse].name);
    }

    for (int i = 0; i < 2; i++) {
        // tell engine to start a new game
        scope(str_destroy) str_t startCmd = str_init();
        str_cpy_c(&startCmd, "");
        str_cat_fmt(&startCmd, "START %i", o->boardSize);
        engines[i].engine_writeln(w, startCmd.buf);
        engines[i].engine_wait_for_ok(w);

        // send game info
        gomocup_game_info_command(eo[i], o, w, &(engines[i]));
    }

    scope(str_destroy) str_t cmd = str_init(), best = str_init();
    move_t played = NONE_MOVE;
    int drawPlyCount = 0;
    int resignCount[NB_COLOR] = {0};
    int ei = reverse;  // engines[ei] has the move
    int64_t timeLeft[2] = {0LL, 0LL};//{eo[0]->time, eo[1]->time};
    bool canUseTurn[2] = {false, false};

    scope(str_destroy) str_t pv = str_init();

    // init time control
    timeLeft[0] = eo[0]->timeoutMatch;
    timeLeft[1] = eo[1]->timeoutMatch;

    // the starting position has been added at game_load_fen()

    for (ply = 0; ; ei = (1 - ei), ply++) {
        if (played != NONE_MOVE) {
            Position::pos_move_with_copy(&pos[ply], &pos[ply - 1], played);
        }

        if (o->debug) {
            pos[ply].pos_print();
        }

        state = game_apply_rules(this, played);
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
        gomocup_turn_info_command(eo[ei], timeLeft[ei], w, &(engines[ei]));
        
        // trigger think!
        if (pos[ply].get_move_count() == 0) {
            engines[ei].engine_writeln(w, "BEGIN");
            canUseTurn[ei] = true;
        } else {
            if (o->useTURN && canUseTurn[ei]) { // use TURN to trigger think
                str_cpy_c(&cmd, "");
                str_cat_fmt(&cmd, "TURN %s", pos[ply].move_to_gomostr(played).c_str());
                engines[ei].engine_writeln(w, cmd.buf);
            } else { // use BOARD to trigger think
                send_board_command(&(pos[ply]), w, &(engines[ei]));
                canUseTurn[ei] = true;
            }
        }

        Info info = {0};
        const bool ok = engines[ei].engine_bestmove(w, &timeLeft[ei], eo[ei]->timeoutTurn,
                                                    &best, &pv, &info, pos[ply].get_move_count() + 1);
        vec_push(this->info, info, Info);

        // Parses the last PV sent. An invalid PV is not fatal, but logs some warnings. Keep track
        // of the resolved position, which is the last in the PV that is not in check (or the
        // current one if that's impossible).
        //Position resolved = resolve_pv(w, g, pv.buf);

        if (!ok) {  // engine_bestmove() time out before parsing a bestmove
            state = STATE_TIME_LOSS;
            break;
        }

        played = pos[ply].gomostr_to_move(best.buf);

        if (!pos[ply].is_legal_move(played)) {
            std::cout << "Illegal move: " << best.buf << std::endl;
            state = STATE_ILLEGAL_MOVE;
            break;
        }

        if (game_rule == RENJU && pos[ply].is_forbidden_move(played)) {
            state = STATE_FORBIDDEN_MOVE;
            break;
        }

        if ((eo[ei]->timeoutTurn || eo[ei]->timeoutMatch || eo[ei]->increment) && timeLeft[ei] < 0) {
            state = STATE_TIME_LOSS;
            break;
        }

        // Apply draw adjudication rule
        if (o->drawCount && abs(info.score) <= o->drawScore) {
            if (++drawPlyCount >= 2 * o->drawCount) {
                state = STATE_DRAW_ADJUDICATION;
                break;
            }
        } else {
            drawPlyCount = 0;
        }

        // Apply resign rule
        if (o->resignCount && info.score <= -o->resignScore) {
            if (++resignCount[ei] >= o->resignCount) {
                state = STATE_RESIGN;
                break;
            }
        } else {
            resignCount[ei] = 0;
        }

        vec_push(pos, (Position){0}, Position);
    }

    assert(state != STATE_NONE);

    // Signed result from black's pov: -1 (loss), 0 (draw), +1 (win)
    const int wpov = state < STATE_SEPARATOR
        ? (pos[ply].get_turn() == BLACK ? RESULT_LOSS : RESULT_WIN)  // lost from turn's pov
        : RESULT_DRAW;

    return state < STATE_SEPARATOR
        ? (ei == 0 ? RESULT_LOSS : RESULT_WIN)  // engine on the move has lost
        : RESULT_DRAW;
}

void Game::game_decode_state(str_t *result, str_t *reason, const char* restxt[3])
{
    const char* DefaultResultTxt[3] = {
        "0-1", "1/2-1/2", "1-0"
    };
    if (!restxt)
        restxt = DefaultResultTxt;

    str_cpy_c(result, restxt[RESULT_DRAW]);
    str_clear(reason);

    // Note: pos.get_turn() returns next side to move, so when pos is a win position
    // and next side to move is <color>, then the side of win is opponent(<color>),
    // which is last moved side

    if (state == STATE_NONE) {
        str_cpy_c(result, "*");
        str_cpy_c(reason, "Unterminated");
    } else if (state == STATE_FIVE_CONNECT) {
        str_cpy_c(result, pos[ply].get_turn() == BLACK ? restxt[RESULT_LOSS] : restxt[RESULT_WIN]);
        str_cpy_c(reason, pos[ply].get_turn() == BLACK ? "White win by five connection" : 
                                                         "Black win by five connection");
    } else if (state == STATE_DRAW_INSUFFICIENT_SPACE)
        str_cpy_c(reason, "Draw by fullfilled board");
    else if (state == STATE_ILLEGAL_MOVE) {
        str_cpy_c(result, pos[ply].get_turn() == BLACK ? restxt[RESULT_LOSS] : restxt[RESULT_WIN]);
        str_cpy_c(reason, pos[ply].get_turn() == BLACK ? "White win by opponent illegal move" :
                                                         "Black win by opponent illegal move");
    } else if (state == STATE_FORBIDDEN_MOVE) {
        assert(pos[ply].get_turn() == BLACK);
        str_cpy_c(result, restxt[RESULT_LOSS]);
        str_cpy_c(reason, "Black play on forbidden position");
    } else if (state == STATE_DRAW_ADJUDICATION)
        str_cpy_c(reason, "Draw by adjudication");
    else if (state == STATE_RESIGN) {
        str_cpy_c(result, pos[ply].get_turn() == BLACK ? restxt[RESULT_LOSS] : restxt[RESULT_WIN]);
        str_cpy_c(reason, pos[ply].get_turn() == BLACK ? "White win by adjudication" :
                                                         "Black win by adjudication");
    } else if (state == STATE_TIME_LOSS) {
        str_cpy_c(result, pos[ply].get_turn() == BLACK ? restxt[RESULT_LOSS] : restxt[RESULT_WIN]);
        str_cpy_c(reason, pos[ply].get_turn() == BLACK ? "White win by time forfeit" : 
                                                         "Black win by time forfeit");
    } else
        assert(false);
}


void Game::game_export_pgn(size_t gameIdx, int verbosity, str_t *out)
{
    // Record game id as event name for each game
    str_cat_fmt(out, "[Event \"%I\"]\n", gameIdx);
    //str_cat_fmt(out, "[Site \"?\"]\n");

    time_t rawtime;
    struct tm * timeinfo;
    char timeBuffer[128];
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(timeBuffer, sizeof(timeBuffer), "[Date \"%Y.%m.%d %H:%M:%S\"]", timeinfo);
    str_cat_fmt(out, "%s\n", timeBuffer);

    str_cat_fmt(out, "[Round \"%i.%i\"]\n", round + 1, game + 1);
    str_cat_fmt(out, "[Black \"%S\"]\n", names[BLACK]);
    str_cat_fmt(out, "[White \"%S\"]\n", names[WHITE]);

    // Result in PGN format "1-0", "0-1", "1/2-1/2" (from white pov)
    scope(str_destroy) str_t result = str_init(), reason = str_init();
    game_decode_state(&result, &reason);
    str_cat_fmt(out, "[Result \"%S\"]\n", result);
    str_cat_fmt(out, "[Termination \"%S\"]\n", reason);

    str_cat_fmt(out, "[PlyCount \"%i\"]\n", ply);

    if (verbosity > 0) {
        // Print the moves
        str_push(out, '\n');

        const std::string dummyMovesStr1 = "1. d4 Nf6 2. c4 e6 3. Nf3 d5 4. Nc3 Bb4";
        const std::string dummyMovesStr2 = "1. d4 Nf6 2. c4 e6 3. Nf3 d5 4. Nc3 Bb4 5. Bg5";
        
        std::string dummyMoves = "";
        if ((this->ply) % 2 == 0) {
            dummyMoves = dummyMovesStr1;
        } else {
            dummyMoves = dummyMovesStr2;
        }
        str_cat_fmt(out, "%s ", dummyMoves.c_str());
    }

    str_cat_c(str_cat(out, result), "\n\n");
}

void Game::game_export_sgf(size_t gameIdx, str_t *out)
{
    const int movePerline = 8;

    str_cat_c(out, "(");
    str_cat_c(out, ";FF[4]GM[4]"); // common info
    
    // Record game id as game name for each game
    str_cat_fmt(out, "GN[%I]", gameIdx);
    // Record engine pair as event name for each game
    str_cat_fmt(out, "EV[%S x %S]", names[BLACK], names[WHITE]);
    time_t rawtime;
    struct tm * timeinfo;
    char timeBuffer[128];
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(timeBuffer, sizeof(timeBuffer), "DT[%Y.%m.%d %H:%M:%S]", timeinfo);
    str_cat_fmt(out, "%s", timeBuffer);
    str_cat_fmt(out, "RO[%i.%i]", round + 1, game + 1);
    str_cat_fmt(out, "RU[%i]", game_rule);
    str_cat_fmt(out, "SZ[%i]", board_size);
    //str_cat_fmt(out, "TM[%s]", "0000");
    str_cat_fmt(out, "PB[%S]", names[BLACK]);
    str_cat_fmt(out, "PW[%S]", names[WHITE]);

    // Result in SGF format "W+score", "0", "B+score"
    const char* ResultTxt[3] = { "W+1", "0", "B+1" };
    scope(str_destroy) str_t result = str_init(), reason = str_init();
    game_decode_state(&result, &reason, ResultTxt);
    str_cat_fmt(out, "RE[%S]", result);
    str_cat_fmt(out, "TE[%S]", reason);
    str_push(out, '\n');

    // Print the moves
    Position* lastPos = &(pos[ply]);
    
    // openning moves
    int openingMoveCnt = lastPos->get_move_count() - ply;

    // played moves
    int moveCnt = 0;
    move_t* histMove = lastPos->get_hist_moves();
    for (int j = 0; j < lastPos->get_move_count(); j++) {
        int thinkPly = j - openingMoveCnt;
        if (openingMoveCnt > 0 && thinkPly == 0) {
            str_push(out, '\n');
        }
        if (moveCnt >= movePerline) {
            str_push(out, '\n');
            moveCnt = 0;
        }
        str_push(out, ';');
    
        Color color = ColorFromMove(histMove[j]);
        Pos p = PosFromMove(histMove[j]);
    
        char coord[3];
        coord[0] = (char)(CoordX(p) + 'a');
        coord[1] = (char)(CoordY(p) + 'a');
        coord[2] = '\0';
        if (color == BLACK) {
            str_cat_fmt(out, "B[%s]", coord);
        } else if (color == WHITE) {
            str_cat_fmt(out, "W[%s]", coord);
        }

        if (j < openingMoveCnt) {
            str_cat_c(out, "C[opening move]");
        } else {
            const int dep = this->info[thinkPly].depth;
            const int scr = this->info[thinkPly].score;
            const int64_t tim = this->info[thinkPly].time;
            //str_cat_fmt(out, "C[%i/%i %Ims]", scr, dep, tim);
            str_cat_fmt(out, "C[%Ims]", tim);

            moveCnt++;
        }
    }

    str_cat_c(out, ")\n\n");
}
