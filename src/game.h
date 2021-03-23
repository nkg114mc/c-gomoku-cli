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
#pragma once
#include "position.h"
#include "engine.h"
#include "options.h"
#include "str.h"

enum {
    STATE_NONE,

    // All possible ways to lose
    STATE_FIVE_CONNECT,  // lost by being checkmated
    STATE_TIME_LOSS,  // lost on time
    STATE_ILLEGAL_MOVE,  // lost by playing an illegal move
    STATE_FORBIDDEN_MOVE,  // lost by playing on a forbidden position
    STATE_RESIGN,  // resigned on behalf of the engine

    STATE_SEPARATOR,  // invalid result, just a market to separate losses from draws

    // All possible ways to draw
    STATE_DRAW_INSUFFICIENT_SPACE,  // draw due to insufficien empty position on board
    STATE_DRAW_ADJUDICATION  // draw by adjudication
};

class Sample {
    Position pos;
    int score;  // score returned by the engine (in cp)
    int result;  // game result from pos.turn's pov
};

class Game {
public:
    str_t names[NB_COLOR];  // names of players, by color
    Position *pos;  // list of positions (including moves) since game start
    Info *info;  // remembered from parsing info lines (for PGN comments)
    Sample *samples;  // list of samples when generating training data
    GameRule game_rule; // rule is gomoku or renju, etc
    int round, game, ply, state;
    bool sfen;  // use S-FEN for this game (ie. HAha instead of KQkq)
    char pad[7];
    int board_size;
    

    void game_init(int round, int game);
    void game_destroy();

    void send_board_set_go();
    bool game_load_fen(const char *fen, int *color, const Options *o);

    int game_play(Worker *w, const Options *o, Engine engines[2],
        const EngineOptions *eo[2], bool reverse);

    void game_decode_state(str_t *result, str_t *reason);
    void game_export_pgn(int verbosity, str_t *out);
    //void game_export_samples(str_t *out);
    void game_export_sgf(int verbosity, str_t *out);
};

