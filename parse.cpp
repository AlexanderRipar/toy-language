#include <cstdarg>
#include <cstdlib>

#include "infra/common.hpp"
#include "infra/alloc_pool.hpp"
#include "infra/container.hpp"
#include "infra/hash.hpp"
#include "error.hpp"
#include "ast2.hpp"
#include "ast2_attach.hpp"
#include "pass_data.hpp"

static constexpr u32 MAX_STRING_LITERAL_BYTES = 4096;

struct Lexeme
{
	Token token;

	u32 offset;

	union
	{
		u64 integer_value;

		f64 float_value; 

		IdentifierId identifier_id;
	};

	Lexeme() noexcept = default;

	Lexeme(Token token, u32 offset, u64 value_bits) noexcept :
		token{ token },
		offset{ offset },
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

struct OperatorDesc
{
	a2::Tag node_type;

	u8 precedence : 6;

	u8 is_right_to_left: 1;

	u8 is_binary : 1;
};

struct Lexer
{
	const char8* curr;

	const char8* begin;

	const char8* end;

	Lexeme peek;

	IdentifierPool* identifiers;

	IdentifierId filepath_id;
};

struct OperatorStack
{
	u32 operand_count;

	u32 operator_top;

	u32 expression_offset;

	OperatorDesc operators[64];

	a2::BuilderToken operand_tokens[128];
};

struct Parser
{
	Lexer lexer;

	a2::Builder builder;
};

static constexpr OperatorDesc UNARY_OPERATOR_DESCS[] = {
	{ a2::Tag::INVALID,           10, false, true  }, // ( - Opening Parenthesis
	{ a2::Tag::UOpMut,             2, false, false }, // mut
	{ a2::Tag::UOpEval,            8, false, false }, // eval
	{ a2::Tag::UOpTry,             8, false, false }, // try
	{ a2::Tag::UOpDefer,           8, false, false }, // defer
	{ a2::Tag::UOpAddr,            2, false, false }, // $
	{ a2::Tag::UOpBitNot,          2, false, false }, // ~
	{ a2::Tag::UOpLogNot,          2, false, false }, // !
	{ a2::Tag::UOpTypeOptPtr,      2, false, false }, // ?
	{ a2::Tag::UOpTypeVar,         2, false, false }, // ...
	{ a2::Tag::UOpTypeTailArray,   2, false, false }, // [...]
	{ a2::Tag::UOpTypeMultiPtr,    2, false, false }, // [*]
	{ a2::Tag::UOpTypeOptMultiPtr, 2, false, false }, // [?]
	{ a2::Tag::UOpTypeSlice,       2, false, false }, // []
	{ a2::Tag::UOpImpliedMember,    1, false, false }, // .
	{ a2::Tag::UOpTypePtr,         2, false, false }, // *
	{ a2::Tag::UOpNegate,          2, false, false }, // -
	{ a2::Tag::UOpPos,             2, false, false }, // +
};

static constexpr OperatorDesc BINARY_OPERATOR_DESCS[] = {
	{ a2::Tag::OpMember,     1, true,  true  }, // .
	{ a2::Tag::OpMul,        2, true,  true  }, // *
	{ a2::Tag::OpSub,        3, true,  true  }, // -
	{ a2::Tag::OpAdd,        3, true,  true  }, // +
	{ a2::Tag::OpDiv,        2, true,  true  }, // /
	{ a2::Tag::OpAddTC,      3, true,  true  }, // +:
	{ a2::Tag::OpSubTC,      3, true,  true  }, // -:
	{ a2::Tag::OpMulTC,      2, true,  true  }, // *:
	{ a2::Tag::OpMod,        2, true,  true  }, // %
	{ a2::Tag::UOpDeref,     1, false, false }, // .*
	{ a2::Tag::OpBitAnd,     6, true,  true  }, // &
	{ a2::Tag::OpBitOr,      6, true,  true  }, // |
	{ a2::Tag::OpBitXor,     6, true,  true  }, // ^
	{ a2::Tag::OpShiftL,     4, true,  true  }, // <<
	{ a2::Tag::OpShiftR,     4, true,  true  }, // >>
	{ a2::Tag::OpLogAnd,     7, true,  true  }, // &&
	{ a2::Tag::OpLogOr,      7, true,  true  }, // ||
	{ a2::Tag::OpCmpLT,      5, true,  true  }, // <
	{ a2::Tag::OpCmpGT,      5, true,  true  }, // >
	{ a2::Tag::OpCmpLE,      5, true,  true  }, // <=
	{ a2::Tag::OpCmpGE,      5, true,  true  }, // >=
	{ a2::Tag::OpCmpNE,      5, true,  true  }, // !=
	{ a2::Tag::OpCmpEQ,      5, true,  true  }, // ==
	{ a2::Tag::OpSet,        9, false, true  }, // =
	{ a2::Tag::OpSetAdd,     9, false, true  }, // +=
	{ a2::Tag::OpSetSub,     9, false, true  }, // -=
	{ a2::Tag::OpSetMul,     9, false, true  }, // *=
	{ a2::Tag::OpSetDiv,     9, false, true  }, // /=
	{ a2::Tag::OpSetAddTC,   9, false, true  }, // +:=
	{ a2::Tag::OpSetSubTC,   9, false, true  }, // -:=
	{ a2::Tag::OpSetMulTC,   9, false, true  }, // *:=
	{ a2::Tag::OpSetMod,     9, false, true  }, // %=
	{ a2::Tag::OpSetBitAnd,  9, false, true  }, // &=
	{ a2::Tag::OpSetBitOr,   9, false, true  }, // |=
	{ a2::Tag::OpSetBitXor,  9, false, true  }, // ^=
	{ a2::Tag::OpSetShiftL,  9, false, true  }, // <<=
	{ a2::Tag::OpSetShiftR,  9, false, true  }, // >>=
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



__declspec(noreturn) static void error(const Lexer* lexer, u64 offset, const char8* format, ...) noexcept
{
	va_list args;

	va_start(args, format);

	vsource_error(offset, { lexer->begin, lexer->end }, entry_from_id(lexer->identifiers, lexer->filepath_id)->range(), format, args);
}

static void skip_block_comment(Lexer* lexer) noexcept
{
	const char8* curr = lexer->curr;

	const u32 comment_offset = static_cast<u32>(curr - lexer->begin);

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
			error(lexer, comment_offset, "'/*' without matching '*/'\n");
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

	const IdentifierEntry* const identifier_value = entry_from_id(lexer->identifiers, identifier_id);

	const Token identifier_token = identifier_value->token();

	return { identifier_token, identifier_token == Token::Ident ? identifier_id.rep : 0 };
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
				error(lexer, lexer->peek.offset, "Binary integer literal exceeds maximum currently supported value of 2^64-1\n");

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
				error(lexer, lexer->peek.offset, "Octal integer literal exceeds maximum currently supported value of 2^64-1\n");

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
				error(lexer, lexer->peek.offset, "Hexadecimal integer literal exceeds maximum currently supported value of 2^64-1\n");

			value = new_value;

			curr += 1;
		}
	}

