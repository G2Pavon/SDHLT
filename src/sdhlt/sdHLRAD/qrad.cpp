/*
 
    R A D I O S I T Y    -aka-    R A D 

    Code based on original code from Valve Software, 
    Modified by Sean "Zoner" Cavanaugh (seanc@gearboxsoftware.com) with permission.
    Modified by Tony "Merl" Moore (merlinis@bigpond.net.au) [AJM]
	Modified by amckern (amckern@yahoo.com)
	Modified by vluzacn (vluzacn@163.com)
	Modified by seedee (cdaniel9000@gmail.com)

*/

#ifdef SYSTEM_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <vector>
#include <string>

#include "qrad.h"
#include "arguments.h"


/*
 * NOTES
 * -----
 * every surface must be divided into at least two g_patches each axis
 */

bool            g_pre25update = DEFAULT_PRE25UPDATE;

vec_t           g_fade = DEFAULT_FADE;

patch_t*        g_face_patches[MAX_MAP_FACES];
entity_t*       g_face_entity[MAX_MAP_FACES];
eModelLightmodes g_face_lightmode[MAX_MAP_FACES];
patch_t*		g_patches;
entity_t*		g_face_texlights[MAX_MAP_FACES];
unsigned        g_num_patches;

static vec3_t   (*emitlight)[MAXLIGHTMAPS]; //LRC
static vec3_t   (*addlight)[MAXLIGHTMAPS]; //LRC
static unsigned char (*newstyles)[MAXLIGHTMAPS];

vec3_t          g_face_offset[MAX_MAP_FACES];              // for rotating bmodels

unsigned        g_numbounce = DEFAULT_BOUNCE;              // 3; /* Originally this was 8 */

vec_t			g_limitthreshold = DEFAULT_LIMITTHRESHOLD;

float           g_lightscale = DEFAULT_LIGHTSCALE;

char            g_source[_MAX_PATH] = "";

char            g_vismatfile[_MAX_PATH] = "";
bool            g_extra = DEFAULT_EXTRA;

float           g_smoothing_threshold;
float			g_smoothing_threshold_2;

float_type g_transfer_compress_type = DEFAULT_TRANSFER_COMPRESS_TYPE;
vector_type g_rgbtransfer_compress_type = DEFAULT_RGBTRANSFER_COMPRESS_TYPE;
bool g_bleedfix = DEFAULT_BLEEDFIX;

// Cosine of smoothing angle(in radians)
bool            g_estimate = DEFAULT_ESTIMATE;


// Patch creation and subdivision criteria
vec_t           g_chop = DEFAULT_CHOP;
vec_t           g_texchop = DEFAULT_TEXCHOP;

// Opaque faces
opaqueList_t*   g_opaque_face_list = NULL;
unsigned        g_opaque_face_count = 0;
unsigned        g_max_opaque_face_count = 0;               // Current array maximum (used for reallocs)
vec_t			g_corings[ALLSTYLES];
vec3_t*			g_translucenttextures = NULL;
vec_t			g_translucentdepth = DEFAULT_TRANSLUCENTDEPTH;
vec_t			g_blur = DEFAULT_BLUR;
bool			g_noemitterrange = DEFAULT_NOEMITTERRANGE;
vec_t			g_texlightgap = DEFAULT_TEXLIGHTGAP;

// Misc
int             leafparents[MAX_MAP_LEAFS];
int             nodeparents[MAX_MAP_NODES];
int				stylewarningcount = 0;
int				stylewarningnext = 1;
vec_t g_maxdiscardedlight = 0;
vec3_t g_maxdiscardedpos = {0, 0, 0};

// =====================================================================================
//  MakeParents
//      blah
// =====================================================================================
static void     MakeParents(const int nodenum, const int parent)
{
    int             i;
    int             j;
    dnode_t*        node;

    nodeparents[nodenum] = parent;
    node = g_dnodes + nodenum;

    for (i = 0; i < 2; i++)
    {
        j = node->children[i];
        if (j < 0)
        {
            leafparents[-j - 1] = nodenum;
        }
        else
        {
            MakeParents(j, nodenum);
        }
    }
}

// =====================================================================================
//
//    TEXTURE LIGHT VALUES
//
// =====================================================================================

// misc
typedef struct
{
    std::string     name;
    vec3_t          value;
    const char*     filename; //either info_texlights or lights.rad filename
}
texlight_t;

static std::vector< texlight_t > s_texlights;
typedef std::vector< texlight_t >::iterator texlight_i;

std::vector<minlight_t> s_minlights;

// =====================================================================================
//  LightForTexture
// =====================================================================================
static void     LightForTexture(const char* const name, vec3_t result)
{
    texlight_i it;
    for (it = s_texlights.begin(); it != s_texlights.end(); it++)
    {
        if (!strcasecmp(name, it->name.c_str()))
        {
            VectorCopy(it->value, result);
            return;
        }
    }
    VectorClear(result);
}


// =====================================================================================
//
//    MAKE FACES
//
// =====================================================================================

// =====================================================================================
//  BaseLightForFace
// =====================================================================================
static void     BaseLightForFace(const dface_t* const f, vec3_t light)
{
	int fn = f - g_dfaces;
	if (g_face_texlights[fn])
	{
		double r, g, b, scaler;
		switch (sscanf (ValueForKey (g_face_texlights[fn], "_light"), "%lf %lf %lf %lf", &r, &g, &b, &scaler))
		{
		case -1:
		case 0:
			r = 0.0;
		case 1:
			g = b = r;
		case 3:
			break;
		case 4:
			r *= scaler / 255.0;
			g *= scaler / 255.0;
			b *= scaler / 255.0;
			break;
		default:
			vec3_t origin;
			GetVectorForKey (g_face_texlights[fn], "origin", origin);
			Log("light at (%f,%f,%f) has bad or missing '_light' value : '%s'\n",
				origin[0], origin[1], origin[2], ValueForKey (g_face_texlights[fn], "_light"));
			r = g = b = 0;
			break;
		}
		light[0] = r > 0? r: 0;
		light[1] = g > 0? g: 0;
		light[2] = b > 0? b: 0;
		return;
	}
    texinfo_t*      tx;
    miptex_t*       mt;
    int             ofs;

    //
    // check for light emited by texture
    //
    tx = &g_texinfo[f->texinfo];

    ofs = ((dmiptexlump_t*)g_dtexdata)->dataofs[tx->miptex];
    mt = (miptex_t*)((byte*) g_dtexdata + ofs);

    LightForTexture(mt->name, light);
}

// =====================================================================================
//  IsSpecial
// =====================================================================================
static bool     IsSpecial(const dface_t* const f)
{
    return g_texinfo[f->texinfo].flags & TEX_SPECIAL;
}

// =====================================================================================
//  PlacePatchInside
// =====================================================================================
static bool     PlacePatchInside(patch_t* patch)
{
    const dplane_t* plane;
    const vec_t*    face_offset = g_face_offset[patch->faceNumber];

    plane = getPlaneFromFaceNumber(patch->faceNumber);

	vec_t pointsfound;
	vec_t pointstested;
	pointsfound = pointstested = 0;
	vec3_t center;
	bool found;
	vec3_t bestpoint;
	vec_t bestdist = -1.0;
	vec3_t point;
	vec_t dist;
	vec3_t v;

	patch->winding->getCenter (center);
	found = false;
	
	VectorMA (center, PATCH_HUNT_OFFSET, plane->normal, point);
	pointstested++;
    if (HuntForWorld (point, face_offset, plane, 4, 0.2, PATCH_HUNT_OFFSET) ||
		HuntForWorld (point, face_offset, plane, 4, 0.8, PATCH_HUNT_OFFSET))
	{
		pointsfound++;
		VectorSubtract (point, center, v);
		dist = VectorLength (v);
		if (!found || dist < bestdist)
		{
			found = true;
			VectorCopy (point, bestpoint);
			bestdist = dist;
		}
	}
	{
		for (int i = 0; i < patch->winding->m_NumPoints; i++)
		{
			const vec_t *p1;
			const vec_t *p2;
			p1 = patch->winding->m_Points[i];
			p2 = patch->winding->m_Points[(i+1)%patch->winding->m_NumPoints];
			VectorAdd (p1, p2, point);
			VectorAdd (point, center, point);
			VectorScale (point, 1.0/3.0, point);
			VectorMA (point, PATCH_HUNT_OFFSET, plane->normal, point);
			pointstested++;
			if (HuntForWorld (point, face_offset, plane, 4, 0.2, PATCH_HUNT_OFFSET) ||
				HuntForWorld (point, face_offset, plane, 4, 0.8, PATCH_HUNT_OFFSET))
			{
				pointsfound++;
				VectorSubtract (point, center, v);
				dist = VectorLength (v);
				if (!found || dist < bestdist)
				{
					found = true;
					VectorCopy (point, bestpoint);
					bestdist = dist;
				}
			}
		}
	}
	patch->exposure = pointsfound / pointstested;

	if (found)
	{
		VectorCopy (bestpoint, patch->origin);
		patch->flags = ePatchFlagNull;
		return true;
	}
	else
	{
		VectorMA (center, PATCH_HUNT_OFFSET, plane->normal, patch->origin);
		patch->flags = ePatchFlagOutside;
		return false;
	}
}
static void		UpdateEmitterInfo (patch_t *patch)
{
#if ACCURATEBOUNCE_DEFAULT_SKYLEVEL + 3 > SKYLEVELMAX
#error "please raise SKYLEVELMAX"
#endif
	const vec_t *origin = patch->origin;
	const Winding *winding = patch->winding;
	vec_t radius = ON_EPSILON;
	for (int x = 0; x < winding->m_NumPoints; x++)
	{
		vec3_t delta;
		vec_t dist;
		VectorSubtract (winding->m_Points[x], origin, delta);
		dist = VectorLength (delta);
		if (dist > radius)
		{
			radius = dist;
		}
	}
	int skylevel = ACCURATEBOUNCE_DEFAULT_SKYLEVEL;
	vec_t area = winding->getArea ();
	vec_t size = 0.8f;
	if (area < size * radius * radius) // the shape is too thin
	{
		skylevel++;
		size *= 0.25f;
		if (area < size * radius * radius)
		{
			skylevel++;
			size *= 0.25f;
			if (area < size * radius * radius)
			{
				// stop here
				radius = sqrt (area / size);
				// just decrease the range to limit the use of the new method. because when the area is small, the new method becomes randomized and unstable.
			}
		}
	}
	patch->emitter_range = ACCURATEBOUNCE_THRESHOLD * radius;
	if (g_noemitterrange)
	{
		patch->emitter_range = 0.0;
	}
	patch->emitter_skylevel = skylevel;
}


