#ifndef HASH_INCLUDE_GUARD
#define HASH_INCLUDE_GUARD

#include "common.hpp"
#include "range.hpp"

static inline u32 fnv1a(Range<byte> bytes) noexcept
{
	u32 hash = 2166136261;

	for (const char8 c : bytes)
		hash = (hash * 16777619) ^ c;

	return hash;
}

#endif // HASH_INCLUDE_GUARD
