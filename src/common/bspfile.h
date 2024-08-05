#pragma once

// upper design bounds

constexpr int MAX_MAP_HULLS = 4;
// hard limit

constexpr int MAX_MAP_MODELS = 512; // 400 //vluzacn
// variable, but 400 brush entities is very stressful on the engine and network code as it is

constexpr int MAX_MAP_BRUSHES = 32768;
// arbitrary, but large numbers of brushes generally require more lightmap's than the compiler can handle

constexpr int MAX_ENGINE_ENTITIES = 16384; // 1024 //vluzacn
constexpr int MAX_MAP_ENTITIES = 16384;    // 2048 //vluzacn
// hard limit, in actuallity it is too much, as temporary entities in the game plus static map entities can overflow

constexpr int MAX_MAP_ENTSTRING = 2048 * 1024; //(512*1024) //vluzacn
// abitrary, 512Kb of string data should be plenty even with TFC FGD's

constexpr int MAX_MAP_PLANES = 32768; // TODO: This can be larger, because although faces can only use plane 0~32767, clipnodes can use plane 0-65535. --vluzacn
constexpr int MAX_INTERNAL_MAP_PLANES = 256 * 1024;
// (from email): I have been building a rather complicated map, and using your latest
// tools (1.61) it seemed to compile fine.  However, in game, the engine was dropping
// a lot of faces from almost every FUNC_WALL, and also caused a strange texture
// phenomenon in software mode (see attached screen shot).  When I compiled with v1.41,
// I noticed that it hit the MAX_MAP_PLANES limit of 32k.  After deleting some brushes
// I was able to bring the map under the limit, and all of the previous errors went away.

constexpr int MAX_MAP_NODES = 32767;
// hard limit (negative short's are used as contents values)
constexpr int MAX_MAP_CLIPNODES = 32767;
// hard limit (negative short's are used as contents values)

constexpr int MAX_MAP_LEAFS = 32760;
constexpr int MAX_MAP_LEAFS_ENGINE = 8192;
// No problem has been observed in testmap or reported, except when viewing the map from outside (some leafs missing, no crash).
// This problem indicates that engine's MAX_MAP_LEAFS is 8192 (for reason, see: Quake - gl_model.c - Mod_Init).
// I don't know if visleafs > 8192 will cause Mod_DecompressVis overflow.

constexpr int MAX_MAP_VERTS = 65535;
constexpr int MAX_MAP_FACES = 65535; // This ought to be 32768, otherwise faces(in world) can become invisible. --vluzacn
constexpr int MAX_MAP_WORLDFACES = 32768;
constexpr int MAX_MAP_MARKSURFACES = 65535;
// hard limit (data structures store them as unsigned shorts)

constexpr int MAX_MAP_TEXTURES = 4096; // 512 //vluzacn
// hard limit (halflife limitation) // I used 2048 different textures in a test map and everything looks fine in both opengl and d3d mode.

constexpr int MAX_MAP_TEXINFO = 32767;
// hard limit (face.texinfo is signed short)
constexpr int MAX_INTERNAL_MAP_TEXINFO = 262144;

constexpr int MAX_MAP_EDGES = 256000;
constexpr int MAX_MAP_SURFEDGES = 512000;
// arbtirary

constexpr int DEFAULT_MAX_MAP_MIPTEX = 0x2000000; // 0x400000 //vluzacn
// 4Mb of textures is enough especially considering the number of people playing the game
// still with voodoo1 and 2 class cards with limited local memory.

constexpr int DEFAULT_MAX_MAP_LIGHTDATA = 0x3000000; // 0x600000 //vluzacn
// arbitrary

constexpr int MAX_MAP_VISIBILITY = 0x800000; // 0x200000 //vluzacn
// arbitrary

// these are for entity key:value pairs
constexpr int MAX_KEY = 128;  // 32 //vluzacn
constexpr int MAX_VAL = 4096; // the name used to be MAX_VALUE //vluzacn
// quote from yahn: 'probably can raise these values if needed'

// texture size limit

constexpr int MAX_TEXTURE_SIZE = 348972; // Bytes in a 512x512 image((256 * 256 * sizeof(short) * 3) / 2) //stop compiler from warning 512*512 texture. --vluzacn
// this is arbitrary, and needs space for the largest realistic texture plus
// room for its mipmaps.'  This value is primarily used to catch damanged or invalid textures
// in a wad file

constexpr int TEXTURE_STEP = 16;       // this constant was previously defined in lightmap.cpp. --vluzacn
constexpr int MAX_SURFACE_EXTENT = 16; // if lightmap extent exceeds 16, the map will not be able to load in 'Software' renderer and HLDS. //--vluzacn

constexpr float ENGINE_ENTITY_RANGE = 4096.0;
//=============================================================================

constexpr int BSPVERSION = 30;
constexpr int TOOLVERSION = 2;

//
// BSP File Structures
//

struct Lump
{
    int fileofs, filelen;
};

