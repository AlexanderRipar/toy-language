#include "core/core.hpp"
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
		CoreData core = create_core_data(range::from_cstring(argv[2]));

		if (run_compilation(&core, false))
		{
			fprintf(stderr, "Success\n");

			release_core_data(&core);

			return EXIT_SUCCESS;
		}
		else
		{
			print_errors(core.errors);

			release_core_data(&core);

			return EXIT_FAILURE;
		}
	}
	else
	{
		fprintf(stderr, "Usage: %s ( -help | -config <filepath> )\n", argv[0]);

		return EXIT_FAILURE;
	}
}
