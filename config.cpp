#include "config.hpp"

#include "common.hpp"
#include "minos.hpp"
#include <cstddef>
#include <type_traits>
#include <cstring>

enum class ConfigType : u8
{
	NONE = 0,
	Container,
	Integer,
	Boolean,
	String,
};

struct ConfigEntry
{
	ConfigType type;

	bool seen;

	u32 offset;

	const char8* name;

	union
	{
		u32 container_child_count;

		u32 integer_default;

		bool boolean_default;

		const char8* string_default;
	};

	ConfigEntry() noexcept = default;

	constexpr ConfigEntry(ConfigType type, u32 offset, const char8* name, u32 default_value_or_child_count)
		: type{ type }, seen{ false }, offset{ offset }, name{ name }, integer_default{ default_value_or_child_count } {}

	constexpr ConfigEntry(ConfigType type, u32 offset, const char8* name, bool default_value)
		: type{ type }, seen{ false }, offset{ offset }, name{ name }, boolean_default{ default_value } {}

	constexpr ConfigEntry(ConfigType type, u32 offset, const char8* name, const char8* default_value)
		: type{ type }, seen{ false }, offset{ offset }, name{ name }, string_default{ default_value } {}
};

#define CONFIG_CONTAINER(name, member, parent_type, child_count) \
	ConfigEntry{ ConfigType::Container, static_cast<u32>(offsetof(parent_type, member)), name, child_count }

#define CONFIG_INTEGER(name, member, parent_type, default_value) \
	ConfigEntry{ ConfigType::Integer, static_cast<u32>(offsetof(parent_type, member)), name, default_value }

#define CONFIG_BOOLEAN(name, member, parent_type, default_value) \
	ConfigEntry{ ConfigType::Boolean, static_cast<u32>(offsetof(parent_type, member)), name, default_value }

#define CONFIG_STRING(name, member, parent_type, default_value) \
	ConfigEntry{ ConfigType::String, static_cast<u32>(offsetof(parent_type, member)), name, default_value }



static constexpr ConfigEntry config_template[] {
	ConfigEntry{ ConfigType::Container, 0, nullptr, 18u },
	CONFIG_CONTAINER("entrypoint", entrypoint, Config, 2u ),
		CONFIG_STRING("filepath", entrypoint.filepath, Config, nullptr),
		CONFIG_STRING("symbol", entrypoint.symbol, Config, nullptr),
	CONFIG_CONTAINER("input", input, Config, 5u),
		CONFIG_INTEGER("bytes-per-read", input.bytes_per_read, Config, 65'536u),
		CONFIG_INTEGER("max-concurrent-reads", input.max_concurrent_reads, Config, 16u),
		CONFIG_INTEGER("max-concurrent-files", input.max_concurrent_files, Config, 8u),
		CONFIG_INTEGER("max-concurrent-reads-per-file", input.max_concurrent_reads_per_file, Config, 2u),
		CONFIG_INTEGER("max-pending-files", input.max_pending_files, Config, 4096u),
	CONFIG_CONTAINER("memory", memory, Config, 8u),
		CONFIG_CONTAINER("files", memory.files, Config, 7u),
			CONFIG_INTEGER("reserve", memory.files.reserve, Config, 4096u),
			CONFIG_INTEGER("initial-commit", memory.files.initial_commit, Config, 4096u),
			CONFIG_INTEGER("commit-increment", memory.files.commit_increment, Config, 4096u),
			CONFIG_CONTAINER("lookup", memory.files.lookup, Config, 3u),
				CONFIG_INTEGER("reserve", memory.files.lookup.reserve, Config, 4096u),
				CONFIG_INTEGER("initial-commit", memory.files.lookup.initial_commit, Config, 4096u),
				CONFIG_INTEGER("commit-increment", memory.files.lookup.commit_increment, Config, 4096u),
};



struct ConfigParseState
{
	const char8* curr;

	u32 line;

	u32 character;

	Config* config;

	u32 context_stack_count;

	ConfigEntry* context_stack[8];

	ConfigEntry config_entries[array_count(config_template)];

	ConfigParseError* error;

	const char8* end;
};

static bool is_name_char(const char8 c) noexcept
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-';
}

