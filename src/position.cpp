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

#include "position.h"

#include "util.h"

#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

typedef int16_t Direction;
const Direction DIRECTION[4] = {1,
                                Position::MaxBoardSize - 1,
                                Position::MaxBoardSize,
                                Position::MaxBoardSize + 1};

inline move_t buildMove(int x, int y, Color side)
{
    assert(side == WHITE || side == BLACK);
    Pos    p = POS(x, y);
    move_t m = (side << 10) | p;
    return m;
}

inline move_t buildMovePos(Pos p, Color side)
{
    assert(side == WHITE || side == BLACK);
    move_t m = (side << 10) | p;
    return m;
}

inline Color opponent_color(Color c)
{
    static const Color OPPSITE_COLOR[4] = {WHITE, BLACK, EMPTY, WALL};
    return OPPSITE_COLOR[c];
}

inline Pos transformPos(Pos p, int boardsize, TransformType type)
{
    int x = CoordX(p), y = CoordY(p);
    int s = boardsize - 1;
    switch (type) {
    case ROTATE_90:  // (x, y) -> (y, s - x)
        return POS(y, s - x);
    case ROTATE_180:  // (x, y) -> (s - x, s - y)
        return POS(s - x, s - y);
    case ROTATE_270:  // (x, y) -> (s - y, x)
        return POS(s - y, x);
    case FLIP_X:  // (x, y) -> (x, s - y)
        return POS(x, s - y);
    case FLIP_Y:  // (x, y) -> (s - x, y)
        return POS(s - x, y);
    case FLIP_XY:  // (x, y) -> (y, x)
        return POS(y, x);
    case FLIP_YX:  // (x, y) -> (s - y, s - x)
        return POS(s - y, s - x);
    default: return POS(x, y);
    }
}

void Position::initBoard(int size)
{
    boardSize    = size;
    boardSizeSqr = boardSize * boardSize;
    moveCount    = 0;
    playerToMove = BLACK;
    key          = (uint64_t)0;
    for (int i = 0; i < MaxBoardSizeSqr; i++) {
        board[i] = (CoordX(i) >= 0 && CoordX(i) < boardSize && CoordY(i) >= 0
                    && CoordY(i) < boardSize)
                       ? EMPTY
                       : WALL;
    }
    winConnectionLen = 0;
}

// init without size change
void Position::clear()
{
    int oldSize = boardSize;
    initBoard(oldSize);
}

Position::Position(int bSize)
{
    assert(bSize > 0 && bSize <= RealBoardSize);
    initBoard(bSize);
}

void Position::move(move_t m)
{
    Pos pos = PosFromMove(m);
    setPiece(pos, playerToMove);
    historyMoves[moveCount] = m;
    playerToMove            = opponent_color(playerToMove);
    key ^= zobristTurn[playerToMove];
    moveCount++;
}

void Position::undo()
{
    assert(moveCount > 0);
    moveCount--;
    Pos lastPos = PosFromMove(historyMoves[moveCount]);
    delPiece(lastPos);
    key ^= zobristTurn[playerToMove];
    playerToMove = opponent_color(playerToMove);
}

void Position::transform(TransformType type)
{
    // Skip identity transform
    if (type == IDENTITY)
        return;

    // Clear previous board stones
    Color tmpBoard[MaxBoardSizeSqr];
    memcpy(tmpBoard, board, sizeof(tmpBoard));
    for (int x = 0; x < boardSize; x++)
        for (int y = 0; y < boardSize; y++) {
            Pos pos = POS(x, y);
            if (board[pos] != EMPTY)
                delPiece(pos);  // delete prev stone if exists
        }

    // Transform all board cells and zobrist key
    for (int x = 0; x < boardSize; x++)
        for (int y = 0; y < boardSize; y++) {
            Pos pos            = POS(x, y);
            Pos transformedPos = transformPos(pos, boardSize, type);
            if (tmpBoard[pos] != EMPTY)
                setPiece(transformedPos, tmpBoard[pos]);
        }

    // Transform all history moves
    for (int i = 0; i < moveCount; i++) {
        move_t move            = historyMoves[i];
        Pos    pos             = PosFromMove(move);
        Color  color           = ColorFromMove(move);
        Pos    transformedPos  = transformPos(pos, boardSize, type);
        move_t transformedMove = buildMovePos(transformedPos, color);
        historyMoves[i]        = transformedMove;
    }

    // Transform all win connection
    for (int i = 0; i < winConnectionLen; i++) {
        winConnectionPos[i] = transformPos(winConnectionPos[i], boardSize, type);
    }
}

