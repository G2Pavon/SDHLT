#ifndef LOG_H__
#define LOG_H__
#include "cmdlib.h" //--vluzacn

#if _MSC_VER >= 1000
#pragma once
#endif

#include "mathtypes.h"
#include "messages.h"


//
// log.c globals
//

extern char*    g_Program;
extern char     g_Mapname[_MAX_PATH];
extern char     g_Wadpath[_MAX_PATH]; //seedee

#define DEFAULT_LOG         true

extern unsigned long g_clientid;                           // Client id of this program
extern unsigned long g_nextclientid;                       // Client id of next client to spawn from this server

//
// log.c Functions
//

extern void     ResetTmpFiles();
extern void     ResetLog();
extern void     ResetErrorLog();
extern void     CheckForErrorLog();

extern void CDECL OpenLog(int clientid);
extern void CDECL CloseLog();
extern void     WriteLog(const char* const message);

extern void     CheckFatal();

#ifdef _DEBUG
#define IfDebug(x) (x)
#else
#define IfDebug(x)
#endif

extern const char * Localize (const char *s);
extern void LoadLangFile (const char *name, const char *programpath);
extern int InitConsole(int argc, char **argv);
extern void CDECL FORMAT_PRINTF(1,2) PrintConsole(const char* const message, ...);
extern void CDECL FORMAT_PRINTF(1,2) Log(const char* const message, ...);
extern void CDECL FORMAT_PRINTF(1,2) Error(const char* const error, ...);
extern void CDECL FORMAT_PRINTF(2,3) Fatal(assume_msgs msgid, const char* const error, ...);
extern void CDECL FORMAT_PRINTF(1,2) Warning(const char* const warning, ...);

extern void CDECL FORMAT_PRINTF(1,2) PrintOnce(const char* const message, ...);

extern void     LogStart(const int argc, char** argv);
extern void     LogEnd();
extern void     Banner();

extern void     LogTimeElapsed(float elapsed_time);

// Should be in hlassert.h, but well so what
extern void     hlassume(bool exp, assume_msgs msgid);

#endif // Should be in hlassert.h, but well so what LOG_H__
