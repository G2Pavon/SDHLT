#include <cstring>

#include "hlbsp.h"
#include "log.h"
#include "blockmem.h"
#include "filelib.h"

static int outleafs;
static int valid;
static int c_falsenodes;
static int c_free_faces;
static int c_keep_faces;

// =====================================================================================
//  PointInLeaf
// =====================================================================================
static auto PointInLeaf(NodeBSP *node, const vec3_t point) -> NodeBSP *
{
    vec_t d;

    if (node->isportalleaf)
    {
        // Log("PointInLeaf::node->contents == %i\n", node->contents);
        return node;
    }

    d = DotProduct(g_mapplanes[node->planenum].normal, point) - g_mapplanes[node->planenum].dist;

    if (d > 0)
        return PointInLeaf(node->children[0], point);

    return PointInLeaf(node->children[1], point);
}

// =====================================================================================
//  PlaceOccupant
// =====================================================================================
static auto PlaceOccupant(const int num, const vec3_t point, NodeBSP *headnode) -> bool
{
    NodeBSP *n;

    n = PointInLeaf(headnode, point);
    if (n->contents == static_cast<int>(contents_t::CONTENTS_SOLID))
    {
        return false;
    }
    // Log("PlaceOccupant::n->contents == %i\n", n->contents);

    n->occupied = num;
    return true;
}

// =====================================================================================
//  MarkLeakTrail
// =====================================================================================
static PortalBSP *prevleaknode;
static FILE *pointfile;
static FILE *linefile;

static void MarkLeakTrail(PortalBSP *n2)
{
    int i;
    vec3_t p1, p2, dir;
    float len;
    PortalBSP *n1;

    n1 = prevleaknode;
    prevleaknode = n2;

    if (!n1)
    {
        return;
    }

    n1->winding->getCenter(p1);
    n2->winding->getCenter(p2);

    // Linefile
    fprintf(linefile, "%f %f %f - %f %f %f\n", p1[0], p1[1], p1[2], p2[0], p2[1], p2[2]);

    // Pointfile
    fprintf(pointfile, "%f %f %f\n", p1[0], p1[1], p1[2]);

    VectorSubtract(p2, p1, dir);
    len = VectorLength(dir);
    VectorNormalize(dir);

    while (len > 2)
    {
        fprintf(pointfile, "%f %f %f\n", p1[0], p1[1], p1[2]);
        for (i = 0; i < 3; i++)
            p1[i] += dir[i] * 2;
        len -= 2;
    }
}

// =====================================================================================
//  RecursiveFillOutside
//      Returns true if an occupied leaf is reached
//      If fill is false, just check, don't fill
// =====================================================================================
static void FreeDetailNode_r(NodeBSP *n)
{
    int i;
    if (n->planenum == -1)
    {
        if (!(n->isportalleaf && n->contents == static_cast<int>(contents_t::CONTENTS_SOLID)))
        {
            delete[] n->markfaces;
            n->markfaces = nullptr;
        }
        return;
    }
    for (i = 0; i < 2; i++)
    {
        FreeDetailNode_r(n->children[i]);
        delete n->children[i];
        n->children[i] = nullptr;
    }
    FaceBSP *f, *next;
    for (f = n->faces; f; f = next)
    {
        next = f->next;
        delete f;
    }
    n->faces = nullptr;
}
static void FillLeaf(NodeBSP *l)
{
    if (!l->isportalleaf)
    {
        Warning("FillLeaf: not leaf");
        return;
    }
    if (l->contents == static_cast<int>(contents_t::CONTENTS_SOLID))
    {
        Warning("FillLeaf: fill solid");
        return;
    }
    FreeDetailNode_r(l);
    l->contents = contents_t::CONTENTS_SOLID;
    l->planenum = -1;
}
static int hit_occupied;
static int backdraw;
static auto RecursiveFillOutside(NodeBSP *l, const bool fill) -> bool
{
    PortalBSP *p;
    int s;

    if ((l->contents == static_cast<int>(contents_t::CONTENTS_SOLID)) || (l->contents == CONTENTS_SKY))
    {
        /*if (l->contents != static_cast<int>(contents_t::CONTENTS_SOLID))
            Log("RecursiveFillOutside::l->contents == %i \n", l->contents);*/

        return false;
    }

    if (l->valid == valid)
    {
        return false;
    }

    if (l->occupied)
    {
        hit_occupied = l->occupied;
        backdraw = 1000;
        return true;
    }

    l->valid = valid;

    // fill it and it's neighbors
    if (fill)
    {
        FillLeaf(l);
    }
    outleafs++;

    for (p = l->portals; p;)
    {
        s = (p->nodes[0] == l);

        if (RecursiveFillOutside(p->nodes[s], fill))
        { // leaked, so stop filling
            if (backdraw-- > 0)
            {
                MarkLeakTrail(p);
            }
            return true;
        }
        p = p->next[!s];
    }

    return false;
}

