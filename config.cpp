#include "config.hpp"

#include "common.hpp"
#include "minos.hpp"
#include <type_traits>
#include <cstring>
#include <cstdio>
#include <cstdarg>



#define CONFIG_CONTAINER(name, member, child_count, helptext) \
	ConfigEntryTemplate{ ConfigEntryTemplate::ContainerData{ child_count }, static_cast<u32>(offsetof(Config, member)), name, helptext }

#define CONFIG_INTEGER(name, member, default_value, min, max, factor, helptext) \
	ConfigEntryTemplate{ ConfigEntryTemplate::IntegerData{ default_value, min, max, factor }, static_cast<u32>(offsetof(Config, member)), name, helptext }

#define CONFIG_STRING(name, member, default_value, helptext) \
	ConfigEntryTemplate{ ConfigEntryTemplate::StringData{ default_value }, static_cast<u32>(offsetof(Config, member)), name, helptext }



enum class ConfigEntryType : u8
{
	NONE = 0,
	Container,
	Integer,
	Boolean,
	String,
};

struct ConfigEntryTemplate
{
	struct ContainerData
	{
		u16 child_count;
	};

	struct IntegerData
	{
		u32 default_value;

		u32 min;

		u32 max;

		u32 factor;
	};

	struct StringData
	{
		Range<char8> default_value;
	};

	struct BooleanData
	{
		bool default_value;
	};

	ConfigEntryType type;

	u32 offset;

	const char8* name;

	const char8* helptext;

	union
	{
		ContainerData container;

		IntegerData integer;

		StringData string;

		BooleanData boolean;
	};

	constexpr ConfigEntryTemplate(ContainerData container, u32 offset, const char8* name, const char8* helptext) noexcept
		: type{ ConfigEntryType::Container }, container{ container }, offset{ offset }, name{ name }, helptext{ helptext } {}

	constexpr ConfigEntryTemplate(IntegerData integer, u32 offset, const char8* name, const char8* helptext) noexcept
		: type{ ConfigEntryType::Integer }, integer{ integer }, offset{ offset }, name{ name }, helptext{ helptext } {}

	constexpr ConfigEntryTemplate(StringData string, u32 offset, const char8* name, const char8* helptext) noexcept
		: type{ ConfigEntryType::String }, string{ string }, offset{ offset }, name{ name }, helptext{ helptext } {}

	constexpr ConfigEntryTemplate(BooleanData boolean, u32 offset, const char8* name, const char8* helptext) noexcept
		: type{ ConfigEntryType::Boolean }, boolean{ boolean }, offset{ offset }, name{ name }, helptext{ helptext } {}
};

