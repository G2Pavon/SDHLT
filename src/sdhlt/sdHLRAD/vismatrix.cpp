#include "qrad.h"

////////////////////////////
// begin old vismat.c
//
#define HALFBIT

// =====================================================================================
//
//      VISIBILITY MATRIX
//      Determine which patches can see each other
//      Use the PVS to accelerate if available
//
// =====================================================================================

static byte *s_vismatrix;

// =====================================================================================
//  TestPatchToFace
//      Sets vis bits for all patches in the face
// =====================================================================================
static void TestPatchToFace(const unsigned patchnum, const int facenum, const int head, const unsigned int bitpos, byte *pvs)
{
	patch_t *patch = &g_patches[patchnum];
	patch_t *patch2 = g_face_patches[facenum];

	// if emitter is behind that face plane, skip all patches

	if (patch2)
	{
		const dplane_t *plane2 = getPlaneFromFaceNumber(facenum);

		if (DotProduct(patch->origin, plane2->normal) > PatchPlaneDist(patch2) + ON_EPSILON - patch->emitter_range)
		{
			// we need to do a real test
			const dplane_t *plane = getPlaneFromFaceNumber(patch->faceNumber);

			for (; patch2; patch2 = patch2->next)
			{
				unsigned m = patch2 - g_patches;

				vec3_t transparency = {1.0, 1.0, 1.0};
				int opaquestyle = -1;

				// check vis between patch and patch2
				// if bit has not already been set
				//  && v2 is not behind light plane
				//  && v2 is visible from v1
				if (m > patchnum)
				{
					if (patch2->leafnum == 0 || !(pvs[(patch2->leafnum - 1) >> 3] & (1 << ((patch2->leafnum - 1) & 7))))
					{
						continue;
					}
					vec3_t origin1, origin2;
					vec3_t delta;
					vec_t dist;
					VectorSubtract(patch->origin, patch2->origin, delta);
					dist = VectorLength(delta);
					if (dist < patch2->emitter_range - ON_EPSILON)
					{
						GetAlternateOrigin(patch->origin, plane->normal, patch2, origin2);
					}
					else
					{
						VectorCopy(patch2->origin, origin2);
					}
					if (DotProduct(origin2, plane->normal) <= PatchPlaneDist(patch) + MINIMUM_PATCH_DISTANCE)
					{
						continue;
					}
					if (dist < patch->emitter_range - ON_EPSILON)
					{
						GetAlternateOrigin(patch2->origin, plane2->normal, patch, origin1);
					}
					else
					{
						VectorCopy(patch->origin, origin1);
					}
					if (DotProduct(origin1, plane2->normal) <= PatchPlaneDist(patch2) + MINIMUM_PATCH_DISTANCE)
					{
						continue;
					}
					if (TestLine(
							origin1, origin2) != CONTENTS_EMPTY)
					{
						continue;
					}
					if (TestSegmentAgainstOpaqueList(
							origin1, origin2, transparency, opaquestyle))
					{
						continue;
					}

					if (opaquestyle != -1)
					{
						AddStyleToStyleArray(m, patchnum, opaquestyle);
						AddStyleToStyleArray(patchnum, m, opaquestyle);
					}
					// Log("SDF::3\n");

					// patchnum can see patch m
					unsigned bitset = bitpos + m;
					ThreadLock(); //--vluzacn
					s_vismatrix[bitset >> 3] |= 1 << (bitset & 7);
					ThreadUnlock(); //--vluzacn
				}
			}
		}
	}
}

// =====================================================================================
// BuildVisLeafs
//      This is run by multiple threads
// =====================================================================================
#ifdef SYSTEM_WIN32
#pragma warning(push)
#pragma warning(disable : 4100) // unreferenced formal parameter
#endif
static void BuildVisLeafs(int threadnum)
{
	int i;
	int facenum, facenum2;
	byte pvs[(MAX_MAP_LEAFS + 7) / 8];
	dleaf_t *srcleaf;
	patch_t *patch;
	int head;
	unsigned bitpos;
	unsigned patchnum;

	while (1)
	{
		//
		// build a minimal BSP tree that only
		// covers areas relevent to the PVS
		//
		i = GetThreadWork();
		if (i == -1)
			break;
		i++; // skip leaf 0
		srcleaf = &g_dleafs[i];
		if (!g_visdatasize)
		{
			memset(pvs, 255, (g_dmodels[0].visleafs + 7) / 8);
		}
		else
		{
			if (srcleaf->visofs == -1)
			{
				continue;
			}
			DecompressVis(&g_dvisdata[srcleaf->visofs], pvs, sizeof(pvs));
		}
		head = 0;

		//
		// go through all the faces inside the
		// leaf, and process the patches that
		// actually have origins inside
		//
		for (facenum = 0; facenum < g_numfaces; facenum++)
		{
			for (patch = g_face_patches[facenum]; patch; patch = patch->next)
			{
				if (patch->leafnum != i)
					continue;
				patchnum = patch - g_patches;
#ifdef HALFBIT
				bitpos = patchnum * g_num_patches - (patchnum * (patchnum + 1)) / 2;
#else
				bitpos = patchnum * g_num_patches;
#endif
				for (facenum2 = facenum + 1; facenum2 < g_numfaces; facenum2++)
					TestPatchToFace(patchnum, facenum2, head, bitpos, pvs);
			}
		}
	}
}

#ifdef SYSTEM_WIN32
#pragma warning(pop)
#endif