// Prints the position in ASCII 'art' (for debugging)
void Position::pos_print() const
{
    std::cout << "  ";
    for (int i = 0; i < boardSize; i++) {
        std::cout << "--";
    }
    std::cout << std::endl;

    Color bd2[1024];
    memcpy(bd2, board, 1024 * sizeof(Color));

    for (int i = 0; i < winConnectionLen; i++) {
        bd2[winConnectionPos[i]] = WALL;
    }

    for (int j = 0; j < boardSize; j++) {
        std::cout << "  ";
        for (int i = 0; i < boardSize; i++) {
            Color       piece = bd2[POS(i, j)];
            std::string ch    = ". ";
            if (piece == WALL) {
                ch = "# ";
            }
            else if (piece == BLACK) {
                ch = "X ";
            }
            else if (piece == WHITE) {
                ch = "O ";
            }
            std::cout << ch;
        }
        std::cout << std::endl;
    }

    std::cout << "  ";
    for (int i = 0; i < boardSize; i++) {
        std::cout << "--";
    }
    std::cout << std::endl;
}

void Position::setPiece(Pos pos, Color piece)
{
    assert(isInBoard(pos));
    assert(board[pos] == EMPTY);
    board[pos] = piece;
    key ^= zobristPc[piece][pos];
}

void Position::delPiece(Pos pos)
{
    assert(isInBoard(pos));
    assert(board[pos] == WHITE || board[pos] == BLACK);
    key ^= zobristPc[board[pos]][pos];
    board[pos] = EMPTY;
}

bool Position::isInBoard(Pos pos) const
{
    assert(pos < MaxBoardSizeSqr);
    return (board[pos] != WALL);
}

bool Position::isInBoardXY(int x, int y) const
{
    return (x >= 0 && x < boardSize) && (y >= 0 && y < boardSize);
}

bool Position::is_legal_move(move_t move) const
{
    Pos movePos = PosFromMove(move);

    if (isInBoard(movePos) && board[movePos] == EMPTY) {
        return true;
    }
    else {
        // std::cout << board[movePos] << std::endl;
        // std::cout << CoordX(movePos) << " " << CoordY(movePos) << std::endl;
        return false;  // not ok
    }
}

ForbiddenType Position::check_forbidden_move(move_t move) const
{
    Pos   pos   = PosFromMove(move);
    Color color = ColorFromMove(move);
    if (color != BLACK)
        return FORBIDDEN_NONE;

    // Check forbidden point using recursive finder
    // Note that forbidden point finder needs an empty pos to judge.
    assert(board[pos] == EMPTY);
    return const_cast<Position *>(this)->isForbidden(pos);
}

void Position::check_five_helper(bool allow_long_connc,
                                 int &conCnt,
                                 int &fiveCnt,
                                 Pos *connectionLine)
{
    bool foundFive = false;
    if (allow_long_connc) {
        if (conCnt >= 5) {
            fiveCnt++;
            foundFive = true;
        }
    }
    else {
        if (conCnt == 5) {
            fiveCnt++;
            foundFive = true;
        }
    }

    if (foundFive) {
        memcpy(winConnectionPos, connectionLine, conCnt * sizeof(Pos));
        winConnectionLen = conCnt;
    }
}

