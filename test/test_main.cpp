#include <cstdio>
#include <vector>

#include "test_helpers.hpp"

std::vector<TestResult> g_test_times;

std::vector<TestResult> g_module_times;

const char8* g_curr_module;

bool g_ignore_debugbreaks;

void ast2_tests() noexcept;

struct TimeDesc
{
	const char8* unit;

	f64 count;
};

static TimeDesc readable_time(u64 duration, u64 ticks_per_second) noexcept
{
	if (duration > ticks_per_second * 60)
		return { "minutes", static_cast<f64>(duration) / (60 * ticks_per_second) };
	else if (duration > ticks_per_second)
		return { "seconds", static_cast<f64>(duration) / ticks_per_second };
	else if (duration * 1000 > ticks_per_second)
		return { "milliseconds", static_cast<f64>(duration * 1000) / ticks_per_second };
	else
		return { "microseconds", static_cast<f64>(duration * 1'000'000) / ticks_per_second };
}

s32 main(s32 argc, const char8** argv) noexcept
{
	const u64 start = minos::exact_timestamp();

	bool command_line_ok = argc == 1;

	if (argc == 2 && strcmp(argv[1], "--ignore-debugbreaks") == 0)
	{
		g_ignore_debugbreaks = true;

		command_line_ok = true;
	}
	
	if (!command_line_ok)
	{
		fprintf(stderr, "Usage %s [--ignore-debugbreaks]\n", argv[0]);

		return EXIT_FAILURE;
	}

	ast2_tests();

	const u64 duration = minos::exact_timestamp() - start;

	const TimeDesc rd = readable_time(duration, minos::exact_timestamp_ticks_per_second());

	u32 test_failure_count = 0;

	u32 assertion_failure_count = 0;

	for (const TestResult& result : g_test_times)
	{
		if (result.failure_count != 0)
		{
			test_failure_count += 1;

			assertion_failure_count += result.failure_count;
		}
	}

	if (test_failure_count != 0)
		fprintf(stderr, "%u out of %llu tests (%u asserts in total) failed in %.1f %s. Rerun under a debugger to trigger the relevant breakpoints.\n", test_failure_count, g_test_times.size(), assertion_failure_count, rd.count, rd.unit);
	else
		fprintf(stderr, "All %llu tests passed in %.1f %s\n", g_test_times.size(), rd.count, rd.unit);
}
