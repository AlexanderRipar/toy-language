#include "core.hpp"

#include "../infra/print/print.hpp"

#include <cstddef>

static constexpr TreeSchemaNode make_container_info(Range<TreeSchemaNode> children, const char8* name, const char8* helptext) noexcept
{
	return TreeSchemaNode{
		TreeSchemaContainerAttach{ children },
		0,
		name,
		helptext,
	};
}

static constexpr TreeSchemaNode make_integer_info(u32 target_offset, s64 min, s64 max, const char8* name, const char8* helptext) noexcept
{
	return TreeSchemaNode{
		TreeSchemaIntegerAttach{ min, max },
		target_offset,
		name,
		helptext,
	};
}

static constexpr TreeSchemaNode make_string_info(u32 target_offset, const char8* name, const char8* helptext) noexcept
{
	return TreeSchemaNode{
		TreeSchemaNodeKind::String,
		target_offset,
		name,
		helptext,
	};
}

static constexpr TreeSchemaNode make_path_info(u32 target_offset, const char8* name, const char8* helptext) noexcept
{
	return TreeSchemaNode{
		TreeSchemaNodeKind::Path,
		target_offset,
		name,
		helptext,
	};
}

static constexpr TreeSchemaNode make_boolean_info(u32 target_offset, const char8* name, const char8* helptext) noexcept
{
	return TreeSchemaNode{
		TreeSchemaNodeKind::Boolean,
		target_offset,
		name,
		helptext,
	};
}

static constexpr TreeSchemaNode CONFIG_ENTRYPOINT[] = {
	make_path_info(offsetof(Config, entrypoint.filepath), "filepath", "Relative path of the source file containing the program's entrypoint"),
	make_string_info(offsetof(Config, entrypoint.symbol), "symbol", "Symbol name of the program's entrypoint function"),
};

static constexpr TreeSchemaNode CONFIG_STD_PRELUDE[] = {
	make_path_info(offsetof(Config, std.prelude.filepath), "filepath", "Path to the file containing the prelude that is made available to all sources"),
};

static constexpr TreeSchemaNode CONFIG_STD[] = {
	make_container_info(Range<TreeSchemaNode>{ CONFIG_STD_PRELUDE }, "prelude", "Prelude parameters"),
};

static constexpr TreeSchemaNode CONFIG_LOGGING_IMPORTS_ASTS[] = {
	make_boolean_info(offsetof(Config, logging.imports.asts.enable), "enable", "Print ASTs after they are parsed"),
	make_path_info(offsetof(Config, logging.imports.asts.log_filepath), "log-file", "Path of the log file. Defaults to stdout"),
};

static constexpr TreeSchemaNode CONFIG_LOGGING_IMPORTS_OPCODES[] = {
	make_boolean_info(offsetof(Config, logging.imports.opcodes.enable), "enable", "Print opcodes generated for top-level members of imported files"),
	make_path_info(offsetof(Config, logging.imports.opcodes.log_filepath), "log-file", "Path of the log file. Defaults to stdout"),
};

static constexpr TreeSchemaNode CONFIG_LOGGING_IMPORTS_TYPES[] = {
	make_boolean_info(offsetof(Config, logging.imports.types.enable), "enable", "Print file types after they are imported and typechecked"),
	make_path_info(offsetof(Config, logging.imports.types.log_filepath), "log-file", "Path of the log file. Defaults to stdout"),
};

static constexpr TreeSchemaNode CONFIG_LOGGING_IMPORTS[] = {
	make_container_info(Range<TreeSchemaNode>{ CONFIG_LOGGING_IMPORTS_ASTS }, "asts", "AST logging parameters"),
	make_container_info(Range<TreeSchemaNode>{ CONFIG_LOGGING_IMPORTS_OPCODES }, "opcodes", "Top-level opcode logging parameters"),
	make_container_info(Range<TreeSchemaNode>{ CONFIG_LOGGING_IMPORTS_TYPES }, "types", "File type logging parameters"),
};

static constexpr TreeSchemaNode CONFIG_LOGGING_CONFIG[] = {
	make_boolean_info(offsetof(Config, logging.config.enable), "enable", "Print config after it is parsed"),
	make_boolean_info(offsetof(Config, logging.config.log_filepath), "log-file", "Path of the log file. Defaults to stdout"),
};

static constexpr TreeSchemaNode CONFIG_LOGGING_DIAGNOSTICS[] = {
	make_boolean_info(offsetof(Config, logging.diagnostics.enable), "enable", "Print diagnostics"),
	make_boolean_info(offsetof(Config, logging.diagnostics.log_filepath), "log-file", "Path of the log file. Defaults to stderr"),
	make_integer_info(offsetof(Config, logging.diagnostics.source_tab_size), 1, 32, "source-tab-size", "Number of characters a tab is equivalent to when reporting column numbers"),
};

static constexpr TreeSchemaNode CONFIG_LOGGING[] = {
	make_container_info(Range<TreeSchemaNode>{ CONFIG_LOGGING_IMPORTS }, "imports", "file import logging parameters"),
	make_container_info(Range<TreeSchemaNode>{ CONFIG_LOGGING_CONFIG }, "config", "Config logging parameters"),
	make_container_info(Range<TreeSchemaNode>{ CONFIG_LOGGING_DIAGNOSTICS }, "diagnostics", "Source-level warning and error message logging parameters"),
};

