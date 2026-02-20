#ifndef MATH_INCLUDE_GUARD
#define MATH_INCLUDE_GUARD

#include "types.hpp"
#include "assert.hpp"

template<typename T, bool assume_one>
static constexpr u8 ctz_shim_(T n) noexcept
{
	if constexpr (assume_one)
	{
		ASSERT_OR_IGNORE(n != 0);
	}
	else
	{
		if (n == 0)
			return 8 * sizeof(T);
	}

	#if defined(COMPILER_MSVC)
		unsigned long index;

		(void) _BitScanForward64(&index, static_cast<u64>(n));

		return static_cast<u8>(index);
	#elif defined(COMPILER_CLANG) || defined(COMPILER_GCC)
		if constexpr (sizeof(T) > 8)
			static_assert(false, "Trailing zero count only supported for up to 64-bit numbers.");
		else if constexpr (sizeof(T) == 8)
			return static_cast<u8>(__builtin_ctzl(static_cast<u64>(n)));
		else
			return static_cast<u8>(__builtin_ctz(static_cast<u32>(n)));
	#else
		#error("Unsupported compiler")
	#endif
}

template<typename T, bool assume_one>
static constexpr u8 clz_shim_(T n) noexcept
{
	if constexpr (assume_one)
	{
		ASSERT_OR_IGNORE(n != 0);
	}
	else
	{
		if (n == 0)
			return 8 * sizeof(T);
	}

	#if defined(COMPILER_MSVC)
		unsigned long index;

		(void) _BitScanReverse64(&index, static_cast<u64>(n));

		const u8 leading_zeros_64 = static_cast<u8>(63 - index);

		const u8 leading_zeros_t = leading_zeros_64 - (64 - sizeof(T) * 8);

		return leading_zeros_t;
	#elif defined(COMPILER_CLANG) || defined(COMPILER_GCC)
		static_assert(sizeof(T) <= 8, "Leading zero count only supported for up to 64-bit numbers.");
		if constexpr (sizeof(T) == 8)
			return static_cast<u8>(__builtin_clzl(static_cast<u64>(n)));
		else
			return static_cast<u8>(__builtin_clz(static_cast<u32>(n)));
	#else
		#error("Unsupported compiler")
	#endif
}

template<typename T>
static constexpr inline bool is_pow2(T n) noexcept
{
	return (n & (n - 1)) == 0;
}

template<typename T>
static constexpr inline T next_pow2(T n, T estimate = 1) noexcept
{
	ASSERT_OR_IGNORE(estimate != 0 && is_pow2(estimate));

	while (estimate < n)
		estimate *= 2;

	return estimate;
}

template<typename T>
static constexpr inline T next_multiple(T n, T factor) noexcept
{
	return ((n + factor - 1) / factor) * factor;
}

template<typename T, u64 COUNT>
inline constexpr u64 array_count([[maybe_unused]] const T(&arr)[COUNT]) noexcept
{
	return COUNT;
}

inline u64 align_to(u64 n, u64 alignment) noexcept
{
	ASSERT_OR_IGNORE(is_pow2(alignment));

	return (n + alignment - 1) & ~(alignment - 1);
}

template<typename T>
inline u8 count_trailing_zeros_assume_one(T n) noexcept
{
	return ctz_shim_<T, true>(n);
}

template<typename T>
inline u8 count_trailing_ones_assume_zero(T n) noexcept
{
	return ctz_shim_<T, true>(~n);
}

template<typename T>
inline u8 count_leading_zeros_assume_one(T n) noexcept
{
	return clz_shim_<T, true>(n);
}

template<typename T>
inline u8 count_leading_ones_assume_zero(T n) noexcept
{
	return clz_shim_<T, true>(~n);
}

template<typename T>
inline bool count_trailing_zeros(T n) noexcept
{
	return ctz_shim_<T, false>(n);
}

template<typename T>
inline u8 count_trailing_ones(T n) noexcept
{
	return ctz_shim_<T, false>(~n);
}

template<typename T>
inline u8 count_leading_zeros(T n) noexcept
{
	return clz_shim_<T, false>(n);
}

template<typename T>
inline u8 count_leading_ones(T n) noexcept
{
	return clz_shim_<T, false>(~n);
}

template<typename T>
inline u8 log10_ceil(T n) noexcept
{
	u8 rst = 1;

	while (n >= 10'000)
	{
		rst += 4;

		n /= 10'000;
	}

	if (n >= 1000)
		return rst + 3;
	else if (n >= 100)
		return rst + 2;
	else if (n >= 10)
		return rst + 1;
	else
		return rst;
}

#endif // MATH_INCLUDE_GUARD
