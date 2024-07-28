#include "bsp5.h"

typedef struct wvert_s
{
    vec_t t;
    struct wvert_s *prev;
    struct wvert_s *next;
} wvert_t;

typedef struct wedge_s
{
    struct wedge_s *next;
    vec3_t dir;
    vec3_t origin;
    wvert_t head;
} wedge_t;

static int numwedges;
static int numwverts;
static int tjuncs;
static int tjuncfaces;

#define MAX_WVERTS 0x40000
#define MAX_WEDGES 0x20000

static wvert_t wverts[MAX_WVERTS];
static wedge_t wedges[MAX_WEDGES];

//============================================================================

#define NUM_HASH 4096

wedge_t *wedge_hash[NUM_HASH];

static vec3_t hash_min;
static vec3_t hash_scale;
// It's okay if the coordinates go under hash_min, because they are hashed in a cyclic way (modulus by hash_numslots)
// So please don't change the hardcoded hash_min and scale
static int hash_numslots[3];
#define MAX_HASH_NEIGHBORS 4

static void InitHash(const vec3_t mins, const vec3_t maxs)
{
    vec3_t size;
    vec_t volume;
    vec_t scale;

    // Let's ignore the parameters and make things more predictable, so there won't be strange cases such as division by 0 or extreme scaling values.
    VectorFill(hash_min, -8000);
    VectorFill(size, 16000);
    memset(wedge_hash, 0, sizeof(wedge_hash));

    volume = size[0] * size[1];

    scale = sqrt(volume / NUM_HASH);

    hash_numslots[0] = (int)floor(size[0] / scale);
    hash_numslots[1] = (int)floor(size[1] / scale);
    while (hash_numslots[0] * hash_numslots[1] > NUM_HASH)
    {
        hash_numslots[0]--;
        hash_numslots[1]--;
    }

    hash_scale[0] = hash_numslots[0] / size[0];
    hash_scale[1] = hash_numslots[1] / size[1];
}

static auto HashVec(const vec3_t vec, int *num_hashneighbors, int *hashneighbors) -> int
{
    int h;
    int i;
    int x;
    int y;
    int slot[2];
    vec_t normalized[2];
    vec_t slotdiff[2];

    for (i = 0; i < 2; i++)
    {
        normalized[i] = hash_scale[i] * (vec[i] - hash_min[i]);
        slot[i] = (int)floor(normalized[i]);
        slotdiff[i] = normalized[i] - (vec_t)slot[i];

        slot[i] = (slot[i] + hash_numslots[i]) % hash_numslots[i];
        slot[i] = (slot[i] + hash_numslots[i]) % hash_numslots[i]; // do it twice to handle negative values
    }

    h = slot[0] * hash_numslots[1] + slot[1];

    *num_hashneighbors = 0;
    for (x = -1; x <= 1; x++)
    {
        if ((x == -1 && slotdiff[0] > hash_scale[0] * (2 * ON_EPSILON)) ||
            (x == 1 && slotdiff[0] < 1 - hash_scale[0] * (2 * ON_EPSILON)))
        {
            continue;
        }
        for (y = -1; y <= 1; y++)
        {
            if ((y == -1 && slotdiff[1] > hash_scale[1] * (2 * ON_EPSILON)) ||
                (y == 1 && slotdiff[1] < 1 - hash_scale[1] * (2 * ON_EPSILON)))
            {
                continue;
            }
            if (*num_hashneighbors >= MAX_HASH_NEIGHBORS)
            {
                Error("HashVec: internal error.");
            }
            hashneighbors[*num_hashneighbors] =
                ((slot[0] + x + hash_numslots[0]) % hash_numslots[0]) * hash_numslots[1] +
                (slot[1] + y + hash_numslots[1]) % hash_numslots[1];
            (*num_hashneighbors)++;
        }
    }

    return h;
}

//============================================================================

static auto CanonicalVector(vec3_t vec) -> bool
{
    if (VectorNormalize(vec))
    {
        if (vec[0] > NORMAL_EPSILON)
        {
            return true;
        }
        else if (vec[0] < -NORMAL_EPSILON)
        {
            VectorSubtract(vec3_origin, vec, vec);
            return true;
        }
        else
        {
            vec[0] = 0;
        }

        if (vec[1] > NORMAL_EPSILON)
        {
            return true;
        }
        else if (vec[1] < -NORMAL_EPSILON)
        {
            VectorSubtract(vec3_origin, vec, vec);
            return true;
        }
        else
        {
            vec[1] = 0;
        }

        if (vec[2] > NORMAL_EPSILON)
        {
            return true;
        }
        else if (vec[2] < -NORMAL_EPSILON)
        {
            VectorSubtract(vec3_origin, vec, vec);
            return true;
        }
        else
        {
            vec[2] = 0;
        }
        //        hlassert(false);
        return false;
    }
    //    hlassert(false);
    return false;
}

