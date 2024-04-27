#include "../common.hpp"
#include "tests.hpp"
#include "helpers.hpp"
#include <cstdio>
#include <cstdlib>

s32 main(s32 argc, const char8** argv)
{
	test_system_init(argc, argv);

	test::threading();

	return test_system_deinit();
}
