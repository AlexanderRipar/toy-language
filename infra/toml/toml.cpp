#include "toml.hpp"

#include "../types.hpp"
#include "../assert.hpp"
#include "../panic.hpp"
#include "../math.hpp"
#include "../range.hpp"
#include "../minos/minos.hpp"
#include "../print/print.hpp"
#include "../container/reserved_vec.hpp"

#include <csetjmp>

struct CodepointBuffer
{
	char8 buf[4];

	u8 length;
};

struct TomlToken
{
	enum class Type
	{
		EMPTY = 0,
		End,
		Identifier,
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

struct TomlParser
{
	static constexpr u32 HEAP_RESERVE = 1 << 18;

	static constexpr u32 HEAP_COMMIT_INCREMENT = 1 << 12;

	const char8* begin;

	const char8* end;

	const char8* curr;

	TomlToken peek;

	u32 context_top;

	const TreeSchemaNode* context_stack[8];

	MutRange<byte> out;

	const Range<char8> filepath;

	Range<char8> path_base;

	ReservedVec<byte> heap;

	MutRange<byte> memory;

	jmp_buf error_jump_buffer;
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

NORETURN static void toml_error(TomlParser* parser, const char8* curr, const char8* message) noexcept
{
	const u64 offset = curr - parser->begin;

	u64 line_begin_offset;

	const u32 line_number = find_line_number(Range{ parser->begin, parser->end }, offset, &line_begin_offset);

	const u32 column_number = static_cast<u32>(1 + offset - line_begin_offset);

	(void) print(minos::standard_file_handle(minos::StdFileName::StdErr), "%[]%:%: ", parser->filepath, line_number, column_number);

	(void) print(minos::standard_file_handle(minos::StdFileName::StdErr), message);

	longjmp(parser->error_jump_buffer, 1);
}



static void skip_whitespace(TomlParser* parser) noexcept
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

static TomlToken next(TomlParser* parser) noexcept
{
	if (parser->peek.type != TomlToken::Type::EMPTY)
	{
		const TomlToken peek = parser->peek;

		parser->peek.type = TomlToken::Type::EMPTY;

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
				toml_error(parser, parser->curr, "Expected at least one digit in integer literal.\n");
		}
		else if (*parser->curr == 'o')
		{
			parser->curr += 1;

			while (is_oct_digit(*parser->curr))
				parser->curr += 1;

			if (parser->curr == token_beg + 2)
				toml_error(parser, parser->curr, "Expected at least one digit in integer literal.\n");
		}
		else if (*parser->curr == 'b')
		{
			parser->curr += 1;

			while (is_bin_digit(*parser->curr))
				parser->curr += 1;

			if (parser->curr == token_beg + 2)
				toml_error(parser, parser->curr, "Expected at least one digit in integer literal.\n");
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
			toml_error(parser, parser->curr, "Unexpected character after integer literal.\n");

		return { TomlToken::Type::Integer, { token_beg, parser->curr } };
	}
	else if (is_alpha(first))
	{
		parser->curr += 1;

		while (is_alpha(*parser->curr) || is_dec_digit(*parser->curr) || *parser->curr == '_' || *parser->curr == '-')
			parser->curr += 1;

		return { TomlToken::Type::Identifier, { token_beg, parser->curr } };
	}
	else if (first < ' ')
	{
		if (first != '\0' || parser->curr != parser->end)
			toml_error(parser, parser->curr, "Unexpected control character in config file.\n");

		return { TomlToken::Type::End, {} };
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
					toml_error(parser, token_beg, "String not ended before end of file.\n");
				}

				parser->curr += 1;
			}

			return { TomlToken::Type::MultilineLiteralString, { token_beg, parser->curr } };
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
					toml_error(parser, token_beg, "Single-line string not ended before end of line.\n");
				}

				parser->curr += 1;
			}

