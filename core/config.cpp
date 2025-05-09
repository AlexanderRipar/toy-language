#include "pass_data.hpp"

#include "../infra/minos.hpp"
#include "../infra/container.hpp"
#include "../diag/diag.hpp"

#include <cstdio>

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

static constexpr ConfigHeader CONFIG_LOGGING_ASTS[] = {
	ConfigHeader::make_boolean(offsetof(Config, logging.asts.enable), "enable", "Print ASTs after they are parsed"),
	ConfigHeader::make_path(offsetof(Config, logging.asts.log_filepath), "log-file", "Path of the log file. Defaults to stdout"),
};

static constexpr ConfigHeader CONFIG_LOGGING_IMPORTS[] = {
	ConfigHeader::make_boolean(offsetof(Config, logging.imports.enable), "enable", "Print file types after they are imported and typechecked"),
	ConfigHeader::make_boolean(offsetof(Config, logging.imports.enable_prelude), "enable-prelude", "Print type of hard-coded prelude pseudo-file"),
	ConfigHeader::make_path(offsetof(Config, logging.imports.log_filepath), "log-file", "Path of the log file. Defaults to stdout"),
};

static constexpr ConfigHeader CONFIG_LOGGING_CONFIG[] = {
	ConfigHeader::make_boolean(offsetof(Config, logging.config.enable), "enable", "Print config after it is parsed"),
};

static constexpr ConfigHeader CONFIG_LOGGING[] = {
	ConfigHeader::make_container(Range<ConfigHeader>{ CONFIG_LOGGING_ASTS }, "asts", "AST logging parameters"),
	ConfigHeader::make_container(Range<ConfigHeader>{ CONFIG_LOGGING_IMPORTS }, "imports", "file import logging parameters"),
	ConfigHeader::make_container(Range<ConfigHeader>{ CONFIG_LOGGING_CONFIG }, "config", "Config logging parameters"),
};

static constexpr ConfigHeader CONFIG_ROOTS[] = {
	ConfigHeader::make_container(Range<ConfigHeader>{ CONFIG_ENTRYPOINT }, "entrypoint", "Entrypoint configuration"),
	ConfigHeader::make_container(Range<ConfigHeader>{ CONFIG_STD }, "std", "Standard library configuration"),
	ConfigHeader::make_container(Range<ConfigHeader>{ CONFIG_LOGGING }, "logging", "Debug log configuration"),
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
	static constexpr u32 HEAP_RESERVE = 1 << 18;

	static constexpr u32 HEAP_COMMIT_INCREMENT = 1 << 12;

	const char8* begin;

	const char8* end;

	const char8* curr;

	ConfigToken peek;

	u32 context_top;

	const ConfigHeader* context_stack[8];

	Config* out;

	const Range<char8> filepath;

	Range<char8> path_base;

	ReservedVec<byte> heap;
};



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

static bool name_equal(Range<char8> text, const char8* name) noexcept
{
	for (u64 i = 0; i != text.count(); ++i)
	{
		if (text[i] != name[i])
			return false;
	}

	return true;
}



static u32 find_line_number(Range<char8> content, u64 offset, u64* out_line_begin_offset) noexcept
{
	u64 line_begin = 0;

	u32 line_number = 1;

	for (u64 i = 0; i != offset; ++i)
	{
		if (content[i] == '\n')
		{
			line_begin = i;

			line_number += 1;
		}
	}

	*out_line_begin_offset = line_begin;

	return line_number;
}

NORETURN static void error(ConfigParser* parser, const char8* curr, const char8* format, ...) noexcept
{
	va_list args;

	va_start(args, format);

	const u64 offset = curr - parser->begin;

	u64 line_begin_offset;

	SourceLocation location;
	location.filepath = parser->filepath;
	location.line_number = find_line_number(Range{ parser->begin, parser->end }, offset, &line_begin_offset);
	location.column_number = static_cast<u32>(1 + offset - line_begin_offset);
	location.context_offset = 0;
	location.context_chars = 0;

	print_error(&location, format, args);

	minos::exit_process(1);
}



