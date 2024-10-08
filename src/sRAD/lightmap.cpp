#include <cstring>

#include "hlrad.h"
#include "threads.h"
#include "hlassert.h"

EdgeShare g_edgeshare[MAX_MAP_EDGES];
vec3_t g_face_centroids[MAX_MAP_EDGES]; // BUG: should this be [MAX_MAP_FACES]?

// #define TEXTURE_STEP   16.0

// =====================================================================================
//  PairEdges
// =====================================================================================
struct intersecttest_t
{
	int numclipplanes;
	dplane_t *clipplanes;
};
auto TestFaceIntersect(intersecttest_t *t, int facenum) -> bool
{
	auto *f2 = &g_bspfaces[facenum];
	auto *w = new Winding(*f2);
	int k;
	for (k = 0; k < w->m_NumPoints; k++)
	{
		VectorAdd(w->m_Points[k], g_face_offset[facenum], w->m_Points[k]);
	}
	for (k = 0; k < t->numclipplanes; k++)
	{
		if (!w->Clip(t->clipplanes[k], false, ON_EPSILON * 4))
		{
			break;
		}
	}
	auto intersect = w->m_NumPoints > 0;
	delete w;
	return intersect;
}
auto CreateIntersectTest(const dplane_t *p, int facenum) -> intersecttest_t *
{
	auto *f = &g_bspfaces[facenum];
	auto *t = new intersecttest_t;
	hlassume(t != nullptr, assume_NoMemory);
	t->clipplanes = new dplane_t[f->numedges];
	hlassume(t->clipplanes != nullptr, assume_NoMemory);
	t->numclipplanes = 0;
	for (int j = 0; j < f->numedges; j++)
	{
		// should we use winding instead?
		auto edgenum = g_bspsurfedges[f->firstedge + j];
		{
			vec3_t v0, v1;
			vec3_t dir, normal;
			if (edgenum < 0)
			{
				VectorCopy(g_bspvertexes[g_bspedges[-edgenum].v[1]].point, v0);
				VectorCopy(g_bspvertexes[g_bspedges[-edgenum].v[0]].point, v1);
			}
			else
			{
				VectorCopy(g_bspvertexes[g_bspedges[edgenum].v[0]].point, v0);
				VectorCopy(g_bspvertexes[g_bspedges[edgenum].v[1]].point, v1);
			}
			VectorAdd(v0, g_face_offset[facenum], v0);
			VectorAdd(v1, g_face_offset[facenum], v1);
			VectorSubtract(v1, v0, dir);
			CrossProduct(dir, p->normal, normal); // facing inward
			if (!VectorNormalize(normal))
			{
				continue;
			}
			VectorCopy(normal, t->clipplanes[t->numclipplanes].normal);
			t->clipplanes[t->numclipplanes].dist = DotProduct(v0, normal);
			t->numclipplanes++;
		}
	}
	return t;
}
void FreeIntersectTest(intersecttest_t *t)
{
	delete[] t->clipplanes;
	delete t;
}

auto AddFaceForVertexNormal(const int edgeabs, int &edgeabsnext, const int edgeend, int &edgeendnext, BSPLumpFace *const f, BSPLumpFace *&fnext, vec_t &angle, vec3_t &normal) -> int
// Must guarantee these faces will form a loop or a chain, otherwise will result in endless loop.
//
//   e[end]/enext[endnext]
//  *
//  |\.
//  |a\ fnext
//  |  \,
//  | f \.
//  |    \.
//  e   enext
//
{
	VectorCopy(getPlaneFromFace(f)->normal, normal);
	auto vnum = g_bspedges[edgeabs].v[edgeend];
	int i, e, count1, count2;
	int edge, edgenext;
	for (count1 = count2 = 0, i = 0; i < f->numedges; i++)
	{
		e = g_bspsurfedges[f->firstedge + i];
		if (g_bspedges[abs(e)].v[0] == g_bspedges[abs(e)].v[1])
			continue;
		if (abs(e) == edgeabs)
		{
			auto iedge = i;
			edge = e;
			count1++;
		}
		else if (g_bspedges[abs(e)].v[0] == vnum || g_bspedges[abs(e)].v[1] == vnum)
		{
			auto iedgenext = i;
			edgenext = e;
			count2++;
		}
	}
	if (count1 != 1 || count2 != 1)
	{
		return -1;
	}
	vec3_t vec1, vec2;
	auto vnum11 = g_bspedges[abs(edge)].v[edge > 0 ? 0 : 1];
	auto vnum12 = g_bspedges[abs(edge)].v[edge > 0 ? 1 : 0];
	auto vnum21 = g_bspedges[abs(edgenext)].v[edgenext > 0 ? 0 : 1];
	auto vnum22 = g_bspedges[abs(edgenext)].v[edgenext > 0 ? 1 : 0];
	if (vnum == vnum12 && vnum == vnum21 && vnum != vnum11 && vnum != vnum22)
	{
		VectorSubtract(g_bspvertexes[vnum11].point, g_bspvertexes[vnum].point, vec1);
		VectorSubtract(g_bspvertexes[vnum22].point, g_bspvertexes[vnum].point, vec2);
		edgeabsnext = abs(edgenext);
		edgeendnext = edgenext > 0 ? 0 : 1;
	}
	else if (vnum == vnum11 && vnum == vnum22 && vnum != vnum12 && vnum != vnum21)
	{
		VectorSubtract(g_bspvertexes[vnum12].point, g_bspvertexes[vnum].point, vec1);
		VectorSubtract(g_bspvertexes[vnum21].point, g_bspvertexes[vnum].point, vec2);
		edgeabsnext = abs(edgenext);
		edgeendnext = edgenext > 0 ? 1 : 0;
	}
	else
	{
		return -1;
	}
	VectorNormalize(vec1);
	VectorNormalize(vec2);
	auto dot = DotProduct(vec1, vec2);
	dot = dot > 1 ? 1 : dot < -1 ? -1
								 : dot;
	angle = acos(dot);
	auto *es = &g_edgeshare[edgeabsnext];
	if (!(es->faces[0] && es->faces[1]))
		return 1;
	if (es->faces[0] == f && es->faces[1] != f)
		fnext = es->faces[1];
	else if (es->faces[1] == f && es->faces[0] != f)
		fnext = es->faces[0];
	else
	{
		return -1;
	}
	return 0;
}

static auto TranslateTexToTex(int facenum, int edgenum, int facenum2, Matrix &m, Matrix &m_inverse) -> bool
// This function creates a matrix that can translate texture coords in face1 into texture coords in face2.
// It keeps all points in the common edge invariant. For example, if there is a point in the edge, and in the texture of face1, its (s,t)=(16,0), and in face2, its (s,t)=(128,64), then we must let matrix*(16,0,0)=(128,64,0)
{
	Matrix worldtotex;
	Matrix worldtotex2;
	BSPLumpVertex *vert[2];
	vec3_t face_vert[2];
	vec3_t face2_vert[2];
	vec3_t face_axis[2];
	vec3_t face2_axis[2];
	const vec3_t v_up = {0, 0, 1};
	Matrix edgetotex, edgetotex2;
	Matrix inv, inv2;

	TranslateWorldToTex(facenum, worldtotex);
	TranslateWorldToTex(facenum2, worldtotex2);

	auto *e = &g_bspedges[edgenum];
	for (int i = 0; i < 2; i++)
	{
		vert[i] = &g_bspvertexes[e->v[i]];
		ApplyMatrix(worldtotex, vert[i]->point, face_vert[i]);
		face_vert[i][2] = 0; // this value is naturally close to 0 assuming that the edge is on the face plane, but let's make this more explicit.
		ApplyMatrix(worldtotex2, vert[i]->point, face2_vert[i]);
		face2_vert[i][2] = 0;
	}

	VectorSubtract(face_vert[1], face_vert[0], face_axis[0]);
	auto len = VectorLength(face_axis[0]);
	CrossProduct(v_up, face_axis[0], face_axis[1]);
	if (CalcMatrixSign(worldtotex) < 0.0) // the three vectors s, t, facenormal are in reverse order
	{
		VectorInverse(face_axis[1]);
	}

	VectorSubtract(face2_vert[1], face2_vert[0], face2_axis[0]);
	auto len2 = VectorLength(face2_axis[0]);
	CrossProduct(v_up, face2_axis[0], face2_axis[1]);
	if (CalcMatrixSign(worldtotex2) < 0.0)
	{
		VectorInverse(face2_axis[1]);
	}

	VectorCopy(face_axis[0], edgetotex.v[0]); // / v[0][0] v[1][0] \ is a rotation (possibly with a reflection by the edge)
	VectorCopy(face_axis[1], edgetotex.v[1]); // \ v[0][1] v[1][1] /
	VectorScale(v_up, len, edgetotex.v[2]);	  // encode the length into the 3rd value of the matrix
	VectorCopy(face_vert[0], edgetotex.v[3]); // map (0,0) into the origin point

	VectorCopy(face2_axis[0], edgetotex2.v[0]);
	VectorCopy(face2_axis[1], edgetotex2.v[1]);
	VectorScale(v_up, len2, edgetotex2.v[2]);
	VectorCopy(face2_vert[0], edgetotex2.v[3]);

	if (!InvertMatrix(edgetotex, inv) || !InvertMatrix(edgetotex2, inv2))
	{
		return false;
	}
	MultiplyMatrix(edgetotex2, inv, m);
	MultiplyMatrix(edgetotex, inv2, m_inverse);

	return true;
}

void PairEdges()
{
	EdgeShare *e;
	memset(&g_edgeshare, 0, sizeof(g_edgeshare));

	auto *f = g_bspfaces;
	for (int i = 0; i < g_bspnumfaces; i++, f++)
	{
		if (g_bsptexinfo[f->texinfo].flags & TEX_SPECIAL)
		{
			// special textures don't have lightmaps
			continue;
		}
		for (int j = 0; j < f->numedges; j++)
		{
			auto k = g_bspsurfedges[f->firstedge + j];
			if (k < 0)
			{
				e = &g_edgeshare[-k];

				hlassert(e->faces[1] == NULL);
				e->faces[1] = f;
			}
			else
			{
				e = &g_edgeshare[k];

				hlassert(e->faces[0] == NULL);
				e->faces[0] = f;
			}

			if (e->faces[0] && e->faces[1])
			{
				// determine if coplanar
				if (e->faces[0]->planenum == e->faces[1]->planenum && e->faces[0]->side == e->faces[1]->side)
				{
					e->coplanar = true;
					VectorCopy(getPlaneFromFace(e->faces[0])->normal, e->interface_normal);
					e->cos_normals_angle = 1.0;
				}
				else
				{
					// see if they fall into a "smoothing group" based on angle of the normals
					vec3_t normals[2];

					VectorCopy(getPlaneFromFace(e->faces[0])->normal, normals[0]);
					VectorCopy(getPlaneFromFace(e->faces[1])->normal, normals[1]);

					e->cos_normals_angle = DotProduct(normals[0], normals[1]);

					auto m0 = g_bsptexinfo[e->faces[0]->texinfo].miptex;
					auto m1 = g_bsptexinfo[e->faces[1]->texinfo].miptex;
					auto smoothvalue = qmax(g_smoothvalues[m0], g_smoothvalues[m1]);
					if (m0 != m1)
					{
						smoothvalue = qmax(smoothvalue, g_smoothing_threshold_2);
					}
					if (smoothvalue >= 1.0 - NORMAL_EPSILON)
					{
						smoothvalue = 2.0;
					}
					if (e->cos_normals_angle > (1.0 - NORMAL_EPSILON))
					{
						e->coplanar = true;
						VectorCopy(getPlaneFromFace(e->faces[0])->normal, e->interface_normal);
						e->cos_normals_angle = 1.0;
					}
					else if (e->cos_normals_angle >= qmax(smoothvalue - NORMAL_EPSILON, NORMAL_EPSILON))
					{
						{
							VectorAdd(normals[0], normals[1], e->interface_normal);
							VectorNormalize(e->interface_normal);
						}
					}
				}
				if (!VectorCompare(g_translucenttextures[g_bsptexinfo[e->faces[0]->texinfo].miptex], g_translucenttextures[g_bsptexinfo[e->faces[1]->texinfo].miptex]))
				{
					e->coplanar = false;
					VectorClear(e->interface_normal);
				}
				{
					auto miptex0 = g_bsptexinfo[e->faces[0]->texinfo].miptex;
					auto miptex1 = g_bsptexinfo[e->faces[1]->texinfo].miptex;
					if (fabs(g_lightingconeinfo[miptex0][0] - g_lightingconeinfo[miptex1][0]) > NORMAL_EPSILON ||
						fabs(g_lightingconeinfo[miptex0][1] - g_lightingconeinfo[miptex1][1]) > NORMAL_EPSILON)
					{
						e->coplanar = false;
						VectorClear(e->interface_normal);
					}
				}
				if (!VectorCompare(e->interface_normal, vec3_origin))
				{
					e->smooth = true;
				}
				if (e->smooth)
				{
					// compute the matrix in advance
					if (!TranslateTexToTex(e->faces[0] - g_bspfaces, abs(k), e->faces[1] - g_bspfaces, e->textotex[0], e->textotex[1]))
					{
						e->smooth = false;
						e->coplanar = false;
						VectorClear(e->interface_normal);

						auto *dv = &g_bspvertexes[g_bspedges[abs(k)].v[0]];
					}
				}
			}
		}
	}
	{
		int edgeabs, edgeabsnext;
		int edgeend, edgeendnext;
		BSPLumpFace *f, *fnext;
		vec_t angle, angles;
		vec3_t normal, normals;
		vec3_t edgenormal;
		int count;
		for (edgeabs = 0; edgeabs < MAX_MAP_EDGES; edgeabs++)
		{
			e = &g_edgeshare[edgeabs];
			if (!e->smooth)
				continue;
			VectorCopy(e->interface_normal, edgenormal);
			if (g_bspedges[edgeabs].v[0] == g_bspedges[edgeabs].v[1])
			{
				vec3_t errorpos;
				VectorCopy(g_bspvertexes[g_bspedges[edgeabs].v[0]].point, errorpos);
				VectorAdd(errorpos, g_face_offset[e->faces[0] - g_bspfaces], errorpos);
				VectorCopy(edgenormal, e->vertex_normal[0]);
				VectorCopy(edgenormal, e->vertex_normal[1]);
			}
			else
			{
				const dplane_t *p0 = getPlaneFromFace(e->faces[0]);
				const dplane_t *p1 = getPlaneFromFace(e->faces[1]);
				intersecttest_t *test0 = CreateIntersectTest(p0, e->faces[0] - g_bspfaces);
				intersecttest_t *test1 = CreateIntersectTest(p1, e->faces[1] - g_bspfaces);
				for (edgeend = 0; edgeend < 2; edgeend++)
				{
					vec3_t errorpos;
					VectorCopy(g_bspvertexes[g_bspedges[edgeabs].v[edgeend]].point, errorpos);
					VectorAdd(errorpos, g_face_offset[e->faces[0] - g_bspfaces], errorpos);
					angles = 0;
					VectorClear(normals);

					for (int d = 0; d < 2; d++)
					{
						f = e->faces[d];
						count = 0, fnext = f, edgeabsnext = edgeabs, edgeendnext = edgeend;
						while (true)
						{
							auto *fcurrent = fnext;
							auto r = AddFaceForVertexNormal(edgeabsnext, edgeabsnext, edgeendnext, edgeendnext, fcurrent, fnext, angle, normal);
							count++;
							if (r == -1)
							{
								break;
							}
							if (count >= 100)
							{
								break;
							}
							if (DotProduct(normal, p0->normal) <= NORMAL_EPSILON || DotProduct(normal, p1->normal) <= NORMAL_EPSILON)
								break;
							auto m0 = g_bsptexinfo[f->texinfo].miptex;
							auto m1 = g_bsptexinfo[fcurrent->texinfo].miptex;
							auto smoothvalue = qmax(g_smoothvalues[m0], g_smoothvalues[m1]);
							if (m0 != m1)
							{
								smoothvalue = qmax(smoothvalue, g_smoothing_threshold_2);
							}
							if (smoothvalue >= 1.0 - NORMAL_EPSILON)
							{
								smoothvalue = 2.0;
							}
							if (DotProduct(edgenormal, normal) < qmax(smoothvalue - NORMAL_EPSILON, NORMAL_EPSILON))
								break;
							if (fcurrent != e->faces[0] && fcurrent != e->faces[1] &&
								(TestFaceIntersect(test0, fcurrent - g_bspfaces) || TestFaceIntersect(test1, fcurrent - g_bspfaces)))
							{
								break;
							}
							angles += angle;
							VectorMA(normals, angle, normal, normals);
							{
								auto in = false;
								if (fcurrent == e->faces[0] || fcurrent == e->faces[1])
								{
									in = true;
								}
								for (FaceList *l = e->vertex_facelist[edgeend]; l; l = l->next)
								{
									if (fcurrent == l->face)
									{
										in = true;
									}
								}
								if (!in)
								{
									auto *l = new FaceList;
									hlassume(l != nullptr, assume_NoMemory);
									l->face = fcurrent;
									l->next = e->vertex_facelist[edgeend];
									e->vertex_facelist[edgeend] = l;
								}
							}
							if (r != 0 || fnext == f)
								break;
						}
					}

					if (angles < NORMAL_EPSILON)
					{
						VectorCopy(edgenormal, e->vertex_normal[edgeend]);
					}
					else
					{
						VectorNormalize(normals);
						VectorCopy(normals, e->vertex_normal[edgeend]);
					}
				}
				FreeIntersectTest(test0);
				FreeIntersectTest(test1);
			}
			if (e->coplanar)
			{
				if (!VectorCompare(e->vertex_normal[0], e->interface_normal) || !VectorCompare(e->vertex_normal[1], e->interface_normal))
				{
					e->coplanar = false;
				}
			}
		}
	}
}

#define MAX_SINGLEMAP ((MAX_SURFACE_EXTENT + 1) * (MAX_SURFACE_EXTENT + 1)) // #define	MAX_SINGLEMAP	(18*18*4) //--vluzacn

typedef enum
{
	WALLFLAG_NONE = 0,
	WALLFLAG_NUDGED = 0x1,
	WALLFLAG_BLOCKED = 0x2, // this only happens when the entire face and its surroundings are covered by solid or opaque entities
	WALLFLAG_SHADOWED = 0x4,
} wallflag_t;

struct lightinfo_t
{
	vec_t *light;
	vec_t facedist;
	vec3_t facenormal;
	bool translucent_b;
	vec3_t translucent_v;
	int miptex;

	int numsurfpt;
	vec3_t surfpt[MAX_SINGLEMAP];
	vec3_t *surfpt_position; //[MAX_SINGLEMAP] // surfpt_position[] are valid positions for light tracing, while surfpt[] are positions for getting phong normal and doing patch interpolation
	int *surfpt_surface;	 //[MAX_SINGLEMAP] // the face that owns this position
	bool surfpt_lightoutside[MAX_SINGLEMAP];

	vec3_t texorg;
	vec3_t worldtotex[2]; // s = (world - texorg) . worldtotex[0]
	vec3_t textoworld[2]; // world = texorg + s * textoworld[0]
	vec3_t texnormal;

	vec_t exactmins[2], exactmaxs[2];

	int texmins[2], texsize[2];
	int lightstyles[256];
	int surfnum;
	BSPLumpFace *face;
	int lmcache_density; // shared by both s and t direction
	int lmcache_offset;	 // shared by both s and t direction
	int lmcache_side;
	vec3_t (*lmcache)[ALLSTYLES]; // lm: short for lightmap // don't forget to free!
	vec3_t *lmcache_normal;		  // record the phong normals
	int *lmcache_wallflags;		  // wallflag_t
	int lmcachewidth;
	int lmcacheheight;
};

// =====================================================================================
//  TextureNameFromFace
// =====================================================================================
static auto TextureNameFromFace(const BSPLumpFace *const f) -> const char *
{
	//
	// check for light emited by texture
	//
	auto *tx = &g_bsptexinfo[f->texinfo];
	auto ofs = ((BSPLumpMiptexHeader *)g_bsptexdata)->dataofs[tx->miptex];
	auto *mt = (BSPLumpMiptex *)((byte *)g_bsptexdata + ofs);

	return mt->name;
}

