#include "toml.hpp"

#include "../types.hpp"
#include "../assert.hpp"
#include "../panic.hpp"
#include "../math.hpp"
#include "../range.hpp"
#include "../minos/minos.hpp"
#include "../print/print.hpp"

static constexpr u64 TOML_HEAP_RESERVE = 1 << 18;

static constexpr u64 TOML_HEAP_COMMIT_INCREMENT = 1 << 12;

static constexpr u32 TOML_TABLE_INITIAL_CAPACITY = 2;

struct CodepointBuffer
{
	char8 buf[4];

	u8 length;
};

struct TomlLocation
{
	u32 line;

	u32 column;
};

enum class TomlTokenTag : u8
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
};

struct TomlToken
{
	TomlTokenTag tag;

	Range<char8> content;

	u32 line;

	u32 column;
};

struct TomlParser
{
	TreeSchemaTable* root_table;

	TreeSchemaTable* active_table;

	const char8* begin;

	const char8* end;

	const char8* curr;

	const char8* line_begin;

	u32 line;

	TreeSchemaAllocator ts_alloc;

	Range<char8> filepath;

	PrintSink error_sink;
};



static TomlLocation find_location(TomlParser* parser, const char8* location) noexcept
{
	ASSERT_OR_IGNORE(location >= parser->begin && location < parser->end);

	const char8* line_begin = parser->begin;

	u32 line_number = 1;

	for (const char8* curr = parser->begin; curr != location; ++curr)
	{
		if (*curr == '\n')
		{
			line_begin = curr + 1;

			line_number += 1;
		}
	}

	return TomlLocation{ line_number, static_cast<u32>(1 + location - line_begin) };
}

static void toml_error_print_header(TomlParser* parser, u32 line, u32 column) noexcept
{
	ASSERT_OR_IGNORE((line == 0) == (column == 0));

	if (line != 0)
		(void) print(parser->error_sink, "%:%:%: ", parser->filepath, line, column);
	else
		(void) print(parser->error_sink, "%: ", parser->filepath);
}

static Range<char8> token_tag_error_prefix(TomlTokenTag tag) noexcept
{
	if (tag == TomlTokenTag::End)
		return range::from_literal_string(" end of input");
	else if (tag == TomlTokenTag::Identifier)
		return range::from_literal_string(" key name");
	else if (tag == TomlTokenTag::Integer)
		return range::from_literal_string(" integer value");
	else if (tag == TomlTokenTag::String || tag == TomlTokenTag::MultilineString || tag == TomlTokenTag::LiteralString || tag == TomlTokenTag::MultilineLiteralString)
		return range::from_literal_string(" string value");
	else
		return range::from_literal_string("");

}

template<typename... Inserts>
static [[nodiscard]] bool toml_line_error(TomlParser* parser, u32 line, u32 column, const char8* message, Inserts... inserts) noexcept
{
	toml_error_print_header(parser, line, column);

	(void) print(parser->error_sink, message, inserts...);

	DEBUGBREAK;

	return false;
}

template<typename... Inserts>
static [[nodiscard]] Maybe<TreeSchemaTable*> toml_io_error(PrintSink error_sink, Range<char8> filepath, const char8* message, Inserts... inserts) noexcept
{
	// Quick-and-dirty semi-initialized parser for error-reporting.
	TomlParser parser{};
	parser.filepath = filepath;
	parser.error_sink = error_sink;

	toml_error_print_header(&parser, 0, 0);

	(void) print(error_sink, message, inserts...);

	return none<TreeSchemaTable*>();
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
		else if (*parser->curr == '\n')
		{
			parser->line += 1;

			parser->line_begin = parser->curr + 1;
		}
		else if (*parser->curr != ' ' && *parser->curr != '\t' && *parser->curr != '\r')
		{
			break;
		}

		parser->curr += 1;
	}
}

