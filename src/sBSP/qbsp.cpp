/*

	BINARY SPACE PARTITION    -aka-    B S P

	Code based on original code from Valve Software,
	Modified by Sean "Zoner" Cavanaugh (seanc@gearboxsoftware.com) with permission.
	Modified by Tony "Merl" Moore (merlinis@bigpond.net.au) [AJM]
	Modified by amckern (amckern@yahoo.com)
	Modified by vluzacn (vluzacn@163.com)
	Modified by seedee (cdaniel9000@gmail.com)
*/

#include <cstring>

#include "bsp5.h"
#include "arguments.h"
#include "filelib.h"
#include "threads.h"

vec3_t g_hull_size[NUM_HULLS][2] =
	{
		{// 0x0x0
		 {0, 0, 0},
		 {0, 0, 0}},
		{// 32x32x72
		 {-16, -16, -36},
		 {16, 16, 36}},
		{// 64x64x64
		 {-32, -32, -32},
		 {32, 32, 32}},
		{// 32x32x36
		 {-16, -16, -18},
		 {16, 16, 18}}};
static FILE *polyfiles[NUM_HULLS];
static FILE *brushfiles[NUM_HULLS];
int g_hullnum = 0;

static FaceBSP *validfaces[MAX_INTERNAL_MAP_PLANES];

char g_bspfilename[_MAX_PATH];
char g_pointfilename[_MAX_PATH];
char g_linefilename[_MAX_PATH];
char g_portfilename[_MAX_PATH];
char g_extentfilename[_MAX_PATH];

// command line flags
bool g_estimate = DEFAULT_ESTIMATE;	 // estimate mode "-estimate"
bool g_bLeakOnly = DEFAULT_LEAKONLY; // leakonly mode "-leakonly"
bool g_bLeaked = false;
int g_subdivide_size = DEFAULT_SUBDIVIDE_SIZE;

bool g_nohull2 = false;

dplane_t g_dplanes[MAX_INTERNAL_MAP_PLANES];

// =====================================================================================
//  Extract File stuff (ExtractFile | ExtractFilePath | ExtractFileBase)
//
// With VS 2005 - and the 64 bit build, i had to pull 3 classes over from
// cmdlib.cpp even with the proper includes to get rid of the lnk2001 error
//
// amckern - amckern@yahoo.com
// =====================================================================================

// Code Deleted. --vluzacn

// =====================================================================================
//  NewFaceFromFace
//      Duplicates the non point information of a face, used by SplitFace and MergeFace.
// =====================================================================================
auto NewFaceFromFace(const FaceBSP *const in) -> FaceBSP *
{
	FaceBSP *newf;

	newf = AllocFace();

	newf->planenum = in->planenum;
	newf->texturenum = in->texturenum;
	newf->original = in->original;
	newf->contents = in->contents;
	newf->facestyle = in->facestyle;
	newf->detaillevel = in->detaillevel;

	return newf;
}

