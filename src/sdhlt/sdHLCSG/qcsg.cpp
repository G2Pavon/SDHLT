#include "csg.h"
#include "arguments.h"

#ifdef SYSTEM_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h> //--vluzacn
#endif

static FILE *out[NUM_HULLS]; // pointer to each of the hull out files (.p0, .p1, ect.)
static FILE *out_detailbrush[NUM_HULLS];
static int c_outfaces;
static int c_csgfaces;
BoundingBox world_bounds;

bool g_skyclip = DEFAULT_SKYCLIP;   // no sky clipping "-noskyclip"
bool g_estimate = DEFAULT_ESTIMATE; // progress estimates "-estimate"

cliptype g_cliptype = DEFAULT_CLIPTYPE; // "-cliptype <value>"

bool g_bClipNazi = DEFAULT_CLIPNAZI; // "-noclipeconomy"

bface_t *NewFaceFromFace(const bface_t *const in) // Duplicates the non point information of a face, used by SplitFace
{
    bface_t *newf;

    newf = (bface_t *)Alloc(sizeof(bface_t));

    newf->contents = in->contents;
    newf->texinfo = in->texinfo;
    newf->planenum = in->planenum;
    newf->plane = in->plane;
    newf->backcontents = in->backcontents;

    return newf;
}

void FreeFace(bface_t *f)
{
    delete f->w;
    Free(f);
}

void WriteFace(const int hull, const bface_t *const f, int detaillevel)
{
    unsigned int i;
    Winding *w;

    ThreadLock();
    if (!hull)
        c_csgfaces++;

    w = f->w; // .p0 format

    fprintf(out[hull], "%i %i %i %i %u\n", detaillevel, f->planenum, f->texinfo, f->contents, w->m_NumPoints); // plane summary

    for (i = 0; i < w->m_NumPoints; i++) // for each of the points on the face
    {
        fprintf(out[hull], "%5.8f %5.8f %5.8f\n", w->m_Points[i][0], w->m_Points[i][1], w->m_Points[i][2]); // write the co-ords
    }
    fprintf(out[hull], "\n");

    ThreadUnlock();
}

void WriteDetailBrush(int hull, const bface_t *faces)
{
    ThreadLock();
    fprintf(out_detailbrush[hull], "0\n");
    for (const bface_t *f = faces; f; f = f->next)
    {
        Winding *w = f->w;
        fprintf(out_detailbrush[hull], "%i %u\n", f->planenum, w->m_NumPoints);
        for (int i = 0; i < w->m_NumPoints; i++)
        {
            fprintf(out_detailbrush[hull], "%5.8f %5.8f %5.8f\n", w->m_Points[i][0], w->m_Points[i][1], w->m_Points[i][2]);
        }
    }
    fprintf(out_detailbrush[hull], "-1 -1\n");
    ThreadUnlock();
}