	if (curr == token_begin + 1)
		error(lexer, lexer->peek.offset, "Expected at least one digit in integer literal\n");

	if (is_identifier_continuation_char(*curr))
		error(lexer, lexer->peek.offset, "Unexpected character '%c' after binary literal\n", *curr);

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
			error(lexer, lexer->peek.offset, "Expected utf-8 surrogate code unit (0b10xx'xxxx) but got 0x%X\n", surrogate);

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
		error(lexer, lexer->peek.offset, "Unexpected code unit 0x%X at start of character literal. This might be an encoding issue regarding the source file, as only utf-8 is supported.\n", first);
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
			error(lexer, lexer->peek.offset, "Expected two hexadecimal digits after character literal escape '\\x' but got '%c' instead of first digit\n", curr[2]);

		const u8 lo = hex_char_value(curr[3]);
		
		if (lo == INVALID_HEX_CHAR_VALUE)
			error(lexer, lexer->peek.offset, "Expected two hexadecimal digits after character literal escape '\\x' but got '%c' instead of second digit\n", curr[3]);

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
				error(lexer, lexer->peek.offset, "Expected six hexadecimal digits after character literal escape '\\X' but got '%c' instead of digit %u\n", curr[i + 2], i + 1);

			codepoint = codepoint * 16 + char_value;
		}

		if (codepoint > 0x10FFFF)
			error(lexer, lexer->peek.offset, "Codepoint 0x%X indicated in character literal escape '\\X' is greater than the maximum unicode codepoint U+10FFFF", codepoint);

		curr += 6;

		break;
	}

	case 'u':
	{
		for (u32 i = 0; i != 4; ++i)
		{
			const char8 c = curr[i + 2];

			if (c < '0' || c > '9')
				error(lexer, lexer->peek.offset, "Expected four decimal digits after character literal escape '\\X' but got '%c' instead of digit %u\n", curr[i + 2], i + 1);

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
		error(lexer, lexer->peek.offset, "Unknown character literal escape '%c'\n");
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
			error(lexer, lexer->peek.offset, "Expected at least one digit after decimal point in float literal\n");

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
			error(lexer, lexer->peek.offset, "Unexpected character '%c' after float literal\n", *curr);

		char8* strtod_end;

		errno = 0;

		const f64 float_value = strtod(token_begin, &strtod_end);

		if (strtod_end != curr)
			error(lexer, lexer->peek.offset, "strtod disagrees with internal parsing about end of float literal\n");

		if (errno == ERANGE)
			error(lexer, lexer->peek.offset, "Float literal exceeds maximum IEEE-754 value\n");

		lexer->curr = curr;

		return { Token::LitFloat, float_value };
	}
	else
	{
		if (max_exceeded)
			error(lexer, lexer->peek.offset, "Integer literal exceeds maximum currently supported value of 2^64-1\n");

		if (is_alphabetic_char(*curr) || *curr == '_')
			error(lexer, lexer->peek.offset, "Unexpected character '%c' after integer literal\n", *curr);

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
		error(lexer, lexer->peek.offset, "Expected end of character literal (') but got %c\n", *lexer->curr);

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
					error(lexer, lexer->peek.offset, "String constant is longer than the supported maximum of %u bytes\n", MAX_STRING_LITERAL_BYTES);

			memcpy(buffer + buffer_index, copy_begin, bytes_to_copy);

			buffer_index += bytes_to_copy;

			lexer->curr = curr;

			const u32 codepoint = scan_escape_char(lexer);

			curr = lexer->curr;

			if (codepoint <= 0x7F)
			{
				if (buffer_index + 1 > sizeof(buffer))
					error(lexer, lexer->peek.offset, "String constant is longer than the supported maximum of %u bytes\n", MAX_STRING_LITERAL_BYTES);
			
				buffer[buffer_index] = static_cast<char8>(codepoint);

				buffer_index += 1;
			}
			else if (codepoint <= 0x7FF)
			{
				if (buffer_index + 2 > sizeof(buffer))
					error(lexer, lexer->peek.offset, "String constant is longer than the supported maximum of %u bytes\n", MAX_STRING_LITERAL_BYTES);

				buffer[buffer_index] = static_cast<char8>((codepoint >> 6) | 0xC0);

				buffer[buffer_index + 1] = static_cast<char8>((codepoint & 0x3F) | 0x80);

				buffer_index += 2;
			}
			else if (codepoint == 0x10000)
			{
				if (buffer_index + 3 > sizeof(buffer))
					error(lexer, lexer->peek.offset, "String constant is longer than the supported maximum of %u bytes\n", MAX_STRING_LITERAL_BYTES);

				buffer[buffer_index] = static_cast<char8>((codepoint >> 12) | 0xE0);

				buffer[buffer_index + 1] = static_cast<char8>(((codepoint >> 6) & 0x3F) | 0x80);

				buffer[buffer_index + 2] = static_cast<char8>((codepoint & 0x3F) | 0x80);

				buffer_index += 3;
			}
			else
			{
				ASSERT_OR_IGNORE(codepoint <= 0x10FFFF);

				if (buffer_index + 4 > sizeof(buffer))
					error(lexer, lexer->peek.offset, "String constant is longer than the supported maximum of %u bytes\n", MAX_STRING_LITERAL_BYTES);

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
			error(lexer, lexer->peek.offset, "String constant spans across newline\n");
		}
		else
		{
			curr += 1;
		}
	}

	const u32 bytes_to_copy = static_cast<u32>(curr - copy_begin);

	if (buffer_index + bytes_to_copy > sizeof(buffer))
			error(lexer, lexer->peek.offset, "String constant is longer than the supported maximum of %u bytes\n", MAX_STRING_LITERAL_BYTES);

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
			error(lexer, lexer->peek.offset, "Illegal identifier starting with '_'\n");

		return { Token::Wildcard };

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
			error(lexer, lexer->peek.offset, "'*/' without previous matching '/*'\n");
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
				error(lexer, lexer->peek.offset, "Unexpected Token '..'\n");

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
			error(lexer, lexer->peek.offset, "Null character in source file\n");

		return { Token::END_OF_SOURCE };				

	default:
		error(lexer, lexer->peek.offset, "Unexpected character '%c' in source file\n", first);
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

	lexer->peek.offset = static_cast<u32>(lexer->curr - lexer->begin);

	const RawLexeme raw = raw_next(lexer);

	return { raw.token, lexer->peek.offset, raw.integer_value };
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

	const OperatorDesc top = stack->operators[stack->operator_top - 1];

	stack->operator_top -= 1;

	if (top.node_type == a2::Tag::INVALID)
		return;

	if (stack->operand_count <= top.is_binary)
		error(&parser->lexer, stack->expression_offset, "Missing operand(s) for operator '%s'\n", a2::tag_name(top.node_type));

	if (top.is_binary)
		stack->operand_count -= 1;

	const a2::BuilderToken operator_token = a2::push_node(&parser->builder, stack->operand_tokens[stack->operand_count - 1], top.node_type, a2::Flag::EMPTY);

	stack->operand_tokens[stack->operand_count - 1] = operator_token;
}

