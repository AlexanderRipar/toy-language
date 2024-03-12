#include "config.hpp"

#include <cstdio>
#include <cassert>
#include <cstring>

static config::Config g_config;

enum class ArgType
{
	NONE = 0,
	Bool,
	U32,
	String,
};

struct ArgDesc
{
	ArgType type;

	bool found;

	const char* name;

	union
	{
		struct
		{
			bool* target;

			bool default_value;
		} bool_data;

		struct
		{
			u32* target;

			u32 default_value;

			u32 min_value;

			u32 max_value;
		} u32_data;

		struct
		{
			const char8** target;

			const char8* default_value;
		} string_data;
	};

	ArgDesc(const char8* name, const char8** target, const char8* default_value) noexcept
		: type{ ArgType::String }, found{ false }, name{ name }, string_data{ target, default_value } {}

	ArgDesc(const char8* name, u32* target, u32 default_value, u32 min_value, u32 max_value) noexcept
		: type{ ArgType::U32 }, found{ false }, name{ name }, u32_data{ target, default_value, min_value, max_value } {}

	ArgDesc(const char8* name, bool* target, bool default_value) noexcept
		: type{ ArgType::Bool }, found{ false }, name{ name }, bool_data{ target, default_value } {}
};

static void print_usage(const char8* invocation, u32 desc_count, const ArgDesc* descs) noexcept
{
	fprintf(stderr, "Usage: %s", invocation);

	for (u32 i = 0; i != desc_count; ++i)
	{
		fprintf(stderr, " [--%s", descs[i].name);

		switch (descs[i].type)
		{
		case ArgType::Bool:
			fprintf(stderr, "[=(true|false)]]");
			break;

		case ArgType::U32:
			fprintf(stderr, "=(%u..%u)]", descs[i].u32_data.min_value, descs[i].u32_data.max_value);
			break;

		case ArgType::String:
			fprintf(stderr, "=value]");
			break;

		default:
			fprintf(stderr, "=???]");
			break;
		}
	}

	fprintf(stderr, " input-files ...\n");
}

static const char8* arg_get_param(const char8* arg, const ArgDesc* desc) noexcept
{
	const char8* name = desc->name;

	while (*name != '\0')
	{
		if (*arg != *name)
			return nullptr;

		arg += 1;

		name += 1;
	}

	if (*arg == '=')
		return arg + 1;

	if (*arg == '\0' && desc->type == ArgType::Bool)
		return arg;

	return nullptr;
}

static bool parse_u32(const char8* text, u32* out) noexcept
{
	u32 value = 0;

	if (*text == '\0')
	{
		return false;
	}
	else if (*text == '0')
	{
		text += 1;

		if (*text == 'x')
		{
			text += 1;

			if (*text == '\0')
				return false;

			while (*text != '\0')
			{
				if (*text >= '0' && *text <= '9')
					value = value * 16 + *text - '0';
				else if (*text >= 'a' && *text <= 'f')
					value = value * 16 + *text - 'a';
				else if (*text >= 'A' && *text <= 'F')
					value = value * 16 + *text - 'A';
				else
					return false;

				text += 1;
			}

			*out = value;

			return true;
		}
		else if (*text == 'b')
		{
			text += 1;

			if (*text == '\0')
				return false;

			while(*text != '\0')
			{
				if (*text >= '0' && *text <= '1')
					value = value * 2 + *text - '0';
				else
					return false;

				text += 1;
			}

			*out = value;

			return true;
		}
		else if (*text == 'o')
		{
			text += 1;

			if (*text == '\0')
				return false;

			while (*text != '\0')
			{
				if (*text >= '0' && *text <= '7')
					value = value * 8 + *text - '0';
				else
					return false;

				text += 1;
			}

			*out = value;

			return true;
		}
	}

	while (*text != '\0')
	{
		if (*text >= '0' && *text <= '9')
			value = value * 10 + *text - '0';
		else
			return false;

		text += 1;
	}

	*out = value;

	return true;
}

