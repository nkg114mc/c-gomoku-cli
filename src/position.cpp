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
#include <iostream>
#include <sstream>
#include <string>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "position.h"
#include "util.h"

const uint16_t POS_MASK = 0x03FF;

inline Pos POS_R(uint8_t x = 0, uint8_t y = 0) { return (x << MAX_BOARD_SIZE_BIT) + y; }
inline Pos POS(uint8_t x = 0, uint8_t y = 0) { return POS_R(x + BOARD_BOUNDARY, y + BOARD_BOUNDARY); }
inline uint8_t CoordX(Pos p) { return (p >> MAX_BOARD_SIZE_BIT) - BOARD_BOUNDARY; }
inline uint8_t CoordY(Pos p) { return (p & ((1 << MAX_BOARD_SIZE_BIT) - 1)) - BOARD_BOUNDARY; }
inline Pos getPosFromMove(move_t move) { return (Pos)(move & POS_MASK); }
inline int getXFromMove(move_t move) { return CoordX(getPosFromMove(move)); }
inline int getYFromMove(move_t move) { return CoordY(getPosFromMove(move)); }
inline Color getColorFromMove(move_t move) { return (Color)(move >> 10); }

inline move_t buildMove(int x, int y, Color side) { 
    assert(side == WHITE || side == BLACK);
    Pos p = POS(x, y);
    move_t m = (side << 10) | p;
    return m;
}
inline move_t buildMoveReal(int x, int y, Color side) {
    assert(side == WHITE || side == BLACK);
    Pos preal = POS_R(x, y);
    move_t m = (side << 10) | preal;
    return m;
}

Color OPPSITE_COLOR[4] = {
	BLACK, WHITE, EMPTY, WALL
};

Color opponent_color(Color c) {
	return OPPSITE_COLOR[c];
}

void Position::initBoard(int size) {
    boardSize = size;
	boardSizeSqr = boardSize * boardSize;
	moveCount = 0;
	playerToMove = BLACK;
	key = (uint64_t)0;
    for (int i = 0; i < MaxBoardSizeSqr; i++) {
		board[i] = (CoordX(i) >= 0 && CoordX(i) < boardSize && CoordY(i) >= 0 && CoordY(i) < boardSize) ? EMPTY : WALL;
	}
}

// init without size change
void Position::clear() {
    int oldSize = boardSize;
    initBoard(oldSize);
}

Position::Position() {
	initBoard(15);
}

Position::Position(int bSize) {
	initBoard(bSize);
}

void Position::move(move_t m) {
    Pos pos = getPosFromMove(m);
	setPiece(pos, playerToMove);
	historyMoves[moveCount] = pos;
	playerToMove = opponent_color(playerToMove);
    key ^= zobristTurn[playerToMove];
	moveCount++;
}

void Position::undo() {
	assert(moveCount > 0);
	moveCount--;
	delPiece(historyMoves[moveCount]);
    key ^= zobristTurn[playerToMove];
	playerToMove = opponent_color(playerToMove);
}

