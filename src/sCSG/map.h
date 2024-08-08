#pragma once
#include "mathlib.h"
#include "bspfile.h"

#include "winding.h"
#include "boundingbox.h"
#include "face.h"
#include "hull.h"
#include "brush.h"

constexpr int MAX_MAP_SIDES = MAX_MAP_BRUSHES * 6;

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