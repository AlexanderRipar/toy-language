#include "task_manag0r.hpp"

#include "container.hpp"
#include "threading.hpp"
#include "hash.hpp"
#include "minos.hpp"
#include "syntax_tree.hpp"

#pragma warning(push)
#pragma warning(disable : 4200) // nonstandard extension used: zero-sized array in struct/union
#pragma warning(disable : 4324) // structure was padded due to alignment specifier

// 'Key' for looking up the FileData corresponding to an os file.
// The info's FileIdentity is the only part that is used for the lookup. The
// other members are used for initializing the FileData before publishing it.
// This is done to
// 1. Meaningfully initialize the file using the handle provided in the call
//    that indicates that a new FileData has been added
// 2. Avoid having to call into the OS to reobtain the file's size and handle,
//    which must already be known, since the FileId can only be obtained with
//    an existing handle.
struct FileKey
{
	// FileInfo of the associated file
	minos::FileInfo info;

	// Handle to the associated file
	minos::FileHandle handle;
};

struct FileData
{
	struct
	{
		u32 hash;

		u32 volume_serial;

		u64 index;
	} identity;

	u32 next;

	std::atomic<minos::FileHandle> filehandle;

	u64 file_bytes;

	bool is_scanned;

	void init(FileKey key, u32 key_hash) noexcept
	{
		identity.hash = key_hash;

		identity.volume_serial = key.info.identity.volume_serial;

		identity.index = key.info.identity.index;

		filehandle = key.handle;

		file_bytes = key.info.file_bytes;

		is_scanned = false;
	}

	static constexpr u32 stride() noexcept
	{
		return static_cast<u32>(next_pow2(sizeof(FileData)));
	}

	static u32 get_required_strides([[maybe_unused]] FileKey key) noexcept
	{
		return 1;
	}

	u32 get_used_strides() const noexcept
	{
		return 1;
	}

	u32 get_hash() const noexcept
	{
		return identity.hash;
	}

	bool equal_to_key(FileKey key, [[maybe_unused]] u32 key_hash) const noexcept
	{
		return identity.volume_serial == key.info.identity.volume_serial && identity.index == key.info.identity.index;
	}

	void set_next(u32 index) noexcept
	{
		next = index;
	}

	u32 get_next() const noexcept
	{
		return next;
	}
};

struct FileProxy
{
	u32 hash;

	u32 next;

	u32 filedata_index;

	u16 filepath_chars;

	char8 filepath[];

	void init(Range<char8> key, u32 key_hash) noexcept
	{
		hash = key_hash;

		filedata_index = ~0u;

		filepath_chars = static_cast<u16>(key.count());

		memcpy(filepath, key.begin(), key.count());
	}

	static constexpr u32 stride() noexcept
	{
		return 16;
	}

	static u32 get_required_strides(Range<char8> key) noexcept
	{
		return static_cast<u32>((offsetof(FileProxy, filepath) + key.count()) / stride());
	}

	u32 get_used_strides() const noexcept
	{
		return static_cast<u32>((offsetof(FileProxy, filepath) + filepath_chars) / stride());
	}

	u32 get_hash() const noexcept
	{
		return hash;
	}

	bool equal_to_key(Range<char8> key, u32 key_hash) const noexcept
	{
		return hash == key_hash && filepath_chars == key.count() && memcmp(key.begin(), filepath, key.count()) == 0;
	}

	void set_next(u32 index) noexcept
	{
		next = index;
	}

	u32 get_next() const noexcept
	{
		return next;
	}
};

union FileRead
{
	struct
	{
		minos::FileHandle filehandle;

		TaskType content_type;

		u32 index_in_heap;

		u32 sequence_number;

		u32 valid_bytes_in_last_blockread;

		u32 required_blockread_count;

		ThreadsafeRingBufferHeader<u16, sizeof(u64)> blockread_index_queue_header;

		u16 blockread_index_queue[];
	};

	u32 freelist_next;
};

struct BlockRead
{
	minos::Overlapped overlapped;

	FileRead* fileread;

	union
	{
		u32 index_in_fileread;

		u32 freelist_next;
	};

