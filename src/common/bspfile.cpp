#include <cstring>
#include <cerrno>

#include "filelib.h"
#include "messages.h"
#include "hlassert.h"
#include "log.h"
#include "mathlib.h"
#include "bspfile.h"
#include "maplib.h"
#include "blockmem.h"

//=============================================================================

int g_max_map_miptex = DEFAULT_MAX_MAP_MIPTEX;
int g_max_map_lightdata = DEFAULT_MAX_MAP_LIGHTDATA;

int g_bspnummodels;
BSPLumpModel g_bspmodels[MAX_MAP_MODELS];
int g_bspmodels_checksum;

int g_bspvisdatasize;
byte g_bspvisdata[MAX_MAP_VISIBILITY];
int g_bspvisdata_checksum;

int g_bsplightdatasize;
byte *g_bsplightdata;
int g_bsplightdata_checksum;

int g_bsptexdatasize;
byte *g_bsptexdata; // (BSPLumpMiptexHeader)
int g_bsptexdata_checksum;

int g_bspentdatasize;
char g_bspentdata[MAX_MAP_ENTSTRING];
int g_bspentdata_checksum;

int g_bspnumleafs;
BSPLumpLeaf g_bspleafs[MAX_MAP_LEAFS];
int g_bspleafs_checksum;

int g_bspnumplanes;
dplane_t g_dplanes[MAX_INTERNAL_MAP_PLANES];
int g_bspplanes_checksum;

int g_bspnumvertexes;
BSPLumpVertex g_bspvertexes[MAX_MAP_VERTS];
int g_bspvertexes_checksum;

int g_bspnumnodes;
BSPLumpNode g_bspnodes[MAX_MAP_NODES];
int g_bspnodes_checksum;

int g_bspnumtexinfo;

BSPLumpTexInfo g_bsptexinfo[MAX_INTERNAL_MAP_TEXINFO];
int g_bsptexinfo_checksum;

int g_bspnumfaces;
BSPLumpFace g_bspfaces[MAX_MAP_FACES];
int g_bspfaces_checksum;

int g_iWorldExtent = 65536; // ENGINE_ENTITY_RANGE; // -worldextent // seedee

int g_bspnumclipnodes;
BSPLumpClipnode g_bspclipnodes[MAX_MAP_CLIPNODES];
int g_bspclipnodes_checksum;

int g_bspnumedges;
BSPLumpEdge g_bspedges[MAX_MAP_EDGES];
int g_bspedges_checksum;

int g_bspnummarksurfaces;
unsigned short g_bspmarksurfaces[MAX_MAP_MARKSURFACES];
int g_bspmarksurfaces_checksum;

int g_bspnumsurfedges;
int g_bspsurfedges[MAX_MAP_SURFEDGES];
int g_bspsurfedges_checksum;

int g_numentities;
Entity g_entities[MAX_MAP_ENTITIES];

/*
 * ===============
 * FastChecksum
 * ===============
 */

static auto FastChecksum(const void *const buffer, int bytes) -> int
{
	int checksum = 0;
	char *buf = (char *)buffer;

	while (bytes--)
	{
		checksum = rotl(checksum, 4) ^ (*buf);
		buf++;
	}

	return checksum;
}

/*
 * ===============
 * CompressVis
 * ===============
 */
auto CompressVis(const byte *const src, const unsigned int src_length, byte *dest, unsigned int dest_length) -> int
{
	unsigned int j;
	byte *dest_p = dest;
	unsigned int current_length = 0;

	for (j = 0; j < src_length; j++)
	{
		current_length++;
		hlassume(current_length <= dest_length, assume_COMPRESSVIS_OVERFLOW);

		*dest_p = src[j];
		dest_p++;

		if (src[j])
		{
			continue;
		}

		unsigned char rep = 1;

		for (j++; j < src_length; j++)
		{
			if (src[j] || rep == 255)
			{
				break;
			}
			else
			{
				rep++;
			}
		}
		current_length++;
		hlassume(current_length <= dest_length, assume_COMPRESSVIS_OVERFLOW);

		*dest_p = rep;
		dest_p++;
		j--;
	}

	return dest_p - dest;
}

// =====================================================================================
//  DecompressVis
//
// =====================================================================================
void DecompressVis(const byte *src, byte *const dest, const unsigned int dest_length)
{
	unsigned int current_length = 0;
	int c;
	byte *out;
	int row;

	row = (g_bspmodels[0].visleafs + 7) >> 3; // same as the length used by VIS program in CompressVis
											// The wrong size will cause DecompressVis to spend extremely long time once the source pointer runs into the invalid area in g_bspvisdata (for example, in BuildFaceLights, some faces could hang for a few seconds), and sometimes to crash.

	out = dest;

	do
	{
		hlassume(src - g_bspvisdata < g_bspvisdatasize, assume_DECOMPRESSVIS_OVERFLOW);

		if (*src)
		{
			current_length++;
			hlassume(current_length <= dest_length, assume_DECOMPRESSVIS_OVERFLOW);

			*out = *src;
			out++;
			src++;
			continue;
		}

		hlassume(&src[1] - g_bspvisdata < g_bspvisdatasize, assume_DECOMPRESSVIS_OVERFLOW);

		c = src[1];
		src += 2;
		while (c)
		{
			current_length++;
			hlassume(current_length <= dest_length, assume_DECOMPRESSVIS_OVERFLOW);

			*out = 0;
			out++;
			c--;

			if (out - dest >= row)
			{
				return;
			}
		}
	} while (out - dest < row);
}

//
// =====================================================================================
//

