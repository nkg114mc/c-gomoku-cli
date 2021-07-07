#include <cstdlib>
#include "workers.h"
#include "util.h"
#include "vec.h"


std::vector<Worker> Workers;

void Worker::deadline_set(const char *engineName, int64_t timeLimit, 
    const char *description, std::function<void()> callback)
{
    assert(timeLimit > 0);

    pthread_mutex_lock(&deadline.mtx);

    deadline.set = true;
    deadline.called = false;
    deadline.engineName = engineName;
    deadline.description = description;
    deadline.timeLimit = timeLimit;
    deadline.callback = callback;

    pthread_mutex_unlock(&deadline.mtx);

    if (log)
        DIE_IF(id, fprintf(log, "deadline: %s must respond to [%s] by %" PRId64 "\n", 
            engineName, description, timeLimit) < 0);
}

void Worker::deadline_clear()
{
    pthread_mutex_lock(&deadline.mtx);

    deadline.set = false;

    if (log)
        DIE_IF(id, fprintf(log, "deadline: %s responded [%s] before %" PRId64 "\n",
            deadline.engineName.c_str(), deadline.description.c_str(), deadline.timeLimit) < 0);

    pthread_mutex_unlock(&deadline.mtx);
}

void Worker::deadline_callback_once()
{
    pthread_mutex_lock(&deadline.mtx);

    if (deadline.set && !deadline.called) {
        deadline.called = true;
        if (deadline.callback)
            deadline.callback();
    }

    pthread_mutex_unlock(&deadline.mtx);
}

int64_t Worker::deadline_overdue()
{
    pthread_mutex_lock(&deadline.mtx);

    const int64_t timeLimit = deadline.timeLimit;
    const bool set = deadline.set;

    pthread_mutex_unlock(&deadline.mtx);

    const int64_t time = system_msec();

    if (set && time > timeLimit)
        return time - timeLimit;
    else
        return 0;
}

Worker::Worker(int i, const char *logName)
{
    seed = (uint64_t)i;
    id = i + 1;
    pthread_mutex_init(&deadline.mtx, NULL);

    log = NULL;
    if (*logName) {
        log = fopen(logName, FOPEN_WRITE_MODE);
        DIE_IF(0, !log);
    }
}

Worker::~Worker()
{
    pthread_mutex_destroy(&deadline.mtx);
    if (log) {
        DIE_IF(0, fclose(log) < 0);
        log = NULL;
    }
}
