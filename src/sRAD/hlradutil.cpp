#include "hlrad.h"
#include "log.h"

static dplane_t backplanes[MAX_MAP_PLANES];

auto PointInLeaf_Worst_r(int nodenum, const vec3_t point) -> BSPLumpLeaf *
{
	while (nodenum >= 0)
	{
		auto *node = &g_bspnodes[nodenum];
		dplane_t *plane = &g_bspplanes[node->planenum];
		vec_t dist = DotProduct(point, plane->normal) - plane->dist;
		if (dist > HUNT_WALL_EPSILON)
		{
			nodenum = node->children[0];
		}
		else if (dist < -HUNT_WALL_EPSILON)
		{
			nodenum = node->children[1];
		}
		else
		{
			BSPLumpLeaf *result[2];
			result[0] = PointInLeaf_Worst_r(node->children[0], point);
			result[1] = PointInLeaf_Worst_r(node->children[1], point);
			if (result[0] == g_bspleafs || result[0]->contents == static_cast<int>(contents_t::CONTENTS_SOLID))
				return result[0];
			if (result[1] == g_bspleafs || result[1]->contents == static_cast<int>(contents_t::CONTENTS_SOLID))
				return result[1];
			if (result[0]->contents == CONTENTS_SKY)
				return result[0];
			if (result[1]->contents == CONTENTS_SKY)
				return result[1];
			if (result[0]->contents == result[1]->contents)
				return result[0];
			return g_bspleafs;
		}
	}

	return &g_bspleafs[-nodenum - 1];
}
auto PointInLeaf_Worst(const vec3_t point) -> BSPLumpLeaf *
{
	return PointInLeaf_Worst_r(0, point);
}
auto PointInLeaf(const vec3_t point) -> BSPLumpLeaf *
{
	int nodenum = 0;
	while (nodenum >= 0)
	{
		auto *node = &g_bspnodes[nodenum];
		dplane_t *plane = &g_bspplanes[node->planenum];
		vec_t dist = DotProduct(point, plane->normal) - plane->dist;
		if (dist >= 0.0)
		{
			nodenum = node->children[0];
		}
		else
		{
			nodenum = node->children[1];
		}
	}

	return &g_bspleafs[-nodenum - 1];
}

/*
 * ==============
 * PatchPlaneDist
 * Fixes up patch planes for brush models with an origin brush
 * ==============
 */
auto PatchPlaneDist(const Patch *const patch) -> vec_t
{
	const dplane_t *plane = getPlaneFromFaceNumber(patch->faceNumber);

	return plane->dist + DotProduct(g_face_offset[patch->faceNumber], plane->normal);
}

void MakeBackplanes()
{
	for (int i = 0; i < g_bspnumplanes; i++)
	{
		backplanes[i].dist = -g_bspplanes[i].dist;
		VectorSubtract(vec3_origin, g_bspplanes[i].normal, backplanes[i].normal);
	}
}

auto getPlaneFromFace(const BSPLumpFace *const face) -> const dplane_t *
{
	if (!face)
	{
		Error("getPlaneFromFace() face was NULL\n");
	}

	if (face->side)
	{
		return &backplanes[face->planenum];
	}
	else
	{
		return &g_bspplanes[face->planenum];
	}
}

auto getPlaneFromFaceNumber(const unsigned int faceNumber) -> const dplane_t *
{
	BSPLumpFace *face = &g_bspfaces[faceNumber];

	if (face->side)
	{
		return &backplanes[face->planenum];
	}
	else
	{
		return &g_bspplanes[face->planenum];
	}
}

// Returns plane adjusted for face offset (for origin brushes, primarily used in the opaque code)
void getAdjustedPlaneFromFaceNumber(unsigned int faceNumber, dplane_t *plane)
{
	BSPLumpFace *face = &g_bspfaces[faceNumber];
	const vec_t *face_offset = g_face_offset[faceNumber];

	plane->type = (planetypes)0;

	if (face->side)
	{
		vec_t dist;

		VectorCopy(backplanes[face->planenum].normal, plane->normal);
		dist = DotProduct(plane->normal, face_offset);
		plane->dist = backplanes[face->planenum].dist + dist;
	}
	else
	{
		vec_t dist;

		VectorCopy(g_bspplanes[face->planenum].normal, plane->normal);
		dist = DotProduct(plane->normal, face_offset);
		plane->dist = g_bspplanes[face->planenum].dist + dist;
	}
}