// =====================================================================================
//  SwapBSPFile
//      byte swaps all data in a bsp file
// =====================================================================================
static void SwapBSPFile(const bool todisk)
{
	int i, j, c;
	BSPLumpModel *d;
	BSPLumpMiptexHeader *mtl;

	// models
	for (i = 0; i < g_bspnummodels; i++)
	{
		d = &g_bspmodels[i];

		for (j = 0; j < MAX_MAP_HULLS; j++)
		{
			d->headnode[j] = LittleLong(d->headnode[j]);
		}

		d->visleafs = LittleLong(d->visleafs);
		d->firstface = LittleLong(d->firstface);
		d->numfaces = LittleLong(d->numfaces);

		for (j = 0; j < 3; j++)
		{
			d->mins[j] = LittleFloat(d->mins[j]);
			d->maxs[j] = LittleFloat(d->maxs[j]);
			d->origin[j] = LittleFloat(d->origin[j]);
		}
	}

	//
	// vertexes
	//
	for (i = 0; i < g_bspnumvertexes; i++)
	{
		for (j = 0; j < 3; j++)
		{
			g_bspvertexes[i].point[j] = LittleFloat(g_bspvertexes[i].point[j]);
		}
	}

	//
	// planes
	//
	for (i = 0; i < g_bspnumplanes; i++)
	{
		for (j = 0; j < 3; j++)
		{
			g_dplanes[i].normal[j] = LittleFloat(g_dplanes[i].normal[j]);
		}
		g_dplanes[i].dist = LittleFloat(g_dplanes[i].dist);
		g_dplanes[i].type = (planetypes)LittleLong(g_dplanes[i].type);
	}

	//
	// texinfos
	//
	for (i = 0; i < g_bspnumtexinfo; i++)
	{
		for (int st = 0; st < 2; st++)
		{
			for (int xyz = 0; xyz < 4; xyz++)
			{
				g_bsptexinfo[i].vecs[st][xyz] = LittleFloat(g_bsptexinfo[i].vecs[st][xyz]);
			}
		}
		g_bsptexinfo[i].miptex = LittleLong(g_bsptexinfo[i].miptex);
		g_bsptexinfo[i].flags = LittleLong(g_bsptexinfo[i].flags);
	}

	//
	// faces
	//
	for (i = 0; i < g_bspnumfaces; i++)
	{
		g_bspfaces[i].texinfo = LittleShort(g_bspfaces[i].texinfo);
		g_bspfaces[i].planenum = LittleShort(g_bspfaces[i].planenum);
		g_bspfaces[i].side = LittleShort(g_bspfaces[i].side);
		g_bspfaces[i].lightofs = LittleLong(g_bspfaces[i].lightofs);
		g_bspfaces[i].firstedge = LittleLong(g_bspfaces[i].firstedge);
		g_bspfaces[i].numedges = LittleShort(g_bspfaces[i].numedges);
	}

	//
	// nodes
	//
	for (i = 0; i < g_bspnumnodes; i++)
	{
		g_bspnodes[i].planenum = LittleLong(g_bspnodes[i].planenum);
		for (j = 0; j < 3; j++)
		{
			g_bspnodes[i].mins[j] = LittleShort(g_bspnodes[i].mins[j]);
			g_bspnodes[i].maxs[j] = LittleShort(g_bspnodes[i].maxs[j]);
		}
		g_bspnodes[i].children[0] = LittleShort(g_bspnodes[i].children[0]);
		g_bspnodes[i].children[1] = LittleShort(g_bspnodes[i].children[1]);
		g_bspnodes[i].firstface = LittleShort(g_bspnodes[i].firstface);
		g_bspnodes[i].numfaces = LittleShort(g_bspnodes[i].numfaces);
	}

	//
	// leafs
	//
	for (i = 0; i < g_bspnumleafs; i++)
	{
		g_bspleafs[i].contents = LittleLong(g_bspleafs[i].contents);
		for (j = 0; j < 3; j++)
		{
			g_bspleafs[i].mins[j] = LittleShort(g_bspleafs[i].mins[j]);
			g_bspleafs[i].maxs[j] = LittleShort(g_bspleafs[i].maxs[j]);
		}

		g_bspleafs[i].firstmarksurface = LittleShort(g_bspleafs[i].firstmarksurface);
		g_bspleafs[i].nummarksurfaces = LittleShort(g_bspleafs[i].nummarksurfaces);
		g_bspleafs[i].visofs = LittleLong(g_bspleafs[i].visofs);
	}

	//
	// clipnodes
	//
	for (i = 0; i < g_bspnumclipnodes; i++)
	{
		g_bspclipnodes[i].planenum = LittleLong(g_bspclipnodes[i].planenum);
		g_bspclipnodes[i].children[0] = LittleShort(g_bspclipnodes[i].children[0]);
		g_bspclipnodes[i].children[1] = LittleShort(g_bspclipnodes[i].children[1]);
	}

	//
	// miptex
	//
	if (g_bsptexdatasize)
	{
		mtl = (BSPLumpMiptexHeader *)g_bsptexdata;
		if (todisk)
		{
			c = mtl->nummiptex;
		}
		else
		{
			c = LittleLong(mtl->nummiptex);
		}
		mtl->nummiptex = LittleLong(mtl->nummiptex);
		for (i = 0; i < c; i++)
		{
			mtl->dataofs[i] = LittleLong(mtl->dataofs[i]);
		}
	}

	//
	// marksurfaces
	//
	for (i = 0; i < g_bspnummarksurfaces; i++)
	{
		g_bspmarksurfaces[i] = LittleShort(g_bspmarksurfaces[i]);
	}

	//
	// surfedges
	//
	for (i = 0; i < g_bspnumsurfedges; i++)
	{
		g_bspsurfedges[i] = LittleLong(g_bspsurfedges[i]);
	}

	//
	// edges
	//
	for (i = 0; i < g_bspnumedges; i++)
	{
		g_bspedges[i].v[0] = LittleShort(g_bspedges[i].v[0]);
		g_bspedges[i].v[1] = LittleShort(g_bspedges[i].v[1]);
	}
}

// =====================================================================================
//  CopyLump
//      balh
// =====================================================================================
static auto CopyLump(int lump, void *dest, int size, const BSPLumpHeader *const header) -> int
{
	int length, ofs;

	length = header->lumps[lump].filelen;
	ofs = header->lumps[lump].fileofs;

	if (length % size)
	{
		Error("LoadBSPFile: odd lump size");
	}

	// special handling for tex and lightdata to keep things from exploding - KGP
	if (lump == LUMP_TEXTURES && dest == (void *)g_bsptexdata)
	{
		hlassume(g_max_map_miptex > length, assume_MAX_MAP_MIPTEX);
	}
	else if (lump == LUMP_LIGHTING && dest == (void *)g_bsplightdata)
	{
		hlassume(g_max_map_lightdata > length, assume_MAX_MAP_LIGHTING);
	}

	memcpy(dest, (byte *)header + ofs, length);

	return length / size;
}

