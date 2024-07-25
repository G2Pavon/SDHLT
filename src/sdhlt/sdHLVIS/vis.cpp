/*
 
    VISIBLE INFORMATION SET    -aka-    V I S

    Code based on original code from Valve Software, 
    Modified by Sean "Zoner" Cavanaugh (seanc@gearboxsoftware.com) with permission.
    Modified by Tony "Merl" Moore (merlinis@bigpond.net.au)
    Contains code by Skyler "Zipster" York (zipster89134@hotmail.com) - Included with permission.
    Modified by amckern (amckern@yahoo.com)
    Modified by vluzacn (vluzacn@163.com)
    Modified by seedee (cdaniel9000@gmail.com)

*/

#include "vis.h"
#include "arguments.h"
#ifdef SYSTEM_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// std:clamp() is at least MVSC 19
#define CLAMP(x, min, max) x <= min ? min : x >= max ? max : x
#endif

#ifdef SYSTEM_POSIX
#include <algorithm>
#define CLAMP(x, min, max) std::clamp(x, min, max)
#endif

#include <string>

/*

 NOTES

*/

int             g_numportals = 0;
unsigned        g_portalleafs = 0;

portal_t*       g_portals;

leaf_t*         g_leafs;
int				*g_leafstarts;
int				*g_leafcounts;
int				g_leafcount_all;

// AJM: MVD
//

static byte*    vismap;
static byte*    vismap_p;
static byte*    vismap_end;                                // past visfile
static int      originalvismapsize;

byte*           g_uncompressed;                            // [bitbytes*portalleafs]

unsigned        g_bitbytes;                                // (portalleafs+63)>>3
unsigned        g_bitlongs;

bool            g_fastvis = DEFAULT_FASTVIS;
bool            g_fullvis = DEFAULT_FULLVIS;
bool            g_estimate = DEFAULT_ESTIMATE;

// AJM: MVD
unsigned int	g_maxdistance = DEFAULT_MAXDISTANCE_RANGE;
//bool			g_postcompile = DEFAULT_POST_COMPILE;
//
const int		g_overview_max = MAX_MAP_ENTITIES;
overview_t		g_overview[g_overview_max];
int				g_overview_count = 0;
leafinfo_t*		g_leafinfos = NULL;

const int       g_room_max = MAX_MAP_ENTITIES;
room_t          g_room[g_room_max];
int             g_room_count = 0;

static int      totalvis = 0;

// =====================================================================================
//  PlaneFromWinding
// =====================================================================================
static void     PlaneFromWinding(winding_t* w, plane_t* plane)
{
    vec3_t          v1;
    vec3_t          v2;

    // calc plane
    VectorSubtract(w->points[2], w->points[1], v1);
    VectorSubtract(w->points[0], w->points[1], v2);
    CrossProduct(v2, v1, plane->normal);
    VectorNormalize(plane->normal);
    plane->dist = DotProduct(w->points[0], plane->normal);
}

// =====================================================================================
//  NewWinding
// =====================================================================================
static winding_t* NewWinding(const int points)
{
    winding_t*      w;
    int             size;

    if (points > MAX_POINTS_ON_WINDING)
    {
        Error("NewWinding: %i points > MAX_POINTS_ON_WINDING", points);
    }

    size = (int)(intptr_t)((winding_t*)0)->points[points];
    w = (winding_t*)calloc(1, size);

    return w;
}


// =====================================================================================
//  GetNextPortal
//      Returns the next portal for a thread to work on
//      Returns the portals from the least complex, so the later ones can reuse the earlier information.
// =====================================================================================
static portal_t* GetNextPortal()
{
    int             j;
    portal_t*       p;
    portal_t*       tp;
    int             min;

    if (GetThreadWork() == -1)
    {
        return NULL;
    }
    ThreadLock();

    min = 99999;
    p = NULL;

    for (j = 0, tp = g_portals; j < g_numportals * 2; j++, tp++)
    {
        if (tp->nummightsee < min && tp->status == stat_none)
        {
            min = tp->nummightsee;
            p = tp;
        }
    }

    if (p)
    {
        p->status = stat_working;
    }
    ThreadUnlock();
    return p;
}


