#ifdef _WIN32

#include "minos.hpp"

#define NOGDICAPMASKS
#define NOVIRTUALKEYCODES
#define NOWINMESSAGES
#define NOWINSTYLES
#define NOSYSMETRICS
#define NOMENUS
#define NOICONS
#define NOKEYSTATES
#define NOSYSCOMMANDS
#define NORASTEROPS
#define NOSHOWWINDOW
#define OEMRESOURCE
#define NOATOM
#define NOCLIPBOARD
#define NOCOLOR
#define NOCTLMGR
#define NODRAWTEXT
#define NOGDI
#define NOKERNEL
#define NOUSER
// #define NONLS
#define NOMB
#define NOMEMMGR
#define NOMETAFILE
#define NOMINMAX
#define NOMSG
#define NOOPENFILE
#define NOSCROLL
#define NOSERVICE
#define NOSOUND
#define NOTEXTMETRIC
#define NOWH
#define NOWINOFFSETS
#define NOCOMM
#define NOKANJI
#define NOHELP
#define NOPROFILER
#define NODEFERWINDOWPOS
#define NOMCX
// #define WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <shellapi.h>
#include <atomic>

static constexpr u32 MAX_COMMAND_LINE_CHARS = 32767;

std::atomic<HANDLE> g_job;

static bool map_path(Range<char8> path, MutRange<char16> buffer, u32* out_chars = nullptr, bool remove_last_element = false) noexcept
{
	if (path.count() > INT32_MAX)
		return false;

	char16 relative_path[minos::MAX_PATH_CHARS + 1];

	const u32 relative_chars = MultiByteToWideChar(CP_UTF8, 0, path.begin(), static_cast<s32>(path.count()), relative_path, static_cast<s32>(array_count(relative_path) - 1));

	if (relative_chars == 0)
		return false;

	if (relative_chars >= 4 && relative_path[0] == '\\' && relative_path[1] == '\\' && relative_path[2] == '?' && relative_path[3] == '\\')
	{
		if (relative_chars + 1 > buffer.count())
			return false;

		memcpy(buffer.begin(), relative_path, relative_chars);

		buffer[relative_chars] = '\0';

		if (out_chars != nullptr)
			*out_chars = relative_chars;

		return true;
	}

	relative_path[relative_chars] = '\0';

	u32 absolute_chars = GetFullPathNameW(relative_path, static_cast<u32>(buffer.count() < UINT32_MAX ? buffer.count() : UINT32_MAX), buffer.begin(), nullptr);

	if (absolute_chars == 0)
		return false;

	if (buffer[absolute_chars - 1] == '\\')
	{
		buffer[absolute_chars - 1] = '\0';

		absolute_chars -= 1;
	}

	if (remove_last_element)
	{
		while (true)
		{
			if (absolute_chars <= 1)
				return false;

			if (buffer[absolute_chars - 1] == '\\')
				break;

			absolute_chars -= 1;
		}

		buffer[absolute_chars - 1] = '\0';

		absolute_chars -= 1;
	}

	if (absolute_chars + 1 >= MAX_PATH)
	{
		if (absolute_chars + 4 + 1 > buffer.count())
			return false;

		memmove(buffer.begin() + 4, buffer.begin(), absolute_chars + 1);

		buffer[0] = '\\';
		buffer[1] = '\\';
		buffer[2] = '?';
		buffer[3] = '\\';

		absolute_chars += 4;
	}

	if (out_chars != nullptr)
		*out_chars = absolute_chars;

	return true;
}

static bool is_relative_path(Range<char8> path) noexcept
{
	// See https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file?redirectedfrom=MSDN#fully_qualified_vs._relative_paths

	if (path.count() >= 1 && (path[0] == '\\' || path[0] == '/'))
		return false; // "Absolute" path or UNC name

	// Check for drive "letter", allowing for any sort of format not including '\' or '/' followed by ':\' or ':/'

	for (u64 i = 0; i + 1 < path.count(); ++i)
	{
		if (path[i] == '\\' || path[i] == '/')
			break;

		if (path[i] == ':' && (path[i + 1] == '/' || path[i] == '\\'))
			return false;
	}

	return true;
}

void minos::init() noexcept
{
	// No-op
}

void minos::deinit() noexcept
{
	// No-op
}

u32 minos::last_error() noexcept
{
	return GetLastError();
}

void* minos::mem_reserve(u64 bytes) noexcept
{
	return VirtualAlloc(nullptr, bytes, MEM_RESERVE, PAGE_READWRITE);
}

bool minos::mem_commit(void* ptr, u64 bytes) noexcept
{
	return VirtualAlloc(ptr, bytes, MEM_COMMIT, PAGE_READWRITE) != nullptr;
}

void minos::mem_unreserve(void* ptr, [[maybe_unused]] u64 bytes) noexcept
{
	if (VirtualFree(ptr, 0, MEM_RELEASE) == 0)
		panic("VirtualFree(MEM_RELEASE) failed (0x%X)\n", last_error());
}

void minos::mem_decommit(void* ptr, u64 bytes) noexcept
{
	const u64 page_mask = page_bytes() - 1;

	ASSERT_OR_IGNORE((reinterpret_cast<u64>(ptr) & page_mask) == 0);

	ASSERT_OR_IGNORE((bytes & page_mask) == 0);

	if (VirtualFree(ptr, bytes, MEM_DECOMMIT) == 0)
		panic("VirtualFree(MEM_DECOMMIT) failed (0x%X)\n", last_error());
}

u32 minos::page_bytes() noexcept
{
	SYSTEM_INFO sysinfo;

	GetSystemInfo(&sysinfo);

	return sysinfo.dwPageSize;
}

void minos::address_wait(const void* address, const void* undesired, u32 bytes) noexcept
{
	ASSERT_OR_IGNORE(bytes == 1 || bytes == 2 || bytes == 4);

	if (!WaitOnAddress(const_cast<void*>(address), const_cast<void*>(undesired), bytes, INFINITE))
		panic("WaitOnAddress failed (0x%X)\n", last_error());
}

