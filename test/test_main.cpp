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

void ast_tests() noexcept;

void type_pool_tests() noexcept;

void integration_tests() noexcept;

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

enum class InvocationType : u8
{
	None = 0,
	Help,
	ExitWith,
	Event,
	Semaphore,
	CheckCwd,
	Shm,
};

struct InvocationInfo
{
	InvocationType type;

	bool has_timeout;

	u32 timeout_milliseconds;

	#if COMPILER_CLANG
		#pragma clang diagnostic push
		#pragma clang diagnostic ignored "-Wnested-anon-types" // anonymous types declared in an anonymous union are an extension
	#elif COMPILER_GCC
		#pragma GCC diagnostic push
		#pragma GCC diagnostic ignored "-Wpedantic" // ISO C++ prohibits anonymous structs
	#endif
	union
	{
		struct
		{
			u32 exit_code;
		} exit_with;

		struct
		{
			minos::EventHandle handle;
		} event;

		struct
		{
			minos::SemaphoreHandle handle;
		} semaphore;

		struct
		{
			Range<char8> suffix;
		} check_cwd;

		struct
		{
			minos::ShmHandle handle;

			u64 reserve_offset;

			u64 reserve_bytes;

			u64 commit_offset;

			u64 commit_bytes;

			u64 read_offset;

			u64 read_value;

			u64 write_offset;

			u64 write_value;
		} shm;
	};
	#if COMPILER_CLANG
		#pragma clang diagnostic pop
	#elif COMPILER_GCC
		#pragma GCC diagnostic pop
	#endif
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
	const char8* prev_invocation_type = nullptr;

	s32 arg_index = 1;