static bool next(TomlParser* parser, TomlToken* out) noexcept
{
	skip_whitespace(parser);

	const char8 first = *parser->curr;

	const char8* const token_beg = parser->curr;

	const u32 token_line = parser->line;

	const u32 token_column = static_cast<u32>(1 + token_beg - parser->line_begin);

	parser->curr += 1;

	if (first == '0')
	{
		if (*parser->curr == 'x')
		{
			parser->curr += 1;

			while (is_hex_digit(*parser->curr))
				parser->curr += 1;

			if (parser->curr == token_beg + 2)
				return toml_line_error(parser, parser->line, static_cast<u32>(1 + token_beg - parser->line_begin), "Expected at least one digit in integer literal.\n");
		}
		else if (*parser->curr == 'o')
		{
			parser->curr += 1;

			while (is_oct_digit(*parser->curr))
				parser->curr += 1;

			if (parser->curr == token_beg + 2)
				return toml_line_error(parser, parser->line, static_cast<u32>(1 + token_beg - parser->line_begin), "Expected at least one digit in integer literal.\n");
		}
		else if (*parser->curr == 'b')
		{
			parser->curr += 1;

			while (is_bin_digit(*parser->curr))
				parser->curr += 1;

			if (parser->curr == token_beg + 2)
				return toml_line_error(parser, parser->line, static_cast<u32>(1 + token_beg - parser->line_begin), "Expected at least one digit in integer literal.\n");
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
			return toml_line_error(parser, parser->line, static_cast<u32>(1 + token_beg - parser->line_begin), "Unexpected character after integer literal.\n");

		*out = TomlToken{ TomlTokenTag::Integer, Range<char8>{ token_beg, parser->curr }, token_line, token_column };

		return true;
	}
	else if (is_alpha(first))
	{
		while (is_alpha(*parser->curr) || is_dec_digit(*parser->curr) || *parser->curr == '_' || *parser->curr == '-')
			parser->curr += 1;

		*out = TomlToken{ TomlTokenTag::Identifier, Range<char8>{ token_beg, parser->curr }, token_line, token_column };

		return true;
	}
	else if (first < ' ')
	{
		if (first != '\0' || parser->curr < parser->end)
			return toml_line_error(parser, parser->line, static_cast<u32>(1 + token_beg - parser->line_begin), "Unexpected control character `\\%` in config file.\n", static_cast<u8>(first));

		*out = TomlToken{ TomlTokenTag::End, Range<char8>{ token_beg, token_beg }, token_line, token_column };

		return true;
	}
	else switch (first)
	{
	case '\'':
	{
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
					return toml_line_error(parser, parser->line, static_cast<u32>(1 + token_beg - parser->line_begin), "String not ended before end of file.\n");
				}

				parser->curr += 1;
			}

			*out = TomlToken{ TomlTokenTag::MultilineLiteralString, Range<char8>{ token_beg, parser->curr }, token_line, token_column };

			return true;
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
					return toml_line_error(parser, parser->line, static_cast<u32>(1 + token_beg - parser->line_begin), "Single-line string not ended before end of line.\n");
				}

				parser->curr += 1;
			}

			*out = TomlToken{ TomlTokenTag::LiteralString, Range<char8>{ token_beg, parser->curr }, token_line, token_column };

			return true;
		}
	}

	case '"':
	{
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
					return toml_line_error(parser, parser->line, static_cast<u32>(1 + token_beg - parser->line_begin), "String not ended before end of file.\n");
				}
				else if (*parser->curr == '\n')
				{
					parser->line += 1;
					parser->line_begin = parser->curr + 1;
				}
				else if (*parser->curr == '\\')
				{
					parser->curr += 1;
				}

				parser->curr += 1;
			}

			*out = TomlToken{ TomlTokenTag::MultilineString, Range<char8>{ token_beg, parser->curr }, token_line, token_column };

			return true;
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
					return toml_line_error(parser, parser->line, static_cast<u32>(1 + token_beg - parser->line_begin), "Single-line string not ended before end of line.\n");
				}
				else if (*parser->curr == '\\')
				{
					parser->curr += 1;
				}

				parser->curr += 1;
			}

			*out = TomlToken{ TomlTokenTag::String, Range<char8>{ token_beg, parser->curr }, token_line, token_column };

			return true;
		}
	}

	case '.':
	{
		*out = TomlToken{ TomlTokenTag::Dot, Range<char8>{ token_beg, parser->curr }, token_line, token_column };

		return true;
	}

	case '=':
	{
		*out = TomlToken{ TomlTokenTag::Set, Range<char8>{ token_beg, parser->curr }, token_line, token_column };

		return true;
	}

	case '[':
	{
		if (*parser->curr == '[')
		{
			parser->curr += 1;

			*out = TomlToken{ TomlTokenTag::DoubleBracketBeg, Range<char8>{ token_beg, parser->curr }, token_line, token_column };

			return true;
		}
		else
		{
			*out = TomlToken{ TomlTokenTag::BracketBeg, Range<char8>{ token_beg, parser->curr }, token_line, token_column };

			return true;
		}
	}

	case ']':
	{
		if (parser->curr[1] == ']')
		{
			parser->curr += 1;

			*out = TomlToken{ TomlTokenTag::DoubleBracketEnd, Range<char8>{ token_beg, parser->curr }, token_line, token_column };

			return true;
		}
		else
		{
			*out = TomlToken{ TomlTokenTag::BracketEnd, Range<char8>{ token_beg, parser->curr }, token_line, token_column };

			return true;
		}
	}

	case '{':
	{
		*out = TomlToken{ TomlTokenTag::CurlyBeg, Range<char8>{ token_beg, parser->curr }, token_line, token_column };

		return true;
	}

	case '}':
	{
		*out = TomlToken{ TomlTokenTag::CurlyEnd, Range<char8>{ token_beg, parser->curr }, token_line, token_column };

		return true;
	}

	case ',':
	{
		*out = TomlToken{ TomlTokenTag::Comma, Range<char8>{ token_beg, parser->curr }, token_line, token_column };

		return true;
	}

	default:
	{
		return toml_line_error(parser, parser->line, static_cast<u32>(1 + token_beg - parser->line_begin), "Unexpected character `%` (0x%[|X]) in TOML file.\n", Range<char8>{ parser->curr, 1 }, static_cast<u8>(*parser->curr));
	}
	}

	ASSERT_UNREACHABLE;
}



