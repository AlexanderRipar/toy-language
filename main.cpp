#include "common.hpp"
#include "config.hpp"
#include "task_manag0r.hpp"
#include <cstdlib>
#include <cstdio>

s32 main(s32 argc, const char8** argv)
{
	if (!config::init(argc, argv))
		return EXIT_FAILURE;

	const config::Config* cfg = config::get();

	printf("base.invocation=%s\n"
	       "base.worker_thread_count=%u\n"
	       "input.initial_input_file_count=%u\n"
	       "input.initial_input_files=\n",
	       cfg->base.invocation,
	       cfg->base.worker_thread_count,
	       cfg->input.initial_input_file_count);

	for (u32 i = 0; i != cfg->input.initial_input_file_count; ++i)
		printf("    %s\n", cfg->input.initial_input_files[i]);

	printf("read.bytes_per_read=%u\n"
	       "read.max_concurrent_read_count=%u\n"
	       "read.max_concurrent_read_count_per_file=%u\n",
	       cfg->read.bytes_per_read,
	       cfg->read.max_concurrent_read_count,
	       cfg->read.max_concurrent_read_count_per_file);

	return EXIT_SUCCESS;
}
