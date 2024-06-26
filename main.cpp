#include "common.hpp"
#include "config.hpp"
#include "task_manag0r.hpp"
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
	else if (argc == 2 && strcmp(argv[1], "--help") == 0)
	{
		fprintf(stderr, "No help available yet :P\n");

		return EXIT_SUCCESS;
	}
	else if (argc == 3 && strcmp(argv[1] , "--config") == 0)
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

		fprintf(stdout,
			"entrypoint = {\n"
			"    filepath = %.*s\n"
			"    symbol = %.*s\n"
			"}\n"
			"\n"
			"input = {\n"
			"    bytes-per-read = %u\n"
			"    max-concurrent-reads = %u\n"
			"    max-concurrent-files = %u\n"
			"    max-concurrent-reads-per-file = %u\n"
			"    max-pending-reads = %u\n"
			"}\n"
			"\n"
			"memory = {\n"
			"    files = {\n"
			"        reserve = %u\n"
			"        initial-commit = %u\n"
			"        commit-increment = %u\n"
			"        lookup = {\n"
			"            reserve = %u\n"
			"            initial-commit = %u\n"
			"            commit-increment = %u\n"
			"        }\n"
			"    }\n"
			"}\n",
			static_cast<s32>(config.entrypoint.filepath.count()), config.entrypoint.filepath.begin(),
			static_cast<s32>(config.entrypoint.symbol.count()), config.entrypoint.symbol.begin(),
			config.input.bytes_per_read,
			config.input.max_concurrent_reads,
			config.input.max_concurrent_files,
			config.input.max_concurrent_reads_per_file,
			config.input.max_pending_files,
			config.memory.files.reserve,
			config.memory.files.initial_commit,
			config.memory.files.commit_increment,
			config.memory.files.lookup.reserve,
			config.memory.files.lookup.initial_commit,
			config.memory.files.lookup.commit_increment
		);

		deinit_config(&config);

		return EXIT_SUCCESS;
	}
	else
	{
		fprintf(stderr, "Usage: %s ( --help | --config <filepath> )\n", argv[0]);

		return EXIT_FAILURE;
	}
}
