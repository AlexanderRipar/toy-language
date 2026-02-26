#include "core/core.hpp"
#include "diag/diag.hpp"
#include "infra/print/print.hpp"
#include "infra/toml/toml.hpp"

#include <cstdlib>
#include <cstring>

s32 main(s32 argc, const char8** argv)
{
	if (argc == 0)
	{
		print(minos::standard_file_handle(minos::StdFileName::StdErr), "No arguments provided (not even invocation)\n");

		return EXIT_FAILURE;
	}
	else if (argc == 2 && strcmp(argv[1], "-help") == 0)
	{
		diag::print_config_help(minos::standard_file_handle(minos::StdFileName::StdOut));

		return EXIT_SUCCESS;
	}
	else if (argc == 3 && strcmp(argv[1] , "-config") == 0)
	{
		MutRange<byte> config_allocation;

		Config config{};

		if (!parse_toml(range::from_cstring(argv[2]), config_schema(), range::from_object_bytes_mut(&config), &config_allocation))
			return EXIT_FAILURE;

		CoreData core = create_core_data(&config);

		if (run_compilation(&core, false))
		{
			print(minos::standard_file_handle(minos::StdFileName::StdErr), "Success\n");

			release_core_data(&core);

			release_toml(config_allocation);

			return EXIT_SUCCESS;
		}
		else
		{
			print_errors(core.errors);

			print(minos::standard_file_handle(minos::StdFileName::StdErr), "\nFailure\n");

			release_core_data(&core);

			release_toml(config_allocation);

			return EXIT_FAILURE;
		}
	}
	else
	{
		print(minos::standard_file_handle(minos::StdFileName::StdErr), "Usage: % ( -help | -config <filepath> )\n", argv[0]);

		return EXIT_FAILURE;
	}
}
