#include "config.hpp"

#include "../infra/minos.hpp"
#include "../infra/container.hpp"
#include "error.hpp"

#include <type_traits>
#include <cstdio>
#include <cstdarg>

struct ConfigHeader;

struct ConfigContainer
{
	Range<ConfigHeader> children;
};

struct ConfigInteger
{
	s64 min;

	s64 max;
};

struct ConfigHeader
{
	enum class Type
	{
		INVALID,
		Container,
		Integer,
		String,
		Boolean,
		Path,
	} type;

	u32 target_offset;

	const char8* name;

	const char8* helptext;

	union
	{
		ConfigContainer container;

		ConfigInteger integer;
	};

	constexpr ConfigHeader(Range<ConfigHeader> children, const char8* name, const char8* helptext) noexcept :
		type{ Type::Container },
		target_offset{ 0 },
		name{ name },
		helptext{helptext },
		container{ children }
	{}

	constexpr ConfigHeader(u64 target_offset, s64 min, s64 max, const char8* name, const char8* helptext) noexcept :
		type{ Type::Integer },
		target_offset{ static_cast<u32>(target_offset) },
		name{ name },
		helptext{ helptext },
		integer{ min, max }
	{}

	constexpr ConfigHeader(Type type, u64 target_offset, const char8* name, const char8* helptext) noexcept :
		type{ type },
		target_offset{ static_cast<u32>(target_offset) },
		name{ name },
		helptext{ helptext },
		container{}
	{}

	static constexpr ConfigHeader make_container(Range<ConfigHeader> children, const char8* name, const char8* helptext) noexcept
	{
		return { children, name, helptext };
	}

	static constexpr ConfigHeader make_integer(u32 target_offset, s64 min, s64 max, const char8* name, const char8* helptext) noexcept
	{
		return { target_offset, min, max, name, helptext };
	}

	static constexpr ConfigHeader make_string(u32 target_offset, const char8* name, const char8* helptext) noexcept
	{
		return { Type::String, target_offset, name, helptext };
	}

	static constexpr ConfigHeader make_path(u32 target_offset, const char8* name, const char8* helptext) noexcept
	{
		return { Type::Path, target_offset, name, helptext };
	}

	static constexpr ConfigHeader make_boolean(u32 target_offset, const char8* name, const char8* helptext) noexcept
	{
		return { Type::Boolean, target_offset, name, helptext };
	}
};

static constexpr ConfigHeader CONFIG_ENTRYPOINT[] = {
	ConfigHeader::make_path(offsetof(Config, entrypoint.filepath), "filepath", "Relative path of the source file containing the program's entrypoint"),
	ConfigHeader::make_string(offsetof(Config, entrypoint.symbol), "symbol", "Symbol name of the program's entrypoint function"),
};

static constexpr ConfigHeader CONFIG_STD[] = {
	ConfigHeader::make_path(offsetof(Config, std.filepath), "filepath", "Path to the file containing standard library source"),
};

static constexpr ConfigHeader CONFIG_ROOTS[] = {
	ConfigHeader::make_container(Range<ConfigHeader>{ CONFIG_ENTRYPOINT }, "entrypoint", "Entrypoint information"),
	ConfigHeader::make_container(Range<ConfigHeader>{ CONFIG_STD }, "std", "Standard library configuration"),
};

static constexpr ConfigHeader CONFIG = ConfigHeader::make_container(Range<ConfigHeader>{ CONFIG_ROOTS }, "config", "");


struct CodepointBuffer
{
	char8 buf[4];

	u8 length;
};

struct ConfigToken
{
	enum class Type
	{
		EMPTY = 0,
		End,
		Identity,
		Dot,
		Set,
		Comma,
		CurlyBeg,
		CurlyEnd,
		BracketBeg,
		BracketEnd,
		DoubleBracketBeg,
		DoubleBracketEnd,
		Integer,
		String,
		MultilineString,
		LiteralString,
		MultilineLiteralString,
	} type;

	Range<char8> content;
};

struct ConfigParser
{
	static constexpr u32 HEAP_RESERVE = 262144;

	static constexpr u32 HEAP_COMMIT_INCREMENT = 16384;

private:

	ReservedVec<byte> m_heap;

	Range<char8> m_content;

	const char8* m_curr;

	ConfigToken m_peek;

	Config* m_out;

	u32 m_context_top;

	const Range<char8> m_filepath;

	const ConfigHeader* m_context_stack[8];

	Range<char8> m_path_base;



	NORETURN void error(u64 offset, const char8* format, ...) const noexcept
	{
		va_list args;

		va_start(args, format);

		vsource_error(offset, m_content, m_filepath, format, args);
	}

