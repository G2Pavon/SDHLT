#include "log.h"

typedef enum
{
    PROGRAM_CSG,
    PROGRAM_BSP,
    PROGRAM_VIS,
    PROGRAM_RAD
} ProgramType;

void Usage(ProgramType programType);

// TODO Settings()