#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <iostream>
#include <cctype>
#include <cstring>

constexpr int MAXTOKEN = 4096;
extern char g_token[MAXTOKEN];

// Función para leer un archivo en un std::string
std::string ReadFileToString(const std::string &filename);

void ProcessFile(const std::string &fileContent);

void LoadMapFile(const char *const filename);