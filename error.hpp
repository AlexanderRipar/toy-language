#ifndef ERROR_INCLUDE_GUARD
#define ERROR_INCLUDE_GUARD

#include "infra/common.hpp"
#include "infra/range.hpp"

__declspec(noreturn) void source_error(u64 offset, Range<char8> source, Range<char8> filepath, const char8* format, ...) noexcept;

__declspec(noreturn) void vsource_error(u64 offset, Range<char8> source, Range<char8> filepath, const char8* format, va_list args) noexcept;

#endif // ERROR_INCLUDE_GUARD