	while (arg_index != argc)
	{
		if (strcmp(argv[arg_index], "--ignore-debugbreaks") == 0)
		{
			if (g_ignore_debugbreaks)
			{
				fprintf(stderr, "%s specified more than once\n", argv[arg_index]);

				return false;
			}

			g_ignore_debugbreaks = true;

			arg_index += 1;
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
		else if (strcmp(argv[arg_index], "--help") == 0 || strcmp(argv[arg_index], "-h") == 0)
		{
			out->type = InvocationType::Help;

			return true;
		}
		else if (strcmp(argv[arg_index], "--exit-with") == 0)
		{
			if (prev_invocation_type != nullptr)
			{
				fprintf(stderr, "%s: Conflicting invocation type %s already specified\n", argv[arg_index], prev_invocation_type);

				return false;
			}

			prev_invocation_type = argv[arg_index];

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

			out->type = InvocationType::ExitWith;

			out->exit_with.exit_code = static_cast<u32>(n);

			arg_index += 2;
		}
		else if (strcmp(argv[arg_index], "--event-wait") == 0 || strcmp(argv[arg_index], "--semaphore-wait") == 0)
		{
			if (prev_invocation_type != nullptr)
			{
				fprintf(stderr, "%s: Conflicting invocation type %s already specified\n", argv[arg_index], prev_invocation_type);

				return false;
			}

			prev_invocation_type = argv[arg_index];

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

			out->type = first_char == 'e' ? InvocationType::Event : InvocationType::Semaphore;

			out->event.handle.m_rep = reinterpret_cast<void*>(n);

			arg_index += 2;
		}
		else if (strcmp(argv[arg_index], "--check-cwd") == 0)
		{
			if (prev_invocation_type != nullptr)
			{
				fprintf(stderr, "%s: Conflicting invocation type %s already specified\n", argv[arg_index], prev_invocation_type);

				return false;
			}

			prev_invocation_type = argv[arg_index];

			if (arg_index + 1 == argc)
			{
				fprintf(stderr, "%s expects an additional argument\n", argv[arg_index]);

				return false;
			}

			out->type = InvocationType::CheckCwd;

			out->check_cwd.suffix = range::from_cstring(argv[arg_index + 1]);

			arg_index += 2;
		}
		else if (strcmp(argv[arg_index], "--shm") == 0)
		{
			if (prev_invocation_type != nullptr)
			{
				fprintf(stderr, "%s: Conflicting invocation type %s already specified\n", argv[arg_index], prev_invocation_type);

				return false;
			}

			prev_invocation_type = argv[arg_index];

			if (arg_index + 9 >= argc)
			{
				fprintf(stderr, "%s expects nine additional arguments\n", argv[arg_index]);

				return false;
			}

			u64 n;

			if (!parse_u64(argv[arg_index + 1], &n))
			{
				fprintf(stderr, "%s expects its 1st argument (shm handle) to be a base-ten number\n", argv[arg_index]);

				return false;
			}

			out->shm.handle.m_rep = reinterpret_cast<void*>(n);

			if (!parse_u64(argv[arg_index + 2], &out->shm.reserve_offset))
			{
				fprintf(stderr, "%s expects its 2nd argument (reservation offset) to be a base-ten number\n", argv[arg_index]);

				return false;
			}

			if (!parse_u64(argv[arg_index + 3], &out->shm.reserve_bytes))
			{
				fprintf(stderr, "%s expects its 3rd argument (reservation bytes) to be a base-ten number\n", argv[arg_index]);

				return false;
			}

			if (!parse_u64(argv[arg_index + 4], &out->shm.commit_offset))
			{
				fprintf(stderr, "%s expects its 4th argument (commit offset) to be a base-ten number\n", argv[arg_index]);

				return false;
			}

			if (!parse_u64(argv[arg_index + 5], &out->shm.commit_bytes))
			{
				fprintf(stderr, "%s expects its 5th argument (commit bytes) to be a base-ten number\n", argv[arg_index]);

				return false;
			}

			if (!parse_u64(argv[arg_index + 6], &out->shm.read_offset))
			{
				fprintf(stderr, "%s expects its 6th argument (read offset) to be a base-ten number\n", argv[arg_index]);

				return false;
			}

			if (!parse_u64(argv[arg_index + 7], &out->shm.read_value))
			{
				fprintf(stderr, "%s expects its 7th argument (expected read value) to be a base-ten number\n", argv[arg_index]);

				return false;
			}

			if (!parse_u64(argv[arg_index + 8], &out->shm.write_offset))
			{
				fprintf(stderr, "%s expects its 8th argument (write offset) to be a base-ten number\n", argv[arg_index]);

				return false;
			}

			if (!parse_u64(argv[arg_index + 9], &out->shm.write_value))
			{
				fprintf(stderr, "%s expects its 9th argument (write value) to be a base-ten number\n", argv[arg_index]);

				return false;
			}

			out->type = InvocationType::Shm;

			arg_index += 10;
		}
		else
		{
			fprintf(stderr, "Unknown argument %s\n", argv[arg_index]);

			return false;
		}
	}

	if (out->has_timeout && (out->type != InvocationType::Event && out->type != InvocationType::Semaphore))
	{
		fprintf(stderr, "`--timeout` must only be specified together with `--event-wait` or `--semaphore-wait`\n");

		return false;
	}

	return true;
}

static void handle_divergent_invocations(const InvocationInfo* invocation) noexcept
{
	switch (invocation->type)
	{
	case InvocationType::None:
	{
		return;
	}

	case InvocationType::Help:
	{
		fprintf(stdout,
			"This is the `comp` project's test suite. The following arguments are supported:\n"
			"  --help | -h           - Show this message.\n"
			"  --ignore-debugbreaks  - Skip any debug break intrinsics triggered due to\n"
			"                          failed tests. This should be enabled when running as\n"
			"                          part of the test suite to avoid dumping core.\n"
			"  --exit-with <N>       - Immediately exit with exit code <N>. This is used for\n"
			"                          testing process spawning.\n"
			"  --event-wait <H>      - Calls `minos::event_wait(<H>)`. If `--timeout <T>` is\n"
			"                          also specified, instead calls\n"
			"                          `minos::event_wait_timeout(<H>, <T>)`.\n"
			"                          If the wait times out, the exit code is 2, otherwise\n"
			"                          it is 0.\n"
			"  --semaphore-wait <H>  - Calls `minos::semaphore_wait(<H>)`. If `--timeout <T>`\n"
			"                          is also specified, instead calls\n"
			"                          `minos::semaphore_wait_timeout(<H>, <T>)`.\n"
			"                          If the wait times out, the exit code is 2, otherwise\n"
			"                          it is 0.\n"
			"  --timeout <N>         - Only available in conjunction with one of the\n"
			"                          `--<type>-wait` options. Modifies it to call\n"
			"                          `minos::<type>_wait_timeout` with the specified\n"
			"                          timeout instead of `minos::<type>_wait`.\n"
			"  --check-cwd <STR>     - Check whether the working directory ends with the given\n"
			"                          string. If so, exit with 0, otherwise with 2.\n"
			"  --shm-reserve <H>\n"
			"    <RES-OFF> <RES-LEN>\n"
			"    <COM-OFF> <COM-LEN>\n"
			"    <RD-OFF> <RD-EXP>\n"
			"    <WR-OFF> <WR-VAL>   - Calls `minos::shm_reserve(<H>, <RES-OFF>, <RES-LEN>)`.\n"
			"                          If the reservation fails, exits with code 2.\n"
			"                          Otherwise, calls\n"
			"                          `minos::shm_commit(<ADDR> + <COM-OFF>, <COM-LEN>)`. If\n"
			"                          the commit fails, exits with code 3.\n"
			"                          Otherwise, reads from the committed shm at offset\n"
			"                          <RD-OFF>. If the read value is not equal to <RD-EXP>,\n"
			"                          exits with 4.\n"
			"                          If <WR-VAL> is not zero, writes <WR-VAL>\n"
			"                          to the committed shm range at offset <WR-OFF>, and\n"
			"                          subsequently reads back. If the read-back value is\n"
			"                          <WR-VAL>, exits with 0. Otherwise, exits with 5.\n"
		);

		minos::exit_process(1);
	}

	case InvocationType::ExitWith:
	{
		minos::exit_process(invocation->exit_with.exit_code);
	}

	case InvocationType::Event:
	{
		if (invocation->has_timeout)
		{
			if (!minos::event_wait_timeout(invocation->event.handle, static_cast<u32>(invocation->timeout_milliseconds)))
				minos::exit_process(2);
		}
		else
		{
			minos::event_wait(invocation->event.handle);
		}

		minos::exit_process(0);
	}

	case InvocationType::Semaphore:
	{
		if (invocation->has_timeout)
		{
			if (!minos::semaphore_wait_timeout(invocation->semaphore.handle, static_cast<u32>(invocation->timeout_milliseconds)))
				minos::exit_process(2);
		}
		else
		{
			minos::semaphore_wait(invocation->semaphore.handle);
		}

		minos::exit_process(0);
	}

	case InvocationType::CheckCwd:
	{
		char8 cwd[8192];

		const u32 cwd_chars = minos::working_directory(MutRange{ cwd });
		if (cwd_chars == 0 || cwd_chars > array_count(cwd))
			panic("Could not get working directory (0x%X)\n", minos::last_error());

		if (invocation->check_cwd.suffix.count() <= cwd_chars)
		{
			if (memcmp(invocation->check_cwd.suffix.begin(), cwd + cwd_chars - invocation->check_cwd.suffix.count(), invocation->check_cwd.suffix.count()) == 0)
				minos::exit_process(0);
		}

		fprintf(stderr, "cwd was %.*s and did not end with %.*s\n", static_cast<s32>(cwd_chars), cwd, static_cast<s32>(invocation->check_cwd.suffix.count()), invocation->check_cwd.suffix.begin());

		minos::exit_process(2);
	}

	case InvocationType::Shm:
	{
		byte* const mem = static_cast<byte*>(minos::shm_reserve(invocation->shm.handle, invocation->shm.reserve_offset, invocation->shm.reserve_bytes));

		if (mem == nullptr)
			minos::exit_process(2);

		minos::Access access;

		if (invocation->shm.write_value == 0)
			access = minos::Access::Read;
		else
			access = minos::Access::Read | minos::Access::Write;

		if (!minos::shm_commit(mem + invocation->shm.commit_offset, access, invocation->shm.commit_bytes))
			minos::exit_process(3);

		if (mem[invocation->shm.read_offset] != invocation->shm.read_value)
			minos::exit_process(4);

		if (invocation->shm.write_value != 0)
		{
			mem[invocation->shm.write_offset] = static_cast<byte>(invocation->shm.write_value);

			if (mem[invocation->shm.write_offset] != static_cast<byte>(invocation->shm.write_value))
				minos::exit_process(5);
		}

		minos::exit_process(0);
	}

	default:
		ASSERT_UNREACHABLE;
	}
}

s32 main(s32 argc, const char8** argv) noexcept
{
	if (minos::mem_reserve(65536) == nullptr)
		panic("mem_reserve is broken :(\n");

	const u64 start = minos::exact_timestamp();

	InvocationInfo invocation{};

	if (!parse_args(argc, argv, &invocation))
	{
		fprintf(stderr,
			"Usage %s\n"
			"    [ --help | -h ]\n"
			"    [ --ignore-debugbreaks ]\n"
			"    [\n"
			"        --exit-with <CODE> |\n"
			"        --check-cwd <SUFFIX> |\n"
			"        --shm <HANDLE> <RES-OFF> <RES-LEN> <COM-OFF> <COM-LEN> <RD-OFF> <RD-EXP> <WR-OFF> <WR-VAL> |\n"
			"      ( --<event|semaphore>-wait <HANDLE> [--timeout <T>] )\n"
			"    ]\n",
			argv[0]
		);

		return EXIT_FAILURE;
	}

	handle_divergent_invocations(&invocation);

	minos_tests();

	ast_tests();

	type_pool_tests();

	integration_tests();

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