static bool parse_unicode_escape_sequence(TomlParser* parser, Range<char8> text, u32 escape_chars, CodepointBuffer* out) noexcept
{
	if (text.count() < escape_chars)
	{
		const TomlLocation location = find_location(parser, text.begin());

		return toml_line_error(parser, location.line, location.column, "`\\%` escape expects % hex digits but found `%`.\n", escape_chars == 4 ? "u" : "U", escape_chars, text.count());
	}

	u32 utf32 = 0;

	for (u32 i = 0; i != escape_chars; ++i)
	{
		const char8 c = text[i];

		if (c >= '0' && c <= '9')
		{
			utf32 = utf32 * 16 + c - '0';
		}
		else if (c >= 'a' && c <= 'f')
		{
			utf32 = utf32 * 16 + c - 'a' + 10;
		}
		else if (c >= 'A' && c <= 'F')
		{
			utf32 = utf32 * 16 + c - 'A' + 10;
		}
		else
		{
		const TomlLocation location = find_location(parser, text.begin() + i);

			return toml_line_error(parser, location.line, location.column, "Expected hexadecimal escape character but found `%`.\n", text.subrange(i, 1));
		}
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
		const TomlLocation location = find_location(parser, text.begin());

		return toml_line_error(parser, location.line, location.column, "Escaped codepoint 0x%[|X] is larger than the maximum unicode codepoint (0x10FFFF).\n", utf32);
	}

	return true;
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
	{
		const TomlLocation location = find_location(parser, text.begin());

		(void) toml_line_error(parser, location.line, location.column, "Unexpected escape sequence `%`.\n", text);

		return 0;
	}
	}
}



static bool parse_value(TomlParser* parser, TomlToken token, void* into, bool is_array, Range<char8> name) noexcept;

