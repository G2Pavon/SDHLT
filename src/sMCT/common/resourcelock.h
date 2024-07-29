#pragma once

#include "cmdlib.h" //--vluzacn

extern auto CreateResourceLock(int LockNumber) -> void *;
extern void ReleaseResourceLock(void **lock);