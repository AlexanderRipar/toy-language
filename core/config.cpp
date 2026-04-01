#include "core.hpp"

#include "../infra/panic.hpp"
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

static constexpr TreeSchemaNode CONFIG_HEAP[] = {
	make_integer_info(offsetof(Config, heap.reserve), 0, INT64_MAX, "reserve", "Size the managed heap's small allocation section can grow to, in bytes"),
	make_integer_info(offsetof(Config, heap.commit_increment), 0, INT64_MAX, "commit-increment", "Number of bytes the managed heap's small allocation section is resized in"),
	make_integer_info(offsetof(Config, heap.max_huge_alloc_count), 0, INT64_MAX, "max-huge-allocation-count", "Maximum number of huge allocations that may be alive at the same time in the managed heap"),
};

static constexpr TreeSchemaNode CONFIG_LOGGING_IMPORTS_ASTS[] = {
	make_boolean_info(offsetof(Config, logging.imports.asts.enable), "enable", "Print ASTs after they are parsed"),
	make_path_info(offsetof(Config, logging.imports.asts.filepath), "log-file", "Path of the log file. Defaults to stdout"),
};

static constexpr TreeSchemaNode CONFIG_LOGGING_IMPORTS_OPCODES[] = {
	make_boolean_info(offsetof(Config, logging.imports.opcodes.enable), "enable", "Print opcodes generated for top-level members of imported files"),
	make_path_info(offsetof(Config, logging.imports.opcodes.filepath), "log-file", "Path of the log file. Defaults to stdout"),
};

static constexpr TreeSchemaNode CONFIG_LOGGING_IMPORTS_TYPES[] = {
	make_boolean_info(offsetof(Config, logging.imports.types.enable), "enable", "Print file types after they are imported and typechecked"),
	make_path_info(offsetof(Config, logging.imports.types.filepath), "log-file", "Path of the log file. Defaults to stdout"),
};

static constexpr TreeSchemaNode CONFIG_LOGGING_IMPORTS[] = {
	make_container_info(Range<TreeSchemaNode>{ CONFIG_LOGGING_IMPORTS_ASTS }, "asts", "AST logging parameters"),
	make_container_info(Range<TreeSchemaNode>{ CONFIG_LOGGING_IMPORTS_OPCODES }, "opcodes", "Top-level opcode logging parameters"),
	make_container_info(Range<TreeSchemaNode>{ CONFIG_LOGGING_IMPORTS_TYPES }, "types", "File type logging parameters"),
};

static constexpr TreeSchemaNode CONFIG_LOGGING_CONFIG[] = {
	make_boolean_info(offsetof(Config, logging.config.enable), "enable", "Print config after it is parsed"),
	make_boolean_info(offsetof(Config, logging.config.filepath), "log-file", "Path of the log file. Defaults to stdout"),
};

static constexpr TreeSchemaNode CONFIG_LOGGING_DIAGNOSTICS[] = {
	make_boolean_info(offsetof(Config, logging.diagnostics.file.enable), "enable", "Print diagnostics"),
	make_boolean_info(offsetof(Config, logging.diagnostics.file.filepath), "log-file", "Path of the log file. Defaults to stderr"),
	make_integer_info(offsetof(Config, logging.diagnostics.source_tab_size), 1, 32, "source-tab-size", "Number of characters a tab is equivalent to when reporting column numbers"),
};

static constexpr TreeSchemaNode CONFIG_LOGGING[] = {
	make_container_info(Range<TreeSchemaNode>{ CONFIG_LOGGING_IMPORTS }, "imports", "file import logging parameters"),
	make_container_info(Range<TreeSchemaNode>{ CONFIG_LOGGING_CONFIG }, "config", "Config logging parameters"),
	make_container_info(Range<TreeSchemaNode>{ CONFIG_LOGGING_DIAGNOSTICS }, "diagnostics", "Source-level warning and error message logging parameters"),
};