static void skip_whitespace(ConfigParser* parser) noexcept
{
	while (true)
	{
		if (*parser->curr == '#')
		{
			parser->curr += 1;

			while (*parser->curr != '\0' && *parser->curr != '\n')
				parser->curr += 1;
		}
		else if (*parser->curr != ' ' && *parser->curr != '\t' && *parser->curr != '\r' && *parser->curr != '\n')
		{
			break;
		}

		parser->curr += 1;
	}
}

static ConfigToken next(ConfigParser* parser) noexcept
{
	if (parser->peek.type != ConfigToken::Type::EMPTY)
	{
		const ConfigToken peek = parser->peek;

		parser->peek.type = ConfigToken::Type::EMPTY;

		return peek;
	}

	skip_whitespace(parser);

	const char8 first = *parser->curr;

	const char8* const token_beg = parser->curr;

	parser->curr += 1;

	if (first == '0')
	{
		if (*parser->curr == 'x')
		{
			parser->curr += 1;

			while (is_hex_digit(*parser->curr))
				parser->curr += 1;

			if (parser->curr == token_beg + 2)
				error(parser, parser->curr, "Expected at least one digit after hexadecimal prefix '0x'\n");
		}
		else if (*parser->curr == 'o')
		{
			parser->curr += 1;

			while (is_oct_digit(*parser->curr))
				parser->curr += 1;

			if (parser->curr == token_beg + 2)
				error(parser, parser->curr, "Expected at least one digit after octal prefix '0o'\n");
		}
		else if (*parser->curr == 'b')
		{
			parser->curr += 1;

			while (is_bin_digit(*parser->curr))
				parser->curr += 1;

			if (parser->curr == token_beg + 2)
				error(parser, parser->curr, "Expected at least one digit after binary prefix '0b'\n");
		}
		else
		{
			while (is_dec_digit(*parser->curr))
				parser->curr += 1;
		}
	}
	else if (is_dec_digit(first))
	{
		while (is_dec_digit(*parser->curr))
			parser->curr += 1;

		if (is_dec_digit(*parser->curr) || is_alpha(*parser->curr))
			error(parser, parser->curr, "Unexpected character '%c' in number\n", *parser->curr);

		return { ConfigToken::Type::Integer, { token_beg, parser->curr } };
	}
	else if (is_alpha(first))
	{
		parser->curr += 1;

		while (is_alpha(*parser->curr) || is_dec_digit(*parser->curr) || *parser->curr == '_' || *parser->curr == '-')
			parser->curr += 1;

		return { ConfigToken::Type::Identity, { token_beg, parser->curr } };
	}
	else if (first < ' ')
	{
		if (first != '\0' || parser->curr != parser->end)
			error(parser, parser->curr, "Unexpected control character U+%02X in config file\n", first);

		return { ConfigToken::Type::End, {} };
	}
	else switch (first)
	{
	case '\'':
		if (*parser->curr == '\'' && parser->curr[1] == '\'')
		{
			parser->curr += 2;

			while (true)
			{
				if (*parser->curr == '\'' && parser->curr[1] == '\'' && parser->curr[2] == '\'')
				{
					parser->curr += 3;

					break;
				}
				else if (*parser->curr == '\0')
				{
					error(parser, token_beg, "String not ended before end of file\n");
				}

				parser->curr += 1;
			}

			return { ConfigToken::Type::MultilineLiteralString, { token_beg, parser->curr } };
		}
		else
		{
			while (true)
			{
				if (*parser->curr == '\'')
				{
					parser->curr += 1;

					break;
				}
				else if (*parser->curr == '\0' || *parser->curr == '\r' || *parser->curr == '\n')
				{
					error(parser, token_beg, "Single-line string not ended before end of line\n");
				}

				parser->curr += 1;
			}

			return { ConfigToken::Type::LiteralString, { token_beg, parser->curr } };
		}

	case '"':
		if (*parser->curr == '"' && parser->curr[1] == '"')
		{
			parser->curr += 2;

			while (true)
			{
				if (*parser->curr == '"' && parser->curr[1] == '"' && parser->curr[2] == '"')
				{
					parser->curr += 3;

					break;
				}
				else if (*parser->curr == '\0')
				{
					error(parser, token_beg, "String not ended before end of file\n");
				}
				else if (*parser->curr == '\\')
				{
					parser->curr += 1;
				}

				parser->curr += 1;
			}

			return { ConfigToken::Type::MultilineString, { token_beg, parser->curr } };
		}
		else
		{
			while (true)
			{
				if (*parser->curr == '"')
				{
					parser->curr += 1;

					break;
				}
				else if (*parser->curr == '\0' || *parser->curr == '\r' || *parser->curr == '\n')
				{
					error(parser, token_beg, "Single-line string not ended before end of line\n");
				}
				else if (*parser->curr == '\\')
				{
					parser->curr += 1;
				}

				parser->curr += 1;
			}

			return { ConfigToken::Type::String, { token_beg, parser->curr } };
		}

	case '.':
		return {  ConfigToken::Type::Dot, { token_beg, parser->curr } };

	case '=':
		return {  ConfigToken::Type::Set, { token_beg, parser->curr } };

	case '[':
		if (*parser->curr == '[')
		{
			parser->curr += 1;

			return { ConfigToken::Type::DoubleBracketBeg, { token_beg, parser->curr } };
		}
		else
		{
			return { ConfigToken::Type::BracketBeg, { token_beg, parser->curr } };
		}

	case ']':
		if (parser->curr[1] == ']')
		{
			parser->curr += 1;

			return { ConfigToken::Type::DoubleBracketEnd, { token_beg, parser->curr } };
		}
		else
		{
			return { ConfigToken::Type::BracketEnd, { token_beg, parser->curr } };
		}

	case '{':
		return { ConfigToken::Type::CurlyBeg, { token_beg, parser->curr } };

	case '}':
		return { ConfigToken::Type::CurlyEnd, { token_beg, parser->curr } };

	case ',':
		return { ConfigToken::Type::Comma, {token_beg, parser->curr } };

	default:
		error(parser, token_beg, "Unexpected character '%c' (U+%02X)\n", first, first);
	}

	ASSERT_UNREACHABLE;
}

