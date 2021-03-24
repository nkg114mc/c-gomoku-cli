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
#include "game.h"
#include "util.h"
#include "vec.h"
#include "options.h"
#include "position.h"


static void gomocup_turn_info_command(Game *g, 
                                      const EngineOptions *eo[2], 
                                      int ei, 
                                      const int64_t timeLeft[2], 
                                      str_t *cmd)
{
/*
    str_cpy_c(cmd, "");

    if (eo[ei]->nodes)
        str_cat_fmt(cmd, " nodes %I", eo[ei]->nodes);

    if (eo[ei]->depth)
        str_cat_fmt(cmd, " depth %i", eo[ei]->depth);

    if (eo[ei]->movetime)
        str_cat_fmt(cmd, " movetime %I", eo[ei]->movetime);

    if (eo[ei]->time || eo[ei]->increment) {
        const int color = g->pos[g->ply].turn;

        str_cat_fmt(cmd, " wtime %I winc %I btime %I binc %I",
            timeLeft[ei ^ color], eo[ei ^ color]->increment,
            timeLeft[ei ^ color ^ BLACK], eo[ei ^ color ^ BLACK]->increment);
    }

    if (eo[ei]->movestogo)
        str_cat_fmt(cmd, " movestogo %i",
            eo[ei]->movestogo - ((g->ply / 2) % eo[ei]->movestogo));
*/
    
    str_cpy_c(cmd, "");

    str_cat_fmt(cmd, "INFO time_left %I", 1000ULL);

/*
timeout_turn  - time limit for each move (milliseconds, 0=play as fast as possible)
timeout_match - time limit of a whole match (milliseconds, 0=no limit)
max_memory    - memory limit (bytes, 0=no limit)
     - remaining time limit of a whole match (milliseconds)

    timeout_turn  - time limit for each move (milliseconds, 0=play as fast as possible)
    timeout_match - time limit of a whole match (milliseconds, 0=no limit)
    max_memory    - memory limit (bytes, 0=no limit)
*/
    
}

static void gomocup_game_info_command(Game *g, const EngineOptions *eo[2], int ei, const Options *option, str_t *cmd)
{
/*
    str_cpy_c(cmd, "");

    if (eo[ei]->nodes)
        str_cat_fmt(cmd, " nodes %I", eo[ei]->nodes);

    if (eo[ei]->depth)
        str_cat_fmt(cmd, " depth %i", eo[ei]->depth);

    if (eo[ei]->movetime)
        str_cat_fmt(cmd, " movetime %I", eo[ei]->movetime);

    if (eo[ei]->time || eo[ei]->increment) {
        const int color = g->pos[g->ply].turn;

        str_cat_fmt(cmd, " wtime %I winc %I btime %I binc %I",
            timeLeft[ei ^ color], eo[ei ^ color]->increment,
            timeLeft[ei ^ color ^ BLACK], eo[ei ^ color ^ BLACK]->increment);
    }

    if (eo[ei]->movestogo)
        str_cat_fmt(cmd, " movestogo %i",
            eo[ei]->movestogo - ((g->ply / 2) % eo[ei]->movestogo));
*/
    
    str_cpy_c(cmd, "");

    // game info

    if (option->gameRule) {
        str_cat_fmt(cmd, "INFO rule %i", option->gameRule);
    }

    // engine specific info

    if (eo[ei]->timeoutTurn) {
        str_cat_fmt(cmd, "INFO timeout_turn %I", eo[ei]->timeoutTurn);
    }

    if (eo[ei]->timeoutMatch) {
        str_cat_fmt(cmd, "INFO timeout_match %I", eo[ei]->timeoutMatch);
    }

    if (eo[ei]->maxMemory) {
        str_cat_fmt(cmd, "INFO max_memory %I", eo[ei]->maxMemory);
    }
}

static void gomocup_board_command(Game *g, const EngineOptions *eo[2], int ei, const int64_t timeLeft[2], str_t *cmd)
{
/*
    str_cpy_c(cmd, "go");

    if (eo[ei]->nodes)
        str_cat_fmt(cmd, " nodes %I", eo[ei]->nodes);

    if (eo[ei]->depth)
        str_cat_fmt(cmd, " depth %i", eo[ei]->depth);

    if (eo[ei]->movetime)
        str_cat_fmt(cmd, " movetime %I", eo[ei]->movetime);

    if (eo[ei]->time || eo[ei]->increment) {
        const int color = g->pos[g->ply].turn;

        str_cat_fmt(cmd, " wtime %I winc %I btime %I binc %I",
            timeLeft[ei ^ color], eo[ei ^ color]->increment,
            timeLeft[ei ^ color ^ BLACK], eo[ei ^ color ^ BLACK]->increment);
    }

    if (eo[ei]->movestogo)
        str_cat_fmt(cmd, " movestogo %i",
            eo[ei]->movestogo - ((g->ply / 2) % eo[ei]->movestogo));
*/
}