// =====================================================================================
//
//    SUBDIVIDE PATCHES
//
// =====================================================================================

// misc
#define MAX_SUBDIVIDE 16384
static Winding* windingArray[MAX_SUBDIVIDE];
static unsigned g_numwindings = 0;

// =====================================================================================
//  cutWindingWithGrid
//      Caller must free this returned value at some point
// =====================================================================================
static void     cutWindingWithGrid (patch_t *patch, const dplane_t *plA, const dplane_t *plB)
	// This function has been rewritten because the original one is not totally correct and may fail to do what it claims.
{
	// patch->winding->m_NumPoints must > 0
	// plA->dist and plB->dist will not be used
	Winding *winding = NULL;
	vec_t chop;
	vec_t epsilon;
	const int max_gridsize = 64;
	vec_t gridstartA;
	vec_t gridstartB;
	int gridsizeA;
	int gridsizeB;
	vec_t gridchopA;
	vec_t gridchopB;
	int numstrips;
	
	winding = new Winding (*patch->winding); // perform all the operations on the copy
	chop = patch->chop;
	chop = qmax (1.0, chop);
	epsilon = 0.6;

	// optimize the grid
	{
		vec_t minA;
		vec_t maxA;
		vec_t minB;
		vec_t maxB;

		minA = minB = BOGUS_RANGE;
		maxA = maxB = -BOGUS_RANGE;
		for (int x = 0; x < winding->m_NumPoints; x++)
		{
			vec_t *point;
			vec_t dotA;
			vec_t dotB;
			point = winding->m_Points[x];
			dotA = DotProduct (point, plA->normal);
			minA = qmin (minA, dotA);
			maxA = qmax (maxA, dotA);
			dotB = DotProduct (point, plB->normal);
			minB = qmin (minB, dotB);
			maxB = qmax (maxB, dotB);
		}

		gridchopA = chop;
		gridsizeA = (int)ceil ((maxA - minA - 2 * epsilon) / gridchopA);
		gridsizeA = qmax (1, gridsizeA);
		if (gridsizeA > max_gridsize)
		{
			gridsizeA = max_gridsize;
			gridchopA = (maxA - minA) / (vec_t)gridsizeA;
		}
		gridstartA = (minA + maxA) / 2.0 - (gridsizeA / 2.0) * gridchopA;

		gridchopB = chop;
		gridsizeB = (int)ceil ((maxB - minB - 2 * epsilon) / gridchopB);
		gridsizeB = qmax (1, gridsizeB);
		if (gridsizeB > max_gridsize)
		{
			gridsizeB = max_gridsize;
			gridchopB = (maxB - minB) / (vec_t)gridsizeB;
		}
		gridstartB = (minB + maxB) / 2.0 - (gridsizeB / 2.0) * gridchopB;
	}
	
	// cut the winding by the direction of plane A and save into windingArray
	{
		g_numwindings = 0;
		for (int i = 1; i < gridsizeA; i++)
		{
			vec_t dist;
			Winding *front = NULL;
			Winding *back = NULL;

			dist = gridstartA + i * gridchopA;
			winding->Clip (plA->normal, dist, &front, &back);

			if (!front || front->WindingOnPlaneSide (plA->normal, dist, epsilon) == SIDE_ON) // ended
			{
				if (front)
				{
					delete front;
					front = NULL;
				}
				if (back)
				{
					delete back;
					back = NULL;
				}
				break;
			}
			if (!back || back->WindingOnPlaneSide (plA->normal, dist, epsilon) == SIDE_ON) // didn't begin
			{
				if (front)
				{
					delete front;
					front = NULL;
				}
				if (back)
				{
					delete back;
					back = NULL;
				}
				continue;
			}

			delete winding;
			winding = NULL;

			windingArray[g_numwindings] = back;
			g_numwindings++;
			back = NULL;

			winding = front;
			front = NULL;
		}

		windingArray[g_numwindings] = winding;
		g_numwindings++;
		winding = NULL;
	}
	
	// cut by the direction of plane B
	{
		numstrips = g_numwindings;
		for (int i = 0; i < numstrips; i++)
		{
			Winding *strip = windingArray[i];
			windingArray[i] = NULL;

			for (int j = 1; j < gridsizeB; j++)
			{
				vec_t dist;
				Winding *front = NULL;
				Winding *back = NULL;

				dist = gridstartB + j * gridchopB;
				strip->Clip (plB->normal, dist, &front, &back);
				
				if (!front || front->WindingOnPlaneSide (plB->normal, dist, epsilon) == SIDE_ON) // ended
				{
					if (front)
					{
						delete front;
						front = NULL;
					}
					if (back)
					{
						delete back;
						back = NULL;
					}
					break;
				}
				if (!back || back->WindingOnPlaneSide (plB->normal, dist, epsilon) == SIDE_ON) // didn't begin
				{
					if (front)
					{
						delete front;
						front = NULL;
					}
					if (back)
					{
						delete back;
						back = NULL;
					}
					continue;
				}

				delete strip;
				strip = NULL;

				windingArray[g_numwindings] = back;
				g_numwindings++;
				back = NULL;

				strip = front;
				front = NULL;
			}

			windingArray[g_numwindings] = strip;
			g_numwindings++;
			strip = NULL;
		}
	}

	delete patch->winding;
	patch->winding = NULL;
}

// =====================================================================================
//  getGridPlanes
//      From patch, determine perpindicular grid planes to subdivide with (returned in planeA and planeB)
//      assume S and T is perpindicular (they SHOULD be in worldcraft 3.3 but aren't always . . .)
// =====================================================================================
static void     getGridPlanes(const patch_t* const p, dplane_t* const pl)
{
    const patch_t*  patch = p;
    dplane_t*       planes = pl;
    const dface_t*  f = g_dfaces + patch->faceNumber;
    texinfo_t*      tx = &g_texinfo[f->texinfo];
    dplane_t*       plane = planes;
    const dplane_t* faceplane = getPlaneFromFaceNumber(patch->faceNumber);
    int             x;

    for (x = 0; x < 2; x++, plane++)
    {
		// cut the patch along texel grid planes
		vec_t			val;
		val = DotProduct (faceplane->normal, tx->vecs[!x]);
		VectorMA (tx->vecs[!x], -val, faceplane->normal, plane->normal);
        VectorNormalize(plane->normal);
        plane->dist = DotProduct(plane->normal, patch->origin);
    }
}

// =====================================================================================
//  SubdividePatch
// =====================================================================================
static void     SubdividePatch(patch_t* patch)
{
    dplane_t        planes[2];
    dplane_t*       plA = &planes[0];
    dplane_t*       plB = &planes[1];
    Winding**       winding;
    unsigned        x;
    patch_t*        new_patch;

    memset(windingArray, 0, sizeof(windingArray));
    g_numwindings = 0;

    getGridPlanes(patch, planes);
    cutWindingWithGrid(patch, plA, plB);

    x = 0;
    patch->next = NULL;
    winding = windingArray;
    while (*winding == NULL)
    {
        winding++;
        x++;
    }
    patch->winding = *winding;
    winding++;
    x++;
    patch->area = patch->winding->getArea();
    patch->winding->getCenter(patch->origin);
    PlacePatchInside(patch);
	UpdateEmitterInfo (patch);

    new_patch = g_patches + g_num_patches;
    for (; x < g_numwindings; x++, winding++)
    {
        if (*winding)
        {
            memcpy(new_patch, patch, sizeof(patch_t));

            new_patch->winding = *winding;
            new_patch->area = new_patch->winding->getArea();
            new_patch->winding->getCenter(new_patch->origin);
            PlacePatchInside(new_patch);
			UpdateEmitterInfo (new_patch);

            new_patch++;
            g_num_patches++;
            hlassume(g_num_patches < MAX_PATCHES, assume_MAX_PATCHES);
        }
    }

    // ATTENTION: We let SortPatches relink all the ->next correctly! instead of doing it here too which is somewhat complicated
}

// =====================================================================================
//  MakePatchForFace
static float    totalarea = 0;
// =====================================================================================