static bool pop_to_precedence(Parser* parser, OperatorStack* stack, u8 precedence, bool pop_equal) noexcept
{
	while (stack->operator_top != 0)
	{
		const OperatorDesc top = stack->operators[stack->operator_top - 1];

		if (top.precedence > precedence || (top.precedence == precedence && !pop_equal))
			return true;

		pop_operator(parser, stack);
	}

	return false;
}

static void push_operand(Parser* parser, OperatorStack* stack, a2::BuilderToken operand_token) noexcept
{
	if (stack->operand_count == array_count(stack->operand_tokens) - 1)
		error(&parser->lexer, stack->expression_offset, "Expression exceeds maximum open operands of %u\n");

	stack->operand_tokens[stack->operand_count] = operand_token;

	stack->operand_count += 1;
}

static void push_operator(Parser* parser, OperatorStack* stack, OperatorDesc op) noexcept
{
	if (op.node_type != a2::Tag::INVALID)
		pop_to_precedence(parser, stack, op.precedence, op.is_right_to_left);

	if (stack->operator_top == array_count(stack->operators))
		error(&parser->lexer, stack->expression_offset, "Expression exceeds maximum depth of %u\n", array_count(stack->operators));

	stack->operators[stack->operator_top] = op;

	stack->operator_top += 1;
}

static void remove_lparen(OperatorStack* stack) noexcept
{
	ASSERT_OR_IGNORE(stack->operator_top != 0 && stack->operators[stack->operator_top - 1].node_type == a2::Tag::INVALID);

	stack->operator_top -= 1;
}

static a2::BuilderToken pop_remaining(Parser* parser, OperatorStack* stack) noexcept
{
	while (stack->operator_top != 0)
		pop_operator(parser, stack);

	if (stack->operand_count != 1)
		error(&parser->lexer, stack->expression_offset, "Mismatched operand / operator count (%u operands remaining)", stack->operand_count);

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

static a2::BuilderToken parse_expr(Parser* parser, bool allow_complex) noexcept;

static a2::BuilderToken parse_definition(Parser* parser, bool is_implicit, bool is_optional_value) noexcept
{
	a2::Flag flags = a2::Flag::EMPTY;

	Lexeme lexeme = next(&parser->lexer);

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
				if ((flags & a2::Flag::Definition_IsPub) != a2::Flag::EMPTY)
					error(&parser->lexer, lexeme.offset, "Definition modifier 'pub' encountered more than once\n");

				flags |= a2::Flag::Definition_IsPub;
			}
			else if (lexeme.token == Token::KwdMut)
			{
				if ((flags & a2::Flag::Definition_IsMut) != a2::Flag::EMPTY)
					error(&parser->lexer, lexeme.offset, "Definition modifier 'mut' encountered more than once\n");

				flags |= a2::Flag::Definition_IsMut;
			}
			else if (lexeme.token == Token::KwdGlobal)
			{
				if ((flags & a2::Flag::Definition_IsGlobal) != a2::Flag::EMPTY)
					error(&parser->lexer, lexeme.offset, "Definition modifier 'global' encountered more than once\n");

				flags |= a2::Flag::Definition_IsGlobal;
			}
			else if (lexeme.token == Token::KwdAuto)
			{
				if ((flags & a2::Flag::Definition_IsAuto) != a2::Flag::EMPTY)
					error(&parser->lexer, lexeme.offset, "Definition modifier 'auto' encountered more than once\n");

				flags |= a2::Flag::Definition_IsAuto;
			}
			else if (lexeme.token == Token::KwdUse)
			{
				if ((flags & a2::Flag::Definition_IsUse) != a2::Flag::EMPTY)
					error(&parser->lexer, lexeme.offset, "Definition modifier 'use' encountered more than once\n");

				flags |= a2::Flag::Definition_IsUse;
			}
			else
			{
				break;
			}

			lexeme = next(&parser->lexer);
		}

		if (flags == a2::Flag::EMPTY && !is_implicit)
			error(&parser->lexer, lexeme.offset, "Missing 'let' or at least one of 'pub', 'mut' or 'global' at start of definition\n");
	}

	if (lexeme.token != Token::Ident)
		error(&parser->lexer, lexeme.offset, "Expected 'Identifier' after Definition modifiers but got '%s'\n", token_name(lexeme.token));

	const IdentifierId identifier_id = lexeme.identifier_id;

	lexeme = peek(&parser->lexer);

	a2::BuilderToken first_child_token = a2::Builder::NO_CHILDREN;

	if (lexeme.token == Token::Colon)
	{
		flags |= a2::Flag::Definition_HasType;

		skip(&parser->lexer);

		first_child_token = parse_expr(parser, false);

		lexeme = peek(&parser->lexer);
	}

	if (lexeme.token == Token::OpSet)
	{
		skip(&parser->lexer);

		const a2::BuilderToken value_token = parse_expr(parser, true);

		if (first_child_token.rep == a2::Builder::NO_CHILDREN.rep)
			first_child_token = value_token;
	}
	else if (!is_optional_value)
	{
		error(&parser->lexer, lexeme.offset, "Expected '=' after Definition identifier and type, but got '%s'\n", token_name(lexeme.token));
	}

	return a2::push_node(&parser->builder, first_child_token, flags, a2::DefinitionData{ identifier_id });
}

