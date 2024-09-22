#ifndef PARSER_INCLUDE_GUARD
#define PARSER_INCLUDE_GUARD

#include "common.hpp"
#include "token.hpp"
#include "structure.hpp"
#include "hash.hpp"
#include "ast/ast_raw.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

static constexpr u32 MAX_STRING_LITERAL_BYTES = 4096;

struct IdentifierMapEntry
{
	u32 m_hash;

	u16 m_length;

	Token m_token;

#pragma warning(push)
#pragma warning(disable : 4200) // C4200: nonstandard extension used: zero-sized array in struct/union
	char8 m_chars[];
#pragma warning(pop)

	static constexpr u32 stride() noexcept
	{
		return 8;
	}

	static u32 required_strides(Range<char8> key) noexcept
	{
		return static_cast<u32>((offsetof(IdentifierMapEntry, m_chars) + key.count() + stride() - 1) / stride());
	}

	u32 used_strides() const noexcept
	{
		return static_cast<u32>((offsetof(IdentifierMapEntry, m_chars) + m_length + stride() - 1) / stride());
	}

	u32 hash() const noexcept
	{
		return m_hash;
	}

	bool equal_to_key(Range<char8> key, u32 key_hash) const noexcept
	{
		return m_hash == key_hash && key.count() == m_length && memcmp(key.begin(), m_chars, m_length) == 0;
	}

	void init(Range<char8> key, u32 key_hash) noexcept
	{
		m_hash = key_hash;

		m_length = static_cast<u16>(key.count());

		m_token = Token::Ident;

		memcpy(m_chars, key.begin(), key.count());
	}

	Range<char8> range() const noexcept
	{
		return Range<char8>{ m_chars, m_length };
	}

	Token token() const noexcept
	{
		return m_token;
	}

	void set_token(Token token) noexcept
	{
		m_token = token;
	}
};

using IdentifierMap = IndexMap<Range<char8>, IdentifierMapEntry>;

struct Lexeme
{
	Token token;

	u32 offset;

	union
	{
		u64 integer_value;

		f64 float_value; 
	};

	Lexeme() noexcept = default;

	Lexeme(Token token, u32 offset, u64 value_bits) noexcept :
		token{ token },
		offset{ offset },
		integer_value{ value_bits }
	{}
};

struct SourceLocation
{
	u32 line;

	u32 character;
};

struct ErrorHandler
{
private:

	Range<char8> m_source;

public:

	void prime(Range<char8> source) noexcept
	{
		m_source = source;
	}

	SourceLocation source_location_from(u32 offset) const noexcept
	{
		ASSERT_OR_IGNORE(offset < m_source.count());

		u32 line = 1;

		u32 line_begin = 0;

		for (u32 i = 0; i != offset; ++i)
		{
			if (m_source[i] == '\n')
			{
				line += 1;

				line_begin = i + 1;
			}
		}

		return { line, offset - line_begin + 1 };
	}

	__declspec(noreturn) void log(u32 offset, const char8* format, ...) const noexcept
	{
		va_list args;

		va_start(args, format);

		vlog(offset, format, args);
	}

	__declspec(noreturn) void vlog(u32 offset, const char8* format, va_list args) const noexcept
	{
		const SourceLocation location = source_location_from(offset);

		fprintf(stderr, "%u:%u: ", location.line, location.character);

		vpanic(format, args);
	}
};

struct Scanner
{
private:

	static constexpr const u8 INVALID_HEX_CHAR_VALUE = 255;

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

	const char8* m_begin;

	const char8* m_curr;

	const char8* m_end;

	IdentifierMap* const m_identifiers;

	Lexeme m_peek;

	const ErrorHandler* const m_error;



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

