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

#if defined(__MINGW32__)
    #include <fcntl.h>
    #include <io.h>
    #include <windows.h>
#elif defined(__linux__)
    #define _GNU_SOURCE
    #include <fcntl.h>
    #include <sys/prctl.h>
    #include <sys/wait.h>
    #include <unistd.h>
#else
    #include <sys/wait.h>
    #include <unistd.h>
#endif

#include "engine.h"
#include "position.h"
#include "util.h"
#include "workers.h"

#include <cassert>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <signal.h>
#include <sstream>
#include <vector>

#ifdef __MINGW32__
// Argument quoting is non trivial on Windows: we need to take care of character
// escaping, and better only add quotes when it is actually needed. Adopted from
// <https://docs.microsoft.com/en-gb/archive/blogs/twistylittlepassagesallalike/everyone-quotes-command-line-arguments-the-wrong-way>
static std::string argvQuote(std::string_view arg)
{
    std::string cmdline;

    // Don't quote unless we actually need to do so -- hopefully
    // avoid problems if programs won't parse quotes properly
    if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == arg.npos)
        cmdline = arg;
    else {
        cmdline.push_back('"');

        for (auto it = arg.begin();; ++it) {
            unsigned numBlackslashes = 0;

            while (it != arg.end() && *it == '\\') {
                ++it;
                ++numBlackslashes;
            }

            // Escape all backslashes, but let the terminating double quotation
            // mark we add below be interpreted as a metacharacter.
            if (it == arg.end()) {
                cmdline.append(numBlackslashes * 2, '\\');
                break;
            }
            // Escape all backslashes and the following double quotation mark.
            else if (*it == '"')
                cmdline.append(numBlackslashes * 2 + 1, '\\').push_back(*it);
            // Backslashes aren't special here.
            else
                cmdline.append(numBlackslashes, '\\').push_back(*it);
        }

        cmdline.push_back('"');
    }

    return cmdline;  // NRVO
}
#endif

Engine::Engine(Worker *worker, bool debug, std::string *outmsg)
    : w(worker)
    , isDebug(debug)
    , pid(0)
    , messages(outmsg)
{}

Engine::~Engine()
{
    terminate();
}