static constexpr ConfigEntryTemplate config_template[] {
	ConfigEntryTemplate{ ConfigEntryTemplate::ContainerData{ 40 }, 0, nullptr, nullptr },
	CONFIG_CONTAINER("parallel", parallel, 1, "Container for configuration of multithreading"),
		CONFIG_INTEGER("thread-count", parallel.thread_count, 1, 1, 4096, 0, "Number of created worker threads working"),
	CONFIG_CONTAINER("entrypoint", entrypoint, 2, "Container for configuring the program's entrypoint"),
		CONFIG_STRING("filepath", entrypoint.filepath, Range<char8>{}, "Path to the file in which the entrypoint is defined"),
		CONFIG_STRING("symbol", entrypoint.symbol, range_from_literal_string("main()"), "Call signature of the entry point procedure"),
	CONFIG_CONTAINER("input", input, 4, "Container for tuning parameters of source file input"),
		CONFIG_INTEGER("bytes-per-read", input.bytes_per_read, 65536, 4096, 1048576, 4096, "Size of the buffer passed to the OS's read procedure"),
		CONFIG_INTEGER("max-concurrent-reads", input.max_concurrent_reads, 16, 1, 32767, 0, "Number of OS reads that can be active simultaneously"),
		CONFIG_INTEGER("max-concurrent-files", input.max_concurrent_files, 8, 1, 4095, 0, "Number of files that can be have active read calls against them simultaneously"),
		CONFIG_INTEGER("max-concurrent-reads-per-file", input.max_concurrent_reads_per_file, 2, 1, 127, 0, "Number of OS reads that can be active simultaneously for a given file"),
	CONFIG_CONTAINER("detail", detail, 29, "Container for configurable implementation details"),
		CONFIG_CONTAINER("input", detail.input, 19, "Container for source file input configuration"),
			CONFIG_CONTAINER("files", detail.input.files, 8, "Container for configuration of set of distinct discovered source files, irrespective of the filepath they were specified under"),
				CONFIG_CONTAINER("map", detail.input.files.map, 3, "Container for configuration of hash map"),
					CONFIG_INTEGER("reserve", detail.input.files.map.reserve, 4096, 2048, 1048576, 2048, "Maximum size of the array backing the open hash set can grow to"),
					CONFIG_INTEGER("initial-commit", detail.input.files.map.initial_commit, 4096, 2048, 1048576, 2048, "Initial size that the array backing the open hash set can grow to"),
					CONFIG_INTEGER("max-insertion-distance", detail.input.files.map.max_insertion_distance, 128, 64, 4096, 64, "Maximum number of map entries which can be locked by an insert before a rehash is triggered"),
				CONFIG_CONTAINER("store", detail.input.files.store, 3, "Container for configuration of the set's entry storage"),
					CONFIG_INTEGER("reserve", detail.input.files.store.reserve, 4096, 256, 1048576, 2048, "Maximum number of entries that can be inserted into the set"),
					CONFIG_INTEGER("commit-increment", detail.input.files.store.commit_increment, 256, 256, 1048576, 256, "Amount by which a thread grows its store when its allocated storage is exhausted"),
					CONFIG_INTEGER("initial-commit-per-thread", detail.input.files.store.initial_commit_per_thread, 256, 256, 32768, 256, "Size of storage initially allocated for each thread"),
			CONFIG_CONTAINER("filenames", detail.input.filenames, 8, "Container for configuration of set of discovered source file names"),
				CONFIG_CONTAINER("map", detail.input.filenames.map, 3, "Container for configuration of hash map"),
					CONFIG_INTEGER("reserve", detail.input.filenames.map.reserve, 4096, 2048, 1048576, 2048, "Maximum size of the array backing the open hash set can grow to"),
					CONFIG_INTEGER("initial-commit", detail.input.filenames.map.initial_commit, 4096, 2048, 1048576, 2048, "Initial size that the array backing the open hash set can grow to"),
					CONFIG_INTEGER("max-insertion-distance", detail.input.filenames.map.max_insertion_distance, 128, 64, 4096, 64, "Maximum number of map entries which can be locked by an insert before a rehash is triggered"),
				CONFIG_CONTAINER("store", detail.input.filenames.store, 3, "Container for configuration of the set's entry storage"),
					CONFIG_INTEGER("reserve", detail.input.filenames.store.reserve, 4096, 256, 1048576, 2048, "Maximum number of entries that can be inserted into the set"),
					CONFIG_INTEGER("commit-increment", detail.input.filenames.store.commit_increment, 256, 256, 1048576, 256, "Amount by which a thread grows its store when its allocated storage is exhausted"),
					CONFIG_INTEGER("initial-commit-per-thread", detail.input.filenames.store.initial_commit_per_thread, 256, 256, 32768, 256, "Size of storage initially allocated for each thread"),
			CONFIG_INTEGER("max-pending-files", detail.input.max_pending_files, 4096, 64, 1048576, 0, "Upper limit on the number of files that have been discovered but have not been yet read"),
		CONFIG_CONTAINER("identifiers", detail.identifiers, 8, "Container for configuration of set of discovered identifiers"),
			CONFIG_CONTAINER("map", detail.identifiers.map, 3, "Container for configuration of hash map"),
				CONFIG_INTEGER("reserve", detail.identifiers.map.reserve, 1048576, 65536, 33554432, 2048, "Maximum size that the array backing the open hash set can grow to"),
				CONFIG_INTEGER("initial-commit", detail.identifiers.map.initial_commit, 16384, 2048, 33554432, 2048, "Initial size that the array backing the open hash set can grow to"),
				CONFIG_INTEGER("max-insertion-distance", detail.identifiers.map.max_insertion_distance, 128, 64, 4096, 64, "Maximum number of map entries which can be locked by an insert before a rehash is triggered"),
			CONFIG_CONTAINER("store", detail.identifiers.store, 3, "Container for configuration of the set's entry storage"),
				CONFIG_INTEGER("reserve", detail.identifiers.store.reserve, 2097152, 512, 134217728, 512, "Maximum number of entries that can be inserted into the set"),
				CONFIG_INTEGER("commit-increment", detail.identifiers.store.commit_increment, 65536, 512, 134217728, 512, "Amount by which a thread grows its store when its allocated storage is exhausted"),
				CONFIG_INTEGER("initial-commit-per-thread", detail.identifiers.store.initial_commit_per_thread, 65536, 512, 134217728, 512, "Size of storage initially allocated for each thread"),
};



struct ConfigEntry
{
	ConfigEntryType type;

	bool seen;

	u16 container_child_count;

	u32 offset;

	const char8* name;

	Range<char8> source;

	ConfigEntry() noexcept = default;

	constexpr ConfigEntry(const ConfigEntryTemplate& tpl) noexcept
		: type{ tpl.type },
		  seen{ false },
		  container_child_count{ tpl.type == ConfigEntryType::Container ? tpl.container.child_count : 0ui16 },
		  offset{ tpl.offset },
		  name{ tpl.name },
		  source{} {}
};

struct ConfigHeap
{
	static constexpr u32 RESERVE = 262'144;

	static constexpr u32 COMMIT_INCREMENT = 8192;

	static constexpr u32 INITIAL_COMMIT = 8192;

	void* ptr;

	u32 commit;

	u32 used;
};

struct CodepointBuffer
{
	char8 buf[4];

	u8 length;
};

enum class ConfigTokenType : u8
{
	NONE = 0,
	INVALID,
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
};

struct ConfigToken
{
	ConfigTokenType type;

	Range<char8> content;

	const char8* tokenizer_error;
};

struct ConfigParseState
{
	const char8* curr;

	const char8* end;

	const char8* begin;

	ConfigToken lookahead;

	Config* config;

	ConfigHeap heap;

	u32 context_stack_count;

	ConfigEntry* context_stack[8];

	ConfigEntry config_entries[sizeof(config_template) / sizeof(ConfigEntryTemplate)];

	ConfigParseError* error;
};

struct LineInfo
{
	const char8* line_begin;

	const char8* line_end;

	u32 line_number;

	u32 character_number;
};



static bool parse_value(ConfigParseState* s) noexcept;



static bool heap_init(ConfigHeap* out) noexcept
{
	memset(out, 0, sizeof(*out));

	void* ptr = minos::reserve(ConfigHeap::RESERVE);

	if (ptr == nullptr)
		return false;

	if (!minos::commit(ptr, ConfigHeap::INITIAL_COMMIT))
		return false;

	out->ptr = ptr;

	out->commit = ConfigHeap::INITIAL_COMMIT;

	out->used = 0;

	return true;
}