			return { TomlToken::Type::LiteralString, { token_beg, parser->curr } };
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
					toml_error(parser, token_beg, "String not ended before end of file.\n");
				}
				else if (*parser->curr == '\\')
				{
					parser->curr += 1;
				}

				parser->curr += 1;
			}

			return { TomlToken::Type::MultilineString, { token_beg, parser->curr } };
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
					toml_error(parser, token_beg, "Single-line string not ended before end of line.\n");
				}
				else if (*parser->curr == '\\')
				{
					parser->curr += 1;
				}

				parser->curr += 1;
			}

			return { TomlToken::Type::String, { token_beg, parser->curr } };
		}

	case '.':
		return {  TomlToken::Type::Dot, { token_beg, parser->curr } };

	case '=':
		return {  TomlToken::Type::Set, { token_beg, parser->curr } };

	case '[':
		if (*parser->curr == '[')
		{
			parser->curr += 1;

			return { TomlToken::Type::DoubleBracketBeg, { token_beg, parser->curr } };
		}
		else
		{
			return { TomlToken::Type::BracketBeg, { token_beg, parser->curr } };
		}

	case ']':
		if (parser->curr[1] == ']')
		{
			parser->curr += 1;

			return { TomlToken::Type::DoubleBracketEnd, { token_beg, parser->curr } };
		}
		else
		{
			return { TomlToken::Type::BracketEnd, { token_beg, parser->curr } };
		}

	case '{':
		return { TomlToken::Type::CurlyBeg, { token_beg, parser->curr } };

	case '}':
		return { TomlToken::Type::CurlyEnd, { token_beg, parser->curr } };

	case ',':
		return { TomlToken::Type::Comma, {token_beg, parser->curr } };

	default:
		toml_error(parser, token_beg, "Unexpected character in TOML file.\n");
	}

	ASSERT_UNREACHABLE;
}

static TomlToken peek(TomlParser* parser) noexcept
{
	if (parser->peek.type != TomlToken::Type::EMPTY)
		return parser->peek;

	parser->peek = next(parser);

	return parser->peek;
}

static void skip(TomlParser* parser) noexcept
{
	(void) next(parser);
}



static void parse_name_element(TomlParser* parser, const TomlToken& token) noexcept
{
	ASSERT_OR_IGNORE(token.type == TomlToken::Type::Identifier);

	if (parser->context_top == array_count(parser->context_stack))
		toml_error(parser, token.content.begin(), "Key nesting limit exceeded.\n");

	ASSERT_OR_IGNORE(parser->context_top != 0);

	const TreeSchemaNode* const context = parser->context_stack[parser->context_top - 1];

	if (context->kind != TreeSchemaNodeKind::Container)
		toml_error(parser, token.content.begin(), "Tried assigning to key that does not expect subkeys.\n");

	for (const TreeSchemaNode& child : context->container.children)
	{
		if (name_equal(token.content, child.name))
		{
			parser->context_stack[parser->context_top] = &child;

			parser->context_top += 1;

			return;
		}
	}

	toml_error(parser, token.content.begin(), "Key does not exist.\n");
}

static u32 parse_names(TomlParser* parser) noexcept
{
	u32 name_count = 1;

	while (true)
	{
		const TomlToken identity = next(parser);

		if (identity.type != TomlToken::Type::Identifier)
			toml_error(parser, identity.content.begin(), "Expected key name.\n");

		parse_name_element(parser, identity);

		const TomlToken next = peek(parser);

		if (next.type != TomlToken::Type::Dot)
			return name_count;

		skip(parser);

		name_count += 1;
	}
}

static void pop_names(TomlParser* parser, u32 count) noexcept
{
	ASSERT_OR_IGNORE(parser->context_top > count);

	parser->context_top -= count;
}



static void parse_value(TomlParser* parser) noexcept;

static void parse_inline_table(TomlParser* parser) noexcept
{
	ASSERT_OR_IGNORE(peek(parser).type == TomlToken::Type::CurlyBeg);

	skip(parser);

	TomlToken token = peek(parser);

	// Empty inline table is a special case
	if (token.type == TomlToken::Type::CurlyEnd)
	{
		skip(parser);

		return;
	}

	while (true)
	{
		const u32 name_depth = parse_names(parser);

		token = peek(parser);

		if (token.type != TomlToken::Type::Set)
			toml_error(parser, token.content.begin(), "Expected `=`.\n");

		parse_value(parser);

		pop_names(parser, name_depth);

		token = next(parser);

		if (token.type == TomlToken::Type::CurlyEnd)
			return;
		else if (token.type != TomlToken::Type::Comma)
			toml_error(parser, token.content.begin(), "Expected `}` or `,`.\n");
	}
}