// =====================================================================================
//  LoadBSPFile
//      balh
// =====================================================================================
void LoadBSPFile(const char *const filename)
{
	BSPLumpHeader *header;
	LoadFile(filename, (char **)&header);
	LoadBSPImage(header);
}

// =====================================================================================
//  LoadBSPImage
//      balh
// =====================================================================================
void LoadBSPImage(BSPLumpHeader *const header)
{
	unsigned int i;

	// swap the header
	for (i = 0; i < sizeof(BSPLumpHeader) / 4; i++)
	{
		((int *)header)[i] = LittleLong(((int *)header)[i]);
	}

	if (header->version != BSPVERSION)
	{
		Error("BSP is version %i, not %i", header->version, BSPVERSION);
	}

	g_bspnummodels = CopyLump(LUMP_MODELS, g_bspmodels, sizeof(BSPLumpModel), header);
	g_bspnumvertexes = CopyLump(LUMP_VERTEXES, g_bspvertexes, sizeof(BSPLumpVertex), header);
	g_bspnumplanes = CopyLump(LUMP_PLANES, g_dplanes, sizeof(dplane_t), header);
	g_bspnumleafs = CopyLump(LUMP_LEAFS, g_bspleafs, sizeof(BSPLumpLeaf), header);
	g_bspnumnodes = CopyLump(LUMP_NODES, g_bspnodes, sizeof(BSPLumpNode), header);
	g_bspnumtexinfo = CopyLump(LUMP_TEXINFO, g_bsptexinfo, sizeof(BSPLumpTexInfo), header);
	g_bspnumclipnodes = CopyLump(LUMP_CLIPNODES, g_bspclipnodes, sizeof(BSPLumpClipnode), header);
	g_bspnumfaces = CopyLump(LUMP_FACES, g_bspfaces, sizeof(BSPLumpFace), header);
	g_bspnummarksurfaces = CopyLump(LUMP_MARKSURFACES, g_bspmarksurfaces, sizeof(g_bspmarksurfaces[0]), header);
	g_bspnumsurfedges = CopyLump(LUMP_SURFEDGES, g_bspsurfedges, sizeof(g_bspsurfedges[0]), header);
	g_bspnumedges = CopyLump(LUMP_EDGES, g_bspedges, sizeof(BSPLumpEdge), header);
	g_bsptexdatasize = CopyLump(LUMP_TEXTURES, g_bsptexdata, 1, header);
	g_bspvisdatasize = CopyLump(LUMP_VISIBILITY, g_bspvisdata, 1, header);
	g_bsplightdatasize = CopyLump(LUMP_LIGHTING, g_bsplightdata, 1, header);
	g_bspentdatasize = CopyLump(LUMP_ENTITIES, g_bspentdata, 1, header);

	delete header; // everything has been copied out

	//
	// swap everything
	//
	SwapBSPFile(false);

	g_bspmodels_checksum = FastChecksum(g_bspmodels, g_bspnummodels * sizeof(g_bspmodels[0]));
	g_bspvertexes_checksum = FastChecksum(g_bspvertexes, g_bspnumvertexes * sizeof(g_bspvertexes[0]));
	g_bspplanes_checksum = FastChecksum(g_dplanes, g_bspnumplanes * sizeof(g_dplanes[0]));
	g_bspleafs_checksum = FastChecksum(g_bspleafs, g_bspnumleafs * sizeof(g_bspleafs[0]));
	g_bspnodes_checksum = FastChecksum(g_bspnodes, g_bspnumnodes * sizeof(g_bspnodes[0]));
	g_bsptexinfo_checksum = FastChecksum(g_bsptexinfo, g_bspnumtexinfo * sizeof(g_bsptexinfo[0]));
	g_bspclipnodes_checksum = FastChecksum(g_bspclipnodes, g_bspnumclipnodes * sizeof(g_bspclipnodes[0]));
	g_bspfaces_checksum = FastChecksum(g_bspfaces, g_bspnumfaces * sizeof(g_bspfaces[0]));
	g_bspmarksurfaces_checksum = FastChecksum(g_bspmarksurfaces, g_bspnummarksurfaces * sizeof(g_bspmarksurfaces[0]));
	g_bspsurfedges_checksum = FastChecksum(g_bspsurfedges, g_bspnumsurfedges * sizeof(g_bspsurfedges[0]));
	g_bspedges_checksum = FastChecksum(g_bspedges, g_bspnumedges * sizeof(g_bspedges[0]));
	g_bsptexdata_checksum = FastChecksum(g_bsptexdata, g_bspnumedges * sizeof(g_bsptexdata[0]));
	g_bspvisdata_checksum = FastChecksum(g_bspvisdata, g_bspvisdatasize * sizeof(g_bspvisdata[0]));
	g_bsplightdata_checksum = FastChecksum(g_bsplightdata, g_bsplightdatasize * sizeof(g_bsplightdata[0]));
	g_bspentdata_checksum = FastChecksum(g_bspentdata, g_bspentdatasize * sizeof(g_bspentdata[0]));
}

//
// =====================================================================================
//

// =====================================================================================
//  AddLump
//      balh
// =====================================================================================
static void AddLump(int lumpnum, void *data, int len, BSPLumpHeader *header, FILE *bspfile)
{
	Lump *lump = &header->lumps[lumpnum];
	lump->fileofs = LittleLong(ftell(bspfile));
	lump->filelen = LittleLong(len);
	SafeWrite(bspfile, data, (len + 3) & ~3);
}