bool minos::address_wait_timeout(const void* address, const void* undesired, u32 bytes, u32 milliseconds) noexcept
{
	if (WaitOnAddress(const_cast<void*>(address), const_cast<void*>(undesired), bytes, milliseconds))
		return true;

	if (GetLastError() != ERROR_TIMEOUT)
		panic("WaitOnAddress failed (0x%X)\n", last_error());

	return false;
}

void minos::address_wake_single(const void* address) noexcept
{
	WakeByAddressSingle(const_cast<void*>(address));
}

void minos::address_wake_all(const void* address) noexcept
{
	WakeByAddressAll(const_cast<void*>(address));
}

void minos::thread_yield() noexcept
{
	YieldProcessor();
}

NORETURN void minos::exit_process(u32 exit_code) noexcept
{
	ExitProcess(exit_code);
}

u32 minos::logical_processor_count() noexcept
{
	SYSTEM_INFO si;

	GetSystemInfo(&si);

	return si.dwNumberOfProcessors;
}

bool minos::thread_create(thread_proc proc, void* param, Range<char8> thread_name, ThreadHandle* opt_out) noexcept
{
	static constexpr u32 MAX_THREAD_NAME_CHARS = 255;

	if (opt_out != nullptr)
		opt_out->m_rep = nullptr;

	if (thread_name.count() > MAX_THREAD_NAME_CHARS)
		panic("Thread name with length %llu bytes exceeds maximum supported length of %u bytes: %.*s\n", thread_name.count(), MAX_THREAD_NAME_CHARS, static_cast<u32>(thread_name.count()), thread_name.begin());

	ThreadHandle handle = { CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(proc), param, 0, nullptr) };

	if (handle.m_rep == nullptr)
		return false;

	if (thread_name.count() != 0)
	{
		char16 buf[MAX_THREAD_NAME_CHARS * 2 + 1];

		s32 chars = MultiByteToWideChar(CP_UTF8, 0, thread_name.begin(), static_cast<s32>(thread_name.count()), buf, static_cast<s32>(array_count(buf) - 1));

		if (chars == 0)
		{
			thread_close(handle);

			return false;
		}

		buf[chars] = '\0';

		if (FAILED(SetThreadDescription(handle.m_rep, buf)))
		{
			thread_close(handle);

			return false;
		}
	}

	if (opt_out != nullptr)
		*opt_out = handle;
	else
		thread_close(handle);

	return true;
}

void minos::thread_close(ThreadHandle handle) noexcept
{
	if (!CloseHandle(handle.m_rep))
		panic("CloseHandle(ThreadHandle) failed (0x%X)\n", last_error());
}

void minos::thread_wait(ThreadHandle handle, u32* opt_out_result) noexcept
{
	if (WaitForSingleObject(handle.m_rep, INFINITE) != WAIT_OBJECT_0)
		panic("WaitForSingleObject(ThreadHandle) failed (0x%X)\n", last_error());

	if (opt_out_result != nullptr)
	{
		DWORD exit_code;

		if (!GetExitCodeThread(handle.m_rep, &exit_code))
			panic("GetExitCodeThread failed (0x%X)\n", last_error());

		*opt_out_result = exit_code;
	}
}

[[nodiscard]] bool minos::thread_wait_timeout(ThreadHandle handle, u32 milliseconds, u32* opt_out_result) noexcept
{
	const u32 result = WaitForSingleObject(handle.m_rep, milliseconds);

	if (result == WAIT_TIMEOUT)
		return false;

	if (result != WAIT_OBJECT_0)
		panic("WaitForSingleObject(ThreadHandle) failed (0x%X)\n", last_error());

	if (opt_out_result != nullptr)
	{
		DWORD exit_code;

		if (!GetExitCodeThread(handle.m_rep, &exit_code))
			panic("GetExitCodeThread failed (0x%X)\n", last_error());

		*opt_out_result = exit_code;
	}

	return true;
}

bool minos::file_create(Range<char8> filepath, Access access, ExistsMode exists_mode, NewMode new_mode, AccessPattern pattern, const CompletionInitializer* opt_completion, bool inheritable, FileHandle* out) noexcept
{
	char16 path_utf16[MAX_PATH_CHARS + 1];

	if (!map_path(filepath, MutRange{ path_utf16 }))
		return false;

	u32 native_access = 0;

	if ((access & Access::Read) == Access::Read)
		native_access |= GENERIC_READ;

	if ((access & Access::Write) == Access::Write)
		native_access |= GENERIC_WRITE;

	if ((access & Access::Execute) == Access::Execute)
		native_access |= GENERIC_EXECUTE;

	u32 native_flags = FILE_ATTRIBUTE_NORMAL;

	u32 native_createmode;

	ASSERT_OR_IGNORE(exists_mode != ExistsMode::Fail || new_mode != NewMode::Fail);

	switch (exists_mode)
	{
	case ExistsMode::Fail:
		ASSERT_OR_IGNORE(new_mode == NewMode::Create);
		native_createmode = CREATE_NEW;
		break;

	case ExistsMode::Open:
		ASSERT_OR_IGNORE(new_mode == NewMode::Fail || new_mode == NewMode::Create);
		native_createmode = new_mode == NewMode::Fail ? OPEN_EXISTING : OPEN_ALWAYS;
		break;

	case ExistsMode::OpenDirectory:
		ASSERT_OR_IGNORE(new_mode == NewMode::Fail);
		native_createmode = OPEN_EXISTING;
		native_flags |= FILE_FLAG_BACKUP_SEMANTICS;
		break;
	
	case ExistsMode::Truncate:
		ASSERT_OR_IGNORE(new_mode == NewMode::Fail || new_mode == NewMode::Create);
		native_createmode = new_mode == NewMode::Fail ? TRUNCATE_EXISTING : CREATE_ALWAYS;
		break;

	default:
		ASSERT_UNREACHABLE;
	}

	switch (pattern)
	{
	case AccessPattern::Sequential:
		native_flags |= FILE_FLAG_SEQUENTIAL_SCAN;
		break;

	case AccessPattern::RandomAccess:
		native_flags |= FILE_FLAG_RANDOM_ACCESS;
		break;

	case AccessPattern::Unbuffered:
		native_flags |= FILE_FLAG_NO_BUFFERING;
		break;
	
	default:
		ASSERT_UNREACHABLE;
	}

	if (opt_completion != nullptr)
		native_flags |= FILE_FLAG_OVERLAPPED;

	SECURITY_ATTRIBUTES security_attributes{ sizeof(SECURITY_ATTRIBUTES), nullptr, inheritable };

	const HANDLE handle = CreateFileW(path_utf16, native_access, FILE_SHARE_READ, &security_attributes, native_createmode, native_flags, nullptr);

	if (handle == INVALID_HANDLE_VALUE)
		return false;

	if (opt_completion != nullptr)
	{
		if (CreateIoCompletionPort(handle, opt_completion->completion.m_rep, opt_completion->key, 0) == nullptr)
			panic("CreateIoCompletionPort failed to associate file (0x%X)\n", last_error());
	}

	out->m_rep = handle;

	return true;
}

