#pragma once

#include "hull.h"

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

extern HullShape g_defaulthulls[NUM_HULLS];
extern int g_numhullshapes;
extern HullShape g_hullshapes[MAX_HULLSHAPES];

auto FindIntPlane(const vec_t *const normal, const vec_t *const origin) -> int;
auto PlaneFromPoints(const vec_t *const p0, const vec_t *const p1, const vec_t *const p2) -> int;

void AddHullPlane(BrushHull *hull, const vec_t *const normal, const vec_t *const origin, const bool check_planenum);
void ExpandBrushWithHullBrush(const Brush *brush, const BrushHull *hull0, const HullBrush *hb, BrushHull *hull);
void ExpandBrush(Brush *brush, const int hullnum);

void SortSides(BrushHull *h);

void MakeHullFaces(const Brush *const b, BrushHull *h);
auto MakeBrushPlanes(Brush *b) -> bool;

static auto TextureContents(const char *const name) -> contents_t;
auto ContentsToString(const contents_t type) -> const char *;
auto CheckBrushContents(const Brush *const b) -> contents_t;

void CreateBrush(const int brushnum); //--vluzacn
auto CreateHullBrush(const Brush *b) -> HullBrush *;
void CreateHullShape(int entitynum, bool disabled, const char *id, int defaulthulls);