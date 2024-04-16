#ifndef MINOS_HPP_INCLUDE_GUARD
#define MINOS_HPP_INCLUDE_GUARD

#include "common.hpp"
#include "range.hpp"

namespace minos
{
	using thread_proc = u32 (__stdcall *) (void* param);

	enum class Access
	{
		Read,
		Write,
		ReadWrite,
		Execute,
	};

	enum class CreateMode
	{
		Open,
		Create,
		OpenOrCreate,
		Recreate,
	};

	enum class AccessPattern
	{
		Sequential,
		RandomAccess,
		Unbuffered,
	};

	struct ThreadHandle
	{
		void* m_rep;
	};

	struct FileHandle
	{
		void* m_rep;
	};

	struct EventHandle
	{
		void* m_rep;
	};
	
	struct FileIdentity
	{
		u32 volume_serial;

		u64 index;
	};

	struct FileInfo
	{
		FileIdentity identity;

		u64 file_bytes;
	};

	struct Overlapped
	{
		u64 unused_0;

		u64 unused_1;

		u64 offset;

		EventHandle event;
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

	u32 logical_processor_count() noexcept;

	bool thread_create(thread_proc proc, void* param, Range<char8> thread_identifier, ThreadHandle* opt_out = nullptr) noexcept;

	void thread_close(ThreadHandle handle) noexcept;

	bool file_create(Range<char8> filepath, Access access, CreateMode createmode, AccessPattern pattern, FileHandle* out) noexcept;

	void file_close(FileHandle handle) noexcept;

	bool file_read(FileHandle handle, void* buffer, u32 bytes_to_read, Overlapped* overlapped) noexcept;

	bool file_get_info(FileHandle handle, FileInfo* out) noexcept;

	void sleep(u32 milliseconds) noexcept;
}

#endif // MINSO_HPP_INCLUDE_GUARD
