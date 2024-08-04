#include <string>
#include <cstring>

#include "maplib.h"
#include "csg.h"
#include "blockmem.h"
#include "log.h"

int g_nummapbrushes;
Brush g_mapbrushes[MAX_MAP_BRUSHES];

int g_numbrushsides;
Side g_brushsides[MAX_MAP_SIDES];

static const vec3_t s_baseaxis[18] = {
	{0, 0, 1}, {1, 0, 0}, {0, -1, 0}, // floor
	{0, 0, -1},
	{1, 0, 0},
	{0, -1, 0}, // ceiling
	{1, 0, 0},
	{0, 1, 0},
	{0, 0, -1}, // west wall
	{-1, 0, 0},
	{0, 1, 0},
	{0, 0, -1}, // east wall
	{0, 1, 0},
	{1, 0, 0},
	{0, 0, -1}, // south wall
	{0, -1, 0},
	{1, 0, 0},
	{0, 0, -1}, // north wall
};

int g_numparsedentities;
int g_numparsedbrushes;

auto CopyCurrentBrush(Entity *entity, const Brush *brush) -> Brush *
{
	if (entity->firstbrush + entity->numbrushes != g_nummapbrushes)
	{
		Error("CopyCurrentBrush: internal error.");
	}
	auto newb = &g_mapbrushes[g_nummapbrushes];
	g_nummapbrushes++;
	hlassume(g_nummapbrushes <= MAX_MAP_BRUSHES, assume_MAX_MAP_BRUSHES);
	memcpy(newb, brush, sizeof(Brush));
	newb->firstside = g_numbrushsides;
	g_numbrushsides += brush->numsides;
	hlassume(g_numbrushsides <= MAX_MAP_SIDES, assume_MAX_MAP_SIDES);
	memcpy(&g_brushsides[newb->firstside], &g_brushsides[brush->firstside], brush->numsides * sizeof(Side));
	newb->entitynum = entity - g_entities;
	newb->brushnum = entity->numbrushes;
	entity->numbrushes++;
	for (int h = 0; h < NUM_HULLS; h++)
	{
		if (brush->hullshapes[h] != nullptr)
		{
			newb->hullshapes[h] = strdup(brush->hullshapes[h]);
		}
		else
		{
			newb->hullshapes[h] = nullptr;
		}
	}
	return newb;
}

void DeleteCurrentEntity(Entity *entity)
{
	if (entity != &g_entities[g_numentities - 1])
	{
		Error("DeleteCurrentEntity: internal error.");
	}
	if (entity->firstbrush + entity->numbrushes != g_nummapbrushes)
	{
		Error("DeleteCurrentEntity: internal error.");
	}
	for (int i = entity->numbrushes - 1; i >= 0; i--)
	{
		auto b = &g_mapbrushes[entity->firstbrush + i];
		if (b->firstside + b->numsides != g_numbrushsides)
		{
			Error("DeleteCurrentEntity: internal error. (Entity %i, Brush %i)",
				  b->originalentitynum, b->originalbrushnum);
		}
		memset(&g_brushsides[b->firstside], 0, b->numsides * sizeof(Side));
		g_numbrushsides -= b->numsides;
		for (int h = 0; h < NUM_HULLS; h++)
		{
			if (b->hullshapes[h])
			{
				delete b->hullshapes[h];
			}
		}
	}
	memset(&g_mapbrushes[entity->firstbrush], 0, entity->numbrushes * sizeof(Brush));
	g_nummapbrushes -= entity->numbrushes;
	while (entity->epairs)
	{
		DeleteKey(entity, entity->epairs->key);
	}
	memset(entity, 0, sizeof(Entity));
	g_numentities--;
}
// =====================================================================================
//  TextureAxisFromPlane
// =====================================================================================
void TextureAxisFromPlane(const Plane *const pln, vec3_t xv, vec3_t yv)
{
	auto bestaxis = 0;
	auto best = 0.0;

	for (int i = 0; i < 6; i++)
	{
		auto dot = DotProduct(pln->normal, s_baseaxis[i * 3]);
		if (dot > best)
		{
			best = dot;
			bestaxis = i;
		}
	}

	VectorCopy(s_baseaxis[bestaxis * 3 + 1], xv);
	VectorCopy(s_baseaxis[bestaxis * 3 + 2], yv);
}