// =====================================================================================
//  CalcFaceExtents
//      Fills in s->texmins[] and s->texsize[]
//      also sets exactmins[] and exactmaxs[]
// =====================================================================================
static void CalcFaceExtents(lightinfo_t *l)
{
	const auto facenum = l->surfnum;
	float mins[2], maxs[2]; // vec_t           mins[2], maxs[2], val; //vluzacn
	int i, e;
	BSPLumpVertex *v;

	auto *s = l->face;

	mins[0] = mins[1] = 99999999;
	maxs[0] = maxs[1] = -99999999;

	auto *tex = &g_bsptexinfo[s->texinfo];

	for (i = 0; i < s->numedges; i++)
	{
		e = g_bspsurfedges[s->firstedge + i];
		if (e >= 0)
		{
			v = g_bspvertexes + g_bspedges[e].v[0];
		}
		else
		{
			v = g_bspvertexes + g_bspedges[-e].v[1];
		}

		for (int j = 0; j < 2; j++)
		{
			auto val = v->point[0] * tex->vecs[j][0] +
					   v->point[1] * tex->vecs[j][1] + v->point[2] * tex->vecs[j][2] + tex->vecs[j][3];
			if (val < mins[j])
			{
				mins[j] = val;
			}
			if (val > maxs[j])
			{
				maxs[j] = val;
			}
		}
	}

	for (i = 0; i < 2; i++)
	{
		l->exactmins[i] = mins[i];
		l->exactmaxs[i] = maxs[i];
	}
	int bmins[2];
	int bmaxs[2];
	GetFaceExtents(l->surfnum, bmins, bmaxs);
	for (i = 0; i < 2; i++)
	{
		mins[i] = bmins[i];
		maxs[i] = bmaxs[i];
		l->texmins[i] = bmins[i];
		l->texsize[i] = bmaxs[i] - bmins[i];
	}

	if (!(tex->flags & TEX_SPECIAL))
	{
		if ((l->texsize[0] > MAX_SURFACE_EXTENT) || (l->texsize[1] > MAX_SURFACE_EXTENT) || l->texsize[0] < 0 || l->texsize[1] < 0 //--vluzacn
		)
		{
			ThreadLock();
			PrintOnce("\nfor Face %li (texture %s) at ", s - g_bspfaces, TextureNameFromFace(s));

			for (i = 0; i < s->numedges; i++)
			{
				e = g_bspsurfedges[s->firstedge + i];
				if (e >= 0)
				{
					v = g_bspvertexes + g_bspedges[e].v[0];
				}
				else
				{
					v = g_bspvertexes + g_bspedges[-e].v[1];
				}
				vec3_t pos;
				VectorAdd(v->point, g_face_offset[facenum], pos);
				Log("(%4.3f %4.3f %4.3f) ", pos[0], pos[1], pos[2]);
			}
			Log("\n");

			Error("Bad surface extents (%d x %d)\nCheck the file ZHLTProblems.html for a detailed explanation of this problem", l->texsize[0], l->texsize[1]);
		}
	}
	// allocate sample light cache
	{
		if (g_extra)
		{
			l->lmcache_density = 3;
		}
		else
		{
			l->lmcache_density = 1;
		}
		l->lmcache_side = (int)ceil((0.5 * g_blur * l->lmcache_density - 0.5) * (1 - NORMAL_EPSILON));
		l->lmcache_offset = l->lmcache_side;
		l->lmcachewidth = l->texsize[0] * l->lmcache_density + 1 + 2 * l->lmcache_side;
		l->lmcacheheight = l->texsize[1] * l->lmcache_density + 1 + 2 * l->lmcache_side;
		l->lmcache = new vec3_t[l->lmcachewidth * l->lmcacheheight][ALLSTYLES];
		hlassume(l->lmcache != nullptr, assume_NoMemory);
		l->lmcache_normal = new vec3_t[l->lmcachewidth * l->lmcacheheight];
		hlassume(l->lmcache_normal != nullptr, assume_NoMemory);
		l->lmcache_wallflags = new int[l->lmcachewidth * l->lmcacheheight];
		hlassume(l->lmcache_wallflags != nullptr, assume_NoMemory);
		l->surfpt_position = new vec3_t[MAX_SINGLEMAP];
		l->surfpt_surface = new int[MAX_SINGLEMAP];
		hlassume(l->surfpt_position != nullptr && l->surfpt_surface != nullptr, assume_NoMemory);
	}
}

// =====================================================================================
//  CalcFaceVectors
//      Fills in texorg, worldtotex. and textoworld
// =====================================================================================
static void CalcFaceVectors(lightinfo_t *l)
{
	int i;
	vec3_t texnormal;

	auto *tex = &g_bsptexinfo[l->face->texinfo];

	// convert from float to double
	for (i = 0; i < 2; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			l->worldtotex[i][j] = tex->vecs[i][j];
		}
	}

	// calculate a normal to the texture axis.  points can be moved along this
	// without changing their S/T
	CrossProduct(tex->vecs[1], tex->vecs[0], texnormal);
	VectorNormalize(texnormal);

	// flip it towards plane normal
	auto distscale = DotProduct(texnormal, l->facenormal);
	if (distscale == 0.0)
	{
		const unsigned facenum = l->face - g_bspfaces;

		ThreadLock();
		Log("Malformed face (%d) normal @ \n", facenum);
		auto *w = new Winding(*l->face);
		{
			const unsigned numpoints = w->m_NumPoints;
			unsigned x;
			for (x = 0; x < numpoints; x++)
			{
				VectorAdd(w->m_Points[x], g_face_offset[facenum], w->m_Points[x]);
			}
		}
		w->Print();
		delete w;
		ThreadUnlock();

		hlassume(false, assume_MalformedTextureFace);
	}

	if (distscale < 0)
	{
		distscale = -distscale;
		VectorSubtract(vec3_origin, texnormal, texnormal);
	}

	// distscale is the ratio of the distance along the texture normal to
	// the distance along the plane normal
	distscale = 1.0 / distscale;

	for (i = 0; i < 2; i++)
	{
		CrossProduct(l->worldtotex[!i], l->facenormal, l->textoworld[i]);
		auto len = DotProduct(l->textoworld[i], l->worldtotex[i]);
		VectorScale(l->textoworld[i], 1 / len, l->textoworld[i]);
	}

	// calculate texorg on the texture plane
	for (i = 0; i < 3; i++)
	{
		l->texorg[i] = -tex->vecs[0][3] * l->textoworld[0][i] - tex->vecs[1][3] * l->textoworld[1][i];
	}

	// project back to the face plane
	auto dist = DotProduct(l->texorg, l->facenormal) - l->facedist;
	dist *= distscale;
	VectorMA(l->texorg, -dist, texnormal, l->texorg);
	VectorCopy(texnormal, l->texnormal);
}

// =====================================================================================
//  SetSurfFromST
// =====================================================================================
static void SetSurfFromST(const lightinfo_t *const l, vec_t *surf, const vec_t s, const vec_t t)
{
	const auto facenum = l->surfnum;
	for (int j = 0; j < 3; j++)
	{
		surf[j] = l->texorg[j] + l->textoworld[0][j] * s + l->textoworld[1][j] * t;
	}

	// Adjust for origin-based models
	VectorAdd(surf, g_face_offset[facenum], surf);
}

typedef enum
{
	LightOutside,		// Not lit
	LightShifted,		// used HuntForWorld on 100% dark face
	LightShiftedInside, // moved to neighbhor on 2nd cleanup pass
	LightNormal,		// Normally lit with no movement
	LightPulledInside,	// Pulled inside by bleed code adjustments
	LightSimpleNudge,	// A simple nudge 1/3 or 2/3 towards center along S or T axist
} light_flag_t;

// =====================================================================================
//  CalcPoints
//      For each texture aligned grid point, back project onto the plane
//      to get the world xyz value of the sample point
// =====================================================================================
static void SetSTFromSurf(const lightinfo_t *const l, const vec_t *surf, vec_t &s, vec_t &t)
{
	const auto facenum = l->surfnum;
	s = t = 0;
	for (int j = 0; j < 3; j++)
	{
		s += (surf[j] - g_face_offset[facenum][j] - l->texorg[j]) * l->worldtotex[0][j];
		t += (surf[j] - g_face_offset[facenum][j] - l->texorg[j]) * l->worldtotex[1][j];
	}
}

struct samplefragedge_t
{
	int edgenum; // g_bspedges index
	int edgeside;
	int nextfacenum; // where to grow
	bool tried;

	vec3_t point1;	  // start point
	vec3_t point2;	  // end point
	vec3_t direction; // normalized; from point1 to point2

	bool noseam;
	vec_t distance; // distance from origin
	vec_t distancereduction;
	vec_t flippedangle;

	vec_t ratio; // if ratio != 1, seam is unavoidable
	Matrix prevtonext;
	Matrix nexttoprev;
};

struct samplefragrect_t
{
	dplane_t planes[4];
};

struct samplefrag_t
{
	samplefrag_t *next;		  // since this is a node in a list
	samplefrag_t *parentfrag; // where it grew from
	samplefragedge_t *parentedge;
	int facenum; // facenum

	vec_t flippedangle; // copied from parent edge
	bool noseam;		// copied from parent edge

	Matrix coordtomycoord; // v[2][2] > 0, v[2][0] = v[2][1] = v[0][2] = v[1][2] = 0.0
	Matrix mycoordtocoord;

	vec3_t origin;			 // original s,t
	vec3_t myorigin;		 // relative to the texture coordinate on that face
	samplefragrect_t rect;	 // original rectangle that forms the boundary
	samplefragrect_t myrect; // relative to the texture coordinate on that face

	Winding *winding;	   // a fragment of the original rectangle in the texture coordinate plane; windings of different frags should not overlap
	dplane_t windingplane; // normal = (0,0,1) or (0,0,-1); if this normal is wrong, point_in_winding() will never return true
	Winding *mywinding;	   // relative to the texture coordinate on that face
	dplane_t mywindingplane;

	int numedges;			 // # of candicates for the next growth
	samplefragedge_t *edges; // candicates for the next growth
};

struct samplefraginfo_t
{
	int maxsize;
	int size;
	samplefrag_t *head;
};

void ChopFrag(samplefrag_t *frag)
// fill winding, windingplane, mywinding, mywindingplane, numedges, edges
{
	// get the shape of the fragment by clipping the face using the boundaries
	Matrix worldtotex;
	const vec3_t v_up = {0, 0, 1};

	auto *f = &g_bspfaces[frag->facenum];
	auto *facewinding = new Winding(*f);

	TranslateWorldToTex(frag->facenum, worldtotex);
	frag->mywinding = new Winding(facewinding->m_NumPoints);
	for (int x = 0; x < facewinding->m_NumPoints; x++)
	{
		ApplyMatrix(worldtotex, facewinding->m_Points[x], frag->mywinding->m_Points[x]);
		frag->mywinding->m_Points[x][2] = 0.0;
	}
	frag->mywinding->RemoveColinearPoints();
	VectorCopy(v_up, frag->mywindingplane.normal); // this is the same as applying the worldtotex matrix to the faceplane
	if (CalcMatrixSign(worldtotex) < 0.0)
	{
		frag->mywindingplane.normal[2] *= -1;
	}
	frag->mywindingplane.dist = 0.0;

	for (int x = 0; x < 4 && frag->mywinding->m_NumPoints > 0; x++)
	{
		frag->mywinding->Clip(frag->myrect.planes[x], false);
	}

	frag->winding = new Winding(frag->mywinding->m_NumPoints);
	for (int x = 0; x < frag->mywinding->m_NumPoints; x++)
	{
		ApplyMatrix(frag->mycoordtocoord, frag->mywinding->m_Points[x], frag->winding->m_Points[x]);
	}
	frag->winding->RemoveColinearPoints();
	VectorCopy(frag->mywindingplane.normal, frag->windingplane.normal);
	if (CalcMatrixSign(frag->mycoordtocoord) < 0.0)
	{
		frag->windingplane.normal[2] *= -1;
	}
	frag->windingplane.dist = 0.0;

	delete facewinding;

	// find the edges where the fragment can grow in the future
	frag->numedges = 0;
	frag->edges = new samplefragedge_t[f->numedges];
	hlassume(frag->edges != nullptr, assume_NoMemory);
	for (int i = 0; i < f->numedges; i++)
	{
		vec_t frac1, frac2;
		vec3_t tmp, v, normal;

		auto *e = &frag->edges[frag->numedges];

		// some basic info
		e->edgenum = abs(g_bspsurfedges[f->firstedge + i]);
		e->edgeside = (g_bspsurfedges[f->firstedge + i] < 0 ? 1 : 0);
		auto *es = &g_edgeshare[e->edgenum];
		if (!es->smooth)
		{
			continue;
		}
		if (es->faces[e->edgeside] - g_bspfaces != frag->facenum)
		{
			Error("internal error 1 in GrowSingleSampleFrag");
		}
		const auto *m = &es->textotex[e->edgeside];
		const auto *m_inverse = &es->textotex[1 - e->edgeside];
		e->nextfacenum = es->faces[1 - e->edgeside] - g_bspfaces;
		if (e->nextfacenum == frag->facenum)
		{
			continue; // an invalid edge (usually very short)
		}
		e->tried = false; // because the frag hasn't been linked into the list yet

		// translate the edge points from world to the texture plane of the original frag
		//   so the distances are able to be compared among edges from different frags
		auto *de = &g_bspedges[e->edgenum];
		auto *dv1 = &g_bspvertexes[de->v[e->edgeside]];
		auto *dv2 = &g_bspvertexes[de->v[1 - e->edgeside]];
		ApplyMatrix(worldtotex, dv1->point, tmp);
		ApplyMatrix(frag->mycoordtocoord, tmp, e->point1);
		e->point1[2] = 0.0;
		ApplyMatrix(worldtotex, dv2->point, tmp);
		ApplyMatrix(frag->mycoordtocoord, tmp, e->point2);
		e->point2[2] = 0.0;
		VectorSubtract(e->point2, e->point1, e->direction);
		auto edgelen = VectorNormalize(e->direction);
		if (edgelen <= ON_EPSILON)
		{
			continue;
		}

		// clip the edge
		frac1 = 0;
		frac2 = 1;
		for (int x = 0; x < 4; x++)
		{

			auto dot1 = DotProduct(e->point1, frag->rect.planes[x].normal) - frag->rect.planes[x].dist;
			auto dot2 = DotProduct(e->point2, frag->rect.planes[x].normal) - frag->rect.planes[x].dist;
			if (dot1 <= ON_EPSILON && dot2 <= ON_EPSILON)
			{
				frac1 = 1;
				frac2 = 0;
			}
			else if (dot1 < 0)
			{
				frac1 = qmax(frac1, dot1 / (dot1 - dot2));
			}
			else if (dot2 < 0)
			{
				frac2 = qmin(frac2, dot1 / (dot1 - dot2));
			}
		}
		if (edgelen * (frac2 - frac1) <= ON_EPSILON)
		{
			continue;
		}
		VectorMA(e->point1, edgelen * frac2, e->direction, e->point2);
		VectorMA(e->point1, edgelen * frac1, e->direction, e->point1);

		// calculate the distance, etc., which are used to determine its priority
		e->noseam = frag->noseam;
		auto dot = DotProduct(frag->origin, e->direction);
		auto dot1 = DotProduct(e->point1, e->direction);
		auto dot2 = DotProduct(e->point2, e->direction);
		dot = qmax(dot1, qmin(dot, dot2));
		VectorMA(e->point1, dot - dot1, e->direction, v);
		VectorSubtract(v, frag->origin, v);
		e->distance = VectorLength(v);
		CrossProduct(e->direction, frag->windingplane.normal, normal);
		VectorNormalize(normal); // points inward
		e->distancereduction = DotProduct(v, normal);
		e->flippedangle = frag->flippedangle + acos(qmin(es->cos_normals_angle, 1.0));

		// calculate the matrix
		e->ratio = (*m_inverse).v[2][2];
		if (e->ratio <= NORMAL_EPSILON || (1 / e->ratio) <= NORMAL_EPSILON)
		{
			continue;
		}

		if (fabs(e->ratio - 1) < 0.005)
		{
			e->prevtonext = *m;
			e->nexttoprev = *m_inverse;
		}
		else
		{
			e->noseam = false;
			e->prevtonext = *m;
			e->nexttoprev = *m_inverse;
		}

		frag->numedges++;
	}
}

static auto GrowSingleFrag(const samplefraginfo_t *info, samplefrag_t *parent, samplefragedge_t *edge) -> samplefrag_t *
{
	auto *frag = new samplefrag_t;
	hlassume(frag != nullptr, assume_NoMemory);

	// some basic info
	frag->next = nullptr;
	frag->parentfrag = parent;
	frag->parentedge = edge;
	frag->facenum = edge->nextfacenum;

	frag->flippedangle = edge->flippedangle;
	frag->noseam = edge->noseam;

	// calculate the matrix
	MultiplyMatrix(edge->prevtonext, parent->coordtomycoord, frag->coordtomycoord);
	MultiplyMatrix(parent->mycoordtocoord, edge->nexttoprev, frag->mycoordtocoord);

	// fill in origin
	VectorCopy(parent->origin, frag->origin);
	ApplyMatrix(frag->coordtomycoord, frag->origin, frag->myorigin);

	// fill in boundaries
	frag->rect = parent->rect;
	for (int x = 0; x < 4; x++)
	{
		// since a plane's parameters are in the dual coordinate space, we translate the original absolute plane into this relative plane by multiplying the inverse matrix
		ApplyMatrixOnPlane(frag->mycoordtocoord, frag->rect.planes[x].normal, frag->rect.planes[x].dist, frag->myrect.planes[x].normal, frag->myrect.planes[x].dist);
		auto len = VectorLength(frag->myrect.planes[x].normal);
		if (!len)
		{
			delete frag;
			return nullptr;
		}
		VectorScale(frag->myrect.planes[x].normal, 1 / len, frag->myrect.planes[x].normal);
		frag->myrect.planes[x].dist /= len;
	}

	// chop windings and edges
	ChopFrag(frag);

	if (frag->winding->m_NumPoints == 0 || frag->mywinding->m_NumPoints == 0)
	{
		// empty
		delete frag->mywinding;
		delete frag->winding;
		delete[] frag->edges;
		delete frag;
		return nullptr;
	}

	// do overlap test

	auto overlap = false;
	auto *clipplanes = new dplane_t[frag->winding->m_NumPoints];
	hlassume(clipplanes != nullptr, assume_NoMemory);
	auto numclipplanes = 0;
	for (int x = 0; x < frag->winding->m_NumPoints; x++)
	{
		vec3_t v;
		VectorSubtract(frag->winding->m_Points[(x + 1) % frag->winding->m_NumPoints], frag->winding->m_Points[x], v);
		CrossProduct(v, frag->windingplane.normal, clipplanes[numclipplanes].normal);
		if (!VectorNormalize(clipplanes[numclipplanes].normal))
		{
			continue;
		}
		clipplanes[numclipplanes].dist = DotProduct(frag->winding->m_Points[x], clipplanes[numclipplanes].normal);
		numclipplanes++;
	}
	for (samplefrag_t *f2 = info->head; f2 && !overlap; f2 = f2->next)
	{
		auto *w = new Winding(*f2->winding);
		for (int x = 0; x < numclipplanes && w->m_NumPoints > 0; x++)
		{
			w->Clip(clipplanes[x], false, 4 * ON_EPSILON);
		}
		if (w->m_NumPoints > 0)
		{
			overlap = true;
		}
		delete w;
	}
	delete[] clipplanes;
	if (overlap)
	{
		// in the original texture plane, this fragment overlaps with some existing fragments
		delete frag->mywinding;
		delete frag->winding;
		delete[] frag->edges;
		delete frag;
		return nullptr;
	}

	return frag;
}

static auto FindBestEdge(samplefraginfo_t *info, samplefrag_t *&bestfrag, samplefragedge_t *&bestedge) -> bool
{
	auto found = false;
	for (auto *f = info->head; f; f = f->next)
	{
		for (auto *e = f->edges; e < f->edges + f->numedges; e++)
		{
			if (e->tried)
			{
				continue;
			}

			bool better;

			if (!found)
			{
				better = true;
			}
			else if ((e->flippedangle < Q_PI + NORMAL_EPSILON) != (bestedge->flippedangle < Q_PI + NORMAL_EPSILON))
			{
				better = ((e->flippedangle < Q_PI + NORMAL_EPSILON) && !(bestedge->flippedangle < Q_PI + NORMAL_EPSILON));
			}
			else if (e->noseam != bestedge->noseam)
			{
				better = (e->noseam && !bestedge->noseam);
			}
			else if (fabs(e->distance - bestedge->distance) > ON_EPSILON)
			{
				better = (e->distance < bestedge->distance);
			}
			else if (fabs(e->distancereduction - bestedge->distancereduction) > ON_EPSILON)
			{
				better = (e->distancereduction > bestedge->distancereduction);
			}
			else
			{
				better = e->edgenum < bestedge->edgenum;
			}

			if (better)
			{
				found = true;
				bestfrag = f;
				bestedge = e;
			}
		}
	}

	return found;
}