void Engine::spawn(const char *cwd, const char *run, const char **argv, bool readStdErr)
{
    assert(argv[0]);

#ifdef __MINGW32__
    // Setup the global job handle and job info, then bind parent process to it.
    // (This will only be called once)
    [[maybe_unused]] static HANDLE handleJob = [this]() {
        HANDLE hJob = CreateJobObject(NULL, NULL);
        DIE_IF(w->id, !hJob);

        JOBOBJECT_BASIC_LIMIT_INFORMATION    jobBasicInfo;
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobExtendedInfo;
        ZeroMemory(&jobBasicInfo, sizeof(JOBOBJECT_BASIC_LIMIT_INFORMATION));
        ZeroMemory(&jobExtendedInfo, sizeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION));

        jobBasicInfo.LimitFlags               = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        jobExtendedInfo.BasicLimitInformation = jobBasicInfo;
        DIE_IF(w->id,
               !SetInformationJobObject(hJob,
                                        JobObjectExtendedLimitInformation,
                                        &jobExtendedInfo,
                                        sizeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION)));

        DIE_IF(w->id, !AssignProcessToJobObject(hJob, GetCurrentProcess()));
        return hJob;
    }();

    // Setup structs needed to create process
    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength              = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle       = TRUE;
    saAttr.lpSecurityDescriptor = NULL;
    PROCESS_INFORMATION piProcInfo;
    STARTUPINFOA        siStartInfo;
    ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));
    ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
    siStartInfo.cb = sizeof(STARTUPINFO);
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

    // Construct full commandline using run and argv
    std::string fullrun, fullcmd;
    fullrun = format("%s%s", cwd, run + 1);  // we need an path relative to the cli

    // Note: all arguments in argv needs to quoted
    // Use an absolute path for engine argv[0]
    fullcmd = argvQuote(std::filesystem::absolute(fullrun).string());
    for (size_t i = 1; argv[i]; i++)  // argv[0] == run
        fullcmd += format(" %s", argvQuote(argv[i]));

    // Pipe handler: read=0, write=1
    HANDLE p_stdin[2], p_stdout[2];

    // Process and pipe creation should be sequential to avoid handle inheritance bug
    static std::mutex mtx;
    {
        std::lock_guard<std::mutex> lock(mtx);

        // Create a pipe for child process's STDOUT
        DIE_IF(w->id, !CreatePipe(&p_stdout[0], &p_stdout[1], &saAttr, 0));
        DIE_IF(w->id, !SetHandleInformation(p_stdout[0], HANDLE_FLAG_INHERIT, 0));

        // Create a pipe for child process's STDIN
        DIE_IF(w->id, !CreatePipe(&p_stdin[0], &p_stdin[1], &saAttr, 0));
        DIE_IF(w->id, !SetHandleInformation(p_stdin[1], HANDLE_FLAG_INHERIT, 0));

        // Create the child process
        siStartInfo.hStdOutput = p_stdout[1];
        siStartInfo.hStdInput  = p_stdin[0];
        if (readStdErr) {
            HANDLE p_stderr;
            DIE_IF(w->id,
                   !DuplicateHandle(GetCurrentProcess(),
                                    p_stdout[1],
                                    GetCurrentProcess(),
                                    &p_stderr,
                                    0,
                                    TRUE,
                                    DUPLICATE_SAME_ACCESS));
            siStartInfo.hStdError = p_stderr;
        }

        const int flag = CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS;
        if (!CreateProcessA(fullrun.c_str(),  // application name
                            fullcmd.data(),   // command line (non-const)
                            nullptr,          // process security attributes
                            nullptr,          // primary thread security attributes
                            true,             // handles are inherited
                            flag,             // creation flags
                            nullptr,          // use parent's environment
                            cwd,              // child process's current directory
                            &siStartInfo,     // STARTUPINFO pointer
                            &piProcInfo       // receives PROCESS_INFORMATION
                            )) {
            // Give a more elaborated error report on wrong engine path
            DIE_OR_ERR(false, "[%d] failed to load engine \"%s\"\n", w->id, run);
            DIE_IF(w->id, true);  // extra error message from OS
        }

        // Close handles to the stdin and stdout pipes no longer needed
        DIE_IF(w->id, !CloseHandle(p_stdin[0]));
        DIE_IF(w->id, !CloseHandle(p_stdout[1]));
    }

    // Keep the handle to the child process
    this->pid      = piProcInfo.dwProcessId;
    this->hProcess = piProcInfo.hProcess;
    // Close the handle to the child's primary thread
    DIE_IF(w->id, !CloseHandle(piProcInfo.hThread));

    // Reopen stdin and stdout pipes using C style FILE
    int stdin_fd  = _open_osfhandle((intptr_t)p_stdin[1], _O_TEXT);
    int stdout_fd = _open_osfhandle((intptr_t)p_stdout[0], _O_TEXT);
    DIE_IF(w->id, stdin_fd == -1);
    DIE_IF(w->id, stdout_fd == -1);
    DIE_IF(w->id, !(this->in = _fdopen(stdout_fd, "r")));
    DIE_IF(w->id, !(this->out = _fdopen(stdin_fd, "w")));

    // Bind child process to the global job, so child process is killed when
    // parent process exits. (This is not needed actually, when parent process
    // already belongs to one job, all subprocesses it creates will automatically
    // be binded to the job by default).
    // DIE_IF(w->id, !AssignProcessToJobObject(handleJob, this->hProcess));
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

    DIE_IF(w->id, (this->pid = fork()) < 0);

    if (this->pid == 0) {
    #ifdef __linux__
        prctl(PR_SET_PDEATHSIG, SIGHUP);  // delegate zombie purge to the kernel
    #endif
        // Plug stdin and stdout
        DIE_IF(w->id, dup2(into[0], STDIN_FILENO) < 0);
        DIE_IF(w->id, dup2(outof[1], STDOUT_FILENO) < 0);

        // For stderr we have 2 choices:
        // - readStdErr=true: dump it into stdout, like doing '2>&1' in bash. This is
        // useful, if we want to see error messages from engines in their respective log
        // file (notably assert() writes to stderr). Of course, such error messages should
        // not be UCI commands, otherwise we will be fooled into parsing them as such.
        // - readStdErr=false: do nothing, which means stderr is inherited from the parent
        // process. Typcically, this means all engines write their error messages to the
        // terminal (unless redirected otherwise).
        if (readStdErr)
            DIE_IF(w->id, dup2(outof[1], STDERR_FILENO) < 0);

    #ifndef __linux__
        // Ugly (and slow) workaround for non-Linux POSIX systems that lack the ability to
        // atomically set O_CLOEXEC when creating pipes.
        for (int fd = 3; fd < sysconf(FOPEN_MAX); close(fd++))
            ;
    #endif

        // Set cwd as current directory, and execute run with argv[]
        DIE_IF(w->id, chdir(cwd) < 0);
        DIE_IF(w->id, execvp(run, argv) < 0);
    }
    else {
        assert(this->pid > 0);

        // in the parent process
        DIE_IF(w->id, close(into[0]) < 0);
        DIE_IF(w->id, close(outof[1]) < 0);

        DIE_IF(w->id, !(this->in = fdopen(outof[0], "r")));
        DIE_IF(w->id, !(this->out = fdopen(into[1], "w")));
    }
