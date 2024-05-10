#ifndef CONFIG_INCLUDE_GUARD
#define CONFIG_INCLUDE_GUARD

#include "common.hpp"

struct Config
{
	struct
	{
		Range<char8> filepath;

		Range<char8> symbol;
	} entrypoint;

	struct
	{
		u32 bytes_per_read;

		u32 max_concurrent_reads;

		u32 max_concurrent_files;

		u32 max_concurrent_reads_per_file;

		u32 max_pending_files;
	} input;

	struct
	{
		struct
		{
			u32 reserve;

			u32 initial_commit;

			u32 commit_increment;

			struct
			{
				u32 reserve;

				u32 initial_commit;

				u32 commit_increment;
			} lookup;
		} files;
	} memory;

	void* m_heap_ptr_;
};

struct ConfigParseError
{
	const char8* message;

	u32 line_number;

	u32 character_number;

	u32 context_begin;

	u32 context_end;

	char8 context[160];
};

bool read_config_from_file(const char8* config_filepath, ConfigParseError* out_error, Config* out) noexcept;

void deinit_config(Config* config) noexcept;

#endif // CONFIG_INCLUDE_GUARD