static auto CreateSampleFrag(int facenum, vec_t s, vec_t t,
							 const vec_t square[2][2],
							 int maxsize) -> samplefraginfo_t *
{
	const vec3_t v_s = {1, 0, 0};
	const vec3_t v_t = {0, 1, 0};

	auto *info = new samplefraginfo_t;
	hlassume(info != nullptr, assume_NoMemory);
	info->maxsize = maxsize;
	info->size = 1;
	info->head = new samplefrag_t;
	hlassume(info->head != nullptr, assume_NoMemory);

	info->head->next = nullptr;
	info->head->parentfrag = nullptr;
	info->head->parentedge = nullptr;
	info->head->facenum = facenum;

	info->head->flippedangle = 0.0;
	info->head->noseam = true;

	MatrixForScale(vec3_origin, 1.0, info->head->coordtomycoord);
	MatrixForScale(vec3_origin, 1.0, info->head->mycoordtocoord);

	info->head->origin[0] = s;
	info->head->origin[1] = t;
	info->head->origin[2] = 0.0;
	VectorCopy(info->head->origin, info->head->myorigin);

	VectorScale(v_s, 1, info->head->rect.planes[0].normal);
	info->head->rect.planes[0].dist = square[0][0]; // smin
	VectorScale(v_s, -1, info->head->rect.planes[1].normal);
	info->head->rect.planes[1].dist = -square[1][0]; // smax
	VectorScale(v_t, 1, info->head->rect.planes[2].normal);
	info->head->rect.planes[2].dist = square[0][1]; // tmin
	VectorScale(v_t, -1, info->head->rect.planes[3].normal);
	info->head->rect.planes[3].dist = -square[1][1]; // tmax
	info->head->myrect = info->head->rect;

	ChopFrag(info->head);

	if (info->head->winding->m_NumPoints == 0 || info->head->mywinding->m_NumPoints == 0)
	{
		// empty
		delete info->head->mywinding;
		delete info->head->winding;
		delete[] info->head->edges;
		delete info->head;
		info->head = nullptr;
		info->size = 0;
	}
	else
	{
		// prune edges
		for (samplefragedge_t *e = info->head->edges; e < info->head->edges + info->head->numedges; e++)
		{
			if (e->nextfacenum == info->head->facenum)
			{
				e->tried = true;
			}
		}
	}

	while (info->size < info->maxsize)
	{
		samplefrag_t *bestfrag;
		samplefragedge_t *bestedge;

		if (!FindBestEdge(info, bestfrag, bestedge))
		{
			break;
		}

		auto *newfrag = GrowSingleFrag(info, bestfrag, bestedge);
		bestedge->tried = true;

		if (newfrag)
		{
			newfrag->next = info->head;
			info->head = newfrag;
			info->size++;

			for (samplefrag_t *f = info->head; f; f = f->next)
			{
				for (samplefragedge_t *e = newfrag->edges; e < newfrag->edges + newfrag->numedges; e++)
				{
					if (e->nextfacenum == f->facenum)
					{
						e->tried = true;
					}
				}
			}
			for (samplefrag_t *f = info->head; f; f = f->next)
			{
				for (samplefragedge_t *e = f->edges; e < f->edges + f->numedges; e++)
				{
					if (e->nextfacenum == newfrag->facenum)
					{
						e->tried = true;
					}
				}
			}
		}
	}

	return info;
}

static auto IsFragEmpty(samplefraginfo_t *fraginfo) -> bool
{
	return (fraginfo->size == 0);
}

static void DeleteSampleFrag(samplefraginfo_t *fraginfo)
{
	while (fraginfo->head)
	{
		auto *f = fraginfo->head;
		fraginfo->head = f->next;
		delete f->mywinding;
		delete f->winding;
		delete[] f->edges;
		delete f;
	}
	delete fraginfo;
}

static auto SetSampleFromST(vec_t *const point,
							vec_t *const position, // a valid world position for light tracing
							int *const surface,	   // the face used for phong normal and patch interpolation
							bool *nudged,
							const lightinfo_t *const l, const vec_t original_s, const vec_t original_t,
							const vec_t square[2][2], // {smin, tmin}, {smax, tmax}
							eModelLightmodes lightmode) -> light_flag_t
{
	light_flag_t LuxelFlag;

	auto facenum = l->surfnum;
	auto *face = l->face;
	const auto *faceplane = getPlaneFromFace(face);

	auto *fraginfo = CreateSampleFrag(facenum, original_s, original_t,
									  square,
									  100);

	samplefrag_t *bestfrag;
	vec3_t bestpos;
	vec_t bests, bestt;
	vec_t best_dist;
	bool best_nudged;

	auto found = false;
	for (auto *f = fraginfo->head; f; f = f->next)
	{
		vec3_t pos;
		vec_t s, t;
		vec_t dist;

		bool nudged_one;
		if (!FindNearestPosition(f->facenum, f->mywinding, f->mywindingplane, f->myorigin[0], f->myorigin[1], pos, &s, &t, &dist, &nudged_one))
		{
			continue;
		}

		bool better;

		if (!found)
		{
			better = true;
		}
		else if (nudged_one != best_nudged)
		{
			better = !nudged_one;
		}
		else if (fabs(dist - best_dist) > 2 * ON_EPSILON)
		{
			better = (dist < best_dist);
		}
		else if (f->noseam != bestfrag->noseam)
		{
			better = (f->noseam && !bestfrag->noseam);
		}
		else
		{
			better = (f->facenum < bestfrag->facenum);
		}

		if (better)
		{
			found = true;
			bestfrag = f;
			VectorCopy(pos, bestpos);
			bests = s;
			bestt = t;
			best_dist = dist;
			best_nudged = nudged_one;
		}
	}

	if (found)
	{
		Matrix worldtotex, textoworld;
		vec3_t tex;

		TranslateWorldToTex(bestfrag->facenum, worldtotex);
		if (!InvertMatrix(worldtotex, textoworld))
		{
			const unsigned facenum = bestfrag->facenum;
			ThreadLock();
			Log("Malformed face (%d) normal @ \n", facenum);
			auto *w = new Winding(g_bspfaces[facenum]);
			for (int x = 0; x < w->m_NumPoints; x++)
			{
				VectorAdd(w->m_Points[x], g_face_offset[facenum], w->m_Points[x]);
			}
			w->Print();
			delete w;
			ThreadUnlock();
			hlassume(false, assume_MalformedTextureFace);
		}

		// point
		tex[0] = bests;
		tex[1] = bestt;
		tex[2] = 0.0;
		{
			vec3_t v;
			ApplyMatrix(textoworld, tex, v);
			VectorCopy(v, point);
		}
		VectorAdd(point, g_face_offset[bestfrag->facenum], point);
		// position
		VectorCopy(bestpos, position);
		// surface
		*surface = bestfrag->facenum;
		// whether nudged to fit
		*nudged = best_nudged;
		// returned value
		LuxelFlag = LightNormal;
	}
	else
	{
		SetSurfFromST(l, point, original_s, original_t);
		VectorMA(point, DEFAULT_HUNT_OFFSET, faceplane->normal, position);
		*surface = facenum;
		*nudged = true;
		LuxelFlag = LightOutside;
	}

	DeleteSampleFrag(fraginfo);

	return LuxelFlag;
}
static void CalcPoints(lightinfo_t *l)
{
	const auto facenum = l->surfnum;
	const auto lightmode = g_face_lightmode[facenum];
	const auto h = l->texsize[1] + 1;
	const auto w = l->texsize[0] + 1;
	const auto starts = l->texmins[0] * TEXTURE_STEP;
	const auto startt = l->texmins[1] * TEXTURE_STEP;
	light_flag_t LuxelFlags[MAX_SINGLEMAP];
	light_flag_t *pLuxelFlags;
	vec_t *surf;
	int s, t;
	l->numsurfpt = w * h;
	for (t = 0; t < h; t++)
	{
		for (s = 0; s < w; s++)
		{
			surf = l->surfpt[s + w * t];
			pLuxelFlags = &LuxelFlags[s + w * t];
			auto us = starts + s * TEXTURE_STEP;
			auto ut = startt + t * TEXTURE_STEP;
			vec_t square[2][2];
			square[0][0] = us - TEXTURE_STEP;
			square[0][1] = ut - TEXTURE_STEP;
			square[1][0] = us + TEXTURE_STEP;
			square[1][1] = ut + TEXTURE_STEP;
			bool nudged;
			*pLuxelFlags = SetSampleFromST(surf,
										   l->surfpt_position[s + w * t], &l->surfpt_surface[s + w * t],
										   &nudged,
										   l, us, ut,
										   square,
										   lightmode);
		}
	}
	{
		int s_other, t_other;
		for (int i = 0; i < h + w; i++)
		{ // propagate valid light samples
			auto adjusted = false;
			for (t = 0; t < h; t++)
			{
				for (s = 0; s < w; s++)
				{
					surf = l->surfpt[s + w * t];
					pLuxelFlags = &LuxelFlags[s + w * t];
					if (*pLuxelFlags != LightOutside)
						continue;
					for (int n = 0; n < 4; n++)
					{
						switch (n)
						{
						case 0:
							s_other = s + 1;
							t_other = t;
							break;
						case 1:
							s_other = s - 1;
							t_other = t;
							break;
						case 2:
							s_other = s;
							t_other = t + 1;
							break;
						case 3:
							s_other = s;
							t_other = t - 1;
							break;
						}
						if (t_other < 0 || t_other >= h || s_other < 0 || s_other >= w)
							continue;
						auto *surf_other = l->surfpt[s_other + w * t_other];
						auto *pLuxelFlags_other = &LuxelFlags[s_other + w * t_other];
						if (*pLuxelFlags_other != LightOutside && *pLuxelFlags_other != LightShifted)
						{
							*pLuxelFlags = LightShifted;
							VectorCopy(surf_other, surf);
							VectorCopy(l->surfpt_position[s_other + w * t_other], l->surfpt_position[s + w * t]);
							l->surfpt_surface[s + w * t] = l->surfpt_surface[s_other + w * t_other];
							adjusted = true;
							break;
						}
					}
				}
			}
			for (t = 0; t < h; t++)
			{
				for (s = 0; s < w; s++)
				{
					pLuxelFlags = &LuxelFlags[s + w * t];
					if (*pLuxelFlags == LightShifted)
					{
						*pLuxelFlags = LightShiftedInside;
					}
				}
			}
			if (!adjusted)
				break;
		}
	}
	for (int i = 0; i < MAX_SINGLEMAP; i++)
	{
		l->surfpt_lightoutside[i] = (LuxelFlags[i] == LightOutside);
	}
}

//==============================================================

struct sample_t
{
	vec3_t pos;
	vec3_t light;
	int surface; // this sample can grow into another face
};

struct facelight_t
{
	int numsamples;
	sample_t *samples[MAXLIGHTMAPS];
};

static DirectLight *directlights[MAX_MAP_LEAFS];
static facelight_t facelight[MAX_MAP_FACES];
static int numdlights;

