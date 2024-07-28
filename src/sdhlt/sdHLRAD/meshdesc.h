/*
meshdesc.h - cached mesh for tracing custom objects
Copyright (C) 2012 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#pragma once

#include "studio.h"
#include "list.h" // simple container

constexpr int AREA_NODES = 32;
constexpr int AREA_DEPTH = 4;

constexpr int MAX_FACET_PLANES = 32;
constexpr int MAX_PLANES = 524288; // unsigned short limit
constexpr int PLANE_HASHES = MAX_PLANES >> 2;

constexpr float PLANE_NORMAL_EPSILON = 0.00001f;
constexpr float PLANE_DIST_EPSILON = 0.04f;

// compute methods
constexpr int SHADOW_FAST = 0;
constexpr int SHADOW_NORMAL = 1;
constexpr int SHADOW_SLOW = 2;

#ifndef M_PI
constexpr double M_PI = 3.14159265358979323846; // matches value in gcc v2 math.h
#endif

typedef unsigned short word;
typedef unsigned int uint;
typedef vec_t vec4_t[4]; // x,y,z,w
typedef vec_t matrix3x4[3][4];

#define Q_rint(x) ((x) < 0 ? ((int)((x) - 0.5f)) : ((int)((x) + 0.5f)))
#ifndef __MSC_VER
#define _inline inline
#endif

typedef struct mplane_s
{
	vec3_t normal;
	float dist;
	byte type;	   // for fast side tests
	byte signbits; // signx + (signy<<1) + (signz<<1)
	byte pad[2];
} mplane_t;

typedef struct hashplane_s
{
	mplane_t pl;
	struct hashplane_s *hash;
} hashplane_t;

typedef struct link_s
{
	struct link_s *prev, *next;
} link_t;

typedef struct areanode_s
{
	int axis; // -1 = leaf node
	float dist;
	struct areanode_s *children[2];
	link_t facets;
} areanode_t;

typedef struct mvert_s
{
	vec3_t point;
	float st[2]; // for alpha-texture test
} mvert_t;

typedef struct
{
	link_t area;			   // linked to a division node or leaf
	mstudiotexture_t *texture; // valid for alpha-testing surfaces
	mvert_t triangle[3];	   // store triangle points
	vec3_t mins, maxs;		   // an individual size of each facet
	vec3_t edge1, edge2;	   // new trace stuff
	byte numplanes;			   // because numplanes for each facet can't exceeds MAX_FACET_PLANES!
	uint *indices;			   // a indexes into mesh plane pool
} mfacet_t;

typedef struct
{
	int trace_mode; // trace method
	vec3_t mins, maxs;
	uint numfacets;
	uint numplanes;
	mfacet_t *facets;
	mplane_t *planes; // shared plane pool
} mmesh_t;

class triset
{
public:
	int v[3]; // indices to vertex list
};

class vector
{
public:
	inline vector(float X = 0.0f, float Y = 0.0f, float Z = 0.0f)
	{
		x = X;
		y = Y;
		z = Z;
	};
	inline vector(const float *rgfl)
	{
		x = rgfl[0];
		y = rgfl[1];
		z = rgfl[2];
	}
	inline vector(float rgfl[3])
	{
		x = rgfl[0];
		y = rgfl[1];
		z = rgfl[2];
	}
	inline vector(const vector &v)
	{
		x = v.x;
		y = v.y;
		z = v.z;
	}
	operator const float *() const { return &x; }
	operator float *() { return &x; }
	float x, y, z;
};

class CMeshDesc
{
private:
	mmesh_t m_mesh;
	const char *m_debugName;		  // just for debug purpoces
	areanode_t areanodes[AREA_NODES]; // AABB tree for speedup trace test
	int numareanodes;
	bool has_tree;		// build AABB tree
	int m_iTotalPlanes; // just for stats
	int m_iNumTris;		// if > 0 we are in build mode
	size_t mesh_size;	// mesh total size

	// used only while mesh is contsructed
	mfacet_t *facets;
	hashplane_t **planehash;
	hashplane_t *planepool;

public:
	CMeshDesc();
	~CMeshDesc();

	// mesh construction
	auto InitMeshBuild(const char *debug_name, int numTrinagles) -> bool;
	auto AddMeshTrinagle(const mvert_t triangle[3], mstudiotexture_t *tex = nullptr) -> bool;
	auto FinishMeshBuild() -> bool;
	void FreeMeshBuild();
	void FreeMesh();

	// local mathlib
	void AngleMatrix(const vec3_t angles, const vec3_t origin, const vec3_t scale, float (*matrix)[4]);
	void ConcatTransforms(float in1[3][4], float in2[3][4], float out[3][4]);
	void QuaternionMatrix(vec4_t quat, const vec3_t origin, float (*matrix)[4]);
	void VectorTransform(const vec3_t in1, float in2[3][4], vec3_t out);
	void AngleQuaternion(const vec3_t angles, vec4_t quat);

	// studio models processing
	void StudioCalcBoneQuaterion(mstudiobone_t *pbone, mstudioanim_t *panim, vec4_t q);
	void StudioCalcBonePosition(mstudiobone_t *pbone, mstudioanim_t *panim, vec3_t pos);
	auto StudioConstructMesh(struct model_s *pModel) -> bool;

	// linked list operations
	void InsertLinkBefore(link_t *l, link_t *before);
	void RemoveLink(link_t *l);
	void ClearLink(link_t *l);

	// AABB tree contsruction
	auto CreateAreaNode(int depth, const vec3_t mins, const vec3_t maxs) -> areanode_t *;
	void RelinkFacet(mfacet_t *facet);
	_inline auto GetHeadNode() -> areanode_t * { return (has_tree) ? &areanodes[0] : nullptr; }

	// plane cache
	auto AddPlaneToPool(const mplane_t *pl) -> uint;
	auto PlaneFromPoints(const mvert_t triangle[3], mplane_t *plane) -> bool;
	auto ComparePlanes(const mplane_t *plane, const vec3_t normal, float dist) -> bool;
	auto PlaneEqual(const mplane_t *p0, const mplane_t *p1) -> bool;
	void CategorizePlane(mplane_t *plane);
	void SnapPlaneToGrid(mplane_t *plane);
	void SnapVectorToGrid(vec3_t normal);

	// check for cache
	_inline auto GetMesh() -> mmesh_t * { return &m_mesh; }

	void ClearBounds(vec3_t mins, vec3_t maxs)
	{
		// make bogus range
		mins[0] = mins[1] = mins[2] = 999999.0f;
		maxs[0] = maxs[1] = maxs[2] = -999999.0f;
	}

	void AddPointToBounds(const vec3_t v, vec3_t mins, vec3_t maxs)
	{
		for (int i = 0; i < 3; i++)
		{
			if (v[i] < mins[i])
				mins[i] = v[i];
			if (v[i] > maxs[i])
				maxs[i] = v[i];
		}
	}

	auto Intersect(const vec3_t trace_mins, const vec3_t trace_maxs) -> bool
	{
		if (m_mesh.mins[0] > trace_maxs[0] || m_mesh.mins[1] > trace_maxs[1] || m_mesh.mins[2] > trace_maxs[2])
			return false;
		if (m_mesh.maxs[0] < trace_mins[0] || m_mesh.maxs[1] < trace_mins[1] || m_mesh.maxs[2] < trace_mins[2])
			return false;
		return true;
	}
};

// simplification
void ProgressiveMesh(List<vector> &vert, List<triset> &tri, List<int> &map, List<int> &permutation);
void PermuteVertices(List<int> &permutation, List<vector> &vert, List<triset> &tris);
auto MapVertex(int a, int mx, List<int> &map) -> int;

// collision description
typedef struct model_s
{
	char name[64]; // model name
	vec3_t origin;
	vec3_t angles;
	vec3_t scale;	// scale X-Form
	int body;		// sets by level-designer
	int skin;		// e.g. various alpha-textures
	int trace_mode; // 0 - ultra fast, 1 - med, 2 - slow

	void *extradata; // model
	void *anims;	 // studio animations

	CMeshDesc mesh; // cform
} model_t;