// =====================================================================================
//  ClearOutFaces_r
//      Removes unused nodes
// =====================================================================================
static void MarkFacesInside_r(NodeBSP *node)
{
    if (node->planenum == -1)
    {
        FaceBSP **fp;
        for (fp = node->markfaces; *fp; fp++)
        {
            (*fp)->outputnumber = 0;
        }
    }
    else
    {
        MarkFacesInside_r(node->children[0]);
        MarkFacesInside_r(node->children[1]);
    }
}
static auto ClearOutFaces_r(NodeBSP *node) -> NodeBSP *
{
    FaceBSP *f;
    FaceBSP *fnext;
    PortalBSP *p;

    // mark the node and all it's faces, so they
    // can be removed if no children use them

    node->valid = 0; // will be set if any children touch it
    for (f = node->faces; f; f = f->next)
    {
        f->outputnumber = -1;
    }

    // go down the children
    if (!node->isportalleaf)
    {
        //
        // decision node
        //
        node->children[0] = ClearOutFaces_r(node->children[0]);
        node->children[1] = ClearOutFaces_r(node->children[1]);

        // free any faces not in open child leafs
        f = node->faces;
        node->faces = nullptr;

        for (; f; f = fnext)
        {
            fnext = f->next;
            if (f->outputnumber == -1)
            { // never referenced, so free it
                c_free_faces++;
                delete f;
            }
            else
            {
                c_keep_faces++;
                f->next = node->faces;
                node->faces = f;
            }
        }

        if (!node->valid)
        {
            // Here leaks memory. --vluzacn
            // this node does not touch any interior leafs

            // if both children are solid, just make this node solid
            if (node->children[0]->contents == static_cast<int>(contents_t::CONTENTS_SOLID) && node->children[1]->contents == static_cast<int>(contents_t::CONTENTS_SOLID))
            {
                node->contents = contents_t::CONTENTS_SOLID;
                node->planenum = -1;
                node->isportalleaf = true;
                return node;
            }

            // if one child is solid, shortcut down the other side
            if (node->children[0]->contents == static_cast<int>(contents_t::CONTENTS_SOLID))
            {
                return node->children[1];
            }
            if (node->children[1]->contents == static_cast<int>(contents_t::CONTENTS_SOLID))
            {
                return node->children[0];
            }

            c_falsenodes++;
        }
        return node;
    }

    //
    // leaf node
    //
    if (node->contents != static_cast<int>(contents_t::CONTENTS_SOLID))
    {
        // this node is still inside

        // mark all the nodes used as portals
        for (p = node->portals; p;)
        {
            if (p->onnode)
            {
                p->onnode->valid = 1;
            }
            if (p->nodes[0] == node) // only write out from first leaf
            {
                p = p->next[0];
            }
            else
            {
                p = p->next[1];
            }
        }

        MarkFacesInside_r(node);

        return node;
    }

    return node;
}

// =====================================================================================
//  isClassnameAllowableOutside
// =====================================================================================
#define MAX_ALLOWABLE_OUTSIDE_GROWTH_SIZE 64

unsigned g_nAllowableOutside = 0;
unsigned g_maxAllowableOutside = 0;
char **g_strAllowableOutsideList;

