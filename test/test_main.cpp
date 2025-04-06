#include <cstdio>
#include <vector>
#include <cstdlib>
#include <inttypes.h>

#include "test_helpers.hpp"

std::vector<TestResult> g_test_times;

std::vector<TestResult> g_module_times;

const char8* g_curr_module;

bool g_ignore_debugbreaks;

void minos_tests() noexcept;

void ast2_tests() noexcept;

void type_pool2_tests() noexcept;

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

struct InvocationInfo
{
	bool ignore_debugbreaks;

	bool show_help;

	bool exit_with;

	u32 exit_with_value;
};

static bool parse_uint(const char8* arg, u32* out) noexcept
{
	u32 value = 0;

	if (*arg == '\0')
		return false;

	do
	{
		if (*arg < '0' || *arg > '9')
			return false;

		value = value * 10 + *arg - '0';

		arg += 1;
	}
	while (*arg != '\0');

	*out = value;

	return true;
}

static bool parse_args(s32 argc, const char8** argv, InvocationInfo* out) noexcept
{
	memset(out, 0, sizeof(*out));

	s32 arg_index = 1;

	while (arg_index != argc)
	{
		if (strcmp(argv[arg_index], "--ignore-debugbreaks") == 0)
		{
			if (out->ignore_debugbreaks)
			{
				fprintf(stderr, "--ignore-debugbreaks specified more than once\n");

				return false;
			}

			out->ignore_debugbreaks = true;

			arg_index += 1;
		}
		else if (strcmp(argv[arg_index], "--exit-with") == 0)
		{
			if (out->exit_with)
			{
				fprintf(stderr, "--ignore-debugbreaks specified more than once\n");

				return false;
			}

			if (arg_index + 1 == argc)
			{
				fprintf(stderr, "--exit-with expects an additional argument\n");

				return false;
			}

			if (!parse_uint(argv[arg_index + 1], &out->exit_with_value))
			{
				fprintf(stderr, "--exit-with expects its additional argument to be a base-ten number\n");

				return false;
			}

			out->exit_with = true;

			arg_index += 2;
		}
		else if (strcmp(argv[arg_index], "--help") == 0 || strcmp(argv[arg_index], "-h") == 0)
		{
			out->show_help = true;

			return true;
		}
		else
		{
			fprintf(stderr, "Unknown argument %s\n", argv[arg_index]);

			return false;
		}
	}

	return true;
}

s32 main(s32 argc, const char8** argv) noexcept
{
	if (minos::mem_reserve(65536) == nullptr)
		panic("mem_reserve is broken :(\n");

	const u64 start = minos::exact_timestamp();

	InvocationInfo invocation;

	if (!parse_args(argc, argv, &invocation))
	{
		fprintf(stderr, "Usage %s [--help | -h] [--ignore-debugbreaks] [--exit-with <N>]\n", argv[0]);

		return EXIT_FAILURE;
	}

	if (invocation.show_help)
	{
		fprintf(stderr,
			"This is the `comp` project's test suite. The following arguments are supported:\n"
			"  --help | -h          - Show this message.\n"
			"  --ignore-debugbreaks - Skip any debug break intrinsics triggered due to\n"
			"                         failed tests. This should be enabled when running as\n"
			"                         part of the test suite to avoid dumping core.\n"
			"  --exit-with <N>      - Immediately exit with exit code <N>. This is used for\n"
			"                         testing process spawning.\n");

		return EXIT_SUCCESS;
	}

	if (invocation.exit_with)
		return invocation.exit_with_value;

	minos_tests();

	ast2_tests();

	type_pool2_tests();

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
		fprintf(stderr, "%u out of %" PRIu64 " tests (%u asserts in total) failed in %.1f %s. Rerun under a debugger to trigger the relevant breakpoints.\n", test_failure_count, g_test_times.size(), assertion_failure_count, rd.count, rd.unit);
	else
		fprintf(stderr, "All %" PRIu64 " tests passed in %.1f %s\n", g_test_times.size(), rd.count, rd.unit);
}
