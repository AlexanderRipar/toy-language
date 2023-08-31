#include "file.hpp"

#include "minimal_os.hpp"
#include <cstdlib>
#include <cassert>
#include <cstdio>

static constexpr const usz STACK_BUF_SIZE = 2048;

static Status interpret_filepath(strview filepath, wchar_t* stack_buf, wchar_t*& out) noexcept
{
	i32 wchar_cnt = MultiByteToWideChar(CP_UTF8, 0, filepath.begin(), static_cast<i32>(filepath.len()), stack_buf, STACK_BUF_SIZE - 1);

	if (wchar_cnt == 0 || wchar_cnt > MAX_PATH)
	{
		wchar_t* wchar_filepath = stack_buf;

		wchar_t* full_filepath = nullptr;

		if (wchar_cnt == 0)
		{
			if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
				return STATUS_FROM_OS(GetLastError());

			wchar_cnt = MultiByteToWideChar(CP_UTF8, 0, filepath.begin(), static_cast<i32>(filepath.len()), nullptr, 0);

			if (wchar_cnt == 0)
				return STATUS_FROM_OS(GetLastError());

			wchar_filepath = static_cast<wchar_t*>(malloc((wchar_cnt + 1) * sizeof(wchar_t)));

			if (wchar_filepath == nullptr)
				return STATUS_FROM_CUSTOM(CustomError::OutOfMemory);

			if (MultiByteToWideChar(CP_UTF8, 0, filepath.begin(), static_cast<i32>(filepath.len()), wchar_filepath, wchar_cnt) == 0)
			{
				free(wchar_filepath);

				return STATUS_FROM_OS(GetLastError());
			}
		}

		wchar_filepath[wchar_cnt] = '\0';

		const DWORD req_full_wchar_cnt = GetFullPathNameW(wchar_filepath, 0, wchar_filepath, nullptr);

		if (req_full_wchar_cnt == 0)
		{
			if (wchar_filepath != stack_buf)
				free(wchar_filepath);

			return STATUS_FROM_OS(GetLastError());
		}

		full_filepath = static_cast<wchar_t*>(malloc((req_full_wchar_cnt + 4) * sizeof(wchar_t)));

		if (full_filepath == nullptr)
		{
			if (wchar_filepath != stack_buf)
				free(wchar_filepath);

			return STATUS_FROM_CUSTOM(CustomError::OutOfMemory);
		}

		full_filepath[0] = '\\';
		full_filepath[1] = '\\';
		full_filepath[2] = '?';
		full_filepath[3] = '\\';

		if (GetFullPathNameW(wchar_filepath + 4, req_full_wchar_cnt, full_filepath + 4, nullptr) == 0)
		{
			if (wchar_filepath != stack_buf)
				free(wchar_filepath);

			return STATUS_FROM_OS(GetLastError());
		}

		if (wchar_filepath != stack_buf)
			free(wchar_filepath);

		out = full_filepath;
	}
	else
	{
		stack_buf[wchar_cnt] = '\0'; 

		out = stack_buf;
	}

	return {};
};

static DWORD interpret_access(File::Access access, File::Create existing_mode) noexcept
{
	const DWORD append_flag = existing_mode == File::Create::Append ? FILE_APPEND_DATA : 0;

	switch (access)
	{
	case File::Access::Read:
		return append_flag | GENERIC_READ;

	case File::Access::ReadWrite:
		return append_flag | GENERIC_READ | GENERIC_WRITE;

	case File::Access::Write:
		return append_flag | GENERIC_WRITE;

	default:
		assert(false);
		return 0;
	}
}

static DWORD interpret_create(File::Create existing_mode, File::Create new_mode) noexcept
{
	if (existing_mode == File::Create::Normal || existing_mode == File::Create::Append)
	{
		if (new_mode == File::Create::Fail)
			return OPEN_EXISTING;		// 0b011
		else
			return OPEN_ALWAYS;		// 0b100
	}
	else if (existing_mode == File::Create::Truncate)
	{
		if (new_mode == File::Create::Fail)
			return TRUNCATE_EXISTING;	// 0b101
		else
			return CREATE_ALWAYS;		// 0b010
	}
	else
	{
		if (new_mode == File::Create::Fail)
			return 0;
		else
			return CREATE_NEW;		// 0b001
	}
}

Status file_open(strview filepath, File::Access access, File::Create existing_mode, File::Create new_mode, File& out) noexcept
{
	wchar_t stack_buf[STACK_BUF_SIZE];

	wchar_t* final_path;
	
	TRY(interpret_filepath(filepath, stack_buf, final_path));

	DWORD final_access = interpret_access(access, existing_mode);

	DWORD final_create = interpret_create(existing_mode, new_mode);

	const HANDLE h = CreateFileW(final_path, final_access, 0, nullptr, final_create, 0, nullptr);

	if (final_path != stack_buf)
		free(final_path);

	if (h == INVALID_HANDLE_VALUE)
		return STATUS_FROM_OS(GetLastError());

	out.m_data = reinterpret_cast<usz>(h);

	return {};
}

Status file_read(File file, void* buf, u32 buf_bytes, u32* out_bytes_read) noexcept
{
	DWORD bytes_read;

	if (ReadFile(reinterpret_cast<HANDLE>(file.m_data), buf, buf_bytes, &bytes_read, nullptr) == 0)
		return STATUS_FROM_OS(GetLastError());

	if (out_bytes_read != nullptr)
		*out_bytes_read = bytes_read;

	return {};
}

Status file_write(File file, const void* buf, u32 buf_bytes) noexcept
{
	DWORD bytes_written;

	if (WriteFile(reinterpret_cast<HANDLE>(file.m_data), buf, buf_bytes, &bytes_written, nullptr) == 0)
		return STATUS_FROM_OS(GetLastError());

	if (bytes_written != buf_bytes)
		return STATUS_FROM_CUSTOM(CustomError::PartialRead);

	return {};
}

Status file_seek(File file, isz location) noexcept
{
	LARGE_INTEGER move_dst;

	move_dst.QuadPart = location;

	if (SetFilePointerEx(reinterpret_cast<HANDLE>(file.m_data), move_dst, nullptr, FILE_BEGIN) == 0)
		return STATUS_FROM_OS(GetLastError());

	return {};
}

Status file_get_size(File file, u64& out_size) noexcept
{
	LARGE_INTEGER file_size;

	if (GetFileSizeEx(reinterpret_cast<HANDLE>(file.m_data), &file_size) == 0)
		return STATUS_FROM_OS(GetLastError());

	out_size = file_size.QuadPart;

	return {};
}

Status file_close(File file) noexcept
{
	if (file.m_data == 0)
		return {};

	if (CloseHandle(reinterpret_cast<HANDLE>(file.m_data)) == 0)
		return STATUS_FROM_OS(GetLastError());

	return {};
}