	static bool name_equal(Range<char8> text, const char8* name) noexcept
	{
		for (u64 i = 0; i != text.count(); ++i)
		{
			if (text[i] != name[i])
				return false;
		}

		return true;
	}

	static bool is_alpha(char8 c) noexcept
	{
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
	}

	static bool is_bin_digit(char8 c) noexcept
	{
		return c == '0' || c == '1';
	}

	static bool is_oct_digit(char8 c) noexcept
	{
		return c >= '0' && c <= '7';
	}

	static bool is_dec_digit(char8 c) noexcept
	{
		return c >= '0' && c <= '9';
	}

	static bool is_hex_digit(char8 c) noexcept
	{
		return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
	}

	void skip_whitespace() noexcept
	{
		while (true)
		{
			if (*m_curr == '#')
			{
				m_curr += 1;

				while (*m_curr != '\0' && *m_curr != '\n')
					m_curr += 1;
			}
			else if (*m_curr != ' ' && *m_curr != '\t' && *m_curr != '\r' && *m_curr != '\n')
			{
				break;
			}

			m_curr += 1;
		}
	}

	[[nodiscard]] ConfigToken next() noexcept
	{
		if (m_peek.type != ConfigToken::Type::EMPTY)
		{
			const ConfigToken peek = m_peek;

			m_peek.type = ConfigToken::Type::EMPTY;

			return peek;
		}
		skip_whitespace();

		const char8 first = *m_curr;

		const char8* const token_beg = m_curr;

		m_curr += 1;

		if (first == '0')
		{
			if (*m_curr == 'x')
			{
				m_curr += 1;

				while (is_hex_digit(*m_curr))
					m_curr += 1;

				if (m_curr == token_beg + 2)
					error(m_curr - m_content.begin(), "Expected at least one digit after hexadecimal prefix '0x'\n");
			}
			else if (*m_curr == 'o')
			{
				m_curr += 1;

				while (is_oct_digit(*m_curr))
					m_curr += 1;

				if (m_curr == token_beg + 2)
					error(m_curr - m_content.begin(), "Expected at least one digit after octal prefix '0o'\n");
			}
			else if (*m_curr == 'b')
			{
				m_curr += 1;

				while (is_bin_digit(*m_curr))
					m_curr += 1;

				if (m_curr == token_beg + 2)
					error(m_curr - m_content.begin(), "Expected at least one digit after binary prefix '0b'\n");
			}
			else
			{
				while (is_dec_digit(*m_curr))
					m_curr += 1;
			}
		}
		else if (is_dec_digit(first))
		{
			while (is_dec_digit(*m_curr))
				m_curr += 1;

			if (is_dec_digit(*m_curr) || is_alpha(*m_curr))
				error(m_curr - m_content.begin(), "Unexpected character '%c' in number\n", *m_curr);

			return { ConfigToken::Type::Integer, { token_beg, m_curr } };
		}
		else if (is_alpha(first))
		{
			m_curr += 1;

			while (is_alpha(*m_curr) || is_dec_digit(*m_curr) || *m_curr == '_' || *m_curr == '-')
				m_curr += 1;

			return { ConfigToken::Type::Identity, { token_beg, m_curr } };
		}
		else if (first < ' ')
		{
			if (first != '\0' || m_curr != m_content.end())
				error(m_curr - m_content.begin(), "Unexpected control character U+%02X in config file\n", first);

			return { ConfigToken::Type::End, {} };
		}
		else switch (first)
		{
		case '\'':
			if (*m_curr == '\'' && m_curr[1] == '\'')
			{
				m_curr += 2;

				while (true)
				{
					if (*m_curr == '\'' && m_curr[1] == '\'' && m_curr[2] == '\'')
					{
						m_curr += 3;

						break;
					}
					else if (*m_curr == '\0')
					{
						error(token_beg - m_content.begin(), "String not ended before end of file\n");
					}

					m_curr += 1;
				}

				return { ConfigToken::Type::MultilineLiteralString, { token_beg, m_curr } };
			}
			else
			{
				while (true)
				{
					if (*m_curr == '\'')
					{
						m_curr += 1;

						break;
					}
					else if (*m_curr == '\0' || *m_curr == '\r' || *m_curr == '\n')
					{
						error(token_beg - m_content.begin(), "Single-line string not ended before end of line\n");
					}

					m_curr += 1;
				}

				return { ConfigToken::Type::LiteralString, { token_beg, m_curr } };
			}

		case '"':
			if (*m_curr == '"' && m_curr[1] == '"')
			{
				m_curr += 2;

				while (true)
				{
					if (*m_curr == '"' && m_curr[1] == '"' && m_curr[2] == '"')
					{
						m_curr += 3;

						break;
					}
					else if (*m_curr == '\0')
					{
						error(token_beg - m_content.begin(), "String not ended before end of file\n");
					}
					else if (*m_curr == '\\')
					{
						m_curr += 1;
					}

					m_curr += 1;
				}

				return { ConfigToken::Type::MultilineString, { token_beg, m_curr } };
			}
			else
			{
				while (true)
				{
					if (*m_curr == '"')
					{
						m_curr += 1;

						break;
					}
					else if (*m_curr == '\0' || *m_curr == '\r' || *m_curr == '\n')
					{
						error(token_beg - m_content.begin(), "Single-line string not ended before end of line\n");
					}
					else if (*m_curr == '\\')
					{
						m_curr += 1;
					}

					m_curr += 1;
				}

				return { ConfigToken::Type::String, { token_beg, m_curr } };
			}

		case '.':
			return {  ConfigToken::Type::Dot, { token_beg, m_curr } };

		case '=':
			return {  ConfigToken::Type::Set, { token_beg, m_curr } };

		case '[':
			if (*m_curr == '[')
			{
				m_curr += 1;

				return { ConfigToken::Type::DoubleBracketBeg, { token_beg, m_curr } };
			}
			else
			{
				return { ConfigToken::Type::BracketBeg, { token_beg, m_curr } };
			}

		case ']':
			if (m_curr[1] == ']')
			{
				m_curr += 1;

				return { ConfigToken::Type::DoubleBracketEnd, { token_beg, m_curr } };
			}
			else
			{
				return { ConfigToken::Type::BracketEnd, { token_beg, m_curr } };
			}

		case '{':
			return { ConfigToken::Type::CurlyBeg, { token_beg, m_curr } };

		case '}':
			return { ConfigToken::Type::CurlyEnd, { token_beg, m_curr } };

		case ',':
			return { ConfigToken::Type::Comma, {token_beg, m_curr } };

		default:
			error(token_beg - m_content.begin(), "Unexpected character '%c' (U+%02X)\n", first, first);
		}

		ASSERT_UNREACHABLE;
	}

