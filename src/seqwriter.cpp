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
#include <cstring>
#include <assert.h>
#include "seqwriter.h"
#include "vec.h"

// SeqStr

void SeqStr::seq_str_init(size_t idx, str_t str)
{
    this->idx = idx;
    this->str = str_init_from(str);
}

void SeqStr::seq_str_destroy()
{
    str_destroy(&str);
}

// SeqWriter

void SeqWriter::seq_writer_init(const char *fileName, const char *mode)
{
    out = fopen(fileName, mode);
    buf = vec_init(SeqStr);
    pthread_mutex_init(&mtx, NULL);
}

void SeqWriter::seq_writer_destroy()
{
    pthread_mutex_destroy(&this->mtx);
    //vec_destroy_rec(this->buf, seq_str_destroy);
    fclose(this->out);
}

void SeqWriter::seq_writer_push(size_t idx, str_t str)
{
    pthread_mutex_lock(&mtx);

    // Append to buf[n]
    const size_t n = vec_size(buf);
    SeqStr sstr;
    sstr.seq_str_init(idx, str);
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

    if (i) {
        // Write buf[0..i-1] to file, and destroy elements
        for (size_t j = 0; j < i; j++) {
            fputs(buf[j].str.buf, out);
            buf[j].seq_str_destroy();
        }
        fflush(out);

        // Delete buf[0..i-1]
        memmove(&buf[0], &buf[i], (vec_size(buf) - i) * sizeof(SeqStr));
        vec_ptr(buf)->size -= i;

        // Updated next expected index
        idxNext += i;
    }

    pthread_mutex_unlock(&mtx);
}
