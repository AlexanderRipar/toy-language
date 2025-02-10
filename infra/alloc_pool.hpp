#ifndef POOL_ALLOC_INCLUDE_GUARD
#define POOL_ALLOC_INCLUDE_GUARD

#include "common.hpp"

struct AllocPool;

AllocPool* create_alloc_pool(u32 reserve, u32 commit_increment) noexcept;

void release_alloc_pool(AllocPool* pool) noexcept;

void* alloc_from_pool(AllocPool* pool, u32 bytes, u32 alignment) noexcept;

void clear(AllocPool* pool, u32 max_remaining_commit = 0) noexcept;

#endif // POOL_ALLOC_INCLUDE_GUARD
