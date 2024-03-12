#ifndef CONFIG_INCLUDE_GUARD
#define CONFIG_INCLUDE_GUARD

#include "common.hpp"

namespace config
{
	struct Config
	{
		struct
		{
			u32 worker_thread_count;

			u32 max_string_length;

			const char8* invocation;
		} base;

		struct
		{
			u32 initial_input_file_count;

			const char8** initial_input_files;
		} input;

		struct
		{
			u32 bytes_per_read;

			u32 max_concurrent_read_count;

			u32 max_concurrent_file_read_count;

			u32 max_concurrent_read_count_per_file;
		} read;

		struct
		{
			u32 file_max_count;

			u32 file_commit_increment_count;

			u32 file_initial_commit_count;

			u32 file_initial_lookup_count;

			u32 file_reads_max_count;

			u32 file_reads_commit_increment_count;

			u32 file_reads_initial_commit_count;
		} mem;
	};

	bool init(u32 argc, const char8** argv) noexcept;

	const Config* get() noexcept;
}

#endif // CONFIG_INCLUDE_GUARD
