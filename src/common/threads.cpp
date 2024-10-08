#include <cstdio>

#include "cmdlib.h"
#include "messages.h"
#include "log.h"
#include "threads.h"
#include "blockmem.h"

#include <sys/time.h>
#include <sys/resource.h>
#include <pthread.h>

#include "hlassert.h"

q_threadpriority g_threadpriority = DEFAULT_THREAD_PRIORITY;

constexpr int THREADTIMES_SIZE = 100;
constexpr float THREADTIMES_SIZEf = static_cast<float>(THREADTIMES_SIZE);

static int dispatch = 0;
static int workcount = 0;
static int oldf = 0;
static bool pacifier = false;
static bool threaded = false;
static double threadstart = 0;
static double threadtimes[THREADTIMES_SIZE];

auto GetThreadWork() -> int
{
    int r, f, i;
    double ct, finish, finish2, finish3;
    static const char *s1 = nullptr; // avoid frequent call of Localize() in PrintConsole
    static const char *s2 = nullptr;

    ThreadLock();
    if (s1 == nullptr)
        s1 = Localize("  (%d%%: est. time to completion %ld/%ld/%ld secs)   ");
    if (s2 == nullptr)
        s2 = Localize("  (%d%%: est. time to completion <1 sec)   ");

    if (dispatch == 0)
    {
        oldf = 0;
    }

    if (dispatch > workcount)
    {
        ThreadUnlock();
        return -1;
    }
    if (dispatch == workcount)
    {
        ThreadUnlock();
        return -1;
    }
    if (dispatch < 0)
    {
        ThreadUnlock();
        return -1;
    }

    f = THREADTIMES_SIZE * dispatch / workcount;
    if (pacifier)
    {
        PrintConsole("\r%6d /%6d", dispatch, workcount);

        if (f != oldf)
        {
            ct = I_FloatTime();
            /* Fill in current time for threadtimes record */
            for (i = oldf; i <= f; i++)
            {
                if (threadtimes[i] < 1)
                {
                    threadtimes[i] = ct;
                }
            }
            oldf = f;

            if (f > 10)
            {
                finish = (ct - threadtimes[0]) * (THREADTIMES_SIZEf - f) / f;
                finish2 = 10.0 * (ct - threadtimes[f - 10]) * (THREADTIMES_SIZEf - f) / THREADTIMES_SIZEf;
                finish3 = THREADTIMES_SIZEf * (ct - threadtimes[f - 1]) * (THREADTIMES_SIZEf - f) / THREADTIMES_SIZEf;

                if (finish > 1.0)
                {
                    PrintConsole(s1, f, (long)(finish), (long)(finish2),
                                 (long)(finish3));
                }
                else
                {
                    PrintConsole(s2, f);
                }
            }
        }
    }
    else
    {
        if (f != oldf)
        {
            oldf = f;
            switch (f)
            {
            case 10:
            case 20:
            case 30:
            case 40:
            case 50:
            case 60:
            case 70:
            case 80:
            case 90:
            case 100:
                /*
                            case 5:
                            case 15:
                            case 25:
                            case 35:
                            case 45:
                            case 55:
                            case 65:
                            case 75:
                            case 85:
                            case 95:
                */
                PrintConsole("%d%%...", f);
            default:
                break;
            }
        }
    }

    r = dispatch;
    dispatch++;

    ThreadUnlock();
    return r;
}

q_threadfunction workfunction;

static void ThreadWorkerFunction(int unused)
{
    int work;

    while ((work = GetThreadWork()) != -1)
    {
        workfunction(work);
    }
}

void RunThreadsOnIndividual(int workcnt, bool showpacifier, q_threadfunction func)
{
    workfunction = func;
    RunThreadsOn(workcnt, showpacifier, ThreadWorkerFunction);
}

int g_numthreads = DEFAULT_NUMTHREADS;

void ThreadSetPriority(q_threadpriority type)
{
    int val;

    g_threadpriority = type;

    // Currently in Linux land users are incapable of raising the priority level of their processes
    // Unless you are root -high is useless . . .
    switch (g_threadpriority)
    {
    case eThreadPriorityLow:
        val = PRIO_MAX;
        break;

    case eThreadPriorityHigh:
        val = PRIO_MIN;
        break;

    case eThreadPriorityNormal:
    default:
        val = 0;
        break;
    }
    setpriority(PRIO_PROCESS, 0, val);
}

void ThreadSetDefault()
{
    if (g_numthreads == -1)
    {
        g_numthreads = 1;
    }
}

typedef void *pthread_addr_t;
pthread_mutex_t *my_mutex;

void ThreadLock()
{
    if (my_mutex)
    {
        pthread_mutex_lock(my_mutex);
    }
}

void ThreadUnlock()
{
    if (my_mutex)
    {
        pthread_mutex_unlock(my_mutex);
    }
}

q_threadfunction q_entry;

static auto ThreadEntryStub(void *pParam) -> void *
{
    q_entry((int)(intptr_t)pParam);
    return nullptr;
}

void threads_InitCrit()
{
    pthread_mutexattr_t mattrib;

    if (!my_mutex)
    {
        my_mutex = (pthread_mutex_t *)Alloc(sizeof(*my_mutex));
        if (pthread_mutexattr_init(&mattrib) == -1)
        {
            Error("pthread_mutex_attr_init failed");
        }
        if (pthread_mutex_init(my_mutex, &mattrib) == -1)
        {
            Error("pthread_mutex_init failed");
        }
    }
}

void threads_UninitCrit()
{
    delete my_mutex;
    my_mutex = nullptr;
}

/*
 * =============
 * RunThreadsOn
 * =============
 */
void RunThreadsOn(int workcnt, bool showpacifier, q_threadfunction func)
{
    int i;
    pthread_t work_threads[MAX_THREADS];
    pthread_addr_t status;
    pthread_attr_t attrib;
    double start, end;

    threadstart = I_FloatTime();
    start = threadstart;
    for (i = 0; i < THREADTIMES_SIZE; i++)
    {
        threadtimes[i] = 0;
    }

    dispatch = 0;
    workcount = workcnt;
    oldf = -1;
    pacifier = showpacifier;
    threaded = true;
    q_entry = func;

    if (pacifier)
    {
        setbuf(stdout, nullptr);
    }

    threads_InitCrit();

    if (pthread_attr_init(&attrib) == -1)
    {
        Error("pthread_attr_init failed");
    }
#ifdef _POSIX_THREAD_ATTR_STACKSIZE
    if (pthread_attr_setstacksize(&attrib, 0x400000) == -1)
    {
        Error("pthread_attr_setstacksize failed");
    }
#endif

    for (i = 0; i < g_numthreads; i++)
    {
        if (pthread_create(&work_threads[i], &attrib, ThreadEntryStub, (void *)(intptr_t)i) == -1)
        {
            Error("pthread_create failed");
        }
    }

    for (i = 0; i < g_numthreads; i++)
    {
        if (pthread_join(work_threads[i], &status) == -1)
        {
            Error("pthread_join failed");
        }
    }

    threads_UninitCrit();

    q_entry = nullptr;
    threaded = false;

    end = I_FloatTime();
    if (pacifier)
    {
        PrintConsole("\r%60s\r", "");
    }

    Log(" (%.2f seconds)\n", end - start);
}