#endif
}

static void engine_parse_cmd(const char *              cmd,
                             std::string &             cwd,
                             std::string &             run,
                             std::vector<std::string> &args)
{
    // Isolate the first token being the command to run.
    std::string token;

    // Read a token from source string, returns pointer to the tail string.
    auto readToken = [&token](const char *src) {
        if (*src == '"') {
            // Argument with spaces is assumed to be (esacped) quoted
            const char *next = string_tok_esc(token, src, '"', '\\');
            // Skip next space between argv[i] and argv[i+1]
            return next + (next && *next == ' ');
        }
        else
            return string_tok_esc(token, src, ' ', '\\');
    };

    // Read argv[0] (engine path)
    const char *tail = readToken(cmd);

    // Split token into (cwd, run). Possible cases:
    // (a) unqualified path, like "demolito" (which evecvp() will search in PATH)
    // (b) qualified path (absolute starting with "/", or relative starting with "./" or
    // "../") For (b), we want to separate into executable and directory, so instead of
    // running
    // "../Engines/demolito" from the cwd, we execute run="./demolito" from
    // cwd="../Engines"
    cwd                   = "./";
    run                   = token;
    const char *lastSlash = strrchr(token.c_str(), '/');

    if (lastSlash) {
        cwd = std::string(token, 0, (size_t)(lastSlash - token.c_str()));
        run = format("./%s", lastSlash + 1);
    }

    // Collect the arguments into a vec of string, args[]
    args.push_back(run);  // argv[0] is the executed command

    while ((tail = readToken(tail)))
        args.push_back(token);
}

void Engine::start(const char *cmd, const char *engine_name, int64_t engine_tolerance)
{
    if (!*cmd)
        DIE("[%d] missing command to start engine.\n", w->id);

    this->name      = engine_name;
    this->tolerance = engine_tolerance;

    // Parse cmd into (cwd, run, args): we want to execute run from cwd with args.
    std::string              cwd, run;
    std::vector<std::string> args;
    engine_parse_cmd(cmd, cwd, run, args);

    // execvp() needs NULL terminated char **, not vec of string. Prepare a char **, whose
    // elements point to the C-string buffers of the elements of args, with the required
    // NULL at the end.
    const char **argv = (const char **)calloc(args.size() + 1, sizeof(char *));

    for (size_t i = 0; i < args.size(); i++) {
        argv[i] = args[i].c_str();
    }

    // Spawn child process and plug pipes
    spawn(cwd.c_str(), run.c_str(), argv, w->log != NULL);

    free(argv);

    // parse engine ABOUT infomation
    parse_about(cmd);
}

void Engine::terminate(bool force)
{
    // Engine was not instanciated with start()
    if (!pid)
        return;

    if (!force) {
        // Order the engine to quit, and grant (tolerance) deadline for obeying
        w->deadline_set(name.c_str(), system_msec() + tolerance, "exit");
        writeln("END");
    }

#ifdef __MINGW32__
    // On windows, wait for (tolerance-200) milliseconds, then force
    // terminate child process if it fails to exit in time
    int64_t waitTime = force ? 0 : std::max<int64_t>(tolerance - 200, 0);
    DWORD   result   = WaitForSingleObject(hProcess, waitTime);
    DIE_IF(w->id, result == WAIT_FAILED);
    if (result == WAIT_TIMEOUT)
        DIE_IF(w->id, !TerminateProcess(hProcess, 0));
    DIE_IF(w->id, !CloseHandle(hProcess));
#else
    if (force) {
        if (waitpid(pid, NULL, WNOHANG) == 0)
            DIE_IF(w->id, kill(pid, SIGTERM) < 0);
    }
    else {
        // On unix/linux, wait until deadline
        waitpid(pid, NULL, 0);
    }
#endif

    if (!force)
        w->deadline_clear();

    if (in)
        DIE_IF(w->id, fclose(in) < 0);
    if (out)
        DIE_IF(w->id, fclose(out) < 0);

    // Reset pid, in, out
    pid = 0;
    in = out = nullptr;
}