// =====================================================================================
//  LeafThread
// =====================================================================================
#ifdef SYSTEM_WIN32
#pragma warning(push)
#pragma warning(disable: 4100)                             // unreferenced formal parameter
#endif

static void     LeafThread(int unused)
{
    portal_t*       p;

    while (1)
    {
        if (!(p = GetNextPortal()))
        {
            return;
        }
        PortalFlow(p);
    }
}

#ifdef SYSTEM_WIN32
#pragma warning(pop)
#endif

// Recursively add `add` to `current` visibility leaf.
std::unordered_map<int, bool> leaf_flow_add_exclude = {};
static void LeafFlowNeighborAddLeaf(const int current, const int add, const int neighbor)
{
    auto outbuffer = g_uncompressed + current * g_bitbytes;

    outbuffer[add >> 3] |= (1 << (add & 7));
    leaf_flow_add_exclude[current] = true;
    
    if (neighbor == 0)
    {
        return;
    }

    auto leaf = &g_leafs[current];

    for (int i = 0; i < leaf->numportals; i++)
    {
        auto p = leaf->portals[i];

        if (leaf_flow_add_exclude[p->leaf]) {
            // Log("leaf %d neighbor %d is excluded\n", current, p->leaf);
            continue;
        }

        LeafFlowNeighborAddLeaf(p->leaf, add, neighbor - 1);
    }
}

// =====================================================================================
//  LeafFlow
//      Builds the entire visibility list for a leaf
// =====================================================================================
static void     LeafFlow(const int leafnum)
{
    leaf_t*         leaf;
    byte*           outbuffer;
    byte            compressed[MAX_MAP_LEAFS / 8];
    unsigned        i;
    unsigned        j;
    int             k;
    int             tmp;
    int             numvis;
    byte*           dest;
    portal_t*       p;

    //
    // flow through all portals, collecting visible bits
    //
    memset(compressed, 0, sizeof(compressed));
    outbuffer = g_uncompressed + leafnum * g_bitbytes;
    leaf = &g_leafs[leafnum];
    tmp = 0;

    const unsigned offset = leafnum >> 3;
    const unsigned bit = (1 << (leafnum & 7));

    for (i = 0; i < leaf->numportals; i++)
    {
        p = leaf->portals[i];
        if (p->status != stat_done)
        {
            Error("portal not done (leaf %d)", leafnum);
        }

        {
            byte* dst = outbuffer;
            byte* src = p->visbits;
            for (j=0; j<g_bitbytes; j++, dst++, src++)
            {
                *dst |= *src;
            }
        }

        if ((tmp == 0) && (outbuffer[offset] & bit))
        {
            tmp = 1;
            Warning("Leaf portals saw into leaf");
            Log("    Problem at portal between leaves %i and %i:\n   ", leafnum, p->leaf);
            for (k = 0; k < p->winding->numpoints; k++)
            {
                Log("    (%4.3f %4.3f %4.3f)\n", p->winding->points[k][0], p->winding->points[k][1], p->winding->points[k][2]);
            }
            Log("\n");
        }
    }

    outbuffer[offset] |= bit;

	if (g_leafinfos[leafnum].isoverviewpoint)
	{
		for (i = 0; i < g_portalleafs; i++)
		{
			outbuffer[i >> 3] |= (1 << (i & 7));
		}
	}
	for (i = 0; i < g_portalleafs; i++)
	{
		if (g_leafinfos[i].isskyboxpoint)
		{
			outbuffer[i >> 3] |= (1 << (i & 7));
		}
	}

    numvis = 0;
    for (i = 0; i < g_portalleafs; i++)
    {
        if (outbuffer[i >> 3] & (1 << (i & 7)))
        {
            numvis++;
        }
    }

    //
    // compress the bit string
    //
    totalvis += numvis;

	byte buffer2[MAX_MAP_LEAFS / 8];
	int diskbytes = (g_leafcount_all + 7) >> 3;
	memset (buffer2, 0, diskbytes);
	for (i = 0; i < g_portalleafs; i++)
	{
		for (j = 0; j < g_leafcounts[i]; j++)
		{
			int srcofs = i >> 3;
			int srcbit = 1 << (i & 7);
			int dstofs = (g_leafstarts[i] + j) >> 3;
			int dstbit = 1 << ((g_leafstarts[i] + j) & 7);
			if (outbuffer[srcofs] & srcbit)
			{
				buffer2[dstofs] |= dstbit;
			}
		}
	}
	i = CompressVis (buffer2, diskbytes, compressed, sizeof (compressed));

    dest = vismap_p;
    vismap_p += i;

    if (vismap_p > vismap_end)
    {
        Error("Vismap expansion overflow");
    }

	for (j = 0; j < g_leafcounts[leafnum]; j++)
	{
		g_dleafs[g_leafstarts[leafnum] + j + 1].visofs = dest - vismap;
	}

    memcpy(dest, compressed, i);
}