vec_t *chopscales; //[nummiptex]
void ReadCustomChopValue()
{
	int num;
	int i, k;
	entity_t *mapent;
	epair_t *ep;

	num = ((dmiptexlump_t *)g_dtexdata)->nummiptex;
	chopscales = (vec_t *)malloc (num * sizeof(vec_t));
	for (i = 0; i < num; i++)
	{
		chopscales[i] = 1.0;
	}
	for (k = 0; k < g_numentities; k++)
	{
		mapent = &g_entities[k];
		if (strcmp(ValueForKey(mapent, "classname"), "info_chopscale"))
			continue;
		for (i = 0; i < num; i++)
		{
			const char *texname = ((miptex_t*)(g_dtexdata+((dmiptexlump_t*)g_dtexdata)->dataofs[i]))->name;
			for (ep = mapent->epairs; ep; ep = ep->next)
			{
				if (strcasecmp (ep->key, texname))
					continue;
				if (!strcasecmp (ep->key, "origin"))
					continue;
				if (atof (ep->value) <= 0)
					continue;
				chopscales[i] = atof (ep->value);
			}
		}
	}
}
vec_t ChopScaleForTexture (int facenum)
{
    return chopscales[g_texinfo[g_dfaces[facenum].texinfo].miptex];
}
vec_t *g_smoothvalues; //[nummiptex]
void ReadCustomSmoothValue()
{
	int num;
	int i, k;
	entity_t *mapent;
	epair_t *ep;

	num = ((dmiptexlump_t *)g_dtexdata)->nummiptex;
	g_smoothvalues = (vec_t *)malloc (num * sizeof(vec_t));
	for (i = 0; i < num; i++)
	{
		g_smoothvalues[i] = g_smoothing_threshold;
	}
	for (k = 0; k < g_numentities; k++)
	{
		mapent = &g_entities[k];
		if (strcmp(ValueForKey(mapent, "classname"), "info_smoothvalue"))
			continue;
		for (i = 0; i < num; i++)
		{
			const char *texname = ((miptex_t*)(g_dtexdata+((dmiptexlump_t*)g_dtexdata)->dataofs[i]))->name;
			for (ep = mapent->epairs; ep; ep = ep->next)
			{
				if (strcasecmp (ep->key, texname))
					continue;
				if (!strcasecmp (ep->key, "origin"))
					continue;
				g_smoothvalues[i] = cos(atof (ep->value) * (Q_PI / 180.0));
			}
		}
	}
}
void ReadTranslucentTextures()
{
	int num;
	int i, k;
	entity_t *mapent;
	epair_t *ep;

	num = ((dmiptexlump_t *)g_dtexdata)->nummiptex;
	g_translucenttextures = (vec3_t *)malloc (num * sizeof(vec3_t));
	for (i = 0; i < num; i++)
	{
		VectorClear (g_translucenttextures[i]);
	}
	for (k = 0; k < g_numentities; k++)
	{
		mapent = &g_entities[k];
		if (strcmp(ValueForKey(mapent, "classname"), "info_translucent"))
			continue;
		for (i = 0; i < num; i++)
		{
			const char *texname = ((miptex_t*)(g_dtexdata+((dmiptexlump_t*)g_dtexdata)->dataofs[i]))->name;
			for (ep = mapent->epairs; ep; ep = ep->next)
			{
				if (strcasecmp (ep->key, texname))
					continue;
				if (!strcasecmp (ep->key, "origin"))
					continue;
				double r, g, b;
				int count;
				count = sscanf (ep->value, "%lf %lf %lf", &r, &g, &b);
				if (count == 1)
				{
					g = b = r;
				}
				else if (count != 3)
				{
					Warning ("ignore bad translucent value '%s'", ep->value);
					continue;
				}
				if (r < 0.0 || r > 1.0 || g < 0.0 || g > 1.0 || b < 0.0 || b > 1.0)
				{
					Warning ("translucent value should be 0.0-1.0");
					continue;
				}
				g_translucenttextures[i][0] = r;
				g_translucenttextures[i][1] = g;
				g_translucenttextures[i][2] = b;
			}
		}
	}
}
vec3_t *g_lightingconeinfo;//[nummiptex]
static vec_t DefaultScaleForPower (vec_t power)
{
	vec_t scale;
	// scale = Pi / Integrate [2 Pi * Sin [x] * Cos[x] ^ power, {x, 0, Pi / 2}]
	scale = (1 + power) / 2.0;
	return scale;
}
void ReadLightingCone ()
{
	int num;
	int i, k;
	entity_t *mapent;
	epair_t *ep;

	num = ((dmiptexlump_t *)g_dtexdata)->nummiptex;
	g_lightingconeinfo = (vec3_t *)malloc (num * sizeof(vec3_t));
	for (i = 0; i < num; i++)
	{
		g_lightingconeinfo[i][0] = 1.0; // default power
		g_lightingconeinfo[i][1] = 1.0; // default scale
	}
	for (k = 0; k < g_numentities; k++)
	{
		mapent = &g_entities[k];
		if (strcmp(ValueForKey(mapent, "classname"), "info_angularfade"))
			continue;
		for (i = 0; i < num; i++)
		{
			const char *texname = ((miptex_t*)(g_dtexdata+((dmiptexlump_t*)g_dtexdata)->dataofs[i]))->name;
			for (ep = mapent->epairs; ep; ep = ep->next)
			{
				if (strcasecmp (ep->key, texname))
					continue;
				if (!strcasecmp (ep->key, "origin"))
					continue;
				double power, scale;
				int count;
				count = sscanf (ep->value, "%lf %lf", &power, &scale);
				if (count == 1)
				{
					scale = 1.0;
				}
				else if (count != 2)
				{
					Warning ("ignore bad angular fade value '%s'", ep->value);
					continue;
				}
				if (power < 0.0 || scale < 0.0)
				{
					Warning ("ignore disallowed angular fade value '%s'", ep->value);
					continue;
				}
				scale *= DefaultScaleForPower (power);
				g_lightingconeinfo[i][0] = power;
				g_lightingconeinfo[i][1] = scale;
			}
		}
	}
}

static vec_t    getScale(const patch_t* const patch)
{
    dface_t*        f = g_dfaces + patch->faceNumber;
    texinfo_t*      tx = &g_texinfo[f->texinfo];
	const dplane_t*	faceplane = getPlaneFromFace (f);
	vec3_t			vecs_perpendicular[2];
	vec_t			scale[2];
	vec_t			dot;
	
	// snap texture "vecs" to faceplane without affecting texture alignment
	for (int x = 0; x < 2; x++)
	{
		dot = DotProduct (faceplane->normal, tx->vecs[x]);
		VectorMA (tx->vecs[x], -dot, faceplane->normal, vecs_perpendicular[x]);
	}
	
	scale[0] = 1 / qmax (NORMAL_EPSILON, VectorLength (vecs_perpendicular[0]));
	scale[1] = 1 / qmax (NORMAL_EPSILON, VectorLength (vecs_perpendicular[1]));

	// don't care about the angle between vecs[0] and vecs[1] (given the length of "vecs", smaller angle = larger texel area), because gridplanes will have the same angle (also smaller angle = larger patch area)

	return sqrt (scale[0] * scale[1]);
}

// =====================================================================================
//  getChop
// =====================================================================================
static bool		getEmitMode (const patch_t *patch)
{
	bool emitmode = false;
	vec_t value = 
		DotProduct (patch->baselight, patch->texturereflectivity) / 3
		;
	if (g_face_texlights[patch->faceNumber])
	{
		if (*ValueForKey (g_face_texlights[patch->faceNumber], "_scale"))
		{
			value *= FloatForKey (g_face_texlights[patch->faceNumber], "_scale");
		}
	}
	if (value > 0.0)
	{
		emitmode = true;
	}
	if (value < 10.0) // DEFAULT_DLIGHT_THRESHOLD '-dlight'
	{
		emitmode = false;
	}
	if (g_face_texlights[patch->faceNumber])
	{
		switch (IntForKey (g_face_texlights[patch->faceNumber], "_fast"))
		{
		case 1:
			emitmode = false;
			break;
		case 2:
			emitmode = true;
			break;
		}
	}
	return emitmode;
}
static vec_t    getChop(const patch_t* const patch)
{
    vec_t           rval;

	if (g_face_texlights[patch->faceNumber])
	{
		if (*ValueForKey (g_face_texlights[patch->faceNumber], "_chop"))
		{
			rval = FloatForKey (g_face_texlights[patch->faceNumber], "_chop");
			if (rval < 1.0)
			{
				rval = 1.0;
			}
			return rval;
		}
	}
	if (!patch->emitmode)
    {
        rval = g_chop * getScale(patch);
    }
    else
    {
        rval = g_texchop * getScale(patch);
		// we needn't do this now, so let's save our compile time.
    }

	rval *= ChopScaleForTexture (patch->faceNumber);
    return rval;
}

