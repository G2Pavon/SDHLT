#pragma once

#include <cstdlib>
#include <string.h>

constexpr int _MAX_PATH = 4096;
constexpr int _MAX_DRIVE = 4096;
constexpr int _MAX_DIR = 4096;
constexpr int _MAX_FNAME = 4096;
constexpr int _MAX_EXT = 4096;

#define STDCALL
#define FASTCALL
#define CDECL

#define INLINE inline

#define _strdup strdup //--vluzacn
#define _strupr strupr //--vluzacn
#define _strlwr strlwr //--vluzacn
#define _open open     //--vluzacn
#define _read read     //--vluzacn
#define _close close   //--vluzacn
#define _unlink unlink //--vluzacn

#define FORCEINLINE __inline__ __attribute__((always_inline))                                                     //--vluzacn
#define FORMAT_PRINTF(STRING_INDEX, FIRST_TO_CHECK) __attribute__((format(printf, STRING_INDEX, FIRST_TO_CHECK))) //--vluzacn