#include <vector>
#include <map>
#include <string>
#include <cstring>

#include "textures.h"
#include "face.h"
#include "threads.h"
#include "log.h"
#include "filelib.h"
#include "mathlib.h"

static int nummiptex = 0;
static WadLumpInfo miptex[MAX_MAP_TEXTURES];
static int nTexLumps = 0;
static WadLumpInfo *lumpinfo = nullptr;
static int nTexFiles = 0;
static FILE *texfiles[MAX_TEXFILES];
static WadPath *texwadpathes[MAX_TEXFILES]; // maps index of the wad to its path
static char *texmap[MAX_INTERNAL_MAP_TEXINFO];
static int numtexmap = 0;

static auto texmap_store(char *texname, bool shouldlock = true) -> int
// This function should never be called unless a new entry in g_bsptexinfo is being allocated.
{
    if (shouldlock)
    {
        ThreadLock();
    }

    hlassume(numtexmap < MAX_INTERNAL_MAP_TEXINFO, assume_MAX_MAP_TEXINFO); // This error should never appear.

    auto i = numtexmap;
    texmap[numtexmap] = strdup(texname);
    numtexmap++;
    if (shouldlock)
    {
        ThreadUnlock();
    }
    return i;
}

static auto texmap_retrieve(int index) -> char *
{
    hlassume(0 <= index && index < numtexmap, assume_first);
    return texmap[index];
}

static void texmap_clear()
{
    ThreadLock();
    for (int i = 0; i < numtexmap; i++)
    {
        delete texmap[i];
    }
    numtexmap = 0;
    ThreadUnlock();
}

// =====================================================================================
//  CleanupName
// =====================================================================================
void CleanupName(const char *const in, char *out)
{
    int i;

    for (i = 0; i < MAXWADNAME; i++)
    {
        if (!in[i])
        {
            break;
        }

        out[i] = toupper(in[i]);
    }

    for (; i < MAXWADNAME; i++)
    {
        out[i] = 0;
    }
}

// =====================================================================================
//  lump_sorters
// =====================================================================================
auto lump_sorter_by_wad_and_name(const void *lump1, const void *lump2) -> int
{
    auto *plump1 = (WadLumpInfo *)lump1;
    auto *plump2 = (WadLumpInfo *)lump2;

    if (plump1->iTexFile == plump2->iTexFile)
    {
        return strcmp(plump1->name, plump2->name);
    }
    else
    {
        return plump1->iTexFile - plump2->iTexFile;
    }
}

auto lump_sorter_by_name(const void *lump1, const void *lump2) -> int
{
    auto *plump1 = (WadLumpInfo *)lump1;
    auto *plump2 = (WadLumpInfo *)lump2;

    return strcmp(plump1->name, plump2->name);
}

// =====================================================================================
//  FindMiptex
//      Find and allocate a texture into the lump data
// =====================================================================================
auto FindMiptex(const char *const name) -> int
{
    int i;
    if (strlen(name) >= MAXWADNAME)
    {
        Error("Texture name is too long (%s)\n", name);
    }

    ThreadLock();
    for (i = 0; i < nummiptex; i++)
    {
        if (!strcmp(name, miptex[i].name))
        {
            ThreadUnlock();
            return i;
        }
    }

    hlassume(nummiptex < MAX_MAP_TEXTURES, assume_MAX_MAP_TEXTURES);
    safe_strncpy(miptex[i].name, name, MAXWADNAME);
    nummiptex++;
    ThreadUnlock();
    return i;
}