static void SaveOutside(const brush_t *const b, const int hull, bface_t *outside, const int mirrorcontents) // The faces remaining on the outside list are final polygons.  Write them to the output file.
{                                                                                                           // Passable contents (water, lava, etc) will generate a mirrored copy of the face to be seen from the inside.
    bface_t *f;
    bface_t *f2;
    bface_t *next;
    int i;
    vec3_t temp;

    for (f = outside; f; f = next)
    {
        next = f->next;

        int frontcontents, backcontents;
        int texinfo = f->texinfo;
        const char *texname = GetTextureByNumber_CSG(texinfo);
        frontcontents = f->contents;
        if (mirrorcontents == CONTENTS_TOEMPTY)
        {
            backcontents = f->backcontents;
        }
        else
        {
            backcontents = mirrorcontents;
        }
        if (frontcontents == CONTENTS_TOEMPTY)
        {
            frontcontents = CONTENTS_EMPTY;
        }
        if (backcontents == CONTENTS_TOEMPTY)
        {
            backcontents = CONTENTS_EMPTY;
        }

        bool frontnull, backnull;
        frontnull = false;
        backnull = false;
        if (mirrorcontents == CONTENTS_TOEMPTY)
        {
            if (strncasecmp(texname, "SKIP", 4) && strncasecmp(texname, "HINT", 4) && strncasecmp(texname, "SOLIDHINT", 9) && strncasecmp(texname, "BEVELHINT", 9))
            {
                backnull = true; // SKIP and HINT are special textures for hlbsp
            }
        }
        if (!strncasecmp(texname, "SOLIDHINT", 9) || !strncasecmp(texname, "BEVELHINT", 9))
        {
            if (frontcontents != backcontents)
            {
                frontnull = backnull = true; // not discardable, so remove "SOLIDHINT" texture name and behave like NULL
            }
        }
        if (b->entitynum != 0 && !strncasecmp(texname, "!", 1))
        {
            backnull = true; // strip water face on one side
        }

        f->contents = frontcontents;
        f->texinfo = frontnull ? -1 : texinfo;
        if (!hull) // count unique faces
        {
            for (f2 = b->hulls[hull].faces; f2; f2 = f2->next)
            {
                if (f2->planenum == f->planenum)
                {
                    if (!f2->used)
                    {
                        f2->used = true;
                        c_outfaces++;
                    }
                    break;
                }
            }
        }

        if (!hull) // check the texture alignment of this face
        {
            int texinfo = f->texinfo;
            const char *texname = GetTextureByNumber_CSG(texinfo);
            texinfo_t *tex = &g_texinfo[texinfo];

            if (texinfo != -1                                                         // nullified textures (NULL, BEVEL, aaatrigger, etc.)
                && !(tex->flags & TEX_SPECIAL)                                        // sky
                && strncasecmp(texname, "SKIP", 4) && strncasecmp(texname, "HINT", 4) // HINT and SKIP will be nullified only after hlbsp
                && strncasecmp(texname, "SOLIDHINT", 9) && strncasecmp(texname, "BEVELHINT", 9))
            {
                vec3_t texnormal; // check for "Malformed face (%d) normal"
                CrossProduct(tex->vecs[1], tex->vecs[0], texnormal);
                VectorNormalize(texnormal);
                if (fabs(DotProduct(texnormal, f->plane->normal)) <= NORMAL_EPSILON)
                {
                    Warning("Entity %i, Brush %i: Malformed texture alignment (texture %s): Texture axis perpendicular to face.",
                            b->originalentitynum, b->originalbrushnum,
                            texname);
                }

                bool bad; // check for "Bad surface extents"
                int i;
                int j;
                vec_t val;

                bad = false;
                for (i = 0; i < f->w->m_NumPoints; i++)
                {
                    for (j = 0; j < 2; j++)
                    {
                        val = DotProduct(f->w->m_Points[i], tex->vecs[j]) + tex->vecs[j][3];
                        if (val < -99999 || val > 999999)
                        {
                            bad = true;
                        }
                    }
                }
                if (bad)
                {
                    Warning("Entity %i, Brush %i: Malformed texture alignment (texture %s): Bad surface extents.", b->originalentitynum, b->originalbrushnum, texname);
                }
            }
        }

        WriteFace(hull, f, (hull ? b->clipnodedetaillevel : b->detaillevel));

        { // if (mirrorcontents != CONTENTS_SOLID)
            f->planenum ^= 1;
            f->plane = &g_mapplanes[f->planenum];
            f->contents = backcontents;
            f->texinfo = backnull ? -1 : texinfo;

            for (i = 0; i < f->w->m_NumPoints / 2; i++) // swap point orders and add points backwards
            {
                VectorCopy(f->w->m_Points[i], temp);
                VectorCopy(f->w->m_Points[f->w->m_NumPoints - 1 - i], f->w->m_Points[i]);
                VectorCopy(temp, f->w->m_Points[f->w->m_NumPoints - 1 - i]);
            }
            WriteFace(hull, f, (hull ? b->clipnodedetaillevel : b->detaillevel));
        }

        FreeFace(f);
    }
}

bface_t *CopyFace(const bface_t *const f)
{
    bface_t *n;

    n = NewFaceFromFace(f);
    n->w = f->w->Copy();
    n->bounds = f->bounds;
    return n;
}

bface_t *CopyFaceList(bface_t *f)
{
    bface_t *head;
    bface_t *n;

    if (f)
    {
        head = CopyFace(f);
        n = head;
        f = f->next;

        while (f)
        {
            n->next = CopyFace(f);
            n = n->next;
            f = f->next;
        }

        return head;
    }
    else
    {
        return NULL;
    }
}

void FreeFaceList(bface_t *f)
{
    if (f)
    {
        if (f->next)
        {
            FreeFaceList(f->next);
        }
        FreeFace(f);
    }
}

static bface_t *CopyFacesToOutside(brushhull_t *bh) // Make a copy of all the faces of the brush, so they can be chewed up by other brushes.
{                                                   // All of the faces start on the outside list. As other brushes take bites out of the faces, the fragments are moved to the  inside list, so they can be freed when they are determined to be completely enclosed in solid.
    bface_t *f;
    bface_t *newf;
    bface_t *outside;

    outside = NULL;

    for (f = bh->faces; f; f = f->next)
    {
        newf = CopyFace(f);
        newf->w->getBounds(newf->bounds);
        newf->next = outside;
        outside = newf;
    }
    return outside;
}

