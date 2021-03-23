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
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>


template<class T1, class T2>
inline auto max(const T1 a, const T2 b) {
	return (a > b) ? a : b;
}

template<class T1, class T2>
inline auto min(const T1 a, const T2 b) {
	return (a < b) ? a : b;
}

uint64_t prng(uint64_t *state);
double prngf(uint64_t *state);

int64_t system_msec(void);
void system_sleep(int64_t msec);

#define DIE(...) do { \
    fprintf(stderr, __VA_ARGS__); \
    exit(EXIT_FAILURE); \
} while (0)

[[ noreturn ]] void die_errno(const int threadId, const char *fileName, int line);

#define DIE_IF(id, v) ({ \
    if (v) \
        die_errno(id, __FILE__, __LINE__); \
})