static a2::BuilderToken parse_return(Parser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(&parser->lexer).token == Token::KwdReturn);

	skip(&parser->lexer);

	const a2::BuilderToken value_token = parse_expr(parser, true);

	return a2::push_node(&parser->builder, value_token, a2::Tag::Return, a2::Flag::EMPTY);
}

static a2::BuilderToken parse_leave(Parser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(&parser->lexer).token == Token::KwdLeave);

	skip(&parser->lexer);

	return a2::push_node(&parser->builder, a2::Builder::NO_CHILDREN, a2::Tag::Leave, a2::Flag::EMPTY);
}

static a2::BuilderToken parse_yield(Parser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(&parser->lexer).token == Token::KwdYield);

	skip(&parser->lexer);

	const a2::BuilderToken value_token = parse_expr(parser, true);

	return a2::push_node(&parser->builder, value_token, a2::Tag::Yield, a2::Flag::EMPTY);
}

static a2::BuilderToken parse_top_level_expr(Parser* parser, bool is_definition_optional_value) noexcept
{
	const Lexeme lexeme = peek(&parser->lexer);

	if (is_definition_start(lexeme.token))
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

static a2::BuilderToken parse_where(Parser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(&parser->lexer).token == Token::KwdWhere);

	skip(&parser->lexer);

	const a2::BuilderToken first_child_token = parse_definition(parser, true, false);

	while (true)
	{
		if (peek(&parser->lexer).token != Token::Comma)
			break;

		skip(&parser->lexer);

		parse_definition(parser, true, false);
	}

	return a2::push_node(&parser->builder, first_child_token, a2::Tag::Where, a2::Flag::EMPTY);
}

static a2::BuilderToken parse_if(Parser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(&parser->lexer).token == Token::KwdIf);

	a2::Flag flags = a2::Flag::EMPTY;

	skip(&parser->lexer);

	const a2::BuilderToken condition_token = parse_expr(parser, false);

	Lexeme lexeme = peek(&parser->lexer);

	if (lexeme.token == Token::KwdWhere)
	{
		flags |= a2::Flag::If_HasWhere;

		parse_where(parser);

		lexeme = peek(&parser->lexer);
	}

	if (lexeme.token == Token::KwdThen)
		skip(&parser->lexer);

	parse_expr(parser, true);

	lexeme = peek(&parser->lexer);

	if (lexeme.token == Token::KwdElse)
	{
		flags |= a2::Flag::If_HasElse;

		skip(&parser->lexer);

		parse_expr(parser, true);
	}

	return a2::push_node(&parser->builder, condition_token, a2::Tag::If, flags);
}

static a2::BuilderToken try_parse_foreach(Parser* parser) noexcept
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
		return a2::Builder::NO_CHILDREN;

	a2::Flag flags = a2::Flag::EMPTY;

	const a2::BuilderToken first_child_token = parse_definition(parser, true, true);

	Lexeme lexeme = peek(&parser->lexer);

	if (lexeme.token == Token::Comma)
	{
		flags |= a2::Flag::ForEach_HasIndex;

		skip(&parser->lexer);

		parse_definition(parser, true, true);

		lexeme = peek(&parser->lexer);
	}

	if (lexeme.token != Token::ThinArrowL)
		error(&parser->lexer, lexeme.offset, "Expected '%s' after for-each loop variables but got '%s'\n", token_name(Token::ThinArrowL), token_name(lexeme.token));

	skip(&parser->lexer);

	parse_expr(parser, false);

	lexeme = peek(&parser->lexer);

	if (lexeme.token == Token::KwdWhere)
	{
		flags |= a2::Flag::ForEach_HasWhere;

		parse_where(parser);

		lexeme = peek(&parser->lexer);
	}

	if (lexeme.token == Token::KwdDo)
		skip(&parser->lexer);

	parse_expr(parser, true);

	lexeme = peek(&parser->lexer);

	if (lexeme.token == Token::KwdFinally)
	{
		flags |= a2::Flag::ForEach_HasFinally;

		parse_expr(parser, true);
	}

	return a2::push_node(&parser->builder, first_child_token, a2::Tag::ForEach, flags);
}

