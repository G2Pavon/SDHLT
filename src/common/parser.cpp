#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <iostream>
#include <cctype>
#include <cstring>

#include "parser.h"

char g_token[MAXTOKEN];
int g_nummapbrushes;
brush_t g_mapbrushes[MAX_MAP_BRUSHES];

int g_numbrushsides;
side_t g_brushsides[MAX_MAP_SIDES];

struct epair_t
{
    struct epair_t *next;
    char *key;
    char *value;
};

struct entity_t
{
    vec3_t origin;
    int firstbrush;
    int numbrushes;
    epair_t *epairs;
};

struct side_t
{
    brush_texture_t td;
    bool bevel;
    vec_t planepts[3][3];
};

struct valve_vects
{
    vec3_t UAxis;
    vec3_t VAxis;
    vec_t shift[2];
    vec_t rotate;
    vec_t scale[2];
};

struct vects_union
{
    valve_vects valve;
};

struct brush_texture_t
{
    vects_union vects;
    char name[32];
};

struct bface_t
{
    struct bface_t *next;
    int planenum;
    plane_t *plane;
    Winding *w;
    int texinfo;
    bool used; // just for face counting
    int contents;
    int backcontents;
    bool bevel; // used for ExpandBrush
    BoundingBox bounds;
};

struct brushhull_t
{
    BoundingBox bounds;
    bface_t *faces;
};

struct brushhull_t
{
    BoundingBox bounds;
    bface_t *faces;
};
struct brush_t
{
    int originalentitynum;
    int originalbrushnum;
    int entitynum;
    int brushnum;

    int firstside;
    int numsides;

    unsigned int noclip; // !!!FIXME: this should be a flag bitfield so we can use it for other stuff (ie. is this a detail brush...)
    unsigned int cliphull;
    bool bevel;
    int detaillevel;
    int chopdown; // allow this brush to chop brushes of lower detail level
    int chopup;   // allow this brush to be chopped by brushes of higher detail level
    int clipnodedetaillevel;
    int coplanarpriority;
    char *hullshapes[NUM_HULLS]; // might be NULL

    int contents;
    brushhull_t hulls[NUM_HULLS];
};

// Función para leer un archivo en un std::string
std::string ReadFileToString(const std::string &filename)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        throw std::runtime_error("Error opening file: " + filename);
    }

    std::ostringstream oss;
    oss << file.rdbuf();
    return oss.str();
}

void ProcessFile(const std::string &fileContent)
{
    std::istringstream stream(fileContent);
    std::string line;

    while (std::getline(stream, line))
    {
        // Procesar cada línea del archivo
        // Por ejemplo, separar en tokens
        std::istringstream lineStream(line);
        std::string token;
        while (lineStream >> token)
        {
            // Convertir el token a char y asignarlo a g_token
            strncpy(g_token, token.c_str(), MAXTOKEN - 1);
            g_token[MAXTOKEN - 1] = '\0'; // Asegurar que esté terminado en null

            // Aquí puedes procesar g_token como necesites
            std::cout << "Token: " << g_token << std::endl;
        }
    }
}

void LoadMapFile(const char *const filename)
{
    try
    {
        std::string fileContent = ReadFileToString(filename);
        ProcessFile(fileContent);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}
