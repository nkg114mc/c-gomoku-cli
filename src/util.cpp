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

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <ctime>
#ifdef __MINGW32__
    #include <Windows.h>
#endif
#include "util.h"

// SplitMix64 PRNG, based on http://xoroshiro.di.unimi.it/splitmix64.c
uint64_t prng(uint64_t &state)
{
    uint64_t rnd = (state += 0x9E3779B97F4A7C15);
    rnd          = (rnd ^ (rnd >> 30)) * 0xBF58476D1CE4E5B9;
    rnd          = (rnd ^ (rnd >> 27)) * 0x94D049BB133111EB;
    rnd ^= rnd >> 31;
    return rnd;
}

double prngf(uint64_t &state)
{
    return (prng(state) >> 11) * 0x1.0p-53;
}

int64_t system_msec()
{
    struct timespec t = {};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000LL + t.tv_nsec / 1000000;
}

void system_sleep(int64_t msec)
{
    const struct timespec t = {.tv_sec  = msec / 1000,
                               .tv_nsec = (msec % 1000) * 1000000LL};
    nanosleep(&t, NULL);
}

[[noreturn]] void die_errno(const int threadId, const char *fileName, int line)
{
#ifdef __MINGW32__
    if (errno <= 0) {
        int  error = GetLastError();
        char buf[256];
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL,
                       error,
                       MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                       buf,
                       (sizeof(buf) / sizeof(wchar_t)),
                       NULL);
        fprintf(stderr,
                "[%d] error in %s: (%d). %s (code %d)\n",
                threadId,
                fileName,
                line,
                buf,
                error);
        exit(EXIT_FAILURE);
    }
#endif

    fprintf(stderr,
            "[%d] error in %s: (%d). %s\n",
            threadId,
            fileName,
            line,
            strerror(errno));
    exit(EXIT_FAILURE);
}

size_t string_getline(std::string &out, FILE *in)
{
    assert(in);
    out.clear();
    int c;

#ifdef __MINGW32__
    _lock_file(in);
#else
    flockfile(in);
#endif

    while (true) {
#ifdef __MINGW32__
        c = _getc_nolock(in);
#else
        c = getc_unlocked(in);
#endif

        if (c != '\n' && c != EOF)
            out.push_back(c);
        else
            break;
    }

#ifdef __MINGW32__
    _unlock_file(in);
#else
    funlockfile(in);
#endif

    return out.size() + (c == '\n');
}

// Read next character using escape character. Result in *out. Retuns tail pointer, and
// sets escaped=true if escape character parsed.
static const char *string_getc_esc(const char *s, char *out, bool *escaped, char esc)
{
    if (*s != esc) {
        *escaped = false;
        *out     = *s;
        return s + 1;
    }
    else {
        assert(*s && *s == esc);
        *escaped = true;
        *out     = *(s + 1);
        return s + 2;
    }
}

const char *string_tok(std::string &token, const char *s, const char *delim)
{
    assert(delim && *delim);

    // empty tail: no-op
    if (!s)
        return nullptr;

    // eat delimiters before token
    s += strspn(s, delim);

    // eat non delimiters into token
    const size_t n = strcspn(s, delim);
    token          = std::string(s, n);
    s += n;

    // return string tail or NULL if token empty
    return !token.empty() ? s : nullptr;
}

const char *string_tok_esc(std::string &token, const char *s, char delim, char esc)
{
    assert(delim && esc);

    // empty tail: no-op
    if (!s)
        return nullptr;

    // clear token
    token.clear();

    const char *tail = s;
    char        c;
    bool        escaped, accumulate = false;

    while (*tail && (tail = string_getc_esc(tail, &c, &escaped, esc))) {
        if (!accumulate && (c != delim || escaped))
            accumulate = true;

        if (accumulate) {
            if (c != delim || escaped)
                token.push_back(c);
            else
                break;
        }
    }

    // return string tail or NULL if token empty
    return !token.empty() ? tail : nullptr;
}

const char *string_prefix(const char *s, const char *prefix)
{
    size_t len = strlen(prefix);
    return strncmp(s, prefix, len) ? NULL : s + len;
}