// returns false when engine timeout or crash, and after that
// is_crashed() can be used to check if the engine has crashed
bool Engine::readln(std::string &line)
{
    if (!in)  // Check if engine has crashed
        return false;

    if (!string_getline(line, in)) {
        // When timeout, main thread will terminate the engine subprocess by force
        // We wait for main thread to complete the termination callback
        w->wait_callback_done();

        // Pipe returning EOF means engine crashed
        // Instead of dying instantly, close pipe to flag engine died and return false
        if (pid) {  // If it is terminated by timeout, process is already closed
            DIE_IF(w->id, fclose(in) < 0);
            DIE_IF(w->id, fclose(out) < 0);
            in = out = nullptr;
        }
        return false;
    }

    if (w->log)
        DIE_IF(w->id, fprintf(w->log, "%s -> %s\n", name.c_str(), line.c_str()) < 0);

    return true;
}

void Engine::writeln(const char *buf)
{
    if (!out)  // Check if engine has crashed
        return;

    DIE_IF(w->id, fputs(buf, out) < 0);
    DIE_IF(w->id, fputc('\n', out) < 0);

    // We take fflush error as engine crashed signal
    if (fflush(out) < 0) {
        // Instead of dying instantly, close pipe to flag engine died
        DIE_IF(w->id, fclose(in) < 0);
        DIE_IF(w->id, fclose(out) < 0);
        in = out = nullptr;
    }

    if (w->log) {
        DIE_IF(w->id, fprintf(w->log, "%s <- %s\n", name.c_str(), buf) < 0);
        DIE_IF(w->id, fflush(w->log) < 0);
    }
}

bool Engine::wait_for_ok(bool fatalError)
{
    std::string line;
    w->deadline_set(name.c_str(), system_msec() + tolerance, "start", [=] {
        if (!fatalError)
            terminate(true);
    });

    do {
        if (!readln(line)) {
            DIE_OR_ERR(fatalError,
                       "[%d] engine %s %s before answering START\n",
                       w->id,
                       name.c_str(),
                       is_crashed() ? "crashed" : "timeout");
            break;
        }

        if (const char *tail = string_prefix(line.c_str(), "ERROR")) {  // an ERROR
            DIE_OR_ERR(fatalError,
                       "[%d] engine %s output error:%s\n",
                       w->id,
                       name.c_str(),
                       tail);
            break;
        }
    } while (line != "OK");

    w->deadline_clear();
    return line == "OK";
}

bool Engine::bestmove(int64_t &    timeLeft,
                      int64_t      maxTurnTime,
                      std::string &best,
                      Info &       info,
                      int          moveply)
{
    const int64_t start          = system_msec();
    const int64_t matchTimeLimit = start + timeLeft;
    int64_t       turnTimeLimit  = matchTimeLimit;
    int64_t       turnTimeLeft   = timeLeft;
    if (maxTurnTime > 0) {
        // engine should not think longer than the turn_time_limit
        turnTimeLimit = start + std::min(timeLeft, maxTurnTime);
        turnTimeLeft  = std::min(timeLeft, maxTurnTime);
    }

    w->deadline_set(name.c_str(), turnTimeLimit + tolerance, "move", [=] {
        terminate(true);
    });
    int64_t     moveOverhead = std::min<int64_t>(tolerance / 2, 1000);
    bool        result       = false;
    std::string line;

    while ((turnTimeLeft + moveOverhead) >= 0 && !result) {
        if (!readln(line))
            goto Exit;

        const int64_t now = system_msec();
        info.time         = now - start;
        timeLeft          = matchTimeLimit - now;
        turnTimeLeft      = turnTimeLimit - now;

        if (isDebug)
            process_message_ifneeded(line.c_str());

        if (const char *tail = string_prefix(line.c_str(), "MESSAGE")) {
            // record engine messages
            if (messages)
                *messages += format("%i) %s: %s\n", moveply, name, tail + 1);

            // parse and store thing infomation to info
            parse_thinking_messages(line.c_str(), info);
        }
        else if (Position::is_valid_move_gomostr(line)) {
            best   = line;
            result = true;
        }
    }

    // Time out. Send "stop" and give the opportunity to the engine to respond with
    // bestmove (still under deadline protection).
    if (!result) {
        writeln("YXSTOP");

        // For turn timeout, explicitly mark time left as negetive
        timeLeft = INT64_MIN;

        do {
            if (!readln(line))
                goto Exit;

            if (isDebug)
                process_message_ifneeded(line.c_str());

            if (const char *tail = string_prefix(line.c_str(), "MESSAGE")) {
                // record engine messages
                if (messages)
                    *messages += format("%i) %s: %s\n", moveply, name, tail + 1);

                // parse and store thing infomation to info
                parse_thinking_messages(line.c_str(), info);
            }
        } while (result = Position::is_valid_move_gomostr(line), !result);
    }

Exit:
    w->deadline_clear();
    return result;
}

