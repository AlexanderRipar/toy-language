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

Slice<const ErrorLocation> get_error_trace() noexcept
{
	return Slice{ nullptr, nullptr };
}

u32 get_dropped_trace_count() noexcept
{
	return 0;
}

#else // STATUS_DISABLE_TRACE
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

		if (data.count < STATUS_MAX_TRACE_DEPTH)
			data.stk[data.count] = loc;

		// Always increment count, to keep track of number of dropped locations
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

Slice<const ErrorLocation> get_error_trace() noexcept
{
	const impl::ErrorData& data = impl::thread_error_data;

	const u32 actual_count = data.count <= STATUS_MAX_TRACE_DEPTH ? data.count : STATUS_MAX_TRACE_DEPTH;

	return Slice{ data.stk, actual_count };
}


u32 get_dropped_trace_count() noexcept
{
	const u32 total_count = impl::thread_error_data.count;

	if (total_count <= STATUS_MAX_TRACE_DEPTH)
		return 0;

	return total_count - STATUS_MAX_TRACE_DEPTH;
}

#endif // STATUS_DISABLE_TRACE

strview Status::kind_name() const noexcept
{
	static constexpr const strview names[] {
		strview::from_literal("Ok"),
		strview::from_literal("Custom"),
		strview::from_literal("Os"),
	};

	u32 i = static_cast<u32>(kind());

	if (i >= sizeof(names) / sizeof(*names))
		return strview::from_literal("[[Unknown]]");

	return names[i];
}

u32 Status::error_message(char* buf, u32 buf_bytes) const noexcept
{
	static constexpr const char unkown_msg[] = "[[Unknown]]";

	const u32 err = error_code();

	switch (kind())
	{
	case Kind::Ok: {

		constexpr const char ok_msg[] = "[[No error]]";

		if (sizeof(ok_msg) <= buf_bytes)
			memcpy(buf, ok_msg, sizeof(ok_msg));

		return sizeof(ok_msg);
	}

	case Kind::Custom: {

		static constexpr const strview custom_msgs[] {
			strview::from_literal("OutOfMemory"),
			strview::from_literal("BadCommandLine"),
			strview::from_literal("PartialRead"),

			strview::from_literal("[[Unknown]]"),
		};

		if (err > sizeof(custom_msgs) / sizeof(*custom_msgs))
		{
			if (sizeof(unkown_msg) <= buf_bytes)
				memcpy(buf, unkown_msg, sizeof(unkown_msg));

			return sizeof(unkown_msg);
		}

		const u64 msg_bytes = custom_msgs[err].len();

		if (msg_bytes <= buf_bytes)
			memcpy(buf, custom_msgs[err].begin(), msg_bytes);

		return static_cast<u32>(msg_bytes);
	}

	case Kind::Os: {

		char* format_msg_buf;

		if (!FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK, nullptr, static_cast<DWORD>(err), SUBLANG_NEUTRAL, reinterpret_cast<LPSTR>(&format_msg_buf), 0, nullptr))
		{
			if (sizeof(unkown_msg) <= buf_bytes)
				memcpy(buf, unkown_msg, sizeof(unkown_msg));

			return sizeof(unkown_msg);
		}

		const usz msg_bytes = static_cast<usz>(strlen(format_msg_buf) + 1);

		if (msg_bytes <= buf_bytes)
			memcpy(buf, format_msg_buf, msg_bytes);

		return static_cast<u32>(msg_bytes);
	}

	default: {
		if (sizeof(unkown_msg) <= buf_bytes)
			memcpy(buf, unkown_msg, sizeof(unkown_msg));

		return sizeof(unkown_msg);
	}
	}
}
