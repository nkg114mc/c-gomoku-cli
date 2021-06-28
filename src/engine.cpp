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

#if defined(__MINGW32__)
    #include <io.h>
    #include <fcntl.h>
    #include <windows.h> 
#elif defined(__linux__)
    #define _GNU_SOURCE
    #include <unistd.h>
    #include <fcntl.h>
    #include <sys/prctl.h>
    #include <sys/wait.h>
#else
    #include <unistd.h>
    #include <sys/wait.h>
#endif

#include <iostream>
#include <sstream>
#include <filesystem>
#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "engine.h"
#include "util.h"
#include "vec.h"
#include "position.h"

static void engine_spawn(const Worker *w, Engine *e, 
    const char *cwd, const char *run, char **argv, bool readStdErr)
{
    assert(argv[0]);

#if defined(__MINGW32__)
    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    // Pipe handler: read=0, write=1
    HANDLE p_stdin[2], p_stdout[2];

    // Setup job handler and job info
    HANDLE hJob = CreateJobObject(NULL, NULL);
    DIE_IF(w->id, !hJob);

    JOBOBJECT_BASIC_LIMIT_INFORMATION jobBasicInfo;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobExtendedInfo;
    ZeroMemory(&jobBasicInfo, sizeof(JOBOBJECT_BASIC_LIMIT_INFORMATION));
    ZeroMemory(&jobExtendedInfo, sizeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION));

    jobBasicInfo.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    jobExtendedInfo.BasicLimitInformation = jobBasicInfo;
    DIE_IF(w->id, !SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, 
        &jobExtendedInfo, sizeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION)));

    // Create a pipe for child process's STDOUT
    DIE_IF(w->id, !CreatePipe(&p_stdout[0], &p_stdout[1], &saAttr, 0));
    DIE_IF(w->id, !SetHandleInformation(p_stdout[0], HANDLE_FLAG_INHERIT, 0));

    // Create a pipe for child process's STDIN
    DIE_IF(w->id, !CreatePipe(&p_stdin[0], &p_stdin[1], &saAttr, 0));
    DIE_IF(w->id, !SetHandleInformation(p_stdin[1], HANDLE_FLAG_INHERIT, 0));

    // Create the child process
    PROCESS_INFORMATION piProcInfo; 
    STARTUPINFO siStartInfo;
    ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));
    ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
    siStartInfo.cb = sizeof(STARTUPINFO); 
    siStartInfo.hStdOutput = p_stdout[1];
    siStartInfo.hStdInput = p_stdin[0];
    if (readStdErr)
        siStartInfo.hStdError = p_stdout[1];
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

    // Construct full commandline using run and argv
    char fullrun[MAX_PATH];
    char fullcmd[32768];
    strcpy_s(fullrun, cwd);
    strcat_s(fullrun, run + 1); // we need an path relative to the cli

    // Use an absolute path for engine argv[0]
    strcpy_s(fullcmd, std::filesystem::absolute(fullrun).string().c_str()); 
    for (size_t i = 1; argv[i]; i++) {  // argv[0] == run
        strcat_s(fullcmd, " ");
        strcat_s(fullcmd, argv[i]);
    }

    const int flag = CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS;
    // FIXME: fullrun, fullcmd, cwd conversion to LPCWSTR
    if (!CreateProcess(
        fullrun,        // application name
        fullcmd,        // command line (non-const)
        NULL,           // process security attributes
        NULL,           // primary thread security attributes
        TRUE,           // handles are inherited
        flag,           // creation flags
        NULL,           // use parent's environment
        cwd,            // child process's current directory
        &siStartInfo,   // STARTUPINFO pointer
        &piProcInfo     // receives PROCESS_INFORMATION
    ))
        DIE("Fail to execute engine %s\n", fullrun);

    // Keep the handle to the child process
    e->pid = piProcInfo.dwProcessId;
    e->hProcess = piProcInfo.hProcess;
    // Close the handle to the child's primary thread
    DIE_IF(w->id, !CloseHandle(piProcInfo.hThread));

    // Close handles to the stdin and stdout pipes no longer needed
    DIE_IF(w->id, !CloseHandle(p_stdin[0]));
    DIE_IF(w->id, !CloseHandle(p_stdout[1]));

    // Reopen stdin and stdout pipes using C style FILE
    int stdin_fd = _open_osfhandle((intptr_t)p_stdin[1], _O_RDONLY | _O_TEXT);
    int stdout_fd = _open_osfhandle((intptr_t)p_stdout[0], _O_RDONLY | _O_TEXT);
    DIE_IF(w->id, stdin_fd == -1);
    DIE_IF(w->id, stdout_fd == -1);
    DIE_IF(w->id, !(e->in = _fdopen(stdout_fd, "r")));
    DIE_IF(w->id, !(e->out = _fdopen(stdin_fd, "w")));

    // Bind child process and parent process to one job, so child process is
    // killed when parent process exits
    DIE_IF(w->id, !AssignProcessToJobObject(hJob, GetCurrentProcess()));
    DIE_IF(w->id, !AssignProcessToJobObject(hJob, e->hProcess));