void minos::file_close(FileHandle handle) noexcept
{
	if (!CloseHandle(handle.m_rep))
		panic("CloseHandle(FileHandle) failed (0x%X)\n", last_error());
}

minos::FileHandle minos::standard_file_handle(StdFileName name) noexcept
{
	DWORD native_name;

	switch (name)
	{
	case StdFileName::StdIn:
		native_name = STD_INPUT_HANDLE;
		break;

	case StdFileName::StdOut:
		native_name = STD_OUTPUT_HANDLE;
		break;

	case StdFileName::StdErr:
		native_name = STD_ERROR_HANDLE;
		break;

	default:
		ASSERT_UNREACHABLE;
	}

	const HANDLE handle = GetStdHandle(native_name);

	if (handle == INVALID_HANDLE_VALUE)
		panic("GetStdHandle failed (0x%X)\n", GetLastError());

	return FileHandle{ handle };
}

bool minos::file_read(FileHandle handle, MutRange<byte> buffer, u64 offset, u32* out_bytes_read) noexcept
{
	DWORD bytes_read;

	const u32 bytes_to_read = buffer.count() < UINT32_MAX ? static_cast<u32>(buffer.count()) : UINT32_MAX;

	Overlapped overlapped{};
	overlapped.offset = offset;

	if (ReadFile(handle.m_rep, buffer.begin(), bytes_to_read, &bytes_read, reinterpret_cast<OVERLAPPED*>(&overlapped)))
	{
		*out_bytes_read = bytes_read;

		return true;
	}
	else if (GetLastError() == ERROR_HANDLE_EOF)
	{
		*out_bytes_read = 0;

		return true;
	}
	else
	{
		return false;
	}

}

bool minos::file_read_async(FileHandle handle, MutRange<byte> buffer, Overlapped* overlapped) noexcept
{
	const u32 bytes_to_read = buffer.count() < UINT32_MAX ? static_cast<u32>(buffer.count()) : UINT32_MAX;

	if (ReadFile(handle.m_rep, buffer.begin(), bytes_to_read, nullptr, reinterpret_cast<OVERLAPPED*>(overlapped)))
		return true;

	return GetLastError() == ERROR_IO_PENDING;
}

bool minos::file_write(FileHandle handle, Range<byte> buffer, u64 offset) noexcept
{
	if (buffer.count() > UINT32_MAX)
	{
		SetLastError(ERROR_INVALID_PARAMETER);

		return false;
	}

	Overlapped overlapped{};
	overlapped.offset = offset;

	DWORD bytes_written;

	return WriteFile(handle.m_rep, buffer.begin(), static_cast<u32>(buffer.count()), &bytes_written, reinterpret_cast<OVERLAPPED*>(&overlapped)) && bytes_written == buffer.count();
}

bool minos::file_write_async(FileHandle handle, Range<byte> buffer, Overlapped* overlapped) noexcept
{
	if (buffer.count() > UINT32_MAX)
	{
		SetLastError(ERROR_INVALID_PARAMETER);

		return false;
	}

	if (WriteFile(handle.m_rep, buffer.begin(), static_cast<u32>(buffer.count()), nullptr, reinterpret_cast<OVERLAPPED*>(overlapped)))
		return true;

	return GetLastError() == ERROR_IO_PENDING;
}

bool minos::file_get_info(FileHandle handle, FileInfo* out) noexcept
{
	BY_HANDLE_FILE_INFORMATION info;

	if (!GetFileInformationByHandle(handle.m_rep, &info))
		return false;

	out->identity.volume_serial = info.dwVolumeSerialNumber;
	out->identity.index = info.nFileIndexLow | (static_cast<u64>(info.nFileIndexHigh) << 32);
	out->bytes = info.nFileSizeLow | (static_cast<u64>(info.nFileSizeHigh) << 32);
	out->creation_time = info.ftCreationTime.dwLowDateTime | (static_cast<u64>(info.ftCreationTime.dwHighDateTime) << 32);
	out->last_modified_time = info.ftLastWriteTime.dwLowDateTime | (static_cast<u64>(info.ftLastWriteTime.dwHighDateTime) << 32);
	out->last_access_time = info.ftLastAccessTime.dwLowDateTime | (static_cast<u64>(info.ftLastAccessTime.dwHighDateTime) << 32);
	out->is_directory = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

	return true;
}

bool minos::file_resize(FileHandle handle, u64 new_bytes) noexcept
{
	LARGE_INTEGER destination;
	destination.QuadPart = new_bytes;

	if (!SetFilePointerEx(handle.m_rep, destination, nullptr, FILE_BEGIN))
		return false;

	return SetEndOfFile(handle.m_rep);
}