	[[nodiscard]] ConfigToken peek() noexcept
	{
		if (m_peek.type != ConfigToken::Type::EMPTY)
			return m_peek;

		m_peek = next();

		return m_peek;
	}

	void skip() noexcept
	{
		(void) next();
	}

	void parse_name_element(const ConfigToken& token) noexcept
	{
		ASSERT_OR_IGNORE(token.type == ConfigToken::Type::Identity);

		if (m_context_top == array_count(m_context_stack))
			error(token.content.begin() - m_content.begin(), "Key nesting limit exceeded\n");

		ASSERT_OR_IGNORE(m_context_top != 0);

		const ConfigHeader* const context = m_context_stack[m_context_top - 1];

		if (context->type != ConfigHeader::Type::Container)
			error(token.content.begin() - m_content.begin(), "Tried assigning to key '%.*s' that does not expect subkeys\n", static_cast<u32>(token.content.count()), token.content.begin());

		for (const ConfigHeader& child : context->container.children)
		{
			if (name_equal(token.content, child.name))
			{
				m_context_stack[m_context_top] = &child;

				m_context_top += 1;

				return;
			}
		}

		error(token.content.begin() - m_content.begin(), "Key '%.*s' does not exist in '%s'\n", static_cast<u32>(token.content.count()), token.content.begin(), context->name);
	}

	void pop_names(u32 count) noexcept
	{
		ASSERT_OR_IGNORE(m_context_top > count);

		m_context_top -= count;
	}

	u32 parse_names() noexcept
	{
		u32 name_count = 1;

		while (true)
		{
			const ConfigToken identity = next();

			if (identity.type != ConfigToken::Type::Identity)
				error(identity.content.begin() - m_content.begin(), "Expcted key but got '%.*s'\n", static_cast<u32>(identity.content.count()), identity.content.begin());

			parse_name_element(identity);

			const ConfigToken next = peek();

			if (next.type != ConfigToken::Type::Dot)
				return name_count;

			skip();

			name_count += 1;
		}
	}