auto isClassnameAllowableOutside(const char *const classname) -> bool
{
    if (g_strAllowableOutsideList)
    {
        unsigned x;
        char **list = g_strAllowableOutsideList;

        for (x = 0; x < g_nAllowableOutside; x++, list++)
        {
            if (list)
            {
                if (!strcasecmp(classname, *list))
                {
                    return true;
                }
            }
        }
    }

    return false;
}

// =====================================================================================
//  FreeAllowableOutsideList
// =====================================================================================
void FreeAllowableOutsideList()
{
    if (g_strAllowableOutsideList)
    {
        delete[] g_strAllowableOutsideList;
        g_strAllowableOutsideList = nullptr;
    }
}

// =====================================================================================
//  LoadAllowableOutsideList
// =====================================================================================
void LoadAllowableOutsideList(const char *const filename)
{
    char *fname;
    int i, x, y;
    char *pData;
    char *pszData;

    if (!filename)
    {
        return;
    }
    else
    {
        unsigned len = strlen(filename) + 5;

        fname = (char *)Alloc(len);
        safe_snprintf(fname, len, "%s", filename);
    }

    if (q_exists(fname))
    {
        if ((i = LoadFile(fname, &pData)))
        {
            Log("Reading allowable void entities from file '%s'\n", fname);
            g_nAllowableOutside = 0;
            for (pszData = pData, y = 0, x = 0; x < i; x++)
            {
                if ((pData[x] == '\n') || (pData[x] == '\r'))
                {
                    pData[x] = 0;
                    if (strlen(pszData))
                    {
                        if (g_nAllowableOutside == g_maxAllowableOutside)
                        {
                            g_maxAllowableOutside += MAX_ALLOWABLE_OUTSIDE_GROWTH_SIZE;
                            g_strAllowableOutsideList =

                                (char **)realloc(g_strAllowableOutsideList, sizeof(char *) * g_maxAllowableOutside);
                        }

                        g_strAllowableOutsideList[y++] = pszData;
                        g_nAllowableOutside++;
                    }
                    pszData = pData + x + 1;
                }
            }
        }
    }
}

