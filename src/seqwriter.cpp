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

#include <cstring>
#include <assert.h>
#include "seqwriter.h"
#include "vec.h"

// SeqStr

void SeqStr::init(size_t idx, str_t str)
{
    this->idx = idx;
    this->str = str_init_from(str);
}

void SeqStr::destroy()
{
    str_destroy(&str);
}

// SeqWriter

SeqWriter::SeqWriter(const char *fileName, const char *mode) : idxNext(0)
{
    out = fopen(fileName, mode);
    buf = vec_init(SeqStr);
    pthread_mutex_init(&mtx, NULL);
}

SeqWriter::~SeqWriter()
{
    pthread_mutex_destroy(&this->mtx);
    //vec_destroy_rec(this->buf, destroy);

    // write out all records even if not sequential
    write_to_i(vec_size(buf));

    fclose(this->out);
}

void SeqWriter::push(size_t idx, str_t str)
{
    pthread_mutex_lock(&mtx);

    // Append to buf[n]
    const size_t n = vec_size(buf);
    SeqStr sstr;
    sstr.init(idx, str);
    vec_push(buf, sstr, SeqStr);

    // insert in correct position
    for (size_t i = 0; i < n; i++)
        if (buf[i].idx > idx) {
            SeqStr tmp = buf[n];
            memmove(&buf[i + 1], &buf[i], (n - i) * sizeof(SeqStr));
            buf[i] = tmp;
            break;
        }

    // Calculate i such that buf[0..i-1] is the longest sequential chunk
    size_t i = 0;
    for (; i < vec_size(buf); i++)
        if (buf[i].idx != idxNext + i) {
            assert(buf[i].idx > idxNext + i);
            break;
        }

    if (i)
        write_to_i(i);

    pthread_mutex_unlock(&mtx);
}

void SeqWriter::write_to_i(size_t i) {
    // Write buf[0..i-1] to file, and destroy elements
    for (size_t j = 0; j < i; j++) {
        fputs(buf[j].str.buf, out);
        buf[j].destroy();
    }
    fflush(out);

    // Delete buf[0..i-1]
    memmove(&buf[0], &buf[i], (vec_size(buf) - i) * sizeof(SeqStr));
    vec_ptr(buf)->size -= i;

    // Updated next expected index
    idxNext += i;
}