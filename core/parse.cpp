#include <cstdarg>
#include <cstdlib>
#include <errno.h>

#include "../infra/common.hpp"
#include "../infra/alloc_pool.hpp"
#include "../infra/container.hpp"
#include "../infra/hash.hpp"
#include "pass_data.hpp"
#include "ast_attach.hpp"

static constexpr u32 MAX_STRING_LITERAL_BYTES = 4096;

struct Lexeme
{
	Token token;

	SourceId source_id;

	union
	{
		u64 integer_value;

		f64 float_value; 

		IdentifierId identifier_id;
	};

	Lexeme() noexcept = default;

	Lexeme(Token token, SourceId source_id, u64 value_bits) noexcept :
		token{ token },
		source_id{ source_id },
		integer_value{ value_bits }
	{}
};

struct RawLexeme
{
	Token token;

	union
	{
		u64 integer_value;

		f64 float_value; 
	};

	RawLexeme(Token token) noexcept : token{ token } {}

	RawLexeme(Token token, u32 value) noexcept : token{ token }, integer_value{ value } {}

	RawLexeme(Token token, u64 value) noexcept : token{ token }, integer_value{ value } {}

	RawLexeme(Token token, f64 value) noexcept : token{ token }, float_value{ value } {}
};

// Operator Description Tuple. Consists of:
//  - `AstTag` with the node type
//  - `AstFlag` with the node flags
//  - `u8` precedence (low to high)
//  - `u8` whether it's right-associative
//  - `u8` is_binary whether it's a binary operator
//         (or unary, no ternary operators anywhere)
struct OperatorDesc
{
	AstTag node_type;

	AstFlag node_flags;

	u8 precedence;

	u8 is_right_to_left: 1;

	u8 is_binary : 1;
};

struct OperatorDescWithSource
{
	OperatorDesc operator_desc;

	SourceId source_id;
};

struct Lexer
{
	const char8* curr;

	const char8* begin;

	const char8* end;

	Lexeme peek;

	u32 source_id_base;

	bool is_std;

	IdentifierPool* identifiers;

	ErrorSink* errors;
};

struct OperatorStack
{
	u32 operand_count;

	u32 operator_top;

	SourceId expression_source_id;

	OperatorDescWithSource operators[64];

	AstBuilderToken operand_tokens[128];
};

struct Parser
{
	Lexer lexer;

	AstBuilder builder;
};

static constexpr char8 BUILTIN_NAMES[][8] = {
	"int",
	"type",
	"c_int",
	"c_flt",
	"c_str",
	{ 't', 'y', 'p', 'e', 'b', 'l', 'd', 'r' },
	"true",
	"typeof",
	"sizeof",
	"alignof",
	{ 's', 't', 'r', 'i', 'd', 'e', 'o', 'f' },
	{ 'o', 'f', 'f', 's', 'e', 't', 'o', 'f' },
	"nameof",
	"import",
	{ 't', 'b', '_', 'c', 'r', 'e', 'a', 't' },
	"tb_add",
	{ 't', 'b', '_', 'c', 'o', 'm', 'p', 'l'},
};


static constexpr OperatorDesc UNARY_OPERATOR_DESCS[] = {
	{ AstTag::INVALID,            AstFlag::EMPTY,      10, false, true  }, // ( - Opening Parenthesis
	{ AstTag::UOpEval,            AstFlag::EMPTY,       8, false, false }, // eval
	{ AstTag::UOpTry,             AstFlag::EMPTY,       8, false, false }, // try
	{ AstTag::UOpDefer,           AstFlag::EMPTY,       8, false, false }, // defer
	{ AstTag::UOpDistinct,        AstFlag::EMPTY,       2, false, false }, // distinct
	{ AstTag::UOpAddr,            AstFlag::EMPTY,       2, false, false }, // $
	{ AstTag::UOpBitNot,          AstFlag::EMPTY,       2, false, false }, // ~
	{ AstTag::UOpLogNot,          AstFlag::EMPTY,       2, false, false }, // !
	{ AstTag::UOpTypeOptPtr,      AstFlag::Type_IsMut,  2, false, false }, // ?
	{ AstTag::UOpTypeVar,         AstFlag::EMPTY,       2, false, false }, // ...
	{ AstTag::UOpTypeTailArray,   AstFlag::EMPTY,       2, false, false }, // [...]
	{ AstTag::UOpTypeMultiPtr,    AstFlag::Type_IsMut,  2, false, false }, // [*]
	{ AstTag::UOpTypeOptMultiPtr, AstFlag::Type_IsMut,  2, false, false }, // [?]
	{ AstTag::UOpTypeSlice,       AstFlag::Type_IsMut,  2, false, false }, // []
	{ AstTag::UOpImpliedMember,   AstFlag::EMPTY,       1, false, false }, // .
	{ AstTag::UOpTypePtr,         AstFlag::Type_IsMut,  2, false, false }, // *
	{ AstTag::UOpNegate,          AstFlag::EMPTY,       2, false, false }, // -
	{ AstTag::UOpPos,             AstFlag::EMPTY,       2, false, false }, // +
};

static constexpr OperatorDesc BINARY_OPERATOR_DESCS[] = {
	{ AstTag::OpMember,    AstFlag::EMPTY, 1, true,  true  }, // .
	{ AstTag::OpMul,       AstFlag::EMPTY, 2, true,  true  }, // *
	{ AstTag::OpSub,       AstFlag::EMPTY, 3, true,  true  }, // -
	{ AstTag::OpAdd,       AstFlag::EMPTY, 3, true,  true  }, // +
	{ AstTag::OpDiv,       AstFlag::EMPTY, 2, true,  true  }, // /
	{ AstTag::OpAddTC,     AstFlag::EMPTY, 3, true,  true  }, // +:
	{ AstTag::OpSubTC,     AstFlag::EMPTY, 3, true,  true  }, // -:
	{ AstTag::OpMulTC,     AstFlag::EMPTY, 2, true,  true  }, // *:
	{ AstTag::OpMod,       AstFlag::EMPTY, 2, true,  true  }, // %
	{ AstTag::UOpDeref,    AstFlag::EMPTY, 1, false, false }, // .*
	{ AstTag::OpBitAnd,    AstFlag::EMPTY, 6, true,  true  }, // &
	{ AstTag::OpBitOr,     AstFlag::EMPTY, 6, true,  true  }, // |
	{ AstTag::OpBitXor,    AstFlag::EMPTY, 6, true,  true  }, // ^
	{ AstTag::OpShiftL,    AstFlag::EMPTY, 4, true,  true  }, // <<
	{ AstTag::OpShiftR,    AstFlag::EMPTY, 4, true,  true  }, // >>
	{ AstTag::OpLogAnd,    AstFlag::EMPTY, 7, true,  true  }, // &&
	{ AstTag::OpLogOr,     AstFlag::EMPTY, 7, true,  true  }, // ||
	{ AstTag::OpCmpLT,     AstFlag::EMPTY, 5, true,  true  }, // <
	{ AstTag::OpCmpGT,     AstFlag::EMPTY, 5, true,  true  }, // >
	{ AstTag::OpCmpLE,     AstFlag::EMPTY, 5, true,  true  }, // <=
	{ AstTag::OpCmpGE,     AstFlag::EMPTY, 5, true,  true  }, // >=
	{ AstTag::OpCmpNE,     AstFlag::EMPTY, 5, true,  true  }, // !=
	{ AstTag::OpCmpEQ,     AstFlag::EMPTY, 5, true,  true  }, // ==
	{ AstTag::OpSet,       AstFlag::EMPTY, 9, false, true  }, // =
	{ AstTag::OpSetAdd,    AstFlag::EMPTY, 9, false, true  }, // +=
	{ AstTag::OpSetSub,    AstFlag::EMPTY, 9, false, true  }, // -=
	{ AstTag::OpSetMul,    AstFlag::EMPTY, 9, false, true  }, // *=
	{ AstTag::OpSetDiv,    AstFlag::EMPTY, 9, false, true  }, // /=
	{ AstTag::OpSetAddTC,  AstFlag::EMPTY, 9, false, true  }, // +:=
	{ AstTag::OpSetSubTC,  AstFlag::EMPTY, 9, false, true  }, // -:=
	{ AstTag::OpSetMulTC,  AstFlag::EMPTY, 9, false, true  }, // *:=
	{ AstTag::OpSetMod,    AstFlag::EMPTY, 9, false, true  }, // %=
	{ AstTag::OpSetBitAnd, AstFlag::EMPTY, 9, false, true  }, // &=
	{ AstTag::OpSetBitOr,  AstFlag::EMPTY, 9, false, true  }, // |=
	{ AstTag::OpSetBitXor, AstFlag::EMPTY, 9, false, true  }, // ^=
	{ AstTag::OpSetShiftL, AstFlag::EMPTY, 9, false, true  }, // <<=
	{ AstTag::OpSetShiftR, AstFlag::EMPTY, 9, false, true  }, // >>=
};





static constexpr const u8 INVALID_HEX_CHAR_VALUE = 255;

static bool is_whitespace(char8 c) noexcept
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static bool is_alphabetic_char(char8 c) noexcept
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool is_numeric_char(char8 c) noexcept
{
	return c >= '0' && c <= '9';
}

