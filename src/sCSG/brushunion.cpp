#include "csg.h"
#include "blockmem.h"
#include "hlassert.h"

static auto NewWindingFromPlane(const brushhull_t *const hull, const int planenum) -> Winding *
{
    Winding *winding;
    Winding *front;
    Winding *back;
    bface_t *face;
    Plane *plane;

    plane = &g_mapplanes[planenum];
    winding = new Winding(plane->normal, plane->dist);

    for (face = hull->faces; face; face = face->next)
    {
        plane = &g_mapplanes[face->planenum];
        winding->Clip(plane->normal, plane->dist, &front, &back);
        delete winding;

        if (front)
        {
            delete front;
        }
        if (back)
        {
            winding = back;
        }
        else
        {
            return nullptr;
        }
    }

    return winding;
}

static void AddFaceToList(bface_t **head, bface_t *newface)
{
    hlassert(newface);
    hlassert(newface->w);
    if (!*head)
    {
        *head = newface;
        return;
    }
    else
    {
        bface_t *node = *head;

        while (node->next)
        {
            node = node->next;
        }
        node->next = newface;
        newface->next = nullptr;
    }
}

static auto NumberOfHullFaces(const brushhull_t *const hull) -> int
{
    int x;
    bface_t *face;

    if (!hull->faces)
    {
        return 0;
    }

    for (x = 0, face = hull->faces; face; face = face->next, x++)
    { // counter
    }

    return x;
}

// Returns false if union of brushes is obviously zero
static void AddPlaneToUnion(brushhull_t *hull, const int planenum)
{
    bool need_new_face = false;

    bface_t *new_face_list;

    bface_t *face;
    bface_t *next;

    Plane *split;
    Winding *front;
    Winding *back;

    new_face_list = nullptr;

    next = nullptr;

    hlassert(hull);

    if (!hull->faces)
    {
        return;
    }
    hlassert(hull->faces->w);

    for (face = hull->faces; face; face = next)
    {
        hlassert(face->w);
        next = face->next;

        // Duplicate plane, ignore
        if (face->planenum == planenum)
        {
            AddFaceToList(&new_face_list, CopyFace(face));
            continue;
        }

        split = &g_mapplanes[planenum];
        face->w->Clip(split->normal, split->dist, &front, &back);

        if (front)
        {
            delete front;
            need_new_face = true;

            if (back)
            { // Intersected the face
                delete face->w;
                face->w = back;
                AddFaceToList(&new_face_list, CopyFace(face));
            }
        }
        else
        {
            // Completely missed it, back is identical to face->w so it is destroyed
            if (back)
            {
                delete back;
                AddFaceToList(&new_face_list, CopyFace(face));
            }
        }
        hlassert(face->w);
    }

    FreeFaceList(hull->faces);
    hull->faces = new_face_list;

    if (need_new_face && (NumberOfHullFaces(hull) > 2))
    {
        Winding *new_winding = NewWindingFromPlane(hull, planenum);

        if (new_winding)
        {
            auto *new_face = (bface_t *)Alloc(sizeof(bface_t));

            new_face->planenum = planenum;
            new_face->w = new_winding;

            new_face->next = hull->faces;
            hull->faces = new_face;
        }
    }
}

static auto CalculateSolidVolume(const brushhull_t *const hull) -> vec_t
{
    // calculate polyhedron origin
    // subdivide face winding into triangles

    // for each face
    // calculate volume of triangle of face to origin
    // add subidivided volume chunk to total

    int x = 0;
    vec_t volume = 0.0;
    vec_t inverse;
    vec3_t midpoint = {0.0, 0.0, 0.0};

    bface_t *face;

    for (face = hull->faces; face; face = face->next, x++)
    {
        vec3_t facemid;

        face->w->getCenter(facemid);
        VectorAdd(midpoint, facemid, midpoint);
    }

    inverse = 1.0 / x;

    VectorScale(midpoint, inverse, midpoint);

    for (face = hull->faces; face; face = face->next, x++)
    {
        Plane *plane = &g_mapplanes[face->planenum];
        vec_t area = face->w->getArea();
        vec_t dist = DotProduct(plane->normal, midpoint);

        dist -= plane->dist;
        dist = fabs(dist);

        volume += area * dist / 3.0;
    }

    return volume;
}

static void DumpHullWindings(const brushhull_t *const hull)
{
    int x = 0;
    bface_t *face;

    for (face = hull->faces; face; face = face->next)
    {
        face->w->Print();
    }
}

static auto isInvalidHull(const brushhull_t *const hull) -> bool
{
    int x = 0;
    bface_t *face;

    vec3_t mins = {99999.0, 99999.0, 99999.0};
    vec3_t maxs = {-99999.0, -99999.0, -99999.0};

    for (face = hull->faces; face; face = face->next)
    {
        unsigned int y;
        Winding *winding = face->w;

        for (y = 0; y < winding->m_NumPoints; y++)
        {
            VectorCompareMinimum(mins, winding->m_Points[y], mins);
            VectorCompareMaximum(maxs, winding->m_Points[y], maxs);
        }
    }

    for (x = 0; x < 3; x++)
    {
        if ((mins[x] < (-BOGUS_RANGE / 2)) || (maxs[x] > (BOGUS_RANGE / 2)))
        {
            return true;
        }
    }
    return false;
}