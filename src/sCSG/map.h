#pragma once

#include "bspfile.h"
#include "mathlib.h"
#include "winding.h"
#include "boundingbox.h"
#include "hull.h"

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

struct Brush
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

extern int g_nummapbrushes;
extern Brush g_mapbrushes[MAX_MAP_BRUSHES];
extern int g_numbrushsides;
extern Side g_brushsides[MAX_MAP_SIDES];
extern HullShape g_defaulthulls[NUM_HULLS];
extern int g_numhullshapes;
extern HullShape g_hullshapes[MAX_HULLSHAPES];

auto CopyCurrentBrush(Entity *entity, const Brush *brush) -> Brush *;
void DeleteCurrentEntity(Entity *entity);

void TextureAxisFromPlane(const Plane *const pln, vec3_t xv, vec3_t yv);

void FaceCheckToolTextures(Brush *b, Side *s);
void ParseFace(Brush *b, Side *s);

auto BrushCheckZHLT_Invisible(Entity *mapent) -> bool;
void BrushNullify(Brush *b, Side *s, bool isInvisible);
void BrushConvertSPLITFACE(Brush *b, Side *s);
void BrushCheckZHLT_noclip(Entity *e, Brush *b);
void BrushCheckFunc_detail(Entity *e, Brush *b);
void BrushCheckZHLT_hull(Entity *e, Brush *b);
void BrushCheckZHLT_usemodel(Entity *e, Brush *b);
void BrushCheckInfo_hullshape(Entity *e, Brush *b);
void BushCheckClipTexture(Brush *b);
void BrushCheckORIGINtexture(Entity *e, Brush *b);
void BrushCheckBOUNDINGBOXtexture(Entity *e, Brush *b);
void BrushCheckClipSkybox(Entity *e, Brush *b, Side *s);
void BrushCheckContentEmpty(Entity *e, Brush *b, Side *s);
void ParseBrush(Entity *mapent);

auto ParseMapEntity() -> bool;
auto CountEngineEntities() -> unsigned int;
auto ContentsToString(const contents_t type) -> const char *;
void LoadMapFile(const char *const filename);