static a2::BuilderToken parse_for(Parser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(&parser->lexer).token == Token::KwdFor);

	a2::Flag flags = a2::Flag::EMPTY;

	skip(&parser->lexer);

	if (const a2::BuilderToken foreach_token = try_parse_foreach(parser); foreach_token.rep != a2::Builder::NO_CHILDREN.rep)
		return foreach_token;

	const a2::BuilderToken first_child_token = parse_expr(parser, false);

	Lexeme lexeme = peek(&parser->lexer);

	if (lexeme.token == Token::Comma)
	{
		flags |= a2::Flag::For_HasStep;

		skip(&parser->lexer);

		parse_expr(parser, true);

		lexeme = peek(&parser->lexer);
	}

	if (lexeme.token == Token::KwdWhere)
	{
		flags |= a2::Flag::For_HasWhere;

		parse_where(parser);

		lexeme = peek(&parser->lexer);
	}

	if (lexeme.token == Token::KwdDo)
		skip(&parser->lexer);

	parse_expr(parser, true);

	lexeme = peek(&parser->lexer);

	if (lexeme.token == Token::KwdFinally)
	{
		flags |= a2::Flag::For_HasFinally;

		parse_expr(parser, true);
	}

	return a2::push_node(&parser->builder, first_child_token, a2::Tag::For, flags);
}

static a2::BuilderToken parse_case(Parser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(&parser->lexer).token == Token::KwdCase);

	skip(&parser->lexer);

	const a2::BuilderToken first_child_token = parse_expr(parser, false);

	Lexeme lexeme = next(&parser->lexer);

	if (lexeme.token != Token::ThinArrowR)
		error(&parser->lexer, lexeme.offset, "Expected '%s' after case label expression but got '%s'\n", token_name(Token::ThinArrowR), token_name(lexeme.token));

	parse_expr(parser, true);

	return a2::push_node(&parser->builder, first_child_token, a2::Tag::Case, a2::Flag::EMPTY);
}

static a2::BuilderToken parse_switch(Parser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(&parser->lexer).token == Token::KwdSwitch);

	a2::Flag flags = a2::Flag::EMPTY;

	skip(&parser->lexer);

	const a2::BuilderToken first_child_token = parse_expr(parser, false);

	Lexeme lexeme = peek(&parser->lexer);

	if (lexeme.token == Token::KwdWhere)
	{
		flags |= a2::Flag::Switch_HasWhere;

		parse_where(parser);

		lexeme = peek(&parser->lexer);
	}

	if (lexeme.token != Token::KwdCase)
		error(&parser->lexer, lexeme.offset, "Expected at least one '%s' after switch expression but got '%s'\n", token_name(Token::KwdCase), token_name(lexeme.token));

	while (true)
	{
		parse_case(parser);

		lexeme = peek(&parser->lexer);

		if (lexeme.token != Token::KwdCase)
			break;
	}

	return a2::push_node(&parser->builder, first_child_token, a2::Tag::Switch, flags);
}

static a2::BuilderToken parse_expects(Parser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(&parser->lexer).token == Token::KwdExpects);

	skip(&parser->lexer);

	const a2::BuilderToken first_child_token = parse_expr(parser, false);

	while (true)
	{
		if (peek(&parser->lexer).token != Token::Comma)
			break;

		skip(&parser->lexer);

		parse_expr(parser, false);
	}

	return a2::push_node(&parser->builder, first_child_token, a2::Tag::Expects, a2::Flag::EMPTY);
}

static a2::BuilderToken parse_ensures(Parser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(&parser->lexer).token == Token::KwdEnsures);

	skip(&parser->lexer);

	const a2::BuilderToken first_child_token = parse_expr(parser, false);

	while (true)
	{
		if (peek(&parser->lexer).token != Token::Comma)
			break;

		skip(&parser->lexer);
		
		parse_expr(parser, false);
	}

	return a2::push_node(&parser->builder, first_child_token, a2::Tag::Ensures, a2::Flag::EMPTY);
}

static a2::BuilderToken parse_func(Parser* parser) noexcept
{
	a2::Flag flags = a2::Flag::EMPTY;

	Lexeme lexeme = next(&parser->lexer);

	if (lexeme.token == Token::KwdProc)
		flags |= a2::Flag::Func_IsProc;
	else if (lexeme.token != Token::KwdFunc)
		error(&parser->lexer, lexeme.offset, "Expected '%s' or '%s' but got '%s'\n", token_name(Token::KwdFunc), token_name(Token::KwdProc), token_name(lexeme.token));

	lexeme = next(&parser->lexer);

	if (lexeme.token != Token::ParenL)
		error(&parser->lexer, lexeme.offset, "Expected '%s' after '%s' but got '%s'\n", token_name(Token::ParenL), token_name(flags == a2::Flag::Func_IsProc ? Token::KwdProc : Token::KwdFunc), token_name(lexeme.token));

	lexeme = peek(&parser->lexer);

	a2::BuilderToken first_child_token = a2::Builder::NO_CHILDREN;

	while (lexeme.token != Token::ParenR)
	{
		const a2::BuilderToken parameter_token = parse_definition(parser, true, true);

		if (first_child_token.rep == a2::Builder::NO_CHILDREN.rep)
			first_child_token = parameter_token;

		lexeme = peek(&parser->lexer);

		if (lexeme.token == Token::Comma)
			skip(&parser->lexer);
		else if (lexeme.token != Token::ParenR)
			error(&parser->lexer, lexeme.offset, "Expected '%s' or '%s' after function parameter definition but got '%s'", token_name(Token::Comma), token_name(Token::ParenR), token_name(lexeme.token));
	}

	skip(&parser->lexer);

	lexeme = peek(&parser->lexer);

	if (lexeme.token == Token::ThinArrowR)
	{
		flags |= a2::Flag::Func_HasReturnType;

		skip(&parser->lexer);

		const a2::BuilderToken return_type_token = parse_expr(parser, false);

		if (first_child_token.rep == a2::Builder::NO_CHILDREN.rep)
			first_child_token = return_type_token;

		lexeme = peek(&parser->lexer);
	}

	if (lexeme.token == Token::KwdExpects)
	{
		flags |= a2::Flag::Func_HasExpects;

		const a2::BuilderToken expects_token = parse_expects(parser);

		if (first_child_token.rep == a2::Builder::NO_CHILDREN.rep)
			first_child_token = expects_token;

		lexeme = peek(&parser->lexer);
	}

	if (lexeme.token == Token::KwdEnsures)
	{
		flags |= a2::Flag::Func_HasEnsures;

		const a2::BuilderToken ensures_token = parse_ensures(parser);

		if (first_child_token.rep == a2::Builder::NO_CHILDREN.rep)
			first_child_token = ensures_token;

		lexeme = peek(&parser->lexer);
	}

	if (lexeme.token == Token::OpSet)
	{
		flags |= a2::Flag::Func_HasBody;

		skip(&parser->lexer);

		const a2::BuilderToken body_token = parse_expr(parser, true);

		if (first_child_token.rep == a2::Builder::NO_CHILDREN.rep)
			first_child_token = body_token;
	}

	return a2::push_node(&parser->builder, first_child_token, a2::Tag::Func, flags);
}