// Applies rules to generate legal moves, and determine the state of the game
static int game_apply_rules(const Game *g, std::vector<move_t> legal_moves, std::vector<move_t> forbidden_moves)
{
    Position *pos = &g->pos[g->ply];

    pos->gen_all_legal_moves(legal_moves);
    pos->compute_forbidden_moves(forbidden_moves);

    bool allow_long_connection = true;
    if (g->game_rule == RENJU) {
        allow_long_connection = false;
    }

    if (pos->check_five_in_line_lastmove(allow_long_connection)) {
        return STATE_FIVE_CONNECT;
    } else if (legal_moves.size() == 0) {
        return STATE_DRAW_INSUFFICIENT_SPACE;
    }

    // game does not end
    return STATE_NONE;
}

// moves are legal move, which are "good move"s
static bool illegal_move(move_t move, const std::vector<move_t> moves)
{
    for (size_t i = 0; i < (moves.size()); i++) {
        if (moves[i] == move) {
            return false; // found it in legal moves, ok
        }
    }
    return true; // not ok
}

// moves are forbidden move, which are "bad move"s
static bool forbidden_move(move_t move, const std::vector<move_t> moves)
{
    for (size_t i = 0; i < moves.size(); i++) {
        if (moves[i] == move) {
            return true; // is a forbidden move, not ok
        }
    }
    return false; // ok
}

void Game::game_init(int round, int game)
{
    round = round;
    game = game;

    names[BLACK] = str_init();
    names[WHITE] = str_init();

    pos = vec_init(Position);
    info = vec_init(Info);
    samples = vec_init(Sample);
}

bool Game::game_load_fen(const char *fen, int *color, const Options *o)
{
    //vec_push(pos, (Position){0}, Position);
    Position p0(o->boardSize);
    vec_push(pos, p0, Position);
/*
    if (pos_set(&pos[0], fen, false, &sfen)) {
        *color = pos[0].turn;
        return true;
    } else
        return false;
*/
    return true;
}

void Game::game_destroy()
{
    vec_destroy(samples);
    vec_destroy(info);
    vec_destroy(pos);

    str_destroy_n(&names[BLACK], &names[WHITE]);
}

void Game::send_board_set_go() {

}

int Game::game_play(Worker *w, const Options *o, Engine engines[2],
    const EngineOptions *eo[2], bool reverse)
