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

#pragma once
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <string>

uint64_t prng(uint64_t &state);
double   prngf(uint64_t &state);

int64_t system_msec(void);
void    system_sleep(int64_t msec);

struct FileLock
{
    FILE *f;

    FileLock(FILE *file);
    ~FileLock();
};

#define DIE_OR_ERR(die, ...)          \
    do {                              \
        FileLock flOutErr(stdout);    \
        fprintf(stderr, __VA_ARGS__); \
        if (die)                      \
            exit(EXIT_FAILURE);       \
    } while (0)

#define DIE(...) DIE_OR_ERR(true, __VA_ARGS__)

[[noreturn]] void die_errno(const int threadId, const char *fileName, int line);

#define DIE_IF(id, v)                          \
    {                                          \
        if (v)                                 \
            die_errno(id, __FILE__, __LINE__); \
    }

#ifdef __linux__
    #define FOPEN_TEXT   "e"
    #define FOPEN_BINARY "e"
#else
    #define FOPEN_TEXT   "N"
    #define FOPEN_BINARY "bN"
#endif

/**
 * printf like formatting for C++ with std::string
 * Original source: https://stackoverflow.com/a/26221725/11722
 */
template <typename... Args>
std::string stringFormatInternal(const char *format, Args &&...args)
{
    size_t size = snprintf(nullptr, 0, format, std::forward<Args>(args)...) + 1;
    if (size <= 0)
        DIE("Error during formatting.");
    char  smallbuf[64];
    char *buf = size <= sizeof(smallbuf) ? smallbuf : new char[size];
    snprintf(buf, size, format, args...);
    std::string ret(buf, buf + size - 1);
    if (size > sizeof(smallbuf))
        delete buf;
    return ret;  // NRVO
}

/**
 * Convert all std::strings to const char* using constexpr if (C++17)
 */
template <typename T> static auto convert_string_to_c_str(T &&t)
{
    if constexpr (std::is_same<std::remove_cv_t<std::remove_reference_t<T>>,
                               std::string>::value)
        return std::forward<T>(t).c_str();
    else
        return std::forward<T>(t);
}

template <typename... Args> std::string format(const char *fmt, Args &&...args)
{
    return stringFormatInternal(fmt,
                                convert_string_to_c_str(std::forward<Args>(args))...);
}

// reads a line from file 'in', into valid string 'out', and return the number of
// characters read (including the '\n' if any). The '\n' is discarded from the output, but
// still counted.
size_t string_getline(std::string &out, FILE *in);

// reads a token into valid string 'token', from s, using delim characters as a
// generalisation for white spaces. returns tail pointer on success, otherwise NULL (no
// more tokens to read).
const char *string_tok(std::string &token, const char *s, const char *delim);

// Similar to string_tok(), but single delimiter, and using escape character. For example:
// s = "alice\ bob charlie", delim=' ', esc='\' => token="alice bob", returns
// tail="charlie"
const char *string_tok_esc(std::string &token, const char *s, char delim, char esc);

// If s starts with prefix, return the tail (from s = prefix + tail), otherwise return
// NULL.
const char *string_prefix(const char *s, const char *prefix);