static a2::BuilderToken parse_trait(Parser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(&parser->lexer).token == Token::KwdTrait);

	a2::Flag flags = a2::Flag::EMPTY;

	skip(&parser->lexer);

	Lexeme lexeme = next(&parser->lexer);

	if (lexeme.token != Token::ParenL)
		error(&parser->lexer, lexeme.offset, "Expected '%s' after '%s' but got '%s'\n", token_name(Token::ParenL), token_name(Token::KwdTrait), token_name(lexeme.token));

	lexeme = peek(&parser->lexer);

	a2::BuilderToken first_child_token = a2::Builder::NO_CHILDREN;

	while (lexeme.token != Token::ParenR)
	{
		const a2::BuilderToken parameter_token = parse_definition(parser, true, true);

		if (first_child_token.rep == a2::Builder::NO_CHILDREN.rep)
			first_child_token = parameter_token;

		lexeme = next(&parser->lexer);

		if (lexeme.token == Token::Comma)
			lexeme = peek(&parser->lexer);
		else if (lexeme.token != Token::ParenR)
			error(&parser->lexer, lexeme.offset, "Expected '%s' or '%s' after trait parameter definition but got '%s'", token_name(Token::Comma), token_name(Token::ParenR), token_name(lexeme.token));
	}

	lexeme = peek(&parser->lexer);

	if (lexeme.token == Token::KwdExpects)
	{
		flags |= a2::Flag::Trait_HasExpects;

		const a2::BuilderToken expects_token = parse_expects(parser);

		if (first_child_token.rep == a2::Builder::NO_CHILDREN.rep)
			first_child_token = expects_token;

		lexeme = peek(&parser->lexer);
	}

	if (lexeme.token != Token::OpSet)
	{
		if ((flags & a2::Flag::Trait_HasExpects) == a2::Flag::EMPTY)
			error(&parser->lexer, lexeme.offset, "Expected '%s' or '%s' after trait parameter list but got '%s'\n", token_name(Token::OpSet), token_name(Token::KwdExpects), token_name(lexeme.token));
		else
			error(&parser->lexer, lexeme.offset, "Expected '%s' after trait expects clause but got '%s'\n", token_name(Token::OpSet), token_name(lexeme.token));
	}

	skip(&parser->lexer);

	const a2::BuilderToken body_token = parse_expr(parser, true);

	if (first_child_token.rep == a2::Builder::NO_CHILDREN.rep)
		first_child_token = body_token;

	return a2::push_node(&parser->builder, first_child_token, a2::Tag::Trait, flags);
}

static a2::BuilderToken parse_impl(Parser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(&parser->lexer).token == Token::KwdImpl);

	a2::Flag flags = a2::Flag::EMPTY;

	skip(&parser->lexer);

	const a2::BuilderToken first_child_token = parse_expr(parser, false);

	Lexeme lexeme = peek(&parser->lexer);

	if (lexeme.token == Token::KwdExpects)
	{
		flags |= a2::Flag::Impl_HasExpects;

		parse_expects(parser);

		lexeme = peek(&parser->lexer);
	}

	if (lexeme.token != Token::OpSet)
	{
		if ((flags & a2::Flag::Trait_HasExpects) == a2::Flag::EMPTY)
			error(&parser->lexer, lexeme.offset, "Expected '%s' or '%s' after trait parameter list but got '%s'\n", token_name(Token::OpSet), token_name(Token::KwdExpects), token_name(lexeme.token));
		else
			error(&parser->lexer, lexeme.offset, "Expected '%s' after trait expects clause but got '%s'\n", token_name(Token::OpSet), token_name(lexeme.token));
	}

	skip(&parser->lexer);

	parse_expr(parser, true);

	return a2::push_node(&parser->builder, first_child_token, a2::Tag::Impl, flags);
}

static a2::BuilderToken parse_definition_or_impl(Parser* parser) noexcept
{
	const Lexeme lexeme = peek(&parser->lexer);

	bool is_definition = is_definition_start(lexeme.token);

	if (is_definition)
		return parse_definition(parser, false, false);
	else if (lexeme.token == Token::KwdImpl)
		return parse_impl(parser);
	else
		error(&parser->lexer, lexeme.offset, "Expected definition or impl but got %s\n", token_name(lexeme.token));
}

