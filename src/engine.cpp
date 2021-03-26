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

#ifdef __linux__
    #define _GNU_SOURCE
    #include <unistd.h>
    #include <fcntl.h>
    #include <sys/prctl.h>
#else
    #include <unistd.h>
#endif

#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>

#include "engine.h"
#include "util.h"
#include "vec.h"
#include "position.h"

static void engine_spawn(const Worker *w, Engine *e, const char *cwd, const char *run, char **argv,
    bool readStdErr)
{
    assert(argv[0]);

    // Pipe diagram: Parent -> [1]into[0] -> Child -> [1]outof[0] -> Parent
    // 'into' and 'outof' are pipes, each with 2 ends: read=0, write=1
    int outof[2] = {0}, into[2] = {0};

#ifdef __linux__
    DIE_IF(w->id, pipe2(outof, O_CLOEXEC) < 0);
    DIE_IF(w->id, pipe2(into, O_CLOEXEC) < 0);
#else
    DIE_IF(w->id, pipe(outof) < 0);
    DIE_IF(w->id, pipe(into) < 0);
#endif

    DIE_IF(w->id, (e->pid = fork()) < 0);

    if (e->pid == 0) {
#ifdef __linux__
        prctl(PR_SET_PDEATHSIG, SIGHUP);  // delegate zombie purge to the kernel
#endif
        // Plug stdin and stdout
        DIE_IF(w->id, dup2(into[0], STDIN_FILENO) < 0);
        DIE_IF(w->id, dup2(outof[1], STDOUT_FILENO) < 0);

        // For stderr we have 2 choices:
        // - readStdErr=true: dump it into stdout, like doing '2>&1' in bash. This is useful, if we
        // want to see error messages from engines in their respective log file (notably assert()
        // writes to stderr). Of course, such error messages should not be UCI commands, otherwise we
        // will be fooled into parsing them as such.
        // - readStdErr=false: do nothing, which means stderr is inherited from the parent process.
        // Typcically, this means all engines write their error messages to the terminal (unless
        // redirected otherwise).
        if (readStdErr)
            DIE_IF(w->id, dup2(outof[1], STDERR_FILENO) < 0);

#ifndef __linux__
        // Ugly (and slow) workaround for non-Linux POSIX systems that lack the ability to
        // atomically set O_CLOEXEC when creating pipes.
        for (int fd = 3; fd < sysconf(FOPEN_MAX); close(fd++));
#endif

        // Set cwd as current directory, and execute run with argv[]
        DIE_IF(w->id, chdir(cwd) < 0);
        DIE_IF(w->id, execvp(run, argv) < 0);
    } else {
        assert(e->pid > 0);

        // in the parent process
        DIE_IF(w->id, close(into[0]) < 0);
        DIE_IF(w->id, close(outof[1]) < 0);

        DIE_IF(w->id, !(e->in = fdopen(outof[0], "r")));
        DIE_IF(w->id, !(e->out = fdopen(into[1], "w")));
    }
}

static void engine_parse_cmd(const char *cmd, str_t *cwd, str_t *run, str_t **args)
{
    // Isolate the first token being the command to run.
    scope(str_destroy) str_t token = str_init();
    const char *tail = cmd;
    tail = str_tok_esc(tail, &token, ' ', '\\');

    // Split token into (cwd, run). Possible cases:
    // (a) unqualified path, like "demolito" (which evecvp() will search in PATH)
    // (b) qualified path (absolute starting with "/", or relative starting with "./" or "../")
    // For (b), we want to separate into executable and directory, so instead of running
    // "../Engines/demolito" from the cwd, we execute run="./demolito" from cwd="../Engines"
    str_cpy_c(cwd, "./");
    str_cpy(run, token);
    const char *lastSlash = strrchr(token.buf, '/');

    if (lastSlash) {
        str_ncpy(cwd, token, (size_t)(lastSlash - token.buf));
        str_cpy_fmt(run, "./%s", lastSlash + 1);
    }

    // Collect the arguments into a vec of str_t, args[]
    vec_push(*args, str_init_from(*run), str_t);  // argv[0] is the executed command

    while ((tail = str_tok_esc(tail, &token, ' ', '\\')))
        vec_push(*args, str_init_from(token), str_t);
}

