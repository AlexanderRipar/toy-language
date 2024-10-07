#ifndef ERROR_INCLUDE_GUARD
#define ERROR_INCLUDE_GUARD

#include "common.hpp"
#include "range.hpp"

#include <cstdarg>
#include <cstdio>

struct SourceLocation
{
	Range<char8> filepath;

	u32 line;

	u32 character;
};

struct ErrorHandler
{
private:

    Range<char8> m_filepath;

    Range<char8> m_content;

public:

    ErrorHandler() noexcept = default;

    ErrorHandler(Range<char8> filepath, Range<char8> content) noexcept :
        m_filepath{ filepath },
        m_content{ content }
    {}

	void prime(Range<char8> filepath, Range<char8> content) noexcept
	{
        m_filepath = filepath;

        m_content = content;
	}

	SourceLocation source_location_from(u64 offset) const noexcept
	{
		ASSERT_OR_IGNORE(offset < m_content.count());

		u64 line = 1;

		u64 line_begin = 0;

		for (u64 i = 0; i != offset; ++i)
		{
			if (m_content[i] == '\n')
			{
				line += 1;

				line_begin = i + 1;
			}
		}

		return { m_filepath, static_cast<u32>(line), static_cast<u32>(offset - line_begin + 1) };
	}

	__declspec(noreturn) void log(u64 offset, const char8* format, ...) const noexcept
	{
		va_list args;

		va_start(args, format);

		vlog(offset, format, args);
	}

	__declspec(noreturn) void vlog(u64 offset, const char8* format, va_list args) const noexcept
	{
		const SourceLocation location = source_location_from(offset);

		fprintf(stderr, "%.*s:%u:%u: ", static_cast<u32>(location.filepath.count()), location.filepath.begin(), location.line, location.character);

		vpanic(format, args);
	}
};

#endif // ERROR_INCLUDE_GUARD
