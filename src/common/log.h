#pragma once

#include "cmdlib.h" //--vluzacn

#include "mathtypes.h"
#include "messages.h"

//
// log.c globals
//

extern char *g_Program;
extern char g_Mapname[_MAX_PATH];
extern char g_Wadpath[_MAX_PATH]; // seedee

constexpr bool DEFAULT_LOG = true;

//
// log.c Functions
//

extern void ResetTmpFiles();
extern void ResetLog();
extern void ResetErrorLog();
extern void CheckForErrorLog();

extern void CloseLog();
extern void WriteLog(const char *const message);

extern void CheckFatal();

#define IfDebug(x)

extern auto Localize(const char *s) -> const char *;
extern void LoadLangFile(const char *name, const char *programpath);
extern auto InitConsole(int argc, char **argv) -> int;
extern void FORMAT_PRINTF(1, 2) PrintConsole(const char *const message, ...);
extern void FORMAT_PRINTF(1, 2) Log(const char *const message, ...);
extern void FORMAT_PRINTF(1, 2) Error(const char *const error, ...);
extern void FORMAT_PRINTF(2, 3) Fatal(assume_msgs msgid, const char *const error, ...);
extern void FORMAT_PRINTF(1, 2) Warning(const char *const warning, ...);

extern void FORMAT_PRINTF(1, 2) PrintOnce(const char *const message, ...);

extern void LogStart(const int argc, char **argv);
extern void LogArguments(int argc, char **argv);
extern void LogEnd();
extern void Banner();

extern void LogTimeElapsed(float elapsed_time);

// Should be in hlassert.h, but well so what
extern void hlassume(bool exp, assume_msgs msgid);