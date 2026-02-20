#ifndef ASSERTION_INCLUDE_GUARD
#define ASSERTION_INCLUDE_GUARD

#include "host_compiler.hpp"
#include "types.hpp"

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

#endif // ASSERTION_INCLUDE_GUARD
