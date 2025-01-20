#ifndef MINOS_HPP_INCLUDE_GUARD
#define MINOS_HPP_INCLUDE_GUARD

#include "common.hpp"
#include "range.hpp"

namespace minos
{
	using thread_proc = u32 (__stdcall *) (void* param);

	namespace timeout
	{
		static constexpr u32 INFINITE = 0xFFFF'FFFF;
	}

	static constexpr u32 SETTABLE_FILE_FLAGS = 0x0020  // FILE_ATTRIBUTE_ARCHIVE
	                                         | 0x0002  // FILE_ATTRIBUTE_HIDDEN
	                                         | 0x0080  // FILE_ATTRIBUTE_NORMAL
	                                         | 0x2000  // FILE_ATTRIBUTE_NOT_CONTENT_INDEXED
	                                         | 0x1000  // FILE_ATTRIBUTE_OFFLINE
	                                         | 0x0001  // FILE_ATTRIBUTE_READONLY
	                                         | 0x0004  // FILE_ATTRIBUTE_SYSTEM
	                                         | 0x0100; // FILE_ATTRIBUTE_TEMPORARY

	enum class Access
	{
		None    = 0x00,
		Read    = 0x01,
		Write   = 0x02,
		Execute = 0x04,
	};

	static inline Access operator|(Access lhs, Access rhs) noexcept
	{
		return static_cast<Access>(static_cast<u32>(lhs) | static_cast<u32>(rhs));
	}

	static inline Access operator&(Access lhs, Access rhs) noexcept
	{
		return static_cast<Access>(static_cast<u32>(lhs) & static_cast<u32>(rhs));
	}

	enum class ExistsMode
	{
		Fail,
		Open,
		OpenDirectory,
		Truncate,
	};

	enum class NewMode
	{
		Fail,
		Create,
	};

	enum class AccessPattern
	{
		Sequential,
		RandomAccess,
		Unbuffered,
	};

	enum class SyncMode
	{
		Asynchronous,
		Synchronous,
	};

	enum class DirectoryEnumerationStatus
	{
		Ok,
		NoMoreFiles,
		Error,
	};

	enum class FileInfoMask
	{
		None             = 0x00,
		Flags            = 0x01,
		CreationTime     = 0x02,
		LastModifiedTime = 0x04,
		LastAccessTime   = 0x08,
	};

	static inline FileInfoMask operator|(FileInfoMask lhs, FileInfoMask rhs) noexcept
	{
		return static_cast<FileInfoMask>(static_cast<u32>(lhs) | static_cast<u32>(rhs));
	}

	static inline FileInfoMask operator&(FileInfoMask lhs, FileInfoMask rhs) noexcept
	{
		return static_cast<FileInfoMask>(static_cast<u32>(lhs) & static_cast<u32>(rhs));
	}

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

	struct CompletionHandle
	{
		void* m_rep;
	};

	struct ProcessHandle
	{
		void* m_rep;
	};

	struct ShmHandle
	{
		void* m_rep;
	};

	struct SemaphoreHandle
	{
		void* m_rep;
	};

	struct DirectoryEnumerationHandle
	{
		void* m_rep;
	};

	struct GenericHandle
	{
		void* m_rep;

		GenericHandle(ProcessHandle h) noexcept : m_rep{ h.m_rep } {}

		GenericHandle(CompletionHandle h) noexcept : m_rep{ h.m_rep } {}

		GenericHandle(EventHandle h) noexcept : m_rep{ h.m_rep } {}

		GenericHandle(FileHandle h) noexcept : m_rep{ h.m_rep } {}

		GenericHandle(ThreadHandle h) noexcept : m_rep{ h.m_rep } {}

		GenericHandle(ShmHandle h) noexcept : m_rep{ h.m_rep } {}

		GenericHandle(SemaphoreHandle h) noexcept : m_rep{ h.m_rep } {}

		GenericHandle(DirectoryEnumerationHandle h) noexcept : m_rep{ h.m_rep } {}
	};
	
	struct FileIdentity
	{
		u32 volume_serial;

		u64 index;
	};

	struct FileInfo
	{
		FileIdentity identity;

		u64 bytes;

		u64 creation_time;

		u64 last_modified_time;

		u64 last_access_time;

		u32 raw_flags;

		bool is_directory;
	};

	struct Overlapped
	{
		u64 unused_0;

		u64 unused_1;

		u64 offset;

		EventHandle event;
	};

	struct CompletionResult
	{
		u64 key;

		Overlapped* overlapped;

		u32 bytes;
	};

	struct DirectoryEnumerationResult
	{
		u64 creation_time;

		u64 last_access_time;

		u64 last_write_time;

		u64 bytes;

		bool is_directory;

		char8 filename[260 * 3];
	};

	static constexpr u32 CACHELINE_BYTES = 64;

	[[nodiscard]] u32 last_error() noexcept;

	[[nodiscard]] void* mem_reserve(u64 bytes) noexcept;

	[[nodiscard]] bool mem_commit(void* ptr, u64 bytes) noexcept;

	void mem_unreserve(void* ptr) noexcept;

	void mem_decommit(void* ptr, u64 bytes) noexcept;

	[[nodiscard]] u32 page_bytes() noexcept;

