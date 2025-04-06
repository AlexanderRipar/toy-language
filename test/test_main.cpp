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

enum class InvocationWaitType : u8
{
	None = 0,
	Event,
	Semaphore,
};

struct InvocationInfo
{
	bool has_ignore_debugbreaks;

	bool has_show_help;

	bool has_exit_with;

	bool has_timeout;

	bool has_check_cwd;

	InvocationWaitType wait_type;

	u32 exit_with_value;

	u32 timeout_milliseconds;

	Range<char8> check_cwd_suffix;

	union
	{
		minos::EventHandle event;

		minos::SemaphoreHandle semaphore;
	};
};

static bool parse_u64(const char8* arg, u64* out) noexcept
{
	u64 value = 0;

	if (*arg == '\0')
		return false;

	do
	{
		if (*arg < '0' || *arg > '9')
			return false;

		value = value * 10 + *arg - '0';

		if (value > UINT32_MAX)
			return false;

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
			if (out->has_ignore_debugbreaks)
			{
				fprintf(stderr, "%s specified more than once\n", argv[arg_index]);

				return false;
			}

			out->has_ignore_debugbreaks = true;

			arg_index += 1;
		}
		else if (strcmp(argv[arg_index], "--exit-with") == 0)
		{
			if (out->has_exit_with)
			{
				fprintf(stderr, "%s specified more than once\n", argv[arg_index]);

				return false;
			}

			if (arg_index + 1 == argc)
			{
				fprintf(stderr, "%s expects an additional argument\n", argv[arg_index]);

				return false;
			}

			u64 n;

			if (!parse_u64(argv[arg_index + 1], &n))
			{
				fprintf(stderr, "%s expects its additional argument to be a base-ten number\n", argv[arg_index]);

				return false;
			}

			if (n > UINT32_MAX)
			{
				fprintf(stderr, "%s expects its argument to be less than 2^32\n", argv[arg_index]);

				return false;
			}

			out->has_exit_with = true;

			out->exit_with_value = static_cast<u32>(n);

			arg_index += 2;
		}
		else if (strcmp(argv[arg_index], "--event-wait") == 0 || strcmp(argv[arg_index], "--semaphore-wait") == 0)
		{
			if (out->wait_type != InvocationWaitType::None)
			{
				fprintf(stderr, "%s specified more than once\n", argv[arg_index]);

				return false;
			}

			if (arg_index + 1 == argc)
			{
				fprintf(stderr, "%s expects an additional argument\n", argv[arg_index]);

				return false;
			}

			u64 n;

			if (!parse_u64(argv[arg_index + 1], &n))
			{
				fprintf(stderr, "%s expects its additional argument to be a base-ten number\n", argv[arg_index]);

				return false;
			}

			const char8 first_char = argv[arg_index][2];

			out->wait_type = first_char == 'e' ? InvocationWaitType::Event : InvocationWaitType::Semaphore;

			out->event.m_rep = reinterpret_cast<void*>(n);

			arg_index += 2;
		}
		else if (strcmp(argv[arg_index], "--timeout") == 0)
		{
			if (out->has_timeout)
			{
				fprintf(stderr, "%s specified more than once\n", argv[arg_index]);

				return false;
			}

			if (arg_index + 1 == argc)
			{
				fprintf(stderr, "%s expects an additional argument\n", argv[arg_index]);

				return false;
			}

			u64 n;

			if (!parse_u64(argv[arg_index + 1], &n))
			{
				fprintf(stderr, "%s expects its additional argument to be a base-ten number\n", argv[arg_index]);

				return false;
			}

			if (n > UINT32_MAX)
			{
				fprintf(stderr, "%s expects its argument to be less than 2^32\n", argv[arg_index]);

				return false;
			}

			out->has_timeout = true;

			out->timeout_milliseconds = static_cast<u32>(n);

			arg_index += 2;
		}
		else if (strcmp(argv[arg_index], "--check-cwd") == 0)
		{
			if (out->has_check_cwd)
			{
				fprintf(stderr, "%s specified more than once\n", argv[arg_index]);

				return false;
			}

			if (arg_index + 1 == argc)
			{
				fprintf(stderr, "%s expects an additional argument\n", argv[arg_index]);

				return false;
			}

			out->has_check_cwd = true;

			out->check_cwd_suffix = range::from_cstring(argv[arg_index + 1]);

			arg_index += 2;
		}
		else if (strcmp(argv[arg_index], "--help") == 0 || strcmp(argv[arg_index], "-h") == 0)
		{
			out->has_show_help = true;

			return true;
		}
		else
		{
			fprintf(stderr, "Unknown argument %s\n", argv[arg_index]);

			return false;
		}
	}

	if (out->has_timeout && out->wait_type == InvocationWaitType::None)
	{
		fprintf(stderr, "`--timeout` must only be specified together with `--<type>-wait`");

		return false;
	}

	return true;
}

