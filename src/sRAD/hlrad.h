#pragma once
#include <vector>
#include <string>

#include "compress.h"
#include "mathlib.h"
#include "messages.h"
#include "win32fix.h"
#include "winding.h"

constexpr float DEFAULT_FADE = 1.0f;
constexpr int DEFAULT_BOUNCE = 8;
// 188 is the fullbright threshold for Goldsrc before 25th anniversary, regardless of the brightness and gamma settings in the graphic options. This is no longer necessary
// However, hlrad can only control the light values of each single light style. So the final in-game brightness may exceed 188 if you have set a high value in the "custom appearance" of the light, or if the face receives light from different styles.
constexpr float DEFAULT_LIMITTHRESHOLD = 255.0f; // We override to 188 with pre25 argument. //seedee
constexpr float DEFAULT_CHOP = 64.0f;
constexpr float DEFAULT_TEXCHOP = 32.0f;
constexpr float DEFAULT_LIGHTSCALE = 2.0f; // 1.0 //vluzacn
constexpr int DEFAULT_SMOOTHING2_VALUE = 0;

// ------------------------------------------------------------------------
// Changes by Adam Foster - afoster@compsoc.man.ac.uk

// superseded by DEFAULT_COLOUR_LIGHTSCALE_*

// superseded by DEFAULT_COLOUR_GAMMA_*
// ------------------------------------------------------------------------

constexpr bool DEFAULT_EXTRA = false;
constexpr bool DEFAULT_INFO = true;

constexpr float_type DEFAULT_TRANSFER_COMPRESS_TYPE = FLOAT16;
constexpr vector_type DEFAULT_RGBTRANSFER_COMPRESS_TYPE = VECTOR32;
constexpr float DEFAULT_TRANSLUCENTDEPTH = 2.0f;
constexpr float DEFAULT_BLUR = 1.5f; // classic lighting is equivalent to "-blur 1.0"
constexpr bool DEFAULT_EMBEDLIGHTMAP_POWEROFTWO = true;
constexpr float DEFAULT_EMBEDLIGHTMAP_DENOMINATOR = 188.0f;
constexpr float DEFAULT_EMBEDLIGHTMAP_GAMMA = 1.05f;
constexpr int DEFAULT_EMBEDLIGHTMAP_RESOLUTION = 1;

constexpr bool DEFAULT_ESTIMATE = true;

// Ideally matches what is in the FGD :)
constexpr auto SPAWNFLAG_NOBLEEDADJUST = 1 << 0;

// DEFAULT_HUNT_OFFSET is how many units in front of the plane to place the samples
// Unit of '1' causes the 1 unit crate trick to cause extra shadows
constexpr float DEFAULT_HUNT_OFFSET = 0.5f;
// DEFAULT_HUNT_SIZE number of iterations (one based) of radial search in HuntForWorld
constexpr int DEFAULT_HUNT_SIZE = 11;
// DEFAULT_HUNT_SCALE amount to grow from origin point per iteration of DEFAULT_HUNT_SIZE in HuntForWorld
constexpr float DEFAULT_HUNT_SCALE = 0.1f;
constexpr float DEFAULT_EDGE_WIDTH = 0.8f;

constexpr float PATCH_HUNT_OFFSET = 0.5f;			//--vluzacn
constexpr float HUNT_WALL_EPSILON = 3 * ON_EPSILON; // place sample at least this distance away from any wall //--vluzacn

constexpr float MINIMUM_PATCH_DISTANCE = ON_EPSILON;
constexpr float ACCURATEBOUNCE_THRESHOLD = 4.0;	   // If the receiver patch is closer to emitter patch than EXACTBOUNCE_THRESHOLD * emitter_patch->radius, calculate the exact visibility amount.
constexpr int ACCURATEBOUNCE_DEFAULT_SKYLEVEL = 5; // sample 1026 normals

constexpr int ALLSTYLES = 64; // HL limit. //--vluzacn

constexpr int RAD_BOGUS_RANGE = 131072;

struct Matrix
{
	vec_t v[4][3];
};

