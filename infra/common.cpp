#include "common.hpp"

#include <cstdio>
#include <cstdarg>
#include <intrin.h>

#include "minos.hpp"

#ifndef NDEBUG
NORETURN void assert_unreachable_helper() noexcept
	{
		fprintf(stderr, "Reached unreachable code\n");
	
		__debugbreak();

		minos::exit_process(1);
	}
#endif // !NDEBUG

NORETURN void vpanic(const char8* format, va_list args) noexcept
{
	vfprintf(stderr, format, args);

	__debugbreak();

	minos::exit_process(1);
}

NORETURN void panic(const char8* format, ...) noexcept
{
	va_list args;

	va_start(args, format);

	vpanic(format, args);
}