	void parse_value() noexcept
	{
		const ConfigToken token = peek();

		switch (token.type)
		{
		case ConfigToken::Type::BracketBeg:
			error(token.content.begin() - m_content.begin(), "Arrays are currently not supported");

		case ConfigToken::Type::CurlyBeg:
			return parse_inline_table();

		case ConfigToken::Type::Identity:
			return parse_boolean();

		case ConfigToken::Type::Integer:
			return parse_integer();

		case ConfigToken::Type::String:
			return parse_string();

		case ConfigToken::Type::LiteralString:
			return parse_literal_string();

		case ConfigToken::Type::MultilineString:
			return parse_multiline_string();

		case ConfigToken::Type::MultilineLiteralString:
			return parse_multiline_literal_string();

		default:
			error(token.content.begin() - m_content.begin(), "Expected a value but got '%.*s'\n", static_cast<u32>(token.content.count()), token.content.begin());
		}
	}

	void parse_inline_table() noexcept
	{
		ASSERT_OR_IGNORE(peek().type == ConfigToken::Type::CurlyBeg);

		skip();

		ConfigToken token = peek();

		// Empty inline table is a special case
		if (token.type == ConfigToken::Type::CurlyEnd)
		{
			skip();
			
			return;
		}

		while (true)
		{
			const u32 name_depth = parse_names();

			token = peek();

			if (token.type != ConfigToken::Type::Set)
				error(token.content.begin() - m_content.begin(), "Expected '=' but got '%.*s'\n", static_cast<u32>(token.content.count()), token.content.begin());

			parse_value();

			pop_names(name_depth);

			token = next();

			if (token.type == ConfigToken::Type::CurlyEnd)
				return;
			else if (token.type != ConfigToken::Type::Comma)
				error(token.content.begin() - m_content.begin(), "Expected '}' or ',' but got '%.*s'\n", static_cast<u32>(token.content.count()), token.content.begin());
		}
	}

	void parse_boolean() noexcept
	{
		const ConfigToken token = next();

		ASSERT_OR_IGNORE(m_context_top != 0);

		const ConfigHeader* const context = m_context_stack[m_context_top - 1];

		bool value;

		if (name_equal(token.content, "true"))
			value = true;
		else if (name_equal(token.content, "false"))
			value = false;
		else
			error(token.content.begin() - m_content.begin(), "Expected a value but got '%.*s'\n", static_cast<u32>(token.content.count()), token.content.begin());

		if (context->type != ConfigHeader::Type::Boolean)
			error(token.content.begin() - m_content.begin(), "Cannot assign boolean to key '%s' expecting different value\n", context->name);

		*reinterpret_cast<bool*>(reinterpret_cast<byte*>(m_out) + context->target_offset) = value;
	}

	void parse_integer() noexcept
	{
		const ConfigToken token = next();

		ASSERT_OR_IGNORE(m_context_top != 0);

		const ConfigHeader* const context = m_context_stack[m_context_top - 1];

		if (context->type != ConfigHeader::Type::Integer)
			error(token.content.begin() - m_content.begin(), "Cannot assign integer to key '%s' expecting different value\n", context->name);

		const Range<char8> text = token.content;

		s64 value = 0;

		ASSERT_OR_IGNORE(text.count() != 0);

		if (text[0] == '0')
		{
			ASSERT_OR_IGNORE(text.count() != 2);

			if (text.count() == 1)
			{
				// Nothing to do; value is already 0
			}
			else if (text[1] == 'x')
			{
				for (u64 i = 2; i != text.count(); ++i)
				{
					const char8 c = text[i];

					if (c >= '0' && c <= '9')
						value = value * 16 + c - '0';
					else if (c >= 'a' && c <= 'f')
						value = value * 16 + c - 'a' + 10;
					else if (c >= 'A' && c <= 'F')
						value = value * 16 + c - 'A' + 10;
					else
						ASSERT_UNREACHABLE;
				}
			}
			else if (text[1] == 'o')
			{
				for (u64 i = 2; i != text.count(); ++i)
				{
					const char8 c = text[i];

					ASSERT_OR_IGNORE(c >= '0' && c <= '7');

					value = value * 8 + c - '0';
				}
			}
			else // if (text[1] == 'b')
			{
				ASSERT_OR_IGNORE(text[1] == 'b');
				
				for (u64 i = 2; i != text.count(); ++i)
				{
					const char8 c = text[i];

					ASSERT_OR_IGNORE(c == '0' || c == '1');

					value = value * 2 + c - '0';
				}
			}
		}
		else
		{
			for (const char8 c : text)
			{
				ASSERT_OR_IGNORE(c >= '0' && c <= '9');

				value = value * 10 + c - '0';
			}
		}

		*reinterpret_cast<s64*>(reinterpret_cast<byte*>(m_out) + context->target_offset) = value;
	}

