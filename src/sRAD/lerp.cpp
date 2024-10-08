#include <vector>
#include <algorithm>

#include "hlrad.h"
#include "log.h"

struct interpolation_t
{
	struct Point
	{
		int patchnum;
		vec_t weight;
	};

	bool isbiased;
	vec_t totalweight;
	std::vector<Point> points;
};

struct localtriangulation_t
{
	struct Wedge
	{
		enum eShape
		{
			eTriangular,
			eConvex,
			eConcave,
			eSquareLeft,
			eSquareRight,
		};

		eShape shape;
		int leftpatchnum;
		vec3_t leftspot;
		vec3_t leftdirection;
		// right side equals to the left side of the next wedge

		vec3_t wedgenormal; // for custom usage
	};
	struct HullPoint
	{
		vec3_t spot;
		vec3_t direction;
	};

	dplane_t plane;
	Winding winding;
	vec3_t center; // center is on the plane

	vec3_t normal;
	int patchnum;
	std::vector<int> neighborfaces; // including the face itself

	std::vector<Wedge> sortedwedges;		 // in clockwise order (same as Winding)
	std::vector<HullPoint> sortedhullpoints; // in clockwise order (same as Winding)
};

struct facetriangulation_t
{
	struct Wall
	{
		vec3_t points[2];
		vec3_t direction;
		vec3_t normal;
	};

	int facenum;
	std::vector<int> neighbors; // including the face itself
	std::vector<Wall> walls;
	std::vector<localtriangulation_t *> localtriangulations;
	std::vector<int> usedpatches;
};

facetriangulation_t *g_facetriangulations[MAX_MAP_FACES];

static auto CalcAdaptedSpot(const localtriangulation_t *lt, const vec3_t position, int surface, vec3_t spot) -> bool
// If the surface formed by the face and its neighbor faces is not flat, the surface should be unfolded onto the face plane
// CalcAdaptedSpot calculates the coordinate of the unfolded spot on the face plane from the original position on the surface
// CalcAdaptedSpot(center) = {0,0,0}
// CalcAdaptedSpot(position on the face plane) = position - center
// Param position: must include g_face_offset
{
	int i;
	vec3_t surfacespot;
	vec3_t phongnormal;
	vec_t frac;
	vec3_t middle;
	vec3_t v;

	for (i = 0; i < (int)lt->neighborfaces.size(); i++)
	{
		if (lt->neighborfaces[i] == surface)
		{
			break;
		}
	}
	if (i == (int)lt->neighborfaces.size())
	{
		VectorClear(spot);
		return false;
	}

	VectorSubtract(position, lt->center, surfacespot);
	vec_t dot = DotProduct(surfacespot, lt->normal);
	VectorMA(surfacespot, -dot, lt->normal, spot);

	// use phong normal instead of face normal, because phong normal is a continuous function
	GetPhongNormal(surface, position, phongnormal);
	dot = DotProduct(spot, phongnormal);
	if (fabs(dot) > ON_EPSILON)
	{
		frac = DotProduct(surfacespot, phongnormal) / dot;
		frac = qmax(0, qmin(frac, 1)); // to correct some extreme cases
	}
	else
	{
		frac = 0;
	}
	VectorScale(spot, frac, middle);

	vec_t dist = VectorLength(spot);
	VectorSubtract(surfacespot, middle, v);
	vec_t dist2 = VectorLength(middle) + VectorLength(v);

	if (dist > ON_EPSILON && fabs(dist2 - dist) > ON_EPSILON)
	{
		VectorScale(spot, dist2 / dist, spot);
	}
	return true;
}

static auto GetAngle(const vec3_t leftdirection, const vec3_t rightdirection, const vec3_t normal) -> vec_t
{
	vec3_t v;

	CrossProduct(rightdirection, leftdirection, v);
	vec_t angle = atan2(DotProduct(v, normal), DotProduct(rightdirection, leftdirection));

	return angle;
}

static auto GetAngleDiff(vec_t angle, vec_t base) -> vec_t
{
	vec_t diff = angle - base;
	if (diff < 0)
	{
		diff += 2 * Q_PI;
	}
	return diff;
}

static auto GetFrac(const vec3_t leftspot, const vec3_t rightspot, const vec3_t direction, const vec3_t normal) -> vec_t
{
	vec3_t v;
	vec_t frac;

	CrossProduct(direction, normal, v);
	vec_t dot1 = DotProduct(leftspot, v);
	vec_t dot2 = DotProduct(rightspot, v);

	// dot1 <= 0 < dot2
	if (dot1 >= -NORMAL_EPSILON)
	{
		frac = 0.0;
	}
	else if (dot2 <= NORMAL_EPSILON)
	{
		frac = 1.0;
	}
	else
	{
		frac = dot1 / (dot1 - dot2);
		frac = qmax(0, qmin(frac, 1));
	}

	return frac;
}

static auto GetDirection(const vec3_t spot, const vec3_t normal, vec3_t direction_out) -> vec_t
{
	vec_t dot = DotProduct(spot, normal);
	VectorMA(spot, -dot, normal, direction_out);
	return VectorNormalize(direction_out);
}

static auto CalcWeight(const localtriangulation_t *lt, const vec3_t spot, vec_t *weight_out) -> bool
// It returns true when the point is inside the hull region (with boundary), even if weight = 0.
{
	vec3_t direction;
	bool istoofar;
	vec_t ratio;
	int i;
	vec_t angle;
	std::vector<vec_t> angles;

	if (GetDirection(spot, lt->normal, direction) <= 2 * ON_EPSILON)
	{
		*weight_out = 1.0;
		return true;
	}

	if ((int)lt->sortedhullpoints.size() == 0)
	{
		*weight_out = 0.0;
		return false;
	}

	angles.resize((int)lt->sortedhullpoints.size());
	for (i = 0; i < (int)lt->sortedhullpoints.size(); i++)
	{
		angle = GetAngle(lt->sortedhullpoints[i].direction, direction, lt->normal);
		angles[i] = GetAngleDiff(angle, 0);
	}
	int j = 0;
	for (i = 1; i < (int)lt->sortedhullpoints.size(); i++)
	{
		if (angles[i] < angles[j])
		{
			j = i;
		}
	}
	const auto *hp1 = &lt->sortedhullpoints[j];
	const auto *hp2 = &lt->sortedhullpoints[(j + 1) % (int)lt->sortedhullpoints.size()];

	vec_t frac = GetFrac(hp1->spot, hp2->spot, direction, lt->normal);

	vec_t len = (1 - frac) * DotProduct(hp1->spot, direction) + frac * DotProduct(hp2->spot, direction);
	vec_t dist = DotProduct(spot, direction);
	if (len <= ON_EPSILON / 4 || dist > len + 2 * ON_EPSILON)
	{
		istoofar = true;
		ratio = 1.0;
	}
	else if (dist >= len - ON_EPSILON)
	{
		istoofar = false; // if we change this "false" to "true", we will see many places turned "green" in "-drawlerp" mode
		ratio = 1.0;	  // in order to prevent excessively small weight
	}
	else
	{
		istoofar = false;
		ratio = dist / len;
		ratio = qmax(0, qmin(ratio, 1));
	}

	*weight_out = 1 - ratio;
	return !istoofar;
}

