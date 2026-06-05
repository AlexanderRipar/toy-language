#include "core.hpp"

#include "../infra/panic.hpp"
#include "../infra/toml/toml.hpp"

#include <cstddef>

#define META_ROOT_TABLE() ConfigMetadataEntry{ ConfigMetadataTag::Table, 0, "", "", { sizeof(ConfigMetadata) / sizeof(ConfigMetadataEntry) } }

#define META_TABLE(name, path, helptext) ConfigMetadataEntry{ ConfigMetadataTag::Table, 0, name, helptext, { sizeof(ConfigMetadata::path) / sizeof(ConfigMetadataEntry) } }

#define META_INTEGER(name, path, default_value, min, max, helptext) ConfigMetadataEntry{ ConfigMetadataTag::Integer, offsetof(Config, path), name, helptext, { default_value, min, max } }

#define META_STRING(name, path, default_value, helptext) ConfigMetadataEntry{ ConfigMetadataTag::String, offsetof(Config, path), name, helptext, { default_value } }

#define META_PATH(name, path, default_value, helptext) ConfigMetadataEntry{ ConfigMetadataTag::Path, offsetof(Config, path), name, helptext, { default_value } }

#define META_BOOLEAN(name, path, default_value, helptext) ConfigMetadataEntry{ ConfigMetadataTag::Boolean, offsetof(Config, path), name, helptext, { default_value } }

#define META_PRINT_SINK(name, path, default_value, helptext) ConfigMetadataEntry{ ConfigMetadataTag::PrintSink, offsetof(Config, path), name, helptext, { default_value } }

#define META_DYNAMIC(name, path, helptext) ConfigMetadataEntry{ ConfigMetadataTag::Dynamic, offsetof(Config, path), name, helptext, {} }

enum class ConfigMetadataTag : u8
{
	INVALID = 0,
	Table,
	Integer,
	String,
	Boolean,
	Path,
	PrintSink,
	Dynamic,
};

union ConfigMetadataAttach
{
	struct
	{
		s64 value;

		s64 min;

		s64 max;
	} integer;

	bool boolean;

	Range<char8> string;

	Range<char8> path;
	
	Range<char8> print_sink_path;

	u64 child_count;

	constexpr ConfigMetadataAttach() noexcept : integer{ 0, 0, 0 } {}

	constexpr ConfigMetadataAttach(s64 integer_value, s64 min, s64 max) noexcept : integer{ integer_value, min, max } {}

	constexpr ConfigMetadataAttach(bool boolean) noexcept : boolean{ boolean } {}

	constexpr ConfigMetadataAttach(Range<char8> stringish) noexcept : string{ stringish } {}

	constexpr ConfigMetadataAttach(u64 child_count) noexcept : child_count{ child_count } {}
};

struct ConfigMetadataEntry
{
	ConfigMetadataTag tag;

	u32 offset;

	const char8* name;

	const char8* helptext;

	ConfigMetadataAttach attach;
};

struct ConfigMetadata
{
	ConfigMetadataEntry self_;

	struct
	{
		ConfigMetadataEntry self_;

		ConfigMetadataEntry path;

		ConfigMetadataEntry symbol;
	} entrypoint;

	struct
	{
		ConfigMetadataEntry self_;

		struct
		{
			ConfigMetadataEntry self_;

			ConfigMetadataEntry path;
		} prelude;
	} std;

	struct
	{
		ConfigMetadataEntry self_;

		ConfigMetadataEntry reserve;

		ConfigMetadataEntry commit_increment;
	} heap;

	struct
	{
		ConfigMetadataEntry self_;

		struct
		{
			ConfigMetadataEntry self_;

			ConfigMetadataEntry reserve;

			ConfigMetadataEntry commit_increment;
		} addresses;

		struct
		{
			ConfigMetadataEntry self_;

			ConfigMetadataEntry reserve;

			ConfigMetadataEntry commit_increment;
		} layouts;
	} shadow_store;

	struct
	{
		ConfigMetadataEntry self_;

		struct
		{
			ConfigMetadataEntry self_;

			ConfigMetadataEntry asts;

			ConfigMetadataEntry opcodes;

			ConfigMetadataEntry types;
		} imports;

		ConfigMetadataEntry config;
	} logging;

	struct
	{
		ConfigMetadataEntry self_;

		ConfigMetadataEntry file;