// =====================================================================================
//  WriteBSPFile
//      Swaps the bsp file in place, so it should not be referenced again
// =====================================================================================
void WriteBSPFile(const char *const filename)
{
	BSPLumpHeader outheader;
	BSPLumpHeader *header;
	FILE *bspfile;

	header = &outheader;
	memset(header, 0, sizeof(BSPLumpHeader));

	SwapBSPFile(true);

	header->version = LittleLong(BSPVERSION);

	bspfile = SafeOpenWrite(filename);
	SafeWrite(bspfile, header, sizeof(BSPLumpHeader)); // overwritten later

	//      LUMP TYPE       DATA            LENGTH                              HEADER  BSPFILE
	AddLump(LUMP_PLANES, g_dplanes, g_bspnumplanes * sizeof(dplane_t), header, bspfile);
	AddLump(LUMP_LEAFS, g_bspleafs, g_bspnumleafs * sizeof(BSPLumpLeaf), header, bspfile);
	AddLump(LUMP_VERTEXES, g_bspvertexes, g_bspnumvertexes * sizeof(BSPLumpVertex), header, bspfile);
	AddLump(LUMP_NODES, g_bspnodes, g_bspnumnodes * sizeof(BSPLumpNode), header, bspfile);
	AddLump(LUMP_TEXINFO, g_bsptexinfo, g_bspnumtexinfo * sizeof(BSPLumpTexInfo), header, bspfile);
	AddLump(LUMP_FACES, g_bspfaces, g_bspnumfaces * sizeof(BSPLumpFace), header, bspfile);
	AddLump(LUMP_CLIPNODES, g_bspclipnodes, g_bspnumclipnodes * sizeof(BSPLumpClipnode), header, bspfile);

	AddLump(LUMP_MARKSURFACES, g_bspmarksurfaces, g_bspnummarksurfaces * sizeof(g_bspmarksurfaces[0]), header, bspfile);
	AddLump(LUMP_SURFEDGES, g_bspsurfedges, g_bspnumsurfedges * sizeof(g_bspsurfedges[0]), header, bspfile);
	AddLump(LUMP_EDGES, g_bspedges, g_bspnumedges * sizeof(BSPLumpEdge), header, bspfile);
	AddLump(LUMP_MODELS, g_bspmodels, g_bspnummodels * sizeof(BSPLumpModel), header, bspfile);

	AddLump(LUMP_LIGHTING, g_bsplightdata, g_bsplightdatasize, header, bspfile);
	AddLump(LUMP_VISIBILITY, g_bspvisdata, g_bspvisdatasize, header, bspfile);
	AddLump(LUMP_ENTITIES, g_bspentdata, g_bspentdatasize, header, bspfile);
	AddLump(LUMP_TEXTURES, g_bsptexdata, g_bsptexdatasize, header, bspfile);

	fseek(bspfile, 0, SEEK_SET);
	SafeWrite(bspfile, header, sizeof(BSPLumpHeader));

	fclose(bspfile);
}

// =====================================================================================
//  GetFaceExtents (with PLATFORM_CAN_CALC_EXTENT on)
// =====================================================================================

auto CalculatePointVecsProduct(const volatile float *point, const volatile float *vecs) -> float
{
	volatile double val;
	volatile double tmp;

	val = (double)point[0] * (double)vecs[0]; // always do one operation at a time and save to memory
	tmp = (double)point[1] * (double)vecs[1];
	val = val + tmp;
	tmp = (double)point[2] * (double)vecs[2];
	val = val + tmp;
	val = val + (double)vecs[3];

	return (float)val;
}

auto CalcFaceExtents_test() -> bool
{
	const int numtestcases = 6;
	volatile float testcases[numtestcases][8] = {
		{1, 1, 1, 1, 0.375 * DBL_EPSILON, 0.375 * DBL_EPSILON, -1, 0},
		{1, 1, 1, 0.375 * DBL_EPSILON, 0.375 * DBL_EPSILON, 1, -1, DBL_EPSILON},
		{DBL_EPSILON, DBL_EPSILON, 1, 0.375, 0.375, 1, -1, DBL_EPSILON},
		{1, 1, 1, 1, 1, 0.375 * FLT_EPSILON, -2, 0.375 * FLT_EPSILON},
		{1, 1, 1, 1, 0.375 * FLT_EPSILON, 1, -2, 0.375 * FLT_EPSILON},
		{1, 1, 1, 0.375 * FLT_EPSILON, 1, 1, -2, 0.375 * FLT_EPSILON}};
	bool ok;

	// If the test failed, please check:
	//   1. whether the calculation is performed on FPU
	//   2. whether the register precision is too low

	ok = true;
	for (int i = 0; i < 6; i++)
	{
		float val = CalculatePointVecsProduct(&testcases[i][0], &testcases[i][3]);
		if (val != testcases[i][7])
		{
			Warning("internal error: CalcFaceExtents_test failed on case %d (%.20f != %.20f).", i, val, testcases[i][7]);
			ok = false;
		}
	}
	return ok;
}