// =====================================================================================
//  CreateDirectLights
// =====================================================================================
void CreateDirectLights()
{
	unsigned i;
	Patch *p;
	DirectLight *dl;
	BSPLumpLeaf *leaf;
	int leafnum;
	Entity *e;
	const char *target;
	vec3_t dest;

	numdlights = 0;
	int styleused[ALLSTYLES];
	memset(styleused, 0, ALLSTYLES * sizeof(styleused[0]));
	styleused[0] = true;
	auto numstyles = 1;

	//
	// surfaces
	//
	for (i = 0, p = g_patches; i < g_num_patches; i++, p++)
	{
		if (p->emitstyle >= 0 && p->emitstyle < ALLSTYLES)
		{
			if (styleused[p->emitstyle] == false)
			{
				styleused[p->emitstyle] = true;
				numstyles++;
			}
		}
		if (
			DotProduct(p->baselight, p->texturereflectivity) / 3 > 0.0 && !(g_face_texlights[p->faceNumber] && *ValueForKey(g_face_texlights[p->faceNumber], "_scale") && FloatForKey(g_face_texlights[p->faceNumber], "_scale") <= 0)) // LRC
		{
			numdlights++;
			dl = (DirectLight *)calloc(1, sizeof(DirectLight));

			hlassume(dl != nullptr, assume_NoMemory);

			VectorCopy(p->origin, dl->origin);

			leaf = PointInLeaf(dl->origin);
			leafnum = leaf - g_bspleafs;

			dl->next = directlights[leafnum];
			directlights[leafnum] = dl;
			dl->style = p->emitstyle; // LRC
			dl->topatch = false;
			if (!p->emitmode)
			{
				dl->topatch = true;
			}
			dl->patch_area = p->area;
			dl->patch_emitter_range = p->emitter_range;
			dl->patch = p;
			dl->texlightgap = 0.0; // DEFAULT_TEXLIGHTGAP
			if (g_face_texlights[p->faceNumber] && *ValueForKey(g_face_texlights[p->faceNumber], "_texlightgap"))
			{
				dl->texlightgap = FloatForKey(g_face_texlights[p->faceNumber], "_texlightgap");
			}
			dl->stopdot = 0.0;
			dl->stopdot2 = 0.0;
			if (g_face_texlights[p->faceNumber])
			{
				if (*ValueForKey(g_face_texlights[p->faceNumber], "_cone"))
				{
					dl->stopdot = FloatForKey(g_face_texlights[p->faceNumber], "_cone");
					dl->stopdot = dl->stopdot >= 90 ? 0 : (float)cos(dl->stopdot / 180 * Q_PI);
				}
				if (*ValueForKey(g_face_texlights[p->faceNumber], "_cone2"))
				{
					dl->stopdot2 = FloatForKey(g_face_texlights[p->faceNumber], "_cone2");
					dl->stopdot2 = dl->stopdot2 >= 90 ? 0 : (float)cos(dl->stopdot2 / 180 * Q_PI);
				}
				if (dl->stopdot2 > dl->stopdot)
					dl->stopdot2 = dl->stopdot;
			}

			dl->type = emit_surface;
			VectorCopy(getPlaneFromFaceNumber(p->faceNumber)->normal, dl->normal);
			VectorCopy(p->baselight, dl->intensity); // LRC
			if (g_face_texlights[p->faceNumber])
			{
				if (*ValueForKey(g_face_texlights[p->faceNumber], "_scale"))
				{
					auto scale = FloatForKey(g_face_texlights[p->faceNumber], "_scale");
					VectorScale(dl->intensity, scale, dl->intensity);
				}
			}
			VectorScale(dl->intensity, p->area, dl->intensity);
			VectorScale(dl->intensity, p->exposure, dl->intensity);
			VectorScale(dl->intensity, 1.0 / Q_PI, dl->intensity);
			VectorMultiply(dl->intensity, p->texturereflectivity, dl->intensity);

			auto *f = &g_bspfaces[p->faceNumber];
			if (g_face_entity[p->faceNumber] - g_entities != 0 && !strncasecmp(GetTextureByNumber(f->texinfo), "!", 1))
			{
				numdlights++;
				auto *dl2 = (DirectLight *)calloc(1, sizeof(DirectLight));
				hlassume(dl2 != nullptr, assume_NoMemory);
				*dl2 = *dl;
				VectorMA(dl->origin, -2, dl->normal, dl2->origin);
				VectorSubtract(vec3_origin, dl->normal, dl2->normal);
				leaf = PointInLeaf(dl2->origin);
				leafnum = leaf - g_bspleafs;
				dl2->next = directlights[leafnum];
				directlights[leafnum] = dl2;
			}
		}

		// LRC        VectorClear(p->totallight[0]);
	}

	//
	// entities
	//
	for (i = 0; i < (unsigned)g_numentities; i++)
	{
		double r, g, b, scaler;

		e = &g_entities[i];
		auto *name = ValueForKey(e, "classname");
		if (strncmp(name, "light", 5))
			continue;
		{
			auto style = IntForKey(e, "style");
			if (style < 0)
			{
				style = -style;
			}
			style = (unsigned char)style;
			if (style > 0 && style < ALLSTYLES && *ValueForKey(e, "zhlt_stylecoring"))
			{
				g_corings[style] = FloatForKey(e, "zhlt_stylecoring");
			}
		}
		if (!strcmp(name, "light_shadow") || !strcmp(name, "light_bounce"))
		{
			auto style = IntForKey(e, "style");
			if (style < 0)
			{
				style = -style;
			}
			style = (unsigned char)style;
			if (style >= 0 && style < ALLSTYLES)
			{
				if (styleused[style] == false)
				{
					styleused[style] = true;
					numstyles++;
				}
			}
			continue;
		}
		if (!strcmp(name, "light_surface"))
		{
			continue;
		}

		numdlights++;
		dl = (DirectLight *)calloc(1, sizeof(DirectLight));

		hlassume(dl != nullptr, assume_NoMemory);

		GetVectorForKey(e, "origin", dl->origin);

		leaf = PointInLeaf(dl->origin);
		leafnum = leaf - g_bspleafs;

		dl->next = directlights[leafnum];
		directlights[leafnum] = dl;

		dl->style = IntForKey(e, "style");
		if (dl->style < 0)
			dl->style = -dl->style; // LRC
		dl->style = (unsigned char)dl->style;
		if (dl->style >= ALLSTYLES)
		{
			Error("invalid light style: style (%d) >= ALLSTYLES (%d)", dl->style, ALLSTYLES);
		}
		if (dl->style >= 0 && dl->style < ALLSTYLES)
		{
			if (styleused[dl->style] == false)
			{
				styleused[dl->style] = true;
				numstyles++;
			}
		}
		dl->topatch = false;
		if (IntForKey(e, "_fast") == 1)
		{
			dl->topatch = true;
		}
		auto pLight = ValueForKey(e, "_light");
		// scanf into doubles, then assign, so it is vec_t size independent
		r = g = b = scaler = 0;
		auto argCnt = sscanf(pLight, "%lf %lf %lf %lf", &r, &g, &b, &scaler);
		dl->intensity[0] = (float)r;
		if (argCnt == 1)
		{
			// The R,G,B values are all equal.
			dl->intensity[1] = dl->intensity[2] = (float)r;
		}
		else if (argCnt == 3 || argCnt == 4)
		{
			// Save the other two G,B values.
			dl->intensity[1] = (float)g;
			dl->intensity[2] = (float)b;

			// Did we also get an "intensity" scaler value too?
			if (argCnt == 4)
			{
				// Scale the normalized 0-255 R,G,B values by the intensity scaler
				dl->intensity[0] = dl->intensity[0] / 255 * (float)scaler;
				dl->intensity[1] = dl->intensity[1] / 255 * (float)scaler;
				dl->intensity[2] = dl->intensity[2] / 255 * (float)scaler;
			}
		}
		else
		{
			Log("light at (%f,%f,%f) has bad or missing '_light' value : '%s'\n",
				dl->origin[0], dl->origin[1], dl->origin[2], pLight);
			continue;
		}

		dl->fade = FloatForKey(e, "_fade");
		if (dl->fade == 0.0)
		{
			dl->fade = g_fade;
		}

		target = ValueForKey(e, "target");

		if (!strcmp(name, "light_spot") || !strcmp(name, "light_environment") || target[0])
		{
			if (!VectorAvg(dl->intensity))
			{
			}
			dl->type = emit_spotlight;
			dl->stopdot = FloatForKey(e, "_cone");
			if (!dl->stopdot)
			{
				dl->stopdot = 10;
			}
			dl->stopdot2 = FloatForKey(e, "_cone2");
			if (!dl->stopdot2)
			{
				dl->stopdot2 = dl->stopdot;
			}
			if (dl->stopdot2 < dl->stopdot)
			{
				dl->stopdot2 = dl->stopdot;
			}
			dl->stopdot2 = (float)cos(dl->stopdot2 / 180 * Q_PI);
			dl->stopdot = (float)cos(dl->stopdot / 180 * Q_PI);

			if (!FindTargetEntity(target)) //--vluzacn
			{
				Warning("light at (%i %i %i) has missing target",
						(int)dl->origin[0], (int)dl->origin[1], (int)dl->origin[2]);
				target = "";
			}
			if (target[0])
			{ // point towards target
				auto *e2 = FindTargetEntity(target);
				if (!e2)
				{
					Warning("light at (%i %i %i) has missing target",
							(int)dl->origin[0], (int)dl->origin[1], (int)dl->origin[2]);
				}
				else
				{
					GetVectorForKey(e2, "origin", dest);
					VectorSubtract(dest, dl->origin, dl->normal);
					VectorNormalize(dl->normal);
				}
			}
			else
			{ // point down angle
				vec3_t vAngles;

				GetVectorForKey(e, "angles", vAngles);

				auto angle = (float)FloatForKey(e, "angle");
				if (angle == ANGLE_UP)
				{
					dl->normal[0] = dl->normal[1] = 0;
					dl->normal[2] = 1;
				}
				else if (angle == ANGLE_DOWN)
				{
					dl->normal[0] = dl->normal[1] = 0;
					dl->normal[2] = -1;
				}
				else
				{
					// if we don't have a specific "angle" use the "angles" YAW
					if (!angle)
					{
						angle = vAngles[1];
					}

					dl->normal[2] = 0;
					dl->normal[0] = (float)cos(angle / 180 * Q_PI);
					dl->normal[1] = (float)sin(angle / 180 * Q_PI);
				}

				angle = FloatForKey(e, "pitch");
				if (!angle)
				{
					// if we don't have a specific "pitch" use the "angles" PITCH
					angle = vAngles[0];
				}

				dl->normal[2] = (float)sin(angle / 180 * Q_PI);
				dl->normal[0] *= (float)cos(angle / 180 * Q_PI);
				dl->normal[1] *= (float)cos(angle / 180 * Q_PI);
			}

			if (FloatForKey(e, "_sky") || !strcmp(name, "light_environment"))
			{
				// -----------------------------------------------------------------------------------
				// Changes by Adam Foster - afoster@compsoc.man.ac.uk
				// diffuse lighting hack - most of the following code nicked from earlier
				// need to get diffuse intensity from new _diffuse_light key
				//
				// What does _sky do for spotlights, anyway?
				// -----------------------------------------------------------------------------------
				pLight = ValueForKey(e, "_diffuse_light");
				r = g = b = scaler = 0;
				argCnt = sscanf(pLight, "%lf %lf %lf %lf", &r, &g, &b, &scaler);
				dl->diffuse_intensity[0] = (float)r;
				if (argCnt == 1)
				{
					// The R,G,B values are all equal.
					dl->diffuse_intensity[1] = dl->diffuse_intensity[2] = (float)r;
				}
				else if (argCnt == 3 || argCnt == 4)
				{
					// Save the other two G,B values.
					dl->diffuse_intensity[1] = (float)g;
					dl->diffuse_intensity[2] = (float)b;

					// Did we also get an "intensity" scaler value too?
					if (argCnt == 4)
					{
						// Scale the normalized 0-255 R,G,B values by the intensity scaler
						dl->diffuse_intensity[0] = dl->diffuse_intensity[0] / 255 * (float)scaler;
						dl->diffuse_intensity[1] = dl->diffuse_intensity[1] / 255 * (float)scaler;
						dl->diffuse_intensity[2] = dl->diffuse_intensity[2] / 255 * (float)scaler;
					}
				}
				else
				{
					// backwards compatibility with maps without _diffuse_light

					dl->diffuse_intensity[0] = dl->intensity[0];
					dl->diffuse_intensity[1] = dl->intensity[1];
					dl->diffuse_intensity[2] = dl->intensity[2];
				}
				// -----------------------------------------------------------------------------------
				pLight = ValueForKey(e, "_diffuse_light2");
				r = g = b = scaler = 0;
				argCnt = sscanf(pLight, "%lf %lf %lf %lf", &r, &g, &b, &scaler);
				dl->diffuse_intensity2[0] = (float)r;
				if (argCnt == 1)
				{
					// The R,G,B values are all equal.
					dl->diffuse_intensity2[1] = dl->diffuse_intensity2[2] = (float)r;
				}
				else if (argCnt == 3 || argCnt == 4)
				{
					// Save the other two G,B values.
					dl->diffuse_intensity2[1] = (float)g;
					dl->diffuse_intensity2[2] = (float)b;

					// Did we also get an "intensity" scaler value too?
					if (argCnt == 4)
					{
						// Scale the normalized 0-255 R,G,B values by the intensity scaler
						dl->diffuse_intensity2[0] = dl->diffuse_intensity2[0] / 255 * (float)scaler;
						dl->diffuse_intensity2[1] = dl->diffuse_intensity2[1] / 255 * (float)scaler;
						dl->diffuse_intensity2[2] = dl->diffuse_intensity2[2] / 255 * (float)scaler;
					}
				}
				else
				{
					dl->diffuse_intensity2[0] = dl->diffuse_intensity[0];
					dl->diffuse_intensity2[1] = dl->diffuse_intensity[1];
					dl->diffuse_intensity2[2] = dl->diffuse_intensity[2];
				}

				dl->type = emit_skylight;
				dl->stopdot2 = FloatForKey(e, "_sky"); // hack stopdot2 to a sky key number
				dl->sunspreadangle = FloatForKey(e, "_spread");
				if (dl->sunspreadangle < 0.0 || dl->sunspreadangle > 180)
				{
					Error("Invalid spread angle '%s'. Please use a number between 0 and 180.\n", ValueForKey(e, "_spread"));
				}
				if (dl->sunspreadangle > 0.0)
				{
					auto testangle = dl->sunspreadangle;
					if (dl->sunspreadangle < SUNSPREAD_THRESHOLD)
					{
						testangle = SUNSPREAD_THRESHOLD; // We will later centralize all the normals we have collected.
					}
					{
						vec_t totalweight = 0;
						int count;
						vec_t testdot = cos(testangle * (Q_PI / 180.0));
						for (count = 0, i = 0; i < g_numskynormals[SUNSPREAD_SKYLEVEL]; i++)
						{
							auto &testnormal = g_skynormals[SUNSPREAD_SKYLEVEL][i];
							vec_t dot = DotProduct(dl->normal, testnormal);
							if (dot >= testdot - NORMAL_EPSILON)
							{
								totalweight += qmax(0, dot - testdot) * g_skynormalsizes[SUNSPREAD_SKYLEVEL][i]; // This is not the right formula when dl->sunspreadangle < SUNSPREAD_THRESHOLD, but it gives almost the same result as the right one.
								count++;
							}
						}
						if (count <= 10 || totalweight <= NORMAL_EPSILON)
						{
							Error("collect spread normals: internal error: can not collect enough normals.");
						}
						dl->numsunnormals = count;
						dl->sunnormals = new vec3_t[count];
						dl->sunnormalweights = new vec_t[count];
						hlassume(dl->sunnormals != nullptr, assume_NoMemory);
						hlassume(dl->sunnormalweights != nullptr, assume_NoMemory);
						for (count = 0, i = 0; i < g_numskynormals[SUNSPREAD_SKYLEVEL]; i++)
						{
							auto &testnormal = g_skynormals[SUNSPREAD_SKYLEVEL][i];
							vec_t dot = DotProduct(dl->normal, testnormal);
							if (dot >= testdot - NORMAL_EPSILON)
							{
								if (count >= dl->numsunnormals)
								{
									Error("collect spread normals: internal error.");
								}
								VectorCopy(testnormal, dl->sunnormals[count]);
								dl->sunnormalweights[count] = qmax(0, dot - testdot) * g_skynormalsizes[SUNSPREAD_SKYLEVEL][i] / totalweight;
								count++;
							}
						}
						if (count != dl->numsunnormals)
						{
							Error("collect spread normals: internal error.");
						}
					}
					if (dl->sunspreadangle < SUNSPREAD_THRESHOLD)
					{
						for (i = 0; i < dl->numsunnormals; i++)
						{
							vec3_t tmp;
							VectorScale(dl->sunnormals[i], 1 / DotProduct(dl->sunnormals[i], dl->normal), tmp);
							VectorSubtract(tmp, dl->normal, tmp);
							VectorMA(dl->normal, dl->sunspreadangle / SUNSPREAD_THRESHOLD, tmp, dl->sunnormals[i]);
							VectorNormalize(dl->sunnormals[i]);
						}
					}
				}
				else
				{
					dl->numsunnormals = 1;
					dl->sunnormals = new vec3_t[3];
					dl->sunnormalweights = new vec_t;
					hlassume(dl->sunnormals != nullptr, assume_NoMemory);
					hlassume(dl->sunnormalweights != nullptr, assume_NoMemory);
					VectorCopy(dl->normal, dl->sunnormals[0]);
					dl->sunnormalweights[0] = 1.0;
				}
			}
		}
		else
		{
			if (!VectorAvg(dl->intensity))
			{
			}
			dl->type = emit_point;
		}

		if (dl->type != emit_skylight)
		{
			// why? --vluzacn
			auto l1 = qmax(dl->intensity[0], qmax(dl->intensity[1], dl->intensity[2]));
			l1 = l1 * l1 / 10;

			dl->intensity[0] *= l1;
			dl->intensity[1] *= l1;
			dl->intensity[2] *= l1;
		}
	}

	int countnormallights = 0, countfastlights = 0;
	{
		for (int l = 0; l < 1 + g_bspmodels[0].visleafs; l++)
		{
			for (dl = directlights[l]; dl; dl = dl->next)
			{
				switch (dl->type)
				{
				case emit_surface:
				case emit_point:
				case emit_spotlight:
					if (!VectorCompare(dl->intensity, vec3_origin))
					{
						if (dl->topatch)
						{
							countfastlights++;
						}
						else
						{
							countnormallights++;
						}
					}
					break;
				case emit_skylight:
					if (!VectorCompare(dl->intensity, vec3_origin))
					{
						if (dl->topatch)
						{
							countfastlights++;
							if (dl->sunspreadangle > 0.0)
							{
								countfastlights--;
								countfastlights += dl->numsunnormals;
							}
						}
						else
						{
							countnormallights++;
							if (dl->sunspreadangle > 0.0)
							{
								countnormallights--;
								countnormallights += dl->numsunnormals;
							}
						}
					}
					if (!VectorCompare(dl->diffuse_intensity, vec3_origin))
					{
						countfastlights += g_numskynormals[SKYLEVEL_SOFTSKYON];
					}
					break;
				default:
					hlassume(false, assume_BadLightType);
					break;
				}
			}
		}
	}
	Log("%i direct lights and %i fast direct lights\n", countnormallights, countfastlights);
	Log("%i light styles\n", numstyles);
	// move all emit_skylight to leaf 0 (the solid leaf)
	DirectLight *skylights = nullptr;
	for (int l = 0; l < 1 + g_bspmodels[0].visleafs; l++)
	{
		DirectLight **pdl;
		for (dl = directlights[l], pdl = &directlights[l]; dl; dl = *pdl)
		{
			if (dl->type == emit_skylight)
			{
				*pdl = dl->next;
				dl->next = skylights;
				skylights = dl;
			}
			else
			{
				pdl = &dl->next;
			}
		}
	}
	while ((dl = directlights[0]) != nullptr)
	{
		// since they are in leaf 0, they won't emit a light anyway
		directlights[0] = dl->next;
		delete dl;
	}
	directlights[0] = skylights;
	int countlightenvironment = 0;
	int countinfosunlight = 0;
	for (int i = 0; i < g_numentities; i++)
	{
		auto *e = &g_entities[i];
		const auto *classname = ValueForKey(e, "classname");
		if (!strcmp(classname, "light_environment"))
		{
			countlightenvironment++;
		}
		if (!strcmp(classname, "info_sunlight"))
		{
			countinfosunlight++;
		}
	}
	if (countlightenvironment > 1 && countinfosunlight == 0)
	{
		// because the map is lit by more than one light_environments, but the game can only recognize one of them when setting sv_skycolor and sv_skyvec.
		Warning("More than one light_environments are in use. Add entity info_sunlight to clarify the sunlight's brightness for in-game model(.mdl) rendering.");
	}
}

// =====================================================================================
//  DeleteDirectLights
// =====================================================================================
void DeleteDirectLights()
{
	for (int l = 0; l < 1 + g_bspmodels[0].visleafs; l++)
	{
		auto *dl = directlights[l];
		while (dl)
		{
			directlights[l] = dl->next;
			delete dl;
			dl = directlights[l];
		}
	}

	// AJM: todo: strip light entities out at this point
	// vluzacn: hlvis and hlrad must not modify entity data, because the following procedures are supposed to produce the same bsp file:
	//  1> hlcsg -> hlbsp -> hlvis -> hlrad  (a normal compile)
	//  2) hlcsg -> hlbsp -> hlvis -> hlrad -> hlcsg -onlyents
	//  3) hlcsg -> hlbsp -> hlvis -> hlrad -> hlcsg -onlyents -> hlrad
}

