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

		if (!import_prelude(core.interp, core.config->std.prelude.filepath))
			goto FAILURE;

		const Maybe<TypeId> main_file_type_id = import_file(core.interp, core.config->entrypoint.filepath, false);

		if (is_none(main_file_type_id))
			goto FAILURE;

		const IdentifierId entrypoint_name = id_from_identifier(core.identifiers, core.config->entrypoint.symbol);

		if (!evaluate_file_definition_by_name(core.interp, get(main_file_type_id), entrypoint_name))
			goto FAILURE;

		release_core_data(&core);

		fprintf(stderr, "Success\n");

		return EXIT_SUCCESS;

	FAILURE:

		print_errors(core.errors);

		release_core_data(&core);

		return EXIT_FAILURE;
	}
	else
	{
		fprintf(stderr, "Usage: %s ( -help | -config <filepath> )\n", argv[0]);

		return EXIT_FAILURE;
	}
}
