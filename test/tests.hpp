#ifndef TESTS_INCLUDE_GUARD
#define TESTS_INCLUDE_GUARD

#include "../common.hpp"
#include <cstdio>

namespace test
{
	u32 container(FILE* out_file) noexcept;

	u32 threading(FILE* out_file) noexcept;
}

#endif // TESTS_INCLUDE_GUARD