extern const char *ContentsToString(const contents_t type);
static void CSGBrush(int brushnum)
{
    int hull;
    brush_t *brush1; // b1
    brush_t *brush2; // b2
    brushhull_t *brushHull1;
    brushhull_t *brushHull2;
    int brushNumber;
    bool shouldOverwrite;
    bface_t *face;  // f
    bface_t *face2; // f2
    bface_t *nextFace;
    bface_t *outsideFaceList; // All of the faces start on the outside list
    entity_t *entity;
    vec_t faceArea;

    brush1 = &g_mapbrushes[brushnum];        // get brush info from the given brushnum that we can work with
    entity = &g_entities[brush1->entitynum]; // get entity info from the given brushnum that we can work with

    for (hull = 0; hull < NUM_HULLS; hull++) // for each of the hulls
    {
        brushHull1 = &brush1->hulls[hull];
        if (brushHull1->faces &&
            (hull ? brush1->clipnodedetaillevel : brush1->detaillevel))
        {
            switch (brush1->contents)
            {
            case CONTENTS_ORIGIN:
            case CONTENTS_BOUNDINGBOX:
            case CONTENTS_HINT:
            case CONTENTS_TOEMPTY:
                break;
            default:
                Error("Entity %i, Brush %i: %s brushes not allowed in detail\n",
                      brush1->originalentitynum, brush1->originalbrushnum,
                      ContentsToString((contents_t)brush1->contents));
                break;
            case CONTENTS_SOLID:
                WriteDetailBrush(hull, brushHull1->faces);
                break;
            }
        }

        outsideFaceList = CopyFacesToOutside(brushHull1); // set outside to a copy of the brush's faces
        shouldOverwrite = false;
        if (brush1->contents == CONTENTS_TOEMPTY)
        {
            for (face = outsideFaceList; face; face = face->next)
            {
                face->contents = CONTENTS_TOEMPTY;
                face->backcontents = CONTENTS_TOEMPTY;
            }
        }

        for (brushNumber = 0; brushNumber < entity->numbrushes; brushNumber++) // for each brush in entity e
        {
            if (entity->firstbrush + brushNumber == brushnum) // see if b2 needs to clip a chunk out of b1
            {
                continue;
            }
            shouldOverwrite = entity->firstbrush + brushNumber > brushnum;

            brush2 = &g_mapbrushes[entity->firstbrush + brushNumber];
            brushHull2 = &brush2->hulls[hull];
            if (brush2->contents == CONTENTS_TOEMPTY)
                continue;
            if (
                (hull ? (brush2->clipnodedetaillevel - 0 > brush1->clipnodedetaillevel + 0) : (brush2->detaillevel - brush2->chopdown > brush1->detaillevel + brush1->chopup)))
                continue; // you can't chop
            if (brush2->contents == brush1->contents &&
                (hull ? (brush2->clipnodedetaillevel != brush1->clipnodedetaillevel) : (brush2->detaillevel != brush1->detaillevel)))
            {
                shouldOverwrite =
                    (hull ? (brush2->clipnodedetaillevel < brush1->clipnodedetaillevel) : (brush2->detaillevel < brush1->detaillevel));
            }
            if (brush2->contents == brush1->contents && hull == 0 && brush2->detaillevel == brush1->detaillevel && brush2->coplanarpriority != brush1->coplanarpriority)
            {
                shouldOverwrite = brush2->coplanarpriority > brush1->coplanarpriority;
            }

            if (!brushHull2->faces)
                continue; // brush isn't in this hull

            if (brushHull1->bounds.testDisjoint(brushHull2->bounds)) // check brush bounding box first. TODO: use boundingbox method instead
            {
                continue;
            }

            face = outsideFaceList;
            outsideFaceList = NULL;
            for (; face; face = nextFace) // divide faces by the planes of the b2 to find which. Fragments are inside
            {
                nextFace = face->next;
                if (brushHull2->bounds.testDisjoint(face->bounds)) // check face bounding box first
                {                                                  // this face doesn't intersect brush2's bbox
                    face->next = outsideFaceList;
                    outsideFaceList = face;
                    continue;
                }
                if (
                    (hull ? (brush2->clipnodedetaillevel > brush1->clipnodedetaillevel) : (brush2->detaillevel > brush1->detaillevel)))
                {
                    const char *texname = GetTextureByNumber_CSG(face->texinfo);
                    if (face->texinfo == -1 || !strncasecmp(texname, "SKIP", 4) || !strncasecmp(texname, "HINT", 4) || !strncasecmp(texname, "SOLIDHINT", 9) || !strncasecmp(texname, "BEVELHINT", 9))
                    { // should not nullify the fragment inside detail brush
                        face->next = outsideFaceList;
                        outsideFaceList = face;
                        continue;
                    }
                }

                Winding *w = new Winding(*face->w); // throw pieces on the front sides of the planes into the outside list, return the remains on the inside, find the fragment inside brush2
                for (face2 = brushHull2->faces; face2; face2 = face2->next)
                {
                    if (face->planenum == face2->planenum)
                    {
                        if (!shouldOverwrite) // face plane is outside brush2
                        {
                            w->m_NumPoints = 0;
                            break;
                        }
                        else
                        {
                            continue;
                        }
                    }
                    if (face->planenum == (face2->planenum ^ 1))
                    {
                        continue;
                    }
                    Winding *fw;
                    Winding *bw;
                    w->Clip(face2->plane->normal, face2->plane->dist, &fw, &bw);
                    if (fw)
                    {
                        delete fw;
                    }
                    if (bw)
                    {
                        delete w;
                        w = bw;
                    }
                    else
                    {
                        w->m_NumPoints = 0;
                        break;
                    }
                }
                if (w->m_NumPoints) // do real split
                {
                    for (face2 = brushHull2->faces; face2; face2 = face2->next)
                    {
                        if (face->planenum == face2->planenum || face->planenum == (face2->planenum ^ 1))
                        {
                            continue;
                        }
                        int valid = 0;
                        int x;
                        for (x = 0; x < w->m_NumPoints; x++)
                        {
                            vec_t dist = DotProduct(w->m_Points[x], face2->plane->normal) - face2->plane->dist;
                            if (dist >= -ON_EPSILON * 4) // only estimate
                            {
                                valid++;
                            }
                        }
                        if (valid >= 2) // this splitplane forms an edge
                        {
                            Winding *fw;
                            Winding *bw;
                            face->w->Clip(face2->plane->normal, face2->plane->dist, &fw, &bw);
                            if (fw)
                            {
                                bface_t *front = NewFaceFromFace(face);
                                front->w = fw;
                                fw->getBounds(front->bounds);
                                front->next = outsideFaceList;
                                outsideFaceList = front;
                            }
                            if (bw)
                            {
                                delete face->w;
                                face->w = bw;
                                bw->getBounds(face->bounds);
                            }
                            else
                            {
                                FreeFace(face);
                                face = NULL;
                                break;
                            }
                        }
                    }
                }
                else
                {
                    face->next = outsideFaceList;
                    outsideFaceList = face;
                    face = NULL;
                }
                delete w;

                faceArea = face ? face->w->getArea() : 0;
                if (face) // there is one convex fragment of the original, face left inside brush2
                {
                    if ((hull ? (brush2->clipnodedetaillevel > brush1->clipnodedetaillevel) : (brush2->detaillevel > brush1->detaillevel)))
                    { // don't chop or set contents, only nullify
                        face->next = outsideFaceList;
                        outsideFaceList = face;
                        face->texinfo = -1;
                        continue;
                    }
                    if ((hull ? brush2->clipnodedetaillevel < brush1->clipnodedetaillevel : brush2->detaillevel < brush1->detaillevel) && brush2->contents == CONTENTS_SOLID)
                    { // real solid
                        FreeFace(face);
                        continue;
                    }
                    if (brush1->contents == CONTENTS_TOEMPTY)
                    {
                        bool onfront = true, onback = true;
                        for (face2 = brushHull2->faces; face2; face2 = face2->next)
                        {
                            if (face->planenum == (face2->planenum ^ 1))
                                onback = false;
                            if (face->planenum == face2->planenum)
                                onfront = false;
                        }
                        if (onfront && face->contents < brush2->contents)
                            face->contents = brush2->contents;
                        if (onback && face->backcontents < brush2->contents)
                            face->backcontents = brush2->contents;
                        if (face->contents == CONTENTS_SOLID && face->backcontents == CONTENTS_SOLID && strncasecmp(GetTextureByNumber_CSG(face->texinfo), "SOLIDHINT", 9) && strncasecmp(GetTextureByNumber_CSG(face->texinfo), "BEVELHINT", 9))
                        {
                            FreeFace(face);
                        }
                        else
                        {
                            face->next = outsideFaceList;
                            outsideFaceList = face;
                        }
                        continue;
                    }
                    if (((brush1->contents > brush2->contents) ||
                         (brush1->contents == brush2->contents && !strncasecmp(GetTextureByNumber_CSG(face->texinfo), "SOLIDHINT", 9))) ||
                        (brush1->contents == brush2->contents && !strncasecmp(GetTextureByNumber_CSG(face->texinfo), "BEVELHINT", 9)))
                    { // inside a water brush
                        face->contents = brush2->contents;
                        face->next = outsideFaceList;
                        outsideFaceList = face;
                    }
                    else // inside a solid brush
                    {
                        FreeFace(face); // throw it away
                    }
                }
            }
        }
        SaveOutside(brush1, hull, outsideFaceList, brush1->contents); // all of the faces left in outside are real surface faces
    }
}