static bool is_identifier_continuation_char(char8 c) noexcept
{
	return is_alphabetic_char(c) || is_numeric_char(c) || c == '_';
}

static u8 hex_char_value(char8 c) noexcept
{
	if (c >= 'a' && c <= 'f')
		return 10 + c - 'a';
	else if (c >= 'A' && c <= 'F')
		return 10 + c - 'A';
	else if (c >= '0' && c <= '9')
		return c - '0';
	else
		return INVALID_HEX_CHAR_VALUE;
}



static void skip_block_comment(Lexer* lexer) noexcept
{
	const char8* curr = lexer->curr;

	curr += 2;

	u32 comment_nesting = 1;

	while (comment_nesting != 0)
	{
		const char8 c = *curr;

		if (c == '/')
		{
			if (curr[1] == '*')
			{
				curr += 2;

				comment_nesting += 1;
			}
			else
			{
				curr += 1;
			}
		}
		else if (c == '*')
		{
			if (curr[1] == '/')
			{
				curr += 2;

				comment_nesting -= 1;
			}
			else
			{
				curr += 1;
			}
		}
		else if (c == '\0')
		{
			source_error(lexer->errors, SourceId{ lexer->source_id_base + static_cast<u32>(curr - lexer->curr) }, "'/*' without matching '*/'\n");
		}
		else
		{
			curr += 1;
		}
	}

	lexer->curr = curr;
}

static void skip_whitespace(Lexer* lexer) noexcept
{
	const char8* curr = lexer->curr;

	while (true)
	{
		while (is_whitespace(*curr))
			curr += 1;
		
		if (*curr == '/')
		{
			if (curr[1] == '/')
			{
				curr += 2;

				while (*curr != '\n' && *curr != '\0')
					curr += 1;
			}
			else if (curr[1] == '*')
			{
				lexer->curr = curr;

				skip_block_comment(lexer);

				curr = lexer->curr;
			}
			else
			{
				break;
			}
		}
		else
		{
			break;
		}
	}

	lexer->curr = curr;
}

static RawLexeme scan_identifier_token(Lexer* lexer) noexcept
{
	const char8* curr = lexer->curr;

	const char8* const token_begin = curr - 1;

	while (is_identifier_continuation_char(*curr))
		curr += 1;

	lexer->curr = curr;

	const Range<char8> identifier_bytes{ token_begin, curr };

	const IdentifierId identifier_id = id_from_identifier(lexer->identifiers, identifier_bytes);

	const IdentifierEntry* const identifier_value = identifier_entry_from_id(lexer->identifiers, identifier_id);

	const Token identifier_token = identifier_value->token();

	return { identifier_token, identifier_token == Token::Ident ? identifier_id.rep : 0 };
}

static RawLexeme scan_builtin_token(Lexer* lexer) noexcept
{
	const char8* curr = lexer->curr;

	const char8* const token_begin = curr;

	while (is_identifier_continuation_char(*curr))
		curr += 1;

	lexer->curr = curr;

	if (curr - token_begin > 8)
		source_error(lexer->errors, lexer->peek.source_id, "Unknown builtin\n");

	u8 name[8]{};

	memcpy(name, token_begin, curr - token_begin);

	for (u64 i = 0; i != array_count(BUILTIN_NAMES); ++i)
	{
		if (memcmp(name, BUILTIN_NAMES[i], 8) == 0)
			return RawLexeme{ Token::Builtin, i };
	}

	source_error(lexer->errors, lexer->peek.source_id, "Unknown builtin\n");
}

static RawLexeme scan_number_token_with_base(Lexer* lexer, char8 base) noexcept
{
	const char8* curr = lexer->curr;

	const char8* const token_begin = curr;

	curr += 1;

	u64 value = 0;

	if (base == 'b')
	{
		while (*curr == '0' || *curr == '1')
		{
			const u64 new_value = value * 2 + *curr - '0';

			if (new_value < value)
				source_error(lexer->errors, lexer->peek.source_id, "Binary integer literal exceeds maximum currently supported value of 2^64-1\n");

			value = new_value;

			curr += 1;
		}
	}
	else if (base == 'o')
	{
		while (*curr >= '0' && *curr <= '7')
		{
			const u64 new_value = value * 8 + *curr - '0';

			if (new_value < value)
				source_error(lexer->errors, lexer->peek.source_id, "Octal integer literal exceeds maximum currently supported value of 2^64-1\n");

			value = new_value;

			curr += 1;
		}
	}
	else
	{
		ASSERT_OR_IGNORE(base == 'x');
		
		while (true)
		{
			const u8 digit_value = hex_char_value(*curr);

			if (digit_value == INVALID_HEX_CHAR_VALUE)
				break;

			const u64 new_value = value * 16 + digit_value;

			if (new_value < value)
				source_error(lexer->errors, lexer->peek.source_id, "Hexadecimal integer literal exceeds maximum currently supported value of 2^64-1\n");

			value = new_value;

			curr += 1;
		}
	}

	if (curr == token_begin + 1)
		source_error(lexer->errors, lexer->peek.source_id, "Expected at least one digit in integer literal\n");

	if (is_identifier_continuation_char(*curr))
		source_error(lexer->errors, lexer->peek.source_id, "Unexpected character '%c' after binary literal\n", *curr);

	lexer->curr = curr;

	return { Token::LitInteger, value };
}

static u32 scan_utf8_char_surrogates(Lexer* lexer, u32 leader_value, u32 surrogate_count) noexcept
{
	const char8* curr = lexer->curr;

	u32 codepoint = leader_value;

	for (u32 i = 0; i != surrogate_count; ++i)
	{
		const char8 surrogate = curr[i + 1];

		if ((surrogate & 0xC0) != 0x80)
			source_error(lexer->errors, lexer->peek.source_id, "Expected utf-8 surrogate code unit (0b10xx'xxxx) but got 0x%X\n", surrogate);

		codepoint |= (surrogate & 0x3F) << (6 * (surrogate_count - i - 1));
	}

	lexer->curr += surrogate_count + 1;

	return codepoint;
}

static u32 scan_utf8_char(Lexer* lexer) noexcept
{
	const char8 first = *lexer->curr;

	if ((first & 0x80) == 0)
	{
		lexer->curr += 1;

		return first;
	}
	else if ((first & 0xE0) == 0xC0)
	{
		return scan_utf8_char_surrogates(lexer, (first & 0x1F) << 6, 1);
	}
	else if ((first & 0xF0) == 0xE0)
	{
		return scan_utf8_char_surrogates(lexer, (first & 0x0F) << 12, 2);

	}
	else if ((first & 0xF8) == 0xF0)
	{
		return scan_utf8_char_surrogates(lexer, (first & 0x07) << 18, 3);
	}
	else
	{
		source_error(lexer->errors, lexer->peek.source_id, "Unexpected code unit 0x%X at start of character literal. This might be an encoding issue regarding the source file, as only utf-8 is supported.\n", first);
	}
}

static u32 scan_escape_char(Lexer* lexer) noexcept
{
	const char8* curr = lexer->curr;

	u32 codepoint = 0;

	const char8 escapee = curr[1];

	switch (escapee)
	{
	case 'x':
	{
		const u8 hi = hex_char_value(curr[2]);

		if (hi == INVALID_HEX_CHAR_VALUE)
			source_error(lexer->errors, lexer->peek.source_id, "Expected two hexadecimal digits after character literal escape '\\x' but got '%c' instead of first digit\n", curr[2]);

		const u8 lo = hex_char_value(curr[3]);
		
		if (lo == INVALID_HEX_CHAR_VALUE)
			source_error(lexer->errors, lexer->peek.source_id, "Expected two hexadecimal digits after character literal escape '\\x' but got '%c' instead of second digit\n", curr[3]);

		curr += 2;

		codepoint = lo + hi * 16;

		break;
	}
		
	case 'X':
	{
		codepoint = 0;

		for (u32 i = 0; i != 6; ++i)
		{
			const u8 char_value = hex_char_value(curr[i + 2]);

			if (char_value == INVALID_HEX_CHAR_VALUE)
				source_error(lexer->errors, lexer->peek.source_id, "Expected six hexadecimal digits after character literal escape '\\X' but got '%c' instead of digit %u\n", curr[i + 2], i + 1);

			codepoint = codepoint * 16 + char_value;
		}

		if (codepoint > 0x10FFFF)
			source_error(lexer->errors, lexer->peek.source_id, "Codepoint 0x%X indicated in character literal escape '\\X' is greater than the maximum unicode codepoint U+10FFFF", codepoint);

		curr += 6;

		break;
	}

	case 'u':
	{
		for (u32 i = 0; i != 4; ++i)
		{
			const char8 c = curr[i + 2];

			if (c < '0' || c > '9')
				source_error(lexer->errors, lexer->peek.source_id, "Expected four decimal digits after character literal escape '\\X' but got '%c' instead of digit %u\n", curr[i + 2], i + 1);

			codepoint = codepoint * 10 + c - '0';
		}

		curr += 4;

		break;
	}

	case '\\':
	case '\'':
	case '"':
		codepoint = escapee;
		break;

	case '0':
		codepoint = '\0';
		break;

	case 'a':
		codepoint = '\a';
		break;

	case 'b':
		codepoint = '\b';
		break;

	case 'f':
		codepoint = '\f';
		break;

	case 'n':
		codepoint = '\n';
		break;

	case 'r':
		codepoint = '\r';
		break;

	case 't':
		codepoint = '\t';
		break;

	case 'v':
		codepoint = '\v';
		break;

	default:
		source_error(lexer->errors, lexer->peek.source_id, "Unknown character literal escape '%c'\n");
	}

	lexer->curr = curr + 2;

	return codepoint;
}

