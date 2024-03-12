#include "task_manag0r.hpp"

#include "range.hpp"
#include "container.hpp"
#include "threading.hpp"
#include "config.hpp"
#include "hash.hpp"
#include <cstdlib>

#include <Windows.h>

#pragma warning(push)
#pragma warning(disable : 4200) // nonstandard extension used: zero-sized array in struct/union
#pragma warning(disable : 4324) // structure was padded due to alignment specifier

static DWORD WINAPI iocp_thread_proc(void* read_queue_ptr) noexcept;





// Uniquely identifies a file.
// Derived from BY_HANDLE_FILE_INFORMATION, according to the documentation at
// https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfileinformationbyhandle#remarks
struct FileId
{
	// nFileIndexLow | static_cast<u64>(nFileIndexHigh) << 32
	u64 index;

	// dwVolumeSerialNumber
	u32 volume;
};

// Data associated with a single file that is persisted until the end of the
// compilation process.
struct FileData
{
	// fnv1a hash of fileid_volume and fileid_index
	u32 hash;

	// The volume member from the FileId derived from a call to
	// GetFileInformationByHandle with filehandle.
	u32 fileid_volume;


	// The index member from the FileId derived from a call to
	// GetFileInformationByHandle with filehandle.
	u64 fileid_index;

	// Handle to the associated file.
	// Opened with GENERIC_READ access, FILE_SHARE_READ sharing and
	// FILE_FLAG_OVERLAPPED as well as FILE_FLAG_NO_BUFFERING.
	// Kept open until the compilation process completes so the file cannot be
	// edited, allowing for consistent re-reading for error reporting.
	// Also created with ReadQueue.iocp directly after being created.
	HANDLE filehandle;

	static constexpr u32 stride() noexcept
	{
		return sizeof(FileData);
	}

	u32 get_hash() const noexcept
	{
		return hash;
	}

	void init(FileId key, u32 key_hash) noexcept
	{
		hash = key_hash;

		fileid_volume = key.volume;

		fileid_index = key.index;
	}

	bool equal_to_key(FileId key, u32 key_hash) const noexcept
	{
		return hash == key_hash && fileid_volume == key.volume && fileid_index == key.index; 
	}

	static u32 get_required_bytes([[maybe_unused]] FileId key) noexcept
	{
		return sizeof(FileData);
	}

	u32 get_used_bytes() const noexcept
	{
		return sizeof(FileData);
	}
};

// Indirection for mapping potentially multiple paths to a single FileData, so
// that a single file is not compiled multiple times. 
struct FileProxy
{
	// fnv1a hash of name
	u32 hash;

	// index of the proxied FileData in its ThreadsafeMap
	u32 index;

	// Number of char8s in name
	u16 name_count;

	// The path used to request the file
	char8 name[];

	static constexpr u32 stride() noexcept
	{
		return 4;
	}

	u32 get_hash() const noexcept
	{
		return hash;
	}

	void init(Range<char8> key, u32 key_hash) noexcept
	{
		hash = key_hash;

		index = ~0u;

		assert(key.count() <= UINT16_MAX);

		name_count = static_cast<u16>(key.count());

		memcpy(name, key.begin(), name_count);
	}

	bool equal_to_key(Range<char8> key, u32 key_hash) const noexcept
	{
		return key_hash == hash && key.count() == name_count && memcmp(key.begin(), name, name_count) == 0;
	}

	static u32 get_required_bytes(Range<char8> key) noexcept
	{
		return static_cast<u32>(offsetof(FileProxy, name) + key.count()) & ~(stride() - 1);
	}

	u32 get_used_bytes() const noexcept
	{
		return static_cast<u32>(offsetof(FileProxy, name) + name_count) & ~(stride() - 1);
	}
};

// State of a file currently being read. This is reused as soon as a file is
// fully read.
struct alignas(64) FileRead
{
	union
	{
		// index of the next unused FileRead, forming a singly linked list
		// headed by ReadQueue.free_file_read_index_head.
		// ~0u for the list's last entry.
		// Note that this member is valid when the FileRead is not currently in
		// use, meaning it is not associated with a file.
		u32 next_free_index;

