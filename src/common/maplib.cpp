#include <cstring>
#include <cstdlib>

#include "cmdlib.h"
#include "filelib.h"
#include "messages.h"
#include "log.h"
#include "maplib.h"

char g_token[MAXTOKEN];

struct FileData
{
    char fileName[_MAX_PATH];
    char *fileBuffer;
    char *currentPosition;
    char *bufferEnd;
    int currentLine;
};

static FileData currentFile;
int currentLine;
bool isEndOfFile;
bool isTokenReady; // only true if UnGetToken was just called

void LoadMapFileData(const char *const fileName)
{
    int size;

    strcpy(currentFile.fileName, fileName);

    size = LoadFile(currentFile.fileName, (char **)&currentFile.fileBuffer);

    Log("Loading %s\n", currentFile.fileName);

    currentFile.currentLine = 1;
    currentFile.currentPosition = currentFile.fileBuffer;
    currentFile.bufferEnd = currentFile.fileBuffer + size;

    isEndOfFile = false;
    isTokenReady = false;
}

void ParseFromMemory(char *buffer, const int size)
{
    strcpy(currentFile.fileName, "memory buffer");

    currentFile.fileBuffer = buffer;
    currentFile.currentLine = 1;
    currentFile.currentPosition = currentFile.fileBuffer;
    currentFile.bufferEnd = currentFile.fileBuffer + size;

    isEndOfFile = false;
    isTokenReady = false;
}

void UnGetToken()
{
    isTokenReady = true;
}

auto EndOfFile(const bool crossline) -> bool
{
    if (!crossline)
        Error("Line %i is incomplete (did you place a \" inside an entity string?) \n", currentLine);

    if (!strcmp(currentFile.fileName, "memory buffer"))
    {
        isEndOfFile = true;
        return false;
    }

    free(currentFile.fileBuffer);

    isEndOfFile = true;
    return false;
}

auto GetToken(const bool crossline) -> bool
{
    char *token_p;

    if (isTokenReady)
    {
        isTokenReady = false;
        return true;
    }

    if (currentFile.currentPosition >= currentFile.bufferEnd)
        return EndOfFile(crossline);

skipspace:
    while (*currentFile.currentPosition <= 32 && *currentFile.currentPosition >= 0)
    {
        if (currentFile.currentPosition >= currentFile.bufferEnd)
            return EndOfFile(crossline);

        if (*currentFile.currentPosition++ == '\n')
        {
            if (!crossline)
                Error("Line %i is incomplete (did you place a \" inside an entity string?) \n", currentLine);
            currentLine = currentFile.currentLine++;
        }
    }

    if (currentFile.currentPosition >= currentFile.bufferEnd)
        return EndOfFile(crossline);

    if (*currentFile.currentPosition == '/' && *((currentFile.currentPosition) + 1) == '/')
    {
        if (!crossline)
            Error("Line %i is incomplete (did you place a \" inside an entity string?) \n", currentLine);

        currentFile.currentPosition++;
        while (*currentFile.currentPosition++ != '\n')
        {
            if (currentFile.currentPosition >= currentFile.bufferEnd)
                return EndOfFile(crossline);
        }
        currentLine = currentFile.currentLine++;
        goto skipspace;
    }

    token_p = g_token;

    if (*currentFile.currentPosition == '"')
    {
        currentFile.currentPosition++;
        while (*currentFile.currentPosition != '"')
        {
            *token_p++ = *currentFile.currentPosition++;

            if (currentFile.currentPosition == currentFile.bufferEnd)
                break;

            if (token_p == &g_token[MAXTOKEN])
                Error("Token too large on line %i\n", currentLine);
        }
        currentFile.currentPosition++;
    }
    else
    {
        while ((*currentFile.currentPosition > 32 || *currentFile.currentPosition < 0))
        {
            *token_p++ = *currentFile.currentPosition++;

            if (currentFile.currentPosition == currentFile.bufferEnd)
                break;

            if (token_p == &g_token[MAXTOKEN])
                Error("Token too large on line %i\n", currentLine);
        }
    }

    *token_p = 0;
    return true;
}

auto TokenAvailable() -> bool
{
    char *search_p = currentFile.currentPosition;

    if (search_p >= currentFile.bufferEnd)
        return false;

    while (*search_p <= 32)
    {
        if (*search_p == '\n')
            return false;

        search_p++;

        if (search_p == currentFile.bufferEnd)
            return false;
    }
    return true;
}