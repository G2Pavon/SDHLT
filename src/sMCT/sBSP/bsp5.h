#pragma once

#include "cmdlib.h"
#include "messages.h"
#include "win32fix.h"
#include "log.h"
#include "hlassert.h"
#include "mathlib.h"
#include "bspfile.h"
#include "blockmem.h"
#include "filelib.h"
#include "threads.h"
#include "winding.h"

constexpr int BOGUS_RANGE = 144000;

// the exact bounding box of the brushes is expanded some for the headnode
// volume.  is this still needed?
constexpr int SIDESPACE = 24;
constexpr int MIN_SUBDIVIDE_SIZE = 64;
constexpr int MAX_SUBDIVIDE_SIZE = 512;
constexpr int DEFAULT_SUBDIVIDE_SIZE = ((MAX_SURFACE_EXTENT - 1) * TEXTURE_STEP);

constexpr int MIN_MAXNODE_SIZE = 64;
constexpr int MAX_MAXNODE_SIZE = 65536;
constexpr int DEFAULT_MAXNODE_SIZE = 1024;

constexpr bool DEFAULT_LEAKONLY = false;
constexpr bool DEFAULT_WATERVIS = false;
constexpr bool DEFAULT_INFO = true;

constexpr bool DEFAULT_ESTIMATE = true;

constexpr int MAXEDGES = 48;
constexpr int MAXPOINTS = 28;     // don't let a base face get past this
                                  // because it can be split more later
constexpr int MAXNODESIZE = 1024; // Valve default is 1024

typedef enum
{
    face_normal = 0,
    face_hint,
    face_skip,
    face_null,
    face_discardable, // contents must not differ between front and back
} facestyle_e;

struct face_t // This structure is layed out so 'pts' is on a quad-word boundary (and the pointers are as well)
{
    struct face_t *next;
    int planenum;
    int texturenum;
    int contents;     // contents in front of face
    int detaillevel;  // defined by hlcsg
    int *outputedges; // used in WriteDrawNodes

    struct face_t *original; // face on node
    int outputnumber;        // only valid for original faces after write surfaces
    int numpoints;
    facestyle_e facestyle;
    int referenced; // only valid for original faces

    // vector quad word aligned
    vec3_t pts[MAXEDGES]; // FIXME: change to use winding_t
};

struct surface_t
{
    struct surface_t *next;
    int planenum;
    vec3_t mins, maxs;
    struct node_t *onnode; // true if surface has already been used
    // as a splitting node
    face_t *faces;   // links to all the faces on either side of the surf
    int detaillevel; // minimum detail level of its faces
};

struct surfchain_t
{
    vec3_t mins, maxs;
    surface_t *surfaces;
};

struct side_t
{
    struct side_s *next;
    dplane_t plane; // facing inside (reversed when loading brush file)
    Winding *w;     // (also reversed)
};

struct brush_t
{
    struct brush_t *next;
    side_t *sides;
};

//
// there is a node_t structure for every node and leaf in the bsp tree
//
constexpr int PLANENUM_LEAF = -1;
constexpr float BOUNDS_EXPANSION = 1.0f; // expand the bounds of detail leafs when clipping its boundsbrush, to prevent some strange brushes in the func_detail from clipping away the entire boundsbrush making the func_detail invisible.

struct node_t
{
    surface_t *surfaces;
    brush_t *detailbrushes;
    brush_t *boundsbrush;
    vec3_t loosemins, loosemaxs; // all leafs and nodes have this, while 'mins' and 'maxs' are only valid for nondetail leafs and nodes.

    bool isdetail;         // is under a diskleaf
    bool isportalleaf;     // not detail and children are detail; only visleafs have contents, portals, mins, maxs
    bool iscontentsdetail; // inside a detail brush
    vec3_t mins, maxs;     // bounding volume of portals;

    // information for decision nodes
    int planenum;               // -1 = leaf node
    struct node_t *children[2]; // only valid for decision nodes
    face_t *faces;              // decision nodes only, list for both sides

    // information for leafs
    int contents;       // leaf nodes (0 for decision nodes)
    face_t **markfaces; // leaf nodes only, point to node faces
    struct portal_s *portals;
    int visleafnum; // -1 = solid
    int valid;      // for flood filling
    int occupied;   // light number in leaf for outside filling
    int empty;
};

constexpr int NUM_HULLS = 4;

//=============================================================================
// solidbsp.c
extern void SubdivideFace(face_t *f, face_t **prevptr);
extern auto SolidBSP(const surfchain_t *const surfhead,
                     brush_t *detailbrushes,
                     bool report_progress) -> node_t *;

