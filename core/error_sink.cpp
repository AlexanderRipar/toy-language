#include "core.hpp"

#include <cstdio>
#include <cstdarg>
#include <csetjmp>
#include <fcntl.h>

#ifdef _WIN32
	#include <io.h>
#endif

struct ErrorSink
{
	SourceReader* reader;

	IdentifierPool* identifiers;

	FILE* log_file;

	bool has_error_jmp_buf;

	jmp_buf* error_jmp_buf;
};

[[nodiscard]] ErrorSink* create_error_sink(AllocPool* alloc, SourceReader* reader, IdentifierPool* identifiers, minos::FileHandle log_file) noexcept
{
	ErrorSink* const errors = static_cast<ErrorSink*>(alloc_from_pool(alloc, sizeof(ErrorSink), alignof(ErrorSink)));

	FILE* log_file_ptr;

	if (log_file.m_rep == minos::standard_file_handle(minos::StdFileName::StdErr).m_rep)
	{
		// Prevent a double-close during CRT teardown.
		log_file_ptr = stderr;
	}
	else
	{
		#ifdef _WIN32
			const s32 fd = _open_osfhandle(reinterpret_cast<intptr_t>(log_file.m_rep), _O_APPEND);

			if (fd == -1)
				panic("_open_osfhandle failed.\n");

			log_file_ptr = _fdopen(fd, "a");
		#else
			log_file_ptr = fdopen(static_cast<s32>(reinterpret_cast<u64>(log_file.m_rep)), "a");
		#endif

		if (log_file_ptr == nullptr)
			panic("Failed to convert diagnostics log file handle to `FILE*`.\n");
	}

	errors->reader = reader;
	errors->identifiers = identifiers;
	errors->log_file = log_file_ptr;
	errors->has_error_jmp_buf = false;

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

void set_error_handling_context(ErrorSink* errors, jmp_buf* setjmpd_longjmp_buf) noexcept
{
	errors->has_error_jmp_buf = true;

	errors->error_jmp_buf = setjmpd_longjmp_buf;
}

NORETURN void error_exit(ErrorSink* errors) noexcept
{
	if (errors->has_error_jmp_buf)
		longjmp(*errors->error_jmp_buf, 1);
	else
	{
		DEBUGBREAK;

		minos::exit_process(1);
	}
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