static void handle_divergent_invocations(const InvocationInfo* invocation) noexcept
{
	if (invocation->has_show_help)
	{
		fprintf(stdout,
			"This is the `comp` project's test suite. The following arguments are supported:\n"
			"  --help | -h          - Show this message.\n"
			"  --ignore-debugbreaks - Skip any debug break intrinsics triggered due to\n"
			"                         failed tests. This should be enabled when running as\n"
			"                         part of the test suite to avoid dumping core.\n"
			"  --exit-with <N>      - Immediately exit with exit code <N>. This is used for\n"
			"                         testing process spawning.\n"
			"  --event-wait <N>     - Calls `minos::event_wait(<N>)`. If `--timeout <T>` is\n"
			"                         also specified, instead calls\n"
			"                         `minos::event_wait_timeout(<N>, <T>)`.\n"
			"                         If the wait times out, the exit code is 2, otherwise it\n"
			"                         is 0.\n"
			"  --semaphore-wait <N> - Calls `minos::semaphore_wait(<N>)`. If `--timeout <T>`\n"
			"                         is also specified, instead calls\n"
			"                         `minos::semaphore_wait_timeout(<N>, <T>)`.\n"
			"                         If the wait times out, the exit code is 2, otherwise it\n"
			"                         is 0.\n"
			"  --timeout <N>        - Only available in conjunction with one of the\n"
			"                         `--<type>-wait` options. Modifies it to call\n"
			"                         `minos::<type>_wait_timeout` with the specified timeout\n"
			"                         instead of `minos::<type>_wait`.\n"
			"  --check-cwd <STR>    - Check whether the working directory ends with the given\n"
			"                         string. If so, exit with 0, otherwise with 2.\n"
		);

		minos::exit_process(EXIT_FAILURE);
	}

	if (invocation->has_exit_with)
		minos::exit_process(invocation->exit_with_value);

	switch (invocation->wait_type)
	{
	case InvocationWaitType::None:
		break;

	case InvocationWaitType::Event:
		if (invocation->has_timeout)
		{
			if (!minos::event_wait_timeout(invocation->event, static_cast<u32>(invocation->timeout_milliseconds)))
				minos::exit_process(2);
		}
		else
		{
			minos::event_wait(invocation->event);
		}

		minos::exit_process(0);

	case InvocationWaitType::Semaphore:
		if (invocation->has_timeout)
		{
			if (!minos::semaphore_wait_timeout(invocation->semaphore, static_cast<u32>(invocation->timeout_milliseconds)))
				minos::exit_process(2);
		}
		else
		{
			minos::semaphore_wait(invocation->semaphore);
		}

		minos::exit_process(0);

	default:
		ASSERT_UNREACHABLE;
	}

	if (invocation->has_check_cwd)
	{
		char8 cwd[8192];

		const u32 cwd_chars = minos::working_directory(MutRange{ cwd });
		if (cwd_chars == 0 || cwd_chars > array_count(cwd))
			panic("Could not get working directory (0x%X)\n", minos::last_error());

		if (invocation->check_cwd_suffix.count() <= cwd_chars)
		{
			if (memcmp(invocation->check_cwd_suffix.begin(), cwd + cwd_chars - invocation->check_cwd_suffix.count(), invocation->check_cwd_suffix.count()) == 0)
				minos::exit_process(0);

			fprintf(stderr, "cwd was %.*s and did not end with %.*s\n", static_cast<s32>(cwd_chars), cwd, static_cast<s32>(invocation->check_cwd_suffix.count()), invocation->check_cwd_suffix.begin());

			minos::exit_process(2);
		}
	}
}

s32 main(s32 argc, const char8** argv) noexcept
{
	if (minos::mem_reserve(65536) == nullptr)
		panic("mem_reserve is broken :(\n");

	const u64 start = minos::exact_timestamp();

	InvocationInfo invocation;

	if (!parse_args(argc, argv, &invocation))
	{
		fprintf(stderr, "Usage %s [--help | -h] [--ignore-debugbreaks] [--exit-with <N>] [--<type>-wait <N> [--timeout <T>]]\n", argv[0]);

		return EXIT_FAILURE;
	}

	handle_divergent_invocations(&invocation);

	g_ignore_debugbreaks = invocation.has_ignore_debugbreaks;

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