// =====================================================================================
//  MakePatchForFace
// =====================================================================================
static void     MakePatchForFace(const int fn, Winding* w, int style
	, int bouncestyle
	) //LRC
{
    const dface_t*  f = g_dfaces + fn;

    // No g_patches at all for the sky!
    if (!IsSpecial(f))
    {
		if (g_face_texlights[fn])
		{
			style = IntForKey (g_face_texlights[fn], "style");
			if (style < 0)
				style = -style;
			style = (unsigned char)style;
			if (style >= ALLSTYLES)
			{
				Error ("invalid light style: style (%d) >= ALLSTYLES (%d)", style, ALLSTYLES);
			}
		}
        patch_t*        patch;
        vec3_t          light;
        vec3_t          centroid = { 0, 0, 0 };

        int             numpoints = w->m_NumPoints;

        if (numpoints < 3)                                 // WTF! (Actually happens in real-world maps too)
        {
            return;
        }
        if (numpoints > MAX_POINTS_ON_WINDING)
        {
            Error("numpoints %d > MAX_POINTS_ON_WINDING", numpoints);
            return;
        }

        patch = &g_patches[g_num_patches];
        hlassume(g_num_patches < MAX_PATCHES, assume_MAX_PATCHES);
        memset(patch, 0, sizeof(patch_t));

        patch->winding = w;

        patch->area = patch->winding->getArea();
        patch->winding->getCenter(patch->origin);
        patch->faceNumber = fn;

        totalarea += patch->area;


        BaseLightForFace(f, light);
        //LRC        VectorCopy(light, patch->totallight);
        VectorCopy(light, patch->baselight);

		patch->emitstyle = style;

		VectorCopy (g_textures[g_texinfo[f->texinfo].miptex].reflectivity, patch->texturereflectivity);
		if (g_face_texlights[fn] && *ValueForKey (g_face_texlights[fn], "_texcolor"))
		{
			vec3_t texturecolor;
			vec3_t texturereflectivity;
			GetVectorForKey (g_face_texlights[fn], "_texcolor", texturecolor);
			for (int k = 0; k < 3; k++)
			{
				texturecolor[k] = floor (texturecolor[k] + 0.001);
			}
			if (VectorMinimum (texturecolor) < -0.001 || VectorMaximum (texturecolor) > 255.001)
			{
				vec3_t origin;
				GetVectorForKey (g_face_texlights[fn], "origin", origin);
				Error ("light_surface entity at (%g,%g,%g): texture color (%g,%g,%g) must be numbers between 0 and 255.", origin[0], origin[1], origin[2], texturecolor[0], texturecolor[1], texturecolor[2]);
			}
			VectorScale (texturecolor, 1.0 / 255.0, texturereflectivity);
			for (int k = 0; k < 3; k++)
			{
				texturereflectivity[k] = pow (texturereflectivity[k], 1.76f );// 2.0(texgamma cvar) / 2.5 (gamma cvar) * 2.2 (screen gamma) = 1.76
			}
			VectorScale (texturereflectivity, 0.7f, texturereflectivity);
			if (VectorMaximum (texturereflectivity) > 1.0 + NORMAL_EPSILON)
			{
				Warning ("Texture '%s': reflectivity (%f,%f,%f) greater than 1.0.", g_textures[g_texinfo[f->texinfo].miptex].name, texturereflectivity[0], texturereflectivity[1], texturereflectivity[2]);
			}
			VectorCopy (texturereflectivity, patch->texturereflectivity);
		}
		{
			vec_t opacity = 0.0;
			if (g_face_entity[fn] - g_entities == 0)
			{
				opacity = 1.0;
			}
			else
			{
				int x;
				for (x = 0; x < g_opaque_face_count; x++)
				{
					opaqueList_t *op = &g_opaque_face_list[x];
					if (op->entitynum == g_face_entity[fn] - g_entities)
					{
						opacity = 1.0;
						if (op->transparency)
						{
							opacity = 1.0 - VectorAvg (op->transparency_scale);
							opacity = opacity > 1.0? 1.0: opacity < 0.0? 0.0: opacity;
						}
						if (op->style != -1)
						{ // toggleable opaque entity
							if (bouncestyle == -1)
							{ // by default
								opacity = 0.0; // doesn't reflect light
							}
						}
						break;
					}
				}
				if (x == g_opaque_face_count)
				{ // not opaque
					if (bouncestyle != -1)
					{ // with light_bounce
						opacity = 1.0; // reflects light
					}
				}
			}
			VectorScale (patch->texturereflectivity, opacity, patch->bouncereflectivity);
		}
		patch->bouncestyle = bouncestyle;
		if (bouncestyle == 0)
		{ // there is an unnamed light_bounce
			patch->bouncestyle = -1; // reflects light normally
		}
		patch->emitmode = getEmitMode (patch);
        patch->scale = getScale(patch);
        patch->chop = getChop(patch);
		VectorCopy (g_translucenttextures[g_texinfo[f->texinfo].miptex], patch->translucent_v);
		patch->translucent_b = !VectorCompare (patch->translucent_v, vec3_origin);
        PlacePatchInside(patch);
		UpdateEmitterInfo (patch);

        g_face_patches[fn] = patch;
        g_num_patches++;

        // Per-face data
        {
            int             j;

            // Centroid of face for nudging samples in direct lighting pass
            for (j = 0; j < f->numedges; j++)
            {
                int             edge = g_dsurfedges[f->firstedge + j];

                if (edge > 0)
                {
                    VectorAdd(g_dvertexes[g_dedges[edge].v[0]].point, centroid, centroid);
                    VectorAdd(g_dvertexes[g_dedges[edge].v[1]].point, centroid, centroid);
                }
                else
                {
                    VectorAdd(g_dvertexes[g_dedges[-edge].v[1]].point, centroid, centroid);
                    VectorAdd(g_dvertexes[g_dedges[-edge].v[0]].point, centroid, centroid);
                }
            }

            // Fixup centroid for anything with an altered origin (rotating models/turrets mostly)
            // Save them for moving direct lighting points towards the face center
            VectorScale(centroid, 1.0 / (f->numedges * 2), centroid);
            VectorAdd(centroid, g_face_offset[fn], g_face_centroids[fn]);
        }

        {
            vec3_t          mins;
            vec3_t          maxs;

            patch->winding->getBounds(mins, maxs);

			vec_t           amt;
			vec_t           length;
			vec3_t          delta;

			VectorSubtract(maxs, mins, delta);
			length = VectorLength(delta);
			amt = patch->chop;

			if (length > amt)
			{
				SubdividePatch(patch);
			}
        }
    }
}

// =====================================================================================
//  AddFaceToOpaqueList
// =====================================================================================
static void     AddFaceToOpaqueList(
									int entitynum, int modelnum, const vec3_t origin
									, const vec3_t &transparency_scale, const bool transparency
									, int style
									, bool block
									)
{
    if (g_opaque_face_count == g_max_opaque_face_count)
    {
        g_max_opaque_face_count += OPAQUE_ARRAY_GROWTH_SIZE;
        g_opaque_face_list = (opaqueList_t*)realloc(g_opaque_face_list, sizeof(opaqueList_t) * g_max_opaque_face_count);

		hlassume (g_opaque_face_list != NULL, assume_NoMemory);
    }

    {
        opaqueList_t*   opaque = &g_opaque_face_list[g_opaque_face_count];

        g_opaque_face_count++;

		if (transparency && style != -1)
		{
			Warning ("Dynamic shadow is not allowed in entity with custom shadow.\n");
			style = -1;
		}
        VectorCopy(transparency_scale, opaque->transparency_scale);
        opaque->transparency = transparency;
		opaque->entitynum = entitynum;
		opaque->modelnum = modelnum;
		VectorCopy (origin, opaque->origin);
		opaque->style = style;
		opaque->block = block;
    }
}

// =====================================================================================
//  FreeOpaqueFaceList
// =====================================================================================
static void     FreeOpaqueFaceList()
{
    unsigned        x;
    opaqueList_t*   opaque = g_opaque_face_list;

    for (x = 0; x < g_opaque_face_count; x++, opaque++)
    {
    }
    free(g_opaque_face_list);

    g_opaque_face_list = NULL;
    g_opaque_face_count = 0;
    g_max_opaque_face_count = 0;
}
static void		LoadOpaqueEntities()
{
	int modelnum, entnum;

	for (modelnum = 0; modelnum < g_nummodels; modelnum++) //Loop through brush models
    {
		char stringmodel[16];
		sprintf (stringmodel, "*%i", modelnum); //Model number to string

		for (entnum = 0; entnum < g_numentities; entnum++) //Loop through map ents
		{
			entity_t *ent = &g_entities[entnum]; //Get the current ent

			if (strcmp (ValueForKey (ent, "model"), stringmodel)) //Skip ents that don't match the current model
				continue;
			vec3_t origin;
			{
				GetVectorForKey (ent, "origin", origin); //Get origin vector of the ent

				if (*ValueForKey (ent, "light_origin") && *ValueForKey (ent, "model_center")) //If the entity has a light_origin and model_center, calculate a new origin
				{
					entity_t *ent2 = FindTargetEntity (ValueForKey (ent, "light_origin"));

					if (ent2)
					{
						vec3_t light_origin, model_center;
						GetVectorForKey (ent2, "origin", light_origin);
						GetVectorForKey (ent, "model_center", model_center);
						VectorSubtract (light_origin, model_center, origin); //New origin
					}
				}
			}
			bool opaque = false;
			{
				if ((IntForKey (ent, "zhlt_lightflags") & eModelLightmodeOpaque)) //If -noopaque is off, and if the entity has opaque light flag
					opaque = true;
			}
			vec3_t d_transparency;
			VectorFill (d_transparency, 0.0); //Initialize transparency value to 0
			bool b_transparency = false;
			{
				const char *s;

				if (*(s = ValueForKey(ent, "zhlt_customshadow"))) //If the entity has a custom shadow (transparency) value
				{
        			double r1 = 1.0, g1 = 1.0, b1 = 1.0, tmp = 1.0;

					if (sscanf(s, "%lf %lf %lf", &r1, &g1, &b1) == 3) //Try to read RGB values
					{
						if(r1<0.0) r1 = 0.0; //Clamp values to min 0
						if(g1<0.0) g1 = 0.0;
						if(b1<0.0) b1 = 0.0;
						d_transparency[0] = r1;
						d_transparency[1] = g1;
						d_transparency[2] = b1;
					}
					else if (sscanf(s, "%lf", &tmp) == 1) //Greyscale version
					{
						if(tmp<0.0) tmp = 0.0;
						VectorFill(d_transparency, tmp); //Set transparency values to the same greyscale value
					}
				}
				if (!VectorCompare (d_transparency, vec3_origin)) //If transparency values are not the default, set the transparency flag
					b_transparency = true;
			}
			int opaquestyle = -1;
			{
				int j;

				for (j = 0; j < g_numentities; j++) //Loop to find a matching light_shadow entity
				{
					entity_t *lightent = &g_entities[j];

					if (!strcmp (ValueForKey (lightent, "classname"), "light_shadow") //If light_shadow targeting the current entity
						&& *ValueForKey (lightent, "target")
						&& !strcmp (ValueForKey (lightent, "target"), ValueForKey (ent, "targetname")))
					{
						opaquestyle = IntForKey (lightent, "style"); //Get the style number and validate it

						if (opaquestyle < 0)
							opaquestyle = -opaquestyle;
						opaquestyle = (unsigned char)opaquestyle;

						if (opaquestyle >= ALLSTYLES)
						{
							Error ("invalid light style: style (%d) >= ALLSTYLES (%d)", opaquestyle, ALLSTYLES);
						}
						break;
					}
				}
			}
			bool block = false;
			{
					block = true;//opaque blocking is enabled
					if (IntForKey (ent, "zhlt_lightflags") & eModelLightmodeNonsolid) //If entity non-solid or has transparency or a specific style, which would prevent it from blocking
						block = false;
					if (b_transparency)
						block = false;
					if (opaquestyle != -1)
						block = false;
			}
			if (opaque) //If opaque add it to the opaque list with its properties
			{
				AddFaceToOpaqueList (entnum, modelnum, origin
					, d_transparency, b_transparency
					, opaquestyle
					, block
					);
			}
		}
	}
	{
		Log("%i opaque models\n", g_opaque_face_count);
		int i, facecount;

		for (facecount = 0, i = 0; i < g_opaque_face_count; i++)
		{
			facecount += CountOpaqueFaces (g_opaque_face_list[i].modelnum);
		}
		Log("%i opaque faces\n", facecount);
	}
}

