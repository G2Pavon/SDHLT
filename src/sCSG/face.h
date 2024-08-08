#pragma once

#include <string>

#include "mathlib.h"
#include "winding.h"

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