static void EmitPlanes()
{
    g_numplanes = g_nummapplanes;
    {
        char name[_MAX_PATH];
        safe_snprintf(name, _MAX_PATH, "%s.pln", g_Mapname);
        FILE *planeout = fopen(name, "wb");
        if (!planeout)
            Error("Couldn't open %s", name);
        SafeWrite(planeout, g_mapplanes, g_nummapplanes * sizeof(plane_t));
        fclose(planeout);
    }
}

static void SetModelNumbers()
{
    int i;
    int models;
    char value[10];

    models = 1;
    for (i = 1; i < g_numentities; i++)
    {
        if (g_entities[i].numbrushes)
        {
            safe_snprintf(value, sizeof(value), "*%i", models);
            models++;
            SetKeyValue(&g_entities[i], "model", value);
        }
    }
}

void ReuseModel()
{
    int i;
    for (i = g_numentities - 1; i >= 1; i--) // so it won't affect the remaining entities in the loop when we move this entity backward
    {
        const char *name = ValueForKey(&g_entities[i], "zhlt_usemodel");
        if (!*name)
        {
            continue;
        }
        int j;
        for (j = 1; j < g_numentities; j++)
        {
            if (*ValueForKey(&g_entities[j], "zhlt_usemodel"))
            {
                continue;
            }
            if (!strcmp(name, ValueForKey(&g_entities[j], "targetname")))
            {
                break;
            }
        }
        if (j == g_numentities)
        {
            if (!strcasecmp(name, "null"))
            {
                SetKeyValue(&g_entities[i], "model", "");
                continue;
            }
            Error("zhlt_usemodel: can not find target entity '%s', or that entity is also using 'zhlt_usemodel'.\n", name);
        }
        SetKeyValue(&g_entities[i], "model", ValueForKey(&g_entities[j], "model"));
        if (j > i) // move this entity backward to prevent precache error in case of .mdl/.spr and wrong result of EntityForModel in case of map model
        {
            entity_t tmp;
            tmp = g_entities[i];
            memmove(&g_entities[i], &g_entities[i + 1], ((j + 1) - (i + 1)) * sizeof(entity_t));
            g_entities[j] = tmp;
        }
    }
}

