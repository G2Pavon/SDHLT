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

extern auto NewFaceFromFace(const BrushFace *const in) -> BrushFace *;
extern auto CopyFace(const BrushFace *const f) -> BrushFace *;

void HandleArgs(int argc, char **argv, const char *&mapname_from_arg);
void OpenHullFiles();
void WriteHullSizeFile();

//============================================================================
// hullfile.cpp
extern vec3_t g_hull_size[NUM_HULLS][2];