// check if there exist any line-of-n-piece-in-same-color exists for side-to-move
// if allow_long_connection, return true if n >= 5
// if allow_long_connection, return true if and only if n == 5
bool Position::check_five_in_line_side(Color side, bool allow_long_connection)
{  // const {
    assert(side == WHITE || side == BLACK);

    int i, j, k;
    int fiveCount = 0;
    Pos connectionLine[32];

    for (i = 0; i < boardSize; i++) {
        int continueCount = 0;
        for (j = 0; j < boardSize; j++) {
            Pos p = POS(i, j);
            if (board[p] == side) {
                continueCount++;
                connectionLine[continueCount - 1] = p;
            }
            else {
                check_five_helper(allow_long_connection,
                                  continueCount,
                                  fiveCount,
                                  connectionLine);
                continueCount = 0;
            }
        }
        check_five_helper(allow_long_connection,
                          continueCount,
                          fiveCount,
                          connectionLine);
    }

    for (j = 0; j < boardSize; j++) {
        int continueCount = 0;
        for (i = 0; i < boardSize; i++) {
            Pos p = POS(i, j);
            if (board[p] == side) {
                continueCount++;
                connectionLine[continueCount - 1] = p;
            }
            else {
                check_five_helper(allow_long_connection,
                                  continueCount,
                                  fiveCount,
                                  connectionLine);
                continueCount = 0;
            }
        }
        check_five_helper(allow_long_connection,
                          continueCount,
                          fiveCount,
                          connectionLine);
    }

    for (k = -(boardSize - 1); k < boardSize; k++) {
        if (k <= 0) {
            i = 0;
            j = -k;
        }
        else {
            i = k;
            j = 0;
        }
        int continueCount = 0;
        while (isInBoard(POS(i, j))) {
            Pos p = POS(i, j);
            if (board[p] == side) {
                continueCount++;
                connectionLine[continueCount - 1] = p;
            }
            else {
                check_five_helper(allow_long_connection,
                                  continueCount,
                                  fiveCount,
                                  connectionLine);
                continueCount = 0;
            }
            i += 1;
            j += 1;
        }
        check_five_helper(allow_long_connection,
                          continueCount,
                          fiveCount,
                          connectionLine);
    }

    for (k = 0; k < (boardSize * 2 - 1); k++) {
        i                 = std::min(k, boardSize - 1);
        j                 = k - i;
        int continueCount = 0;
        while (isInBoard(POS(i, j))) {
            Pos p = POS(i, j);
            if (board[p] == side) {
                continueCount++;
                connectionLine[continueCount - 1] = p;
            }
            else {
                check_five_helper(allow_long_connection,
                                  continueCount,
                                  fiveCount,
                                  connectionLine);
                continueCount = 0;
            }
            i -= 1;
            j += 1;
        }
        check_five_helper(allow_long_connection,
                          continueCount,
                          fiveCount,
                          connectionLine);
    }

    assert(fiveCount <= 1);
    if (fiveCount > 0) {
        return true;
    }
    return false;
}

bool Position::check_five_in_line_lastmove(bool allow_long_connection)
{  // const {
    if (moveCount < 5) {
        return false;
    }
    Pos   lastPos   = PosFromMove(historyMoves[moveCount - 1]);
    Color lastPiece = board[lastPos];
    return check_five_in_line_side(lastPiece, allow_long_connection);
}

move_t Position::gomostr_to_move(std::string_view movestr) const
{
    int    commaCount = 0;
    size_t commaIdx   = 0;
    for (size_t i = 0; i < movestr.length(); i++) {
        if (movestr[i] == ',') {
            commaIdx = i;
            commaCount++;
        }
    }

    assert(commaCount == 1);

    size_t      secondLen = movestr.length() - commaIdx - 1;
    std::string xstr {movestr.substr(0, commaIdx)};
    std::string ystr {movestr.substr(commaIdx + 1, secondLen)};

    int x = std::stoi(xstr);
    int y = std::stoi(ystr);
    assert(x >= 0 && x < boardSize);
    assert(y >= 0 && y < boardSize);

    return buildMove(x, y, playerToMove);
}

static bool isNumber(std::string &str)
{
    char *p;
    strtol(str.c_str(), &p, 10);
    return !*p;
}