// =====================================================================================
//  CalcPortalVis
// =====================================================================================
static void     CalcPortalVis()
{
    if (g_fastvis)
    {
        int             i;

        for (i = 0; i < g_numportals * 2; i++)
        {
            g_portals[i].visbits = g_portals[i].mightsee;
            g_portals[i].status = stat_done;
        }
        return;
    }
    NamedRunThreadsOn(g_numportals * 2, g_estimate, LeafThread);
}

// AJM: MVD
// =====================================================================================
//  SaveVisData
// =====================================================================================
void		SaveVisData(const char *filename)
{
	int i;
	FILE *fp = fopen(filename, "wb");

	if(!fp)
		return;

	SafeWrite(fp, g_dvisdata, (vismap_p - g_dvisdata));

	// BUG BUG BUG!
	// Leaf offsets need to be saved too!!!!
	for(i = 0; i < g_numleafs; i++)
	{
		SafeWrite(fp, &g_dleafs[i].visofs, sizeof(int));
	}

	fclose(fp);
}


// AJM UNDONE HLVIS_MAXDIST THIS!!!!!!!!!!!!!

// AJM: MVD modified
// =====================================================================================
//  CalcVis
// =====================================================================================
static void     CalcVis()
{
    unsigned        i;
	char visdatafile[_MAX_PATH];

	safe_snprintf(visdatafile, _MAX_PATH, "%s.vdt", g_Mapname);

	// Remove this file
	unlink(visdatafile);

    NamedRunThreadsOn(g_numportals * 2, g_estimate, BasePortalVis);

    // First do a normal VIS, save to file, then redo MaxDistVis

    CalcPortalVis();

    // Add additional leaves to the uncompressed vis.
    for (i = 0; i < g_portalleafs; i++)
    {
        if (!g_leafinfos[i].additional_leaves.empty())
        {
            for (int leaf : g_leafinfos[i].additional_leaves)
            {
                LeafFlowNeighborAddLeaf(i, leaf, g_leafinfos[i].neighbor);
                leaf_flow_add_exclude.clear();
            }
        }
    }

    //
    // assemble the leaf vis lists by oring and compressing the portal lists
    //
    for (i = 0; i < g_portalleafs; i++)
    {
        LeafFlow(i);
    }

    Log("average leafs visible: %i\n", totalvis / g_portalleafs);

    if(g_maxdistance)
    {
        totalvis = 0;
        
        Log("saving visdata to %s...\n", visdatafile);
        SaveVisData(visdatafile);

        // We need to reset the uncompressed variable and portal visbits
        free(g_uncompressed);
        g_uncompressed = (byte*)calloc(g_portalleafs, g_bitbytes);

        vismap_p = g_dvisdata;

        // We don't need to run BasePortalVis again			
        NamedRunThreadsOn(g_portalleafs, g_estimate, MaxDistVis);

        // No need to run this - MaxDistVis now writes directly to visbits after the initial VIS
        //CalcPortalVis();
    
        for (i = 0; i < g_portalleafs; i++)
        {
            LeafFlow(i);
        }
        Log("average maxdistance leafs visible: %i\n", totalvis / g_portalleafs);
    }
}


// =====================================================================================
//  CheckNullToken
// =====================================================================================
static INLINE void FASTCALL CheckNullToken(const char*const token)
{
    if (token == NULL) 
    {
        Error("LoadPortals: Damaged or invalid .prt file\n");
    }
}