// =====================================================================================
//  TEX_InitFromWad
// =====================================================================================
auto TEX_InitFromWad() -> bool
{
    int i;
    WadInfo wadinfo;
    WadPath *currentwad;

    Log("\n"); // looks cleaner
    // update wad inclusion
    for (i = 0; i < g_iNumWadPaths; i++) // loop through all wadpaths in map
    {
        currentwad = g_pWadPaths[i];
        currentwad->usedbymap = false; //-nowadtextures
    }

    auto *pszWadroot = getenv("WADROOT");

    // for eachwadpath
    for (i = 0; i < g_iNumWadPaths; i++)
    {
        FILE *texfile; // temporary used in this loop
        currentwad = g_pWadPaths[i];
        auto *pszWadFile = currentwad->path;
        texwadpathes[nTexFiles] = currentwad;
        texfiles[nTexFiles] = fopen(pszWadFile, "rb");

        if (!texfiles[nTexFiles] && pszWadroot)
        {
            char szTmp[_MAX_PATH];
            char szFile[_MAX_PATH];
            char szSubdir[_MAX_PATH];

            ExtractFile(pszWadFile, szFile);

            ExtractFilePath(pszWadFile, szTmp);
            ExtractFile(szTmp, szSubdir);

            // szSubdir will have a trailing separator
            safe_snprintf(szTmp, _MAX_PATH, "%s" SYSTEM_SLASH_STR "%s%s", pszWadroot, szSubdir, szFile);
            texfiles[nTexFiles] = fopen(szTmp, "rb");

            if (!texfiles[nTexFiles])
            {
                // if we cant find it, Convert to lower case and try again
                strlwr(szTmp);
                texfiles[nTexFiles] = fopen(szTmp, "rb");
            }
        }

        if (!texfiles[nTexFiles])
        {
            pszWadFile = currentwad->path; // correct it back
            // still cant find it, error out
            Fatal(assume_COULD_NOT_FIND_WAD, "Could not open wad file %s", pszWadFile);
            continue;
        }

        pszWadFile = currentwad->path; // correct it back

        // temp assignment to make things cleaner:
        texfile = texfiles[nTexFiles];

        // read in this wadfiles information
        SafeRead(texfile, &wadinfo, sizeof(wadinfo));

        // make sure its a valid format
        if (strncmp(wadinfo.identification, "WAD3", 4))
        {
            Log(" - ");
            Error("%s isn't a Wadfile!", pszWadFile);
        }

        wadinfo.numlumps = LittleLong(wadinfo.numlumps);
        wadinfo.infotableofs = LittleLong(wadinfo.infotableofs);

        // read in lump
        if (fseek(texfile, wadinfo.infotableofs, SEEK_SET))
            Warning("fseek to %d in wadfile %s failed\n", wadinfo.infotableofs, pszWadFile);

        // memalloc for this lump
        lumpinfo = (WadLumpInfo *)realloc(lumpinfo, (nTexLumps + wadinfo.numlumps) * sizeof(WadLumpInfo));

        for (int j = 0; j < wadinfo.numlumps; j++, nTexLumps++)
        {
            SafeRead(texfile, &lumpinfo[nTexLumps], (sizeof(WadLumpInfo) - sizeof(int))); // iTexFile is NOT read from file
            char szWadFileName[_MAX_PATH];
            ExtractFile(pszWadFile, szWadFileName);
            CleanupName(lumpinfo[nTexLumps].name, lumpinfo[nTexLumps].name);
            lumpinfo[nTexLumps].filepos = LittleLong(lumpinfo[nTexLumps].filepos);
            lumpinfo[nTexLumps].disksize = LittleLong(lumpinfo[nTexLumps].disksize);
            lumpinfo[nTexLumps].iTexFile = nTexFiles;
        }

        // AJM: this feature is dependant on autowad. :(
        // CONSIDER: making it standard?
        currentwad->totaltextures = wadinfo.numlumps;

        nTexFiles++;
        hlassume(nTexFiles < MAX_TEXFILES, assume_MAX_TEXFILES);
    }

    // sort texlumps in memory by name
    qsort((void *)lumpinfo, (size_t)nTexLumps, sizeof(lumpinfo[0]), lump_sorter_by_name);

    CheckFatal();
    return true;
}

