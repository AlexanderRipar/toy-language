#ifndef PARSER_INCLUDE_GUARD
#define PARSER_INCLUDE_GUARD

#include "common.hpp"
#include "reader.hpp"
#include "token.hpp"
#include "structure.hpp"
#include "hash.hpp"
#include "ast/ast_raw.hpp"
#include "error.hpp"

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
		const u32 comment_offset = static_cast<u32>(m_curr - m_begin);

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
				m_error->log(comment_offset, "'/*' without matching '*/'\n");
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

		case '_':
			if (is_identifier_continuation_char(second))
				m_error->log(m_peek.offset, "Illegal identifier starting with '_'\n");

			return { Token::Wildcard };

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

		case '-':
			if (second == '>')
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
			else if (second == '/')
			{
				m_error->log(m_peek.offset, "'*/' without previous matching '/*'\n");
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
			else if (second == '-')
			{
				m_curr += 1;

				return { Token::ThinArrowL };
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
				return { Token::ArrayInitializer };
			}
			else if (second == '{')
			{
				m_curr += 1;
				return { Token::CompositeInitializer };
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
			else if (second == '*' && m_curr[1] == ']')
			{
				m_curr += 2;

				return { Token::TypMultiPtr };
			}
			else if (second == '?' && m_curr[1] == ']')
			{
				m_curr += 2;

				return { Token::TypOptMultiPtr };
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
			m_error->log(m_peek.offset, "Unexpected character '%c' in source file\n", first);
		}
	}

public:

	Scanner(IdentifierMap* identifiers, const ErrorHandler* error) noexcept :
		m_identifiers{ identifiers },
		m_error{ error }
	{}

	void prime(SourceFile source) noexcept
	{
		const Range<char8> content = source.content();

		ASSERT_OR_IGNORE(content.count() != 0 && content.end()[-1] == '\0');

		m_begin = content.begin();

		m_curr = content.begin();

		m_end = content.end() - 1;

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

	Lexeme peek_n(u32 n) noexcept
	{
		ASSERT_OR_IGNORE(n != 0);

		const Lexeme remembered_peek = peek();

		const char8* const remembered_curr = m_curr;

		m_peek.token = Token::EMPTY;

		Lexeme result = remembered_peek;

		for (u32 i = 0; i != n; ++i)
			result = next();

		m_curr = remembered_curr;

		m_peek = remembered_peek;

		return result;
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
		ast::raw::Type node_type;

		u8 precedence : 6;

		u8 is_right_to_left: 1;
		
		u8 is_binary : 1;
	};

	struct OperatorStack
	{
	private:

		u32 m_free_operand_count;

		u32 m_operator_top;

		u32 m_expression_offset;

		Parser* m_parser;

		OperatorDesc m_operators[64];

		void pop_operator() noexcept
		{
			ASSERT_OR_IGNORE(m_operator_top != 0);

			const OperatorDesc top = m_operators[m_operator_top - 1];
		
			m_operator_top -= 1;

			if (top.node_type == ast::raw::Type::INVALID)
				return;

			if (m_free_operand_count <= top.is_binary)
				m_parser->m_error.log(m_expression_offset, "Missing operand(s) for operator '%s'\n", ast::raw::type_name(top.node_type));

			m_free_operand_count -= top.is_binary;

			m_parser->append_node(top.node_type, 1 + top.is_binary);
		}

	public:

		OperatorStack(u32 expression_offset, Parser* parser) noexcept :
			m_free_operand_count{ 0 },
			m_operator_top{ 0 },
			m_expression_offset{ expression_offset },
			m_parser{ parser }
		{}

		void push_operand() noexcept
		{
			m_free_operand_count += 1;
		}

		void push_operator(OperatorDesc op) noexcept
		{
			if (op.node_type != ast::raw::Type::INVALID)
				pop_to_precedence(op.precedence, op.is_right_to_left);

			if (m_operator_top == array_count(m_operators))
				m_parser->m_error.log(m_expression_offset, "Operator nesting exceeds maximum depth of %u\n", array_count(m_operators));

			m_operators[m_operator_top] = op;

			m_operator_top += 1;
		}

		bool pop_to_precedence(u8 precedence, bool pop_equal) noexcept
		{
			while (m_operator_top != 0)
			{
				const OperatorDesc top = m_operators[m_operator_top - 1];

				if (top.precedence > precedence || (top.precedence == precedence && !pop_equal))
					return true;

				pop_operator();
			}

			return false;
		}

		void remove_lparen() noexcept
		{
			ASSERT_OR_IGNORE(m_operator_top != 0 && m_operators[m_operator_top - 1].node_type == ast::raw::Type::INVALID);

			m_operator_top -= 1;
		}

		void pop_remaining() noexcept
		{
			while (m_operator_top != 0)
				pop_operator();

			if (m_free_operand_count != 1)
				m_parser->m_error.log(m_expression_offset, "Mismatched operand / operator count (%u operands remaining)", m_free_operand_count);
		}
	};

	static constexpr OperatorDesc UNARY_OPERATOR_DESCS[] = {
		{ ast::raw::Type::INVALID,           10, false, true  }, // ( - Opening Parenthesis
		{ ast::raw::Type::UOpTry,             8, false, false }, // try
		{ ast::raw::Type::UOpDefer,           8, false, false }, // defer
		{ ast::raw::Type::UOpAddr,            2, false, false }, // $
		{ ast::raw::Type::UOpBitNot,          2, false, false }, // ~
		{ ast::raw::Type::UOpLogNot,          2, false, false }, // !
		{ ast::raw::Type::UOpTypeOptPtr,      2, false, false }, // ?
		{ ast::raw::Type::UOpTypeVar,         2, false, false }, // ...
		{ ast::raw::Type::UOpTypeTailArray,   2, false, false }, // [...]
		{ ast::raw::Type::UOpTypeMultiPtr,    2, false, false }, // [*]
		{ ast::raw::Type::UOpTypeOptMultiPtr, 2, false, false }, // [?]
		{ ast::raw::Type::UOpTypeSlice,       2, false, false }, // []
		{ ast::raw::Type::OpImpliedMember,    1, false, false }, // .
		{ ast::raw::Type::UOpTypePtr,         2, false, false }, // *
		{ ast::raw::Type::UOpNegate,          2, false, false }, // -
		{ ast::raw::Type::UOpPos,             2, false, false }, // +
	};

	static constexpr OperatorDesc BINARY_OPERATOR_DESCS[] = {
		{ ast::raw::Type::OpMember,     1, true,  true  }, // .
		{ ast::raw::Type::OpMul,        2, true,  true  }, // *
		{ ast::raw::Type::OpSub,        3, true,  true  }, // -
		{ ast::raw::Type::OpAdd,        3, true,  true  }, // +
		{ ast::raw::Type::OpDiv,        2, true,  true  }, // /
		{ ast::raw::Type::OpAddTC,      3, true,  true  }, // +:
		{ ast::raw::Type::OpSubTC,      3, true,  true  }, // -:
		{ ast::raw::Type::OpMulTC,      2, true,  true  }, // *:
		{ ast::raw::Type::OpMod,        2, true,  true  }, // %
		{ ast::raw::Type::UOpDeref,     1, false, false }, // .*
		{ ast::raw::Type::OpBitAnd,     6, true,  true  }, // &
		{ ast::raw::Type::OpBitOr,      6, true,  true  }, // |
		{ ast::raw::Type::OpBitXor,     6, true,  true  }, // ^
		{ ast::raw::Type::OpShiftL,     4, true,  true  }, // <<
		{ ast::raw::Type::OpShiftR,     4, true,  true  }, // >>
		{ ast::raw::Type::OpLogAnd,     7, true,  true  }, // &&
		{ ast::raw::Type::OpLogOr,      7, true,  true  }, // ||
		{ ast::raw::Type::OpCmpLT,      5, true,  true  }, // <
		{ ast::raw::Type::OpCmpGT,      5, true,  true  }, // >
		{ ast::raw::Type::OpCmpLE,      5, true,  true  }, // <=
		{ ast::raw::Type::OpCmpGE,      5, true,  true  }, // >=
		{ ast::raw::Type::OpCmpNE,      5, true,  true  }, // !=
		{ ast::raw::Type::OpCmpEQ,      5, true,  true  }, // ==
		{ ast::raw::Type::OpSet,        9, false, true  }, // =
		{ ast::raw::Type::OpSetAdd,     9, false, true  }, // +=
		{ ast::raw::Type::OpSetSub,     9, false, true  }, // -=
		{ ast::raw::Type::OpSetMul,     9, false, true  }, // *=
		{ ast::raw::Type::OpSetDiv,     9, false, true  }, // /=
		{ ast::raw::Type::OpSetAddTC,   9, false, true  }, // +:=
		{ ast::raw::Type::OpSetSubTC,   9, false, true  }, // -:=
		{ ast::raw::Type::OpSetMulTC,   9, false, true  }, // *:=
		{ ast::raw::Type::OpSetMod,     9, false, true  }, // %=
		{ ast::raw::Type::OpSetBitAnd,  9, false, true  }, // &=
		{ ast::raw::Type::OpSetBitOr,   9, false, true  }, // |=
		{ ast::raw::Type::OpSetBitXor,  9, false, true  }, // ^=
		{ ast::raw::Type::OpSetShiftL,  9, false, true  }, // <<=
		{ ast::raw::Type::OpSetShiftR,  9, false, true  }, // >>=
	};

	static constexpr AttachmentRange<char8, Token> KEYWORDS[] = {
		range::from_literal_string("if",      Token::KwdIf),
		range::from_literal_string("then",    Token::KwdThen),
		range::from_literal_string("else",    Token::KwdElse),
		range::from_literal_string("for",     Token::KwdFor),
		range::from_literal_string("do",      Token::KwdDo),
		range::from_literal_string("finally", Token::KwdFinally),
		range::from_literal_string("switch",  Token::KwdSwitch),
		range::from_literal_string("case",    Token::KwdCase),
		range::from_literal_string("try",     Token::KwdTry),
		range::from_literal_string("catch",   Token::KwdCatch),
		range::from_literal_string("defer",   Token::KwdDefer),
		range::from_literal_string("func",    Token::KwdFunc),
		range::from_literal_string("proc",    Token::KwdProc),
		range::from_literal_string("trait",   Token::KwdTrait),
		range::from_literal_string("impl",    Token::KwdImpl),
		range::from_literal_string("where",   Token::KwdWhere),
		range::from_literal_string("expects", Token::KwdExpects),
		range::from_literal_string("ensures", Token::KwdEnsures),
		range::from_literal_string("pub",     Token::KwdPub),
		range::from_literal_string("mut",     Token::KwdMut),
		range::from_literal_string("let",     Token::KwdLet),
		range::from_literal_string("auto",    Token::KwdAuto),
		range::from_literal_string("use",     Token::KwdUse),
		range::from_literal_string("global",  Token::KwdGlobal),
	};

	IdentifierMap m_identifiers;

	Scanner m_scanner;

	ReservedVec<u32> m_asts;

	ReservedVec<u32> m_ast_scratch;

	ReservedVec<u32> m_stack_scratch;

	ErrorHandler m_error;

	static bool is_definition_start(Token token) noexcept
	{
		return token == Token::KwdLet
		    || token == Token::KwdPub
			|| token == Token::KwdMut
			|| token == Token::KwdGlobal
			|| token == Token::KwdAuto
			|| token == Token::KwdUse;
	}

	static void reverse_node(ReservedVec<u32>* target, const ast::raw::Node* src) noexcept
	{
		ast::raw::Node* const dst = reinterpret_cast<ast::raw::Node*>(target->reserve_exact(sizeof(ast::raw::Node) + src->data_dwords * sizeof(u32)));

		memcpy(dst, src, sizeof(ast::raw::Node) + src->data_dwords * sizeof(u32));

		if (src->child_count != 0)
		{
			const u32 offset = reinterpret_cast<const u32*>(src)[2 + src->data_dwords];

			reverse_node(target, reinterpret_cast<const ast::raw::Node*>(reinterpret_cast<const u32*>(src) - offset));
		}

		if (src->next_sibling_offset != 0)
			reverse_node(target, reinterpret_cast<const ast::raw::Node*>(reinterpret_cast<const u32*>(src) + src->next_sibling_offset));
	}

	ast::raw::Node* append_node(ast::raw::Type type, u16 child_count, ast::raw::Flag flags = ast::raw::Flag::EMPTY, u8 data_dwords = 0) noexcept
	{
		ASSERT_OR_IGNORE(static_cast<u8>(flags) < 64);

		ASSERT_OR_IGNORE(data_dwords < 3 || (child_count == 0 && data_dwords < 4));

		ast::raw::Node* const node = static_cast<ast::raw::Node*>(m_ast_scratch.reserve_exact(sizeof(ast::raw::Node) + (data_dwords + static_cast<u8>(child_count != 0)) * sizeof(u32)));

		node->type = type;

		node->data_dwords = data_dwords;

		node->flags = static_cast<u8>(flags);

		node->child_count = child_count;

		if (child_count != 0)
		{
			u32 child_index = m_stack_scratch.begin()[m_stack_scratch.used() - child_count];

			reinterpret_cast<u32*>(node)[2 + data_dwords] = static_cast<u32>(reinterpret_cast<u32*>(node) - (m_ast_scratch.begin() + child_index));

			for (u16 i = 1; i != child_count; ++i)
			{
				ast::raw::Node* const child = reinterpret_cast<ast::raw::Node*>(m_ast_scratch.begin() + child_index);

				const u32 next_child_index = m_stack_scratch.begin()[m_stack_scratch.used() - child_count + i];

				child->next_sibling_offset = next_child_index - child_index;

				child_index = next_child_index;
			}

			m_stack_scratch.pop(child_count);
		}

		m_stack_scratch.append(static_cast<u32>(reinterpret_cast<u32*>(node) - m_ast_scratch.begin()));

		return node;
	}

	void parse_expr(bool allow_complex) noexcept
	{
		Lexeme lexeme = m_scanner.peek();

		OperatorStack stack{ lexeme.offset, this };

		bool expecting_operand = true;

		while (true)
		{
			if (expecting_operand)
			{
				if (lexeme.token == Token::Ident)
				{
					expecting_operand = false;

					ast::raw::Node* const header = append_node(ast::raw::Type::ValIdentifer, 0, ast::raw::Flag::EMPTY, 1);

					*reinterpret_cast<u32*>(header + 1) = static_cast<u32>(lexeme.integer_value);

					stack.push_operand();
				}
				else if (lexeme.token == Token::LitString)
				{
					expecting_operand = false;

					ast::raw::Node* const header = append_node(ast::raw::Type::ValString, 0, ast::raw::Flag::EMPTY, 1);

					*reinterpret_cast<u32*>(header + 1) = static_cast<u32>(lexeme.integer_value);

					stack.push_operand();
				}
				else if (lexeme.token == Token::LitFloat)
				{
					expecting_operand = false;

					ast::raw::Node* const header = append_node(ast::raw::Type::ValFloat, 0, ast::raw::Flag::EMPTY, 2);

					*reinterpret_cast<f64*>(header + 1) = lexeme.float_value;

					stack.push_operand();
				}
				else if (lexeme.token == Token::LitInteger)
				{
					expecting_operand = false;

					const u8 data_dwords = lexeme.integer_value < 64 ? 0 : lexeme.integer_value <= UINT32_MAX ? 1 : 2;

					ast::raw::Node* const header = append_node(ast::raw::Type::ValInteger, 0, ast::raw::Flag::EMPTY, data_dwords);

					if (data_dwords == 0)
						header->flags = static_cast<u8>(lexeme.integer_value);
					else if (data_dwords == 1)
						*reinterpret_cast<u32*>(header + 1) = static_cast<u32>(lexeme.integer_value);
					else
						memcpy(header + 1, &lexeme.integer_value, sizeof(u64));

					stack.push_operand();
				}
				else if (lexeme.token == Token::LitChar)
				{
					expecting_operand = false;

					ast::raw::Node* const header = append_node(ast::raw::Type::ValChar, 0, ast::raw::Flag::EMPTY, 1);

					*reinterpret_cast<u32*>(header + 1) = static_cast<u32>(lexeme.integer_value);

					stack.push_operand();
				}
				else if (lexeme.token == Token::Wildcard)
				{
					expecting_operand = false;

					append_node(ast::raw::Type::Wildcard, 0);

					stack.push_operand();
				}
				else if (lexeme.token == Token::CompositeInitializer)
				{
					expecting_operand = false;

					m_scanner.skip();

					lexeme = m_scanner.peek();

					u16 child_count = 0;

					while (lexeme.token != Token::CurlyR)
					{
						if (child_count == UINT16_MAX)
							m_error.log(m_scanner.peek().offset, "Number of top-level expressions in composite initializer exceeds the supported maximum of %u\n", UINT16_MAX);

						child_count += 1;

						parse_expr(true);

						lexeme = m_scanner.peek();

						if (lexeme.token == Token::Comma)
						{
							m_scanner.skip();

							lexeme = m_scanner.peek();
						}
						else if (lexeme.token != Token::CurlyR)
						{
							m_error.log(lexeme.offset, "Expected '}' or ',' after composite initializer argument expression but got '%s'\n", token_name(lexeme.token));
						}
					}

					append_node(ast::raw::Type::CompositeInitializer, child_count);

					stack.push_operand();
				}
				else if (lexeme.token == Token::ArrayInitializer)
				{
					expecting_operand = false;

					m_scanner.skip();

					lexeme = m_scanner.peek();

					u16 child_count = 0;

					while (lexeme.token != Token::BracketR)
					{
						if (child_count == UINT16_MAX)
							m_error.log(m_scanner.peek().offset, "Number of top-level expressions in array initializer exceeds the supported maximum of %u\n", UINT16_MAX);

						child_count += 1;

						parse_expr(true);

						lexeme = m_scanner.peek();

						if (lexeme.token == Token::Comma)
						{
							m_scanner.skip();

							lexeme = m_scanner.peek();
						}
						else if (lexeme.token != Token::BracketR)
						{
							m_error.log(lexeme.offset, "Expected ']' or ',' after array initializer argument expression but got '%s'\n", token_name(lexeme.token));
						}
					}

					append_node(ast::raw::Type::ArrayInitializer, child_count);

					stack.push_operand();
				}
				else if (lexeme.token == Token::BracketL) // Array Type
				{
					m_scanner.skip();

					parse_expr(false);

					lexeme = m_scanner.peek();

					if (lexeme.token != Token::BracketR)
						m_error.log(lexeme.offset, "Expected ']' after array type's size expression, but got '%s'\n", token_name(lexeme.token));

					stack.push_operand();

					stack.push_operator({ ast::raw::Type::OpTypeArray, 2, false, true });
				}
				else if (lexeme.token == Token::CurlyL) // Block
				{
					expecting_operand = false;

					m_scanner.skip();

					lexeme = m_scanner.peek();

					u16 child_count = 0;

					while (lexeme.token != Token::CurlyR)
					{
						if (child_count == UINT16_MAX)
							m_error.log(m_scanner.peek().offset, "Number of top-level expressions in block exceeds the supported maximum of %u\n", UINT16_MAX);

						child_count += 1;

						parse_top_level_expr(false);

						lexeme = m_scanner.peek();

						if (lexeme.token == Token::CurlyR)
							break;
					}

					append_node(ast::raw::Type::Block, child_count);

					stack.push_operand();
				}
				else if (lexeme.token == Token::KwdIf)
				{
					expecting_operand = false;

					parse_if();

					stack.push_operand();

					lexeme = m_scanner.peek();

					continue;
				}
				else if (lexeme.token == Token::KwdFor)
				{
					expecting_operand = false;

					parse_for();

					stack.push_operand();

					lexeme = m_scanner.peek();

					continue;
				}
				else if (lexeme.token == Token::KwdSwitch)
				{
					expecting_operand = false;

					parse_switch();

					stack.push_operand();

					lexeme = m_scanner.peek();

					continue;
				}
				else if (lexeme.token == Token::KwdFunc || lexeme.token == Token::KwdProc)
				{
					expecting_operand = false;

					parse_func();

					stack.push_operand();

					lexeme = m_scanner.peek();

					continue;
				}
				else if (lexeme.token == Token::KwdTrait)
				{
					expecting_operand = false;

					parse_trait();

					stack.push_operand();

					lexeme = m_scanner.peek();

					continue;
				}
				else if (lexeme.token == Token::KwdImpl)
				{
					expecting_operand = false;

					parse_impl();

					stack.push_operand();

					lexeme = m_scanner.peek();

					continue;
				}
				else // Unary operator
				{
					const u8 token_ordinal = static_cast<u8>(lexeme.token);

					const u8 lo_ordinal = static_cast<u8>(Token::ParenL);

					const u8 hi_ordinal = static_cast<u8>(Token::OpAdd);

					if (token_ordinal < lo_ordinal || token_ordinal > hi_ordinal)
						m_error.log(lexeme.offset, "Expected operand or unary operator but got '%s'\n", token_name(lexeme.token));

					const OperatorDesc op = UNARY_OPERATOR_DESCS[token_ordinal - lo_ordinal];

					stack.push_operator(op);
				}
			}
			else
			{
				if (lexeme.token == Token::ParenL) // Function call
				{
					stack.pop_to_precedence(1, true);

					m_scanner.skip();

					lexeme = m_scanner.peek();

					u16 child_count = 1;

					while (lexeme.token != Token::ParenR)
					{
						if (child_count == UINT16_MAX)
							m_error.log(m_scanner.peek().offset, "Number of arguments to function call exceeds the supported maximum of %u\n", UINT16_MAX - 1);

						child_count += 1;

						parse_top_level_expr(true);

						lexeme = m_scanner.peek();

						if (lexeme.token == Token::Comma)
						{
							m_scanner.skip();

							lexeme = m_scanner.peek();
						}
						else if (lexeme.token != Token::ParenR)
						{
							m_error.log(lexeme.offset, "Expected ')' or ',' after function argument expression but got '%s'\n", token_name(lexeme.token));
						}
					}

					append_node(ast::raw::Type::Call, child_count);
				}
				else if (lexeme.token == Token::ParenR) // Closing parenthesis
				{
					if (!stack.pop_to_precedence(10, false))
						return; // No need for stack.pop_remaining; pop_to_lparen already popped everything

					stack.remove_lparen();
				}
				else if (lexeme.token == Token::BracketL) // Array Index
				{
					stack.pop_to_precedence(1, true);

					m_scanner.skip();

					parse_expr(false);

					lexeme = m_scanner.peek();

					if (lexeme.token != Token::BracketR)
						m_error.log(lexeme.offset, "Expected ']' after array index expression, but got '%s'\n", token_name(lexeme.token));

					append_node(ast::raw::Type::OpArrayIndex, 2);
				}
				else if (lexeme.token == Token::KwdCatch)
				{
					u16 child_count = 2;

					ast::raw::Flag flags = ast::raw::Flag::EMPTY;

					stack.pop_to_precedence(1, true);

					m_scanner.skip();

					lexeme = m_scanner.peek();

					if (is_definition_start(lexeme.token) || m_scanner.peek_n(1).token == Token::ThinArrowR)
					{
						child_count += 1;

						flags |= ast::raw::Flag::Catch_HasDefinition;

						parse_definition(true, true);

						lexeme = m_scanner.next();

						if (lexeme.token != Token::ThinArrowR)
							m_error.log(lexeme.offset, "Expected '%s' after inbound definition in catch, but got '%s'\n", token_name(Token::ThinArrowR), token_name(lexeme.token));
					}

					parse_expr(false);

					append_node(ast::raw::Type::Catch, child_count, flags);

					lexeme = m_scanner.peek();

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

					stack.push_operator(op);
					
					expecting_operand = op.is_binary;
				}
			}

			m_scanner.skip();

			lexeme = m_scanner.peek();
		}

		stack.pop_remaining();
	}

	void parse_top_level_expr(bool is_definition_optional_value) noexcept
	{
		const Lexeme lexeme = m_scanner.peek();

		if (is_definition_start(lexeme.token))
			parse_definition(false, is_definition_optional_value);
		else
			parse_expr(true);
	}

	void parse_if() noexcept
	{
		ASSERT_OR_IGNORE(m_scanner.peek().token == Token::KwdIf);

		u16 child_count = 2;

		ast::raw::Flag flags = ast::raw::Flag::EMPTY;

		m_scanner.skip();

		parse_expr(false);

		Lexeme lexeme = m_scanner.peek();

		if (lexeme.token == Token::KwdWhere)
		{
			child_count += 1;

			flags |= ast::raw::Flag::If_HasWhere;

			parse_where();

			lexeme = m_scanner.peek();
		}

		if (lexeme.token == Token::KwdThen)
			m_scanner.skip();

		parse_expr(true);

		lexeme = m_scanner.peek();

		if (lexeme.token == Token::KwdElse)
		{
			child_count += 1;

			flags |= ast::raw::Flag::If_HasElse;

			m_scanner.skip();

			parse_expr(true);
		}

		append_node(ast::raw::Type::If, child_count, flags);
	}

	void parse_for() noexcept
	{
		ASSERT_OR_IGNORE(m_scanner.peek().token == Token::KwdFor);

		u16 child_count = 2;

		ast::raw::Flag flags = ast::raw::Flag::EMPTY;

		m_scanner.skip();

		if (try_parse_foreach())
			return;

		parse_expr(false);

		Lexeme lexeme = m_scanner.peek();

		if (lexeme.token == Token::Comma)
		{
			child_count += 1;

			flags |= ast::raw::Flag::For_HasStep;

			m_scanner.skip();

			parse_expr(true);

			lexeme = m_scanner.peek();
		}

		if (lexeme.token == Token::KwdWhere)
		{
			child_count += 1;

			flags |= ast::raw::Flag::For_HasWhere;

			parse_where();

			lexeme = m_scanner.peek();
		}

		if (lexeme.token == Token::KwdDo)
			m_scanner.skip();

		parse_expr(true);

		lexeme = m_scanner.peek();

		if (lexeme.token == Token::KwdFinally)
		{
			child_count += 1;
			
			flags |= ast::raw::Flag::For_HasFinally;

			parse_expr(true);
		}

		append_node(ast::raw::Type::For, child_count, flags);
	}

	[[nodiscard]] bool try_parse_foreach() noexcept
	{
		bool is_foreach = false;

		if (is_definition_start(m_scanner.peek().token))
		{
			is_foreach = true;
		}
		else if (const Lexeme lookahead_1 = m_scanner.peek_n(1); lookahead_1.token == Token::ThinArrowL)
		{
			is_foreach = true;
		}
		else if (lookahead_1.token == Token::Comma)
		{
			if (const Lexeme lookahead_2 = m_scanner.peek_n(2); is_definition_start(lookahead_2.token))
				is_foreach = true;
			if (const Lexeme lookahead_3 = m_scanner.peek_n(3); lookahead_3.token == Token::ThinArrowL)
				is_foreach = true;
		}

		if (!is_foreach)
			return false;

		u16 child_count = 3;

		ast::raw::Flag flags = ast::raw::Flag::EMPTY;

		parse_definition(true, true);

		Lexeme lexeme = m_scanner.peek();

		if (lexeme.token == Token::Comma)
		{
			child_count += 1;

			flags |= ast::raw::Flag::ForEach_HasIndex;

			m_scanner.skip();

			parse_definition(true, true);

			lexeme = m_scanner.peek();
		}

		if (lexeme.token != Token::ThinArrowL)
			m_error.log(lexeme.offset, "Expected '%s' after for-each loop variables but got '%s'\n", token_name(Token::ThinArrowL), token_name(lexeme.token));

		m_scanner.skip();

		parse_expr(false);

		lexeme = m_scanner.peek();

		if (lexeme.token == Token::KwdWhere)
		{
			child_count += 1;

			flags |= ast::raw::Flag::ForEach_HasWhere;

			parse_where();

			lexeme = m_scanner.peek();
		}

		if (lexeme.token == Token::KwdDo)
			m_scanner.skip();

		parse_expr(true);

		lexeme = m_scanner.peek();

		if (lexeme.token == Token::KwdFinally)
		{
			child_count += 1;
			
			flags |= ast::raw::Flag::ForEach_HasFinally;

			parse_expr(true);
		}

		append_node(ast::raw::Type::ForEach, child_count, flags);

		return true;
	}

	void parse_switch() noexcept
	{
		ASSERT_OR_IGNORE(m_scanner.peek().token == Token::KwdSwitch);

		u16 child_count = 1;

		ast::raw::Flag flags = ast::raw::Flag::EMPTY;

		m_scanner.skip();

		parse_expr(false);

		Lexeme lexeme = m_scanner.peek();

		if (lexeme.token == Token::KwdWhere)
		{
			child_count += 1;

			flags |= ast::raw::Flag::Switch_HasWhere;

			parse_where();

			lexeme = m_scanner.peek();
		}

		if (lexeme.token != Token::KwdCase)
			m_error.log(lexeme.offset, "Expected at least one '%s' after switch expression but got '%s'\n", token_name(Token::KwdCase), token_name(lexeme.token));

		while (true)
		{
			if (child_count == UINT16_MAX)
				m_error.log(m_scanner.peek().offset, "Combined number of cases, where-clause and switch expression in switch exceeds the supported maximum of %u\n", UINT16_MAX);

			child_count += 1;

			parse_case();

			lexeme = m_scanner.peek();

			if (lexeme.token != Token::KwdCase)
				break;
		}

		append_node(ast::raw::Type::Switch, child_count, flags);
	}

	void parse_case() noexcept
	{
		ASSERT_OR_IGNORE(m_scanner.peek().token == Token::KwdCase);

		m_scanner.skip();

		parse_expr(false);

		Lexeme lexeme = m_scanner.next();

		if (lexeme.token != Token::ThinArrowR)
			m_error.log(lexeme.offset, "Expected '%s' after case label expression but got '%s'\n", token_name(Token::ThinArrowR), token_name(lexeme.token));

		parse_expr(true);

		append_node(ast::raw::Type::Case, 2);
	}

	void parse_where() noexcept
	{
		ASSERT_OR_IGNORE(m_scanner.peek().token == Token::KwdWhere);

		m_scanner.skip();

		u16 child_count = 0;

		Lexeme lexeme = m_scanner.peek();

		while (true)
		{
			if (child_count == UINT16_MAX)
				m_error.log(m_scanner.peek().offset, "Number of definitions in where clause exceeds the supported maximum of %u\n", UINT16_MAX);

			child_count += 1;

			parse_definition(true, false);

			lexeme = m_scanner.peek();

			if (lexeme.token != Token::Comma)
				break;

			m_scanner.skip();
		}

		append_node(ast::raw::Type::Where, child_count);
	}

	void parse_expects() noexcept
	{
		ASSERT_OR_IGNORE(m_scanner.peek().token == Token::KwdExpects);

		u16 child_count = 0;

		m_scanner.skip();

		Lexeme lexeme = m_scanner.peek();

		while (true)
		{
			if (child_count == UINT16_MAX)
				m_error.log(m_scanner.peek().offset, "Number of expressions in expects clause exceeds the supported maximum of %u\n", UINT16_MAX);

			child_count += 1;

			parse_expr(false);

			lexeme = m_scanner.peek();

			if (lexeme.token != Token::Comma)
				break;

			m_scanner.skip();
		}

		append_node(ast::raw::Type::Expects, child_count);
	}

	void parse_ensures() noexcept
	{
		ASSERT_OR_IGNORE(m_scanner.peek().token == Token::KwdEnsures);

		u16 child_count = 0;

		m_scanner.skip();

		Lexeme lexeme = m_scanner.peek();

		while (true)
		{
			if (child_count == UINT16_MAX)
				m_error.log(m_scanner.peek().offset, "Number of expressions in ensures clause exceeds the supported maximum of %u\n", UINT16_MAX);

			child_count += 1;

			parse_expr(false);

			lexeme = m_scanner.peek();

			if (lexeme.token != Token::Comma)
				break;

			m_scanner.skip();
		}

		append_node(ast::raw::Type::Ensures, child_count);
	}

	void parse_func() noexcept
	{
		u16 child_count = 0;

		ast::raw::Flag flags = ast::raw::Flag::EMPTY;

		Lexeme lexeme = m_scanner.next();

		if (lexeme.token == Token::KwdProc)
			flags |= ast::raw::Flag::Func_IsProc;
		else if (lexeme.token != Token::KwdFunc)
			m_error.log(lexeme.offset, "Expected '%s' or '%s' but got '%s'\n", token_name(Token::KwdFunc), token_name(Token::KwdProc), token_name(lexeme.token));

		lexeme = m_scanner.next();

		if (lexeme.token != Token::ParenL)
			m_error.log(lexeme.offset, "Expected '%s' after '%s' but got '%s'\n", token_name(Token::ParenL), token_name(flags == ast::raw::Flag::Func_IsProc ? Token::KwdProc : Token::KwdFunc), token_name(lexeme.token));

		lexeme = m_scanner.peek();

		while (lexeme.token != Token::ParenR)
		{
			if (child_count == UINT16_MAX)
				m_error.log(lexeme.offset, "Number of parameters in function parameter list exceeds the supported maximum of %u\n", UINT16_MAX);
			
			child_count += 1;

			parse_definition(true, true);

			lexeme = m_scanner.peek();

			if (lexeme.token == Token::Comma)
				m_scanner.skip();
			else if (lexeme.token != Token::ParenR)
				m_error.log(lexeme.offset, "Expected '%s' or '%s' after function parameter definition but got '%s'", token_name(Token::Comma), token_name(Token::ParenR), token_name(lexeme.token));
		}

		m_scanner.skip();

		lexeme = m_scanner.peek();

		if (lexeme.token == Token::ThinArrowR)
		{
			child_count += 1;

			flags |= ast::raw::Flag::Func_HasReturnType;

			m_scanner.skip();

			parse_expr(false);

			lexeme = m_scanner.peek();
		}

		if (lexeme.token == Token::KwdExpects)
		{
			child_count += 1;

			flags |= ast::raw::Flag::Func_HasExpects;

			parse_expects();

			lexeme = m_scanner.peek();
		}

		if (lexeme.token == Token::KwdEnsures)
		{
			child_count += 1;

			flags |= ast::raw::Flag::Func_HasEnsures;

			parse_ensures();

			lexeme = m_scanner.peek();
		}

		if (lexeme.token == Token::OpSet)
		{
			child_count += 1;

			flags |= ast::raw::Flag::Func_HasBody;

			m_scanner.skip();

			parse_expr(true);
		}

		append_node(ast::raw::Type::Func, child_count, flags);
	}

	void parse_trait() noexcept
	{
		ASSERT_OR_IGNORE(m_scanner.peek().token == Token::KwdTrait);

		u16 child_count = 1;

		ast::raw::Flag flags = ast::raw::Flag::EMPTY;

		m_scanner.skip();

		Lexeme lexeme = m_scanner.next();

		if (lexeme.token != Token::ParenL)
			m_error.log(lexeme.offset, "Expected '%s' after '%s' but got '%s'\n", token_name(Token::ParenL), token_name(Token::KwdTrait), token_name(lexeme.token));

		lexeme = m_scanner.peek();

		while (lexeme.token != Token::ParenR)
		{
			if (child_count == UINT16_MAX)
				m_error.log(lexeme.offset, "Number of parameters in trait parameter list exceeds the supported maximum of %u\n", UINT16_MAX);
			
			child_count += 1;

			parse_definition(true, true);

			lexeme = m_scanner.next();

			if (lexeme.token == Token::Comma)
				lexeme = m_scanner.peek();
			else if (lexeme.token != Token::ParenR)
				m_error.log(lexeme.offset, "Expected '%s' or '%s' after trait parameter definition but got '%s'", token_name(Token::Comma), token_name(Token::ParenR), token_name(lexeme.token));
		}

		lexeme = m_scanner.peek();

		if (lexeme.token == Token::KwdExpects)
		{
			child_count += 1;

			flags |= ast::raw::Flag::Trait_HasExpects;

			parse_expects();

			lexeme = m_scanner.peek();
		}

		if (lexeme.token != Token::OpSet)
		{
			if ((flags & ast::raw::Flag::Trait_HasExpects) == ast::raw::Flag::EMPTY)
				m_error.log(lexeme.offset, "Expected '%s' or '%s' after trait parameter list but got '%s'\n", token_name(Token::OpSet), token_name(Token::KwdExpects), token_name(lexeme.token));
			else
				m_error.log(lexeme.offset, "Expected '%s' after trait expects clause but got '%s'\n", token_name(Token::OpSet), token_name(lexeme.token));
		}

		m_scanner.skip();

		parse_expr(true);

		append_node(ast::raw::Type::Trait, child_count, flags);
	}

	void parse_impl() noexcept
	{
		ASSERT_OR_IGNORE(m_scanner.peek().token == Token::KwdImpl);

		u16 child_count = 2;

		ast::raw::Flag flags = ast::raw::Flag::EMPTY;

		m_scanner.skip();

		parse_expr(false);

		Lexeme lexeme = m_scanner.peek();

		if (lexeme.token == Token::KwdExpects)
		{
			child_count += 1;

			flags |= ast::raw::Flag::Impl_HasExpects;

			parse_expects();

			lexeme = m_scanner.peek();
		}

		if (lexeme.token != Token::OpSet)
		{
			if ((flags & ast::raw::Flag::Trait_HasExpects) == ast::raw::Flag::EMPTY)
				m_error.log(lexeme.offset, "Expected '%s' or '%s' after trait parameter list but got '%s'\n", token_name(Token::OpSet), token_name(Token::KwdExpects), token_name(lexeme.token));
			else
				m_error.log(lexeme.offset, "Expected '%s' after trait expects clause but got '%s'\n", token_name(Token::OpSet), token_name(lexeme.token));
		}

		m_scanner.skip();

		parse_expr(true);

		append_node(ast::raw::Type::Impl, child_count, flags);
	}

	void parse_definition(bool is_implicit, bool is_optional_value) noexcept
	{
		u16 child_count = 0;

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
				else if (lexeme.token == Token::KwdAuto)
				{
					if ((flags & ast::raw::Flag::Definition_IsAuto) != ast::raw::Flag::EMPTY)
						m_error.log(lexeme.offset, "Definition modifier 'auto' encountered more than once\n");

					flags |= ast::raw::Flag::Definition_IsAuto;
				}
				else if (lexeme.token == Token::KwdUse)
				{
					if ((flags & ast::raw::Flag::Definition_IsUse) != ast::raw::Flag::EMPTY)
						m_error.log(lexeme.offset, "Definition modifier 'use' encountered more than once\n");

					flags |= ast::raw::Flag::Definition_IsUse;
				}
				else
				{
					break;
				}

				lexeme = m_scanner.next();
			}

			if (flags == ast::raw::Flag::EMPTY && !is_implicit)
				m_error.log(lexeme.offset, "Missing 'let' or at least one of 'pub', 'mut' or 'global' at start of definition\n");
		}

		if (lexeme.token != Token::Ident)
			m_error.log(lexeme.offset, "Expected 'Identifier' after Definition modifiers but got '%s'\n", token_name(lexeme.token));

		const u32 identifier_id = static_cast<u32>(lexeme.integer_value);

		lexeme = m_scanner.peek();

		if (lexeme.token == Token::Colon)
		{
			child_count += 1;

			flags |= ast::raw::Flag::Definition_HasType;

			m_scanner.skip();

			parse_expr(false);

			lexeme = m_scanner.peek();
		}
		
		if (lexeme.token == Token::OpSet)
		{
			child_count += 1;

			m_scanner.skip();

			parse_expr(true);
		}
		else if (!is_optional_value)
		{
			m_error.log(lexeme.offset, "Expected '=' after Definition identifier and type, but got '%s'\n", token_name(lexeme.token));
		}

		ast::raw::Node* const header = append_node(ast::raw::Type::Definition, child_count, flags, 1);

		*reinterpret_cast<u32*>(header + 1) = identifier_id;
	}

