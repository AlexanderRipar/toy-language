#ifndef HASH_INCLUDE_GUARD
#define HASH_INCLUDE_GUARD

#include "common.hpp"
#include "range.hpp"

static constexpr u32 FNV1A_SEED = 2166136261;

static inline u32 fnv1a_step(u32 seed, byte next) noexcept
{
	return (seed * 16777619) ^ next;
}

static inline u32 fnv1a_step(u32 seed, Range<byte> next) noexcept
{
	u32 hash = seed;

	for (const byte c : next)
		hash = fnv1a_step(hash, c);

	return hash;
}

static inline u32 fnv1a(byte data) noexcept
{
	return fnv1a_step(FNV1A_SEED, data);
}

static inline u32 fnv1a(Range<byte> data) noexcept
{
	return fnv1a_step(FNV1A_SEED, data);
}

#endif // HASH_INCLUDE_GUARD
