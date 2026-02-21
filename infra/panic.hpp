#ifndef PANIC_INCLUDE_GUARD
#define PANIC_INCLUDE_GUARD

#include "types.hpp"
#include "host_compiler.hpp"

#include <cstdarg>

NORETURN void panic(const char8* format, ...) noexcept;

NORETURN void vpanic(const char8* format, va_list args) noexcept;

void warn(const char8* format, ...) noexcept;

void vwarn(const char8* format, va_list args) noexcept;

#define TODO(message) panic("Encountered open TODO in %s at %s:%d: %s\n", __FUNCTION__, __FILE__, __LINE__, *(message) == '\0' ? "?" : (message))

#endif // PANIC_INCLUDE_GUARD