// Will modify the plane with the new dist
void TranslatePlane(dplane_t *plane, const vec_t *delta)
{
	plane->dist += DotProduct(plane->normal, delta);
}

// HuntForWorld will never return CONTENTS_SKY or contents_t::CONTENTS_SOLID leafs
auto HuntForWorld(vec_t *point, const vec_t *plane_offset, const dplane_t *plane, int hunt_size, vec_t hunt_scale, vec_t hunt_offset) -> BSPLumpLeaf *
{
	BSPLumpLeaf *leaf;

	vec3_t current_point;
	vec3_t original_point;

	vec3_t best_point;
	BSPLumpLeaf *best_leaf = nullptr;
	vec_t best_dist = 99999999.0;

	vec3_t scales;

	dplane_t new_plane = *plane;

	scales[0] = 0.0;
	scales[1] = -hunt_scale;
	scales[2] = hunt_scale;

	VectorCopy(point, best_point);
	VectorCopy(point, original_point);

	TranslatePlane(&new_plane, plane_offset);

	for (int a = 0; a < hunt_size; a++)
	{
		for (int x = 0; x < 3; x++)
		{
			current_point[0] = original_point[0] + (scales[x % 3] * a);
			for (int y = 0; y < 3; y++)
			{
				current_point[1] = original_point[1] + (scales[y % 3] * a);
				for (int z = 0; z < 3; z++)
				{
					if (a == 0)
					{
						if (x || y || z)
							continue;
					}
					vec3_t delta;
					current_point[2] = original_point[2] + (scales[z % 3] * a);

					SnapToPlane(&new_plane, current_point, hunt_offset);
					VectorSubtract(current_point, original_point, delta);
					vec_t dist = DotProduct(delta, delta);

					{
						int x;
						for (x = 0; x < g_opaque_face_count; x++)
						{
							if (TestPointOpaque(g_opaque_face_list[x].modelnum, g_opaque_face_list[x].origin, g_opaque_face_list[x].block, current_point))
								break;
						}
						if (x < g_opaque_face_count)
							continue;
					}
					if (dist < best_dist)
					{
						if ((leaf = PointInLeaf_Worst(current_point)) != g_bspleafs)
						{
							if ((leaf->contents != CONTENTS_SKY) && (leaf->contents != static_cast<int>(contents_t::CONTENTS_SOLID)))
							{
								if (x || y || z)
								{
									// dist = best_dist;
									best_dist = dist;
									best_leaf = leaf;
									VectorCopy(current_point, best_point);
									continue;
								}
								else
								{
									VectorCopy(current_point, point);
									return leaf;
								}
							}
						}
					}
				}
			}
		}
		if (best_leaf)
		{
			break;
		}
	}

	VectorCopy(best_point, point);
	return best_leaf;
}

// ApplyMatrix: (x y z 1)T -> matrix * (x y z 1)T
void ApplyMatrix(const Matrix &m, const vec3_t in, vec3_t &out)
{
	hlassume(&in[0] != &out[0], assume_first);
	VectorCopy(m.v[3], out);
	for (int i = 0; i < 3; i++)
	{
		VectorMA(out, in[i], m.v[i], out);
	}
}

// ApplyMatrixOnPlane: (x y z -dist) -> (x y z -dist) * matrix
void ApplyMatrixOnPlane(const Matrix &m_inverse, const vec3_t in_normal, vec_t in_dist, vec3_t &out_normal, vec_t &out_dist)
// out_normal is not normalized
{
	hlassume(&in_normal[0] != &out_normal[0], assume_first);
	for (int i = 0; i < 3; i++)
	{
		out_normal[i] = DotProduct(in_normal, m_inverse.v[i]);
	}
	out_dist = -(DotProduct(in_normal, m_inverse.v[3]) - in_dist);
}

