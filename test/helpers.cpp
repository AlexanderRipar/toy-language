#include "helpers.hpp"
#include "../minos.hpp"
#include "../threading.hpp"
#include <cstdlib>
#include <cstdio>
#include <atomic>

struct ThreadData
{
	u32 thread_count;

	std::atomic<u32> remaining_thread_count;

	std::atomic<u32> started_thread_count;

	thread_proc_impl_ proc;

	void* arg;

	#pragma warning(push)
	#pragma warning(disable : 4200) // nonstandard extension used: zero-sized array in struct/union
	u32 thread_ids[];
	#pragma warning(pop)
};

struct
{
	bool silent = false;

	u32 timeout = 0;

	FILE* logfile = nullptr;

	std::atomic<u32> error_count = 0;

	Mutex logfile_mutex;

} g_test_system_data;

static u32 run_thread_helper_proc(void* arg) noexcept
{
	u32 thread_id = *static_cast<u32*>(arg);

	ThreadData* data = reinterpret_cast<ThreadData*>(static_cast<byte*>(arg) - thread_id * sizeof(u32) - sizeof(ThreadData));

	if (data->started_thread_count.fetch_add(1, std::memory_order_relaxed) + 1 == data->thread_count)
	{
		minos::address_wake_all(&data->started_thread_count);
	}
	else
	{
		u32 started_thread_count = data->started_thread_count.load(std::memory_order_relaxed);

		while (started_thread_count != data->thread_count)
		{
			minos::address_wait(&data->started_thread_count, &started_thread_count, sizeof(started_thread_count));

			started_thread_count = data->started_thread_count.load(std::memory_order_relaxed);
		}
	}

	data->proc(data->arg, thread_id, data->thread_count);

	if (data->remaining_thread_count.fetch_sub(1, std::memory_order_relaxed) == 1)
		minos::address_wake_single(&data->remaining_thread_count);

	return 0;
}

static u32 timeout_thread_proc([[maybe_unused]] void*) noexcept
{
	minos::sleep(g_test_system_data.timeout);

	log(LogLevel::Fatal, "Tests timed out after %d ms\n", g_test_system_data.timeout);

	exit(1);
}

void run_on_threads_and_wait_impl_(u32 thread_count, thread_proc_impl_ proc, void* arg) noexcept
{
	ThreadData* data = static_cast<ThreadData*>(malloc(sizeof(ThreadData) + thread_count * sizeof(u32)));

	CHECK_NE(data, nullptr, "malloc failed\n");

	data->thread_count = thread_count;

	data->started_thread_count.store(0, std::memory_order_relaxed);

	data->remaining_thread_count.store(thread_count, std::memory_order_relaxed);

	data->proc = proc;

	data->arg = arg;

	for (u32 i = 0; i != thread_count; ++i)
		data->thread_ids[i] = i;

	for (u32 i = 0; i != thread_count; ++i)
	{
		char8 desc[32];

		s32 desc_chars = sprintf_s(desc, "test worker thread %u", i);

		ASSERT_OR_EXIT(desc_chars > 0);

		ASSERT_OR_EXIT(minos::thread_create(run_thread_helper_proc, data->thread_ids + i, Range{ desc, static_cast<u32>(desc_chars) }));
	}

	u32 remaining_thread_count = data->remaining_thread_count.load();

	while (remaining_thread_count != 0)
	{
		minos::address_wait(&data->remaining_thread_count, &remaining_thread_count, sizeof(remaining_thread_count));

		remaining_thread_count = data->remaining_thread_count.load();
	}

	free(data);
}

void log(LogLevel level, const char8* fmt, ...) noexcept
{
	const char8* prefix;
	
	switch(level)
	{
	case LogLevel::Info:
		prefix = "[info]  ";
		break;

	case LogLevel::Failure:
		prefix = "[FAIL]  ";
		break;

	case LogLevel::Fatal:
		prefix = "[OOPS]  ";
		break;

	default:
		ASSERT_UNREACHABLE;
	}

	g_test_system_data.logfile_mutex.acquire();

	if (g_test_system_data.logfile != nullptr)
	{
		fprintf(g_test_system_data.logfile, prefix);

		va_list args;
		va_start(args, fmt);

		vfprintf(g_test_system_data.logfile, fmt, args);

		va_end(args);
	}

	if (!g_test_system_data.silent)
	{
		fprintf(stdout, prefix);

		va_list args;
		va_start(args, fmt);

		vfprintf(stdout, fmt, args);

		va_end(args);
	}

	g_test_system_data.logfile_mutex.release();
}

void add_error() noexcept
{
	g_test_system_data.error_count.fetch_add(1, std::memory_order_relaxed);
}

bool is_debugbreak_enabled() noexcept
{
	return g_test_system_data.timeout == 0;
}

void test_system_init(u32 argc, const char8** argv) noexcept
{
	for (u32 i = 1; i != argc; ++i)
	{
		if (strcmp(argv[i], "--logfile") == 0)
		{
			i += 1;

			if (i == argc)
			{
				fprintf(stderr, "[Test Init] Expected filename after --logfile\n");

				exit(1);
			}

			if (g_test_system_data.logfile != nullptr)
			{
				fprintf(stderr, "[Test Init] --logfile may only appear once\n");

				exit(1);
			}

			if (fopen_s(&g_test_system_data.logfile, argv[i], "w") != 0)
			{
				fprintf(stderr, "[Test Init] Could not open logfile %s\n", argv[i]);

				exit(1);
			}
		}
		else if (strcmp(argv[i], "--silent") == 0)
		{
			if (g_test_system_data.silent)
			{
				fprintf(stderr, "[Test Init] --silent may only appear once\n");

				exit(1);
			}

			g_test_system_data.silent = true;
		}
		else if (strcmp(argv[i], "--timeout") == 0)
		{
			i += 1;

			if (i == argc)
			{
				fprintf(stderr, "[Test Init] Expected timeout value in milliseconds after --timeout\n");

				exit(1);
			}

			if (g_test_system_data.timeout != 0)
			{
				fprintf(stderr, "[Test Init] --timeout may only appear once\n");

				exit(1);
			}

			u32 timeout = 0;

			for (const char8* c = argv[i]; *c != '\0'; ++c)
			{
				if (*c < '0' || *c > '9')
				{
					fprintf(stderr, "[Test Init] Expected timeout value as a base 10 number after --timeout\n");

					exit(1);
				}

				timeout = timeout * 10 + *c - '0';
			}

			if (timeout < 1000)
			{
				fprintf(stderr, "[Test Init] Increasing timeout from the given %d to the minimum of 1000 ms\n", timeout);

				timeout = 1000;
			}

			g_test_system_data.timeout = timeout;
		}
		else
		{
			fprintf(stderr, "[Test Init] Unknown option '%s' encountered\n", argv[i]);

			exit(1);
		}
	}

	if (g_test_system_data.timeout != 0)
	{
		if (!minos::thread_create(timeout_thread_proc, nullptr, Range{ "Timout watchdog" }))
		{
			fprintf(stderr, "[Test Init] Failed to create timeout watchdog thread\n");

			exit(1);
		}
	}

	g_test_system_data.logfile_mutex.init();
}

u32 test_system_deinit() noexcept
{
	if (g_test_system_data.error_count == 0)
	{
		log(LogLevel::Info, "All tests passed\n");

		return 0;
	}
	else
	{
		log(LogLevel::Info, "%d tests failed.\n", g_test_system_data.error_count.load(std::memory_order_relaxed));

		return 1;
	}
}