// =====================================================================================
//  MakePatches
// =====================================================================================
static entity_t *FindTexlightEntity (int facenum)
{
	dface_t *face = &g_dfaces[facenum];
	const dplane_t *dplane = getPlaneFromFace (face);
	const char *texname = GetTextureByNumber (face->texinfo);
	entity_t *faceent = g_face_entity[facenum];
	vec3_t centroid;
	Winding *w = new Winding (*face);
	w->getCenter (centroid);
	delete w;
	VectorAdd (centroid, g_face_offset[facenum], centroid);

	entity_t *found = NULL;
	vec_t bestdist = -1;
	for (int i = 0; i < g_numentities; i++)
	{
		entity_t *ent = &g_entities[i];
		if (strcmp (ValueForKey (ent, "classname"), "light_surface"))
			continue;
		if (strcasecmp (ValueForKey (ent, "_tex"), texname))
			continue;
		vec3_t delta;
		GetVectorForKey (ent, "origin", delta);
		VectorSubtract (delta, centroid, delta);
		vec_t dist = VectorLength (delta);
		if (*ValueForKey (ent, "_frange"))
		{
			if (dist > FloatForKey (ent, "_frange"))
				continue;
		}
		if (*ValueForKey (ent, "_fdist"))
		{
			if (fabs (DotProduct (delta, dplane->normal)) > FloatForKey (ent, "_fdist"))
				continue;
		}
		if (*ValueForKey (ent, "_fclass"))
		{
			if (strcmp (ValueForKey (faceent, "classname"), ValueForKey (ent, "_fclass")))
				continue;
		}
		if (*ValueForKey (ent, "_fname"))
		{
			if (strcmp (ValueForKey (faceent, "targetname"), ValueForKey (ent, "_fname")))
				continue;
		}
		if (bestdist >= 0 && dist > bestdist)
			continue;
		found = ent;
		bestdist = dist;
	}
	return found;
}
static void     MakePatches()
{
    int             i;
    int             j;
    unsigned int    k;
    dface_t*        f;
    int             fn;
    Winding*        w;
    dmodel_t*       mod;
    vec3_t          origin;
    entity_t*       ent;
    const char*     s;
    vec3_t          light_origin;
    vec3_t          model_center;
    bool            b_light_origin;
    bool            b_model_center;
    eModelLightmodes lightmode;

    int				style; //LRC


    Log("%i faces\n", g_numfaces);

    Log("Create Patches : ");
	g_patches = (patch_t *)AllocBlock (MAX_PATCHES * sizeof (patch_t));

    for (i = 0; i < g_nummodels; i++)
    {
        b_light_origin = false;
        b_model_center = false;
        lightmode = eModelLightmodeNull;


        mod = g_dmodels + i;
        ent = EntityForModel(i);
        VectorCopy(vec3_origin, origin);

        if (*(s = ValueForKey(ent, "zhlt_lightflags")))
        {
            lightmode = (eModelLightmodes)atoi(s);
        }

        // models with origin brushes need to be offset into their in-use position
        if (*(s = ValueForKey(ent, "origin")))
        {
            double          v1, v2, v3;

            if (sscanf(s, "%lf %lf %lf", &v1, &v2, &v3) == 3)
            {
                origin[0] = v1;
                origin[1] = v2;
                origin[2] = v3;
            }

        }

        // Allow models to be lit in an alternate location (pt1)
        if (*(s = ValueForKey(ent, "light_origin")))
        {
            entity_t*       e = FindTargetEntity(s);

            if (e)
            {
                if (*(s = ValueForKey(e, "origin")))
                {
                    double          v1, v2, v3;

                    if (sscanf(s, "%lf %lf %lf", &v1, &v2, &v3) == 3)
                    {
                        light_origin[0] = v1;
                        light_origin[1] = v2;
                        light_origin[2] = v3;

                        b_light_origin = true;
                    }
                }
            }
        }

        // Allow models to be lit in an alternate location (pt2)
        if (*(s = ValueForKey(ent, "model_center")))
        {
            double          v1, v2, v3;

            if (sscanf(s, "%lf %lf %lf", &v1, &v2, &v3) == 3)
            {
                model_center[0] = v1;
                model_center[1] = v2;
                model_center[2] = v3;

                b_model_center = true;
            }
        }

        // Allow models to be lit in an alternate location (pt3)
        if (b_light_origin && b_model_center)
        {
            VectorSubtract(light_origin, model_center, origin);
        }

		//LRC:
		if (*(s = ValueForKey(ent, "style")))
		{
			style = atoi(s);
			if (style < 0)
				style = -style;
		}
		else
		{
			style = 0;
		}
        //LRC (ends)
		style = (unsigned char)style;
		if (style >= ALLSTYLES)
		{
			Error ("invalid light style: style (%d) >= ALLSTYLES (%d)", style, ALLSTYLES);
		}
		int bouncestyle = -1;
		{
			int j;
			for (j = 0; j < g_numentities; j++)
			{
				entity_t *lightent = &g_entities[j];
				if (!strcmp (ValueForKey (lightent, "classname"), "light_bounce")
					&& *ValueForKey (lightent, "target")
					&& !strcmp (ValueForKey (lightent, "target"), ValueForKey (ent, "targetname")))
				{
					bouncestyle = IntForKey (lightent, "style");
					if (bouncestyle < 0)
						bouncestyle = -bouncestyle;
					bouncestyle = (unsigned char)bouncestyle;
					if (bouncestyle >= ALLSTYLES)
					{
						Error ("invalid light style: style (%d) >= ALLSTYLES (%d)", bouncestyle, ALLSTYLES);
					}
					break;
				}
			}
		}

        for (j = 0; j < mod->numfaces; j++)
        {
            fn = mod->firstface + j;
            g_face_entity[fn] = ent;
            VectorCopy(origin, g_face_offset[fn]);
			g_face_texlights[fn] = FindTexlightEntity (fn);
            g_face_lightmode[fn] = lightmode;
            f = g_dfaces + fn;
            w = new Winding(*f);
            for (k = 0; k < w->m_NumPoints; k++)
            {
                VectorAdd(w->m_Points[k], origin, w->m_Points[k]);
            }
            MakePatchForFace(fn, w, style
				, bouncestyle
				); //LRC
        }
    }

    Log("%i base patches\n", g_num_patches);
    Log("%i square feet [%.2f square inches]\n", (int)(totalarea / 144), totalarea);
}