#define MAX_SWITCHED_LIGHTS 32
#define MAX_LIGHTTARGETS_NAME 64
static void SetLightStyles()
{
    int stylenum;
    const char *t;
    entity_t *e;
    int i, j;
    char value[10];
    char lighttargets[MAX_SWITCHED_LIGHTS][MAX_LIGHTTARGETS_NAME];

    bool newtexlight = false;

    stylenum = 0; // any light that is controlled (has a targetname) must have a unique style number generated for it
    for (i = 1; i < g_numentities; i++)
    {
        e = &g_entities[i];

        t = ValueForKey(e, "classname");
        if (strncasecmp(t, "light", 5))
        {
            t = ValueForKey(e, "style"); // if it's not a normal light entity, allocate it a new style if necessary.
            switch (atoi(t))
            {
            case 0: // not a light, no style, generally pretty boring
                continue;
            case -1: // normal switchable texlight
                safe_snprintf(value, sizeof(value), "%i", 32 + stylenum);
                SetKeyValue(e, "style", value);
                stylenum++;
                continue;
            case -2: // backwards switchable texlight
                safe_snprintf(value, sizeof(value), "%i", -(32 + stylenum));
                SetKeyValue(e, "style", value);
                stylenum++;
                continue;
            case -3:                          // (HACK) a piggyback texlight: switched on and off by triggering a real light that has the same name
                SetKeyValue(e, "style", "0"); // just in case the level designer didn't give it a name
                newtexlight = true;           // don't 'continue', fall out
            }
        }
        t = ValueForKey(e, "targetname");
        if (*ValueForKey(e, "zhlt_usestyle"))
        {
            t = ValueForKey(e, "zhlt_usestyle");
            if (!strcasecmp(t, "null"))
            {
                t = "";
            }
        }
        if (!t[0])
        {
            continue;
        }

        for (j = 0; j < stylenum; j++) // find this targetname
        {
            if (!strcmp(lighttargets[j], t))
            {
                break;
            }
        }
        if (j == stylenum)
        {
            hlassume(stylenum < MAX_SWITCHED_LIGHTS, assume_MAX_SWITCHED_LIGHTS);
            safe_strncpy(lighttargets[j], t, MAX_LIGHTTARGETS_NAME);
            stylenum++;
        }
        safe_snprintf(value, sizeof(value), "%i", 32 + j);
        SetKeyValue(e, "style", value);
    }
}

static void ConvertHintToEmpty()
{
    int i;
    for (i = 0; i < MAX_MAP_BRUSHES; i++) // Convert HINT brushes to EMPTY after they have been carved by csg
    {
        if (g_mapbrushes[i].contents == CONTENTS_HINT)
        {
            g_mapbrushes[i].contents = CONTENTS_EMPTY;
        }
    }
}