// =====================================================================================
//  SplitFaceTmp
//      blah
// =====================================================================================
static void SplitFaceTmp(FaceBSP *in, const dplane_t *const split, FaceBSP **front, FaceBSP **back)
{
	vec_t dists[MAXEDGES + 1];
	int sides[MAXEDGES + 1];
	int counts[3];
	vec_t dot;
	int i;
	int j;
	FaceBSP *newf;
	FaceBSP *new2;
	vec_t *p1;
	vec_t *p2;
	vec3_t mid;

	if (in->numpoints < 0)
	{
		Error("SplitFace: freed face");
	}
	counts[0] = counts[1] = counts[2] = 0;

	// determine sides for each point
	for (i = 0; i < in->numpoints; i++)
	{
		dot = DotProduct(in->pts[i], split->normal);
		dot -= split->dist;
		dists[i] = dot;
		if (dot > ON_EPSILON)
		{
			sides[i] = SIDE_FRONT;
		}
		else if (dot < -ON_EPSILON)
		{
			sides[i] = SIDE_BACK;
		}
		else
		{
			sides[i] = SIDE_ON;
		}
		counts[sides[i]]++;
	}
	sides[i] = sides[0];
	dists[i] = dists[0];

	if (!counts[0] && !counts[1])
	{
		if (in->detaillevel)
		{
			// put front face in front node, and back face in back node.
			const dplane_t *faceplane = &g_dplanes[in->planenum];
			if (DotProduct(faceplane->normal, split->normal) > NORMAL_EPSILON) // usually near 1.0 or -1.0
			{
				*front = in;
				*back = nullptr;
			}
			else
			{
				*front = nullptr;
				*back = in;
			}
		}
		else
		{
			// not func_detail. front face and back face need to pair.
			vec_t sum = 0.0;
			for (i = 0; i < in->numpoints; i++)
			{
				dot = DotProduct(in->pts[i], split->normal);
				dot -= split->dist;
				sum += dot;
			}
			if (sum > NORMAL_EPSILON)
			{
				*front = in;
				*back = nullptr;
			}
			else
			{
				*front = nullptr;
				*back = in;
			}
		}
		return;
	}
	if (!counts[0])
	{
		*front = nullptr;
		*back = in;
		return;
	}
	if (!counts[1])
	{
		*front = in;
		*back = nullptr;
		return;
	}

	*back = newf = NewFaceFromFace(in);
	*front = new2 = NewFaceFromFace(in);

	// distribute the points and generate splits

	for (i = 0; i < in->numpoints; i++)
	{
		if (newf->numpoints > MAXEDGES || new2->numpoints > MAXEDGES)
		{
			Error("SplitFace: numpoints > MAXEDGES");
		}

		p1 = in->pts[i];

		if (sides[i] == SIDE_ON)
		{
			VectorCopy(p1, newf->pts[newf->numpoints]);
			newf->numpoints++;
			VectorCopy(p1, new2->pts[new2->numpoints]);
			new2->numpoints++;
			continue;
		}

		if (sides[i] == SIDE_FRONT)
		{
			VectorCopy(p1, new2->pts[new2->numpoints]);
			new2->numpoints++;
		}
		else
		{
			VectorCopy(p1, newf->pts[newf->numpoints]);
			newf->numpoints++;
		}

		if (sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i])
		{
			continue;
		}

		// generate a split point
		p2 = in->pts[(i + 1) % in->numpoints];

		dot = dists[i] / (dists[i] - dists[i + 1]);
		for (j = 0; j < 3; j++)
		{ // avoid round off error when possible
			if (split->normal[j] == 1)
			{
				mid[j] = split->dist;
			}
			else if (split->normal[j] == -1)
			{
				mid[j] = -split->dist;
			}
			else
			{
				mid[j] = p1[j] + dot * (p2[j] - p1[j]);
			}
		}

		VectorCopy(mid, newf->pts[newf->numpoints]);
		newf->numpoints++;
		VectorCopy(mid, new2->pts[new2->numpoints]);
		new2->numpoints++;
	}

	if (newf->numpoints > MAXEDGES || new2->numpoints > MAXEDGES)
	{
		Error("SplitFace: numpoints > MAXEDGES");
	}
	{
		auto *wd = new Winding(newf->numpoints);
		int x;
		for (x = 0; x < newf->numpoints; x++)
		{
			VectorCopy(newf->pts[x], wd->m_Points[x]);
		}
		wd->RemoveColinearPoints();
		newf->numpoints = wd->m_NumPoints;
		for (x = 0; x < newf->numpoints; x++)
		{
			VectorCopy(wd->m_Points[x], newf->pts[x]);
		}
		delete wd;
		if (newf->numpoints == 0)
		{
			*back = nullptr;
		}
	}
	{
		auto *wd = new Winding(new2->numpoints);
		int x;
		for (x = 0; x < new2->numpoints; x++)
		{
			VectorCopy(new2->pts[x], wd->m_Points[x]);
		}
		wd->RemoveColinearPoints();
		new2->numpoints = wd->m_NumPoints;
		for (x = 0; x < new2->numpoints; x++)
		{
			VectorCopy(wd->m_Points[x], new2->pts[x]);
		}
		delete wd;
		if (new2->numpoints == 0)
		{
			*front = nullptr;
		}
	}
}

// =====================================================================================
//  SplitFace
//      blah
// =====================================================================================
void SplitFace(FaceBSP *in, const dplane_t *const split, FaceBSP **front, FaceBSP **back)
{
	SplitFaceTmp(in, split, front, back);

	// free the original face now that is is represented by the fragments
	if (*front && *back)
	{
		delete in;
	}
}

// =====================================================================================
//  AllocFace
// =====================================================================================
auto AllocFace() -> FaceBSP *
{
	FaceBSP *f;

	f = new FaceBSP;
	memset(f, 0, sizeof(FaceBSP));

	f->planenum = -1;

	return f;
}

// =====================================================================================
//  AllocSurface
// =====================================================================================
auto AllocSurface() -> SurfaceBSP *
{
	SurfaceBSP *s;

	s = new SurfaceBSP;
	memset(s, 0, sizeof(SurfaceBSP));

	return s;
}

// =====================================================================================
//  FreeSurface
// =====================================================================================
void FreeSurface(SurfaceBSP *s)
{
	delete s;
}

// =====================================================================================
//  AllocPortal
// =====================================================================================
auto AllocPortal() -> PortalBSP *
{
	PortalBSP *p;

	p = new PortalBSP;
	memset(p, 0, sizeof(PortalBSP));

	return p;
}

// =====================================================================================
//  FreePortal
// =====================================================================================
void FreePortal(PortalBSP *p) // consider: inline
{
	delete p;
}

auto AllocSide() -> SideBSP *
{
	SideBSP *s;
	s = new SideBSP;
	memset(s, 0, sizeof(SideBSP));
	return s;
}

void FreeSide(SideBSP *s)
{
	if (s->w)
	{
		delete s->w;
	}
	delete s;
	return;
}

auto NewSideFromSide(const SideBSP *s) -> SideBSP *
{
	SideBSP *news;
	news = AllocSide();
	news->plane = s->plane;
	news->w = new Winding(*s->w);
	return news;
}

auto AllocBrush() -> BrushBSP *
{
	BrushBSP *b;
	b = new BrushBSP;
	memset(b, 0, sizeof(BrushBSP));
	return b;
}

void FreeBrush(BrushBSP *b)
{
	if (b->sides)
	{
		SideBSP *s, *next;
		for (s = b->sides; s; s = next)
		{
			next = s->next;
			FreeSide(s);
		}
	}
	delete b;
	return;
}