static bool heap_alloc(ConfigHeap* heap, u32 bytes, void** out) noexcept
{
	if (heap->commit < heap->used + bytes)
	{
		const u32 extra_commit = next_multiple(bytes - (heap->commit - heap->used), ConfigHeap::COMMIT_INCREMENT);

		if (heap->commit + extra_commit > ConfigHeap::RESERVE)
			return false;

		if (!minos::commit(static_cast<byte*>(heap->ptr) + heap->commit, extra_commit))
			return false;

		heap->commit += extra_commit;
	}

	*out = static_cast<byte*>(heap->ptr) + heap->used;

	heap->used += bytes;

	return true;
}

static void heap_cleanup(ConfigHeap* heap)
{
	minos::unreserve(heap->ptr);
}



static bool is_valid_char_after_token_type(ConfigTokenType type, char8 c) noexcept
{
	if (c == '\0' || c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',' || c == '[' || c == ']' || c == '{' || c == '}')
		return true;

	if (type == ConfigTokenType::Identity)
		return c == '.';

	return type != ConfigTokenType::Integer
		&& type != ConfigTokenType::String
		&& type != ConfigTokenType::LiteralString
		&& type != ConfigTokenType::MultilineString
		&& type != ConfigTokenType::MultilineLiteralString;
}

static void skip_whitespace(ConfigParseState* s) noexcept
{
	while (true)
	{
		const char8 c = *s->curr;

		if (c == '#')
		{
			s->curr += 1;

			while (*s->curr != '\0' && *s->curr != '\n')
				s->curr += 1;
		}
		else if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
		{
			break;
		}

		s->curr += 1;
	}
}

static void advance(ConfigParseState* s) noexcept
{
	skip_whitespace(s);

	const char8 c = *s->curr;

	const char8* const token_beg = s->curr;

	const char8* token_error = nullptr;

	ConfigTokenType token_type = ConfigTokenType::NONE;

	if (c >= '0' && c <= '9')
	{
		token_type = ConfigTokenType::Integer;

		if (c == '0')
		{
			if (s->curr[1] == 'x')
			{
				s->curr += 2;

				while ((*s->curr >= '0' && *s->curr <= '9') || (*s->curr >= 'a' && *s->curr <= 'f') || (*s->curr >= 'A' && *s->curr <= 'F'))
					s->curr += 1;
			}
			else if (s->curr[1] == 'o')
			{
				s->curr += 2;

				while (*s->curr >= '0' && *s->curr <= '7')
					s->curr += 1;
			}
			else if (s->curr[1] == 'b')
			{
				s->curr += 2;

				while (*s->curr == '0' || *s->curr == '1')
					s->curr += 1;
			}
			else
			{
				s->curr += 1;
			}
		}
		else
		{
			s->curr += 1;

			while (*s->curr >= '0' && *s->curr <= '9')
				s->curr += 1;
		}
	}
	else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
	{
		token_type = ConfigTokenType::Identity;

		s->curr += 1;

		while ((*s->curr >= 'a' && *s->curr <= 'z') || (*s->curr >= 'A' && *s->curr <= 'Z') || (*s->curr >= '0' && *s->curr <= '9') || *s->curr == '_' || *s->curr == '-')
			s->curr += 1;
	}
	else if (c < ' ')
	{
		if (c == '\0' && s->curr == s->end)
		{
			token_type = ConfigTokenType::End;
		}
		else
		{
			token_type = ConfigTokenType::INVALID;

			token_error = "Encountered unexpected control character";
		}
	}
	else switch (c)
	{
	case '\'':
	{
		if (s->curr[1] == '\'' && s->curr[2] == '\'')
		{
			token_type = ConfigTokenType::MultilineLiteralString;

			s->curr += 3;

			while (true)
			{
				const char8 c1 = *s->curr;

				if (c1 == '\'' && s->curr[1] == '\'' && s->curr[2] == '\'')
				{
					s->curr += 3;

					break;
				}
				else if (c1 == '\0')
				{
					token_type = ConfigTokenType::INVALID;

					token_error = "String not ended properly";

					break;
				}

				s->curr += 1;
			}
		}
		else
		{
			token_type = ConfigTokenType::LiteralString;

			s->curr += 1;

			while (true)
			{
				const char8 c1 = *s->curr;

				if (c1 == '\'')
				{
					s->curr += 1;

					break;
				}
				else if (c1 == '\0' || c1 == '\r' || c1 == '\n')
				{
					token_type = ConfigTokenType::INVALID;

					token_error = "String not ended properly";

					break;
				}

				s->curr += 1;
			}
		}

		break;
	}

	case '"':
	{
		if (s->curr[1] == '"' && s->curr[2] == '"')
		{
			token_type = ConfigTokenType::MultilineString;

			s->curr += 3;

			while (true)
			{
				const char8 c1 = *s->curr;

				if (c1 == '"' && s->curr[1] == '"' && s->curr[2] == '"')
				{
					s->curr += 3;

					break;
				}
				else if (s->curr == '\0')
				{
					token_type = ConfigTokenType::INVALID;

					token_error = "String not ended properly";

					break;
				}
				else if (c1 == '\\')
				{
					s->curr += 1;
				}

				s->curr += 1;
			}
		}
		else
		{
			token_type = ConfigTokenType::String;

			s->curr += 1;

			while (true)
			{
				const char8 c1 = *s->curr;

				if (c1 == '"')
				{
					s->curr += 1;

					break;
				}
				else if (c1 == '\0' || c1 == '\r' || c1 == '\n')
				{
					token_type = ConfigTokenType::INVALID;

					token_error = "String not ended properly";

					break;
				}
				else if (c1 == '\\')
				{
					s->curr += 1;
				}

				s->curr += 1;
			}
		}

		break;
	}

	case '.':
	{
		token_type = ConfigTokenType::Dot;

		s->curr += 1;

		break;
	}

	case '=':
	{
		token_type = ConfigTokenType::Set;

		s->curr += 1;

		break;
	}

	case '[':
	{
		if (s->curr[1] == '[')
		{
			token_type = ConfigTokenType::DoubleBracketBeg;

			s->curr += 2;
		}
		else
		{
			token_type = ConfigTokenType::BracketBeg;

			s->curr += 1;
		}

		break;
	}

	case ']':
	{
		if (s->curr[1] == ']')
		{
			token_type = ConfigTokenType::DoubleBracketEnd;

			s->curr += 2;
		}
		else
		{
			token_type = ConfigTokenType::BracketEnd;

			s->curr += 1;
		}

		break;
	}

	case '{':
	{
		token_type = ConfigTokenType::CurlyBeg;

		s->curr += 1;

		break;
	}

	case '}':
	{
		token_type = ConfigTokenType::CurlyEnd;

		s->curr += 1;

		break;
	}

	case ',':
	{
		token_type = ConfigTokenType::Comma;

		s->curr += 1;

		break;
	}

	default:
	{
		token_type = ConfigTokenType::INVALID;

		token_error = "Unexpected character";

		while (*s->curr != ' ' && *s->curr != '\t' && *s->curr != '\n' && *s->curr != '\n' && *s->curr != '\0')
			s->curr += 1;

		break;
	}
	}

	ASSERT_OR_IGNORE(token_type != ConfigTokenType::NONE);

	ASSERT_OR_IGNORE((token_type != ConfigTokenType::INVALID) ^ (token_error != nullptr));

	const char8* const token_end = s->curr == token_beg ? s->curr + 1 : s->curr;

	if (token_type != ConfigTokenType::INVALID && !is_valid_char_after_token_type(token_type, *s->curr))
		s->lookahead = ConfigToken{ ConfigTokenType::INVALID, Range<char8>{ s->curr, s->curr + 1 }, "Unexpected character after token" };
	else
		s->lookahead = ConfigToken{ token_type, Range<char8>{ token_beg, token_end }, token_error };
}