constexpr int LUMP_ENTITIES = 0;
constexpr int LUMP_PLANES = 1;
constexpr int LUMP_TEXTURES = 2;
constexpr int LUMP_VERTEXES = 3;
constexpr int LUMP_VISIBILITY = 4;
constexpr int LUMP_NODES = 5;
constexpr int LUMP_TEXINFO = 6;
constexpr int LUMP_FACES = 7;
constexpr int LUMP_LIGHTING = 8;
constexpr int LUMP_CLIPNODES = 9;
constexpr int LUMP_LEAFS = 10;
constexpr int LUMP_MARKSURFACES = 11;
constexpr int LUMP_EDGES = 12;
constexpr int LUMP_SURFEDGES = 13;
constexpr int LUMP_MODELS = 14;
constexpr int HEADER_LUMPS = 15;

// #define LUMP_MISCPAD      -1
// #define LUMP_ZEROPAD      -2

struct BSPLumpModel
{
    float mins[3], maxs[3];
    float origin[3];
    int headnode[MAX_MAP_HULLS];
    int visleafs; // not including the solid leaf 0
    int firstface, numfaces;
};

struct BSPLumpHeader
{
    int version;
    Lump lumps[HEADER_LUMPS];
};

struct BSPLumpMiptexHeader
{
    int nummiptex;
    int dataofs[4]; // [nummiptex]
};

constexpr int MAXTEXTURENAME = 16;
constexpr int MIPLEVELS = 4;
struct BSPLumpMiptex
{
    char name[MAXTEXTURENAME];
    unsigned width, height;
    unsigned offsets[MIPLEVELS]; // four mip maps stored
};

struct BSPLumpVertex
{
    float point[3];
};

struct dplane_t
{
    float normal[3];
    float dist;
    planetypes type; // PLANE_X - PLANE_ANYZ ?remove? trivial to regenerate
};

typedef enum
{
    CONTENTS_EMPTY = -1,
    CONTENTS_SOLID = -2,
    CONTENTS_WATER = -3,
    CONTENTS_SLIME = -4,
    CONTENTS_LAVA = -5,
    CONTENTS_SKY = -6,
    CONTENTS_ORIGIN = -7, // removed at csg time

    CONTENTS_CURRENT_0 = -9,
    CONTENTS_CURRENT_90 = -10,
    CONTENTS_CURRENT_180 = -11,
    CONTENTS_CURRENT_270 = -12,
    CONTENTS_CURRENT_UP = -13,
    CONTENTS_CURRENT_DOWN = -14,

    CONTENTS_TRANSLUCENT = -15,
    CONTENTS_HINT = -16, // Filters down to CONTENTS_EMPTY by bsp, ENGINE SHOULD NEVER SEE THIS

    CONTENTS_NULL = -17, // AJM  // removed in csg and bsp, VIS or RAD shouldnt have to deal with this, only clip planes!

    CONTENTS_BOUNDINGBOX = -19, // similar to CONTENTS_ORIGIN

    CONTENTS_TOEMPTY = -32,
} contents_t;

// !!! if this is changed, it must be changed in asm_i386.h too !!!
struct BSPLumpNode
{
    int planenum;
    short children[2]; // negative numbers are -(leafs+1), not nodes
    short mins[3];     // for sphere culling
    short maxs[3];
    unsigned short firstface;
    unsigned short numfaces; // counting both sides
};

struct BSPLumpClipnode
{
    int planenum;
    short children[2]; // negative numbers are contents
};

struct BSPLumpTexInfo
{
    float vecs[2][4]; // [s/t][xyz offset]
    int miptex;
    int flags;
};

constexpr int TEX_SPECIAL = 1; // sky or slime or null, no lightmap or 256 subdivision

// note that edge 0 is never used, because negative edge nums are used for
// counterclockwise use of the edge in a face
struct BSPLumpEdge
{
    unsigned short v[2]; // vertex numbers
};

constexpr int MAXLIGHTMAPS = 4;
struct BSPLumpFace
{
    unsigned short planenum;
    short side;

    int firstedge; // we must support > 64k edges
    short numedges;
    short texinfo;

    // lighting info
    byte styles[MAXLIGHTMAPS];
    int lightofs; // start of [numstyles*surfsize] samples
};

constexpr int AMBIENT_WATER = 0;
constexpr int AMBIENT_SKY = 1;
constexpr int AMBIENT_SLIME = 2;
constexpr int AMBIENT_LAVA = 3;

constexpr int NUM_AMBIENTS = 4; // automatic ambient sounds

// leaf 0 is the generic CONTENTS_SOLID leaf, used for all solid areas
// all other leafs need visibility info
struct BSPLumpLeaf
{
    int contents;
    int visofs; // -1 = no visibility info

    short mins[3]; // for frustum culling
    short maxs[3];

    unsigned short firstmarksurface;
    unsigned short nummarksurfaces;

