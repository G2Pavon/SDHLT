#include "arguments.h"


void Usage(ProgramType programType)
{
    Log("Usage:");
    switch (programType) {
        case PROGRAM_CSG:
            Log(" %s.exe map_name.map -argument", g_Program);
            Log("\n %s Arguments :\n\n", g_Program);
            Log("    -console #       : Set to 0 to turn off the pop-up console (default is 1)\n");
            Log("    -clipeconomy     : turn clipnode economy mode on\n");
            Log("    -cliptype value  : set to smallest, normalized, simple, precise, or legacy (default)\n");
            Log("    -lightdata #     : Alter maximum lighting memory limit (in kb)\n");
            Log("    -low | -high     : run program an altered priority level\n");
            Log("    -noskyclip       : disable automatic clipping of SKY brushes\n");
            Log("    -texdata #       : Alter maximum texture memory limit (in kb)\n");
            Log("    -threads #       : manually specify the number of threads to run\n");
            Log("    -worldextent #   : Extend map geometry limits beyond +/-32768.\n");
            break;

        case PROGRAM_BSP:
            Log(" %s.exe map_name.map -argument", g_Program);
            Log("\n %s Arguments :\n\n", g_Program);
            Log("    -console #     : Set to 0 to turn off the pop-up console (default is 1)\n");
            Log("    -leakonly      : Run BSP only enough to check for LEAKs\n");
            Log("    -subdivide #   : Sets the face subdivide size\n");
            Log("    -maxnodesize # : Sets the maximum portal node size\n\n");
            Log("    -notjunc       : Don't break edges on t-junctions     (not for final runs)\n");
            Log("    -nobrink       : Don't smooth brinks                  (not for final runs)\n");
            Log("    -nofill        : Don't fill outside (will mask LEAKs) (not for final runs)\n");
            Log("    -noinsidefill  : Don't fill empty spaces\n");
            Log("    -noopt         : Don't optimize planes on BSP write   (not for final runs)\n");
            Log("    -noclipnodemerge: Don't optimize clipnodes\n");
            Log("    -texdata #     : Alter maximum texture memory limit (in kb)\n");
            Log("    -lightdata #   : Alter maximum lighting memory limit (in kb)\n");
            Log("    -low | -high   : run program an altered priority level\n");
            Log("    -threads #     : manually specify the number of threads to run\n");
            Log("    -nohull2       : Don't generate hull 2 (the clipping hull for large monsters and pushables)\n");
            Log("    -viewportal    : Show portal boundaries in 'mapname_portal.pts' file\n");
            break;

        case PROGRAM_VIS:
            Log(" %s.exe <mapname.map> -argument", g_Program);
            Log("\n %s Arguments :\n\n", g_Program);
            Log("    -console #      : Set to 0 to turn off the pop-up console (default is 1)\n");
            Log("    -full           : Full vis\n");
            Log("    -fast           : Fast vis\n\n");
            Log("    -texdata #      : Alter maximum texture memory limit (in kb)\n");
            Log("    -lightdata #    : Alter maximum lighting memory limit (in kb)\n"); //lightdata //--vluzacn
            Log("    -low | -high    : run program an altered priority level\n");
            Log("    -maxdistance #  : Alter the maximum distance for visibility\n");
            Log("    -threads #      : manually specify the number of threads to run\n");
            break;

        case PROGRAM_RAD:
            Log(" %s.exe <mapname.map> -argument", g_Program);
            Log("\n %s Arguments :\n\n", g_Program);
            Log("    -console #      : Set to 0 to turn off the pop-up console (default is 1)\n");
            Log("    -waddir folder  : Search this folder for wad files.\n");
            Log("    -fast           : Fast rad\n");
            Log("    -vismatrix value: Set vismatrix method to normal, sparse or off.\n");
            Log("    -pre25          : Optimize compile for pre-Half-Life 25th anniversary update.\n");
            Log("    -extra          : Improve lighting quality by doing 9 point oversampling\n");
            Log("    -bounce #       : Set number of radiosity bounces\n");
            Log("    -ambient r g b  : Set ambient world light (0.0 to 1.0, r g b)\n");
            Log("    -limiter #      : Set light clipping threshold (-1=None)\n");
            Log("    -circus         : Enable 'circus' mode for locating unlit lightmaps\n");
            Log("    -nospread       : Disable sunlight spread angles for this compile\n");
            Log("    -nopaque        : Disable the opaque zhlt_lightflags for this compile\n\n");
            Log("    -nostudioshadow : Disable opaque studiomodels, ignore zhlt_studioshadow for this compile\n\n");
            Log("    -smooth #       : Set smoothing threshold for blending (in degrees)\n");
            Log("    -smooth2 #      : Set smoothing threshold between different textures\n");
            Log("    -chop #         : Set radiosity patch size for normal textures\n");
            Log("    -texchop #      : Set radiosity patch size for texture light faces\n\n");
            Log("    -notexscale     : Do not scale radiosity patches with texture scale\n");
            Log("    -coring #       : Set lighting threshold before blackness\n");
            Log("    -dlight #       : Set direct lighting threshold\n");
            Log("    -fade #         : Set global fade (larger values = shorter lights)\n");
            Log("    -texlightgap #  : Set global gap distance for texlights\n");
            Log("    -scale #        : Set global light scaling value\n");
            Log("    -gamma #        : Set global gamma value\n\n");
            Log("    -sky #          : Set ambient sunlight contribution in the shade outside\n");
            Log("    -lights file    : Manually specify a lights.rad file to use\n");
            Log("    -noskyfix       : Disable light_environment being global\n");
            Log("    -incremental    : Use or create an incremental transfer list file\n\n");
            Log("    -texdata #      : Alter maximum texture memory limit (in kb)\n");
            Log("    -lightdata #    : Alter maximum lighting memory limit (in kb)\n");
            Log("    -low | -high    : run program an altered priority level\n");
            Log("    -threads #      : manually specify the number of threads to run\n");
            Log("    -colourgamma r g b  : Sets different gamma values for r, g, b\n");
            Log("    -colourscale r g b  : Sets different lightscale values for r, g ,b\n");
            Log("    -colourjitter r g b : Adds noise, independent colours, for dithering\n");
            Log("    -jitter r g b       : Adds noise, monochromatic, for dithering\n");
            Log("    -customshadowwithbounce : Enables custom shadows with bounce light\n");
            Log("    -rgbtransfers           : Enables RGB Transfers (for custom shadows)\n\n");
            Log("    -minlight #    : Minimum final light (integer from 0 to 255)\n");
            Log("    -softsky #     : Smooth skylight.(0=off 1=on)\n");
            Log("    -depth #       : Thickness of translucent objects.\n");
            Log("    -blockopaque # : Remove the black areas around opaque entities.(0=off 1=on)\n");
            Log("    -notextures    : Don't load textures.\n");
            Log("    -texreflectgamma # : Gamma that relates reflectivity to texture color bits.\n");
            Log("    -texreflectscale # : Reflectivity for 255-white texture.\n");
            Log("    -blur #        : Enlarge lightmap sample to blur the lightmap.\n");
            Log("    -noemitterrange: Don't fix pointy texlights.\n");
            Log("    -nobleedfix    : Don't fix wall bleeding problem for faces with large seams.\n");
            Log("    -noattic       : Skip shadow processing for small attic lights.\n");
            Log("    -nooutside     : Skip shadow processing for small outside lights.\n");
            Log("    -fixsunlight   : Force correct sun lighting (overrides debug modes).\n");
            Log("    -nooverlay     : Do not use overlay textures for the sky (for debugging)\n");
            Log("    -nosunlight    : Do not use sunlight as ambient light.\n");
            Log("    -nobloom       : Disable bloom effect (default is enabled).\n");
            Log("    -notexlight    : Disable texture lighting\n");
            Log("    -notexsmoothing: Disable texture smoothing (default is enabled).\n");
            Log("    -noexposure    : Disable exposure adjustment (default is enabled).\n");
            Log("    -lineargamma   : Enable linear gamma correction\n");
            Log("    -low | -high   : run program an altered priority level\n");
            Log("    -threads #     : manually specify the number of threads to run\n");
            break;

        default:
            Log("Unknown program type.\n");
            exit(1);
    }
    exit(1);
}