static void CalcInterpolation_Square(const localtriangulation_t *lt, int i, const vec3_t spot, interpolation_t *interp)
{
	vec_t weights[4];
	vec3_t normal1;
	vec3_t normal2;
	vec3_t normal;
	vec_t frac;
	vec_t frac_near;
	vec_t frac_far;
	vec_t ratio;
	vec3_t mid_far;
	vec3_t mid_near;
	vec3_t test;

	auto *w1 = &lt->sortedwedges[i];
	auto *w2 = &lt->sortedwedges[(i + 1) % (int)lt->sortedwedges.size()];
	auto *w3 = &lt->sortedwedges[(i + 2) % (int)lt->sortedwedges.size()];
	if (w1->shape != localtriangulation_t::Wedge::eSquareLeft || w2->shape != localtriangulation_t::Wedge::eSquareRight)
	{
		Error("CalcInterpolation_Square: internal error: not square.");
	}

	weights[0] = 0.0;
	weights[1] = 0.0;
	weights[2] = 0.0;
	weights[3] = 0.0;

	// find mid_near on (o,p3), mid_far on (p1,p2), spot on (mid_near,mid_far)
	CrossProduct(w1->leftdirection, lt->normal, normal1);
	VectorNormalize(normal1);
	CrossProduct(w2->wedgenormal, lt->normal, normal2);
	VectorNormalize(normal2);
	vec_t dot1 = DotProduct(spot, normal1) - 0;
	vec_t dot2 = DotProduct(spot, normal2) - DotProduct(w3->leftspot, normal2);
	if (dot1 <= NORMAL_EPSILON)
	{
		frac = 0.0;
	}
	else if (dot2 <= NORMAL_EPSILON)
	{
		frac = 1.0;
	}
	else
	{
		frac = dot1 / (dot1 + dot2);
		frac = qmax(0, qmin(frac, 1));
	}

	dot1 = DotProduct(w3->leftspot, normal1) - 0;
	dot2 = 0 - DotProduct(w3->leftspot, normal2);
	if (dot1 <= NORMAL_EPSILON)
	{
		frac_near = 1.0;
	}
	else if (dot2 <= NORMAL_EPSILON)
	{
		frac_near = 0.0;
	}
	else
	{
		frac_near = (frac * dot2) / ((1 - frac) * dot1 + frac * dot2);
	}
	VectorScale(w3->leftspot, frac_near, mid_near);

	dot1 = DotProduct(w2->leftspot, normal1) - 0;
	dot2 = DotProduct(w1->leftspot, normal2) - DotProduct(w3->leftspot, normal2);
	if (dot1 <= NORMAL_EPSILON)
	{
		frac_far = 1.0;
	}
	else if (dot2 <= NORMAL_EPSILON)
	{
		frac_far = 0.0;
	}
	else
	{
		frac_far = (frac * dot2) / ((1 - frac) * dot1 + frac * dot2);
	}
	VectorScale(w1->leftspot, 1 - frac_far, mid_far);
	VectorMA(mid_far, frac_far, w2->leftspot, mid_far);

	CrossProduct(lt->normal, w3->leftdirection, normal);
	VectorNormalize(normal);
	vec_t dot = DotProduct(spot, normal) - 0;
	dot1 = (1 - frac_far) * DotProduct(w1->leftspot, normal) + frac_far * DotProduct(w2->leftspot, normal) - 0;
	if (dot <= NORMAL_EPSILON)
	{
		ratio = 0.0;
	}
	else if (dot >= dot1)
	{
		ratio = 1.0;
	}
	else
	{
		ratio = dot / dot1;
		ratio = qmax(0, qmin(ratio, 1));
	}

	VectorScale(mid_near, 1 - ratio, test);
	VectorMA(test, ratio, mid_far, test);
	VectorSubtract(test, spot, test);

	weights[0] += 0.5 * (1 - ratio) * (1 - frac_near);
	weights[3] += 0.5 * (1 - ratio) * frac_near;
	weights[1] += 0.5 * ratio * (1 - frac_far);
	weights[2] += 0.5 * ratio * frac_far;

	// find mid_near on (o,p1), mid_far on (p2,p3), spot on (mid_near,mid_far)
	CrossProduct(lt->normal, w3->leftdirection, normal1);
	VectorNormalize(normal1);
	CrossProduct(w1->wedgenormal, lt->normal, normal2);
	VectorNormalize(normal2);
	dot1 = DotProduct(spot, normal1) - 0;
	dot2 = DotProduct(spot, normal2) - DotProduct(w1->leftspot, normal2);
	if (dot1 <= NORMAL_EPSILON)
	{
		frac = 0.0;
	}
	else if (dot2 <= NORMAL_EPSILON)
	{
		frac = 1.0;
	}
	else
	{
		frac = dot1 / (dot1 + dot2);
		frac = qmax(0, qmin(frac, 1));
	}

	dot1 = DotProduct(w1->leftspot, normal1) - 0;
	dot2 = 0 - DotProduct(w1->leftspot, normal2);
	if (dot1 <= NORMAL_EPSILON)
	{
		frac_near = 1.0;
	}
	else if (dot2 <= NORMAL_EPSILON)
	{
		frac_near = 0.0;
	}
	else
	{
		frac_near = (frac * dot2) / ((1 - frac) * dot1 + frac * dot2);
	}
	VectorScale(w1->leftspot, frac_near, mid_near);

	dot1 = DotProduct(w2->leftspot, normal1) - 0;
	dot2 = DotProduct(w3->leftspot, normal2) - DotProduct(w1->leftspot, normal2);
	if (dot1 <= NORMAL_EPSILON)
	{
		frac_far = 1.0;
	}
	else if (dot2 <= NORMAL_EPSILON)
	{
		frac_far = 0.0;
	}
	else
	{
		frac_far = (frac * dot2) / ((1 - frac) * dot1 + frac * dot2);
	}
	VectorScale(w3->leftspot, 1 - frac_far, mid_far);
	VectorMA(mid_far, frac_far, w2->leftspot, mid_far);

	CrossProduct(w1->leftdirection, lt->normal, normal);
	VectorNormalize(normal);
	dot = DotProduct(spot, normal) - 0;
	dot1 = (1 - frac_far) * DotProduct(w3->leftspot, normal) + frac_far * DotProduct(w2->leftspot, normal) - 0;
	if (dot <= NORMAL_EPSILON)
	{
		ratio = 0.0;
	}
	else if (dot >= dot1)
	{
		ratio = 1.0;
	}
	else
	{
		ratio = dot / dot1;
		ratio = qmax(0, qmin(ratio, 1));
	}

	VectorScale(mid_near, 1 - ratio, test);
	VectorMA(test, ratio, mid_far, test);
	VectorSubtract(test, spot, test);

	weights[0] += 0.5 * (1 - ratio) * (1 - frac_near);
	weights[1] += 0.5 * (1 - ratio) * frac_near;
	weights[3] += 0.5 * ratio * (1 - frac_far);
	weights[2] += 0.5 * ratio * frac_far;

	interp->isbiased = false;
	interp->totalweight = 1.0;
	interp->points.resize(4);
	interp->points[0].patchnum = lt->patchnum;
	interp->points[0].weight = weights[0];
	interp->points[1].patchnum = w1->leftpatchnum;
	interp->points[1].weight = weights[1];
	interp->points[2].patchnum = w2->leftpatchnum;
	interp->points[2].weight = weights[2];
	interp->points[3].patchnum = w3->leftpatchnum;
	interp->points[3].weight = weights[3];
}