static ConfigToken peek(ConfigParser* parser) noexcept
{
	if (parser->peek.type != ConfigToken::Type::EMPTY)
		return parser->peek;

	parser->peek = next(parser);

	return parser->peek;
}

static void skip(ConfigParser* parser) noexcept
{
	(void) next(parser);
}



static void parse_name_element(ConfigParser* parser, const ConfigToken& token) noexcept
{
	ASSERT_OR_IGNORE(token.type == ConfigToken::Type::Identity);

	if (parser->context_top == array_count(parser->context_stack))
		error(parser, token.content.begin(), "Key nesting limit exceeded\n");

	ASSERT_OR_IGNORE(parser->context_top != 0);

	const ConfigHeader* const context = parser->context_stack[parser->context_top - 1];

	if (context->type != ConfigHeader::Type::Container)
		error(parser, token.content.begin(), "Tried assigning to key '%.*s' that does not expect subkeys\n", static_cast<u32>(token.content.count()), token.content.begin());

	for (const ConfigHeader& child : context->container.children)
	{
		if (name_equal(token.content, child.name))
		{
			parser->context_stack[parser->context_top] = &child;

			parser->context_top += 1;

			return;
		}
	}

	error(parser, token.content.begin(), "Key '%.*s' does not exist in '%s'\n", static_cast<u32>(token.content.count()), token.content.begin(), context->name);
}

static u32 parse_names(ConfigParser* parser) noexcept
{
	u32 name_count = 1;

	while (true)
	{
		const ConfigToken identity = next(parser);

		if (identity.type != ConfigToken::Type::Identity)
			error(parser, identity.content.begin(), "Expcted key but got '%.*s'\n", static_cast<u32>(identity.content.count()), identity.content.begin());

		parse_name_element(parser, identity);

		const ConfigToken next = peek(parser);

		if (next.type != ConfigToken::Type::Dot)
			return name_count;

		skip(parser);

		name_count += 1;
	}
}

static void pop_names(ConfigParser* parser, u32 count) noexcept
{
	ASSERT_OR_IGNORE(parser->context_top > count);

	parser->context_top -= count;
}



static void parse_value(ConfigParser* parser) noexcept;