#define ScaleCorrection (1.0 / 128.0)

// =====================================================================================
// These Face* functions are more like ProcessFace(), so i think they should called outside ParseFace

void FaceCheckToolTextures(Brush *b, Side *s)
{
	if (!strncasecmp(g_token, "NOCLIP", 6) || !strncasecmp(g_token, "NULLNOCLIP", 10))
	{
		strcpy(g_token, "NULL");
		b->noclip = true;
	}
	if (!strncasecmp(g_token, "BEVELBRUSH", 10))
	{
		strcpy(g_token, "NULL");
		b->bevel = true;
	}
	if (!strncasecmp(g_token, "BEVEL", 5))
	{
		strcpy(g_token, "NULL");
		s->bevel = true;
	}
	if (!strncasecmp(g_token, "BEVELHINT", 9))
	{
		s->bevel = true;
	}
	if (!strncasecmp(g_token, "CLIP", 4))
	{
		b->cliphull |= (1 << NUM_HULLS); // arbitrary nonexistent hull
		int h;
		if (!strncasecmp(g_token, "CLIPHULL", 8) && (h = g_token[8] - '0', 0 < h && h < NUM_HULLS))
		{
			b->cliphull |= (1 << h); // hull h
		}
		if (!strncasecmp(g_token, "CLIPBEVEL", 9))
		{
			s->bevel = true;
		}
		if (!strncasecmp(g_token, "CLIPBEVELBRUSH", 14))
		{
			b->bevel = true;
		}
		strcpy(g_token, "SKIP");
	}
}
// End of Face* functions, now is time to parse face
// =====================================================================================

void ParseFace(Brush *b, Side *s)
{
	// read the three point plane definition
	for (int i = 0; i < 3; i++) // Read 3 point plane definition for brush side
	{
		if (i != 0) // If not the first point get next token
		{
			GetToken(true);
		}
		if (strcmp(g_token, "(")) // Token must be '('
		{
			Error("Parsing Entity %i, Brush %i, Side %i : Expecting '(' got '%s'",
				  b->originalentitynum, b->originalbrushnum,
				  b->numsides, g_token);
		}
		for (int j = 0; j < 3; j++) // Get three coords for the point
		{
			GetToken(false);				   // Get next token on same line
			s->planepts[i][j] = atof(g_token); // Convert token to float and store in planepts
		}
		GetToken(false);

		if (strcmp(g_token, ")"))
		{
			Error("Parsing	Entity %i, Brush %i, Side %i : Expecting ')' got '%s'",
				  b->originalentitynum, b->originalbrushnum,
				  b->numsides, g_token);
		}
	}

	// read the texturedef
	GetToken(false);
	strupr(g_token);

	FaceCheckToolTextures(b, s); // Check for tool textures

	safe_strncpy(s->texture.name, g_token, sizeof(s->texture.name));

	// texture U axis
	GetToken(false);
	if (strcmp(g_token, "["))
	{
		hlassume(false, assume_MISSING_BRACKET_IN_TEXTUREDEF);
	}

	GetToken(false);
	s->texture.UAxis[0] = atof(g_token);
	GetToken(false);
	s->texture.UAxis[1] = atof(g_token);
	GetToken(false);
	s->texture.UAxis[2] = atof(g_token);
	GetToken(false);
	s->texture.shift[0] = atof(g_token);

	GetToken(false);
	if (strcmp(g_token, "]"))
	{
		Error("missing ']' in texturedef (U)");
	}

	// texture V axis
	GetToken(false);
	if (strcmp(g_token, "["))
	{
		Error("missing '[' in texturedef (V)");
	}

	GetToken(false);
	s->texture.VAxis[0] = atof(g_token);
	GetToken(false);
	s->texture.VAxis[1] = atof(g_token);
	GetToken(false);
	s->texture.VAxis[2] = atof(g_token);
	GetToken(false);
	s->texture.shift[1] = atof(g_token);

	GetToken(false);
	if (strcmp(g_token, "]"))
	{
		Error("missing ']' in texturedef (V)");
	}

	// Texture rotation is implicit in U/V axes.
	GetToken(false);
	s->texture.rotate = 0;

	// texure scale
	GetToken(false);
	s->texture.scale[0] = atof(g_token);
	GetToken(false);
	s->texture.scale[1] = atof(g_token);
}