static RawLexeme scan_number_token(Lexer* lexer, char8 first) noexcept
{
	const char8* curr = lexer->curr;

	const char8* const token_begin = curr - 1;

	u64 integer_value = first - '0';

	bool max_exceeded = false;

	while (is_numeric_char(*curr))
	{
		const u64 new_value = integer_value * 10 + *curr - '0';

		if (new_value < integer_value)
			max_exceeded = true;

		integer_value = new_value;

		curr += 1;
	}

	if (*curr == '.')
	{
		curr += 1;

		if (!is_numeric_char(*curr))
			source_error(lexer->errors, lexer->peek.source_id, "Expected at least one digit after decimal point in float literal\n");

		while (is_numeric_char(*curr))
			curr += 1;

		if (*curr == 'e')
		{
			curr += 1;

			if (*curr == '+' || *curr == '-')
				curr += 1;

			while (is_numeric_char(*curr))
				curr += 1;
		}
		
		if (is_alphabetic_char(*curr) || *curr == '_')
			source_error(lexer->errors, lexer->peek.source_id, "Unexpected character '%c' after float literal\n", *curr);

		char8* strtod_end;

		errno = 0;

		const f64 float_value = strtod(token_begin, &strtod_end);

		if (strtod_end != curr)
			source_error(lexer->errors, lexer->peek.source_id, "strtod disagrees with internal parsing about end of float literal\n");

		if (errno == ERANGE)
			source_error(lexer->errors, lexer->peek.source_id, "Float literal exceeds maximum IEEE-754 value\n");

		lexer->curr = curr;

		return { Token::LitFloat, float_value };
	}
	else
	{
		if (max_exceeded)
			source_error(lexer->errors, lexer->peek.source_id, "Integer literal exceeds maximum currently supported value of 2^64-1\n");

		if (is_alphabetic_char(*curr) || *curr == '_')
			source_error(lexer->errors, lexer->peek.source_id, "Unexpected character '%c' after integer literal\n", *curr);

		lexer->curr = curr;

		return { Token::LitInteger, integer_value };
	}

}

static RawLexeme scan_char_token(Lexer* lexer) noexcept
{
	u32 codepoint;

	if (*lexer->curr == '\\')
		codepoint = scan_escape_char(lexer);
	else
		codepoint = scan_utf8_char(lexer);
	
	if (*lexer->curr != '\'')
		source_error(lexer->errors, lexer->peek.source_id, "Expected end of character literal (') but got %c\n", *lexer->curr);

	lexer->curr += 1;

	return { Token::LitChar, codepoint };
}

static RawLexeme scan_string_token(Lexer* lexer) noexcept
{
	char8 buffer[MAX_STRING_LITERAL_BYTES];

	u32 buffer_index = 0;

	const char8* curr = lexer->curr;

	const char8* copy_begin = curr;

	while (*curr != '"')
	{
		if (*curr == '\\')
		{
			const u32 bytes_to_copy = static_cast<u32>(curr - copy_begin);

			if (buffer_index + bytes_to_copy > sizeof(buffer))
					source_error(lexer->errors, lexer->peek.source_id, "String constant is longer than the supported maximum of %u bytes\n", MAX_STRING_LITERAL_BYTES);

			memcpy(buffer + buffer_index, copy_begin, bytes_to_copy);

			buffer_index += bytes_to_copy;

			lexer->curr = curr;

			const u32 codepoint = scan_escape_char(lexer);

			curr = lexer->curr;

			if (codepoint <= 0x7F)
			{
				if (buffer_index + 1 > sizeof(buffer))
					source_error(lexer->errors, lexer->peek.source_id, "String constant is longer than the supported maximum of %u bytes\n", MAX_STRING_LITERAL_BYTES);
			
				buffer[buffer_index] = static_cast<char8>(codepoint);

				buffer_index += 1;
			}
			else if (codepoint <= 0x7FF)
			{
				if (buffer_index + 2 > sizeof(buffer))
					source_error(lexer->errors, lexer->peek.source_id, "String constant is longer than the supported maximum of %u bytes\n", MAX_STRING_LITERAL_BYTES);

				buffer[buffer_index] = static_cast<char8>((codepoint >> 6) | 0xC0);

				buffer[buffer_index + 1] = static_cast<char8>((codepoint & 0x3F) | 0x80);

				buffer_index += 2;
			}
			else if (codepoint == 0x10000)
			{
				if (buffer_index + 3 > sizeof(buffer))
					source_error(lexer->errors, lexer->peek.source_id, "String constant is longer than the supported maximum of %u bytes\n", MAX_STRING_LITERAL_BYTES);

				buffer[buffer_index] = static_cast<char8>((codepoint >> 12) | 0xE0);

				buffer[buffer_index + 1] = static_cast<char8>(((codepoint >> 6) & 0x3F) | 0x80);

				buffer[buffer_index + 2] = static_cast<char8>((codepoint & 0x3F) | 0x80);

				buffer_index += 3;
			}
			else
			{
				ASSERT_OR_IGNORE(codepoint <= 0x10FFFF);

				if (buffer_index + 4 > sizeof(buffer))
					source_error(lexer->errors, lexer->peek.source_id, "String constant is longer than the supported maximum of %u bytes\n", MAX_STRING_LITERAL_BYTES);

				buffer[buffer_index] = static_cast<char8>((codepoint >> 18) | 0xE0);

				buffer[buffer_index + 1] = static_cast<char8>(((codepoint >> 12) & 0x3F) | 0x80);

				buffer[buffer_index + 2] = static_cast<char8>(((codepoint >> 6) & 0x3F) | 0x80);

				buffer[buffer_index + 3] = static_cast<char8>((codepoint & 0x3F) | 0x80);

				buffer_index += 4;
			}

			copy_begin = buffer + buffer_index;
		}
		else if (*curr == '\n')
		{
			source_error(lexer->errors, lexer->peek.source_id, "String constant spans across newline\n");
		}
		else
		{
			curr += 1;
		}
	}

	const u32 bytes_to_copy = static_cast<u32>(curr - copy_begin);

	if (buffer_index + bytes_to_copy > sizeof(buffer))
			source_error(lexer->errors, lexer->peek.source_id, "String constant is longer than the supported maximum of %u bytes\n", MAX_STRING_LITERAL_BYTES);

	memcpy(buffer + buffer_index, copy_begin, bytes_to_copy);

	buffer_index += bytes_to_copy;

	const Range<char8> string_bytes{ buffer, buffer_index };

	const IdentifierId string_index = id_from_identifier(lexer->identifiers, string_bytes);

	lexer->curr = curr + 1;

	return { Token::LitString, string_index.rep };
}

