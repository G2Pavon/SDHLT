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

#include <algorithm>
#include <string>
#include <cstring>

#include "hlvis.h"
#include "arguments.h"
#include "threads.h"
#include "filelib.h"

/*

 NOTES

*/

int g_numportals = 0;
unsigned g_portalleafs = 0;

PortalVIS *g_portals;

LeafVIS *g_leafs;
int *g_leafstarts;
int *g_leafcounts;
int g_leafcount_all;

// AJM: MVD
//

static byte *g_vismap;
static byte *g_vismap_p;
static byte *g_vismap_end; // past visfile
static int g_originalvismapsize;

byte *g_uncompressed; // [bitbytes*portalleafs]

unsigned g_bitbytes; // (portalleafs+63)>>3
unsigned g_bitlongs;

bool g_fastvis = DEFAULT_FASTVIS;
bool g_fullvis = DEFAULT_FULLVIS;
bool g_estimate = DEFAULT_ESTIMATE;

// AJM: MVD
unsigned int g_maxdistance = DEFAULT_MAXDISTANCE_RANGE;
// bool			g_postcompile = DEFAULT_POST_COMPILE;
//
const int g_overview_max = MAX_MAP_ENTITIES;
OverviewVIS g_overview[g_overview_max];
int g_overview_count = 0;
LeafInfoVIS *g_leafinfos = nullptr;

static int g_totalvis = 0;

