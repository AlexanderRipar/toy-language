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
#define WIN32_LEAN_AND_MEAN

#include <Windows.h>

void* minos::reserve(u64 bytes) noexcept
{
	return VirtualAlloc(nullptr, bytes, MEM_RESERVE, PAGE_READWRITE);
}

bool minos::commit(void* ptr, u64 bytes) noexcept
{
	return VirtualAlloc(ptr, bytes, MEM_COMMIT, PAGE_READWRITE) != nullptr;
}

void minos::unreserve(void* ptr) noexcept
{
	ASSERT_OR_EXIT(VirtualFree(ptr, 0, MEM_RELEASE) != 0);
}

u32 minos::page_bytes() noexcept
{
	SYSTEM_INFO sysinfo;

	GetSystemInfo(&sysinfo);

	return sysinfo.dwPageSize;
}

void minos::address_wait(void* address, void* undesired, u32 bytes) noexcept
{
	ASSERT_OR_EXIT(WaitOnAddress(address, undesired, bytes, INFINITE));
}

bool minos::address_wait_timeout(void* address, void* undesired, u32 bytes, u32 milliseconds) noexcept
{
	if (WaitOnAddress(address, undesired, bytes, milliseconds))
		return true;

	ASSERT_OR_EXIT(GetLastError() == ERROR_TIMEOUT);

	return false;
}

void minos::address_wake_single(void* address) noexcept
{
	WakeByAddressSingle(address);
}

void minos::address_wake_all(void* address) noexcept
{
	WakeByAddressAll(address);
}

void minos::yield() noexcept
{
	YieldProcessor();
}

__declspec(noreturn) void minos::exit_process(u32 exit_code) noexcept
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

	ASSERT_OR_EXIT(thread_name.count() <= MAX_THREAD_NAME_CHARS);

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
	ASSERT_OR_EXIT(CloseHandle(handle.m_rep));
}

bool minos::file_create(Range<char8> filepath, Access access, CreateMode createmode, AccessPattern pattern, SyncMode syncmode, FileHandle* out) noexcept
{
	char16 filepath_utf16[8192];

	const s32 filepath_utf16_chars = MultiByteToWideChar(CP_UTF8, 0, filepath.begin(), static_cast<s32>(filepath.count()), filepath_utf16, static_cast<s32>(array_count(filepath_utf16) - 1));

	if (filepath_utf16_chars == 0)
		return false;

	filepath_utf16[filepath_utf16_chars] = '\0';

	DWORD native_access;

	switch (access)
	{
	case Access::Read:
		native_access = GENERIC_READ;
		break;

	case Access::Write:
		native_access = GENERIC_WRITE;
		break;

	case Access::ReadWrite:
		native_access = GENERIC_READ | GENERIC_WRITE;
		break;

	case Access::Execute:
		native_access = GENERIC_EXECUTE;
		break;

	default:
		ASSERT_UNREACHABLE;
	}

	DWORD native_createmode;

	switch (createmode)
	{
	case CreateMode::Open:
		native_createmode = OPEN_EXISTING;
		break;
	
	case CreateMode::Create:
		native_createmode = CREATE_NEW;
		break;

	case CreateMode::OpenOrCreate:
		native_createmode = OPEN_ALWAYS;
		break;

	case CreateMode::Recreate:
		native_createmode = CREATE_ALWAYS;
		break;

	default:
		ASSERT_UNREACHABLE;
	}

	DWORD native_flags = FILE_ATTRIBUTE_NORMAL;

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

	switch (syncmode)
	{
	case SyncMode::Asynchronous:
		native_flags |= FILE_FLAG_OVERLAPPED;
		break;

	case SyncMode::Synchronous:
		break;

	default:
		ASSERT_UNREACHABLE;
	}

	const HANDLE handle = CreateFileW(filepath_utf16, native_access, FILE_SHARE_READ, nullptr, native_createmode, native_flags, nullptr);

	if (handle == INVALID_HANDLE_VALUE)
		return false;

	out->m_rep = handle;

	return true;
}

void minos::file_close(FileHandle handle) noexcept
{
	ASSERT_OR_EXIT(CloseHandle(handle.m_rep));
}

bool minos::file_read(FileHandle handle, void* buffer, u32 bytes_to_read, Overlapped* overlapped) noexcept
{
	if (ReadFile(handle.m_rep, buffer, bytes_to_read, nullptr, reinterpret_cast<OVERLAPPED*>(overlapped)))
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

	out->file_bytes = info.nFileSizeLow | (static_cast<u64>(info.nFileSizeHigh) << 32);

	return true;
}

bool minos::overlapped_wait(FileHandle handle, Overlapped* overlapped) noexcept
{
	DWORD bytes;

	return GetOverlappedResult(handle.m_rep, reinterpret_cast<OVERLAPPED*>(overlapped), &bytes, true);
}

bool minos::event_create(EventHandle* out) noexcept
{
	HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);

	if (event == nullptr)
		return false;

	out->m_rep = event;

	return true;
}

void minos::event_close(EventHandle handle) noexcept
{
	ASSERT_OR_EXIT(CloseHandle(handle.m_rep));
}

void minos::event_wake(EventHandle handle) noexcept
{
	ASSERT_OR_EXIT(SetEvent(handle.m_rep));
}

void minos::event_wait(EventHandle handle) noexcept
{
	ASSERT_OR_EXIT(WaitForSingleObject(handle.m_rep, INFINITE) == 0);
}

bool minos::completion_create(CompletionHandle* out) noexcept
{
	HANDLE handle = CreateIoCompletionPort(nullptr, nullptr, 0, 1);

	if (handle == nullptr)
		return false;

	out->m_rep = handle;

	return true;
}

void minos::completion_close(CompletionHandle handle) noexcept
{
	ASSERT_OR_EXIT(CloseHandle(handle.m_rep));
}

void minos::completion_associate_file(CompletionHandle completion, FileHandle file, u64 key) noexcept
{
	ASSERT_OR_EXIT(CreateIoCompletionPort(file.m_rep, completion.m_rep, key, 0) != nullptr);
}

bool minos::completion_wait(CompletionHandle completion, CompletionResult* out) noexcept
{
	return GetQueuedCompletionStatus(
			completion.m_rep,
			reinterpret_cast<DWORD*>(&out->bytes),
			reinterpret_cast<ULONG_PTR*>(&out->key),
			reinterpret_cast<OVERLAPPED**>(&out->overlapped), INFINITE
	);
}

void minos::sleep(u32 milliseconds) noexcept
{
	Sleep(milliseconds);
}
