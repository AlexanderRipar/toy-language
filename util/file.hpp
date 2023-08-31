#ifndef UTIL_FILE_INCLUDE_GUARD
#define UTIL_FILE_INCLUDE_GUARD

#include "strview.hpp"
#include "types.hpp"
#include "status.hpp"

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

Status file_open(strview filepath, File::Access access, File::Create existing_mode, File::Create new_mode, File& out) noexcept;

Status file_read(File file, void* buf, u32 buf_bytes, u32* out_bytes_read) noexcept;

Status file_write(File file, const void* buf, u32 buf_bytes) noexcept;

Status file_seek(File file, isz location) noexcept;

Status file_get_size(File file, u64& out_size) noexcept;

Status file_close(File file) noexcept;


#endif // UTIL_FILE_INCLUDE_GUARD
