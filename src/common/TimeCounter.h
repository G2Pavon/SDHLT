#pragma once

#include "cmdlib.h"

class TimeCounter
{
public:
    void start()
    {
        starttime = I_FloatTime();
    }

    void stop()
    {
        double stop = I_FloatTime();
        accum += stop - starttime;
    }

    auto getTotal() const -> double
    {
        return accum;
    }

    void reset()
    {
        memset(this, 0, sizeof(*this));
    }

    // Construction
public:
    TimeCounter()
    {
        reset();
    }
    // Default Destructor ok
    // Default Copy Constructor ok
    // Default Copy Operator ok

protected:
    double starttime;
    double accum;
};