		ConfigMetadataEntry source_tab_size;
	} diagnostics;

	ConfigMetadataEntry defines;

	ConfigMetadataEntry compile_all;
};

struct TomlConfigMappingContext
{
	Config* out;

	Range<char8> directory_path;

	Maybe<TreeSchemaAllocator*> ts_alloc;

	Range<char8> filepath;

	PrintSink error_sink;

	char8 directory_path_buf[4096];
};



static constexpr ConfigMetadata init_metadata() noexcept
{
	ConfigMetadata rst{};

	rst.self_ = META_ROOT_TABLE();

	rst.entrypoint.self_ = META_TABLE("entrypoint", entrypoint, "Entrypoint configuration");
	rst.entrypoint.path = META_PATH("path", entrypoint.filepath, range::from_literal_string("main.evl"), "Relative path of the source file containing the program's entrypoint");
	rst.entrypoint.symbol = META_STRING("symbol", entrypoint.symbol, range::from_literal_string("main"), "Symbol name of the program's entrypoint function");

	rst.std.self_ = META_TABLE("std", std, "Standard library configuration");
	rst.std.prelude.self_ = META_TABLE("prelude", std.prelude, "Prelude parameters");
	rst.std.prelude.path = META_PATH("path", std.prelude.filepath, range::from_literal_string("std/prelude.evl"), "Path to the file containing the prelude that is made available to all sources");

	rst.heap.self_ = META_TABLE("heap", heap, "Managed heap configuration");
	rst.heap.reserve = META_INTEGER("reserve", heap.reserve, 1 << 30, 1 << 12, static_cast<s64>(1) << 31, "Size the managed heap's small allocation section can grow to, in bytes");
	rst.heap.commit_increment = META_INTEGER("commit-increment", heap.commit_increment, 1 << 18, 1 << 12, static_cast<s64>(1) << 31, "Number of bytes the managed heap is grown by at a time");

	rst.shadow_store.self_ = META_TABLE("shadow-store", shadow_store, "Shadow store configuration for holding zero-sized values");
	rst.shadow_store.addresses.self_ = META_TABLE("addresses", shadow_store.addresses, "Allocation information for the shadow store address table. This holds association between addresses and their shadow data");
	rst.shadow_store.addresses.reserve = META_INTEGER("reserve", shadow_store.addresses.reserve, 1 << 24, 1 << 12, static_cast<s64>(1) << 31, "Maximum number of entries allowed in the shadow store's address table");
	rst.shadow_store.addresses.commit_increment = META_INTEGER("commit-increment", shadow_store.addresses.commit_increment, 1 << 14, 1 << 12, static_cast<s64>(1) << 31, "Number of entries added to the shadow store's address table upon filling up");
	rst.shadow_store.layouts.self_ = META_TABLE("layouts", shadow_store.layouts, "Allocation information for the shadow store layout table. This holds the layouts of shadow store's address table entries");
	rst.shadow_store.layouts.reserve = META_INTEGER("reserve", shadow_store.layouts.reserve, 1 << 24, 1 << 12, static_cast<s64>(1) << 31, "Maximum number of entries allowed in the shadow store's layout table");
	rst.shadow_store.layouts.commit_increment = META_INTEGER("reserve", shadow_store.layouts.commit_increment, 1 << 14, 1 << 12, static_cast<s64>(1) << 31, "Number of entries added to the shadow store's layout table upon filling up");

	rst.logging.self_ = META_TABLE("logging", logging, "Logging configuration");
	rst.logging.imports.self_ = META_TABLE("imports", logging.imports, "File import logging parameters");
	rst.logging.imports.asts = META_PRINT_SINK("asts", logging.imports.asts_sink, range::from_literal_string("$none"), "File abstract syntax trees are written to when files are imported");
	rst.logging.imports.opcodes = META_PRINT_SINK("opcodes", logging.imports.opcodes_sink, range::from_literal_string("$none"), "File interpreter opcodes are written to when files are imported");
	rst.logging.imports.types = META_PRINT_SINK("types", logging.imports.types_sink, range::from_literal_string("$none"), "File top-level types are written to when files are imported");
	rst.logging.config = META_PRINT_SINK("config", logging.config_sink, range::from_literal_string("$none"), "File the parsed configuration gets written to");

	rst.diagnostics.self_ = META_TABLE("diagnostics", diagnostics, "Error message configuration");
	rst.diagnostics.file = META_PRINT_SINK("path", diagnostics.sink, range::from_literal_string("$stderr"), "File errors generated during compilation are written to");
	rst.diagnostics.source_tab_size = META_INTEGER("tab-size", diagnostics.source_tab_size, 4, 1, 32, "Number of characters a tab is equivalent to when reporting column numbers");

	rst.defines = META_DYNAMIC("defines", defines, "Definitions made accessible to the program under compilation via `std.comp_env().*.definitions`. May contain arbitrary nested values");

	rst.compile_all = META_BOOLEAN("compile-all", compile_all, false, "Whether to compile all top-level definitions. If this is `true`, all top-level definitions of imported files are compiled. If it is `false`, only definitions used by public symbols are compiled");

	return rst;
}