static bool parse_subkey(TomlParser* parser, TomlToken token, TreeSchemaTable* table) noexcept;



static bool add_value_to_table_or_array(TomlParser* parser, void* into, bool into_is_array, TreeSchemaValue value) noexcept
{
	if (into_is_array)
	{
		if (!ts_array_add_value(&parser->ts_alloc, static_cast<TreeSchemaArray*>(into), value))
			return toml_line_error(parser, value.source_line, value.source_column, "Failed to grow `TreeSchemaArray`.\n");
	}
	else
	{
		const TreeSchemaTableAddResult rst = ts_table_add_value(&parser->ts_alloc, static_cast<TreeSchemaTable*>(into), value);

		if (rst == TreeSchemaTableAddResult::NoMemory)
		{
			return toml_line_error(parser, value.source_line, value.source_column, "Failed to grow `TreeSchemaTable`.\n");
		}
		else if (rst == TreeSchemaTableAddResult::DuplicateName)
		{
			const TreeSchemaValue* const existing = get(ts_table_find_value(static_cast<TreeSchemaTable*>(into), value.name_and_tag.range()));

			return toml_line_error(parser, value.source_line, value.source_column, "Table already contains a key named `%` (defined at %:%:%)", value.name_and_tag.range(), parser->filepath, existing->source_line, existing->source_column);
		}

		ASSERT_OR_IGNORE(rst == TreeSchemaTableAddResult::Ok);
	}

	return true;
}

static bool parse_boolean(TomlParser* parser, TomlToken token, void* into, bool into_is_array, Range<char8> name) noexcept
{
	if ((token.content.count() != 4 || !range::mem_equal(token.content, range::from_literal_string("true")))
	 && (token.content.count() != 5 || !range::mem_equal(token.content, range::from_literal_string("false")))
	) {
		return toml_line_error(parser, token.line, token.column, "Expected value but got key name `%`.\n", token.content);
	}

	TreeSchemaValue toml_value{};
	
	if (!ts_value_from_boolean(&parser->ts_alloc, name, token.line, token.column, token.content.count() == 4, &toml_value))
		return toml_line_error(parser, token.line, token.column, "Failed to allocate `TreeSchemaValue`.\n");

	return add_value_to_table_or_array(parser, into, into_is_array, toml_value);
}

static bool parse_inline_table(TomlParser* parser, TomlToken token, void* into, bool into_is_array, Range<char8> name) noexcept
{
	ASSERT_OR_IGNORE(token.tag == TomlTokenTag::CurlyBeg);

	const Maybe<TreeSchemaTable*> new_table = ts_table_create(&parser->ts_alloc);

	if (is_none(new_table))
		return toml_line_error(parser, token.line, token.column, "Failed to allocate `TreeSchemaTable`.\n");

	TreeSchemaTable* const table = get(new_table);

	TreeSchemaValue table_value{};
	
	if (!ts_value_from_table(&parser->ts_alloc, name, token.line, token.column, table, &table_value))
		return toml_line_error(parser, token.line, token.column, "Failed to allocate `TreeSchemaValue`.\n");

	if (!add_value_to_table_or_array(parser, into, into_is_array, table_value))
		return false;

	while (true)
	{
		if (!next(parser, &token))
			return false;

		if (!parse_subkey(parser, token, table))
			return false;

		if (!next(parser, &token))
			return false;

		if (token.tag == TomlTokenTag::CurlyEnd)
		{
			return true;
		}
		else if (token.tag != TomlTokenTag::Comma)
		{
			const Range<char8> prefix = token_tag_error_prefix(token.tag);

			return toml_line_error(parser, token.line, token.column, "Expected `,` or `}` after key-value pair in inline table but found% `%`.\n", prefix, token.content);
		}
	}
}