	// 0 if read is pending
	// 1 if read has completed XOR all preceding reads have completed
	// 2 if read has completed AND all preceding reads have completed
	std::atomic<u32> completion_state;
};

struct RemainderBuffer
{
	u16 valid_buffer_bytes;

	char8 buffer[8189];

	char8 reserved_terminator;
};



struct FileMap
{
private:

	ThreadsafeMap2<Range<char8>, FileProxy> m_filenames;

	ThreadsafeMap2<FileKey, FileData> m_files;

public:

	struct InitInfo
	{
		decltype(m_filenames)::InitInfo filenames;

		decltype(m_files)::InitInfo files;
	};

	static u64 required_bytes(InitInfo info) noexcept
	{
		return decltype(m_filenames)::required_bytes(info.filenames) + decltype(m_files)::required_bytes(info.files);
	}

	bool init(InitInfo info, MemorySubregion memory) noexcept
	{
		MemorySubregion filename_memory = memory.partition_head(decltype(m_filenames)::required_bytes(info.filenames));

		if (!m_filenames.init(info.filenames, filename_memory))
			return false;

		return m_files.init(info.files, memory);
	}

	FileData* get_filedata(u32 thread_id, Range<char8> filepath, bool& out_is_new) noexcept
	{
		FileProxy* proxy = m_filenames.value_from(thread_id, filepath, fnv1a(filepath.as_byte_range()));

		if (proxy->filedata_index != ~0u)
		{
			out_is_new = false;

			return m_files.value_from(proxy->filedata_index);
		}

		FileKey key;

		ASSERT_OR_EXIT(minos::file_create(filepath, minos::Access::Read, minos::CreateMode::Open, minos::AccessPattern::Unbuffered, &key.handle));

		ASSERT_OR_EXIT(minos::file_get_info(key.handle, &key.info));

		FileData* filedata = m_files.value_from(thread_id, key, fnv1a(byte_range_from(&key.info.identity)), &out_is_new);

		proxy->filedata_index = m_files.index_from(filedata);

		if (!out_is_new)
			minos::file_close(key.handle);

		return filedata;
	}

	u32 index_from(const FileData* filedata) noexcept
	{
		return m_files.index_from(filedata);
	}

	FileData* fileread_from(u32 index) noexcept
	{
		return m_files.value_from(index);
	}
};



struct alignas(minos::CACHELINE_BYTES) File0r
{
private:

	struct HeapLine
	{
		static constexpr u32 COUNT = minos::CACHELINE_BYTES / sizeof(u16);

		u16 inds[COUNT];
	};

	// Map handling conversion from filepaths to FileData.
	FileMap m_filedata;

	// Maximum number of BlockReads that can be associated with a given
	// FileRead at the same time.
	u32 m_max_blockread_count_per_fileread;

	// Capacity of the queue storing associated BlockRead indices for every
	// FileRead. Due to the way ThreadsafeRingBufferHeader is implemented this
	// must always be a power of two. It is the smallest power of two that is
	// greater than or equal to m_max_blockread_count_per_fileread. 
	u32 m_blockread_queue_capacity_per_fileread;

	// Byte stride used for indexing into m_filereads. This is necessary as
	// each FileRead is followed by an array of u16s representing indices
	// into m_blockreads. The size of this array depends on the runtime
	// parameter m_blockread_queue_capacity_per_fileread, meaning that FileRead
	// is in fact a variable-sized struct.
	u32 m_fileread_stride;

	// Number of bytes reserved as read buffer for each BlockRead.
	// Always a multiple of the system's page size.
	u32 m_buffer_bytes_per_blockread;

	// Byte stride used for indexing into m_blockread_buffers. This is distinct
	// from m_buffer_bytes_per_blockread, since every buffer is followed by a
	// padding page to allow for a sentinel ('\0') to be appended to each read,
	// even if it uses the entire buffer.
	u32 m_blockread_buffer_stride;

	// Capacity of the queue holding indices into m_files.
	u32 m_max_queued_fileread_count;

	char8* m_blockread_buffers;

	BlockRead* m_blockreads;

	byte* m_filereads;

