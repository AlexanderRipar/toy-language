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

bool minos::unreserve(void* ptr) noexcept
{
	return VirtualFree(ptr, 0, MEM_RELEASE);
}

u32 minos::page_bytes() noexcept
{
	SYSTEM_INFO sysinfo;

	GetSystemInfo(&sysinfo);

	return sysinfo.dwAllocationGranularity;
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

u32 minos::logical_processor_count() noexcept
{
	SYSTEM_INFO si;

	GetSystemInfo(&si);

	return si.dwNumberOfProcessors;
}