void MultiplyMatrix(const Matrix &m_left, const Matrix &m_right, Matrix &m)
// The following two processes are equivalent:
//  1) ApplyMatrix (m1, v_in, v_temp), ApplyMatrix (m2, v_temp, v_out);
//  2) MultiplyMatrix (m2, m1, m), ApplyMatrix (m, v_in, v_out);
{
	const vec_t lastrow[4] = {0, 0, 0, 1};

	hlassume(&m != &m_left && &m != &m_right, assume_first);
	for (int i = 0; i < 3; i++)
	{
		for (int j = 0; j < 4; j++)
		{
			m.v[j][i] = m_left.v[0][i] * m_right.v[j][0] + m_left.v[1][i] * m_right.v[j][1] + m_left.v[2][i] * m_right.v[j][2] + m_left.v[3][i] * lastrow[j];
		}
	}
}

auto MultiplyMatrix(const Matrix &m_left, const Matrix &m_right) -> Matrix
{
	Matrix m;

	MultiplyMatrix(m_left, m_right, m);
	return m;
}

void MatrixForScale(const vec3_t center, vec_t scale, Matrix &m)
{
	for (int i = 0; i < 3; i++)
	{
		VectorClear(m.v[i]);
		m.v[i][i] = scale;
	}
	VectorScale(center, 1 - scale, m.v[3]);
}

auto MatrixForScale(const vec3_t center, vec_t scale) -> Matrix
{
	Matrix m;

	MatrixForScale(center, scale, m);
	return m;
}

auto CalcMatrixSign(const Matrix &m) -> vec_t
{
	vec3_t v;

	CrossProduct(m.v[0], m.v[1], v);
	return DotProduct(v, m.v[2]);
}

void TranslateWorldToTex(int facenum, Matrix &m)
// without g_face_offset
{
	int i;
	auto *f = &g_bspfaces[facenum];
	auto *ti = &g_bsptexinfo[f->texinfo];
	const dplane_t *fp = getPlaneFromFace(f);
	for (i = 0; i < 3; i++)
	{
		m.v[i][0] = ti->vecs[0][i];
		m.v[i][1] = ti->vecs[1][i];
		m.v[i][2] = fp->normal[i];
	}
	m.v[3][0] = ti->vecs[0][3];
	m.v[3][1] = ti->vecs[1][i];
	m.v[3][2] = -fp->dist;
}

auto InvertMatrix(const Matrix &m, Matrix &m_inverse) -> bool
{
	double texplanes[2][4];
	double faceplane[4];
	double texaxis[2][3];
	double normalaxis[3];
	double texorg[3];

	for (int i = 0; i < 4; i++)
	{
		texplanes[0][i] = m.v[i][0];
		texplanes[1][i] = m.v[i][1];
		faceplane[i] = m.v[i][2];
	}

	auto sqrlen1 = DotProduct(texplanes[0], texplanes[0]);
	auto sqrlen2 = DotProduct(texplanes[1], texplanes[1]);
	auto sqrlen3 = DotProduct(faceplane, faceplane);
	if (sqrlen1 <= NORMAL_EPSILON * NORMAL_EPSILON || sqrlen2 <= NORMAL_EPSILON * NORMAL_EPSILON || sqrlen3 <= NORMAL_EPSILON * NORMAL_EPSILON)
	// s gradient, t gradient or face normal is too close to 0
	{
		return false;
	}

	CrossProduct(texplanes[0], texplanes[1], normalaxis);
	auto det = DotProduct(normalaxis, faceplane);
	if (det * det <= sqrlen1 * sqrlen2 * sqrlen3 * NORMAL_EPSILON * NORMAL_EPSILON)
	// s gradient, t gradient and face normal are coplanar
	{
		return false;
	}
	VectorScale(normalaxis, 1 / det, normalaxis);

	CrossProduct(texplanes[1], faceplane, texaxis[0]);
	VectorScale(texaxis[0], 1 / det, texaxis[0]);

	CrossProduct(faceplane, texplanes[0], texaxis[1]);
	VectorScale(texaxis[1], 1 / det, texaxis[1]);

	VectorScale(normalaxis, -faceplane[3], texorg);
	VectorMA(texorg, -texplanes[0][3], texaxis[0], texorg);
	VectorMA(texorg, -texplanes[1][3], texaxis[1], texorg);

	VectorCopy(texaxis[0], m_inverse.v[0]);
	VectorCopy(texaxis[1], m_inverse.v[1]);
	VectorCopy(normalaxis, m_inverse.v[2]);
	VectorCopy(texorg, m_inverse.v[3]);
	return true;
}