static RawLexeme raw_next(Lexer* lexer) noexcept
{
	const char8 first = *lexer->curr;

	lexer->curr += 1;

	const char8 second = first == '\0' ? '\0' : *lexer->curr;

	switch (first)
	{
	case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g': case 'h':
	case 'i': case 'j': case 'k': case 'l': case 'm': case 'n': case 'o': case 'p':
	case 'q': case 'r': case 's': case 't': case 'u': case 'v': case 'w': case 'x':
	case 'y': case 'z':
	case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G': case 'H':
	case 'I': case 'J': case 'K': case 'L': case 'M': case 'N': case 'O': case 'P':
	case 'Q': case 'R': case 'S': case 'T': case 'U': case 'V': case 'W': case 'X':
	case 'Y': case 'Z':
		return scan_identifier_token(lexer);
	
	case '0':
		if (second == 'b' || second == 'o' || second == 'x')
			return scan_number_token_with_base(lexer, second);

	// fallthrough

	case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8':
	case '9':
		return scan_number_token(lexer, first);

	case '\'':
		return scan_char_token(lexer);

	case '"':
		return scan_string_token(lexer);

	case '_':
		if (is_identifier_continuation_char(second))
		{
			if (!lexer->is_std)
				source_error(lexer->errors, lexer->peek.source_id, "Illegal identifier starting with '_'\n");

			return scan_builtin_token(lexer);
		}
		else
		{
			return { Token::Wildcard };
		}


	case '+':
		if (second == '=')
		{
			lexer->curr += 1;

			return { Token::OpSetAdd };
		}
		else if (second == ':')
		{
			if (lexer->curr[1] == '=')
			{
				lexer->curr += 2;

				return { Token::OpSetAddTC };
			}
			else
			{
				lexer->curr += 1;

				return { Token::OpAddTC };
			}
		}
		else
		{
			return { Token::OpAdd };
		}

	case '-':
		if (second == '>')
		{
			lexer->curr += 1;

			return { Token::ThinArrowR };
		}
		else if (second == ':')
		{
			if (lexer->curr[1] == '=')
			{
				lexer->curr += 2;

				return { Token::OpSetSubTC };
			}
			else
			{
				lexer->curr += 1;

				return { Token::OpSubTC };
			}
		}
		else if (second == '=')
		{
			lexer->curr += 1;

			return { Token::OpSetSub };
		}
		else
		{
			return { Token::OpSub };
		}

	case '*':
		if (second == '=')
		{
			lexer->curr += 1;

			return { Token::OpSetMul };
		}
		else if (second == ':')
		{
			if (lexer->curr[1] == '=')
			{
				lexer->curr += 2;

				return { Token::OpSetMulTC };
			}
			else
			{
				lexer->curr += 1;

				return { Token::OpMulTC };
			}
		}
		else if (second == '/')
		{
			source_error(lexer->errors, lexer->peek.source_id, "'*/' without previous matching '/*'\n");
		}
		else
		{
			return { Token::OpMulOrTypPtr };
		}

	case '/':
		if (second == '=')
		{
			lexer->curr += 1;

			return { Token::OpSetDiv };
		}
		else
		{
			return { Token::OpDiv };
		}

	case '%':
		if (second == '=')
		{
			lexer->curr += 1;

			return { Token::OpSetMod };
		}
		else
		{
			return { Token::OpMod };
		}

	case '&':
		if (second == '&')
		{
			lexer->curr += 1;

			return { Token::OpLogAnd };
		}
		else if (second == '=')
		{
			lexer->curr += 1;

			return { Token::OpSetAnd };
		}
		else
		{
			return { Token::OpAnd };
		}

	case '|':
		if (second == '|')
		{
			lexer->curr += 1;

			return { Token::OpLogAnd };
		}
		else if (second == '=')
		{
			lexer->curr += 1;

			return { Token::OpSetOr };
		}
		else
		{
			return { Token::OpOr };
		}

	case '^':
		if (second == '=')
		{
			lexer->curr += 1;

			return { Token::OpSetXor };
		}
		else
		{
			return { Token::OpXor };
		}

	case '<':
		if (second == '<')
		{
			if (lexer->curr[1] == '=')
			{
				lexer->curr += 2;

				return { Token::OpSetShl };
			}
			else
			{
				lexer->curr += 1;

				return { Token::OpShl };
			}
		}
		else if (second == '=')
		{
			lexer->curr += 1;

			return { Token::OpLe };
		}
		else if (second == '-')
		{
			lexer->curr += 1;

			return { Token::ThinArrowL };
		}
		else
		{
			return { Token::OpLt };
		}

	case '>':
		if (second == '>')
		{
			if (lexer->curr[1] == '=')
			{
				lexer->curr += 2;

				return { Token::OpSetShr };
			}
			else
			{
				lexer->curr += 1;

				return { Token::OpShr };
			}
		}
		else if (second == '=')
		{
			lexer->curr += 1;

			return { Token::OpGe };
		}
		else
		{
			return { Token::OpGt };
		}

	case '.':
		if (second == '.')
		{
			if (lexer->curr[1] != '.')
				source_error(lexer->errors, lexer->peek.source_id, "Unexpected Token '..'\n");

			lexer->curr += 2;

			return { Token::TypVar };
		}
		else if (second == '*')
		{
			lexer->curr += 1;

			return { Token::UOpDeref };
		}
		else if (second == '[')
		{
			lexer->curr += 1;

			return { Token::ArrayInitializer };
		}
		else if (second == '{')
		{
			lexer->curr += 1;

			return { Token::CompositeInitializer };
		}
		else
		{
			return { Token::OpMemberOrRef };
		}

	case '!':
		if (second == '=')
		{
			lexer->curr += 1;

			return { Token::OpNe };
		}
		else
		{
			return { Token::UOpLogNot };
		}

	case '=':
		if (second == '=')
		{
			lexer->curr += 1;

			return { Token::OpEq };
		}
		else if (second == '>')
		{
			lexer->curr += 1;

			return { Token::WideArrowR };
		}
		else
		{
			return { Token::OpSet };
		}

	case '$':
		return { Token::UOpAddr };

	case '~':
		return { Token::UOpNot };

	case '?':
		return { Token::TypOptPtr };

	case ':':
		return { Token::Colon };

	case ',':
		return { Token::Comma };

	case '#':
		return { Token::Pragma };

	case '[':
		if (second == '.' && lexer->curr[1] == '.' && lexer->curr[2] == '.' && lexer->curr[3] == ']')
		{
			lexer->curr += 4;

			return { Token::TypTailArray };
		}
		else if (second == '*' && lexer->curr[1] == ']')
		{
			lexer->curr += 2;

			return { Token::TypMultiPtr };
		}
		else if (second == '?' && lexer->curr[1] == ']')
		{
			lexer->curr += 2;

			return { Token::TypOptMultiPtr };
		}
		else if (second == ']')
		{
			lexer->curr += 1;

			return { Token::TypSlice };
		}
		else
		{
			return { Token::BracketL };
		}

	case ']':
		return { Token::BracketR };

	case '{':
		return { Token::CurlyL };

	case '}':
		return { Token::CurlyR };

	case '(':
		return { Token::ParenL };

	case ')':
		return { Token::ParenR };

	case '\0':
		lexer->curr -= 1;

		if (lexer->curr != lexer->end)
			source_error(lexer->errors, lexer->peek.source_id, "Null character in source file\n");

		return { Token::END_OF_SOURCE };				

	default:
		source_error(lexer->errors, lexer->peek.source_id, "Unexpected character '%c' in source file\n", first);
	}
}

static Lexeme next(Lexer* lexer) noexcept
{
	if (lexer->peek.token != Token::EMPTY)
	{
		const Lexeme rst = lexer->peek;

		lexer->peek.token = Token::EMPTY;

		return rst;
	}

	skip_whitespace(lexer);

	lexer->peek.source_id = SourceId{ lexer->source_id_base + static_cast<u32>(lexer->curr - lexer->begin) };

	const RawLexeme raw = raw_next(lexer);

	return { raw.token, lexer->peek.source_id, raw.integer_value };
}

static Lexeme peek(Lexer* lexer) noexcept
{
	if (lexer->peek.token == Token::EMPTY)
		lexer->peek = next(lexer);

	return lexer->peek;
}

static Lexeme peek_n(Lexer* lexer, u32 n) noexcept
{
	ASSERT_OR_IGNORE(n != 0);

	const Lexeme remembered_peek = peek(lexer);

	const char8* const remembered_curr = lexer->curr;

	lexer->peek.token = Token::EMPTY;

	Lexeme result = remembered_peek;

	for (u32 i = 0; i != n; ++i)
		result = next(lexer);

	lexer->curr = remembered_curr;

	lexer->peek = remembered_peek;

	return result;
}

static void skip(Lexer* lexer) noexcept
{
	(void) next(lexer);
}



static void pop_operator(Parser* parser, OperatorStack* stack) noexcept
{
	ASSERT_OR_IGNORE(stack->operator_top != 0);

	const OperatorDescWithSource top = stack->operators[stack->operator_top - 1];

	stack->operator_top -= 1;

	if (top.operator_desc.node_type == AstTag::INVALID)
		return;

	if (stack->operand_count <= top.operator_desc.is_binary)
		source_error(parser->lexer.errors, stack->expression_source_id, "Missing operand(s) for operator '%s'\n", ast_tag_name(top.operator_desc.node_type));

	if (top.operator_desc.is_binary)
		stack->operand_count -= 1;

	const AstBuilderToken operator_token = push_node(&parser->builder, stack->operand_tokens[stack->operand_count - 1], top.source_id, top.operator_desc.node_flags, top.operator_desc.node_type);

	stack->operand_tokens[stack->operand_count - 1] = operator_token;
}

static bool pop_to_precedence(Parser* parser, OperatorStack* stack, u8 precedence, bool pop_equal) noexcept
{
	while (stack->operator_top != 0)
	{
		const OperatorDescWithSource top = stack->operators[stack->operator_top - 1];

		if (top.operator_desc.precedence > precedence || (top.operator_desc.precedence == precedence && !pop_equal))
			return true;

		pop_operator(parser, stack);
	}

	return false;
}

static void push_operand(Parser* parser, OperatorStack* stack, AstBuilderToken operand_token) noexcept
{
	if (stack->operand_count == array_count(stack->operand_tokens) - 1)
		source_error(parser->lexer.errors, stack->expression_source_id, "Expression exceeds maximum open operands of %u\n");

	stack->operand_tokens[stack->operand_count] = operand_token;

	stack->operand_count += 1;
}

static void push_operator(Parser* parser, OperatorStack* stack, OperatorDescWithSource op) noexcept
{
	if (op.operator_desc.node_type != AstTag::INVALID)
		pop_to_precedence(parser, stack, op.operator_desc.precedence, op.operator_desc.is_right_to_left);

	if (stack->operator_top == array_count(stack->operators))
		source_error(parser->lexer.errors, stack->expression_source_id, "Expression exceeds maximum depth of %u\n", array_count(stack->operators));

	stack->operators[stack->operator_top] = op;

	stack->operator_top += 1;
}

static void remove_lparen(OperatorStack* stack) noexcept
{
	ASSERT_OR_IGNORE(stack->operator_top != 0 && stack->operators[stack->operator_top - 1].operator_desc.node_type == AstTag::INVALID);

	stack->operator_top -= 1;
}