// =====================================================================================
//  FindTexture
// =====================================================================================
auto FindTexture(const WadLumpInfo *const source) -> WadLumpInfo *
{
    WadLumpInfo *found = nullptr;

    found = (WadLumpInfo *)bsearch(source, (void *)lumpinfo, (size_t)nTexLumps, sizeof(lumpinfo[0]), lump_sorter_by_name);
    if (!found)
    {
        Warning("::FindTexture() texture %s not found!", source->name);
        if (!strcmp(source->name, "NULL") || !strcmp(source->name, "SKIP"))
        {
            Log("Are you sure you included sdhlt.wad in your wadpath list?\n");
        }
    }

    if (found)
    {
        // get the first and last matching lump
        auto *first = found;
        auto *last = found;
        while (first - 1 >= lumpinfo && lump_sorter_by_name(first - 1, source) == 0)
        {
            first = first - 1;
        }
        while (last + 1 < lumpinfo + nTexLumps && lump_sorter_by_name(last + 1, source) == 0)
        {
            last = last + 1;
        }
        // find the best matching lump
        WadLumpInfo *best = nullptr;
        for (found = first; found < last + 1; found++)
        {
            bool better = false;
            if (best == nullptr)
            {
                better = true;
            }
            else if (found->iTexFile != best->iTexFile)
            {
                auto *found_wadpath = texwadpathes[found->iTexFile];
                auto *best_wadpath = texwadpathes[best->iTexFile];
                if (found_wadpath->usedbymap != best_wadpath->usedbymap)
                {
                    better = !found_wadpath->usedbymap; // included wad is better
                }
                else
                {
                    better = found->iTexFile < best->iTexFile; // upper in the wad list is better
                }
            }
            else if (found->filepos != best->filepos)
            {
                better = found->filepos < best->filepos; // when there are several lumps with the same name in one wad file
            }

            if (better)
            {
                best = found;
            }
        }
        found = best;
    }
    return found;
}

// =====================================================================================
//  LoadLump
// =====================================================================================
auto LoadLump(const WadLumpInfo *const source, byte *dest, int *texsize, int dest_maxsize, byte *&writewad_data, int &writewad_datasize) -> int
{
    writewad_data = nullptr;
    writewad_datasize = -1;

    *texsize = 0;
    if (source->filepos)
    {
        if (fseek(texfiles[source->iTexFile], source->filepos, SEEK_SET))
        {
            Warning("fseek to %d failed\n", source->filepos);
            Error("File read failure");
        }
        *texsize = source->disksize;

        if (texwadpathes[source->iTexFile]->usedbymap)
        {
            // Just read the miptex header and zero out the data offsets.
            // We will load the entire texture from the WAD at engine runtime¿
            auto *miptex = (BSPLumpMiptex *)dest;
            hlassume((int)sizeof(BSPLumpMiptex) <= dest_maxsize, assume_MAX_MAP_MIPTEX);
            SafeRead(texfiles[source->iTexFile], dest, sizeof(BSPLumpMiptex));

            for (int i = 0; i < MIPLEVELS; i++)
                miptex->offsets[i] = 0;
            writewad_data = new byte[source->disksize];
            hlassume(writewad_data != nullptr, assume_NoMemory);
            if (fseek(texfiles[source->iTexFile], source->filepos, SEEK_SET))
                Error("File read failure");
            SafeRead(texfiles[source->iTexFile], writewad_data, source->disksize);
            writewad_datasize = source->disksize;
            return sizeof(BSPLumpMiptex);
        }
        else
        {
            // Load the entire texture here so the BSP contains the texture
            hlassume(source->disksize <= dest_maxsize, assume_MAX_MAP_MIPTEX);
            SafeRead(texfiles[source->iTexFile], dest, source->disksize);
            return source->disksize;
        }
    }

    Error("::LoadLump() texture %s not found!", source->name);
    return 0;
}

// =====================================================================================
//  AddAnimatingTextures
// =====================================================================================
void AddAnimatingTextures()
{
    char name[MAXWADNAME];
    auto base = nummiptex;

    for (int i = 0; i < base; i++)
    {
        if ((miptex[i].name[0] != '+') && (miptex[i].name[0] != '-'))
        {
            continue;
        }

        safe_strncpy(name, miptex[i].name, MAXWADNAME);

        for (int j = 0; j < 20; j++)
        {
            if (j < 10)
            {
                name[1] = '0' + j;
            }
            else
            {
                name[1] = 'A' + j - 10; // alternate animation
            }

            // see if this name exists in the wadfile
            for (int k = 0; k < nTexLumps; k++)
            {
                if (!strcmp(name, lumpinfo[k].name))
                {
                    FindMiptex(name); // add to the miptex list
                    break;
                }
            }
        }
    }

    if (nummiptex - base)
    {
        Log("added %i additional animating textures.\n", nummiptex - base);
    }
}

