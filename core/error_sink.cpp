#include "core.hpp"

#include <cstdio>
#include <cstdarg>
#include <csetjmp>

struct ErrorSink
{
	SourceReader* reader;

	IdentifierPool* identifiers;

	bool has_error_dst;

	// So... I guess MSVC pads aligns this to 16 bytes and then decides to warn
	// about it. We are fine with this extra alignment, since it is handled by
	// `AllocPool`, so we can just ignore the warning.
	#if COMPILER_MSVC
	#pragma warning(push)
	#pragma warning(disable : 4324) // C4324: structure was padded due to alignment specifier
	#endif
	jmp_buf error_dst;
	#if COMPILER_MSVC
	#pragma warning(pop)
	#endif
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

	error_exit(errors);
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

// We don't do C++ object destruction here lol.
#if COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable : 4611) // C4611: interaction between '_setjmp' and C++ object destruction is non-portable
#endif
bool set_error_handling_context(ErrorSink* errors) noexcept
{
	errors->has_error_dst = true;

	// This is intentionally an `if (true) return true`, as `setjmp` must only
	// be used in very specific context (e.g. in a comparison in the condition
	// of an `if`), so
	// DO *NOT* CLEAN THIS UP.
	if (setjmp(errors->error_dst) != 0)
		return true;

	return false;
}
#if COMPILER_MSVC
#pragma warning(pop)
#endif

NORETURN void error_exit(ErrorSink* errors) noexcept
{
	DEBUGBREAK;

	if (errors->has_error_dst)
		longjmp(errors->error_dst, 1);
	else
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
