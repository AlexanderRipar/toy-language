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

	u8 source_tab_size;

	ReservedVec<ErrorRecord> records;

	minos::FileHandle log_file;
};

static const char8* error_message_of(CompileError error) noexcept
{
	static constexpr const char8* FORMATS[] = {
		"[unknown compiler error code]",                                                                                                                // INVALID
		"Value of compile-time integer exceeds bounds of implicit conversion target.\n",                                                                // CompIntegerValueTooLarge
		"Cannot implicitly convert to destination type.",                                                                                               // TypesCannotConvert
		"Destination type of composite literal conversion has no corresponding member.\n",                                                              // CompositeLiteralTargetIsMissingMember
		"Too many members in composite literal to convert to destination type.\n",                                                                      // CompositeLiteralTargetHasTooFewMembers
		"Member mapped more than once during composite literal conversion.\n",                                                                          // CompositeLiteralTargetMemberMappedTwice
		"Member cannot be initialized from composite literal member because their types do not match.\n",                                               // ImplicitConversionCompositeLiteralMemberTypesCannotConvert
		"Composite initializer is missing initializer for member of target type which lacks a default value.\n",                                        // CompositeLiteralSourceIsMissingMember
		"Could not unify argument types.\n",                                                                                                            // NoCommonArgumentType
		"Array literal element cannot be implicitly converted to type of preceding elements.\n",                                                        // NoCommonArrayElementType
		"Parameter with the given name does not exist for the called function.\n",                                                                      // CallNoSuchNamedParameter
		"Multiple arguments for given for the same parameter.\n",                                                                                       // CallArgumentMappedTwice
		"Too many arguments supplied to function call.\n",                                                                                              // CallTooManyArgs
		"Missing argument for parameter.\n",                                                                                                            // CallMissingArg
		"Left-hand-side of slicing operator must be of multi-pointer and not single-pointer type.\n",                                                   // SliceOperatorInvalidLhsType
		"End index of slicing operator cannot be elided with left-hand-side of multi-pointer type, as the end cannot be derived.\n",                    // SliceOperatorMultiPtrElidedEndIndex
		"Index of slicing operator must be less than the element count of the indexed array or slice.\n",                                               // SliceOperatorIndexOutOfBounds
		"Begin index of slicing operator must be less than or equal to end index.\n",                                                                   // SliceOperatorIndicesReversed
		"Index of slicing operator must fit into unsigned 64-bit integer.\n",                                                                           // SliceOperatorIndexTooLarge
		"Left-hand-side of slicing operator occurring in untyped context cannot be an empty array literal.\n",                                          // SliceOperatorUntypedArrayLiteral
		"Operand of `.*` must be a pointer.\n",                                                                                                         // DerefInvalidOperandType
		"`~` can only be applied to integer operands.\n",                                                                                               // BitNotInvalidOperandType
		"Unary `-` can only be applied to signed integer or float-point operands.\n",                                                                   // NegateInvalidOperandType
		"Unary `+` can only be applied to integer or float-point operands.\n",                                                                          // UnaryPlusInvalidOperandType
		"The operator is only supported for Integer and Float operands.\n",                                                                             // BinaryOperatorNumericInvalidArgumentType
		"The operator is only supported for Integer operands.\n",                                                                                       // BinaryOperatorIntegerInvalidArgumentType
		"The operator is only supported for Integer and Boolean operands.\n",                                                                           // BinaryOperatorIntegerOrBoolInvalidArgumentType
		"Overflow encountered while evaluating operator.\n",                                                                                            // ArithmeticOverflow
		"Division by zero encountered.\n",                                                                                                              // DivideByZero
		"Modulo by zero encountered.\n",                                                                                                                // ModuloByZero
		"Right-hand-side of shift operator must not be negative.\n",                                                                                    // ShiftRHSNegative
		"Shifting by 2^16 or more is not supported.\n",                                                                                                 // ShiftRHSTooLarge
		"Left-hand-side of `.` has no member with the given name.\n",                                                                                   // MemberNoSuchName
		"Left-hand-side of `.` must be either a composite value or a composite type.\n",                                                                // MemberInvalidLhsType
		"`.` with a type-valued left-hand-side can only access global members.\n",                                                                      // MemberNonGlobalAccessedThroughType
		"Cannot compare values of the given type.\n",                                                                                                   // CompareIncomparableType
		"Cannot order values of the given type.\n",                                                                                                     // CompareUnorderedType
		"Left-hand-side of `=` operator must be mutable.\n",                                                                                            // SetLhsNotMutable
		"Array element count must be of Integer type.\n",                                                                                               // TypeArrayCountInvalidType
		"Array element count must fit into unsigned 64-bit integer.\n",                                                                                 // TypeArrayCountTooLarge
		"Left-hand-side of index operator must have array, slice, or multi-pointer type.\n",                                                            // ArrayIndexLhsInvalidType
		"Right-hand-side of index operator must have integer type.\n",                                                                                  // ArrayIndexRhsInvalidType
		"Right-hand-side of index operator must fit into unsigned 64-bit integer.\n",                                                                   // ArrayIndexRhsTooLarge
		"Index exceeds element count.\n",                                                                                                               // ArrayIndexOutOfBounds
		"Array initializers with more than 65535 elements are not supported.\n",                                                                        // ArrayInitializerTooManyElements
		"Array initializers must initialize all elements.\n",                                                                                           // ArrayInitializerMissingElement
		"Array initializer sets one of its elements more than once.\n",                                                                                 // ArrayInitializerDuplicateElement
		"Expected expression to evaluate to void.\n",                                                                                                   // ExpectedVoid
		"Alignment passed to `_complete_type` must not exceed the maximum supported value of 2^32 - 1.\n",                                              // BuiltinCompleteTypeAlignTooLarge
		"Alignment passed to `_complete_type` must not be zero.\n",                                                                                     // BuiltinCompleteTypeAlignZero
		"Alignment passed to `_complete_type` must be a power of two.\n",                                                                               // BuiltinCompleteTypeAlignNotPowTwo
		"Function type passed to _returntypeof must not have a templated return type.\n",                                                               // ReturntypeOfTemplatedReturnType
		"Initializer of global variable cannot reference its own value.\n",                                                                             // CyclicGlobalInitializerDependency
		"Reached `unreachable`.\n",                                                                                                                     // UnreachableReached
		"File does not contain a global definition with the given name.\n",                                                                             // GlobalNameNotDefined
		"Total size of closed-over values in single closure exceeds supported maximum of 2^32 - 1.\n",                                                  // ClosureTooLarge
		"Exceeded maximum number of definitions in a single scope.\n",                                                                                  // ScopeTooManyDefinitions
		"More than one definition with the same name in the same scope.\n",                                                                             // ScopeDuplicateName
		"Name not defined\n",                                                                                                                           // ScopeNameNotDefined
		"Unexpected character in source file.\n",                                                                                                       // LexUnexpectedCharacter
		"Null character in source file.\n",                                                                                                             // LexNullCharacter
		"`/*` without matching `*/`.\n",                                                                                                                // LexCommentMismatchedBegin
		"`*/` without previous matching `/*`.\n",                                                                                                       // LexCommentMismatchedEnd
		"Unknown builtin.\n",                                                                                                                           // LexBuiltinUnknown
		"Expected at least one digit in integer literal.\n",                                                                                            // LexNumberWithBaseMissingDigits
		"Expected at least one digit after decimal point in float literal.\n",                                                                          // LexNumberUnexpectedCharacterAfterDecimalPoint
		"Unexpected character after integer literal.\n",                                                                                                // LexNumberUnexpectedCharacterAfterInteger
		"Unexpected character after float literal.\n",                                                                                                  // LexNumberUnexpectedCharacterAfterFloat
		"Float literal exceeds maximum IEEE-754 value.\n",                                                                                              // LexNumberFloatTooLarge
		"Expected utf-8 surrogate code unit (0b10xx'xxxx).\n",                                                                                          // LexCharacterBadSurrogateCodeUnit
		"Unexpected code unit at start of character literal. This might be an encoding issue regarding the source file, as only utf-8 is supported.\n", // LexCharacterBadLeadCodeUnit
		"Expected two hexadecimal digits after character literal escape `\\x`.\n",                                                                      // LexCharacterEscapeSequenceLowerXBadChar
		"Expected six hexadecimal digits after character literal escape `\\X`.\n",                                                                      // LexCharacterEscapeSequenceUpperXInvalidChar
		"Codepoint indicated in character literal escape `\\X` is greater than the maximum unicode codepoint U+10FFFF.\n",                              // LexCharacterEscapeSequenceUpperXCodepointTooLarge
		"Expected four decimal digits after character literal escape `\\u`.\n",                                                                         // LexCharacterEscapeSequenceUInvalidChar
		"Unknown character literal escape.\n",                                                                                                          // LexCharacterEscapeSequenceUnknown
		"Expected end of character literal `'`.\n",                                                                                                     // LexCharacterExpectedEnd
		"String constant is longer than the supported maximum of 4096 bytes.\n",                                                                        // LexStringTooLong
		"String constant spans across newline.\n",                                                                                                      // LexStringCrossesNewline
		"String not ended before end of file.\n",                                                                                                       // LexStringMissingEnd
		"Illegal identifier starting with `_`.\n",                                                                                                      // LexIdentifierInitialUnderscore
		"Unexpected control character in config file.\n",                                                                                               // LexConfigUnexpectedControlCharacter
		"Single-line string not ended before end of line.\n",                                                                                           // LexConfigSingleLineStringCrossesNewline
		"Unexpected character in config file.\n",                                                                                                       // LexConfigUnexpectedCharacter
		"Missing operand for unary operator.\n",                                                                                                        // ParseUnaryOperatorMissingOperand
		"Missing operand for binary operator.\n",                                                                                                       // ParseBinaryOperatorMissingOperand
		"Expression exceeds maximum number of open operands.\n",                                                                                        // ParseOpenOperandCountTooLarge
		"Expression exceeds maximum number of open operators.\n",                                                                                       // ParseOpenOperatorCountTooLarge
		"Mismatched operand / operator count.\n",                                                                                                       // ParseOperatorOperandCountMismatch
		"Function parameters must not be `pub`.\n",                                                                                                     // ParseFunctionParameterIsPub
		"Definition modifier `pub` encountered more than once.\n",                                                                                      // ParseDefinitionMultiplePub
		"Definition modifier `mut` encountered more than once.\n",                                                                                      // ParseDefinitionMultipleMut
		"Expected definition name.\n",                                                                                                                  // ParseDefinitionMissingName
		"Expected `=` after Definition identifier and type.\n",                                                                                         // ParseDefinitionMissingEquals
		"Expected `<-` after for-each loop variables.\n",                                                                                               // ParseForeachExpectThinArrowLeft
		"Expected `->` after case label expression.\n",                                                                                                 // ParseCaseMissingThinArrowRight
		"Expected at least one case after switch expression.\n",                                                                                        // ParseSwitchMissingCase
		"Expected `(` after `proc`.\n",                                                                                                                 // ParseSignatureMissingParenthesisAfterProc
		"Expected `(` after `func`.\n",                                                                                                                 // ParseSignatureMissingParenthesisAfterFunc
		"Expected `(` after `trait`.\n",                                                                                                                // ParseSignatureMissingParenthesisAfterTrait
		"Exceeded maximum of 64 function parameters.\n",                                                                                                // ParseSignatureTooManyParameters
		"Expected `,` or `)` after parameter definition.\n",                                                                                            // ParseSignatureUnexpectedParameterListEnd
		"Expected `=` or `expects` after trait parameter list.\n",                                                                                      // ParseTraitMissingSetOrExpects
		"Expected `=` after trait expects clause.\n",                                                                                                   // ParseTraitMissingSet
		"Expected definition or `impl` at file's top level.\n",                                                                                         // ParseUnexpectedTopLevelExpr
		"Expected `}` or `,` after composite initializer member expression.\n",                                                                         // ParseCompositeLiteralUnexpectedToken
		"Expected `]` or `,` after array initializer element expression.\n",                                                                            // ParseArrayLiteralUnexpectedToken
		"Expected `]` after array type's size expression.\n",                                                                                           // ParseArrayTypeUnexpectedToken
		"Expected identifier after prefix `.` operator.\n",                                                                                             // ParseImpliedMemberUnexpectedToken
		"Expected operand or unary operator.\n",                                                                                                        // ParseExprExpectOperand
		"Exceeded maximum of 64 function call arguments.\n",                                                                                            // ParseCallTooManyArguments
		"Expected `)` or `,` after function argument expression.\n",                                                                                    // ParseCallUnexpectedToken
		"Expected `]` after slice index expression.\n",                                                                                                 // ParseSliceUnexpectedToken
		"Expected `]` after array index expression.\n",                                                                                                 // ParseArrayIndexUnexpectedToken
		"Expected `->` after inbound definition in catch.\n",                                                                                           // ParseCatchMissingThinArrowRightAfterDefinition
		"Expected identifier after infix `.` operator.\n",                                                                                              // ParseMemberUnexpectedToken
		"Key nesting limit exceeded.\n",                                                                                                                // ParseConfigKeyNestingLimitExceeded
		"Tried assigning to key that does not expect subkeys.\n",                                                                                       // ParseConfigKeyNotExpectingSubkeys
		"Key does not exist.\n",                                                                                                                        // ParseConfigKeyDoesNotExist
		"Expected key name.\n",                                                                                                                         // ParseConfigExpectedKey
		"Expected `=`.\n",                                                                                                                              // ParseConfigExpectedEquals
		"Expected `}` or `,`.\n",                                                                                                                       // ParseConfigExpectedClosingCurlyOrComma
		"Expected a value.\n",                                                                                                                          // ParseConfigExpectedValue
		"Value has the wrong type for the given key.\n",                                                                                                // ParseConfigWrongValueTypeForKey
		"`\\u` escape expects four hex digits.\n",                                                                                                      // ParseConfigEscapeSequenceLowerUTooFewCharacters
		"`\\U` escape expects eight hex digits.\n",                                                                                                     // ParseConfigEscapeSequenceUpperUTooFewCharacters
		"Expected hexadecimal escape character but.\n",                                                                                                 // ParseConfigEscapeSequenceUtfInvalidCharacter
		"Escaped codepoint is larger than the maximum unicode codepoint (0x10FFFF).\n",                                                                 // ParseConfigEscapeSequenceUtfCodepointTooLarge
		"Unexpected escape sequence.\n",                                                                                                                // ParseConfigEscapeSequenceInvalid
		"Resulting absolute path exceeds maximum path length.\n",                                                                                       // ParseConfigPathTooLong
		"Expected `]`.\n",                                                                                                                              // ParseConfigExpectedClosingBracket
		"Expected `=` or `.`.\n",                                                                                                                       // ParseConfigExpectedEqualsOrDot
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

ErrorSink* create_error_sink(HandlePool* alloc, SourceReader* reader, IdentifierPool* identifiers, AstPool* asts, u8 source_tab_size, minos::FileHandle log_file) noexcept
{
	ErrorSink* const errors = alloc_handle_from_pool<ErrorSink>(alloc);

	byte* const memory = static_cast<byte*>(minos::mem_reserve(sizeof(ErrorRecord) * MAX_ERROR_RECORD_COUNT));

	if (memory == nullptr)
		panic("Could not reserve memory for ErrorSink (0x%X).\n", minos::last_error());

	errors->reader = reader;
	errors->identifiers = identifiers;
	errors->asts = asts;
	errors->error_count = 0;
	errors->source_tab_size = source_tab_size;
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

		print_error(errors->log_file, &location, record.error, errors->source_tab_size);
	}
}

Range<ErrorRecord> get_errors(ErrorSink* errors) noexcept
{
	return Range<ErrorRecord>{ errors->records.begin(), errors->records.end() };
}

void print_error(minos::FileHandle dst, const SourceLocation* location, CompileError error, u8 tab_size) noexcept
{
	FILE* const c_fileptr = c_fileptr_from_minos(dst);

	const char8* const message = error_message_of(error);

	const u32 error_offset_in_context = location->column_number < location->context_offset + 1
		? 0
		: location->column_number - location->context_offset - 1;

	const u32 column_number = location->column_number + location->tabs_before_column_number * (tab_size - 1);

	const u8 log10_line_number = log10_ceil(location->line_number);

	const s32 error_indicator_preindent = log10_line_number < 5
		? 5
		: static_cast<s32>(log10_line_number);

	fprintf(c_fileptr,
		" %.*s:%u:%u: %s\n"
		" %5u | %.*s\n"
		" %*s | ",
		static_cast<s32>(location->filepath.count()), location->filepath.begin(),
		location->line_number,
		column_number,
		message,
		location->line_number,
		static_cast<s32>(location->context_chars), location->context,
		error_indicator_preindent, ""
	);

	for (u32 i = 0; i != error_offset_in_context; ++i)
	{
		if (location->context[i] == '\t')
			fputc('\t', c_fileptr);
		else
			fputc(' ', c_fileptr);
	}

	fputs("^\n", c_fileptr);
}
