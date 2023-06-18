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

bool open_file(strview filepath, File::Access access, File::Create existing_mode, File::Create new_mode, File& out) noexcept;

bool read_file(File file, void* buf, u32 buf_bytes, u32* out_bytes_read) noexcept;

bool write_file(File file, const void* buf, u32 buf_bytes) noexcept;

bool get_file_size(File file, u64& out_size) noexcept;

bool close_file(File file) noexcept;