static void CalcInterpolation(const localtriangulation_t *lt, const vec3_t spot, interpolation_t *interp)
// The interpolation function is defined over the entire plane, so CalcInterpolation never fails.
{
	vec3_t direction;

	int i;
	vec_t angle;
	std::vector<vec_t> angles;

	if (GetDirection(spot, lt->normal, direction) <= 2 * ON_EPSILON)
	{
		// spot happens to be at the center
		interp->isbiased = false;
		interp->totalweight = 1.0;
		interp->points.resize(1);
		interp->points[0].patchnum = lt->patchnum;
		interp->points[0].weight = 1.0;
		return;
	}

	if ((int)lt->sortedwedges.size() == 0) // this local triangulation only has center patch
	{
		interp->isbiased = true;
		interp->totalweight = 1.0;
		interp->points.resize(1);
		interp->points[0].patchnum = lt->patchnum;
		interp->points[0].weight = 1.0;
		return;
	}

	// Find the wedge with minimum non-negative angle (counterclockwise) pass the spot
	angles.resize((int)lt->sortedwedges.size());
	for (i = 0; i < (int)lt->sortedwedges.size(); i++)
	{
		angle = GetAngle(lt->sortedwedges[i].leftdirection, direction, lt->normal);
		angles[i] = GetAngleDiff(angle, 0);
	}
	int j = 0;
	for (i = 1; i < (int)lt->sortedwedges.size(); i++)
	{
		if (angles[i] < angles[j])
		{
			j = i;
		}
	}
	auto *w = &lt->sortedwedges[j];
	auto *wnext = &lt->sortedwedges[(j + 1) % (int)lt->sortedwedges.size()];

	// Different wedge types have different interpolation methods
	switch (w->shape)
	{
	case localtriangulation_t::Wedge::eSquareLeft:
	case localtriangulation_t::Wedge::eSquareRight:
	case localtriangulation_t::Wedge::eTriangular:
		// w->wedgenormal is undefined
		{
			vec_t dist;
			bool istoofar;
			vec_t ratio;

			vec_t frac = GetFrac(w->leftspot, wnext->leftspot, direction, lt->normal);

			vec_t len = (1 - frac) * DotProduct(w->leftspot, direction) + frac * DotProduct(wnext->leftspot, direction);
			dist = DotProduct(spot, direction);
			if (len <= ON_EPSILON / 4 || dist > len + 2 * ON_EPSILON)
			{
				istoofar = true;
				ratio = 1.0;
			}
			else if (dist >= len - ON_EPSILON)
			{
				istoofar = false;
				ratio = 1.0;
			}
			else
			{
				istoofar = false;
				ratio = dist / len;
				ratio = qmax(0, qmin(ratio, 1));
			}

			if (istoofar)
			{
				interp->isbiased = true;
				interp->totalweight = 1.0;
				interp->points.resize(2);
				interp->points[0].patchnum = w->leftpatchnum;
				interp->points[0].weight = 1 - frac;
				interp->points[1].patchnum = wnext->leftpatchnum;
				interp->points[1].weight = frac;
			}
			else if (w->shape == localtriangulation_t::Wedge::eSquareLeft)
			{
				i = w - &lt->sortedwedges[0];
				CalcInterpolation_Square(lt, i, spot, interp);
			}
			else if (w->shape == localtriangulation_t::Wedge::eSquareRight)
			{
				i = w - &lt->sortedwedges[0];
				i = (i - 1 + (int)lt->sortedwedges.size()) % (int)lt->sortedwedges.size();
				CalcInterpolation_Square(lt, i, spot, interp);
			}
			else
			{
				interp->isbiased = false;
				interp->totalweight = 1.0;
				interp->points.resize(3);
				interp->points[0].patchnum = lt->patchnum;
				interp->points[0].weight = 1 - ratio;
				interp->points[1].patchnum = w->leftpatchnum;
				interp->points[1].weight = ratio * (1 - frac);
				interp->points[2].patchnum = wnext->leftpatchnum;
				interp->points[2].weight = ratio * frac;
			}
		}
		break;
	case localtriangulation_t::Wedge::eConvex:
		// w->wedgenormal is the unit vector pointing from w->leftspot to wnext->leftspot
		{
			vec_t frac;

			vec_t dot1 = DotProduct(w->leftspot, w->wedgenormal) - DotProduct(spot, w->wedgenormal);
			vec_t dot2 = DotProduct(wnext->leftspot, w->wedgenormal) - DotProduct(spot, w->wedgenormal);
			vec_t dot = 0 - DotProduct(spot, w->wedgenormal);
			// for eConvex type: dot1 < dot < dot2

			if (dot1 >= -NORMAL_EPSILON) // 0 <= dot1 < dot < dot2
			{
				interp->isbiased = true;
				interp->totalweight = 1.0;
				interp->points.resize(1);
				interp->points[0].patchnum = w->leftpatchnum;
				interp->points[0].weight = 1.0;
			}
			else if (dot2 <= NORMAL_EPSILON) // dot1 < dot < dot2 <= 0
			{
				interp->isbiased = true;
				interp->totalweight = 1.0;
				interp->points.resize(1);
				interp->points[0].patchnum = wnext->leftpatchnum;
				interp->points[0].weight = 1.0;
			}
			else if (dot > 0) // dot1 < 0 < dot < dot2
			{
				frac = dot1 / (dot1 - dot);
				frac = qmax(0, qmin(frac, 1));

				interp->isbiased = true;
				interp->totalweight = 1.0;
				interp->points.resize(2);
				interp->points[0].patchnum = w->leftpatchnum;
				interp->points[0].weight = 1 - frac;
				interp->points[1].patchnum = lt->patchnum;
				interp->points[1].weight = frac;
			}
			else // dot1 < dot <= 0 < dot2
			{
				frac = dot / (dot - dot2);
				frac = qmax(0, qmin(frac, 1));

				interp->isbiased = true;
				interp->totalweight = 1.0;
				interp->points.resize(2);
				interp->points[0].patchnum = lt->patchnum;
				interp->points[0].weight = 1 - frac;
				interp->points[1].patchnum = wnext->leftpatchnum;
				interp->points[1].weight = frac;
			}
		}
		break;
	case localtriangulation_t::Wedge::eConcave:
	{
		vec_t len;
		vec_t dist;
		vec_t ratio;

		if (DotProduct(spot, w->wedgenormal) < 0) // the spot is closer to the left edge than the right edge
		{
			len = DotProduct(w->leftspot, w->leftdirection);
			dist = DotProduct(spot, w->leftdirection);
			if (dist <= NORMAL_EPSILON)
			{
				interp->isbiased = true;
				interp->totalweight = 1.0;
				interp->points.resize(1);
				interp->points[0].patchnum = lt->patchnum;
				interp->points[0].weight = 1.0;
			}
			else if (dist >= len)
			{
				interp->isbiased = true;
				interp->totalweight = 1.0;
				interp->points.resize(1);
				interp->points[0].patchnum = w->leftpatchnum;
				interp->points[0].weight = 1.0;
			}
			else
			{
				ratio = dist / len;
				ratio = qmax(0, qmin(ratio, 1));

				interp->isbiased = true;
				interp->totalweight = 1.0;
				interp->points.resize(2);
				interp->points[0].patchnum = lt->patchnum;
				interp->points[0].weight = 1 - ratio;
				interp->points[1].patchnum = w->leftpatchnum;
				interp->points[1].weight = ratio;
			}
		}
		else // the spot is closer to the right edge than the left edge
		{
			len = DotProduct(wnext->leftspot, wnext->leftdirection);
			dist = DotProduct(spot, wnext->leftdirection);
			if (dist <= NORMAL_EPSILON)
			{
				interp->isbiased = true;
				interp->totalweight = 1.0;
				interp->points.resize(1);
				interp->points[0].patchnum = lt->patchnum;
				interp->points[0].weight = 1.0;
			}
			else if (dist >= len)
			{
				interp->isbiased = true;
				interp->totalweight = 1.0;
				interp->points.resize(1);
				interp->points[0].patchnum = wnext->leftpatchnum;
				interp->points[0].weight = 1.0;
			}
			else
			{
				ratio = dist / len;
				ratio = qmax(0, qmin(ratio, 1));

				interp->isbiased = true;
				interp->totalweight = 1.0;
				interp->points.resize(2);
				interp->points[0].patchnum = lt->patchnum;
				interp->points[0].weight = 1 - ratio;
				interp->points[1].patchnum = wnext->leftpatchnum;
				interp->points[1].weight = ratio;
			}
		}
	}
	break;
	default:
		Error("CalcInterpolation: internal error: invalid wedge type.");
		break;
	}
}