void LoadWadValue()
{
    char *wadvalue;
    ParseFromMemory(g_dentdata, g_entdatasize);
    epair_t *e;
    entity_t ent0;
    entity_t *mapent = &ent0;
    memset(mapent, 0, sizeof(entity_t));
    if (!GetToken(true))
    {
        wadvalue = strdup("");
    }
    else
    {
        if (strcmp(g_token, "{"))
        {
            Error("ParseEntity: { not found");
        }
        while (1)
        {
            if (!GetToken(true))
            {
                Error("ParseEntity: EOF without closing brace");
            }
            if (!strcmp(g_token, "}"))
            {
                break;
            }
            e = ParseEpair();
            e->next = mapent->epairs;
            mapent->epairs = e;
        }
        wadvalue = strdup(ValueForKey(mapent, "wad"));
        epair_t *next;
        for (e = mapent->epairs; e; e = next)
        {
            next = e->next;
            free(e->key);
            free(e->value);
            free(e);
        }
    }
    SetKeyValue(&g_entities[0], "wad", wadvalue);
    free(wadvalue);
}

void WriteBSP(const char *const name)
{
    char path[_MAX_PATH];

    safe_snprintf(path, _MAX_PATH, "%s.bsp", name);

    SetModelNumbers();
    ReuseModel();
    SetLightStyles();

    WriteMiptex();
    UnparseEntities();
    ConvertHintToEmpty(); // this is ridiculous. --vluzacn
    WriteBSPFile(path);
}

unsigned int BrushClipHullsDiscarded = 0;
unsigned int ClipNodesDiscarded = 0;
static void MarkEntForNoclip(entity_t *ent)
{
    int i;
    brush_t *b;

    for (i = ent->firstbrush; i < ent->firstbrush + ent->numbrushes; i++)
    {
        b = &g_mapbrushes[i];
        b->noclip = 1;

        BrushClipHullsDiscarded++;
        ClipNodesDiscarded += b->numsides;
    }
}

static void CheckForNoClip()
{

    if (!g_bClipNazi)
        return; // NO CLIP FOR YOU!!!

    int count = 0;
    for (int i = 0; i < g_numentities; i++)
    {
        entity_t *ent = &g_entities[i];

        if (!ent->numbrushes || i == 0) // Skip entities that are not models or worldspawn
            continue;

        char entclassname[MAX_KEY];
        strcpy_s(entclassname, ValueForKey(ent, "classname"));
        int spawnflags = atoi(ValueForKey(ent, "spawnflags"));
        int skin = IntForKey(ent, "skin"); // vluzacn

        bool markForNoclip = false;

        if (skin != -16)
        {
            if (strcmp(entclassname, "env_bubbles") == 0 || strcmp(entclassname, "func_illusionary") == 0)
            {
                markForNoclip = true;
            }
            else if (spawnflags & 8) // Pasable flag
            {
                if (strcmp(entclassname, "func_train") == 0 ||
                    strcmp(entclassname, "func_door") == 0 ||
                    strcmp(entclassname, "func_water") == 0 ||
                    strcmp(entclassname, "func_door_rotating") == 0 ||
                    strcmp(entclassname, "func_pendulum") == 0 ||
                    strcmp(entclassname, "func_tracktrain") == 0 ||
                    strcmp(entclassname, "func_vehicle") == 0)
                {
                    markForNoclip = true;
                }
            }
            else if (skin != 0)
            {
                if (strcmp(entclassname, "func_water") == 0)
                { // Removed func_door
                    markForNoclip = true;
                }
            }
            else if (spawnflags & 2) // Not solid flag
            {
                if (strcmp(entclassname, "func_conveyor") == 0)
                {
                    markForNoclip = true;
                }
            }
            else if (spawnflags & 1) // Not solid flag
            {
                if (strcmp(entclassname, "func_rot_button") == 0)
                {
                    markForNoclip = true;
                }
            }
            else if (spawnflags & 64) // Not solid flag
            {
                if (strcmp(entclassname, "func_rotating") == 0)
                {
                    markForNoclip = true;
                }
            }
        }
        if (markForNoclip)
        {
            MarkEntForNoclip(ent);
            count++;
        }
    }
    Log("%i entities discarded from clipping hulls\n", count);
}