auto NewBrushFromBrush(const BrushBSP *b) -> BrushBSP *
{
	BrushBSP *newb;
	newb = AllocBrush();
	SideBSP *s, **pnews;
	for (s = b->sides, pnews = &newb->sides; s; s = s->next, pnews = &(*pnews)->next)
	{
		*pnews = NewSideFromSide(s);
	}
	return newb;
}

void ClipBrush(BrushBSP **b, const dplane_t *split, vec_t epsilon)
{
	SideBSP *s, **pnext;
	Winding *w;
	for (pnext = &(*b)->sides, s = *pnext; s; s = *pnext)
	{
		if (s->w->Clip(*split, false, epsilon))
		{
			pnext = &s->next;
		}
		else
		{
			*pnext = s->next;
			FreeSide(s);
		}
	}
	if (!(*b)->sides)
	{ // empty brush
		FreeBrush(*b);
		*b = nullptr;
		return;
	}
	w = new Winding(*split);
	for (s = (*b)->sides; s; s = s->next)
	{
		if (!w->Clip(s->plane, false, epsilon))
		{
			break;
		}
	}
	if (w->m_NumPoints == 0)
	{
		delete w;
	}
	else
	{
		s = AllocSide();
		s->plane = *split;
		s->w = w;
		s->next = (*b)->sides;
		(*b)->sides = s;
	}
	return;
}

void SplitBrush(BrushBSP *in, const dplane_t *split, BrushBSP **front, BrushBSP **back)
// 'in' will be freed
{
	in->next = nullptr;
	bool onfront;
	bool onback;
	onfront = false;
	onback = false;
	SideBSP *s;
	for (s = in->sides; s; s = s->next)
	{
		switch (s->w->WindingOnPlaneSide(split->normal, split->dist, 2 * ON_EPSILON))
		{
		case SIDE_CROSS:
			onfront = true;
			onback = true;
			break;
		case SIDE_FRONT:
			onfront = true;
			break;
		case SIDE_BACK:
			onback = true;
			break;
		case SIDE_ON:
			break;
		}
		if (onfront && onback)
			break;
	}
	if (!onfront && !onback)
	{
		FreeBrush(in);
		*front = nullptr;
		*back = nullptr;
		return;
	}
	if (!onfront)
	{
		*front = nullptr;
		*back = in;
		return;
	}
	if (!onback)
	{
		*front = in;
		*back = nullptr;
		return;
	}
	*front = in;
	*back = NewBrushFromBrush(in);
	dplane_t frontclip = *split;
	dplane_t backclip = *split;
	VectorSubtract(vec3_origin, backclip.normal, backclip.normal);
	backclip.dist = -backclip.dist;
	ClipBrush(front, &frontclip, NORMAL_EPSILON);
	ClipBrush(back, &backclip, NORMAL_EPSILON);
	return;
}

auto BrushFromBox(const vec3_t mins, const vec3_t maxs) -> BrushBSP *
{
	BrushBSP *b = AllocBrush();
	dplane_t planes[6];
	for (int k = 0; k < 3; k++)
	{
		VectorFill(planes[k].normal, 0.0);
		planes[k].normal[k] = 1.0;
		planes[k].dist = mins[k];
		VectorFill(planes[k + 3].normal, 0.0);
		planes[k + 3].normal[k] = -1.0;
		planes[k + 3].dist = -maxs[k];
	}
	b->sides = AllocSide();
	b->sides->plane = planes[0];
	b->sides->w = new Winding(planes[0]);
	for (int k = 1; k < 6; k++)
	{
		ClipBrush(&b, &planes[k], NORMAL_EPSILON);
		if (b == nullptr)
		{
			break;
		}
	}
	return b;
}

void CalcBrushBounds(const BrushBSP *b, vec3_t &mins, vec3_t &maxs)
{
	VectorFill(mins, BOGUS_RANGE);
	VectorFill(maxs, -BOGUS_RANGE);
	for (SideBSP *s = b->sides; s; s = s->next)
	{
		vec3_t windingmins, windingmaxs;
		s->w->getBounds(windingmins, windingmaxs);
		VectorCompareMinimum(mins, windingmins, mins);
		VectorCompareMaximum(maxs, windingmaxs, maxs);
	}
}

// =====================================================================================
//  AllocNode
//      blah
// =====================================================================================
auto AllocNode() -> NodeBSP *
{
	NodeBSP *n;

	n = new NodeBSP;
	memset(n, 0, sizeof(NodeBSP));

	return n;
}

// =====================================================================================
//  AddPointToBounds
// =====================================================================================
void AddPointToBounds(const vec3_t v, vec3_t mins, vec3_t maxs)
{
	int i;
	vec_t val;

	for (i = 0; i < 3; i++)
	{
		val = v[i];
		if (val < mins[i])
		{
			mins[i] = val;
		}
		if (val > maxs[i])
		{
			maxs[i] = val;
		}
	}
}

// =====================================================================================
//  AddFaceToBounds
// =====================================================================================
static void AddFaceToBounds(const FaceBSP *const f, vec3_t mins, vec3_t maxs)
{
	int i;

	for (i = 0; i < f->numpoints; i++)
	{
		AddPointToBounds(f->pts[i], mins, maxs);
	}
}

