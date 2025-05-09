#ifndef COMMON_INCLUDE_GUARD
#define COMMON_INCLUDE_GUARD

#include <cassert>
#include <cstdint>
#include <cstdarg>
#include <inttypes.h>

#if defined(_MSC_VER) && !defined(__INTEL_COMPILER)
	#define COMPILER_MSVC 1
	#define COMPILER_NAME "msvc"
	#define NORETURN __declspec(noreturn)
#elif defined(__clang__)
	#define COMPILER_CLANG 1
	#define COMPILER_NAME "clang"
	#define NORETURN [[noreturn]]
#elif defined(__GNUC__)
	#define COMPILER_GCC 1
	#define COMPILER_NAME "gcc"
	#define NORETURN __attribute__ ((noreturn))
#else
	#error("Unsupported compiler")
#endif

using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using ureg = uint64_t;

using s8  = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;
using sreg = int64_t;

using byte = u8;

using char8 = char;
using char16 = wchar_t;

using f32 = float;
using f64 = double;

#ifdef NDEBUG
	#define ASSERT_OR_IGNORE(x) do {} while (false)
	#define ASSERT_UNREACHABLE do { 1 / 0; } while (false)
#else
	NORETURN void assert_unreachable_helper() noexcept;

	#define ASSERT_OR_IGNORE(x) assert(x)
	#define ASSERT_UNREACHABLE assert_unreachable_helper()
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

NORETURN void panic(const char8* format, ...) noexcept;

NORETURN void vpanic(const char8* format, va_list args) noexcept;

#define TODO(message) panic("Encountered open TODO in %s at %s:%d: %s\n", __FUNCTION__, __FILE__, __LINE__, *(message) == '\0' ? "?" : (message))

#endif // COMMON_INCLUDE_GUARD