// a 4x4 matrix that represents the following transformation (see the ApplyMatrix function)
//
//  / X \    / v[0][0] v[1][0] v[2][0] v[3][0] \ / X \.
//  | Y | -> | v[0][1] v[1][1] v[2][1] v[3][1] | | Y |
//  | Z |    | v[0][2] v[1][2] v[2][2] v[3][2] | | Z |
//  \ 1 /    \    0       0       0       1    / \ 1 /

//
// LIGHTMAP.C STUFF
//

typedef enum
{
	emit_surface,
	emit_point,
	emit_spotlight,
	emit_skylight
} emittype_t;

struct DirectLight
{
	struct DirectLight *next;
	emittype_t type;
	int style;
	vec3_t origin;
	vec3_t intensity;
	vec3_t normal;	// for surfaces and spotlights
	float stopdot;	// for spotlights
	float stopdot2; // for spotlights

	// 'Arghrad'-like features
	vec_t fade; // falloff scaling for linear and inverse square falloff 1.0 = normal, 0.5 = farther, 2.0 = shorter etc

	// -----------------------------------------------------------------------------------
	// Changes by Adam Foster - afoster@compsoc.man.ac.uk
	// Diffuse light_environment light colour
	// Really horrible hack which probably won't work!
	vec3_t diffuse_intensity;
	// -----------------------------------------------------------------------------------
	vec3_t diffuse_intensity2;
	vec_t sunspreadangle;
	int numsunnormals;
	vec3_t *sunnormals;
	vec_t *sunnormalweights;

	vec_t patch_area;
	vec_t patch_emitter_range;
	struct Patch *patch;
	vec_t texlightgap;
	bool topatch;
};

struct TransferIndex
{
	unsigned size : 12;
	unsigned index : 20;
};

typedef unsigned transfer_raw_index_t;
typedef unsigned char transfer_data_t;

typedef unsigned char rgb_transfer_data_t;

constexpr int MAX_COMPRESSED_TRANSFER_INDEX_SIZE = (1 << 12) - 1;

constexpr int MAX_PATCHES = 65535 * 16; // limited by TransferIndex
constexpr int MAX_VISMATRIX_PATCHES = 65535;
constexpr int MAX_SPARSE_VISMATRIX_PATCHES = MAX_PATCHES;

typedef enum
{
	ePatchFlagNull = 0,
	ePatchFlagOutside = 1
} ePatchFlags;

struct Patch
{
	struct Patch *next; // next in face
	vec3_t origin;		// Center centroid of winding (cached info calculated from winding)
	vec_t area;			// Surface area of this patch (cached info calculated from winding)
	vec_t exposure;
	vec_t emitter_range;  // Range from patch origin (cached info calculated from winding)
	int emitter_skylevel; // The "skylevel" used for sampling of normals, when the receiver patch is within the range of ACCURATEBOUNCE_THRESHOLD * this->radius. (cached info calculated from winding)
	Winding *winding;	  // Winding (patches are triangles, so its easy)
	vec_t scale;		  // Texture scale for this face (blend of S and T scale)
	vec_t chop;			  // Texture chop for this face factoring in S and T scale

	unsigned iIndex;
	unsigned iData;

	TransferIndex *tIndex;
	transfer_data_t *tData;
	rgb_transfer_data_t *tRGBData;

	int faceNumber;
	ePatchFlags flags;
	bool translucent_b; // gather light from behind
	vec3_t translucent_v;
	vec3_t texturereflectivity;
	vec3_t bouncereflectivity;

	unsigned char totalstyle[MAXLIGHTMAPS];
	unsigned char directstyle[MAXLIGHTMAPS];
	// HLRAD_AUTOCORING: totallight: all light gathered by patch
	vec3_t totallight[MAXLIGHTMAPS]; // accumulated by radiosity does NOT include light accounted for by direct lighting
	// HLRAD_AUTOCORING: directlight: emissive light gathered by sample
	vec3_t directlight[MAXLIGHTMAPS]; // direct light only
	int bouncestyle;				  // light reflected from this patch must convert to this style. -1 = normal (don't convert)
	unsigned char emitstyle;
	vec3_t baselight; // emissivity only, uses emitstyle
	bool emitmode;	  // texlight emit mode. 1 for normal, 0 for fast.
	vec_t samples;
	vec3_t *samplelight_all;	   // NULL, or [ALLSTYLES] during BuildFacelights
	unsigned char *totalstyle_all; // NULL, or [ALLSTYLES] during BuildFacelights
	vec3_t *totallight_all;		   // NULL, or [ALLSTYLES] during BuildFacelights
	vec3_t *directlight_all;	   // NULL, or [ALLSTYLES] during BuildFacelights
	int leafnum;
};