public:

	Parser() noexcept :
		m_identifiers{ 1 << 24, 1 << 14, 1 << 28, 1 << 16, 1 << 16 },
		m_scanner{ &m_identifiers, &m_error },
		m_asts{ 1ui64 << 31, 1ui64 << 17 },
		m_ast_scratch{ 1ui64 << 31, 1ui64 << 17 },
		m_stack_scratch{ 1ui64 << 31, 1ui64 << 17 },
		m_error{}
	{
		for (u32 i = 0; i != array_count(KEYWORDS); ++i)
			m_identifiers.value_from(KEYWORDS[i].range(), fnv1a(KEYWORDS[i].as_byte_range()))->set_token(KEYWORDS[i].attachment());
	}

	u32 index_from_string(Range<char8> string) noexcept
	{
		return m_identifiers.index_from(string, fnv1a(string.as_byte_range()));
	}

	ast::raw::Tree parse(SourceFile source) noexcept
	{
		const IdentifierMapEntry* filepath = m_identifiers.value_from(source.filepath_id());

		m_error.prime(Range<char8>{ filepath->m_chars, filepath->m_length }, source.content());

		m_scanner.prime(source);

		u16 child_count = 0;

		while (true)
		{
			const Lexeme lexeme = m_scanner.peek();

			if (lexeme.token == Token::END_OF_SOURCE)
				break;

			if (child_count == UINT16_MAX)
				m_error.log(m_scanner.peek().offset, "Number of top-level definitions exceeds the supported maximum of %u\n", UINT16_MAX);

			child_count += 1;
			
			parse_top_level_expr(false);
		};

		append_node(ast::raw::Type::Program, child_count);

		ASSERT_OR_IGNORE(m_stack_scratch.used() == 1);

		const u32 tree_offset = m_asts.used();

		reverse_node(&m_asts, reinterpret_cast<ast::raw::Node*>(m_ast_scratch.begin() + *m_stack_scratch.begin()));

		m_ast_scratch.reset();

		m_stack_scratch.reset();

		return ast::raw::Tree{ reinterpret_cast<ast::raw::Node*>(m_asts.begin() + tree_offset), m_asts.used() - tree_offset };
	}

	const IdentifierMap* identifiers() const noexcept
	{
		return &m_identifiers;
	}
};

#endif // PARSER_INCLUDE_GUARD
