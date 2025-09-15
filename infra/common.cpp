#include "common.hpp"

#include <cstdio>
#include <cstdarg>

#include "minos/minos.hpp"

#ifndef NDEBUG
NORETURN void assert_unreachable_helper(const char8* file, u32 line) noexcept
{
	fprintf(stderr, "Reached unreachable code (%s:%u)\n", file, line);

	DEBUGBREAK;

	minos::exit_process(1);
}

NORETURN void assert_or_ignore_helper(const char8* file, u32 line, const char8* expr) noexcept
{
	fprintf(stderr, "Assertion `%s` failed (%s:%u)\n", expr, file, line);

	DEBUGBREAK;

	minos::exit_process(1);
}
#endif // !NDEBUG

NORETURN void panic(const char8* format, ...) noexcept
{
	va_list args;

	va_start(args, format);

	vpanic(format, args);
}

NORETURN void vpanic(const char8* format, va_list args) noexcept
{
	vfprintf(stderr, format, args);

	DEBUGBREAK;

	minos::exit_process(1);
}

void warn(const char8* format, ...) noexcept
{
	va_list args;

	va_start(args, format);

	vwarn(format, args);

	va_end(args);
}

void vwarn(const char8* format, va_list args) noexcept
{
	vfprintf(stderr, format, args);

	DEBUGBREAK;
}