// LRC
auto GetTotalLight(Patch *patch, int style) -> vec3_t *;

struct FaceList
{
	BSPLumpFace *face;
	FaceList *next;
};
struct EdgeShare
{
	BSPLumpFace *faces[2];
	vec3_t interface_normal; // HLRAD_GetPhongNormal_VL: this field must be set when smooth==true
	vec3_t vertex_normal[2];
	vec_t cos_normals_angle; // HLRAD_GetPhongNormal_VL: this field must be set when smooth==true
	bool coplanar;
	bool smooth;
	FaceList *vertex_facelist[2]; // possible smooth faces, not include faces[0] and faces[1]
	Matrix textotex[2];			  // how we translate texture coordinates from one face to the other face
};

extern EdgeShare g_edgeshare[MAX_MAP_EDGES];

//
// lerp.c stuff
//

// These are bitflags for lighting adjustments for special cases
typedef enum
{
	eModelLightmodeNull = 0,
	eModelLightmodeOpaque = 0x02,
	eModelLightmodeNonsolid = 0x08, // for opaque entities with {texture
} eModelLightmodes;

struct OpaqueList
{
	int entitynum;
	int modelnum;
	vec3_t origin;

	vec3_t transparency_scale;
	bool transparency;
	int style; // -1 = no style; transparency must be false if style >= 0
	// style0 and same style will change to this style, other styles will be blocked.
	bool block; // this entity can't be seen inside, so all lightmap sample should move outside.
};

constexpr int OPAQUE_ARRAY_GROWTH_SIZE = 1024;

struct RADTexture
{
	char name[16]; // not always same with the name in texdata
	int width, height;
	byte *canvas; //[height][width]
	byte palette[256][3];
	vec3_t reflectivity;
};
extern int g_numtextures;
extern RADTexture *g_textures;
extern void LoadTextures();
extern void EmbedLightmapInTextures();

struct MinLight
{
	std::string name;
	float value;
}; // info_minlights

typedef std::vector<MinLight>::iterator minlight_i;

//
// hlrad globals
//

extern std::vector<MinLight> s_minlights;
extern Patch *g_face_patches[MAX_MAP_FACES];
extern Entity *g_face_entity[MAX_MAP_FACES];
extern vec3_t g_face_offset[MAX_MAP_FACES]; // for models with origins
extern eModelLightmodes g_face_lightmode[MAX_MAP_FACES];
extern vec3_t g_face_centroids[MAX_MAP_EDGES];
extern Entity *g_face_texlights[MAX_MAP_FACES];
extern Patch *g_patches; // shrinked to its real size, because 1048576 patches * 256 bytes = 256MB will be too big
extern unsigned g_num_patches;

extern float g_lightscale;

extern void MakeShadowSplits();

//==============================================

extern bool g_extra;
extern vec_t g_limitthreshold;
extern unsigned g_numbounce;
extern float g_qgamma;
extern float g_smoothing_threshold;

extern float g_smoothing_threshold_2;
extern vec_t *g_smoothvalues; //[nummiptex]
extern bool g_estimate;
extern char g_source[_MAX_PATH];
extern vec_t g_fade;
extern vec_t g_chop;	// Chop value for normal textures
extern vec_t g_texchop; // Chop value for texture lights
extern OpaqueList *g_opaque_face_list;
extern unsigned g_opaque_face_count;	 // opaque entity count //HLRAD_OPAQUE_NODE
extern unsigned g_max_opaque_face_count; // Current array maximum (used for reallocs)

extern const vec3_t vec3_one;

extern float_type g_transfer_compress_type;
extern vector_type g_rgbtransfer_compress_type;
extern float g_corings[ALLSTYLES];
extern int g_stylewarningcount; // not thread safe
extern int g_stylewarningnext;	// not thread safe
extern vec3_t *g_translucenttextures;
extern vec_t g_translucentdepth;
extern vec3_t *g_lightingconeinfo; //[nummiptex]; X component = power, Y component = scale, Z component = nothing
extern vec_t g_blur;
extern vec_t g_maxdiscardedlight;
extern vec3_t g_maxdiscardedpos;