static bool parse_array(TomlParser* parser, TomlToken token, void* into, bool into_is_array, Range<char8> name) noexcept
{
	ASSERT_OR_IGNORE(token.tag == TomlTokenTag::BracketBeg);

	const Maybe<TreeSchemaArray*> new_array = ts_array_create(&parser->ts_alloc);

	if (is_none(new_array))
		return false;

	TreeSchemaArray* const array = get(new_array);

	TreeSchemaValue array_value{};

	if (!ts_value_from_array(&parser->ts_alloc, name, token.line, token.column, array, &array_value))
		return toml_line_error(parser, token.line, token.column, "Failed to allocate `TreeSchemaValue`.\n");

	if (!add_value_to_table_or_array(parser, into, into_is_array, array_value))
		return false;

	while (true)
	{
		if (!next(parser, &token))
			return false;

		if (!parse_value(parser, token, array, true, Range<char8>{}))
			return false;

		if (!next(parser, &token))
			return false;

		if (token.tag == TomlTokenTag::BracketEnd)
		{
			return true;
		}
		else if (token.tag != TomlTokenTag::Comma)
		{
			const Range<char8> prefix = token_tag_error_prefix(token.tag);

			return toml_line_error(parser, token.line, token.column, "Expected `,` or `]` after value in array but found% `%`.\n", prefix, token.content);
		}
	}
}

static bool parse_integer(TomlParser* parser, TomlToken token, void* into, bool into_is_array, Range<char8> name) noexcept
{
	ASSERT_OR_IGNORE(token.tag == TomlTokenTag::Integer);

	// This has to hold, as otherwise the token would not have been classified
	// as an integer.
	ASSERT_OR_IGNORE(token.content.count() != 0);

	const Range<char8> text = token.content;

	s64 value = 0;

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

	TreeSchemaValue toml_value{};

	if (!ts_value_from_integer(&parser->ts_alloc, name, token.line, token.column, value, &toml_value))
		return toml_line_error(parser, token.line, token.column, "Failed to allocate `TreeSchemaValue`.\n");

	return add_value_to_table_or_array(parser, into, into_is_array, toml_value);
}

static bool parse_escaped_string(TomlParser* parser, TomlToken token, void* into, bool into_is_array, Range<char8> name) noexcept
{
	ASSERT_OR_IGNORE(token.tag == TomlTokenTag::String || token.tag == TomlTokenTag::MultilineString);

	Range<char8> text = token.tag == TomlTokenTag::String
		? token.content.subrange(1, token.content.count() - 2)
		: token.content.subrange(3, token.content.count() - 6);

	if (text[0] == '\n')
		text = Range{ text.begin() + 1, text.end() };
	else if (text[0] == '\r' && text[1] == '\n')
		text = Range{ text.begin() + 2, text.end() };

	u64 allocation_size = 0;

	Range<char8> string;
	
	if (!ts_string_create(&parser->ts_alloc, Range<char8>{}, &string))
		return toml_line_error(parser, token.line, token.column, "Failed to allocate `TreeSchemaString`.\n");

	u32 uncopied_begin = 0;

	u32 i = 0;

	while (i < static_cast<u32>(text.count()))
	{
		if (text[i] == '\\')
		{
			CodepointBuffer utf8;

			const u32 escape_chars = parse_escape_sequence(parser, Range{ text.begin() + i, text.end() }, &utf8);

			const u32 uncopied_length = i - uncopied_begin;

			if (!ts_string_append(&parser->ts_alloc, text.subrange(uncopied_begin, uncopied_length), &string))
				return toml_line_error(parser, token.line, token.column, "Failed to grow `TreeSchemaString`.\n");

			if (!ts_string_append(&parser->ts_alloc, Range<char8>{ utf8.buf, utf8.length }, &string))
				return toml_line_error(parser, token.line, token.column, "Failed to grow `TreeSchemaString`.\n");

			allocation_size += uncopied_length + utf8.length;

			i += escape_chars;

			uncopied_begin = i;
		}
		else
		{
			i += 1;
		}
	}

	ASSERT_OR_IGNORE(i == text.count());

	const u32 uncopied_length = i - uncopied_begin;

	if (!ts_string_append(&parser->ts_alloc, text.subrange(uncopied_begin, uncopied_length), &string))
		return toml_line_error(parser, token.line, token.column, "Failed to grow `TreeSchemaString`.\n");

	allocation_size += uncopied_length;

	TreeSchemaValue toml_value{};
	
	if (!ts_value_from_string(&parser->ts_alloc, name, token.line, token.column, string, &toml_value))
		return toml_line_error(parser, token.line, token.column, "Failed to allocate `TreeSchemaValue`.\n");

	return add_value_to_table_or_array(parser, into, into_is_array, toml_value);
}