static void ApplyInterpolation(const interpolation_t *interp, int numstyles, const int *styles, vec3_t *outs)
{
	int j;
	for (j = 0; j < numstyles; j++)
	{
		VectorClear(outs[j]);
	}
	if (interp->totalweight <= 0)
	{
		return;
	}
	for (int i = 0; i < (int)interp->points.size(); i++)
	{
		for (j = 0; j < numstyles; j++)
		{
			const vec3_t *b = GetTotalLight(&g_patches[interp->points[i].patchnum], styles[j]);
			VectorMA(outs[j], interp->points[i].weight / interp->totalweight, *b, outs[j]);
		}
	}
}

// =====================================================================================
//  InterpolateSampleLight
// =====================================================================================
void InterpolateSampleLight(const vec3_t position, int surface, int numstyles, const int *styles, vec3_t *outs)
{
	try
	{
		std::vector<vec_t> localweights;
		std::vector<interpolation_t *> localinterps;

		int i;
		int j;
		int n;
		const localtriangulation_t *lt;
		vec3_t spot;
		vec_t weight;
		vec3_t v;
		vec_t bestdist;

		if (surface < 0 || surface >= g_bspnumfaces)
		{
			Error("InterpolateSampleLight: internal error: surface number out of range.");
		}
		const auto *ft = g_facetriangulations[surface];
		auto *maininterp = new interpolation_t;
		maininterp->points.reserve(64);

		// Calculate local interpolations and their weights
		localweights.resize(0);
		localinterps.resize(0);
		for (i = 0; i < (int)ft->neighbors.size(); i++) // for this face and each of its neighbors
		{
			const auto *ft2 = g_facetriangulations[ft->neighbors[i]];
			for (j = 0; j < (int)ft2->localtriangulations.size(); j++) // for each patch on that face
			{
				lt = ft2->localtriangulations[j];
				if (!CalcAdaptedSpot(lt, position, surface, spot))
				{
					continue;
				}
				if (!CalcWeight(lt, spot, &weight))
				{
					continue;
				}
				auto *interp = new interpolation_t;
				interp->points.reserve(4);
				CalcInterpolation(lt, spot, interp);

				localweights.push_back(weight);
				localinterps.push_back(interp);
			}
		}

		// Combine into one interpolation
		maininterp->isbiased = false;
		maininterp->totalweight = 0;
		maininterp->points.resize(0);
		for (i = 0; i < (int)localinterps.size(); i++)
		{
			if (localinterps[i]->isbiased)
			{
				maininterp->isbiased = true;
			}
			for (j = 0; j < (int)localinterps[i]->points.size(); j++)
			{
				weight = localinterps[i]->points[j].weight * localweights[i];
				if (g_patches[localinterps[i]->points[j].patchnum].flags == ePatchFlagOutside)
				{
					weight *= 0.01;
				}
				n = (int)maininterp->points.size();
				maininterp->points.resize(n + 1);
				maininterp->points[n].patchnum = localinterps[i]->points[j].patchnum;
				maininterp->points[n].weight = weight;
				maininterp->totalweight += weight;
			}
		}
		if (maininterp->totalweight > 0)
		{
			ApplyInterpolation(maininterp, numstyles, styles, outs);
		}
		else
		{
			// try again, don't multiply localweights[i] (which equals to 0)
			maininterp->isbiased = false;
			maininterp->totalweight = 0;
			maininterp->points.resize(0);
			for (i = 0; i < (int)localinterps.size(); i++)
			{
				if (localinterps[i]->isbiased)
				{
					maininterp->isbiased = true;
				}
				for (j = 0; j < (int)localinterps[i]->points.size(); j++)
				{
					weight = localinterps[i]->points[j].weight;
					if (g_patches[localinterps[i]->points[j].patchnum].flags == ePatchFlagOutside)
					{
						weight *= 0.01;
					}
					n = (int)maininterp->points.size();
					maininterp->points.resize(n + 1);
					maininterp->points[n].patchnum = localinterps[i]->points[j].patchnum;
					maininterp->points[n].weight = weight;
					maininterp->totalweight += weight;
				}
			}
			if (maininterp->totalweight > 0)
			{
				ApplyInterpolation(maininterp, numstyles, styles, outs);
			}
			else
			{
				// worst case, simply use the nearest patch

				const localtriangulation_t *best = nullptr;
				for (i = 0; i < (int)ft->localtriangulations.size(); i++)
				{
					lt = ft->localtriangulations[i];
					VectorCopy(position, v);
					snap_to_winding(lt->winding, lt->plane, v);
					VectorSubtract(v, position, v);
					vec_t dist = VectorLength(v);
					if (best == nullptr || dist < bestdist - ON_EPSILON)
					{
						best = lt;
						bestdist = dist;
					}
				}

				if (best)
				{
					lt = best;
					VectorSubtract(position, lt->center, spot);
					vec_t dot = DotProduct(spot, lt->normal);
					VectorMA(spot, -dot, lt->normal, spot);
					CalcInterpolation(lt, spot, maininterp);

					maininterp->totalweight = 0;
					for (j = 0; j < (int)maininterp->points.size(); j++)
					{
						if (g_patches[maininterp->points[j].patchnum].flags == ePatchFlagOutside)
						{
							maininterp->points[j].weight *= 0.01;
						}
						maininterp->totalweight += maininterp->points[j].weight;
					}
					ApplyInterpolation(maininterp, numstyles, styles, outs);
				}
				else
				{
					maininterp->isbiased = true;
					maininterp->totalweight = 0;
					maininterp->points.resize(0);
					ApplyInterpolation(maininterp, numstyles, styles, outs);
				}
			}
		}
		delete maininterp;

		for (i = 0; i < (int)localinterps.size(); i++)
		{
			delete localinterps[i];
		}
	}
	catch (std::bad_alloc)
	{
		hlassume(false, assume_NoMemory);
	}
}