extern void MakeTnodes(BSPLumpModel *bm);
extern void PairEdges();
constexpr int SKYLEVELMAX = 8;
constexpr int SKYLEVEL_SOFTSKYON = 7;
constexpr int SKYLEVEL_SOFTSKYOFF = 4;
constexpr int SUNSPREAD_SKYLEVEL = 7;
constexpr int SUNSPREAD_THRESHOLD = 15.0;
extern int g_numskynormals[SKYLEVELMAX + 1];	 // 0, 6, 18, 66, 258, 1026, 4098, 16386, 65538
extern vec3_t *g_skynormals[SKYLEVELMAX + 1];	 //[numskynormals]
extern vec_t *g_skynormalsizes[SKYLEVELMAX + 1]; // the weight of each normal
extern void BuildDiffuseNormals();
extern void BuildFacelights(int facenum);
extern void PrecompLightmapOffsets();
extern void ReduceLightmap();
extern void FinalLightFace(int facenum);
extern void ScaleDirectLights();			 // run before AddPatchLights
extern void CreateFacelightDependencyList(); // run before AddPatchLights
extern void AddPatchLights(int facenum);
extern void FreeFacelightDependencyList();
extern auto TestLine(const vec3_t start, const vec3_t stop, vec_t *skyhitout = nullptr) -> int;

struct OpaqueModel
{
	vec3_t mins, maxs;
	int headnode;
};
extern OpaqueModel *opaquemodels;

extern void CreateOpaqueNodes();
extern auto TestLineOpaque(int modelnum, const vec3_t modelorigin, const vec3_t start, const vec3_t stop) -> int;
extern auto CountOpaqueFaces(int modelnum) -> int;
extern void DeleteOpaqueNodes();
extern auto TestPointOpaque_r(int nodenum, bool solid, const vec3_t point) -> int;
FORCEINLINE int TestPointOpaque(int modelnum, const vec3_t modelorigin, bool solid, const vec3_t point) // use "forceinline" because "inline" does nothing here (TODO: move to trace.cpp)
{
	OpaqueModel *thismodel = &opaquemodels[modelnum];
	vec3_t newpoint;
	VectorSubtract(point, modelorigin, newpoint);
	int axial;
	for (axial = 0; axial < 3; axial++)
	{
		if (newpoint[axial] > thismodel->maxs[axial])
			return 0;
		if (newpoint[axial] < thismodel->mins[axial])
			return 0;
	}
	return TestPointOpaque_r(thismodel->headnode, solid, newpoint);
}

extern void CreateDirectLights();
extern void DeleteDirectLights();
extern void GetPhongNormal(int facenum, const vec3_t spot, vec3_t phongnormal); // added "const" --vluzacn

typedef bool (*funcCheckVisBit)(unsigned, unsigned, vec3_t &, unsigned int &);
extern funcCheckVisBit g_CheckVisBit;
extern auto CheckVisBitBackwards(unsigned receiver, unsigned emitter, const vec3_t &backorigin, const vec3_t &backnormal, vec3_t &transparency_out) -> bool;
extern void MdlLightHack();

// hlradutil.c
extern auto PatchPlaneDist(const Patch *const patch) -> vec_t;
extern auto PointInLeaf(const vec3_t point) -> BSPLumpLeaf *;
extern void MakeBackplanes();
extern auto getPlaneFromFace(const BSPLumpFace *const face) -> const dplane_t *;
extern auto getPlaneFromFaceNumber(unsigned int facenum) -> const dplane_t *;
extern void getAdjustedPlaneFromFaceNumber(unsigned int facenum, dplane_t *plane);
extern auto HuntForWorld(vec_t *point, const vec_t *plane_offset, const dplane_t *plane, int hunt_size, vec_t hunt_scale, vec_t hunt_offset) -> BSPLumpLeaf *;
extern void ApplyMatrix(const Matrix &m, const vec3_t in, vec3_t &out);
extern void ApplyMatrixOnPlane(const Matrix &m_inverse, const vec3_t in_normal, vec_t in_dist, vec3_t &out_normal, vec_t &out_dist);
extern void MultiplyMatrix(const Matrix &m_left, const Matrix &m_right, Matrix &m);
extern auto MultiplyMatrix(const Matrix &m_left, const Matrix &m_right) -> Matrix;
extern void MatrixForScale(const vec3_t center, vec_t scale, Matrix &m);
extern auto MatrixForScale(const vec3_t center, vec_t scale) -> Matrix;
extern auto CalcMatrixSign(const Matrix &m) -> vec_t;
extern void TranslateWorldToTex(int facenum, Matrix &m);
extern auto InvertMatrix(const Matrix &m, Matrix &m_inverse) -> bool;
extern void FindFacePositions(int facenum);
extern void FreePositionMaps();
extern auto FindNearestPosition(int facenum, const Winding *texwinding, const dplane_t &texplane, vec_t s, vec_t t, vec3_t &pos, vec_t *best_s, vec_t *best_t, vec_t *best_dist, bool *nudged) -> bool;

