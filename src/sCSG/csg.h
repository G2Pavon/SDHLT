#pragma once

#include "mathlib.h"
#include "map.h"

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
#define BOGUS_RANGE g_iWorldExtent // seedee

//=============================================================================
// map.c

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