// =====================================================================================
//  WriteMiptex
//     Unified console logging updated //seedee
// =====================================================================================
void WriteMiptex()
{
    int texsize, totaltexsize = 0;

    g_bsptexdatasize = 0;
    {
        if (!TEX_InitFromWad())
            return;

        AddAnimatingTextures();
    }
    {
        for (int i = 0; i < nummiptex; i++)
        {
            WadLumpInfo *found;

            found = FindTexture(miptex + i);
            if (found)
            {
                miptex[i] = *found;
                texwadpathes[found->iTexFile]->usedtextures++;
            }
            else
            {
                miptex[i].iTexFile = miptex[i].filepos = miptex[i].disksize = 0;
            }
        }
    }

    // Now we have filled lumpinfo for each miptex and the number of used textures for each wad.
    {
        char szUsedWads[MAX_VAL];
        int i;

        szUsedWads[0] = 0;
        std::vector<WadPath *> usedWads;
        std::vector<WadPath *> includedWads;

        for (i = 0; i < nTexFiles; i++)
        {
            WadPath *currentwad = texwadpathes[i];
            if (currentwad->usedbymap && currentwad->usedtextures > 0)
            {
                char tmp[_MAX_PATH];
                ExtractFile(currentwad->path, tmp);
                safe_strncat(szUsedWads, tmp, MAX_VAL); // Concat wad names
                safe_strncat(szUsedWads, ";", MAX_VAL);
                usedWads.push_back(currentwad);
            }
        }
        for (i = 0; i < nTexFiles; i++)
        {
            WadPath *currentwad = texwadpathes[i];
            if (!currentwad->usedbymap && currentwad->usedtextures > 0)
            {
                includedWads.push_back(currentwad);
            }
        }
        if (!usedWads.empty())
        {
            Log("Wad files used by map\n");
            Log("---------------------\n");
            for (std::vector<WadPath *>::iterator it = usedWads.begin(); it != usedWads.end(); ++it)
            {
                WadPath *currentwad = *it;
                LogWadUsage(currentwad, nummiptex);
            }
            Log("---------------------\n\n");
        }
        else
        {
            Log("No wad files used by the map\n");
        }
        if (!includedWads.empty())
        {
            Log("Additional wad files included\n");
            Log("-----------------------------\n");

            for (std::vector<WadPath *>::iterator it = includedWads.begin(); it != includedWads.end(); ++it)
            {
                WadPath *currentwad = *it;
                LogWadUsage(currentwad, nummiptex);
            }
            Log("-----------------------------\n\n");
        }
        else
        {
            Log("No additional wad files included\n\n");
        }
        SetKeyValue(&g_entities[0], "wad", szUsedWads);
    }

    {
        auto *tx = g_bsptexinfo;

        // Sort them FIRST by wadfile and THEN by name for most efficient loading in the engine.
        qsort((void *)miptex, (size_t)nummiptex, sizeof(miptex[0]), lump_sorter_by_wad_and_name);

        // Sleazy Hack 104 Pt 2 - After sorting the miptex array, reset the texinfos to point to the right miptexs
        for (int i = 0; i < g_bspnumtexinfo; i++, tx++)
        {
            auto *miptex_name = texmap_retrieve(tx->miptex);

            tx->miptex = FindMiptex(miptex_name);
        }
        texmap_clear();
    }
    {
        // Now setup to get the miptex data (or just the headers if using -wadtextures) from the wadfile
        auto *l = (BSPLumpMiptexHeader *)g_bsptexdata;
        auto *data = (byte *)&l->dataofs[nummiptex];
        l->nummiptex = nummiptex;
        char writewad_name[_MAX_PATH]; // Write temp wad file with processed textures
        struct dlumpinfo_t             // Lump info in temp wad
        {
            int filepos;
            int disksize;
            int size;
            char type;
            char compression;
            char pad1, pad2;
            char name[MAXWADNAME];
        };
        WadInfo writewad_header;

        safe_snprintf(writewad_name, _MAX_PATH, "%s.wa_", g_Mapname); // Generate temp wad file name based on mapname
        auto *writewad_file = SafeOpenWrite(writewad_name);

        // Malloc for storing lump info
        auto writewad_maxlumpinfos = nummiptex;
        auto *writewad_lumpinfos = new dlumpinfo_t[writewad_maxlumpinfos];
        hlassume(writewad_lumpinfos != nullptr, assume_NoMemory);

        // Header for the temp wad file
        writewad_header.identification[0] = 'W';
        writewad_header.identification[1] = 'A';
        writewad_header.identification[2] = 'D';
        writewad_header.identification[3] = '3';
        writewad_header.numlumps = 0;

        if (fseek(writewad_file, sizeof(WadInfo), SEEK_SET)) // Move file pointer to skip header
            Error("File write failure");
        for (int i = 0; i < nummiptex; i++) // Process each miptex, writing its data to the temp wad file
        {
            l->dataofs[i] = data - (byte *)l;
            byte *writewad_data;
            int writewad_datasize;
            auto len = LoadLump(miptex + i, data, &texsize, &g_bsptexdata[g_max_map_miptex] - data, writewad_data, writewad_datasize); // Load lump data

            if (writewad_data)
            {
                // Prepare lump info for temp wad file
                dlumpinfo_t *writewad_lumpinfo = &writewad_lumpinfos[writewad_header.numlumps];
                writewad_lumpinfo->filepos = ftell(writewad_file);
                writewad_lumpinfo->disksize = writewad_datasize;
                writewad_lumpinfo->size = miptex[i].size;
                writewad_lumpinfo->type = miptex[i].type;
                writewad_lumpinfo->compression = miptex[i].compression;
                writewad_lumpinfo->pad1 = miptex[i].pad1;
                writewad_lumpinfo->pad2 = miptex[i].pad2;
                memcpy(writewad_lumpinfo->name, miptex[i].name, MAXWADNAME);
                writewad_header.numlumps++;
                SafeWrite(writewad_file, writewad_data, writewad_datasize); // Write the processed lump info temp wad file
                delete writewad_data;
            }

            if (!len)
            {
                l->dataofs[i] = -1; // Mark texture not found
            }
            else
            {
                totaltexsize += texsize;

                hlassume(totaltexsize < g_max_map_miptex, assume_MAX_MAP_MIPTEX);
            }
            data += len;
        }
        g_bsptexdatasize = data - g_bsptexdata;
        // Write lump info and header to the temp wad file
        writewad_header.infotableofs = ftell(writewad_file);
        SafeWrite(writewad_file, writewad_lumpinfos, writewad_header.numlumps * sizeof(dlumpinfo_t));
        if (fseek(writewad_file, 0, SEEK_SET))
            Error("File write failure");
        SafeWrite(writewad_file, &writewad_header, sizeof(WadInfo));
        if (fclose(writewad_file))
            Error("File write failure");
    }
    Log("Texture usage: %1.2f/%1.2f MB)\n", (float)totaltexsize / (1024 * 1024), (float)g_max_map_miptex / (1024 * 1024));
}

