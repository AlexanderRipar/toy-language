#ifndef CONFIG_INCLUDE_GUARD
#define CONFIG_INCLUDE_GUARD

#include "../infra/common.hpp"
#include "../infra/range.hpp"

struct Config
{
	struct
	{
		Range<char8> filepath = range::from_literal_string("main.evl");

		Range<char8> symbol = range::from_literal_string("main");
	} entrypoint;

	struct
	{
		Range<char8> filepath = range::from_literal_string("std.evl");
	} std;
	

	void* m_heap_ptr;
};

Config read_config(Range<char8> filepath) noexcept;

void deinit_config(Config* config) noexcept;

void print_config(const Config* config) noexcept;

void print_config_help(u32 depth = 0) noexcept;

#endif // CONFIG_INCLUDE_GUARD