// =====================================================================================
//  PlaneFromWinding
// =====================================================================================
static void PlaneFromWinding(WindingVIS *w, PlaneVIS *plane)
{
    vec3_t v1;
    vec3_t v2;

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
static auto NewWinding(const int points) -> WindingVIS *
{
    if (points > MAX_POINTS_ON_WINDING)
    {
        Error("NewWinding: %i points > MAX_POINTS_ON_WINDING", points);
    }

    auto size = (int)(intptr_t)((WindingVIS *)nullptr)->points[points];
    auto *w = (WindingVIS *)calloc(1, size);

    return w;
}

// =====================================================================================
//  GetNextPortal
//      Returns the next portal for a thread to work on
//      Returns the portals from the least complex, so the later ones can reuse the earlier information.
// =====================================================================================
static auto GetNextPortal() -> PortalVIS *
{
    int j;
    PortalVIS *p;
    PortalVIS *tp;

    if (GetThreadWork() == -1)
    {
        return nullptr;
    }
    ThreadLock();

    auto min = 99999;
    p = nullptr;

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
static void LeafThread(int unused)
{
    PortalVIS *p;

    while (true)
    {
        if (!(p = GetNextPortal()))
        {
            return;
        }
        PortalFlow(p);
    }
}

// =====================================================================================
//  LeafFlow
//      Builds the entire visibility list for a leaf
// =====================================================================================
static void LeafFlow(const int leafnum)
{
    byte compressed[MAX_MAP_LEAFS / 8];
    unsigned i;
    unsigned j;

    //
    // flow through all portals, collecting visible bits
    //
    memset(compressed, 0, sizeof(compressed));
    auto *outbuffer = g_uncompressed + leafnum * g_bitbytes;
    auto *leaf = &g_leafs[leafnum];
    auto tmp = 0;

    const unsigned offset = leafnum >> 3;
    const unsigned bit = (1 << (leafnum & 7));

    for (i = 0; i < leaf->numportals; i++)
    {
        auto *p = leaf->portals[i];
        if (p->status != stat_done)
        {
            Error("portal not done (leaf %d)", leafnum);
        }

        {
            byte *dst = outbuffer;
            byte *src = p->visbits;
            for (j = 0; j < g_bitbytes; j++, dst++, src++)
            {
                *dst |= *src;
            }
        }

        if ((tmp == 0) && (outbuffer[offset] & bit))
        {
            tmp = 1;
            Warning("Leaf portals saw into leaf");
            Log("    Problem at portal between leaves %i and %i:\n   ", leafnum, p->leaf);
            for (int k = 0; k < p->winding->numpoints; k++)
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

    auto numvis = 0;
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
    g_totalvis += numvis;

    byte buffer2[MAX_MAP_LEAFS / 8];
    int diskbytes = (g_leafcount_all + 7) >> 3;
    memset(buffer2, 0, diskbytes);
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
    i = CompressVis(buffer2, diskbytes, compressed, sizeof(compressed));

    auto *dest = g_vismap_p;
    g_vismap_p += i;

    if (g_vismap_p > g_vismap_end)
    {
        Error("Vismap expansion overflow");
    }

    for (j = 0; j < g_leafcounts[leafnum]; j++)
    {
        g_bspleafs[g_leafstarts[leafnum] + j + 1].visofs = dest - g_vismap;
    }

    memcpy(dest, compressed, i);
}

// =====================================================================================
//  CalcPortalVis
// =====================================================================================
static void CalcPortalVis()
{
    if (g_fastvis)
    {
        for (int i = 0; i < g_numportals * 2; i++)
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
void SaveVisData(const char *filename)
{
    FILE *fp = fopen(filename, "wb");

    if (!fp)
        return;

    SafeWrite(fp, g_bspvisdata, (g_vismap_p - g_bspvisdata));

    // BUG BUG BUG!
    // Leaf offsets need to be saved too!!!!
    for (int i = 0; i < g_bspnumleafs; i++)
    {
        SafeWrite(fp, &g_bspleafs[i].visofs, sizeof(int));
    }

    fclose(fp);
}

// AJM UNDONE HLVIS_MAXDIST THIS!!!!!!!!!!!!!

// AJM: MVD modified
// =====================================================================================
//  CalcVis
// =====================================================================================
static void CalcVis()
{
    unsigned i;
    char visdatafile[_MAX_PATH];

    safe_snprintf(visdatafile, _MAX_PATH, "%s.vdt", g_Mapname);

    // Remove this file
    unlink(visdatafile);

    NamedRunThreadsOn(g_numportals * 2, g_estimate, BasePortalVis);

    // First do a normal VIS, save to file, then redo MaxDistVis

    CalcPortalVis();
    //
    // assemble the leaf vis lists by oring and compressing the portal lists
    //
    for (i = 0; i < g_portalleafs; i++)
    {
        LeafFlow(i);
    }

    Log("average leafs visible: %i\n", g_totalvis / g_portalleafs);

    if (g_maxdistance)
    {
        g_totalvis = 0;

        Log("saving visdata to %s...\n", visdatafile);
        SaveVisData(visdatafile);

        // We need to reset the uncompressed variable and portal visbits
        free(g_uncompressed);
        g_uncompressed = (byte *)calloc(g_portalleafs, g_bitbytes);

        g_vismap_p = g_bspvisdata;

        // We don't need to run BasePortalVis again
        NamedRunThreadsOn(g_portalleafs, g_estimate, MaxDistVis);

        // No need to run this - MaxDistVis now writes directly to visbits after the initial VIS
        // CalcPortalVis();

        for (i = 0; i < g_portalleafs; i++)
        {
            LeafFlow(i);
        }
        Log("average maxdistance leafs visible: %i\n", g_totalvis / g_portalleafs);
    }
}

// =====================================================================================
//  CheckNullToken
// =====================================================================================
static inline void CheckNullToken(const char *const token)
{
    if (token == nullptr)
    {
        Error("LoadPortals: Damaged or invalid .prt file\n");
    }
}

// =====================================================================================
//  LoadPortals
// =====================================================================================
static void LoadPortals(char *portal_image)
{
    int i, j;
    PortalVIS *p;
    int numpoints;
    int leafnums[2];
    PlaneVIS plane;
    const char *const seperators = " ()\r\n\t";

    auto *token = strtok(portal_image, seperators);
    CheckNullToken(token);
    if (!sscanf(token, "%u", &g_portalleafs))
    {
        Error("LoadPortals: failed to read header: number of leafs");
    }

    token = strtok(nullptr, seperators);
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
    g_portals = (PortalVIS *)calloc(2 * g_numportals, sizeof(PortalVIS));
    g_leafs = (LeafVIS *)calloc(g_portalleafs, sizeof(LeafVIS));
    g_leafinfos = (LeafInfoVIS *)calloc(g_portalleafs, sizeof(LeafInfoVIS));
    g_leafcounts = (int *)calloc(g_portalleafs, sizeof(int));
    g_leafstarts = (int *)calloc(g_portalleafs, sizeof(int));

    g_originalvismapsize = g_portalleafs * ((g_portalleafs + 7) / 8);

    g_vismap = g_vismap_p = g_bspvisdata;
    g_vismap_end = g_vismap + MAX_MAP_VISIBILITY;

    if (g_portalleafs > MAX_MAP_LEAFS)
    { // this may cause hlvis to overflow, because numportalleafs can be larger than g_bspnumleafs in some special cases
        Error("Too many portalleafs (g_portalleafs(%d) > MAX_MAP_LEAFS(%d)).", g_portalleafs, MAX_MAP_LEAFS);
    }
    g_leafcount_all = 0;
    for (i = 0; i < g_portalleafs; i++)
    {
        unsigned rval = 0;
        token = strtok(nullptr, seperators);
        CheckNullToken(token);
        rval += sscanf(token, "%i", &g_leafcounts[i]);
        if (rval != 1)
        {
            Error("LoadPortals: read leaf %i failed", i);
        }
        g_leafstarts[i] = g_leafcount_all;
        g_leafcount_all += g_leafcounts[i];
    }
    if (g_leafcount_all != g_bspmodels[0].visleafs)
    { // internal error (this should never happen)
        Error("Corrupted leaf mapping (g_leafcount_all(%d) != g_bspmodels[0].visleafs(%d)).", g_leafcount_all, g_bspmodels[0].visleafs);
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
    }
    for (i = 0, p = g_portals; i < g_numportals; i++)
    {
        unsigned rval = 0;

        token = strtok(nullptr, seperators);
        CheckNullToken(token);
        rval += sscanf(token, "%i", &numpoints);
        token = strtok(nullptr, seperators);
        CheckNullToken(token);
        rval += sscanf(token, "%i", &leafnums[0]);
        token = strtok(nullptr, seperators);
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

        auto *w = p->winding = NewWinding(numpoints);
        w->original = true;
        w->numpoints = numpoints;

        for (j = 0; j < numpoints; j++)
        {
            int k;
            double v[3];
            unsigned rval = 0;

            token = strtok(nullptr, seperators);
            CheckNullToken(token);
            rval += sscanf(token, "%lf", &v[0]);
            token = strtok(nullptr, seperators);
            CheckNullToken(token);
            rval += sscanf(token, "%lf", &v[1]);
            token = strtok(nullptr, seperators);
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
        auto *l = &g_leafs[leafnums[0]];
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
static void LoadPortalsByFilename(const char *const filename)
{
    char *file_image;

    if (!q_exists(filename))
    {
        Error("Portal file '%s' does not exist, cannot vis the map\n", filename);
    }
    LoadFile(filename, &file_image);
    LoadPortals(file_image);
    delete file_image;
}

auto VisLeafnumForPoint(const vec3_t point) -> int
{
    auto nodenum = 0;
    while (nodenum >= 0)
    {
        auto *node = &g_bspnodes[nodenum];
        auto *plane = &g_bspplanes[node->planenum];
        auto dist = DotProduct(point, plane->normal) - plane->dist;
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

void HandleArgs(int argc, char **argv, const char *&mapname_from_arg)
{
    for (int i = 1; i < argc; i++)
    {
        if (!strcasecmp(argv[i], "-fast"))
        {
            Log("g_fastvis = true\n");
            g_fastvis = true;
        }
        else if (!strcasecmp(argv[i], "-full"))
        {
            g_fullvis = true;
        }
        // AJM: MVD
        else if (!strcasecmp(argv[i], "-maxdistance"))
        {
            if (i + 1 < argc) // added "1" .--vluzacn
            {
                g_maxdistance = abs(atoi(argv[++i]));
            }
            else
            {
                Usage(ProgramType::PROGRAM_VIS);
            }
        }
        else if (!mapname_from_arg)
        {
            mapname_from_arg = argv[i];
        }
        else
        {
            Log("Unknown option \"%s\"\n", argv[i]);
            Usage(ProgramType::PROGRAM_VIS);
        }
    }

    if (!mapname_from_arg)
    {
        Log("No mapfile specified\n");
        Usage(ProgramType::PROGRAM_VIS);
    }
}

// =====================================================================================
//  main
// =====================================================================================
auto main(const int argc, char **argv) -> int
{
    char portalfile[_MAX_PATH];
    char source[_MAX_PATH];
    double start, end;
    const char *mapname_from_arg = nullptr;
    g_Program = "sVIS";

    if (InitConsole(argc, argv) < 0)
        Usage(ProgramType::PROGRAM_VIS);
    if (argc == 1)
    {
        Usage(ProgramType::PROGRAM_VIS);
    }
    HandleArgs(argc, argv, mapname_from_arg);

    safe_strncpy(g_Mapname, mapname_from_arg, _MAX_PATH);
    FlipSlashes(g_Mapname);
    StripExtension(g_Mapname);

    atexit(CloseLog);
    ThreadSetDefault();
    ThreadSetPriority(g_threadpriority);
    LogArguments(argc, argv);
    CheckForErrorLog();
    hlassume(CalcFaceExtents_test(), assume_first);
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
        const char *current_entity_classname = ValueForKey(&g_entities[i], "classname");

        if (!strcmp(current_entity_classname, "info_overview_point"))
        {
            if (g_overview_count < g_overview_max)
            {
                vec3_t p;
                GetVectorForKey(&g_entities[i], "origin", p);
                VectorCopy(p, g_overview[g_overview_count].origin);
                g_overview[g_overview_count].visleafnum = VisLeafnumForPoint(p);
                g_overview[g_overview_count].reverse = IntForKey(&g_entities[i], "reverse");
                g_overview_count++;
            }
        }
    }
    LoadPortalsByFilename(portalfile);
    g_uncompressed = (byte *)calloc(g_portalleafs, g_bitbytes);

    CalcVis();
    g_bspvisdatasize = g_vismap_p - g_bspvisdata;
    Log("g_bspvisdatasize:%i  compressed from %i\n", g_bspvisdatasize, g_originalvismapsize);
    WriteBSPFile(source);

    end = I_FloatTime();
    LogTimeElapsed(end - start);

    free(g_uncompressed);
    // END VIS
    return 0;
}