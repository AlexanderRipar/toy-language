#ifndef CONFIG_INCLUDE_GUARD
#define CONFIG_INCLUDE_GUARD

#include "common.hpp"

struct Config
{
	struct ThreadsafeMapConfig
	{
		struct
		{
			u32 reserve;

			u32 initial_commit;

			u32 max_insertion_distance;
		} map;

		struct
		{
			u32 reserve;

			u32 commit_increment;

			u32 initial_commit_per_thread;
		} store;
	};

	struct
	{
		u32 thread_count;
	} parallel;

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
	} input;

	struct
	{
		struct
		{
			ThreadsafeMapConfig files;

			ThreadsafeMapConfig filenames;

			u32 max_pending_files;
		}input;
	} detail;

	void* m_heap_ptr_;
};

struct ConfigParseError
{
	u32 line_number;

	u32 character_number;

	u32 context_begin;

	u32 context_end;

	char8 message[256];

	char8 context[256];
};

bool read_config_from_file(const char8* config_filepath, ConfigParseError* out_error, Config* out) noexcept;

void deinit_config(Config* config) noexcept;

void print_config(const Config* config) noexcept;

void print_config_help(u32 depth = 0) noexcept;

#endif // CONFIG_INCLUDE_GUARD
