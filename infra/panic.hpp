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
NORETURN void warn(const char8* format, Inserts... inserts) noexcept
{
	(void) print(minos::standard_file_handle(minos::StdFileName::StdErr), format, inserts...);

	DEBUGBREAK;
}

#define TODO(message) panic("Encountered open TODO in % at %:%: %\n", __FUNCTION__, __FILE__, __LINE__, *(message) == '\0' ? "?" : (message))

#endif // PANIC_INCLUDE_GUARD
