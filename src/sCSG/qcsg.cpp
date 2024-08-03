#include "csg.h"
#include "textures.h"
#include "maplib.h"
#include "arguments.h"
#include "threads.h"
#include "blockmem.h"
#include "filelib.h"

static FILE *out[NUM_HULLS]; // pointer to each of the hull out files (.p0, .p1, ect.)
static FILE *out_detailbrush[NUM_HULLS];
static int c_outfaces;
static int c_csgfaces;
BoundingBox world_bounds;

bool g_skyclip = DEFAULT_SKYCLIP;       // no sky clipping "-noskyclip"
bool g_estimate = DEFAULT_ESTIMATE;     // progress estimates "-estimate"
cliptype g_cliptype = DEFAULT_CLIPTYPE; // "-cliptype <value>"
bool g_bClipNazi = DEFAULT_CLIPNAZI;    // "-noclipeconomy"

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
                Usage(ProgramType::PROGRAM_CSG);
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
                Usage(ProgramType::PROGRAM_CSG);
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
                Usage(ProgramType::PROGRAM_CSG);
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
            Usage(ProgramType::PROGRAM_CSG);
        }
    }
    if (!mapname_from_arg)
    {
        Log("No mapfile specified\n");
        Usage(ProgramType::PROGRAM_CSG);
    }
}

auto NewFaceFromFace(const bface_t *const in) -> bface_t * // Duplicates the non point information of a face, used by SplitFace
{
    auto *newFace = (bface_t *)Alloc(sizeof(bface_t));
    newFace->contents = in->contents;
    newFace->texinfo = in->texinfo;
    newFace->planenum = in->planenum;
    newFace->plane = in->plane;
    newFace->backcontents = in->backcontents;

    return newFace;
}

void FreeFace(bface_t *face)
{
    delete face->w;
    Free(face);
}

void WriteFace(const int hull, const bface_t *const face, int detaillevel)
{
    ThreadLock();
    if (!hull)
        c_csgfaces++;
    auto *w = face->w; // .p0 format

    fprintf(out[hull], "%i %i %i %i %u\n", detaillevel, face->planenum, face->texinfo, face->contents, w->m_NumPoints); // plane summary

    for (int i = 0; i < w->m_NumPoints; i++) // for each of the points on the face
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
    for (const bface_t *face = faces; face; face = face->next)
    {
        auto *w = face->w;
        fprintf(out_detailbrush[hull], "%i %u\n", face->planenum, w->m_NumPoints);
        for (int i = 0; i < w->m_NumPoints; i++)
        {
            fprintf(out_detailbrush[hull], "%5.8f %5.8f %5.8f\n", w->m_Points[i][0], w->m_Points[i][1], w->m_Points[i][2]);
        }
    }
    fprintf(out_detailbrush[hull], "-1 -1\n");
    ThreadUnlock();
}

