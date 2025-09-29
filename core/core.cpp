#include "core.hpp"

static minos::FileHandle get_log_file(bool enable, Range<char8> filepath, minos::StdFileName fallback) noexcept
{
	if (!enable)
		return minos::FileHandle{};

	if (filepath.count() == 0)
		return minos::standard_file_handle(fallback);

	minos::FileHandle log_file;

	if (!minos::file_create(filepath, minos::Access::Write, minos::ExistsMode::Truncate, minos::NewMode::Create, minos::AccessPattern::Sequential, nullptr, false, &log_file))
		panic("Failed to open log file %.*s (0x%X)\n", static_cast<s32>(filepath.count()), filepath.begin(), minos::last_error());

	return log_file;
}

CoreData create_core_data(Range<char8> config_filepath) noexcept
{
	AllocPool* const alloc = create_alloc_pool(1u << 24, 1u << 18);

	Config* const config = create_config(alloc, config_filepath);

	const minos::FileHandle config_log_file = get_log_file(config->logging.config.enable, config->logging.config.log_filepath, minos::StdFileName::StdOut);

	if (config_log_file.m_rep != nullptr)
		print_config(config_log_file, config);

	const minos::FileHandle ast_log_file = get_log_file(config->logging.asts.enable, config->logging.asts.log_filepath, minos::StdFileName::StdOut);

	const minos::FileHandle imports_log_file = get_log_file(config->logging.imports.enable, config->logging.imports.log_filepath, minos::StdFileName::StdOut);

	const minos::FileHandle diagnostics_log_file = get_log_file(config->logging.diagnostics.enable, config->logging.diagnostics.log_filepath, minos::StdFileName::StdErr);

	CoreData core;
	core.alloc = alloc;
	core.config = config;
	core.identifiers = create_identifier_pool(core.alloc);
	core.reader = create_source_reader(core.alloc);
	core.errors = create_error_sink(core.alloc, core.reader, core.identifiers, diagnostics_log_file);
	core.globals = create_global_value_pool(core.alloc);
	core.types = create_type_pool(core.alloc);
	core.asts = create_ast_pool(core.alloc);
	core.parser = create_parser(core.alloc, core.identifiers, core.globals, core.types, core.asts, core.errors);
	core.partials = create_partial_value_pool(core.alloc);
	core.closures = create_closure_pool(core.alloc, core.types);
	core.lex = create_lexical_analyser(core.alloc, core.identifiers, core.asts, core.errors);
	core.interp = create_interpreter(core.alloc, core.config, core.reader, core.parser, core.types, core.asts, core.identifiers, core.globals, core.partials, core.closures, core.lex, core.errors, imports_log_file, ast_log_file, config->logging.imports.enable_prelude);

	return core;
}

void release_core_data(CoreData* core) noexcept
{
	release_config(core->config);
	release_identifier_pool(core->identifiers);
	release_source_reader(core->reader);
	release_error_sink(core->errors);
	release_global_value_pool(core->globals);
	release_type_pool(core->types);
	release_ast_pool(core->asts);
	release_parser(core->parser);
	release_partial_value_pool(core->partials);
	release_closure_pool(core->closures);
	release_lexical_analyser(core->lex);
	release_interpreter(core->interp);

	release_alloc_pool(core->alloc);
}