#else
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
#endif
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

void Engine::engine_init(Worker *w, const char *cmd, const char *engname, bool debug, str_t *outmsg)
{
    if (!*cmd)
        DIE("[%d] missing command to start engine.\n", w->id);

    this->name = str_init_from_c(engname);
    this->isDebug = debug;
    this->messages = outmsg;

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

    // parse engine ABOUT infomation
    engine_about(w, cmd);
}

void Engine::engine_destroy(Worker *w)
{
    // Engine was not instanciated with engine_init()
    if (!pid) {
        return;
    }

    // Order the engine to quit, and grant 1s deadline for obeying
    w->deadline_set(name.buf, system_msec() + tolerance);
    engine_writeln(w, "END");

#ifdef __MINGW32__
    WaitForSingleObject((HANDLE)hProcess, INFINITE);
    CloseHandle((HANDLE)hProcess);
#else
    waitpid(pid, NULL, 0);
#endif

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

void Engine::engine_writeln(const Worker *w, const char *buf)
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
    w->deadline_set(name.buf, system_msec() + tolerance);
    scope(str_destroy) str_t line = str_init();

    do {
        engine_readln(w, &line);

        const char *tail = str_prefix(line.buf, "ERROR");
        if (tail != NULL) { // an ERROR
            DIE("Engine[%s] output error:%s\n", this->name.buf, tail);
        }
    } while (strcmp(line.buf, "OK"));
    w->deadline_clear();
}

bool Engine::engine_bestmove(Worker *w, int64_t *timeLeft, int64_t maxTurnTime, str_t *best,
    str_t *pv, Info *info, int moveply)
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
    
    w->deadline_set(name.buf, turnTimeLimit + tolerance);

    while ((turnTimeLeft + tolerance) >= 0 && !result) {
        engine_readln(w, &line);

        const int64_t now = system_msec();
        info->time = now - start;
        *timeLeft = matchTimeLimit - now;
        turnTimeLeft = turnTimeLimit - now;

        const char *tail = NULL;

        if (this->isDebug) {
            engine_process_message_ifneeded(line.buf);
        }

        if ((tail = str_prefix(line.buf, "MESSAGE"))) {
            // record engine messages
            if (messages)
                str_cat_fmt(messages, "%i) %S: %s\n", moveply, name, tail + 1);

            // parse and store thing infomation to info
            engine_parse_thinking_messages(line.buf, info);
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

static void parse_and_display_engine_about(str_t &line, str_t* engine_name) {
    int flag = 0;
    std::vector<std::string> tokens;
    std::stringstream ss;
    for (size_t i = 0; i < line.len; i++) {
        char ch = line.buf[i];
        if (ch == '\"') {
            flag = (flag + 1) % 2;
        } else if (ch == ',' || ch == ' ' || ch == '=') {
            if (flag > 0) {
                ss << ch;
            } else {
                if (ss.str().length() > 0) {
                    tokens.push_back(ss.str());
                }
                ss.clear();
                ss.str("");
            }
        } else {
            ss << ch;
        }
    }
    if (ss.str().length() > 0) {
        tokens.push_back(ss.str());
    }

    std::string name = "?";
    std::string author = "?";
    std::string version = "?";
    std::string country = "?";
    for (size_t i = 0; i < tokens.size(); i++) {
        if (tokens[i] == "name") {
            if ((i + 1) < tokens.size()) {
                name = tokens[i + 1];
                // Set engine name to about name
                if (!*engine_name->buf)
                    str_cpy_c(engine_name, name.c_str());
            }
        } else if (tokens[i] == "version") {
            if ((i + 1) < tokens.size()) {
                version = tokens[i + 1];
            }
            
        } else if (tokens[i] == "author") {
            if ((i + 1) < tokens.size()) {
                author = tokens[i + 1];
            }
        } else if (tokens[i] == "country") {
            if ((i + 1) < tokens.size()) {
                country = tokens[i + 1];
            }
        }
    }
    std::cout << "Load engine: " << name << " (version " << version << ") by " << author << ", " << country << std::endl;
}

// process engine ABOUT command
void Engine::engine_about(Worker *w, const char* fallbackName) {
    w->deadline_set(*name.buf ? name.buf : fallbackName, system_msec() + tolerance);
    engine_writeln(w, "ABOUT");
    scope(str_destroy) str_t line = str_init();

    engine_readln(w, &line);

    w->deadline_clear();
    
    // parse about infos
    parse_and_display_engine_about(line, &name);

    // If we can not get a name from engname or about, use fallback name instead
    if (!*name.buf)
        str_cpy_c(&name, fallbackName);
}

// process MESSAGE, UNKNOWN, ERROR, DEBUG messages
void Engine::engine_process_message_ifneeded(const char *line)
{
    // Isolate the first token being the command to run.
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

void Engine::engine_parse_thinking_messages([[maybe_unused]] const char *line, Info *info)
{
    // Set default value
    info->score = 0;
    info->depth = 0;
}