struct position_t
{
	bool valid;
	bool nudged;
	vec_t best_s; // FindNearestPosition will return this value
	vec_t best_t;
	vec3_t pos; // with DEFAULT_HUNT_OFFSET
};

// Size of potision_t (21) * positions per sample (9) * max number of samples (max AllocBlock (64) * 128 * 128)
//   = 200MB of RAM
// But they are freed before BuildVisLeafs, so it's not a problem.

struct positionmap_t
{
	bool valid;
	int facenum;
	vec3_t face_offset;
	vec3_t face_centroid;
	Matrix worldtotex;
	Matrix textoworld;
	Winding *facewinding;
	dplane_t faceplane;
	Winding *facewindingwithoffset;
	dplane_t faceplanewithoffset;
	Winding *texwinding;
	dplane_t texplane; // (0, 0, 1, 0) or (0, 0, -1, 0)
	vec3_t texcentroid;
	vec3_t start;	  // s_start, t_start, 0
	vec3_t step;	  // s_step, t_step, 0
	int w;			  // number of s
	int h;			  // number of t
	position_t *grid; // [h][w]
};

static positionmap_t g_face_positions[MAX_MAP_FACES];

static auto IsPositionValid(positionmap_t *map, const vec3_t &pos_st, vec3_t &pos_out, bool usephongnormal = true, bool doedgetest = true, int hunt_size = 2, vec_t hunt_scale = 0.2) -> bool
{
	vec3_t pos;
	vec3_t pos_normal;

	ApplyMatrix(map->textoworld, pos_st, pos);
	VectorAdd(pos, map->face_offset, pos);
	if (usephongnormal)
	{
		GetPhongNormal(map->facenum, pos, pos_normal);
	}
	else
	{
		VectorCopy(map->faceplanewithoffset.normal, pos_normal);
	}
	VectorMA(pos, DEFAULT_HUNT_OFFSET, pos_normal, pos);

	vec_t hunt_offset = DotProduct(pos, map->faceplanewithoffset.normal) - map->faceplanewithoffset.dist; // might be smaller than DEFAULT_HUNT_OFFSET

	// push the point 0.2 units around to avoid walls
	if (!HuntForWorld(pos, vec3_origin, &map->faceplanewithoffset, hunt_size, hunt_scale, hunt_offset))
	{
		return false;
	}

	if (doedgetest && !point_in_winding_noedge(*map->facewindingwithoffset, map->faceplanewithoffset, pos, DEFAULT_EDGE_WIDTH))
	{
		// if the sample has gone beyond face boundaries, be careful that it hasn't passed a wall
		vec3_t test;
		vec3_t transparency;
		int opaquestyle;

		VectorCopy(pos, test);
		snap_to_winding_noedge(*map->facewindingwithoffset, map->faceplanewithoffset, test, DEFAULT_EDGE_WIDTH, 4 * DEFAULT_EDGE_WIDTH);

		if (!HuntForWorld(test, vec3_origin, &map->faceplanewithoffset, hunt_size, hunt_scale, hunt_offset))
		{
			return false;
		}

		if (TestLine(pos, test) != CONTENTS_EMPTY)
		{
			return false;
		}

		if (TestSegmentAgainstOpaqueList(pos, test, transparency, opaquestyle) == true || opaquestyle != -1)
		{
			return false;
		}
	}

	VectorCopy(pos, pos_out);
	return true;
}