static auto FindEdge(const vec3_t p1, const vec3_t p2, vec_t *t1, vec_t *t2) -> wedge_t *
{
    vec3_t origin;
    vec3_t dir;
    wedge_t *w;
    vec_t temp;
    int h;
    int num_hashneighbors;
    int hashneighbors[MAX_HASH_NEIGHBORS];

    VectorSubtract(p2, p1, dir);
    if (!CanonicalVector(dir))
    {
#if _DEBUG
        Warning("CanonicalVector: degenerate @ (%4.3f %4.3f %4.3f )\n", p1[0], p1[1], p1[2]);
#endif
    }

    *t1 = DotProduct(p1, dir);
    *t2 = DotProduct(p2, dir);

    VectorMA(p1, -*t1, dir, origin);

    if (*t1 > *t2)
    {
        temp = *t1;
        *t1 = *t2;
        *t2 = temp;
    }

    h = HashVec(origin, &num_hashneighbors, hashneighbors);

    for (int i = 0; i < num_hashneighbors; ++i)
        for (w = wedge_hash[hashneighbors[i]]; w; w = w->next)
        {
            if (fabs(w->origin[0] - origin[0]) > EQUAL_EPSILON ||
                fabs(w->origin[1] - origin[1]) > EQUAL_EPSILON ||
                fabs(w->origin[2] - origin[2]) > EQUAL_EPSILON)
            {
                continue;
            }
            if (fabs(w->dir[0] - dir[0]) > NORMAL_EPSILON ||
                fabs(w->dir[1] - dir[1]) > NORMAL_EPSILON ||
                fabs(w->dir[2] - dir[2]) > NORMAL_EPSILON)
            {
                continue;
            }

            return w;
        }

    hlassume(numwedges < MAX_WEDGES, assume_MAX_WEDGES);
    w = &wedges[numwedges];
    numwedges++;

    w->next = wedge_hash[h];
    wedge_hash[h] = w;

    VectorCopy(origin, w->origin);
    VectorCopy(dir, w->dir);
    w->head.next = w->head.prev = &w->head;
    w->head.t = 99999;
    return w;
}

/*
 * ===============
 * AddVert
 *
 * ===============
 */
#define T_EPSILON ON_EPSILON

static void AddVert(const wedge_t *const w, const vec_t t)
{
    wvert_t *v;
    wvert_t *newv;

    v = w->head.next;
    do
    {
        if (fabs(v->t - t) < T_EPSILON)
        {
            return;
        }
        if (v->t > t)
        {
            break;
        }
        v = v->next;
    } while (true);

    // insert a new wvert before v
    hlassume(numwverts < MAX_WVERTS, assume_MAX_WVERTS);

    newv = &wverts[numwverts];
    numwverts++;

    newv->t = t;
    newv->next = v;
    newv->prev = v->prev;
    v->prev->next = newv;
    v->prev = newv;
}

/*
 * ===============
 * AddEdge
 * ===============
 */
static void AddEdge(const vec3_t p1, const vec3_t p2)
{
    wedge_t *w;
    vec_t t1;
    vec_t t2;

    w = FindEdge(p1, p2, &t1, &t2);
    AddVert(w, t1);
    AddVert(w, t2);
}

/*
 * ===============
 * AddFaceEdges
 *
 * ===============
 */
static void AddFaceEdges(const face_t *const f)
{
    int i, j;

    for (i = 0; i < f->numpoints; i++)
    {
        j = (i + 1) % f->numpoints;
        AddEdge(f->pts[i], f->pts[j]);
    }
}

//============================================================================

static byte superfacebuf[1024 * 16];
static face_t *superface = (face_t *)superfacebuf;
static int MAX_SUPERFACEEDGES = (sizeof(superfacebuf) - sizeof(face_t) + sizeof(superface->pts)) / sizeof(vec3_t);
static face_t *newlist;

