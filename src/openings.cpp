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

#include "openings.h"

#include "util.h"

#include <cassert>
#include <string>

Openings::Openings(const char *fileName, bool random, uint64_t srand) : file(nullptr)
{
    if (*fileName) {
        DIE_IF(0, !(file = fopen(fileName, "r" FOPEN_TEXT)));
    }

    if (file) {
        // Fill o.index[] to record file offsets for each lines
        std::string line;

        do {
            index.push_back(ftell(file));
        } while (string_getline(line, file));

        index.pop_back();  // EOF offset must be removed

        if (random) {
            // Shuffle o.index[], which will be read sequentially from the beginning. This
            // allows consistent treatment of random and !random, and guarantees no
            // repetition N-cycles in the random case, rather than sqrt(N) (birthday
            // paradox) if random seek each time.
            uint64_t seed = srand ? srand : (uint64_t)system_msec();

            for (size_t i = index.size() - 1; i > 0; i--) {
                const size_t j   = prng(seed) % (i + 1);
                long         tmp = index[i];
                index[i]         = index[j];
                index[j]         = tmp;
            }
        }

        printf("Load opening file %s\n", fileName);
    }
}

Openings::~Openings()
{
    if (file)
        DIE_IF(0, fclose(file) < 0);
}

// Returns current round
size_t Openings::next(std::string &opening_str, size_t idx, int threadId)
{
    if (!file) {
        opening_str.clear();
        return 0;
    }

    // Read opening string from file
    std::string line;

    {
        std::lock_guard lock(mtx);
        DIE_IF(threadId, fseek(file, index[idx % index.size()], SEEK_SET) < 0);
        DIE_IF(threadId, !string_getline(line, file));
    }

    opening_str = std::move(line);
    return idx / index.size();
}