static constexpr TreeSchemaNode CONFIG_ENABLE[] = {
	make_boolean_info(offsetof(Config, enable.heap), "ast_pool", "Whether to initialize the managed heap. Shared storage for all kinds of stuff with interesting lifetimes"),
	make_boolean_info(offsetof(Config, enable.ast_pool), "ast_pool", "Whether to initialize the ast pool. Responsible for storing parsed ASTs"),
	make_boolean_info(offsetof(Config, enable.error_sink), "error_sink", "Whether to initialize the error sink. Responsible for reporting compilation errors"),
	make_boolean_info(offsetof(Config, enable.global_value_pool), "global_value_pool", "Whether to initialize the global value pool. Responsible for storing global values and values with global lifetime during compilation"),
	make_boolean_info(offsetof(Config, enable.identifier_pool), "identifier_pool", "Whether to initialize the identifier pool. Responsible for interning identifiers in the compiled source code"),
	make_boolean_info(offsetof(Config, enable.interpreter), "interpreter", "Whether to initialize the interpreter. Responsible for running source code during compilation"),
	make_boolean_info(offsetof(Config, enable.lexical_analyser), "lexical_analyser", "Whether to initialize the lexical analyser. Responsible for enriching parsed ASTs with statically inferrable information"),
	make_boolean_info(offsetof(Config, enable.opcode_pool), "opcode_pool", "Whether to initialize the opcode pool. Responsible for producing and storing intermediate language opcodes"),
	make_boolean_info(offsetof(Config, enable.parser), "parser", "Whether to initialize the parser. Responsible for parsing source code into ASTs"),
	make_boolean_info(offsetof(Config, enable.source_reader), "source_reader", "Whether to initialize the source reader. Responsible for reading source files and making sure no source file is processed more than once by keeping track of seen files"),
	make_boolean_info(offsetof(Config, enable.type_pool), "type_pool", "Whether to initialize the type pool. Responsible for storing types"),
};

static constexpr TreeSchemaNode CONFIG_ROOTS[] = {
	make_container_info(Range<TreeSchemaNode>{ CONFIG_ENTRYPOINT }, "entrypoint", "Entrypoint configuration"),
	make_container_info(Range<TreeSchemaNode>{ CONFIG_STD }, "std", "Standard library configuration"),
	make_container_info(Range<TreeSchemaNode>{ CONFIG_HEAP }, "heap", "Managed heap configuration"),
	make_container_info(Range<TreeSchemaNode>{ CONFIG_LOGGING }, "logging", "Debug log configuration"),
	make_container_info(Range<TreeSchemaNode>{ CONFIG_ENABLE }, "enable", "Allows only initializing certain submodules. Mainly intended for testing, when not all modules are exercised"),
	make_boolean_info(offsetof(Config, compile_all), "compile-all", "Whether to compile all top-level definitions"),
};

static constexpr TreeSchemaNode CONFIG = make_container_info(Range<TreeSchemaNode>{ CONFIG_ROOTS }, "config", "");





Maybe<minos::FileHandle> config_open_log_file(ConfigLogFileRef log_file, Maybe<minos::StdFileName> fallback) noexcept
{
	if (!log_file.enable)
		return none<minos::FileHandle>();

	if (log_file.filepath.count() == 0)
		return is_some(fallback) ? some(minos::standard_file_handle(get(fallback))) : none<minos::FileHandle>();

	minos::FileHandle file;

	if (!minos::file_create(log_file.filepath, minos::Access::Write, minos::ExistsMode::Truncate, minos::NewMode::Create, minos::AccessPattern::Sequential, none<const minos::CompletionInitializer*>(), false, &file))
		panic("Failed to open log file % (0x%[|X])\n", log_file.filepath, minos::last_error());

	return some(file);
}

const TreeSchemaNode* config_schema() noexcept
{
	return &CONFIG;
}