static bool name_equal(const char8* curr, const char8* name, const char8** out_next) noexcept
{
	u32 i = 0;

	while (true)
	{
		const char8 c = curr[i];

		if (!is_name_char(c))
		{
			if (name[i] != '\0')
				return false;

			*out_next = curr + i;

			return true;
		}

		if (c != name[i])
			return false;

		i += 1;
	}
}

static bool parse_error(ConfigParseState* s, const char8* message) noexcept
{
	static constexpr u32 MAX_PREV_COPY = 40;

	static constexpr u32 MAX_CONTEXT = sizeof(s->error->context);

	static_assert(MAX_PREV_COPY < sizeof(s->error->context) / 2);

	ConfigParseError* const error = s->error;

	error->message = message;

	error->line = s->line;

	error->character = s->character;

	const u32 adj_character = s->character - 1;

	u32 prev_copy = adj_character;

	u32 copy_offset = 0;

	if (adj_character > MAX_PREV_COPY)
	{
		prev_copy = MAX_PREV_COPY;

		copy_offset = 4;

		error->context[0] = '.';
		error->context[1] = '.';
		error->context[2] = '.';
		error->context[3] = ' ';
	}

	error->character_in_context = prev_copy + copy_offset;

	memcpy(error->context + copy_offset, s->curr - prev_copy, prev_copy);

	u32 i = 0;

	while (i + prev_copy + copy_offset < MAX_CONTEXT - 5)
	{
		const char8 c = s->curr[i];

		if (c == '\0' || c == '\n' || (c == '\r' && s->curr[i + 1] == '\n'))
		{
			error->context[i + prev_copy + copy_offset] = '\0';

			return false;
		}

		error->context[i + prev_copy + copy_offset] = c;

		i += 1;
	}

	error->context[MAX_CONTEXT - 5] = ' ';
	error->context[MAX_CONTEXT - 4] = '.';
	error->context[MAX_CONTEXT - 3] = '.';
	error->context[MAX_CONTEXT - 2] = '.';
	error->context[MAX_CONTEXT - 1] = '\0';

	return false;
}

static void advance(ConfigParseState* s) noexcept
{
	while (true)
	{
		const char8 c = *s->curr;

		if (c == ' ' || c == '\t' || c == '\r')
		{
			s->curr += 1;

			s->character += 1;
		}
		else if (c == '\n')
		{
			s->curr += 1;

			s->character = 1;

			s->line += 1;
		}
		else
		{
			break;
		}
	}
}

static bool is_at_name(const ConfigParseState* s)
{
	const char8 c = *s->curr;

	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-';
}

static bool next_char_if(ConfigParseState* s, char8 c, bool skip_advance = false) noexcept
{
	if (*s->curr != c)
		return false;

	s->character += 1;

	s->curr += 1;

	if (!skip_advance)
		advance(s);

	return true;
}

static void reset_context(ConfigParseState* s) noexcept
{
	s->context_stack_count = 1;
}

static void pop_contexts(ConfigParseState* s, u32 count) noexcept
{
	ASSERT_OR_IGNORE(count < s->context_stack_count);

	s->context_stack_count -= count;
}

static bool push_single_context(ConfigParseState* s) noexcept
{
	ConfigEntry* const parent = s->context_stack[s->context_stack_count - 1];

	if (parent->type != ConfigType::Container)
		return parse_error(s, "Expected value instead of key");

	ConfigEntry* const children = parent + 1;

	for (u32 i = 0; i != parent->container_child_count; ++i)
	{
		const char8* next;

		if (name_equal(s->curr, children[i].name, &next))
		{
			s->character += static_cast<u32>(next - s->curr);

			s->curr = next;

			advance(s);

			s->context_stack[s->context_stack_count] = children + i;

			s->context_stack_count += 1;

			return true;
		}

		if (children[i].type == ConfigType::Container)
			i += children[i].container_child_count;
	}

	return parse_error(s, "Unexpected key");
}

static u32 push_contexts(ConfigParseState* s) noexcept
{
	if (!push_single_context(s))
		return 0;

	u32 push_count = 1;

	while (next_char_if(s, '.'))
	{
		if (!push_single_context(s))
		{
			pop_contexts(s, push_count);

			return 0;
		}

		push_count += 1;
	}

	return push_count;
}