	void parse_string() noexcept
	{
		const ConfigToken token = next();

		ASSERT_OR_IGNORE(token.content.count() >= 2);

		parse_escaped_string_base(Range{ token.content.begin() + 1, token.content.end() - 1 });
	}

	void parse_literal_string() noexcept
	{
		const ConfigToken token = next();

		ASSERT_OR_IGNORE(token.content.count() >= 2);

		parse_literal_string_base(Range{ token.content.begin() + 1, token.content.end() - 1});
	}

	void parse_multiline_string() noexcept
	{
		const ConfigToken token = next();

		ASSERT_OR_IGNORE(token.content.count() >= 6);

		parse_escaped_string_base(Range{ token.content.begin() + 3, token.content.end() - 3 });
	}

	void parse_multiline_literal_string() noexcept
	{
		const ConfigToken token = next();

		ASSERT_OR_IGNORE(token.content.count() >= 6);

		parse_literal_string_base(Range{ token.content.begin() + 3, token.content.end() - 3});
	}

	void parse_escaped_string_base(Range<char8> string) noexcept
	{
		ASSERT_OR_IGNORE(m_context_top != 0);

		const ConfigHeader* const context = m_context_stack[m_context_top - 1];

		if (context->type != ConfigHeader::Type::String && context->type != ConfigHeader::Type::Path)
			error(string.begin() - m_content.begin(), "Cannot assign string to key '%s' expecting different value\n", context->name);

		if (string[0] == '\n')
			string = Range{ string.begin() + 1, string.end() };
		else if (string[0] == '\r' && string[1] == '\n')
			string = Range{ string.begin() + 2, string.end() };

		void* const allocation_begin = m_heap.begin() + m_heap.used();

		u32 uncopied_begin = 0;

		u32 i = 0;

		while (i < static_cast<u32>(string.count()))
		{
			if (string[i] == '\\')
			{
				CodepointBuffer utf8;

				const u32 escape_chars = parse_escape_sequence(Range{ string.begin() + i, string.end() }, &utf8);

				const u32 uncopied_length = i - uncopied_begin;

				m_heap.append_exact(string.begin() + uncopied_begin, uncopied_length);

				m_heap.append_exact(utf8.buf, utf8.length);

				i += escape_chars;

				uncopied_begin = i;
			}
			else
			{
				i += 1;
			}
		}

		ASSERT_OR_IGNORE(i == string.count());

		const u32 uncopied_length = i - uncopied_begin;

		m_heap.append_exact(string.begin() + uncopied_begin, uncopied_length);

		Range<char8> value{ static_cast<const char8*>(allocation_begin), reinterpret_cast<const char8*>(m_heap.begin()) + m_heap.used() };

		if (context->type == ConfigHeader::Type::Path)
		{
			char8 path_buf[minos::MAX_PATH_CHARS];

			const u32 path_chars = minos::path_to_absolute_relative_to(value, m_path_base, MutRange{ path_buf });

			if (path_chars == 0 || path_chars > array_count(path_buf))
				error(string.begin() - m_content.begin(), "Resulting absolute path exceeds maximum of %u characters\n", minos::MAX_PATH_CHARS);

			m_heap.pop_by(static_cast<u32>(value.count()));

			value = { value.begin(), path_chars };

			m_heap.append_exact(path_buf, path_chars);
		}

		*reinterpret_cast<Range<char8>*>(reinterpret_cast<byte*>(m_out) + context->target_offset) = value;
	}

	void parse_literal_string_base(Range<char8> string) noexcept
	{
		ASSERT_OR_IGNORE(m_context_top != 0);

		const ConfigHeader* const context = m_context_stack[m_context_top - 1];

		if (context->type != ConfigHeader::Type::String && context->type != ConfigHeader::Type::Path)
			error(string.begin() - m_content.begin(), "Cannot assign string to key '%s' expecting different value\n", context->name);

		if (string[0] == '\n')
			string = Range{ string.begin() + 1, string.end() };
		else if (string[0] == '\r' && string[1] == '\n')
			string = Range{ string.begin() + 2, string.end() };

		Range<char8> value;

		if (context->type == ConfigHeader::Type::Path)
		{
			char8 path_buf[minos::MAX_PATH_CHARS];

			const u32 path_chars = minos::path_to_absolute_relative_to(string, m_path_base, MutRange{ path_buf });

			if (path_chars == 0 || path_chars > array_count(path_buf))
				error(string.begin() - m_content.begin(), "Resulting absolute path exceeds maximum of %u characters\n", minos::MAX_PATH_CHARS);

			value = { reinterpret_cast<const char8*>(m_heap.begin() + m_heap.used()), path_chars };

			m_heap.append_exact(path_buf, path_chars);
		}
		else
		{
			void* const allocation_begin = m_heap.begin() + m_heap.used();

			m_heap.append_exact(string.begin(), static_cast<u32>(string.count()));
	
			value = { static_cast<const char8*>(allocation_begin), static_cast<u32>(string.count()) };
		}

		*reinterpret_cast<Range<char8>*>(reinterpret_cast<byte*>(m_out) + context->target_offset) = value;
	}

