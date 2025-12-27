#include "core.hpp"

#include "../infra/container/reserved_vec.hpp"

#include <cstdio>
#include <cstdarg>
#include <csetjmp>
#include <fcntl.h>

#ifdef _WIN32
	#include <io.h>
#endif

static constexpr u32 MAX_ERROR_RECORD_COUNT = 16384;

struct ErrorSink
{
	SourceReader* reader;

	IdentifierPool* identifiers;

	AstPool* asts;

	u32 error_count;

	ReservedVec<ErrorRecord> records;

	minos::FileHandle log_file;
};

static const char8* error_message_of(CompileError error) noexcept
{
	static constexpr const char8* FORMATS[] = {
		"[unknown compiler error code]",
		"Value of compile-time integer exceeds bounds of implicit conversion target.\n",
		"Cannot implicitly convert to destination type.",
		"Destination type of composite literal conversion has no corresponding member.\n",
		"Too many members in composite literal to convert to destination type.\n",
		"Member mapped more than once during composite literal conversion.\n",
		"Member cannot be initialized from composite literal member because their types do not match.\n",
		"Composite initializer is missing initializer for member of target type which lacks a default value.\n",
		"Cannot use expression as a location of the desired type, as it requires implicit conversion.\n",
		"Could not unify argument types.\n",
		"Array literal element cannot be implicitly converted to type of preceding elements.\n",
		"Parameter with the given name does not exist for the called function.\n",
		"Multiple arguments for given for the same parameter.\n",
		"Too many arguments supplied to function call.\n",
		"Missing argument for parameter.\n",
		"Left-hand-side of slicing operator must be of multi-pointer and not single-pointer type.\n",
		"Begin index of slicing operator must be of integer type.\n",
		"End index of slicing operator cannot be elided with left-hand-side of multi-pointer type, as the end cannot be derived.\n",
		"Index of slicing operator must be less than the element count of the indexed array or slice.\n",
		"Begin index of slicing operator must be less than or equal to end index.\n",
		"Index of slicing operator must fit into unsigned 64-bit integer.\n",
		"Left-hand-side of slicing operator occurring in untyped context cannot be an empty array literal.\n",
		"Operand of `.*` must be a pointer.\n",
		"`~` can only be applied to integer operands.\n",
		"Unary `-` can only be applied to signed integer or float-point operands.\n",
		"Unary `+` can only be applied to integer or float-point operands.\n",
		"The operator is only supported for Integer and Float operands.\n",
		"The operator is only supported for Integer operands.\n",
		"The operator is only supported for Integer and Boolean operands.\n",
		"Overflow encountered while evaluating operator.\n",
		"Division by zero encountered.\n",
		"Right-hand-side of shift operator must not be negative.\n",
		"Shifting by 2^16 or more is not supported.\n",
		"Left-hand-side of `.` has no member with the given name.\n",
		"Left-hand-side of `.` must be either a composite value or a composite type.\n",
		"`.` with a type-valued left-hand-side can only access global members.\n",
		"Cannot compare values of the given type.\n",
		"Cannot order values of the given type.\n",
		"Left-hand-side of `=` operator must be mutable.\n",
		"Array element count must be of Integer type.\n",
		"Array element count must fit into unsigned 64-bit integer.\n",
		"Left-hand-side of index operator must have array, slice, or multi-pointer type.\n",
		"Right-hand-side of index operator must have integer type.\n",
		"Right-hand-side of index operator must fit into unsigned 64-bit integer.\n",
		"Index exceeds element count.\n",
		"Alignment passed to `_complete_type` must not exceed the maximum supported value of 2^32 - 1.\n",
		"Alignment passed to `_complete_type` must not be zero.\n",
		"Alignment passed to `_complete_type` must be a power of two.\n",
		"Reached `unreachable`.\n",
		"Total size of closed-over values in single closure exceeds supported maximum of 2^32 - 1.\n",
		"Exceeded maximum number of definitions in a single scope.\n",
		"More than one definition with the same name in the same scope.\n",
		"Name not defined\n",
		"Unexpected character in source file.\n",
		"Null character in source file.\n",
		"`/*` without matching `*/`.\n",
		"`*/` without previous matching `/*`.\n",
		"Unknown builtin.\n",
		"Expected at least one digit in integer literal.\n",
		"Expected at least one digit after decimal point in float literal.\n",
		"Unexpected character after integer literal.\n",
		"Unexpected character after float literal.\n",
		"Float literal exceeds maximum IEEE-754 value.\n",
		"Expected utf-8 surrogate code unit (0b10xx'xxxx).\n",
		"Unexpected code unit at start of character literal. This might be an encoding issue regarding the source file, as only utf-8 is supported.\n",
		"Expected two hexadecimal digits after character literal escape `\\x`.\n",
		"Expected six hexadecimal digits after character literal escape `\\X`.\n",
		"Codepoint indicated in character literal escape `\\X` is greater than the maximum unicode codepoint U+10FFFF.\n",
		"Expected four decimal digits after character literal escape `\\u`.\n",
		"Unknown character literal escape.\n",
		"Expected end of character literal `'`.\n",
		"String constant is longer than the supported maximum of 4096 bytes.\n",
		"String constant spans across newline.\n",
		"String not ended before end of file.\n",
		"Illegal identifier starting with `_`.\n",
		"Unexpected control character in config file.\n",
		"Single-line string not ended before end of line.\n",
		"Unexpected character in config file.\n",
		"Missing operand for unary operator.\n",
		"Missing operand for binary operator.\n",
		"Expression exceeds maximum number of open operands.\n",
		"Expression exceeds maximum number of open operators.\n",
		"Mismatched operand / operator count.\n",
		"Function parameters must not be `pub`.\n",
		"Definition modifier `pub` encountered more than once.\n",
		"Definition modifier `mut` encountered more than once.\n",
		"Expected definition name.\n",
		"Expected `=` after Definition identifier and type.\n",
		"Expected `<-` after for-each loop variables.\n",
		"Expected `->` after case label expression.\n",
		"Expected at least one case after switch expression.\n",
		"Expected `(` after `proc`.\n",
		"Expected `(` after `func`.\n",
		"Expected `(` after `trait`.\n",
		"Exceeded maximum of 64 function parameters.\n",
		"Expected `,` or `)` after parameter definition.\n",
		"Expected `=` or `expects` after trait parameter list.\n",
		"Expected `=` after trait expects clause.\n",
		"Expected definition or `impl` at file's top level.\n",
		"Expected `}` or `,` after composite initializer member expression.\n",
		"Expected `]` or `,` after array initializer element expression.\n",
		"Expected `]` after array type's size expression.\n",
		"Expected identifier after prefix `.` operator.\n",
		"Expected operand or unary operator.\n",
		"Exceeded maximum of 64 function call arguments.\n",
		"Expected `)` or `,` after function argument expression.\n",
		"Expected `]` after slice index expression.\n",
		"Expected `]` after array index expression.\n",
		"Expected `->` after inbound definition in catch.\n",
		"Expected identifier after infix `.` operator.\n",
		"Key nesting limit exceeded.\n",
		"Tried assigning to key that does not expect subkeys.\n",
		"Key does not exist.\n",
		"Expected key name.\n",
		"Expected `=`.\n",
		"Expected `}` or `,`.\n",
		"Expected a value.\n",
		"Value has the wrong type for the given key.\n",
		"`\\u` escape expects four hex digits.\n",
		"`\\U` escape expects eight hex digits.\n",
		"Expected hexadecimal escape character but.\n",
		"Escaped codepoint is larger than the maximum unicode codepoint (0x10FFFF).\n",
		"Unexpected escape sequence.\n",
		"Resulting absolute path exceeds maximum path length.\n",
		"Expected `]`.\n",
		"Expected `=` or `.`.\n",
	};

	u32 ordinal = static_cast<u32>(error);

	if (ordinal >= array_count(FORMATS))
		ordinal = 0;

	return FORMATS[ordinal];
}