static ConfigToken peek_token(ConfigParseState* s, ConfigTokenType expected = ConfigTokenType::NONE) noexcept
{
	const ConfigToken token = s->lookahead;

	ASSERT_OR_IGNORE(token.type != ConfigTokenType::NONE);

	if (expected != ConfigTokenType::NONE && token.type != expected)
		return ConfigToken{ ConfigTokenType::INVALID, token.content, "Unexpected token" };

	return token;
}

static ConfigToken consume_token(ConfigParseState* s, ConfigTokenType expected = ConfigTokenType::NONE) noexcept
{
	const ConfigToken token = s->lookahead;

	ASSERT_OR_IGNORE(token.type != ConfigTokenType::NONE);

	if (expected != ConfigTokenType::NONE && token.type != expected)
		return ConfigToken{ ConfigTokenType::INVALID, token.content, "Unexpected token" };

	advance(s);

	return token;
}



static LineInfo get_line_from_position(ConfigParseState* s, const char8* position) noexcept
{
	if (position == nullptr)
		return LineInfo{ nullptr, nullptr, 0, 0 };

	u32 line_number = 1;

	const char8* curr = s->begin;

	const char8* line_begin = curr;

	while (curr != position)
	{
		if (const char8 c = *curr; c == '\0')
		{
			break;
		}
		else if (c == '\n')
		{
			line_begin = curr + 1;

			line_number += 1;
		}

		curr += 1;
	}

	while (*curr != '\0' && *curr != '\n' && *curr != '\r')
		curr += 1;

	return LineInfo{ line_begin, curr, line_number, static_cast<u32>(position - line_begin) + 1 };
}

static void set_error_impl(ConfigParseError* out, u32 line_number, u32 character_number, Range<char8> context, u32 begin_in_context, u32 end_in_context, const char8* format, va_list vargs) noexcept
{
	out->line_number = line_number;

	out->character_number = character_number;

	ASSERT_OR_IGNORE(context.count() < sizeof(out->context));

	memcpy(out->context, context.begin(), context.count());

	out->context[context.count()] = '\0';

	out->context_begin = begin_in_context;

	out->context_end = end_in_context;

	vsnprintf(out->message, sizeof(out->message), format, vargs);
}

static bool runtime_error(ConfigParseError* out, const char8* format, ...) noexcept
{
	va_list vargs;
	va_start(vargs, format);

	set_error_impl(out, 0, 0, Range<char8>{}, 0, 0, format, vargs);

	va_end(vargs);

	return false;
}

static bool parse_error(ConfigParseState* s, Range<char8> issue, const char8* format, ...) noexcept
{
	static constexpr u32 MAX_LOOKBACK = 80;

	const LineInfo pos = get_line_from_position(s, issue.begin());

	Range<char8> context = {};

	u32 begin_in_context = 0;

	u32 end_in_context = 0;

	const char8* const copy_begin = pos.line_begin > issue.begin() - MAX_LOOKBACK ? pos.line_begin : issue.begin() - MAX_LOOKBACK;

	const u32 copy_count = pos.line_end < copy_begin + sizeof(ConfigParseError::context) ? static_cast<u32>(pos.line_end - copy_begin) : sizeof(ConfigParseError::context);

	context = Range{ copy_begin, copy_count };

	begin_in_context = static_cast<u32>(issue.begin() - copy_begin);

	end_in_context = static_cast<u32>(begin_in_context + issue.count()) < copy_count ? static_cast<u32>(begin_in_context + issue.count()) : copy_count;

	va_list vargs;
	va_start(vargs, format);

	set_error_impl(s->error, pos.line_number, pos.character_number, context, begin_in_context, end_in_context, format, vargs);

	va_end(vargs);

	heap_cleanup(&s->heap);

	return false;
}