static bool parse_literal_string(TomlParser* parser, TomlToken token, void* into, bool into_is_array, Range<char8> name) noexcept
{
	ASSERT_OR_IGNORE(token.tag == TomlTokenTag::LiteralString || token.tag == TomlTokenTag::MultilineLiteralString);

	Range<char8> text = token.tag == TomlTokenTag::LiteralString
		? token.content.subrange(1, token.content.count() - 2)
		: token.content.subrange(3, token.content.count() - 6);

	if (text[0] == '\n')
		text = Range{ text.begin() + 1, text.end() };
	else if (text[0] == '\r' && text[1] == '\n')
		text = Range{ text.begin() + 2, text.end() };

	Range<char8> string;

	if (!ts_string_create(&parser->ts_alloc, text, &string))
		return toml_line_error(parser, token.line, token.column, "Failed to allocate `TreeSchemaString`.\n");

	TreeSchemaValue toml_value{};
	
	if (!ts_value_from_string(&parser->ts_alloc, name, token.line, token.column, string, &toml_value))
		return toml_line_error(parser, token.line, token.column, "Failed to allocate `TreeSchemaValue`.\n");

	return add_value_to_table_or_array(parser, into, into_is_array, toml_value);
}



static bool parse_value(TomlParser* parser, TomlToken token, void* into, bool is_array, Range<char8> name) noexcept
{
	if (token.tag == TomlTokenTag::Identifier)
	{
		return parse_boolean(parser, token, into, is_array, name);
	}
	else if (token.tag == TomlTokenTag::CurlyBeg)
	{
		return parse_inline_table(parser, token, into, is_array, name);
	}
	else if (token.tag == TomlTokenTag::BracketBeg)
	{
		return parse_array(parser, token, into, is_array, name);
	}
	else if (token.tag == TomlTokenTag::Integer)
	{
		return parse_integer(parser, token, into, is_array, name);
	}
	else if (token.tag == TomlTokenTag::String || token.tag == TomlTokenTag::MultilineString)
	{
		return parse_escaped_string(parser, token, into, is_array, name);
	}
	else if (token.tag == TomlTokenTag::LiteralString || token.tag == TomlTokenTag::MultilineLiteralString)
	{
		return parse_literal_string(parser, token, into, is_array, name);
	}
	else
	{
		const Range<char8> prefix = token_tag_error_prefix(token.tag);

		return toml_line_error(parser, token.line, token.column, "Expected value but found% `%`.\n", prefix, token.content);
	}
}