bool minos::event_create(EventHandle* out) noexcept
{
	SECURITY_ATTRIBUTES security_attributes{ sizeof(SECURITY_ATTRIBUTES), nullptr, true };

	const HANDLE event = CreateEventW(&security_attributes, FALSE, FALSE, nullptr);

	if (event == nullptr)
		return false;

	out->m_rep = event;

	return true;
}

void minos::event_close(EventHandle handle) noexcept
{
	if (!CloseHandle(handle.m_rep))
		panic("CloseHandle(EventHandle) failed (0x%X)\n", last_error());
}

void minos::event_wake(EventHandle handle) noexcept
{
	if (!SetEvent(handle.m_rep))
		panic("SetEvent failed (0x%X)\n", last_error());
}

void minos::event_wait(EventHandle handle) noexcept
{
	const u32 wait_result = WaitForSingleObject(handle.m_rep, INFINITE);

	if (wait_result != 0)
		panic("WaitForSingleObject(EventHandle) failed with 0x%X (0x%X)\n", wait_result, last_error());
}

bool minos::event_wait_timeout(EventHandle handle, u32 milliseconds) noexcept
{
	const u32 wait_result = WaitForSingleObject(handle.m_rep, milliseconds);

	if (wait_result == 0)
		return true;
	else if (wait_result == WAIT_TIMEOUT)
		return false;

	panic("WaitForSingleObject(EventHandle, timeout) failed with 0x%X (0x%X)\n", wait_result, last_error());
}

bool minos::completion_create(CompletionHandle* out) noexcept
{
	HANDLE handle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);

	if (handle == nullptr)
		return false;

	out->m_rep = handle;

	return true;
}

void minos::completion_close(CompletionHandle handle) noexcept
{
	if (!CloseHandle(handle.m_rep))
		panic("CloseHandle(CompletionHandle) failed (0x%X)\n", last_error());
}

bool minos::completion_wait(CompletionHandle completion, CompletionResult* out) noexcept
{
	if (GetQueuedCompletionStatus(
			completion.m_rep,
			reinterpret_cast<DWORD*>(&out->bytes),
			reinterpret_cast<ULONG_PTR*>(&out->key),
			reinterpret_cast<OVERLAPPED**>(&out->overlapped), INFINITE
	))
		return true;

	return GetLastError() == ERROR_HANDLE_EOF;
}

void minos::sleep(u32 milliseconds) noexcept
{
	Sleep(milliseconds);
}

static bool construct_command_line(MutRange<char16> buffer, Range<char16> exe_path, Range<Range<char8>> command_line) noexcept
{
	if (exe_path.count() + 2 > buffer.count())
		return false;

	u32 index = 0;

	buffer[index++] = '"';

	memcpy(buffer.begin() + index, exe_path.begin(), exe_path.count() * sizeof(char16));

	index += static_cast<u32>(exe_path.count());

	buffer[index++] = '"';
	
	for (const Range<char8> argument : command_line)
	{
		if (index + 2 > buffer.count())
			return false;

		buffer[index++] = ' ';
		buffer[index++] = '"';

		const u32 argument_written = MultiByteToWideChar(CP_UTF8, 0, argument.begin(), static_cast<s32>(argument.count()), buffer.begin() + index, static_cast<s32>(buffer.count() - index));

		if (argument_written == 0)
			return false;

		u32 escape_count = 0;

		for (u32 i = 0; i != argument_written; ++i)
		{
			if (buffer[index + i] == '"')
				escape_count += 1;
		}

		if (escape_count != 0)
		{
			u32 offset = escape_count;

			if (index + argument_written + escape_count > buffer.count())
				return false;

			for (u32 i = 0; i != argument_written; ++i)
			{
				const char16 c = buffer[index + argument_written - i - 1];

				buffer[index + argument_written + offset - i - 1] = c;

				if (c == '"')
				{
					offset -= 1;

					buffer[index + argument_written + offset - i - 1] = '\\';
				}
			}
		}

		index += argument_written + escape_count;

		if (index == buffer.count())
			return false;

		buffer[index++] = '"';
	}

	if (index == buffer.count())
		return false;

	buffer[index] = '\0';

	return true;
}

static HANDLE get_global_job_object() noexcept
{
	const HANDLE existing = g_job.load(std::memory_order_relaxed);

	if (existing != nullptr)
		return existing;

	HANDLE created = CreateJobObjectW(nullptr, nullptr);

	if (created == nullptr)
		panic("CreateJobObjectW failed during lazy global job object initialization (0x%X)\n", minos::last_error());

	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit_info{};
	limit_info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

	if (!SetInformationJobObject(created, JobObjectExtendedLimitInformation, &limit_info, sizeof(limit_info)))
		panic("SetInformationJobObject(JOBOBJECT_EXTENDED_LIMIT_INFORMATION) with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE failed during lazy global job object initialization (0x%X)\n", minos::last_error());

	HANDLE exchanged = nullptr;

	if (g_job.compare_exchange_strong(exchanged, created))
		return created;

	if (!CloseHandle(created))
		panic("CloseHandle(JobHandle) failed during race in lazy global job object initialization (0x%X)\n", minos::last_error());

	return exchanged;
}