		struct
		{
			// Offset into the file to be passed to the next call to ReadFile. This
			// must always be a multiple of the page size and must be set to 0 when a
			// FileRead is initiated.
			u64 current_offset;

			// Total size of the file in bytes.
			u64 total_bytes;

			// Pointer to the FileData for the file being read.
			FileData* data;

			// Handle to the file being read. This is duplicated from the
			// FileData pointed to by data for cache coherency (and since
			// we're overaligning this anyways to avoid false sharing and thus
			// have some space).
			HANDLE filehandle;

			// Index of the first associated BlockRead that has not yet been
			// scanned. ~0u if there are no associated unscanned BlockReads.
			// This is used to 'pop' BlockReads during scanning.
			u32 first_block_read_index;

			// Index of the last associated BlockRead that has not yet been
			// scanned. ~0u if there are no associated unscanned BlockReads.
			// Whenever a new BlockRead is associated with this FileRead, it is
			// linked into the list of BlockReads by setting
			// ReadQueue.block_reads[last_block_read_index].next_block_read to
			// its index, and then setting last_block_read_index to its index
			// instead.
			u32 last_block_read_index;

			// Number of bytes currently in use in the associated
			// remainder_buffer located at
			// ReadQueue.first_remainder_buffer + (this - ReadQueue.file_reads) * ReadQueue.per_remainder_buffer_bytes
			// This must never be greater than
			// ReadQueue.per_remainder_buffer_bytes. Should a string literal in
			// any input file exceed this maximum, compilation is aborted.
			u32 remainder_buffer_used_bytes;

			// Number of currently active block_reads associated with the file
			// underlying this FileRead.
			u32 active_block_read_count;

			// Index of the next FileRead with associated BlockReads that are
			// ready for scanning. ~0u if there is no such FileRead. 
			u32 next_outstanding_scan_index;
		};
	};

	struct Func_NextFreeIndex
	{
		u32* operator()(FileRead* ptr) const noexcept { return &ptr->next_free_index; }
	};

	struct Func_NextScanIndex
	{
		u32* operator()(FileRead* ptr) const noexcept { return &ptr->next_outstanding_scan_index; }
	};
};

// Data associated with a call to ReadFile.
struct alignas(64) BlockRead
{
	union
	{
		// Index of the next unused BlockRead, forming a singly linked list
		// headed by ReadQueue.free_block_read_index_head.
		// ~0u for the list's last entry.
		u32 next_free_index;

		struct
		{
			// OVERLAPPED structure passed to the ReadFile call.
			// Must be initialized to 0 apart from the Offset and OffsetHigh
			// members, which indicate the offset into the file from which
			// reading will begin. These must be set to
			// FileRead.current_offset, which is then incremented by the number
			// of bytes being read, so that the next read can be started at the
			// proper offset.
			// @TODO: hEvent might also need to be set since multiple ReadFiles
			// can be concurrently active for a single filehandle. According to
			// the documentation, this leads to a race with the filehandle
			// being signalled on each ReadFile completion, without the ability
			// to trace which ReadFile completed.
			// However, this *might* not be a problem when using completion
			// ports, as is the case here. See
			// https://learn.microsoft.com/en-us/windows/win32/sync/synchronization-and-overlapped-input-and-output
			//
			//   If no event object is specified in the OVERLAPPED structure,
			//   the system signals the state of the file, named pipe, or
			//   communications device when the overlapped operation has been
			//   completed. Thus, you can specify these handles as
			//   synchronization objects in a wait function, though their use
			//   for this purpose can be difficult to manage because, when
			//   performing simultaneous overlapped operations on the same
			//   file, named pipe, or communications device, there is no way to
			//   know which operation caused the object's state to be signaled.
			OVERLAPPED overlapped;

			// Index of the next BlockRead associated with the same FileRead.
			// ~0u if there is no such BlockRead. 'Next' in this case means the
			// BlockRead to the offset directly after this BlockRead.
			// See prev_block_read_complete for why this is necessary.
			u32 next_block_read_index;

