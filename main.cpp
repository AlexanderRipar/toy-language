#include "common.hpp"
#include "config.hpp"
#include "parser.hpp"
#include "reader.hpp"
#include "ast/ast_fmt.hpp"
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

		Reader reader{};

		Parser parser{};

		reader.read(config.entrypoint.filepath, parser.index_from_string(config.entrypoint.filepath));

		SourceFile file;

		while (reader.await_completed_read(&file))
		{
			ast::raw::Tree tree = parser.parse(file);

			reader.release_read(file);

			ast::raw::format(stderr, &tree, parser.identifiers());
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
