#include "core/pass_data.hpp"
#include "diag/diag.hpp"

#include <cstdlib>
#include <cstring>
#include <cstdio>

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

		if (config->logging.config.enable)
		{
			minos::FileHandle config_log_file;

			if (config->logging.config.log_filepath.count() == 0)
			{
				config_log_file = minos::standard_file_handle(minos::StdFileName::StdOut);
			}
			else
			{
				if (!minos::file_create(config->logging.config.log_filepath, minos::Access::Write, minos::ExistsMode::Truncate, minos::NewMode::Create, minos::AccessPattern::Sequential, nullptr, false, &config_log_file))
					panic("Failed to open config log file %.*s (0x%X)\n", static_cast<s32>(config->logging.config.log_filepath.count()), config->logging.config.log_filepath.begin(), minos::last_error());
			}

			print_config(config_log_file, config);
		}

		minos::FileHandle ast_log_file{};

		if (config->logging.asts.enable)
		{
			if (config->logging.asts.log_filepath.count() == 0)
			{
				ast_log_file = minos::standard_file_handle(minos::StdFileName::StdOut);
			}
			else
			{
				if (!minos::file_create(config->logging.asts.log_filepath, minos::Access::Write, minos::ExistsMode::Truncate, minos::NewMode::Create, minos::AccessPattern::Sequential, nullptr, false, &ast_log_file))
					panic("Failed to open ast log file %.*s (0x%X)\n", static_cast<s32>(config->logging.asts.log_filepath.count()), config->logging.asts.log_filepath.begin(), minos::last_error());
			}
		}

		IdentifierPool* const identifiers = create_identifier_pool(alloc);

		SourceReader* const reader = create_source_reader(alloc);

		ErrorSink* const errors = create_error_sink(alloc, reader, identifiers);

		TypePool* const types = create_type_pool(alloc, errors);

		GlobalValuePool* const globals = create_global_value_pool(alloc, types);

		AstPool* const asts = create_ast_pool(alloc);

		Parser* const parser = create_parser(alloc, identifiers, globals, asts, errors, ast_log_file);

		Interpreter* const interp = create_interpreter(alloc, config, reader, parser, types, asts, identifiers, globals, errors);

		const TypeId main_file_type_id = import_file(interp, config->entrypoint.filepath, false);

		SourceLocation main_file_type_location = source_location_from_source_id(reader, type_source_from_id(types, main_file_type_id));

		diag::print_type(minos::standard_file_handle(minos::StdFileName::StdOut), identifiers, types, main_file_type_id, &main_file_type_location);

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