// =====================================================================================
// These Brush* functions are more like ProcessBrush(), so i think they should called outside ParseBrush

static auto BrushCheckZHLT_Invisible(Entity *mapent) -> bool
{
	using namespace std;

	string keyval(ValueForKey(mapent, "classname"));
	keyval.assign(ValueForKey(mapent, "zhlt_invisible"));
	if (!keyval.empty() && strcmp(keyval.c_str(), "0"))
	{
		return true;
	}

	return false;
}

void BrushNullify(Brush *b, Side *s, bool isInvisible)
{
	for (int i = 0; i < b->numsides; i++)
	{
		s = &g_brushsides[b->firstside + i];
		if (isInvisible && strncasecmp(s->texture.name, "BEVEL", 5) && strncasecmp(s->texture.name, "ORIGIN", 6) && strncasecmp(s->texture.name, "HINT", 4) && strncasecmp(s->texture.name, "SKIP", 4) && strncasecmp(s->texture.name, "SOLIDHINT", 9) && strncasecmp(s->texture.name, "BEVELHINT", 9) && strncasecmp(s->texture.name, "SPLITFACE", 9) && strncasecmp(s->texture.name, "BOUNDINGBOX", 11) && strncasecmp(s->texture.name, "CONTENT", 7) && strncasecmp(s->texture.name, "SKY", 3))
		{
			safe_strncpy(s->texture.name, "NULL", sizeof(s->texture.name));
		}
		if (!strncasecmp(s->texture.name, "CONTENT", 7))
		{
			strcpy(s->texture.name, "NULL");
		}
		if (!strncasecmp(s->texture.name, "AAATRIGGER", 10))
		{
			strcpy(s->texture.name, "NULL");
		}
	}
}

void BrushConvertSPLITFACE(Brush *b, Side *s)
{
	for (int i = 0; i < b->numsides; i++)
	{
		s = &g_brushsides[b->firstside + i];
		if (!strncasecmp(s->texture.name, "SPLITFACE", 9))
		{
			strcpy(s->texture.name, "SKIP");
		}
	}
}

void BrushCheckZHLT_noclip(Entity *e, Brush *b)
{
	if (IntForKey(e, "zhlt_noclip")) // If zhlt_noclip
	{
		b->noclip = 1;
	}
}

void BrushCheckFunc_detail(Entity *e, Brush *b)
{
	if (!strcmp(ValueForKey(e, "classname"), "func_detail"))
	{
		b->detaillevel = IntForKey(e, "zhlt_detaillevel");
		b->chopdown = IntForKey(e, "zhlt_chopdown");
		b->chopup = IntForKey(e, "zhlt_chopup");
		b->clipnodedetaillevel = IntForKey(e, "zhlt_clipnodedetaillevel");
		b->coplanarpriority = IntForKey(e, "zhlt_coplanarpriority");
		bool wrong = false;

		if (b->detaillevel < 0)
		{
			wrong = true;
			b->detaillevel = 0;
		}
		if (b->chopdown < 0)
		{
			wrong = true;
			b->chopdown = 0;
		}
		if (b->chopup < 0)
		{
			wrong = true;
			b->chopup = 0;
		}
		if (b->clipnodedetaillevel < 0)
		{
			wrong = true;
			b->clipnodedetaillevel = 0;
		}
		if (wrong)
		{
			Warning("Entity %i, Brush %i: incorrect settings for detail brush.",
					b->originalentitynum, b->originalbrushnum);
		}
	}
}

void BrushCheckZHLT_hull(Entity *e, Brush *b)
{
	for (int h = 0; h < NUM_HULLS; h++) // Loop through all hulls
	{
		char key[16];					// Key name for the hull shape.
		sprintf(key, "zhlt_hull%d", h); // Format key name to include the hull number, used to look up hull shape data in entity properties
		auto value = ValueForKey(e, key);

		if (*value) // If we have a value associated with the key from the entity properties copy the value to brush's hull shape for this hull
		{
			b->hullshapes[h] = strdup(value);
		}
		else // Set brush hull shape for this hull to NULL
		{
			b->hullshapes[h] = nullptr;
		}
	}
}

