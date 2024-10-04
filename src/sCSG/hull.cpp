#include <cstring>

#include "hull.h"
#include "threads.h"

HullShape g_defaulthulls[NUM_HULLS];
int g_numhullshapes;
HullShape g_hullshapes[MAX_HULLSHAPES];

void InitDefaultHulls()
{
    for (int h = 0; h < NUM_HULLS; h++)
    {
        HullShape *hs = &g_defaulthulls[h];
        hs->id = strdup("");
        hs->disabled = true;
        hs->numbrushes = 0;
        hs->brushes = new HullBrush *[0];
        hlassume(hs->brushes != nullptr, assume_NoMemory);
    }
}

void DeleteHullBrush(HullBrush *hb)
{
    for (HullBrushFace *hbf = hb->faces; hbf < hb->faces + hb->numfaces; hbf++)
    {
        if (hbf->vertexes)
        {
            delete[] hbf->vertexes;
        }
    }
    delete[] hb->faces;
    delete[] hb->edges;
    delete[] hb->vertexes;
    delete hb;
}

auto CopyHullBrush(const HullBrush *hb) -> HullBrush *
{
    auto *hb2 = new HullBrush;
    hlassume(hb2 != nullptr, assume_NoMemory);
    memcpy(hb2, hb, sizeof(HullBrush));
    hb2->faces = new HullBrushFace[hb->numfaces];
    hlassume(hb2->faces != nullptr, assume_NoMemory);
    memcpy(hb2->faces, hb->faces, hb->numfaces * sizeof(HullBrushFace));
    hb2->edges = new HullBrushEdge[hb->numedges];
    hlassume(hb2->edges != nullptr, assume_NoMemory);
    memcpy(hb2->edges, hb->edges, hb->numedges * sizeof(HullBrushEdge));
    hb2->vertexes = new HullBrushVertex[hb->numvertexes];
    hlassume(hb2->vertexes != nullptr, assume_NoMemory);
    memcpy(hb2->vertexes, hb->vertexes, hb->numvertexes * sizeof(HullBrushVertex));
    for (int i = 0; i < hb->numfaces; i++)
    {
        auto *f2 = &hb2->faces[i];
        const auto *f = &hb->faces[i];
        f2->vertexes = new vec3_t[f->numvertexes];
        hlassume(f2->vertexes != nullptr, assume_NoMemory);
        memcpy(f2->vertexes, f->vertexes, f->numvertexes * sizeof(vec3_t));
    }
    return hb2;
}