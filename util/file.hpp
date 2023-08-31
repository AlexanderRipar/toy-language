#ifndef UTIL_FILE_INCLUDE_GUARD
#define UTIL_FILE_INCLUDE_GUARD

#include "strview.hpp"
#include "types.hpp"

struct File
{
	usz m_data = 0;

	enum class Access
	{
		Read,
		Write,
		ReadWrite,
	};

	enum class Create
	{
		Normal,
		Append,
		Truncate,
		Fail,
	};
};

bool file_open(strview filepath, File::Access access, File::Create existing_mode, File::Create new_mode, File& out) noexcept;

bool file_read(File file, void* buf, u32 buf_bytes, u32* out_bytes_read) noexcept;

bool file_write(File file, const void* buf, u32 buf_bytes) noexcept;

bool file_seek(File file, isz location) noexcept;

bool file_get_size(File file, u64& out_size) noexcept;

bool file_close(File file) noexcept;


#endif // UTIL_FILE_INCLUDE_GUARD
