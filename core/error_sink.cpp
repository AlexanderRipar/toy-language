#include "core.hpp"

#include <cstdio>
#include <cstdarg>

struct ErrorSink
{
	SourceReader* reader;

	IdentifierPool* identifiers;
};

[[nodiscard]] ErrorSink* create_error_sink(AllocPool* alloc, SourceReader* reader, IdentifierPool* identifiers) noexcept
{
	ErrorSink* const errors = static_cast<ErrorSink*>(alloc_from_pool(alloc, sizeof(ErrorSink), alignof(ErrorSink)));

	errors->reader = reader;
	errors->identifiers = identifiers;

	return errors;
}

void release_error_sink([[maybe_unused]] ErrorSink* errors) noexcept
{
	// No-op
}

NORETURN void source_error(ErrorSink* errors, SourceId source_id, const char8* format, ...) noexcept
{
	va_list args;

	va_start(args, format);

	vsource_error(errors, source_id, format, args);
}

NORETURN void vsource_error(ErrorSink* errors, SourceId source_id, const char8* format, va_list args) noexcept
{
	const SourceLocation location = source_location_from_source_id(errors->reader, source_id);

	print_error(&location, format, args);

	error_exit();
}

void source_error_nonfatal(ErrorSink* errors, SourceId source_id, const char8* format, ...) noexcept
{
	va_list args;

	va_start(args, format);

	vsource_error_nonfatal(errors, source_id, format, args);

	va_end(args);
}

void vsource_error_nonfatal(ErrorSink* errors, SourceId source_id, const char8* format, va_list args) noexcept
{
	const SourceLocation location = source_location_from_source_id(errors->reader, source_id);

	print_error(&location, format, args);
}

void source_warning(ErrorSink* errors, SourceId source_id, const char8* format, ...) noexcept
{
	va_list args;

	va_start(args, format);

	vsource_warning(errors, source_id, format, args);

	va_end(args);
}

void vsource_warning(ErrorSink* errors, SourceId source_id, const char8* format, va_list args) noexcept
{
	const SourceLocation location = source_location_from_source_id(errors->reader, source_id);

	print_error(&location, format, args);
}

NORETURN void error_exit() noexcept
{
	DEBUGBREAK;

	minos::exit_process(1);
}

void print_error(const SourceLocation* location, const char8* format, va_list args) noexcept
{
	fprintf(stderr, "%.*s:%u:%u: ",
		static_cast<s32>(location->filepath.count()), location->filepath.begin(),
		location->line_number,
		location->column_number
	);

	vfprintf(stderr, format, args);
}
