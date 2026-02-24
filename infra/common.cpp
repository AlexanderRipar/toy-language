#include "assert.hpp"
#include "panic.hpp"
#include "host_compiler.hpp"

#include "types.hpp"
#include "minos/minos.hpp"

#include <cstdio>
#include <cstdarg>

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

bool perform_debugbreak_helper() noexcept
{
	return minos::has_debugger_attached();
}
