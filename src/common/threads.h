#pragma once
#include "cmdlib.h" //--vluzacn
#include "log.h"

constexpr int MAX_THREADS = 64;

typedef enum
{
    eThreadPriorityLow = -1,
    eThreadPriorityNormal,
    eThreadPriorityHigh
} q_threadpriority;

typedef void (*q_threadfunction)(int);

constexpr int DEFAULT_NUMTHREADS = 1;

#define DEFAULT_THREAD_PRIORITY eThreadPriorityNormal

extern int g_numthreads;
extern q_threadpriority g_threadpriority;

extern void ThreadSetPriority(q_threadpriority type);
extern void ThreadSetDefault();
extern auto GetThreadWork() -> int;
extern void ThreadLock();
extern void ThreadUnlock();

extern void RunThreadsOnIndividual(int workcnt, bool showpacifier, q_threadfunction);
extern void RunThreadsOn(int workcnt, bool showpacifier, q_threadfunction);

#define NamedRunThreadsOn(n, p, f)     \
    {                                  \
        Log("%s\n", Localize(#f ":")); \
        RunThreadsOn(n, p, f);         \
    }
#define NamedRunThreadsOnIndividual(n, p, f) \
    {                                        \
        Log("%s\n", Localize(#f ":"));       \
        RunThreadsOnIndividual(n, p, f);     \
    }