// Play a game:
// - engines[reverse] plays the first move (which does not mean white, that depends on the FEN)
// - sets state value: see enum STATE_* codes
// - returns RESULT_LOSS/DRAW/WIN from engines[0] pov
{
    for (int color = BLACK; color <= WHITE; color++) {
        str_cpy(&names[color], engines[color ^ pos[0].get_turn() ^ reverse].name);
    }

    for (int i = 0; i < 2; i++) {
        // send game info
        scope(str_destroy) str_t infoCmd = str_init();
        gomocup_game_info_command(this, eo, i, o, &infoCmd);
        engines[i].engine_writeln(w, infoCmd.buf);
        // tell engine to start a new game
        scope(str_destroy) str_t startCmd = str_init();
        str_cpy_c(&startCmd, "");
        str_cat_fmt(&startCmd, "START %i", o->boardSize);
        engines[i].engine_writeln(w, startCmd.buf);
        engines[i].engine_wait_for_ok(w);
    }

    scope(str_destroy) str_t cmd = str_init(), best = str_init();
    move_t played = 0;
    int drawPlyCount = 0;
    int resignCount[NB_COLOR] = {0};
    int ei = reverse;  // engines[ei] has the move
    int64_t timeLeft[2] = {eo[0]->time, eo[1]->time};
    scope(str_destroy) str_t pv = str_init();


    // the starting position has been added at game_load_fen()

    for (ply = 0; ; ei = (1 - ei), ply++) {
        std::vector<move_t> legalMoves;
        std::vector<move_t> forbiddenMoves;

        if (played != 0) {
            Position::pos_move_with_copy(&pos[ply], &pos[ply - 1], played);
        }

        pos[ply].pos_print();

        state = game_apply_rules(this, legalMoves, forbiddenMoves);
        if (state > STATE_NONE) {
            break;
        }
/*
        // this should be replaced by YXBOARD
        uci_position_command(this, &cmd); 
        engines[ei].engine_writeln(w, cmd.buf);
        //engines[ei].engine_sync(w);
        engines[i].engine_wait_for_ok(w);
*/
        // Prepare timeLeft[ei]
        if (eo[ei]->movetime) {
            // movetime is special: discard movestogo, time, increment
            timeLeft[ei] = eo[ei]->movetime;
        } else if (eo[ei]->time || eo[ei]->increment) {
            // Always apply increment (can be zero)
            timeLeft[ei] += eo[ei]->increment;

            // movestogo specific clock reset event
            if (eo[ei]->movestogo && ply > 1 && ((ply / 2) % eo[ei]->movestogo) == 0) {
                timeLeft[ei] += eo[ei]->time;
            }
        } else {
            // Only depth and/or nodes limit
            timeLeft[ei] = INT64_MAX / 2;  // HACK: system_msec() + timeLeft must not overflow
        }

        // output game/turn info
        //uci_go_command(this, eo, ei, timeLeft, &cmd);
        //engines[ei].engine_writeln(w, cmd.buf);
        gomocup_turn_info_command(this, eo, ei, timeLeft, &cmd);
        if (strlen(cmd.buf) > 0) {
            engines[ei].engine_writeln(w, cmd.buf);
        }
        
        // trigger think!
        if (ply == 0) {
            engines[ei].engine_writeln(w, "BEGIN");
        } else {
            if (false) { // use BOARD to trigger think

            } else { // use TURN to trigger think
                std::string last_move_str = pos[ply].move_to_gomostr(played);
                printf("Get move str [%s]\n", last_move_str.c_str());
                std::string turn_cmd = std::string("TURN ") + last_move_str;
                char tmp[32];
                strcpy(tmp, turn_cmd.c_str());
                engines[ei].engine_writeln(w, tmp);
            }
        }

        Info info = {0};
        const bool ok = engines[ei].engine_bestmove(w, &timeLeft[ei], &best, &pv, &info);
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

        if (forbidden_move(played, forbiddenMoves)) {
            state = STATE_FORBIDDEN_MOVE;
            break;
        }

        //if (illegal_move(played, legalMoves)) {
        if (!pos[ply].is_legal_move(played)) {
            state = STATE_ILLEGAL_MOVE;
            break;
        }

        if ((eo[ei]->time || eo[ei]->increment || eo[ei]->movetime) && timeLeft[ei] < 0) {
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

        legalMoves.clear();
        forbiddenMoves.clear();
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

void Game::game_decode_state(str_t *result, str_t *reason)
{
    str_cpy_c(result, "1/2-1/2");
    str_clear(reason);

    if (state == STATE_NONE) {
        str_cpy_c(result, "*");
        str_cpy_c(reason, "Unterminated");
    } else if (state == STATE_FIVE_CONNECT) {
        str_cpy_c(result, pos[ply].get_turn() == BLACK ? "0-1" : "1-0");
        str_cpy_c(reason, pos[ply].get_turn() == BLACK ? "White win by five connection" : 
                                                         "Black win by five connection");
    } else if (state == STATE_DRAW_INSUFFICIENT_SPACE)
        str_cpy_c(reason, "Draw by fullfilled board");
    else if (state == STATE_ILLEGAL_MOVE) {
        str_cpy_c(result, pos[ply].get_turn() == BLACK ? "0-1" : "1-0");
        str_cpy_c(reason, pos[ply].get_turn() == BLACK ? "White win by opponent illegal move" :
                                                         "Black win by opponent illegal move");
    } else if (state == STATE_FORBIDDEN_MOVE) {
        //str_cpy_c(result, pos[ply].turn == BLACK ? "0-1" : "1-0");
        assert(pos[ply].get_turn() == BLACK);
        str_cpy_c(result, "0-1");
        str_cpy_c(reason, "black play on forbidden position");
    } else if (state == STATE_DRAW_ADJUDICATION)
        str_cpy_c(reason, "Draw by adjudication");
    else if (state == STATE_RESIGN) {
        str_cpy_c(result, pos[ply].get_turn() == BLACK ? "0-1" : "1-0");
        str_cpy_c(reason, pos[ply].get_turn() == BLACK ? "White win by adjudication" :
                                                         "Black win by adjudication");
    } else if (state == STATE_TIME_LOSS) {
        str_cpy_c(result, pos[ply].get_turn() == BLACK ? "0-1" : "1-0");
        str_cpy_c(reason, pos[ply].get_turn() == BLACK ? "White win by time forfeit" : 
                                                         "Black win by time forfeit");
    } else
        assert(false);
}


void Game::game_export_pgn(int verbosity, str_t *out)
{
/*
    str_cpy_fmt(out, "[Round \"%i.%i\"]\n", round + 1, game + 1);
    str_cat_fmt(out, "[White \"%S\"]\n", names[WHITE]);
    str_cat_fmt(out, "[Black \"%S\"]\n", names[BLACK]);

    // Result in PGN format "1-0", "0-1", "1/2-1/2" (from white pov)
    scope(str_destroy) str_t result = str_init(), reason = str_init();
    game_decode_state(g, &result, &reason);
    str_cat_fmt(out, "[Result \"%S\"]\n", result);
    str_cat_fmt(out, "[Termination \"%S\"]\n", reason);

    scope(str_destroy) str_t fen = str_init();
    pos_get(&pos[0], &fen, sfen);
    str_cat_fmt(out, "[FEN \"%S\"]\n", fen);

    if (pos[0].chess960)
        str_cat_c(out, "[Variant \"Chess960\"]\n");

    str_cat_fmt(out, "[PlyCount \"%i\"]\n", ply);
    scope(str_destroy) str_t san = str_init();

    if (verbosity > 0) {
        // Print the moves
        str_push(out, '\n');

        const int pliesPerLine = verbosity == 2 ? 6
            : verbosity == 3 ? 5
            : 16;

        for (int ply = 1; ply <= ply; ply++) {
            // Write move number
            if (pos[ply - 1].turn == WHITE || ply == 1)
                str_cat_fmt(out, pos[ply - 1].turn == WHITE ? "%i. " : "%i... ",
                    pos[ply - 1].fullMove);

            // Append SAN move
            pos_move_to_san(&pos[ply - 1], pos[ply].lastMove, &san);
            str_cat(out, san);

            // Append check marker
            if (pos[ply].checkers) {
                if (ply == ply && state == STATE_CHECKMATE)
                    str_push(out, '#');  // checkmate
                else
                    str_push(out, '+');  // normal check
            }

            // Write PGN comment
            const int depth = info[ply - 1].depth, score = info[ply - 1].score;

            if (verbosity == 2) {
                if (score > INT_MAX / 2)
                    str_cat_fmt(out, " {M%i/%i}", INT_MAX - score, depth);
                else if (score < INT_MIN / 2)
                    str_cat_fmt(out, " {-M%i/%i}", score - INT_MIN, depth);
                else
                    str_cat_fmt(out, " {%i/%i}", score, depth);
            } else if (verbosity == 3) {
                const int64_t time = info[ply - 1].time;

                if (score > INT_MAX / 2)
                    str_cat_fmt(out, " {M%i/%i %Ims}", INT_MAX - score, depth, time);
                else if (score < INT_MIN / 2)
                    str_cat_fmt(out, " {-M%i/%i %Ims}", score - INT_MIN, depth, time);
                else
                    str_cat_fmt(out, " {%i/%i %Ims}", score, depth, time);
            }

            // Append delimiter
            str_push(out, ply % pliesPerLine == 0 ? '\n' : ' ');
        }
    }

    str_cat_c(str_cat(out, result), "\n\n");
*/
}

/*
static Position resolve_pv(const Worker *w, const Game *g, const char *pv)
{
    scope(str_destroy) str_t token = str_init();
    const char *tail = pv;

    // Start with current position. We can't guarantee that the resolved position won't be in check,
    // but a valid one must be returned.
    Position resolved = g->pos[g->ply];

    Position p[2];
    p[0] = resolved;
    int idx = 0;
    move_t *moves = vec_init_reserve(64, move_t);

    while ((tail = str_tok(tail, &token, " "))) {
        const move_t m = pos_lan_to_move(&p[idx], token.buf);
        moves = gen_all_moves(&p[idx], moves);

        if (illegal_move(m, moves)) {
            printf("[%d] WARNING: Illegal move in PV '%s%s' from %s\n", w->id, token.buf, tail,
                g->names[g->pos[g->ply].turn].buf);

            if (w->log)
                DIE_IF(w->id, fprintf(w->log, "WARNING: illegal move in PV '%s%s'\n", token.buf,
                    tail) < 0);

            break;
        }

        pos_move(&p[(idx + 1) % 2], &p[idx], m);
        idx = (idx + 1) % 2;

        if (!p[idx].checkers)
            resolved = p[idx];
    }

    vec_destroy(moves);
    return resolved;
}
*/

void Game::game_export_sgf(int verbosity, str_t *out)
{

}