	u32 parse_escape_sequence(Range<char8> text, CodepointBuffer* out) noexcept
	{
		ASSERT_OR_IGNORE(text.count() >= 2);

		ASSERT_OR_IGNORE(text[0] == '\\');

		out->length = 1;

		switch (text[1])
		{
		case 'b':
			out->buf[0] = '\b';
			return 2;

		case 't':
			out->buf[0] = '\t';
			return 2;

		case 'n':
			out->buf[0] = '\n';
			return 2;

		case 'f':
			out->buf[0] = '\f';
			return 2;

		case 'r':
			out->buf[0] = '\r';
			return 2;

		case '"':
			out->buf[0] = '"';
			return 2;

		case '\\':
			out->buf[0] = '\\';
			return 2;

		case 'u':
			parse_unicode_escape_sequence(Range{ text.begin() + 2, text.end() }, 4, out);

			return 6;

		case 'U':
			parse_unicode_escape_sequence(Range{ text.begin() + 2, text.end() }, 8, out);

			return 10;

		case ' ':
		case '\r':
		case '\n':
		case '\t':
		{
			out->length = 0;

			bool has_newline = false;

			u32 i = 1;

			while (i < text.count())
			{
				if (text[i] == '\n')
					has_newline = true;
				else if (text[i] != ' ' && text[i] != '\t' && text[i] != '\r')
					break; // Just do default processing; We're not at the end of a line

				i += 1;
			}

			if (has_newline)
				return i;
		}
		
		// FALLTHROUGH

		default:
			error(text.begin() - m_content.begin(), "Unexpected escape sequence '\\%c'\n", text[1]); 
		}
	}

	void parse_unicode_escape_sequence(Range<char8> text, u32 escape_chars, CodepointBuffer* out) noexcept
	{
		if (text.count() < escape_chars)
			error(text.begin() - m_content.begin(), escape_chars == 4 ? "\\u escape expects four hex digits but got %llu" : "\\U escape expects eight hex digits but got %llu\n", text.count());

		u32 utf32 = 0;

		for (u32 i = 0; i != escape_chars; ++i)
		{
			const char8 c = text[i];

			if (c >= '0' && c <= '9')
				utf32 = utf32 * 16 + c - '0';
			else if (c >= 'a' && c <= 'f')
				utf32 = utf32 * 16 + c - 'a' + 10;
			else if (c >= 'A' && c <= 'F')
				utf32 = utf32 * 16 + c - 'A' + 10;
			else
				error(text.begin() + i - m_content.begin(), "Expected hexadecimal escape character but got '%c'\n", c);
		}

		if (utf32 <= 0x7F)
		{
			out->buf[0] = static_cast<char8>(utf32);

			out->length = 1;
		}
		else if (utf32 <= 0x7FF)
		{
			out->buf[0] = static_cast<char8>(0xC0 | (utf32 >> 6));
			out->buf[1] = static_cast<char8>(0x80 | (utf32 & 0x3F));

			out->length = 2;
		}
		else if (utf32 <= 0xFFFF)
		{
			out->buf[0] = static_cast<char8>(0xE0 | (utf32 >> 12));
			out->buf[1] = static_cast<char8>(0x80 | ((utf32 >> 6) & 0x3F));
			out->buf[2] = static_cast<char8>(0x80 | (utf32 & 0x3F));

			out->length = 3;
		}
		else if (utf32 <= 0x10FFFF)
		{
			out->buf[0] = static_cast<char8>(0xF0 | (utf32 >> 18));
			out->buf[1] = static_cast<char8>(0x80 | ((utf32 >> 12) & 0x3F));
			out->buf[2] = static_cast<char8>(0x80 | ((utf32 >> 6) & 0x3F));
			out->buf[3] = static_cast<char8>(0x80 | (utf32 & 0x3F));

			out->length = 4;
		}
		else
		{
			error(text.begin() - m_content.begin(), "Escaped codepoint is larger than the maximum unicode codepoint (0x10FFFF)");
		}
	}

public:

