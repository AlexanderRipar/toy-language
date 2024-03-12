#ifndef COMMON_INCLUDE_GUARD
#define COMMON_INCLUDE_GUARD

#include <cassert>
#include <cstdint>

using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using s8  = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;

using byte = u8;

using char8 = char;
using char16 = wchar_t;

using uint = u64;
using sint = s64;

template<typename T, uint COUNT>
inline uint array_count([[maybe_unused]] const T(&arr)[COUNT]) noexcept
{
	return COUNT;
}

template<typename T>
inline T assert_value_helper(T t) noexcept
{
	assert(t);

	return t;
}

#ifdef NDEBUG
	#include "minos.hpp"
	#define ASSERT_OR_IGNORE(x)
	#define ASSERT_OR_EXIT(x) do { if (!(x)) { __debugbreak(); minos::exit_process(1); } } while (false)
	#define ASSERT_UNREACHABLE minos::exit_process(1)
	#define ASSERT_OR_EXECUTE(x) (x)
#elif defined(assert)
	#include "minos.hpp"
	#define ASSERT_OR_IGNORE(x) assert(x)
	#define ASSERT_OR_EXIT(x) assert(x)
	#define ASSERT_UNREACHABLE do { __debugbreak(); minos::exit_process(1); } while (false)
	#define ASSERT_OR_EXECUTE(x) assert_value_helper(x)
#else
	#error Could not properly define ASSERT_*
#endif

template<typename T>
static constexpr inline bool is_pow2(T n) noexcept
{
	return (n & (n - 1)) == 0;
}

template<typename T>
static constexpr inline T next_pow2(T n, T estimate = 1) noexcept
{
	while (estimate < n)
		estimate *= 2;

	return estimate;
}

template<typename T>
static constexpr inline T next_multiple(T n, T factor) noexcept
{
	return ((n + factor - 1) / factor) * factor;
}

#endif // COMMON_INCLUDE_GUARD
