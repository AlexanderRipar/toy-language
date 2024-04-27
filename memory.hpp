#ifndef MEMORY_INCLUDE_GUARD
#define MEMORY_INCLUDE_GUARD
    
#include "common.hpp"
#include "minos.hpp"
#include <cstring>

struct MemoryRequirements
{
    u64 bytes;

    u32 alignment;
};

#endif // MEMORY_INCLUDE_GUARD