static constexpr ConfigMetadata CONFIG_METADATA = init_metadata();



static bool string_equal(Range<char8> a, Range<char8> b)
{
	if (a.count() != b.count())
		return false;

	return memcmp(a.begin(), b.begin(), a.count()) == 0;
}

static void config_error_header(TomlConfigMappingContext* ctx, u32 line, u32 column) noexcept
{
	if (line == 0)
	{
		(void) print(ctx->error_sink, "%: ", ctx->filepath);
	}
	else
	{
		ASSERT_OR_IGNORE(column != 0);

		(void) print(ctx->error_sink, "%:%:%: ", ctx->filepath, line, column);
	}
}

template<typename... Inserts>
static bool config_error(TomlConfigMappingContext* ctx, u32 line, u32 column, const char8* message, Inserts... inserts) noexcept
{
	config_error_header(ctx, line, column);

	(void) print(ctx->error_sink, message, inserts...);

	DEBUGBREAK;

	return false;
}



static bool absolute_path_from_config_path(TomlConfigMappingContext* ctx, const TreeSchemaValue* toml, const ConfigMetadataEntry* curr, Range<char8>* out) noexcept
{
	ASSERT_OR_IGNORE(toml->name_and_tag.attachment() == TreeSchemaValueTag::String);

	ASSERT_OR_IGNORE(curr->tag == ConfigMetadataTag::Path || curr->tag == ConfigMetadataTag::PrintSink);

	const Range<char8> path = toml->value.string;

	if (path.count() == 0)
		return config_error(ctx, toml->source_line, toml->source_column, "Configuration paths for input files must not be empty.\n");
	else if (path[0] == '$')
		return config_error(ctx, toml->source_line, toml->source_column, "Configuration paths for input files cannot use `$` escape pseudo-paths. Prefix the path with `./` to force it to be treated as a filesystem path.\n");

	if (is_some(ctx->ts_alloc))
	{
		char8 resolved_path_buf[4096];

		const u32 resolved_path_count = minos::path_to_absolute_relative_to(path, ctx->directory_path, MutRange{ resolved_path_buf });

		if (resolved_path_count == 0 || resolved_path_count > sizeof(resolved_path_buf))
			return config_error(ctx, toml->source_line, toml->source_column, "Failed to resolve path `%` relative to the TOML configuration file (0x%[|X]).\n", path, minos::last_error());

		const Range<char8> resolved_path{ resolved_path_buf, resolved_path_count };

		if (!ts_string_create(get(ctx->ts_alloc), resolved_path, out))
			return config_error(ctx, 0, 0, "Failed to create TOML string.\n");
	}
	else
	{
		*out = curr->attach.path;
	}

	return true;
}

