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

#include <cassert>
#include <string>
#include <string_view>
#include <vector>

typedef uint16_t move_t;
typedef uint16_t Pos;

const move_t NONE_MOVE = 0xFFFF;

enum Color { BLACK, WHITE, EMPTY, WALL };

#define BOARD_BOUNDARY     5
#define MAX_BOARD_SIZE_BIT 5
#define NB_COLOR           2

enum GameRule { GOMOKU_FIVE_OR_MORE = 0, GOMOKU_EXACT_FIVE = 1, RENJU = 4 };

const int      RULES_COUNT        = 3;
const GameRule ALL_VALID_RULES[3] = {GOMOKU_FIVE_OR_MORE, GOMOKU_EXACT_FIVE, RENJU};

enum OpeningType { OPENING_OFFSET, OPENING_POS };

enum TransformType {
    IDENTITY,    // (x, y) -> (x, y)
    ROTATE_90,   // (x, y) -> (y, s - x)
    ROTATE_180,  // (x, y) -> (s - x, s - y)
    ROTATE_270,  // (x, y) -> (s - y, x)
    FLIP_X,      // (x, y) -> (x, s - y)
    FLIP_Y,      // (x, y) -> (s - x, y)
    FLIP_XY,     // (x, y) -> (y, x)
    FLIP_YX,     // (x, y) -> (s - y, s - x)
    NB_TRANS
};

class Position
{
public:
    static const int MaxBoardSize    = 1 << MAX_BOARD_SIZE_BIT;
    static const int MaxBoardSizeSqr = MaxBoardSize * MaxBoardSize;
    static const int RealBoardSize   = MaxBoardSize - 2 * BOARD_BOUNDARY;

    Position(int bSize = 15);

    inline int           get_size() const { return boardSize; }
    inline Color         get_turn() const { return playerToMove; }
    inline int           get_move_count() const { return moveCount; }
    inline int           get_moves_left() const { return boardSizeSqr - moveCount; }
    inline const move_t *get_hist_moves() const { return historyMoves; }

    void move(move_t m);
    void undo();

    void transform(TransformType type);

    move_t      gomostr_to_move(std::string_view movestr) const;
    std::string move_to_gomostr(move_t move) const;
    std::string move_to_opening_str(move_t move, OpeningType type) const;

    void clear();
    void pos_print() const;

    bool is_legal_move(move_t move) const;
    bool is_forbidden_move(move_t move) const;

    bool check_five_in_line_side(Color side,
                                 bool  allow_long_connection = true);  // const;
    bool check_five_in_line_lastmove(bool allow_long_connection);     // const;

    // about opening
    bool        apply_opening(std::string_view opening_str, OpeningType type);
    std::string to_opening_str(OpeningType type) const;

    // static methods
    static void pos_move_with_copy(Position *after, const Position *before, move_t m);
    static bool is_valid_move_gomostr(std::string_view movestr);

private:
    Color    board[MaxBoardSizeSqr];
    int      boardSize;
    int      boardSizeSqr;
    int      moveCount;
    move_t   historyMoves[MaxBoardSizeSqr];
    Color    playerToMove;
    uint64_t key;
    int      winConnectionLen;
    Pos      winConnectionPos[32];

    void initBoard(int size);
    void setPiece(Pos pos, Color piece);
    void delPiece(Pos pos);
    bool isInBoard(Pos pos) const;
    bool isInBoardXY(int x, int y) const;

    void check_five_helper(bool allow_long_connc,
                           int &conCnt,
                           int &fiveCnt,
                           Pos *connectionLine);
    bool parse_opening_offset_linestr(std::vector<Pos> &opening_pos,
                                      std::string_view  linestr);
    bool parse_opening_pos_linestr(std::vector<Pos> &opening_pos,
                                   std::string_view  linestr);

    // renju helpers
    enum OpenFourType { OF_NONE, OF_TRUE /*_OOOO_*/, OF_LONG /*O_OOO_O*/ };
    bool         isForbidden(Pos pos);
    bool         isFive(Pos pos, Color piece);
    bool         isFive(Pos pos, Color piece, int iDir);
    bool         isOverline(Pos pos, Color piece);
    bool         isFour(Pos pos, Color piece, int iDir);
    OpenFourType isOpenFour(Pos pos, Color piece, int iDir);
    bool         isOpenThree(Pos pos, Color piece, int iDir);
    bool         isDoubleFour(Pos pos, Color piece);
    bool         isDoubleThree(Pos pos, Color piece);
};

inline Color oppositeColor(Color color)
{
    assert(color == WHITE || color == BLACK);
    int c = color;
    return (Color)(c ^ 0x1);  // branchless for: color == WHITE ? BLACK : WHITE
}

inline Pos POS_RAW(uint8_t x = 0, uint8_t y = 0)
{
    return (x << MAX_BOARD_SIZE_BIT) + y;
}
inline Pos POS(uint8_t x = 0, uint8_t y = 0)
{
    return POS_RAW(x + BOARD_BOUNDARY, y + BOARD_BOUNDARY);
}
inline int CoordX(Pos p)
{
    return (p >> MAX_BOARD_SIZE_BIT) - BOARD_BOUNDARY;
}
inline int CoordY(Pos p)
{
    return (p & ((1 << MAX_BOARD_SIZE_BIT) - 1)) - BOARD_BOUNDARY;
}
inline Pos PosFromMove(move_t move)
{
    return (Pos)(move & 0x03FF);
}
inline Color ColorFromMove(move_t move)
{
    return (Color)(move >> 10);
}

extern uint64_t zobristPc[4][Position::MaxBoardSizeSqr];
extern uint64_t zobristTurn[4];

void initZobrish();