// =====================================================================================
//  LogWadUsage //seedee
// =====================================================================================
void LogWadUsage(WadPath *currentwad, int nummiptex)
{
    if (currentwad == nullptr)
    {
        return;
    }
    char currentwadName[_MAX_PATH];
    ExtractFile(currentwad->path, currentwadName);
    auto percentUsed = (double)currentwad->usedtextures / (double)nummiptex * 100;

    Log("[%s] %i/%i texture%s (%2.2f%%)\n - %s\n", currentwadName, currentwad->usedtextures, currentwad->totaltextures, currentwad->usedtextures == 1 ? "" : "s", percentUsed, currentwad->path);
}

// =====================================================================================
//  TexinfoForBrushTexture
// =====================================================================================
auto TexinfoForBrushTexture(FaceTexture *bt, const vec3_t origin) -> int
{
    BSPLumpTexInfo tx;
    int i;

    if (!strncasecmp(bt->name, "NULL", 4))
    {
        return -1;
    }
    memset(&tx, 0, sizeof(tx));
    FindMiptex(bt->name);

    // set the special flag
    if (bt->name[0] == '*' || !strncasecmp(bt->name, "sky", 3)

        // =====================================================================================
        // Cpt_Andrew - Env_Sky Check
        // =====================================================================================
        || !strncasecmp(bt->name, "env_sky", 5)
        // =====================================================================================

        || !strncasecmp(bt->name, "origin", 6) || !strncasecmp(bt->name, "null", 4) || !strncasecmp(bt->name, "aaatrigger", 10))
    {
        // actually only 'sky' and 'aaatrigger' needs this. --vluzacn
        tx.flags |= TEX_SPECIAL;
    }
    else
    {
        if (!bt->scale[0])
        {
            bt->scale[0] = 1;
        }
        if (!bt->scale[1])
        {
            bt->scale[1] = 1;
        }
        else
        {
            auto scale = 1 / bt->scale[0];
            VectorScale(bt->UAxis, scale, tx.vecs[0]);
            scale = 1 / bt->scale[1];
            VectorScale(bt->VAxis, scale, tx.vecs[1]);
        }

        tx.vecs[0][3] = bt->shift[0] + DotProduct(origin, tx.vecs[0]);
        tx.vecs[1][3] = bt->shift[1] + DotProduct(origin, tx.vecs[1]);
    }

    //
    // find the g_bsptexinfo
    //
    ThreadLock();
    auto *tc = g_bsptexinfo;
    for (i = 0; i < g_bspnumtexinfo; i++, tc++)
    {
        // Sleazy hack 104, Pt 3 - Use strcmp on names to avoid dups
        if (strcmp(texmap_retrieve(tc->miptex), bt->name) != 0)
        {
            continue;
        }
        if (tc->flags != tx.flags)
        {
            continue;
        }
        for (int j = 0; j < 2; j++)
        {
            for (int k = 0; k < 4; k++)
            {
                if (tc->vecs[j][k] != tx.vecs[j][k])
                {
                    goto skip;
                }
            }
        }
        ThreadUnlock();
        return i;
    skip:;
    }

    hlassume(g_bspnumtexinfo < MAX_INTERNAL_MAP_TEXINFO, assume_MAX_MAP_TEXINFO);

    *tc = tx;
    tc->miptex = texmap_store(bt->name, false);
    g_bspnumtexinfo++;
    ThreadUnlock();
    return i;
}

