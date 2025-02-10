#ifndef HASH_INCLUDE_GUARD
#define HASH_INCLUDE_GUARD

#include "common.hpp"
#include "range.hpp"

static inline u32 fnv1a(Range<byte> bytes) noexcept
{
	u32 hash = 2166136261;

	for (const byte c : bytes)
		hash = (hash * 16777619) ^ c;

	return hash;
}

static inline u32 fnv1a_step(u32 seed, byte next) noexcept
{
	return (seed * 16777619) ^ next;
}

#endif // HASH_INCLUDE_GUARD