static auto TestLineSegmentIntersectWall(const facetriangulation_t *facetrian, const vec3_t p1, const vec3_t p2) -> bool
{
	for (int i = 0; i < (int)facetrian->walls.size(); i++)
	{
		const auto *wall = &facetrian->walls[i];
		vec_t bottom = DotProduct(wall->points[0], wall->direction);
		vec_t top = DotProduct(wall->points[1], wall->direction);
		vec_t front = DotProduct(p1, wall->normal) - DotProduct(wall->points[0], wall->normal);
		vec_t back = DotProduct(p2, wall->normal) - DotProduct(wall->points[0], wall->normal);
		if (front > ON_EPSILON && back > ON_EPSILON || front < -ON_EPSILON && back < -ON_EPSILON)
		{
			continue;
		}
		vec_t dot1 = DotProduct(p1, wall->direction);
		vec_t dot2 = DotProduct(p2, wall->direction);
		if (fabs(front) <= 2 * ON_EPSILON && fabs(back) <= 2 * ON_EPSILON)
		{
			top = qmin(top, qmax(dot1, dot2));
			bottom = qmax(bottom, qmin(dot1, dot2));
		}
		else
		{
			vec_t frac = front / (front - back);
			frac = qmax(0, qmin(frac, 1));
			vec_t dot = dot1 + frac * (dot2 - dot1);
			top = qmin(top, dot);
			bottom = qmax(bottom, dot);
		}
		if (top - bottom >= -ON_EPSILON)
		{
			return true;
		}
	}

	return false;
}

static auto TestFarPatch(const localtriangulation_t *lt, const vec3_t p2, const Winding &p2winding) -> bool
{
	int i;
	vec3_t v;
	vec_t dist;

	vec_t size1 = 0;
	for (i = 0; i < lt->winding.m_NumPoints; i++)
	{
		VectorSubtract(lt->winding.m_Points[i], lt->center, v);
		dist = VectorLength(v);
		if (dist > size1)
		{
			size1 = dist;
		}
	}

	vec_t size2 = 0;
	for (i = 0; i < p2winding.m_NumPoints; i++)
	{
		VectorSubtract(p2winding.m_Points[i], p2, v);
		dist = VectorLength(v);
		if (dist > size2)
		{
			size2 = dist;
		}
	}

	VectorSubtract(p2, lt->center, v);
	dist = VectorLength(v);

	return dist > 1.4 * (size1 + size2);
}

#define TRIANGLE_SHAPE_THRESHOLD (115.0 * Q_PI / 180)
// If one of the angles in a triangle exceeds this threshold, the most distant point will be removed or the triangle will break into a convex-type wedge.

static void GatherPatches(localtriangulation_t *lt, const facetriangulation_t *facetrian)
{
	int i;
	const Patch *patch2;
	vec3_t v;
	localtriangulation_t::Wedge point;
	std::vector<localtriangulation_t::Wedge> points;
	std::vector<std::pair<vec_t, int>> angles;
	vec_t angle;

	points.resize(0);
	for (i = 0; i < (int)lt->neighborfaces.size(); i++)
	{
		int facenum2 = lt->neighborfaces[i];
		const dplane_t *dp2 = getPlaneFromFaceNumber(facenum2);
		for (patch2 = g_face_patches[facenum2]; patch2; patch2 = patch2->next)
		{
			int patchnum2 = patch2 - g_patches;

			point.leftpatchnum = patchnum2;
			VectorMA(patch2->origin, -PATCH_HUNT_OFFSET, dp2->normal, v);

			// Do permission tests using the original position of the patch
			if (patchnum2 == lt->patchnum || point_in_winding(lt->winding, lt->plane, v))
			{
				continue;
			}
			if (facenum2 != facetrian->facenum && TestLineSegmentIntersectWall(facetrian, lt->center, v))
			{
				continue;
			}
			if (TestFarPatch(lt, v, *patch2->winding))
			{
				continue;
			}

			// Store the adapted position of the patch
			if (!CalcAdaptedSpot(lt, v, facenum2, point.leftspot))
			{
				continue;
			}
			if (GetDirection(point.leftspot, lt->normal, point.leftdirection) <= 2 * ON_EPSILON)
			{
				continue;
			}
			points.push_back(point);
		}
	}

	// Sort the patches into clockwise order
	angles.resize((int)points.size());
	for (i = 0; i < (int)points.size(); i++)
	{
		angle = GetAngle(points[0].leftdirection, points[i].leftdirection, lt->normal);
		if (i == 0)
		{
			angle = 0.0;
		}
		angles[i].first = GetAngleDiff(angle, 0);
		angles[i].second = i;
	}
	std::sort(angles.begin(), angles.end());

	lt->sortedwedges.resize((int)points.size());
	for (i = 0; i < (int)points.size(); i++)
	{
		lt->sortedwedges[i] = points[angles[i].second];
	}
}

