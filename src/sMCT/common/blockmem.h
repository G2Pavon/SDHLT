#pragma once

#include "cmdlib.h" //--vluzacn

extern auto AllocBlock(unsigned long size) -> void *;
extern auto FreeBlock(void *pointer) -> bool;

extern auto Alloc(unsigned long size) -> void *;
extern auto Free(void *pointer) -> bool;

#if defined(CHECK_HEAP)
extern void HeapCheck();
#else
#define HeapCheck()
#endif