void BrushCheckZHLT_usemodel(Entity *e, Brush *b)
{
	if (*ValueForKey(&g_entities[b->entitynum], "zhlt_usemodel"))
	{
		memset(&g_brushsides[b->firstside], 0, b->numsides * sizeof(Side));
		g_numbrushsides -= b->numsides;
		for (int h = 0; h < NUM_HULLS; h++)
		{
			if (b->hullshapes[h])
			{
				delete b->hullshapes[h];
			}
		}
		memset(b, 0, sizeof(Brush));
		g_nummapbrushes--;
		e->numbrushes--;
		return;
	}
}

void BrushCheckInfo_hullshape(Entity *e, Brush *b)
{
	if (!strcmp(ValueForKey(&g_entities[b->entitynum], "classname"), "info_hullshape"))
	{
		// all brushes should be erased, but not now.
		return;
	}
}

void BushCheckClipTexture(Brush *b)
{
	if (b->cliphull != 0) // has CLIP* texture
	{
		unsigned int mask_anyhull = 0;
		for (int h = 1; h < NUM_HULLS; h++)
		{
			mask_anyhull |= (1 << h);
		}
		if ((b->cliphull & mask_anyhull) == 0) // no CLIPHULL1 or CLIPHULL2 or CLIPHULL3 texture
		{
			b->cliphull |= mask_anyhull; // CLIP all hulls
		}
	}
}

void BrushCheckORIGINtexture(Entity *e, Brush *b)
{
	auto content = b->contents;
	if (content == CONTENTS_ORIGIN)
	{
		if (*ValueForKey(e, "origin"))
		{
			Error("Entity %i, Brush %i: Only one ORIGIN brush allowed.",
				  b->originalentitynum, b->originalbrushnum);
		}
		char string[MAXTOKEN];
		vec3_t origin;

		b->contents = contents_t::CONTENTS_SOLID;
		CreateBrush(e->firstbrush + b->brushnum); // to get sizes
		b->contents = content;

		for (int i = 0; i < NUM_HULLS; i++)
		{
			b->hulls[i].faces = nullptr;
		}

		if (b->entitynum != 0) // Ignore for WORLD (code elsewhere enforces no ORIGIN in world message)
		{
			VectorAdd(b->hulls[0].bounds.m_Mins, b->hulls[0].bounds.m_Maxs, origin);
			VectorScale(origin, 0.5, origin);

			safe_snprintf(string, MAXTOKEN, "%i %i %i", (int)origin[0], (int)origin[1], (int)origin[2]);
			SetKeyValue(&g_entities[b->entitynum], "origin", string);
		}
	}
}

void BrushCheckBOUNDINGBOXtexture(Entity *e, Brush *b)
{
	auto content = b->contents;
	if (content == CONTENTS_BOUNDINGBOX)
	{
		if (*ValueForKey(e, "zhlt_minsmaxs"))
		{
			Error("Entity %i, Brush %i: Only one BoundingBox brush allowed.",
				  b->originalentitynum, b->originalbrushnum);
		}
		char string[MAXTOKEN];
		vec3_t mins, maxs;
		char *origin = nullptr;
		if (*ValueForKey(e, "origin"))
		{
			origin = strdup(ValueForKey(e, "origin"));
			SetKeyValue(e, "origin", "");
		}

		b->contents = contents_t::CONTENTS_SOLID;
		CreateBrush(e->firstbrush + b->brushnum); // to get sizes
		b->contents = content;

		for (int i = 0; i < NUM_HULLS; i++)
		{
			b->hulls[i].faces = nullptr;
		}

		if (b->entitynum != 0) // Ignore for WORLD (code elsewhere enforces no ORIGIN in world message)
		{
			VectorCopy(b->hulls[0].bounds.m_Mins, mins);
			VectorCopy(b->hulls[0].bounds.m_Maxs, maxs);

			safe_snprintf(string, MAXTOKEN, "%.0f %.0f %.0f %.0f %.0f %.0f", mins[0], mins[1], mins[2], maxs[0], maxs[1], maxs[2]);
			SetKeyValue(&g_entities[b->entitynum], "zhlt_minsmaxs", string);
		}

		if (origin)
		{
			SetKeyValue(e, "origin", origin);
			delete origin;
		}
	}
}

