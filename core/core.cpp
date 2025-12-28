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
	HandlePool* const alloc = create_handle_pool(1u << 24, 1u << 18);

	Config* const config = create_config(alloc, config_filepath);

	const minos::FileHandle config_log_file = get_log_file(config->logging.config.enable, config->logging.config.log_filepath, minos::StdFileName::StdOut);

	if (config_log_file.m_rep != nullptr)
		print_config(config_log_file, config);

	const minos::FileHandle imported_asts_log_file = get_log_file(config->logging.imports.asts.enable, config->logging.imports.asts.log_filepath, minos::StdFileName::StdOut);

	const minos::FileHandle imported_opcodes_log_file = get_log_file(config->logging.imports.opcodes.enable, config->logging.imports.opcodes.log_filepath, minos::StdFileName::StdOut);

	const minos::FileHandle imported_types_log_file = get_log_file(config->logging.imports.types.enable, config->logging.imports.types.log_filepath, minos::StdFileName::StdOut);

	const minos::FileHandle diagnostics_log_file = get_log_file(config->logging.diagnostics.enable, config->logging.diagnostics.log_filepath, minos::StdFileName::StdErr);

	CoreData core;
	core.alloc = alloc;
	core.config = config;
	core.identifiers = create_identifier_pool(core.alloc);
	core.reader = create_source_reader(core.alloc);
	core.globals = create_global_value_pool(core.alloc);
	core.types = create_type_pool(core.alloc);
	core.asts = create_ast_pool(core.alloc);
	core.errors = create_error_sink(core.alloc, core.reader, core.identifiers, core.asts, diagnostics_log_file);
	core.parser = create_parser(core.alloc, core.identifiers, core.globals, core.types, core.asts, core.errors);
	core.opcodes = create_opcode_pool(core.alloc, core.asts);
	core.lex = create_lexical_analyser(core.alloc, core.identifiers, core.asts, core.errors);
	core.interp = create_interpreter(core.alloc, core.asts, core.types, core.globals, core.opcodes, core.reader, core.parser, core.identifiers, core.lex, core.errors, imported_asts_log_file, imported_opcodes_log_file, imported_types_log_file);

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
	release_opcode_pool(core->opcodes);
	release_lexical_analyser(core->lex);
	release_interpreter(core->interp);

	release_handle_pool(core->alloc);
}

bool run_compilation(CoreData* core, bool main_is_std) noexcept
{
	if (!import_prelude(core->interp, core->config->std.prelude.filepath))
		return false;

	const Maybe<TypeId> main_file_type_id = import_file(core->interp, core->config->entrypoint.filepath, main_is_std);

	if (is_none(main_file_type_id))
		return false;

	if (core->config->compile_all)
	{
		if (!evaluate_all_file_definitions(core->interp, get(main_file_type_id)))
			return false;
	}
	else
	{
		const IdentifierId entrypoint_name = id_from_identifier(core->identifiers, core->config->entrypoint.symbol);

		if (!evaluate_file_definition_by_name(core->interp, get(main_file_type_id), entrypoint_name))
			return false;
	}

	return true;
}