static void PurgePatches(localtriangulation_t *lt)
{
	std::vector<localtriangulation_t::Wedge> points;
	int i;
	std::vector<int> next;
	std::vector<int> prev;
	std::vector<int> valid;
	std::vector<std::pair<vec_t, int>> dists;
	vec_t angle;
	vec3_t normal;
	vec3_t v;

	points.swap(lt->sortedwedges);
	lt->sortedwedges.resize(0);

	next.resize((int)points.size());
	prev.resize((int)points.size());
	valid.resize((int)points.size());
	dists.resize((int)points.size());
	for (i = 0; i < (int)points.size(); i++)
	{
		next[i] = (i + 1) % (int)points.size();
		prev[i] = (i - 1 + (int)points.size()) % (int)points.size();
		valid[i] = 1;
		dists[i].first = DotProduct(points[i].leftspot, points[i].leftdirection);
		dists[i].second = i;
	}
	std::sort(dists.begin(), dists.end());

	for (i = 0; i < (int)points.size(); i++)
	{
		int cur = dists[i].second;
		if (valid[cur] == 0)
		{
			continue;
		}
		valid[cur] = 2; // mark current patch as final

		CrossProduct(points[cur].leftdirection, lt->normal, normal);
		VectorNormalize(normal);
		VectorScale(normal, cos(TRIANGLE_SHAPE_THRESHOLD), v);
		VectorMA(v, sin(TRIANGLE_SHAPE_THRESHOLD), points[cur].leftdirection, v);
		while (next[cur] != cur && valid[next[cur]] != 2)
		{
			angle = GetAngle(points[cur].leftdirection, points[next[cur]].leftdirection, lt->normal);
			if (fabs(angle) <= (1.0 * Q_PI / 180) ||
				GetAngleDiff(angle, 0) <= Q_PI + NORMAL_EPSILON && DotProduct(points[next[cur]].leftspot, v) >= DotProduct(points[cur].leftspot, v) - ON_EPSILON / 2)
			{
				// remove next patch
				valid[next[cur]] = 0;
				next[cur] = next[next[cur]];
				prev[next[cur]] = cur;
				continue;
			}
			// the triangle is good
			break;
		}

		CrossProduct(lt->normal, points[cur].leftdirection, normal);
		VectorNormalize(normal);
		VectorScale(normal, cos(TRIANGLE_SHAPE_THRESHOLD), v);
		VectorMA(v, sin(TRIANGLE_SHAPE_THRESHOLD), points[cur].leftdirection, v);
		while (prev[cur] != cur && valid[prev[cur]] != 2)
		{
			angle = GetAngle(points[prev[cur]].leftdirection, points[cur].leftdirection, lt->normal);
			if (fabs(angle) <= (1.0 * Q_PI / 180) ||
				GetAngleDiff(angle, 0) <= Q_PI + NORMAL_EPSILON && DotProduct(points[prev[cur]].leftspot, v) >= DotProduct(points[cur].leftspot, v) - ON_EPSILON / 2)
			{
				// remove previous patch
				valid[prev[cur]] = 0;
				prev[cur] = prev[prev[cur]];
				next[prev[cur]] = cur;
				continue;
			}
			// the triangle is good
			break;
		}
	}

	for (i = 0; i < (int)points.size(); i++)
	{
		if (valid[i] == 2)
		{
			lt->sortedwedges.push_back(points[i]);
		}
	}
}

static void PlaceHullPoints(localtriangulation_t *lt)
{
	int i;
	int j;
	vec3_t v;
	vec_t angle;
	localtriangulation_t::HullPoint hp;
	std::vector<localtriangulation_t::HullPoint> spots;
	std::vector<std::pair<vec_t, int>> angles;
	std::vector<localtriangulation_t::HullPoint> arc_spots;
	std::vector<vec_t> arc_angles;
	std::vector<int> next;
	std::vector<int> prev;

	spots.reserve(lt->winding.m_NumPoints);
	spots.resize(0);
	for (i = 0; i < (int)lt->winding.m_NumPoints; i++)
	{
		VectorSubtract(lt->winding.m_Points[i], lt->center, v);
		vec_t dot = DotProduct(v, lt->normal);
		VectorMA(v, -dot, lt->normal, hp.spot);
		if (!GetDirection(hp.spot, lt->normal, hp.direction))
		{
			continue;
		}
		spots.push_back(hp);
	}

	if ((int)lt->sortedwedges.size() == 0)
	{
		angles.resize((int)spots.size());
		for (i = 0; i < (int)spots.size(); i++)
		{
			angle = GetAngle(spots[0].direction, spots[i].direction, lt->normal);
			if (i == 0)
			{
				angle = 0.0;
			}
			angles[i].first = GetAngleDiff(angle, 0);
			angles[i].second = i;
		}
		std::sort(angles.begin(), angles.end());
		lt->sortedhullpoints.resize(0);
		for (i = 0; i < (int)spots.size(); i++)
		{
			lt->sortedhullpoints.push_back(spots[angles[i].second]);
		}
		return;
	}

	lt->sortedhullpoints.resize(0);
	for (i = 0; i < (int)lt->sortedwedges.size(); i++)
	{
		const auto *w = &lt->sortedwedges[i];
		const auto *wnext = &lt->sortedwedges[(i + 1) % (int)lt->sortedwedges.size()];

		angles.resize((int)spots.size());
		for (j = 0; j < (int)spots.size(); j++)
		{
			angle = GetAngle(w->leftdirection, spots[j].direction, lt->normal);
			angles[j].first = GetAngleDiff(angle, 0);
			angles[j].second = j;
		}
		std::sort(angles.begin(), angles.end());
		angle = GetAngle(w->leftdirection, wnext->leftdirection, lt->normal);
		if ((int)lt->sortedwedges.size() == 1)
		{
			angle = 2 * Q_PI;
		}
		else
		{
			angle = GetAngleDiff(angle, 0);
		}

		arc_spots.resize((int)spots.size() + 2);
		arc_angles.resize((int)spots.size() + 2);
		next.resize((int)spots.size() + 2);
		prev.resize((int)spots.size() + 2);

		VectorCopy(w->leftspot, arc_spots[0].spot);
		VectorCopy(w->leftdirection, arc_spots[0].direction);
		arc_angles[0] = 0;
		next[0] = 1;
		prev[0] = -1;
		int n = 1;
		for (j = 0; j < (int)spots.size(); j++)
		{
			if (NORMAL_EPSILON <= angles[j].first && angles[j].first <= angle - NORMAL_EPSILON)
			{
				arc_spots[n] = spots[angles[j].second];
				arc_angles[n] = angles[j].first;
				next[n] = n + 1;
				prev[n] = n - 1;
				n++;
			}
		}
		VectorCopy(wnext->leftspot, arc_spots[n].spot);
		VectorCopy(wnext->leftdirection, arc_spots[n].direction);
		arc_angles[n] = angle;
		next[n] = -1;
		prev[n] = n - 1;
		n++;

		for (j = 1; next[j] != -1; j = next[j])
		{
			while (prev[j] != -1)
			{
				if (arc_angles[next[j]] - arc_angles[prev[j]] <= Q_PI + NORMAL_EPSILON)
				{
					vec_t frac = GetFrac(arc_spots[prev[j]].spot, arc_spots[next[j]].spot, arc_spots[j].direction, lt->normal);
					vec_t len = (1 - frac) * DotProduct(arc_spots[prev[j]].spot, arc_spots[j].direction) + frac * DotProduct(arc_spots[next[j]].spot, arc_spots[j].direction);
					vec_t dist = DotProduct(arc_spots[j].spot, arc_spots[j].direction);
					if (dist <= len + NORMAL_EPSILON)
					{
						j = prev[j];
						next[j] = next[next[j]];
						prev[next[j]] = j;
						continue;
					}
				}
				break;
			}
		}

		for (j = 0; next[j] != -1; j = next[j])
		{
			lt->sortedhullpoints.push_back(arc_spots[j]);
		}
	}
}

