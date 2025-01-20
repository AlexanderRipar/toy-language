#include <cstdio>
#include <vector>

#include "test_helpers.hpp"

u32 g_failure_count = 0;

std::vector<TestTime> g_test_times;

std::vector<TestTime> g_module_times;

const char8* g_curr_module;

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

s32 main() noexcept
{
	const u64 start = minos::exact_timestamp();

	ast2_tests();

	const u64 duration = minos::exact_timestamp() - start;

	if (g_failure_count != 0)
		fprintf(stderr, "%u tests failed. Rerun under a debugger to trigger the relevant breakpoints.\n", g_failure_count);

	const TimeDesc rd = readable_time(duration, minos::exact_timestamp_ticks_per_second());

	fprintf(stdout, "Done in %.1f %s\n", rd.count, rd.unit);
}