// =====================================================================================
//  LoadPortals
// =====================================================================================
static void     LoadPortals(char* portal_image)
{
    int             i, j;
    portal_t*       p;
    leaf_t*         l;
    int             numpoints;
    winding_t*      w;
    int             leafnums[2];
    plane_t         plane;
    const char* const seperators = " ()\r\n\t";
    char*           token;

    token = strtok(portal_image, seperators);
    CheckNullToken(token);
    if (!sscanf(token, "%u", &g_portalleafs))
    {
        Error("LoadPortals: failed to read header: number of leafs");
    }

    token = strtok(NULL, seperators);
    CheckNullToken(token);
    if (!sscanf(token, "%i", &g_numportals))
    {
        Error("LoadPortals: failed to read header: number of portals");
    }
    
    Log("%4i portalleafs\n", g_portalleafs);
    Log("%4i numportals\n", g_numportals);

    g_bitbytes = ((g_portalleafs + 63) & ~63) >> 3;
    g_bitlongs = g_bitbytes / sizeof(long);

    // each file portal is split into two memory portals
    g_portals = (portal_t*)calloc(2 * g_numportals, sizeof(portal_t));
    g_leafs = (leaf_t*)calloc(g_portalleafs, sizeof(leaf_t));
	g_leafinfos = (leafinfo_t*)calloc(g_portalleafs, sizeof(leafinfo_t));
	g_leafcounts = (int*)calloc(g_portalleafs, sizeof(int));
	g_leafstarts = (int*)calloc(g_portalleafs, sizeof(int));

    originalvismapsize = g_portalleafs * ((g_portalleafs + 7) / 8);

    vismap = vismap_p = g_dvisdata;
    vismap_end = vismap + MAX_MAP_VISIBILITY;

	if (g_portalleafs > MAX_MAP_LEAFS)
	{ // this may cause hlvis to overflow, because numportalleafs can be larger than g_numleafs in some special cases
		Error ("Too many portalleafs (g_portalleafs(%d) > MAX_MAP_LEAFS(%d)).", g_portalleafs, MAX_MAP_LEAFS);
	}
	g_leafcount_all = 0;
	for (i = 0; i < g_portalleafs; i++)
	{
		unsigned rval = 0;
		token = strtok(NULL, seperators);
		CheckNullToken(token);
		rval += sscanf(token, "%i", &g_leafcounts[i]);
		if (rval != 1)
		{
			Error("LoadPortals: read leaf %i failed", i);
		}
		g_leafstarts[i] = g_leafcount_all;
		g_leafcount_all += g_leafcounts[i];
	}
	if (g_leafcount_all != g_dmodels[0].visleafs)
	{ // internal error (this should never happen)
		Error ("Corrupted leaf mapping (g_leafcount_all(%d) != g_dmodels[0].visleafs(%d)).", g_leafcount_all, g_dmodels[0].visleafs);
	}
	for (i = 0; i < g_portalleafs; i++)
	{
		for (j = 0; j < g_overview_count; j++)
		{
			int d = g_overview[j].visleafnum - g_leafstarts[i];
			if (0 <= d && d < g_leafcounts[i])
			{
				if (g_overview[j].reverse)
				{
					g_leafinfos[i].isskyboxpoint = true;
				}
				else
				{
					g_leafinfos[i].isoverviewpoint = true;
				}
			}
		}

        for (j = 0; j < g_room_count; j++)
        {
            int d1 = g_room[j].visleafnum - g_leafstarts[i];

            if (0 <= d1 && d1 < g_leafcounts[i])
            {
                for (int k = 0; k < g_portalleafs; k++)
                {
                    int d2 = g_room[j].target_visleafnum - g_leafstarts[k];

                    if (0 <= d2 && d2 < g_leafcounts[k])
                    {
                        g_leafinfos[i].additional_leaves.push_back(k);
                        g_leafinfos[i].neighbor = g_room[j].neighbor;
                    }
                }
            }
        }
	}
    for (i = 0, p = g_portals; i < g_numportals; i++)
    {
        unsigned rval = 0;

        token = strtok(NULL, seperators);
        CheckNullToken(token);
        rval += sscanf(token, "%i", &numpoints);
        token = strtok(NULL, seperators);
        CheckNullToken(token);
        rval += sscanf(token, "%i", &leafnums[0]);
        token = strtok(NULL, seperators);
        CheckNullToken(token);
        rval += sscanf(token, "%i", &leafnums[1]);

        if (rval != 3)
        {
            Error("LoadPortals: reading portal %i", i);
        }
        if (numpoints > MAX_POINTS_ON_WINDING)
        {
            Error("LoadPortals: portal %i has too many points", i);
        }
        if (((unsigned)leafnums[0] > g_portalleafs) || ((unsigned)leafnums[1] > g_portalleafs))
        {
            Error("LoadPortals: reading portal %i", i);
        }

        w = p->winding = NewWinding(numpoints);
        w->original = true;
        w->numpoints = numpoints;

        for (j = 0; j < numpoints; j++)
        {
            int             k;
            double          v[3];
            unsigned        rval = 0;

            token = strtok(NULL, seperators);
            CheckNullToken(token);
            rval += sscanf(token, "%lf", &v[0]);
            token = strtok(NULL, seperators);
            CheckNullToken(token);
            rval += sscanf(token, "%lf", &v[1]);
            token = strtok(NULL, seperators);
            CheckNullToken(token);
            rval += sscanf(token, "%lf", &v[2]);

            // scanf into double, then assign to vec_t
            if (rval != 3)
            {
                Error("LoadPortals: reading portal %i", i);
            }
            for (k = 0; k < 3; k++)
            {
                w->points[j][k] = v[k];
            }
        }

        // calc plane
        PlaneFromWinding(w, &plane);

        // create forward portal
        l = &g_leafs[leafnums[0]];
        hlassume(l->numportals < MAX_PORTALS_ON_LEAF, assume_MAX_PORTALS_ON_LEAF);
        l->portals[l->numportals] = p;
        l->numportals++;

        p->winding = w;
        VectorSubtract(vec3_origin, plane.normal, p->plane.normal);
        p->plane.dist = -plane.dist;
        p->leaf = leafnums[1];
        p++;

        // create backwards portal
        l = &g_leafs[leafnums[1]];
        hlassume(l->numportals < MAX_PORTALS_ON_LEAF, assume_MAX_PORTALS_ON_LEAF);
        l->portals[l->numportals] = p;
        l->numportals++;

        p->winding = NewWinding(w->numpoints);
        p->winding->numpoints = w->numpoints;
        for (j = 0; j < w->numpoints; j++)
        {
            VectorCopy(w->points[w->numpoints - 1 - j], p->winding->points[j]);
        }

        p->plane = plane;
        p->leaf = leafnums[0];
        p++;

    }
}