static void parse_inline_table(ConfigParser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(parser).type == ConfigToken::Type::CurlyBeg);

	skip(parser);

	ConfigToken token = peek(parser);

	// Empty inline table is a special case
	if (token.type == ConfigToken::Type::CurlyEnd)
	{
		skip(parser);

		return;
	}

	while (true)
	{
		const u32 name_depth = parse_names(parser);

		token = peek(parser);

		if (token.type != ConfigToken::Type::Set)
			error(parser, token.content.begin(), "Expected '=' but got '%.*s'\n", static_cast<u32>(token.content.count()), token.content.begin());

		parse_value(parser);

		pop_names(parser, name_depth);

		token = next(parser);

		if (token.type == ConfigToken::Type::CurlyEnd)
			return;
		else if (token.type != ConfigToken::Type::Comma)
			error(parser, token.content.begin(), "Expected '}' or ',' but got '%.*s'\n", static_cast<u32>(token.content.count()), token.content.begin());
	}
}

static void parse_boolean(ConfigParser* parser) noexcept
{
	const ConfigToken token = next(parser);

	ASSERT_OR_IGNORE(parser->context_top != 0);

	const ConfigHeader* const context = parser->context_stack[parser->context_top - 1];

	bool value;

	if (name_equal(token.content, "true"))
		value = true;
	else if (name_equal(token.content, "false"))
		value = false;
	else
		error(parser, token.content.begin(), "Expected a value but got '%.*s'\n", static_cast<u32>(token.content.count()), token.content.begin());

	if (context->type != ConfigHeader::Type::Boolean)
		error(parser, token.content.begin(), "Cannot assign boolean to key '%s' expecting different value\n", context->name);

	*reinterpret_cast<bool*>(reinterpret_cast<byte*>(parser->out) + context->target_offset) = value;
}

static void parse_integer(ConfigParser* parser) noexcept
{
	const ConfigToken token = next(parser);

	ASSERT_OR_IGNORE(parser->context_top != 0);

	const ConfigHeader* const context = parser->context_stack[parser->context_top - 1];

	if (context->type != ConfigHeader::Type::Integer)
		error(parser, token.content.begin(), "Cannot assign integer to key '%s' expecting different value\n", context->name);

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

	*reinterpret_cast<s64*>(reinterpret_cast<byte*>(parser->out) + context->target_offset) = value;
}

static void parse_unicode_escape_sequence(ConfigParser* parser, Range<char8> text, u32 escape_chars, CodepointBuffer* out) noexcept
{
	if (text.count() < escape_chars)
		error(parser, text.begin(), escape_chars == 4 ? "\\u escape expects four hex digits but got %llu" : "\\U escape expects eight hex digits but got %llu\n", text.count());

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
			error(parser, text.begin() + i, "Expected hexadecimal escape character but got '%c'\n", c);
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
		error(parser, text.begin(), "Escaped codepoint is larger than the maximum unicode codepoint (0x10FFFF)");
	}
}

static u32 parse_escape_sequence(ConfigParser* parser, Range<char8> text, CodepointBuffer* out) noexcept
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
		parse_unicode_escape_sequence(parser, Range{ text.begin() + 2, text.end() }, 4, out);

		return 6;

	case 'U':
		parse_unicode_escape_sequence(parser, Range{ text.begin() + 2, text.end() }, 8, out);

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
		error(parser, text.begin(), "Unexpected escape sequence '\\%c'\n", text[1]);
	}
}