bool config::init(u32 argc, const char8** argv) noexcept
{
	const u32 logical_processor_count = minos::logical_processor_count();

	ArgDesc descs[] {
		{ "worker-thread-count",                &g_config.base.worker_thread_count,               logical_processor_count, 1,      1024         },
		{ "max-string-length",                  &g_config.base.max_string_length,                  256,                    4096,   65536        },
		{ "max-concurrent-read-count",          &g_config.read.max_concurrent_read_count,          16,                     1,      65536        },
		{ "max-concurrent-read-count-per-file", &g_config.read.max_concurrent_read_count_per_file, 4,                      1,      512          },
		{ "max-concurrent-file-read-count",     &g_config.read.max_concurrent_file_read_count,     1,                      0x80,   4096         },
		{ "bytes-per-read",                     &g_config.read.bytes_per_read,                     0x1'0000,               0x1000, 0x8000'0000  },
		{ "file-max-count",                     &g_config.mem.file_max_count,                      0x100'0000,             0x100,  0x4000'0000  },
		{ "file-commit-increment-count",        &g_config.mem.file_commit_increment_count,         0x8'0000,               0x100,  0x100'0000   },
		{ "file-initial-commit-count",          &g_config.mem.file_initial_commit_count,           0x20'0000,              0x100,  0x100'0000   },
		{ "file-initial-lookup-count",          &g_config.mem.file_initial_lookup_count,           0x1000,                 0x40,   0x100'0000   },
	};

	const char8* invocation = argv[0];

	for (const char8* c = invocation; *c != '\0'; ++c)
	{
		if (*c == '\\')
			invocation = c + 1;
	}

	if (argc == 1)
	{
		print_usage(invocation, static_cast<u32>(array_count(descs)), descs);

		fprintf(stderr, "Use \"%s --help\" for more details", invocation);

		return false;
	}

	u32 argind = 1;

	while (argind != argc)
	{
		const char8* const arg = argv[argind];

		if (arg[0] != '-' || arg[1] != '-')
			break;

		ArgDesc* desc = nullptr;

		const char8* arg_param = nullptr;

		for (u32 i = 0; i != array_count(descs); ++i)
		{
			arg_param = arg_get_param(arg + 2, &descs[i]);

			if (arg_param != nullptr)
			{
				desc = &descs[i];

				break;
			}
		}

		if (desc == nullptr)
		{
			fprintf(stderr, "Unknown option %s.\n", arg);

			print_usage(invocation, static_cast<u32>(array_count(descs)), descs);

			return false;
		}

		if (desc->found)
		{
			fprintf(stderr, "More than one value specified for option --%s.\n", desc->name);

			return false;
		}

		desc->found = true;

		switch (desc->type)
		{
		case ArgType::Bool: {

			if (*arg_param == '\0' || strcmp(arg_param, "true") == 0)
			{
				*desc->bool_data.target = true;
			}
			else if (strcmp(arg_param, "false") == 0)
			{
				*desc->bool_data.target = false;
			}
			else
			{
				fprintf(stderr, "Invalid value '%s' supplied for option --%s. Expected no value, 'true' or 'false'.", arg_param, desc->name);

				return false;
			}

			break;
		}

		case ArgType::U32: {

			u32 arg_value;

			if (!parse_u32(arg_param, &arg_value))
			{
				fprintf(stderr, "Invalid value '%s' supplied for option --%s. Expected numeric value.", arg_param, desc->name);

				return false;
			}
			else if (arg_value < desc->u32_data.min_value)
			{
				fprintf(stderr, "The value '%s' (%u) supplied for option --%s is too small. Expected numeric value from '%u' to '%u'", arg_param, arg_value, desc->name, desc->u32_data.min_value, desc->u32_data.max_value);

				return false;
			}
			else if (arg_value > desc->u32_data.max_value)
			{
				fprintf(stderr, "The value '%s' (%u) supplied for option --%s is too large. Expected numeric value from '%u' to '%u'", arg_param, arg_value, desc->name, desc->u32_data.min_value, desc->u32_data.max_value);

				return false;
			}

			*desc->u32_data.target = arg_value;

			break;
		}

		case ArgType::String: {

			*desc->string_data.target = arg_param;

			break;
		}

		default: {

			ASSERT_UNREACHABLE;
		}
		}

		argind += 1;
	}

	if (argind == argc)
	{
		fprintf(stderr, "No files to be compiled specified.\n");

		print_usage(invocation, static_cast<u32>(array_count(descs)), descs);

		return false;
	}

	g_config.input.initial_input_file_count = argc - argind;

	g_config.input.initial_input_files = &argv[argind];

	for (u32 i = 0; i != array_count(descs); ++i)
	{
		if (descs[i].found)
			continue;

		switch (descs[i].type)
		{
		case ArgType::Bool:
			*descs[i].bool_data.target = descs[i].bool_data.default_value;
			break;

		case ArgType::U32:
			*descs[i].u32_data.target = descs[i].u32_data.default_value;
			break;

		case ArgType::String:
			*descs[i].string_data.target = descs[i].string_data.default_value;
			break;

		default:
			ASSERT_UNREACHABLE;
		}
	}

	g_config.base.invocation = invocation;

	return true;
}

const config::Config* config::get() noexcept
{
	return &g_config;
}