void BrushCheckClipSkybox(Entity *e, Brush *b, Side *s)
{
	if (g_skyclip && b->contents == CONTENTS_SKY && !b->noclip)
	{
		auto newb = CopyCurrentBrush(e, b);
		newb->contents = contents_t::CONTENTS_SOLID;
		newb->cliphull = ~0;
		for (int i = 0; i < newb->numsides; i++)
		{
			s = &g_brushsides[newb->firstside + i];
			strcpy(s->texture.name, "NULL");
		}
	}
}

void BrushCheckContentEmpty(Entity *e, Brush *b, Side *s)
{

	if (b->cliphull != 0 && b->contents == CONTENTS_TOEMPTY)
	{
		// check for mix of CLIP and normal texture
		bool mixed = false;
		for (int i = 0; i < b->numsides; i++)
		{
			s = &g_brushsides[b->firstside + i];
			if (!strncasecmp(s->texture.name, "NULL", 4))
			{ // this is not supposed to be a HINT brush, so remove all invisible faces from hull 0.
				strcpy(s->texture.name, "SKIP");
			}
			if (strncasecmp(s->texture.name, "SKIP", 4))
				mixed = true;
		}
		if (mixed)
		{
			auto newb = CopyCurrentBrush(e, b);
			newb->cliphull = 0;
		}
		b->contents = contents_t::CONTENTS_SOLID;
		for (int i = 0; i < b->numsides; i++)
		{
			s = &g_brushsides[b->firstside + i];
			strcpy(s->texture.name, "NULL");
		}
	}
}
// End of Brush* functions, now is time to parse brush
// =====================================================================================

// =====================================================================================
//  Parse a brush from script
// =====================================================================================
static void ParseBrush(Entity *mapent)
{
	auto b = &g_mapbrushes[g_nummapbrushes]; // Current brush
	int i, j;								 // Loop counters
	Side *side;								 // Current side of the brush
	bool ok;
	auto nullify = BrushCheckZHLT_Invisible(mapent); // If the current entity is part of an invis entity
	hlassume(g_nummapbrushes < MAX_MAP_BRUSHES, assume_MAX_MAP_BRUSHES);

	g_nummapbrushes++;										// Increment the global brush counter, we are adding a new brush
	b->firstside = g_numbrushsides;							// Set the first side of the brush to current global side count20
	b->originalentitynum = g_numparsedentities;				// Record original entity number brush belongs to
	b->originalbrushnum = g_numparsedbrushes;				// Record original brush number
	b->entitynum = g_numentities - 1;						// Set brush entity number to last created entity
	b->brushnum = g_nummapbrushes - mapent->firstbrush - 1; // Calculate the brush number within the current entity.
	b->noclip = 0;											// Initialize false for now

	BrushCheckZHLT_noclip(mapent, b);
	b->cliphull = 0;
	b->bevel = false;				  // Change depending on the tool texture used
	BrushCheckFunc_detail(mapent, b); // Validate func_detail values
	BrushCheckZHLT_hull(mapent, b);

	mapent->numbrushes++;
	ok = GetToken(true);

	while (ok) // Loop through brush sides
	{
		if (!strcmp(g_token, "}")) // If we have reached the end of the brush
		{
			break;
		}

		hlassume(g_numbrushsides < MAX_MAP_SIDES, assume_MAX_MAP_SIDES);
		side = &g_brushsides[g_numbrushsides]; // Get next brush side from global array
		g_numbrushsides++;					   // Global brush side counter
		b->numsides++;						   // Number of sides for the current brush
		side->bevel = false;

		ParseFace(b, side); // ( x1 y1 z1 ) ( x2 y2 z2 ) ( x3 y3 z3 ) TEXTURENAME [ Ux Uy Uz Ushift ] [ Vx Vy Vz Vshift ] rotation Uscale Vscale

		ok = GetToken(true); // Done with line, this reads the first item from the next line
	};
	BushCheckClipTexture(b); // Check CLIP* texture

	b->contents = CheckBrushContents(b);

	BrushNullify(b, side, nullify); // Replace textures by NULL texture

	BrushConvertSPLITFACE(b, side); // Change to SKIP now that we have set brush content.

	//
	// origin brushes are removed, but they set
	// the rotation origin for the rest of the brushes
	// in the entity
	//
	BrushCheckORIGINtexture(mapent, b);

	BrushCheckZHLT_usemodel(mapent, b);

	BrushCheckInfo_hullshape(mapent, b);

	BrushCheckBOUNDINGBOXtexture(mapent, b);

	BrushCheckClipSkybox(mapent, b, side);

	BrushCheckContentEmpty(mapent, b, side);
}