static bool parse_value(ConfigParseState* s) noexcept
{
	ConfigEntry* const context = s->context_stack[s->context_stack_count - 1];

	if (context->type == ConfigType::Container)
		return parse_error(s, "Cannot assign value to key not expecting a value");

	if (context->seen)
		return false;

	const char8* curr = s->curr;

	switch (context->type)
	{
	case ConfigType::Integer:
	{
		u32 value = 0;

		if (*curr == '0')
		{
			if (curr[1] == 'x')
			{
				curr += 2;

				if ((*curr < '0' || *curr > '9') && (*curr < 'a' || *curr > 'f') && (*curr < 'A' || *curr > 'F'))
					return parse_error(s, "Integer needs at least one digit");

				while (true)
				{
					if (*curr >= '0' && *curr <= '9')
						value = value * 16 + *curr - '0';
					else if (*curr >= 'a' && *curr <= 'f')
						value = value * 16 + *curr - 'a' + 10;
					else if (*curr >= 'A' && *curr <= 'F')
						value = value * 16 + *curr - 'A' + 10;
					else
						break;

					curr += 1;
				}
			}
			else if (curr[1] == 'o')
			{
				curr += 2;

				if (*curr < '0' || *curr > '7')
					return parse_error(s, "Integer needs at least one digit");

				while (*curr >= '0' || *curr <= '7')
				{
					value = value * 8 + *curr - '0';

					curr += 1;
				}
			}
			else if (curr[1] == 'b')
			{
				curr += 2;

				if (*curr != '0' && *curr != '1')
					return parse_error(s, "Integer needs at least one digit");

				while (*curr == '0' || *curr == '1')
				{
					value = value * 2 + *curr - '0';

					curr += 1;
				}
			}
			else if (curr[1] < '0' || curr[1] > '9')
			{
				curr += 1;
			}
			else
			{
				return parse_error(s, "Integer may not contain leading zeroes");
			}
		}
		else if (*curr >= '1' && *curr <= '9')
		{
			while (*curr >= '0' && *curr <= '9')
			{
				value = value * 10 + *curr - '0';

				curr += 1;
			}
		}

		if (is_name_char(*curr))
			return parse_error(s, "Integer value must be followed by whitespace");

		*reinterpret_cast<u32*>(reinterpret_cast<byte*>(s->config) + context->offset) = value;

		s->character += static_cast<u32>(curr - s->curr);

		s->curr = curr;

		break;
	}

	case ConfigType::Boolean:
	{
		bool value;

		if (name_equal(curr, "true", &curr))
			value = true;
		else if (name_equal(curr, "false", &curr))
			value = false;
		else
			return false;

		if (is_name_char(*curr))
			return false;

		*reinterpret_cast<bool*>(reinterpret_cast<byte*>(s->config) + context->offset) = value;

		s->character += static_cast<u32>(curr - s->curr);

		s->curr = curr;

		break;
	}

	case ConfigType::String:
	{
		Range<char8> value;

		// @TODO: Implement string parsing

		return parse_error(s, "Strings are not currently supported");
	}

	default:
		ASSERT_UNREACHABLE;
	}

	context->seen = true;

	advance(s);

	return true;
}

static bool parse_inline_table(ConfigParseState* s) noexcept
{
	if (s->context_stack[s->context_stack_count - 1]->type != ConfigType::Container)
		return parse_error(s, "Cannot assign an Inline Table to a key expecting a value");

	while (true)
	{
		const u32 pushed_count = push_contexts(s);

		if (pushed_count == 0)
			return false;

		if (!next_char_if(s, '='))
			return parse_error(s, "Expected '=' after key");

		if (next_char_if(s, '{'))
		{
			if (!parse_inline_table(s))
				return false;
		}
		else
		{
			if (!parse_value(s))
				return false;
		}

		pop_contexts(s, pushed_count);

		if (next_char_if(s, '}'))
			return true;

		if (!next_char_if(s, ','))
			return parse_error(s, "Expected '}' or ',' after key-value pair in Inline Table");
	}
}

static bool is_at_end(const ConfigParseState* s) noexcept
{
	return *s->curr == '\0' && s->curr == s->end;
}



static void set_config_defaults(MutRange<ConfigEntry> entries, Config* out) noexcept
{
	for (ConfigEntry& entry : entries)
	{
		if (entry.seen)
			continue;

		void* const target = reinterpret_cast<byte*>(out) + entry.offset;

		switch (entry.type)
		{
		case ConfigType::Container:
			continue;

		case ConfigType::Integer:
			*static_cast<u32*>(target) = entry.integer_default;
			break;

		case ConfigType::Boolean:
			*static_cast<bool*>(target) = entry.boolean_default;
			break;

		case ConfigType::String:
			*static_cast<Range<char8>*>(target) = entry.string_default == nullptr ? Range<char8>{} : range_from_cstring(entry.string_default);
			break;
		
		default:
			ASSERT_UNREACHABLE;
		}
	}
}

