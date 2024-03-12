#include "../common.hpp"
#include "tests.hpp"
#include <cstdio>
#include <cstdlib>

s32 main(s32 argc, const char8** argv)
{
	FILE* out_file = stdout;

	if (argc == 2)
	{
		if (fopen_s(&out_file, argv[1], "w") != 0)
		{
			fprintf(stderr, "Could not open file %s\n", argv[1]);

			return EXIT_FAILURE;
		}
	}
	else if (argc != 1)
	{
		fprintf(stderr, "Usage: %s [output-file]\n", argv[0]);

		return EXIT_FAILURE;
	}

	u32 error_count = 0;

	error_count += test::container(out_file);

	error_count += test::threading(out_file);

	if (error_count == 0)
	{
		fprintf(stdout, "All tests passed.\n");

		return EXIT_SUCCESS;
	}

	fprintf(stdout, "%u test cases failed. See %s for log.\n", error_count, argc == 2 ? argv[1] : "above");

	return EXIT_FAILURE;
}
