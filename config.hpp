#ifndef CONFIG_INCLUDE_GUARD
#define CONFIG_INCLUDE_GUARD

#include "common.hpp"
#include "range.hpp"

struct Config
{
	struct
	{
		Range<char8> filepath = range::from_literal_string("main.evl");

		Range<char8> symbol = range::from_literal_string("main");
	} entrypoint;

	void* m_heap_ptr;
};

Config read_config(Range<char8> filepath) noexcept;

void deinit_config(Config* config) noexcept;

void print_config(const Config* config) noexcept;

void print_config_help(u32 depth = 0) noexcept;

#endif // CONFIG_INCLUDE_GUARD