static bool print_sink_from_config_path(TomlConfigMappingContext* ctx, const TreeSchemaValue* toml, const ConfigMetadataEntry* curr, ConfigPrintSink* out) noexcept
{
	ASSERT_OR_IGNORE(toml->name_and_tag.attachment() == TreeSchemaValueTag::String);

	const Range<char8> path = toml->value.string;

	if (path.count() == 0 || string_equal(path, range::from_literal_string("$none")))
	{
		*out = ConfigPrintSink{ { range::from_literal_string("$none"), false }, {} };
	}
	else if (string_equal(path, range::from_literal_string("$stderr")))
	{
		*out = ConfigPrintSink{ { range::from_literal_string("$stderr"), true }, print_make_sink(minos::standard_file_handle(minos::StdFileName::StdErr)) };
	}
	else if (string_equal(path, range::from_literal_string("$stdout")))
	{
		*out = ConfigPrintSink{ { range::from_literal_string("$stderr"), true }, print_make_sink(minos::standard_file_handle(minos::StdFileName::StdOut)) };
	}
	else if (path[0] != '$')
	{
		Range<char8> resolved_path;

		if (!absolute_path_from_config_path(ctx, toml, curr, &resolved_path))
			return false;

		minos::FileHandle file;

		if (!minos::file_create(resolved_path, minos::Access::Write, minos::ExistsMode::Truncate, minos::NewMode::Create, minos::AccessPattern::Sequential, none<const minos::CompletionInitializer*>(), false, &file))
			return config_error(ctx, toml->source_line, toml->source_column, "Failed to open file `%` (0x%[|X]).\n", resolved_path, minos::last_error());

		*out = ConfigPrintSink{ { resolved_path, true }, print_make_sink(file) };
	}
	else
	{
		return config_error(ctx, toml->source_line, toml->source_column, "Configuration path `%` prefixed with `$` is not a known path escape. Prefix it with `./` to force it to be treated as a filesystem path.\n");
	}

	return true;
}