// =====================================================================================
//  FillOutside
// =====================================================================================
auto FillOutside(NodeBSP *node, const bool leakfile, const unsigned hullnum) -> NodeBSP *
{
    int s;
    int i;
    bool inside;
    bool ret;
    vec3_t origin;
    const char *cl;

    if (hullnum == 2 && g_nohull2)
        return node;

    //
    // place markers for all entities so
    // we know if we leak inside
    //
    inside = false;
    for (i = 1; i < g_numentities; i++)
    {
        GetVectorForKey(&g_entities[i], "origin", origin);
        cl = ValueForKey(&g_entities[i], "classname");
        if (!isClassnameAllowableOutside(cl))
        {
            /*if (!VectorCompare(origin, vec3_origin))
             */
            if (*ValueForKey(&g_entities[i], "origin")) //--vluzacn
            {
                origin[2] += 1; // so objects on floor are ok

                // nudge playerstart around if needed so clipping hulls allways
                // have a vlaid point
                if (!strcmp(cl, "info_player_start"))
                {
                    int x, y;

                    for (x = -16; x <= 16; x += 16)
                    {
                        for (y = -16; y <= 16; y += 16)
                        {
                            origin[0] += x;
                            origin[1] += y;
                            if (PlaceOccupant(i, origin, node))
                            {
                                inside = true;
                                goto gotit;
                            }
                            origin[0] -= x;
                            origin[1] -= y;
                        }
                    }
                gotit:;
                }
                else
                {
                    if (PlaceOccupant(i, origin, node))
                        inside = true;
                }
            }
        }
    }

    if (!inside)
    {
        Warning("No entities exist in hull %i, no filling performed for this hull", hullnum);
        return node;
    }

    if (!g_outside_node.portals)
    {
        Warning("No outside node portal found in hull %i, no filling performed for this hull", hullnum);
        return node;
    }

    s = !(g_outside_node.portals->nodes[1] == &g_outside_node);

    // first check to see if an occupied leaf is hit
    outleafs = 0;
    valid++;

    prevleaknode = nullptr;

    if (leakfile)
    {
        pointfile = fopen(g_pointfilename, "w");
        if (!pointfile)
        {
            Error("Couldn't open pointfile %s\n", g_pointfilename);
        }

        linefile = fopen(g_linefilename, "w");
        if (!linefile)
        {
            Error("Couldn't open linefile %s\n", g_linefilename);
        }
    }

    ret = RecursiveFillOutside(g_outside_node.portals->nodes[s], false);

    if (leakfile)
    {
        fclose(pointfile);
        fclose(linefile);
    }

    if (ret)
    {
        GetVectorForKey(&g_entities[hit_occupied], "origin", origin);

        {
            Warning("=== LEAK in hull %i ===\nEntity %s @ (%4.0f,%4.0f,%4.0f)",
                    hullnum, ValueForKey(&g_entities[hit_occupied], "classname"), origin[0], origin[1], origin[2]);
            PrintOnce(
                "\n  A LEAK is a hole in the map, where the inside of it is exposed to the\n"
                "(unwanted) outside region.  The entity listed in the error is just a helpful\n"
                "indication of where the beginning of the leak pointfile starts, so the\n"
                "beginning of the line can be quickly found and traced to until reaching the\n"
                "outside. Unless this entity is accidentally on the outside of the map, it\n"
                "probably should not be deleted.  Some complex rotating objects entities need\n"
                "their origins outside the map.  To deal with these, just enclose the origin\n"
                "brush with a solid world brush\n");
        }

        if (!g_bLeaked)
        {
            // First leak spits this out
            Log("Leak pointfile generated\n\n");
        }

        if (g_bLeakOnly)
        {
            Error("Stopped by leak.");
        }

        g_bLeaked = true;

        return node;
    }
    if (leakfile && !ret)
    {
        unlink(g_linefilename);
        unlink(g_pointfilename);
    }

    // now go back and fill things in
    valid++;
    RecursiveFillOutside(g_outside_node.portals->nodes[s], true);

    // remove faces and nodes from filled in leafs
    c_falsenodes = 0;
    c_free_faces = 0;
    c_keep_faces = 0;
    node = ClearOutFaces_r(node);

    // save portal file for vis tracing
    if ((hullnum == 0) && leakfile)
    {
        WritePortalfile(node);
    }

    return node;
}

void ResetMark_r(NodeBSP *node)
{
    if (node->isportalleaf)
    {
        if (node->contents == static_cast<int>(contents_t::CONTENTS_SOLID) || node->contents == CONTENTS_SKY)
        {
            node->empty = 0;
        }
        else
        {
            node->empty = 1;
        }
    }
    else
    {
        ResetMark_r(node->children[0]);
        ResetMark_r(node->children[1]);
    }
}
void MarkOccupied_r(NodeBSP *node)
{
    if (node->empty == 1)
    {
        node->empty = 0;
        PortalBSP *p;
        int s;
        for (p = node->portals; p; p = p->next[!s])
        {
            s = (p->nodes[0] == node);
            MarkOccupied_r(p->nodes[s]);
        }
    }
}
void RemoveUnused_r(NodeBSP *node)
{
    if (node->isportalleaf)
    {
        if (node->empty == 1)
        {
            FillLeaf(node);
        }
    }
    else
    {
        RemoveUnused_r(node->children[0]);
        RemoveUnused_r(node->children[1]);
    }
}
void FillInside(NodeBSP *node)
{
    int i;
    g_outside_node.empty = 0;
    ResetMark_r(node);
    for (i = 1; i < g_numentities; i++)
    {
        if (*ValueForKey(&g_entities[i], "origin"))
        {
            vec3_t origin;
            NodeBSP *innode;
            GetVectorForKey(&g_entities[i], "origin", origin);
            origin[2] += 1;
            innode = PointInLeaf(node, origin);
            MarkOccupied_r(innode);
            origin[2] -= 2;
            innode = PointInLeaf(node, origin);
            MarkOccupied_r(innode);
        }
    }
    RemoveUnused_r(node);
}