static bool alloc_error(ConfigParseState* s) noexcept
{
	heap_cleanup(&s->heap);

	return runtime_error(s->error, "Could not allocate memory");
}



static bool name_equal(Range<char8> text, const char8* name) noexcept
{
	for (uint i = 0; i != text.count(); ++i)
	{
		if (text[i] != name[i])
			return false;
	}

	return true;
}

static ConfigEntry* get_context(ConfigParseState* s) noexcept
{
	ASSERT_OR_IGNORE(s->context_stack_count > 1 && s->context_stack_count <= array_count(s->context_stack));

	return s->context_stack[s->context_stack_count - 1];
}

static bool parse_name_element(const ConfigToken& token, ConfigParseState* s) noexcept
{
	ASSERT_OR_IGNORE(token.type == ConfigTokenType::Identity);

	if (s->context_stack_count == array_count(s->context_stack) - 1)
		return parse_error(s, token.content, "Key nesting limit exceeded");

	ConfigEntry* const context = s->context_stack[s->context_stack_count - 1];

	if (context->type != ConfigEntryType::Container)
		return parse_error(s, token.content, "Tried assigning to a key that does not expect subkeys");

	ConfigEntry* const children = context + 1;

	for (u32 i = 0; i != context->container_child_count; ++i)
	{
		if (name_equal(token.content, children[i].name))
		{
			s->context_stack[s->context_stack_count] = children + i;

			s->context_stack_count += 1;

			return true;
		}
	}

	return parse_error(s, token.content, "Key does not exist");
}

static u32 parse_names(ConfigParseState* s) noexcept
{
	u32 name_count = 1;

	while (true)
	{
		const ConfigToken identity = consume_token(s, ConfigTokenType::Identity);

		if (identity.type == ConfigTokenType::INVALID)
			return static_cast<u32>(parse_error(s, identity.content, "Expected key"));

		if (!parse_name_element(identity, s))
			return 0;

		const ConfigToken next = peek_token(s);

		if (next.type != ConfigTokenType::Dot)
			return name_count;

		name_count += 1;

		advance(s);
	}
}

static void pop_names(ConfigParseState* s, u32 count) noexcept
{
	ASSERT_OR_IGNORE(s->context_stack_count > count);

	s->context_stack_count -= count;
}

static bool parse_unicode_escape_sequence(Range<char8> text, u32 escape_chars, ConfigParseState* s, CodepointBuffer* out) noexcept
{
	if (text.count() < escape_chars)
		return parse_error(s, Range<char8>{ text.begin() - 2, text.count() + 2 }, escape_chars == 4 ? "\\u escape expects four hex digits" : "\\U escape expects eight hex digits");

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
			return parse_error(s, Range{ text.begin() + i, 1 }, "Expected hexadecimal escape character");
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
		return parse_error(s, Range{ text.begin(), escape_chars }, "Escaped codepoint is larger than the maximum unicode codepoint (0x10FFFF)");
	}

	return true;
}

static u32 parse_escape_sequence(Range<char8> text, ConfigParseState* s, CodepointBuffer* out) noexcept
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
		return parse_unicode_escape_sequence(Range{ text.begin() + 2, text.end() }, 4, s, out) ? 6 : 0;

	case 'U':
		return parse_unicode_escape_sequence(Range{ text.begin() + 2, text.end() }, 8, s, out) ? 10 : 0;

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
		return static_cast<u32>(parse_error(s, Range{ text.begin(), 2 }, "Unexpected escape sequence")); 
	}
}

static bool parse_literal_string_base(Range<char8> string, ConfigParseState* s) noexcept
{
	ConfigEntry* const context = get_context(s);

	context->source = string;

	if (context->type != ConfigEntryType::String)
		return parse_error(s, string, "Cannot assign string to key expecting different value");

	if (string[0] == '\n')
		string = Range{ string.begin() + 1, string.end() };
	else if (string[0] == '\r' && string[1] == '\n')
		string = Range{ string.begin() + 2, string.end() };

	void* allocation;

	if (!heap_alloc(&s->heap, static_cast<u32>(string.count()), &allocation))
		return alloc_error(s);

	memcpy(allocation, string.begin(), string.count());

	const Range<char8> value = Range{ static_cast<const char8*>(allocation), string.count() };

	*reinterpret_cast<Range<char8>*>(reinterpret_cast<byte*>(s->config) + context->offset) = value;

	return true;
}

