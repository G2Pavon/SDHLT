#pragma once

#include "mathlib.h"
#include "winding.h"
#include "boundingbox.h"

#ifndef DOUBLEVEC_T
#error you must add -dDOUBLEVEC_T to the project!
#endif

typedef enum
{
    clip_smallest,
    clip_normalized,
    clip_simple,
    clip_precise,
    clip_legacy
} cliptype;
extern cliptype g_cliptype;

constexpr bool DEFAULT_SKYCLIP = true;
constexpr float FLOOR_Z = 0.7f;
constexpr cliptype DEFAULT_CLIPTYPE = clip_simple;
constexpr bool DEFAULT_CLIPNAZI = false;
constexpr bool DEFAULT_ESTIMATE = true;

constexpr int MAX_HULLSHAPES = 128; // arbitrary
constexpr int NUM_HULLS = 4;        // NUM_HULLS should be no larger than MAX_MAP_HULLS

#define BOGUS_RANGE g_iWorldExtent // seedee

struct plane_t
{
    vec3_t normal;
    vec3_t origin;
    vec_t dist;
    planetypes type;
};

struct valve_vects
{
    vec3_t UAxis;
    vec3_t VAxis;
    vec_t shift[2];
    vec_t rotate;
    vec_t scale[2];
};

struct vects_union
{
    valve_vects valve;
};

struct brush_texture_t
{
    vects_union vects;
    char name[32];
};

struct side_t
{
    brush_texture_t td;
    bool bevel;
    vec_t planepts[3][3];
};

struct bface_t
{
    struct bface_t *next;
    int planenum;
    plane_t *plane;
    Winding *w;
    int texinfo;
    bool used; // just for face counting
    int contents;
    int backcontents;
    bool bevel; // used for ExpandBrush
    BoundingBox bounds;
};

struct brushhull_t
{
    BoundingBox bounds;
    bface_t *faces;
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
    brushhull_t hulls[NUM_HULLS];
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

//=============================================================================
// map.c

constexpr int MAX_MAP_SIDES = MAX_MAP_BRUSHES * 6;

extern int g_nummapbrushes;
extern brush_t g_mapbrushes[MAX_MAP_BRUSHES];
extern int g_numbrushsides;
extern side_t g_brushsides[MAX_MAP_SIDES];
extern hullshape_t g_defaulthulls[NUM_HULLS];
extern int g_numhullshapes;
extern hullshape_t g_hullshapes[MAX_HULLSHAPES];

extern void TextureAxisFromPlane(const plane_t *const pln, vec3_t xv, vec3_t yv);
extern void LoadMapFile(const char *const filename);

//=============================================================================
// textures.cpp

extern void WriteMiptex();
extern auto TexinfoForBrushTexture(const plane_t *const plane, brush_texture_t *bt, const vec3_t origin) -> int;
extern auto GetTextureByNumber_CSG(int texturenumber) -> const char *;

//=============================================================================
// brush.c

extern auto Brush_LoadEntity(entity_t *ent, int hullnum) -> brush_t *;
extern auto CheckBrushContents(const brush_t *const b) -> contents_t;

extern void CreateBrush(int brushnum);
extern void CreateHullShape(int entitynum, bool disabled, const char *id, int defaulthulls);
extern void InitDefaultHulls();

//=============================================================================
// csg.c

constexpr int MAX_SWITCHED_LIGHTS = 32;
constexpr int MAX_LIGHTTARGETS_NAME = 64;

extern bool g_skyclip;
extern bool g_estimate;
extern bool g_bClipNazi;

extern plane_t g_mapplanes[MAX_INTERNAL_MAP_PLANES];
extern int g_nummapplanes;

extern auto NewFaceFromFace(const bface_t *const in) -> bface_t *;
extern auto CopyFace(const bface_t *const f) -> bface_t *;

extern void FreeFace(bface_t *f);
extern void FreeFaceList(bface_t *f);
void HandleArgs(int argc, char **argv, const char *&mapname_from_arg);
void OpenHullFiles();
void WriteHullSizeFile();

//============================================================================
// hullfile.cpp
extern vec3_t g_hull_size[NUM_HULLS][2];

#ifdef HLCSG_GAMETEXTMESSAGE_UTF8
extern char *ANSItoUTF8(const char *);
#endif

#ifdef HLCSG_GAMETEXTMESSAGE_UTF8
void ConvertGameTextMessages()
#endif