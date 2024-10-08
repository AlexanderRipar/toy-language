#ifndef READER_INCLUDE_GUARD
#define READER_INCLUDE_GUARD

#include "common.hpp"
#include "threading.hpp"
#include "structure.hpp"
#include <cstdlib>
#include <atomic>

struct SourceFile
{
private:

	MutAttachmentRange<char8, u32> m_content_and_filepath;

public:

	SourceFile() noexcept : m_content_and_filepath{ nullptr, nullptr } {}

	SourceFile(char8* begin, u32 bytes, u32 filepath_id) noexcept : m_content_and_filepath{ begin, bytes, filepath_id } {}

	Range<char8> content() const noexcept
	{
		return m_content_and_filepath.range();
	}

	char8* raw_begin() noexcept
	{
		return m_content_and_filepath.begin();
	}

	u32 filepath_id() const noexcept
	{
		return m_content_and_filepath.attachment();
	}
};

struct Reader
{
private:

	struct Read
	{
		minos::Overlapped overlapped;

		minos::FileHandle filehandle;

		char8* content;

		u32 bytes;

		u32 next;

		u32 filepath_id;
	};

	thd::IndexStackListHeader<Read, offsetof(Read, next)> m_completed_reads;

	thd::IndexStackListHeader<Read, offsetof(Read, next)> m_unused_reads;

	thd::Semaphore m_available_read_count;

	std::atomic<u32> m_pending_read_count;

	Read m_reads[512];

	minos::CompletionHandle m_completion_handle;

	minos::ThreadHandle m_completion_thread;

	static u32 completion_thread_proc(void* param) noexcept
	{
		Reader* const reader = static_cast<Reader*>(param);

		while (true)
		{
			minos::CompletionResult result;

			if (!minos::completion_wait(reader->m_completion_handle, &result))
				panic("Could not wait for read completion (0x%X)\n", minos::last_error());

			Read* const read = reinterpret_cast<Read*>(result.overlapped);

			reader->m_completed_reads.push(reader->m_reads, static_cast<u32>(read - reader->m_reads));

			reader->m_available_read_count.post();
		}
	}

public:

	Reader() noexcept :
		m_completed_reads{},
		m_unused_reads{ m_reads, static_cast<u32>(array_count(m_reads)) },
		m_available_read_count{ 0 },
		m_pending_read_count{ 0 }
	{
		if (!minos::completion_create(&m_completion_handle))
			panic("Could not create read completion handle (0x%X)\n", minos::last_error());

		if (!minos::thread_create(completion_thread_proc, this, range::from_literal_string("Read Completions"), &m_completion_thread))
			panic("Could not create read completion thread (0x%X)\n", minos::last_error());
	}

	void read(Range<char8> filepath, u32 filepath_id) noexcept
	{
		// TODO: filepath-based caching goes here

		minos::FileHandle filehandle;

		if (!minos::file_create(filepath, minos::Access::Read, minos::CreateMode::Open, minos::AccessPattern::Sequential, minos::SyncMode::Asynchronous, &filehandle))
			panic("Could not open source file %.*s for reading (0x%X)\n", static_cast<u32>(filepath.count()), filepath.begin(), minos::last_error());

		// TODO: FileIdentity-based caching goes here

		minos::FileInfo fileinfo;

		if (!minos::file_get_info(filehandle, &fileinfo))
			panic("Could not get information on source file %.*s (0x%X)\n", static_cast<u32>(filepath.count()), filepath.begin(), minos::last_error());

		if (fileinfo.file_bytes > UINT32_MAX)
			panic("Could not read source file %.*s as its size %llu exceeds the supported maximum of %u bytes (< 4gb)\n", static_cast<u32>(filepath.count()), filepath.begin(), fileinfo.file_bytes, UINT32_MAX);

		Read* const read = m_unused_reads.pop(m_reads);

		if (read == nullptr)
			panic("Could not allocate read metadata due to too many parallel reads\n");

		memset(read, 0, sizeof(*read));

		read->filehandle = filehandle;

		read->bytes = static_cast<u32>(fileinfo.file_bytes);

		read->filepath_id = filepath_id;

		read->content = static_cast<char8*>(malloc(fileinfo.file_bytes + 1));

		read->content[fileinfo.file_bytes] = '\0';

		if (read->content == nullptr)
			panic("Could not allocate buffer of %llu bytes for reading source file %.*s into\n", fileinfo.file_bytes, static_cast<u32>(filepath.count()), filepath.begin());

		minos::completion_associate_file(m_completion_handle, filehandle, 1);

		if (!minos::file_read(filehandle, read->content, read->bytes, &read->overlapped))
			panic("Could not read source file %.*s (0x%X)\n", static_cast<u32>(filepath.count()), filepath.begin(), minos::last_error());

		m_pending_read_count.fetch_add(1, std::memory_order_relaxed);
	}

	[[nodiscard]] bool poll_completed_read(Range<char8>* out) noexcept
	{
		Read* const read = m_completed_reads.pop(m_reads);

		if (read == nullptr)
			return false;

		if (!m_available_read_count.try_claim())
			panic("Could not acquire token from completed read counter when knowing there is at least one completed read\n");

		if (m_pending_read_count.fetch_sub(1, std::memory_order_relaxed) == 0)
			panic("Could not decrement pending read counter when knowing there is at least one pending read\n");

		*out = Range<char8>{ read->content, read->bytes + 1 };

		return true;
	}

	[[nodiscard]] bool await_completed_read(SourceFile* out) noexcept
	{
		if (m_pending_read_count.load(std::memory_order_relaxed) == 0)
			return false;

		m_pending_read_count.fetch_sub(1, std::memory_order_relaxed);

		m_available_read_count.await();

		Read* const read = m_completed_reads.pop(m_reads);

		if (read == nullptr)
			panic("Could not retrieve completed read when expecting there to be at least one\n");

		*out = SourceFile{ read->content, read->bytes + 1, read->filepath_id };

		return true;
	}

	void release_read(SourceFile file) noexcept
	{
		free(file.raw_begin());
	}
};

#endif // READER_INCLUDE_GUARD
