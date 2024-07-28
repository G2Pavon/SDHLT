//=======================================================================
//			Copyright (C) XashXT Group 2011
//		         stringlib.h - safety string routines
//=======================================================================
#pragma once

#include <cstring>
#include <cstdio>

extern void Q_strnupr(const char *in, char *out, size_t size_out);
extern void Q_strnlwr(const char *in, char *out, size_t size_out);
extern auto Q_isdigit(const char *str) -> bool;
extern auto Q_strlen(const char *string) -> int;
extern auto Q_toupper(const char in) -> char;
extern auto Q_tolower(const char in) -> char;
extern size_t Q_strncat(char *dst, const char *src, size_t size);
extern size_t Q_strncpy(char *dst, const char *src, size_t size);
extern auto copystring(const char *s) -> char *; // don't forget release memory after use
#define freestring(a) (delete[] (a))
auto Q_strchr(const char *s, char c) -> char *;
auto Q_strrchr(const char *s, char c) -> char *;
auto Q_strnicmp(const char *s1, const char *s2, int n) -> int;
auto Q_strncmp(const char *s1, const char *s2, int n) -> int;
auto Q_strstr(const char *string, const char *string2) -> char *;
auto Q_stristr(const char *string, const char *string2) -> char *;
auto Q_vsnprintf(char *buffer, size_t buffersize, const char *format, va_list args) -> int;
auto Q_snprintf(char *buffer, size_t buffersize, const char *format, ...) -> int;
auto Q_sprintf(char *buffer, const char *format, ...) -> int;
auto Q_pretifymem(float value, int digitsafterdecimal) -> char *;
void _Q_timestring(int seconds, char *msg, size_t size);
auto va(const char *format, ...) -> char *;
void Q_getwd(char *out, size_t len);

#define Q_strupr(in, out) Q_strnupr(in, out, 99999)
#define Q_strlwr(int, out) Q_strnlwr(in, out, 99999)
#define Q_strcat(dst, src) Q_strncat(dst, src, 99999)
#define Q_strcpy(dst, src) Q_strncpy(dst, src, 99999)
#define Q_stricmp(s1, s2) Q_strnicmp(s1, s2, 99999)
#define Q_strcmp(s1, s2) Q_strncmp(s1, s2, 99999)
#define Q_vsprintf(buffer, format, args) Q_vsnprintf(buffer, 99999, format, args)
#define Q_memprint(val) Q_pretifymem(val, 2)
#define Q_timestring(a, b) _Q_timestring(a, b, sizeof(b))

auto COM_ParseFile(char *data, char *token) -> char *;