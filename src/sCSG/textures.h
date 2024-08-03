#pragma once

#include "mathtypes.h"
#include "win32fix.h"

constexpr int MAX_WADPATHS = 128; // arbitrary

struct wadpath_t
{
    char path[_MAX_PATH];
    bool usedbymap;    // does this map requrie this wad to be included in the bsp?
    int usedtextures;  // number of textures in this wad the map actually uses
    int totaltextures; // total textures in this wad
}; // !!! the above two are VERY DIFFERENT. ie (usedtextures == 0) != (usedbymap == false)

extern wadpath_t *g_pWadPaths[MAX_WADPATHS];
extern int g_iNumWadPaths;

extern void PushWadPath(const char *const path, bool inuse);
extern void FreeWadPaths();
extern void GetUsedWads();

constexpr int MAXWADNAME = 16;
constexpr int MAX_TEXFILES = 128;

struct Plane;
struct face_texture_t;

// Struct definitions
struct wadinfo_t
{
    char identification[4]; // should be WAD3
    int numlumps;
    int infotableofs;
};

struct lumpinfo_t
{
    int filepos;
    int disksize;
    int size; // uncompressed
    char type;
    char compression;
    char pad1, pad2;
    char name[MAXWADNAME]; // Texture name // must be null terminated // upper case

    int iTexFile; // index of the wad this texture is located in
};

void LogWadUsage(wadpath_t *currentwad, int nummiptex);
void CleanupName(const char *const in, char *out);
int FindMiptex(const char *const name);
bool TEX_InitFromWad();
lumpinfo_t *FindTexture(const lumpinfo_t *const source);
int LoadLump(const lumpinfo_t *const source, byte *dest, int *texsize, int dest_maxsize, byte *&writewad_data, int &writewad_datasize);
void AddAnimatingTextures();
void WriteMiptex();
void LogWadUsage(wadpath_t *currentwad);
int TexinfoForBrushTexture(const Plane *const plane, face_texture_t *bt, const vec3_t origin);
const char *GetTextureByNumber_CSG(int texturenumber);
