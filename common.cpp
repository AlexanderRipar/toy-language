#include "common.hpp"

#include "minos.hpp"
#include <cstdio>
#include <cstdarg>
#include <intrin.h>

__declspec(noreturn) void vpanic(const char8* format, va_list args) noexcept
{
	vfprintf(stderr, format, args);

	__debugbreak();

	minos::exit_process(1);
}

__declspec(noreturn) void panic(const char8* format, ...) noexcept
{
	va_list args;

	va_start(args, format);

	vpanic(format, args);
}