	RemainderBuffer* m_remainder_buffers;

	HeapLine* m_fileread_heap;

	u32* m_pending_file_queue;

	std::atomic<u32> alignas(minos::CACHELINE_BYTES) m_heap_status;

	std::atomic<u32> m_heap_used_count;

	ThreadsafeIndexStackListHeader<BlockRead, offsetof(BlockRead, freelist_next)> alignas(minos::CACHELINE_BYTES) m_blockread_freelist;

	ThreadsafeStridedIndexStackListHeader<FileRead, offsetof(FileRead, freelist_next)> alignas(minos::CACHELINE_BYTES) m_fileread_freelist;

	ThreadsafeRingBufferHeader<u32> m_pending_file_queue_header;

public:

	struct InitInfo
	{
		FileMap::InitInfo filemap;

		u32 max_blockreads_per_fileread;

		u32 blockread_buffer_bytes;

		u32 max_blockread_count;

		u32 max_fileread_count;

		u32 max_queued_fileread_count;
	};

	static u64 required_bytes(InitInfo info) noexcept
	{
		const u64 page_mask = minos::page_bytes() - 1;

		const u64 filemap_bytes = FileMap::required_bytes(info.filemap);

		const u64 blockread_bytes = (info.blockread_buffer_bytes + page_mask + 1) * info.max_blockread_count;

		const u32 queue_count_per_fileread = next_pow2(info.max_blockreads_per_fileread);

		const u64 queue_bytes_per_fileread = next_multiple(queue_count_per_fileread * sizeof(*FileRead::blockread_index_queue), alignof(FileRead));

		const u64 fileread_bytes = ((sizeof(FileRead) + queue_bytes_per_fileread) * info.max_fileread_count + page_mask) & ~page_mask;

		const u64 remainder_buffer_bytes = sizeof(RemainderBuffer) * info.max_fileread_count;

		const u64 heap_bytes = ((info.max_fileread_count + HeapLine::COUNT - 1) / HeapLine::COUNT) * sizeof(HeapLine);

		const u64 pending_file_queue_bytes = info.max_queued_fileread_count * sizeof(*m_pending_file_queue);

		return filemap_bytes + blockread_bytes + fileread_bytes + remainder_buffer_bytes + heap_bytes + pending_file_queue_bytes;
	}

	bool init(InitInfo info, MemorySubregion memory) noexcept
	{
		const u64 filemap_bytes = FileMap::required_bytes(info.filemap);

		MemorySubregion filemap_memory = memory.partition_head(filemap_bytes);

		if (!m_filedata.init(info.filemap, filemap_memory))
			return false;

		const u64 page_mask = minos::page_bytes() - 1;

		const u64 bytes_per_blockread = (info.blockread_buffer_bytes + page_mask + 1);

		const u64 blockread_bytes = bytes_per_blockread * info.max_blockread_count;

		const u32 queue_count_per_fileread = next_pow2(info.max_blockreads_per_fileread);

		const u64 queue_bytes_per_fileread = next_multiple(info.max_blockreads_per_fileread * sizeof(*FileRead::blockread_index_queue), alignof(FileRead));

		const u64 fileread_bytes = ((sizeof(FileRead) + queue_bytes_per_fileread) * info.max_fileread_count + page_mask) & ~page_mask;

		const u64 remainder_buffer_bytes = sizeof(RemainderBuffer) * info.max_fileread_count;

		const u64 heap_bytes = ((info.max_fileread_count + HeapLine::COUNT - 1) / HeapLine::COUNT) * sizeof(HeapLine);

		const u64 pending_file_queue_bytes = info.max_queued_fileread_count * sizeof(*m_pending_file_queue);

		if (!memory.commit(0, blockread_bytes + fileread_bytes + remainder_buffer_bytes + heap_bytes + pending_file_queue_bytes))
			return false;

		m_max_blockread_count_per_fileread = info.max_blockreads_per_fileread;

		m_blockread_queue_capacity_per_fileread = queue_count_per_fileread;

		m_fileread_stride = static_cast<u32>(sizeof(FileRead) + queue_bytes_per_fileread);

		m_buffer_bytes_per_blockread = info.blockread_buffer_bytes;

		m_blockread_buffer_stride = static_cast<u32>(bytes_per_blockread);

		m_max_queued_fileread_count = info.max_queued_fileread_count;

		m_blockreads = static_cast<BlockRead*>(memory.data());

		m_filereads = static_cast<byte*>(memory.data()) + blockread_bytes;

		m_remainder_buffers = reinterpret_cast<RemainderBuffer*>(static_cast<byte*>(memory.data()) + blockread_bytes + fileread_bytes);
		
		m_fileread_heap = reinterpret_cast<HeapLine*>(static_cast<byte*>(memory.data()) + blockread_bytes + fileread_bytes + remainder_buffer_bytes);

		m_pending_file_queue = reinterpret_cast<u32*>(static_cast<byte*>(memory.data()) + blockread_bytes + fileread_bytes + remainder_buffer_bytes + heap_bytes);

		m_heap_status.store(0, std::memory_order_relaxed);

		m_heap_used_count.store(0, std::memory_order_relaxed);

		m_blockread_freelist.init(m_blockreads, info.max_blockread_count);

		m_fileread_freelist.init(m_filereads, m_fileread_stride, info.max_fileread_count);

		m_pending_file_queue_header.init();

		return true;
	}