			// Index of the associated FileRead. Used as
			// ReadQueue.file_reads[file_read_index].
			// When a scan completes, FileRead.active_block_read_count must be
			// decremented.
			u32 file_read_index;

			u32 index_in_file_read;

			// Indicates whether the ReadFile currently associated with this
			// BlockRead has already completed.
			// A BlockRead is ready for further processing when complete and
			// prev_block_read_complete are both true.
			bool complete;
		};
	};

	struct Func_NextFreeIndex
	{
		u32* operator()(BlockRead* ptr) const noexcept { return &ptr->next_free_index; }
	};
};

// Central repository for all read-related data. Instantiated as a singleton on
// compilation start. Internal and external alignment to 64 bytes is used to
// avoid false sharing.
// Note that any annotation identifying a member as constant only applies to
// the member itself, not any data it points to or indexes into.
struct alignas(64) ReadQueue
{
	// Number of bytes available in each read_buffer.
	// Constant after initialization.
	u32 per_read_buffer_bytes;

	// Number of bytes to use when indexing into the array of read_buffers.
	// This is distinct from per_read_buffer_bytes since each buffer has an
	// empty page at its end which is used for holding a sentinel value ('\0'),
	// allowing to uniformly indicate the buffer's end even when the entire
	// buffer is in use.
	// Note that just using a count would of course also be a possibility, but
	// Andrei Alexandrescu said this is cool so we're going for it!
	// Constant after initialization.
	u32 read_buffer_stride;

	// Number of bytes in each remainder_buffer.
	// Constant after initialization.
	u32 per_remainder_buffer_bytes;

	// Maximum number of BlockReads that can be active concurrently.
	// Constant after initialization.
	u32 max_block_read_count;

	// Maximum number of FileReads that can be active concurrently.
	// Constant after initialization.
	u32 max_active_file_read_count;

	// Limit on the number of BlockReads that can be occupied by a single
	// FileRead. This is mainly in place so that a single FileRead does not hog
	// all (or most) BlockReads, making other FileReads stall.
	// Constant after initialization.
	u32 max_per_file_read_block_read_count;

	// Pointer to the first remainder_buffer. There are
	// max_active_file_read_count consecutive buffers starting at this address,
	// each one being per_remainder_buffer_bytes bytes long.
	// Constant after initialization.
	FixedBuffer<char8, u32> remainder_buffers;

	// Pointer to the first FileRead. There are max_active_file_read_count
	// FileReads starting at this address.
	// Constant after initialization.
	FixedBuffer<FileRead, u32> file_reads;

	// Pointer to the first BlockRead. There are max_block_read_count
	// BlockReads starting at this address. Each one is associated with the
	// read_buffer starting at read_buffers.data() + (<the-block-read> -
	// block_reads) * per_read_buffer_bytes.
	// Constant after initialization.
	FixedBuffer<BlockRead, u32> block_reads;

	// Pointer to an array of max_per_file_read_block_read_count u16s, each
	// holding the head of a doubly linked list of FileReads with
	// active_block_read_count equal to their offset into this array.
	// Used to find the FileRead with the lowest number of active BlockReads to
	// prioritize new ReadFile calls.
	// Array elements for which there are no ReadFiles with corresponding
	// active_block_read_count are set to ~0u.
	// Note that the doubly linked list also includes a backlink to this head,
	// indicated by FileRead.prev_same_count_read_index being ~0u.
	// Constant after initialization.
	FixedBuffer<ThreadsafeIndexStackList<FileRead, FileRead::Func_NextScanIndex>, u32> file_read_active_count_index_heads;

	// Buffer holding an array of max_block_read_count read_buffers, each one
	// per_read_buffer_bytes bytes long.
	// Note that when indexing into this array of buffers, read_buffer_stride
	// must be used instead of per_read_buffer_bytes. See read_buffer_stride
	// for further details.
	// Constant after initialization.
	FixedBuffer<char8, u64> read_buffers;

	MemoryRegion memory;

	// Handle to the IO Completion Port which receives completed ReadFiles.
	// These are handled on a dedicated thread. No handle to that thread is
	// stored, since it is never needed after creation.
	// Constant after initialization.
	HANDLE iocp;