// =====================================================================================
//  ParseMapEntity
//      parse an entity from script
// =====================================================================================
auto ParseMapEntity() -> bool
{
	bool all_clip = true;
	int entity_index;
	Entity *current_entity;
	EntityProperty *e;

	g_numparsedbrushes = 0;
	if (!GetToken(true))
	{
		return false;
	}

	entity_index = g_numentities;

	if (strcmp(g_token, "{"))
	{
		Error("Parsing Entity %i, expected '{' got '%s'",
			  g_numparsedentities,
			  g_token);
	}

	hlassume(g_numentities < MAX_MAP_ENTITIES, assume_MAX_MAP_ENTITIES);
	g_numentities++;

	current_entity = &g_entities[entity_index];
	current_entity->firstbrush = g_nummapbrushes;
	current_entity->numbrushes = 0;

	while (true)
	{
		if (!GetToken(true))
			Error("ParseEntity: EOF without closing brace");

		if (!strcmp(g_token, "}")) // end of our context
			break;

		if (!strcmp(g_token, "{")) // must be a brush
		{
			ParseBrush(current_entity);
			g_numparsedbrushes++;
		}
		else // else assume an epair
		{
			e = ParseEpair();
			if (current_entity->numbrushes > 0)
				Warning("Error: ParseEntity: Keyvalue comes after brushes."); //--vluzacn
			SetKeyValue(current_entity, e->key, e->value);
			delete e->key;
			delete e->value;
			delete e;
		}
	}

	// Check if all brushes are clip brushes
	{
		for (int i = 0; i < current_entity->numbrushes; ++i)
		{
			auto *brush = &g_mapbrushes[current_entity->firstbrush + i];
			if (brush->cliphull == 0 && brush->contents != CONTENTS_ORIGIN && brush->contents != CONTENTS_BOUNDINGBOX)
			{
				all_clip = false;
			}
		}
	}

	if (*ValueForKey(current_entity, "zhlt_usemodel"))
	{
		if (!*ValueForKey(current_entity, "origin"))
			Warning("Entity %i: 'zhlt_usemodel' requires the entity to have an origin brush.",
					g_numparsedentities);
		current_entity->numbrushes = 0;
	}

	if (strcmp(ValueForKey(current_entity, "classname"), "info_hullshape")) // info_hullshape is not affected by '-scale'
	{
		bool ent_move_b = false, ent_scale_b = false;
		vec3_t ent_move = {0, 0, 0}, ent_scale_origin = {0, 0, 0};
		vec_t ent_scale = 1;

		double v[4] = {0, 0, 0, 0};
		if (*ValueForKey(current_entity, "zhlt_transform"))
		{
			switch (sscanf(ValueForKey(current_entity, "zhlt_transform"), "%lf %lf %lf %lf", v, v + 1, v + 2, v + 3))
			{
			case 1:
				ent_scale_b = true;
				ent_scale = v[0];
				break;
			case 3:
				ent_move_b = true;
				VectorCopy(v, ent_move);
				break;
			case 4:
				ent_scale_b = true;
				ent_scale = v[0];
				ent_move_b = true;
				VectorCopy(v + 1, ent_move);
				break;
			default:
				Warning("bad value '%s' for key 'zhlt_transform'", ValueForKey(current_entity, "zhlt_transform"));
			}
			DeleteKey(current_entity, "zhlt_transform");
		}
		GetVectorForKey(current_entity, "origin", ent_scale_origin);

		if (ent_move_b || ent_scale_b)
		{
			for (int ibrush = 0; ibrush < current_entity->numbrushes; ++ibrush)
			{
				auto *brush = &g_mapbrushes[current_entity->firstbrush + ibrush];
				for (int iside = 0; iside < brush->numsides; ++iside)
				{
					auto *side = &g_brushsides[brush->firstside + iside];
					for (int ipoint = 0; ipoint < 3; ++ipoint)
					{
						auto *point = side->planepts[ipoint];
						if (ent_scale_b)
						{
							VectorSubtract(point, ent_scale_origin, point);
							VectorScale(point, ent_scale, point);
							VectorAdd(point, ent_scale_origin, point);
						}
						if (ent_move_b)
						{
							VectorAdd(point, ent_move, point);
						}
					}
					bool zeroscale = false;
					if (!side->texture.scale[0])
					{
						side->texture.scale[0] = 1;
					}
					if (!side->texture.scale[1])
					{
						side->texture.scale[1] = 1;
					}
					if (ent_scale_b)
					{
						vec_t coord[2];
						if (fabs(side->texture.scale[0]) > NORMAL_EPSILON)
						{
							coord[0] = DotProduct(ent_scale_origin, side->texture.UAxis) / side->texture.scale[0] + side->texture.shift[0];
							side->texture.scale[0] *= ent_scale;
							if (fabs(side->texture.scale[0]) > NORMAL_EPSILON)
							{
								side->texture.shift[0] = coord[0] - DotProduct(ent_scale_origin, side->texture.UAxis) / side->texture.scale[0];
							}
							else
							{
								zeroscale = true;
							}
						}
						else
						{
							zeroscale = true;
						}
						if (fabs(side->texture.scale[1]) > NORMAL_EPSILON)
						{
							coord[1] = DotProduct(ent_scale_origin, side->texture.VAxis) / side->texture.scale[1] + side->texture.shift[1];
							side->texture.scale[1] *= ent_scale;
							if (fabs(side->texture.scale[1]) > NORMAL_EPSILON)
							{
								side->texture.shift[1] = coord[1] - DotProduct(ent_scale_origin, side->texture.VAxis) / side->texture.scale[1];
							}
							else
							{
								zeroscale = true;
							}
						}
						else
						{
							zeroscale = true;
						}
					}
					if (ent_move_b)
					{
						if (fabs(side->texture.scale[0]) > NORMAL_EPSILON)
						{
							side->texture.shift[0] -= DotProduct(ent_move, side->texture.UAxis) / side->texture.scale[0];
						}
						else
						{
							zeroscale = true;
						}
						if (fabs(side->texture.scale[1]) > NORMAL_EPSILON)
						{
							side->texture.shift[1] -= DotProduct(ent_move, side->texture.VAxis) / side->texture.scale[1];
						}
						else
						{
							zeroscale = true;
						}
					}
					if (zeroscale)
					{
						Error("Entity %i, Brush %i: invalid texture scale.\n",
							  brush->originalentitynum, brush->originalbrushnum);
					}
				}
			}
			// Process 'zhlt_minsmaxs'
			double b[2][3];
			if (sscanf(ValueForKey(current_entity, "zhlt_minsmaxs"), "%lf %lf %lf %lf %lf %lf", &b[0][0], &b[0][1], &b[0][2], &b[1][0], &b[1][1], &b[1][2]) == 6)
			{
				for (auto &p : b)
				{
					auto *point = p;
					if (ent_scale_b)
					{
						VectorSubtract(point, ent_scale_origin, point);
						VectorScale(point, ent_scale, point);
						VectorAdd(point, ent_scale_origin, point);
					}
					if (ent_move_b)
					{
						VectorAdd(point, ent_move, point);
					}
				}
				char string[MAXTOKEN];
				safe_snprintf(string, MAXTOKEN, "%.0f %.0f %.0f %.0f %.0f %.0f", b[0][0], b[0][1], b[0][2], b[1][0], b[1][1], b[1][2]);
				SetKeyValue(current_entity, "zhlt_minsmaxs", string);
			}
		}
	}

	CheckFatal();

	GetVectorForKey(current_entity, "origin", current_entity->origin);

	if (!strcmp("func_group", ValueForKey(current_entity, "classname")) || !strcmp("func_detail", ValueForKey(current_entity, "classname")))
	{
		// This is pretty gross, because the brushes are expected to be in linear order for each entity
		auto *temp = static_cast<Brush *>(Alloc(current_entity->numbrushes * sizeof(Brush)));
		memcpy(temp, g_mapbrushes + current_entity->firstbrush, current_entity->numbrushes * sizeof(Brush));

		auto worldbrushes = g_entities[0].numbrushes;
		for (int i = 0; i < current_entity->numbrushes; ++i)
		{
			temp[i].entitynum = 0;
			temp[i].brushnum += worldbrushes;
		}

		// Make space to move the brushes (overlapped copy)
		memmove(g_mapbrushes + worldbrushes + current_entity->numbrushes,
				g_mapbrushes + worldbrushes, sizeof(Brush) * (g_nummapbrushes - worldbrushes - current_entity->numbrushes));

		// Copy the new brushes down
		memcpy(g_mapbrushes + worldbrushes, temp, sizeof(Brush) * current_entity->numbrushes);

		// Fix up indexes
		g_numentities--;
		g_entities[0].numbrushes += current_entity->numbrushes;
		for (int i = 1; i < g_numentities; ++i)
		{
			g_entities[i].firstbrush += current_entity->numbrushes;
		}
		memset(current_entity, 0, sizeof(*current_entity));
		delete temp;
		return true;
	}

	if (!strcmp(ValueForKey(current_entity, "classname"), "info_hullshape"))
	{
		bool disabled = IntForKey(current_entity, "disabled");
		const char *id = ValueForKey(current_entity, "targetname");
		int defaulthulls = IntForKey(current_entity, "defaulthulls");
		CreateHullShape(entity_index, disabled, id, defaulthulls);
		DeleteCurrentEntity(current_entity);
		return true;
	}

	if (fabs(current_entity->origin[0]) > ENGINE_ENTITY_RANGE + ON_EPSILON ||
		fabs(current_entity->origin[1]) > ENGINE_ENTITY_RANGE + ON_EPSILON ||
		fabs(current_entity->origin[2]) > ENGINE_ENTITY_RANGE + ON_EPSILON)
	{
		const char *classname = ValueForKey(current_entity, "classname");
		if (strncmp(classname, "light", 5))
		{
			Warning("Entity %i (classname \"%s\"): origin outside +/-%.0f: (%.0f,%.0f,%.0f)",
					g_numparsedentities,
					classname, (double)ENGINE_ENTITY_RANGE, current_entity->origin[0], current_entity->origin[1], current_entity->origin[2]);
		}
	}

	return true;
}

