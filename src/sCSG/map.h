#include "bspfile.h"
#include "mathlib.h"
#include "winding.h"
#include "boundingbox.h"

constexpr int MAX_HULLSHAPES = 128; // arbitrary
constexpr int NUM_HULLS = 4;        // NUM_HULLS should be no larger than MAX_MAP_HULLS

constexpr int MAX_MAP_SIDES = MAX_MAP_BRUSHES * 6;

struct Plane
{
    vec3_t normal;
    vec3_t origin;
    vec_t dist;
    planetypes type;
};

struct FaceTexture
{
    char name[32];
    vec3_t UAxis;
    vec3_t VAxis;
    vec_t shift[2];
    vec_t rotate;
    vec_t scale[2];
};

struct Side
{
    FaceTexture texture;
    bool bevel;
    vec_t planepts[3][3];
};

struct BrushFace
{
    struct BrushFace *next;
    int planenum;
    Plane *plane;
    Winding *w;
    int texinfo;
    bool used; // just for face counting
    int contents;
    int backcontents;
    bool bevel; // used for ExpandBrush
    BoundingBox bounds;
};

struct BrushHull
{
    BoundingBox bounds;
    BrushFace *faces;
};

struct brush_t
{
    int originalentitynum;
    int originalbrushnum;
    int entitynum;
    int brushnum;

    int firstside;
    int numsides;

    unsigned int noclip; // !!!FIXME: this should be a flag bitfield so we can use it for other stuff (ie. is this a detail brush...)
    unsigned int cliphull;
    bool bevel;
    int detaillevel;
    int chopdown; // allow this brush to chop brushes of lower detail level
    int chopup;   // allow this brush to be chopped by brushes of higher detail level
    int clipnodedetaillevel;
    int coplanarpriority;
    char *hullshapes[NUM_HULLS]; // might be NULL

    int contents;
    BrushHull hulls[NUM_HULLS];
};

struct hullbrushface_t
{
    vec3_t normal;
    vec3_t point;

    int numvertexes;
    vec3_t *vertexes;
};

struct hullbrushedge_t
{
    vec3_t normals[2];
    vec3_t point;
    vec3_t vertexes[2];
    vec3_t delta; // delta has the same direction as CrossProduct(normals[0],normals[1])
};

struct hullbrushvertex_t
{
    vec3_t point;
};

struct hullbrush_t
{
    int numfaces;
    hullbrushface_t *faces;
    int numedges;
    hullbrushedge_t *edges;
    int numvertexes;
    hullbrushvertex_t *vertexes;
};

struct hullshape_t
{
    char *id;
    bool disabled;
    int numbrushes; // must be 0 or 1
    hullbrush_t **brushes;
};

extern int g_nummapbrushes;
extern brush_t g_mapbrushes[MAX_MAP_BRUSHES];
extern int g_numbrushsides;
extern Side g_brushsides[MAX_MAP_SIDES];
extern hullshape_t g_defaulthulls[NUM_HULLS];
extern int g_numhullshapes;
extern hullshape_t g_hullshapes[MAX_HULLSHAPES];

extern void TextureAxisFromPlane(const Plane *const pln, vec3_t xv, vec3_t yv);
extern void LoadMapFile(const char *const filename);