static AstBuilderToken pop_remaining(Parser* parser, OperatorStack* stack) noexcept
{
	while (stack->operator_top != 0)
		pop_operator(parser, stack);

	if (stack->operand_count != 1)
		source_error(parser->lexer.errors, stack->expression_source_id, "Mismatched operand / operator count (%u operands remaining)", stack->operand_count);

	return stack->operand_tokens[0];
}





static bool is_definition_start(Token token) noexcept
{
	return token == Token::KwdLet
		|| token == Token::KwdPub
		|| token == Token::KwdMut
		|| token == Token::KwdGlobal
		|| token == Token::KwdAuto
		|| token == Token::KwdUse;
}

static AstBuilderToken parse_expr(Parser* parser, bool allow_complex) noexcept;

static AstBuilderToken parse_definition(Parser* parser, bool is_implicit, bool is_optional_value) noexcept
{
	AstFlag flags = AstFlag::EMPTY;

	Lexeme lexeme = next(&parser->lexer);

	const SourceId source_id = lexeme.source_id;

	if (lexeme.token == Token::KwdLet)
	{
		lexeme = next(&parser->lexer);
	}
	else
	{
		while (true)
		{
			if (lexeme.token == Token::KwdPub)
			{
				if ((flags & AstFlag::Definition_IsPub) != AstFlag::EMPTY)
					source_error(parser->lexer.errors, lexeme.source_id, "Definition modifier 'pub' encountered more than once\n");

				flags |= AstFlag::Definition_IsPub;
			}
			else if (lexeme.token == Token::KwdMut)
			{
				if ((flags & AstFlag::Definition_IsMut) != AstFlag::EMPTY)
					source_error(parser->lexer.errors, lexeme.source_id, "Definition modifier 'mut' encountered more than once\n");

				flags |= AstFlag::Definition_IsMut;
			}
			else if (lexeme.token == Token::KwdGlobal)
			{
				if ((flags & AstFlag::Definition_IsGlobal) != AstFlag::EMPTY)
					source_error(parser->lexer.errors, lexeme.source_id, "Definition modifier 'global' encountered more than once\n");

				flags |= AstFlag::Definition_IsGlobal;
			}
			else if (lexeme.token == Token::KwdAuto)
			{
				if ((flags & AstFlag::Definition_IsAuto) != AstFlag::EMPTY)
					source_error(parser->lexer.errors, lexeme.source_id, "Definition modifier 'auto' encountered more than once\n");

				flags |= AstFlag::Definition_IsAuto;
			}
			else if (lexeme.token == Token::KwdUse)
			{
				if ((flags & AstFlag::Definition_IsUse) != AstFlag::EMPTY)
					source_error(parser->lexer.errors, lexeme.source_id, "Definition modifier 'use' encountered more than once\n");

				flags |= AstFlag::Definition_IsUse;
			}
			else
			{
				break;
			}

			lexeme = next(&parser->lexer);
		}

		if (flags == AstFlag::EMPTY && !is_implicit)
			source_error(parser->lexer.errors, lexeme.source_id, "Missing 'let' or at least one of 'pub', 'mut' or 'global' at start of definition\n");
	}

	if (lexeme.token != Token::Ident)
		source_error(parser->lexer.errors, lexeme.source_id, "Expected 'Identifier' after Definition modifiers but got '%s'\n", token_name(lexeme.token));

	const IdentifierId identifier_id = lexeme.identifier_id;

	lexeme = peek(&parser->lexer);

	AstBuilderToken first_child_token = AstBuilder::NO_CHILDREN;

	if (lexeme.token == Token::Colon)
	{
		flags |= AstFlag::Definition_HasType;

		skip(&parser->lexer);

		first_child_token = parse_expr(parser, false);

		lexeme = peek(&parser->lexer);
	}

	if (lexeme.token == Token::OpSet)
	{
		skip(&parser->lexer);

		const AstBuilderToken value_token = parse_expr(parser, true);

		if (first_child_token == AstBuilder::NO_CHILDREN)
			first_child_token = value_token;
	}
	else if (!is_optional_value)
	{
		source_error(parser->lexer.errors, lexeme.source_id, "Expected '=' after Definition identifier and type, but got '%s'\n", token_name(lexeme.token));
	}

	return push_node(&parser->builder, first_child_token, source_id, flags, DefinitionData{ identifier_id, INVALID_TYPE_ID, INVALID_VALUE_ID });
}

static AstBuilderToken parse_return(Parser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(&parser->lexer).token == Token::KwdReturn);

	const SourceId source_id = next(&parser->lexer).source_id;

	const AstBuilderToken value_token = parse_expr(parser, true);

	return push_node(&parser->builder, value_token, source_id, AstFlag::EMPTY, AstTag::Return);
}

static AstBuilderToken parse_leave(Parser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(&parser->lexer).token == Token::KwdLeave);

	const SourceId source_id = next(&parser->lexer).source_id;

	return push_node(&parser->builder, AstBuilder::NO_CHILDREN, source_id, AstFlag::EMPTY, AstTag::Leave);
}

static AstBuilderToken parse_yield(Parser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(&parser->lexer).token == Token::KwdYield);

	const SourceId source_id = next(&parser->lexer).source_id;

	const AstBuilderToken value_token = parse_expr(parser, true);

	return push_node(&parser->builder, value_token, source_id, AstFlag::EMPTY, AstTag::Yield);
}

static AstBuilderToken parse_top_level_expr(Parser* parser, bool is_definition_optional_value, bool* out_is_definition) noexcept
{
	const Lexeme lexeme = peek(&parser->lexer);

	bool is_definition = is_definition_start(lexeme.token);

	*out_is_definition = is_definition;

	if (is_definition)
		return parse_definition(parser, false, is_definition_optional_value);
	else if (lexeme.token == Token::KwdReturn)
		return parse_return(parser);
	else if (lexeme.token == Token::KwdLeave)
		return parse_leave(parser);
	else if (lexeme.token == Token::KwdYield)
		return parse_yield(parser);
	else
		return parse_expr(parser, true);
}

static AstBuilderToken parse_where(Parser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(&parser->lexer).token == Token::KwdWhere);

	const SourceId source_id = next(&parser->lexer).source_id;

	const AstBuilderToken first_child_token = parse_definition(parser, true, false);

	while (true)
	{
		if (peek(&parser->lexer).token != Token::Comma)
			break;

		skip(&parser->lexer);

		parse_definition(parser, true, false);
	}

	return push_node(&parser->builder, first_child_token, source_id, AstFlag::EMPTY, AstTag::Where);
}

static AstBuilderToken parse_if(Parser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(&parser->lexer).token == Token::KwdIf);

	AstFlag flags = AstFlag::EMPTY;

	const SourceId source_id = next(&parser->lexer).source_id;

	const AstBuilderToken condition_token = parse_expr(parser, false);

	Lexeme lexeme = peek(&parser->lexer);

	if (lexeme.token == Token::KwdWhere)
	{
		flags |= AstFlag::If_HasWhere;

		parse_where(parser);

		lexeme = peek(&parser->lexer);
	}

	if (lexeme.token == Token::KwdThen)
		skip(&parser->lexer);

	parse_expr(parser, true);

	lexeme = peek(&parser->lexer);

	if (lexeme.token == Token::KwdElse)
	{
		flags |= AstFlag::If_HasElse;

		skip(&parser->lexer);

		parse_expr(parser, true);
	}

	return push_node(&parser->builder, condition_token, source_id, flags, AstTag::If);
}

static AstBuilderToken try_parse_foreach(Parser* parser, SourceId source_id) noexcept
{
	bool is_foreach = false;

	if (is_definition_start(peek(&parser->lexer).token))
	{
		is_foreach = true;
	}
	else if (const Lexeme lookahead_1 = peek_n(&parser->lexer, 1); lookahead_1.token == Token::ThinArrowL)
	{
		is_foreach = true;
	}
	else if (lookahead_1.token == Token::Comma)
	{
		if (const Lexeme lookahead_2 = peek_n(&parser->lexer, 2); is_definition_start(lookahead_2.token))
			is_foreach = true;
		if (const Lexeme lookahead_3 = peek_n(&parser->lexer, 3); lookahead_3.token == Token::ThinArrowL)
			is_foreach = true;
	}

	if (!is_foreach)
		return AstBuilder::NO_CHILDREN;

	AstFlag flags = AstFlag::EMPTY;

	const AstBuilderToken first_child_token = parse_definition(parser, true, true);

	Lexeme lexeme = peek(&parser->lexer);

	if (lexeme.token == Token::Comma)
	{
		flags |= AstFlag::ForEach_HasIndex;

		skip(&parser->lexer);

		parse_definition(parser, true, true);

		lexeme = peek(&parser->lexer);
	}

	if (lexeme.token != Token::ThinArrowL)
		source_error(parser->lexer.errors, lexeme.source_id, "Expected '%s' after for-each loop variables but got '%s'\n", token_name(Token::ThinArrowL), token_name(lexeme.token));

	skip(&parser->lexer);

	parse_expr(parser, false);

	lexeme = peek(&parser->lexer);

	if (lexeme.token == Token::KwdWhere)
	{
		flags |= AstFlag::ForEach_HasWhere;

		parse_where(parser);

		lexeme = peek(&parser->lexer);
	}

	if (lexeme.token == Token::KwdDo)
		skip(&parser->lexer);

	parse_expr(parser, true);

	lexeme = peek(&parser->lexer);

	if (lexeme.token == Token::KwdFinally)
	{
		flags |= AstFlag::ForEach_HasFinally;

		parse_expr(parser, true);
	}

	return push_node(&parser->builder, first_child_token, source_id, flags, AstTag::ForEach);
}