static FILE* c_fileptr_from_minos(minos::FileHandle filehandle) noexcept
{
	ASSERT_OR_IGNORE(filehandle.m_rep != nullptr);

	if (filehandle.m_rep == minos::standard_file_handle(minos::StdFileName::StdErr).m_rep)
	{
		// Prevent a double-close during CRT teardown.
		return stderr;
	}
	else
	{
		FILE* c_fileptr;

		{
		#ifdef _WIN32
			const s32 fd = _open_osfhandle(reinterpret_cast<intptr_t>(filehandle.m_rep), _O_APPEND);

			if (fd == -1)
				panic("_open_osfhandle failed.\n");

			c_fileptr = _fdopen(fd, "a");
		#else
			c_fileptr = fdopen(static_cast<s32>(reinterpret_cast<u64>(filehandle.m_rep)), "a");
		#endif
		}

		if (c_fileptr == nullptr)
			panic("Failed to convert diagnostics log file handle to `FILE*`.\n");

		return c_fileptr;
	}
}

ErrorSink* create_error_sink(HandlePool* alloc, SourceReader* reader, IdentifierPool* identifiers, AstPool* asts, minos::FileHandle log_file) noexcept
{
	ErrorSink* const errors = alloc_handle_from_pool<ErrorSink>(alloc);

	byte* const memory = static_cast<byte*>(minos::mem_reserve(sizeof(ErrorRecord) * MAX_ERROR_RECORD_COUNT));

	if (memory == nullptr)
		panic("Could not reserve memory for ErrorSink (0x%X).\n", minos::last_error());

	errors->reader = reader;
	errors->identifiers = identifiers;
	errors->asts = asts;
	errors->records.init({ memory, sizeof(ErrorRecord) * MAX_ERROR_RECORD_COUNT }, 1024);
	errors->log_file = log_file;

	return errors;
}