bool minos::process_create(Range<char8> exe_path, Range<Range<char8>> command_line, Range<char8> working_directory, Range<GenericHandle> inherited_handles, bool inheritable, ProcessHandle* out) noexcept
{
	STARTUPINFOEXW startup_info{};
	startup_info.StartupInfo.cb = sizeof(startup_info);

	u64 proc_thread_attribute_list_bytes = 0;

	if (inherited_handles.count() != 0)
	{
		if (!InitializeProcThreadAttributeList(nullptr, 1, 0, &proc_thread_attribute_list_bytes) && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
			return false;
	}

	const u32 working_directory_16_chars = working_directory.count() == 0 ? 0 : MultiByteToWideChar(CP_UTF8, 0, working_directory.begin(), static_cast<s32>(working_directory.count()), nullptr, 0) + 1;

	if (working_directory.count() != 0 && working_directory_16_chars == 0)
		return false;

	const u64 total_bytes = proc_thread_attribute_list_bytes
	                      + (MAX_COMMAND_LINE_CHARS + 1) * sizeof(char16)
						  + working_directory_16_chars * sizeof(char16);

	void* const command_line_buffer = mem_reserve(total_bytes);

	if (command_line_buffer == nullptr)
		return false;

	if (!mem_commit(command_line_buffer, total_bytes))
	{
		mem_unreserve(command_line_buffer, total_bytes);

		return false;
	}

	char16* const command_line_16 = static_cast<char16*>(command_line_buffer);

	char16* const working_directory_16 = working_directory.count() == 0 ? nullptr : command_line_16 + MAX_COMMAND_LINE_CHARS + 1;

	LPPROC_THREAD_ATTRIBUTE_LIST const attribute_list = inherited_handles.count() == 0 ? nullptr : reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(command_line_16 + MAX_COMMAND_LINE_CHARS + 1 + working_directory_16_chars);

	if (inherited_handles.count() != 0)
	{
		if (!InitializeProcThreadAttributeList(attribute_list, 1, 0, &proc_thread_attribute_list_bytes))
		{
			mem_unreserve(command_line_buffer, total_bytes);
			
			return false;
		}

		if (!UpdateProcThreadAttribute(attribute_list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, const_cast<GenericHandle*>(inherited_handles.begin()), inherited_handles.count() * sizeof(GenericHandle), nullptr, nullptr))
		{
			mem_unreserve(command_line_buffer, total_bytes);

			return false;
		}
	}

	if (working_directory.count() != 0)
	{
		if (static_cast<u32>(MultiByteToWideChar(CP_UTF8, 0, working_directory.begin(), static_cast<s32>(working_directory.count()), working_directory_16, working_directory_16_chars - 1)) != working_directory_16_chars - 1)
		{
			mem_unreserve(command_line_buffer, total_bytes);

			return false;
		}

		working_directory_16[working_directory_16_chars - 1] = '\0';
	}

	char16 exe_path_utf16[MAX_PATH_CHARS + 1];

	u32 exe_path_utf16_chars;

	if (exe_path.count() == 0)
	{
		exe_path_utf16_chars = GetModuleFileNameW(nullptr, exe_path_utf16, static_cast<u32>(array_count(exe_path_utf16)));
	
		if (exe_path_utf16_chars == array_count(exe_path_utf16))
		{
			mem_unreserve(command_line_buffer, total_bytes);

			return false;
		}
	}
	else
	{
		if (!map_path(exe_path, MutRange{ exe_path_utf16 }, &exe_path_utf16_chars))
		{
			mem_unreserve(command_line_buffer, total_bytes);

			return false;
		}
	}

	if (!construct_command_line({ command_line_16, MAX_COMMAND_LINE_CHARS + 1 }, Range{ exe_path_utf16, exe_path_utf16_chars }, command_line))
	{
		mem_unreserve(command_line_buffer, total_bytes);

		return false;
	}

	SECURITY_ATTRIBUTES security_attributes{ sizeof(SECURITY_ATTRIBUTES), nullptr, inheritable };

	PROCESS_INFORMATION process_info;

	const bool success = CreateProcessW(nullptr, command_line_16, &security_attributes, nullptr, inherited_handles.count() != 0, CREATE_SUSPENDED, nullptr, working_directory_16, &startup_info.StartupInfo, &process_info) != 0;

	mem_unreserve(command_line_buffer, total_bytes);

	if (!success)
		return false;

	if (!AssignProcessToJobObject(get_global_job_object(), process_info.hProcess))
	{
		if (!CloseHandle(process_info.hProcess))
			panic("CloseHandle(ProcessHandle) failed (0x%X)\n", last_error());

		if (!CloseHandle(process_info.hThread))
			panic("CloseHandle(ThreadHandle) failed (0x%X)\n", last_error());

		return false;
	}

	if (ResumeThread(process_info.hThread) == static_cast<DWORD>(-1))
	{
		if (!CloseHandle(process_info.hProcess))
			panic("CloseHandle(ProcessHandle) failed (0x%X)\n", last_error());

		if (!CloseHandle(process_info.hThread))
			panic("CloseHandle(ThreadHandle) failed (0x%X)\n", last_error());

		return false;
	}

	if (!CloseHandle(process_info.hThread))
		panic("CloseHandle(ThreadHandle) failed (0x%X)\n", last_error());

	out->m_rep = process_info.hProcess;

	return true;
}

void minos::process_close(ProcessHandle handle) noexcept
{
	if (!CloseHandle(handle.m_rep))
		panic("CloseHandle(ProcessHandle) failed (0x%X)\n", last_error());
}

void minos::process_wait(ProcessHandle handle, u32* opt_out_result) noexcept
{
	const u32 wait_result = WaitForSingleObject(handle.m_rep, INFINITE);

	if (wait_result != WAIT_OBJECT_0)
		panic("WaitForSingleObject(ProcessHandle) failed with 0x%X (0x%X)\n", wait_result, last_error());

	if (opt_out_result != nullptr)
	{
		DWORD exit_code;

		if (!GetExitCodeProcess(handle.m_rep, &exit_code))
			panic("GetExitCodeProcess failed (0x%X)\n", last_error());

		*opt_out_result = exit_code;
	}
}

bool minos::process_wait_timeout(ProcessHandle handle, u32 milliseconds, u32* opt_out_result) noexcept
{
	const u32 wait_result = WaitForSingleObject(handle.m_rep, milliseconds);

	if (wait_result == WAIT_OBJECT_0)
	{
		if (opt_out_result != nullptr)
		{
			DWORD exit_code;

			if (!GetExitCodeProcess(handle.m_rep, &exit_code))
				panic("GetExitCodeProcess failed (0x%X)\n", last_error());

			*opt_out_result = exit_code;
		}

		return true;
	}
	else if (wait_result == WAIT_TIMEOUT)
		return false;

	panic("WaitForSingleObject(ProcessHandle, timeout) failed with 0x%X (0x%X)\n", wait_result, last_error());
}

bool minos::shm_create(Access access, u64 bytes, ShmHandle* out) noexcept
{
	u32 native_access = 0;

	if ((access & Access::Write) == Access::Write)
	{
		if ((access & Access::Execute) == Access::Execute)
			native_access = PAGE_EXECUTE_READWRITE;
		else
			native_access = PAGE_READWRITE;
	}
	else
	{
		if ((access & Access::Execute) == Access::Execute)
			native_access = PAGE_EXECUTE_READ;
		else
			native_access = PAGE_READONLY;
	}

	SECURITY_ATTRIBUTES security_attributes{ sizeof(SECURITY_ATTRIBUTES), nullptr, true };

	const HANDLE handle = CreateFileMappingW(INVALID_HANDLE_VALUE, &security_attributes, native_access | SEC_RESERVE, static_cast<u32>(bytes >> 32), static_cast<u32>(bytes), nullptr);

	if (handle == nullptr)
		return false;

	// This tagging is needed to cleanly implement shm_reserve, since
	// `MapViewOfFile` needs the right access flags, even though we aren't
	// committing anything at that point. Exact repetition of the access rights
	// granted by `shm_create` is required, since if we over-request access
	// compared to that specified to `CreateFileMappingW`, the function fails
	// outright; if we under-request it instead, the later `VirtualAlloc` from
	// `minos::shm_commit` fails if it requests a conflicting access.
	// We store the data in the low two handle bits, since these are reserved
	// for application-level tagging and ignored by OS-calls.
	// This is "documented" here:
	// https://devblogs.microsoft.com/oldnewthing/20080827-00/?p=21073
	// Sadly, the link to the actual docs is broken now, but it used to work
	// and is also documented as a comment in ntdef.h.

	const u64 handle_bits = reinterpret_cast<u64>(handle);

	ASSERT_OR_IGNORE((handle_bits & 3) == 0);

	u64 access_flag_bits = 0;

	if ((access & minos::Access::Write) == minos::Access::Write)
		access_flag_bits |= 1;

	if ((access & minos::Access::Execute) == minos::Access::Execute)
		access_flag_bits |= 2;

	out->m_rep = reinterpret_cast<void*>(handle_bits | access_flag_bits);

	return true;
}

void minos::shm_close(ShmHandle handle) noexcept
{
	if (!CloseHandle(handle.m_rep))
		panic("CloseHandle(ShmHandle) failed (0x%X)\n", last_error());
}

void* minos::shm_reserve(ShmHandle handle, u64 offset, u64 bytes) noexcept
{
	SYSTEM_INFO si;

	GetSystemInfo(&si);

	const u64 aligned_offset = offset & ~static_cast<u64>(si.dwAllocationGranularity - 1);

	const u64 adjusted_bytes = bytes + offset - aligned_offset;

	// Retrieve access rights set in `shm_create` call.

	const u64 handle_tag_bits = reinterpret_cast<u64>(handle.m_rep);

	DWORD native_access = FILE_MAP_READ;

	if ((handle_tag_bits & 1) == 1)
		native_access |= FILE_MAP_WRITE;

	if ((handle_tag_bits & 2) == 2)
		native_access |= FILE_MAP_EXECUTE;

	void* const ptr = MapViewOfFile(handle.m_rep, native_access, static_cast<u32>(aligned_offset >> 32), static_cast<u32>(aligned_offset), adjusted_bytes);

	if (ptr == nullptr)
		return nullptr;

	return static_cast<byte*>(ptr) + offset - aligned_offset;
}

void minos::shm_unreserve(void* address, [[maybe_unused]] u64 bytes) noexcept
{
	SYSTEM_INFO si;

	GetSystemInfo(&si);

	const u64 aligned_address_bits = reinterpret_cast<u64>(address) & ~static_cast<u64>(si.dwAllocationGranularity - 1);

	void* const aligned_address = reinterpret_cast<void*>(aligned_address_bits);

	if (!UnmapViewOfFile(aligned_address))
		panic("UnmapViewOfFile failed (0x%X)\n", last_error());
}

bool minos::shm_commit(void* address, Access access, u64 bytes) noexcept
{
	DWORD native_protect;

	if (access == Access::None)
	{
		native_protect = PAGE_NOACCESS;
	}
	if ((access & Access::Write) == Access::Write)
	{
		if ((access & Access::Execute) == Access::Execute)
			native_protect = PAGE_EXECUTE_READWRITE;
		else
			native_protect = PAGE_READWRITE;
	}
	else
	{
		if ((access & Access::Execute) == Access::Execute)
			native_protect = PAGE_EXECUTE_READ;
		else
			native_protect = PAGE_READONLY;
	}

	return VirtualAlloc(address, bytes, MEM_COMMIT, native_protect);
}

bool minos::sempahore_create(u32 initial_count, SemaphoreHandle* out) noexcept
{
	SECURITY_ATTRIBUTES security_attribute{ sizeof(SECURITY_ATTRIBUTES), nullptr, true };

	const HANDLE handle = CreateSemaphoreW(&security_attribute, initial_count, LONG_MAX, nullptr);

	if (handle == nullptr)
		return false;

	out->m_rep = handle;

	return true;
}

void minos::semaphore_close(SemaphoreHandle handle) noexcept
{
	if (!CloseHandle(handle.m_rep))
		panic("CloseHandle(SemaphoreHandle) failed (0x%X)\n", last_error());
}

void minos::semaphore_post(SemaphoreHandle handle, u32 count) noexcept
{
	if (!ReleaseSemaphore(handle.m_rep, count, nullptr))
		panic("ReleaseSemaphore failed (0x%X)\n", last_error());
}

void minos::semaphore_wait(SemaphoreHandle handle) noexcept
{
	const u32 wait_result = WaitForSingleObject(handle.m_rep, INFINITE);

	if (wait_result != 0)
		panic("WaitForSingleObject(SemaphoreHandle) failed with 0x%X (0x%X)\n", wait_result, last_error());
}

bool minos::semaphore_wait_timeout(SemaphoreHandle handle, u32 milliseconds) noexcept
{
	const u32 wait_result = WaitForSingleObject(handle.m_rep, milliseconds);

	if (wait_result == 0)
		return true;
	else if (wait_result == WAIT_TIMEOUT)
		return false;

	panic("WaitForSingleObject(SemaphoreHandle, timeout) failed with 0x%X (0x%X)\n", wait_result, last_error());
}

static void make_directory_enumeration_result(const WIN32_FIND_DATAW* data, minos::DirectoryEnumerationResult* out) noexcept
{
	out->is_directory = (data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

	out->creation_time = data->ftCreationTime.dwLowDateTime | (static_cast<u64>(data->ftCreationTime.dwHighDateTime) << 32);

	out->last_access_time = data->ftLastAccessTime.dwLowDateTime | (static_cast<u64>(data->ftLastAccessTime.dwHighDateTime) << 32);

	out->last_write_time = data->ftLastWriteTime.dwLowDateTime | (static_cast<u64>(data->ftLastWriteTime.dwHighDateTime) << 32);

	out->bytes = data->nFileSizeLow | (static_cast<u64>(data->nFileSizeHigh) << 32);

	if (WideCharToMultiByte(CP_UTF8, 0, data->cFileName, -1, out->filename, static_cast<s32>(array_count(out->filename)), nullptr, nullptr) == 0)
		panic("Failed utf-16 to utf-8 conversion with guaranteed-to-be sufficient output buffer size (0x%X)\n", minos::last_error());
}

minos::DirectoryEnumerationStatus minos::directory_enumeration_create(Range<char8> directory_path, DirectoryEnumerationHandle* out, DirectoryEnumerationResult* out_first) noexcept
{
	char16 path_utf16[MAX_PATH_CHARS + 1];

	u32 path_chars;

	if (!map_path(directory_path, MutRange{ path_utf16 }, &path_chars))
		return DirectoryEnumerationStatus::Error;

	if (path_chars + 3 > MAX_PATH_CHARS)
		return DirectoryEnumerationStatus::Error;

	path_utf16[path_chars] = '\\';
	path_utf16[path_chars + 1] = '*';
	path_utf16[path_chars + 2] = '\0';

	WIN32_FIND_DATAW first;

	const HANDLE handle = FindFirstFileExW(path_utf16, FindExInfoBasic, &first, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);

	if (handle == INVALID_HANDLE_VALUE)
		return last_error() == ERROR_FILE_NOT_FOUND ? DirectoryEnumerationStatus::NoMoreFiles : DirectoryEnumerationStatus::Error;

	out->m_rep = handle;

	while (first.cFileName[0] == '.' && (first.cFileName[1] == '\0' || (first.cFileName[1] == '.' && first.cFileName[2] == '\0')))
	{
		if (!FindNextFileW(handle, &first))
			return last_error() == ERROR_NO_MORE_FILES ? DirectoryEnumerationStatus::NoMoreFiles : DirectoryEnumerationStatus::Error;
	}

	make_directory_enumeration_result(&first, out_first);

	return DirectoryEnumerationStatus::Ok;
}

minos::DirectoryEnumerationStatus minos::directory_enumeration_next(DirectoryEnumerationHandle handle, DirectoryEnumerationResult* out) noexcept
{
	WIN32_FIND_DATAW data;

	if (!FindNextFileW(handle.m_rep, &data))
		return last_error() == ERROR_NO_MORE_FILES ? DirectoryEnumerationStatus::NoMoreFiles : DirectoryEnumerationStatus::Error;

	make_directory_enumeration_result(&data, out);

	return DirectoryEnumerationStatus::Ok;
}

void minos::directory_enumeration_close(DirectoryEnumerationHandle handle) noexcept
{
	if (handle.m_rep == nullptr)
		return;

	if (!FindClose(handle.m_rep))
		panic("FindClose failed (0x%X)\n", last_error());
}

bool minos::directory_create(Range<char8> path) noexcept
{
	char16 path_utf16[MAX_PATH_CHARS + 1];
	
	if (!map_path(path, MutRange{ path_utf16 }))
		return false;

	const bool success = CreateDirectoryW(path_utf16, nullptr);

	return success;
}

bool minos::path_remove_file(Range<char8> path) noexcept
{
	char16 path_utf16[MAX_PATH_CHARS + 1];

	if (!map_path(path, MutRange{ path_utf16 }))
		return false;

	return DeleteFileW(path_utf16);
}

bool minos::path_remove_directory(Range<char8> path) noexcept
{
	char16 path_utf16[MAX_PATH_CHARS + 1];

	if (!map_path(path, MutRange{ path_utf16 }))
		return false;

	return RemoveDirectoryW(path_utf16);
}

bool minos::path_is_directory(Range<char8> path) noexcept
{
	char16 path_utf16[MAX_PATH_CHARS + 1];

	if (!map_path(path, MutRange{ path_utf16 }))
		return false;

	const u32 attributes = GetFileAttributesW(path_utf16);

	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool minos::path_is_file(Range<char8> path) noexcept
{
	char16 path_utf16[MAX_PATH_CHARS + 1];

	if (!map_path(path, MutRange{ path_utf16 }))
		return false;

	const u32 attributes = GetFileAttributesW(path_utf16);

	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

u32 minos::working_directory(MutRange<char8> out_buf) noexcept
{
	char16 path_utf16[MAX_PATH_CHARS + 1];

	const u32 path_utf16_chars = GetCurrentDirectoryW(static_cast<u32>(array_count(path_utf16)), path_utf16);

	if (path_utf16_chars == 0)
		return 0;

	const s32 chars = WideCharToMultiByte(CP_UTF8, 0, path_utf16, static_cast<s32>(path_utf16_chars), out_buf.begin(), static_cast<s32>(out_buf.count()), nullptr, nullptr);

	if (chars == 0)
	{
		if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
			return WideCharToMultiByte(CP_UTF8, 0, path_utf16, static_cast<s32>(path_utf16_chars), nullptr, 0, nullptr, nullptr);

		return 0;
	}

	return chars;
}

static u32 path_to_absolute_impl(Range<char8> path, MutRange<char8> out_buf, bool remove_last_path_element) noexcept
{
	char16 path_utf16[minos::MAX_PATH_CHARS + 1];

	u32 path_chars;

	if (!map_path(path, MutRange{ path_utf16 }, &path_chars, remove_last_path_element))
		return 0;

	char16* trimmed_path;
	
	if (path_utf16[0] == '\\' && path_utf16[1] == '\\' && path_utf16[2] == '?' && path_utf16[3] == '\\')
	{
		trimmed_path = path_utf16 + 4;

		path_chars -= 4;
	}
	else
	{
		trimmed_path = path_utf16;
	}

	s32 path_chars_utf8 = WideCharToMultiByte(CP_UTF8, 0, path_utf16, path_chars, out_buf.begin(), static_cast<s32>(out_buf.count()), nullptr, nullptr);

	if (path_chars_utf8 == 0)
		path_chars_utf8 = WideCharToMultiByte(CP_UTF8, 0, path_utf16, path_chars, nullptr, 0, nullptr, nullptr);

	return path_chars_utf8;
}

u32 minos::path_to_absolute(Range<char8> path, MutRange<char8> out_buf) noexcept
{
	return path_to_absolute_impl(path, out_buf, false);
}

static bool remove_last_path_elem(Range<char8> buf, u32* inout_chars) noexcept
{
	u32 chars = *inout_chars;

	while (chars > 1)
	{
		chars -= 1;

		if (buf[chars] == '\\')
		{
			*inout_chars = chars;

			return true;
		}
	}

	return false;
}

u32 minos::path_to_absolute_relative_to(Range<char8> path, Range<char8> base, MutRange<char8> out_buf) noexcept
{
	if (!is_relative_path(path))
	{
		const u32 absolute_path_chars = path_to_absolute(path, out_buf);

		return absolute_path_chars > out_buf.count() ? 0 : absolute_path_chars;
	}

	u32 path_chars = path_to_absolute(base, out_buf);

	if (path_chars == 0 || path_chars > out_buf.count())
		return 0;

	bool is_elem_start = true;

	for (u64 i = 0; i != path.count(); ++i)
	{
		if (path[i] == '.' && is_elem_start)
		{
			if (i + 1 == path.count() || path[i + 1] == '\\' || path[i + 1] == '/')
				continue;

			if (path[i + 1] == '.' && (i + 2 == path.count() || path[i + 2] == '\\' || path[i + 2] == '/'))
			{
				if (!remove_last_path_elem(Range{ out_buf.begin(), out_buf.end() }, &path_chars))
					return 0;

				i += 1;

				continue;
			}
		}
		else if (path[i] == '\\' || path[i] == '/')
		{
			is_elem_start = true;

			continue;
		}

		if (is_elem_start)
		{
			if (path_chars == out_buf.count())
				return 0;

			out_buf[path_chars] = '\\';

			path_chars += 1;

			is_elem_start = false;
		}

		if (path_chars == out_buf.count())
			return 0;

		out_buf[path_chars] = path[i];

		path_chars += 1;
	}

	return path_chars;
}

u32 minos::path_to_absolute_directory(Range<char8> path, MutRange<char8> out_buf) noexcept
{
	return path_to_absolute_impl(path, out_buf, true);
}

bool minos::path_get_info(Range<char8> path, FileInfo* out) noexcept
{
	char16 path_utf16[8192];

	const s32 path_utf16_chars = MultiByteToWideChar(CP_UTF8, 0, path.begin(), static_cast<s32>(path.count()), path_utf16, static_cast<s32>(array_count(path_utf16) - 1));

	if (path_utf16_chars == 0)
		return false;

	path_utf16[path_utf16_chars] = '\0';

	WIN32_FILE_ATTRIBUTE_DATA info;

	if (!GetFileAttributesExW(path_utf16, GetFileExInfoStandard, &info))
		return false;

	const bool is_directory = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

	out->identity = {};
	out->bytes = is_directory ? 0 : info.nFileSizeLow | (static_cast<u64>(info.nFileSizeHigh) << 32);
	out->creation_time = info.ftCreationTime.dwLowDateTime | (static_cast<u64>(info.ftCreationTime.dwHighDateTime) << 32);
	out->last_modified_time = info.ftLastWriteTime.dwLowDateTime | (static_cast<u64>(info.ftLastWriteTime.dwHighDateTime) << 32);
	out->last_access_time = info.ftLastAccessTime.dwLowDateTime | (static_cast<u64>(info.ftLastAccessTime.dwHighDateTime) << 32);
	out->is_directory = is_directory;

	return true;
}

u64 minos::timestamp_utc() noexcept
{
	FILETIME filetime;

	GetSystemTimeAsFileTime(&filetime);

	return filetime.dwLowDateTime | (static_cast<u64>(filetime.dwHighDateTime) << 32);
}

u64 minos::timestamp_ticks_per_second() noexcept
{
	return 10'000'000ui64;
}

u64 minos::exact_timestamp() noexcept
{
	LARGE_INTEGER result;

	ASSERT_OR_IGNORE(QueryPerformanceCounter(&result) != 0);

	return result.QuadPart;
}

u64 minos::exact_timestamp_ticks_per_second() noexcept
{
	LARGE_INTEGER result;

	ASSERT_OR_IGNORE(QueryPerformanceFrequency(&result) != 0);

	return result.QuadPart;
}

#endif // defined(_WIN32)