void Engine::engine_init(Worker *w, const char *cmd, const char *name, const str_t *options)
{
    if (!*cmd)
        DIE("[%d] missing command to start engine.\n", w->id);

    this->name = str_init_from_c(*name ? name : cmd); // default value

    // Parse cmd into (cwd, run, args): we want to execute run from cwd with args.
    scope(str_destroy) str_t cwd = str_init(), run = str_init();
    str_t *args = vec_init(str_t);
    engine_parse_cmd(cmd, &cwd, &run, &args);

    // execvp() needs NULL terminated char **, not vec of str_t. Prepare a char **, whose elements
    // point to the C-string buffers of the elements of args, with the required NULL at the end.
    char **argv = (char**)calloc(vec_size(args) + 1, sizeof(char *));

    for (size_t i = 0; i < vec_size(args); i++) {
        argv[i] = args[i].buf;
    }

    // Spawn child process and plug pipes
    engine_spawn(w, this, cwd.buf, run.buf, argv, w->log != NULL);

    vec_destroy_rec(args, str_destroy);
    free(argv);

/*
    // Start the uci..uciok dialogue
    // No such thing in gomocup
*/
    // parse engine ABOUT infomation
    engine_about(w);

    for (size_t i = 0; i < vec_size(options); i++) {
        //scope(str_destroy) str_t oname = str_init(), ovalue = str_init();
        //str_tok(str_tok(options[i].buf, &oname, "="), &ovalue, "=");
        //str_cpy_fmt(&line, "setoption name %S value %S", oname, ovalue);
        //engine_writeln(w, line.buf);
    }
}

void Engine::engine_destroy(Worker *w)
{
    // Engine was not instanciated with engine_init()
    if (!pid) {
        return;
    }

    // Order the engine to quit, and grant 1s deadline for obeying
    w->deadline_set(name.buf, system_msec() + 1000);
    engine_writeln(w, "END");
    waitpid(pid, NULL, 0);
    w->deadline_clear();

    str_destroy(&name);
    DIE_IF(w->id, fclose(in) < 0);
    DIE_IF(w->id, fclose(out) < 0);
}

void Engine::engine_readln(const Worker *w, str_t *line)
{
    if (!str_getline(line, in))
        DIE("[%d] could not read from %s\n", w->id, name.buf);

    if (w->log)
        DIE_IF(w->id, fprintf(w->log, "%s -> %s\n", name.buf, line->buf) < 0);
}

void Engine::engine_writeln(const Worker *w, char *buf)
{
    DIE_IF(w->id, fputs(buf, out) < 0);
    DIE_IF(w->id, fputc('\n', out) < 0);
    DIE_IF(w->id, fflush(out) < 0);

    if (w->log) {
        DIE_IF(w->id, fprintf(w->log, "%s <- %s\n", name.buf, buf) < 0);
        DIE_IF(w->id, fflush(w->log) < 0);
    }
}


void Engine::engine_wait_for_ok(Worker *w)
{
    w->deadline_set(name.buf, system_msec() + 4000);
    scope(str_destroy) str_t line = str_init();

    do {
        engine_readln(w, &line);
    } while (strcmp(line.buf, "OK"));
    w->deadline_clear();
}

bool Engine::engine_bestmove(Worker *w, int64_t *timeLeft, int64_t maxTurnTime, str_t *best, str_t *pv,
    Info *info)
{
    int result = false;
    scope(str_destroy) str_t line = str_init(), token = str_init();
    str_clear(pv);

    const int64_t start = system_msec();
    const int64_t matchTimeLimit = start + *timeLeft;
    int64_t turnTimeLimit = matchTimeLimit;
    int64_t turnTimeLeft = *timeLeft;
    if (maxTurnTime > 0) {
        // engine should not think longer than the turn_time_limit
        turnTimeLimit = start + min(*timeLeft, maxTurnTime);
        turnTimeLeft = min(*timeLeft, maxTurnTime);
    }
    
    w->deadline_set(name.buf, turnTimeLimit + 1000);

    //while (*timeLeft >= 0 && !result) {
    while ((turnTimeLeft + 1000) >= 0 && !result) {
        engine_readln(w, &line);

        const int64_t now = system_msec();
        info->time = now - start;
        *timeLeft = matchTimeLimit - now;
        turnTimeLeft = turnTimeLimit - now;

        const char *tail = NULL;

        if ((tail = str_prefix(line.buf, "MESSAGE"))) {
            engine_process_message_ifneeded(line.buf);
            /*
            while ((tail = str_tok(tail, &token, " "))) {
                if (!strcmp(token.buf, "depth")) {
                    if ((tail = str_tok(tail, &token, " ")))
                        info->depth = atoi(token.buf);
                } else if (!strcmp(token.buf, "score")) {
                    if ((tail = str_tok(tail, &token, " "))) {
                        if (!strcmp(token.buf, "cp") && (tail = str_tok(tail, &token, " ")))
                            info->score = atoi(token.buf);
                        else if (!strcmp(token.buf, "mate") && (tail = str_tok(tail, &token, " "))) {
                            const int movesToMate = atoi(token.buf);
                            info->score = movesToMate < 0 ? INT_MIN - movesToMate : INT_MAX - movesToMate;
                        } else
                            DIE("illegal syntax after 'score' in '%s'\n", line.buf);
                    }
                } else if (!strcmp(token.buf, "pv")) {
                    str_cpy_c(pv, tail + strspn(tail, " "));
                }
            }*/
        } else if (Position::is_valid_move_gomostr(line.buf)) {
            str_cpy(best, line);
            result = true;
        }
    }

    // Time out. Send "stop" and give the opportunity to the engine to respond with bestmove (still
    // under deadline protection).
    if (!result) {
        engine_writeln(w, "YXSTOP");

        do {
            engine_readln(w, &line);
            engine_process_message_ifneeded(line.buf);
        } while (!Position::is_valid_move_gomostr(line.buf));
    }

    w->deadline_clear();
    return result;
}

