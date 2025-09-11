#include <cstdlib>
#include <cstring>
#include <cstdio>

#include "../infra/common.hpp"
#include "../infra/minos/minos.hpp"

s32 main(s32 argc, const char8** argv) noexcept
{
	if (argc == 1)
	{
		return EXIT_SUCCESS;
	}
	else if (argc == 2)
	{
		char8 cwd[8192];

		const u32 cwd_chars = minos::working_directory(MutRange{ cwd });

		if (cwd_chars == 0 || cwd_chars > array_count(cwd))
		{
			fprintf(stderr, "minos::working_directory failed (0x%X)\n", minos::last_error());

			return EXIT_FAILURE;
		}

		const char8* expected_suffix = argv[1];

		const u32 expected_suffix_chars = static_cast<u32>(strlen(expected_suffix));

		if (expected_suffix_chars > cwd_chars)
			return 2;

		const char8* suffix = cwd + cwd_chars - expected_suffix_chars;

		if (memcmp(suffix, expected_suffix, expected_suffix_chars) == 0)
		{
			return EXIT_SUCCESS;
		}

		return 2;
	}
	else
	{
		fprintf(stderr, "Usage: %s [<expected-working-directory-suffix>]\n", argv[0]);

		return EXIT_FAILURE;
	}
}