static auto TryMakeSquare(localtriangulation_t *lt, int i) -> bool
{
	vec3_t v;
	vec3_t dir1;
	vec3_t dir2;

	auto *w1 = &lt->sortedwedges[i];
	auto *w2 = &lt->sortedwedges[(i + 1) % (int)lt->sortedwedges.size()];
	auto *w3 = &lt->sortedwedges[(i + 2) % (int)lt->sortedwedges.size()];

	// (o, p1, p2) and (o, p2, p3) must be triangles and not in a square
	if (w1->shape != localtriangulation_t::Wedge::eTriangular || w2->shape != localtriangulation_t::Wedge::eTriangular)
	{
		return false;
	}

	// (o, p1, p3) must be a triangle
	vec_t angle = GetAngle(w1->leftdirection, w3->leftdirection, lt->normal);
	angle = GetAngleDiff(angle, 0);
	if (angle >= TRIANGLE_SHAPE_THRESHOLD)
	{
		return false;
	}

	// (p2, p1, p3) must be a triangle
	VectorSubtract(w1->leftspot, w2->leftspot, v);
	if (!GetDirection(v, lt->normal, dir1))
	{
		return false;
	}
	VectorSubtract(w3->leftspot, w2->leftspot, v);
	if (!GetDirection(v, lt->normal, dir2))
	{
		return false;
	}
	angle = GetAngle(dir2, dir1, lt->normal);
	angle = GetAngleDiff(angle, 0);
	if (angle >= TRIANGLE_SHAPE_THRESHOLD)
	{
		return false;
	}

	w1->shape = localtriangulation_t::Wedge::eSquareLeft;
	VectorSubtract(vec3_origin, dir1, w1->wedgenormal);
	w2->shape = localtriangulation_t::Wedge::eSquareRight;
	VectorCopy(dir2, w2->wedgenormal);
	return true;
}

static void FindSquares(localtriangulation_t *lt)
{
	int i;
	std::vector<std::pair<vec_t, int>> dists;

	if ((int)lt->sortedwedges.size() <= 2)
	{
		return;
	}

	dists.resize((int)lt->sortedwedges.size());
	for (i = 0; i < (int)lt->sortedwedges.size(); i++)
	{
		auto *w = &lt->sortedwedges[i];
		dists[i].first = DotProduct(w->leftspot, w->leftdirection);
		dists[i].second = i;
	}
	std::sort(dists.begin(), dists.end());

	for (i = 0; i < (int)lt->sortedwedges.size(); i++)
	{
		TryMakeSquare(lt, dists[i].second);
		TryMakeSquare(lt, (dists[i].second - 2 + (int)lt->sortedwedges.size()) % (int)lt->sortedwedges.size());
	}
}

static auto CreateLocalTriangulation(const facetriangulation_t *facetrian, int patchnum) -> localtriangulation_t *
{
	vec3_t v;
	vec3_t normal;

	int facenum = facetrian->facenum;
	const auto *patch = &g_patches[patchnum];
	auto *lt = new localtriangulation_t;

	// Fill basic information for this local triangulation
	lt->plane = *getPlaneFromFaceNumber(facenum);
	lt->plane.dist += DotProduct(g_face_offset[facenum], lt->plane.normal);
	lt->winding = *patch->winding;
	VectorMA(patch->origin, -PATCH_HUNT_OFFSET, lt->plane.normal, lt->center);
	vec_t dot = DotProduct(lt->center, lt->plane.normal) - lt->plane.dist;
	VectorMA(lt->center, -dot, lt->plane.normal, lt->center);
	if (!point_in_winding_noedge(lt->winding, lt->plane, lt->center, DEFAULT_EDGE_WIDTH))
	{
		snap_to_winding_noedge(lt->winding, lt->plane, lt->center, DEFAULT_EDGE_WIDTH, 4 * DEFAULT_EDGE_WIDTH);
	}
	VectorCopy(lt->plane.normal, lt->normal);
	lt->patchnum = patchnum;
	lt->neighborfaces = facetrian->neighbors;

	// Gather all patches from nearby faces
	GatherPatches(lt, facetrian);

	// Remove distant patches
	PurgePatches(lt);

	// Calculate wedge types
	vec_t total = 0.0;
	for (int i = 0; i < (int)lt->sortedwedges.size(); i++)
	{
		auto *w = &lt->sortedwedges[i];
		auto *wnext = &lt->sortedwedges[(i + 1) % (int)lt->sortedwedges.size()];

		vec_t angle = GetAngle(w->leftdirection, wnext->leftdirection, lt->normal);
		angle = GetAngleDiff(angle, 0);
		if ((int)lt->sortedwedges.size() == 1)
		{
			angle = 2 * Q_PI;
		}
		total += angle;

		if (angle <= Q_PI + NORMAL_EPSILON)
		{
			if (angle < TRIANGLE_SHAPE_THRESHOLD)
			{
				w->shape = localtriangulation_t::Wedge::eTriangular;
				VectorClear(w->wedgenormal);
			}
			else
			{
				w->shape = localtriangulation_t::Wedge::eConvex;
				VectorSubtract(wnext->leftspot, w->leftspot, v);
				GetDirection(v, lt->normal, w->wedgenormal);
			}
		}
		else
		{
			w->shape = localtriangulation_t::Wedge::eConcave;
			VectorAdd(wnext->leftdirection, w->leftdirection, v);
			CrossProduct(lt->normal, v, normal);
			VectorSubtract(wnext->leftdirection, w->leftdirection, v);
			VectorAdd(normal, v, normal);
			GetDirection(normal, lt->normal, w->wedgenormal);
		}
	}
	FindSquares(lt);

	// Calculate hull points
	PlaceHullPoints(lt);

	return lt;
}

static void FreeLocalTriangulation(localtriangulation_t *lt)
{
	delete lt;
}

