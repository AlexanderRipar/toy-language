#ifndef _WIN32

#include "minos.hpp"

#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sched.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/eventfd.h>
#include <limits.h>
#include <time.h>
#include <cstdlib>
#include <cstring>

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
	cpu_set_t set;

	// TODO: This can fail with EINVAL in case of more than 1024 cpus present
	// on the system
	// See https://linux.die.net/man/2/sched_getaffinity
	if (sched_getaffinity(0, sizeof(set), &set) != 0)
		panic("sched_getaffinity(0) failed (0x%X)\n", last_error());

	return CPU_COUNT(&set);
}

struct TrampolineThreadData
{
	minos::thread_proc proc;

	void* param;
};

static void* trampoline_thread_proc(void* param) noexcept
{
	TrampolineThreadData data = *static_cast<TrampolineThreadData*>(param);

	free(param);

	return reinterpret_cast<void*>(data.proc(data.param));
}

bool minos::thread_create(thread_proc proc, void* param, Range<char8> thread_name, ThreadHandle* opt_out) noexcept
{
	pthread_t thread;

	pthread_attr_t attr;

	if (pthread_attr_init(&attr) != 0)
		return false;

	TrampolineThreadData* const trampoline_data = static_cast<TrampolineThreadData*>(malloc(sizeof(TrampolineThreadData)));
	trampoline_data->proc = proc;
	trampoline_data->param = param;

	const s32 result = pthread_create(&thread, &attr, trampoline_thread_proc, trampoline_data);

	if (pthread_attr_destroy(&attr) != 0)
		panic("pthread_attr_destroy failed (0x%X)\n", last_error());

	if (result != 0)
		return false;

	if (opt_out != nullptr)
		*opt_out = { reinterpret_cast<void*>(thread) };

	if (thread_name.count())
	{
		char8 name_buf[16];

		const u64 name_chars = thread_name.count() < 15 ? thread_name.count() : 15;

		memcpy(name_buf, thread_name.begin(), name_chars);

		name_buf[name_chars] = '\0';

		if (pthread_setname_np(thread, name_buf) != 0)
			panic("pthread_setname_np failed (0x%X)\n", last_error());
	}

	return true;
}

void minos::thread_close([[maybe_unused]] ThreadHandle handle) noexcept
{
	// No-op
}