static bool parse_subkey(TomlParser* parser, TomlToken token, TreeSchemaTable* table) noexcept
{
	ASSERT_OR_IGNORE(token.tag == TomlTokenTag::Identifier);

	const Range<char8> name = token.content;

	const u32 line = token.line;

	const u32 column = token.column;

	if (!next(parser, &token))
		return false;

	if (token.tag == TomlTokenTag::Dot)
	{
		const Maybe<TreeSchemaValue*> existing_value = ts_table_find_value(table, name);

		TreeSchemaTable* subkey_table;

		if (is_none(existing_value))
		{
			const Maybe<TreeSchemaTable*> new_subkey = ts_table_create(&parser->ts_alloc);

			if (is_none(new_subkey))
				return toml_line_error(parser, token.line, token.column, "Failed to allocate `TreeSchemaTable`.\n");

			TreeSchemaValue new_subkey_value{};
			
			if (!ts_value_from_table(&parser->ts_alloc, name, line, column, get(new_subkey), &new_subkey_value))
				return toml_line_error(parser, token.line, token.column, "Failed to allocate `TreeSchemaValue`.\n");

			const TreeSchemaTableAddResult add_rst = ts_table_add_value(&parser->ts_alloc, table, new_subkey_value);

			if (add_rst == TreeSchemaTableAddResult::NoMemory)
				return toml_line_error(parser, token.line, token.column, "Failed to grow `TreeSchemaTable`.\n");

			ASSERT_OR_IGNORE(add_rst == TreeSchemaTableAddResult::Ok);

			subkey_table = get(new_subkey);
		}
		else if (get(existing_value)->name_and_tag.attachment() == TreeSchemaValueTag::Table)
		{
			subkey_table = get(existing_value)->value.table;
		}
		else
		{
			return toml_line_error(parser, line, column, "Key `%` was already defined at %:%:%", token.content, parser->filepath, get(existing_value)->source_line, get(existing_value)->source_column);
		}

		if (!next(parser, &token))
			return false;

		return parse_subkey(parser, token, subkey_table);
	}
	else if (token.tag == TomlTokenTag::Set)
	{
		if (!next(parser, &token))
			return false;

		return parse_value(parser, token, table, false, name);
	}
	else
	{
		const Range<char8> prefix = token_tag_error_prefix(token.tag);

		return toml_line_error(parser, token.line, token.column, "Unexpected% `%` after key name. Expected `.` or `=`.\n", prefix, token.content);
	}
}

static bool parse_table(TomlParser* parser) noexcept
{
	TomlToken token;

	if (!next(parser, &token))
		return false;

	if (token.tag != TomlTokenTag::Identifier)
	{
		const Range<char8> prefix = token_tag_error_prefix(token.tag);

		return toml_line_error(parser, token.line, token.column, "Expected key name after top-level `[`, but found% `%`.\n", prefix, token.content);
	}

	const Maybe<TreeSchemaTable*> table = ts_table_create(&parser->ts_alloc);

	if (is_none(table))
		return toml_line_error(parser, token.line, token.column, "Failed to allocate `TreeSchemaTable`.\n");

	TreeSchemaValue table_value{};
	
	if (!ts_value_from_table(&parser->ts_alloc, token.content, token.line, token.column, get(table), &table_value))
		return toml_line_error(parser, token.line, token.column, "Failed to allocate `TreeSchemaValue`.\n");

	if (!add_value_to_table_or_array(parser, parser->root_table, false, table_value))
		return false;

	parser->active_table = get(table);



	if (!next(parser, &token))
		return false;

	if (token.tag != TomlTokenTag::BracketEnd)
	{
		Range<char8> prefix;

		if (token.tag == TomlTokenTag::End)
		{
			return toml_line_error(parser, token.line, token.column, "Expected `]` to end top-level table name, but found end of input.\n");
		}

		if (token.tag == TomlTokenTag::Integer)
			prefix = range::from_literal_string(" integer value");
		else if (token.tag == TomlTokenTag::String || token.tag == TomlTokenTag::MultilineString || token.tag == TomlTokenTag::LiteralString || token.tag == TomlTokenTag::MultilineLiteralString)
			prefix = range::from_literal_string(" string value");
		else
			prefix = range::from_literal_string("");

		return toml_line_error(parser, token.line, token.column, "Expected `]` to end top-level table name, but found% `%`.\n");
	}

	return true;
}

static bool parse_toml_root(TomlParser* parser, TomlToken token) noexcept
{
	if (token.tag == TomlTokenTag::Identifier)
	{
		return parse_subkey(parser, token, parser->active_table);
	}
	else if (token.tag == TomlTokenTag::BracketBeg)
	{
		return parse_table(parser);
	}
	else if (token.tag == TomlTokenTag::DoubleBracketBeg)
	{
		TODO("Implement TOML arrays-of-tables");
	}
	else
	{
		Range<char8> prefix;

		if (token.tag == TomlTokenTag::Integer)
			prefix = range::from_literal_string(" integer value");
		else if (token.tag == TomlTokenTag::String || token.tag == TomlTokenTag::MultilineString || token.tag == TomlTokenTag::LiteralString || token.tag == TomlTokenTag::MultilineLiteralString)
			prefix = range::from_literal_string(" string value");
		else
			prefix = range::from_literal_string("");

		return toml_line_error(parser, token.line, token.column, "Unexpected top-level% `%`. Expected key name.\n", prefix, token.content);
	}
}

