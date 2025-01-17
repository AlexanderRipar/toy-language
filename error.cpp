#include "error.hpp"

#include <cstdarg>
#include <cstdio>

struct SourceLocation
{
	u32 line;

	u32 character;
};

static SourceLocation source_location_from(u32 offset, Range<char8> content) noexcept
{
	ASSERT_OR_IGNORE(offset < content.count());

	u64 line = 1;

	u64 line_begin = 0;

	for (u64 i = 0; i != offset; ++i)
	{
		if (content[i] == '\n')
		{
			line += 1;

			line_begin = i + 1;
		}
	}

	return { static_cast<u32>(line), static_cast<u32>(offset - line_begin + 1) };
}

__declspec(noreturn) void source_error(u64 offset, Range<char8> content, Range<char8> filepath, const char8* format, ...) noexcept
{
	va_list args;

	va_start(args, format);

	vsource_error(offset, content, filepath, format, args);
}

__declspec(noreturn) void vsource_error(u64 offset, Range<char8> content, Range<char8> filepath, const char8* format, va_list args) noexcept
{
	ASSERT_OR_IGNORE(offset <= UINT32_MAX);

	const SourceLocation location = source_location_from(static_cast<u32>(offset), content);

	fprintf(stderr, "%.*s:%u:%u: ", static_cast<u32>(filepath.count()), filepath.begin(), location.line, location.character);

	vpanic(format, args);
}
