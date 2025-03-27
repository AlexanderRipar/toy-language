#include "common.hpp"

#include <cstdio>
#include <cstdarg>

#if COMPILER_MSVC
	#include <intrin.h>
	#define DEBUGBREAK __debugbreak()
#elif COMPILER_CLANG
	#if !__has_builtin(__builtin_debugtrap)
		#error("Required __builtin_debugtrap not supported by used clang version")
	#endif

	#define DEBUGBREAK __builtin_debugtrap()
#elif COMPILER_GCC
	#include <signal.h>

	#define DEBUGBREAK raise(SIGTRAP)
#else
	#error("Unknown compiler")
#endif

#include "minos.hpp"

#ifndef NDEBUG
NORETURN void assert_unreachable_helper() noexcept
	{
		fprintf(stderr, "Reached unreachable code\n");
	
		DEBUGBREAK;

		minos::exit_process(1);
	}
#endif // !NDEBUG

NORETURN void vpanic(const char8* format, va_list args) noexcept
{
	vfprintf(stderr, format, args);

	DEBUGBREAK;

	minos::exit_process(1);
}

NORETURN void panic(const char8* format, ...) noexcept
{
	va_list args;

	va_start(args, format);

	vpanic(format, args);
}
