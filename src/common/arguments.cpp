#include "arguments.h"

void Usage(ProgramType programType)
{
    Log("Usage:");
    switch (programType)
    {
    case ProgramType::PROGRAM_CSG:
        Log(" %s.exe map_name.map -argument", g_Program);
        Log("\n %s Arguments :\n\n", g_Program);
        Log("    -clipeconomy     : turn clipnode economy mode on\n");
        Log("    -cliptype value  : set to smallest, normalized, simple, precise, or legacy (default)\n");
        Log("    -lightdata #     : Alter maximum lighting memory limit (in kb)\n");
        Log("    -noskyclip       : disable automatic clipping of SKY brushes\n");
        Log("    -texdata #       : Alter maximum texture memory limit (in kb)\n");
        Log("    -worldextent #   : Extend map geometry limits beyond +/-32768.\n");
        break;

    case ProgramType::PROGRAM_BSP:
        Log(" %s.exe map_name.map -argument", g_Program);
        Log("\n %s Arguments :\n\n", g_Program);
        Log("    -console #     : Set to 0 to turn off the pop-up console (default is 1)\n");
        Log("    -subdivide #   : Sets the face subdivide size\n");
        Log("    -maxnodesize # : Sets the maximum portal node size\n\n");
        Log("    -texdata #     : Alter maximum texture memory limit (in kb)\n");
        Log("    -lightdata #   : Alter maximum lighting memory limit (in kb)\n");
        Log("    -nohull2       : Don't generate hull 2 (the clipping hull for large monsters and pushables)\n");
        break;

    case ProgramType::PROGRAM_VIS:
        Log(" %s.exe <mapname.map> -argument", g_Program);
        Log("\n %s Arguments :\n\n", g_Program);
        Log("    -console #      : Set to 0 to turn off the pop-up console (default is 1)\n");
        Log("    -full           : Full vis\n");
        Log("    -fast           : Fast vis\n\n");
        Log("    -texdata #      : Alter maximum texture memory limit (in kb)\n");
        Log("    -lightdata #    : Alter maximum lighting memory limit (in kb)\n"); // lightdata //--vluzacn
        Log("    -low | -high    : run program an altered priority level\n");
        Log("    -maxdistance #  : Alter the maximum distance for visibility\n");
        Log("    -threads #      : manually specify the number of threads to run\n");
        break;

    case ProgramType::PROGRAM_RAD:
        Log(" %s.exe <mapname.map> -argument", g_Program);
        Log("\n %s Arguments :\n\n", g_Program);
        Log("    -console #      : Set to 0 to turn off the pop-up console (default is 1)\n");
        Log("    -extra          : Improve lighting quality by doing 9 point oversampling\n");
        Log("    -bounce #       : Set number of radiosity bounces\n");
        Log("    -limiter #      : Set light clipping threshold (-1=None)\n");
        Log("    -chop #         : Set radiosity patch size for normal textures\n");
        Log("    -texchop #      : Set radiosity patch size for texture light faces\n\n");
        Log("    -fade #         : Set global fade (larger values = shorter lights)\n");
        Log("    -texdata #      : Alter maximum texture memory limit (in kb)\n");
        Log("    -low | -high    : run program an altered priority level\n");
        Log("    -lightdata #    : Alter maximum lighting memory limit (in kb)\n");
        Log("    -threads #      : manually specify the number of threads to run\n");
        break;

    default:
        Log("Unknown program type.\n");
        exit(1);
    }
    exit(1);
}