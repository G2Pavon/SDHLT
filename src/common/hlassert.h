#pragma once

#include "cmdlib.h" //--vluzacn

#ifdef SYSTEM_POSIX

#define assume(exp, message)                                                                      \
    {                                                                                             \
        if (!(exp))                                                                               \
        {                                                                                         \
            Error("\nAssume '%s' failed\n at %s:%d\n %s\n\n", #exp, __FILE__, __LINE__, message); \
        }                                                                                         \
    } // #define assume(exp, message) {if (!(exp)) {Error("\nAssume '%s' failed\n\n", #exp, __FILE__, __LINE__, message);}} //--vluzacn
#define hlassert(exp)
#endif // SYSTEM_POSIX