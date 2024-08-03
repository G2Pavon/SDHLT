#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstring>
#include <cstdio>
#include <cctype>
#include <cstdarg>

#include "cmdlib.h"
#include "messages.h"
#include "hlassert.h"
#include "blockmem.h"
#include "log.h"
#include "mathlib.h"

#define PATHSEPARATOR(c) ((c) == '\\' || (c) == '/')

/*
 * ================
 * I_FloatTime
 * ================
 */

auto I_FloatTime() -> double
{
    struct timeval tp;
    struct timezone tzp;
    static int secbase;

    gettimeofday(&tp, &tzp);

    if (!secbase)
    {
        secbase = tp.tv_sec;
        return tp.tv_usec / 1000000.0;
    }

    return (tp.tv_sec - secbase) + tp.tv_usec / 1000000.0;
}

auto strupr(char *string) -> char *
{
    int i;
    int len = strlen(string);

    for (i = 0; i < len; i++)
    {
        string[i] = toupper(string[i]);
    }
    return string;
}

auto strlwr(char *string) -> char *
{
    int i;
    int len = strlen(string);

    for (i = 0; i < len; i++)
    {
        string[i] = tolower(string[i]);
    }
    return string;
}

/*--------------------------------------------------------------------
// New implementation of FlipSlashes, DefaultExtension, StripFilename,
// StripExtension, ExtractFilePath, ExtractFile, ExtractFileBase, etc.
----------------------------------------------------------------------*/

// Since all of these functions operate around either the extension
// or the directory path, centralize getting both numbers here so we
// can just reference them everywhere else.  Use strrchr to give a
// speed boost while we're at it.
inline void getFilePositions(const char *path, int *extension_position, int *directory_position)
{
    const char *ptr = strrchr(path, '.');
    if (ptr == nullptr)
    {
        *extension_position = -1;
    }
    else
    {
        *extension_position = ptr - path;
    }

    ptr = qmax(strrchr(path, '/'), strrchr(path, '\\'));
    if (ptr == nullptr)
    {
        *directory_position = -1;
    }
    else
    {
        *directory_position = ptr - path;
        if (*directory_position > *extension_position)
        {
            *extension_position = -1;
        }

        // cover the case where we were passed a directory - get 2nd-to-last slash
        if (*directory_position == (int)strlen(path) - 1)
        {
            do
            {
                --(*directory_position);
            } while (*directory_position > -1 && path[*directory_position] != '/' && path[*directory_position] != '\\');
        }
    }
}

auto FlipSlashes(char *string) -> char *
{
    char *ptr = string;
    if (SYSTEM_SLASH_CHAR == '\\')
    {
        while ((ptr = strchr(ptr, '/')))
        {
            *ptr = SYSTEM_SLASH_CHAR;
        }
    }
    else
    {
        while ((ptr = strchr(ptr, '\\')))
        {
            *ptr = SYSTEM_SLASH_CHAR;
        }
    }
    return string;
}

void DefaultExtension(char *path, const char *extension)
{
    int extension_pos, directory_pos;
    getFilePositions(path, &extension_pos, &directory_pos);
    if (extension_pos == -1)
    {
        strcat(path, extension);
    }
}

void StripExtension(char *path)
{
    int extension_pos, directory_pos;
    getFilePositions(path, &extension_pos, &directory_pos);
    if (extension_pos != -1)
    {
        path[extension_pos] = 0;
    }
}

void ExtractFilePath(const char *const path, char *dest)
{
    int extension_pos, directory_pos;
    getFilePositions(path, &extension_pos, &directory_pos);
    if (directory_pos != -1)
    {
        memcpy(dest, path, directory_pos + 1); // include directory slash
        dest[directory_pos + 1] = 0;
    }
    else
    {
        dest[0] = 0;
    }
}

void ExtractFile(const char *const path, char *dest)
{
    int extension_pos, directory_pos;
    getFilePositions(path, &extension_pos, &directory_pos);

    int length = strlen(path);

    length -= directory_pos + 1;

    memcpy(dest, path + directory_pos + 1, length); // exclude directory slash
    dest[length] = 0;
}

/*
 * ============================================================================
 *
 * BYTE ORDER FUNCTIONS
 *
 * ============================================================================
 */

auto LittleShort(const short l) -> short
{
    return l;
}

auto LittleLong(const int l) -> int
{
    return l;
}

auto LittleFloat(const float l) -> float
{
    return l;
}

//=============================================================================

auto FORMAT_PRINTF(3, 4) safe_snprintf(char *const dest, const size_t count, const char *const args, ...) -> bool
{
    size_t amt;
    va_list argptr;

    hlassert(count > 0);

    va_start(argptr, args);
    amt = vsnprintf(dest, count, args, argptr);
    va_end(argptr);

    // truncated (bad!, snprintf doesn't null terminate the string when this happens)
    if (amt == count)
    {
        dest[count - 1] = 0;
        return false;
    }

    return true;
}

auto safe_strncpy(char *const dest, const char *const src, const size_t count) -> bool
{
    return safe_snprintf(dest, count, "%s", src);
}

auto safe_strncat(char *const dest, const char *const src, const size_t count) -> bool
{
    if (count)
    {
        strncat(dest, src, count);

        dest[count - 1] = 0; // Ensure it is null terminated
        return true;
    }
    else
    {
        Warning("safe_strncat passed empty count");
        return false;
    }
}

auto TerminatedString(const char *buffer, const int size) -> bool
{
    int x;

    for (x = 0; x < size; x++, buffer++)
    {
        if ((*buffer) == 0)
        {
            return true;
        }
    }
    return false;
}