// Before WriteMiptex(), for each texinfo in g_bsptexinfo, .miptex is a string rather than texture index, so this function should be used instead of GetTextureByNumber.
auto GetTextureByNumber_CSG(int texturenumber) -> const char *
{
    if (texturenumber == -1)
        return "";
    return texmap_retrieve(g_bsptexinfo[texturenumber].miptex);
}

//
//
//
//

WadPath *g_pWadPaths[MAX_WADPATHS];
int g_iNumWadPaths = 0;

// =====================================================================================
//  PushWadPath
//      adds a wadpath into the wadpaths list, without duplicates
// =====================================================================================
void PushWadPath(const char *const path, bool inuse)
{
    WadPath *currentWad;
    hlassume(g_iNumWadPaths < MAX_WADPATHS, assume_MAX_TEXFILES);
    currentWad = new WadPath;
    safe_strncpy(currentWad->path, path, _MAX_PATH); // Copy path into currentWad->path
    currentWad->usedbymap = inuse;
    currentWad->usedtextures = 0;  // Updated later in autowad procedures
    currentWad->totaltextures = 0; // Updated later to reflect total

    if (g_iNumWadPaths < MAX_WADPATHS) // Fix buffer overrun //seedee
    {
        g_pWadPaths[g_iNumWadPaths] = currentWad;
        g_iNumWadPaths++;
    }
    else
    {
        delete currentWad;
        Error("PushWadPath: too many wadpaths (%i/%i)", g_iNumWadPaths, MAX_WADPATHS);
    }
}

// =====================================================================================
//  FreeWadPaths
// =====================================================================================
void FreeWadPaths()
{
    int i;
    WadPath *current;

    for (i = 0; i < g_iNumWadPaths; i++)
    {
        current = g_pWadPaths[i];
        delete current;
    }
}

// =====================================================================================
//  GetUsedWads
//      parse the "wad" keyvalue into WadPath structs
// =====================================================================================
void GetUsedWads()
{
    const char *pszWadPaths;
    char szTmp[_MAX_PATH];
    int i, j;
    pszWadPaths = ValueForKey(&g_entities[0], "wad");

    for (i = 0;;) // Loop through wadpaths
    {
        for (j = i; pszWadPaths[j] != '\0'; j++) // Find end of wadpath (semicolon)
        {
            if (pszWadPaths[j] == ';')
            {
                break;
            }
        }
        if (j - i > 0) // If wadpath is not empty
        {
            int length = qmin(j - i, _MAX_PATH - 1); // Get length of wadpath
            memcpy(szTmp, &pszWadPaths[i], length);
            szTmp[length] = '\0'; // Null terminate

            if (g_iNumWadPaths >= MAX_WADPATHS)
            {
                Error("Too many wad files (%d/%d)\n", g_iNumWadPaths, MAX_WADPATHS);
            }
            PushWadPath(szTmp, true); // Add wadpath to list
        }
        if (pszWadPaths[j] == '\0') // Break if end of wadpaths
        {
            break;
        }
        i = j + 1;
    }
}