// =====================================================================================
//  ClearBounds
// =====================================================================================
static void ClearBounds(vec3_t mins, vec3_t maxs)
{
	mins[0] = mins[1] = mins[2] = 99999;
	maxs[0] = maxs[1] = maxs[2] = -99999;
}

// =====================================================================================
//  SurflistFromValidFaces
//      blah
// =====================================================================================
static auto SurflistFromValidFaces() -> SurfchainBSP *
{
	SurfaceBSP *n;
	int i;
	FaceBSP *f;
	FaceBSP *next;
	SurfchainBSP *sc;

	sc = new SurfchainBSP;
	ClearBounds(sc->mins, sc->maxs);
	sc->surfaces = nullptr;

	// grab planes from both sides
	for (i = 0; i < g_numplanes; i += 2)
	{
		if (!validfaces[i] && !validfaces[i + 1])
		{
			continue;
		}
		n = AllocSurface();
		n->next = sc->surfaces;
		sc->surfaces = n;
		ClearBounds(n->mins, n->maxs);
		n->detaillevel = -1;
		n->planenum = i;

		n->faces = nullptr;
		for (f = validfaces[i]; f; f = next)
		{
			next = f->next;
			f->next = n->faces;
			n->faces = f;
			AddFaceToBounds(f, n->mins, n->maxs);
			if (n->detaillevel == -1 || f->detaillevel < n->detaillevel)
			{
				n->detaillevel = f->detaillevel;
			}
		}
		for (f = validfaces[i + 1]; f; f = next)
		{
			next = f->next;
			f->next = n->faces;
			n->faces = f;
			AddFaceToBounds(f, n->mins, n->maxs);
			if (n->detaillevel == -1 || f->detaillevel < n->detaillevel)
			{
				n->detaillevel = f->detaillevel;
			}
		}

		AddPointToBounds(n->mins, sc->mins, sc->maxs);
		AddPointToBounds(n->maxs, sc->mins, sc->maxs);

		validfaces[i] = nullptr;
		validfaces[i + 1] = nullptr;
	}

	// merge all possible polygons

	MergeAll(sc->surfaces);

	return sc;
}

// =====================================================================================
//  CheckFaceForNull
//      Returns true if the passed face is facetype null
// =====================================================================================
auto CheckFaceForNull(const FaceBSP *const f) -> bool
{
	if (f->contents == CONTENTS_SKY)
	{
		const char *name = GetTextureByNumber(f->texturenum);
		if (strncasecmp(name, "sky", 3)) // for env_rain
			return true;
	}
	const char *name = GetTextureByNumber(f->texturenum);
	if (!strncasecmp(name, "null", 4))
		return true;

	return false;
}
// =====================================================================================
// Cpt_Andrew - UTSky Check
// =====================================================================================
auto CheckFaceForEnv_Sky(const FaceBSP *const f) -> bool
{
	const char *name = GetTextureByNumber(f->texturenum);
	if (!strncasecmp(name, "env_sky", 7))
		return true;
	return false;
}
// =====================================================================================

// =====================================================================================
//  CheckFaceForHint
//      Returns true if the passed face is facetype hint
// =====================================================================================
auto CheckFaceForHint(const FaceBSP *const f) -> bool
{
	const char *name = GetTextureByNumber(f->texturenum);
	if (!strncasecmp(name, "hint", 4))
		return true;
	return false;
}

// =====================================================================================
//  CheckFaceForSkipt
//      Returns true if the passed face is facetype skip
// =====================================================================================
auto CheckFaceForSkip(const FaceBSP *const f) -> bool
{
	const char *name = GetTextureByNumber(f->texturenum);
	if (!strncasecmp(name, "skip", 4))
		return true;
	return false;
}

auto CheckFaceForDiscardable(const FaceBSP *f) -> bool
{
	const char *name = GetTextureByNumber(f->texturenum);
	if (!strncasecmp(name, "SOLIDHINT", 9) || !strncasecmp(name, "BEVELHINT", 9))
		return true;
	return false;
}

// =====================================================================================
//  SetFaceType
// =====================================================================================
static auto SetFaceType(FaceBSP *f) -> facestyle_e
{
	if (CheckFaceForHint(f))
	{
		f->facestyle = face_hint;
	}
	else if (CheckFaceForSkip(f))
	{
		f->facestyle = face_skip;
	}
	else if (CheckFaceForNull(f))
	{
		f->facestyle = face_null;
	}
	else if (CheckFaceForDiscardable(f))
	{
		f->facestyle = face_discardable;
	}

	// =====================================================================================
	// Cpt_Andrew - Env_Sky Check
	// =====================================================================================
	// else if (CheckFaceForUTSky(f))
	else if (CheckFaceForEnv_Sky(f))
	{
		f->facestyle = face_null;
	}
	// =====================================================================================

	else
	{
		f->facestyle = face_normal;
	}
	return f->facestyle;
}