bool Position::is_valid_move_gomostr(std::string_view movestr)
{
    int    commaCount = 0;
    size_t commaIdx   = 0;
    for (size_t i = 0; i < movestr.length(); i++) {
        if (movestr[i] == ',') {
            commaIdx = i;
            commaCount++;
        }
    }

    if (commaCount != 1) {
        return false;  // no comma, or more than one comma?
    }

    size_t      secondLen = movestr.length() - commaIdx - 1;
    std::string xstr {movestr.substr(0, commaIdx)};
    std::string ystr {movestr.substr(commaIdx + 1, secondLen)};

    // any of two coords are not number
    return isNumber(xstr) && isNumber(ystr);
}

std::string Position::move_to_gomostr(move_t move) const
{
    int               x = CoordX(PosFromMove(move));
    int               y = CoordY(PosFromMove(move));
    std::stringstream ss("");
    ss << x << "," << y;
    return ss.str();
}

std::string Position::move_to_opening_str(move_t move, OpeningType type) const
{
    std::stringstream ss;
    int               hboardSize = this->boardSize / 2;
    Pos               p          = PosFromMove(move);

    switch (type) {
    case OPENING_OFFSET:
        ss << (CoordX(p) - hboardSize) << "," << (CoordY(p) - hboardSize);
        break;
    case OPENING_POS: ss << char(CoordX(p) + 'a') << int(CoordY(p) + 1); break;
    }

    return ss.str();
}

// apply the openning str in the specific format
bool Position::apply_opening(std::string_view opening_str, OpeningType type)
{
    std::vector<Pos> openning_pos;
    bool             parsingOk = false;
    switch (type) {
    case OPENING_OFFSET:
        parsingOk = parse_opening_offset_linestr(openning_pos, opening_str);
        break;
    case OPENING_POS:
        parsingOk = parse_opening_pos_linestr(openning_pos, opening_str);
        break;
    }
    if (!parsingOk) {
        return false;
    }

    clear();  // set board to init
    for (size_t i = 0; i < openning_pos.size(); i++) {
        move_t mv = buildMovePos(openning_pos[i], this->get_turn());
        move(mv);  // make opening move
    }
    return true;
}

bool Position::parse_opening_offset_linestr(std::vector<Pos> &opening_pos,
                                            std::string_view  linestr)
{
    opening_pos.clear();
    int hboardSize = this->boardSize / 2;

    std::stringstream ss;
    for (size_t i = 0; i < linestr.size(); i++) {
        char ch = linestr[i];
        if ((ch <= '9' && ch >= '0') || ch == '-') {
            ss << ch;
        }
        else if (ch == ',' || ch == ' ') {
            ss << ' ';
        }
        else {
            printf("Can not apply openning, unknown coordinate '%c'.\n", ch);
            return false;
        }
    }

    int cnt  = 0;
    int ofst = -9999;
    int buff[3];
    while (ss >> ofst) {
        if (ofst != -9999) {
            if (ofst >= -16 && ofst <= 15) {
                buff[cnt++] = ofst;
                if (cnt == 2) {
                    int currx = buff[0] + hboardSize;
                    int curry = buff[1] + hboardSize;
                    if (!isInBoardXY(currx, curry)) {
                        printf(
                            "Can not apply openning, the current board is too small.\n");
                        return false;
                    }

                    Pos p = POS(currx, curry);
                    opening_pos.push_back(p);

                    cnt = 0;
                }
            }
        }
        ofst = -9999;
    }

    return true;  // ok
}

bool Position::parse_opening_pos_linestr(std::vector<Pos> &opening_pos,
                                         std::string_view  linestr)
{
    opening_pos.clear();

    std::stringstream ss;
    for (size_t i = 0; i < linestr.size(); i++) {
        char ch = linestr[i];

        if (ch >= 'a' && ch <= 'z') {
            ss << ' ' << int(ch - 'a') << ' ';
        }
        else if (ch >= '0' && ch <= '9') {
            ss << ch;
        }
        else {
            printf("Can not apply openning, unknown coordinate '%c'.\n", ch);
            return false;
        }
    }

    int cnt = 0;
    int buff[3];
    int coord = -9999;
    while (ss >> coord) {
        if (coord != -9999) {
            buff[cnt++] = coord;
            if (cnt == 2) {
                int currx = buff[0];
                int curry = buff[1] - 1;
                if (!isInBoardXY(currx, curry)) {
                    printf("Can not apply openning, the current board is too small.\n");
                    return false;
                }

                Pos p = POS(currx, curry);
                opening_pos.push_back(p);
                cnt = 0;
            }
        }
        coord = -9999;
    }

    return true;  // ok
}

