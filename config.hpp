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
};

struct ConfigParseError
{
	const char8* message;

	u32 line;

	u32 character;

	u32 character_in_context;

	char8 context[120];
};

bool read_config_from_file(const char8* config_filepath, ConfigParseError* out_error, Config* out) noexcept;


#endif // CONFIG_INCLUDE_GUARD