//=============================================================================
// merge.c
extern void MergePlaneFaces(surface_t *plane);
extern void MergeAll(surface_t *surfhead);

//=============================================================================
// surfaces.c
extern void MakeFaceEdges();
extern auto GetEdge(const vec3_t p1, const vec3_t p2, face_t *f) -> int;

//=============================================================================
// portals.c
struct portal_t
{
    dplane_t plane;
    node_t *onnode;   // NULL = outside box
    node_t *nodes[2]; // [0] = front side of plane
    struct portal_t *next[2];
    Winding *winding;
};

extern node_t g_outside_node; // portals outside the world face this

extern void AddPortalToNodes(portal_t *p, node_t *front, node_t *back);
extern void RemovePortalFromNode(portal_t *portal, node_t *l);
extern void MakeHeadnodePortals(node_t *node, const vec3_t mins, const vec3_t maxs);

extern void FreePortals(node_t *node);
extern void WritePortalfile(node_t *headnode);

//=============================================================================
// tjunc.c
void tjunc(node_t *headnode);

//=============================================================================
// writebsp.c
extern void WriteClipNodes(node_t *headnode);
extern void WriteDrawNodes(node_t *headnode);

extern void BeginBSPFile();
extern void FinishBSPFile();

//=============================================================================
// outside.c
extern auto FillOutside(node_t *node, bool leakfile, unsigned hullnum) -> node_t *;
extern void LoadAllowableOutsideList(const char *const filename);
extern void FreeAllowableOutsideList();
extern void FillInside(node_t *node);

//=============================================================================
// misc functions

extern auto AllocFace() -> face_t *;
extern void FreeFace(face_t *f);

extern auto AllocPortal() -> struct portal_s *;
extern void FreePortal(struct portal_s *p);

extern auto AllocSurface() -> surface_t *;
extern void FreeSurface(surface_t *s);

extern auto AllocSide() -> side_t *;
extern void FreeSide(side_t *s);
extern auto NewSideFromSide(const side_t *s) -> side_t *;
extern auto AllocBrush() -> brush_t *;
extern void FreeBrush(brush_t *b);
extern auto NewBrushFromBrush(const brush_t *b) -> brush_t *;
extern void SplitBrush(brush_t *in, const dplane_t *split, brush_t **front, brush_t **back);
extern auto BrushFromBox(const vec3_t mins, const vec3_t maxs) -> brush_t *;
extern void CalcBrushBounds(const brush_t *b, vec3_t &mins, vec3_t &maxs);

extern auto AllocNode() -> node_t *;

extern auto CheckFaceForHint(const face_t *const f) -> bool;
extern auto CheckFaceForSkip(const face_t *const f) -> bool;
extern auto CheckFaceForNull(const face_t *const f) -> bool;
extern auto CheckFaceForDiscardable(const face_t *f) -> bool;
constexpr float BRINK_FLOOR_THRESHOLD = 0.7f;
typedef enum
{
    BrinkNone = 0,
    BrinkFloorBlocking,
    BrinkFloor,
    BrinkWallBlocking,
    BrinkWall,
    BrinkAny,
} bbrinklevel_e;
extern auto CreateBrinkinfo(const dclipnode_t *clipnodes, int headnode) -> void *;
extern auto FixBrinks(const void *brinkinfo, bbrinklevel_e level, int &headnode_out, dclipnode_t *clipnodes_out, int maxsize, int size, int &size_out) -> bool;
extern void DeleteBrinkinfo(void *brinkinfo);

// =====================================================================================
// Cpt_Andrew - UTSky Check
// =====================================================================================
extern auto CheckFaceForEnv_Sky(const face_t *const f) -> bool;
// =====================================================================================

//=============================================================================
// cull.c
extern void CullStuff();

//=============================================================================
// qbsp.c
extern bool g_watervis;
extern bool g_estimate;
extern int g_maxnode_size;
extern int g_subdivide_size;
extern int g_hullnum;
extern bool g_bLeakOnly;
extern bool g_bLeaked;
extern char g_portfilename[_MAX_PATH];
extern char g_pointfilename[_MAX_PATH];
extern char g_linefilename[_MAX_PATH];
extern char g_bspfilename[_MAX_PATH];
extern char g_extentfilename[_MAX_PATH];

extern bool g_nohull2;

extern auto NewFaceFromFace(const face_t *const in) -> face_t *;
extern void SplitFace(face_t *in, const dplane_t *const split, face_t **front, face_t **back);

void HandleArgs(int argc, char **argv, const char *&mapname_from_arg);