	static bool is_identifier_start_char(char8 c) noexcept
	{
		return is_alphabetic_char(c);
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

	void skip_comment() noexcept
	{
		m_curr += 2;

		u32 comment_nesting = 1;

		while (comment_nesting != 0)
		{
			const char8 curr = *m_curr;

			if (curr == '/')
			{
				if (m_curr[1] == '*')
				{
					m_curr += 2;

					comment_nesting += 1;
				}
				else
				{
					m_curr += 1;
				}
			}
			else if (curr == '*')
			{
				if (m_curr[1] == '/')
				{
					m_curr += 2;

					comment_nesting -= 1;
				}
				else
				{
					m_curr += 1;
				}
			}
			else if (curr == '\0')
			{
				return;
			}
			else
			{
				m_curr += 1;
			}
		}
	}

	void skip_whitespace() noexcept
	{
		while (true)
		{
			while (is_whitespace(*m_curr))
				m_curr += 1;
			
			if (*m_curr == '/')
			{
				if (m_curr[1] == '/')
				{
					m_curr += 2;

					while (*m_curr != '\n' && *m_curr != '\0')
						m_curr += 1;
				}
				else if (m_curr[1] == '*')
				{
					skip_comment();
				}
				else
				{
					return;
				}
			}
			else
			{
				return;
			}
		}
	}

	RawLexeme scan_identifier_token() noexcept
	{
		const char8* token_begin = m_curr - 1;

		while (is_identifier_continuation_char(*m_curr))
			m_curr += 1;
		
		const Range<char8> identifier_bytes{ token_begin, m_curr };

		const u32 identifier_id = m_identifiers->index_from(identifier_bytes, fnv1a(identifier_bytes.as_byte_range()));

		const IdentifierMapEntry* const identifier_value = m_identifiers->value_from(identifier_id);

		const Token identifier_token = identifier_value->token();

		return { identifier_token, identifier_token == Token::Ident ? identifier_id : 0 };
	}

	RawLexeme scan_number_token_with_base(char8 base) noexcept
	{
		const char8* const token_begin = m_curr;

		m_curr += 1;

		u64 value = 0;

		if (base == 'b')
		{
			while (*m_curr == '0' || *m_curr == '1')
			{
				const u64 new_value = value * 2 + *m_curr - '0';

				if (new_value < value)
					m_error->log(m_peek.offset, "Binary integer literal exceeds maximum currently supported value of 2^64-1\n");

				value = new_value;

				m_curr += 1;
			}
		}
		else if (base == 'o')
		{
			while (*m_curr >= '0' && *m_curr <= '7')
			{
				const u64 new_value = value * 8 + *m_curr - '0';

				if (new_value < value)
					m_error->log(m_peek.offset, "Octal integer literal exceeds maximum currently supported value of 2^64-1\n");

				value = new_value;

				m_curr += 1;
			}
		}
		else
		{
			ASSERT_OR_IGNORE(base == 'x');
			
			while (true)
			{
				const u8 digit_value = hex_char_value(*m_curr);

				if (digit_value == INVALID_HEX_CHAR_VALUE)
					break;

				const u64 new_value = value * 16 + digit_value;

				if (new_value < value)
					m_error->log(m_peek.offset, "Hexadecimal integer literal exceeds maximum currently supported value of 2^64-1\n");

				value = new_value;

				m_curr += 1;
			}
		}

		if (m_curr == token_begin + 1)
			m_error->log(m_peek.offset, "Expected at least one digit in integer literal\n");

		if (is_identifier_continuation_char(*m_curr))
			m_error->log(m_peek.offset, "Unexpected character '%c' after binary literal\n", *m_curr);

		return { Token::LitInteger, value };
	}

	u32 scan_utf8_char_surrogates(u32 leader_value, u32 surrogate_count) noexcept
	{
		u32 codepoint = leader_value;

		for (u32 i = 0; i != surrogate_count; ++i)
		{
			const char8 surrogate = m_curr[i + 1];

			if ((surrogate & 0xC0) != 0x80)
				m_error->log(m_peek.offset, "Expected utf-8 surrogate code unit (0b10xx'xxxx) but got 0x%X\n", surrogate);

			codepoint |= (surrogate & 0x3F) << (6 * (surrogate_count - i - 1));
		}

		m_curr += surrogate_count + 1;

		return codepoint;
	}

	u32 scan_utf8_char() noexcept
	{
		const char8 first = *m_curr;

		if ((first & 0x80) == 0)
		{
			m_curr += 1;

			return first;
		}
		else if ((first & 0xE0) == 0xC0)
		{
			return scan_utf8_char_surrogates((first & 0x1F) << 6, 1);
		}
		else if ((first & 0xF0) == 0xE0)
		{
			return scan_utf8_char_surrogates((first & 0x0F) << 12, 2);

		}
		else if ((first & 0xF8) == 0xF0)
		{
			return scan_utf8_char_surrogates((first & 0x07) << 18, 3);
		}
		else
		{
			m_error->log(m_peek.offset, "Unexpected code unit 0x%X at start of character literal. This might be an encoding issue regarding the source file, as only utf-8 is supported.\n", first);
		}
		
	}

	u32 scan_escape_char() noexcept
	{
		u32 codepoint = 0;

		const char8 escapee = m_curr[1];

		switch (escapee)
		{
		case 'x':
		{
			const u8 hi = hex_char_value(m_curr[2]);

			if (hi == INVALID_HEX_CHAR_VALUE)
				m_error->log(m_peek.offset, "Expected two hexadecimal digits after character literal escape '\\x' but got '%c' instead of first digit\n", m_curr[2]);

			const u8 lo = hex_char_value(m_curr[3]);
			
			if (lo == INVALID_HEX_CHAR_VALUE)
				m_error->log(m_peek.offset, "Expected two hexadecimal digits after character literal escape '\\x' but got '%c' instead of secod digit\n", m_curr[3]);

			m_curr += 2;

			codepoint = lo + hi * 16;

			break;
		}
			
		case 'X':
		{
			codepoint = 0;

			for (u32 i = 0; i != 6; ++i)
			{
				const u8 char_value = hex_char_value(m_curr[i + 2]);

				if (char_value == INVALID_HEX_CHAR_VALUE)
					m_error->log(m_peek.offset, "Expected six hexadecimal digits after character literal escape '\\X' but got '%c' instead of digit %u\n", m_curr[i + 2], i + 1);

				codepoint = codepoint * 16 + char_value;
			}

			if (codepoint > 0x10FFFF)
				m_error->log(m_peek.offset, "Codepoint 0x%X indicated in character literal escape '\\X' is greater than the maximum unicode codepoint U+10FFFF", codepoint);

			m_curr += 6;

			break;
		}

		case 'u':
		{
			for (u32 i = 0; i != 4; ++i)
			{
				const char8 c = m_curr[i + 2];

				if (c < '0' || c > '9')
					m_error->log(m_peek.offset, "Expected four decimal digits after character literal escape '\\X' but got '%c' instead of digit %u\n", m_curr[i + 2], i + 1);

				codepoint = codepoint * 10 + c - '0';
			}

			m_curr += 4;

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
			m_error->log(m_peek.offset, "Unknown character literal escape '%c'\n");
		}

		m_curr += 2;

		return codepoint;
	}

	RawLexeme scan_number_token(char8 first) noexcept
	{
		const char8* const token_begin = m_curr - 1;

		u64 integer_value = first - '0';

		bool max_exceeded = false;

		while (is_numeric_char(*m_curr))
		{
			const u64 new_value = integer_value * 10 + *m_curr - '0';

			if (new_value < integer_value)
				max_exceeded = true;

			integer_value = new_value;

			m_curr += 1;
		}

		if (*m_curr == '.')
		{
			m_curr += 1;

			if (!is_numeric_char(*m_curr))
				m_error->log(m_peek.offset, "Expected at least one digit after decimal point in float literal\n");

			while (is_numeric_char(*m_curr))
				m_curr += 1;

			if (*m_curr == 'e')
			{
				m_curr += 1;

				if (*m_curr == '+' || *m_curr == '-')
					m_curr += 1;

				while (is_numeric_char(*m_curr))
					m_curr += 1;
			}
			
			if (is_alphabetic_char(*m_curr) || *m_curr == '_')
				m_error->log(m_peek.offset, "Unexpected character '%c' after float literal\n", *m_curr);

			char8* strtod_end;

			errno = 0;

			const f64 float_value = strtod(token_begin, &strtod_end);

			if (strtod_end != m_curr)
				m_error->log(m_peek.offset, "strtod disagrees with internal parsing about end of float literal\n");

			if (errno == ERANGE)
				m_error->log(m_peek.offset, "Float literal exceeds maximum IEEE-754 value\n");

			return { Token::LitFloat, float_value };
		}
		else
		{
			if (max_exceeded)
				m_error->log(m_peek.offset, "Integer literal exceeds maximum currently supported value of 2^64-1\n");

			if (is_alphabetic_char(*m_curr) || *m_curr == '_')
				m_error->log(m_peek.offset, "Unexpected character '%c' after integer literal\n", *m_curr);

			return { Token::LitInteger, integer_value };
		}

	}

	RawLexeme scan_char_token() noexcept
	{
		u32 codepoint;

		if (*m_curr == '\\')
			codepoint = scan_escape_char();
		else
			codepoint = scan_utf8_char();
		
		if (*m_curr != '\'')
			m_error->log(m_peek.offset, "Expected end of character literal (') but got %c\n", *m_curr);

		m_curr += 1;

		return { Token::LitChar, codepoint };
	}

	RawLexeme scan_string_token() noexcept
	{
		char8 buffer[MAX_STRING_LITERAL_BYTES];

		u32 buffer_index = 0;

		const char8* copy_begin = m_curr;

		while (*m_curr != '"')
		{
			if (*m_curr == '\\')
			{
				const u32 bytes_to_copy = static_cast<u32>(m_curr - copy_begin);

				if (buffer_index + bytes_to_copy > sizeof(buffer))
						m_error->log(m_peek.offset, "String constant is longer than the supported maximum of %u bytes\n", MAX_STRING_LITERAL_BYTES);

				memcpy(buffer + buffer_index, copy_begin, bytes_to_copy);

				buffer_index += bytes_to_copy;

				const u32 codepoint = scan_escape_char();

				if (codepoint <= 0x7F)
				{
					if (buffer_index + 1 > sizeof(buffer))
						m_error->log(m_peek.offset, "String constant is longer than the supported maximum of %u bytes\n", MAX_STRING_LITERAL_BYTES);
				
					buffer[buffer_index] = static_cast<char8>(codepoint);

					buffer_index += 1;
				}
				else if (codepoint <= 0x7FF)
				{
					if (buffer_index + 2 > sizeof(buffer))
						m_error->log(m_peek.offset, "String constant is longer than the supported maximum of %u bytes\n", MAX_STRING_LITERAL_BYTES);

					buffer[buffer_index] = static_cast<char8>((codepoint >> 6) | 0xC0);

					buffer[buffer_index + 1] = static_cast<char8>((codepoint & 0x3F) | 0x80);

					buffer_index += 2;
				}
				else if (codepoint == 0x10000)
				{
					if (buffer_index + 3 > sizeof(buffer))
						m_error->log(m_peek.offset, "String constant is longer than the supported maximum of %u bytes\n", MAX_STRING_LITERAL_BYTES);

					buffer[buffer_index] = static_cast<char8>((codepoint >> 12) | 0xE0);

					buffer[buffer_index + 1] = static_cast<char8>(((codepoint >> 6) & 0x3F) | 0x80);

					buffer[buffer_index + 2] = static_cast<char8>((codepoint & 0x3F) | 0x80);

					buffer_index += 3;
				}
				else
				{
					ASSERT_OR_IGNORE(codepoint <= 0x10FFFF);

					if (buffer_index + 4 > sizeof(buffer))
						m_error->log(m_peek.offset, "String constant is longer than the supported maximum of %u bytes\n", MAX_STRING_LITERAL_BYTES);

					buffer[buffer_index] = static_cast<char8>((codepoint >> 18) | 0xE0);

					buffer[buffer_index + 1] = static_cast<char8>(((codepoint >> 12) & 0x3F) | 0x80);

					buffer[buffer_index + 2] = static_cast<char8>(((codepoint >> 6) & 0x3F) | 0x80);

					buffer[buffer_index + 3] = static_cast<char8>((codepoint & 0x3F) | 0x80);

					buffer_index += 4;
				}

				copy_begin = buffer + buffer_index;
			}
			else if (*m_curr == '\n')
			{
				m_error->log(m_peek.offset, "String constant spans across newline\n");
			}
			else
			{
				m_curr += 1;
			}
		}

		const u32 bytes_to_copy = static_cast<u32>(m_curr - copy_begin);

		if (buffer_index + bytes_to_copy > sizeof(buffer))
				m_error->log(m_peek.offset, "String constant is longer than the supported maximum of %u bytes\n", MAX_STRING_LITERAL_BYTES);

		memcpy(buffer + buffer_index, copy_begin, bytes_to_copy);

		buffer_index += bytes_to_copy;

		const Range<char8> string_bytes{ buffer, buffer_index };

		const u32 string_index = m_identifiers->index_from(string_bytes, fnv1a(string_bytes.as_byte_range()));

		m_curr += 1;

		return { Token::LitString, string_index };
	}

	RawLexeme raw_next() noexcept
	{
		const char8 first = *m_curr;

		m_curr += 1;

		const char8 second = first == '\0' ? '\0' : *m_curr;

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
			return scan_identifier_token();
		
		case '0':
			if (second == 'b' || second == 'o' || second == 'x')
				return scan_number_token_with_base(second);

		// fallthrough
		
		case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8':
		case '9':
			return scan_number_token(first);

		case '\'':
			return scan_char_token();

		case '"':
			return scan_string_token();

		case '+':
			if (second == '=')
			{
				m_curr += 1;

				return { Token::OpSetAdd };
			}
			else if (second == ':')
			{
				if (m_curr[1] == '=')
				{
					m_curr += 2;

					return { Token::OpSetAddTC };
				}
				else
				{
					m_curr += 1;

					return { Token::OpAddTC };
				}
			}
			else
			{
				return { Token::OpAdd };
			}

		case '-':if (second == '>')
			{
				m_curr += 1;

				return { Token::ThinArrowR };
			}
			else if (second == ':')
			{
				if (m_curr[1] == '=')
				{
					m_curr += 2;

					return { Token::OpSetSubTC };
				}
				else
				{
					m_curr += 1;

					return { Token::OpSubTC };
				}
			}
			else if (second == '=')
			{
				m_curr += 1;

				return { Token::OpSetSub };
			}
			else
			{
				return { Token::OpSub };
			}

		case '*':
			if (second == '=')
			{
				m_curr += 1;

				return { Token::OpSetMul };
			}
			else if (second == ':')
			{
				if (m_curr[1] == '=')
				{
					m_curr += 2;

					return { Token::OpSetMulTC };
				}
				else
				{
					m_curr += 1;

					return { Token::OpMulTC };
				}
			}
			else
			{
				return { Token::OpMulOrTypPtr };
			}

		case '/':
			if (second == '=')
			{
				m_curr += 1;

				return { Token::OpSetDiv };
			}
			else
			{
				return { Token::OpDiv };
			}

		case '%':
			if (second == '=')
			{
				m_curr += 1;

				return { Token::OpSetMod };
			}
			else
			{
				return { Token::OpMod };
			}

		case '&':
			if (second == '&')
			{
				m_curr += 1;

				return { Token::OpLogAnd };
			}
			else if (second == '=')
			{
				m_curr += 1;

				return { Token::OpSetAnd };
			}
			else
			{
				return { Token::OpAnd };
			}

		case '|':
			if (second == '|')
			{
				m_curr += 1;

				return { Token::OpLogAnd };
			}
			else if (second == '=')
			{
				m_curr += 1;

				return { Token::OpSetOr };
			}
			else
			{
				return { Token::OpOr };
			}

		case '^':
			if (second == '=')
			{
				m_curr += 1;

				return { Token::OpSetXor };
			}
			else
			{
				return { Token::OpXor };
			}

		case '<':
			if (second == '<')
			{
				if (m_curr[1] == '=')
				{
					m_curr += 2;

					return { Token::OpSetShl };
				}
				else
				{
					m_curr += 1;

					return { Token::OpShl };
				}
			}
			else if (second == '=')
			{
				m_curr += 1;

				return { Token::OpLe };
			}
			else
			{
				return { Token::OpLt };
			}

		case '>':
			if (second == '>')
			{
				if (m_curr[1] == '=')
				{
					m_curr += 2;

					return { Token::OpSetShr };
				}
				else
				{
					m_curr += 1;

					return { Token::OpShr };
				}
			}
			else if (second == '=')
			{
				m_curr += 1;

				return { Token::OpGe };
			}
			else
			{
				return { Token::OpGt };
			}

		case '.':
			if (second == '.')
			{
				if (m_curr[1] != '.')
					m_error->log(m_peek.offset, "Unexpected Token '..'\n");

				m_curr += 2;

				return { Token::TypVar };
			}
			else if (second == '*')
			{
				m_curr += 1;

				return { Token::UOpDeref };
			}
			else if (second == '[')
			{
				m_curr += 1;
				return { Token::LitArray };
			}
			else if (second == '{')
			{
				m_curr += 1;
				return { Token::LitComposite };
			}
			else
			{
				return { Token::OpMemberOrRef };
			}

		case '!':
			if (second == '=')
			{
				m_curr += 1;

				return { Token::OpNe };
			}
			else
			{
				return { Token::UOpLogNot };
			}

		case '=':
			if (second == '=')
			{
				m_curr += 1;

				return { Token::OpEq };
			}
			else if (second == '>')
			{
				m_curr += 1;

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
			if (second == '.' && m_curr[1] == '.' && m_curr[2] == '.' && m_curr[3] == ']')
			{
				m_curr += 4;

				return { Token::TypTailArray };
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
			m_curr -= 1;

			if (m_curr != m_end)
				m_error->log(m_peek.offset, "Null character in source file\n");

			return { Token::END_OF_SOURCE };				

		default:
			m_error->log(m_peek.offset, "Unexpected character '%c' in source file\n", *m_curr);
		}
	}

public:

	Scanner(IdentifierMap* identifiers, const ErrorHandler* error) noexcept :
		m_identifiers{ identifiers },
		m_error{ error }
	{}

	void prime(Range<char8> source) noexcept
	{
		ASSERT_OR_IGNORE(source.count() != 0 && source.end()[-1] == '\0');

		m_begin = source.begin();

		m_curr = source.begin();

		m_end = source.end() - 1;

		m_peek.token = Token::EMPTY;
	}

	Lexeme next() noexcept
	{
		if (m_peek.token != Token::EMPTY)
		{
			const Lexeme rst = m_peek;

			m_peek.token = Token::EMPTY;

			return rst;
		}

		skip_whitespace();

		m_peek.offset = static_cast<u32>(m_curr - m_begin);

		const RawLexeme raw = raw_next();

		return { raw.token, m_peek.offset, raw.integer_value };
	}

	Lexeme peek() noexcept
	{
		if (m_peek.token != Token::EMPTY)
			return m_peek;

		m_peek = next();

		return m_peek;
	}

	void skip() noexcept
	{
		(void) next();
	}
};

struct Parser
{
private:

	struct OperatorDesc
	{
		ast::NodeType node_type;

		u8 precedence : 5;

		u8 is_left_to_right: 1;
		
		u8 is_binary : 1;

		u8 is_context_end : 1;
	};

	struct OperatorStack
	{
	private:

		struct NestingContextTemplate
		{
			Token closing_token;

			u8 allow_multiple : 1;

			u8 allow_comma : 1;

			// Used for array type '[count]Type', so that the operator for '['
			// is re-pushed (without context flag) once ']' is reached
			u8 delay_node : 1;
			
			u8 allow_empty : 1;
		};

		struct NestingContext
		{
			Token token; 

			Token closing_token;

			u8 allow_multiple : 1;

			u8 allow_comma : 1;

			u8 delay_node : 1;

			u8 allow_empty : 1;

			u8 child_count;

			u32 token_offset;
		};

		static constexpr NestingContextTemplate CONTEXT_TEMPLATES[] = {
			{ Token::ParenR,   false, false, false, false }, // ( - Opening Parenthesis
			{ Token::ParenR,   true,  true,  false,  true }, // ( - Call
			{ Token::BracketR, false, false, true,  false }, // [ - Array Type
			{ Token::BracketR, false, false, false, false }, // [ - Array Index
			{ Token::CurlyR,   true,  true,  false,  true }, // .{
			{ Token::BracketR, true,  true,  false,  true }, // .[
		};

		u32 m_free_operand_count;

		u32 m_operator_top;

		u32 m_context_top;

		OperatorDesc m_operators[64];

		NestingContext m_contexts[64];

		u32 m_expression_offset;

		const ErrorHandler* const m_error;

		ast::raw::TreeBuilder* const m_builder;

		void pop_operator() noexcept
		{
			ASSERT_OR_IGNORE(m_operator_top != 0);

			const OperatorDesc top = m_operators[m_operator_top - 1];
		
			m_operator_top -= 1;

			if (top.precedence == 0)
			{
				ASSERT_OR_IGNORE(m_context_top != 0);

				const NestingContext context = m_contexts[m_context_top - 1];

				m_error->log(context.token_offset, "'%s' missing matching '%s'", token_name(context.token), token_name(context.closing_token));
			}

			if (m_free_operand_count <= top.is_binary)
				m_error->log(m_expression_offset, "Missing operand(s) for operator %u\n", top.node_type);

			m_free_operand_count -= top.is_binary;

			m_builder->append(top.node_type, 1 + top.is_binary);
		}

		void pop_to_context() noexcept
		{
			while (true)
			{
				if (m_operator_top == 0)
					m_error->log(m_expression_offset, "Ran out of operators while trying to find innermost context\n");

				if (m_operators[m_operator_top - 1].precedence == 0)
					return;

				pop_operator();
			}
		}

	public:

		OperatorStack(u32 expression_offset, const ErrorHandler* error, ast::raw::TreeBuilder* builder) noexcept :
			m_free_operand_count{ 0 },
			m_operator_top{ 0 },
			m_context_top{ 0 },
			m_expression_offset{ expression_offset },
			m_error{ error },
			m_builder{ builder }
		{}

		void push_operand() noexcept
		{
			if (m_context_top != 0 && m_contexts[m_context_top - 1].child_count == 0)
				m_contexts[m_context_top - 1].child_count = 1;

			m_free_operand_count += 1;
		}

		[[nodiscard]] bool push_operator(OperatorDesc op, Lexeme lexeme) noexcept
		{
			while (m_operator_top != 0)
			{
				const OperatorDesc top = m_operators[m_operator_top - 1];

				if (top.precedence < op.precedence)
					break;

				if (top.precedence == op.precedence && !op.is_left_to_right)
					break;

				pop_operator();
			}

			if (op.precedence == 0)
			{
				if (op.is_context_end)
				{
					const NestingContext context = m_contexts[m_context_top - 1];

					if (context.closing_token != lexeme.token)
						m_error->log(context.token_offset, "'%s' missing matching '%s'", token_name(context.token), token_name(context.closing_token));

					m_context_top -= 1;

					ASSERT_OR_IGNORE(m_operator_top != 0 && m_operators[m_operator_top - 1].precedence == 0 && !m_operators[m_operator_top - 1].is_context_end);

					ASSERT_OR_IGNORE(!context.delay_node || context.child_count == 0);

					if (context.delay_node)
					{
						m_operators[m_operator_top - 1].precedence = 1;
					}
					else
					{
						if (context.child_count == 0 && !context.allow_empty)
							m_error->log(context.token_offset, "Missing expression between '%s' and '%s'\n", token_name(context.token), token_name(context.closing_token));

						m_builder->append(m_operators[m_operator_top - 1].node_type, context.child_count + 1);

						m_free_operand_count -= context.child_count;

						m_operator_top -= 1;
					}

					return context.delay_node;
				}
				else
				{
					if (m_context_top == array_count(m_contexts))
						m_error->log(m_expression_offset, "Context nesting exceeds maximum depth of %u\n", array_count(m_contexts));

					const u8 ordinal = static_cast<u8>(op.node_type);

					if (ordinal > static_cast<u8>(ast::NodeType::ArrayInitializer))
						m_error->log(m_expression_offset, "Unexpected context node type %u\n", ordinal);

					const NestingContextTemplate tpl = CONTEXT_TEMPLATES[ordinal];

					m_contexts[m_context_top] = { lexeme.token, tpl.closing_token, tpl.allow_multiple, tpl.allow_comma, tpl.delay_node, tpl.allow_empty, 0, lexeme.offset };

					m_context_top += 1;
				}
			}

			if (m_operator_top == array_count(m_operators))
				m_error->log(m_expression_offset, "Operator nesting exceeds maximum depth of %u\n", array_count(m_operators));

			m_operators[m_operator_top] = op;

			m_operator_top += 1;

			return op.is_binary;
		}

		void pop_remaining() noexcept
		{
			while (m_operator_top != 0)
				pop_operator();

			if (m_free_operand_count != 1)
				m_error->log(m_expression_offset, "Mismatched operand / operator count (%u operands remaining)", m_free_operand_count);
		}

		[[nodiscard]] bool push_comma() noexcept
		{
			if (m_context_top == 0 || !m_contexts[m_context_top - 1].allow_comma)
				return false;

			pop_to_context();

			m_contexts[m_context_top - 1].child_count += 1;

			return true;
		}
	};

	static constexpr OperatorDesc TYPE_ARRAY_OPERATOR_DESC      = { ast::NodeType::OpTypeArray, 2, false, false };

	static constexpr OperatorDesc UNARY_OPERATOR_DESCS[] = {
		{ ast::NodeType::UOpTry,               8, false, false }, // try
		{ ast::NodeType::UOpDefer,             8, false, false }, // defer
		{ ast::NodeType::ArrayInitializer,     0, false, false }, // .[
		{ ast::NodeType::CompositeInitializer, 0, false, false }, // .{
		{ ast::NodeType::UOpAddr,              2, false, false }, // $
		{ ast::NodeType::UOpDeref,             1, false, false }, // .*
		{ ast::NodeType::UOpBitNot,            2, false, false }, // ~
		{ ast::NodeType::UOpLogNot,            2, false, false }, // !
		{ ast::NodeType::UOpTypOptPtr,         2, false, false }, // ?
		{ ast::NodeType::UOpTypVar,            2, false, false }, // ...
		{ ast::NodeType::UOpTypTailArray,      2, false, false }, // [...]
		{ ast::NodeType::UOpTypeMultiPtr,      2, false, false }, // [*]
		{ ast::NodeType::UOpTypeSlice,         2, false, false }, // []
		{ ast::NodeType::OpImpliedMember,      1, false, false }, // .
		{ ast::NodeType::INVALID,              0, false, true  }, // ( - Opening Parenthesis
		{ ast::NodeType::INVALID,              0, false, false, true }, // )
		{ ast::NodeType::OpTypeArray,          0, false, true  }, // [ - Array Index
		{ ast::NodeType::INVALID,              0, false, false, true }, // ]
		{ ast::NodeType::UOpTypPtr,            2, false, false }, // *
		{ ast::NodeType::UOpNegate,            2, false, false }, // -
		{ ast::NodeType::UOpPos,               2, false, false }, // +
	};

	static constexpr OperatorDesc BINARY_OPERATOR_DESCS[] = {
		{ ast::NodeType::OpMember,     1, true,  true  }, // .
		{ ast::NodeType::Call,         0, false, true  }, // ( - Call
		{ ast::NodeType::INVALID,      0, false, false, true }, // )
		{ ast::NodeType::OpArrayIndex, 0, true,  true  }, // [ - Array Index
		{ ast::NodeType::INVALID,      0, false, false, true }, // ]
		{ ast::NodeType::OpMul,        2, true,  true  }, // *
		{ ast::NodeType::OpSub,        3, true,  true  }, // -
		{ ast::NodeType::OpAdd,        3, true,  true  }, // +
		{ ast::NodeType::OpDiv,        2, true,  true  }, // /
		{ ast::NodeType::OpAddTC,      3, true,  true  }, // +:
		{ ast::NodeType::OpSubTC,      3, true,  true  }, // -:
		{ ast::NodeType::OpMulTC,      2, true,  true  }, // *:
		{ ast::NodeType::OpMod,        2, true,  true  }, // %
		{ ast::NodeType::OpBitAnd,     6, true,  true  }, // &
		{ ast::NodeType::OpBitOr,      6, true,  true  }, // |
		{ ast::NodeType::OpBitXor,     6, true,  true  }, // ^
		{ ast::NodeType::OpShiftL,     4, true,  true  }, // <<
		{ ast::NodeType::OpShiftR,     4, true,  true  }, // >>
		{ ast::NodeType::OpLogAnd,     7, true,  true  }, // &&
		{ ast::NodeType::OpLogOr,      7, true,  true  }, // ||
		{ ast::NodeType::OpCmpLT,      5, true,  true  }, // <
		{ ast::NodeType::OpCmpGT,      5, true,  true  }, // >
		{ ast::NodeType::OpCmpLE,      5, true,  true  }, // <=
		{ ast::NodeType::OpCmpGE,      5, true,  true  }, // >=
		{ ast::NodeType::OpCmpNE,      5, true,  true  }, // !=
		{ ast::NodeType::OpCmpEQ,      5, true,  true  }, // ==
		{ ast::NodeType::OpSet,        9, false, true  }, // =
		{ ast::NodeType::OpSetAdd,     9, false, true  }, // +=
		{ ast::NodeType::OpSetSub,     9, false, true  }, // -=
		{ ast::NodeType::OpSetMul,     9, false, true  }, // *=
		{ ast::NodeType::OpSetDiv,     9, false, true  }, // /=
		{ ast::NodeType::OpSetAddTC,   9, false, true  }, // +:=
		{ ast::NodeType::OpSetSubTC,   9, false, true  }, // -:=
		{ ast::NodeType::OpSetMulTC,   9, false, true  }, // *:=
		{ ast::NodeType::OpSetMod,     9, false, true  }, // %=
		{ ast::NodeType::OpSetBitAnd,  9, false, true  }, // &=
		{ ast::NodeType::OpSetBitOr,   9, false, true  }, // |=
		{ ast::NodeType::OpSetBitXor,  9, false, true  }, // ^=
		{ ast::NodeType::OpSetShiftL,  9, false, true  }, // <<=
		{ ast::NodeType::OpSetShiftR,  9, false, true  }, // >>=
		{ ast::NodeType::OpCatch,      8, true,  true  }, // catch
	};

	static constexpr AttachmentRange<char8, Token> KEYWORDS[] = {
		range::from_literal_string("if", Token::KwdIf),
		range::from_literal_string("then", Token::KwdThen),
		range::from_literal_string("else", Token::KwdElse),
		range::from_literal_string("for", Token::KwdFor),
		range::from_literal_string("do", Token::KwdDo),
		range::from_literal_string("finally", Token::KwdFinally),
		range::from_literal_string("switch", Token::KwdSwitch),
		range::from_literal_string("case", Token::KwdCase),
		range::from_literal_string("try", Token::KwdTry),
		range::from_literal_string("catch", Token::KwdCatch),
		range::from_literal_string("defer", Token::KwdDefer),
		range::from_literal_string("func", Token::KwdFunc),
		range::from_literal_string("proc", Token::KwdProc),
		range::from_literal_string("trait", Token::KwdTrait),
		range::from_literal_string("impl", Token::KwdImpl),
		range::from_literal_string("where", Token::KwdWhere),
		range::from_literal_string("let", Token::KwdLet),
		range::from_literal_string("mut", Token::KwdMut),
		range::from_literal_string("auto", Token::KwdAuto),
		range::from_literal_string("pub", Token::KwdPub),
		range::from_literal_string("global", Token::KwdGlobal),
	};

	IdentifierMap m_identifiers;

	Scanner m_scanner;

	ast::raw::TreeBuilder m_builder;

	u64* const m_ast_memory;

	u32 m_ast_used;

	const u32 m_ast_commit_increment;

	const u32 m_ast_reserve;

	ErrorHandler m_error;



	void parse_expr(bool include_set) noexcept
	{
		Lexeme lexeme = m_scanner.peek();

		OperatorStack stack{ lexeme.offset, &m_error, &m_builder };

		bool expecting_operand = true;

		while (true)
		{
			if (expecting_operand)
			{
				if (lexeme.token == Token::Ident)
				{
					expecting_operand = false;

					ast::raw::attach::ValIdentifierData* data;

					m_builder.append(&data, 0);

					data->identifier_id = static_cast<u32>(lexeme.integer_value);

					stack.push_operand();
				}
				else if (lexeme.token == Token::LitString)
				{
					expecting_operand = false;

					ast::raw::attach::ValStringData* data;

					m_builder.append(&data, 0);

					data->string_id = static_cast<u32>(lexeme.integer_value);

					stack.push_operand();
				}
				else if (lexeme.token == Token::LitFloat)
				{
					expecting_operand = false;

					ast::raw::attach::ValFloatData* data;

					m_builder.append(&data, 0);

					data->set(lexeme.float_value);

					stack.push_operand();
				}
				else if (lexeme.token == Token::LitInteger)
				{
					expecting_operand = false;

					ast::raw::attach::ValIntegerData* data;

					m_builder.append(&data, 0);

					data->set(lexeme.integer_value);

					stack.push_operand();
				}
				else if (lexeme.token == Token::LitChar)
				{
					expecting_operand = false;

					ast::raw::attach::ValCharData* data;

					m_builder.append(&data, 0);

					data->codepoint = static_cast<u32>(lexeme.integer_value);

					stack.push_operand();
				}
				else // Unary operator
				{
					const u8 token_ordinal = static_cast<u8>(lexeme.token);

					const u8 lo_ordinal = static_cast<u8>(Token::KwdTry);

					const u8 hi_ordinal = static_cast<u8>(Token::OpAdd);

					if (token_ordinal < lo_ordinal || token_ordinal > hi_ordinal)
						m_error.log(lexeme.offset, "Expected operand or unary operator but got '%s'\n", token_name(lexeme.token));

					const OperatorDesc op = UNARY_OPERATOR_DESCS[token_ordinal - lo_ordinal];

					expecting_operand = stack.push_operator(op, lexeme);
				}
			}
			else
			{
				/*
				if (lexeme.token == Token::ParenL) // Function call
				{
					m_scanner.skip();

					u32 argument_count = 0;

					lexeme = m_scanner.peek();

					if (lexeme.token != Token::ParenR)
					{
						while (true)
						{
							argument_count += 1;

							if (argument_count == 255)
							{
								lexeme = m_scanner.peek();

								m_error.log(lexeme.offset, "Number of arguments to function call exceeds the supported maximum of 254\n");
							}

							parse_expr(true);

							lexeme = m_scanner.peek();

							if (lexeme.token == Token::ParenR)
								break;
							else if (lexeme.token != Token::Comma)
								m_error.log(lexeme.offset, "Expected ')' or ',' after function argument expression but got '%s'\n", token_name(lexeme.token));

							m_scanner.skip();
						}
					}

					m_builder.append(ast::NodeType::Call, static_cast<u8>(argument_count + 1));
				}
				*/
				if (lexeme.token == Token::Comma)
				{
					expecting_operand = true;

					if (!stack.push_comma())
						break;
				}
				else // Binary operator
				{
					const u8 token_ordinal = static_cast<u8>(lexeme.token);

					const u8 lo_ordinal = static_cast<u8>(Token::OpMemberOrRef);

					const u8 hi_ordinal = static_cast<u8>(Token::BracketR);

					if (token_ordinal < lo_ordinal || token_ordinal > hi_ordinal || (!include_set && lexeme.token == Token::OpSet))
						break;

					const OperatorDesc op = BINARY_OPERATOR_DESCS[token_ordinal - lo_ordinal];

					expecting_operand = stack.push_operator(op, lexeme);
				}
			}

			m_scanner.skip();

			lexeme = m_scanner.peek();
		}

		stack.pop_remaining();
	}

	void parse_definition() noexcept
	{
		ast::raw::Flag flags = ast::raw::Flag::EMPTY;

		Lexeme lexeme = m_scanner.next();

		if (lexeme.token == Token::KwdLet)
		{
			lexeme = m_scanner.next();
		}
		else
		{
			while (true)
			{
				if (lexeme.token == Token::KwdPub)
				{
					if ((flags & ast::raw::Flag::Definition_IsPub) != ast::raw::Flag::EMPTY)
						m_error.log(lexeme.offset, "Definition modifier 'pub' encountered more than once\n");

					flags |= ast::raw::Flag::Definition_IsPub;
				}
				else if (lexeme.token == Token::KwdMut)
				{
					if ((flags & ast::raw::Flag::Definition_IsMut) != ast::raw::Flag::EMPTY)
						m_error.log(lexeme.offset, "Definition modifier 'mut' encountered more than once\n");

					flags |= ast::raw::Flag::Definition_IsMut;
				}
				else if (lexeme.token == Token::KwdGlobal)
				{
					if ((flags & ast::raw::Flag::Definition_IsGlobal) != ast::raw::Flag::EMPTY)
						m_error.log(lexeme.offset, "Definition modifier 'global' encountered more than once\n");

					flags |= ast::raw::Flag::Definition_IsGlobal;
				}
				else
				{
					break;
				}
			
				lexeme = m_scanner.next();
			}

			if (flags == ast::raw::Flag::EMPTY)
				m_error.log(lexeme.offset, "Missing 'let' or at least one of 'pub', 'mut' or 'global' at start of definition\n");
		}

		if (lexeme.token != Token::Ident)
			m_error.log(lexeme.offset, "Expected 'Identifier' after Definition modifiers but got '%s'\n", token_name(lexeme.token));

		const u32 identifier_id = static_cast<u32>(lexeme.integer_value);

		lexeme = m_scanner.peek();

		const bool has_type = lexeme.token == Token::Colon; 

		if (has_type)
		{
			m_scanner.skip();

			parse_expr(false);

			flags |= ast::raw::Flag::Definition_HasType;

			lexeme = m_scanner.peek();
		}
		
		if (lexeme.token != Token::OpSet)
			m_error.log(lexeme.offset, "Expected '=' after Definition identifier and type, but got '%s'\n", token_name(lexeme.token));

		m_scanner.skip();

		parse_expr(false);

		ast::raw::attach::DefinitionData* data;

		m_builder.append(&data, has_type ? 2 : 1, flags);

		data->identifier_id = identifier_id;

		return;
	}

public:

	Parser(u32 ast_reserve, u32 ast_commit_increment) noexcept :
		m_identifiers{ 1 << 24, 1 << 14, 1 << 28, 1 << 16, 1 << 16 },
		m_scanner{ &m_identifiers, &m_error },
		m_builder{},
		m_ast_memory{ static_cast<u64*>(minos::reserve(static_cast<u64>(ast_reserve) * 8)) },
		m_ast_used{ 0 },
		m_ast_commit_increment{ ast_commit_increment },
		m_ast_reserve{ ast_reserve }
	{
		for (u32 i = 0; i != array_count(KEYWORDS); ++i)
			m_identifiers.value_from(KEYWORDS[i].range(), fnv1a(KEYWORDS[i].as_byte_range()))->set_token(KEYWORDS[i].attachment());

		if (m_ast_memory == nullptr)
			panic("Could not reserve memory for ASTs (0x%X)\n", minos::last_error());
	}

	ast::raw::Tree parse(Range<char8> source) noexcept
	{
		m_builder = { reinterpret_cast<ast::raw::NodeHeader*>(m_ast_memory + m_ast_used), m_ast_reserve - m_ast_used, m_ast_commit_increment };

		m_error.prime(source);

		m_scanner.prime(source);

		while (true)
		{
			const Lexeme lexeme = m_scanner.peek();

			if (lexeme.token == Token::END_OF_SOURCE)
				break;
			else if (lexeme.token != Token::KwdLet
			      && lexeme.token != Token::KwdPub
				  && lexeme.token != Token::KwdMut
				  && lexeme.token != Token::KwdGlobal)
				m_error.log(lexeme.offset, "Expected 'let', 'pub', 'mut' or 'global', but got '%s'\n", token_name(lexeme.token));

			parse_definition();
		};

		return m_builder.build();
	}
};

#endif // PARSER_INCLUDE_GUARD
