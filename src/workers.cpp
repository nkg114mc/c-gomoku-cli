#include <cstdlib>
#include "workers.h"
#include "util.h"
#include "vec.h"


std::vector<Worker> Workers;

void Worker::deadline_set(const char *engineName, int64_t timeLimit, const char *description)
{
    assert(timeLimit > 0);

    pthread_mutex_lock(&deadline.mtx);

    deadline.set = true;
    str_cpy_c(&deadline.engineName, engineName);
    str_cpy_c(&deadline.description, description);
    deadline.timeLimit = timeLimit;

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
            deadline.engineName.buf, deadline.description.buf, deadline.timeLimit) < 0);

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

void Worker::worker_init(int i, const char *logName)
{
    seed = (uint64_t)i;
    id = i + 1;
    pthread_mutex_init(&deadline.mtx, NULL);
    deadline.engineName = str_init();
    deadline.description = str_init();

    log = NULL;
    if (*logName) {
        log = fopen(logName, FOPEN_WRITE_MODE);
        DIE_IF(0, !log);
    }

}

void Worker::worker_destroy()
{
    str_destroy(&deadline.engineName);
    str_destroy(&deadline.description);
    pthread_mutex_destroy(&deadline.mtx);

    if (log) {
        DIE_IF(0, fclose(log) < 0);
        log = NULL;
    }
}
