#include "status.hpp"

#include "minimal_os.hpp"

#ifndef STATUS_MAX_TRACE_DEPTH
#define STATUS_MAX_TRACE_DEPTH 32
#elif STATUS_MAX_TRACE_DEPTH < 0 && !defined(STATUS_DISABLE_TRACE)
#error STATUS_MAX_TRACE DEPTH must be greater than 0
#endif

#define STRINGIFY(x_) #x_

#pragma detect_mismatch("status_max_trace_depth", STRINGIFY(STATUS_MAX_TRACE_DEPTH))

#ifdef STATUS_DISABLE_TRACE
#pragma detect_mismatch("status_disable_trace", "true")
#else
#pragma detect_mismatch("status_disable_trace", "false")

namespace impl
{
	struct ErrorData
	{
		u32 count;

		ErrorLocation stk[STATUS_MAX_TRACE_DEPTH];
	};

	thread_local ErrorData thread_error_data;

	__declspec(noinline) void push_error_location(const ErrorLocation& loc) noexcept
	{
		ErrorData& data = thread_error_data;

		if (data.count >= STATUS_MAX_TRACE_DEPTH)
			return;

		data.stk[data.count] = loc;

		data.count += 1;
	}

	Status register_error(Status status, const ErrorLocation& loc) noexcept
	{
		ErrorData& data = thread_error_data;

		data.stk[0] = loc;
		
		data.count = 1;

		return status;
	}
}

#endif