	// Indirect map from paths to actual files. This is mainly in place since
	// multiple paths may resolve to the same underlying file, which should not
	// trigger multiple compiles.
	// Mutable.
	ThreadsafeMap<Range<char8>, FileProxy> seen_filenames;

	// Map from files' unique identifiers to data related to them, which is
	// persisted until the end of the compilation process.
	// Mutable.
	ThreadsafeMap<FileId, FileData> seen_files;

	// Total number of FileReads currently in use (i.e., not part of the
	// free-list headed by free_file_read_index_head).
	// This is used to check whether there is any ongoing read-work so that the
	// compilation process is not ended early.
	// Mutable.
	u32 current_active_file_read_count;

	// Freelist of BlockReads.
	// Mutable.
	ThreadsafeIndexStackList<BlockRead, BlockRead::Func_NextFreeIndex> free_block_reads;

	// Freelist of FileReads.
	// Mutable.
	ThreadsafeIndexStackList<FileRead, FileRead::Func_NextFreeIndex> free_file_reads;

	// Index of the first FileData for which no FileRead has yet been created.
	// Used as an index for seen_files.value_from(u32 index).
	// This must be incremented atomically whenever a new FileData starts being
	// processed.
	// Mutable.
	u32 first_outstanding_file_data_index;

	// Linked list of FileReads that have scannable BlockReads.
	ThreadsafeIndexStackList<FileRead, FileRead::Func_NextScanIndex> outstanding_scans;
};



struct TaskQueue
{
	Semaphore pending_tasks;

	ReadQueue reads;

	MemoryRegionStackAllocator allocator;
};

static DWORD WINAPI iocp_thread_proc(void* task_queue_ptr) noexcept
{
	TaskQueue* const q = static_cast<TaskQueue*>(task_queue_ptr);

	while (true)
	{
		DWORD read_bytes;

		ULONG_PTR completion_key;

		OVERLAPPED* overlapped;

		ASSERT_OR_EXIT(GetQueuedCompletionStatus(q->reads.iocp, &read_bytes, &completion_key, &overlapped, INFINITE));

		if (completion_key == 2)
			return 0;

		BlockRead* block_read = CONTAINING_RECORD(overlapped, BlockRead, overlapped);

		block_read->complete = true;

		FileRead* file_read = q->reads.file_reads.data() + block_read->file_read_index;

		const u32 block_read_index = static_cast<u32>(block_read - q->reads.block_reads.data());

		if (file_read->first_block_read_index == block_read_index)
		{
			q->reads.outstanding_scans.push(static_cast<u32>(file_read - q->reads.file_reads.data()), q->reads.file_reads.data());

			q->pending_tasks.signal();
		}
	}
}

static DWORD WINAPI worker_thread_proc(void* task_queue_ptr) noexcept
{
	TaskQueue* q = static_cast<TaskQueue*>(task_queue_ptr);

	while (true)
	{
		q->pending_tasks.wait();

		if (FileRead* to_scan = q->reads.outstanding_scans.pop(q->reads.file_reads.data()); to_scan != nullptr)
		{
			// @TODO: Scan
		}
		else
		{
			assert(false);
		}
	}
}



struct ReadQueueInitDesc
{
	u32 max_string_length;

	u32 max_concurrent_fileread_count;

	u32 max_concurrent_blockread_count;

	u32 bytes_per_blockread;

	u32 max_concurrent_blockreads_per_fileread;

	u32 filedata_reserve_count;

	u32 filedata_initial_commit_count;

	u32 filedata_commit_increment_count;

	u32 filedata_initial_lookup_count;
};

static bool init(ReadQueue* q, const ReadQueueInitDesc* desc) noexcept
{
	SYSTEM_INFO sysinfo;

	GetSystemInfo(&sysinfo);

	const u32 page_mask = sysinfo.dwAllocationGranularity - 1;

	const u32 adj_bytes_per_blockread = (desc->bytes_per_blockread + page_mask) & ~page_mask;

	const u32 adj_bytes_per_remainder = (desc->max_string_length + 63) & ~63;


}