static void parse_boolean(TomlParser* parser) noexcept
{
	const TomlToken token = next(parser);

	ASSERT_OR_IGNORE(parser->context_top != 0);

	const TreeSchemaNode* const context = parser->context_stack[parser->context_top - 1];

	bool value;

	if (name_equal(token.content, "true"))
		value = true;
	else if (name_equal(token.content, "false"))
		value = false;
	else
		toml_error(parser, token.content.begin(), "Expected a value.\n");

	if (context->kind != TreeSchemaNodeKind::Boolean)
		toml_error(parser, token.content.begin(), "Value has the wrong type for the given key.\n");

	*reinterpret_cast<bool*>(parser->out.begin() + context->target_offset) = value;
}

static void parse_integer(TomlParser* parser) noexcept
{
	const TomlToken token = next(parser);

	ASSERT_OR_IGNORE(parser->context_top != 0);

	const TreeSchemaNode* const context = parser->context_stack[parser->context_top - 1];

	if (context->kind != TreeSchemaNodeKind::Integer)
		toml_error(parser, token.content.begin(), "Value has the wrong type for the given key.\n");

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

	*reinterpret_cast<s64*>(parser->out.begin() + context->target_offset) = value;
}

static void parse_unicode_escape_sequence(TomlParser* parser, Range<char8> text, u32 escape_chars, CodepointBuffer* out) noexcept
{
	if (text.count() < escape_chars)
	{
		const char8* error = escape_chars == 4
			? "`\\u` escape expects four hex digits.\n"
			: "`\\U` escape expects eight hex digits.\n";

		toml_error(parser, text.begin(), error);
	}

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
			toml_error(parser, text.begin() + i, "Expected hexadecimal escape character.\n");
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
		toml_error(parser, text.begin(), "Escaped codepoint is larger than the maximum unicode codepoint (0x10FFFF).\n");
	}
}

static u32 parse_escape_sequence(TomlParser* parser, Range<char8> text, CodepointBuffer* out) noexcept
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
		toml_error(parser, text.begin(), "Unexpected escape sequence.\n");
	}
}

static void parse_escaped_string_base(TomlParser* parser, Range<char8> string) noexcept
{
	ASSERT_OR_IGNORE(parser->context_top != 0);

	const TreeSchemaNode* const context = parser->context_stack[parser->context_top - 1];

	if (context->kind != TreeSchemaNodeKind::String && context->kind != TreeSchemaNodeKind::Path)
		toml_error(parser, string.begin(), "Value has the wrong type for the given key.\n");

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

	if (context->kind == TreeSchemaNodeKind::Path)
	{
		char8 path_buf[minos::MAX_PATH_CHARS];

		const u32 path_chars = minos::path_to_absolute_relative_to(value, parser->path_base, MutRange{ path_buf });

		if (path_chars == 0 || path_chars > array_count(path_buf))
			toml_error(parser, string.begin(), "Resulting absolute path exceeds maximum path length.\n");

		parser->heap.pop_by(static_cast<u32>(value.count()));

		value = { value.begin(), path_chars };

		parser->heap.append_exact(path_buf, path_chars);
	}

	*reinterpret_cast<Range<char8>*>(parser->out.begin() + context->target_offset) = value;
}

static void parse_string(TomlParser* parser) noexcept
{
	const TomlToken token = next(parser);

	ASSERT_OR_IGNORE(token.content.count() >= 2);

	parse_escaped_string_base(parser, Range{ token.content.begin() + 1, token.content.end() - 1 });
}

static void parse_multiline_string(TomlParser* parser) noexcept
{
	const TomlToken token = next(parser);

	ASSERT_OR_IGNORE(token.content.count() >= 6);

	parse_escaped_string_base(parser, Range{ token.content.begin() + 3, token.content.end() - 3 });
}