// Prints the position in ASCII 'art' (for debugging)
void Position::pos_print() const
{
   	std::cout << "  ";
	for (int i = 0; i < boardSize; i++) {
		std::cout << "--";
	}
	std::cout << std::endl;
	
	for (int j = 0; j < boardSize; j++) {
		std::cout << "  ";
		for (int i = 0; i < boardSize; i++) {
			Color piece = board[POS(i, j)];
			std::string ch = ". ";
			if (piece == WALL) {
				ch = "# ";
			} else if (piece == BLACK) {
				ch = "X ";
			} else if (piece == WHITE) {
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

void Position::setPiece(Pos pos, Color piece) {
	assert(isInBoard(pos));
	assert(board[pos] == EMPTY);
	board[pos] = piece;
	key ^= zobristPc[piece][pos];
}

void Position::delPiece(Pos pos) {
	assert(isInBoard(pos));
	assert(board[pos] == WHITE || board[pos] == BLACK);
	key ^= zobristPc[board[pos]][pos];
	board[pos] = EMPTY;
}

void Position::gen_all_legal_moves(std::vector<move_t> &legal_moves) const {
    legal_moves.clear();
    
    for (int i = 0; i < boardSize; i++) {
        for (int j = 0; j < boardSize; j++) {
            Pos p = POS(i, j);
            if (board[p] == EMPTY) {
                move_t m = buildMove(i, j, playerToMove);
                legal_moves.push_back(m);
            }
        }
    }
}

bool Position::isInBoard(Pos pos) const {
	assert(pos < MaxBoardSizeSqr);
	return (board[pos] != WALL);
}

bool Position::is_legal_move(move_t move) const {
    Pos movePos = getPosFromMove(move);
    Color moveSide = getColorFromMove(move);

    if (isInBoard(movePos)) {
        if (board[movePos] == EMPTY) {
            //if (moveSide = playerToMove) {
                // ok
                return true;
            //}
        }
    }
    return false; // not ok
}

void Position::compute_forbidden_moves(std::vector<move_t> &forbidden_moves) const {
    // TODO: implement this funciton
    // do nothing so far

}

void check_five_helper(bool allow_long_connc, int &conCnt, int & fiveCnt) {
    if (allow_long_connc) {
        if (conCnt >= 5) {
            fiveCnt++;
        }
    } else {
        if (conCnt == 5) {
            fiveCnt++;
        }
    }
}

// check if there exist any line-of-n-piece-in-same-color exists for side-to-move
// if allow_long_connection, return true if n >= 5
// if allow_long_connection, return true if and only if n == 5
bool Position::check_five_in_line_side(Color side, bool allow_long_connection) const {
    assert(side == WHITE || side == BLACK);

    if (moveCount >= 10) {
        return true;
    }

    int i, j;
    int fiveCount = 0;

    for (i = 0; i < boardSize; i++) {
        int continueCount = 0;
        for (j = 0; j < boardSize; j++) {
            Pos p = POS(i, j);
            if (board[p] == side) {
                continueCount++;
            } else {
                check_five_helper(allow_long_connection, continueCount, fiveCount);
                continueCount = 0;
            }
        }
        check_five_helper(allow_long_connection, continueCount, fiveCount);
    }

    for (j = 0; j < boardSize; j++) {
        int continueCount = 0;        
        for (i = 0; i < boardSize; i++) {
            Pos p = POS(i, j);
            if (board[p] == side) {
                continueCount++;
            } else {
                check_five_helper(allow_long_connection, continueCount, fiveCount);
                continueCount = 0;
            }
        }
        check_five_helper(allow_long_connection, continueCount, fiveCount);
    }

    
    for (i = 0; i < boardSize; i++) {
        int continueCount = 0;
        for (j = 0; j < boardSize; j++) {
            Pos p = POS(i, j);
            if (board[p] == side) {
                continueCount++;
            } else {
                check_five_helper(allow_long_connection, continueCount, fiveCount);
                continueCount = 0;
            }
        }
        check_five_helper(allow_long_connection, continueCount, fiveCount);
    }



    return false;
}
    
move_t Position::gomostr_to_move(char *move_str) const {
	std::string mvstr = std::string(move_str);	
    
    int commaCount = 0;
    int commaIdx = 0;
    for (int i = 0; i < mvstr.length(); i++) {
        if (mvstr[i] == ',') {
            commaIdx = i;
            commaCount++;
        }
    }

    assert(commaCount == 1);

    int firstLen = commaIdx;
    std::string xstr = mvstr.substr(0, firstLen);
    int secondLen = mvstr.length() - commaIdx - 1;
    std::string ystr = mvstr.substr(commaIdx + 1, secondLen);
    std::cout << xstr << " " << ystr << std::endl;

    int x = std::stoi(xstr);
    int y = std::stoi(ystr);
    assert(x >= 0 && x < boardSize);
    assert(y >= 0 && y < boardSize);

    return buildMove(x, y, playerToMove);
}

static bool isNumber(std::string &str) {
    char* p;
    long converted = strtol(str.c_str(), &p, 10);
    if (*p) {
       return false;
    }
    return true;
}

bool Position::is_valid_move_gomostr(char *move_str) {
    int i;
    std::string mvstr = std::string(move_str);

    int commaCount = 0;
    int commaIdx = 0;
    for (i = 0; i < mvstr.length(); i++) {
        if (mvstr[i] == ',') {
            commaIdx = i;
            commaCount++;
        }
    }

    if (commaCount != 1) {
        return false; // no comma, or more than one comma?
    }

    int firstLen = commaIdx;
    std::string xstr = mvstr.substr(0, firstLen);
    int secondLen = mvstr.length() - commaIdx - 1;
    std::string ystr = mvstr.substr(commaIdx + 1, secondLen);
    if ((!isNumber(xstr)) || (!isNumber(ystr))) {
        return false; // any of two coords are not number
    }

    return true;
}


std::string Position::move_to_gomostr(move_t move) const {
    int x = getXFromMove(move);
    int y = getYFromMove(move);
    std::stringstream ss("");
    ss << x << "," << y;
    return ss.str();
}

// this is a static method
void Position::pos_move_with_copy(Position *after, const Position *before, move_t m) {
    memcpy(after, before, sizeof(Position));
    after->move(m);
}


// zobrist

uint64_t zobristPc[4][Position::MaxBoardSizeSqr];
uint64_t zobristTurn[4];

uint64_t get_rnd64() {
  uint64_t r1 = rand();
  uint64_t r2 = rand();
  uint64_t r3 = rand();
  uint64_t r4 = rand();
  uint64_t r = (r1 << 48) | (r2 << 32) | (r3 << 16) | (r4);
  return r;
}

void initZobrish() {
    for (int i = 0; i < Position::MaxBoardSizeSqr; i++) {
        for (int j = 0; j < 4; j++) {
            if (j == EMPTY || j == WALL) {
                zobristPc[j][i] = 0;
            } else {
                zobristPc[j][i] = get_rnd64();
            }
        }
    }
    zobristTurn[EMPTY] = 0;
    zobristTurn[BLACK] = 0;
    zobristTurn[WHITE] = get_rnd64();
    zobristTurn[WALL]  = 0;
}