// =====================================================================================
//  LoadPortalsByFilename
// =====================================================================================
static void     LoadPortalsByFilename(const char* const filename)
{
    char* file_image;

    if (!q_exists(filename))
    {
        Error("Portal file '%s' does not exist, cannot vis the map\n", filename);
    }
    LoadFile(filename, &file_image);
    LoadPortals(file_image);
    free(file_image);
}

// =====================================================================================
//  Settings
// =====================================================================================
static void     Settings()
{
    char*           tmp;

    Log("\n-= Current %s Settings =-\n", g_Program);
    Log("Name               |  Setting  |  Default\n" "-------------------|-----------|-------------------------\n");
    // ZHLT Common Settings
    if (DEFAULT_NUMTHREADS == -1)
    {
        Log("threads             [ %7d ] [  Varies ]\n", g_numthreads);
    }
    else
    {
        Log("threads             [ %7d ] [ %7d ]\n", g_numthreads, DEFAULT_NUMTHREADS);
    }
    Log("max texture memory  [ %7d ] [ %7d ]\n", g_max_map_miptex, DEFAULT_MAX_MAP_MIPTEX);
    Log("max vis distance    [ %7d ] [ %7d ]\n", g_maxdistance, DEFAULT_MAXDISTANCE_RANGE);
    switch (g_threadpriority)
    {
    case eThreadPriorityNormal:
    default:
        tmp = "Normal";
        break;
    case eThreadPriorityLow:
        tmp = "Low";
        break;
    case eThreadPriorityHigh:
        tmp = "High";
        break;
    }
    Log("priority            [ %7s ] [ %7s ]\n", tmp, "Normal");
    // HLVIS Specific Settings
    Log("fast vis            [ %7s ] [ %7s ]\n", g_fastvis ? "on" : "off", DEFAULT_FASTVIS ? "on" : "off");
    Log("full vis            [ %7s ] [ %7s ]\n", g_fullvis ? "on" : "off", DEFAULT_FULLVIS ? "on" : "off");
    Log("\n\n");
}

