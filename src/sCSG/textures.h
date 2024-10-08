#pragma once

#include "mathtypes.h"
#include "win32fix.h"

constexpr int MAX_WADPATHS = 128; // arbitrary

struct WadPath
{
    char path[_MAX_PATH];
    bool usedbymap;    // does this map requrie this wad to be included in the bsp?
    int usedtextures;  // number of textures in this wad the map actually uses
    int totaltextures; // total textures in this wad
}; // !!! the above two are VERY DIFFERENT. ie (usedtextures == 0) != (usedbymap == false)

extern WadPath *g_pWadPaths[MAX_WADPATHS];
extern int g_iNumWadPaths;

extern void PushWadPath(const char *const path, bool inuse);
extern void FreeWadPaths();
extern void GetUsedWads();

constexpr int MAXWADNAME = 16;
constexpr int MAX_TEXFILES = 128;

struct Plane;
struct FaceTexture;

// Struct definitions
struct WadInfo
{
    char identification[4]; // should be WAD3
    int numlumps;
    int infotableofs;
};

struct WadLumpInfo
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

void LogWadUsage(WadPath *currentwad, int nummiptex);
void CleanupName(const char *const in, char *out);
int FindMiptex(const char *const name);
bool TEX_InitFromWad();
WadLumpInfo *FindTexture(const WadLumpInfo *const source);
int LoadLump(const WadLumpInfo *const source, byte *dest, int *texsize, int dest_maxsize, byte *&writewad_data, int &writewad_datasize);
void AddAnimatingTextures();
void WriteMiptex();
void LogWadUsage(WadPath *currentwad);
int TexinfoForBrushTexture(FaceTexture *bt, const vec3_t origin);
const char *GetTextureByNumber_CSG(int texturenumber);