static void CalcSinglePosition(positionmap_t *map, int is, int it)
{
	dplane_t clipplanes[4];
	const vec3_t v_s = {1, 0, 0};
	const vec3_t v_t = {0, 1, 0};
	auto *p = &map->grid[is + map->w * it];
	vec_t smin = map->start[0] + is * map->step[0];
	vec_t smax = map->start[0] + (is + 1) * map->step[0];
	vec_t tmin = map->start[1] + it * map->step[1];
	vec_t tmax = map->start[1] + (it + 1) * map->step[1];

	VectorScale(v_s, 1, clipplanes[0].normal);
	clipplanes[0].dist = smin;
	VectorScale(v_s, -1, clipplanes[1].normal);
	clipplanes[1].dist = -smax;
	VectorScale(v_t, 1, clipplanes[2].normal);
	clipplanes[2].dist = tmin;
	VectorScale(v_t, -1, clipplanes[3].normal);
	clipplanes[3].dist = -tmax;

	p->nudged = true; // it's nudged unless it can get its position directly from its s,t
	auto *zone = new Winding(*map->texwinding);
	for (int x = 0; x < 4 && zone->m_NumPoints > 0; x++)
	{
		zone->Clip(clipplanes[x], false);
	}
	if (zone->m_NumPoints == 0)
	{
		p->valid = false;
	}
	else
	{
		vec3_t original_st;
		vec3_t test_st;

		original_st[0] = map->start[0] + (is + 0.5) * map->step[0];
		original_st[1] = map->start[1] + (it + 0.5) * map->step[1];
		original_st[2] = 0.0;

		p->valid = false;

		if (!p->valid)
		{
			VectorCopy(original_st, test_st);
			snap_to_winding(*zone, map->texplane, test_st);

			if (IsPositionValid(map, test_st, p->pos))
			{
				p->valid = true;
				p->nudged = false;
				p->best_s = test_st[0];
				p->best_t = test_st[1];
			}
		}

		if (!p->valid)
		{
			zone->getCenter(test_st);
			if (IsPositionValid(map, test_st, p->pos))
			{
				p->valid = true;
				p->best_s = test_st[0];
				p->best_t = test_st[1];
			}
		}

		if (!p->valid)
		{
			const int numnudges = 12;
			vec3_t nudgelist[numnudges] = {{0.1, 0, 0}, {-0.1, 0, 0}, {0, 0.1, 0}, {0, -0.1, 0}, {0.3, 0, 0}, {-0.3, 0, 0}, {0, 0.3, 0}, {0, -0.3, 0}, {0.3, 0.3, 0}, {-0.3, 0.3, 0}, {-0.3, -0.3, 0}, {0.3, -0.3, 0}};

			for (int i = 0; i < numnudges; i++)
			{
				VectorMultiply(nudgelist[i], map->step, test_st);
				VectorAdd(test_st, original_st, test_st);
				snap_to_winding(*zone, map->texplane, test_st);

				if (IsPositionValid(map, test_st, p->pos))
				{
					p->valid = true;
					p->best_s = test_st[0];
					p->best_t = test_st[1];
					break;
				}
			}
		}
	}
	delete zone;
}