// =====================================================================================
//  ReadSurfs
// =====================================================================================
static auto ReadSurfs(FILE *file) -> SurfchainBSP *
{
	int r;
	int detaillevel;
	int planenum, g_texinfo, contents, numpoints;
	FaceBSP *f;
	int i;
	double v[3];
	int line = 0;
	double inaccuracy, inaccuracy_count = 0.0, inaccuracy_total = 0.0, inaccuracy_max = 0.0;

	// read in the polygons
	while (true)
	{
		if (file == polyfiles[2] && g_nohull2)
			break;
		line++;
		r = fscanf(file, "%i %i %i %i %i\n", &detaillevel, &planenum, &g_texinfo, &contents, &numpoints);
		if (r == 0 || r == -1)
		{
			return nullptr;
		}
		if (planenum == -1) // end of model
		{
			break;
		}
		if (r != 5)
		{
			Error("ReadSurfs (line %i): scanf failure", line);
		}
		if (numpoints > MAXPOINTS)
		{
			Error("ReadSurfs (line %i): %i > MAXPOINTS\nThis is caused by a face with too many verticies (typically found on end-caps of high-poly cylinders)\n", line, numpoints);
		}
		if (planenum > g_numplanes)
		{
			Error("ReadSurfs (line %i): %i > g_numplanes\n", line, planenum);
		}
		if (g_texinfo > g_numtexinfo)
		{
			Error("ReadSurfs (line %i): %i > g_numtexinfo", line, g_texinfo);
		}
		if (detaillevel < 0)
		{
			Error("ReadSurfs (line %i): detaillevel %i < 0", line, detaillevel);
		}

		if (!strcasecmp(GetTextureByNumber(g_texinfo), "skip"))
		{
			for (i = 0; i < numpoints; i++)
			{
				line++;
				r = fscanf(file, "%lf %lf %lf\n", &v[0], &v[1], &v[2]);
				if (r != 3)
				{
					Error("::ReadSurfs (face_skip), fscanf of points failed at line %i", line);
				}
			}
			fscanf(file, "\n");
			continue;
		}

		f = AllocFace();
		f->detaillevel = detaillevel;
		f->planenum = planenum;
		f->texturenum = g_texinfo;
		f->contents = contents;
		f->numpoints = numpoints;
		f->next = validfaces[planenum];
		validfaces[planenum] = f;

		SetFaceType(f);

		for (i = 0; i < f->numpoints; i++)
		{
			line++;
			r = fscanf(file, "%lf %lf %lf\n", &v[0], &v[1], &v[2]);
			if (r != 3)
			{
				Error("::ReadSurfs (face_normal), fscanf of points failed at line %i", line);
			}
			VectorCopy(v, f->pts[i]);
		}
		fscanf(file, "\n");
	}

	return SurflistFromValidFaces();
}
static auto ReadBrushes(FILE *file) -> BrushBSP *
{
	BrushBSP *brushes = nullptr;
	while (true)
	{
		if (file == brushfiles[2] && g_nohull2)
			break;
		int r;
		int brushinfo;
		r = fscanf(file, "%i\n", &brushinfo);
		if (r == 0 || r == -1)
		{
			if (brushes == nullptr)
			{
				Error("ReadBrushes: no more models");
			}
			else
			{
				Error("ReadBrushes: file end");
			}
		}
		if (brushinfo == -1)
		{
			break;
		}
		BrushBSP *b;
		b = AllocBrush();
		b->next = brushes;
		brushes = b;
		SideBSP **psn;
		psn = &b->sides;
		while (true)
		{
			int planenum;
			int numpoints;
			r = fscanf(file, "%i %u\n", &planenum, &numpoints);
			if (r != 2)
			{
				Error("ReadBrushes: get side failed");
			}
			if (planenum == -1)
			{
				break;
			}
			SideBSP *s;
			s = AllocSide();
			s->plane = g_dplanes[planenum ^ 1];
			s->w = new Winding(numpoints);
			int x;
			for (x = 0; x < numpoints; x++)
			{
				double v[3];
				r = fscanf(file, "%lf %lf %lf\n", &v[0], &v[1], &v[2]);
				if (r != 3)
				{
					Error("ReadBrushes: get point failed");
				}
				VectorCopy(v, s->w->m_Points[numpoints - 1 - x]);
			}
			s->next = nullptr;
			*psn = s;
			psn = &s->next;
		}
	}
	return brushes;
}