static bool parse_escaped_string_base(Range<char8> string, ConfigParseState* s) noexcept
{
	ConfigEntry* const context = get_context(s);

	context->source = string;

	if (context->type != ConfigEntryType::String)
		return parse_error(s, string, "Cannot assign string to key expecting different value");

	if (string[0] == '\n')
		string = Range{ string.begin() + 1, string.end() };
	else if (string[0] == '\r' && string[1] == '\n')
		string = Range{ string.begin() + 2, string.end() };

	void* allocation_begin;

	if (!heap_alloc(&s->heap, 0, &allocation_begin))
		panic("config parsing ran out of heap space\n");

	u32 uncopied_begin = 0;

	u32 i = 0;

	while (i < static_cast<u32>(string.count()))
	{
		if (string[i] == '\\')
		{
			CodepointBuffer utf8;

			const u32 escape_chars = parse_escape_sequence(Range{ string.begin() + i, string.end() }, s, &utf8);

			if (escape_chars == 0)
				return false;

			const u32 uncopied_length = i - uncopied_begin;

			void* allocation;

			if (!heap_alloc(&s->heap, uncopied_length + utf8.length, &allocation))
				return alloc_error(s);

			memcpy(allocation, string.begin() + uncopied_begin, uncopied_length);

			memcpy(static_cast<byte*>(allocation) + uncopied_length, utf8.buf, utf8.length);

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

	void* allocation;

	if (!heap_alloc(&s->heap, uncopied_length, &allocation))
		panic("config parsing ran out of heap space\n");

	memcpy(allocation, string.begin() + uncopied_begin, uncopied_length);

	const Range<char8> value = Range{ static_cast<const char8*>(allocation_begin), static_cast<const char8*>(allocation) + uncopied_length };

	*reinterpret_cast<Range<char8>*>(reinterpret_cast<byte*>(s->config) + context->offset) = value;

	return true;
}

static bool parse_inline_table(const ConfigToken& token, ConfigParseState* s) noexcept
{
	ASSERT_OR_IGNORE(token.type == ConfigTokenType::CurlyBeg);

	// Empty inline table is a special case
	if (peek_token(s).type == ConfigTokenType::CurlyEnd)
	{
		advance(s);

		return true;
	}

	while (true)
	{
		const u32 name_depth = parse_names(s);

		if (name_depth == 0)
			return false;

		const ConfigToken set = consume_token(s, ConfigTokenType::Set);

		if (set.type != ConfigTokenType::Set)
			return parse_error(s, set.content, "Expected '='");

		if (!parse_value(s))
			return false;

		pop_names(s, name_depth);

		const ConfigToken curly_end_or_comma = consume_token(s);

		if (curly_end_or_comma.type == ConfigTokenType::CurlyEnd)
			return true;
		else if (curly_end_or_comma.type != ConfigTokenType::Comma)
			return parse_error(s, curly_end_or_comma.content, "Expected '}' or ','");
	}
}

static bool parse_boolean(const ConfigToken& token, ConfigParseState* s) noexcept
{
	ConfigEntry* const context = get_context(s);

	context->source = token.content;

	bool value;

	if (name_equal(token.content, "true"))
		value = true;
	else if (name_equal(token.content, "false"))
		value = false;
	else
		return parse_error(s, token.content, "Expected a value");

	if (context->type != ConfigEntryType::Boolean)
		return parse_error(s, token.content, "Cannot assign boolean to key expecting different value");

	*reinterpret_cast<bool*>(reinterpret_cast<byte*>(s->config) + context->offset) = value;

	return true;
}

static bool parse_integer(const ConfigToken& token, ConfigParseState* s) noexcept
{
	ConfigEntry* const context = get_context(s);

	context->source = token.content;

	if (context->type != ConfigEntryType::Integer)
		return parse_error(s, token.content, "Cannot assign integer to key expecting different value");

	const Range<char8> text = token.content;

	u64 value = 0;

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
			for (uint i = 2; i != text.count(); ++i)
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

				if (value != static_cast<u32>(value))
					return parse_error(s, text, "The given integer exeeds the allowed maximum of 2^32 -1");
			}
		}
		else if (text[1] == 'o')
		{
			for (uint i = 2; i != text.count(); ++i)
			{
				const char8 c = text[i];

				ASSERT_OR_IGNORE(c >= '0' && c <= '7');

				value = value * 8 + c - '0';

				if (value != static_cast<u32>(value))
					return parse_error(s, text, "The given integer exeeds the allowed maximum of 2^32 -1");
			}
		}
		else // if (text[1] == 'b')
		{
			ASSERT_OR_IGNORE(text[1] == 'b');
			
			for (uint i = 2; i != text.count(); ++i)
			{
				const char8 c = text[i];

				ASSERT_OR_IGNORE(c == '0' || c == '1');

				value = value * 2 + c - '0';

				if (value != static_cast<u32>(value))
					return parse_error(s, text, "The given integer exeeds the allowed maximum of 2^32 -1");
			}
		}
	}
	else
	{
		for (const char8 c : text)
		{
			ASSERT_OR_IGNORE(c >= '0' && c <= '9');

			value = value * 10 + c - '0';

			if (value != static_cast<u32>(value))
				return parse_error(s, text, "The given integer exeeds the allowed maximum of 2^32 -1");
		}
	}

	*reinterpret_cast<u32*>(reinterpret_cast<byte*>(s->config) + context->offset) = static_cast<u32>(value);

	return true;
}

static bool parse_string(const ConfigToken& token, ConfigParseState* s) noexcept
{
	ASSERT_OR_IGNORE(token.content.count() >= 2);

	return parse_escaped_string_base(Range{ token.content.begin() + 1, token.content.end() - 1 }, s);
}

static bool parse_literal_string(const ConfigToken& token, ConfigParseState* s) noexcept
{
	ASSERT_OR_IGNORE(token.content.count() >= 2);

	return parse_literal_string_base(Range{ token.content.begin() + 1, token.content.end() - 1}, s);
}

static bool parse_multiline_string(const ConfigToken& token, ConfigParseState* s) noexcept
{
	ASSERT_OR_IGNORE(token.content.count() >= 6);

	return parse_escaped_string_base(Range{ token.content.begin() + 3, token.content.end() - 3 }, s);
}

static bool parse_multiline_literal_string(const ConfigToken& token, ConfigParseState* s) noexcept
{
	ASSERT_OR_IGNORE(token.content.count() >= 6);

	return parse_literal_string_base(Range{ token.content.begin() + 3, token.content.end() - 3}, s);
}

