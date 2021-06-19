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

#pragma once
#include <pthread.h>
#include <assert.h>
#include <cstdio>
#include "str.h"

class SeqStr {
public:
    size_t idx;
    str_t str;
    void seq_str_init(size_t idx, str_t str);
    void seq_str_destroy();
};

class SeqWriter {
public:
    pthread_mutex_t mtx;
    SeqStr *buf;
    FILE *out;
    size_t idxNext;

    void seq_writer_init(const char *fileName, const char *mode);

    void seq_writer_destroy();
    void seq_writer_push(size_t idx, str_t str);

private:
    void write_to_i(size_t i);
};