static bool parse_toml(TomlParser* parser) noexcept
{
	TomlToken token;

	if (!next(parser, &token))
		return false;

	while (token.tag != TomlTokenTag::End)
	{
		if (!parse_toml_root(parser, token))
			return false;

		if (!next(parser, &token))
			return false;
	}

	return true;
}



Maybe<TreeSchemaTable*> parse_toml_blob(Range<char8> blob, Range<char8> filepath, PrintSink error_sink, TreeSchemaAllocator* inout_alloc) noexcept
{
	TomlParser parser{};
	parser.begin = blob.begin();
	parser.curr = blob.begin();
	parser.end = blob.end();
	parser.line_begin = blob.begin();
	parser.line = 1;
	parser.ts_alloc = ts_allocator_create(TOML_HEAP_RESERVE, TOML_HEAP_COMMIT_INCREMENT);
	parser.filepath = filepath;
	parser.error_sink = error_sink;

	const Maybe<TreeSchemaTable*> root_table = ts_table_create(&parser.ts_alloc);

	if (is_none(root_table))
		return none<TreeSchemaTable*>();

	parser.root_table = get(root_table);
	parser.active_table = get(root_table);

	if (!parse_toml(&parser))
		return none<TreeSchemaTable*>();

	*inout_alloc = parser.ts_alloc;

	return root_table;
}

Maybe<TreeSchemaTable*> parse_toml_file(Range<char8> filepath, PrintSink error_sink, TreeSchemaAllocator* inout_alloc) noexcept
{
	minos::FileHandle file;

	if (!minos::file_create(filepath, minos::Access::Read, minos::ExistsMode::Open, minos::NewMode::Fail, minos::AccessPattern::Sequential, none<const minos::CompletionInitializer*>(), false, &file))
		return toml_io_error(error_sink, filepath, "Failed to open TOML file (0x%[|X]).\n", minos::last_error());

	minos::FileInfo file_info;

	if (!minos::file_get_info(file, &file_info))
	{
		minos::file_close(file);

		return toml_io_error(error_sink, filepath, "Failed to get size of TOML file (0x%[|X]).\n", minos::last_error());
	}

	void* const memory = minos::mem_reserve(file_info.bytes);

	if (memory == nullptr)
	{
		minos::file_close(file);

		return toml_io_error(error_sink, filepath, "Failed to reserve % bytes of memory for reading TOML file (0x%[|X]).\n", minos::last_error());
	}

	if (!minos::mem_commit(memory, file_info.bytes))
	{
		minos::mem_unreserve(memory, file_info.bytes);

		minos::file_close(file);

		return toml_io_error(error_sink, filepath, "Failed to commit % bytes of memory for reading TOML file (0x%[|X]).\n", minos::last_error());
	}

	u32 bytes_read;

	if (!minos::file_read(file, MutRange<byte>{ static_cast<byte*>(memory), file_info.bytes }, 0, &bytes_read))
	{
		minos::mem_unreserve(memory, file_info.bytes);

		minos::file_close(file);

		return toml_io_error(error_sink, filepath, "Failed to read from TOML file (0x%[|X])", minos::last_error());
	}

	if (bytes_read != file_info.bytes)
	{
		minos::mem_unreserve(memory, file_info.bytes);

		minos::file_close(file);

		return toml_io_error(error_sink, filepath, "Failed to read % bytes from from TOML file, reading % bytes instead (0x%[|X])", file_info.bytes, bytes_read, minos::last_error());
	}

	const Maybe<TreeSchemaTable*> result = parse_toml_blob(Range<char8>{ static_cast<char8*>(memory), file_info.bytes }, filepath, error_sink, inout_alloc);

	minos::mem_unreserve(memory, file_info.bytes);

	minos::file_close(file);

	return result;
}