static a2::BuilderToken parse_expr(Parser* parser, bool allow_complex) noexcept
{
	Lexeme lexeme = peek(&parser->lexer);

	OperatorStack stack;
	stack.operator_top = 0;
	stack.operand_count = 0;
	stack.expression_offset = lexeme.offset;

	bool expecting_operand = true;

	while (true)
	{
		if (expecting_operand)
		{
			if (lexeme.token == Token::Ident)
			{
				expecting_operand = false;

				const a2::BuilderToken value_token = a2::push_node(&parser->builder, a2::Builder::NO_CHILDREN, a2::Flag::EMPTY, a2::ValIdentifierData{ lexeme.identifier_id });

				push_operand(parser, &stack, value_token);
			}
			else if (lexeme.token == Token::LitString)
			{
				expecting_operand = false;

				const a2::BuilderToken value_token = a2::push_node(&parser->builder, a2::Builder::NO_CHILDREN, a2::Flag::EMPTY, a2::ValStringData{ lexeme.identifier_id });

				push_operand(parser, &stack, value_token);
			}
			else if (lexeme.token == Token::LitFloat)
			{
				expecting_operand = false;

				const a2::BuilderToken value_token = a2::push_node(&parser->builder, a2::Builder::NO_CHILDREN, a2::Flag::EMPTY, a2::ValFloatData{ lexeme.float_value });

				push_operand(parser, &stack, value_token);
			}
			else if (lexeme.token == Token::LitInteger)
			{
				expecting_operand = false;

				const a2::BuilderToken value_token = a2::push_node(&parser->builder, a2::Builder::NO_CHILDREN, a2::Flag::EMPTY, a2::ValIntegerData{ lexeme.integer_value });

				push_operand(parser, &stack, value_token);
			}
			else if (lexeme.token == Token::LitChar)
			{
				expecting_operand = false;

				const a2::BuilderToken value_token = a2::push_node(&parser->builder, a2::Builder::NO_CHILDREN, a2::Flag::EMPTY, a2::ValCharData{ static_cast<u32>(lexeme.integer_value) });

				push_operand(parser, &stack, value_token);
			}
			else if (lexeme.token == Token::Wildcard)
			{
				expecting_operand = false;

				const a2::BuilderToken value_token = a2::push_node(&parser->builder, a2::Builder::NO_CHILDREN, a2::Tag::Wildcard, a2::Flag::EMPTY);

				push_operand(parser, &stack, value_token);
			}
			else if (lexeme.token == Token::CompositeInitializer)
			{
				expecting_operand = false;

				skip(&parser->lexer);

				lexeme = peek(&parser->lexer);

				a2::BuilderToken first_child_token = a2::Builder::NO_CHILDREN;

				while (lexeme.token != Token::CurlyR)
				{
					const a2::BuilderToken curr_token = parse_expr(parser, true);

					if (first_child_token.rep == a2::Builder::NO_CHILDREN.rep)
						first_child_token = curr_token;

					lexeme = peek(&parser->lexer);

					if (lexeme.token == Token::Comma)
					{
						skip(&parser->lexer);

						lexeme = peek(&parser->lexer);
					}
					else if (lexeme.token != Token::CurlyR)
					{
						error(&parser->lexer, lexeme.offset, "Expected '}' or ',' after composite initializer argument expression but got '%s'\n", token_name(lexeme.token));
					}
				}

				const a2::BuilderToken composite_token = a2::push_node(&parser->builder, first_child_token, a2::Tag::CompositeInitializer, a2::Flag::EMPTY);

				push_operand(parser, &stack, composite_token);
			}
			else if (lexeme.token == Token::ArrayInitializer)
			{
				expecting_operand = false;

				skip(&parser->lexer);

				lexeme = peek(&parser->lexer);

				a2::BuilderToken first_child_token = a2::Builder::NO_CHILDREN;

				while (lexeme.token != Token::BracketR)
				{
					const a2::BuilderToken curr_token = parse_expr(parser, true);

					if (first_child_token.rep == a2::Builder::NO_CHILDREN.rep)
						first_child_token = curr_token;

					lexeme = peek(&parser->lexer);

					if (lexeme.token == Token::Comma)
					{
						skip(&parser->lexer);

						lexeme = peek(&parser->lexer);
					}
					else if (lexeme.token != Token::BracketR)
					{
						error(&parser->lexer, lexeme.offset, "Expected ']' or ',' after array initializer argument expression but got '%s'\n", token_name(lexeme.token));
					}
				}

				const a2::BuilderToken array_token = a2::push_node(&parser->builder, first_child_token, a2::Tag::ArrayInitializer, a2::Flag::EMPTY);
				
				push_operand(parser, &stack, array_token);
			}
			else if (lexeme.token == Token::BracketL) // Array Type
			{
				pop_to_precedence(parser, &stack, 2, false);

				skip(&parser->lexer);

				parse_expr(parser, false);

				lexeme = peek(&parser->lexer);

				if (lexeme.token != Token::BracketR)
					error(&parser->lexer, lexeme.offset, "Expected ']' after array type's size expression, but got '%s'\n", token_name(lexeme.token));

				// TODO: Work out how to make this into an infix operator or something
				// Use pop_to_precedence and then manually replace top
				const a2::BuilderToken array_token = a2::push_node(&parser->builder, stack.operand_tokens[stack.operand_count - 1], a2::Tag::OpTypeArray, a2::Flag::EMPTY);

				stack.operand_tokens[stack.operand_count - 1] = array_token;
			}
			else if (lexeme.token == Token::CurlyL) // Block
			{
				expecting_operand = false;

				skip(&parser->lexer);

				lexeme = peek(&parser->lexer);

				a2::BuilderToken first_child_token = a2::Builder::NO_CHILDREN;

				while (lexeme.token != Token::CurlyR)
				{
					const a2::BuilderToken curr_token = parse_top_level_expr(parser, false);

					if (first_child_token.rep == a2::Builder::NO_CHILDREN.rep)
						first_child_token = curr_token;

					lexeme = peek(&parser->lexer);

					if (lexeme.token == Token::CurlyR)
						break;
				}

				const a2::BuilderToken block_token = a2::push_node(&parser->builder, first_child_token, a2::Flag::EMPTY, a2::BlockData{});
				
				push_operand(parser, &stack, block_token);
			}
			else if (lexeme.token == Token::KwdIf)
			{
				expecting_operand = false;

				const a2::BuilderToken if_token = parse_if(parser);

				push_operand(parser, &stack, if_token);

				lexeme = peek(&parser->lexer);

				continue;
			}
			else if (lexeme.token == Token::KwdFor)
			{
				expecting_operand = false;

				const a2::BuilderToken for_token = parse_for(parser);

				push_operand(parser, &stack, for_token);

				lexeme = peek(&parser->lexer);

				continue;
			}
			else if (lexeme.token == Token::KwdSwitch)
			{
				expecting_operand = false;

				const a2::BuilderToken switch_token = parse_switch(parser);

				push_operand(parser, &stack, switch_token);

				lexeme = peek(&parser->lexer);

				continue;
			}
			else if (lexeme.token == Token::KwdFunc || lexeme.token == Token::KwdProc)
			{
				expecting_operand = false;

				const a2::BuilderToken func_token = parse_func(parser);

				push_operand(parser, &stack, func_token);

				lexeme = peek(&parser->lexer);

				continue;
			}
			else if (lexeme.token == Token::KwdTrait)
			{
				expecting_operand = false;

				const a2::BuilderToken trait_token = parse_trait(parser);

				push_operand(parser, &stack, trait_token);

				lexeme = peek(&parser->lexer);

				continue;
			}
			else if (lexeme.token == Token::KwdImpl)
			{
				expecting_operand = false;

				const a2::BuilderToken impl_token = parse_impl(parser);

				push_operand(parser, &stack, impl_token);

				lexeme = peek(&parser->lexer);

				continue;
			}
			else // Unary operator
			{
				const u8 token_ordinal = static_cast<u8>(lexeme.token);

				const u8 lo_ordinal = static_cast<u8>(Token::ParenL);

				const u8 hi_ordinal = static_cast<u8>(Token::OpAdd);

				if (token_ordinal < lo_ordinal || token_ordinal > hi_ordinal)
					error(&parser->lexer, lexeme.offset, "Expected operand or unary operator but got '%s'\n", token_name(lexeme.token));

				const OperatorDesc op = UNARY_OPERATOR_DESCS[token_ordinal - lo_ordinal];

				push_operator(parser, &stack, op);
			}
		}
		else
		{
			if (lexeme.token == Token::ParenL) // Function call
			{
				ASSERT_OR_IGNORE(stack.operand_count != 0);

				pop_to_precedence(parser, &stack, 1, true);

				skip(&parser->lexer);

				lexeme = peek(&parser->lexer);

				while (lexeme.token != Token::ParenR)
				{
					const a2::BuilderToken curr_token = parse_top_level_expr(parser, true);

					lexeme = peek(&parser->lexer);

					if (lexeme.token == Token::Comma)
					{
						skip(&parser->lexer);

						lexeme = peek(&parser->lexer);
					}
					else if (lexeme.token != Token::ParenR)
					{
						error(&parser->lexer, lexeme.offset, "Expected ')' or ',' after function argument expression but got '%s'\n", token_name(lexeme.token));
					}
				}

				const a2::BuilderToken call_token = a2::push_node(&parser->builder, stack.operand_tokens[stack.operand_count - 1], a2::Tag::Call, a2::Flag::EMPTY);
				
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

				pop_to_precedence(parser, &stack, 1, true);

				skip(&parser->lexer);

				parse_expr(parser, false);

				lexeme = peek(&parser->lexer);

				if (lexeme.token != Token::BracketR)
					error(&parser->lexer, lexeme.offset, "Expected ']' after array index expression, but got '%s'\n", token_name(lexeme.token));

				const a2::BuilderToken index_token = a2::push_node(&parser->builder, stack.operand_tokens[stack.operand_count - 1], a2::Tag::OpArrayIndex, a2::Flag::EMPTY);
				
				stack.operand_tokens[stack.operand_count - 1] = index_token;
			}
			else if (lexeme.token == Token::KwdCatch)
			{
				a2::Flag flags = a2::Flag::EMPTY;

				pop_to_precedence(parser, &stack, 1, true);

				skip(&parser->lexer);

				lexeme = peek(&parser->lexer);

				if (is_definition_start(lexeme.token) || peek_n(&parser->lexer, 1).token == Token::ThinArrowR)
				{
					flags |= a2::Flag::Catch_HasDefinition;

					parse_definition(parser, true, true);

					lexeme = next(&parser->lexer);

					if (lexeme.token != Token::ThinArrowR)
						error(&parser->lexer, lexeme.offset, "Expected '%s' after inbound definition in catch, but got '%s'\n", token_name(Token::ThinArrowR), token_name(lexeme.token));
				}

				parse_expr(parser, false);

				const a2::BuilderToken catch_token = a2::push_node(&parser->builder, stack.operand_tokens[stack.operand_count - 1], a2::Tag::Catch, flags);

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

				push_operator(parser, &stack, op);
				
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
	a2::BuilderToken first_child_token = a2::Builder::NO_CHILDREN;

	while (true)
	{
		const Lexeme lexeme = peek(&parser->lexer);

		if (lexeme.token == Token::END_OF_SOURCE)
			break;

		const a2::BuilderToken curr_token = parse_definition_or_impl(parser);

		if (first_child_token.rep == a2::Builder::NO_CHILDREN.rep)
			first_child_token = curr_token;
	};

	a2::push_node(&parser->builder, first_child_token, a2::Flag::EMPTY, a2::FileData{ 0, parser->lexer.filepath_id });
}



Parser* create_parser(AllocPool* pool, IdentifierPool* identifiers) noexcept
{
	Parser* const parser = static_cast<Parser*>(alloc(pool, sizeof(Parser), alignof(Parser)));

	parser->lexer.identifiers = identifiers;
	parser->builder.scratch.init(1u << 31, 1u << 18);

	return parser;
}

a2::Node* parse(Parser* parser, SourceFile source, ReservedVec<u32>* out) noexcept
{
	ASSERT_OR_IGNORE(source.content().count() != 0 && source.content().end()[-1] == '\0');

	const Range<char8> content = source.content();

	parser->lexer.begin = content.begin();
	parser->lexer.end = content.end() - 1;
	parser->lexer.curr = content.begin();
	parser->lexer.filepath_id = source.filepath_id();
	parser->lexer.peek.token = Token::EMPTY;

	parse_file(parser);

	return a2::complete_ast(&parser->builder, out);
}