static constexpr TreeSchemaNode CONFIG_ROOTS[] = {
	make_container_info(Range<TreeSchemaNode>{ CONFIG_ENTRYPOINT }, "entrypoint", "Entrypoint configuration"),
	make_container_info(Range<TreeSchemaNode>{ CONFIG_STD }, "std", "Standard library configuration"),
	make_container_info(Range<TreeSchemaNode>{ CONFIG_LOGGING }, "logging", "Debug log configuration"),
	make_boolean_info(offsetof(Config, compile_all), "compile-all", "Whether to compile all top-level definitions. Defaults to `false`"),
};

static constexpr TreeSchemaNode CONFIG = make_container_info(Range<TreeSchemaNode>{ CONFIG_ROOTS }, "config", "");



static void print_config_node(PrintSink sink, const Config* config, const TreeSchemaNode* node, u32 indent) noexcept
{
	if (node->kind == TreeSchemaNodeKind::Container)
	{
		print(sink, "%[< %]% {\n", "", indent * 2, node->name);

		for (const TreeSchemaNode& child : node->container.children)
			print_config_node(sink, config, &child, indent + 1);

		print(sink, "%[< %]}\n", "", indent * 2);
	}
	else if (node->kind == TreeSchemaNodeKind::Integer)
	{
		const s64 value = *reinterpret_cast<const s64*>(reinterpret_cast<const byte*>(config) + node->target_offset);

		print(sink, "%[< %]% = %\n", "", indent * 2, node->name, value);
	}
	else if (node->kind == TreeSchemaNodeKind::String || node->kind == TreeSchemaNodeKind::Path)
	{
		const Range<char8> value = *reinterpret_cast<const Range<char8>*>(reinterpret_cast<const byte*>(config) + node->target_offset);

		print(sink, "%[< %]% = '%'\n", "", indent * 2, node->name, value);
	}
	else if (node->kind == TreeSchemaNodeKind::Boolean)
	{
		const bool value = *reinterpret_cast<const bool*>(reinterpret_cast<const byte*>(config) + node->target_offset);

		print(sink, "%[< %]% = %\n", "", indent * 2, node->name, value);
	}
	else
	{
		ASSERT_UNREACHABLE;
	}
}

static void print_config_help_node(PrintSink sink, const Config* defaults, const TreeSchemaNode* node, u32 indent, u32 max_indent) noexcept
{
	if (node->kind == TreeSchemaNodeKind::Container)
	{
		print(sink, "%[< %]% {\n%[< %]%\n",
			"", indent * 2,
			node->name,
			"", (indent + 1) * 2,
			node->helptext
		);

		if (indent != max_indent)
		{
			for (const TreeSchemaNode& child : node->container.children)
				print_config_help_node(sink, defaults, &child, indent + 1, max_indent);
		}

		print(sink, "%[< %]}\n",
			"", indent * 2
		);
	}
	else if (node->kind == TreeSchemaNodeKind::Integer)
	{
		const s64 default_value = *reinterpret_cast<const s64*>(reinterpret_cast<const byte*>(defaults) + node->target_offset);

		print(sink, "%[< %]% {\n%[< %]%\n%[< %]type: integer\n%[< %]default: %\n",
			"", indent * 2,
			node->name,
			"", (indent + 1) * 2,
			node->helptext,
			"", (indent + 1) * 2,
			"", (indent + 1) * 2,
			default_value
		);

		if (node->integer.min != INT64_MIN)
		{
			print(sink, "%[< %]min: %\n",
				"", (indent + 1) * 2,
				node->integer.min
			);
		}

		if (node->integer.max != INT64_MAX)
		{
			print(sink, "%[< %]max: %\n",
				"", (indent + 1) * 2,
				node->integer.max
			);
		}

		print(sink, "%[< %]}\n",
			"", indent * 2
		);
	}
	else if (node->kind == TreeSchemaNodeKind::String || node->kind == TreeSchemaNodeKind::Path)
	{
		const Range<char8> default_value = *reinterpret_cast<const Range<char8>*>(reinterpret_cast<const byte*>(defaults) + node->target_offset);

		print(sink, "%[< %]% {\n%[< %]%\n%[< %]type: %\n%[< %]default: %\n%[< %]}\n",
			"", indent * 2,
			node->name,
			"", (indent + 1) * 2,
			node->helptext,
			"", (indent + 1) * 2,
			node->kind == TreeSchemaNodeKind::String ? "string" : "path",
			"", (indent + 1) * 2,
			default_value,
			"", indent * 2
		);
	}
	else if (node->kind == TreeSchemaNodeKind::Boolean)
	{
		const bool default_value = *reinterpret_cast<const bool*>(reinterpret_cast<const byte*>(defaults) + node->target_offset);

		print(sink, "%[< %]% {\n%[< %]%\n%[< %]type: bool\n%[< %]default: %\n%[< %]}\n",
			"", indent * 2,
			node->name,
			"", (indent + 1) * 2,
			node->helptext,
			"", (indent + 1) * 2,
			"", (indent + 1) * 2,
			default_value,
			"", indent * 2
		);
	}
	else
	{
		ASSERT_UNREACHABLE;
	}
}



const TreeSchemaNode* config_schema() noexcept
{
	return &CONFIG;
}

void print_config(PrintSink sink, const Config* config) noexcept
{
	print(sink, "\n### CONFIG ###\n\n");

	for (const TreeSchemaNode& root : CONFIG.container.children)
		print_config_node(sink, config, &root, 0);
}

void print_config_help(PrintSink sink, u32 depth) noexcept
{
	print(sink, "config parameters:\n");

	const Config defaults{};

	for (const TreeSchemaNode& root : CONFIG.container.children)
		print_config_help_node(sink, &defaults, &root, 0, depth == 0 ? UINT32_MAX : depth);
}