static void parse_and_display_engine_about(const Worker *   w,
                                           std::string_view line,
                                           std::string &    engine_name)
{
    int                      flag = 0;
    std::vector<std::string> tokens;
    std::stringstream        ss;
    for (size_t i = 0; i < line.size(); i++) {
        char ch = line[i];
        if (ch == '\"') {
            flag = (flag + 1) % 2;
        }
        else if (ch == ',' || ch == ' ' || ch == '=') {
            if (flag > 0) {
                ss << ch;
            }
            else {
                if (ss.str().length() > 0) {
                    tokens.push_back(ss.str());
                }
                ss.clear();
                ss.str("");
            }
        }
        else {
            ss << ch;
        }
    }
    if (ss.str().length() > 0) {
        tokens.push_back(ss.str());
    }

    std::string name    = "?";
    std::string author  = "?";
    std::string version = "?";
    std::string country = "?";
    for (size_t i = 0; i < tokens.size(); i++) {
        if (tokens[i] == "name") {
            if ((i + 1) < tokens.size()) {
                name = tokens[i + 1];
                // Set engine name to about name
                if (engine_name.empty())
                    engine_name = name;
            }
        }
        else if (tokens[i] == "version") {
            if ((i + 1) < tokens.size()) {
                version = tokens[i + 1];
            }
        }
        else if (tokens[i] == "author") {
            if ((i + 1) < tokens.size()) {
                author = tokens[i + 1];
            }
        }
        else if (tokens[i] == "country") {
            if ((i + 1) < tokens.size()) {
                country = tokens[i + 1];
            }
        }
    }

    std::printf("[%d] Load engine: %s (version %s) by %s, %s\n",
                w->id,
                name.c_str(),
                version.c_str(),
                author.c_str(),
                country.c_str());
}

// process engine ABOUT command
void Engine::parse_about(const char *fallbackName)
{
    w->deadline_set(!name.empty() ? name.c_str() : fallbackName,
                    system_msec() + tolerance,
                    "about");
    writeln("ABOUT");

    std::string line;
    if (!readln(line)) {
        DIE("[%d] engine %s exited before answering ABOUT\n", w->id, name.c_str());
    }

    w->deadline_clear();

    // parse about infos
    parse_and_display_engine_about(w, line, name);

    // If we can not get a name from engname or about, use fallback name instead
    if (name.empty())
        name = fallbackName;
}

// process MESSAGE, UNKNOWN, ERROR, DEBUG messages
void Engine::process_message_ifneeded(const char *line)
{
    // Isolate the first token being the command to run.
    const char *tail = nullptr;

    if ((tail = string_prefix(line, "MESSAGE"))) {
        printf("engine %s output message:%s\n", name.c_str(), tail);
    }
    else if ((tail = string_prefix(line, "UNKNOWN"))) {
        printf("engine %s output unknown:%s\n", name.c_str(), tail);
    }
    else if ((tail = string_prefix(line, "DEBUG"))) {
        printf("engine %s output debug:%s\n", name.c_str(), tail);
    }
    else if ((tail = string_prefix(line, "ERROR"))) {
        printf("engine %s output error:%s\n", name.c_str(), tail);
    }
}

void Engine::parse_thinking_messages([[maybe_unused]] const char *line, Info &info)
{
    // Set default value
    info.score = 0;
    info.depth = 0;
}