    byte ambient_level[NUM_AMBIENTS];
};

//============================================================================

constexpr int ANGLE_UP = -1.0;   // #define ANGLE_UP    -1 //--vluzacn
constexpr int ANGLE_DOWN = -2.0; // #define ANGLE_DOWN  -2 //--vluzacn

//
// BSP File Data
//

extern int g_bspnummodels;
extern BSPLumpModel g_bspmodels[MAX_MAP_MODELS];
extern int g_bspmodels_checksum;

extern int g_bspvisdatasize;
extern byte g_bspvisdata[MAX_MAP_VISIBILITY];
extern int g_bspvisdata_checksum;

extern int g_bsplightdatasize;
extern byte *g_bsplightdata;
extern int g_bsplightdata_checksum;

extern int g_bsptexdatasize;
extern byte *g_bsptexdata; // (BSPLumpMiptexHeader)
extern int g_bsptexdata_checksum;

extern int g_bspentdatasize;
extern char g_bspentdata[MAX_MAP_ENTSTRING];
extern int g_bspentdata_checksum;

extern int g_bspnumleafs;
extern BSPLumpLeaf g_bspleafs[MAX_MAP_LEAFS];
extern int g_bspleafs_checksum;

extern int g_bspnumplanes;
extern dplane_t g_bspplanes[MAX_INTERNAL_MAP_PLANES]; // don't rename until remove winding.h g_dplanes macro
extern int g_bspplanes_checksum;

extern int g_bspnumvertexes;
extern BSPLumpVertex g_bspvertexes[MAX_MAP_VERTS];
extern int g_bspvertexes_checksum;

extern int g_bspnumnodes;
extern BSPLumpNode g_bspnodes[MAX_MAP_NODES];
extern int g_bspnodes_checksum;

extern int g_bspnumtexinfo;
extern BSPLumpTexInfo g_bsptexinfo[MAX_INTERNAL_MAP_TEXINFO];
extern int g_bsptexinfo_checksum;

extern int g_bspnumfaces;
extern BSPLumpFace g_bspfaces[MAX_MAP_FACES];
extern int g_bspfaces_checksum;

extern int g_iWorldExtent;

extern int g_bspnumclipnodes;
extern BSPLumpClipnode g_bspclipnodes[MAX_MAP_CLIPNODES];
extern int g_bspclipnodes_checksum;

extern int g_bspnumedges;
extern BSPLumpEdge g_bspedges[MAX_MAP_EDGES];
extern int g_bspedges_checksum;

extern int g_bspnummarksurfaces;
extern unsigned short g_bspmarksurfaces[MAX_MAP_MARKSURFACES];
extern int g_bspmarksurfaces_checksum;

extern int g_bspnumsurfedges;
extern int g_bspsurfedges[MAX_MAP_SURFEDGES];
extern int g_bspsurfedges_checksum;

extern void DecompressVis(const byte *src, byte *const dest, const unsigned int dest_length);
extern auto CompressVis(const byte *const src, const unsigned int src_length, byte *dest, unsigned int dest_length) -> int;

extern void LoadBSPImage(BSPLumpHeader *header);
extern void LoadBSPFile(const char *const filename);
extern void WriteBSPFile(const char *const filename);
extern void WriteExtentFile(const char *const filename);
extern auto CalcFaceExtents_test() -> bool;
extern void GetFaceExtents(int facenum, int mins_out[2], int maxs_out[2]);
extern auto ParseImplicitTexinfoFromTexture(int miptex) -> int;
extern auto ParseTexinfoForFace(const BSPLumpFace *f) -> int;
extern void DeleteEmbeddedLightmaps();

//
// Entity Related Stuff
//
struct EntityProperty
{
    struct EntityProperty *next;
    char *key;
    char *value;
};

struct Entity
{
    vec3_t origin;
    int firstbrush;
    int numbrushes;
    EntityProperty *epairs;
};

extern int g_numentities;
extern Entity g_entities[MAX_MAP_ENTITIES];

extern void ParseEntities();
extern void UnparseEntities();

extern void DeleteKey(Entity *ent, const char *const key);
extern void SetKeyValue(Entity *ent, const char *const key, const char *const value);
extern auto ValueForKey(const Entity *const ent, const char *const key) -> const char *;
extern auto IntForKey(const Entity *const ent, const char *const key) -> int;
extern auto FloatForKey(const Entity *const ent, const char *const key) -> vec_t;
extern void GetVectorForKey(const Entity *const ent, const char *const key, vec3_t vec);

extern auto FindTargetEntity(const char *const target) -> Entity *;
extern auto ParseEpair() -> EntityProperty *;
extern auto EntityForModel(int modnum) -> Entity *;

//
// Texture Related Stuff
//

extern int g_max_map_miptex;
extern int g_max_map_lightdata;
extern void dtexdata_init();
extern void dtexdata_free();

extern auto GetTextureByNumber(int texturenumber) -> char *;