static bool validate_config(const Config* config) noexcept
{
	// @TODO: Implement validation logic

	config;

	return true;
}

static bool parse_config(const char8* config_string, u32 config_string_chars, ConfigParseError* out_error, Config* out) noexcept
{
	memset(out, 0, sizeof(*out));

	ConfigParseState state;
	state.curr = config_string;
	state.line = 1;
	state.character = 1;
	state.config = out;
	state.context_stack_count = 1;
	state.context_stack[0] = state.config_entries;
	memcpy(state.config_entries, config_template, sizeof(config_template));
	state.error = out_error;
	state.end = config_string + config_string_chars;

	ConfigParseState* const s = &state;

	advance(s);

	while (true)
	{
		if (next_char_if(s, '[', true))
		{
			if (next_char_if(s, '['))
			{
				return parse_error(s, "Arrays of Tables are not currently supported");
			}
			else
			{
				advance(s);

				reset_context(s);

				if (push_contexts(s) == 0)
					return false;

				if (!next_char_if(s, ']'))
					return parse_error(s, "Expected ']' at end of Table definition");
			}
		}
		else if (is_at_name(s))
		{
			const u32 pushed_count = push_contexts(s);
			
			if (pushed_count == 0)
				return false;

			if (!next_char_if(s, '='))
				return parse_error(s, "Expected '=' after key");

			if (next_char_if(s, '{'))
			{
				if (!parse_inline_table(s))
					return false;
			}
			else if (next_char_if(s, '['))
			{
				return parse_error(s, "Arrays are not currently supported");
			}
			else
			{
				if (!parse_value(s))
					return false;
			}

			pop_contexts(s, pushed_count);
		}
		else if (is_at_end(s))
		{
			break;
		}
		else
		{
			return parse_error(s, "Unexpected character");
		}
	}

	set_config_defaults(MutRange{ state.config_entries }, out);

	return validate_config(out);
}

bool read_config_from_file(const char8* config_filepath, ConfigParseError* out_error, Config* out) noexcept
{
	minos::FileHandle filehandle;

	if (!minos::file_create(range_from_cstring(config_filepath), minos::Access::Read, minos::CreateMode::Open, minos::AccessPattern::Sequential, &filehandle))
	{
		*out_error = { "Could not open file", 0, 0 };

		return false;
	}

	minos::FileInfo fileinfo;

	if (!minos::file_get_info(filehandle, &fileinfo))
	{
		*out_error = { "Could not determine file length", 0, 0 };

		return false;
	}

	if (fileinfo.file_bytes > UINT32_MAX)
	{
		*out_error = { "Config file exceeds the maximum size of 4GB", 0, 0 };

		return false;
	}

	char8 stack_buffer[8192];

	char8* buffer;

	if (sizeof(stack_buffer) <= fileinfo.file_bytes)
	{
		buffer = static_cast<char8*>(minos::reserve(fileinfo.file_bytes));

		if (buffer == nullptr)
		{
			*out_error = { "Failed to allocate buffer", 0, 0 };

			return false;
		}

		if (!minos::commit(buffer, fileinfo.file_bytes))
		{
			minos::unreserve(buffer);

			*out_error = { "Failed to allocate buffer", 0, 0 };

			return false;
		}
	}
	else
	{
		buffer = stack_buffer;
	}

	minos::Overlapped overlapped{};

	if (!minos::file_read(filehandle, buffer, static_cast<u32>(fileinfo.file_bytes), &overlapped))
	{
		*out_error = { "Could not read config file", 0, 0 };

		return false;
	}

	if (!minos::overlapped_wait(filehandle, &overlapped))
	{
		*out_error = { "Could not read config file", 0, 0 };

		return false;
	}

	minos::file_close(filehandle);

	buffer[fileinfo.file_bytes] = '\0';

	const bool parse_ok = parse_config(buffer, static_cast<u32>(fileinfo.file_bytes), out_error, out);

	if (buffer != stack_buffer)
		minos::unreserve(buffer);

	return parse_ok;
}