static void parse_escaped_string_base(ConfigParser* parser, Range<char8> string) noexcept
{
	ASSERT_OR_IGNORE(parser->context_top != 0);

	const ConfigHeader* const context = parser->context_stack[parser->context_top - 1];

	if (context->type != ConfigHeader::Type::String && context->type != ConfigHeader::Type::Path)
		error(parser, string.begin(), "Cannot assign string to key '%s' expecting different value\n", context->name);

	if (string[0] == '\n')
		string = Range{ string.begin() + 1, string.end() };
	else if (string[0] == '\r' && string[1] == '\n')
		string = Range{ string.begin() + 2, string.end() };

	void* const allocation_begin = parser->heap.begin() + parser->heap.used();

	u32 uncopied_begin = 0;

	u32 i = 0;

	while (i < static_cast<u32>(string.count()))
	{
		if (string[i] == '\\')
		{
			CodepointBuffer utf8;

			const u32 escape_chars = parse_escape_sequence(parser, Range{ string.begin() + i, string.end() }, &utf8);

			const u32 uncopied_length = i - uncopied_begin;

			parser->heap.append_exact(string.begin() + uncopied_begin, uncopied_length);

			parser->heap.append_exact(utf8.buf, utf8.length);

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

	parser->heap.append_exact(string.begin() + uncopied_begin, uncopied_length);

	Range<char8> value{ static_cast<const char8*>(allocation_begin), reinterpret_cast<const char8*>(parser->heap.begin()) + parser->heap.used() };

	if (context->type == ConfigHeader::Type::Path)
	{
		char8 path_buf[minos::MAX_PATH_CHARS];

		const u32 path_chars = minos::path_to_absolute_relative_to(value, parser->path_base, MutRange{ path_buf });

		if (path_chars == 0 || path_chars > array_count(path_buf))
			error(parser, string.begin(), "Resulting absolute path exceeds maximum of %u characters\n", minos::MAX_PATH_CHARS);

		parser->heap.pop_by(static_cast<u32>(value.count()));

		value = { value.begin(), path_chars };

		parser->heap.append_exact(path_buf, path_chars);
	}

	*reinterpret_cast<Range<char8>*>(reinterpret_cast<byte*>(parser->out) + context->target_offset) = value;
}

static void parse_string(ConfigParser* parser) noexcept
{
	const ConfigToken token = next(parser);

	ASSERT_OR_IGNORE(token.content.count() >= 2);

	parse_escaped_string_base(parser, Range{ token.content.begin() + 1, token.content.end() - 1 });
}

static void parse_multiline_string(ConfigParser* parser) noexcept
{
	const ConfigToken token = next(parser);

	ASSERT_OR_IGNORE(token.content.count() >= 6);

	parse_escaped_string_base(parser, Range{ token.content.begin() + 3, token.content.end() - 3 });
}

static void parse_literal_string_base(ConfigParser* parser, Range<char8> string) noexcept
{
	ASSERT_OR_IGNORE(parser->context_top != 0);

	const ConfigHeader* const context = parser->context_stack[parser->context_top - 1];

	if (context->type != ConfigHeader::Type::String && context->type != ConfigHeader::Type::Path)
		error(parser, string.begin(), "Cannot assign string to key '%s' expecting different value\n", context->name);

	if (string[0] == '\n')
		string = Range{ string.begin() + 1, string.end() };
	else if (string[0] == '\r' && string[1] == '\n')
		string = Range{ string.begin() + 2, string.end() };

	Range<char8> value;

	if (context->type == ConfigHeader::Type::Path)
	{
		char8 path_buf[minos::MAX_PATH_CHARS];

		const u32 path_chars = minos::path_to_absolute_relative_to(string, parser->path_base, MutRange{ path_buf });

		if (path_chars == 0 || path_chars > array_count(path_buf))
			error(parser, string.begin(), "Resulting absolute path exceeds maximum of %u characters\n", minos::MAX_PATH_CHARS);

		value = { reinterpret_cast<const char8*>(parser->heap.begin() + parser->heap.used()), path_chars };

		parser->heap.append_exact(path_buf, path_chars);
	}
	else
	{
		void* const allocation_begin = parser->heap.begin() + parser->heap.used();

		parser->heap.append_exact(string.begin(), static_cast<u32>(string.count()));

		value = { static_cast<const char8*>(allocation_begin), static_cast<u32>(string.count()) };
	}

	*reinterpret_cast<Range<char8>*>(reinterpret_cast<byte*>(parser->out) + context->target_offset) = value;
}

static void parse_literal_string(ConfigParser* parser) noexcept
{
	const ConfigToken token = next(parser);

	ASSERT_OR_IGNORE(token.content.count() >= 2);

	parse_literal_string_base(parser, Range{ token.content.begin() + 1, token.content.end() - 1});
}

static void parse_multiline_literal_string(ConfigParser* parser) noexcept
{
	const ConfigToken token = next(parser);

	ASSERT_OR_IGNORE(token.content.count() >= 6);

	parse_literal_string_base(parser, Range{ token.content.begin() + 3, token.content.end() - 3});
}

static void parse_value(ConfigParser* parser) noexcept
{
	const ConfigToken token = peek(parser);

	switch (token.type)
	{
	case ConfigToken::Type::BracketBeg:
		error(parser, token.content.begin(), "Arrays are currently not supported");

	case ConfigToken::Type::CurlyBeg:
		return parse_inline_table(parser);

	case ConfigToken::Type::Identity:
		return parse_boolean(parser);

	case ConfigToken::Type::Integer:
		return parse_integer(parser);

	case ConfigToken::Type::String:
		return parse_string(parser);

	case ConfigToken::Type::LiteralString:
		return parse_literal_string(parser);

	case ConfigToken::Type::MultilineString:
		return parse_multiline_string(parser);

	case ConfigToken::Type::MultilineLiteralString:
		return parse_multiline_literal_string(parser);

	default:
		error(parser, token.content.begin(), "Expected a value but got '%.*s'\n", static_cast<u32>(token.content.count()), token.content.begin());
	}
}





static void parse_config(ConfigParser* parser) noexcept
{
	while (true)
	{
		ConfigToken token = peek(parser);

		switch (token.type)
		{
		case ConfigToken::Type::BracketBeg:
		{
			skip(parser);

			parser->context_top = 1;

			(void) parse_names(parser);

			token = next(parser);

			if (token.type != ConfigToken::Type::BracketEnd)
				error(parser, token.content.begin(), "Expected ']' but got %.*s\n", static_cast<u32>(token.content.count()), token.content.begin());

			break;
		}

		case ConfigToken::Type::Identity:
		{
			const u32 name_depth = parse_names(parser);

			token = next(parser);

			if (token.type != ConfigToken::Type::Set)
				error(parser, token.content.begin(), "Expected '=' or '.' but got '%.*s'\n", static_cast<u32>(token.content.count()), token.content.begin());

			parse_value(parser);

			pop_names(parser, name_depth);

			break;
		}

		case ConfigToken::Type::End:
		{
			return;
		}

		case ConfigToken::Type::DoubleBracketBeg:
			error(parser, token.content.begin(), "Arrays of Tables are not currently supported\n");

		default:
			ASSERT_UNREACHABLE;
		}
	}
}

static ConfigParser init_config_parser(Range<char8> filepath, Config* out) noexcept
{
	ConfigParser parser;

	parser.out = out;
	parser.peek = {};
	parser.context_top = 1;
	parser.context_stack[0] = &CONFIG;
	parser.heap.init(ConfigParser::HEAP_RESERVE, ConfigParser::HEAP_COMMIT_INCREMENT);

	minos::FileHandle filehandle;

	if (!minos::file_create(filepath, minos::Access::Read, minos::ExistsMode::Open, minos::NewMode::Fail, minos::AccessPattern::Sequential, nullptr, false, &filehandle))
		panic("Could not open config file '%.*s' (0x%X)\n", static_cast<u32>(filepath.count()), filepath.begin(), minos::last_error());

	minos::FileInfo fileinfo;

	if (!minos::file_get_info(filehandle, &fileinfo))
		panic("Could not determine length of config file '%.*s' (0x%X)\n", static_cast<u32>(filepath.count()), filepath.begin(), minos::last_error());

	if (fileinfo.bytes > UINT32_MAX)
		panic("Length of config file '%.*s' (%llu bytes) exceeds the maximum size of 4GB", static_cast<u32>(filepath.count()), filepath.begin(), fileinfo.bytes);

	parser.heap.reserve_exact(static_cast<u32>(fileinfo.bytes + 1));

	char8* buffer = static_cast<char8*>(minos::mem_reserve(fileinfo.bytes + 1));

	if (buffer == nullptr)
		panic("Could not reserve buffer of %" PRId64 " bytes for reading config file (0x%X)\n", fileinfo.bytes + 1, minos::last_error());

	if (!minos::mem_commit(buffer, fileinfo.bytes + 1))
		panic("Could not commit buffer of %" PRIu64 " bytes for reading config file (0x%X)\n", fileinfo.bytes + 1, minos::last_error());

	buffer[fileinfo.bytes] = '\0';

	parser.begin = buffer;
	parser.end = buffer + fileinfo.bytes + 1;
	parser.curr = buffer;

	u32 bytes_read;

	if (!minos::file_read(filehandle, MutRange{ reinterpret_cast<byte*>(buffer), fileinfo.bytes }, 0, &bytes_read))
		panic("Could not read config file '%.*s' (0x%X)\n", static_cast<u32>(filepath.count()), filepath.begin(), minos::last_error());

	if (bytes_read != fileinfo.bytes)
		panic("Could not read config file '%.*s' completely (read %u out of %" PRIu64 " bytes)\n", static_cast<u32>(filepath.count()), filepath.begin(), bytes_read, fileinfo.bytes);

	minos::file_close(filehandle);

	char8 path_base[minos::MAX_PATH_CHARS];

	const u32 path_base_chars = minos::path_to_absolute_directory(filepath, MutRange{ path_base });

	if (path_base_chars == 0 || path_base_chars > array_count(path_base))
		panic("Could not determine folder containing config file (0x%X)\n", minos::last_error());

	parser.path_base = { reinterpret_cast<char8*>(parser.heap.begin() + parser.heap.used()), path_base_chars };

	parser.heap.append_exact(path_base, path_base_chars);

	out->m_heap_ptr = parser.heap.begin();
	out->m_config_filepath = filepath;

	return parser;
}



static void print_config_node(diag::PrintContext* ctx, const Config* config, const ConfigHeader* node, u32 indent) noexcept
{
	if (node->type == ConfigHeader::Type::Container)
	{
		diag::buf_printf(ctx, "%*s%s {\n", indent * 2, "", node->name);

		for (const ConfigHeader& child : node->container.children)
			print_config_node(ctx, config, &child, indent + 1);

		diag::buf_printf(ctx, "%*s}\n", indent * 2, "");
	}
	else if (node->type == ConfigHeader::Type::Integer)
	{
		const s64 value = *reinterpret_cast<const s64*>(reinterpret_cast<const byte*>(config) + node->target_offset);

		diag::buf_printf(ctx, "%*s%s = %" PRId64 "\n", indent * 2, "", node->name, value);
	}
	else if (node->type == ConfigHeader::Type::String || node->type == ConfigHeader::Type::Path)
	{
		const Range<char8> value = *reinterpret_cast<const Range<char8>*>(reinterpret_cast<const byte*>(config) + node->target_offset);

		diag::buf_printf(ctx, "%*s%s = '%.*s'\n", indent * 2, "", node->name, static_cast<u32>(value.count()), value.begin());
	}
	else if (node->type == ConfigHeader::Type::Boolean)
	{
		const bool value = *reinterpret_cast<const bool*>(reinterpret_cast<const byte*>(config) + node->target_offset);

		diag::buf_printf(ctx, "%*s%s = %s\n", indent * 2, "", node->name, value ? "true" : "false");
	}
	else
	{
		ASSERT_UNREACHABLE;
	}
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



Config* create_config(AllocPool* alloc, Range<char8> filepath) noexcept
{
	Config* const config = static_cast<Config*>(alloc_from_pool(alloc, sizeof(Config), alignof(Config)));

	ConfigParser parser = init_config_parser(filepath, config);

	parse_config(&parser);

	minos::mem_unreserve(const_cast<char8*>(parser.begin), parser.end - parser.begin);

	return config;
}

void release_config(Config* config) noexcept
{
	ASSERT_OR_IGNORE(config->m_heap_ptr != nullptr);

	minos::mem_unreserve(config->m_heap_ptr, ConfigParser::HEAP_RESERVE);

	config->m_heap_ptr = nullptr;
}

void print_config(minos::FileHandle out, const Config* config) noexcept
{
	diag::PrintContext ctx;
	ctx.curr = ctx.buf;
	ctx.file = out;

	diag::buf_printf(&ctx, "\n#### CONFIG [%.*s] ####\n\n", static_cast<s32>(config->m_config_filepath.count()), config->m_config_filepath.begin());

	for (const ConfigHeader& root : CONFIG.container.children)
		print_config_node(&ctx, config, &root, 0);

	diag::buf_flush(&ctx);
}

void print_config_help(u32 depth) noexcept
{
	printf("config parameters:\n");

	const Config defaults{};

	for (const ConfigHeader& root : CONFIG.container.children)
		print_config_help_node(&defaults, &root, 0, depth == 0 ? UINT32_MAX : depth);
}