// =====================================================================================
//  GatherSampleLight
// =====================================================================================
int g_numskynormals[SKYLEVELMAX + 1];
vec3_t *g_skynormals[SKYLEVELMAX + 1];
vec_t *g_skynormalsizes[SKYLEVELMAX + 1];
typedef double point_t[3];
struct edge_t
{
	int point[2];
	bool divided;
	int child[2];
};
struct triangle_t
{
	int edge[3];
	int dir[3];
};
void CopyToSkynormals(int skylevel, int numpoints, point_t *points, int numedges, edge_t *edges, int numtriangles, triangle_t *triangles)
{
	hlassume(numpoints == (1 << (2 * skylevel)) + 2, assume_first);
	hlassume(numedges == (1 << (2 * skylevel)) * 4 - 4, assume_first);
	hlassume(numtriangles == (1 << (2 * skylevel)) * 2, assume_first);
	g_numskynormals[skylevel] = numpoints;
	g_skynormals[skylevel] = new vec3_t[numpoints];
	g_skynormalsizes[skylevel] = new vec_t[numpoints];
	hlassume(g_skynormals[skylevel] != nullptr, assume_NoMemory);
	hlassume(g_skynormalsizes[skylevel] != nullptr, assume_NoMemory);
	int j;
	for (j = 0; j < numpoints; j++)
	{
		VectorCopy(points[j], g_skynormals[skylevel][j]);
		g_skynormalsizes[skylevel][j] = 0;
	}
	double totalsize = 0;
	for (j = 0; j < numtriangles; j++)
	{
		int pt[3];
		for (int k = 0; k < 3; k++)
		{
			pt[k] = edges[triangles[j].edge[k]].point[triangles[j].dir[k]];
		}
		double currentsize;
		double tmp[3];
		CrossProduct(points[pt[0]], points[pt[1]], tmp);
		currentsize = DotProduct(tmp, points[pt[2]]);
		hlassume(currentsize > 0, assume_first);
		g_skynormalsizes[skylevel][pt[0]] += currentsize / 3.0;
		g_skynormalsizes[skylevel][pt[1]] += currentsize / 3.0;
		g_skynormalsizes[skylevel][pt[2]] += currentsize / 3.0;
		totalsize += currentsize;
	}
	for (j = 0; j < numpoints; j++)
	{
		g_skynormalsizes[skylevel][j] /= totalsize;
	}
}
void BuildDiffuseNormals()
{
	int j;
	g_numskynormals[0] = 0;
	g_skynormals[0] = nullptr; // don't use this
	g_skynormalsizes[0] = nullptr;
	auto numpoints = 6;
	auto *points = new point_t[((1 << (2 * SKYLEVELMAX)) + 2)];
	hlassume(points != nullptr, assume_NoMemory);
	points[0][0] = 1, points[0][1] = 0, points[0][2] = 0;
	points[1][0] = -1, points[1][1] = 0, points[1][2] = 0;
	points[2][0] = 0, points[2][1] = 1, points[2][2] = 0;
	points[3][0] = 0, points[3][1] = -1, points[3][2] = 0;
	points[4][0] = 0, points[4][1] = 0, points[4][2] = 1;
	points[5][0] = 0, points[5][1] = 0, points[5][2] = -1;
	auto numedges = 12;
	auto *edges = new edge_t[((1 << (2 * SKYLEVELMAX)) * 4 - 4)];
	hlassume(edges != nullptr, assume_NoMemory);
	edges[0].point[0] = 0, edges[0].point[1] = 2, edges[0].divided = false;
	edges[1].point[0] = 2, edges[1].point[1] = 1, edges[1].divided = false;
	edges[2].point[0] = 1, edges[2].point[1] = 3, edges[2].divided = false;
	edges[3].point[0] = 3, edges[3].point[1] = 0, edges[3].divided = false;
	edges[4].point[0] = 2, edges[4].point[1] = 4, edges[4].divided = false;
	edges[5].point[0] = 4, edges[5].point[1] = 3, edges[5].divided = false;
	edges[6].point[0] = 3, edges[6].point[1] = 5, edges[6].divided = false;
	edges[7].point[0] = 5, edges[7].point[1] = 2, edges[7].divided = false;
	edges[8].point[0] = 4, edges[8].point[1] = 0, edges[8].divided = false;
	edges[9].point[0] = 0, edges[9].point[1] = 5, edges[9].divided = false;
	edges[10].point[0] = 5, edges[10].point[1] = 1, edges[10].divided = false;
	edges[11].point[0] = 1, edges[11].point[1] = 4, edges[11].divided = false;
	int numtriangles = 8;
	auto *triangles = new triangle_t[((1 << (2 * SKYLEVELMAX)) * 2)];
	hlassume(triangles != nullptr, assume_NoMemory);
	triangles[0].edge[0] = 0, triangles[0].dir[0] = 0, triangles[0].edge[1] = 4, triangles[0].dir[1] = 0, triangles[0].edge[2] = 8, triangles[0].dir[2] = 0;
	triangles[1].edge[0] = 1, triangles[1].dir[0] = 0, triangles[1].edge[1] = 11, triangles[1].dir[1] = 0, triangles[1].edge[2] = 4, triangles[1].dir[2] = 1;
	triangles[2].edge[0] = 2, triangles[2].dir[0] = 0, triangles[2].edge[1] = 5, triangles[2].dir[1] = 1, triangles[2].edge[2] = 11, triangles[2].dir[2] = 1;
	triangles[3].edge[0] = 3, triangles[3].dir[0] = 0, triangles[3].edge[1] = 8, triangles[3].dir[1] = 1, triangles[3].edge[2] = 5, triangles[3].dir[2] = 0;
	triangles[4].edge[0] = 0, triangles[4].dir[0] = 1, triangles[4].edge[1] = 9, triangles[4].dir[1] = 0, triangles[4].edge[2] = 7, triangles[4].dir[2] = 0;
	triangles[5].edge[0] = 1, triangles[5].dir[0] = 1, triangles[5].edge[1] = 7, triangles[5].dir[1] = 1, triangles[5].edge[2] = 10, triangles[5].dir[2] = 0;
	triangles[6].edge[0] = 2, triangles[6].dir[0] = 1, triangles[6].edge[1] = 10, triangles[6].dir[1] = 1, triangles[6].edge[2] = 6, triangles[6].dir[2] = 1;
	triangles[7].edge[0] = 3, triangles[7].dir[0] = 1, triangles[7].edge[1] = 6, triangles[7].dir[1] = 0, triangles[7].edge[2] = 9, triangles[7].dir[2] = 1;
	CopyToSkynormals(1, numpoints, points, numedges, edges, numtriangles, triangles);
	for (int i = 1; i < SKYLEVELMAX; i++)
	{
		int oldnumedges = numedges;
		for (j = 0; j < oldnumedges; j++)
		{
			if (!edges[j].divided)
			{
				hlassume(numpoints < (1 << (2 * SKYLEVELMAX)) + 2, assume_first);
				point_t mid;
				VectorAdd(points[edges[j].point[0]], points[edges[j].point[1]], mid);
				auto len = sqrt(DotProduct(mid, mid));
				hlassume(len > 0.2, assume_first);
				VectorScale(mid, 1 / len, mid);
				auto p2 = numpoints;
				VectorCopy(mid, points[numpoints]);
				numpoints++;
				hlassume(numedges < (1 << (2 * SKYLEVELMAX)) * 4 - 4, assume_first);
				edges[j].child[0] = numedges;
				edges[numedges].divided = false;
				edges[numedges].point[0] = edges[j].point[0];
				edges[numedges].point[1] = p2;
				numedges++;
				hlassume(numedges < (1 << (2 * SKYLEVELMAX)) * 4 - 4, assume_first);
				edges[j].child[1] = numedges;
				edges[numedges].divided = false;
				edges[numedges].point[0] = p2;
				edges[numedges].point[1] = edges[j].point[1];
				numedges++;
				edges[j].divided = true;
			}
		}
		int oldnumtriangles = numtriangles;
		for (j = 0; j < oldnumtriangles; j++)
		{
			int mid[3];
			for (int k = 0; k < 3; k++)
			{
				hlassume(numtriangles < (1 << (2 * SKYLEVELMAX)) * 2, assume_first);
				mid[k] = edges[edges[triangles[j].edge[k]].child[0]].point[1];
				triangles[numtriangles].edge[0] = edges[triangles[j].edge[k]].child[1 - triangles[j].dir[k]];
				triangles[numtriangles].dir[0] = triangles[j].dir[k];
				triangles[numtriangles].edge[1] = edges[triangles[j].edge[(k + 1) % 3]].child[triangles[j].dir[(k + 1) % 3]];
				triangles[numtriangles].dir[1] = triangles[j].dir[(k + 1) % 3];
				triangles[numtriangles].edge[2] = numedges + k;
				triangles[numtriangles].dir[2] = 1;
				numtriangles++;
			}
			for (int k = 0; k < 3; k++)
			{
				hlassume(numedges < (1 << (2 * SKYLEVELMAX)) * 4 - 4, assume_first);
				triangles[j].edge[k] = numedges;
				triangles[j].dir[k] = 0;
				edges[numedges].divided = false;
				edges[numedges].point[0] = mid[k];
				edges[numedges].point[1] = mid[(k + 1) % 3];
				numedges++;
			}
		}
		CopyToSkynormals(i + 1, numpoints, points, numedges, edges, numtriangles, triangles);
	}
	delete[] points;
	delete[] edges;
	delete[] triangles;
}
static void GatherSampleLight(const vec3_t pos, const byte *const pvs, const vec3_t normal, vec3_t *sample, byte *styles, int step, int miptex, int texlightgap_surfacenum)
{
	vec3_t delta;
	float dot, dot2;
	float ratio;
	int style_index;
	int step_match;
	bool sky_used = false;
	vec3_t testline_origin;
	vec3_t adds[ALLSTYLES];
	int style;
	memset(adds, 0, ALLSTYLES * sizeof(vec3_t));
	auto lighting_power = g_lightingconeinfo[miptex][0];
	auto lighting_scale = g_lightingconeinfo[miptex][1];
	auto lighting_diversify = (lighting_power != 1.0 || lighting_scale != 1.0);
	vec3_t texlightgap_textoworld[2];
	// calculates textoworld
	{
		auto *f = &g_bspfaces[texlightgap_surfacenum];
		const auto *dp = getPlaneFromFace(f);
		auto *tex = &g_bsptexinfo[f->texinfo];

		for (int x = 0; x < 2; x++)
		{
			CrossProduct(tex->vecs[1 - x], dp->normal, texlightgap_textoworld[x]);
			auto len = DotProduct(texlightgap_textoworld[x], tex->vecs[x]);
			if (fabs(len) < NORMAL_EPSILON)
			{
				VectorClear(texlightgap_textoworld[x]);
			}
			else
			{
				VectorScale(texlightgap_textoworld[x], 1 / len, texlightgap_textoworld[x]);
			}
		}
	}

	for (int i = 0; i < 1 + g_bspmodels[0].visleafs; i++)
	{
		auto *l = directlights[i];
		if (l)
		{
			if (i == 0 ? true : pvs[(i - 1) >> 3] & (1 << ((i - 1) & 7))) // true = DEFAULT g_skylighting_fix a.k.a !-noskyfix
			{
				for (; l; l = l->next)
				{
					// skylights work fundamentally differently than normal lights
					if (l->type == emit_skylight)
					{
						do // add sun light
						{
							// check step
							step_match = (int)l->topatch;
							if (step != step_match)
								continue;
							// check intensity
							if (!(l->intensity[0] || l->intensity[1] || l->intensity[2]))
								continue;
							// loop over the normals
							for (int j = 0; j < l->numsunnormals; j++)
							{
								// make sure the angle is okay
								dot = -DotProduct(normal, l->sunnormals[j]);
								if (dot <= NORMAL_EPSILON) // ON_EPSILON / 10 //--vluzacn
								{
									continue;
								}

								// search back to see if we can hit a sky brush
								VectorScale(l->sunnormals[j], -RAD_BOGUS_RANGE, delta);
								VectorAdd(pos, delta, delta);
								vec3_t skyhit;
								VectorCopy(delta, skyhit);
								if (TestLine(pos, delta, skyhit) != CONTENTS_SKY)
								{
									continue; // occluded
								}

								vec3_t transparency;
								int opaquestyle;
								if (TestSegmentAgainstOpaqueList(pos,
																 skyhit, transparency, opaquestyle))
								{
									continue;
								}

								vec3_t add_one;
								if (lighting_diversify)
								{
									dot = lighting_scale * pow(dot, lighting_power);
								}
								VectorScale(l->intensity, dot * l->sunnormalweights[j], add_one);
								VectorMultiply(add_one, transparency, add_one);
								// add to the total brightness of this sample
								style = l->style;
								if (opaquestyle != -1)
								{
									if (style == 0 || style == opaquestyle)
										style = opaquestyle;
									else
										continue; // dynamic light of other styles hits this toggleable opaque entity, then it completely vanishes.
								}
								VectorAdd(adds[style], add_one, adds[style]);
							} // (loop over the normals)
						} while (false);
						do // add sky light
						{
							// check step
							step_match = 1;
							if (step != step_match)
								continue;
							// check intensity
							if (VectorCompare(l->diffuse_intensity, vec3_origin) && VectorCompare(l->diffuse_intensity2, vec3_origin))
								continue;

							vec3_t sky_intensity;

							// loop over the normals
							auto *skynormals = g_skynormals[7]; // 7 = SKYLEVEL_SOFTSKYON else 4 = SKYLEVEL_SOFTSKYOFF if -fast
							auto *skyweights = g_skynormalsizes[7];
							for (int j = 0; j < g_numskynormals[7]; j++)
							{
								// make sure the angle is okay
								dot = -DotProduct(normal, skynormals[j]);
								if (dot <= NORMAL_EPSILON) // ON_EPSILON / 10 //--vluzacn
								{
									continue;
								}

								// search back to see if we can hit a sky brush
								VectorScale(skynormals[j], -RAD_BOGUS_RANGE, delta);
								VectorAdd(pos, delta, delta);
								vec3_t skyhit;
								VectorCopy(delta, skyhit);
								if (TestLine(pos, delta, skyhit) != CONTENTS_SKY)
								{
									continue; // occluded
								}

								vec3_t transparency;
								int opaquestyle;
								if (TestSegmentAgainstOpaqueList(pos,
																 skyhit, transparency, opaquestyle))
								{
									continue;
								}

								vec_t factor = qmin(qmax(0.0, (1 - DotProduct(l->normal, skynormals[j])) / 2), 1.0); // how far this piece of sky has deviated from the sun
								VectorScale(l->diffuse_intensity, 1 - factor, sky_intensity);
								VectorMA(sky_intensity, factor, l->diffuse_intensity2, sky_intensity);
								VectorScale(sky_intensity, skyweights[j] * 1.0 / 2, sky_intensity); // 1.0 = DEFAULT_INDIRECT_SUN a.k.a g_indirect_sun a.k.a '-sky'
								vec3_t add_one;
								if (lighting_diversify)
								{
									dot = lighting_scale * pow(dot, lighting_power);
								}
								VectorScale(sky_intensity, dot, add_one);
								VectorMultiply(add_one, transparency, add_one);
								// add to the total brightness of this sample
								style = l->style;
								if (opaquestyle != -1)
								{
									if (style == 0 || style == opaquestyle)
										style = opaquestyle;
									else
										continue; // dynamic light of other styles hits this toggleable opaque entity, then it completely vanishes.
								}
								VectorAdd(adds[style], add_one, adds[style]);
							} // (loop over the normals)

						} while (false);
					}
					else // not emit_skylight
					{
						step_match = (int)l->topatch;
						if (step != step_match)
							continue;
						if (!(l->intensity[0] || l->intensity[1] || l->intensity[2]))
							continue;
						VectorCopy(l->origin, testline_origin);
						float denominator;

						VectorSubtract(l->origin, pos, delta);
						if (l->type == emit_surface)
						{
							// move emitter back to its plane
							VectorMA(delta, -PATCH_HUNT_OFFSET, l->normal, delta);
						}
						auto dist = VectorNormalize(delta);
						dot = DotProduct(delta, normal);
						//                        if (dot <= 0.0)
						//                            continue;

						if (dist < 1.0)
						{
							dist = 1.0;
						}

						denominator = dist * dist * l->fade;

						vec3_t add;
						switch (l->type)
						{
						case emit_point:
						{
							if (dot <= NORMAL_EPSILON)
							{
								continue;
							}
							auto denominator = dist * dist * l->fade;
							if (lighting_diversify)
							{
								dot = lighting_scale * pow(dot, lighting_power);
							}
							ratio = dot / denominator;
							VectorScale(l->intensity, ratio, add);
							break;
						}

						case emit_surface:
						{
							auto light_behind_surface = false;
							if (dot <= NORMAL_EPSILON)
							{
								light_behind_surface = true;
							}
							if (lighting_diversify && !light_behind_surface)
							{
								dot = lighting_scale * pow(dot, lighting_power);
							}
							dot2 = -DotProduct(delta, l->normal);
							// discard the texlight if the spot is too close to the texlight plane
							if (l->texlightgap > 0)
							{
								auto test = dot2 * dist;														 // distance from spot to texlight plane;
								test -= l->texlightgap * fabs(DotProduct(l->normal, texlightgap_textoworld[0])); // maximum distance reduction if the spot is allowed to shift l->texlightgap pixels along s axis
								test -= l->texlightgap * fabs(DotProduct(l->normal, texlightgap_textoworld[1])); // maximum distance reduction if the spot is allowed to shift l->texlightgap pixels along t axis
								if (test < -ON_EPSILON)
								{
									continue;
								}
							}
							if (dot2 * dist <= MINIMUM_PATCH_DISTANCE)
							{
								continue;
							}
							auto range = l->patch_emitter_range;
							if (l->stopdot > 0.0) // stopdot2 > 0.0 or stopdot > 0.0
							{
								auto range_scale = 1 - l->stopdot2 * l->stopdot2;
								range_scale = 1 / sqrt(qmax(NORMAL_EPSILON, range_scale));
								// range_scale = 1 / sin (cone2)
								range_scale = qmin(range_scale, 2); // restrict this to 2, because skylevel has limit.
								range *= range_scale;				// because smaller cones are more likely to create the ugly grid effect.

								if (dot2 <= l->stopdot2 + NORMAL_EPSILON)
								{
									if (dist >= range) // use the old method, which will merely give 0 in this case
									{
										continue;
									}
									ratio = 0.0;
								}
								else if (dot2 <= l->stopdot)
								{
									ratio = dot * dot2 * (dot2 - l->stopdot2) / (dist * dist * (l->stopdot - l->stopdot2));
								}
								else
								{
									ratio = dot * dot2 / (dist * dist);
								}
							}
							else
							{
								ratio = dot * dot2 / (dist * dist);
							}

							// analogous to the one in MakeScales
							// 0.4f is tested to be able to fully eliminate bright spots
							if (ratio * l->patch_area > 0.4f)
							{
								ratio = 0.4f / l->patch_area;
							}
							if (dist < range - ON_EPSILON)
							{ // do things slow
								if (light_behind_surface)
								{
									dot = 0.0;
									ratio = 0.0;
								}
								GetAlternateOrigin(pos, normal, l->patch, testline_origin);
								vec_t sightarea;
								int skylevel = l->patch->emitter_skylevel;
								if (l->stopdot > 0.0) // stopdot2 > 0.0 or stopdot > 0.0
								{
									const auto *emitnormal = getPlaneFromFaceNumber(l->patch->faceNumber)->normal;
									if (l->stopdot2 >= 0.8) // about 37deg
									{
										skylevel += 1; // because the range is larger
									}
									sightarea = CalcSightArea_SpotLight(pos, normal, l->patch->winding, emitnormal, l->stopdot, l->stopdot2, skylevel, lighting_power, lighting_scale); // because we have doubled the range
								}
								else
								{
									sightarea = CalcSightArea(pos, normal, l->patch->winding, skylevel, lighting_power, lighting_scale);
								}

								auto frac = dist / range;
								frac = (frac - 0.5) * 2; // make a smooth transition between the two methods
								frac = qmax(0, qmin(frac, 1));

								auto ratio2 = (sightarea / l->patch_area); // because l->patch->area has been multiplied into l->intensity
								ratio = frac * ratio + (1 - frac) * ratio2;
							}
							else
							{
								if (light_behind_surface)
								{
									continue;
								}
							}
							VectorScale(l->intensity, ratio, add);
							break;
						}

						case emit_spotlight:
						{
							if (dot <= NORMAL_EPSILON)
							{
								continue;
							}
							dot2 = -DotProduct(delta, l->normal);
							if (dot2 <= l->stopdot2)
							{
								continue; // outside light cone
							}

							// Variable power falloff (1 = inverse linear, 2 = inverse square
							auto denominator = dist * l->fade;
							{
								denominator *= dist;
							}
							if (lighting_diversify)
							{
								dot = lighting_scale * pow(dot, lighting_power);
							}
							ratio = dot * dot2 / denominator;

							if (dot2 <= l->stopdot)
							{
								ratio *= (dot2 - l->stopdot2) / (l->stopdot - l->stopdot2);
							}
							VectorScale(l->intensity, ratio, add);
							break;
						}

						default:
						{
							hlassume(false, assume_BadLightType);
							break;
						}
						}
						if (TestLine(pos,
									 testline_origin) != CONTENTS_EMPTY)
						{
							continue;
						}
						vec3_t transparency;
						int opaquestyle;
						if (TestSegmentAgainstOpaqueList(pos,
														 testline_origin, transparency, opaquestyle))
						{
							continue;
						}
						VectorMultiply(add, transparency, add);
						// add to the total brightness of this sample
						style = l->style;
						if (opaquestyle != -1)
						{
							if (style == 0 || style == opaquestyle)
								style = opaquestyle;
							else
								continue; // dynamic light of other styles hits this toggleable opaque entity, then it completely vanishes.
						}
						VectorAdd(adds[style], add, adds[style]);
					} // end emit_skylight
				}
			}
		}
	}

	for (style = 0; style < ALLSTYLES; ++style)
	{
		if (VectorMaximum(adds[style]) > g_corings[style] * 0.1)
		{
			for (style_index = 0; style_index < ALLSTYLES; style_index++)
			{
				if (styles[style_index] == style || styles[style_index] == 255)
				{
					break;
				}
			}

			if (style_index == ALLSTYLES) // shouldn't happen
			{
				if (++g_stylewarningcount >= g_stylewarningnext)
				{
					g_stylewarningnext = g_stylewarningcount * 2;
					Warning("Too many direct light styles on a face(%f,%f,%f)", pos[0], pos[1], pos[2]);
					Warning(" total %d warnings for too many styles", g_stylewarningcount);
				}
				return;
			}

			if (styles[style_index] == 255)
			{
				styles[style_index] = style;
			}

			VectorAdd(sample[style_index], adds[style], sample[style_index]);
		}
		else
		{
			if (VectorMaximum(adds[style]) > g_maxdiscardedlight + NORMAL_EPSILON)
			{
				ThreadLock();
				if (VectorMaximum(adds[style]) > g_maxdiscardedlight + NORMAL_EPSILON)
				{
					g_maxdiscardedlight = VectorMaximum(adds[style]);
					VectorCopy(pos, g_maxdiscardedpos);
				}
				ThreadUnlock();
			}
		}
	}
}

// =====================================================================================
//  AddSampleToPatch
//      Take the sample's collected light and add it back into the apropriate patch for the radiosity pass.
// =====================================================================================
static void AddSamplesToPatches(const sample_t **samples, const unsigned char *styles, int facenum, const lightinfo_t *l)
{
	Patch *patch;
	int i, j, m, k;

	auto numtexwindings = 0;
	for (patch = g_face_patches[facenum]; patch; patch = patch->next)
	{
		numtexwindings++;
	}
	auto **texwindings = new Winding *[numtexwindings];
	hlassume(texwindings != nullptr, assume_NoMemory);

	// translate world winding into winding in s,t plane
	for (j = 0, patch = g_face_patches[facenum]; j < numtexwindings; j++, patch = patch->next)
	{
		auto *w = new Winding(patch->winding->m_NumPoints);
		for (int x = 0; x < w->m_NumPoints; x++)
		{
			vec_t s, t;
			SetSTFromSurf(l, patch->winding->m_Points[x], s, t);
			w->m_Points[x][0] = s;
			w->m_Points[x][1] = t;
			w->m_Points[x][2] = 0.0;
		}
		w->RemoveColinearPoints();
		texwindings[j] = w;
	}

	for (i = 0; i < l->numsurfpt; i++)
	{
		// prepare clip planes
		auto s_vec = l->texmins[0] * TEXTURE_STEP + (i % (l->texsize[0] + 1)) * TEXTURE_STEP;
		auto t_vec = l->texmins[1] * TEXTURE_STEP + (i / (l->texsize[0] + 1)) * TEXTURE_STEP;

		dplane_t clipplanes[4];
		VectorClear(clipplanes[0].normal);
		clipplanes[0].normal[0] = 1;
		clipplanes[0].dist = s_vec - 0.5 * TEXTURE_STEP;
		VectorClear(clipplanes[1].normal);
		clipplanes[1].normal[0] = -1;
		clipplanes[1].dist = -(s_vec + 0.5 * TEXTURE_STEP);
		VectorClear(clipplanes[2].normal);
		clipplanes[2].normal[1] = 1;
		clipplanes[2].dist = t_vec - 0.5 * TEXTURE_STEP;
		VectorClear(clipplanes[3].normal);
		clipplanes[3].normal[1] = -1;
		clipplanes[3].dist = -(t_vec + 0.5 * TEXTURE_STEP);

		// clip each patch
		for (j = 0, patch = g_face_patches[facenum]; j < numtexwindings; j++, patch = patch->next)
		{
			auto *w = new Winding(*texwindings[j]);
			for (k = 0; k < 4; k++)
			{
				if (w->m_NumPoints)
				{
					w->Clip(clipplanes[k], false);
				}
			}
			if (w->m_NumPoints)
			{
				// add sample to patch
				auto area = w->getArea() / (TEXTURE_STEP * TEXTURE_STEP);
				patch->samples += area;
				for (m = 0; m < ALLSTYLES && styles[m] != 255; m++)
				{
					auto style = styles[m];
					const auto *s = &samples[m][i];
					for (k = 0; k < ALLSTYLES && patch->totalstyle_all[k] != 255; k++)
					{
						if (patch->totalstyle_all[k] == style)
						{
							break;
						}
					}
					if (k == ALLSTYLES)
					{
						if (++g_stylewarningcount >= g_stylewarningnext)
						{
							g_stylewarningnext = g_stylewarningcount * 2;
							Warning("Too many direct light styles on a face(?,?,?)\n");
							Warning(" total %d warnings for too many styles", g_stylewarningcount);
						}
					}
					else
					{
						if (patch->totalstyle_all[k] == 255)
						{
							patch->totalstyle_all[k] = style;
						}
						VectorMA(patch->samplelight_all[k], area, s->light, patch->samplelight_all[k]);
					}
				}
			}
			delete w;
		}
	}

	for (j = 0; j < numtexwindings; j++)
	{
		delete texwindings[j];
	}
	delete[] texwindings;
}