bool minos::file_create(Range<char8> filepath, Access access, ExistsMode exists_mode, NewMode new_mode, AccessPattern pattern, SyncMode syncmode, bool inheritable, FileHandle* out) noexcept
{
	// TODO: SyncMode::Asynchronous is not yet supported. It just acts as-if it were async
	if (filepath.count() > PATH_MAX)
		return false;

	char8 terminated_filepath[PATH_MAX + 1];

	memcpy(terminated_filepath, filepath.begin(), filepath.count());

	terminated_filepath[filepath.count()] = '\0';

	s32 oflag;

	if ((access & (Access::Read | Access::Write)) == (Access::Read | Access::Write))
		oflag = O_RDWR;
	else if ((access & (Access::Read | Access::Write)) == (Access::Read))
		oflag = O_RDONLY;
	else if ((access & (Access::Read | Access::Write)) == Access::Write)
		oflag = O_WRONLY;
	else
		panic("file_create access flags combination 0x%X not currently supported under linux\n", static_cast<u32>(access));

	ASSERT_OR_IGNORE(new_mode != NewMode::Fail || exists_mode != ExistsMode::Fail);

	if (exists_mode == ExistsMode::Truncate)
		oflag |= O_TRUNC;

	if (pattern == AccessPattern::Unbuffered)
		oflag |= O_DIRECT;

	if (!inheritable)
		oflag |= O_CLOEXEC;

	s32 fd;

	if (new_mode == NewMode::Create)
	{
		oflag |= O_CREAT;
	
		if (exists_mode == ExistsMode::Fail)
			oflag |= O_EXCL;

		fd = open(terminated_filepath, oflag, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
	}
	else
	{
		fd = open(terminated_filepath, oflag);
	}

	if (fd == -1)
		return false;

	*out = { reinterpret_cast<void*>(fd) };

	return true;
}

void minos::file_close(FileHandle handle) noexcept
{
	if (close(static_cast<s32>(reinterpret_cast<u64>(handle.m_rep))) != 0)
		panic("close(filefd) failed (0x%X)\n", last_error());
}

bool minos::file_read(FileHandle handle, void* buffer, u32 bytes_to_read, Overlapped* overlapped) noexcept
{
	return pread(static_cast<s32>(reinterpret_cast<u64>(handle.m_rep)), buffer, bytes_to_read, overlapped->offset) >= 0;
}

bool minos::file_write(FileHandle handle, const void* buffer, u32 bytes_to_write, Overlapped* overlapped) noexcept
{
	return pwrite(static_cast<s32>(reinterpret_cast<u64>(handle.m_rep)), buffer, bytes_to_write, overlapped->offset) >= 0;
}

bool minos::file_get_info(FileHandle handle, FileInfo* out) noexcept
{
	struct stat info;

	if (fstat(static_cast<s32>(reinterpret_cast<u64>(handle.m_rep)), &info) != 0)
		return false;
		
	out->identity.volume_serial = info.st_dev;
	out->identity.index = info.st_ino;
	out->bytes = info.st_size;
	out->creation_time = 0; // This is not supported under *nix
	out->last_modified_time = info.st_mtime; // Use mtime instead of ctime, as metadata changes likely do not matter (?)
	out->last_access_time = info.st_atime;
	out->is_directory = S_ISDIR(info.st_mode);

	return true;
}

bool minos::file_set_info(FileHandle handle, const FileInfo* info, FileInfoMask mask) noexcept
{
	panic("minos::file_set_info is not yet implemented\n");
}

bool minos::file_resize(FileHandle handle, u64 new_bytes) noexcept
{
	return ftruncate(static_cast<s32>(reinterpret_cast<u64>(handle.m_rep)), new_bytes) == 0;
}

bool minos::overlapped_wait(FileHandle handle, Overlapped* overlapped) noexcept
{
	panic("minos::overlapped_wait is not yet implemented\n");
}

bool minos::event_create(bool inheritable, EventHandle* out) noexcept
{
	const s32 fd = eventfd(0, (inheritable ? 0 : EFD_CLOEXEC));

	if (fd == -1)
		return false;

	*out = { reinterpret_cast<void*>(fd) };

	return true;
}

void minos::event_close(EventHandle handle) noexcept
{
	if (close(static_cast<s32>(reinterpret_cast<u64>(handle.m_rep))) != 0)
		panic("close(eventfd) failed (0x%X)\n", last_error());
}

void minos::event_wake(EventHandle handle) noexcept
{
	u64 increment = 1;

	if (write(static_cast<s32>(reinterpret_cast<u64>(handle.m_rep)), &increment, sizeof(increment)) < 0)
		panic("write(eventfd) failed (0x%X)\n", last_error());
}

void minos::event_wait(EventHandle handle) noexcept
{
	u64 value;

	if (read(static_cast<s32>(reinterpret_cast<u64>(handle.m_rep)), &value, sizeof(value)) < 0)
		panic("read(eventfd) failed (0x%X)\n", last_error());
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
	if (usleep(milliseconds * 1000) != 0)
		panic("usleep failed (0x%X)\n", last_error());
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
	if (path.count() > PATH_MAX)
		return false;

	char8 terminated_path[PATH_MAX + 1];

	memcpy(terminated_path, path.begin(), path.count());

	terminated_path[path.count()] = '\0';

	struct stat info;

	if (stat(terminated_path, &info) != 0)
		return false;

	return S_ISDIR(info.st_mode);
}

bool minos::path_is_file(Range<char8> path) noexcept
{
	if (path.count() > PATH_MAX)
		return false;

	char8 terminated_path[PATH_MAX + 1];

	memcpy(terminated_path, path.begin(), path.count());

	terminated_path[path.count()] = '\0';

	struct stat info;

	if (stat(terminated_path, &info) != 0)
		return false;

	return S_ISREG(info.st_mode);
}

static u64 remove_last_path_elem(MutRange<char8> out_buf, u64 out_index) noexcept
{
	if (out_index <= 1)
		return 0;

	ASSERT_OR_IGNORE(out_buf[0] == '/');

	out_index -= 1;

	while (out_buf[out_index] != '/')
		out_index -= 1;

	return out_index;
}

static u32 append_relative_path(Range<char8> path, MutRange<char8> out_buf, u64 out_index) noexcept
{
	bool is_element_start = true;

	for (u64 i = 0; i != path.count(); ++i)
	{
		if (path[i] == '/')
		{
			is_element_start = true;
		}
		else if (is_element_start && path[i] == '.')
		{
			if (i + 1 == path.count() || path[i + 1] == '/')
			{
				i += 1;

				continue;
			}
			else if (i + 2 == path.count() || (i + 2 < path.count() && path[i + 1] == '.' && path[i + 2] == '/'))
			{
				out_index = remove_last_path_elem(out_buf, out_index);

				if (out_index == 0)
					return 0;

				i += 2;

				continue;
			}
		}

		if (is_element_start)
		{
			if (out_index == out_buf.count())
				return 0;

			is_element_start = false;

			out_buf[out_index] = '/';

			out_index += 1;
		}

		if (out_index == out_buf.count())
			return 0;

		out_buf[out_index] = path[i];

		out_index += 1;
	}

	if (out_index > 1 && out_buf[out_index - 1] == '/')
		out_index -= 1;

	return out_index;
}

u32 minos::path_to_absolute(Range<char8> path, MutRange<char8> out_buf) noexcept
{
	if (path.count() > 0 && path[0] == '/')
	{
		if (path.count() < out_buf.count())
			memcpy(out_buf.begin(), path.begin(), path.count());
		
		return path.count();
	}

	if (getcwd(out_buf.begin(), out_buf.count()) == nullptr)
		return 0;

	u64 out_index = 0;

	while (out_buf[out_index] != '\0')
		out_index += 1;
	
	return append_relative_path(path, out_buf, out_index);
}

u32 minos::path_to_absolute_relative_to(Range<char8> path, Range<char8> base, MutRange<char8> out_buf) noexcept
{
	if (path.count() != 0 && path[0] == '/')
	{
		if (path.count() <= out_buf.count())
			memcpy(out_buf.begin(), path.begin(), path.count());

		return path.count();
	}

	const u64 out_index = path_to_absolute(base, out_buf);

	if (out_index == 0 || out_index > out_buf.count())
		return 0;

	return append_relative_path(path, out_buf, out_index);
}

u32 minos::path_to_absolute_directory(Range<char8> path, MutRange<char8> out_buf) noexcept
{
	const u64 out_index = path_to_absolute(path, out_buf);

	if (out_index == 0 || out_index > out_buf.count())
		return 0;

	return remove_last_path_elem(out_buf, out_index);
}

bool minos::path_get_info(Range<char8> path, FileInfo* out) noexcept
{
	if (path.count() > PATH_MAX)
		return false;

	char8 terminated_path[PATH_MAX + 1];

	memcpy(terminated_path, path.begin(), path.count());

	terminated_path[path.count()] = '\0';

	struct stat info;

	if (stat(terminated_path, &info) != 0)
		return false;

	out->identity.volume_serial = info.st_dev;
	out->identity.index = info.st_ino;
	out->bytes = info.st_size;
	out->creation_time = 0; // This is not supported under *nix
	out->last_modified_time = info.st_mtime; // Use mtime instead of ctime, as metadata changes likely do not matter (?)
	out->last_access_time = info.st_atime;
	out->is_directory = S_ISDIR(info.st_mode);

	return true;
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