/*
static bool init(ReadQueue* q) noexcept
{
	const config::Config* cfg = config::get();

	SYSTEM_INFO sysinfo;

	GetSystemInfo(&sysinfo);

	const u32 page_mask = sysinfo.dwAllocationGranularity - 1;

	const u32 adj_read_buffer_bytes = (cfg->read.bytes_per_read + page_mask) & ~page_mask;

	const u32 adj_remainder_buffer_bytes = (cfg->base.max_string_length + 63) & ~63;

	q->free_block_reads.init();

	q->free_file_reads.init();

	q->first_outstanding_file_data_index = 0;

	q->per_read_buffer_bytes = adj_read_buffer_bytes;

	q->read_buffer_stride = adj_read_buffer_bytes + sysinfo.dwAllocationGranularity;

	q->per_remainder_buffer_bytes = adj_remainder_buffer_bytes;

	q->current_active_file_read_count = 0;

	q->max_active_file_read_count = cfg->read.max_concurrent_file_read_count;

	q->max_per_file_read_block_read_count = cfg->read.max_concurrent_read_count_per_file;

	const u64 total_read_buffer_bytes = static_cast<u64>(cfg->read.max_concurrent_read_count) * (adj_read_buffer_bytes + sysinfo.dwAllocationGranularity);

	const u64 total_remainder_buffer_bytes = static_cast<u64>(cfg->read.max_concurrent_file_read_count) * adj_remainder_buffer_bytes;

	const u64 total_file_read_bytes = cfg->read.max_concurrent_file_read_count * sizeof(FileRead);

	const u64 total_block_read_bytes = cfg->read.max_concurrent_read_count * sizeof(BlockRead);

	const u64 file_read_active_count_index_heads_bytes = static_cast<u64>(cfg->read.max_concurrent_read_count_per_file) * sizeof(u16);

	const u64 total_bytes = total_read_buffer_bytes + total_remainder_buffer_bytes + total_file_read_bytes + total_block_read_bytes + file_read_active_count_index_heads_bytes;

	if (!q->seen_filenames.init(
			cfg->mem.file_max_count * 64,
			cfg->mem.file_commit_increment_count * 64,
			cfg->mem.file_initial_commit_count * 64,
			cfg->mem.file_max_count * 2,
			cfg->mem.file_initial_lookup_count))
		goto FAILURE;

	if (!q->seen_files.init(
			cfg->mem.file_max_count * sizeof(FileData),
			cfg->mem.file_commit_increment_count * sizeof(FileData),
			cfg->mem.file_initial_commit_count * sizeof(FileData),
			cfg->mem.file_max_count * 2,
			cfg->mem.file_initial_lookup_count))
		goto FAILURE;

	const u64 total_read_buffer_bytes = static_cast<u64>(cfg->read.max_concurrent_read_count) * (adj_read_buffer_bytes + sysinfo.dwAllocationGranularity);

	const u64 total_remainder_buffer_bytes = static_cast<u64>(cfg->read.max_concurrent_file_read_count) * adj_remainder_buffer_bytes;

	const u64 total_file_read_bytes = cfg->read.max_concurrent_file_read_count * sizeof(FileRead);

	const u64 total_block_read_bytes = cfg->read.max_concurrent_read_count * sizeof(BlockRead);

	const u64 file_read_active_count_index_heads_bytes = static_cast<u64>(cfg->read.max_concurrent_read_count_per_file) * sizeof(u16);

	if (!q->read_buffers.init(
			total_read_buffer_bytes +
			total_remainder_buffer_bytes +
			total_file_read_bytes +
			total_block_read_bytes +
			file_read_active_count_index_heads_bytes))
		goto FAILURE;

	char8* ptr = q->read_buffers.data() + total_read_buffer_bytes;

	q->remainder_buffers = ptr;

	ptr += total_remainder_buffer_bytes;

	q->file_reads = reinterpret_cast<FileRead*>(ptr);

	ptr += total_file_read_bytes;

	q->block_reads = reinterpret_cast<BlockRead*>(ptr);

	ptr += total_block_read_bytes;

	q->file_read_active_count_index_heads = reinterpret_cast<u16*>(ptr);

	for (u32 i = 0; i != cfg->read.max_concurrent_read_count - 1; ++i)
		q->block_reads[i].next_free_index = i + 1;

	q->block_reads[cfg->read.max_concurrent_read_count - 1].next_free_index = ~0u;

	for (u32 i = 0; i != cfg->read.max_concurrent_file_read_count - 1; ++i)
		q->file_reads[i].next_free_index = static_cast<u16>(i + 1);

	q->file_reads[cfg->read.max_concurrent_file_read_count - 1].next_free_index = static_cast<u16>(~0u);

	memset(q->file_read_active_count_index_heads, 0, file_read_active_count_index_heads_bytes);

	q->iocp = CreateIoCompletionPort(nullptr, nullptr, 0, 0);

	if (q->iocp == nullptr)
		goto FAILURE;

	HANDLE iocp_thread = CreateThread(nullptr, 0, iocp_thread_proc, q, CREATE_SUSPENDED, nullptr);

	if (iocp_thread == nullptr)
		goto FAILURE;

	if (SetThreadDescription(iocp_thread, L"read_iocp_thread") != S_OK)
	{
		ASSERT_OR_EXECUTE(CloseHandle(iocp_thread));

		goto FAILURE;
	}

	if (ResumeThread(iocp_thread) == -1)
	{
		ASSERT_OR_EXECUTE(CloseHandle(iocp_thread));

		goto FAILURE;
	}

	ASSERT_OR_EXECUTE(CloseHandle(iocp_thread));

	return true;

FAILURE:

	ASSERT_OR_EXECUTE(deinit(q));

	return false;
}
*/