// =====================================================================================
//  GetPhongNormal
// =====================================================================================
void GetPhongNormal(int facenum, const vec3_t spot, vec3_t phongnormal)
{
	const auto *f = g_bspfaces + facenum;
	const auto *p = getPlaneFromFace(f);
	vec3_t facenormal;

	VectorCopy(p->normal, facenormal);
	VectorCopy(facenormal, phongnormal);

	{
		// Calculate modified point normal for surface
		// Use the edge normals iff they are defined.  Bend the surface towards the edge normal(s)
		// Crude first attempt: find nearest edge normal and do a simple interpolation with facenormal.
		// Second attempt: find edge points+center that bound the point and do a three-point triangulation(baricentric)
		// Better third attempt: generate the point normals for all vertices and do baricentric triangulation.

		for (int j = 0; j < f->numedges; j++)
		{
			vec3_t p1;
			vec3_t p2;
			vec3_t v1;
			vec3_t v2;
			vec3_t vspot;
			unsigned prev_edge;
			unsigned next_edge;

			if (j)
			{
				prev_edge = f->firstedge + ((j + f->numedges - 1) % f->numedges);
			}
			else
			{
				prev_edge = f->firstedge + f->numedges - 1;
			}

			if ((j + 1) != f->numedges)
			{
				next_edge = f->firstedge + ((j + 1) % f->numedges);
			}
			else
			{
				next_edge = f->firstedge;
			}

			auto e = g_bspsurfedges[f->firstedge + j];
			auto e1 = g_bspsurfedges[prev_edge];
			auto e2 = g_bspsurfedges[next_edge];

			auto *es = &g_edgeshare[abs(e)];
			auto *es1 = &g_edgeshare[abs(e1)];
			auto *es2 = &g_edgeshare[abs(e2)];

			if ((!es->smooth || es->coplanar) && (!es1->smooth || es1->coplanar) && (!es2->smooth || es2->coplanar))
			{
				continue;
			}

			if (e > 0)
			{
				VectorCopy(g_bspvertexes[g_bspedges[e].v[0]].point, p1);
				VectorCopy(g_bspvertexes[g_bspedges[e].v[1]].point, p2);
			}
			else
			{
				VectorCopy(g_bspvertexes[g_bspedges[-e].v[1]].point, p1);
				VectorCopy(g_bspvertexes[g_bspedges[-e].v[0]].point, p2);
			}

			// Adjust for origin-based models
			VectorAdd(p1, g_face_offset[facenum], p1);
			VectorAdd(p2, g_face_offset[facenum], p2);
			for (int s = 0; s < 2; s++) // split every edge into two parts
			{
				vec3_t s1, s2;
				if (s == 0)
				{
					VectorCopy(p1, s1);
				}
				else
				{
					VectorCopy(p2, s1);
				}

				VectorAdd(p1, p2, s2); // edge center
				VectorScale(s2, 0.5, s2);

				VectorSubtract(s1, g_face_centroids[facenum], v1);
				VectorSubtract(s2, g_face_centroids[facenum], v2);
				VectorSubtract(spot, g_face_centroids[facenum], vspot);

				auto aa = DotProduct(v1, v1);
				auto bb = DotProduct(v2, v2);
				auto ab = DotProduct(v1, v2);
				auto a1 = (bb * DotProduct(v1, vspot) - ab * DotProduct(vspot, v2)) / (aa * bb - ab * ab);
				auto a2 = (DotProduct(vspot, v2) - a1 * ab) / bb;

				// Test center to sample vector for inclusion between center to vertex vectors (Use dot product of vectors)
				if (a1 >= -0.01 && a2 >= -0.01)
				{
					// calculate distance from edge to pos
					vec3_t n1, n2;
					vec3_t temp;

					if (es->smooth)
						if (s == 0)
						{
							VectorCopy(es->vertex_normal[e > 0 ? 0 : 1], n1);
						}
						else
						{
							VectorCopy(es->vertex_normal[e > 0 ? 1 : 0], n1);
						}
					else if (s == 0 && es1->smooth)
					{
						VectorCopy(es1->vertex_normal[e1 > 0 ? 1 : 0], n1);
					}
					else if (s == 1 && es2->smooth)
					{
						VectorCopy(es2->vertex_normal[e2 > 0 ? 0 : 1], n1);
					}
					else
					{
						VectorCopy(facenormal, n1);
					}

					if (es->smooth)
					{
						VectorCopy(es->interface_normal, n2);
					}
					else
					{
						VectorCopy(facenormal, n2);
					}

					// Interpolate between the center and edge normals based on sample position
					VectorScale(facenormal, 1.0 - a1 - a2, phongnormal);
					VectorScale(n1, a1, temp);
					VectorAdd(phongnormal, temp, phongnormal);
					VectorScale(n2, a2, temp);
					VectorAdd(phongnormal, temp, phongnormal);
					VectorNormalize(phongnormal);
					break;
				}
			} // s=0,1
		}
	}
}

// =====================================================================================
//  BuildFacelights
// =====================================================================================
void CalcLightmap(lightinfo_t *l, byte *styles)
{
	int j;
	byte pvs[(MAX_MAP_LEAFS + 7) / 8];
	int lastoffset;
	byte pvs2[(MAX_MAP_LEAFS + 7) / 8];
	int lastoffset2;

	auto facenum = l->surfnum;
	memset(l->lmcache, 0, l->lmcachewidth * l->lmcacheheight * sizeof(vec3_t[ALLSTYLES]));

	// for each sample whose light we need to calculate
	for (int i = 0; i < l->lmcachewidth * l->lmcacheheight; i++)
	{
		vec_t s_vec, t_vec;
		int nearest_s, nearest_t;
		vec3_t spot;
		vec_t square[2][2]; // the max possible range in which this sample point affects the lighting on a face
		vec3_t surfpt;		// the point on the surface (with no HUNT_OFFSET applied), used for getting phong normal and doing patch interpolation
		int surface;
		vec3_t pointnormal;
		bool blocked;
		vec3_t spot2;
		vec3_t pointnormal2;
		vec3_t *sampled;
		vec3_t *normal_out;
		bool nudged;
		int *wallflags_out;

		// prepare input parameter and output parameter
		{
			auto s = ((i % l->lmcachewidth) - l->lmcache_offset) / (vec_t)l->lmcache_density;
			auto t = ((i / l->lmcachewidth) - l->lmcache_offset) / (vec_t)l->lmcache_density;
			s_vec = l->texmins[0] * TEXTURE_STEP + s * TEXTURE_STEP;
			t_vec = l->texmins[1] * TEXTURE_STEP + t * TEXTURE_STEP;
			nearest_s = qmax(0, qmin((int)floor(s + 0.5), l->texsize[0]));
			nearest_t = qmax(0, qmin((int)floor(t + 0.5), l->texsize[1]));
			sampled = l->lmcache[i];
			normal_out = &l->lmcache_normal[i];
			wallflags_out = &l->lmcache_wallflags[i];
			//
			// The following graph illustrates the range in which a sample point can affect the lighting of a face when g_blur = 1.5 and g_extra = on
			//              X : the sample point. They are placed on every TEXTURE_STEP/lmcache_density (=16.0/3) texture pixels. We calculate light for each sample point, which is the main time sink.
			//              + : the lightmap pixel. They are placed on every TEXTURE_STEP (=16.0) texture pixels, which is hard coded inside the GoldSrc engine. Their brightness are averaged from the sample points in a square with size g_blur*TEXTURE_STEP.
			//              o : indicates that this lightmap pixel is affected by the sample point 'X'. The higher g_blur, the more 'o'.
			//       |/ / / | : indicates that the brightness of this area is affected by the lightmap pixels 'o' and hence by the sample point 'X'. This is because the engine uses bilinear interpolation to display the lightmap.
			//
			//    ==============================================================================================================================================
			//    || +     +     +     +     +     + || +     +     +     +     +     + || +     +     +     +     +     + || +     +     +     +     +     + ||
			//    ||                                 ||                                 ||                                 ||                                 ||
			//    ||                                 ||                                 ||                                 ||                                 ||
			//    || +     +-----+-----+     +     + || +     +-----+-----+-----+     + || +     +-----+-----+-----+     + || +     +     +-----+-----+     + ||
			//    ||       | / / / / / |             ||       | / / / / / / / / |       ||       | / / / / / / / / |       ||             | / / / / / |       ||
			//    ||       |/ / / / / /|             ||       |/ / / / / / / / /|       ||       |/ / / / / / / / /|       ||             |/ / / / / /|       ||
			//    || +     + / / X / / +     +     + || +     + / / o X / o / / +     + || +     + / / o / X o / / +     + || +     +     + / / X / / +     + ||
			//    ||       |/ / / / / /|             ||       |/ / / / / / / / /|       ||       |/ / / / / / / / /|       ||             |/ / / / / /|       ||
			//    ||       | / / / / / |             ||       | / / / / / / / / |       ||       | / / / / / / / / |       ||             | / / / / / |       ||
			//    || +     +-----+-----+     +     + || +     +-----+-----+-----+     + || +     +-----+-----+-----+     + || +     +     +-----+-----+     + ||
			//    ||                                 ||                                 ||                                 ||                                 ||
			//    ||                                 ||                                 ||                                 ||                                 ||
			//    || +     +     +     +     +     + || +     +     +     +     +     + || +     +     +     +     +     + || +     +     +     +     +     + ||
			//    ==============================================================================================================================================
			//    || +     +     +     +     +     + || +     +     +     +     +     + || +     +     +     +     +     + || +     +     +     +     +     + ||
			//    ||                                 ||                                 ||                                 ||                                 ||
			//    ||                                 ||                                 ||                                 ||                                 ||
			//    || +     +-----+-----+     +     + || +     +-----+-----+-----+     + || +     +-----+-----+-----+     + || +     +     +-----+-----+     + ||
			//    ||       | / / / / / |             ||       | / / / / / / / / |       ||       | / / / / / / / / |       ||             | / / / / / |       ||
			//    ||       |/ / / / / /|             ||       |/ / / / / / / / /|       ||       |/ / / / / / / / /|       ||             |/ / / / / /|       ||
			//    || +     + / / o / / +     +     + || +     + / / o / / o / / +     + || +     + / / o / / o / / +     + || +     +     + / / o / / +     + ||
			//    ||       |/ / /X/ / /|             ||       |/ / / /X/ / / / /|       ||       |/ / / / /X/ / / /|       ||             |/ / /X/ / /|       ||
			//    ||       | / / / / / |             ||       | / / / / / / / / |       ||       | / / / / / / / / |       ||             | / / / / / |       ||
			//    || +     +/ / /o/ / /+     +     + || +     +/ / /o/ / /o/ / /+     + || +     +/ / /o/ / /o/ / /+     + || +     +     +/ / /o/ / /+     + ||
			//    ||       | / / / / / |             ||       | / / / / / / / / |       ||       | / / / / / / / / |       ||             | / / / / / |       ||
			//    ||       |/ / / / / /|             ||       |/ / / / / / / / /|       ||       |/ / / / / / / / /|       ||             |/ / / / / /|       ||
			//    || +     +-----+-----+     +     + || +     +-----+-----+-----+     + || +     +-----+-----+-----+     + || +     +     +-----+-----+     + ||
			//    ==============================================================================================================================================
			//
			square[0][0] = l->texmins[0] * TEXTURE_STEP + ceil(s - (l->lmcache_side + 0.5) / (vec_t)l->lmcache_density) * TEXTURE_STEP - TEXTURE_STEP;
			square[0][1] = l->texmins[1] * TEXTURE_STEP + ceil(t - (l->lmcache_side + 0.5) / (vec_t)l->lmcache_density) * TEXTURE_STEP - TEXTURE_STEP;
			square[1][0] = l->texmins[0] * TEXTURE_STEP + floor(s + (l->lmcache_side + 0.5) / (vec_t)l->lmcache_density) * TEXTURE_STEP + TEXTURE_STEP;
			square[1][1] = l->texmins[1] * TEXTURE_STEP + floor(t + (l->lmcache_side + 0.5) / (vec_t)l->lmcache_density) * TEXTURE_STEP + TEXTURE_STEP;
		}
		// find world's position for the sample
		{
			{
				blocked = false;
				if (SetSampleFromST(
						surfpt, spot, &surface,
						&nudged,
						l, s_vec, t_vec,
						square,
						g_face_lightmode[facenum]) == LightOutside)
				{
					j = nearest_s + (l->texsize[0] + 1) * nearest_t;
					if (l->surfpt_lightoutside[j])
					{
						blocked = true;
					}
					else
					{
						// the area this light sample has effect on is completely covered by solid, so take whatever valid position.
						VectorCopy(l->surfpt[j], surfpt);
						VectorCopy(l->surfpt_position[j], spot);
						surface = l->surfpt_surface[j];
					}
				}
			}
			if (l->translucent_b)
			{
				const dplane_t *surfaceplane = getPlaneFromFaceNumber(surface);
				auto *surfacewinding = new Winding(g_bspfaces[surface]);

				VectorCopy(spot, spot2);
				for (int x = 0; x < surfacewinding->m_NumPoints; x++)
				{
					VectorAdd(surfacewinding->m_Points[x], g_face_offset[surface], surfacewinding->m_Points[x]);
				}
				if (!point_in_winding_noedge(*surfacewinding, *surfaceplane, spot2, 0.2))
				{
					snap_to_winding_noedge(*surfacewinding, *surfaceplane, spot2, 0.2, 4 * 0.2);
				}
				VectorMA(spot2, -(g_translucentdepth + 2 * DEFAULT_HUNT_OFFSET), surfaceplane->normal, spot2);

				delete surfacewinding;
			}
			*wallflags_out = WALLFLAG_NONE;
			if (blocked)
			{
				*wallflags_out |= (WALLFLAG_BLOCKED | WALLFLAG_NUDGED);
			}
			if (nudged)
			{
				*wallflags_out |= WALLFLAG_NUDGED;
			}
		}
		// calculate normal for the sample
		{
			GetPhongNormal(surface, surfpt, pointnormal);
			if (l->translucent_b)
			{
				VectorSubtract(vec3_origin, pointnormal, pointnormal2);
			}
			VectorCopy(pointnormal, *normal_out);
		}
		// calculate visibility for the sample
		{
			if (!g_bspvisdatasize)
			{
				if (i == 0)
				{
					memset(pvs, 255, (g_bspmodels[0].visleafs + 7) / 8);
				}
			}
			else
			{
				auto *leaf = PointInLeaf(spot);
				auto thisoffset = leaf->visofs;
				if (i == 0 || thisoffset != lastoffset)
				{
					if (thisoffset == -1)
					{
						memset(pvs, 0, (g_bspmodels[0].visleafs + 7) / 8);
					}
					else
					{
						DecompressVis(&g_bspvisdata[leaf->visofs], pvs, sizeof(pvs));
					}
				}
				lastoffset = thisoffset;
			}
			if (l->translucent_b)
			{
				if (!g_bspvisdatasize)
				{
					if (i == 0)
					{
						memset(pvs2, 255, (g_bspmodels[0].visleafs + 7) / 8);
					}
				}
				else
				{
					auto *leaf2 = PointInLeaf(spot2);
					auto thisoffset2 = leaf2->visofs;
					if (i == 0 || thisoffset2 != lastoffset2)
					{
						if (thisoffset2 == -1)
						{
							memset(pvs2, 0, (g_bspmodels[0].visleafs + 7) / 8);
						}
						else
						{
							DecompressVis(&g_bspvisdata[leaf2->visofs], pvs2, sizeof(pvs2));
						}
					}
					lastoffset2 = thisoffset2;
				}
			}
		}
		// gather light
		{
			if (!blocked)
			{
				GatherSampleLight(spot, pvs, pointnormal, sampled, styles, 0, l->miptex, surface);
			}
			if (l->translucent_b)
			{
				vec3_t sampled2[ALLSTYLES];
				memset(sampled2, 0, ALLSTYLES * sizeof(vec3_t));
				if (!blocked)
				{
					GatherSampleLight(spot2, pvs2, pointnormal2, sampled2, styles, 0, l->miptex, surface);
				}
				for (j = 0; j < ALLSTYLES && styles[j] != 255; j++)
				{
					for (int x = 0; x < 3; x++)
					{
						sampled[j][x] = (1.0 - l->translucent_v[x]) * sampled[j][x] + l->translucent_v[x] * sampled2[j][x];
					}
				}
			}
		}
	}
}
void BuildFacelights(const int facenum)
{
	unsigned char f_styles[ALLSTYLES];
	sample_t *fl_samples[ALLSTYLES];
	lightinfo_t l;
	int i;
	int j;
	int k;
	Patch *patch;
	byte pvs[(MAX_MAP_LEAFS + 7) / 8];
	int thisoffset = -1, lastoffset = -1;
	vec3_t spot2, normal2;
	byte pvs2[(MAX_MAP_LEAFS + 7) / 8];
	int thisoffset2 = -1, lastoffset2 = -1;

	auto *f = &g_bspfaces[facenum];

	//
	// some surfaces don't need lightmaps
	//
	f->lightofs = -1;
	for (j = 0; j < ALLSTYLES; j++)
	{
		f_styles[j] = 255;
	}

	if (g_bsptexinfo[f->texinfo].flags & TEX_SPECIAL)
	{
		for (j = 0; j < MAXLIGHTMAPS; j++)
		{
			f->styles[j] = 255;
		}
		return; // non-lit texture
	}

	f_styles[0] = 0;
	if (g_face_patches[facenum] && g_face_patches[facenum]->emitstyle)
	{
		f_styles[1] = g_face_patches[facenum]->emitstyle;
	}

	memset(&l, 0, sizeof(l));

	l.surfnum = facenum;
	l.face = f;

	VectorCopy(g_translucenttextures[g_bsptexinfo[f->texinfo].miptex], l.translucent_v);
	l.translucent_b = !VectorCompare(l.translucent_v, vec3_origin);
	l.miptex = g_bsptexinfo[f->texinfo].miptex;

	//
	// rotate plane
	//
	auto *plane = getPlaneFromFace(f);
	VectorCopy(plane->normal, l.facenormal);
	l.facedist = plane->dist;

	CalcFaceVectors(&l);
	CalcFaceExtents(&l);
	CalcPoints(&l);
	CalcLightmap(&l, f_styles);

	auto lightmapwidth = l.texsize[0] + 1;
	auto lightmapheight = l.texsize[1] + 1;
	auto size = lightmapwidth * lightmapheight;
	hlassume(size <= MAX_SINGLEMAP, assume_MAX_SINGLEMAP);

	facelight[facenum].numsamples = l.numsurfpt;

	for (k = 0; k < ALLSTYLES; k++)
	{
		fl_samples[k] = (sample_t *)calloc(l.numsurfpt, sizeof(sample_t));
		hlassume(fl_samples[k] != nullptr, assume_NoMemory);
	}
	for (patch = g_face_patches[facenum]; patch; patch = patch->next)
	{
		hlassume(patch->totalstyle_all = new unsigned char[ALLSTYLES], assume_NoMemory);
		hlassume(patch->samplelight_all = new vec3_t[ALLSTYLES], assume_NoMemory);
		hlassume(patch->totallight_all = new vec3_t[ALLSTYLES], assume_NoMemory);
		hlassume(patch->directlight_all = new vec3_t[ALLSTYLES], assume_NoMemory);
		for (j = 0; j < ALLSTYLES; j++)
		{
			patch->totalstyle_all[j] = 255;
			VectorClear(patch->samplelight_all[j]);
			VectorClear(patch->totallight_all[j]);
			VectorClear(patch->directlight_all[j]);
		}
		patch->totalstyle_all[0] = 0;
	}

	auto *sample_wallflags = new int[(2 * l.lmcache_side + 1) * (2 * l.lmcache_side + 1)];
	auto *spot = l.surfpt[0];
	for (i = 0; i < l.numsurfpt; i++, spot += 3)
	{

		for (k = 0; k < ALLSTYLES; k++)
		{
			VectorCopy(spot, fl_samples[k][i].pos);
			fl_samples[k][i].surface = l.surfpt_surface[i];
		}

		int s, t;
		vec3_t centernormal;
		auto s_center = (i % lightmapwidth) * l.lmcache_density + l.lmcache_offset;
		auto t_center = (i / lightmapwidth) * l.lmcache_density + l.lmcache_offset;
		auto sizehalf = 0.5 * g_blur * l.lmcache_density;
		auto subsamples = 0.0;
		VectorCopy(l.lmcache_normal[s_center + l.lmcachewidth * t_center], centernormal);
		{
			auto s_origin = s_center;
			auto t_origin = t_center;
			for (s = s_center - l.lmcache_side; s <= s_center + l.lmcache_side; s++)
			{
				for (t = t_center - l.lmcache_side; t <= t_center + l.lmcache_side; t++)
				{
					auto *pwallflags = &sample_wallflags[(s - s_center + l.lmcache_side) + (2 * l.lmcache_side + 1) * (t - t_center + l.lmcache_side)];
					*pwallflags = l.lmcache_wallflags[s + l.lmcachewidth * t];
				}
			}
			// project the "shadow" from the origin point
			for (s = s_center - l.lmcache_side; s <= s_center + l.lmcache_side; s++)
			{
				for (t = t_center - l.lmcache_side; t <= t_center + l.lmcache_side; t++)
				{
					auto *pwallflags = &sample_wallflags[(s - s_center + l.lmcache_side) + (2 * l.lmcache_side + 1) * (t - t_center + l.lmcache_side)];
					int coord[2] = {s - s_origin, t - t_origin};
					auto axis = abs(coord[0]) >= abs(coord[1]) ? 0 : 1;
					auto sign = coord[axis] >= 0 ? 1 : -1;
					auto blocked1 = false;
					auto blocked2 = false;
					for (int dist = 1; dist < abs(coord[axis]); dist++)
					{
						int test1[2];
						int test2[2];
						test1[axis] = test2[axis] = sign * dist;
						auto intercept = (double)coord[1 - axis] * (double)test1[axis] / (double)coord[axis];
						test1[1 - axis] = (int)floor(intercept + 0.01);
						test2[1 - axis] = (int)ceil(intercept - 0.01);
						if (abs(test1[0] + s_origin - s_center) > l.lmcache_side || abs(test1[1] + t_origin - t_center) > l.lmcache_side ||
							abs(test2[0] + s_origin - s_center) > l.lmcache_side || abs(test2[1] + t_origin - t_center) > l.lmcache_side)
						{
							Warning("HLRAD_AVOIDWALLBLEED: internal error. Contact vluzacn@163.com concerning this issue.");
							continue;
						}
						auto wallflags1 = sample_wallflags[(test1[0] + s_origin - s_center + l.lmcache_side) + (2 * l.lmcache_side + 1) * (test1[1] + t_origin - t_center + l.lmcache_side)];
						auto wallflags2 = sample_wallflags[(test2[0] + s_origin - s_center + l.lmcache_side) + (2 * l.lmcache_side + 1) * (test2[1] + t_origin - t_center + l.lmcache_side)];
						if (wallflags1 & WALLFLAG_NUDGED)
						{
							blocked1 = true;
						}
						if (wallflags2 & WALLFLAG_NUDGED)
						{
							blocked2 = true;
						}
					}
					if (blocked1 && blocked2)
					{
						*pwallflags |= WALLFLAG_SHADOWED;
					}
				}
			}
		}
		for (int pass = 0; pass < 2; pass++)
		{
			for (s = s_center - l.lmcache_side; s <= s_center + l.lmcache_side; s++)
			{
				for (t = t_center - l.lmcache_side; t <= t_center + l.lmcache_side; t++)
				{
					auto weighting = (qmin(0.5, sizehalf - (s - s_center)) - qmax(-0.5, -sizehalf - (s - s_center))) * (qmin(0.5, sizehalf - (t - t_center)) - qmax(-0.5, -sizehalf - (t - t_center)));
					auto wallflags = sample_wallflags[(s - s_center + l.lmcache_side) + (2 * l.lmcache_side + 1) * (t - t_center + l.lmcache_side)];
					if (wallflags & (WALLFLAG_BLOCKED | WALLFLAG_SHADOWED))
					{
						continue;
					}
					if (wallflags & WALLFLAG_NUDGED)
					{
						if (pass == 0)
						{
							continue;
						}
					}
					auto pos = s + l.lmcachewidth * t;
					// when blur distance (g_blur) is large, the subsample can be very far from the original lightmap sample (aligned with interval TEXTURE_STEP (16.0))
					// in some cases such as a thin cylinder, the subsample can even grow into the opposite side
					// as a result, when exposed to a directional light, the light on the cylinder may "leak" into the opposite dark side
					// this correction limits the effect of blur distance when the normal changes very fast
					// this correction will not break the smoothness that HLRAD_GROWSAMPLE ensures
					auto weighting_correction = DotProduct(l.lmcache_normal[pos], centernormal);
					weighting_correction = (weighting_correction > 0) ? weighting_correction * weighting_correction : 0;
					weighting = weighting * weighting_correction;
					for (j = 0; j < ALLSTYLES && f_styles[j] != 255; j++)
					{
						VectorMA(fl_samples[j][i].light, weighting, l.lmcache[pos][j], fl_samples[j][i].light);
					}
					subsamples += weighting;
				}
			}
			if (subsamples > NORMAL_EPSILON)
			{
				break;
			}
			else
			{
				subsamples = 0.0;
				for (j = 0; j < ALLSTYLES && f_styles[j] != 255; j++)
				{
					VectorClear(fl_samples[j][i].light);
				}
			}
		}
		if (subsamples > 0)
		{
			for (j = 0; j < ALLSTYLES && f_styles[j] != 255; j++)
			{
				VectorScale(fl_samples[j][i].light, 1.0 / subsamples, fl_samples[j][i].light);
			}
		}
	} // end of i loop
	delete[] sample_wallflags;

	// average up the direct light on each patch for radiosity
	AddSamplesToPatches((const sample_t **)fl_samples, f_styles, facenum, &l);
	{
		for (patch = g_face_patches[facenum]; patch; patch = patch->next)
		{
			// LRC:
			unsigned istyle;
			if (patch->samples <= ON_EPSILON * ON_EPSILON)
				patch->samples = 0.0;
			if (patch->samples)
			{
				for (istyle = 0; istyle < ALLSTYLES && patch->totalstyle_all[istyle] != 255; istyle++)
				{
					vec3_t v;
					VectorScale(patch->samplelight_all[istyle], 1.0f / patch->samples, v);
					VectorAdd(patch->directlight_all[istyle], v, patch->directlight_all[istyle]);
				}
			}
			// LRC (ends)
		}
	}
	for (patch = g_face_patches[facenum]; patch; patch = patch->next)
	{
		// get the PVS for the pos to limit the number of checks
		if (!g_bspvisdatasize)
		{
			memset(pvs, 255, (g_bspmodels[0].visleafs + 7) / 8);
			lastoffset = -1;
		}
		else
		{
			auto *leaf = PointInLeaf(patch->origin);

			thisoffset = leaf->visofs;
			if (patch == g_face_patches[facenum] || thisoffset != lastoffset)
			{
				if (thisoffset == -1)
				{
					memset(pvs, 0, (g_bspmodels[0].visleafs + 7) / 8);
				}
				else
				{
					DecompressVis(&g_bspvisdata[leaf->visofs], pvs, sizeof(pvs));
				}
			}
			lastoffset = thisoffset;
		}
		if (l.translucent_b)
		{
			if (!g_bspvisdatasize)
			{
				memset(pvs2, 255, (g_bspmodels[0].visleafs + 7) / 8);
				lastoffset2 = -1;
			}
			else
			{
				VectorMA(patch->origin, -(g_translucentdepth + 2 * PATCH_HUNT_OFFSET), l.facenormal, spot2);
				auto *leaf2 = PointInLeaf(spot2);

				thisoffset2 = leaf2->visofs;
				if (l.numsurfpt == 0 || thisoffset2 != lastoffset2)
				{
					if (thisoffset2 == -1)
					{
						memset(pvs2, 0, (g_bspmodels[0].visleafs + 7) / 8);
					}
					else
					{
						DecompressVis(&g_bspvisdata[leaf2->visofs], pvs2, sizeof(pvs2));
					}
				}
				lastoffset2 = thisoffset2;
			}
			vec3_t frontsampled[ALLSTYLES], backsampled[ALLSTYLES];
			for (j = 0; j < ALLSTYLES; j++)
			{
				VectorClear(frontsampled[j]);
				VectorClear(backsampled[j]);
			}
			VectorSubtract(vec3_origin, l.facenormal, normal2);
			GatherSampleLight(patch->origin, pvs, l.facenormal, frontsampled,
							  patch->totalstyle_all, 1, l.miptex, facenum);
			GatherSampleLight(spot2, pvs2, normal2, backsampled,
							  patch->totalstyle_all, 1, l.miptex, facenum);
			for (j = 0; j < ALLSTYLES && patch->totalstyle_all[j] != 255; j++)
			{
				for (int x = 0; x < 3; x++)
				{
					patch->totallight_all[j][x] += (1.0 - l.translucent_v[x]) * frontsampled[j][x] + l.translucent_v[x] * backsampled[j][x];
				}
			}
		}
		else
		{
			GatherSampleLight(patch->origin, pvs, l.facenormal,
							  patch->totallight_all,
							  patch->totalstyle_all, 1, l.miptex, facenum);
		}
	}

	// light from dlight_threshold and above is sent out, but the
	// texture itself should still be full bright

	// if( VectorAvg( face_patches[facenum]->baselight ) >= dlight_threshold)       // Now all lighted surfaces glow
	{
		// LRC:
		if (g_face_patches[facenum])
		{
			for (j = 0; j < ALLSTYLES && f_styles[j] != 255; j++)
			{
				if (f_styles[j] == g_face_patches[facenum]->emitstyle)
				{
					break;
				}
			}
			if (j == ALLSTYLES)
			{
				if (++g_stylewarningcount >= g_stylewarningnext)
				{
					g_stylewarningnext = g_stylewarningcount * 2;
					Warning("Too many direct light styles on a face(?,?,?)");
					Warning(" total %d warnings for too many styles", g_stylewarningcount);
				}
			}
			else
			{
				if (f_styles[j] == 255)
				{
					f_styles[j] = g_face_patches[facenum]->emitstyle;
				}

				auto *s = fl_samples[j];
				for (i = 0; i < l.numsurfpt; i++, s++)
				{
					VectorAdd(s->light, g_face_patches[facenum]->baselight, s->light);
				}
			}
		}
		// LRC (ends)
	}
	// samples
	{
		auto *fl = &facelight[facenum];
		vec_t maxlights[ALLSTYLES];
		for (j = 0; j < ALLSTYLES && f_styles[j] != 255; j++)
		{
			maxlights[j] = 0;
			for (i = 0; i < fl->numsamples; i++)
			{
				auto b = VectorMaximum(fl_samples[j][i].light);
				maxlights[j] = qmax(maxlights[j], b);
			}
			if (maxlights[j] <= g_corings[f_styles[j]] * 0.1) // light is too dim, discard this style to reduce RAM usage
			{
				if (maxlights[j] > g_maxdiscardedlight + NORMAL_EPSILON)
				{
					ThreadLock();
					if (maxlights[j] > g_maxdiscardedlight + NORMAL_EPSILON)
					{
						g_maxdiscardedlight = maxlights[j];
						VectorCopy(g_face_centroids[facenum], g_maxdiscardedpos);
					}
					ThreadUnlock();
				}
				maxlights[j] = 0;
			}
		}
		for (k = 0; k < MAXLIGHTMAPS; k++)
		{
			int bestindex = -1;
			if (k == 0)
			{
				bestindex = 0;
			}
			else
			{
				vec_t bestmaxlight = 0;
				for (j = 1; j < ALLSTYLES && f_styles[j] != 255; j++)
				{
					if (maxlights[j] > bestmaxlight + NORMAL_EPSILON)
					{
						bestmaxlight = maxlights[j];
						bestindex = j;
					}
				}
			}
			if (bestindex != -1)
			{
				maxlights[bestindex] = 0;
				f->styles[k] = f_styles[bestindex];
				fl->samples[k] = new sample_t[fl->numsamples];
				hlassume(fl->samples[k] != nullptr, assume_NoMemory);
				memcpy(fl->samples[k], fl_samples[bestindex], fl->numsamples * sizeof(sample_t));
			}
			else
			{
				f->styles[k] = 255;
				fl->samples[k] = nullptr;
			}
		}
		for (j = 1; j < ALLSTYLES && f_styles[j] != 255; j++)
		{
			if (maxlights[j] > g_maxdiscardedlight + NORMAL_EPSILON)
			{
				ThreadLock();
				if (maxlights[j] > g_maxdiscardedlight + NORMAL_EPSILON)
				{
					g_maxdiscardedlight = maxlights[j];
					VectorCopy(g_face_centroids[facenum], g_maxdiscardedpos);
				}
				ThreadUnlock();
			}
		}
		for (j = 0; j < ALLSTYLES; j++)
		{
			delete fl_samples[j];
		}
	}
	// patches
	for (patch = g_face_patches[facenum]; patch; patch = patch->next)
	{
		vec_t maxlights[ALLSTYLES];
		for (j = 0; j < ALLSTYLES && patch->totalstyle_all[j] != 255; j++)
		{
			maxlights[j] = VectorMaximum(patch->totallight_all[j]);
		}
		for (k = 0; k < MAXLIGHTMAPS; k++)
		{
			int bestindex = -1;
			if (k == 0)
			{
				bestindex = 0;
			}
			else
			{
				vec_t bestmaxlight = 0;
				for (j = 1; j < ALLSTYLES && patch->totalstyle_all[j] != 255; j++)
				{
					if (maxlights[j] > bestmaxlight + NORMAL_EPSILON)
					{
						bestmaxlight = maxlights[j];
						bestindex = j;
					}
				}
			}
			if (bestindex != -1)
			{
				maxlights[bestindex] = 0;
				patch->totalstyle[k] = patch->totalstyle_all[bestindex];
				VectorCopy(patch->totallight_all[bestindex], patch->totallight[k]);
			}
			else
			{
				patch->totalstyle[k] = 255;
			}
		}
		for (j = 1; j < ALLSTYLES && patch->totalstyle_all[j] != 255; j++)
		{
			if (maxlights[j] > g_maxdiscardedlight + NORMAL_EPSILON)
			{
				ThreadLock();
				if (maxlights[j] > g_maxdiscardedlight + NORMAL_EPSILON)
				{
					g_maxdiscardedlight = maxlights[j];
					VectorCopy(patch->origin, g_maxdiscardedpos);
				}
				ThreadUnlock();
			}
		}
		for (j = 0; j < ALLSTYLES && patch->totalstyle_all[j] != 255; j++)
		{
			maxlights[j] = VectorMaximum(patch->directlight_all[j]);
		}
		for (k = 0; k < MAXLIGHTMAPS; k++)
		{
			auto bestindex = -1;
			if (k == 0)
			{
				bestindex = 0;
			}
			else
			{
				vec_t bestmaxlight = 0;
				for (j = 1; j < ALLSTYLES && patch->totalstyle_all[j] != 255; j++)
				{
					if (maxlights[j] > bestmaxlight + NORMAL_EPSILON)
					{
						bestmaxlight = maxlights[j];
						bestindex = j;
					}
				}
			}
			if (bestindex != -1)
			{
				maxlights[bestindex] = 0;
				patch->directstyle[k] = patch->totalstyle_all[bestindex];
				VectorCopy(patch->directlight_all[bestindex], patch->directlight[k]);
			}
			else
			{
				patch->directstyle[k] = 255;
			}
		}
		for (j = 1; j < ALLSTYLES && patch->totalstyle_all[j] != 255; j++)
		{
			if (maxlights[j] > g_maxdiscardedlight + NORMAL_EPSILON)
			{
				ThreadLock();
				if (maxlights[j] > g_maxdiscardedlight + NORMAL_EPSILON)
				{
					g_maxdiscardedlight = maxlights[j];
					VectorCopy(patch->origin, g_maxdiscardedpos);
				}
				ThreadUnlock();
			}
		}
		delete[] patch->totalstyle_all;
		patch->totalstyle_all = nullptr;
		delete[] patch->samplelight_all;
		patch->samplelight_all = nullptr;
		delete[] patch->totallight_all;
		patch->totallight_all = nullptr;
		delete[] patch->directlight_all;
		patch->directlight_all = nullptr;
	}
	delete[] l.lmcache;
	delete[] l.lmcache_normal;
	delete[] l.lmcache_wallflags;
	delete[] l.surfpt_position;
	delete[] l.surfpt_surface;
}