// process 
void Engine::engine_about(Worker *w) {
    w->deadline_set(name.buf, system_msec() + 2000);
    engine_writeln(w, "ABOUT");
    scope(str_destroy) str_t line = str_init();

    engine_readln(w, &line);

    // parse about infos
    printf("Get Engine About:[%s]\n", line.buf);

    w->deadline_clear();
}

// process MESSAGE, UNKNOWN, ERROR, DEBUG messages
void Engine::engine_process_message_ifneeded(const char *line)
{
    // Isolate the first token being the command to run.
    //scope(str_destroy) str_t token = str_init();
    const char *tail = NULL;

    tail = str_prefix(line, "MESSAGE");
    if (tail != NULL) { // a MESSAGE
        printf("Engine[%s] output message:%s\n", this->name.buf, tail);
        return;
    }

    tail = str_prefix(line, "UNKNOWN");
    if (tail != NULL) { // a UNKNOWN
        printf("Engine[%s] output unknown:%s\n", this->name.buf, tail);
        return;
    }

    tail = str_prefix(line, "DEBUG");
    if (tail != NULL) { // a DEBUG
        printf("Engine[%s] output debug:%s\n", this->name.buf, tail);
        return;
    }

    tail = str_prefix(line, "ERROR");
    if (tail != NULL) { // an ERROR
        printf("Engine[%s] output error:%s\n", this->name.buf, tail);
        return;
    }
}

/*
void Engine::engine_sync(Worker *w)
{
    w->deadline_set(name.buf, system_msec() + 2000);
    engine_writeln(w, "isready");
    scope(str_destroy) str_t line = str_init();

    do {
        engine_readln(w, &line);
    } while (strcmp(line.buf, "readyok"));

    w->deadline_clear();
}
*/
/*
bool Engine::engine_bestmove(Worker *w, int64_t *timeLeft, str_t *best, str_t *pv,
    Info *info)
{
    int result = false;
    scope(str_destroy) str_t line = str_init(), token = str_init();
    str_clear(pv);

    const int64_t start = system_msec(), timeLimit = start + *timeLeft;
    w->deadline_set(name.buf, timeLimit + 1000);

    while (*timeLeft >= 0 && !result) {
        engine_readln(w, &line);

        const int64_t now = system_msec();
        info->time = now - start;
        *timeLeft = timeLimit - now;

        const char *tail = NULL;

        if ((tail = str_prefix(line.buf, "info "))) {
            while ((tail = str_tok(tail, &token, " "))) {
                if (!strcmp(token.buf, "depth")) {
                    if ((tail = str_tok(tail, &token, " ")))
                        info->depth = atoi(token.buf);
                } else if (!strcmp(token.buf, "score")) {
                    if ((tail = str_tok(tail, &token, " "))) {
                        if (!strcmp(token.buf, "cp") && (tail = str_tok(tail, &token, " ")))
                            info->score = atoi(token.buf);
                        else if (!strcmp(token.buf, "mate") && (tail = str_tok(tail, &token, " "))) {
                            const int movesToMate = atoi(token.buf);
                            info->score = movesToMate < 0 ? INT_MIN - movesToMate : INT_MAX - movesToMate;
                        } else
                            DIE("illegal syntax after 'score' in '%s'\n", line.buf);
                    }
                } else if (!strcmp(token.buf, "pv"))
                    str_cpy_c(pv, tail + strspn(tail, " "));
            }
        } else if ((tail = str_prefix(line.buf, "bestmove "))) {
            str_tok(tail, &token, " ");
            str_cpy(best, token);
            result = true;
        }
    }

    // Time out. Send "stop" and give the opportunity to the engine to respond with bestmove (still
    // under deadline protection).
    if (!result) {
        engine_writeln(w, "stop");

        do {
            engine_readln(w, &line);
        } while (!str_prefix(line.buf, "bestmove "));
    }

    w->deadline_clear();
    return result;
}
*/
