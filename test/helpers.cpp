#include "helpers.hpp"
#include "../minos.hpp"
#include <cstdlib>
#include <cstdio>
#include <atomic>

struct ThreadData
{
	u32 thread_count;

	std::atomic<u32> remaining_thread_count;

	std::atomic<u32> error_count;

	FILE* out_file;

	thread_proc proc;

	void* arg;

	#pragma warning(push)
	#pragma warning(disable : 4200) // nonstandard extension used: zero-sized array in struct/union
	u32 thread_ids[];
	#pragma warning(pop)
};

static u32 run_thread_helper_proc(void* arg) noexcept
{
	u32 thread_id = *static_cast<u32*>(arg);

	ThreadData* data = reinterpret_cast<ThreadData*>(static_cast<byte*>(arg) - thread_id * sizeof(u32) - sizeof(ThreadData));

	const u32 error_count = data->proc(data->out_file, data->arg, thread_id, data->thread_count);

	data->error_count.fetch_add(error_count);

	if (data->remaining_thread_count.fetch_sub(1) == 1)
		minos::address_wake_single(&data->remaining_thread_count);

	return 0;
}

u32 run_on_threads_and_wait(FILE* out_file, u32 thread_count, thread_proc proc, void* arg, u32 timeout) noexcept
{
	TEST_INIT;

	ThreadData* data = static_cast<ThreadData*>(malloc(sizeof(ThreadData) + thread_count * sizeof(u32)));

	CHECK_NE(data, nullptr, "malloc failed\n");

	data->thread_count = thread_count;

	data->remaining_thread_count.store(thread_count, std::memory_order_relaxed);

	data->error_count.store(0, std::memory_order_relaxed);

	data->out_file = out_file;

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
		minos::address_wait_timeout(&data->remaining_thread_count, &remaining_thread_count, sizeof(remaining_thread_count), timeout);

		remaining_thread_count = data->remaining_thread_count.load();
	}

	u32 error_count = data->error_count.load(std::memory_order_relaxed);

	free(data);

	return error_count;
}

void log(LogLevel level, FILE* f, const char8* fmt, ...) noexcept
{
	switch (level)
	{
	case LogLevel::Info:
		fprintf(f, "[Info] ");
		break;

	case LogLevel::Failure:
		fprintf(f, "[Fail] ");
		break;

	default:
		assert(false);
		fprintf(f, "[????] ");
		break;
	}

	va_list args;

	va_start(args, fmt);

	if (level == LogLevel::Failure)
		vfprintf(stderr, fmt, args);

	if (f != nullptr)
		vfprintf(f, fmt, args);

	va_end(args);
}