// convert a position back to opening string (assuming current position is
// a normal position, played by black and white alternately)
std::string Position::to_opening_str(OpeningType type) const
{
    std::stringstream ss;
    int               hboardSize = this->boardSize / 2;

    switch (type) {
    case OPENING_OFFSET:
        for (int i = 0; i < get_move_count(); i++) {
            Pos p = PosFromMove(get_hist_moves()[i]);
            if (i)
                ss << ", ";
            ss << (CoordX(p) - hboardSize) << "," << (CoordY(p) - hboardSize);
        }
        break;
    case OPENING_POS:
        for (int i = 0; i < get_move_count(); i++) {
            Pos p = PosFromMove(get_hist_moves()[i]);
            ss << char(CoordX(p) + 'a') << int(CoordY(p) + 1);
        }
        break;
    }

    return ss.str();
}

// this is a static method
void Position::pos_move_with_copy(Position *after, const Position *before, move_t m)
{
    memcpy(after, before, sizeof(Position));
    after->move(m);
}

// renju helpers
ForbiddenType Position::isForbidden(Pos pos)
{
    if (isDoubleThree(pos, BLACK))
        return DOUBLE_THREE;
    else if (isDoubleFour(pos, BLACK))
        return DOUBLE_FOUR;
    else if (isOverline(pos, BLACK))
        return OVERLINE;
    else
        return FORBIDDEN_NONE;
}

bool Position::isFive(Pos pos, Color piece)
{
    if (board[pos] != EMPTY)
        return false;

    for (int iDir = 0; iDir < 4; iDir++) {
        if (isFive(pos, piece, iDir))
            return true;
    }
    return false;
}

bool Position::isFive(Pos pos, Color piece, int iDir)
{
    if (board[pos] != EMPTY)
        return false;

    int i, j;
    int count = 1;
    for (i = 1; i < 6; i++) {
        if (board[pos - DIRECTION[iDir] * i] == piece)
            count++;
        else
            break;
    }
    for (j = 1; j < 7 - i; j++) {
        if (board[pos + DIRECTION[iDir] * j] == piece)
            count++;
        else
            break;
    }
    return count == 5;
}

bool Position::isOverline(Pos pos, Color piece)
{
    if (board[pos] != EMPTY)
        return false;

    for (Direction dir : DIRECTION) {
        int i, j;
        int count = 1;
        for (i = 1; i < 6; i++) {
            if (board[pos - dir * i] == piece)
                count++;
            else
                break;
        }
        for (j = 1; j < 7 - i; j++) {
            if (board[pos + dir * j] == piece)
                count++;
            else
                break;
        }
        if (count > 5)
            return true;
    }
    return false;
}

bool Position::isFour(Pos pos, Color piece, int iDir)
{
    if (board[pos] != EMPTY)
        return false;
    else if (isFive(pos, piece))
        return false;
    else if (piece == BLACK && isOverline(pos, BLACK))
        return false;
    else if (piece == BLACK || piece == WHITE) {
        bool four = false;
        setPiece(pos, piece);

        int i, j;
        for (i = 1; i < 5; i++) {
            Pos posi = pos - DIRECTION[iDir] * i;
            if (board[posi] == piece)
                continue;
            else if (board[posi] == EMPTY && isFive(posi, piece, iDir))
                four = true;
            break;
        }
        for (j = 1; !four && j < 6 - i; j++) {
            Pos posi = pos + DIRECTION[iDir] * j;
            if (board[posi] == piece)
                continue;
            else if (board[posi] == EMPTY && isFive(posi, piece, iDir))
                four = true;
            break;
        }

        delPiece(pos);
        return four;
    }
    else
        return false;
}

