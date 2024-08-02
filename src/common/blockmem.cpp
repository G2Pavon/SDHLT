/// ********* POSIX **********

#ifdef SYSTEM_POSIX
#include <cstdlib>
#include "cmdlib.h"
#include "messages.h"
#include "log.h"

// =====================================================================================
//  AllocBlock
// =====================================================================================
auto AllocBlock(const unsigned long size) -> void *
{
    if (!size)
    {
        Warning("Attempting to allocate 0 bytes");
    }
    return calloc(1, size);
}

// =====================================================================================
//  FreeBlock
// =====================================================================================
auto FreeBlock(void *pointer) -> bool
{
    if (!pointer)
    {
        Warning("Freeing a null pointer");
    }
    free(pointer);
    return true;
}

// =====================================================================================
//  Alloc
// =====================================================================================
auto Alloc(const unsigned long size) -> void *
{
    return AllocBlock(size);
}

// =====================================================================================
//  Free
// =====================================================================================
auto Free(void *pointer) -> bool
{
    return FreeBlock(pointer);
}

#endif /// ********* POSIX **********