static bool deinit(ReadQueue* q) noexcept
{
	bool is_ok = static_cast<bool>(ASSERT_OR_EXECUTE(q->memory.deinit()));

	if (q->iocp != nullptr)
		is_ok &= static_cast<bool>(ASSERT_OR_EXECUTE(CloseHandle(q->iocp)));

	memset(q, 0, sizeof(*q));

	return is_ok;
}



struct TaskQueueInitDesc
{
	
};

static bool init(TaskQueue* q, const TaskQueueInitDesc* desc) noexcept
{

}

static bool deinit(TaskQueue* q) noexcept
{

}




// Read file:
// If it has already been read fully
//     Return it
// Else if it has not been seen at all
//     Open the file
//     Initiate reading
// Add the requesting Task to the read's dependee list
// Return a new Task to work on while the read completes

// Initiate reading:
// While (the number of reads is less than the maximal number of reads per file) and (there is a free read buffer) and (the file has more reads to be issued)
//     Call ReadFile

/*
static HANDLE open_file(Range<char8> absolute_path) noexcept
{
	char16 utf16_path_buf[4096];

	char16* utf16_path;

	const s32 utf16_path_count = MultiByteToWideChar(CP_UTF8, 0, absolute_path.begin(), absolute_path.count(), nullptr, 0);

	if (utf16_path_count == 0)
	{
		return nullptr;
	}
	else if (utf16_path_count < array_count(utf16_path_buf))
	{
		utf16_path = utf16_path_buf;
	}
	else
	{
		utf16_path = static_cast<char16*>(malloc((utf16_path_count + 1) * sizeof(char16)));

		if (utf16_path == nullptr)
			return nullptr;
	}

	assert(MultiByteToWideChar(CP_UTF8, 0, absolute_path.begin(), absolute_path.count(), utf16_path, utf16_path_count) == utf16_path_count);

	utf16_path[utf16_path_count] = '\0';

	const HANDLE filehandle = CreateFileW(utf16_path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

	if (utf16_path != utf16_path_buf)
		free(utf16_path);

	if (filehandle == INVALID_HANDLE_VALUE)
		return nullptr;

	return filehandle;
}

static bool read_into_buffer(FileRead* read, u32 read_index) noexcept
{

}

static bool read_file(ReadQueue* q, Range<char8> path) noexcept
{
	FileProxy* proxy = q->seen_filenames.value_from(path, fnv1a(path.as_byte_range()));

	if (proxy == nullptr)
		return false;

	if (proxy->index != ~0u)
	{
		// @TODO: File already read

		return true;
	}

	HANDLE filehandle = open_file(path);

	if (filehandle == nullptr)
		return false;

	BY_HANDLE_FILE_INFORMATION fileinfo;

	if (!GetFileInformationByHandle(filehandle, &fileinfo))
	{
		assert(CloseHandle(filehandle));

		return false;
	}

	FileId fileid{ fileinfo.nFileIndexLow | static_cast<u64>(fileinfo.nFileIndexHigh) << 32, fileinfo.dwVolumeSerialNumber };

	FileData* data = q->seen_files.value_from(fileid, fnv1a(Range{ reinterpret_cast<byte*>(&fileid), 6 }));

	if (data == nullptr)
	{
		assert(CloseHandle(filehandle));

		return false;
	}

	proxy->index = q->seen_files.index_from(data);

	if (data->filehandle == nullptr)
	{
		data->filehandle = filehandle;

		FileRead* read = q->open_file_reads.alloc();

		if (read == nullptr)
			return false;

		read->active_read_count = 0;

		read->current_offset = 0;

		read->data = data;

		read->remainder_buffer = nullptr; // @TODO

		read->remainder_buffer_used_bytes = 0; // @TODO

		read->total_bytes = fileinfo.nFileSizeLow | static_cast<u64>(fileinfo.nFileIndexHigh) << 32;

		for (u32 i = 0; i != config::get()->read.max_concurrent_read_count_per_file && read->current_offset != read->total_bytes; ++i)
		{
			const u32 read_index = q->first_free_read_index;

			if (read_index == ~0u)
				break;

			if (!read_into_buffer(read, read_index))
				return false;
		}
	}
	else
	{
		assert(CloseHandle(filehandle));

		// @TODO: File already read
	}

	return true;
}
*/