// makescales.c
extern void MakeScalesSparseVismatrix();

// transfers.c
extern size_t g_total_transfer;
extern auto readtransfers(const char *const transferfile, long numpatches) -> bool;
extern void writetransfers(const char *const transferfile, long total_patches);

// vismatrixutil.c (shared between vismatrix.c and sparse.c)
extern void MakeScales(int threadnum);
extern void DumpTransfersMemoryUsage();
extern void MakeRGBScales(int threadnum);

// transparency.c (transparency array functions - shared between vismatrix.c and sparse.c)
extern void GetTransparency(const unsigned p1, const unsigned p2, vec3_t &trans, unsigned int &next_index);
extern void AddTransparencyToRawArray(const unsigned p1, const unsigned p2, const vec3_t trans);
extern void CreateFinalTransparencyArrays(const char *print_name);
extern void FreeTransparencyArrays();
extern void GetStyle(const unsigned p1, const unsigned p2, int &style, unsigned int &next_index);
extern void AddStyleToStyleArray(const unsigned p1, const unsigned p2, const int style);
extern void CreateFinalStyleArrays(const char *print_name);
extern void FreeStyleArrays();

// lerp.c
extern void CreateTriangulations(int facenum);
extern void GetTriangulationPatches(int facenum, int *numpatches, const int **patches);
extern void InterpolateSampleLight(const vec3_t position, int surface, int numstyles, const int *styles, vec3_t *outs);
extern void FreeTriangulations();

// mathutil.c
extern auto TestSegmentAgainstOpaqueList(const vec_t *p1, const vec_t *p2, vec3_t &scaleout, int &opaquestyleout) -> bool;
extern auto intersect_line_plane(const dplane_t *const plane, const vec_t *const p1, const vec_t *const p2, vec3_t point) -> bool;
extern auto intersect_linesegment_plane(const dplane_t *const plane, const vec_t *const p1, const vec_t *const p2, vec3_t point) -> bool;
extern void plane_from_points(const vec3_t p1, const vec3_t p2, const vec3_t p3, dplane_t *plane);
extern auto point_in_winding(const Winding &w, const dplane_t &plane, const vec_t *point, vec_t epsilon = 0.0) -> bool;
extern auto point_in_winding_noedge(const Winding &w, const dplane_t &plane, const vec_t *point, vec_t width) -> bool;
extern void snap_to_winding(const Winding &w, const dplane_t &plane, vec_t *point);
extern auto snap_to_winding_noedge(const Winding &w, const dplane_t &plane, vec_t *point, vec_t width, vec_t maxmove) -> vec_t;
extern void SnapToPlane(const dplane_t *const plane, vec_t *const point, vec_t offset);
extern auto CalcSightArea(const vec3_t receiver_origin, const vec3_t receiver_normal, const Winding *emitter_winding, int skylevel, vec_t lighting_power, vec_t lighting_scale) -> vec_t;
extern auto CalcSightArea_SpotLight(const vec3_t receiver_origin, const vec3_t receiver_normal, const Winding *emitter_winding, const vec3_t emitter_normal, vec_t emitter_stopdot, vec_t emitter_stopdot2, int skylevel, vec_t lighting_power, vec_t lighting_scale) -> vec_t;
extern void GetAlternateOrigin(const vec3_t pos, const vec3_t normal, const Patch *patch, vec3_t &origin);