// =====================================================================================
//  patch_sorter
// =====================================================================================
static int CDECL patch_sorter(const void* p1, const void* p2)
{
    patch_t*        patch1 = (patch_t*)p1;
    patch_t*        patch2 = (patch_t*)p2;

    if (patch1->faceNumber < patch2->faceNumber)
    {
        return -1;
    }
    else if (patch1->faceNumber > patch2->faceNumber)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

// =====================================================================================
//  patch_sorter
//      This sorts the patches by facenumber, which makes their runs compress even better
// =====================================================================================
static void     SortPatches()
{
	// SortPatches is the ideal place to do this, because the address of the patches are going to be invalidated.
	patch_t *old_patches = g_patches;
	g_patches = (patch_t *)AllocBlock ((g_num_patches + 1) * sizeof (patch_t)); // allocate one extra slot considering how terribly the code were written
	memcpy (g_patches, old_patches, g_num_patches * sizeof (patch_t));
	FreeBlock (old_patches);
    qsort((void*)g_patches, (size_t) g_num_patches, sizeof(patch_t), patch_sorter);

    // Fixup g_face_patches & Fixup patch->next
    memset(g_face_patches, 0, sizeof(g_face_patches));
    {
        unsigned        x;
        patch_t*        patch = g_patches + 1;
        patch_t*        prev = g_patches;

		g_face_patches[prev->faceNumber] = prev;

        for (x = 1; x < g_num_patches; x++, patch++)
        {
            if (patch->faceNumber != prev->faceNumber)
            {
                prev->next = NULL;
                g_face_patches[patch->faceNumber] = patch;
            }
            else
            {
                prev->next = patch;
            }
            prev = patch;
        }
    }
	for (unsigned x = 0; x < g_num_patches; x++)
	{
		patch_t *patch = &g_patches[x];
		patch->leafnum = PointInLeaf (patch->origin) - g_dleafs;
	}
}

// =====================================================================================
//  FreePatches
// =====================================================================================
static void     FreePatches()
{
    unsigned        x;
    patch_t*        patch = g_patches;

    // AJM EX
    //Log("patches: %i of %i (%2.2lf percent)\n", g_num_patches, MAX_PATCHES, (double)((double)g_num_patches / (double)MAX_PATCHES));

    for (x = 0; x < g_num_patches; x++, patch++)
    {
        delete patch->winding;
    }
    memset(g_patches, 0, sizeof(patch_t) * g_num_patches);
	FreeBlock (g_patches);
	g_patches = NULL;
}

//=====================================================================

// =====================================================================================
//  WriteWorld
// =====================================================================================
static void     WriteWorld(const char* const name)
{
    unsigned        i;
    unsigned        j;
    FILE*           out;
    patch_t*        patch;
    Winding*        w;

    out = fopen(name, "w");

    if (!out)
        Error("Couldn't open %s", name);

    for (j = 0, patch = g_patches; j < g_num_patches; j++, patch++)
    {
        w = patch->winding;
        Log("%i\n", w->m_NumPoints);
        for (i = 0; i < w->m_NumPoints; i++)
        {
            Log("%5.2f %5.2f %5.2f %5.3f %5.3f %5.3f\n",
                w->m_Points[i][0],
                w->m_Points[i][1],
                w->m_Points[i][2], patch->totallight[0][0] / 256, patch->totallight[0][1] / 256, patch->totallight[0][2] / 256); //LRC
        }
        Log("\n");
    }

    fclose(out);
}

// =====================================================================================
//  CollectLight
// =====================================================================================
static void     CollectLight()
{
    unsigned        j; //LRC
    unsigned        i;
    patch_t*        patch;

    for (i = 0, patch = g_patches; i < g_num_patches; i++, patch++)
    {
		vec3_t newtotallight[MAXLIGHTMAPS];
		for (j = 0; j < MAXLIGHTMAPS && newstyles[i][j] != 255; j++)
		{
			VectorClear (newtotallight[j]);
			int k;
			for (k = 0; k < MAXLIGHTMAPS && patch->totalstyle[k] != 255; k++)
			{
				if (patch->totalstyle[k] == newstyles[i][j])
				{
					VectorCopy (patch->totallight[k], newtotallight[j]);
					break;
				}
			}
		}
		for (j = 0; j < MAXLIGHTMAPS; j++)
		{
			if (newstyles[i][j] != 255)
			{
				patch->totalstyle[j] = newstyles[i][j];
				VectorCopy (newtotallight[j], patch->totallight[j]);
				VectorCopy (addlight[i][j], emitlight[i][j]);
			}
			else
			{
				patch->totalstyle[j] = 255;
			}
		}
    }
}

// =====================================================================================
//  GatherLight
//      Get light from other g_patches
//      Run multi-threaded
// =====================================================================================
#ifdef SYSTEM_WIN32
#pragma warning(push)
#pragma warning(disable: 4100)                             // unreferenced formal parameter
#endif
static void     GatherLight(int threadnum)
{
    int             j;
    patch_t*        patch;

    unsigned        k,m; //LRC
//LRC    vec3_t          sum;

    unsigned        iIndex;
    transfer_data_t* tData;
    transfer_index_t* tIndex;
	float f;
	vec3_t			adds[ALLSTYLES];
	int				style;
	unsigned int	fastfind_index = 0;

    while (1)
    {
        j = GetThreadWork();
        if (j == -1)
        {
            break;
        }
		memset (adds, 0, ALLSTYLES * sizeof(vec3_t));

        patch = &g_patches[j];

        tData = patch->tData;
        tIndex = patch->tIndex;
        iIndex = patch->iIndex;

		for (m = 0; m < MAXLIGHTMAPS && patch->totalstyle[m] != 255; m++)
		{
			VectorAdd (adds[patch->totalstyle[m]], patch->totallight[m], adds[patch->totalstyle[m]]);
		}

        for (k = 0; k < iIndex; k++, tIndex++)
        {
            unsigned        l;
            unsigned        size = (tIndex->size + 1);
            unsigned        patchnum = tIndex->index;

            for (l = 0; l < size; l++, tData+=float_size[g_transfer_compress_type], patchnum++)
            {
                vec3_t          v;
                 //LRC:
				patch_t*		emitpatch = &g_patches[patchnum];
				unsigned		emitstyle;
				int				opaquestyle = -1;
				GetStyle (j, patchnum, opaquestyle, fastfind_index);
				float_decompress (g_transfer_compress_type, tData, &f);

				// for each style on the emitting patch
				for (emitstyle = 0; emitstyle < MAXLIGHTMAPS && emitpatch->directstyle[emitstyle] != 255; emitstyle++)
				{
					VectorScale(emitpatch->directlight[emitstyle], f, v);
					VectorMultiply(v, emitpatch->bouncereflectivity, v);
					if (isPointFinite (v))
					{
						int addstyle = emitpatch->directstyle[emitstyle];
						if (emitpatch->bouncestyle != -1)
						{
							if (addstyle == 0 || addstyle == emitpatch->bouncestyle)
								addstyle = emitpatch->bouncestyle;
							else
								continue;
						}
						if (opaquestyle != -1)
						{
							if (addstyle == 0 || addstyle == opaquestyle)
								addstyle = opaquestyle;
							else
								continue;
						}
						VectorAdd(adds[addstyle], v, adds[addstyle]);
					}
				}
				for (emitstyle = 0; emitstyle < MAXLIGHTMAPS && emitpatch->totalstyle[emitstyle] != 255; emitstyle++)
				{
					VectorScale(emitlight[patchnum][emitstyle], f, v);
					VectorMultiply(v, emitpatch->bouncereflectivity, v);
					if (isPointFinite(v))
					{
						int addstyle = emitpatch->totalstyle[emitstyle];
						if (emitpatch->bouncestyle != -1)
						{
							if (addstyle == 0 || addstyle == emitpatch->bouncestyle)
								addstyle = emitpatch->bouncestyle;
							else
								continue;
						}
						if (opaquestyle != -1)
						{
							if (addstyle == 0 || addstyle == opaquestyle)
								addstyle = opaquestyle;
							else
								continue;
						}
						VectorAdd(adds[addstyle], v, adds[addstyle]);
					}
				}
                //LRC (ends)
            }
        }

		vec_t maxlights[ALLSTYLES];
		for (style = 0; style < ALLSTYLES; style++)
		{
			maxlights[style] = VectorMaximum (adds[style]);
		}
		for (m = 0; m < MAXLIGHTMAPS; m++)
		{
			unsigned char beststyle = 255;
			if (m == 0)
			{
				beststyle = 0;
			}
			else
			{
				vec_t bestmaxlight = 0;
				for (style = 1; style < ALLSTYLES; style++)
				{
					if (maxlights[style] > bestmaxlight + NORMAL_EPSILON)
					{
						bestmaxlight = maxlights[style];
						beststyle = style;
					}
				}
			}
			if (beststyle != 255)
			{
				maxlights[beststyle] = 0;
				newstyles[j][m] = beststyle;
				VectorCopy (adds[beststyle], addlight[j][m]);
			}
			else
			{
				newstyles[j][m] = 255;
			}
		}
		for (style = 1; style < ALLSTYLES; style++)
		{
			if (maxlights[style] > g_maxdiscardedlight + NORMAL_EPSILON)
			{
				ThreadLock ();
				if (maxlights[style] > g_maxdiscardedlight + NORMAL_EPSILON)
				{
					g_maxdiscardedlight = maxlights[style];
					VectorCopy (patch->origin, g_maxdiscardedpos);
				}
				ThreadUnlock ();
			}
		}
    }
}

// RGB Transfer version
static void     GatherRGBLight(int threadnum)
{
    int             j;
    patch_t*        patch;

    unsigned        k,m; //LRC
//LRC    vec3_t          sum;

    unsigned        iIndex;
    rgb_transfer_data_t* tRGBData;
    transfer_index_t* tIndex;
	float f[3];
	vec3_t			adds[ALLSTYLES];
	int				style;
	unsigned int	fastfind_index = 0;

    while (1)
    {
        j = GetThreadWork();
        if (j == -1)
        {
            break;
        }
		memset (adds, 0, ALLSTYLES * sizeof(vec3_t));

        patch = &g_patches[j];

        tRGBData = patch->tRGBData;
        tIndex = patch->tIndex;
        iIndex = patch->iIndex;

		for (m = 0; m < MAXLIGHTMAPS && patch->totalstyle[m] != 255; m++)
		{
			VectorAdd (adds[patch->totalstyle[m]], patch->totallight[m], adds[patch->totalstyle[m]]);
		}

        for (k = 0; k < iIndex; k++, tIndex++)
        {
            unsigned        l;
            unsigned        size = (tIndex->size + 1);
            unsigned        patchnum = tIndex->index;
            for (l = 0; l < size; l++, tRGBData+=vector_size[g_rgbtransfer_compress_type], patchnum++)
            {
                vec3_t          v;
                 //LRC:
				patch_t*		emitpatch = &g_patches[patchnum];
				unsigned		emitstyle;
				int				opaquestyle = -1;
				GetStyle (j, patchnum, opaquestyle, fastfind_index);
				vector_decompress (g_rgbtransfer_compress_type, tRGBData, &f[0], &f[1], &f[2]);

				// for each style on the emitting patch
				for (emitstyle = 0; emitstyle < MAXLIGHTMAPS && emitpatch->directstyle[emitstyle] != 255; emitstyle++)
				{
					VectorMultiply(emitpatch->directlight[emitstyle], f, v);
					VectorMultiply(v, emitpatch->bouncereflectivity, v);
					if (isPointFinite (v))
					{
						int addstyle = emitpatch->directstyle[emitstyle];
						if (emitpatch->bouncestyle != -1)
						{
							if (addstyle == 0 || addstyle == emitpatch->bouncestyle)
								addstyle = emitpatch->bouncestyle;
							else
								continue;
						}
						if (opaquestyle != -1)
						{
							if (addstyle == 0 || addstyle == opaquestyle)
								addstyle = opaquestyle;
							else
								continue;
						}
						VectorAdd(adds[addstyle], v, adds[addstyle]);
					}
				}
				for (emitstyle = 0; emitstyle < MAXLIGHTMAPS && emitpatch->totalstyle[emitstyle] != 255; emitstyle++)
				{
					VectorMultiply(emitlight[patchnum][emitstyle], f, v);
					VectorMultiply(v, emitpatch->bouncereflectivity, v);
					if (isPointFinite(v))
					{
						int addstyle = emitpatch->totalstyle[emitstyle];
						if (emitpatch->bouncestyle != -1)
						{
							if (addstyle == 0 || addstyle == emitpatch->bouncestyle)
								addstyle = emitpatch->bouncestyle;
							else
								continue;
						}
						if (opaquestyle != -1)
						{
							if (addstyle == 0 || addstyle == opaquestyle)
								addstyle = opaquestyle;
							else
								continue;
						}
						VectorAdd(adds[addstyle], v, adds[addstyle]);
					}
				}
                //LRC (ends)
            }
        }

		vec_t maxlights[ALLSTYLES];
		for (style = 0; style < ALLSTYLES; style++)
		{
			maxlights[style] = VectorMaximum (adds[style]);
		}
		for (m = 0; m < MAXLIGHTMAPS; m++)
		{
			unsigned char beststyle = 255;
			if (m == 0)
			{
				beststyle = 0;
			}
			else
			{
				vec_t bestmaxlight = 0;
				for (style = 1; style < ALLSTYLES; style++)
				{
					if (maxlights[style] > bestmaxlight + NORMAL_EPSILON)
					{
						bestmaxlight = maxlights[style];
						beststyle = style;
					}
				}
			}
			if (beststyle != 255)
			{
				maxlights[beststyle] = 0;
				newstyles[j][m] = beststyle;
				VectorCopy (adds[beststyle], addlight[j][m]);
			}
			else
			{
				newstyles[j][m] = 255;
			}
		}
		for (style = 1; style < ALLSTYLES; style++)
		{
			if (maxlights[style] > g_maxdiscardedlight + NORMAL_EPSILON)
			{
				ThreadLock ();
				if (maxlights[style] > g_maxdiscardedlight + NORMAL_EPSILON)
				{
					g_maxdiscardedlight = maxlights[style];
					VectorCopy (patch->origin, g_maxdiscardedpos);
				}
				ThreadUnlock ();
			}
		}
    }
}

#ifdef SYSTEM_WIN32
#pragma warning(pop)
#endif

// =====================================================================================
//  BounceLight
// =====================================================================================
static void     BounceLight()
{
    unsigned        i;
    char            name[64];

    unsigned        j; //LRC

    for (i = 0; i < g_num_patches; i++)
    {
		patch_t *patch = &g_patches[i];
		for (j = 0; j < MAXLIGHTMAPS && patch->totalstyle[j] != 255; j++)
		{
			VectorCopy (patch->totallight[j], emitlight[i][j]);
		}
    }

    for (i = 0; i < g_numbounce; i++)
    {
        Log("Bounce %u ", i + 1);
		{NamedRunThreadsOn(g_num_patches, g_estimate, GatherLight);}
        CollectLight();
    }
	for (i = 0; i < g_num_patches; i++)
	{
		patch_t *patch = &g_patches[i];
		for (j = 0; j < MAXLIGHTMAPS && patch->totalstyle[j] != 255; j++)
		{
			VectorCopy (emitlight[i][j], patch->totallight[j]);
		}
	}
}

// =====================================================================================
//  CheckMaxPatches
// =====================================================================================
static void     CheckMaxPatches()
{
	hlassume(g_num_patches < MAX_SPARSE_VISMATRIX_PATCHES, assume_MAX_PATCHES);
}

// =====================================================================================
//  MakeScalesStub
// =====================================================================================
static void     MakeScalesStub()
{
	MakeScalesSparseVismatrix(); // g_method = DEFAULT_METHOD = eMethodSparseVismatrix
}

// =====================================================================================
//  FreeTransfers
// =====================================================================================
static void     FreeTransfers()
{
    unsigned        x;
    patch_t*        patch = g_patches;

    for (x = 0; x < g_num_patches; x++, patch++)
    {
        if (patch->tData)
        {
            FreeBlock(patch->tData);
            patch->tData = NULL;
        }
        if (patch->tRGBData)
        {
            FreeBlock(patch->tRGBData);
            patch->tRGBData = NULL;
        }
        if (patch->tIndex)
        {
            FreeBlock(patch->tIndex);
            patch->tIndex = NULL;
        }
    }
}

static void ExtendLightmapBuffer ()
{
	int maxsize;
	int i;
	int j;
	int ofs;
	dface_t *f;

	maxsize = 0;
	for (i = 0; i < g_numfaces; i++)
	{
		f = &g_dfaces[i];
		if (f->lightofs >= 0)
		{
			ofs = f->lightofs;
			for (j = 0; j < MAXLIGHTMAPS && f->styles[j] != 255; j++)
			{
				ofs += (MAX_SURFACE_EXTENT + 1) * (MAX_SURFACE_EXTENT + 1) * 3;
			}
			if (ofs > maxsize)
			{
				maxsize = ofs;
			}
		}
	}
	if (maxsize >= g_lightdatasize)
	{
		hlassume (maxsize <= g_max_map_lightdata, assume_MAX_MAP_LIGHTING);
		memset (&g_dlightdata[g_lightdatasize], 0, maxsize - g_lightdatasize);
		g_lightdatasize = maxsize;
	}
}

// =====================================================================================
//  RadWorld
// =====================================================================================
static void     RadWorld()
{

    MakeBackplanes();
    MakeParents(0, -1);
    MakeTnodes(&g_dmodels[0]);
	CreateOpaqueNodes();
	LoadOpaqueEntities();

    // turn each face into a single patch
    MakePatches();
    CheckMaxPatches();                                     // Check here for exceeding max patches, to prevent a lot of work from occuring before an error occurs
    SortPatches();                                         // Makes the runs in the Transfer Compression really good
    PairEdges();

	BuildDiffuseNormals ();
    // create directlights out of g_patches and lights
    CreateDirectLights();
	LoadStudioModels(); //seedee
    Log("\n");
	
	// generate a position map for each face
	NamedRunThreadsOnIndividual(g_numfaces, g_estimate, FindFacePositions);

    // build initial facelights
    NamedRunThreadsOnIndividual(g_numfaces, g_estimate, BuildFacelights);

	FreePositionMaps ();

    // free up the direct lights now that we have facelights
    DeleteDirectLights();

    if (g_numbounce > 0)
    {
        // build transfer lists
        MakeScalesStub();

		// these arrays are only used in CollectLight, GatherLight and BounceLight
		emitlight = (vec3_t (*)[MAXLIGHTMAPS])AllocBlock ((g_num_patches + 1) * sizeof (vec3_t [MAXLIGHTMAPS]));
		addlight = (vec3_t (*)[MAXLIGHTMAPS])AllocBlock ((g_num_patches + 1) * sizeof (vec3_t [MAXLIGHTMAPS]));
		newstyles = (unsigned char (*)[MAXLIGHTMAPS])AllocBlock ((g_num_patches + 1) * sizeof (unsigned char [MAXLIGHTMAPS]));
        // spread light around
        BounceLight();

		FreeBlock (emitlight);
		emitlight = NULL;
		FreeBlock (addlight);
		addlight = NULL;
		FreeBlock (newstyles);
		newstyles = NULL;
    }

    FreeTransfers();
	FreeStyleArrays ();
	
	NamedRunThreadsOnIndividual (g_numfaces, g_estimate, CreateTriangulations);

    // blend bounced light into direct light and save
    PrecompLightmapOffsets();


	ScaleDirectLights ();

	{

		CreateFacelightDependencyList ();

		NamedRunThreadsOnIndividual (g_numfaces, g_estimate, AddPatchLights);

		FreeFacelightDependencyList ();
	}

	FreeTriangulations ();

    NamedRunThreadsOnIndividual(g_numfaces, g_estimate, FinalLightFace);
	MdlLightHack ();
	ReduceLightmap();
	if (g_lightdatasize == 0)
	{
		g_lightdatasize = 1;
		g_dlightdata[0] = 0;
	}
	ExtendLightmapBuffer (); // expand the size of lightdata array (for a few KB) to ensure that game engine reads within its valid range
}


// =====================================================================================
//  Settings
// =====================================================================================
static void     Settings()
{
    char*           tmp;
    char            buf1[1024];
    char            buf2[1024];

    Log("\n-= Current %s Settings =-\n", g_Program);
    Log("Name                | Setting             | Default\n"
        "--------------------|---------------------|-------------------------\n");

    // ZHLT Common Settings
    if (DEFAULT_NUMTHREADS == -1)
    {
        Log("threads              [ %17d ] [            Varies ]\n", g_numthreads);
    }
    else
    {
        Log("threads              [ %17d ] [ %17d ]\n", g_numthreads, DEFAULT_NUMTHREADS);
    }

    Log("max texture memory   [ %17d ] [ %17d ]\n", g_max_map_miptex, DEFAULT_MAX_MAP_MIPTEX);
		Log("max lighting memory  [ %17d ] [ %17d ]\n", g_max_map_lightdata, DEFAULT_MAX_MAP_LIGHTDATA); //lightdata

    switch (g_threadpriority)
    {
    case eThreadPriorityNormal:
    default:
        tmp = "Normal";
        break;
    case eThreadPriorityLow:
        tmp = "Low";
        break;
    case eThreadPriorityHigh:
        tmp = "High";
        break;
    }
    Log("priority             [ %17s ] [ %17s ]\n", tmp, "Normal");
    Log("\n");
	Log("pre-25th anniversary [ %17s ] [ %17s ]\n", g_pre25update ? "on" : "off", DEFAULT_PRE25UPDATE ? "on" : "off");
    Log("oversampling (-extra)[ %17s ] [ %17s ]\n", g_extra ? "on" : "off", DEFAULT_EXTRA ? "on" : "off");
    Log("bounces              [ %17d ] [ %17d ]\n", g_numbounce, DEFAULT_BOUNCE);
    safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_limitthreshold);
    safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_LIMITTHRESHOLD);
    Log("light limit threshold[ %17s ] [ %17s ]\n", g_limitthreshold >= 0 ? buf1 : "None", buf2);
    Log("smoothing threshold 2[ %17s ] [ %17s ]\n", buf1, buf2);
    Log("\n");
    safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_chop);
    safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_CHOP);
    Log("chop value           [ %17s ] [ %17s ]\n", buf1, buf2);
    safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_texchop);
    safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_TEXCHOP);
    Log("texchop value        [ %17s ] [ %17s ]\n", buf1, buf2);
    Log("\n");

    safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_fade);
    safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_FADE);
    Log("global fade          [ %17s ] [ %17s ]\n", buf1, buf2);
    safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_texlightgap);
    safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_TEXLIGHTGAP);
    Log("global texlight gap  [ %17s ] [ %17s ]\n", buf1, buf2);
    
    // ------------------------------------------------------------------------
    // Changes by Adam Foster - afoster@compsoc.man.ac.uk
    // replaces the old stuff for displaying current values for gamma and lightscale
    // ------------------------------------------------------------------------

    safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_lightscale);
    safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_LIGHTSCALE);
    Log("global light scale   [ %17s ] [ %17s ]\n", buf1, buf2);
	safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_translucentdepth);
	safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_TRANSLUCENTDEPTH);
	Log("translucent depth    [ %17s ] [ %17s ]\n", buf1, buf2);
	Log("no emitter range     [ %17s ] [ %17s ]\n", g_noemitterrange ? "on" : "off", DEFAULT_NOEMITTERRANGE ? "on" : "off");
	Log("wall bleeding fix    [ %17s ] [ %17s ]\n", g_bleedfix ? "on" : "off", DEFAULT_BLEEDFIX ? "on" : "off");

    Log("\n\n");
}

