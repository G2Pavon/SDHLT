#pragma once

// #define MODIFICATIONS_STRING "Submit detailed bug reports to (zoner@gearboxsoftware.com)\n"
// #define MODIFICATIONS_STRING "Submit detailed bug reports to (merlinis@bigpond.net.au)\n"
// #define MODIFICATIONS_STRING "Submit detailed bug reports to (amckern@yahoo.com)\n"
// #define MODIFICATIONS_STRING "Submit detailed bug reports to (vluzacn@163.com)\n" //--vluzacn
#define MODIFICATIONS_STRING "Submit detailed bug reports to (github.com/seedee/SDHLT/issues)\n"

#ifdef _DEBUG
#define ZHLT_VERSIONSTRING "v3.4 dbg"
#else
#define ZHLT_VERSIONSTRING "v3.4"
#endif

#define SDHLT_VERSIONSTRING "v1.2.0"

#if !defined(SCSG) && !defined(SBSP) && !defined(SVIS) && !defined(SRAD) // seedee
#error "You must define one of these in the settings of each project: SDHLCSG, SDHLBSP, SDHLVIS, SDHLRAD. The most likely cause is that you didn't load the project from the .sln file."
#endif
#if !defined(VERSION_64BIT) && !defined(VERSION_LINUX) && !defined(VERSION_OTHER) //--vluzacn
#error "You must define one of these in the settings of each project: VERSION_64BIT, VERSION_LINUX, VERSION_OTHER. The most likely cause is that you didn't load the project from the .sln file."
#endif

#ifdef VERSION_64BIT
#define PLATFORM_VERSIONSTRING "64-bit"
#endif
#ifdef VERSION_LINUX
#define PLATFORM_VERSIONSTRING "linux"
#endif
#ifdef VERSION_OTHER
#define PLATFORM_VERSIONSTRING "???"
#endif

//=====================================================================
// AJM: Different features of the tools can be undefined here
//      these are not officially beta tested, but seem to work okay

// ZHLT_* features are spread across more than one tool. Hence, changing
//      one of these settings probably means recompiling the whole set

// tool specific settings below only mean a recompile of the tool affected

#ifdef SYSTEM_WIN32
#define HLCSG_GAMETEXTMESSAGE_UTF8 //--vluzacn
#endif

//=====================================================================

#if _MSC_VER < 1400
#define strcpy_s strcpy   //--vluzacn
#define sprintf_s sprintf //--vluzacn
#endif

#ifdef __MINGW32__
#include <io.h>
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "win32fix.h"
#include "mathtypes.h"

#ifdef SYSTEM_WIN32
#pragma warning(disable : 4127) // conditional expression is constant
#pragma warning(disable : 4115) // named type definition in parentheses
#pragma warning(disable : 4244) // conversion from 'type' to type', possible loss of data
// AJM
#pragma warning(disable : 4786) // identifier was truncated to '255' characters in the browser information
#pragma warning(disable : 4305) // truncation from 'const double' to 'float'
#pragma warning(disable : 4800) // forcing value to bool 'true' or 'false' (performance warning)
#endif

#ifdef STDC_HEADERS
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <cctype>
#include <ctime>
#include <cstdarg>
#include <climits>
#endif

#include <cstdint> //--vluzacn

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef SYSTEM_WIN32
#define SYSTEM_SLASH_CHAR '\\'
#define SYSTEM_SLASH_STR "\\"
#endif
#ifdef SYSTEM_POSIX
#define SYSTEM_SLASH_CHAR '/'
#define SYSTEM_SLASH_STR "/"
#endif

// the dec offsetof macro doesn't work very well...
#define myoffsetof(type, identifier) ((size_t) & ((type *)0)->identifier)
#define sizeofElement(type, identifier) (sizeof((type *)0)->identifier)

#ifdef SYSTEM_POSIX
extern auto strupr(char *string) -> char *;
extern auto strlwr(char *string) -> char *;
#endif
extern bool CDECL FORMAT_PRINTF(3, 4) safe_snprintf(char *const dest, const size_t count, const char *const args, ...);
extern auto safe_strncpy(char *const dest, const char *const src, const size_t count) -> bool;
extern auto safe_strncat(char *const dest, const char *const src, const size_t count) -> bool;
extern auto TerminatedString(const char *buffer, const int size) -> bool;

extern auto FlipSlashes(char *string) -> char *;

extern auto I_FloatTime() -> double;

extern void DefaultExtension(char *path, const char *extension);
extern void StripExtension(char *path);

extern void ExtractFile(const char *const path, char *dest);
extern void ExtractFilePath(const char *const path, char *dest);

extern auto LittleShort(short l) -> short;
extern auto LittleLong(int l) -> int;
extern auto LittleFloat(float l) -> float;