void GetFaceExtents(int facenum, int mins_out[2], int maxs_out[2])
{
	BSPLumpFace *f;
	float mins[2], maxs[2], val;
	int i, j, e;
	BSPLumpVertex *v;
	BSPLumpTexInfo *tex;
	int bmins[2], bmaxs[2];

	f = &g_bspfaces[facenum];

	mins[0] = mins[1] = 999999;
	maxs[0] = maxs[1] = -99999;

	tex = &g_bsptexinfo[ParseTexinfoForFace(f)];

	for (i = 0; i < f->numedges; i++)
	{
		e = g_bspsurfedges[f->firstedge + i];
		if (e >= 0)
		{
			v = &g_bspvertexes[g_bspedges[e].v[0]];
		}
		else
		{
			v = &g_bspvertexes[g_bspedges[-e].v[1]];
		}
		for (j = 0; j < 2; j++)
		{
			// The old code: val = v->point[0] * tex->vecs[j][0] + v->point[1] * tex->vecs[j][1] + v->point[2] * tex->vecs[j][2] + tex->vecs[j][3];
			//   was meant to be compiled for x86 under MSVC (prior to VS 11), so the intermediate values were stored as 64-bit double by default.
			// The new code will produce the same result as the old code, but it's portable for different platforms.
			// See this article for details: Intermediate Floating-Point Precision by Bruce-Dawson http://www.altdevblogaday.com/2012/03/22/intermediate-floating-point-precision/

			// The essential reason for having this ugly code is to get exactly the same value as the counterpart of game engine.
			// The counterpart of game engine is the function CalcFaceExtents in HLSDK.
			// So we must also know how Valve compiles HLSDK. I think Valve compiles HLSDK with VC6.0 in the past.
			val = CalculatePointVecsProduct(v->point, tex->vecs[j]);
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
		bmins[i] = (int)floor(mins[i] / TEXTURE_STEP);
		bmaxs[i] = (int)ceil(maxs[i] / TEXTURE_STEP);
	}

	for (i = 0; i < 2; i++)
	{
		mins_out[i] = bmins[i];
		maxs_out[i] = bmaxs[i];
	}
}

// =====================================================================================
//  WriteExtentFile
// =====================================================================================
void WriteExtentFile(const char *const filename)
{
	FILE *f;
	f = fopen(filename, "w");
	if (!f)
	{
		Error("Error opening %s: %s", filename, strerror(errno));
	}
	fprintf(f, "%i\n", g_bspnumfaces);
	for (int i = 0; i < g_bspnumfaces; i++)
	{
		int mins[2];
		int maxs[2];
		GetFaceExtents(i, mins, maxs);
		fprintf(f, "%i %i %i %i\n", mins[0], mins[1], maxs[0], maxs[1]);
	}
	fclose(f);
}

//
// =====================================================================================
//
const int BLOCK_WIDTH = 128;
const int BLOCK_HEIGHT = 128;
struct lightmapblock_t
{
	lightmapblock_t *next;
	bool used;
	int allocated[BLOCK_WIDTH];
};
void DoAllocBlock(lightmapblock_t *blocks, int w, int h)
{
	lightmapblock_t *block;
	// code from Quake
	int i, j;
	int best, best2;
	int x = 0;
	if (w < 1 || h < 1)
	{
		Error("DoAllocBlock: internal error.");
	}
	for (block = blocks; block; block = block->next)
	{
		best = BLOCK_HEIGHT;
		for (i = 0; i < BLOCK_WIDTH - w; i++)
		{
			best2 = 0;
			for (j = 0; j < w; j++)
			{
				if (block->allocated[i + j] >= best)
					break;
				if (block->allocated[i + j] > best2)
					best2 = block->allocated[i + j];
			}
			if (j == w)
			{
				x = i;
				best = best2;
			}
		}
		if (best + h <= BLOCK_HEIGHT)
		{
			block->used = true;
			for (i = 0; i < w; i++)
			{
				block->allocated[x + i] = best + h;
			}
			return;
		}
		if (!block->next)
		{ // need to allocate a new block
			if (!block->used)
			{
				Warning("CountBlocks: invalid extents %dx%d", w, h);
				return;
			}
			block->next = new lightmapblock_t;
			hlassume(block->next != nullptr, assume_NoMemory);
			memset(block->next, 0, sizeof(lightmapblock_t));
		}
	}
}

#define ENTRIES(a) (sizeof(a) / sizeof(*(a)))
#define ENTRYSIZE(a) (sizeof(*(a)))

// =====================================================================================
//  ParseImplicitTexinfoFromTexture
//      purpose: get the actual texinfo for a face. the tools shouldn't directly use f->texinfo after embedlightmap is done
// =====================================================================================
auto ParseImplicitTexinfoFromTexture(int miptex) -> int
{
	int texinfo;
	int numtextures = g_bsptexdatasize ? ((BSPLumpMiptexHeader *)g_bsptexdata)->nummiptex : 0;
	int offset;
	int size;
	BSPLumpMiptex *mt;
	char name[16];

	if (miptex < 0 || miptex >= numtextures)
	{
		Warning("ParseImplicitTexinfoFromTexture: internal error: invalid texture number %d.", miptex);
		return -1;
	}
	offset = ((BSPLumpMiptexHeader *)g_bsptexdata)->dataofs[miptex];
	size = g_bsptexdatasize - offset;
	if (offset < 0 || g_bsptexdata + offset < (byte *)&((BSPLumpMiptexHeader *)g_bsptexdata)->dataofs[numtextures] ||
		size < (int)sizeof(BSPLumpMiptex))
	{
		return -1;
	}

	mt = (BSPLumpMiptex *)&g_bsptexdata[offset];
	safe_strncpy(name, mt->name, 16);

	if (!(strlen(name) >= 6 && !strncasecmp(&name[1], "_rad", 4) && '0' <= name[5] && name[5] <= '9'))
	{
		return -1;
	}

	texinfo = atoi(&name[5]);
	if (texinfo < 0 || texinfo >= g_bspnumtexinfo)
	{
		Warning("Invalid index of original texinfo: %d parsed from texture name '%s'.", texinfo, name);
		return -1;
	}

	return texinfo;
}

auto ParseTexinfoForFace(const BSPLumpFace *f) -> int
{
	int texinfo;
	int miptex;
	int texinfo2;

	texinfo = f->texinfo;
	miptex = g_bsptexinfo[texinfo].miptex;
	if (miptex != -1)
	{
		texinfo2 = ParseImplicitTexinfoFromTexture(miptex);
		if (texinfo2 != -1)
		{
			texinfo = texinfo2;
		}
	}

	return texinfo;
}

// =====================================================================================
//  DeleteEmbeddedLightmaps
//      removes all "?_rad*" textures that are created by hlrad
//      this function does nothing if the map has no textures with name "?_rad*"
// =====================================================================================
void DeleteEmbeddedLightmaps()
{
	int countrestoredfaces = 0;
	int countremovedtexinfos = 0;
	int countremovedtextures = 0;
	int i;
	int numtextures = g_bsptexdatasize ? ((BSPLumpMiptexHeader *)g_bsptexdata)->nummiptex : 0;

	// Step 1: parse the original texinfo index stored in each "?_rad*" texture
	//         and restore the texinfo for the faces that have had their lightmap embedded

	for (i = 0; i < g_bspnumfaces; i++)
	{
		BSPLumpFace *f = &g_bspfaces[i];
		int texinfo;

		texinfo = ParseTexinfoForFace(f);
		if (texinfo != f->texinfo)
		{
			f->texinfo = texinfo;
			countrestoredfaces++;
		}
	}

	// Step 2: remove redundant texinfo
	{
		bool *texinfoused = new bool[g_bspnumtexinfo];
		hlassume(texinfoused != nullptr, assume_NoMemory);

		for (i = 0; i < g_bspnumtexinfo; i++)
		{
			texinfoused[i] = false;
		}
		for (i = 0; i < g_bspnumfaces; i++)
		{
			BSPLumpFace *f = &g_bspfaces[i];

			if (f->texinfo < 0 || f->texinfo >= g_bspnumtexinfo)
			{
				continue;
			}
			texinfoused[f->texinfo] = true;
		}
		for (i = g_bspnumtexinfo - 1; i > -1; i--)
		{
			BSPLumpTexInfo *info = &g_bsptexinfo[i];

			if (texinfoused[i])
			{
				break; // still used by a face; should not remove this texinfo
			}
			if (info->miptex < 0 || info->miptex >= numtextures)
			{
				break; // invalid; should not remove this texinfo
			}
			if (ParseImplicitTexinfoFromTexture(info->miptex) == -1)
			{
				break; // not added by hlrad; should not remove this texinfo
			}
			countremovedtexinfos++;
		}
		g_bspnumtexinfo = i + 1; // shrink g_bsptexinfo
		delete[] texinfoused;
	}

	// Step 3: remove redundant textures
	{
		int numremaining; // number of remaining textures
		bool *textureused = new bool[numtextures];
		hlassume(textureused != nullptr, assume_NoMemory);

		for (i = 0; i < numtextures; i++)
		{
			textureused[i] = false;
		}
		for (i = 0; i < g_bspnumtexinfo; i++)
		{
			BSPLumpTexInfo *info = &g_bsptexinfo[i];

			if (info->miptex < 0 || info->miptex >= numtextures)
			{
				continue;
			}
			textureused[info->miptex] = true;
		}
		for (i = numtextures - 1; i > -1; i--)
		{
			if (textureused[i] || ParseImplicitTexinfoFromTexture(i) == -1)
			{
				break; // should not remove this texture
			}
			countremovedtextures++;
		}
		numremaining = i + 1;
		delete[] textureused;

		if (numremaining < numtextures)
		{
			auto *texdata = (BSPLumpMiptexHeader *)g_bsptexdata;
			byte *dataaddr = (byte *)&texdata->dataofs[texdata->nummiptex];
			int datasize = (g_bsptexdata + texdata->dataofs[numremaining]) - dataaddr;
			byte *newdataaddr = (byte *)&texdata->dataofs[numremaining];
			memmove(newdataaddr, dataaddr, datasize);
			g_bsptexdatasize = (newdataaddr + datasize) - g_bsptexdata;
			texdata->nummiptex = numremaining;
			for (i = 0; i < numremaining; i++)
			{
				if (texdata->dataofs[i] < 0) // bad texture
				{
					continue;
				}
				texdata->dataofs[i] += newdataaddr - dataaddr;
			}

			numtextures = texdata->nummiptex;
		}
	}

	if (countrestoredfaces > 0 || countremovedtexinfos > 0 || countremovedtextures > 0)
	{
		Log("DeleteEmbeddedLightmaps: restored %d faces, removed %d texinfos and %d textures.\n",
			countrestoredfaces, countremovedtexinfos, countremovedtextures);
	}
}

// =====================================================================================
//  ParseEpair
//      entity key/value pairs
// =====================================================================================
auto ParseEpair() -> EntityProperty *
{
	EntityProperty *e;

	e = (EntityProperty *)Alloc(sizeof(EntityProperty));

	if (strlen(g_token) >= MAX_KEY - 1)
		Error("ParseEpair: Key token too long (%i > MAX_KEY)", (int)strlen(g_token));

	e->key = strdup(g_token);
	GetToken(false);

	if (strlen(g_token) >= MAX_VAL - 1) // MAX_VALUE //vluzacn
		Error("ParseEpar: Value token too long (%i > MAX_VALUE)", (int)strlen(g_token));

	e->value = strdup(g_token);

	return e;
}

/*
 * ================
 * ParseEntity
 * ================
 */

auto ParseEntity() -> bool
{
	EntityProperty *e;
	Entity *mapent;

	if (!GetToken(true))
	{
		return false;
	}

	if (strcmp(g_token, "{"))
	{
		Error("ParseEntity: { not found");
	}

	if (g_numentities == MAX_MAP_ENTITIES)
	{
		Error("g_numentities == MAX_MAP_ENTITIES");
	}

	mapent = &g_entities[g_numentities];
	g_numentities++;

	while (true)
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

	// ugly code
	if (!strncmp(ValueForKey(mapent, "classname"), "light", 5) && *ValueForKey(mapent, "_tex"))
	{
		SetKeyValue(mapent, "convertto", ValueForKey(mapent, "classname"));
		SetKeyValue(mapent, "classname", "light_surface");
	}
	if (!strcmp(ValueForKey(mapent, "convertfrom"), "light_shadow") || !strcmp(ValueForKey(mapent, "convertfrom"), "light_bounce"))
	{
		SetKeyValue(mapent, "convertto", ValueForKey(mapent, "classname"));
		SetKeyValue(mapent, "classname", ValueForKey(mapent, "convertfrom"));
		SetKeyValue(mapent, "convertfrom", "");
	}
	if (!strcmp(ValueForKey(mapent, "classname"), "light_environment") &&
		!strcmp(ValueForKey(mapent, "convertfrom"), "info_sunlight"))
	{
		while (mapent->epairs)
		{
			DeleteKey(mapent, mapent->epairs->key);
		}
		memset(mapent, 0, sizeof(Entity));
		g_numentities--;
		return true;
	}
	if (!strcmp(ValueForKey(mapent, "classname"), "light_environment") &&
		IntForKey(mapent, "_fake"))
	{
		SetKeyValue(mapent, "classname", "info_sunlight");
	}

	return true;
}

// =====================================================================================
//  ParseEntities
//      Parses the dentdata string into entities
// =====================================================================================
void ParseEntities()
{
	g_numentities = 0;
	ParseFromMemory(g_bspentdata, g_bspentdatasize);

	while (ParseEntity())
	{
	}
}

// =====================================================================================
//  UnparseEntities
//      Generates the dentdata string from all the entities
// =====================================================================================
auto anglesforvector(float angles[3], const float vector[3]) -> int
{
	float z = vector[2], r = sqrt(vector[0] * vector[0] + vector[1] * vector[1]);
	float tmp;
	if (sqrt(z * z + r * r) < NORMAL_EPSILON)
	{
		return -1;
	}
	else
	{
		tmp = sqrt(z * z + r * r);
		z /= tmp, r /= tmp;
		if (r < NORMAL_EPSILON)
		{
			if (z < 0)
			{
				angles[0] = -90, angles[1] = 0;
			}
			else
			{
				angles[0] = 90, angles[1] = 0;
			}
		}
		else
		{
			angles[0] = atan(z / r) / Q_PI * 180;
			float x = vector[0], y = vector[1];
			tmp = sqrt(x * x + y * y);
			x /= tmp, y /= tmp;
			if (x < -1 + NORMAL_EPSILON)
			{
				angles[1] = -180;
			}
			else
			{
				if (y >= 0)
				{
					angles[1] = 2 * atan(y / (1 + x)) / Q_PI * 180;
				}
				else
				{
					angles[1] = 2 * atan(y / (1 + x)) / Q_PI * 180 + 360;
				}
			}
		}
	}
	angles[2] = 0;
	return 0;
}
void UnparseEntities()
{
	char *buf;
	char *end;
	EntityProperty *ep;
	char line[MAXTOKEN];
	int i;

	buf = g_bspentdata;
	end = buf;
	*end = 0;

	for (i = 0; i < g_numentities; i++)
	{
		Entity *mapent = &g_entities[i];
		if (!strcmp(ValueForKey(mapent, "classname"), "info_sunlight") ||
			!strcmp(ValueForKey(mapent, "classname"), "light_environment"))
		{
			float vec[3] = {0, 0, 0};
			{
				sscanf(ValueForKey(mapent, "angles"), "%f %f %f", &vec[0], &vec[1], &vec[2]);
				float pitch = FloatForKey(mapent, "pitch");
				if (pitch)
					vec[0] = pitch;

				const char *target = ValueForKey(mapent, "target");
				if (target[0])
				{
					Entity *targetent = FindTargetEntity(target);
					if (targetent)
					{
						float origin1[3] = {0, 0, 0}, origin2[3] = {0, 0, 0}, normal[3];
						sscanf(ValueForKey(mapent, "origin"), "%f %f %f", &origin1[0], &origin1[1], &origin1[2]);
						sscanf(ValueForKey(targetent, "origin"), "%f %f %f", &origin2[0], &origin2[1], &origin2[2]);
						VectorSubtract(origin2, origin1, normal);
						anglesforvector(vec, normal);
					}
				}
			}
			char stmp[1024];
			safe_snprintf(stmp, 1024, "%g %g %g", vec[0], vec[1], vec[2]);
			SetKeyValue(mapent, "angles", stmp);
			DeleteKey(mapent, "pitch");

			if (!strcmp(ValueForKey(mapent, "classname"), "info_sunlight"))
			{
				if (g_numentities == MAX_MAP_ENTITIES)
				{
					Error("g_numentities == MAX_MAP_ENTITIES");
				}
				Entity *newent = &g_entities[g_numentities++];
				newent->epairs = mapent->epairs;
				SetKeyValue(newent, "classname", "light_environment");
				SetKeyValue(newent, "_fake", "1");
				mapent->epairs = nullptr;
			}
		}
	}
	for (i = 0; i < g_numentities; i++)
	{
		Entity *mapent = &g_entities[i];
		if (!strcmp(ValueForKey(mapent, "classname"), "light_shadow") || !strcmp(ValueForKey(mapent, "classname"), "light_bounce"))
		{
			SetKeyValue(mapent, "convertfrom", ValueForKey(mapent, "classname"));
			SetKeyValue(mapent, "classname", (*ValueForKey(mapent, "convertto") ? ValueForKey(mapent, "convertto") : "light"));
			SetKeyValue(mapent, "convertto", "");
		}
	}
	// ugly code
	for (i = 0; i < g_numentities; i++)
	{
		Entity *mapent = &g_entities[i];
		if (!strcmp(ValueForKey(mapent, "classname"), "light_surface"))
		{
			if (!*ValueForKey(mapent, "_tex"))
			{
				SetKeyValue(mapent, "_tex", "                ");
			}
			const char *newclassname = ValueForKey(mapent, "convertto");
			if (!*newclassname)
			{
				SetKeyValue(mapent, "classname", "light");
			}
			else if (strncmp(newclassname, "light", 5))
			{
				Error("New classname for 'light_surface' should begin with 'light' not '%s'.\n", newclassname);
			}
			else
			{
				SetKeyValue(mapent, "classname", newclassname);
			}
			SetKeyValue(mapent, "convertto", "");
		}
	}
#ifdef SCSG // seedee
	{
		int i, j;
		int count = 0;
		bool *lightneedcompare = new bool[g_numentities];
		hlassume(lightneedcompare != nullptr, assume_NoMemory);
		memset(lightneedcompare, 0, g_numentities * sizeof(bool));
		for (i = g_numentities - 1; i > -1; i--)
		{
			Entity *ent = &g_entities[i];
			const char *classname = ValueForKey(ent, "classname");
			const char *targetname = ValueForKey(ent, "targetname");
			int style = IntForKey(ent, "style");
			if (!targetname[0] || (strcmp(classname, "light") && strcmp(classname, "light_spot") && strcmp(classname, "light_environment")))
				continue;
			for (j = i + 1; j < g_numentities; j++)
			{
				if (!lightneedcompare[j])
					continue;
				Entity *ent2 = &g_entities[j];
				const char *targetname2 = ValueForKey(ent2, "targetname");
				int style2 = IntForKey(ent2, "style");
				if (style == style2 && !strcmp(targetname, targetname2))
					break;
			}
			if (j < g_numentities)
			{
				DeleteKey(ent, "targetname");
				count++;
			}
			else
			{
				lightneedcompare[i] = true;
			}
		}
		if (count > 0)
		{
			Log("%d redundant named lights optimized.\n", count);
		}
		delete[] lightneedcompare;
	}
#endif
	for (i = 0; i < g_numentities; i++)
	{
		ep = g_entities[i].epairs;
		if (!ep)
		{
			continue; // ent got removed
		}

		strcat(end, "{\n");
		end += 2;

		for (ep = g_entities[i].epairs; ep; ep = ep->next)
		{
			sprintf(line, "\"%s\" \"%s\"\n", ep->key, ep->value);
			strcat(end, line);
			end += strlen(line);
		}
		strcat(end, "}\n");
		end += 2;

		if (end > buf + MAX_MAP_ENTSTRING)
		{
			Error("Entity text too long");
		}
	}
	g_bspentdatasize = end - buf + 1;
}

// =====================================================================================
//  SetKeyValue
//      makes a keyvalue
// =====================================================================================
void DeleteKey(Entity *ent, const char *const key)
{
	EntityProperty **pep;
	for (pep = &ent->epairs; *pep; pep = &(*pep)->next)
	{
		if (!strcmp((*pep)->key, key))
		{
			EntityProperty *ep = *pep;
			*pep = ep->next;
			delete ep->key;
			delete ep->value;
			delete ep;
			return;
		}
	}
}
void SetKeyValue(Entity *ent, const char *const key, const char *const value)
{
	EntityProperty *ep;

	if (!value[0])
	{
		DeleteKey(ent, key);
		return;
	}
	for (ep = ent->epairs; ep; ep = ep->next)
	{
		if (!strcmp(ep->key, key))
		{
			char *value2 = strdup(value);
			delete ep->value;
			ep->value = value2;
			return;
		}
	}
	ep = (EntityProperty *)Alloc(sizeof(*ep));
	ep->next = ent->epairs;
	ent->epairs = ep;
	ep->key = strdup(key);
	ep->value = strdup(value);
}

// =====================================================================================
//  ValueForKey
//      returns the value for a passed entity and key
// =====================================================================================
auto ValueForKey(const Entity *const ent, const char *const key) -> const char *
{
	EntityProperty *ep;

	for (ep = ent->epairs; ep; ep = ep->next)
	{
		if (!strcmp(ep->key, key))
		{
			return ep->value;
		}
	}
	return "";
}

// =====================================================================================
//  IntForKey
// =====================================================================================
auto IntForKey(const Entity *const ent, const char *const key) -> int
{
	return atoi(ValueForKey(ent, key));
}

// =====================================================================================
//  FloatForKey
// =====================================================================================
auto FloatForKey(const Entity *const ent, const char *const key) -> vec_t
{
	return atof(ValueForKey(ent, key));
}

// =====================================================================================
//  GetVectorForKey
//      returns value for key in vec[0-2]
// =====================================================================================
void GetVectorForKey(const Entity *const ent, const char *const key, vec3_t vec)
{
	const char *k;
	double v1, v2, v3;

	k = ValueForKey(ent, key);
	// scanf into doubles, then assign, so it is vec_t size independent
	v1 = v2 = v3 = 0;
	sscanf(k, "%lf %lf %lf", &v1, &v2, &v3);
	vec[0] = v1;
	vec[1] = v2;
	vec[2] = v3;
}

// =====================================================================================
//  FindTargetEntity
//
// =====================================================================================
auto FindTargetEntity(const char *const target) -> Entity *
{
	int i;
	const char *n;

	for (i = 0; i < g_numentities; i++)
	{
		n = ValueForKey(&g_entities[i], "targetname");
		if (!strcmp(n, target))
		{
			return &g_entities[i];
		}
	}

	return nullptr;
}

void dtexdata_init()
{
	g_bsptexdata = (byte *)AllocBlock(g_max_map_miptex);
	hlassume(g_bsptexdata != nullptr, assume_NoMemory);
	g_bsplightdata = (byte *)AllocBlock(g_max_map_lightdata);
	hlassume(g_bsplightdata != nullptr, assume_NoMemory);
}

void dtexdata_free()
{
	FreeBlock(g_bsptexdata);
	g_bsptexdata = nullptr;
	FreeBlock(g_bsplightdata);
	g_bsplightdata = nullptr;
}

// =====================================================================================
//  GetTextureByNumber
//      Touchy function, can fail with a page fault if all the data isnt kosher
//      (i.e. map was compiled with missing textures)
// =====================================================================================
static char emptystring[1] = {'\0'};
auto GetTextureByNumber(int texturenumber) -> char *
{
	if (texturenumber == -1)
		return emptystring;
	BSPLumpTexInfo *info;
	BSPLumpMiptex *miptex;
	int ofs;

	info = &g_bsptexinfo[texturenumber];
	ofs = ((BSPLumpMiptexHeader *)g_bsptexdata)->dataofs[info->miptex];
	miptex = (BSPLumpMiptex *)(&g_bsptexdata[ofs]);

	return miptex->name;
}

// =====================================================================================
//  EntityForModel
//      returns entity addy for given modelnum
// =====================================================================================
auto EntityForModel(const int modnum) -> Entity *
{
	int i;
	const char *s;
	char name[16];

	sprintf(name, "*%i", modnum);
	// search the entities for one using modnum
	for (i = 0; i < g_numentities; i++)
	{
		s = ValueForKey(&g_entities[i], "model");
		if (!strcmp(s, name))
		{
			return &g_entities[i];
		}
	}

	return &g_entities[0];
}