static bool parse_value(ConfigParseState* s) noexcept
{
	ConfigEntry* const context = get_context(s);

	const ConfigToken token = consume_token(s);

	if (context->seen)
		return parse_error(s, token.content, "Cannot assign to the same key more than once");

	context->seen = true;

	switch (token.type)
	{
	case ConfigTokenType::BracketBeg:
		return parse_error(s, token.content, "Arrays are currently not supported");

	case ConfigTokenType::CurlyBeg:
		return parse_inline_table(token, s);

	case ConfigTokenType::Identity:
		return parse_boolean(token, s);

	case ConfigTokenType::Integer:
		return parse_integer(token, s);

	case ConfigTokenType::String:
		return parse_string(token, s);

	case ConfigTokenType::LiteralString:
		return parse_literal_string(token, s);

	case ConfigTokenType::MultilineString:
		return parse_multiline_string(token, s);

	case ConfigTokenType::MultilineLiteralString:
		return parse_multiline_literal_string(token, s);

	default:
		return parse_error(s, token.content, token.tokenizer_error == nullptr ? "Expected a value" : token.tokenizer_error);
	}
}



static bool validate_config(ConfigParseState* s) noexcept
{
	for (u32 i = 0; i != array_count(config_template); ++i)
	{
		const ConfigEntry& e = s->config_entries[i];

		if (!e.seen || e.type != ConfigEntryType::Integer)
			continue;

		const ConfigEntryTemplate& tpl = config_template[i];

		const u32 value = *reinterpret_cast<const u32*>(reinterpret_cast<const byte*>(s->config) + tpl.offset);

		if (const u32 min = tpl.integer.min, max = tpl.integer.max; min == 0 && value > max)
		{
			return parse_error(s, e.source, "The value of %s (%u) is  greater than the allowed maximum of %u",
				tpl.name, value, max);
		}
		else if (max == ~0u && value < min)
		{
			return parse_error(s, e.source, "The value of %s (%u) is less than the allowed minimum of %u",
				tpl.name, value, min);
		}
		else if (value < min || value > max)
		{
			return parse_error(s, e.source, "The value of %s (%u) is outside the expected range of %u to %u",
				tpl.name, value, min, max);
		}
		else if (const u32 factor = tpl.integer.factor; factor != 0 && value % factor != 0)
		{
			return parse_error(s, e.source, "The value of %s (%u) is not a multiple of %u",
				tpl.name, value, factor);
		}
	}

	return true;
}

static void init_config_to_defaults(Config* out) noexcept
{
	for (const ConfigEntryTemplate& tpl : config_template)
	{
		void* const target = reinterpret_cast<byte*>(out) + tpl.offset;

		switch (tpl.type)
		{
		case ConfigEntryType::Container:
			break;

		case ConfigEntryType::Integer:
			*static_cast<u32*>(target) = tpl.integer.default_value;
			break;

		case ConfigEntryType::Boolean:
			*static_cast<bool*>(target) = tpl.boolean.default_value;
			break;

		case ConfigEntryType::String:
			*static_cast<Range<char8>*>(target) = tpl.string.default_value;
			break;

		default:
			ASSERT_UNREACHABLE;
		}
	}
}

static bool parse_config(const char8* config_string, u32 config_string_chars, ConfigParseError* out_error, Config* out) noexcept
{
	init_config_to_defaults(out);

	ConfigParseState state;
	state.config = out;
	state.curr = config_string;
	state.end = config_string + config_string_chars;
	state.begin = config_string;
	state.lookahead.type = ConfigTokenType::NONE;
	state.context_stack_count = 1;
	state.context_stack[0] = state.config_entries;
	state.error = out_error;

	for (u32 i = 0; i != sizeof(config_template) / sizeof(ConfigEntryTemplate); ++i)
		state.config_entries[i] = ConfigEntry{ config_template[i] };

	if (!heap_init(&state.heap))
		return alloc_error(&state);

	out->m_heap_ptr_ = state.heap.ptr;

	advance(&state);

	while (true)
	{
		const ConfigToken token = peek_token(&state);

		switch (token.type)
		{
		case ConfigTokenType::BracketBeg:
		{
			advance(&state);

			state.context_stack_count = 1;

			if (parse_names(&state) == 0)
				return false;

			const ConfigToken bracket_end = consume_token(&state, ConfigTokenType::BracketEnd);

			if (bracket_end.type == ConfigTokenType::INVALID)
				return parse_error(&state, bracket_end.content, "Expected ']' or '.'");

			break;
		}

		case ConfigTokenType::DoubleBracketBeg:
		{
			return parse_error(&state, token.content, "Arrays of Tables are not currently supported");
		}

		case ConfigTokenType::Identity:
		{
			const u32 name_depth = parse_names(&state);

			if (name_depth == 0)
				return false;

			const ConfigToken set = consume_token(&state, ConfigTokenType::Set);

			if (set.type == ConfigTokenType::INVALID)
				return parse_error(&state, set.content, "Expected '=' or '.'");

			if (!parse_value(&state))
				return false;

			pop_names(&state, name_depth);

			break;
		}

		case ConfigTokenType::End:
		{
			return validate_config(&state);
		}

		default:
		{
			return parse_error(&state, token.content, "Unexpected token");
		}
		}
	}
}