// =====================================================================================
//  ProcessModel (model a.k.a brush entities)
// =====================================================================================
static auto ProcessModel() -> bool
{
	SurfchainBSP *surfs;
	BrushBSP *detailbrushes;
	NodeBSP *nodes;
	BSPLumpModel *model;
	int startleafs;

	surfs = ReadSurfs(polyfiles[0]);

	if (!surfs)
		return false; // all models are done
	detailbrushes = ReadBrushes(brushfiles[0]);

	hlassume(g_nummodels < MAX_MAP_MODELS, assume_MAX_MAP_MODELS);

	startleafs = g_numleafs;
	int modnum = g_nummodels;
	model = &g_dmodels[modnum];
	g_nummodels++;

	g_hullnum = 0; // vluzacn
	VectorFill(model->mins, 99999);
	VectorFill(model->maxs, -99999);
	{
		vec3_t mins, maxs;
		int i;
		VectorSubtract(surfs->mins, g_hull_size[g_hullnum][0], mins);
		VectorSubtract(surfs->maxs, g_hull_size[g_hullnum][1], maxs);
		for (i = 0; i < 3; i++)
		{
			if (mins[i] > maxs[i])
			{
				vec_t tmp;
				tmp = (mins[i] + maxs[i]) / 2;
				mins[i] = tmp;
				maxs[i] = tmp;
			}
		}
		for (i = 0; i < 3; i++)
		{
			model->maxs[i] = qmax(model->maxs[i], maxs[i]);
			model->mins[i] = qmin(model->mins[i], mins[i]);
		}
	}

	// SolidBSP generates a node tree
	nodes = SolidBSP(surfs,
					 detailbrushes,
					 modnum == 0);

	// build all the portals in the bsp tree
	// some portals are solid polygons, and some are paths to other leafs
	if (g_nummodels == 1) // assume non-world bmodels are simple
	{
		FillInside(nodes);
		nodes = FillOutside(nodes, (g_bLeaked != true), 0); // make a leakfile if bad
	}

	FreePortals(nodes);

	// fix tjunctions
	tjunc(nodes);

	MakeFaceEdges();

	// emit the faces for the bsp file
	model->headnode[0] = g_numnodes;
	model->firstface = g_numfaces;
	bool novisiblebrushes = false;
	// model->headnode[0]<0 will crash HL, so must split it.
	if (nodes->planenum == -1)
	{
		novisiblebrushes = true;
		if (nodes->markfaces[0] != nullptr)
			hlassume(false, assume_EmptySolid);
		if (g_numplanes == 0)
			Error("No valid planes.\n");
		nodes->planenum = 0; // arbitrary plane
		nodes->children[0] = AllocNode();
		nodes->children[0]->planenum = -1;
		nodes->children[0]->contents = CONTENTS_EMPTY;
		nodes->children[0]->isdetail = false;
		nodes->children[0]->isportalleaf = true;
		nodes->children[0]->iscontentsdetail = false;
		nodes->children[0]->faces = nullptr;
		nodes->children[0]->markfaces = (FaceBSP **)calloc(1, sizeof(FaceBSP *));
		VectorFill(nodes->children[0]->mins, 0);
		VectorFill(nodes->children[0]->maxs, 0);
		nodes->children[1] = AllocNode();
		nodes->children[1]->planenum = -1;
		nodes->children[1]->contents = CONTENTS_EMPTY;
		nodes->children[1]->isdetail = false;
		nodes->children[1]->isportalleaf = true;
		nodes->children[1]->iscontentsdetail = false;
		nodes->children[1]->faces = nullptr;
		nodes->children[1]->markfaces = (FaceBSP **)calloc(1, sizeof(FaceBSP *));
		VectorFill(nodes->children[1]->mins, 0);
		VectorFill(nodes->children[1]->maxs, 0);
		nodes->contents = 0;
		nodes->isdetail = false;
		nodes->isportalleaf = false;
		nodes->faces = nullptr;
		nodes->markfaces = nullptr;
		VectorFill(nodes->mins, 0);
		VectorFill(nodes->maxs, 0);
	}
	WriteDrawNodes(nodes);
	model->numfaces = g_numfaces - model->firstface;
	model->visleafs = g_numleafs - startleafs;

	// the clipping hulls are simpler
	for (g_hullnum = 1; g_hullnum < NUM_HULLS; g_hullnum++)
	{
		surfs = ReadSurfs(polyfiles[g_hullnum]);
		detailbrushes = ReadBrushes(brushfiles[g_hullnum]);
		{
			int hullnum = g_hullnum;
			vec3_t mins, maxs;
			int i;
			VectorSubtract(surfs->mins, g_hull_size[hullnum][0], mins);
			VectorSubtract(surfs->maxs, g_hull_size[hullnum][1], maxs);
			for (i = 0; i < 3; i++)
			{
				if (mins[i] > maxs[i])
				{
					vec_t tmp;
					tmp = (mins[i] + maxs[i]) / 2;
					mins[i] = tmp;
					maxs[i] = tmp;
				}
			}
			for (i = 0; i < 3; i++)
			{
				model->maxs[i] = qmax(model->maxs[i], maxs[i]);
				model->mins[i] = qmin(model->mins[i], mins[i]);
			}
		}
		nodes = SolidBSP(surfs,
						 detailbrushes,
						 modnum == 0);
		if (g_nummodels == 1) // assume non-world bmodels are simple
		{
			nodes = FillOutside(nodes, (g_bLeaked != true), g_hullnum);
		}
		FreePortals(nodes);
		/*
			KGP 12/31/03 - need to test that the head clip node isn't empty; if it is
			we need to set model->headnode equal to the content type of the head, or create
			a trivial single-node case where the content type is the same for both leaves
			if setting the content type is invalid.
		*/
		if (nodes->planenum == -1) // empty!
		{
			model->headnode[g_hullnum] = nodes->contents;
		}
		else
		{
			model->headnode[g_hullnum] = g_numclipnodes;
			WriteClipNodes(nodes);
		}
	}

	Entity *ent;
	ent = EntityForModel(modnum);
	if (ent != &g_entities[0] && *ValueForKey(ent, "zhlt_minsmaxs"))
	{
		double origin[3], mins[3], maxs[3];
		VectorClear(origin);
		sscanf(ValueForKey(ent, "origin"), "%lf %lf %lf", &origin[0], &origin[1], &origin[2]);
		if (sscanf(ValueForKey(ent, "zhlt_minsmaxs"), "%lf %lf %lf %lf %lf %lf", &mins[0], &mins[1], &mins[2], &maxs[0], &maxs[1], &maxs[2]) == 6)
		{
			VectorSubtract(mins, origin, model->mins);
			VectorSubtract(maxs, origin, model->maxs);
		}
	}
	if (model->mins[0] > model->maxs[0])
	{
		Entity *ent = EntityForModel(g_nummodels - 1);
		if (g_nummodels - 1 != 0 && ent == &g_entities[0])
		{
			ent = nullptr;
		}
		Warning(R"(Empty solid entity: model %d (entity: classname "%s", origin "%s", targetname "%s"))",
				g_nummodels - 1,
				(ent ? ValueForKey(ent, "classname") : "unknown"),
				(ent ? ValueForKey(ent, "origin") : "unknown"),
				(ent ? ValueForKey(ent, "targetname") : "unknown"));
		VectorClear(model->mins); // fix "backward minsmaxs" in HL
		VectorClear(model->maxs);
	}
	else if (novisiblebrushes)
	{
		Entity *ent = EntityForModel(g_nummodels - 1);
		if (g_nummodels - 1 != 0 && ent == &g_entities[0])
		{
			ent = nullptr;
		}
		Warning(R"(No visible brushes in solid entity: model %d (entity: classname "%s", origin "%s", targetname "%s", range (%.0f,%.0f,%.0f) - (%.0f,%.0f,%.0f)))",
				g_nummodels - 1,
				(ent ? ValueForKey(ent, "classname") : "unknown"),
				(ent ? ValueForKey(ent, "origin") : "unknown"),
				(ent ? ValueForKey(ent, "targetname") : "unknown"),
				model->mins[0], model->mins[1], model->mins[2], model->maxs[0], model->maxs[1], model->maxs[2]);
	}
	return true;
}

