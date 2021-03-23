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
#include <assert.h>
#include "openings.h"
#include "util.h"
#include "vec.h"

void Openings::openings_init(const char *fileName, bool random, uint64_t srand, int threadId)
{
    //index = vec_init(size_t);
    index = (long*) vec_init(size_t);

    if (*fileName)
        DIE_IF(threadId, !(file = fopen(fileName, "re")));

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
                //swap(index[i], index[j]);
            }
        }
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
        str_cpy_c(fen, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        return;
    }

    // Read 'fen' from file
    scope(str_destroy) str_t line = str_init();

    pthread_mutex_lock(&mtx);
    DIE_IF(threadId, fseek(file, index[idx % vec_size(index)], SEEK_SET) < 0);
    DIE_IF(threadId, !str_getline(&line, file));
    pthread_mutex_unlock(&mtx);

    str_tok(line.buf, fen, ";");
}
