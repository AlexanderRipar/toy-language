#include "config.hpp"
#include "pass_data.hpp"
#include "diag/diag.hpp"
#include "infra/hash.hpp"
#include "ast2_helper.hpp"

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
		Config config = read_config(range::from_cstring(argv[2]));

		print_config(&config);

		AllocPool* const pool = create_alloc_pool(1u << 24, 1u << 18);

		IdentifierPool* const identifiers = create_identifier_pool(pool);

		Parser* const parser = create_parser(pool, identifiers);

		SourceReader* const reader = create_source_reader(pool);

		request_read(reader, config.entrypoint.filepath, id_from_identifier(identifiers, config.entrypoint.filepath));

		SourceFile file;

		ReservedVec<u32> asts;
		asts.init(1u << 31, 1u << 18);

		if (!await_completed_read(reader, &file))
			panic("Could not read main source file\n");

		a2::Node* main_root = parse(parser, file, &asts);

		release_read(reader, file);

		diag::print_ast(stderr, main_root);

		const OptPtr<a2::Node> main_definition = a2::try_find_definition(main_root, id_from_identifier(identifiers, config.entrypoint.symbol));

		if (is_none(main_definition))
			panic("Could not find entrypoint symbol \"%.*s\" at top level of source file \"%.*s\"", static_cast<s32>(config.entrypoint.symbol.count()), config.entrypoint.symbol.begin(), static_cast<s32>(config.entrypoint.filepath.count()), config.entrypoint.filepath.begin());

		while (await_completed_read(reader, &file))
		{
			a2::Node* root = parse(parser, file, &asts);

			release_read(reader, file);

			diag::print_ast(stderr, root);
		}

		deinit_config(&config);

		return EXIT_SUCCESS;
	}
	else
	{
		fprintf(stderr, "Usage: %s ( -help | -config <filepath> )\n", argv[0]);

		return EXIT_FAILURE;
	}
}
