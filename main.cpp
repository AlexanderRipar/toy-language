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
		Config config;

		ConfigParseError error;

		if (!read_config_from_file(argv[2], &error, &config))
		{
			fprintf(stderr,
				"[%s:%u:%u] %s:\n    %s\n    %*s",
				argv[2],
				error.line_number,
				error.character_number,
				error.message,
				error.context,
				error.context_begin, "");

			for (u32 i = 0; i != error.context_end - error.context_begin; ++i)
				fprintf(stderr, "^");

			fprintf(stderr, "\n");

			return EXIT_FAILURE;
		}

		print_config(&config);

		Reader reader;

		reader.init();

		reader.read(config.entrypoint.filepath);

		Parser parser{};

		Range<char8> file;

		while (reader.await_completed_read(&file))
		{
			ast::raw::Tree tree = parser.parse(file);

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