// =====================================================================================
//  ProcessFile
// =====================================================================================
static void ProcessFile(const char *const filename)
{
	int i;
	char name[_MAX_PATH];

	// delete existing files
	safe_snprintf(g_portfilename, _MAX_PATH, "%s.prt", filename);
	unlink(g_portfilename);

	safe_snprintf(g_pointfilename, _MAX_PATH, "%s.pts", filename);
	unlink(g_pointfilename);

	safe_snprintf(g_linefilename, _MAX_PATH, "%s.lin", filename);
	unlink(g_linefilename);

	safe_snprintf(g_extentfilename, _MAX_PATH, "%s.ext", filename);
	unlink(g_extentfilename);
	// open the hull files
	for (i = 0; i < NUM_HULLS; i++)
	{
		// mapname.p[0-3]
		snprintf(name, sizeof(name), "%s.p%i", filename, i);
		polyfiles[i] = fopen(name, "r");

		if (!polyfiles[i])
			Error("Can't open %s", name);
		snprintf(name, sizeof(name), "%s.b%i", filename, i);
		brushfiles[i] = fopen(name, "r");
		if (!brushfiles[i])
			Error("Can't open %s", name);
	}
	{
		FILE *f;
		char name[_MAX_PATH];
		safe_snprintf(name, _MAX_PATH, "%s.hsz", filename);
		f = fopen(name, "r");
		if (!f)
		{
			Warning("Couldn't open %s", name);
		}
		else
		{
			float x1, y1, z1;
			float x2, y2, z2;
			for (i = 0; i < NUM_HULLS; i++)
			{
				int count;
				count = fscanf(f, "%f %f %f %f %f %f\n", &x1, &y1, &z1, &x2, &y2, &z2);
				if (count != 6)
				{
					Error("Load hull size (line %i): scanf failure", i + 1);
				}
				g_hull_size[i][0][0] = x1;
				g_hull_size[i][0][1] = y1;
				g_hull_size[i][0][2] = z1;
				g_hull_size[i][1][0] = x2;
				g_hull_size[i][1][1] = y2;
				g_hull_size[i][1][2] = z2;
			}
			fclose(f);
		}
	}

	// load the output of csg
	safe_snprintf(g_bspfilename, _MAX_PATH, "%s.bsp", filename);
	LoadBSPFile(g_bspfilename);
	ParseEntities();
	{
		char name[_MAX_PATH];
		safe_snprintf(name, _MAX_PATH, "%s.pln", filename);
		FILE *planefile = fopen(name, "rb");
		if (!planefile)
		{
			Warning("Couldn't open %s", name);
#undef dplane_t
#undef g_dplanes
			for (i = 0; i < g_numplanes; i++)
			{
				plane_t *mp = &g_mapplanes[i];
				dplane_t *dp = &g_dplanes[i];
				VectorCopy(dp->normal, mp->normal);
				mp->dist = dp->dist;
				mp->type = dp->type;
			}
#define dplane_t plane_t
#define g_dplanes g_mapplanes
		}
		else
		{
			if (q_filelength(planefile) != g_numplanes * sizeof(dplane_t))
			{
				Error("Invalid plane data");
			}
			SafeRead(planefile, g_dplanes, g_numplanes * sizeof(dplane_t));
			fclose(planefile);
		}
	}
	// init the tables to be shared by all models
	BeginBSPFile();

	// process each model individually
	while (ProcessModel())
		;

	// write the updated bsp file out
	FinishBSPFile();

	// Because the bsp file has been updated, these polyfiles are no longer valid.
	for (i = 0; i < NUM_HULLS; i++)
	{
		snprintf(name, sizeof(name), "%s.p%i", filename, i);
		fclose(polyfiles[i]);
		polyfiles[i] = nullptr;
		unlink(name);
		snprintf(name, sizeof(name), "%s.b%i", filename, i);
		fclose(brushfiles[i]);
		brushfiles[i] = nullptr;
		unlink(name);
	}
	safe_snprintf(name, _MAX_PATH, "%s.hsz", filename);
	unlink(name);
	safe_snprintf(name, _MAX_PATH, "%s.pln", filename);
	unlink(name);
}

