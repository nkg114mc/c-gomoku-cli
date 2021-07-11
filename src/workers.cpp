#include "workers.h"

#include "util.h"

#include <cassert>
#include <cstdlib>

Worker::Worker(int i, const char *logName) : id(i + 1), seed(i), log(nullptr)
{
    if (*logName) {
        log = fopen(logName, FOPEN_WRITE_MODE);
        DIE_IF(0, !log);
    }
}

Worker::~Worker()
{
    if (log) {
        DIE_IF(0, fclose(log) < 0);
        log = NULL;
    }
}

void Worker::deadline_set(const char *          engineName,
                          int64_t               timeLimit,
                          const char *          description,
                          std::function<void()> callback)
{
    assert(timeLimit > 0);

    {
        std::lock_guard lock(deadline.mtx);

        deadline.set         = true;
        deadline.called      = false;
        deadline.engineName  = engineName;
        deadline.description = description;
        deadline.timeLimit   = timeLimit;
        deadline.callback    = callback;
    }

    if (log)
        DIE_IF(id,
               fprintf(log,
                       "deadline: %s must respond to [%s] by %" PRId64 "\n",
                       engineName,
                       description,
                       timeLimit)
                   < 0);
}

void Worker::deadline_clear()
{
    std::lock_guard lock(deadline.mtx);

    deadline.set = false;

    if (log)
        DIE_IF(id,
               fprintf(log,
                       "deadline: %s responded [%s] before %" PRId64 "\n",
                       deadline.engineName.c_str(),
                       deadline.description.c_str(),
                       deadline.timeLimit)
                   < 0);
}

void Worker::deadline_callback_once()
{
    std::lock_guard lock(deadline.mtx);

    if (deadline.set && !deadline.called) {
        deadline.called = true;
        if (deadline.callback)
            deadline.callback();
    }
}

int64_t Worker::deadline_overdue()
{
    const int64_t time = system_msec();

    std::lock_guard lock(deadline.mtx);

    if (deadline.set && time > deadline.timeLimit)
        return time - deadline.timeLimit;
    else
        return 0;
}

void Worker::wait_callback_done()
{
    deadline.mtx.lock();
    deadline.mtx.unlock();
}