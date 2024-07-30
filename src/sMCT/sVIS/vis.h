#pragma once

#include <thread>

#include "cmdlib.h"
#include "messages.h"
#include "win32fix.h"
#include "log.h"
#include "hlassert.h"
#include "mathlib.h"
#include "bspfile.h"
#include "threads.h"
#include "filelib.h"
#include "winding.h"

constexpr int DEFAULT_MAXDISTANCE_RANGE = 0;

constexpr bool DEFAULT_FULLVIS = false;
constexpr bool DEFAULT_INFO = true;
constexpr bool DEFAULT_ESTIMATE = true;
constexpr bool DEFAULT_FASTVIS = false;

constexpr int MAX_PORTALS = 32768;

// #define USE_CHECK_STACK
#define RVIS_LEVEL_1
#define RVIS_LEVEL_2

#define PORTALFILE "PRT1" // WTF?

constexpr int MAX_POINTS_ON_FIXED_WINDING = 32;

struct winding_t
{
    bool original; // don't free, it's part of the portal
    int numpoints;
    vec3_t points[MAX_POINTS_ON_FIXED_WINDING];
};

struct plane_t
{
    vec3_t normal;
    float dist;
};

typedef enum
{
    stat_none,
    stat_working,
    stat_done
} vstatus_t;

struct portal_t
{
    plane_t plane; // normal pointing into neighbor
    int leaf;      // neighbor
    winding_t *winding;
    vstatus_t status;
    byte *visbits;
    byte *mightsee;
    unsigned nummightsee;
    int numcansee;
};

struct sep_t
{
    struct sep_t *next;
    plane_t plane; // from portal is on positive side
};

struct passage_t
{
    struct passage_t *next;
    int from, to; // leaf numbers
    sep_t *planes;
};

constexpr int MAX_PORTALS_ON_LEAF = 256;
struct leaf_t
{
    unsigned numportals;
    passage_t *passages;
    portal_t *portals[MAX_PORTALS_ON_LEAF];
};

struct pstack_t
{
    byte mightsee[MAX_MAP_LEAFS / 8]; // bit string
#ifdef USE_CHECK_STACK
    struct pstack_t *next;
#endif
    struct pstack_t *head;

    leaf_t *leaf;
    portal_t *portal; // portal exiting
    winding_t *source;
    winding_t *pass;

    winding_t windings[3]; // source, pass, temp in any order
    char freewindings[3];

    const plane_t *portalplane;

#ifdef RVIS_LEVEL_2
    int clipPlaneCount;
    plane_t *clipPlane;
#endif
};

struct threaddata_t
{
    byte *leafvis; // bit string
    //      byte            fullportal[MAX_PORTALS/8];              // bit string
    portal_t *base;
    pstack_t pstack_head;
};

extern bool g_fastvis;
extern bool g_fullvis;

extern int g_numportals;
extern unsigned g_portalleafs;

extern unsigned int g_maxdistance;
// extern bool		g_postcompile;

struct overview_t
{
    vec3_t origin;
    int visleafnum;
    int reverse;
};
extern const int g_overview_max;
extern overview_t g_overview[];
extern int g_overview_count;

struct leafinfo_t
{
    bool isoverviewpoint;
    bool isskyboxpoint;
};
extern leafinfo_t *g_leafinfos;

extern portal_t *g_portals;
extern leaf_t *g_leafs;

extern byte *g_uncompressed;
extern unsigned g_bitbytes;
extern unsigned g_bitlongs;

extern volatile int g_vislocalpercent;

extern void BasePortalVis(int threadnum);

extern void MaxDistVis(int threadnum);
// extern void		PostMaxDistVis(int threadnum);

extern void PortalFlow(portal_t *p);
extern void CalcAmbientSounds();
void HandleArgs(int argc, char **argv, const char *&mapname_from_arg);