#pragma once

#include "messages.h"
#include "win32fix.h"
#include "mathlib.h"
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

struct FaceBSP // This structure is layed out so 'pts' is on a quad-word boundary (and the pointers are as well)
{
    struct FaceBSP *next;
    int planenum;
    int texturenum;
    int contents;     // contents in front of face
    int detaillevel;  // defined by hlcsg
    int *outputedges; // used in WriteDrawNodes

    struct FaceBSP *original; // face on node
    int outputnumber;         // only valid for original faces after write surfaces
    int numpoints;
    facestyle_e facestyle;
    int referenced; // only valid for original faces

    // vector quad word aligned
    vec3_t pts[MAXEDGES]; // FIXME: change to use winding_t
};

struct SurfaceBSP
{
    struct SurfaceBSP *next;
    int planenum;
    vec3_t mins, maxs;
    struct NodeBSP *onnode; // true if surface has already been used
    // as a splitting node
    FaceBSP *faces;  // links to all the faces on either side of the surf
    int detaillevel; // minimum detail level of its faces
};

struct SurfchainBSP
{
    vec3_t mins, maxs;
    SurfaceBSP *surfaces;
};

struct SideBSP
{
    struct SideBSP *next;
    dplane_t plane; // facing inside (reversed when loading brush file)
    Winding *w;     // (also reversed)
};

struct BrushBSP
{
    struct BrushBSP *next;
    SideBSP *sides;
};

//
// there is a NodeBSP structure for every node and leaf in the bsp tree
//
constexpr int PLANENUM_LEAF = -1;
constexpr float BOUNDS_EXPANSION = 1.0f; // expand the bounds of detail leafs when clipping its boundsbrush, to prevent some strange brushes in the func_detail from clipping away the entire boundsbrush making the func_detail invisible.

struct NodeBSP
{
    SurfaceBSP *surfaces;
    BrushBSP *detailbrushes;
    BrushBSP *boundsbrush;
    vec3_t loosemins, loosemaxs; // all leafs and nodes have this, while 'mins' and 'maxs' are only valid for nondetail leafs and nodes.

    bool isdetail;         // is under a diskleaf
    bool isportalleaf;     // not detail and children are detail; only visleafs have contents, portals, mins, maxs
    bool iscontentsdetail; // inside a detail brush
    vec3_t mins, maxs;     // bounding volume of portals;

    // information for decision nodes
    int planenum;                // -1 = leaf node
    struct NodeBSP *children[2]; // only valid for decision nodes
    FaceBSP *faces;              // decision nodes only, list for both sides

    // information for leafs
    int contents;        // leaf nodes (0 for decision nodes)
    FaceBSP **markfaces; // leaf nodes only, point to node faces
    struct PortalBSP *portals;
    int visleafnum; // -1 = solid
    int valid;      // for flood filling
    int occupied;   // light number in leaf for outside filling
    int empty;
};

constexpr int NUM_HULLS = 4;

//=============================================================================
// solidbsp.c
extern void SubdivideFace(FaceBSP *f, FaceBSP **prevptr);
extern auto SolidBSP(const SurfchainBSP *const surfhead,
                     BrushBSP *detailbrushes,
                     bool report_progress) -> NodeBSP *;

//=============================================================================
// merge.c
extern void MergePlaneFaces(SurfaceBSP *plane);
extern void MergeAll(SurfaceBSP *surfhead);

//=============================================================================
// surfaces.c
extern void MakeFaceEdges();
extern auto GetEdge(const vec3_t p1, const vec3_t p2, FaceBSP *f) -> int;

//=============================================================================
// portals.c
struct PortalBSP
{
    dplane_t plane;
    NodeBSP *onnode;   // NULL = outside box
    NodeBSP *nodes[2]; // [0] = front side of plane
    struct PortalBSP *next[2];
    Winding *winding;
};

extern NodeBSP g_outside_node; // portals outside the world face this

extern void AddPortalToNodes(PortalBSP *p, NodeBSP *front, NodeBSP *back);
extern void RemovePortalFromNode(PortalBSP *portal, NodeBSP *l);
extern void MakeHeadnodePortals(NodeBSP *node, const vec3_t mins, const vec3_t maxs);

extern void FreePortals(NodeBSP *node);
extern void WritePortalfile(NodeBSP *headnode);

//=============================================================================
// tjunc.c
void tjunc(NodeBSP *headnode);

//=============================================================================
// writebsp.c
extern void WriteClipNodes(NodeBSP *headnode);
extern void WriteDrawNodes(NodeBSP *headnode);

extern void BeginBSPFile();
extern void FinishBSPFile();

//=============================================================================
// outside.c
extern auto FillOutside(NodeBSP *node, bool leakfile, unsigned hullnum) -> NodeBSP *;
extern void LoadAllowableOutsideList(const char *const filename);
extern void FreeAllowableOutsideList();
extern void FillInside(NodeBSP *node);

//=============================================================================
// misc functions

extern auto AllocFace() -> FaceBSP *;

extern auto AllocPortal() -> struct PortalBSP *;
extern void FreePortal(struct PortalBSP *p);

extern auto AllocSurface() -> SurfaceBSP *;
extern void FreeSurface(SurfaceBSP *s);

extern auto AllocSide() -> SideBSP *;
extern void FreeSide(SideBSP *s);
extern auto NewSideFromSide(const SideBSP *s) -> SideBSP *;
extern auto AllocBrush() -> BrushBSP *;
extern void FreeBrush(BrushBSP *b);
extern auto NewBrushFromBrush(const BrushBSP *b) -> BrushBSP *;
extern void SplitBrush(BrushBSP *in, const dplane_t *split, BrushBSP **front, BrushBSP **back);
extern auto BrushFromBox(const vec3_t mins, const vec3_t maxs) -> BrushBSP *;
extern void CalcBrushBounds(const BrushBSP *b, vec3_t &mins, vec3_t &maxs);

extern auto AllocNode() -> NodeBSP *;

extern auto CheckFaceForHint(const FaceBSP *const f) -> bool;
extern auto CheckFaceForSkip(const FaceBSP *const f) -> bool;
extern auto CheckFaceForNull(const FaceBSP *const f) -> bool;
extern auto CheckFaceForDiscardable(const FaceBSP *f) -> bool;
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
extern auto CreateBrinkinfo(const BSPLumpClipnode *clipnodes, int headnode) -> void *;
extern auto FixBrinks(const void *brinkinfo, bbrinklevel_e level, int &headnode_out, BSPLumpClipnode *clipnodes_out, int maxsize, int size, int &size_out) -> bool;
extern void DeleteBrinkinfo(void *brinkinfo);

// =====================================================================================
// Cpt_Andrew - UTSky Check
// =====================================================================================
extern auto CheckFaceForEnv_Sky(const FaceBSP *const f) -> bool;
// =====================================================================================

//=============================================================================
// cull.c
extern void CullStuff();

//=============================================================================
// hlbsp.c
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

extern auto NewFaceFromFace(const FaceBSP *const in) -> FaceBSP *;
extern void SplitFace(FaceBSP *in, const dplane_t *const split, FaceBSP **front, FaceBSP **back);

void HandleArgs(int argc, char **argv, const char *&mapname_from_arg);