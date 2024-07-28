#pragma once

#include "cmdlib.h"

#define MAXTOKEN 4096

extern char g_token[MAXTOKEN];

extern void LoadScriptFile(const char *const filename);
extern void ParseFromMemory(char *buffer, int size);

extern auto GetToken(bool crossline) -> bool;
extern void UnGetToken();
extern auto TokenAvailable() -> bool;

#define MAX_WAD_PATHS 42
extern char g_szWadPaths[MAX_WAD_PATHS][_MAX_PATH];
extern int g_iNumWadPaths;