	void address_wait(void* address, void* undesired, u32 bytes) noexcept;

	[[nodiscard]] bool address_wait_timeout(void* address, void* undesired, u32 bytes, u32 milliseconds) noexcept;

	void address_wake_single(void* address) noexcept;

	void address_wake_all(void* address) noexcept;

	void thread_yield() noexcept;

	__declspec(noreturn) void exit_process(u32 exit_code) noexcept;

	[[nodiscard]] u32 logical_processor_count() noexcept;

	[[nodiscard]] bool thread_create(thread_proc proc, void* param, Range<char8> thread_identifier, ThreadHandle* opt_out = nullptr) noexcept;

	void thread_close(ThreadHandle handle) noexcept;

	[[nodiscard]] bool file_create(Range<char8> filepath, Access access, ExistsMode exists_mode, NewMode new_mode, AccessPattern pattern, SyncMode syncmode, bool inheritable, FileHandle* out) noexcept;

	void file_close(FileHandle handle) noexcept;

	[[nodiscard]] bool file_read(FileHandle handle, void* buffer, u32 bytes_to_read, Overlapped* overlapped) noexcept;

	[[nodiscard]] bool file_write(FileHandle handle, const void* buffer, u32 bytes_to_write, Overlapped* overlapped) noexcept;

	[[nodiscard]] bool file_get_info(FileHandle handle, FileInfo* out) noexcept;

	[[nodiscard]] bool file_set_info(FileHandle handle, const FileInfo* info, FileInfoMask mask) noexcept;

	[[nodiscard]] bool file_resize(FileHandle handle, u64 new_bytes) noexcept;

	[[nodiscard]] bool overlapped_wait(FileHandle handle, Overlapped* overlapped) noexcept;

	[[nodiscard]] bool event_create(bool inheritable, EventHandle* out) noexcept;

	void event_close(EventHandle handle) noexcept;

	void event_wake(EventHandle handle) noexcept;

	void event_wait(EventHandle handle) noexcept;

	[[nodiscard]] bool event_wait_timeout(EventHandle handle, u32 milliseconds) noexcept;

	[[nodiscard]] bool completion_create(CompletionHandle* out) noexcept;

	void completion_close(CompletionHandle handle) noexcept;

	void completion_associate_file(CompletionHandle completion, FileHandle file, u64 key) noexcept;

	[[nodiscard]] bool completion_wait(CompletionHandle completion, CompletionResult* out) noexcept;

	void sleep(u32 milliseconds) noexcept;

	[[nodiscard]] bool process_create(Range<char8> exe_path, Range<Range<char8>> command_line, Range<char8> working_directory, Range<GenericHandle> inherited_handles, bool inheritable, ProcessHandle* out) noexcept;

	void process_wait(ProcessHandle handle) noexcept;

	[[nodiscard]] bool process_wait_timeout(ProcessHandle handle, u32 milliseconds) noexcept;

	[[nodiscard]] bool process_get_exit_code(ProcessHandle handle, u32* out) noexcept;

	[[nodiscard]] bool shm_create(Access access, u64 bytes, ShmHandle* out) noexcept;

	void shm_close(ShmHandle handle) noexcept;

	[[nodiscard]] void* shm_reserve(ShmHandle handle, Access access, u64 offset, u64 bytes) noexcept;

	[[nodiscard]] bool sempahore_create(u32 initial_count, u32 maximum_count, bool inheritable, SemaphoreHandle* out) noexcept;

	void semaphore_close(SemaphoreHandle handle) noexcept;

	void semaphore_post(SemaphoreHandle handle, u32 count) noexcept;

	void semaphore_wait(SemaphoreHandle handle) noexcept;

	[[nodiscard]] bool semaphore_wait_timeout(SemaphoreHandle handle, u32 milliseconds) noexcept;

	[[nodiscard]] DirectoryEnumerationStatus directory_enumeration_create(Range<char8> directory_path, DirectoryEnumerationHandle* out, DirectoryEnumerationResult* out_first) noexcept;

	[[nodiscard]] DirectoryEnumerationStatus directory_enumeration_next(DirectoryEnumerationHandle handle, DirectoryEnumerationResult* out) noexcept;

	void directory_enumeration_close(DirectoryEnumerationHandle handle) noexcept;

	[[nodiscard]] bool directory_create(Range<char8> path) noexcept;

	[[nodiscard]] bool path_is_directory(Range<char8> path) noexcept;

	[[nodiscard]] u32 path_to_absolute(Range<char8> path, MutRange<char8> out_buf) noexcept;

	[[nodiscard]] bool path_get_info(Range<char8> path, FileInfo* out) noexcept;

	[[nodiscard]] u64 timestamp_utc() noexcept;

	[[nodiscard]] u64 timestamp_local() noexcept;

	[[nodiscard]] s64 timestamp_local_offset() noexcept;

	[[nodiscard]] u64 timestamp_ticks_per_second() noexcept;

	[[nodiscard]] u64 exact_timestamp() noexcept;

	[[nodiscard]] u64 exact_timestamp_ticks_per_second() noexcept;

	Range<Range<char8>> command_line_get() noexcept;
}

#endif // MINSO_HPP_INCLUDE_GUARD