static void ProcessModels()
{
    int i, j;
    int placed;
    int first, contents;
    brush_t temp;

    for (i = 0; i < g_numentities; i++)
    {
        if (!g_entities[i].numbrushes) // only models
            continue;

        first = g_entities[i].firstbrush; // sort the contents down so stone bites water, etc
        brush_t *temps = (brush_t *)malloc(g_entities[i].numbrushes * sizeof(brush_t));
        hlassume(temps, assume_NoMemory);
        for (j = 0; j < g_entities[i].numbrushes; j++)
        {
            temps[j] = g_mapbrushes[first + j];
        }
        int placedcontents;
        bool b_placedcontents = false;
        for (placed = 0; placed < g_entities[i].numbrushes;)
        {
            bool b_contents = false;
            for (j = 0; j < g_entities[i].numbrushes; j++)
            {
                brush_t *brush = &temps[j];
                if (b_placedcontents && brush->contents <= placedcontents)
                    continue;
                if (b_contents && brush->contents >= contents)
                    continue;
                b_contents = true;
                contents = brush->contents;
            }
            for (j = 0; j < g_entities[i].numbrushes; j++)
            {
                brush_t *brush = &temps[j];
                if (brush->contents == contents)
                {
                    g_mapbrushes[first + placed] = *brush;
                    placed++;
                }
            }
            b_placedcontents = true;
            placedcontents = contents;
        }
        free(temps);

        if (i == 0) // csg them in order, first its worldspawn....
        {
            NamedRunThreadsOnIndividual(g_entities[i].numbrushes, g_estimate, CSGBrush);
            CheckFatal();
        }
        else
        {
            for (j = 0; j < g_entities[i].numbrushes; j++)
            {
                CSGBrush(first + j);
            }
        }

        for (j = 0; j < NUM_HULLS; j++)
        { // write end of model marker
            fprintf(out[j], "-1 -1 -1 -1 -1\n");
            fprintf(out_detailbrush[j], "-1\n");
        }
    }
}

static void SetModelCenters(int entitynum)
{
    int i;
    int last;
    char string[MAXTOKEN];
    entity_t *e = &g_entities[entitynum];
    BoundingBox bounds;
    vec3_t center;

    if ((entitynum == 0) || (e->numbrushes == 0)) // skip worldspawn and point entities
        return;

    if (!*ValueForKey(e, "light_origin")) // skip if its not a zhlt_flags light_origin
        return;

    for (i = e->firstbrush, last = e->firstbrush + e->numbrushes; i < last; i++)
    {
        if (g_mapbrushes[i].contents != CONTENTS_ORIGIN && g_mapbrushes[i].contents != CONTENTS_BOUNDINGBOX)
        {
            bounds.add(g_mapbrushes[i].hulls->bounds);
        }
    }

    VectorAdd(bounds.m_Mins, bounds.m_Maxs, center);
    VectorScale(center, 0.5, center);

    safe_snprintf(string, MAXTOKEN, "%i %i %i", (int)center[0], (int)center[1], (int)center[2]);
    SetKeyValue(e, "model_center", string);
}

static void BoundWorld()
{
    int i;
    brushhull_t *h;

    world_bounds.reset();

    for (i = 0; i < g_nummapbrushes; i++)
    {
        h = &g_mapbrushes[i].hulls[0];
        if (!h->faces)
        {
            continue;
        }
        world_bounds.add(h->bounds);
    }
}

void CSGCleanup()
{
    FreeWadPaths();
}

void HandleArgs(int argc, char **argv, const char *&mapname_from_arg)
{
    int i;
    for (i = 1; i < argc; i++)
    {
        if (!strcasecmp(argv[i], "-worldextent"))
        {
            g_iWorldExtent = atoi(argv[++i]);
        }
        else if (!strcasecmp(argv[i], "-noskyclip"))
        {
            g_skyclip = false;
        }
        else if (!strcasecmp(argv[i], "-clipeconomy"))
        {
            g_bClipNazi = true;
        }

        else if (!strcasecmp(argv[i], "-cliptype"))
        {
            if (i + 1 < argc)
            {
                ++i;
                if (!strcasecmp(argv[i], "smallest"))
                {
                    g_cliptype = clip_smallest;
                }
                else if (!strcasecmp(argv[i], "normalized"))
                {
                    g_cliptype = clip_normalized;
                }
                else if (!strcasecmp(argv[i], "simple"))
                {
                    g_cliptype = clip_simple;
                }
                else if (!strcasecmp(argv[i], "precise"))
                {
                    g_cliptype = clip_precise;
                }
                else if (!strcasecmp(argv[i], "legacy"))
                {
                    g_cliptype = clip_legacy;
                }
            }
            else
            {
                Log("Error: -cliptype: incorrect usage of parameter\n");
                Usage(PROGRAM_CSG);
            }
        }
        else if (!strcasecmp(argv[i], "-texdata"))
        {
            if (i + 1 < argc)
            {
                int x = atoi(argv[++i]) * 1024;
                {
                    g_max_map_miptex = x;
                }
            }
            else
            {
                Usage(PROGRAM_CSG);
            }
        }
        else if (!strcasecmp(argv[i], "-lightdata"))
        {
            if (i + 1 < argc)
            {
                int x = atoi(argv[++i]) * 1024;

                {
                    g_max_map_lightdata = x;
                }
            }
            else
            {
                Usage(PROGRAM_CSG);
            }
        }
        else if (!mapname_from_arg)
        {
            const char *temp = argv[i];
            mapname_from_arg = temp;
        }
        else
        {
            Log("Unknown option \"%s\"\n", argv[i]);
            Usage(PROGRAM_CSG);
        }
    }
    if (!mapname_from_arg)
    {
        Log("No mapfile specified\n");
        Usage(PROGRAM_CSG);
    }
}

