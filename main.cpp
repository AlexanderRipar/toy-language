#include "core/core.hpp"
#include "diag/diag.hpp"

#include <cstdlib>
#include <cstring>
#include <cstdio>

static minos::FileHandle log_file(bool enable, Range<char8> filepath) noexcept
{
	if (!enable)
		return minos::FileHandle{};

	if (filepath.count() == 0)
		return minos::standard_file_handle(minos::StdFileName::StdOut);

	minos::FileHandle log_file;

	if (!minos::file_create(filepath, minos::Access::Write, minos::ExistsMode::Truncate, minos::NewMode::Create, minos::AccessPattern::Sequential, nullptr, false, &log_file))
		panic("Failed to open log file %.*s (0x%X)\n", static_cast<s32>(filepath.count()), filepath.begin(), minos::last_error());

	return log_file;
}

s32 main(s32 argc, const char8** argv)
{
	if (argc == 0)
	{
		fprintf(stderr, "No arguments provided (not even invocation)\n");

		return EXIT_FAILURE;
	}
	else if (argc == 2 && strcmp(argv[1], "-help") == 0)
	{
		print_config_help();

		return EXIT_SUCCESS;
	}
	else if (argc == 3 && strcmp(argv[1] , "-config") == 0)
	{
		AllocPool* const alloc = create_alloc_pool(1u << 24, 1u << 18);

		Config* const config = create_config(alloc, range::from_cstring(argv[2]));



		const minos::FileHandle config_log_file = log_file(config->logging.config.enable, config->logging.config.log_filepath);

		if (config_log_file.m_rep != nullptr)
			print_config(config_log_file, config);

		const minos::FileHandle ast_log_file = log_file(config->logging.asts.enable, config->logging.asts.log_filepath);

		const minos::FileHandle imports_log_file = log_file(config->logging.imports.enable, config->logging.imports.log_filepath);



		IdentifierPool* const identifiers = create_identifier_pool(alloc);

		SourceReader* const reader = create_source_reader(alloc);

		ErrorSink* const errors = create_error_sink(alloc, reader, identifiers);

		GlobalValuePool* const globals = create_global_value_pool(alloc);

		TypePool* const types = create_type_pool(alloc, globals, errors);

		AstPool* const asts = create_ast_pool(alloc);

		Parser* const parser = create_parser(alloc, identifiers, globals, types, asts, errors, ast_log_file);

		PartialValuePool* const partials = create_partial_value_pool(alloc);

		Interpreter* const interp = create_interpreter(alloc, config, reader, parser, types, asts, identifiers, globals, partials, errors, imports_log_file, config->logging.imports.enable_prelude);

		const TypeId main_file_type_id = import_file(interp, config->entrypoint.filepath, false);

		release_config(config);

		fprintf(stderr, "\nCompleted successfully\n");

		return EXIT_SUCCESS;
	}
	else
	{
		fprintf(stderr, "Usage: %s ( -help | -config <filepath> )\n", argv[0]);

		return EXIT_FAILURE;
	}
}