void FindFacePositions(int facenum)
// this function must be called after g_face_offset and g_face_centroids and g_edgeshare have been calculated
{
	vec3_t v;
	const vec3_t v_up = {0, 0, 1};
	vec_t texmins[2], texmaxs[2];
	int imins[2], imaxs[2];
	int x;
	int k;

	auto *f = &g_bspfaces[facenum];
	auto *map = &g_face_positions[facenum];
	map->valid = true;
	map->facenum = facenum;
	map->facewinding = nullptr;
	map->facewindingwithoffset = nullptr;
	map->texwinding = nullptr;
	map->grid = nullptr;

	auto *ti = &g_bsptexinfo[f->texinfo];
	if (ti->flags & TEX_SPECIAL)
	{
		map->valid = false;
		return;
	}

	VectorCopy(g_face_offset[facenum], map->face_offset);
	VectorCopy(g_face_centroids[facenum], map->face_centroid);
	TranslateWorldToTex(facenum, map->worldtotex);
	if (!InvertMatrix(map->worldtotex, map->textoworld))
	{
		map->valid = false;
		return;
	}

	map->facewinding = new Winding(*f);
	map->faceplane = *getPlaneFromFace(f);
	map->facewindingwithoffset = new Winding(map->facewinding->m_NumPoints);
	for (x = 0; x < map->facewinding->m_NumPoints; x++)
	{
		VectorAdd(map->facewinding->m_Points[x], map->face_offset, map->facewindingwithoffset->m_Points[x]);
	}
	map->faceplanewithoffset = map->faceplane;
	map->faceplanewithoffset.dist = map->faceplane.dist + DotProduct(map->face_offset, map->faceplane.normal);

	map->texwinding = new Winding(map->facewinding->m_NumPoints);
	for (x = 0; x < map->facewinding->m_NumPoints; x++)
	{
		ApplyMatrix(map->worldtotex, map->facewinding->m_Points[x], map->texwinding->m_Points[x]);
		map->texwinding->m_Points[x][2] = 0.0;
	}
	map->texwinding->RemoveColinearPoints();
	VectorCopy(v_up, map->texplane.normal);
	if (CalcMatrixSign(map->worldtotex) < 0.0)
	{
		map->texplane.normal[2] *= -1;
	}
	map->texplane.dist = 0.0;
	if (map->texwinding->m_NumPoints == 0)
	{
		delete map->facewinding;
		map->facewinding = nullptr;
		delete map->facewindingwithoffset;
		map->facewindingwithoffset = nullptr;
		delete map->texwinding;
		map->texwinding = nullptr;
		map->valid = false;
		return;
	}
	VectorSubtract(map->face_centroid, map->face_offset, v);
	ApplyMatrix(map->worldtotex, v, map->texcentroid);
	map->texcentroid[2] = 0.0;

	for (x = 0; x < map->texwinding->m_NumPoints; x++)
	{
		for (k = 0; k < 2; k++)
		{
			if (x == 0 || map->texwinding->m_Points[x][k] < texmins[k])
				texmins[k] = map->texwinding->m_Points[x][k];
			if (x == 0 || map->texwinding->m_Points[x][k] > texmaxs[k])
				texmaxs[k] = map->texwinding->m_Points[x][k];
		}
	}
	vec_t density = 3.0;
	map->step[0] = (vec_t)TEXTURE_STEP / density;
	map->step[1] = (vec_t)TEXTURE_STEP / density;
	map->step[2] = 1.0;
	for (k = 0; k < 2; k++)
	{
		imins[k] = (int)floor(texmins[k] / map->step[k] + 0.5 - ON_EPSILON);
		imaxs[k] = (int)ceil(texmaxs[k] / map->step[k] - 0.5 + ON_EPSILON);
	}
	map->start[0] = (imins[0] - 0.5) * map->step[0];
	map->start[1] = (imins[1] - 0.5) * map->step[1];
	map->start[2] = 0.0;
	map->w = imaxs[0] - imins[0] + 1;
	map->h = imaxs[1] - imins[1] + 1;
	if (map->w <= 0 || map->h <= 0 || (double)map->w * (double)map->h > 99999999)
	{
		delete map->facewinding;
		map->facewinding = nullptr;
		delete map->facewindingwithoffset;
		map->facewindingwithoffset = nullptr;
		delete map->texwinding;
		map->texwinding = nullptr;
		map->valid = false;
		return;
	}

	map->grid = new position_t[map->w * map->h];
	hlassume(map->grid != nullptr, assume_NoMemory);

	for (int it = 0; it < map->h; it++)
	{
		for (int is = 0; is < map->w; is++)
		{
			CalcSinglePosition(map, is, it);
		}
	}

	return;
}

void FreePositionMaps()
{
	for (int facenum = 0; facenum < g_bspnumfaces; facenum++)
	{
		positionmap_t *map = &g_face_positions[facenum];
		if (map->valid)
		{
			delete map->facewinding;
			map->facewinding = nullptr;
			delete map->facewindingwithoffset;
			map->facewindingwithoffset = nullptr;
			delete map->texwinding;
			map->texwinding = nullptr;
			delete[] map->grid;
			map->grid = nullptr;
			map->valid = false;
		}
	}
}

