#pragma once

enum class ProgramType
{
    PROGRAM_CSG,
    PROGRAM_BSP,
    PROGRAM_VIS,
    PROGRAM_RAD
};

void Usage(ProgramType programType);

// TODO Settings()