int        VisLeafnumForPoint(const vec3_t point)
{
    int             nodenum;
    vec_t           dist;
    dnode_t*        node;
    dplane_t*       plane;

    nodenum = 0;
    while (nodenum >= 0)
    {
        node = &g_dnodes[nodenum];
        plane = &g_dplanes[node->planenum];
        dist = DotProduct(point, plane->normal) - plane->dist;
        if (dist >= 0.0)
        {
            nodenum = node->children[0];
        }
        else
        {
            nodenum = node->children[1];
        }
    }

    return -nodenum - 2;
}

// =====================================================================================
//  main
// =====================================================================================
int             main(const int argc, char** argv)
{
    char            portalfile[_MAX_PATH];
    char            source[_MAX_PATH];
    int             i;
    double          start, end;
    const char*     mapname_from_arg = NULL;
    g_Program = "sdHLVIS";

	int argcold = argc;
	char ** argvold = argv;
	{
		int argc;
		char ** argv;
		ParseParamFile (argcold, argvold, argc, argv);
        if (InitConsole (argc, argv) < 0)
            Usage(PROGRAM_VIS);
        if (argc == 1)
        {
            Usage(PROGRAM_VIS);
        }
        for (i = 1; i < argc; i++)
        {
            if (!strcasecmp(argv[i], "-threads"))
            {
                if (i + 1 < argc)	//added "1" .--vluzacn
                {
                    g_numthreads = atoi(argv[++i]);
                    if (g_numthreads < 1)
                    {
                        Log("Expected value of at least 1 for '-threads'\n");
                        Usage(PROGRAM_VIS);
                    }
                }
                else
                {
                    Usage(PROGRAM_VIS);
                }
            }
            else if (!strcasecmp(argv[i], "-console"))
            {
    #ifndef SYSTEM_WIN32
                Warning("The option '-console #' is only valid for Windows.");
    #endif
                if (i + 1 < argc)
                    ++i;
                else
                    Usage(PROGRAM_VIS);
            }
            else if (!strcasecmp(argv[i], "-fast"))
            {
                Log("g_fastvis = true\n");
                g_fastvis = true;
            }
            else if (!strcasecmp(argv[i], "-full"))
            {
                g_fullvis = true;
            }
            else if (!strcasecmp(argv[i], "-low"))
            {
                g_threadpriority = eThreadPriorityLow;
            }
            else if (!strcasecmp(argv[i], "-high"))
            {
                g_threadpriority = eThreadPriorityHigh;
            }
            else if (!strcasecmp(argv[i], "-texdata"))
            {
                if (i + 1 < argc)	//added "1" .--vluzacn
                {
                    int             x = atoi(argv[++i]) * 1024;

                    //if (x > g_max_map_miptex) //--vluzacn
                    {
                        g_max_map_miptex = x;
                    }
                }
                else
                {
                    Usage(PROGRAM_VIS);
                }
            }
            else if (!strcasecmp(argv[i], "-lightdata")) //lightdata
            {
                if (i + 1 < argc) //--vluzacn
                {
                    int             x = atoi(argv[++i]) * 1024;

                    //if (x > g_max_map_lightdata) //--vluzacn
                    {
                        g_max_map_lightdata = x; //--vluzacn
                    }
                }
                else
                {
                    Usage(PROGRAM_VIS);
                }
            }
            // AJM: MVD
            else if(!strcasecmp(argv[i], "-maxdistance"))
            {
                if(i + 1 < argc)	//added "1" .--vluzacn
                {
                    g_maxdistance = abs(atoi(argv[++i]));
                }
                else
                {
                    Usage(PROGRAM_VIS);
                }
            }
            else if (argv[i][0] == '-')
            {
                Log("Unknown option \"%s\"", argv[i]);
                Usage(PROGRAM_VIS);
            }
            else if (!mapname_from_arg)
            {
                mapname_from_arg = argv[i];
            }
            else
            {
                Log("Unknown option \"%s\"\n", argv[i]);
                Usage(PROGRAM_VIS);
            }
        }

        if (!mapname_from_arg)
        {
            Log("No mapfile specified\n");
            Usage(PROGRAM_VIS);
        }

        safe_strncpy(g_Mapname, mapname_from_arg, _MAX_PATH);
        FlipSlashes(g_Mapname);
        StripExtension(g_Mapname);
        atexit(CloseLog);
        ThreadSetDefault();
        ThreadSetPriority(g_threadpriority);
        LogStart(argcold, argvold);
        LogArguments(argc, argv);
        CheckForErrorLog();
        
    #ifdef PLATFORM_CAN_CALC_EXTENT
        hlassume (CalcFaceExtents_test (), assume_first);
    #endif
        dtexdata_init();
        atexit(dtexdata_free);
        // END INIT

        // BEGIN VIS
        start = I_FloatTime();

        safe_strncpy(source, g_Mapname, _MAX_PATH);
        safe_strncat(source, ".bsp", _MAX_PATH);
        safe_strncpy(portalfile, g_Mapname, _MAX_PATH);
        safe_strncat(portalfile, ".prt", _MAX_PATH);
        LoadBSPFile(source);
        ParseEntities();
        int i;
        for (i = 0; i < g_numentities; i++)
        {
            const char* current_entity_classname = ValueForKey (&g_entities[i], "classname");

            if (!strcmp (current_entity_classname, "info_overview_point")
                )
            {
                if (g_overview_count < g_overview_max)
                {
                    vec3_t p;
                    GetVectorForKey (&g_entities[i], "origin", p);
                    VectorCopy (p, g_overview[g_overview_count].origin);
                    g_overview[g_overview_count].visleafnum = VisLeafnumForPoint (p);
                    g_overview[g_overview_count].reverse = IntForKey (&g_entities[i], "reverse");
                    g_overview_count++;
                }
            }

            else if (!strcmp (current_entity_classname, "info_portal"))
            {
                if (g_room_count < g_room_max)
                {
                    vec3_t room_origin;

                    GetVectorForKey (&g_entities[i], "origin", room_origin);
                    g_room[g_room_count].visleafnum = VisLeafnumForPoint (room_origin);
                    g_room[g_room_count].neighbor = CLAMP(IntForKey (&g_entities[i], "neighbor"), 0, MAX_ROOM_NEIGHBOR);

                    const char* target = ValueForKey (&g_entities[i], "target");

                    if (strlen(target) == 0)
                    {
                        continue;
                    }

                    bool has_target = false;

                    // Find the target entity.
                    // Rewalk yes, very sad.
                    for (int j = 0; j < g_numentities; j++)
                    {
                        const char* current_entity_classname_nested = ValueForKey (&g_entities[j], "classname");

                        // Find a `info_leaf` and check if its targetname matches our target
                        if (!strcmp (current_entity_classname_nested, "info_leaf")
                            && !strcmp(ValueForKey (&g_entities[j], "targetname"), target))
                        {
                            vec3_t room_target_origin;

                            GetVectorForKey (&g_entities[j], "origin", room_target_origin);
                            g_room[g_room_count].target_visleafnum = VisLeafnumForPoint (room_target_origin);

                            has_target = true;
                        }
                    }

                    if (!has_target)
                    {
                        Warning("Entity %d (info_portal) does not have a target leaf.", i);
                    }
                    g_room_count++;
                }
            }
        }
        LoadPortalsByFilename(portalfile);
        Settings();
        g_uncompressed = (byte*)calloc(g_portalleafs, g_bitbytes);

        CalcVis();
        g_visdatasize = vismap_p - g_dvisdata;
        Log("g_visdatasize:%i  compressed from %i\n", g_visdatasize, originalvismapsize);
        PrintBSPFileSizes();
        WriteBSPFile(source);

        end = I_FloatTime();
        LogTimeElapsed(end - start);

        free(g_uncompressed);
        // END VIS
    }
    return 0;
}