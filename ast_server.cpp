#include "ast_server.hpp"

#include "hash.hpp"

bool lex_and_parse(JobServer* job_server, void* param) noexcept;

u32 AstServer::completion_thread_proc(void* param) noexcept
{
	AstServer* const processing = static_cast<AstServer*>(param);

	while (true)
	{
		minos::CompletionResult result;

		if (!minos::completion_wait(processing->m_completion, &result))
			minos::exit_process(42);

		if (result.key == 1)
			return 0;

		ASSERT_OR_IGNORE(result.key == 0);

		ReadData* const read = reinterpret_cast<ReadData*>(reinterpret_cast<byte*>(result.overlapped) - offsetof(ReadData, overlapped));

		ASSERT_OR_IGNORE(read->bytes == result.bytes);

		processing->m_ready_reads.push(processing->m_reads, read - processing->m_reads);
	}
}

MemoryRequirements AstServer::get_memory_requirements(const InitInfo& info) noexcept
{
	const MemoryRequirements paths_reqs = PathMap::get_memory_requirements({ info.thread_count, info.paths.map, info.paths.store });

	const MemoryRequirements files_reqs = FileMap::get_memory_requirements({ info.thread_count, info.files.map, info.files.store });

	const u64 paths_end = paths_reqs.bytes;

	const u64 files_end = next_multiple(paths_end, static_cast<u64>(files_reqs.alignment)) + files_reqs.bytes;

	const u64 reads_end = next_multiple(files_end, alignof(ReadData)) + info.concurrent_read_capacity * sizeof(ReadData);

	return { reads_end, maximum(maximum(paths_reqs.alignment, files_reqs.alignment), static_cast<u32>(alignof(ReadData))) };
}

bool AstServer::init(const InitInfo& info, byte* memory, JobServer* job_server) noexcept
{
	const MemoryRequirements paths_reqs = PathMap::get_memory_requirements({ info.thread_count, info.paths.map, info.paths.store });

	const MemoryRequirements files_reqs = FileMap::get_memory_requirements({ info.thread_count, info.files.map, info.files.store });

	const u64 files_off = next_multiple(paths_reqs.bytes, static_cast<u64>(files_reqs.alignment));

	const u64 reads_off = next_multiple(files_off + files_reqs.bytes, alignof(ReadData));

	if (!m_path_map.init({ info.thread_count, info.paths.map, info.paths.store }, memory))
		return false;

	if (!m_file_map.init({ info.thread_count, info.files.map, info.files.store }, memory + files_off))
		return false;

	m_job_server = job_server;

	m_read_capacity = info.concurrent_read_capacity;

	m_reads = reinterpret_cast<ReadData*>(memory + reads_off);

	m_read_freelist.init(m_reads, m_read_capacity);

	if (!minos::completion_create(&m_completion))
		return false;

	if (!minos::thread_create(completion_thread_proc, this, range_from_literal_string("I/O completion worker"), &m_completion_thread))
	{
		minos::completion_close(m_completion);

		return false;
	}

	return true;
}

bool AstServer::request_ast_from_file(u32 thread_id, Range<char8> path, AstHandle* out) noexcept
{
	PathMapping* const mapping = m_path_map.value_from(thread_id, path, fnv1a(path.as_byte_range()));

	if (mapping->filedata_index != ~0u)
	{
		// TODO: Implement
		return false;
	}
	
	minos::FileHandle filehandle;

	if (!minos::file_create(path, minos::Access::Read, minos::CreateMode::Open, minos::AccessPattern::Unbuffered, minos::SyncMode::Asynchronous, &filehandle))
		return false;

	minos::FileInfo file_info;

	if (!minos::file_get_info(filehandle, &file_info))
		return false;

	const FileKey key{ file_info, filehandle };

	bool is_new;

	FileData* const data = m_file_map.value_from(thread_id, key, fnv1a(Range{ &key, 1 }.as_byte_range()), &is_new);

	if (!is_new)
	{
		// TODO: Implement
		return false;
	}

	// TODO: Check for up-to-date cached pre-parsed data

	minos::completion_associate_file(m_completion, data->filehandle, 0);

	ReadData* const read = m_read_freelist.pop(m_reads);

	if (read == nullptr)
		minos::exit_process(101);

	if (file_info.file_bytes > UINT32_MAX)
		minos::exit_process(101);

	if (!minos::file_read(data->filehandle, nullptr /* TODO: Allocate buffer */, static_cast<u32>(file_info.file_bytes), &read->overlapped))
		return false;

	// TODO: Actually return something meaningful
	*out = {};

	return true;
}

bool AstServer::get_job(job_proc* out_proc, void* out_param) noexcept
{
	ReadData* read = m_ready_reads.pop(m_reads);

	if (read == nullptr)
		return false;

	*out_proc = lex_and_parse;

	*static_cast<ReadData**>(out_param) = read;

	return true;
}

void AstServer::notify_job_complete(u32 key) noexcept
{
	m_read_freelist.push(m_reads, key);
}