// =====================================================================================
//  PrecompLightmapOffsets
// =====================================================================================
void PrecompLightmapOffsets()
{
	int lightstyles;

	g_bsplightdatasize = 0;

	for (int facenum = 0; facenum < g_bspnumfaces; facenum++)
	{
		auto *f = &g_bspfaces[facenum];
		auto *fl = &facelight[facenum];

		if (g_bsptexinfo[f->texinfo].flags & TEX_SPECIAL)
		{
			continue; // non-lit texture
		}

		{
			int i, j, k;
			vec_t maxlights[ALLSTYLES];
			{
				vec3_t maxlights1[ALLSTYLES];
				vec3_t maxlights2[ALLSTYLES];
				for (j = 0; j < ALLSTYLES; j++)
				{
					VectorClear(maxlights1[j]);
					VectorClear(maxlights2[j]);
				}
				for (k = 0; k < MAXLIGHTMAPS && f->styles[k] != 255; k++)
				{
					for (i = 0; i < fl->numsamples; i++)
					{
						VectorCompareMaximum(maxlights1[f->styles[k]], fl->samples[k][i].light, maxlights1[f->styles[k]]);
					}
				}
				int numpatches;
				const int *patches;
				GetTriangulationPatches(facenum, &numpatches, &patches); // collect patches and their neighbors

				for (i = 0; i < numpatches; i++)
				{
					auto *patch = &g_patches[patches[i]];
					for (k = 0; k < MAXLIGHTMAPS && patch->totalstyle[k] != 255; k++)
					{
						VectorCompareMaximum(maxlights2[patch->totalstyle[k]], patch->totallight[k], maxlights2[patch->totalstyle[k]]);
					}
				}
				for (j = 0; j < ALLSTYLES; j++)
				{
					vec3_t v;
					VectorAdd(maxlights1[j], maxlights2[j], v);
					maxlights[j] = VectorMaximum(v);
					if (maxlights[j] <= g_corings[j] * 0.01)
					{
						if (maxlights[j] > g_maxdiscardedlight + NORMAL_EPSILON)
						{
							g_maxdiscardedlight = maxlights[j];
							VectorCopy(g_face_centroids[facenum], g_maxdiscardedpos);
						}
						maxlights[j] = 0;
					}
				}
			}
			unsigned char oldstyles[MAXLIGHTMAPS];
			sample_t *oldsamples[MAXLIGHTMAPS];
			for (k = 0; k < MAXLIGHTMAPS; k++)
			{
				oldstyles[k] = f->styles[k];
				oldsamples[k] = fl->samples[k];
			}
			for (k = 0; k < MAXLIGHTMAPS; k++)
			{
				unsigned char beststyle = 255;
				if (k == 0)
				{
					beststyle = 0;
				}
				else
				{
					vec_t bestmaxlight = 0;
					for (j = 1; j < ALLSTYLES; j++)
					{
						if (maxlights[j] > bestmaxlight + NORMAL_EPSILON)
						{
							bestmaxlight = maxlights[j];
							beststyle = j;
						}
					}
				}
				if (beststyle != 255)
				{
					maxlights[beststyle] = 0;
					f->styles[k] = beststyle;
					fl->samples[k] = new sample_t[fl->numsamples];
					hlassume(fl->samples[k] != nullptr, assume_NoMemory);
					for (i = 0; i < MAXLIGHTMAPS && oldstyles[i] != 255; i++)
					{
						if (oldstyles[i] == f->styles[k])
						{
							break;
						}
					}
					if (i < MAXLIGHTMAPS && oldstyles[i] != 255)
					{
						memcpy(fl->samples[k], oldsamples[i], fl->numsamples * sizeof(sample_t));
					}
					else
					{
						memcpy(fl->samples[k], oldsamples[0], fl->numsamples * sizeof(sample_t)); // copy 'sample.pos' from style 0 to the new style - because 'sample.pos' is actually the same for all styles! (why did we decide to store it in many places?)
						for (j = 0; j < fl->numsamples; j++)
						{
							VectorClear(fl->samples[k][j].light);
						}
					}
				}
				else
				{
					f->styles[k] = 255;
					fl->samples[k] = nullptr;
				}
			}
			for (j = 1; j < ALLSTYLES; j++)
			{
				if (maxlights[j] > g_maxdiscardedlight + NORMAL_EPSILON)
				{
					g_maxdiscardedlight = maxlights[j];
					VectorCopy(g_face_centroids[facenum], g_maxdiscardedpos);
				}
			}
			for (k = 0; k < MAXLIGHTMAPS && oldstyles[k] != 255; k++)
			{
				delete oldsamples[k];
			}
		}

		for (lightstyles = 0; lightstyles < MAXLIGHTMAPS; lightstyles++)
		{
			if (f->styles[lightstyles] == 255)
			{
				break;
			}
		}

		if (!lightstyles)
		{
			continue;
		}

		f->lightofs = g_bsplightdatasize;
		g_bsplightdatasize += fl->numsamples * 3 * lightstyles;
		hlassume(g_bsplightdatasize <= g_max_map_lightdata, assume_MAX_MAP_LIGHTING); // lightdata
	}
}
void ReduceLightmap()
{
	auto *oldlightdata = new byte[g_bsplightdatasize];
	hlassume(oldlightdata != nullptr, assume_NoMemory);
	memcpy(oldlightdata, g_bsplightdata, g_bsplightdatasize);
	g_bsplightdatasize = 0;

	for (int facenum = 0; facenum < g_bspnumfaces; facenum++)
	{
		auto *f = &g_bspfaces[facenum];
		auto *fl = &facelight[facenum];
		if (g_bsptexinfo[f->texinfo].flags & TEX_SPECIAL)
		{
			continue; // non-lit texture
		}
		// just need to zero the lightmap so that it won't contribute to lightdata size
		if (IntForKey(g_face_entity[facenum], "zhlt_striprad"))
		{
			f->lightofs = g_bsplightdatasize;
			for (int k = 0; k < MAXLIGHTMAPS; k++)
			{
				f->styles[k] = 255;
			}
			continue;
		}
		if (f->lightofs == -1)
		{
			continue;
		}

		int k;
		unsigned char oldstyles[MAXLIGHTMAPS];
		auto oldofs = f->lightofs;
		f->lightofs = g_bsplightdatasize;
		for (k = 0; k < MAXLIGHTMAPS; k++)
		{
			oldstyles[k] = f->styles[k];
			f->styles[k] = 255;
		}
		int numstyles = 0;
		for (k = 0; k < MAXLIGHTMAPS && oldstyles[k] != 255; k++)
		{
			unsigned char maxb = 0;
			for (int i = 0; i < fl->numsamples; i++)
			{
				unsigned char *v = &oldlightdata[oldofs + fl->numsamples * 3 * k + i * 3];
				maxb = qmax(maxb, VectorMaximum(v));
			}
			if (maxb <= 0) // black
			{
				continue;
			}
			f->styles[numstyles] = oldstyles[k];
			hlassume(g_bsplightdatasize + fl->numsamples * 3 * (numstyles + 1) <= g_max_map_lightdata, assume_MAX_MAP_LIGHTING);
			memcpy(&g_bsplightdata[f->lightofs + fl->numsamples * 3 * numstyles], &oldlightdata[oldofs + fl->numsamples * 3 * k], fl->numsamples * 3);
			numstyles++;
		}
		g_bsplightdatasize += fl->numsamples * 3 * numstyles;
	}
	delete oldlightdata;
}