static AstBuilderToken parse_for(Parser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(&parser->lexer).token == Token::KwdFor);

	AstFlag flags = AstFlag::EMPTY;

	const SourceId source_id = next(&parser->lexer).source_id;

	if (const AstBuilderToken foreach_token = try_parse_foreach(parser, source_id); foreach_token != AstBuilder::NO_CHILDREN)
		return foreach_token;

	const AstBuilderToken first_child_token = parse_expr(parser, false);

	Lexeme lexeme = peek(&parser->lexer);

	if (lexeme.token == Token::Comma)
	{
		flags |= AstFlag::For_HasStep;

		skip(&parser->lexer);

		parse_expr(parser, true);

		lexeme = peek(&parser->lexer);
	}

	if (lexeme.token == Token::KwdWhere)
	{
		flags |= AstFlag::For_HasWhere;

		parse_where(parser);

		lexeme = peek(&parser->lexer);
	}

	if (lexeme.token == Token::KwdDo)
		skip(&parser->lexer);

	parse_expr(parser, true);

	lexeme = peek(&parser->lexer);

	if (lexeme.token == Token::KwdFinally)
	{
		flags |= AstFlag::For_HasFinally;

		parse_expr(parser, true);
	}

	return push_node(&parser->builder, first_child_token, source_id, flags, AstTag::For);
}

static AstBuilderToken parse_case(Parser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(&parser->lexer).token == Token::KwdCase);

	const SourceId source_id = next(&parser->lexer).source_id;

	const AstBuilderToken first_child_token = parse_expr(parser, false);

	Lexeme lexeme = next(&parser->lexer);

	if (lexeme.token != Token::ThinArrowR)
		source_error(parser->lexer.errors, lexeme.source_id, "Expected '%s' after case label expression but got '%s'\n", token_name(Token::ThinArrowR), token_name(lexeme.token));

	parse_expr(parser, true);

	return push_node(&parser->builder, first_child_token, source_id, AstFlag::EMPTY, AstTag::Case);
}

static AstBuilderToken parse_switch(Parser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(&parser->lexer).token == Token::KwdSwitch);

	AstFlag flags = AstFlag::EMPTY;

	const SourceId source_id = next(&parser->lexer).source_id;

	const AstBuilderToken first_child_token = parse_expr(parser, false);

	Lexeme lexeme = peek(&parser->lexer);

	if (lexeme.token == Token::KwdWhere)
	{
		flags |= AstFlag::Switch_HasWhere;

		parse_where(parser);

		lexeme = peek(&parser->lexer);
	}

	if (lexeme.token != Token::KwdCase)
		source_error(parser->lexer.errors, lexeme.source_id, "Expected at least one '%s' after switch expression but got '%s'\n", token_name(Token::KwdCase), token_name(lexeme.token));

	while (true)
	{
		parse_case(parser);

		lexeme = peek(&parser->lexer);

		if (lexeme.token != Token::KwdCase)
			break;
	}

	return push_node(&parser->builder, first_child_token, source_id, flags, AstTag::Switch);
}

static AstBuilderToken parse_expects(Parser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(&parser->lexer).token == Token::KwdExpects);

	const SourceId source_id = next(&parser->lexer).source_id;

	const AstBuilderToken first_child_token = parse_expr(parser, false);

	while (true)
	{
		if (peek(&parser->lexer).token != Token::Comma)
			break;

		skip(&parser->lexer);

		parse_expr(parser, false);
	}

	return push_node(&parser->builder, first_child_token, source_id, AstFlag::EMPTY, AstTag::Expects);
}

static AstBuilderToken parse_ensures(Parser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(&parser->lexer).token == Token::KwdEnsures);

	const SourceId source_id = next(&parser->lexer).source_id;

	const AstBuilderToken first_child_token = parse_expr(parser, false);

	while (true)
	{
		if (peek(&parser->lexer).token != Token::Comma)
			break;

		skip(&parser->lexer);
		
		parse_expr(parser, false);
	}

	return push_node(&parser->builder, first_child_token, source_id, AstFlag::EMPTY, AstTag::Ensures);
}

static AstBuilderToken parse_func(Parser* parser) noexcept
{
	AstFlag flags = AstFlag::EMPTY;

	Lexeme lexeme = next(&parser->lexer);

	const SourceId func_source_id = lexeme.source_id;

	if (lexeme.token == Token::KwdProc)
		flags |= AstFlag::Func_IsProc;
	else if (lexeme.token != Token::KwdFunc)
		source_error(parser->lexer.errors, lexeme.source_id, "Expected '%s' or '%s' but got '%s'\n", token_name(Token::KwdFunc), token_name(Token::KwdProc), token_name(lexeme.token));

	lexeme = next(&parser->lexer);

	const SourceId parameter_list_source_id = lexeme.source_id;

	if (lexeme.token != Token::ParenL)
		source_error(parser->lexer.errors, lexeme.source_id, "Expected '%s' after '%s' but got '%s'\n", token_name(Token::ParenL), token_name(flags == AstFlag::Func_IsProc ? Token::KwdProc : Token::KwdFunc), token_name(lexeme.token));

	lexeme = peek(&parser->lexer);

	AstBuilderToken first_parameter_token = AstBuilder::NO_CHILDREN;

	while (lexeme.token != Token::ParenR)
	{
		const AstBuilderToken parameter_token = parse_definition(parser, true, true);

		if (first_parameter_token == AstBuilder::NO_CHILDREN)
			first_parameter_token = parameter_token;

		lexeme = peek(&parser->lexer);

		if (lexeme.token == Token::Comma)
			skip(&parser->lexer);
		else if (lexeme.token != Token::ParenR)
			source_error(parser->lexer.errors, lexeme.source_id, "Expected '%s' or '%s' after function parameter definition but got '%s'", token_name(Token::Comma), token_name(Token::ParenR), token_name(lexeme.token));
	}

	const AstBuilderToken first_child_token = push_node(&parser->builder, first_parameter_token, parameter_list_source_id, AstFlag::EMPTY, AstTag::ParameterList);

	skip(&parser->lexer);

	lexeme = peek(&parser->lexer);

	if (lexeme.token == Token::ThinArrowR)
	{
		flags |= AstFlag::Func_HasReturnType;

		skip(&parser->lexer);

		// Return type
		parse_expr(parser, false);

		lexeme = peek(&parser->lexer);
	}

	if (lexeme.token == Token::KwdExpects)
	{
		flags |= AstFlag::Func_HasExpects;

		// Expects
		parse_expects(parser);

		lexeme = peek(&parser->lexer);
	}

	if (lexeme.token == Token::KwdEnsures)
	{
		flags |= AstFlag::Func_HasEnsures;

		// Ensures
		parse_ensures(parser);

		lexeme = peek(&parser->lexer);
	}

	if (lexeme.token == Token::OpSet)
	{
		flags |= AstFlag::Func_HasBody;

		skip(&parser->lexer);

		// Body
		parse_expr(parser, true);
	}

	return push_node(&parser->builder, first_child_token, func_source_id, flags, FuncData{ INVALID_TYPE_ID, INVALID_TYPE_ID, INVALID_SCOPE_ID });
}

static AstBuilderToken parse_trait(Parser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(&parser->lexer).token == Token::KwdTrait);

	AstFlag flags = AstFlag::EMPTY;

	const SourceId source_id = next(&parser->lexer).source_id;

	Lexeme lexeme = next(&parser->lexer);

	if (lexeme.token != Token::ParenL)
		source_error(parser->lexer.errors, lexeme.source_id, "Expected '%s' after '%s' but got '%s'\n", token_name(Token::ParenL), token_name(Token::KwdTrait), token_name(lexeme.token));

	lexeme = peek(&parser->lexer);

	AstBuilderToken first_child_token = AstBuilder::NO_CHILDREN;

	while (lexeme.token != Token::ParenR)
	{
		const AstBuilderToken parameter_token = parse_definition(parser, true, true);

		if (first_child_token == AstBuilder::NO_CHILDREN)
			first_child_token = parameter_token;

		lexeme = next(&parser->lexer);

		if (lexeme.token == Token::Comma)
			lexeme = peek(&parser->lexer);
		else if (lexeme.token != Token::ParenR)
			source_error(parser->lexer.errors, lexeme.source_id, "Expected '%s' or '%s' after trait parameter definition but got '%s'", token_name(Token::Comma), token_name(Token::ParenR), token_name(lexeme.token));
	}

	lexeme = peek(&parser->lexer);

	if (lexeme.token == Token::KwdExpects)
	{
		flags |= AstFlag::Trait_HasExpects;

		const AstBuilderToken expects_token = parse_expects(parser);

		if (first_child_token == AstBuilder::NO_CHILDREN)
			first_child_token = expects_token;

		lexeme = peek(&parser->lexer);
	}

	if (lexeme.token != Token::OpSet)
	{
		if ((flags & AstFlag::Trait_HasExpects) == AstFlag::EMPTY)
			source_error(parser->lexer.errors, lexeme.source_id, "Expected '%s' or '%s' after trait parameter list but got '%s'\n", token_name(Token::OpSet), token_name(Token::KwdExpects), token_name(lexeme.token));
		else
			source_error(parser->lexer.errors, lexeme.source_id, "Expected '%s' after trait expects clause but got '%s'\n", token_name(Token::OpSet), token_name(lexeme.token));
	}

	skip(&parser->lexer);

	const AstBuilderToken body_token = parse_expr(parser, true);

	if (first_child_token == AstBuilder::NO_CHILDREN)
		first_child_token = body_token;

	return push_node(&parser->builder, first_child_token, source_id, flags, AstTag::Trait);
}

