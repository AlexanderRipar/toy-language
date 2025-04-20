#include "core/config.hpp"
#include "core/pass_data.hpp"
#include "core/ast_helper.hpp"
#include "diag/diag.hpp"
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

		AllocPool* const alloc = create_alloc_pool(1u << 24, 1u << 18);

		IdentifierPool* const identifiers = create_identifier_pool(alloc);

		SourceReader* const reader = create_source_reader(alloc);

		ErrorSink* const errors = create_error_sink(alloc, reader, identifiers);

		Parser* const parser = create_parser(alloc, identifiers, errors);

		AstPool* const asts = create_ast_pool(alloc);

		deinit_config(&config);

		fprintf(stderr, "\nCompleted successfully\n");

		return EXIT_SUCCESS;
	}
	else
	{
		fprintf(stderr, "Usage: %s ( -help | -config <filepath> )\n", argv[0]);

		return EXIT_FAILURE;
	}
}