Position::OpenFourType Position::isOpenFour(Pos pos, Color piece, int iDir)
{
    if (board[pos] != EMPTY)
        return OF_NONE;
    else if (isFive(pos, piece))
        return OF_NONE;
    else if (piece == BLACK && isOverline(pos, BLACK))
        return OF_NONE;
    else if (piece == BLACK || piece == WHITE) {
        setPiece(pos, piece);

        int i, j;
        int count = 1;
        int five  = 0;

        for (i = 1; i < 5; i++) {
            Pos posi = pos - DIRECTION[iDir] * i;
            if (board[posi] == piece) {
                count++;
                continue;
            }
            else if (board[posi] == EMPTY)
                five += isFive(posi, piece, iDir);
            break;
        }
        for (j = 1; five && j < 6 - i; j++) {
            Pos posi = pos + DIRECTION[iDir] * j;
            if (board[posi] == piece) {
                count++;
                continue;
            }
            else if (board[posi] == EMPTY)
                five += isFive(posi, piece, iDir);
            break;
        }

        delPiece(pos);
        return five == 2 ? (count == 4 ? OF_TRUE : OF_LONG) : OF_NONE;
    }
    else
        return OF_NONE;
}

bool Position::isOpenThree(Pos pos, Color piece, int iDir)
{
    if (board[pos] != EMPTY)
        return false;
    else if (isFive(pos, piece))
        return false;
    else if (piece == BLACK && isOverline(pos, BLACK))
        return false;
    else if (piece == BLACK || piece == WHITE) {
        bool openthree = false;
        setPiece(pos, piece);

        int i, j;
        for (i = 1; i < 5; i++) {
            Pos posi = pos - DIRECTION[iDir] * i;
            if (board[posi] == piece)
                continue;
            else if (board[posi] == EMPTY && isOpenFour(posi, piece, iDir) == OF_TRUE
                     && !isDoubleFour(posi, piece) && !isDoubleThree(posi, piece))
                openthree = true;
            break;
        }
        for (j = 1; !openthree && j < 6 - i; j++) {
            Pos posi = pos + DIRECTION[iDir] * j;
            if (board[posi] == piece)
                continue;
            else if (board[posi] == EMPTY && isOpenFour(posi, piece, iDir) == OF_TRUE
                     && !isDoubleFour(posi, piece) && !isDoubleThree(posi, piece))
                openthree = true;
            break;
        }

        delPiece(pos);
        return openthree;
    }
    else
        return false;
}

bool Position::isDoubleFour(Pos pos, Color piece)
{
    if (board[pos] != EMPTY)
        return false;
    else if (isFive(pos, piece))
        return false;

    int nFour = 0;
    for (int iDir = 0; iDir < 4; iDir++) {
        if (isOpenFour(pos, piece, iDir) == OF_LONG)
            nFour += 2;
        else if (isFour(pos, piece, iDir))
            nFour++;

        if (nFour >= 2)
            return true;
    }

    return false;
}

bool Position::isDoubleThree(Pos pos, Color piece)
{
    if (board[pos] != EMPTY)
        return false;
    else if (isFive(pos, piece))
        return false;

    int nThree = 0;
    for (int iDir = 0; iDir < 4; iDir++) {
        if (isOpenThree(pos, piece, iDir))
            nThree++;

        if (nThree >= 2)
            return true;
    }

    return false;
}

// zobrist

uint64_t zobristPc[4][Position::MaxBoardSizeSqr];
uint64_t zobristTurn[4];

uint64_t get_rnd64()
{
    uint64_t r1 = rand();
    uint64_t r2 = rand();
    uint64_t r3 = rand();
    uint64_t r4 = rand();
    uint64_t r  = (r1 << 48) | (r2 << 32) | (r3 << 16) | (r4);
    return r;
}

void initZobrish()
{
    for (int i = 0; i < Position::MaxBoardSizeSqr; i++) {
        for (int j = 0; j < 4; j++) {
            if (j == EMPTY || j == WALL) {
                zobristPc[j][i] = 0;
            }
            else {
                zobristPc[j][i] = get_rnd64();
            }
        }
    }
    zobristTurn[EMPTY] = 0;
    zobristTurn[BLACK] = 0;
    zobristTurn[WHITE] = get_rnd64();
    zobristTurn[WALL]  = 0;
}
