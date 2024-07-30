#pragma once

#include <cstdio>
#include <ctime>

extern auto getfiletime(const char *const filename) -> time_t;
extern auto getfilesize(const char *const filename) -> long;
extern auto getfiledata(const char *const filename, char *buffer, const int buffersize) -> long;
extern auto q_exists(const char *const filename) -> bool;
extern auto q_filelength(FILE *f) -> int;

extern FILE *SafeOpenWrite(const char *const filename);
extern FILE *SafeOpenRead(const char *const filename);
extern void SafeRead(FILE *f, void *buffer, int count);
extern void SafeWrite(FILE *f, const void *const buffer, int count);

extern auto LoadFile(const char *const filename, char **bufferptr) -> int;
extern void SaveFile(const char *const filename, const void *const buffer, int count);