	ConfigParser(Range<char8> filepath) noexcept :
		m_content{},
		m_peek{},
		m_out{},
		m_context_top{ 1 },
		m_filepath{ filepath },
		m_context_stack{ &CONFIG }
	{
		m_heap.init(HEAP_RESERVE, HEAP_COMMIT_INCREMENT);

		minos::FileHandle filehandle;

		if (!minos::file_create(filepath, minos::Access::Read, minos::ExistsMode::Open, minos::NewMode::Fail, minos::AccessPattern::Sequential, nullptr, false, &filehandle))
			panic("Could not open config file '%.*s' (0x%X)\n", static_cast<u32>(filepath.count()), filepath.begin(), minos::last_error());

		minos::FileInfo fileinfo;

		if (!minos::file_get_info(filehandle, &fileinfo))
			panic("Could not determine length of config file '%.*s' (0x%X)\n", static_cast<u32>(filepath.count()), filepath.begin(), minos::last_error());

		if (fileinfo.bytes > UINT32_MAX)
			panic("Length of config file '%.*s' (%llu bytes) exceeds the maximum size of 4GB", static_cast<u32>(filepath.count()), filepath.begin(), fileinfo.bytes);

		m_heap.reserve_exact(static_cast<u32>(fileinfo.bytes + 1));

		char8* buffer = static_cast<char8*>(minos::mem_reserve(fileinfo.bytes + 1));

		if (buffer == nullptr)
			panic("Could not reserve buffer of %" PRId64 " bytes for reading config file (0x%X)\n", fileinfo.bytes + 1, minos::last_error());

		if (!minos::mem_commit(buffer, fileinfo.bytes + 1))
			panic("Could not commit buffer of %" PRIu64 " bytes for reading config file (0x%X)\n", fileinfo.bytes + 1, minos::last_error());

		buffer[fileinfo.bytes] = '\0';

		m_content = { buffer, fileinfo.bytes + 1 };

		m_curr = m_content.begin();

		minos::Overlapped overlapped{};

		if (!minos::file_read(filehandle, buffer, static_cast<u32>(fileinfo.bytes), &overlapped))
			panic("Could not read config file '%.*s' (0x%X)\n", static_cast<u32>(filepath.count()), filepath.begin(), minos::last_error());

		minos::file_close(filehandle);

		char8 path_base[minos::MAX_PATH_CHARS];

		const u32 path_base_chars = minos::path_to_absolute_directory(filepath, MutRange{ path_base });

		if (path_base_chars == 0 || path_base_chars > array_count(path_base))
			panic("Could not determine folder containing config file (0x%X)\n", minos::last_error());

		m_path_base = { reinterpret_cast<char8*>(m_heap.begin() + m_heap.used()), path_base_chars };

		m_heap.append_exact(path_base, path_base_chars);
	}

	Config parse() noexcept
	{
		Config config{};

		m_out = &config;

		config.m_heap_ptr = m_heap.begin();

		while (true)
		{
			ConfigToken token = peek();

			switch (token.type)
			{
			case ConfigToken::Type::BracketBeg:
			{
				skip();

				m_context_top = 1;

				(void) parse_names();

				token = next();

				if (token.type != ConfigToken::Type::BracketEnd)
					error(token.content.begin() - m_content.begin(), "Expected ']' but got %.*s\n", static_cast<u32>(token.content.count()), token.content.begin());

				break;
			}

			case ConfigToken::Type::Identity:
			{
				const u32 name_depth = parse_names();

				token = next();

				if (token.type != ConfigToken::Type::Set)
					error(token.content.begin() - m_content.begin(), "Expected '=' or '.' but got '%.*s'\n", static_cast<u32>(token.content.count()), token.content.begin());
				
				parse_value();

				pop_names(name_depth);

				break;
			}

			case ConfigToken::Type::End:
			{
				// validate_config(&config);

				return config;
			}

			case ConfigToken::Type::DoubleBracketBeg:
				error(token.content.begin() - m_content.begin(), "Arrays of Tables are not currently supported\n");

			default:
				ASSERT_UNREACHABLE;
			}
		}

		minos::mem_unreserve(const_cast<char8*>(m_content.begin()), m_content.count());
	}
};



Config read_config(Range<char8> filepath) noexcept
{
	ConfigParser parser{ filepath };

	return parser.parse();
}

void deinit_config(Config* config) noexcept
{
	ASSERT_OR_IGNORE(config->m_heap_ptr != nullptr);

	minos::mem_unreserve(config->m_heap_ptr, ConfigParser::HEAP_RESERVE);

	config->m_heap_ptr = nullptr;
}