void release_error_sink([[maybe_unused]] ErrorSink* errors) noexcept
{
	// No-op
}

void record_error(ErrorSink* errors, SourceId source_id, CompileError error) noexcept
{
	DEBUGBREAK;

	const u32 prev_error_count = errors->error_count;

	errors->error_count = prev_error_count + 1;

	if (prev_error_count < MAX_ERROR_RECORD_COUNT)
		errors->records.append(ErrorRecord{ error, source_id });
}

void record_error(ErrorSink* errors, const AstNode* source_node, CompileError error) noexcept
{
	record_error(errors, source_id_of_ast_node(errors->asts, source_node), error);
}

void print_errors(ErrorSink* errors) noexcept
{
	if (errors->log_file.m_rep == nullptr)
		return;

	const Range<ErrorRecord> records = get_errors(errors);

	for (const ErrorRecord record : records)
	{
		const SourceLocation location = source_location_from_source_id(errors->reader, record.source_id);

		print_error(errors->log_file, &location, record.error);
	}
}

Range<ErrorRecord> get_errors(ErrorSink* errors) noexcept
{
	return Range<ErrorRecord>{ errors->records.begin(), errors->records.end() };
}

void print_error(minos::FileHandle dst, const SourceLocation* location, CompileError error) noexcept
{
	FILE* const c_fileptr = c_fileptr_from_minos(dst);

	const char8* const message = error_message_of(error);

	fprintf(c_fileptr,
		" %.*s:%u:%u: %s\n"
		" %5u | %.*s\n"
		"       | %*s^\n",
		static_cast<s32>(location->filepath.count()), location->filepath.begin(),
		location->line_number,
		location->column_number,
		message,
		location->line_number,
		static_cast<s32>(location->context_chars), location->context,
		static_cast<s32>(location->context_offset), ""
	);
}
