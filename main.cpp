#include "config.hpp"
#include "pass_data.hpp"
#include "diag/inc.hpp"
#include "infra/hash.hpp"

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
