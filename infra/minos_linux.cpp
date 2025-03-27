#ifndef _WIN32

#include "minos.hpp"

#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>
#include <cstdlib>

// TODO: Remove
#if COMPILER_CLANG
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Wunused-parameter" // unused parameter
#elif COMPILER_GCC
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wunused-parameter" // unused parameter
#else
	#error("Unsupported compiler")
#endif

u32 minos::last_error() noexcept
{
	return errno;
}

void* minos::mem_reserve(u64 bytes) noexcept
{
	void* const ptr = mmap(nullptr, bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	return ptr == MAP_FAILED ? nullptr : ptr;
}

bool minos::mem_commit(void* ptr, u64 bytes) noexcept
{
	return mprotect(ptr, bytes, PROT_READ | PROT_WRITE) == 0;
}

void minos::mem_unreserve(void* ptr, u64 bytes) noexcept
{
	if (munmap(ptr, bytes) != 0)
		panic("munmap failed (0x%X)\n", last_error());
}

void minos::mem_decommit(void* ptr, u64 bytes) noexcept
{
	if (mprotect(ptr, bytes, PROT_NONE) != 0)
		panic("mprotect(PROT_NONE) failed (0x%X)\n", last_error());
}

u32 minos::page_bytes() noexcept
{
	return static_cast<u32>(getpagesize());
}

void minos::address_wait(void* address, void* undesired, u32 bytes) noexcept
{
	panic("minos::address_wait is not yet implemented\n");
}

bool minos::address_wait_timeout(void* address, void* undesired, u32 bytes, u32 milliseconds) noexcept
{
	panic("minos::address_wait_timeout is not yet implemented\n");
}

void minos::address_wake_single(void* address) noexcept
{
	panic("minos::address_wake_single is not yet implemented\n");
}

void minos::address_wake_all(void* address) noexcept
{
	panic("minos::address_wake_all is not yet implemented\n");
}

void minos::thread_yield() noexcept
{
	panic("minos::thread_yield is not yet implemented\n");
}

NORETURN void minos::exit_process(u32 exit_code) noexcept
{
	exit(exit_code);
}

u32 minos::logical_processor_count() noexcept
{
	panic("minos::logical_processor_count is not yet implemented\n");
}

bool minos::thread_create(thread_proc proc, void* param, Range<char8> thread_identifier, ThreadHandle* opt_out) noexcept
{
	panic("minos::thread_create is not yet implemented\n");
}

void minos::thread_close(ThreadHandle handle) noexcept
{
	panic("minos::thread_close is not yet implemented\n");
}

bool minos::file_create(Range<char8> filepath, Access access, ExistsMode exists_mode, NewMode new_mode, AccessPattern pattern, SyncMode syncmode, bool inheritable, FileHandle* out) noexcept
{
	panic("minos::file_create is not yet implemented\n");
}

void minos::file_close(FileHandle handle) noexcept
{
	panic("minos::file_close is not yet implemented\n");
}

bool minos::file_read(FileHandle handle, void* buffer, u32 bytes_to_read, Overlapped* overlapped) noexcept
{
	panic("minos::file_read is not yet implemented\n");
}

bool minos::file_write(FileHandle handle, const void* buffer, u32 bytes_to_write, Overlapped* overlapped) noexcept
{
	panic("minos::file_write is not yet implemented\n");
}

bool minos::file_get_info(FileHandle handle, FileInfo* out) noexcept
{
	panic("minos::file_get_info is not yet implemented\n");
}

bool minos::file_set_info(FileHandle handle, const FileInfo* info, FileInfoMask mask) noexcept
{
	panic("minos::file_set_info is not yet implemented\n");
}

bool minos::file_resize(FileHandle handle, u64 new_bytes) noexcept
{
	panic("minos::file_resize is not yet implemented\n");
}

bool minos::overlapped_wait(FileHandle handle, Overlapped* overlapped) noexcept
{
	panic("minos::overlapped_wait is not yet implemented\n");
}

bool minos::event_create(bool inheritable, EventHandle* out) noexcept
{
	panic("minos::event_create is not yet implemented\n");
}

void minos::event_close(EventHandle handle) noexcept
{
	panic("minos::event_close is not yet implemented\n");
}

void minos::event_wake(EventHandle handle) noexcept
{
	panic("minos::event_wake is not yet implemented\n");
}

void minos::event_wait(EventHandle handle) noexcept
{
	panic("minos::event_wait is not yet implemented\n");
}

bool minos::event_wait_timeout(EventHandle handle, u32 milliseconds) noexcept
{
	panic("minos::event_wait_timeout is not yet implemented\n");
}

bool minos::completion_create(CompletionHandle* out) noexcept
{
	panic("minos::completion_create is not yet implemented\n");
}

void minos::completion_close(CompletionHandle handle) noexcept
{
	panic("minos::completion_close is not yet implemented\n");
}

void minos::completion_associate_file(CompletionHandle completion, FileHandle file, u64 key) noexcept
{
	panic("minos::completion_associate_file is not yet implemented\n");
}

bool minos::completion_wait(CompletionHandle completion, CompletionResult* out) noexcept
{
	panic("minos::completion_wait is not yet implemented\n");
}

void minos::sleep(u32 milliseconds) noexcept
{
	panic("minos::sleep is not yet implemented\n");
}

bool minos::process_create(Range<char8> exe_path, Range<Range<char8>> command_line, Range<char8> working_directory, Range<GenericHandle> inherited_handles, bool inheritable, ProcessHandle* out) noexcept
{
	panic("minos::process_create is not yet implemented\n");
}

void minos::process_wait(ProcessHandle handle) noexcept
{
	panic("minos::process_wait is not yet implemented\n");
}

bool minos::process_wait_timeout(ProcessHandle handle, u32 milliseconds) noexcept
{
	panic("minos::process_wait_timeout is not yet implemented\n");
}

bool minos::process_get_exit_code(ProcessHandle handle, u32* out) noexcept
{
	panic("minos::process_get_exit_code is not yet implemented\n");
}

bool minos::shm_create(Access access, u64 bytes, ShmHandle* out) noexcept
{
	panic("minos::shm_create is not yet implemented\n");
}

void minos::shm_close(ShmHandle handle) noexcept
{
	panic("minos::shm_close is not yet implemented\n");
}

void* minos::shm_reserve(ShmHandle handle, Access access, u64 offset, u64 bytes) noexcept
{
	panic("minos::shm_reserve is not yet implemented\n");
}

bool minos::sempahore_create(u32 initial_count, u32 maximum_count, bool inheritable, SemaphoreHandle* out) noexcept
{
	panic("minos::sempahore_create is not yet implemented\n");
}

void minos::semaphore_close(SemaphoreHandle handle) noexcept
{
	panic("minos::semaphore_close is not yet implemented\n");
}

void minos::semaphore_post(SemaphoreHandle handle, u32 count) noexcept
{
	panic("minos::semaphore_post is not yet implemented\n");
}

void minos::semaphore_wait(SemaphoreHandle handle) noexcept
{
	panic("minos::semaphore_wait is not yet implemented\n");
}

bool minos::semaphore_wait_timeout(SemaphoreHandle handle, u32 milliseconds) noexcept
{
	panic("minos::semaphore_wait_timeout is not yet implemented\n");
}

minos::DirectoryEnumerationStatus minos::directory_enumeration_create(Range<char8> directory_path, DirectoryEnumerationHandle* out, DirectoryEnumerationResult* out_first) noexcept
{
	panic("minos::directory_enumeration_create is not yet implemented\n");
}

minos::DirectoryEnumerationStatus minos::directory_enumeration_next(DirectoryEnumerationHandle handle, DirectoryEnumerationResult* out) noexcept
{
	panic("minos::directory_enumeration_next is not yet implemented\n");
}

void minos::directory_enumeration_close(DirectoryEnumerationHandle handle) noexcept
{
	panic("minos::directory_enumeration_close is not yet implemented\n");
}

bool minos::directory_create(Range<char8> path) noexcept
{
	panic("minos::directory_create is not yet implemented\n");
}

bool minos::path_is_directory(Range<char8> path) noexcept
{
	panic("minos::path_is_directory is not yet implemented\n");
}

bool minos::path_is_file(Range<char8> path) noexcept
{
	panic("minos::path_is_file is not yet implemented\n");
}

u32 minos::path_to_absolute(Range<char8> path, MutRange<char8> out_buf) noexcept
{
	panic("minos::path_to_absolute is not yet implemented\n");
}

u32 minos::path_to_absolute_relative_to(Range<char8> path, Range<char8> base, MutRange<char8> out_buf) noexcept
{
	panic("minos::path_to_absolute_relative_to is not yet implemented\n");
}

u32 minos::path_to_absolute_directory(Range<char8> path, MutRange<char8> out_buf) noexcept
{
	panic("minos::path_to_absolute_directory is not yet implemented\n");
}

bool minos::path_get_info(Range<char8> path, FileInfo* out) noexcept
{
	panic("minos::path_get_info is not yet implemented\n");
}

u64 minos::timestamp_utc() noexcept
{
	return time(nullptr);
}

u64 minos::timestamp_ticks_per_second() noexcept
{
	return 1;
}

u64 minos::exact_timestamp() noexcept
{
	timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		panic("clock_gettime failed (0x%X)\n", last_error());

	return static_cast<u64>(ts.tv_nsec);
}

u64 minos::exact_timestamp_ticks_per_second() noexcept
{
	timespec ts;

	if (clock_getres(CLOCK_MONOTONIC, &ts) != 0)
		panic("clock_getres failed (0x%X)\n", last_error());

	return static_cast<u64>(1000000000) / static_cast<u64>(ts.tv_nsec);
}

// TODO: Remove
#if COMPILER_CLANG
	#pragma clang diagnostic pop
#elif COMPILER_GCC
#pragma GCC diagnostic pop
#else
	#error("Unknown compiler")
#endif

#endif