static bool map_defaults_to_config(TomlConfigMappingContext* ctx, const ConfigMetadataEntry** meta) noexcept
{
	const ConfigMetadataEntry* const curr = *meta;

	*meta += 1;

	switch (curr->tag)
	{
	case ConfigMetadataTag::Table:
	{
		const ConfigMetadataEntry* const end = curr + curr->attach.child_count;

		while (*meta != end)
		{
			if (!map_defaults_to_config(ctx, meta))
				return false;
		}

		return true;
	}

	case ConfigMetadataTag::Integer:
	{
		ASSERT_OR_IGNORE(curr->offset + sizeof(s64) < sizeof(Config));

		ASSERT_OR_IGNORE(curr->attach.integer.min < curr->attach.integer.value && curr->attach.integer.max > curr->attach.integer.value);

		*reinterpret_cast<s64*>(reinterpret_cast<byte*>(ctx->out) + curr->offset) = curr->attach.integer.value;

		return true;
	}

	case ConfigMetadataTag::String:
	{
		ASSERT_OR_IGNORE(curr->offset + sizeof(Range<char8>) < sizeof(Config));

		*reinterpret_cast<Range<char8>*>(reinterpret_cast<byte*>(ctx->out) + curr->offset) = curr->attach.string;

		return true;
	}

	case ConfigMetadataTag::Boolean:
	{
		ASSERT_OR_IGNORE(curr->offset + sizeof(bool) < sizeof(Config));

		*reinterpret_cast<bool*>(reinterpret_cast<byte*>(ctx->out) + curr->offset) = curr->attach.boolean;

		return true;
	}

	case ConfigMetadataTag::Path:
	{
		ASSERT_OR_IGNORE(curr->offset + sizeof(Range<char8>) < sizeof(Config));

		const TreeSchemaValue default_value = ts_value_from_string_unnamed(0, 0, curr->attach.print_sink_path);

		Range<char8> value;

		if (!absolute_path_from_config_path(ctx, &default_value, curr, &value))
			return false;

		*reinterpret_cast<Range<char8>*>(reinterpret_cast<byte*>(ctx->out) + curr->offset) = value;

		return true;
	}

	case ConfigMetadataTag::PrintSink:
	{
		ASSERT_OR_IGNORE(curr->offset + sizeof(ConfigPrintSink) < sizeof(Config));

		const TreeSchemaValue default_value = ts_value_from_string_unnamed(0, 0, curr->attach.print_sink_path);

		ConfigPrintSink value;

		if (!print_sink_from_config_path(ctx, &default_value, curr, &value))
			return false;

		*reinterpret_cast<ConfigPrintSink*>(reinterpret_cast<byte*>(ctx->out) + curr->offset) = value;

		return true;
	}

	case ConfigMetadataTag::Dynamic:
	{
		ASSERT_OR_IGNORE(curr->offset + sizeof(Maybe<const TreeSchemaTable*>) < sizeof(Config));

		*reinterpret_cast<Maybe<const TreeSchemaTable*>*>(reinterpret_cast<byte*>(ctx->out) + curr->offset) = none<const TreeSchemaTable*>();

		return true;
	}

	case ConfigMetadataTag::INVALID:
		; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

static bool map_toml_to_config(TomlConfigMappingContext* ctx, const TreeSchemaValue* toml, const ConfigMetadataEntry** meta) noexcept
{
	const ConfigMetadataEntry* const curr = *meta;

	*meta += 1;

	switch (curr->tag)
	{
	case ConfigMetadataTag::Table:
	{
		if (toml->name_and_tag.attachment() != TreeSchemaValueTag::Table)
			return config_error(ctx, toml->source_line, toml->source_column, "Config parameter `%` must be a table.\n", curr->name);

		const TreeSchemaTable* const table = toml->value.table;

		u32 unused_subkey_count = ts_table_count(table);

		const ConfigMetadataEntry* const end = curr + curr->attach.child_count;

		while (*meta != end)
		{
			const ConfigMetadataEntry* const child = *meta;

			const Maybe<const TreeSchemaValue*> child_toml = ts_table_find_value(table, range::from_cstring(child->name));

			if (is_some(child_toml))
			{
				ASSERT_OR_IGNORE(unused_subkey_count != 0);

				unused_subkey_count -= 1;

				if (!map_toml_to_config(ctx, get(child_toml), meta))
					return false;
			}
			else
			{
				if (!map_defaults_to_config(ctx, meta))
					return false;
			}
		}

		if (unused_subkey_count == 0)
			return true;

		TreeSchemaTableIterator it = ts_table_values(table);

		while (has_next(&it))
		{
			const TreeSchemaValue* subkey = next(&it);

			bool has_match = false;

			for (const ConfigMetadataEntry* match = curr + 1; match != end; ++match)
			{
				if (string_equal(subkey->name_and_tag.range(), range::from_cstring(match->name)))
				{
					has_match = true;

					break;
				}
			}

			if (!has_match)
			{
				(void) config_error(ctx, subkey->source_line, subkey->source_column, "`%` is not a configuration parameter.", subkey->name_and_tag.range());

				unused_subkey_count -= 1;

				if (unused_subkey_count == 0)
					break;
			}
		}

		return false;
	}

	case ConfigMetadataTag::Integer:
	{
		ASSERT_OR_IGNORE(curr->offset + sizeof(s64) < sizeof(Config));

		if (toml->name_and_tag.attachment() != TreeSchemaValueTag::Integer)
			return config_error(ctx, toml->source_line, toml->source_column, "Config parameter `%` must be an integer.\n", curr->name);

		const s64 value = toml->value.integer;

		if (value < curr->attach.integer.min)
			return config_error(ctx, toml->source_line, toml->source_column, "Value `%` of config parameter `%` is less than the allowed minimum of `%`.\n", value, curr->name, curr->attach.integer.min);

		if (value > curr->attach.integer.max)
			return config_error(ctx, toml->source_line, toml->source_column, "Value `%` of config parameter `%` is greater than the allowed maximum of `%`.\n", value, curr->name, curr->attach.integer.max);

		*reinterpret_cast<s64*>(reinterpret_cast<byte*>(ctx->out) + curr->offset) = value;

		return true;
	}

	case ConfigMetadataTag::String:
	{
		ASSERT_OR_IGNORE(curr->offset + sizeof(Range<char8>) < sizeof(Config));

		if (toml->name_and_tag.attachment() != TreeSchemaValueTag::String)
			return config_error(ctx, toml->source_line, toml->source_column, "Config parameter `%` must be a string.\n", curr->name);

		const Range<char8> value = toml->value.string;

		*reinterpret_cast<Range<char8>*>(reinterpret_cast<byte*>(ctx->out) + curr->offset) = value;

		return true;
	}

	case ConfigMetadataTag::Boolean:
	{
		ASSERT_OR_IGNORE(curr->offset + sizeof(bool) < sizeof(Config));

		if (toml->name_and_tag.attachment() != TreeSchemaValueTag::Boolean)
			return config_error(ctx, toml->source_line, toml->source_column, "Config parameter `%` must be a string.\n", curr->name);

		const bool value = toml->value.boolean;

		*reinterpret_cast<bool*>(reinterpret_cast<byte*>(ctx->out) + curr->offset) = value;

		return true;
	}

	case ConfigMetadataTag::Path:
	{
		ASSERT_OR_IGNORE(curr->offset + sizeof(Range<char8>) < sizeof(Config));

		if (toml->name_and_tag.attachment() != TreeSchemaValueTag::String)
			return config_error(ctx, toml->source_line, toml->source_column, "Config parameter `%` must be a string.\n", curr->name);

		Range<char8> value;
		
		if (!absolute_path_from_config_path(ctx, toml, curr, &value))
			return false;

		*reinterpret_cast<Range<char8>*>(reinterpret_cast<byte*>(ctx->out) + curr->offset) = value;

		return true;
	}

	case ConfigMetadataTag::PrintSink:
	{
		ASSERT_OR_IGNORE(curr->offset + sizeof(ConfigPrintSink) < sizeof(Config));

		if (toml->name_and_tag.attachment() != TreeSchemaValueTag::String)
			return config_error(ctx, toml->source_line, toml->source_column, "Config parameter `%` must be a string.\n", curr->name);

		ConfigPrintSink value;

		if (!print_sink_from_config_path(ctx, toml, curr, &value))
			return false;

		*reinterpret_cast<ConfigPrintSink*>(reinterpret_cast<byte*>(ctx->out) + curr->offset) = value;

		return true;
	}

	case ConfigMetadataTag::Dynamic:
	{
		ASSERT_OR_IGNORE(curr->offset + sizeof(Maybe<const TreeSchemaTable*>) < sizeof(Config));

		if (toml->name_and_tag.attachment() != TreeSchemaValueTag::Table)
			return config_error(ctx, toml->source_line, toml->source_column, "Config parameter `%` must be a TOML table.\n", curr->name);

		*reinterpret_cast<Maybe<const TreeSchemaTable*>*>(reinterpret_cast<byte*>(ctx->out) + curr->offset) = some<const TreeSchemaTable*>(toml->value.table);

		return true;
	}

	case ConfigMetadataTag::INVALID:
		; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}




static void print_tree_schema_value(PrintSink sink, const TreeSchemaValue* value, u32 indent) noexcept
{
	switch (value->name_and_tag.attachment())
	{
	case TreeSchemaValueTag::Table:
	{
		(void) print(sink, "{\n");

		const TreeSchemaTable* const table = value->value.table;

		TreeSchemaTableIterator it = ts_table_values(table);

		while (has_next(&it))
		{
			const TreeSchemaValue* const subvalue = next(&it);

			(void) print(sink, "%[< %]% = ", "", (indent + 1) * 4, subvalue->name_and_tag.range());

			print_tree_schema_value(sink, subvalue, indent + 1);
		}

		(void) print(sink, "%[< %]}\n", "", indent * 4);

		return;
	}

	case TreeSchemaValueTag::Array:
	{
		(void) print(sink, "[\n");

		const TreeSchemaArray* const array = value->value.array;

		const u32 count = ts_array_count(array);

		for (u32 i = 0; i != count; ++i)
		{
			const TreeSchemaValue* const subvalue = ts_array_at(array, i);

			(void) print(sink, "%[< %]% = ", "", (indent + 1) * 4, subvalue->name_and_tag.range());

			print_tree_schema_value(sink, subvalue, indent + 1);
		}

		(void) print(sink, "%[< %]]\n", "", indent * 4);

		return;
	}

	case TreeSchemaValueTag::Integer:
	{
		(void) print(sink, "%\n", value->value.integer);

		return;
	}

	case TreeSchemaValueTag::String:
	{
		(void) print(sink, "%\n", value->value.string);

		return;
	}

	case TreeSchemaValueTag::Boolean:
	{
		(void) print(sink, "%\n", value->value.boolean);

		return;
	}

	case TreeSchemaValueTag::INVALID:
		; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}



static void print_config_element(PrintSink sink, const Config* config, const ConfigMetadataEntry** meta, u32 indent) noexcept
{
	const ConfigMetadataEntry* const curr = *meta;

	*meta += 1;

	(void) print(sink, "%[< %]% ", "", indent * 4, curr->name);

	switch (curr->tag)
	{
	case ConfigMetadataTag::Table:
	{
		(void) print(sink, "{\n");

		const ConfigMetadataEntry* end = curr + curr->attach.child_count;

		while (*meta != end)
			print_config_element(sink, config, meta, indent + 1);

		(void) print(sink, "%[< %]}\n", "", indent * 4);

		return;
	}

	case ConfigMetadataTag::Integer:
	{
		ASSERT_OR_IGNORE(curr->offset + sizeof(s64) < sizeof(Config));

		const s64 value = *reinterpret_cast<const s64*>(reinterpret_cast<const byte*>(config) + curr->offset);

		print(sink, "= %\n", value);

		return;
	}

	case ConfigMetadataTag::String:
	case ConfigMetadataTag::Path:
	{
		ASSERT_OR_IGNORE(curr->offset + sizeof(Range<char8>) < sizeof(Config));

		const Range<char8> value = *reinterpret_cast<const Range<char8>*>(reinterpret_cast<const byte*>(config) + curr->offset);

		print(sink, "= '%'\n", value);

		return;
	}

	case ConfigMetadataTag::Boolean:
	{
		ASSERT_OR_IGNORE(curr->offset + sizeof(bool) < sizeof(Config));

		const bool value = *reinterpret_cast<const bool*>(reinterpret_cast<const byte*>(config) + curr->offset);

		print(sink, "= %\n", value);

		return;
	}

	case ConfigMetadataTag::PrintSink:
	{
		ASSERT_OR_IGNORE(curr->offset + sizeof(ConfigPrintSink) < sizeof(Config));

		const ConfigPrintSink value = *reinterpret_cast<const ConfigPrintSink*>(reinterpret_cast<const byte*>(config) + curr->offset);

		print(sink, "= '%'\n", value.name_and_enabled.range());
		
		return;
	}

	case ConfigMetadataTag::Dynamic:
	{
		ASSERT_OR_IGNORE(curr->offset + sizeof(Maybe<const TreeSchemaTable*>) < sizeof(Config));

		const Maybe<const TreeSchemaTable*> value = *reinterpret_cast<const Maybe<const TreeSchemaTable*>*>(reinterpret_cast<const byte*>(config) + curr->offset);

		if (is_some(value))
		{
			(void) print(sink, "= ");

			const TreeSchemaValue table_value = ts_value_from_table_unnamed(0, 0, const_cast<TreeSchemaTable*>(get(value)));

			print_tree_schema_value(sink, &table_value, indent);
		}
		else
		{
			(void) print(sink, "= none\n");
		}

		return;
	}

	case ConfigMetadataTag::INVALID:
		; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

static void print_config_help_element(PrintSink sink, const ConfigMetadataEntry** meta, u32 indent, u32 max_indent) noexcept
{
	const ConfigMetadataEntry* const curr = *meta;

	*meta += 1;

	switch (curr->tag)
	{
	case ConfigMetadataTag::Table:
	{
		const bool is_root = curr == &CONFIG_METADATA.self_;

		if (!is_root)
		{
			(void) print(sink, "%[< %]- %\n%[< %]%\n\n",
				"", (indent + 1) * 4 - 2,
				curr->name,
				"", (indent + 1) * 4,
				curr->helptext
			);
		}

		const ConfigMetadataEntry* const end = curr + curr->attach.child_count;

		if (max_indent != 0 && indent == max_indent)
		{
			*meta = end;

			return;
		}

		while (*meta != end)
			print_config_help_element(sink, meta, is_root ? indent : indent + 1, max_indent);

		return;
	}

	case ConfigMetadataTag::Integer:
	{
		(void) print(sink, "%[< %]- %\n%[< %]%\n%[< %]type:    integer\n%[< %]default: %\n",
			"", (indent + 1) * 4 - 2,
			curr->name,
			"", (indent + 1) * 4,
			curr->helptext,
			"", (indent + 1) * 4,
			"", (indent + 1) * 4,
			curr->attach.integer.value
		);

		const s64 min = curr->attach.integer.min;

		const s64 max = curr->attach.integer.max;

		if (min != INT64_MIN && max != INT64_MAX)
		{
			(void) print(sink, "%[< %]range:   % .. %\n\n", "", (indent + 1) * 4, min, max);
		}
		else if (min != INT64_MIN)
		{
			(void) print(sink, "%[< %]range:   % ..\n\n", "", (indent + 1) * 4, min);
		}
		else if (max != INT64_MAX)
		{
			(void) print(sink, "%[< %]range:   .. %\n\n", "", (indent + 1) * 4, max);
		}
		else
		{
			(void) print(sink, "\n");
		}

		return;
	}

	case ConfigMetadataTag::String:
	case ConfigMetadataTag::Path:
	case ConfigMetadataTag::PrintSink:
	{
		print(sink, "%[< %]- %\n%[< %]%\n%[< %]type:    %\n%[< %]default: '%'\n\n",
			"", (indent + 1) * 4 - 2,
			curr->name,
			"", (indent + 1) * 4,
			curr->helptext,
			"", (indent + 1) * 4,
			curr->tag == ConfigMetadataTag::String ? "string" : curr->tag == ConfigMetadataTag::Path ? "input path" : "output path",
			"", (indent + 1) * 4,
			curr->attach.string
		);

		return;
	}

	case ConfigMetadataTag::Boolean:
	{
		print(sink, "%[< %]- %\n%[< %]%\n%[< %]type:    bool\n%[< %]default: %\n\n",
			"", (indent + 1) * 4 - 2,
			curr->name,
			"", (indent + 1) * 4,
			curr->helptext,
			"", (indent + 1) * 4,
			"", (indent + 1) * 4,
			curr->attach.boolean
		);

		return;
	}

	case ConfigMetadataTag::Dynamic:
	{
		print(sink, "%[< %]- %\n%[< %]%\n%[< %]type:    dynamic\n%[< %]default: none\n\n",
			"", (indent + 1) * 4 - 2,
			curr->name,
			"", (indent + 1) * 4,
			curr->helptext,
			"", (indent + 1) * 4,
			"", (indent + 1) * 4
		);

		return;
	}

	case ConfigMetadataTag::INVALID:
		; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}



bool config_from_toml_file(Range<char8> filepath, PrintSink error_sink, Config* out_config, TreeSchemaAllocator* out_ts_alloc) noexcept
{
	*out_config = Config{};

	TreeSchemaAllocator ts_alloc = ts_allocator_create(1 << 19, 1 << 12);

	const Maybe<TreeSchemaTable*> opt_toml = parse_toml_file(filepath, error_sink, &ts_alloc);

	const ConfigMetadataEntry* meta_root = &CONFIG_METADATA.self_;

	if (is_none(opt_toml))
	{
		ts_allocator_release(ts_alloc);

		return false;
	}

	TomlConfigMappingContext ctx;
	ctx.filepath = filepath;
	ctx.error_sink = error_sink;
	ctx.out = out_config;
	ctx.ts_alloc = some(&ts_alloc);



	const u32 directory_path_count = minos::path_to_absolute_directory(filepath, MutRange{ ctx.directory_path_buf });

	if (directory_path_count == 0 || directory_path_count > sizeof(ctx.directory_path_buf))
		return config_error(&ctx, 0, 0, "Failed to get path of directory containing TOML file `%`.\n (0x%[|X])", filepath, minos::last_error());

	ctx.directory_path = Range{ ctx.directory_path_buf, directory_path_count };



	const TreeSchemaValue root_value = ts_value_from_table_unnamed(0, 0, get(opt_toml));


	if (!map_toml_to_config(&ctx, &root_value, &meta_root))
	{
		ts_allocator_release(ts_alloc);

		return false;
	}

	*out_ts_alloc = ts_alloc;

	return true;
}

Config config_defaults() noexcept
{
	Config config{};

	TomlConfigMappingContext ctx;
	ctx.filepath = range::from_literal_string("<pseudo-file>");
	ctx.error_sink = print_make_sink(minos::standard_file_handle(minos::StdFileName::StdErr));
	ctx.out = &config;
	ctx.ts_alloc = none<TreeSchemaAllocator*>();
	
	const ConfigMetadataEntry* meta = &CONFIG_METADATA.self_;

	const ConfigMetadataEntry* const end = meta + meta->attach.child_count;

	while (meta != end)
	{
		if (!map_defaults_to_config(&ctx, &meta))
			panic("Failed to create default config.\n");
	}

	return config;
}

void print_config(PrintSink sink, const Config* config) noexcept
{
	const ConfigMetadataEntry* meta = &CONFIG_METADATA.self_;

	const ConfigMetadataEntry* const end = meta + meta->attach.child_count;

	while (meta < end)
		print_config_element(sink, config, &meta, 0);
}

void print_config_help(PrintSink sink, u32 depth) noexcept
{
	const ConfigMetadataEntry* meta = &CONFIG_METADATA.self_;

	const ConfigMetadataEntry* const end = meta + meta->attach.child_count;

	while (meta < end)
		print_config_help_element(sink, &meta, 0, depth);
}