#ifdef HLCSG_GAMETEXTMESSAGE_UTF8
void ConvertGameTextMessages()
{
    int count = 0;
    for (int i = 0; i < g_numentities; i++)
    {
        entity_t *ent = &g_entities[i];
        const char *value;
        char *newvalue;

        // Check if the entity is a "game_text"
        if (strcmp(ValueForKey(ent, "classname"), "game_text"))
        {
            continue;
        }

        // Get the current value of the "message" key
        value = ValueForKey(ent, "message");
        if (*value)
        {
            // Convert the ANSI value to UTF-8
            newvalue = ANSItoUTF8(value);
            if (strcmp(newvalue, value))
            {
                // Set the new UTF-8 value
                SetKeyValue(ent, "message", newvalue);
                count++;
            }
            free(newvalue);
        }
    }
    if (count)
    {
        Log("%d game_text messages converted from Windows ANSI(CP_ACP) to UTF-8 encoding\n", count);
    }
}
#endif

int main(const int argc, char **argv)
{
    int i;
    char name[_MAX_PATH];                // mapanme
    double start, end;                   // start/end time log
    const char *mapname_from_arg = NULL; // mapname path from passed argvar

    g_Program = "sdHLCSG";
    if (InitConsole(argc, argv) < 0)
        Usage(PROGRAM_CSG);
    if (argc == 1)
        Usage(PROGRAM_CSG);

    InitDefaultHulls();
    HandleArgs(argc, argv, mapname_from_arg);

    safe_strncpy(g_Mapname, mapname_from_arg, _MAX_PATH); // handle mapname
    FlipSlashes(g_Mapname);
    StripExtension(g_Mapname);

    ResetTmpFiles();

    ResetErrorLog();
    atexit(CloseLog);
    LogArguments(argc, argv);
#ifdef PLATFORM_CAN_CALC_EXTENT
    hlassume(CalcFaceExtents_test(), assume_first);
#endif
    atexit(CSGCleanup); // AJM
    dtexdata_init();
    atexit(dtexdata_free);

    start = I_FloatTime(); // START CSG

    safe_strncpy(name, mapname_from_arg, _MAX_PATH); // make a copy of the nap name
    FlipSlashes(name);
    DefaultExtension(name, ".map"); // might be .reg
    LoadMapFile(name);
    ThreadSetDefault();
    ThreadSetPriority(g_threadpriority);

#ifdef HLCSG_GAMETEXTMESSAGE_UTF8
    ConvertGameTextMessages(); // Windows ANSI(CP_ACP) to UTF-8
#endif

    GetUsedWads(); // Get wads from worldspawn "wad" key

    CheckForNoClip(); // Check brushes that should not generate clipnodes

    NamedRunThreadsOnIndividual(g_nummapbrushes, g_estimate, CreateBrush); // createbrush
    CheckFatal();

    BoundWorld(); // boundworld

    for (i = 0; i < g_numentities; i++)
    {
        SetModelCenters(i); // Set model centers //NamedRunThreadsOnIndividual(g_numentities, g_estimate, SetModelCenters); //--vluzacn
    }
    for (i = 0; i < NUM_HULLS; i++) // open hull files
    {
        char name[_MAX_PATH];

        safe_snprintf(name, _MAX_PATH, "%s.p%i", g_Mapname, i);

        out[i] = fopen(name, "w");

        if (!out[i])
            Error("Couldn't open %s", name);
        safe_snprintf(name, _MAX_PATH, "%s.b%i", g_Mapname, i);
        out_detailbrush[i] = fopen(name, "w");
        if (!out_detailbrush[i])
            Error("Couldn't open %s", name);
    }
    {
        FILE *f;
        char name[_MAX_PATH];
        safe_snprintf(name, _MAX_PATH, "%s.hsz", g_Mapname);
        f = fopen(name, "w");
        if (!f)
            Error("Couldn't open %s", name);
        float x1, y1, z1;
        float x2, y2, z2;
        for (i = 0; i < NUM_HULLS; i++)
        {
            x1 = g_hull_size[i][0][0];
            y1 = g_hull_size[i][0][1];
            z1 = g_hull_size[i][0][2];
            x2 = g_hull_size[i][1][0];
            y2 = g_hull_size[i][1][1];
            z2 = g_hull_size[i][1][2];
            fprintf(f, "%g %g %g %g %g %g\n", x1, y1, z1, x2, y2, z2);
        }
        fclose(f);
    }

    ProcessModels();

    for (i = 0; i < NUM_HULLS; i++) // close hull files
    {
        fclose(out[i]);
        fclose(out_detailbrush[i]);
    }

    EmitPlanes();

    WriteBSP(g_Mapname);
    end = I_FloatTime(); // elapsed time
    LogTimeElapsed(end - start);
    return 0;
}