static AstBuilderToken parse_impl(Parser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(&parser->lexer).token == Token::KwdImpl);

	AstFlag flags = AstFlag::EMPTY;

	const SourceId source_id = next(&parser->lexer).source_id;

	const AstBuilderToken first_child_token = parse_expr(parser, false);

	Lexeme lexeme = peek(&parser->lexer);

	if (lexeme.token == Token::KwdExpects)
	{
		flags |= AstFlag::Impl_HasExpects;

		parse_expects(parser);

		lexeme = peek(&parser->lexer);
	}

	if (lexeme.token != Token::OpSet)
	{
		if ((flags & AstFlag::Trait_HasExpects) == AstFlag::EMPTY)
			source_error(parser->lexer.errors, lexeme.source_id, "Expected '%s' or '%s' after trait parameter list but got '%s'\n", token_name(Token::OpSet), token_name(Token::KwdExpects), token_name(lexeme.token));
		else
			source_error(parser->lexer.errors, lexeme.source_id, "Expected '%s' after trait expects clause but got '%s'\n", token_name(Token::OpSet), token_name(lexeme.token));
	}

	skip(&parser->lexer);

	parse_expr(parser, true);

	return push_node(&parser->builder, first_child_token, source_id, flags, AstTag::Impl);
}

static AstBuilderToken parse_definition_or_impl(Parser* parser, bool* out_is_definition) noexcept
{
	const Lexeme lexeme = peek(&parser->lexer);

	bool is_definition = is_definition_start(lexeme.token);

	*out_is_definition = is_definition;

	if (is_definition)
		return parse_definition(parser, false, false);
	else if (lexeme.token == Token::KwdImpl)
		return parse_impl(parser);
	else
		source_error(parser->lexer.errors, lexeme.source_id, "Expected definition or impl but got %s\n", token_name(lexeme.token));
}