static void SplitFaceForTjunc(face_t *f, face_t *original)
{
    int i;
    face_t *newface;
    face_t *chain;
    vec3_t dir, test;
    vec_t v;
    int firstcorner, lastcorner;

#ifdef _DEBUG
    static int counter = 0;

    Log("SplitFaceForTjunc %d\n", counter++);
#endif

    chain = nullptr;
    do
    {
        hlassume(f->original == nullptr, assume_ValidPointer); // "SplitFaceForTjunc: f->original"

        if (f->numpoints <= MAXPOINTS)
        { // the face is now small enough without more cutting
            // so copy it back to the original
            *original = *f;
            original->original = chain;
            original->next = newlist;
            newlist = original;
            return;
        }

        tjuncfaces++;

    restart:
        // find the last corner
        VectorSubtract(f->pts[f->numpoints - 1], f->pts[0], dir);
        VectorNormalize(dir);
        for (lastcorner = f->numpoints - 1; lastcorner > 0; lastcorner--)
        {
            VectorSubtract(f->pts[lastcorner - 1], f->pts[lastcorner], test);
            VectorNormalize(test);
            v = DotProduct(test, dir);
            if (v < 1.0 - ON_EPSILON || v > 1.0 + ON_EPSILON)
            {
                break;
            }
        }

        // find the first corner
        VectorSubtract(f->pts[1], f->pts[0], dir);
        VectorNormalize(dir);
        for (firstcorner = 1; firstcorner < f->numpoints - 1; firstcorner++)
        {
            VectorSubtract(f->pts[firstcorner + 1], f->pts[firstcorner], test);
            VectorNormalize(test);
            v = DotProduct(test, dir);
            if (v < 1.0 - ON_EPSILON || v > 1.0 + ON_EPSILON)
            {
                break;
            }
        }

        if (firstcorner + 2 >= MAXPOINTS)
        {
            // rotate the point winding
            VectorCopy(f->pts[0], test);
            for (i = 1; i < f->numpoints; i++)
            {
                VectorCopy(f->pts[i], f->pts[i - 1]);
            }
            VectorCopy(test, f->pts[f->numpoints - 1]);
            goto restart;
        }

        // cut off as big a piece as possible, less than MAXPOINTS, and not
        // past lastcorner

        newface = NewFaceFromFace(f);

        hlassume(f->original == nullptr, assume_ValidPointer); // "SplitFaceForTjunc: f->original"

        newface->original = chain;
        chain = newface;
        newface->next = newlist;
        newlist = newface;
        if (f->numpoints - firstcorner <= MAXPOINTS)
        {
            newface->numpoints = firstcorner + 2;
        }
        else if (lastcorner + 2 < MAXPOINTS && f->numpoints - lastcorner <= MAXPOINTS)
        {
            newface->numpoints = lastcorner + 2;
        }
        else
        {
            newface->numpoints = MAXPOINTS;
        }

        for (i = 0; i < newface->numpoints; i++)
        {
            VectorCopy(f->pts[i], newface->pts[i]);
        }

        for (i = newface->numpoints - 1; i < f->numpoints; i++)
        {
            VectorCopy(f->pts[i], f->pts[i - (newface->numpoints - 2)]);
        }
        f->numpoints -= (newface->numpoints - 2);
    } while (true);
}

/*
 * ===============
 * FixFaceEdges
 *
 * ===============
 */
static void FixFaceEdges(face_t *f)
{
    int i;
    int j;
    int k;
    wedge_t *w;
    wvert_t *v;
    vec_t t1;
    vec_t t2;

    *superface = *f;

restart:
    for (i = 0; i < superface->numpoints; i++)
    {
        j = (i + 1) % superface->numpoints;

        w = FindEdge(superface->pts[i], superface->pts[j], &t1, &t2);

        for (v = w->head.next; v->t < t1 + T_EPSILON; v = v->next)
        {
        }

        if (v->t < t2 - T_EPSILON)
        {
            tjuncs++;
            // insert a new vertex here
            for (k = superface->numpoints; k > j; k--)
            {
                VectorCopy(superface->pts[k - 1], superface->pts[k]);
            }
            VectorMA(w->origin, v->t, w->dir, superface->pts[j]);
            superface->numpoints++;
            hlassume(superface->numpoints < MAX_SUPERFACEEDGES, assume_MAX_SUPERFACEEDGES);
            goto restart;
        }
    }

    if (superface->numpoints <= MAXPOINTS)
    {
        *f = *superface;
        f->next = newlist;
        newlist = f;
        return;
    }

    // the face needs to be split into multiple faces because of too many edges

    SplitFaceForTjunc(superface, f);
}

//============================================================================

static void tjunc_find_r(node_t *node)
{
    face_t *f;

    if (node->planenum == PLANENUM_LEAF)
    {
        return;
    }

    for (f = node->faces; f; f = f->next)
    {
        AddFaceEdges(f);
    }

    tjunc_find_r(node->children[0]);
    tjunc_find_r(node->children[1]);
}

static void tjunc_fix_r(node_t *node)
{
    face_t *f;
    face_t *next;

    if (node->planenum == PLANENUM_LEAF)
    {
        return;
    }

    newlist = nullptr;

    for (f = node->faces; f; f = next)
    {
        next = f->next;
        FixFaceEdges(f);
    }

    node->faces = newlist;

    tjunc_fix_r(node->children[0]);
    tjunc_fix_r(node->children[1]);
}

/*
 * ===========
 * tjunc
 *
 * ===========
 */
void tjunc(node_t *headnode)
{
    vec3_t maxs, mins;
    int i;
    //
    // identify all points on common edges
    //

    // origin points won't allways be inside the map, so extend the hash area
    for (i = 0; i < 3; i++)
    {
        if (fabs(headnode->maxs[i]) > fabs(headnode->mins[i]))
        {
            maxs[i] = fabs(headnode->maxs[i]);
        }
        else
        {
            maxs[i] = fabs(headnode->mins[i]);
        }
    }
    VectorSubtract(vec3_origin, maxs, mins);

    InitHash(mins, maxs);

    numwedges = numwverts = 0;

    tjunc_find_r(headnode);

    //
    // add extra vertexes on edges where needed
    //
    tjuncs = tjuncfaces = 0;

    tjunc_fix_r(headnode);
}
