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

#pragma once
#include "engine.h"
#include "extern/lz4frame.h"
#include "options.h"
#include "position.h"

#include <string>
#include <string_view>
#include <vector>

enum {
    STATE_NONE,

    // All possible ways to lose
    STATE_FIVE_CONNECT,    // lost by being checkmated
    STATE_TIME_LOSS,       // lost on time
    STATE_CRASHED,         // lost by crashing in the middle of a game
    STATE_ILLEGAL_MOVE,    // lost by playing an illegal move
    STATE_FORBIDDEN_MOVE,  // lost by playing on a forbidden position
    STATE_RESIGN,          // resigned on behalf of the engine

    STATE_SEPARATOR,  // invalid result, just a market to separate losses from draws

    // All possible ways to draw
    STATE_DRAW_INSUFFICIENT_SPACE,  // draw due to insufficien empty position on board
    STATE_DRAW_ADJUDICATION         // draw by adjudication
};

struct Sample
{
    Position pos;
    move_t   move;    // move returned by the engine
    int      result;  // game result from pos.turn's pov
};

class Game
{
public:
    std::string           names[NB_COLOR];  // names of players, by color
    std::vector<Position> pos;   // list of positions (including moves) since game start
    std::vector<Info>     info;  // remembered from parsing info lines (for PGN comments)
    std::vector<Sample>   samples;    // list of samples when generating training data
    GameRule              game_rule;  // rule is gomoku or renju, etc
    int                   round, game, ply, state, board_size;
    Worker *const         w;

    Game(int round, int game, Worker *worker);

    bool load_opening(std::string_view opening_str,
                      const Options &  o,
                      size_t           currentRound,
                      Color &          color);
    int
    play(const Options &o, Engine engines[2], const EngineOptions *eo[2], bool reverse);

    void
    decode_state(std::string &result, std::string &reason, const char *restxt[3]) const;
    std::string export_pgn(size_t gameIdx, int verbosity) const;
    std::string export_sgf(size_t gameIdx) const;
    void
    export_samples(FILE *out, bool bin, LZ4F_compressionContext_t lz4Ctx = nullptr) const;

private:
    int  game_apply_rules(move_t lastmove);
    void compute_time_left(const EngineOptions &eo, int64_t &timeLeft);
    void send_board_command(const Position &position, Engine &engine);
    void gomocup_turn_info_command(const EngineOptions &eo,
                                   const int64_t        timeLeft,
                                   Engine &             engine);
    void gomocup_game_info_command(const EngineOptions &eo,
                                   const Options &      option,
                                   Engine &             engine);
    void export_samples_csv(FILE *out) const;
    void export_samples_bin(FILE *out, LZ4F_compressionContext_t lz4Ctx) const;
};
