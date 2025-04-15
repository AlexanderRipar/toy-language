#include "pass_data.hpp"

#include <cstdio>

[[nodiscard]] ErrorSink* create_error_sink([[maybe_unused]] AllocPool* pool, SourceReader* reader) noexcept
{
	return reinterpret_cast<ErrorSink*>(reader);
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
	const SourceLocation location = source_location_from_source_id(reinterpret_cast<SourceReader*>(errors), source_id);

	print_error(&location, format, args);

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