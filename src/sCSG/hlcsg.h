#pragma once

#include <cstdio>

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
#define CSG_BOGUS_RANGE g_iWorldExtent // seedee

//=============================================================================
// brush.c
extern auto CheckBrushContents(const Brush *const b) -> contents_t;
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

extern Plane g_mapplanes[MAX_INTERNAL_MAP_PLANES];
extern int g_nummapplanes;

auto NewFaceFromFace(const BrushFace *const in) -> BrushFace *;
auto CopyFace(const BrushFace *const face) -> BrushFace *;
auto CopyFacesToOutside(BrushHull *bh) -> BrushFace *;

void WriteFace(const int hull, const BrushFace *const face, int detaillevel, FILE **out);
void WriteDetailBrush(int hull, const BrushFace *faces, FILE **out);

void BoundWorld(int numbrushes, Brush *brushes, BoundingBox wolrdbounds);

void SaveOutside(const Brush *const brush, const int hull, BrushFace *outside, const int mirrorcontents);
void CSGBrush(int brushnum);
extern auto ContentsToString(const contents_t type) -> const char *;

void SetModelNumbers(Entity *entities, int numentities);
void ReuseModel(Entity *entities, int numentities);
void SetLightStyles(Entity *entities, int numentities);
void ConvertHintToEmpty(Brush *brushes);
void MarkEntForNoclip(Entity *ent, Brush *brushes);
void CSGCleanup();
void CheckForNoClip(Entity *entities, int numentities);
void BoundWorld(Brush *brushes, BoundingBox worldbounds, int numbrushes);
void SetModelCenters(Entity *entities, int numentities, Brush *brushes);
void ProcessModels(Entity *entities, Brush *brushes, int numentities);

void OpenHullFiles(FILE **outhull, FILE **outdetail, const char *mapname);
void CloseHullFiles(FILE **hulls, FILE **detailbrush);
void WriteHullSizeFile(vec3_t (*hull_size)[2], const char *mapname);

void EmitPlanes(Plane *planes, int num_map_planes, const char *mapname);
void WriteBSP(const char *const name, Brush *brushes);

void HandleArgs(int argc, char **argv, const char *&mapname_from_arg);

//============================================================================
// hullfile.cpp
extern vec3_t g_hull_size[NUM_HULLS][2];