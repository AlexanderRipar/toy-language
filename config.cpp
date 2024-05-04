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



static bool is_name_char(const char8 c) noexcept
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-';
}

static bool is_whitespace(const char8 c) noexcept
{
	return c == '\n' || c == '\r' || c == '\t' || c == ' ';
}

static const char8* skip_whitespace(const char8* c) noexcept
{
	while (is_whitespace(*c))
		c += 1;
	
	return c;
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

static ConfigEntry* lookup_name_element(const char8* curr, ConfigEntry* context, const char8** out_next) noexcept
{
	if (context->type != ConfigType::Container)
		return nullptr;

	ConfigEntry* children = context + 1;

	for (u32 i = 0; i != context->container_child_count; ++i)
	{
		if (name_equal(curr, children[i].name, out_next))
			return children + i;

		if (children[i].type == ConfigType::Container)
			i += children[i].container_child_count;
	}

	return nullptr;
}

static ConfigEntry* lookup_composite_name(const char8* curr, ConfigEntry* context, const char8** out_next) noexcept
{
	ConfigEntry* e = lookup_name_element(curr, context, &curr);

	if (e == nullptr)
		return false;

	curr = skip_whitespace(curr);

	while (*curr == '.')
	{
		curr = skip_whitespace(curr + 1);

		e = lookup_name_element(curr, e, &curr);

		if (e == nullptr)
			return false;

		curr = skip_whitespace(curr);
	}

	*out_next = curr;

	return e;
}

static bool parse_value(const char8* curr, ConfigEntry* context, Config* out, const char8** out_next) noexcept
{
	if (context->type == ConfigType::Container)
		return false;

	if (context->seen)
		return false;

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
					return false;

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
					return false;

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
					return false;

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
				return false;
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
			return false;

		*reinterpret_cast<u32*>(reinterpret_cast<byte*>(out) + context->offset) = value;

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

		*reinterpret_cast<bool*>(reinterpret_cast<byte*>(out) + context->offset) = value;

		break;
	}

	case ConfigType::String:
	{
		Range<char8> value;

		// @TODO: Implement string parsing

		return false;
	}

	default:
		ASSERT_UNREACHABLE;
	}

	context->seen = true;

	*out_next = skip_whitespace(curr);

	return true;
}

static bool parse_inline_table(const char8* curr, ConfigEntry* context, Config* out, const char8** out_next) noexcept
{
	if (context->type != ConfigType::Container)
		return false;

	while (true)
	{
		if (!is_name_char(*curr))
			return false;

		ConfigEntry* child = lookup_composite_name(curr, context, &curr);

		if (child == nullptr)
			return false;

		if (*curr != '=')
			return false;

		curr = skip_whitespace(curr + 1);

		if (*curr == '{')
		{
			curr = skip_whitespace(curr + 1);

			if (!parse_inline_table(curr, child, out, &curr))
				return false;
		}
		else
		{
			if (!parse_value(curr, child, out, &curr))
				return false;
		}

		if (*curr == '}')
			break;
		else if (*curr != ',')
			return false;

		curr = skip_whitespace(curr + 1);
	}

	*out_next = skip_whitespace(curr + 1);

	return true;
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

static bool parse_config(const char8* config, Config* out) noexcept
{
	memset(out, 0, sizeof(*out));

	const char8* curr = skip_whitespace(config);

	ConfigEntry config_copy[array_count(config_template)];

	memcpy(config_copy, config_template, sizeof(config_copy));

	ConfigEntry* const root_context = config_copy;

	ConfigEntry* context = root_context;

	while (true)
	{
		if (*curr == '[')
		{
			if (curr[1] == '[')
			{
				return false;
			}
			else
			{
				curr = skip_whitespace(curr + 1);

				context = lookup_composite_name(curr, root_context, &curr);

				if (context == nullptr)
					return false;

				if (*curr != ']')
					return false;

				curr += 1;
			}
		}
		else if (is_name_char(*curr))
		{
			ConfigEntry* const child = lookup_composite_name(curr, context, &curr);

			if (child == nullptr)
				return false;

			if (*curr != '=')
				return false;

			curr = skip_whitespace(curr + 1);

			if (*curr == '{')
			{
				curr = skip_whitespace(curr + 1);

				if (!parse_inline_table(curr, child, out, &curr))
					return false;
			}
			else if (*curr == '[')
			{
				return false;
			}
			else
			{
				if (!parse_value(curr, child, out, &curr))
					return false;
			}
		}
		else if (*curr == '\0')
		{
			break;
		}
		else
		{
			return false;
		}
	}

	set_config_defaults(MutRange{ config_copy }, out);

	return validate_config(out);
}

bool read_config_from_file(const char8* config_filepath, Config* out) noexcept
{
	minos::FileHandle filehandle;

	if (!minos::file_create(range_from_cstring(config_filepath), minos::Access::Read, minos::CreateMode::Open, minos::AccessPattern::Sequential, &filehandle))
		return false;

	minos::FileInfo fileinfo;

	if (!minos::file_get_info(filehandle, &fileinfo))
		return false;

	if (fileinfo.file_bytes > UINT32_MAX)
		return false;

	char8 stack_buffer[8192];

	char8* buffer;

	if (sizeof(stack_buffer) <= fileinfo.file_bytes)
	{
		buffer = static_cast<char8*>(minos::reserve(fileinfo.file_bytes));

		if (buffer == nullptr)
			return false;

		if (!minos::commit(buffer, fileinfo.file_bytes))
		{
			minos::unreserve(buffer);

			return false;
		}
	}
	else
	{
		buffer = stack_buffer;
	}

	minos::Overlapped overlapped{};

	if (!minos::file_read(filehandle, buffer, static_cast<u32>(fileinfo.file_bytes), &overlapped))
		return false;

	if (!minos::overlapped_wait(filehandle, &overlapped))
		return false;

	minos::file_close(filehandle);

	buffer[fileinfo.file_bytes] = '\0';

	const bool parse_ok = parse_config(buffer, out);

	if (buffer != stack_buffer)
		minos::unreserve(buffer);

	return parse_ok;
}
