#include "test_helpers.hpp"

#include <cstdlib>

struct RunThreadData
{
	HANDLE event;

	u32 remaining_thread_count;

	u32 error_count;

	thread_proc actual_proc;

	void* actual_arg;
};

static DWORD run_thread_helper_proc(void* arg) noexcept
{
	RunThreadData* data = static_cast<RunThreadData*>(arg);

	const u32 error_count = data->actual_proc(data->actual_arg);

	InterlockedAdd(reinterpret_cast<LONG*>(&data->error_count), error_count);

	if (InterlockedDecrement(&data->remaining_thread_count) == 0)
	{
		if (!SetEvent(data->event))
		{
			fprintf(stderr, "SetEvent failed: %d\n", GetLastError());

			exit(2);
		}
	}

	return 0;
}

u32 run_on_threads_and_wait(u32 thread_count, thread_proc f, void* arg, u32 timeout) noexcept
{
	RunThreadData data{};

	if ((data.event = CreateEventW(nullptr, false, false, nullptr)) == nullptr)
	{
		fprintf(stderr, "CreateEventW failed: %d\n", GetLastError());

		exit(2);
	}

	data.remaining_thread_count = thread_count;

	data.actual_proc = f;

	data.actual_arg = arg;

	for (u32 i = 0; i != thread_count; ++i)
	{
		if (CreateThread(nullptr, 0, run_thread_helper_proc, &data, 0, nullptr) == nullptr)
		{
			fprintf(stderr, "CreateThread failed: %d\n", GetLastError());

			exit(2);
		}
	}

	WaitForSingleObject(data.event, timeout);

	return data.error_count;
}

void log_error(FILE* f, const char8* fmt, ...) noexcept
{
	va_list args;

	va_start(args, fmt);

	vfprintf(stderr, fmt, args);

	if (f != nullptr)
		vfprintf(f, fmt, args);

	va_end(args);
}
