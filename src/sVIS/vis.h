#pragma once

#include "mathlib.h"
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

struct WindingVIS
{
    bool original; // don't free, it's part of the portal
    int numpoints;
    vec3_t points[MAX_POINTS_ON_FIXED_WINDING];
};

struct PlaneVIS
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

struct PortalVIS
{
    PlaneVIS plane; // normal pointing into neighbor
    int leaf;       // neighbor
    WindingVIS *winding;
    vstatus_t status;
    byte *visbits;
    byte *mightsee;
    unsigned nummightsee;
    int numcansee;
};

struct SepVIS
{
    struct SepVIS *next;
    PlaneVIS plane; // from portal is on positive side
};

struct PassageVIS
{
    struct PassageVIS *next;
    int from, to; // leaf numbers
    SepVIS *planes;
};

constexpr int MAX_PORTALS_ON_LEAF = 256;
struct LeafVIS
{
    unsigned numportals;
    PassageVIS *passages;
    PortalVIS *portals[MAX_PORTALS_ON_LEAF];
};

struct pstackVIS
{
    byte mightsee[MAX_MAP_LEAFS / 8]; // bit string
#ifdef USE_CHECK_STACK
    struct pstackVIS *next;
#endif
    struct pstackVIS *head;

    LeafVIS *leaf;
    PortalVIS *portal; // portal exiting
    WindingVIS *source;
    WindingVIS *pass;

    WindingVIS windings[3]; // source, pass, temp in any order
    char freewindings[3];

    const PlaneVIS *portalplane;

#ifdef RVIS_LEVEL_2
    int clipPlaneCount;
    PlaneVIS *clipPlane;
#endif
};

struct ThreadDataVIS
{
    byte *leafvis; // bit string
    //      byte            fullportal[MAX_PORTALS/8];              // bit string
    PortalVIS *base;
    pstackVIS pstack_head;
};

extern bool g_fastvis;
extern bool g_fullvis;

extern int g_numportals;
extern unsigned g_portalleafs;

extern unsigned int g_maxdistance;
// extern bool		g_postcompile;

struct OverviewVIS
{
    vec3_t origin;
    int visleafnum;
    int reverse;
};
extern const int g_overview_max;
extern OverviewVIS g_overview[];
extern int g_overview_count;

struct LeafInfoVIS
{
    bool isoverviewpoint;
    bool isskyboxpoint;
};
extern LeafInfoVIS *g_leafinfos;

extern PortalVIS *g_portals;
extern LeafVIS *g_leafs;

extern byte *g_uncompressed;
extern unsigned g_bitbytes;
extern unsigned g_bitlongs;

extern volatile int g_vislocalpercent;

extern void BasePortalVis(int threadnum);

extern void MaxDistVis(int threadnum);
// extern void		PostMaxDistVis(int threadnum);

extern void PortalFlow(PortalVIS *p);
extern void CalcAmbientSounds();
void HandleArgs(int argc, char **argv, const char *&mapname_from_arg);