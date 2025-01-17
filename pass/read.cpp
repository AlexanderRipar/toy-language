#include <cstdlib>
#include <atomic>

#include "../infra/common.hpp"
#include "../infra/threading.hpp"
#include "../infra/container.hpp"
#include "pass_data.hpp"

void read::request_read(Globals* data, Range<char8> filepath, u32 filepath_id) noexcept
{
	// TODO: filepath-based caching goes here

	minos::FileHandle filehandle;

	if (!minos::file_create(filepath, minos::Access::Read, minos::ExistsMode::Open, minos::NewMode::Fail, minos::AccessPattern::Sequential, minos::SyncMode::Asynchronous, false, &filehandle))
		panic("Could not open source file %.*s for reading (0x%X)\n", static_cast<u32>(filepath.count()), filepath.begin(), minos::last_error());

	// TODO: FileIdentity-based caching goes here

	minos::FileInfo fileinfo;

	if (!minos::file_get_info(filehandle, &fileinfo))
		panic("Could not get information on source file %.*s (0x%X)\n", static_cast<u32>(filepath.count()), filepath.begin(), minos::last_error());

	if (fileinfo.bytes > UINT32_MAX)
		panic("Could not read source file %.*s as its size %llu exceeds the supported maximum of %u bytes (< 4gb)\n", static_cast<u32>(filepath.count()), filepath.begin(), fileinfo.bytes, UINT32_MAX);

	Read* const read = data->read.unused_reads.pop(data->read.reads);

	if (read == nullptr)
		panic("Could not allocate read metadata due to too many parallel reads\n");

	memset(read, 0, sizeof(*read));

	read->filehandle = filehandle;

	read->bytes = static_cast<u32>(fileinfo.bytes);

	read->filepath_id = filepath_id;

	read->content = static_cast<char8*>(malloc(fileinfo.bytes + 1));

	read->content[fileinfo.bytes] = '\0';

	if (read->content == nullptr)
		panic("Could not allocate buffer of %llu bytes for reading source file %.*s into\n", fileinfo.bytes, static_cast<u32>(filepath.count()), filepath.begin());

	minos::completion_associate_file(data->read.completion_handle, filehandle, 1);

	if (!minos::file_read(filehandle, read->content, read->bytes, &read->overlapped))
		panic("Could not read source file %.*s (0x%X)\n", static_cast<u32>(filepath.count()), filepath.begin(), minos::last_error());

	data->read.pending_read_count.fetch_add(1, std::memory_order_relaxed);
}

[[nodiscard]] bool read::poll_completed_read(Globals* data, Range<char8>* out) noexcept
{
	Read* const read = data->read.completed_reads.pop(data->read.reads);

	if (read == nullptr)
		return false;

	if (!data->read.available_read_count.try_claim())
		panic("Could not acquire token from completed read counter when knowing there is at least one completed read\n");

	if (data->read.pending_read_count.fetch_sub(1, std::memory_order_relaxed) == 0)
		panic("Could not decrement pending read counter when knowing there is at least one pending read\n");

	*out = Range<char8>{ read->content, read->bytes + 1 };

	return true;
}

[[nodiscard]] bool read::await_completed_read(Globals* data, SourceFile* out) noexcept
{
	if (data->read.pending_read_count.load(std::memory_order_relaxed) == 0)
		return false;

	data->read.pending_read_count.fetch_sub(1, std::memory_order_relaxed);

	data->read.available_read_count.await();

	Read* const read = data->read.completed_reads.pop(data->read.reads);

	if (read == nullptr)
		panic("Could not retrieve completed read when expecting there to be at least one\n");

	*out = SourceFile{ read->content, read->bytes + 1, read->filepath_id };

	return true;
}

void read::release_read([[maybe_unused]] Globals* data, SourceFile file) noexcept
	{
		free(file.raw_begin());
	}