void HandleArgs(int argc, char **argv, const char *&mapname_from_arg)
{
	int i;
	for (i = 1; i < argc; i++)
	{
		if (!strcasecmp(argv[i], "-nohull2"))
		{
			g_nohull2 = true;
		}
		else if (!strcasecmp(argv[i], "-subdivide"))
		{
			if (i + 1 < argc) // added "1" .--vluzacn
			{
				g_subdivide_size = atoi(argv[++i]);
				if (g_subdivide_size > MAX_SUBDIVIDE_SIZE)
				{
					Warning("Maximum value for subdivide size is %i, '-subdivide %i' ignored",
							MAX_SUBDIVIDE_SIZE, g_subdivide_size);
					g_subdivide_size = MAX_SUBDIVIDE_SIZE;
				}
				else if (g_subdivide_size < MIN_SUBDIVIDE_SIZE)
				{
					Warning("Mininum value for subdivide size is %i, '-subdivide %i' ignored",
							MIN_SUBDIVIDE_SIZE, g_subdivide_size);
					g_subdivide_size = MIN_SUBDIVIDE_SIZE; // MAX_SUBDIVIDE_SIZE; //--vluzacn
				}
			}
			else
			{
				Usage(ProgramType::PROGRAM_BSP);
			}
		}
		else if (!strcasecmp(argv[i], "-maxnodesize"))
		{
			if (i + 1 < argc) // added "1" .--vluzacn
			{
				g_maxnode_size = atoi(argv[++i]);
				if (g_maxnode_size > MAX_MAXNODE_SIZE)
				{
					Warning("Maximum value for max node size is %i, '-maxnodesize %i' ignored",
							MAX_MAXNODE_SIZE, g_maxnode_size);
					g_maxnode_size = MAX_MAXNODE_SIZE;
				}
				else if (g_maxnode_size < MIN_MAXNODE_SIZE)
				{
					Warning("Mininimum value for max node size is %i, '-maxnodesize %i' ignored",
							MIN_MAXNODE_SIZE, g_maxnode_size);
					g_maxnode_size = MIN_MAXNODE_SIZE; // MAX_MAXNODE_SIZE; //vluzacn
				}
			}
			else
			{
				Usage(ProgramType::PROGRAM_BSP);
			}
		}
		else if (!strcasecmp(argv[i], "-texdata"))
		{
			if (i + 1 < argc) // added "1" .--vluzacn
			{
				int x = atoi(argv[++i]) * 1024;

				// if (x > g_max_map_miptex) //--vluzacn
				{
					g_max_map_miptex = x;
				}
			}
			else
			{
				Usage(ProgramType::PROGRAM_BSP);
			}
		}
		else if (!strcasecmp(argv[i], "-lightdata"))
		{
			if (i + 1 < argc) // added "1" .--vluzacn
			{
				int x = atoi(argv[++i]) * 1024;

				// if (x > g_max_map_lightdata) //--vluzacn
				{
					g_max_map_lightdata = x;
				}
			}
			else
			{
				Usage(ProgramType::PROGRAM_BSP);
			}
		}
		else if (!mapname_from_arg)
		{
			mapname_from_arg = argv[i];
		}
		else
		{
			Log("Unknown option \"%s\"\n", argv[i]);
			Usage(ProgramType::PROGRAM_BSP);
		}
	}

	if (!mapname_from_arg)
	{
		Log("No mapfile specified\n");
		Usage(ProgramType::PROGRAM_BSP);
	}
}

// =====================================================================================
//  main
// =====================================================================================
auto main(const int argc, char **argv) -> int
{
	double start, end;
	const char *mapname_from_arg = nullptr;

	g_Program = "sBSP";
	if (InitConsole(argc, argv) < 0)
		Usage(ProgramType::PROGRAM_BSP);
	// if we dont have any command line argvars, print out usage and die
	if (argc == 1)
		Usage(ProgramType::PROGRAM_BSP);

	HandleArgs(argc, argv, mapname_from_arg);

	safe_strncpy(g_Mapname, mapname_from_arg, _MAX_PATH);
	FlipSlashes(g_Mapname);
	StripExtension(g_Mapname);

	atexit(CloseLog);
	ThreadSetDefault();
	ThreadSetPriority(g_threadpriority);
	LogArguments(argc, argv);
	CheckForErrorLog();
	hlassume(CalcFaceExtents_test(), assume_first);
	dtexdata_init();
	atexit(dtexdata_free);

	// BEGIN BSP
	start = I_FloatTime();

	ProcessFile(g_Mapname);

	end = I_FloatTime();
	LogTimeElapsed(end - start);
	// END BSP

	FreeAllowableOutsideList();
	return 0;
}