static void parse_literal_string_base(TomlParser* parser, Range<char8> string) noexcept
{
	ASSERT_OR_IGNORE(parser->context_top != 0);

	const TreeSchemaNode* const context = parser->context_stack[parser->context_top - 1];

	if (context->kind != TreeSchemaNodeKind::String && context->kind != TreeSchemaNodeKind::Path)
		toml_error(parser, string.begin(), "Value has the wrong type for the given key.\n");

	if (string[0] == '\n')
		string = Range{ string.begin() + 1, string.end() };
	else if (string[0] == '\r' && string[1] == '\n')
		string = Range{ string.begin() + 2, string.end() };

	Range<char8> value;

	if (context->kind == TreeSchemaNodeKind::Path)
	{
		char8 path_buf[minos::MAX_PATH_CHARS];

		const u32 path_chars = minos::path_to_absolute_relative_to(string, parser->path_base, MutRange{ path_buf });

		if (path_chars == 0 || path_chars > array_count(path_buf))
			toml_error(parser, string.begin(), "Resulting absolute path exceeds maximum path length.\n");

		value = { reinterpret_cast<const char8*>(parser->heap.begin() + parser->heap.used()), path_chars };

		parser->heap.append_exact(path_buf, path_chars);
	}
	else
	{
		void* const allocation_begin = parser->heap.begin() + parser->heap.used();

		parser->heap.append_exact(string.begin(), static_cast<u32>(string.count()));

		value = { static_cast<const char8*>(allocation_begin), static_cast<u32>(string.count()) };
	}

	*reinterpret_cast<Range<char8>*>(parser->out.begin() + context->target_offset) = value;
}

static void parse_literal_string(TomlParser* parser) noexcept
{
	const TomlToken token = next(parser);

	ASSERT_OR_IGNORE(token.content.count() >= 2);

	parse_literal_string_base(parser, Range{ token.content.begin() + 1, token.content.end() - 1});
}

static void parse_multiline_literal_string(TomlParser* parser) noexcept
{
	const TomlToken token = next(parser);

	ASSERT_OR_IGNORE(token.content.count() >= 6);

	parse_literal_string_base(parser, Range{ token.content.begin() + 3, token.content.end() - 3});
}

static void parse_value(TomlParser* parser) noexcept
{
	const TomlToken token = peek(parser);

	switch (token.type)
	{
	case TomlToken::Type::BracketBeg:
		TODO("Implement toml array parsing (when necessary)");

	case TomlToken::Type::CurlyBeg:
		return parse_inline_table(parser);

	case TomlToken::Type::Identifier:
		return parse_boolean(parser);

	case TomlToken::Type::Integer:
		return parse_integer(parser);

	case TomlToken::Type::String:
		return parse_string(parser);

	case TomlToken::Type::LiteralString:
		return parse_literal_string(parser);

	case TomlToken::Type::MultilineString:
		return parse_multiline_string(parser);

	case TomlToken::Type::MultilineLiteralString:
		return parse_multiline_literal_string(parser);

	default:
		toml_error(parser, token.content.begin(), "Expected a value.\n");
	}
}





