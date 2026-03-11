#ifndef PANIC_INCLUDE_GUARD
#define PANIC_INCLUDE_GUARD

#include "types.hpp"
#include "host_compiler.hpp"
#include "minos/minos.hpp"
#include "print/print.hpp"

template<typename... Inserts>
NORETURN void panic(const char8* format, Inserts... inserts) noexcept
{
	(void) print(minos::standard_file_handle(minos::StdFileName::StdErr), format, inserts...);

	DEBUGBREAK;

	minos::exit_process(1);
}

template<typename... Inserts>
void warn(const char8* format, Inserts... inserts) noexcept
{
	(void) print(minos::standard_file_handle(minos::StdFileName::StdErr), format, inserts...);

	DEBUGBREAK;
}

template<typename... Inserts>
NORETURN void todo_helper(const char8* function, const char8* file, u32 line, const char8* format, Inserts... inserts) noexcept
{
	const minos::FileHandle err = minos::standard_file_handle(minos::StdFileName::StdErr);

	(void) print(err, "Encountered open TODO in % at %:%:\n    ", function, file, line);

	(void) print(err, format, inserts...);

	(void) print(err, "\n");

	DEBUGBREAK;

	minos::exit_process(1);
}

#define TODO(...) todo_helper(__FUNCTION__, __FILE__, __LINE__, __VA_ARGS__)

#endif // PANIC_INCLUDE_GUARD