// Change the sample light right under a mdl file entity's origin.
// Use this when "mdl" in shadow has incorrect brightness.

const int MLH_MAXFACECOUNT = 16;
const int MLH_MAXSAMPLECOUNT = 4;
const vec_t MLH_LEFT = 0;
const vec_t MLH_RIGHT = 1;

struct mdllight_t
{
	vec3_t origin;
	vec3_t floor;
	struct
	{
		int num;
		struct
		{
			bool exist;
			int seq;
		} style[ALLSTYLES];
		struct
		{
			int num;
			vec3_t pos;
			unsigned char *(style[ALLSTYLES]);
		} sample[MLH_MAXSAMPLECOUNT];
		int samplecount;
	} face[MLH_MAXFACECOUNT];
	int facecount;
};

auto MLH_AddFace(mdllight_t *ml, int facenum) -> int
{
	auto *f = &g_bspfaces[facenum];
	int i, j;
	for (i = 0; i < ml->facecount; i++)
	{
		if (ml->face[i].num == facenum)
		{
			return -1;
		}
	}
	if (ml->facecount >= MLH_MAXFACECOUNT)
	{
		return -1;
	}
	i = ml->facecount;
	ml->facecount++;
	ml->face[i].num = facenum;
	ml->face[i].samplecount = 0;
	for (j = 0; j < ALLSTYLES; j++)
	{
		ml->face[i].style[j].exist = false;
	}
	for (j = 0; j < MAXLIGHTMAPS && f->styles[j] != 255; j++)
	{
		ml->face[i].style[f->styles[j]].exist = true;
		ml->face[i].style[f->styles[j]].seq = j;
	}
	return i;
}
void MLH_AddSample(mdllight_t *ml, int facenum, int w, int h, int s, int t, const vec3_t pos)
{
	auto *f = &g_bspfaces[facenum];
	int i;
	auto r = MLH_AddFace(ml, facenum);
	if (r == -1)
	{
		return;
	}
	auto size = w * h;
	auto num = s + w * t;
	for (i = 0; i < ml->face[r].samplecount; i++)
	{
		if (ml->face[r].sample[i].num == num)
		{
			return;
		}
	}
	if (ml->face[r].samplecount >= MLH_MAXSAMPLECOUNT)
	{
		return;
	}
	i = ml->face[r].samplecount;
	ml->face[r].samplecount++;
	ml->face[r].sample[i].num = num;
	VectorCopy(pos, ml->face[r].sample[i].pos);
	for (int j = 0; j < ALLSTYLES; j++)
	{
		if (ml->face[r].style[j].exist)
		{
			ml->face[r].sample[i].style[j] = &g_bsplightdata[f->lightofs + (num + size * ml->face[r].style[j].seq) * 3];
		}
	}
}
void MLH_CalcExtents(const BSPLumpFace *f, int *texturemins, int *extents)
{
	int bmins[2];
	int bmaxs[2];

	GetFaceExtents(f - g_bspfaces, bmins, bmaxs);
	for (int i = 0; i < 2; i++)
	{
		texturemins[i] = bmins[i] * TEXTURE_STEP;
		extents[i] = (bmaxs[i] - bmins[i]) * TEXTURE_STEP;
	}
}
void MLH_GetSamples_r(mdllight_t *ml, int nodenum, const float *start, const float *end)
{
	if (nodenum < 0)
		return;
	auto *node = &g_bspnodes[nodenum];
	float mid[3];
	auto *plane = &g_bspplanes[node->planenum];
	auto front = DotProduct(start, plane->normal) - plane->dist;
	auto back = DotProduct(end, plane->normal) - plane->dist;
	auto side = front < 0;
	if ((back < 0) == side)
	{
		MLH_GetSamples_r(ml, node->children[side], start, end);
		return;
	}
	auto frac = front / (front - back);
	mid[0] = start[0] + (end[0] - start[0]) * frac;
	mid[1] = start[1] + (end[1] - start[1]) * frac;
	mid[2] = start[2] + (end[2] - start[2]) * frac;
	MLH_GetSamples_r(ml, node->children[side], start, mid);
	if (ml->facecount > 0)
	{
		return;
	}
	{
		for (int i = 0; i < node->numfaces; i++)
		{
			auto *f = &g_bspfaces[node->firstface + i];
			auto *tex = &g_bsptexinfo[f->texinfo];
			const auto *texname = GetTextureByNumber(f->texinfo);
			if (!strncmp(texname, "sky", 3))
			{
				continue;
			}
			if (f->lightofs == -1)
			{
				continue;
			}
			auto s = (int)(DotProduct(mid, tex->vecs[0]) + tex->vecs[0][3]);
			auto t = (int)(DotProduct(mid, tex->vecs[1]) + tex->vecs[1][3]);
			int texturemins[2], extents[2];
			MLH_CalcExtents(f, texturemins, extents);
			if (s < texturemins[0] || t < texturemins[1])
			{
				continue;
			}
			auto ds = s - texturemins[0];
			auto dt = t - texturemins[1];
			if (ds > extents[0] || dt > extents[1])
			{
				continue;
			}
			ds >>= 4;
			dt >>= 4;
			MLH_AddSample(ml, node->firstface + i, extents[0] / TEXTURE_STEP + 1, extents[1] / TEXTURE_STEP + 1, ds, dt, mid);
			break;
		}
	}
	if (ml->facecount > 0)
	{
		VectorCopy(mid, ml->floor);
		return;
	}
	MLH_GetSamples_r(ml, node->children[!side], mid, end);
}
void MLH_mdllightCreate(mdllight_t *ml)
{
	// code from Quake
	float p[3];
	float end[3];
	ml->facecount = 0;
	VectorCopy(ml->origin, ml->floor);
	VectorCopy(ml->origin, p);
	VectorCopy(ml->origin, end);
	end[2] -= 2048;
	MLH_GetSamples_r(ml, 0, p, end);
}

auto MLH_CopyLight(const vec3_t from, const vec3_t to) -> int
{
	int i, j, k, count = 0;
	mdllight_t mlfrom, mlto;
	VectorCopy(from, mlfrom.origin);
	VectorCopy(to, mlto.origin);
	MLH_mdllightCreate(&mlfrom);
	MLH_mdllightCreate(&mlto);
	if (mlfrom.facecount == 0 || mlfrom.face[0].samplecount == 0)
		return -1;
	for (i = 0; i < mlto.facecount; ++i)
		for (j = 0; j < mlto.face[i].samplecount; ++j, ++count)
			for (k = 0; k < ALLSTYLES; ++k)
				if (mlto.face[i].style[k].exist && mlfrom.face[0].style[k].exist)
				{
					VectorCopy(mlfrom.face[0].sample[0].style[k], mlto.face[i].sample[j].style[k]);
				}
	return count;
}

void MdlLightHack()
{
	vec3_t origin1, origin2;
	int used = 0, countent = 0, countsample = 0;
	for (int ient = 0; ient < g_numentities; ++ient)
	{
		auto *ent1 = &g_entities[ient];
		auto *target = ValueForKey(ent1, "zhlt_copylight");
		if (!strcmp(target, ""))
			continue;
		used = 1;
		auto *ent2 = FindTargetEntity(target);
		if (ent2 == nullptr)
		{
			Warning("target entity '%s' not found", target);
			continue;
		}
		GetVectorForKey(ent1, "origin", origin1);
		GetVectorForKey(ent2, "origin", origin2);
		auto r = MLH_CopyLight(origin2, origin1);
		if (r < 0)
			Warning("can not copy light from (%f,%f,%f)", origin2[0], origin2[1], origin2[2]);
		else
		{
			countent += 1;
			countsample += r;
		}
	}
	if (used)
		Log("Adjust mdl light: modified %d samples for %d entities\n", countsample, countent);
}

struct facelightlist_t
{
	int facenum;
	facelightlist_t *next;
};

static facelightlist_t *g_dependentfacelights[MAX_MAP_FACES];

// =====================================================================================
//  CreateFacelightDependencyList
// =====================================================================================
void CreateFacelightDependencyList()
{
	int i;
	facelightlist_t *item;

	for (i = 0; i < MAX_MAP_FACES; i++)
	{
		g_dependentfacelights[i] = nullptr;
	}

	// for each face
	for (int facenum = 0; facenum < g_bspnumfaces; facenum++)
	{
		auto *f = &g_bspfaces[facenum];
		auto *fl = &facelight[facenum];
		if (g_bsptexinfo[f->texinfo].flags & TEX_SPECIAL)
		{
			continue;
		}

		for (int k = 0; k < MAXLIGHTMAPS && f->styles[k] != 255; k++)
		{
			for (i = 0; i < fl->numsamples; i++)
			{
				auto surface = fl->samples[k][i].surface; // that surface contains at least one sample from this face
				if (0 <= surface && surface < g_bspnumfaces)
				{
					// insert this face into the dependency list of that surface
					for (item = g_dependentfacelights[surface]; item != nullptr; item = item->next)
					{
						if (item->facenum == facenum)
							break;
					}
					if (item)
					{
						continue;
					}

					item = new facelightlist_t;
					hlassume(item != nullptr, assume_NoMemory);
					item->facenum = facenum;
					item->next = g_dependentfacelights[surface];
					g_dependentfacelights[surface] = item;
				}
			}
		}
	}
}

// =====================================================================================
//  FreeFacelightDependencyList
// =====================================================================================
void FreeFacelightDependencyList()
{
	for (int i = 0; i < MAX_MAP_FACES; i++)
	{
		while (g_dependentfacelights[i])
		{
			auto *item = g_dependentfacelights[i];
			g_dependentfacelights[i] = item->next;
			delete item;
		}
	}
}

// =====================================================================================
//  ScaleDirectLights
// =====================================================================================
void ScaleDirectLights()
{
	for (int facenum = 0; facenum < g_bspnumfaces; facenum++)
	{
		auto *f = &g_bspfaces[facenum];

		if (g_bsptexinfo[f->texinfo].flags & TEX_SPECIAL)
		{
			continue;
		}

		auto *fl = &facelight[facenum];

		for (int k = 0; k < MAXLIGHTMAPS && f->styles[k] != 255; k++)
		{
			for (int i = 0; i < fl->numsamples; i++)
			{
				auto *samp = &fl->samples[k][i];
			}
		}
	}
}

// =====================================================================================
//  AddPatchLights
//    This function is run multithreaded
// =====================================================================================
void AddPatchLights(int facenum)
{
	auto *f = &g_bspfaces[facenum];

	if (g_bsptexinfo[f->texinfo].flags & TEX_SPECIAL)
	{
		return;
	}

	for (auto *item = g_dependentfacelights[facenum]; item != nullptr; item = item->next)
	{
		auto *f_other = &g_bspfaces[item->facenum];
		auto *fl_other = &facelight[item->facenum];
		for (int k = 0; k < MAXLIGHTMAPS && f_other->styles[k] != 255; k++)
		{
			for (int i = 0; i < fl_other->numsamples; i++)
			{
				auto *samp = &fl_other->samples[k][i];
				if (samp->surface != facenum)
				{ // the sample is not in this surface
					continue;
				}

				{
					vec3_t v;

					int style = f_other->styles[k];
					InterpolateSampleLight(samp->pos, samp->surface, 1, &style, &v);

					VectorAdd(samp->light, v, v);
					if (VectorMaximum(v) >= g_corings[f_other->styles[k]])
					{
						VectorCopy(v, samp->light);
					}
					else
					{
						if (VectorMaximum(v) > g_maxdiscardedlight + NORMAL_EPSILON)
						{
							ThreadLock();
							if (VectorMaximum(v) > g_maxdiscardedlight + NORMAL_EPSILON)
							{
								g_maxdiscardedlight = VectorMaximum(v);
								VectorCopy(samp->pos, g_maxdiscardedpos);
							}
							ThreadUnlock();
						}
					}
				}
			} // loop samples
		}
	}
}

// =====================================================================================
//  FinalLightFace
//      Add the indirect lighting on top of the direct lighting and save into final map format
// =====================================================================================
void FinalLightFace(const int facenum)
{
	int i, j, k;
	vec3_t lb;
	int lightstyles;
	int lbi[3];

	auto *f = &g_bspfaces[facenum];
	auto *fl = &facelight[facenum];

	if (g_bsptexinfo[f->texinfo].flags & TEX_SPECIAL)
	{
		return; // non-lit texture
	}

	for (lightstyles = 0; lightstyles < MAXLIGHTMAPS; lightstyles++)
	{
		if (f->styles[lightstyles] == 255)
		{
			break;
		}
	}

	if (!lightstyles)
	{
		return;
	}

	auto minlight = FloatForKey(g_face_entity[facenum], "_minlight") * 255; // seedee
	minlight = (minlight > 255) ? 255 : minlight;

	const auto *texname = GetTextureByNumber(f->texinfo);

	if (!strncasecmp(texname, "%", 1)) // If texture name has % flag //seedee
	{
		auto texnameLength = strlen(texname);

		if (texnameLength > 1)
		{
			auto *minlightValue = new char[texnameLength + 1];
			auto valueIndex = 0;
			auto i = 1;

			if (texname[i] >= '0' && texname[i] <= '9') // Loop until non-digit is found or we run out of space
			{
				while (texname[i] != '\0' && texname[i] >= '0' && texname[i] <= '9' && valueIndex < texnameLength)
				{
					minlightValue[valueIndex++] = texname[i++];
				}
				minlightValue[valueIndex] = '\0';
				minlight = atoi(minlightValue);
				delete[] minlightValue;
				minlight = (minlight > 255) ? 255 : minlight;
			}
		}
		else
		{
			minlight = 255;
		}
	}
	for (auto it = s_minlights.begin(); it != s_minlights.end(); it++)
	{
		if (!strcasecmp(texname, it->name.c_str()))
		{
			float minlightValue = it->value * 255.0f;
			minlight = static_cast<int>(minlightValue);
			minlight = (minlight > 255) ? 255 : minlight;
		}
	}
	auto *original_basiclight = (vec3_t *)calloc(fl->numsamples, sizeof(vec3_t));
	auto *final_basiclight = (int(*)[3])calloc(fl->numsamples, sizeof(int[3]));
	hlassume(original_basiclight != nullptr, assume_NoMemory);
	hlassume(final_basiclight != nullptr, assume_NoMemory);
	for (k = 0; k < lightstyles; k++)
	{
		auto *samp = fl->samples[k];
		for (j = 0; j < fl->numsamples; j++, samp++)
		{
			VectorCopy(samp->light, lb);
			if (f->styles[0] != 0)
			{
				Warning("wrong f->styles[0]");
			}
			VectorCompareMaximum(lb, vec3_origin, lb);
			if (k == 0)
			{
				VectorCopy(lb, original_basiclight[j]);
			}
			else
			{
				VectorAdd(lb, original_basiclight[j], lb);
			}
			// ------------------------------------------------------------------------
			// Changes by Adam Foster - afoster@compsoc.man.ac.uk
			// colour lightscale used by -scale and -colourscale, default values is 2.0
			lb[0] *= 2.0; // g_colour_lightscale[1] = 2
			lb[1] *= 2.0; // g_colour_lightscale[2] = 2
			lb[2] *= 2.0; // g_colour_lightscale[3] = 2
			// ------------------------------------------------------------------------

			// clip from the bottom first
			for (i = 0; i < 3; i++)
			{
				if (lb[i] < minlight)
				{
					lb[i] = minlight;
				}
			}

			// ------------------------------------------------------------------------
			// Changes by Adam Foster - afoster@compsoc.man.ac.uk

			// AJM: your code is formatted really wierd, and i cant understand a damn thing.
			//      so i reformatted it into a somewhat readable "normal" fashion. :P
			lb[0] = (float)pow(lb[0] / 256.0f, 0.55) * 256.0f; // 0.55 DEFAULT_COLOUR_GAMMA_RED a.k.a '-gamma r g b'
			lb[1] = (float)pow(lb[1] / 256.0f, 0.55) * 256.0f; // 0.55 DEFAULT_COLOUR_GAMMA_GREEN
			lb[2] = (float)pow(lb[2] / 256.0f, 0.55) * 256.0f; // 0.55 DEFAULT_COLOUR_GAMMA_BLUE

			// Two different ways of adding noise to the lightmap - colour jitter
			// (red, green and blue channels are independent), and mono jitter
			// (monochromatic noise). For simulating dithering, on the cheap. :)

			// Tends to create seams between adjacent polygons, so not ideal.

			// Got really weird results when it was set to limit values to 256.0f - it
			// was as if r, g or b could wrap, going close to zero.

			// clip from the top
			{
				vec_t max = VectorMaximum(lb);
				if (g_limitthreshold >= 0 && max > g_limitthreshold)
				{
					VectorScale(lb, g_limitthreshold / max, lb);
				}
			}
			for (i = 0; i < 3; ++i)
				if (lb[i] < 0) // never i guess?
					lb[i] = 0;
			// ------------------------------------------------------------------------
			for (i = 0; i < 3; ++i)
			{
				lbi[i] = (int)floor(lb[i] + 0.5);
				if (lbi[i] < 0)
					lbi[i] = 0;
			}
			if (k == 0)
			{
				VectorCopy(lbi, final_basiclight[j]);
			}
			else
			{
				VectorSubtract(lbi, final_basiclight[j], lbi);
			}
			for (i = 0; i < 3; ++i)
			{
				if (lbi[i] < 0)
					lbi[i] = 0;
				if (lbi[i] > 255)
					lbi[i] = 255;
			}
			{
				unsigned char *colors = &g_bsplightdata[f->lightofs + k * fl->numsamples * 3 + j * 3];

				colors[0] = (unsigned char)lbi[0];
				colors[1] = (unsigned char)lbi[1];
				colors[2] = (unsigned char)lbi[2];
			}
		}
	}
	delete[] original_basiclight;
	delete[] final_basiclight;
}

// LRC
vec3_t totallight_default = {0, 0, 0};

// LRC - utility for getting the right totallight value from a patch
auto GetTotalLight(Patch *patch, int style) -> vec3_t *
{
	for (int i = 0; i < MAXLIGHTMAPS && patch->totalstyle[i] != 255; i++)
	{
		if (patch->totalstyle[i] == style)
		{
			return &(patch->totallight[i]);
		}
	}
	return &totallight_default;
}