/*
 * Per file that needs to be scanned (PerFileInputData)
 *
 * HANDLE to the file (associated with IOCP; may be accessed indirectly via PerFileData or duplicated here to save an indirection)
 * Byte offset to be used by the next ReadFile call
 * Buffer for remaining data from scan of previous read
 * Number of bytes remaining from scan of previous read (i.e., in the above buffer)
 * Number of currently used read buffers (implied by address?)
 * Pointer to data kept around after scanning completes (PerFileData)
 * 
 */

/*
 * --- Interface ---
 *
 * - request_ast -
 * 
 * Inputs: Path to the file to be parsed
 * Outputs: A handle to the ast of the file
 * 
 * Initializes the sequence of tasks needed to parse a file into an abstract
 * syntax tree and returns a handle to the - possibly not yet parsed - ast.
 * 
 * 
 * - get_ast -
 * 
 * Inputs: Handle to an ast
 *         The Task that needs the ast
 *         The level of completion of the ast (raw or linked)
 * Outputs: If the ast has been completly parsed, the ast
 *          Otherwise, a new task to be performed
 * 
 * Checks whether the ast identified by the handle is already completed to the
 * requested level.
 * If the ast is available at the requested level, it is returned.
 * Otherwise, the requesting Task is blocked and a new Task to be performed is
 * returned. The blocked Task is reissued after the ast is available at the
 * requested level.
 * 
 * 
 * - next -
 * 
 * Inputs: The previously performed, completed Task
 * Outputs: A new Task to be performed
 * 
 * Marks the given Task as completed, unblocks Tasks that depended on it, and
 * returns a new Task to be performed.
 * If no Tasks are available at the time of the call, the function waits until
 * a Task becomes available.
 */

/*
 * // a.prog
 *
 * let b = import("b")
 * 
 * let f = func(s : b.S) -> u32 = s.x + s.y
 * 
 * 
 * 
 * // b.prog
 * 
 * let a = import("a")
 * 
 * let S = Struct(x : u32, y : u32)
 * 
 * let main = proc() -> u32 =
 * {
 * 	let s = S.create(1, 2)
 * 
 * 	return a.f(s)
 * }
 */

#pragma warning(pop)
