#pragma once

#include "mathtypes.h"

constexpr int MAX_HULLSHAPES = 128; // arbitrary
constexpr int NUM_HULLS = 4;        // NUM_HULLS should be no larger than MAX_MAP_HULLS

struct HullBrushFace
{
    vec3_t normal;
    vec3_t point;

    int numvertexes;
    vec3_t *vertexes;
};

struct HullBrushEdge
{
    vec3_t normals[2];
    vec3_t point;
    vec3_t vertexes[2];
    vec3_t delta; // delta has the same direction as CrossProduct(normals[0],normals[1])
};

struct HullBrushVertex
{
    vec3_t point;
};

struct HullBrush
{
    int numfaces;
    HullBrushFace *faces;
    int numedges;
    HullBrushEdge *edges;
    int numvertexes;
    HullBrushVertex *vertexes;
};

struct HullShape
{
    char *id;
    bool disabled;
    int numbrushes; // must be 0 or 1
    HullBrush **brushes;
};