static AstBuilderToken parse_expr(Parser* parser, bool allow_complex) noexcept
{
	Lexeme lexeme = peek(&parser->lexer);

	OperatorStack stack;
	stack.operator_top = 0;
	stack.operand_count = 0;
	stack.expression_source_id = lexeme.source_id;

	bool expecting_operand = true;

	while (true)
	{
		if (expecting_operand)
		{
			if (lexeme.token == Token::Ident)
			{
				expecting_operand = false;

				const AstBuilderToken value_token = push_node(&parser->builder, AstBuilder::NO_CHILDREN, lexeme.source_id, AstFlag::EMPTY, ValIdentifierData{ lexeme.identifier_id });

				push_operand(parser, &stack, value_token);
			}
			else if (lexeme.token == Token::LitString)
			{
				expecting_operand = false;

				const AstBuilderToken value_token = push_node(&parser->builder, AstBuilder::NO_CHILDREN, lexeme.source_id, AstFlag::EMPTY, ValStringData{ lexeme.identifier_id });

				push_operand(parser, &stack, value_token);
			}
			else if (lexeme.token == Token::LitFloat)
			{
				expecting_operand = false;

				const AstBuilderToken value_token = push_node(&parser->builder, AstBuilder::NO_CHILDREN, lexeme.source_id, AstFlag::EMPTY, ValFloatData{ lexeme.float_value });

				push_operand(parser, &stack, value_token);
			}
			else if (lexeme.token == Token::LitInteger)
			{
				expecting_operand = false;

				const AstBuilderToken value_token = push_node(&parser->builder, AstBuilder::NO_CHILDREN, lexeme.source_id, AstFlag::EMPTY, ValIntegerData{ lexeme.integer_value });

				push_operand(parser, &stack, value_token);
			}
			else if (lexeme.token == Token::LitChar)
			{
				expecting_operand = false;

				const AstBuilderToken value_token = push_node(&parser->builder, AstBuilder::NO_CHILDREN, lexeme.source_id, AstFlag::EMPTY, ValCharData{ static_cast<u32>(lexeme.integer_value) });

				push_operand(parser, &stack, value_token);
			}
			else if (lexeme.token == Token::Wildcard)
			{
				expecting_operand = false;

				const AstBuilderToken value_token = push_node(&parser->builder, AstBuilder::NO_CHILDREN, lexeme.source_id, AstFlag::EMPTY, AstTag::Wildcard);

				push_operand(parser, &stack, value_token);
			}
			else if (lexeme.token == Token::CompositeInitializer)
			{
				expecting_operand = false;

				const SourceId source_id = lexeme.source_id;

				skip(&parser->lexer);

				lexeme = peek(&parser->lexer);

				AstBuilderToken first_child_token = AstBuilder::NO_CHILDREN;

				while (lexeme.token != Token::CurlyR)
				{
					const AstBuilderToken curr_token = parse_expr(parser, true);

					if (first_child_token == AstBuilder::NO_CHILDREN)
						first_child_token = curr_token;

					lexeme = peek(&parser->lexer);

					if (lexeme.token == Token::Comma)
					{
						skip(&parser->lexer);

						lexeme = peek(&parser->lexer);
					}
					else if (lexeme.token != Token::CurlyR)
					{
						source_error(parser->lexer.errors, lexeme.source_id, "Expected '}' or ',' after composite initializer argument expression but got '%s'\n", token_name(lexeme.token));
					}
				}

				const AstBuilderToken composite_token = push_node(&parser->builder, first_child_token, source_id, AstFlag::EMPTY, AstTag::CompositeInitializer);

				push_operand(parser, &stack, composite_token);
			}
			else if (lexeme.token == Token::ArrayInitializer)
			{
				expecting_operand = false;

				const SourceId source_id = lexeme.source_id;

				skip(&parser->lexer);

				lexeme = peek(&parser->lexer);

				AstBuilderToken first_child_token = AstBuilder::NO_CHILDREN;

				while (lexeme.token != Token::BracketR)
				{
					const AstBuilderToken curr_token = parse_expr(parser, true);

					if (first_child_token == AstBuilder::NO_CHILDREN)
						first_child_token = curr_token;

					lexeme = peek(&parser->lexer);

					if (lexeme.token == Token::Comma)
					{
						skip(&parser->lexer);

						lexeme = peek(&parser->lexer);
					}
					else if (lexeme.token != Token::BracketR)
					{
						source_error(parser->lexer.errors, lexeme.source_id, "Expected ']' or ',' after array initializer argument expression but got '%s'\n", token_name(lexeme.token));
					}
				}

				const AstBuilderToken array_token = push_node(&parser->builder, first_child_token, source_id, AstFlag::EMPTY, AstTag::ArrayInitializer);
				
				push_operand(parser, &stack, array_token);
			}
			else if (lexeme.token == Token::BracketL) // Array Type
			{
				const SourceId source_id = lexeme.source_id;

				pop_to_precedence(parser, &stack, 2, false);

				skip(&parser->lexer);

				parse_expr(parser, false);

				lexeme = peek(&parser->lexer);

				if (lexeme.token != Token::BracketR)
					source_error(parser->lexer.errors, lexeme.source_id, "Expected ']' after array type's size expression, but got '%s'\n", token_name(lexeme.token));

				// TODO: Work out how to make this into an infix operator or something
				// Use pop_to_precedence and then manually replace top
				const AstBuilderToken array_token = push_node(&parser->builder, stack.operand_tokens[stack.operand_count - 1], source_id, AstFlag::EMPTY, AstTag::OpTypeArray);

				stack.operand_tokens[stack.operand_count - 1] = array_token;
			}
			else if (lexeme.token == Token::CurlyL) // Block
			{
				expecting_operand = false;

				const SourceId source_id = lexeme.source_id;

				skip(&parser->lexer);

				lexeme = peek(&parser->lexer);

				AstBuilderToken first_child_token = AstBuilder::NO_CHILDREN;

				u32 definition_count = 0;

				while (lexeme.token != Token::CurlyR)
				{
					bool is_definition;

					const AstBuilderToken curr_token = parse_top_level_expr(parser, false, &is_definition);

					if (is_definition)
						definition_count += 1;

					if (first_child_token == AstBuilder::NO_CHILDREN)
						first_child_token = curr_token;

					lexeme = peek(&parser->lexer);

					if (lexeme.token == Token::CurlyR)
						break;
				}

				const AstBuilderToken block_token = push_node(&parser->builder, first_child_token, source_id, AstFlag::EMPTY, BlockData{ definition_count, INVALID_SCOPE_ID });
				
				push_operand(parser, &stack, block_token);
			}
			else if (lexeme.token == Token::KwdIf)
			{
				expecting_operand = false;

				const AstBuilderToken if_token = parse_if(parser);

				push_operand(parser, &stack, if_token);

				lexeme = peek(&parser->lexer);

				continue;
			}
			else if (lexeme.token == Token::KwdFor)
			{
				expecting_operand = false;

				const AstBuilderToken for_token = parse_for(parser);

				push_operand(parser, &stack, for_token);

				lexeme = peek(&parser->lexer);

				continue;
			}
			else if (lexeme.token == Token::KwdSwitch)
			{
				expecting_operand = false;

				const AstBuilderToken switch_token = parse_switch(parser);

				push_operand(parser, &stack, switch_token);

				lexeme = peek(&parser->lexer);

				continue;
			}
			else if (lexeme.token == Token::KwdFunc || lexeme.token == Token::KwdProc)
			{
				expecting_operand = false;

				const AstBuilderToken func_token = parse_func(parser);

				push_operand(parser, &stack, func_token);

				lexeme = peek(&parser->lexer);

				continue;
			}
			else if (lexeme.token == Token::KwdTrait)
			{
				expecting_operand = false;

				const AstBuilderToken trait_token = parse_trait(parser);

				push_operand(parser, &stack, trait_token);

				lexeme = peek(&parser->lexer);

				continue;
			}
			else if (lexeme.token == Token::KwdImpl)
			{
				expecting_operand = false;

				const AstBuilderToken impl_token = parse_impl(parser);

				push_operand(parser, &stack, impl_token);

				lexeme = peek(&parser->lexer);

				continue;
			}
			else if (lexeme.token == Token::Builtin)
			{
				expecting_operand = false;

				const AstBuilderToken value_token = push_node(&parser->builder, AstBuilder::NO_CHILDREN, lexeme.source_id, static_cast<AstFlag>(lexeme.integer_value), AstTag::Builtin);

				push_operand(parser, &stack, value_token);
			}
			else // Unary operator
			{
				const SourceId source_id = lexeme.source_id;

				const u8 token_ordinal = static_cast<u8>(lexeme.token);

				const u8 lo_ordinal = static_cast<u8>(Token::ParenL);

				const u8 hi_ordinal = static_cast<u8>(Token::OpAdd);

				if (token_ordinal < lo_ordinal || token_ordinal > hi_ordinal)
					source_error(parser->lexer.errors, lexeme.source_id, "Expected operand or unary operator but got '%s'\n", token_name(lexeme.token));

				OperatorDesc op = UNARY_OPERATOR_DESCS[token_ordinal - lo_ordinal];

				skip(&parser->lexer);

				lexeme = peek(&parser->lexer);

				if (op.node_flags == AstFlag::Type_IsMut)
				{
					if (lexeme.token == Token::KwdMut)
					{
						skip(&parser->lexer);

						lexeme = peek(&parser->lexer);
					}
					else
					{
						op.node_flags = AstFlag::EMPTY;
					}
				}

				push_operator(parser, &stack, { op, source_id });

				continue;
			}
		}
		else
		{
			if (lexeme.token == Token::ParenL) // Function call
			{
				ASSERT_OR_IGNORE(stack.operand_count != 0);

				const SourceId source_id = lexeme.source_id;

				pop_to_precedence(parser, &stack, 1, true);

				skip(&parser->lexer);

				lexeme = peek(&parser->lexer);

				while (lexeme.token != Token::ParenR)
				{
					bool unused;

					parse_top_level_expr(parser, true, &unused);

					lexeme = peek(&parser->lexer);

					if (lexeme.token == Token::Comma)
					{
						skip(&parser->lexer);

						lexeme = peek(&parser->lexer);
					}
					else if (lexeme.token != Token::ParenR)
					{
						source_error(parser->lexer.errors, lexeme.source_id, "Expected ')' or ',' after function argument expression but got '%s'\n", token_name(lexeme.token));
					}
				}

				const AstBuilderToken call_token = push_node(&parser->builder, stack.operand_tokens[stack.operand_count - 1], source_id, AstFlag::EMPTY, AstTag::Call);
				
				stack.operand_tokens[stack.operand_count - 1] = call_token;
			}
			else if (lexeme.token == Token::ParenR) // Closing parenthesis
			{
				if (!pop_to_precedence(parser, &stack, 10, false))
				{
					ASSERT_OR_IGNORE(stack.operand_count == 1);

					return stack.operand_tokens[stack.operand_count - 1]; // No need for stack.pop_remaining; pop_to_lparen already popped everything
				}

				remove_lparen(&stack);
			}
			else if (lexeme.token == Token::BracketL) // Array Index
			{
				ASSERT_OR_IGNORE(stack.operand_count != 0);

				const SourceId source_id = lexeme.source_id;

				pop_to_precedence(parser, &stack, 1, true);

				skip(&parser->lexer);

				parse_expr(parser, false);

				lexeme = peek(&parser->lexer);

				if (lexeme.token != Token::BracketR)
					source_error(parser->lexer.errors, lexeme.source_id, "Expected ']' after array index expression, but got '%s'\n", token_name(lexeme.token));

				const AstBuilderToken index_token = push_node(&parser->builder, stack.operand_tokens[stack.operand_count - 1], source_id, AstFlag::EMPTY, AstTag::OpArrayIndex);
				
				stack.operand_tokens[stack.operand_count - 1] = index_token;
			}
			else if (lexeme.token == Token::KwdCatch)
			{
				const SourceId source_id = lexeme.source_id;

				AstFlag flags = AstFlag::EMPTY;

				pop_to_precedence(parser, &stack, 1, true);

				skip(&parser->lexer);

				lexeme = peek(&parser->lexer);

				if (is_definition_start(lexeme.token) || peek_n(&parser->lexer, 1).token == Token::ThinArrowR)
				{
					flags |= AstFlag::Catch_HasDefinition;

					parse_definition(parser, true, true);

					lexeme = next(&parser->lexer);

					if (lexeme.token != Token::ThinArrowR)
						source_error(parser->lexer.errors, lexeme.source_id, "Expected '%s' after inbound definition in catch, but got '%s'\n", token_name(Token::ThinArrowR), token_name(lexeme.token));
				}

				parse_expr(parser, false);

				const AstBuilderToken catch_token = push_node(&parser->builder, stack.operand_tokens[stack.operand_count - 1], source_id, flags, AstTag::Catch);

				stack.operand_tokens[stack.operand_count - 1] = catch_token;

				lexeme = peek(&parser->lexer);

				continue;
			}
			else // Binary operator
			{
				const u8 token_ordinal = static_cast<u8>(lexeme.token);

				const u8 lo_ordinal = static_cast<u8>(Token::OpMemberOrRef);

				const u8 hi_ordinal = static_cast<u8>(Token::OpSetShr);

				if (token_ordinal < lo_ordinal || token_ordinal > hi_ordinal || (!allow_complex && lexeme.token == Token::OpSet))
					break;

				const OperatorDesc op = BINARY_OPERATOR_DESCS[token_ordinal - lo_ordinal];

				push_operator(parser, &stack, { op, lexeme.source_id });
				
				expecting_operand = op.is_binary;
			}
		}

		skip(&parser->lexer);

		lexeme = peek(&parser->lexer);
	}

	return pop_remaining(parser, &stack);
}

static void parse_file(Parser* parser) noexcept
{
	AstBuilderToken first_child_token = AstBuilder::NO_CHILDREN;

	u32 definition_count = 0;

	while (true)
	{
		const Lexeme lexeme = peek(&parser->lexer);

		if (lexeme.token == Token::END_OF_SOURCE)
			break;

		bool is_definition;

		const AstBuilderToken curr_token = parse_definition_or_impl(parser, &is_definition);

		if (is_definition)
			definition_count += 1;

		if (first_child_token == AstBuilder::NO_CHILDREN)
			first_child_token = curr_token;
	};

	push_node(&parser->builder, first_child_token, SourceId{ parser->lexer.source_id_base }, AstFlag::EMPTY, FileData{ BlockData{ definition_count, INVALID_SCOPE_ID } });
}



Parser* create_parser(AllocPool* pool, IdentifierPool* identifiers, ErrorSink* errors) noexcept
{
	Parser* const parser = static_cast<Parser*>(alloc_from_pool(pool, sizeof(Parser), alignof(Parser)));

	parser->lexer.identifiers = identifiers;
	parser->lexer.errors = errors;
	parser->builder.scratch.init(1u << 31, 1u << 18);

	return parser;
}

AstNode* parse(Parser* parser, SourceFileRead read, bool is_std, AstPool* out) noexcept
{
	ASSERT_OR_IGNORE(read.content.count() != 0 && read.content.end()[-1] == '\0');

	const Range<char8> content = read.content;

	parser->lexer.begin = content.begin();
	parser->lexer.end = content.end() - 1;
	parser->lexer.curr = content.begin();
	parser->lexer.source_id_base = read.source_file->source_id_base;
	parser->lexer.peek.token = Token::EMPTY;
	parser->lexer.is_std = is_std;

	parse_file(parser);

	return complete_ast(&parser->builder, out);
}

AstBuilder* get_ast_builder(Parser* parser) noexcept
{
	return &parser->builder;
}