static TomlParser init_toml_parser(Range<char8> filepath, const TreeSchemaNode* schema, MutRange<byte> inout_parsed, MutRange<byte>* out_allocation) noexcept
{
	byte* const memory = static_cast<byte*>(minos::mem_reserve(TomlParser::HEAP_RESERVE));

	if (memory == nullptr)
		panic("Could not reserve memory for TomlParser (0x%[|X]).\n", minos::last_error());

	TomlParser parser;

	parser.out = inout_parsed;
	parser.peek = {};
	parser.context_top = 1;
	parser.context_stack[0] = schema;
	parser.heap.init(MutRange<byte>{ memory, TomlParser::HEAP_RESERVE }, TomlParser::HEAP_COMMIT_INCREMENT);
	parser.memory = MutRange<byte>{ memory, TomlParser::HEAP_RESERVE };

	minos::FileHandle filehandle;

	if (!minos::file_create(filepath, minos::Access::Read, minos::ExistsMode::Open, minos::NewMode::Fail, minos::AccessPattern::Sequential, nullptr, false, &filehandle))
		panic("Could not open config file '%' (0x%[|X])\n", filepath, minos::last_error());

	minos::FileInfo fileinfo;

	if (!minos::file_get_info(filehandle, &fileinfo))
		panic("Could not determine length of config file '%' (0x%[|X])\n", filepath, minos::last_error());

	if (fileinfo.bytes > UINT32_MAX)
		panic("Length of config file '%' (% bytes) exceeds the maximum size of 4GB", filepath, fileinfo.bytes);

	parser.heap.reserve_exact(static_cast<u32>(fileinfo.bytes + 1));

	char8* buffer = static_cast<char8*>(minos::mem_reserve(fileinfo.bytes + 1));

	if (buffer == nullptr)
		panic("Could not reserve buffer of % bytes for reading config file (0x%[|X])\n", fileinfo.bytes + 1, minos::last_error());

	if (!minos::mem_commit(buffer, fileinfo.bytes + 1))
		panic("Could not commit buffer of % bytes for reading config file (0x%[|X])\n", fileinfo.bytes + 1, minos::last_error());

	buffer[fileinfo.bytes] = '\0';

	parser.begin = buffer;
	parser.end = buffer + fileinfo.bytes + 1;
	parser.curr = buffer;

	u32 bytes_read;

	if (!minos::file_read(filehandle, MutRange{ reinterpret_cast<byte*>(buffer), fileinfo.bytes }, 0, &bytes_read))
		panic("Could not read config file '%' (0x%[|X])\n", filepath, minos::last_error());

	if (bytes_read != fileinfo.bytes)
		panic("Could not read config file '%' completely (read % out of % bytes)\n", filepath, bytes_read, fileinfo.bytes);

	minos::file_close(filehandle);

	char8 path_base[minos::MAX_PATH_CHARS];

	const u32 path_base_chars = minos::path_to_absolute_directory(filepath, MutRange{ path_base });

	if (path_base_chars == 0 || path_base_chars > array_count(path_base))
		panic("Could not determine folder containing config file (0x%[|X])\n", minos::last_error());

	parser.path_base = { reinterpret_cast<char8*>(parser.heap.begin() + parser.heap.used()), path_base_chars };

	parser.heap.append_exact(path_base, path_base_chars);

	*out_allocation = MutRange{ parser.heap.begin(), TomlParser::HEAP_RESERVE };

	return parser;
}

static void parse_toml_impl(TomlParser* parser) noexcept
{
	while (true)
	{
		TomlToken token = peek(parser);

		switch (token.type)
		{
		case TomlToken::Type::BracketBeg:
		{
			skip(parser);

			parser->context_top = 1;

			(void) parse_names(parser);

			token = next(parser);

			if (token.type != TomlToken::Type::BracketEnd)
				toml_error(parser, token.content.begin(), "Expected `]`.\n");

			break;
		}

		case TomlToken::Type::Identifier:
		{
			const u32 name_depth = parse_names(parser);

			token = next(parser);

			if (token.type != TomlToken::Type::Set)
				toml_error(parser, token.content.begin(), "Expected `=` or `.`.\n");

			parse_value(parser);

			pop_names(parser, name_depth);

			break;
		}

		case TomlToken::Type::End:
		{
			return;
		}

		case TomlToken::Type::DoubleBracketBeg:
			TODO("Implement TOML arrays-of-tables (when necessary)");

		default:
			ASSERT_UNREACHABLE;
		}
	}
}



bool parse_toml(Range<char8> filepath, const TreeSchemaNode* schema, MutRange<byte> inout_parsed, MutRange<byte>* out_allocation) noexcept
{
	MutRange<byte> allocation;

	TomlParser parser = init_toml_parser(filepath, schema, inout_parsed, &allocation);

	bool is_ok;

	if (setjmp(parser.error_jump_buffer) == 0)
	{
		parse_toml_impl(&parser);

		*out_allocation = allocation;

		is_ok = true;
	}
	else
	{
		release_toml(allocation);

		*out_allocation = {};

		is_ok = false;
	}

	minos::mem_unreserve(const_cast<char8*>(parser.begin), parser.end - parser.begin);

	return is_ok;
}

void release_toml(MutRange<byte> allocation) noexcept
{
	minos::mem_unreserve(allocation.begin(), allocation.count());
}