	bool issue_fileread(u32 thread_id, Range<char8> filepath, TaskType content_type) noexcept
	{
		bool is_new_filedata;

		FileData* const filedata = m_filedata.get_filedata(thread_id, filepath, is_new_filedata);

		if (!is_new_filedata)
		{
			// @TODO: File already read/being read

			return false;
		}

		FileRead* const fileread = m_fileread_freelist.pop(m_filereads, m_fileread_stride);

		// No FileReads available at the moment; Push to pending
		if (fileread == nullptr)
			ASSERT_OR_EXIT(m_pending_file_queue_header.enqueue(m_pending_file_queue, m_max_queued_fileread_count, m_filedata.index_from(filedata)));

		const u32 required_blockread_count = static_cast<u32>((filedata->file_bytes + m_buffer_bytes_per_blockread - 1) / m_buffer_bytes_per_blockread);

		fileread->filehandle = filedata->filehandle;

		fileread->content_type = content_type;

		fileread->index_in_heap = 0; // @TODO: Figure out how to actually get this into the heap and set the index accordingly

		fileread->valid_bytes_in_last_blockread = static_cast<u32>(filedata->file_bytes - static_cast<u64>((required_blockread_count - 1) * m_buffer_bytes_per_blockread));

		fileread->required_blockread_count = required_blockread_count;

		fileread->blockread_index_queue_header.init();

		const u32 max_initial_blockread_count = required_blockread_count < m_max_blockread_count_per_fileread ? required_blockread_count : m_max_blockread_count_per_fileread;

		u32 added_blockread_count = 0;

		for (u32 i = 0; i != max_initial_blockread_count; ++i)
		{
			BlockRead* const blockread = m_blockread_freelist.pop(m_blockreads);

			if (blockread == nullptr)
				break;

			blockread->fileread = fileread;

			blockread->completion_state = 0;

			const u32 blockread_index = static_cast<u32>(blockread - m_blockreads);

			ASSERT_OR_EXECUTE(fileread->blockread_index_queue_header.enqueue(fileread->blockread_index_queue, m_blockread_queue_capacity_per_fileread, static_cast<u16>(blockread_index), &blockread->index_in_fileread));

			blockread->overlapped.offset = blockread->index_in_fileread * m_buffer_bytes_per_blockread;

			const u32 bytes_to_read = blockread->index_in_fileread == fileread->required_blockread_count - 1 ? fileread->valid_bytes_in_last_blockread : m_buffer_bytes_per_blockread;

			ASSERT_OR_EXIT(minos::file_read(fileread->filehandle, m_blockread_buffers + m_buffer_bytes_per_blockread * blockread_index, bytes_to_read, &blockread->overlapped));

			added_blockread_count += 1;
		}

		// @TODO: Get fileread into heap
	}
};

#pragma warning(pop)
