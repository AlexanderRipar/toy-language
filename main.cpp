#include "config.hpp"
#include "pass/pass_data.hpp"
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

		Globals parse_data{};

		read::request_read(&parse_data, config.entrypoint.filepath, parse_data.identifiers.index_from(config.entrypoint.filepath, fnv1a(config.entrypoint.filepath.as_byte_range())));

		SourceFile file;

		while (read::await_completed_read(&parse_data, &file))
		{
			ast::Tree tree = parse(&parse_data, file);

			read::release_read(&parse_data, file);

			diag::print_ast(stderr, &tree, &parse_data);
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
