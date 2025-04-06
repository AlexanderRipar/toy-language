#include <cstdlib>
#include <atomic>

#include "../infra/common.hpp"
#include "../infra/threading.hpp"
#include "../infra/minos.hpp"
#include "pass_data.hpp"

struct Read
{
	minos::Overlapped overlapped;

	minos::FileHandle filehandle;

	char8* content;

	u32 bytes;

	u32 next;

	IdentifierId filepath_id;
};

struct SourceReader
{
	thd::IndexStackListHeader<Read, offsetof(Read, next)> completed_reads;

	thd::IndexStackListHeader<Read, offsetof(Read, next)> unused_reads;

	thd::Semaphore available_read_count;

	std::atomic<u32> pending_read_count;

	Read reads[512];

	minos::CompletionHandle completion_handle;

	minos::ThreadHandle completion_thread;
};



static u32 read_completion_thread_proc(void* param) noexcept
{
	SourceReader* const reader = static_cast<SourceReader*>(param);

	while (true)
	{
		minos::CompletionResult result;

		if (!minos::completion_wait(reader->completion_handle, &result))
			panic("Could not wait for read completion (0x%X)\n", minos::last_error());

		Read* const read = reinterpret_cast<Read*>(result.overlapped);

		reader->completed_reads.push(reader->reads, static_cast<u32>(read - reader->reads));

		reader->available_read_count.post();
	}
}



SourceReader* create_source_reader(AllocPool* pool) noexcept
{
	SourceReader* const reader = static_cast<SourceReader*>(alloc_from_pool(pool, sizeof(SourceReader), alignof(SourceReader)));

	reader->completed_reads.init();

	reader->unused_reads.init(reader->reads, static_cast<u32>(array_count(reader->reads)));

	reader->available_read_count.init(0);

	reader->pending_read_count.store(0, std::memory_order_relaxed);

	if (!minos::completion_create(&reader->completion_handle))
		panic("Could not create read completion handle (0x%X)\n", minos::last_error());

	if (!minos::thread_create(read_completion_thread_proc, reader, range::from_literal_string("Read Completions"), &reader->completion_thread))
		panic("Could not create read completion thread (0x%X)\n", minos::last_error());

	return reader;
}

void request_read(SourceReader* reader, Range<char8> filepath, IdentifierId filepath_id) noexcept
{
	// TODO: filepath-based caching goes here

	minos::FileHandle filehandle;

	minos::CompletionInitializer completion_init;
	completion_init.completion = reader->completion_handle;
	completion_init.key = 1;

	if (!minos::file_create(filepath, minos::Access::Read, minos::ExistsMode::Open, minos::NewMode::Fail, minos::AccessPattern::Sequential, &completion_init, false, &filehandle))
		panic("Could not open source file %.*s for reading (0x%X)\n", static_cast<u32>(filepath.count()), filepath.begin(), minos::last_error());

	// TODO: FileIdentity-based caching goes here

	minos::FileInfo fileinfo;

	if (!minos::file_get_info(filehandle, &fileinfo))
		panic("Could not get information on source file %.*s (0x%X)\n", static_cast<u32>(filepath.count()), filepath.begin(), minos::last_error());

	if (fileinfo.bytes > UINT32_MAX)
		panic("Could not read source file %.*s as its size %llu exceeds the supported maximum of %u bytes (< 4gb)\n", static_cast<u32>(filepath.count()), filepath.begin(), fileinfo.bytes, UINT32_MAX);

	Read* const read = reader->unused_reads.pop(reader->reads);

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

	if (!minos::file_read(filehandle, read->content, read->bytes, &read->overlapped))
		panic("Could not read source file %.*s (0x%X)\n", static_cast<u32>(filepath.count()), filepath.begin(), minos::last_error());

	reader->pending_read_count.fetch_add(1, std::memory_order_relaxed);
}

[[nodiscard]] bool poll_completed_read(SourceReader* reader, SourceFile* out) noexcept
{
	Read* const read = reader->completed_reads.pop(reader->reads);

	if (read == nullptr)
		return false;

	if (!reader->available_read_count.try_claim())
		panic("Could not acquire token from completed read counter when knowing there is at least one completed read\n");

	if (reader->pending_read_count.fetch_sub(1, std::memory_order_relaxed) == 0)
		panic("Could not decrement pending read counter when knowing there is at least one pending read\n");

	*out = SourceFile{ read->content, read->bytes + 1, read->filepath_id };

	return true;
}

[[nodiscard]] bool await_completed_read(SourceReader* reader, SourceFile* out) noexcept
{
	if (reader->pending_read_count.load(std::memory_order_relaxed) == 0)
		return false;

	reader->pending_read_count.fetch_sub(1, std::memory_order_relaxed);

	reader->available_read_count.await();

	Read* const read = reader->completed_reads.pop(reader->reads);

	if (read == nullptr)
		panic("Could not retrieve completed read when expecting there to be at least one\n");

	*out = SourceFile{ read->content, read->bytes + 1, read->filepath_id };

	return true;
}

void release_read([[maybe_unused]] SourceReader* reader, SourceFile file) noexcept
{
	free(file.raw_begin());
}