auto FindNearestPosition(int facenum, const Winding *texwinding, const dplane_t &texplane, vec_t s, vec_t t, vec3_t &pos, vec_t *best_s, vec_t *best_t, vec_t *dist, bool *nudged) -> bool
{
	vec3_t original_st;
	int x;
	int itmin, itmax, ismin, ismax;
	int is;
	int it;
	vec3_t v;
	int best_is;
	int best_it;
	vec_t best_dist;
	bool found;

	auto *map = &g_face_positions[facenum];
	if (!map->valid)
	{
		return false;
	}

	original_st[0] = s;
	original_st[1] = t;
	original_st[2] = 0.0;

	if (point_in_winding(*map->texwinding, map->texplane, original_st, 4 * ON_EPSILON))
	{
		itmin = (int)ceil((original_st[1] - map->start[1] - 2 * ON_EPSILON) / map->step[1]) - 1;
		itmax = (int)floor((original_st[1] - map->start[1] + 2 * ON_EPSILON) / map->step[1]);
		ismin = (int)ceil((original_st[0] - map->start[0] - 2 * ON_EPSILON) / map->step[0]) - 1;
		ismax = (int)floor((original_st[0] - map->start[0] + 2 * ON_EPSILON) / map->step[0]);
		itmin = qmax(0, itmin);
		itmax = qmin(itmax, map->h - 1);
		ismin = qmax(0, ismin);
		ismax = qmin(ismax, map->w - 1);

		found = false;
		bool best_nudged = true;
		for (it = itmin; it <= itmax; it++)
		{
			for (is = ismin; is <= ismax; is++)
			{
				position_t *p;
				vec3_t current_st;
				vec_t d;

				p = &map->grid[is + map->w * it];
				if (!p->valid)
				{
					continue;
				}
				current_st[0] = p->best_s;
				current_st[1] = p->best_t;
				current_st[2] = 0.0;

				VectorSubtract(current_st, original_st, v);
				d = VectorLength(v);

				if (!found ||
					!p->nudged && best_nudged ||
					p->nudged == best_nudged &&
						d < best_dist - 2 * ON_EPSILON)
				{
					found = true;
					best_is = is;
					best_it = it;
					best_dist = d;
					best_nudged = p->nudged;
				}
			}
		}

		if (found)
		{
			auto *p = &map->grid[best_is + map->w * best_it];
			VectorCopy(p->pos, pos);
			*best_s = p->best_s;
			*best_t = p->best_t;
			*dist = 0.0;
			*nudged = p->nudged;
			return true;
		}
	}
	*nudged = true;

	itmin = map->h;
	itmax = -1;
	ismin = map->w;
	ismax = -1;
	for (x = 0; x < texwinding->m_NumPoints; x++)
	{
		it = (int)floor((texwinding->m_Points[x][1] - map->start[1] + 0.5 * ON_EPSILON) / map->step[1]);
		itmin = qmin(itmin, it);
		it = (int)ceil((texwinding->m_Points[x][1] - map->start[1] - 0.5 * ON_EPSILON) / map->step[1]) - 1;
		itmax = qmax(it, itmax);
		is = (int)floor((texwinding->m_Points[x][0] - map->start[0] + 0.5 * ON_EPSILON) / map->step[0]);
		ismin = qmin(ismin, is);
		is = (int)ceil((texwinding->m_Points[x][0] - map->start[0] - 0.5 * ON_EPSILON) / map->step[0]) - 1;
		ismax = qmax(is, ismax);
	}
	itmin = qmax(0, itmin);
	itmax = qmin(itmax, map->h - 1);
	ismin = qmax(0, ismin);
	ismax = qmin(ismax, map->w - 1);

	found = false;
	for (it = itmin; it <= itmax; it++)
	{
		for (is = ismin; is <= ismax; is++)
		{
			vec3_t current_st;

			auto *p = &map->grid[is + map->w * it];
			if (!p->valid)
			{
				continue;
			}
			current_st[0] = p->best_s;
			current_st[1] = p->best_t;
			current_st[2] = 0.0;

			VectorSubtract(current_st, original_st, v);
			vec_t d = VectorLength(v);

			if (!found || d < best_dist - ON_EPSILON)
			{
				found = true;
				best_is = is;
				best_it = it;
				best_dist = d;
			}
		}
	}

	if (found)
	{
		auto *p = &map->grid[best_is + map->w * best_it];
		VectorCopy(p->pos, pos);
		*best_s = p->best_s;
		*best_t = p->best_t;
		*dist = best_dist;
		return true;
	}

	return false;
}