// =====================================================================================
//  CountEngineEntities
// =====================================================================================
auto CountEngineEntities() -> unsigned int
{
	unsigned num_engine_entities = 0;
	auto *mapent = g_entities;

	// for each entity in the map
	for (int x = 0; x < g_numentities; x++, mapent++)
	{
		auto *classname = ValueForKey(mapent, "classname");
		// if its a light_spot or light_env, dont include it as an engine entity!
		if (classname)
		{
			if (!strncasecmp(classname, "light", 5) || !strncasecmp(classname, "light_spot", 10) || !strncasecmp(classname, "light_environment", 17))
			{
				auto *style = ValueForKey(mapent, "style");
				auto *targetname = ValueForKey(mapent, "targetname");

				// lightspots and lightenviroments dont have a targetname or style
				if (!strlen(targetname) && !atoi(style))
				{
					continue;
				}
			}
		}
		num_engine_entities++;
	}

	return num_engine_entities;
}

// =====================================================================================
//  LoadMapFile
//      wrapper for LoadMapFileData
//      parse in script entities
// =====================================================================================
auto ContentsToString(const contents_t type) -> const char *;

void LoadMapFile(const char *const filename)
{
	unsigned num_engine_entities;

	LoadMapFileData(filename);

	g_numentities = 0;
	g_numparsedentities = 0;
	while (ParseMapEntity())
	{
		g_numparsedentities++;
	}
	num_engine_entities = CountEngineEntities();

	hlassume(num_engine_entities < MAX_ENGINE_ENTITIES, assume_MAX_ENGINE_ENTITIES);
	CheckFatal();
}