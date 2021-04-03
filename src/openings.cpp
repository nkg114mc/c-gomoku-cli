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

#include <sstream>
#include <string>
#include <assert.h>
#include "openings.h"
#include "util.h"
#include "vec.h"

void Openings::openings_init(const char *fileName, bool random, uint64_t srand, int threadId)
{
    //index = vec_init(size_t);
    index = (long*) vec_init(size_t);

    if (*fileName) {
        DIE_IF(threadId, !(file = fopen(fileName, "re")));
    }

    if (file) {
        // Fill o.index[] to record file offsets for each lines
        scope(str_destroy) str_t line = str_init();

        do {
            vec_push(index, ftell(file), long);
        } while (str_getline(&line, file));

        vec_pop(index);  // EOF offset must be removed

        if (random) {
            // Shuffle o.index[], which will be read sequentially from the beginning. This allows
            // consistent treatment of random and !random, and guarantees no repetition N-cycles in
            // the random case, rather than sqrt(N) (birthday paradox) if random seek each time.
            const size_t n = vec_size(index);
            uint64_t seed = srand ? srand : (uint64_t)system_msec();

            for (size_t i = n - 1; i > 0; i--) {
                const size_t j = prng(&seed) % (i + 1);
                long tmp = index[i];
                index[i] = index[j];
                index[j] = tmp;
            }
        }

        printf("Load opening file %s\n", fileName);
    }

    pthread_mutex_init(&mtx, NULL);
}

void Openings::openings_destroy(int threadId)
{
    if (file)
        DIE_IF(threadId, fclose(file) < 0);

    pthread_mutex_destroy(&mtx);
    vec_destroy(index);
}

void Openings::openings_next(str_t *fen, size_t idx, int threadId)
{
    if (!file) {
        str_cpy_c(fen, "");
        return;
    }

    // Read 'fen' from file
    scope(str_destroy) str_t line = str_init();

    pthread_mutex_lock(&mtx);
    DIE_IF(threadId, fseek(file, index[idx % vec_size(index)], SEEK_SET) < 0);
    DIE_IF(threadId, !str_getline(&line, file));
    pthread_mutex_unlock(&mtx);

    str_cpy(fen, line);

    assert(openings_validate_opening_str(*fen));
}


bool Openings::openings_validate_opening_str(str_t &line) {

    std::stringstream ss;
    for (int i = 0; i < line.len; i++) {
        char ch = line.buf[i];
        if ((ch <= '9' && ch >= '0') || ch == '-') {
            ss << ch;
        } else {
            ss << ' '; 
        }
    }

    int cnt = 0;
    int maxOffset = 32 / 2;

    int ofst = -9999;
    while (ss >> ofst) {
        if (ofst != -9999) {
            if (ofst >= -16 && ofst <= 15) {
                cnt++;
            } else {
                printf("Coord offset is too large: %d!\n", ofst);
                return false;
            }
        } else {
            printf("Invalid coord offset: %d!\n", ofst);
            return false;
        }
        ofst = -9999;
    }

    if (cnt % 2 != 0) {
        printf("Coord offsets are not paired (total %d offsets)!\n", cnt);
        return false;
    }
    return true;
}