static void SaveOutside(const brush_t *const brush, const int hull, bface_t *outside, const int mirrorcontents) // The faces remaining on the outside list are final polygons.  Write them to the output file.
{                                                                                                               // Passable contents (water, lava, etc) will generate a mirrored copy of the face to be seen from the inside.
    bface_t *face;
    bface_t *face2;
    bface_t *next;
    vec3_t temp;

    for (face = outside; face; face = next)
    {
        next = face->next;

        int frontcontents, backcontents;
        auto texinfo = face->texinfo;
        const auto *texname = GetTextureByNumber_CSG(texinfo);
        frontcontents = face->contents;
        if (mirrorcontents == CONTENTS_TOEMPTY)
        {
            backcontents = face->backcontents;
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
        auto frontnull = false;
        auto backnull = false;
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
        if (brush->entitynum != 0 && !strncasecmp(texname, "!", 1))
        {
            backnull = true; // strip water face on one side
        }

        face->contents = frontcontents;
        if (frontnull)
        {
            face->texinfo = -1;
        }
        else
        {
            face->texinfo = texinfo;
        }

        if (!hull) // count unique faces
        {
            for (face2 = brush->hulls[hull].faces; face2; face2 = face2->next)
            {
                if (face2->planenum == face->planenum)
                {
                    if (!face2->used)
                    {
                        face2->used = true;
                        c_outfaces++;
                    }
                    break;
                }
            }
        }

        if (!hull) // check the texture alignment of this face
        {
            auto texinfo = face->texinfo;
            const auto *texname = GetTextureByNumber_CSG(texinfo);
            auto *tex = &g_texinfo[texinfo];

            if (texinfo != -1                                                         // nullified textures (NULL, BEVEL, aaatrigger, etc.)
                && !(tex->flags & TEX_SPECIAL)                                        // sky
                && strncasecmp(texname, "SKIP", 4) && strncasecmp(texname, "HINT", 4) // HINT and SKIP will be nullified only after hlbsp
                && strncasecmp(texname, "SOLIDHINT", 9) && strncasecmp(texname, "BEVELHINT", 9))
            {
                vec3_t texnormal; // check for "Malformed face (%d) normal"
                CrossProduct(tex->vecs[1], tex->vecs[0], texnormal);
                VectorNormalize(texnormal);
                if (fabs(DotProduct(texnormal, face->plane->normal)) <= NORMAL_EPSILON)
                {
                    Warning("Entity %i, Brush %i: Malformed texture alignment (texture %s): Texture axis perpendicular to face.",
                            brush->originalentitynum, brush->originalbrushnum,
                            texname);
                }

                auto bad = false;
                for (int i = 0; i < face->w->m_NumPoints; i++)
                {
                    for (int j = 0; j < 2; j++)
                    {
                        auto val = DotProduct(face->w->m_Points[i], tex->vecs[j]) + tex->vecs[j][3];
                        if (val < -99999 || val > 999999)
                        {
                            bad = true;
                        }
                    }
                }
                if (bad)
                {
                    Warning("Entity %i, Brush %i: Malformed texture alignment (texture %s): Bad surface extents.", brush->originalentitynum, brush->originalbrushnum, texname);
                }
            }
        }

        WriteFace(hull, face, (hull ? brush->clipnodedetaillevel : brush->detaillevel));

        { // if (mirrorcontents != static_cast<int>(contents_t::CONTENTS_SOLID))
            face->planenum ^= 1;
            face->plane = &g_mapplanes[face->planenum];
            face->contents = backcontents;
            face->texinfo = backnull ? -1 : texinfo;

            for (int i = 0; i < face->w->m_NumPoints / 2; i++) // swap point orders and add points backwards
            {
                VectorCopy(face->w->m_Points[i], temp);
                VectorCopy(face->w->m_Points[face->w->m_NumPoints - 1 - i], face->w->m_Points[i]);
                VectorCopy(temp, face->w->m_Points[face->w->m_NumPoints - 1 - i]);
            }
            WriteFace(hull, face, (hull ? brush->clipnodedetaillevel : brush->detaillevel));
        }
        FreeFace(face);
    }
}

auto CopyFace(const bface_t *const face) -> bface_t *
{
    auto *newFace = NewFaceFromFace(face);
    newFace->w = face->w->Copy();
    newFace->bounds = face->bounds;
    return newFace;
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

static auto CopyFacesToOutside(brushhull_t *bh) -> bface_t *
{
    bface_t *outside = nullptr;

    for (auto *f = bh->faces; f; f = f->next)
    {
        auto *newf = CopyFace(f);
        newf->w->getBounds(newf->bounds);
        newf->next = outside;
        outside = newf;
    }
    return outside;
}

extern auto ContentsToString(const contents_t type) -> const char *;

static void CSGBrush(int brushnum)
{
    bface_t *face;
    bface_t *face2; // f2
    bface_t *nextFace;

    auto *brush1 = &g_mapbrushes[brushnum];        // get brush info from the given brushnum that we can work with
    auto *entity = &g_entities[brush1->entitynum]; // get entity info from the given brushnum that we can work with

    for (int hull = 0; hull < NUM_HULLS; hull++) // for each of the hulls
    {
        auto *brushHull1 = &brush1->hulls[hull];
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
            case contents_t::CONTENTS_SOLID:
                WriteDetailBrush(hull, brushHull1->faces);
                break;
            }
        }

        auto *outsideFaceList = CopyFacesToOutside(brushHull1); // set outside to a copy of the brush's faces
        auto shouldOverwrite = false;
        if (brush1->contents == CONTENTS_TOEMPTY)
        {
            for (face = outsideFaceList; face; face = face->next)
            {
                face->contents = CONTENTS_TOEMPTY;
                face->backcontents = CONTENTS_TOEMPTY;
            }
        }

        for (int brushNumber = 0; brushNumber < entity->numbrushes; brushNumber++) // for each brush in entity e
        {
            if (entity->firstbrush + brushNumber == brushnum) // see if b2 needs to clip a chunk out of b1
            {
                continue;
            }
            shouldOverwrite = entity->firstbrush + brushNumber > brushnum;

            auto *brush2 = &g_mapbrushes[entity->firstbrush + brushNumber];
            auto *brushHull2 = &brush2->hulls[hull];
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
            outsideFaceList = nullptr;
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

                auto *w = new Winding(*face->w); // throw pieces on the front sides of the planes into the outside list, return the remains on the inside, find the fragment inside brush2
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
                                face = nullptr;
                                break;
                            }
                        }
                    }
                }
                else
                {
                    face->next = outsideFaceList;
                    outsideFaceList = face;
                    face = nullptr;
                }
                delete w;

                auto faceArea = face ? face->w->getArea() : 0;
                if (face) // there is one convex fragment of the original, face left inside brush2
                {
                    if ((hull ? (brush2->clipnodedetaillevel > brush1->clipnodedetaillevel) : (brush2->detaillevel > brush1->detaillevel)))
                    { // don't chop or set contents, only nullify
                        face->next = outsideFaceList;
                        outsideFaceList = face;
                        face->texinfo = -1;
                        continue;
                    }
                    if ((hull ? brush2->clipnodedetaillevel < brush1->clipnodedetaillevel : brush2->detaillevel < brush1->detaillevel) && brush2->contents == static_cast<int>(contents_t::CONTENTS_SOLID))
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
                        if (face->contents == static_cast<int>(contents_t::CONTENTS_SOLID) && face->backcontents == static_cast<int>(contents_t::CONTENTS_SOLID) && strncasecmp(GetTextureByNumber_CSG(face->texinfo), "SOLIDHINT", 9) && strncasecmp(GetTextureByNumber_CSG(face->texinfo), "BEVELHINT", 9))
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

static void SetModelNumbers()
{
    char value[10];
    auto models = 1;
    for (int i = 1; i < g_numentities; i++)
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
    for (int i = g_numentities - 1; i >= 1; i--) // so it won't affect the remaining entities in the loop when we move this entity backward
    {
        auto *name = ValueForKey(&g_entities[i], "zhlt_usemodel");
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

static void SetLightStyles()
{
    int j;
    char value[10];
    char lighttargets[MAX_SWITCHED_LIGHTS][MAX_LIGHTTARGETS_NAME];

    auto newtexlight = false;

    auto stylenum = 0; // any light that is controlled (has a targetname) must have a unique style number generated for it
    for (int i = 1; i < g_numentities; i++)
    {
        auto *e = &g_entities[i];

        auto *t = ValueForKey(e, "classname");
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
    for (int i = 0; i < MAX_MAP_BRUSHES; i++) // Convert HINT brushes to EMPTY after they have been carved by csg
    {
        if (g_mapbrushes[i].contents == CONTENTS_HINT)
        {
            g_mapbrushes[i].contents = CONTENTS_EMPTY;
        }
    }
}

unsigned int BrushClipHullsDiscarded = 0;
unsigned int ClipNodesDiscarded = 0;
static void MarkEntForNoclip(entity_t *ent)
{
    for (int i = ent->firstbrush; i < ent->firstbrush + ent->numbrushes; i++)
    {
        auto *b = &g_mapbrushes[i];
        b->noclip = 1;

        BrushClipHullsDiscarded++;
        ClipNodesDiscarded += b->numsides;
    }
}

void CSGCleanup()
{
    FreeWadPaths();
}

static void CheckForNoClip()
{

    if (!g_bClipNazi)
        return; // NO CLIP FOR YOU!!!

    int count = 0;
    for (int i = 0; i < g_numentities; i++)
    {
        auto *ent = &g_entities[i];

        if (!ent->numbrushes || i == 0) // Skip entities that are not models or worldspawn
            continue;

        char entclassname[MAX_KEY];
        strcpy(entclassname, ValueForKey(ent, "classname"));
        int spawnflags = atoi(ValueForKey(ent, "spawnflags"));
        auto skin = IntForKey(ent, "skin"); // vluzacn

        auto markForNoclip = false;

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

static void BoundWorld()
{
    world_bounds.reset();
    for (int i = 0; i < g_nummapbrushes; i++)
    {
        auto *h = &g_mapbrushes[i].hulls[0];
        if (!h->faces)
        {
            continue;
        }
        world_bounds.add(h->bounds);
    }
}

static void SetModelCenters(int entitynum)
{
    int last;
    char string[MAXTOKEN];
    auto *e = &g_entities[entitynum];
    BoundingBox bounds;
    vec3_t center;

    if ((entitynum == 0) || (e->numbrushes == 0)) // skip worldspawn and point entities
        return;

    if (!*ValueForKey(e, "light_origin")) // skip if its not a zhlt_flags light_origin
        return;

    for (int i = e->firstbrush, last = e->firstbrush + e->numbrushes; i < last; i++)
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

void OpenHullFiles()
{
    for (int i = 0; i < NUM_HULLS; i++)
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
}

void WriteHullSizeFile()
{
    char name[_MAX_PATH];
    safe_snprintf(name, _MAX_PATH, "%s.hsz", g_Mapname);
    auto *f = fopen(name, "w");
    if (!f)
        Error("Couldn't open %s", name);

    for (int i = 0; i < NUM_HULLS; i++)
    {
        float x1 = g_hull_size[i][0][0];
        float y1 = g_hull_size[i][0][1];
        float z1 = g_hull_size[i][0][2];
        float x2 = g_hull_size[i][1][0];
        float y2 = g_hull_size[i][1][1];
        float z2 = g_hull_size[i][1][2];
        fprintf(f, "%g %g %g %g %g %g\n", x1, y1, z1, x2, y2, z2);
    }
    fclose(f);
}

static void ProcessModels() // a.k.a brush entity
{
    int j;
    int contents;

    for (int i = 0; i < g_numentities; i++)
    {
        if (!g_entities[i].numbrushes) // only models
            continue;

        auto first = g_entities[i].firstbrush; // sort the contents down so stone bites water, etc
        auto *temps = (brush_t *)malloc(g_entities[i].numbrushes * sizeof(brush_t));
        hlassume(temps, assume_NoMemory);
        for (j = 0; j < g_entities[i].numbrushes; j++)
        {
            temps[j] = g_mapbrushes[first + j];
        }
        int placedcontents;
        auto b_placedcontents = false;
        for (auto placed = 0; placed < g_entities[i].numbrushes;)
        {
            auto b_contents = false;
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

static void EmitPlanes()
{
    g_numplanes = g_nummapplanes;
    {
        char name[_MAX_PATH];
        safe_snprintf(name, _MAX_PATH, "%s.pln", g_Mapname);
        auto *planeout = fopen(name, "wb");
        if (!planeout)
            Error("Couldn't open %s", name);
        SafeWrite(planeout, g_mapplanes, g_nummapplanes * sizeof(Plane));
        fclose(planeout);
    }
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

auto main(const int argc, char **argv) -> int
{
    char name[_MAX_PATH];                   // mapanme
    double start, end;                      // start/end time log
    const char *mapname_from_arg = nullptr; // mapname path from passed argvar

    g_Program = "sCSG";
    if (InitConsole(argc, argv) < 0)
        Usage(ProgramType::PROGRAM_CSG);
    if (argc == 1)
        Usage(ProgramType::PROGRAM_CSG);

    InitDefaultHulls();
    HandleArgs(argc, argv, mapname_from_arg);

    safe_strncpy(g_Mapname, mapname_from_arg, _MAX_PATH); // handle mapname
    FlipSlashes(g_Mapname);
    StripExtension(g_Mapname);

    ResetTmpFiles();

    ResetErrorLog();
    atexit(CloseLog);
    LogArguments(argc, argv);
    hlassume(CalcFaceExtents_test(), assume_first);
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

    GetUsedWads(); // Get wads from worldspawn "wad" key

    CheckForNoClip(); // Check brushes that should not generate clipnodes

    NamedRunThreadsOnIndividual(g_nummapbrushes, g_estimate, CreateBrush); // createbrush
    CheckFatal();

    BoundWorld(); // boundworld

    for (int i = 0; i < g_numentities; i++)
    {
        SetModelCenters(i); // Set model centers
    }
    OpenHullFiles();
    WriteHullSizeFile();

    ProcessModels();

    for (int i = 0; i < NUM_HULLS; i++) // close hull files
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