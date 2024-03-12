#ifndef MINOS_HPP_INCLUDE_GUARD
#define MINOS_HPP_INCLUDE_GUARD

#include "common.hpp"
#include "range.hpp"

namespace minos
{
	using thread_proc = u32 (__stdcall *) (void* param);

	struct ThreadHandle
	{
		void* m_rep;
	};

	struct FileHandle
	{
		void* m_rep;
	};

	static constexpr u32 CACHELINE_BYTES = 64;

	void* reserve(u64 bytes) noexcept;

	bool commit(void* ptr, u64 bytes) noexcept;

	bool unreserve(void* ptr) noexcept;

	u32 page_bytes() noexcept;

	void address_wait(void* address, void* undesired, u32 bytes) noexcept;

	bool address_wait_timeout(void* address, void* undesired, u32 bytes, u32 milliseconds) noexcept;

	void address_wake_single(void* address) noexcept;

	void address_wake_all(void* address) noexcept;

	void yield() noexcept;

	__declspec(noreturn) void exit_process(u32 exit_code) noexcept;

	bool thread_create(thread_proc proc, void* param, Range<char8> thread_identifier, ThreadHandle* opt_out = nullptr) noexcept;

	void thread_close(ThreadHandle handle) noexcept;

	u32 logical_processor_count() noexcept;
}

#endif // MINSO_HPP_INCLUDE_GUARD
