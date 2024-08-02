#pragma once

#include "cmdlib.h"

constexpr int MAXTOKEN = 4096;
extern char g_token[MAXTOKEN];

constexpr int MAX_WAD_PATHS = 42;
extern char g_szWadPaths[MAX_WAD_PATHS][_MAX_PATH];
extern int g_iNumWadPaths;

extern void LoadMapFileData(const char *const filename);
extern void ParseFromMemory(char *buffer, int size);

extern auto GetToken(bool crossline) -> bool;
extern void UnGetToken();
extern auto TokenAvailable() -> bool;