// AJM: added in
// add minlights entity //seedee
// =====================================================================================
//  ReadInfoTexAndMinlights
//      try and parse texlight info from the info_texlights entity 
// =====================================================================================
void            ReadInfoTexAndMinlights()
{
    int         k;
    int         values;
    float       r, g, b, i, min;
    entity_t*   mapent;
    epair_t*    ep;
    texlight_t  texlight;
	minlight_t minlight;

    for (k = 0; k < g_numentities; k++)
    {
        mapent = &g_entities[k];
		bool foundMinlights = false;
		bool foundTexlights = false;

		if (!strcmp(ValueForKey(mapent, "classname"), "info_minlights")) {
			Log("Reading per-tex minlights from info_minlights map entity\n");

			for (ep = mapent->epairs; ep; ep = ep->next)
			{
				if (!strcmp(ep->key, "classname")
					|| !strcmp(ep->key, "origin")
					)
					continue; // we dont care about these keyvalues
				if (sscanf(ep->value, "%f", &min) != 1)
				{
					Warning("Ignoring bad minlight '%s' in info_minlights entity", ep->key);
					continue;
				}
				minlight.name = ep->key;
				minlight.value = min;
				s_minlights.push_back(minlight);
			}
			
		}
		else if (!strcmp(ValueForKey(mapent, "classname"), "info_texlights")) {
			Log("Reading texlights from info_texlights map entity\n");

			for (ep = mapent->epairs; ep; ep = ep->next)
			{
				if (!strcmp(ep->key, "classname")
					|| !strcmp(ep->key, "origin")
					)
					continue; // we dont care about these keyvalues

				values = sscanf(ep->value, "%f %f %f %f", &r, &g, &b, &i);

				if (values == 1)
				{
					g = b = r;
				}
				else if (values == 4) // use brightness value.
				{
					r *= i / 255.0;
					g *= i / 255.0;
					b *= i / 255.0;
				}
				else if (values != 3)
				{
					Warning("Ignoring bad texlight '%s' in info_texlights entity", ep->key);
					continue;
				}

				texlight.name = ep->key;
				texlight.value[0] = r;
				texlight.value[1] = g;
				texlight.value[2] = b;
				texlight.filename = "info_texlights";
				s_texlights.push_back(texlight);
			}
			foundTexlights = true;
		}
		if (foundMinlights && foundTexlights)
		{
			break;
		}
    }
}

