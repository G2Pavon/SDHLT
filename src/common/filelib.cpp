#ifdef SYSTEM_WIN32
#include <sys/stat.h>
#include <io.h>
#include <fcntl.h>
#endif

#ifdef SYSTEM_POSIX
#include <sys/stat.h>

#include <fcntl.h>

#include <unistd.h>
#endif

#include "cmdlib.h"
#include "messages.h"
#include "log.h"
#include "mathtypes.h"
#include "mathlib.h"
#include "blockmem.h"

/*
 * ==============
 * getfiletime
 * ==============
 */

auto getfiletime(const char *const filename) -> time_t
{
    time_t filetime = 0;
    struct stat filestat;

    if (stat(filename, &filestat) == 0)
        filetime = qmax(filestat.st_mtime, filestat.st_ctime);

    return filetime;
}

/*
 * ==============
 * getfilesize
 * ==============
 */
auto getfilesize(const char *const filename) -> long
{
    long size = 0;
    struct stat filestat;

    if (stat(filename, &filestat) == 0)
        size = filestat.st_size;

    return size;
}

/*
 * ==============
 * getfiledata
 * ==============
 */
auto getfiledata(const char *const filename, char *buffer, const int buffersize) -> long
{
    long size = 0;
    int handle;
    time_t start, end;

    time(&start);

    if ((handle = _open(filename, O_RDONLY)) != -1)
    {
        int bytesread;

        Log("%-20s Restoring [%-13s - ", "BuildVisMatrix:", filename);
        while ((bytesread = _read(handle, buffer, qmin(32 * 1024, buffersize - size))) > 0)
        {
            size += bytesread;
            buffer += bytesread;
        }
        _close(handle);
        time(&end);
        Log("%10.3fMB] (%li)\n", size / (1024.0 * 1024.0), end - start);
    }

    if (buffersize != size)
    {
        Warning("Invalid file [%s] found.  File will be rebuilt!\n", filename);
        _unlink(filename);
    }

    return size;
}

/*
 * ================
 * filelength
 * ================
 */
auto q_filelength(FILE *f) -> int
{
    int pos;
    int end;

    pos = ftell(f);
    fseek(f, 0, SEEK_END);
    end = ftell(f);
    fseek(f, pos, SEEK_SET);

    return end;
}

/*
 * ================
 * exists
 * ================
 */
auto q_exists(const char *const filename) -> bool
{
    FILE *f;

    f = fopen(filename, "rb");

    if (!f)
    {
        return false;
    }
    else
    {
        fclose(f);
        return true;
    }
}

FILE *SafeOpenWrite(const char *const filename)
{
    FILE *f;

    f = fopen(filename, "wb");

    if (!f)
        Error("Error opening %s: %s", filename, strerror(errno));

    return f;
}

FILE *SafeOpenRead(const char *const filename)
{
    FILE *f;

    f = fopen(filename, "rb");

    if (!f)
        Error("Error opening %s: %s", filename, strerror(errno));

    return f;
}

void SafeRead(FILE *f, void *buffer, int count)
{
    if (fread(buffer, 1, count, f) != (size_t)count)
        Error("File read failure");
}

void SafeWrite(FILE *f, const void *const buffer, int count)
{
    if (fwrite(buffer, 1, count, f) != (size_t)count)
        Error("File write failure"); // Error("File read failure"); //--vluzacn
}

/*
 * ==============
 * LoadFile
 * ==============
 */
auto LoadFile(const char *const filename, char **bufferptr) -> int
{
    FILE *f;
    int length;
    char *buffer;

    f = SafeOpenRead(filename);
    length = q_filelength(f);
    buffer = (char *)Alloc(length + 1);
    SafeRead(f, buffer, length);
    fclose(f);

    *bufferptr = buffer;
    return length;
}

/*
 * ==============
 * SaveFile
 * ==============
 */
void SaveFile(const char *const filename, const void *const buffer, int count)
{
    FILE *f;

    f = SafeOpenWrite(filename);
    SafeWrite(f, buffer, count);
    fclose(f);
}