bool read_config_from_file(const char8* config_filepath, ConfigParseError* out_error, Config* out) noexcept
{
	minos::FileHandle filehandle;

	if (!minos::file_create(range_from_cstring(config_filepath), minos::Access::Read, minos::CreateMode::Open, minos::AccessPattern::Sequential, minos::SyncMode::Synchronous, &filehandle))
		return runtime_error(out_error, "Could not open file");

	minos::FileInfo fileinfo;

	if (!minos::file_get_info(filehandle, &fileinfo))
		return runtime_error(out_error, "Could not determine file length");

	if (fileinfo.file_bytes > UINT32_MAX)
		return runtime_error(out_error, "Config file exceeds the maximum size of 4GB");

	char8 stack_buffer[8192];

	char8* buffer;

	if (sizeof(stack_buffer) <= fileinfo.file_bytes)
	{
		buffer = static_cast<char8*>(minos::reserve(fileinfo.file_bytes));

		if (buffer == nullptr)
			return runtime_error(out_error, "Failed to allocate buffer");

		if (!minos::commit(buffer, fileinfo.file_bytes))
		{
			minos::unreserve(buffer);

			return runtime_error(out_error, "Failed to allocate buffer");
		}
	}
	else
	{
		buffer = stack_buffer;
	}

	minos::Overlapped overlapped{};

	if (!minos::file_read(filehandle, buffer, static_cast<u32>(fileinfo.file_bytes), &overlapped))
			return runtime_error(out_error, "Could not read config file");

	minos::file_close(filehandle);

	buffer[fileinfo.file_bytes] = '\0';

	const bool parse_ok = parse_config(buffer, static_cast<u32>(fileinfo.file_bytes), out_error, out);

	if (buffer != stack_buffer)
		minos::unreserve(buffer);

	return parse_ok;
}

void deinit_config(Config* config) noexcept
{
	minos::unreserve(config->m_heap_ptr_);
}

void print_config(const Config* config) noexcept
{
	u32 indent_stack[8];
	indent_stack[0] = config_template[0].container.child_count;

	u32 indent_count = 1;

	u32 indent = 0;

	for (u32 i = 1; i != array_count(config_template); ++i)
	{
		const ConfigEntryTemplate& tpl = config_template[i];

		switch (tpl.type)
		{
		case ConfigEntryType::Container:
		{
			printf("%*s%s = {\n", indent, "", tpl.name);

			indent_stack[indent_count - 1] -= tpl.container.child_count;

			ASSERT_OR_IGNORE(indent_count + 1 < array_count(indent_stack));

			indent_stack[indent_count] = tpl.container.child_count;

			indent_count += 1;

			indent += 4;

			break;
		}

		case ConfigEntryType::Integer:
		{
			const u32 value = *reinterpret_cast<const u32*>(reinterpret_cast<const byte*>(config) + tpl.offset);

			printf("%*s%s = %u\n", indent, "", tpl.name, value);

			break;
		}

		case ConfigEntryType::Boolean:
		{
			const bool value = *reinterpret_cast<const bool*>(reinterpret_cast<const byte*>(config) + tpl.offset);

			printf("%*s%s = %s\n", indent, "", tpl.name, value ? "true" : "false");

			break;
		}

		case ConfigEntryType::String:
		{
			const Range<char8> value = *reinterpret_cast<const Range<char8>*>(reinterpret_cast<const byte*>(config) + tpl.offset);

			printf("%*s%s = '%.*s'\n", indent, "", tpl.name, static_cast<u32>(value.count()), value.begin());

			break;
		}

		default: ASSERT_UNREACHABLE;
		}

		ASSERT_OR_IGNORE(indent_count != 0);

		while (indent_count > 0 && indent_stack[indent_count - 1] == 0)
		{
			indent_count -= 1;

			indent -= 4;

			printf("%*s}\n", indent, "");
		}

		indent_stack[indent_count - 1] -= 1;
	}

	ASSERT_OR_IGNORE(indent_count == 1 && indent_stack[0] == 0);
}

void print_config_help(u32 depth) noexcept
{
	printf("config parameters:\n");

	ASSERT_OR_IGNORE(config_template[0].type == ConfigEntryType::Container);

	u32 indent_stack[8];
	indent_stack[0] = config_template[0].container.child_count;

	u32 indent_count = 1;

	u32 indent = 0;

	for (u32 i = 1; i != array_count(config_template); ++i)
	{
		const ConfigEntryTemplate& tpl = config_template[i];

		printf("\n");

		switch (tpl.type)
		{
		case ConfigEntryType::Container:
		{
			printf("%*s[%s]\n%*s  %s\n", indent, "", tpl.name, indent, "", tpl.helptext);

			indent_stack[indent_count - 1] -= tpl.container.child_count;

			if (indent_count < depth || depth == 0)
			{
				ASSERT_OR_IGNORE(indent_count + 1 < array_count(indent_stack));

				indent_stack[indent_count] = tpl.container.child_count;

				indent_count += 1;

				indent += 4;
			}
			else
			{
				i += tpl.container.child_count;
			}

			break;
		}

		case ConfigEntryType::Integer:
		{
			printf("%*s[%s] - Integer (default: %u | range: %u - %u", indent, "", tpl.name, tpl.integer.default_value, tpl.integer.min, tpl.integer.max);

			if (tpl.integer.factor != 0)
				printf(" | factor: %u", tpl.integer.factor);

			printf(")\n%*s  %s\n", indent, "", tpl.helptext);

			break;
		}

		case ConfigEntryType::Boolean:
		{
			printf("%*s[%s] - Boolean (default: %s)\n%*s  %s\n", indent, "", tpl.name, tpl.boolean.default_value ? "true" : "false", indent, "", tpl.helptext);

			break;
		}

		case ConfigEntryType::String:
		{
			printf("%*s[%s] - String (default: '%.*s')\n%*s  %s\n", indent, "", tpl.name, static_cast<s32>(tpl.string.default_value.count()), tpl.string.default_value.begin(), indent, "", tpl.helptext);

			break;
		}

		default: ASSERT_UNREACHABLE;
		}

		ASSERT_OR_IGNORE(indent_count != 0);

		while (indent_count > 0 && indent_stack[indent_count - 1] == 0)
		{
			indent_count -= 1;

			indent -= 4;
		}

		indent_stack[indent_count - 1] -= 1;
	}

	ASSERT_OR_IGNORE(indent_count == 1 && indent_stack[0] == 0);
}