// =====================================================================================
//  main
// =====================================================================================
int             main(const int argc, char** argv)
{
    int             i;
    double          start, end;
    const char*     mapname_from_arg = NULL;
	char temp[_MAX_PATH]; //seedee

    g_Program = "sdHLRAD";

	int argcold = argc;
	char ** argvold = argv;
	{
		int argc;
		char ** argv;
		ParseParamFile (argcold, argvold, argc, argv);
		{
	if (InitConsole (argc, argv) < 0)
		Usage(PROGRAM_RAD);
    if (argc == 1)
        Usage(PROGRAM_RAD);

    for (i = 1; i < argc; i++)
    {
		if (!strcasecmp(argv[i], "-console"))
		{
#ifndef SYSTEM_WIN32
			Warning("The option '-console #' is only valid for Windows.");
#endif
			if (i + 1 < argc)
				++i;
			else
				Usage(PROGRAM_RAD);
		}
		else if (!strcasecmp(argv[i], "-extra"))
        {
            g_extra = true;

			if (g_numbounce < 12)
			{
				g_numbounce = 12;
			}
        }
        else if (!strcasecmp(argv[i], "-bounce"))
        {
            if (i + 1 < argc)	//added "1" .--vluzacn
            {
                g_numbounce = atoi(argv[++i]);

                if (g_numbounce > 1000)
                {
                    Log("Unexpectedly large value (>1000) for '-bounce'\n");
                    Usage(PROGRAM_RAD);
                }
            }
            else
            {
                Usage(PROGRAM_RAD);
            }
        }
        else if (!strcasecmp(argv[i], "-threads"))
        {
            if (i + 1 < argc)	//added "1" .--vluzacn
            {
                g_numthreads = atoi(argv[++i]);
                if (g_numthreads < 1)
                {
                    Log("Expected value of at least 1 for '-threads'\n");
                    Usage(PROGRAM_RAD);
                }
            }
            else
            {
                Usage(PROGRAM_RAD);
            }
        }
        else if (!strcasecmp(argv[i], "-chop"))
        {
            if (i + 1 < argc)	//added "1" .--vluzacn
            {
                g_chop = atof(argv[++i]);
                if (g_chop < 1)
                {
                    Log("expected value greater than 1 for '-chop'\n");
                    Usage(PROGRAM_RAD);
                }
                if (g_chop < 32)
                {
                    Log("Warning: Chop values below 32 are not recommended.");
                }
            }
            else
            {
                Usage(PROGRAM_RAD);
            }
        }
        else if (!strcasecmp(argv[i], "-texchop"))
        {
            if (i + 1 < argc)	//added "1" .--vluzacn
            {
                g_texchop = atof(argv[++i]);
                if (g_texchop < 1)
                {
                    Log("expected value greater than 1 for '-texchop'\n");
                    Usage(PROGRAM_RAD);
                }
                if (g_texchop < 32)
                {
                    Log("Warning: texchop values below 16 are not recommended.");
                }
            }
            else
            {
                Usage(PROGRAM_RAD);
            }
        }
        else if (!strcasecmp(argv[i], "-fade"))
        {
            if (i + 1 < argc)	//added "1" .--vluzacn
            {
                g_fade = (float)atof(argv[++i]);
                if (g_fade < 0.0)
                {
                    Log("-fade must be a positive number\n");
                    Usage(PROGRAM_RAD);
                }
            }
            else
            {
                Usage(PROGRAM_RAD);
            }
        }
        else if (!strcasecmp(argv[i], "-limiter"))
        {
            if (i + 1 < argc)	//"1" was added to check if there is another argument afterwards (expected value) //seedee
            {
                g_limitthreshold = atof(argv[++i]);
            }
            else
            {
                Usage(PROGRAM_RAD);
            }
        }
        else if (!strcasecmp(argv[i], "-low"))
        {
            g_threadpriority = eThreadPriorityLow;
        }
        else if (!strcasecmp(argv[i], "-high"))
        {
            g_threadpriority = eThreadPriorityHigh;
        }
        else if (!strcasecmp(argv[i], "-texdata"))
        {
            if (i + 1 < argc)	//added "1" .--vluzacn
            {
                int             x = atoi(argv[++i]) * 1024;

                //if (x > g_max_map_miptex) //--vluzacn
                {
                    g_max_map_miptex = x;
                }
            }
            else
            {
                Usage(PROGRAM_RAD);
            }
        }
        else if (!strcasecmp(argv[i], "-lightdata")) //lightdata
        {
            if (i + 1 < argc) //--vluzacn
            {
                int             x = atoi(argv[++i]) * 1024;

                //if (x > g_max_map_lightdata) //--vluzacn
                {
                    g_max_map_lightdata = x;
                }
            }
            else
            {
                Usage(PROGRAM_RAD);
            }
        }
		else if (!strcasecmp (argv[i], "-noemitterrange"))
		{
			g_noemitterrange = true;
		}
		else if (!strcasecmp (argv[i], "-nobleedfix"))
		{
			g_bleedfix = false;
		}
		else if (!strcasecmp (argv[i], "-texlightgap"))
		{
			if (i + 1 < argc)
			{
				g_texlightgap = atof (argv[++i]);
			}
			else
			{
				Usage(PROGRAM_RAD);
			}
		}
		else if (!strcasecmp(argv[i], "-pre25")) //Pre25 should be after everything else to override
		{
			g_pre25update = true;
            g_limitthreshold = 188.0;
		}
        else if (argv[i][0] == '-')
        {
            Log("Unknown option \"%s\"\n", argv[i]);
            Usage(PROGRAM_RAD);
        }
        else if (!mapname_from_arg)
        {
            mapname_from_arg = argv[i];
        }
        else
        {
            Log("Unknown option \"%s\"\n", argv[i]);
            Usage(PROGRAM_RAD);
        }
    }

    if (!mapname_from_arg)
    {
        Log("No mapname specified\n");
        Usage(PROGRAM_RAD);
    }

    g_smoothing_threshold = (float)cos(50.0 * (Q_PI / 180.0)); // 50.0 = DEFAULT_SMOOTHING_VALUE = g_smoothing_value =  a.k.a '-smooth'

    safe_strncpy(g_Mapname, mapname_from_arg, _MAX_PATH);
    FlipSlashes(g_Mapname);
	ExtractFilePath(g_Mapname, temp);	// skip mapname
	ExtractFilePath(temp, g_Wadpath);
    StripExtension(g_Mapname);
    atexit(CloseLog);
    ThreadSetDefault();
    ThreadSetPriority(g_threadpriority);
    LogStart(argcold, argvold);
	LogArguments(argc, argv);
    CheckForErrorLog();

	compress_compatability_test ();
#ifdef PLATFORM_CAN_CALC_EXTENT
	hlassume (CalcFaceExtents_test (), assume_first);
#endif
    dtexdata_init();
    atexit(dtexdata_free);
    // END INIT

    // BEGIN RAD
    start = I_FloatTime();

    // normalise maxlight

	safe_snprintf(g_source, _MAX_PATH, "%s.bsp", g_Mapname);
    LoadBSPFile(g_source);
#ifndef PLATFORM_CAN_CALC_EXTENT
	char extentfilename[_MAX_PATH];
	safe_snprintf (extentfilename, _MAX_PATH, "%s.ext", g_Mapname);
	Log ("Loading extent file '%s'\n", extentfilename);
	if (!q_exists (extentfilename))
	{
		hlassume (false, assume_NO_EXTENT_FILE);
	}
	LoadExtentFile (extentfilename);
#endif
    ParseEntities();
    Settings();
	DeleteEmbeddedLightmaps ();
	LoadTextures ();
	ReadCustomChopValue ();
	ReadCustomSmoothValue ();
	ReadTranslucentTextures ();
	ReadLightingCone ();
	
	g_smoothing_threshold_2 = 1.0; // cos(0 * (Q_PI / 180.0)), 0 = DEFAULT_SMOOTHING2_VALUE = g_smoothing_value_2 a.k.a '-smooth2'
	{
		int style;
		for (style = 0; style < ALLSTYLES; ++style)
		if (style) {
			g_corings[style] = 0.01; // 0.01 = DEFAULT_CORING = g_coring a.k.a -coring
		} else {
			g_corings[style] = 0;
		}
	}
    if (!g_visdatasize)
    {
		Warning("No vis information.");
    }
    RadWorld();
	FreeStudioModels(); //seedee
    FreeOpaqueFaceList();
    FreePatches();
	DeleteOpaqueNodes ();

	EmbedLightmapInTextures ();

	PrintBSPFileSizes();
    WriteBSPFile(g_source);

    end = I_FloatTime();
    LogTimeElapsed(end - start);
    // END RAD

		}
	}
    return 0;
}
