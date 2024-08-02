#pragma once

//=====================================================================
// AJM: Different features of the tools can be undefined here
//      these are not officially beta tested, but seem to work okay

// ZHLT_* features are spread across more than one tool. Hence, changing
//      one of these settings probably means recompiling the whole set

// tool specific settings below only mean a recompile of the tool affected

//=====================================================================

#include <unistd.h>
#include "win32fix.h"

#ifdef __MINGW32__
#include <io.h>
#endif

#include <sys/time.h>

#define MODIFICATIONS_STRING "Submit detailed bug reports to (github.com/seedee/SDHLT/issues)\n"

#define ZHLT_VERSIONSTRING "v3.4"

#define SDHLT_VERSIONSTRING "v1.2.0"

#if !defined(SCSG) && !defined(SBSP) && !defined(SVIS) && !defined(SRAD) // seedee
#error "You must define one of these in the settings of each project: SDHLCSG, SDHLBSP, SDHLVIS, SDHLRAD. The most likely cause is that you didn't load the project from the .sln file."
#endif

#define PLATFORM_VERSIONSTRING "linux"

#define SYSTEM_SLASH_CHAR '/'
#define SYSTEM_SLASH_STR "/"
extern auto strupr(char *string) -> char *;
extern auto strlwr(char *string) -> char *;

extern bool CDECL FORMAT_PRINTF(3, 4) safe_snprintf(char *const dest, const size_t count, const char *const args, ...);
extern auto safe_strncpy(char *const dest, const char *const src, const size_t count) -> bool;
extern auto safe_strncat(char *const dest, const char *const src, const size_t count) -> bool;
extern auto TerminatedString(const char *buffer, const int size) -> bool;

extern auto FlipSlashes(char *string) -> char *;
extern void DefaultExtension(char *path, const char *extension);
extern void StripExtension(char *path);

extern void ExtractFile(const char *const path, char *dest);
extern void ExtractFilePath(const char *const path, char *dest);

extern auto I_FloatTime() -> double;

extern auto LittleShort(short l) -> short;
extern auto LittleLong(int l) -> int;
extern auto LittleFloat(float l) -> float;