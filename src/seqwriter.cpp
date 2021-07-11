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

#include "seqwriter.h"

#include <algorithm>
#include <cassert>
#include <cstring>

template <typename T> auto insert_sorted(std::vector<T> &vec, T &&item)
{
    auto pos = std::upper_bound(vec.begin(), vec.end(), item);
    return vec.insert(pos, std::move(item));
}

SeqWriter::SeqWriter(const char *fileName, const char *mode) : idxNext(0)
{
    out = fopen(fileName, mode);
}

SeqWriter::~SeqWriter()
{
    // write out all records even if not sequential
    write_to_i(buf.size());

    fclose(out);
}

void SeqWriter::push(size_t idx, std::string_view str)
{
    std::lock_guard lock(mtx);

    // Insert to buf[n] in correct position
    insert_sorted(buf, SeqStr {idx, str});

    // Calculate i such that buf[0..i-1] is the longest sequential chunk
    size_t i = 0;
    for (; i < buf.size(); i++)
        if (buf[i].idx != idxNext + i) {
            assert(buf[i].idx > idxNext + i);
            break;
        }

    if (i)
        write_to_i(i);
}

void SeqWriter::write_to_i(size_t i)
{
    // Write buf[0..i-1] to file
    for (size_t j = 0; j < i; j++) {
        fputs(buf[j].str.c_str(), out);
    }
    fflush(out);

    // Delete buf[0..i-1]
    buf.erase(buf.begin(), buf.begin() + i);

    // Updated next expected index
    idxNext += i;
}