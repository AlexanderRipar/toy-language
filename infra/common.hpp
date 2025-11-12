#ifndef COMMON_INCLUDE_GUARD
#define COMMON_INCLUDE_GUARD

#include <cstdint>
#include <cstdarg>
#include <inttypes.h>

#if defined(_MSC_VER) && !defined(__INTEL_COMPILER)
	#include <intrin.h>
	#define COMPILER_MSVC 1
	#define COMPILER_NAME "msvc"
	#define NORETURN __declspec(noreturn)
	#define DEBUGBREAK __debugbreak()
#elif defined(__clang__)
	#define COMPILER_CLANG 1
	#define COMPILER_NAME "clang"
	#define NORETURN [[noreturn]]
	#define DEBUGBREAK __builtin_debugtrap()
	#if !__has_builtin(__builtin_debugtrap)
		#error("Required __builtin_debugtrap not supported by used clang version")
	#endif
#elif defined(__GNUC__)
	#include <signal.h>
	#define COMPILER_GCC 1
	#define COMPILER_NAME "gcc"
	#define NORETURN __attribute__ ((noreturn))
	#define DEBUGBREAK raise(SIGTRAP)
#else
	#error("Unsupported compiler")
#endif

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

using f32 = float;
using f64 = double;

#ifdef NDEBUG
	#define ASSERT_OR_IGNORE(x) do {} while (false)

	#if defined(COMPILER_MSVC)
		#define ASSERT_UNREACHABLE __assume(false)
	#elif defined(COMPILER_GCC) || defined(COMPILER_CLANG)
		#define ASSERT_UNREACHABLE __builtin_unreachable()
	#else
		#error("Unsupported compiler")
	#endif
#else
	NORETURN void assert_unreachable_helper(const char8* file, u32 line) noexcept;

	NORETURN void assert_or_ignore_helper(const char8* file, u32 line, const char8* expr) noexcept;

	#define ASSERT_OR_IGNORE(x) do { if (!(x)) assert_or_ignore_helper(__FILE__, __LINE__, #x); } while (false)

	#define ASSERT_UNREACHABLE assert_unreachable_helper(__FILE__, __LINE__)
#endif

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

template<typename, typename>
constexpr bool is_same_cpp_type = false;
template <typename T>
constexpr bool is_same_cpp_type<T, T> = true;

NORETURN void panic(const char8* format, ...) noexcept;

NORETURN void vpanic(const char8* format, va_list args) noexcept;

void warn(const char8* format, ...) noexcept;

void vwarn(const char8* format, va_list args) noexcept;

#define TODO(message) panic("Encountered open TODO in %s at %s:%d: %s\n", __FUNCTION__, __FILE__, __LINE__, *(message) == '\0' ? "?" : (message))

#endif // COMMON_INCLUDE_GUARD