static void FindNeighbors(facetriangulation_t *facetrian)
{
	int i;
	int j;
	int e;
	const EdgeShare *es;
	const FaceList *fl;
	int facenum2;
	const BSPLumpFace *f2;
	const dplane_t *dp2;

	int facenum = facetrian->facenum;
	const auto *f = &g_bspfaces[facenum];
	const dplane_t *dp = getPlaneFromFace(f);

	facetrian->neighbors.resize(0);

	facetrian->neighbors.push_back(facenum);

	for (i = 0; i < f->numedges; i++)
	{
		e = g_bspsurfedges[f->firstedge + i];
		es = &g_edgeshare[abs(e)];
		if (!es->smooth)
		{
			continue;
		}
		f2 = es->faces[e > 0 ? 1 : 0];
		facenum2 = f2 - g_bspfaces;
		dp2 = getPlaneFromFace(f2);
		if (DotProduct(dp->normal, dp2->normal) < -NORMAL_EPSILON)
		{
			continue;
		}
		for (j = 0; j < (int)facetrian->neighbors.size(); j++)
		{
			if (facetrian->neighbors[j] == facenum2)
			{
				break;
			}
		}
		if (j == (int)facetrian->neighbors.size())
		{
			facetrian->neighbors.push_back(facenum2);
		}
	}

	for (i = 0; i < f->numedges; i++)
	{
		e = g_bspsurfedges[f->firstedge + i];
		es = &g_edgeshare[abs(e)];
		if (!es->smooth)
		{
			continue;
		}
		for (int side = 0; side < 2; side++)
		{
			for (fl = es->vertex_facelist[side]; fl; fl = fl->next)
			{
				f2 = fl->face;
				facenum2 = f2 - g_bspfaces;
				dp2 = getPlaneFromFace(f2);
				if (DotProduct(dp->normal, dp2->normal) < -NORMAL_EPSILON)
				{
					continue;
				}
				for (j = 0; j < (int)facetrian->neighbors.size(); j++)
				{
					if (facetrian->neighbors[j] == facenum2)
					{
						break;
					}
				}
				if (j == (int)facetrian->neighbors.size())
				{
					facetrian->neighbors.push_back(facenum2);
				}
			}
		}
	}
}

static void BuildWalls(facetriangulation_t *facetrian)
{
	auto facenum = facetrian->facenum;
	const auto *f = &g_bspfaces[facenum];
	const dplane_t *dp = getPlaneFromFace(f);

	facetrian->walls.resize(0);

	for (int i = 0; i < (int)facetrian->neighbors.size(); i++)
	{
		int facenum2 = facetrian->neighbors[i];
		const auto *f2 = &g_bspfaces[facenum2];
		const dplane_t *dp2 = getPlaneFromFace(f2);
		if (DotProduct(dp->normal, dp2->normal) <= 0.1)
		{
			continue;
		}
		for (int j = 0; j < f2->numedges; j++)
		{
			int e = g_bspsurfedges[f2->firstedge + j];
			const auto *es = &g_edgeshare[abs(e)];
			if (!es->smooth)
			{
				facetriangulation_t::Wall wall;

				VectorAdd(g_bspvertexes[g_bspedges[abs(e)].v[0]].point, g_face_offset[facenum], wall.points[0]);
				VectorAdd(g_bspvertexes[g_bspedges[abs(e)].v[1]].point, g_face_offset[facenum], wall.points[1]);
				VectorSubtract(wall.points[1], wall.points[0], wall.direction);
				vec_t dot = DotProduct(wall.direction, dp->normal);
				VectorMA(wall.direction, -dot, dp->normal, wall.direction);
				if (VectorNormalize(wall.direction))
				{
					CrossProduct(wall.direction, dp->normal, wall.normal);
					VectorNormalize(wall.normal);
					facetrian->walls.push_back(wall);
				}
			}
		}
	}
}

static void CollectUsedPatches(facetriangulation_t *facetrian)
{
	int k;

	facetrian->usedpatches.resize(0);
	for (int i = 0; i < (int)facetrian->localtriangulations.size(); i++)
	{
		const auto *lt = facetrian->localtriangulations[i];

		int patchnum = lt->patchnum;
		for (k = 0; k < (int)facetrian->usedpatches.size(); k++)
		{
			if (facetrian->usedpatches[k] == patchnum)
			{
				break;
			}
		}
		if (k == (int)facetrian->usedpatches.size())
		{
			facetrian->usedpatches.push_back(patchnum);
		}

		for (int j = 0; j < (int)lt->sortedwedges.size(); j++)
		{
			const auto *w = &lt->sortedwedges[j];

			patchnum = w->leftpatchnum;
			for (k = 0; k < (int)facetrian->usedpatches.size(); k++)
			{
				if (facetrian->usedpatches[k] == patchnum)
				{
					break;
				}
			}
			if (k == (int)facetrian->usedpatches.size())
			{
				facetrian->usedpatches.push_back(patchnum);
			}
		}
	}
}

// =====================================================================================
//  CreateTriangulations
// =====================================================================================
void CreateTriangulations(int facenum)
{
	try
	{
		const Patch *patch;

		g_facetriangulations[facenum] = new facetriangulation_t;
		auto *facetrian = g_facetriangulations[facenum];

		facetrian->facenum = facenum;

		// Find neighbors
		FindNeighbors(facetrian);

		// Build walls
		BuildWalls(facetrian);

		// Create local triangulation around each patch
		facetrian->localtriangulations.resize(0);
		for (patch = g_face_patches[facenum]; patch; patch = patch->next)
		{
			int patchnum = patch - g_patches;
			auto *lt = CreateLocalTriangulation(facetrian, patchnum);
			facetrian->localtriangulations.push_back(lt);
		}

		// Collect used patches
		CollectUsedPatches(facetrian);
	}
	catch (std::bad_alloc)
	{
		hlassume(false, assume_NoMemory);
	}
}

// =====================================================================================
//  GetTriangulationPatches
// =====================================================================================
void GetTriangulationPatches(int facenum, int *numpatches, const int **patches)
{
	const auto *facetrian = g_facetriangulations[facenum];
	*numpatches = (int)facetrian->usedpatches.size();
	*patches = facetrian->usedpatches.data();
}

// =====================================================================================
//  FreeTriangulations
// =====================================================================================
void FreeTriangulations()
{
	try
	{
		for (int i = 0; i < g_bspnumfaces; i++)
		{
			auto *facetrian = g_facetriangulations[i];

			for (int j = 0; j < (int)facetrian->localtriangulations.size(); j++)
			{
				FreeLocalTriangulation(facetrian->localtriangulations[j]);
			}

			delete facetrian;
			g_facetriangulations[i] = nullptr;
		}
	}
	catch (std::bad_alloc)
	{
		hlassume(false, assume_NoMemory);
	}
}