static void print_config_node(const Config* config, const ConfigHeader* node, u32 indent) noexcept
{
	if (node->type == ConfigHeader::Type::Container)
	{
		fprintf(stdout, "%*s%s {\n", indent * 2, "", node->name);

		for (const ConfigHeader& child : node->container.children)
			print_config_node(config, &child, indent + 1);

		fprintf(stdout, "%*s}\n", indent * 2, "");
	}
	else if (node->type == ConfigHeader::Type::Integer)
	{
		const s64 value = *reinterpret_cast<const s64*>(reinterpret_cast<const byte*>(config) + node->target_offset);

		fprintf(stdout, "%*s%s = %" PRId64 "\n", indent, "", node->name, value);
	}
	else if (node->type == ConfigHeader::Type::String || node->type == ConfigHeader::Type::Path)
	{
		const Range<char8> value = *reinterpret_cast<const Range<char8>*>(reinterpret_cast<const byte*>(config) + node->target_offset);

		fprintf(stdout, "%*s%s = '%.*s'\n", indent, "", node->name, static_cast<u32>(value.count()), value.begin());
	}
	else if (node->type == ConfigHeader::Type::Boolean)
	{
		const bool value = *reinterpret_cast<const bool*>(reinterpret_cast<const byte*>(config) + node->target_offset);

		fprintf(stdout, "%*s%s = %s\n", indent, "", node->name, value ? "true" : "false");
	}
	else
	{
		ASSERT_UNREACHABLE;
	}
}

void print_config(const Config* config) noexcept
{
	for (const ConfigHeader& root : CONFIG.container.children)
		print_config_node(config, &root, 0);
}

static void print_config_help_node(const Config* defaults, const ConfigHeader* node, u32 indent, u32 max_indent) noexcept
{
	if (node->type == ConfigHeader::Type::Container)
	{
		fprintf(stdout, "%*s%s {\n%*s%s\n", indent * 2, "", node->name, (indent + 1) * 2, "", node->helptext);

		if (indent != max_indent)
		{
			for (const ConfigHeader& child : node->container.children)
				print_config_help_node(defaults, &child, indent + 1, max_indent);
		}

		fprintf(stdout, "%*s}\n", indent * 2, "");
	}
	else if (node->type == ConfigHeader::Type::Integer)
	{
		const s64 default_value = *reinterpret_cast<const s64*>(reinterpret_cast<const byte*>(defaults) + node->target_offset);

		fprintf(stdout, "%*s%s {\n%*s%s\n%*stype: integer\n%*sdefault: %" PRId64 "\n", indent * 2, "", node->name, (indent + 1) * 2, "", node->helptext, (indent + 1) * 2, "", (indent + 1) * 2, "", default_value);

		if (node->integer.min != INT64_MIN)
			fprintf(stdout, "%*smin: %" PRId64 "\n", (indent + 1) * 2, "", node->integer.min);

		if (node->integer.max != INT64_MAX)
			fprintf(stdout, "%*smax: %" PRId64 "\n", (indent + 1) * 2, "", node->integer.max);

		fprintf(stdout, "%*s}\n", indent * 2, "");
	}
	else if (node->type == ConfigHeader::Type::String || node->type == ConfigHeader::Type::Path)
	{
		const Range<char8> default_value = *reinterpret_cast<const Range<char8>*>(reinterpret_cast<const byte*>(defaults) + node->target_offset);

		fprintf(stdout, "%*s%s {\n%*s%s\n%*stype: %s\n%*sdefault: %.*s\n%*s}\n", indent * 2, "", node->name, (indent + 1) * 2, "", node->helptext, (indent + 1) * 2, "", node->type == ConfigHeader::Type::String ? "string" : "path", (indent + 1) * 2, "", static_cast<u32>(default_value.count()), default_value.begin(), indent * 2, "");
	}
	else if (node->type == ConfigHeader::Type::Boolean)
	{
		const bool default_value = *reinterpret_cast<const bool*>(reinterpret_cast<const byte*>(defaults) + node->target_offset);

		fprintf(stdout, "%*s%s {\n%*s%s\n%*stype: bool\n%*sdefault: %s\n%*s}\n", indent * 2, "", node->name, (indent + 1) * 2, "", node->helptext, (indent + 1) * 2, "", (indent + 1) * 2, "", default_value ? "true" : "false", indent * 2, "");
	}
	else
	{
		ASSERT_UNREACHABLE;
	}
}

void print_config_help(u32 depth) noexcept
{
	printf("config parameters:\n");

	const Config defaults{};

	for (const ConfigHeader& root : CONFIG.container.children)
		print_config_help_node(&defaults, &root, 0, depth == 0 ? UINT32_MAX : depth);
}
