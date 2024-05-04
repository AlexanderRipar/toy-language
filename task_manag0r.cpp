#include "task_manag0r.hpp"

#include "threading.hpp"
#include "hash.hpp"
#include "minos.hpp"
#include "syntax_tree.hpp"

#pragma warning(push)
#pragma warning(disable : 4200) // nonstandard extension used: zero-sized array in struct/union
#pragma warning(disable : 4324) // structure was padded due to alignment specifier

struct BlockRead;

static constexpr u32 FILEREAD_COUNT_BITS = 12;

static constexpr u32 MAX_FILEREAD_COUNT = (1 << FILEREAD_COUNT_BITS) - 1;

static constexpr u32 BLOCKREAD_COUNT_BITS = 16;

static constexpr u32 MAX_BLOCKREAD_COUNT = (1 << BLOCKREAD_COUNT_BITS) - 1;

static constexpr u32 MAX_CONCURRENT_BLOCKREADS_PER_FILEREAD = 254;

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
	struct alignas(4) Position
	{
		u16 issued_blockread_count;

		u16 last_issued_blockread_index;
	};

	struct
	{
		minos::FileHandle filehandle;

		u32 file_index;

		u32 bytes_in_final_blockread;

		std::atomic<Position> position;

		u16 required_blockread_count;

		u16 index_in_heap;

		TaskType content_type;
	};

	u32 freelist_next;
};

struct alignas(64) BlockRead
{
	struct alignas(8) Position
	{
		u16 fileread_index;

		u16 index_in_fileread;

		u16 next_blockread_index;

		u16 zero_padding;
	};

	minos::Overlapped overlapped;

	byte* buffer;

	std::atomic<Position> position;

	std::atomic<u16> completion_state;

	u32 freelist_next;
};

struct RemainderBuffer
{
	u16 valid_buffer_bytes;

	byte buffer[8189];

	byte reserved_terminator;
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

	static MemoryRequirements get_memory_requirements(const InitInfo& info) noexcept
	{
		const MemoryRequirements filenames_req = decltype(m_filenames)::get_memory_requirements(info.filenames);

		const MemoryRequirements files_req = decltype(m_files)::get_memory_requirements(info.files);

		const u64 files_alignment_mask = files_req.alignment - 1;

		const u64 files_offset = (filenames_req.bytes + files_alignment_mask) & ~files_alignment_mask;

		const u64 total_bytes = files_offset + files_req.bytes;

		const u32 total_alignment = filenames_req.alignment > files_req.alignment ? filenames_req.alignment : files_req.alignment;

		return { total_bytes, total_alignment };
	}

	bool init(const InitInfo& info, byte* memory) noexcept
	{
		const MemoryRequirements filenames_req = m_filenames.get_memory_requirements(info.filenames);

		const MemoryRequirements files_req = m_files.get_memory_requirements(info.files);

		const u64 files_alignment_mask = files_req.alignment - 1;

		const u64 files_offset = (filenames_req.bytes + files_alignment_mask) & ~files_alignment_mask;

		if (!m_filenames.init(info.filenames, memory))
			return false;

		return m_files.init(info.files, memory + files_offset);
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

		ASSERT_OR_EXIT(minos::file_create(filepath, minos::Access::Read, minos::CreateMode::Open, minos::AccessPattern::Unbuffered, minos::SyncMode::Asynchronous, &key.handle));

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

	FileData* filedata_from(u32 index) noexcept
	{
		return m_files.value_from(index);
	}
};

struct FileReadPriorityQueue
{
	struct InitInfo
	{
		u32 max_active_fileread_count;

		u32 max_concurrent_blockread_count_per_fileread;
	};

private:

	struct HeapEntry
	{
		u32 priority : 8;

		u32 fileread_index : 12;

		u32 remaining_blockread_count : 12;
	};

	static constexpr u32 HEAP_SHIFT = 4;

	static constexpr u16 HEAP_N = 1 << HEAP_SHIFT;

	static constexpr u32 MAX_REMAINING_BLOCKREAD_COUNT = (1 << 12) - 1;

	static constexpr u32 LEAST_PRIORITY = 0xFF;



	Mutex m_mutex;

	HeapEntry* alignas(minos::CACHELINE_BYTES) m_priorities;

	u32 m_max_blockread_count_per_fileread;

	u32 m_active_fileread_count;



	void swap_heap_entries(FileRead* filereads, u32 index0, u32 index1) noexcept
	{
		HeapEntry entry0 = m_priorities[index0];

		HeapEntry entry1 = m_priorities[index1];

		m_priorities[index0] = entry1;

		m_priorities[index1] = entry0;

		filereads[entry0.fileread_index].index_in_heap = static_cast<u16>(index1);

		filereads[entry1.fileread_index].index_in_heap = static_cast<u16>(index0);
	}

	void heapify_down(FileRead* filereads, u32 parent_index, u32 parent_priority) noexcept
	{
		while (parent_index < m_active_fileread_count)
		{
			const u32 child_index = (parent_index + 1) << HEAP_SHIFT;

			u32 swap_index;

			u32 min_priority = parent_priority;

			const u32 end_index = child_index + HEAP_N < m_active_fileread_count ? child_index + HEAP_N : m_active_fileread_count;

			for (u32 i = child_index; i != end_index; ++i)
			{
				if (const u32 priority = m_priorities[i].priority; priority < min_priority)
				{
					min_priority = priority;

					swap_index = i;
				}
			}

			if (min_priority == parent_priority)
				return;

			swap_heap_entries(filereads, parent_index, swap_index);

			parent_index = swap_index;

			parent_priority = min_priority;
		}
	}

	void heapify_up(FileRead* filereads, u32 child_index, u32 child_priority) noexcept
	{
		while (child_index > HEAP_N)
		{
			const u32 parent_index = (child_index >> 2) - 1;

			if (child_priority >= m_priorities[parent_index].priority)
				return;

			swap_heap_entries(filereads, child_index, parent_index);

			child_index = parent_index;
		}
	}

	static void check_init_info(const InitInfo& info) noexcept
	{
		ASSERT_OR_EXIT(info.max_active_fileread_count < MAX_FILEREAD_COUNT);

		ASSERT_OR_EXIT(info.max_concurrent_blockread_count_per_fileread < LEAST_PRIORITY);
	}

	static u64 adjust_heap_count(const InitInfo& info) noexcept
	{
		u32 leaf_count = HEAP_N;

		u32 actual_count = leaf_count;

		while (actual_count < info.max_active_fileread_count)
		{
			leaf_count *= HEAP_N;

			actual_count += leaf_count;
		}

		return actual_count;
	}

public:

	static MemoryRequirements get_memory_requirements(const InitInfo& info) noexcept
	{
		return { adjust_heap_count(info) * sizeof(HeapEntry), minos::CACHELINE_BYTES };
	}

	bool init(const InitInfo& info, byte* memory) noexcept
	{
		if (!minos::commit(memory, get_memory_requirements(info).bytes))
			return false;

		m_mutex.init();

		m_priorities = reinterpret_cast<HeapEntry*>(memory);

		m_max_blockread_count_per_fileread = info.max_concurrent_blockread_count_per_fileread;

		m_active_fileread_count = 0;

		return true;
	}

	void decrease_read_count(FileRead* filereads, FileRead* to_decrease) noexcept
	{
		const u32 index_in_heap = to_decrease->index_in_heap;

		m_mutex.acquire();

		if (m_priorities[index_in_heap].remaining_blockread_count == 0)
		{
			m_mutex.release();

			return;
		}

		const u32 new_priority = m_priorities[index_in_heap].priority - 1;

		ASSERT_OR_IGNORE(new_priority < m_max_blockread_count_per_fileread);

		m_priorities[index_in_heap].priority = new_priority;

		heapify_up(filereads, index_in_heap, new_priority);

		m_mutex.release();
	}

	FileRead* get_min_read_count_and_increase(FileRead* filereads) noexcept
	{
		m_mutex.acquire();

		u32 min_index;

		u32 min_priority = LEAST_PRIORITY;

		for (u16 i = 0; i != HEAP_N; ++i)
		{
			if (const u32 priority = m_priorities[i].priority; priority < min_priority)
			{
				min_index = i;

				min_priority = priority;
			}
		}

		if (min_priority >= m_max_blockread_count_per_fileread)
		{
			m_mutex.release();

			return nullptr;
		}

		u32 new_priority = min_priority + 1;

		if (m_priorities[min_index].remaining_blockread_count == 1)
		{
			new_priority = LEAST_PRIORITY;

			m_active_fileread_count -= 1;
		}

		m_priorities[min_index].remaining_blockread_count -= 1;

		m_priorities[min_index].priority = new_priority;

		FileRead* const to_return = filereads + m_priorities[min_index].fileread_index;

		heapify_down(filereads, min_index, new_priority);

		m_mutex.release();

		return to_return;
	}

	void insert_at_min_read_count(FileRead* filereads, FileRead* to_insert) noexcept
	{
		m_mutex.acquire();

		const u32 fileread_index = static_cast<u32>(to_insert - filereads);

		ASSERT_OR_IGNORE(fileread_index <= MAX_FILEREAD_COUNT);

		ASSERT_OR_IGNORE(to_insert->required_blockread_count <= MAX_REMAINING_BLOCKREAD_COUNT);

		m_priorities[m_active_fileread_count] = { fileread_index, to_insert->required_blockread_count, 0 };

		to_insert->index_in_heap = static_cast<u16>(m_active_fileread_count);

		m_active_fileread_count += 1;

		heapify_up(filereads, m_active_fileread_count - 1, 0);

		m_mutex.release();
	}
};

static void scan_buffer(byte* buffer, u32 bytes, RemainderBuffer* rem, FileData* filedata) noexcept
{
	// @TODO
	buffer;
	bytes;
	rem;
	filedata;
}

struct FileFilet
{
	struct InitInfo
	{
		FileMap::InitInfo filemap;

		// Upper bound on the number of file requests which can be queued in
		// case no FileRead is available at the time.
		// This must be a power of two.
		u32 max_pending_fileread_count;

		// Maximum number of FileReads that can be processed concurrently.
		u32 max_fileread_count;

		// Maximum number of BlockReads that can be processed concurrently.
		u32 max_blockread_count;

		// Maximum number of BlockReads that can be associated with a single
		// FileRead at a time.
		u32 max_concurrent_blockread_count_per_fileread;

		// Number of bytes that are read with every BlockRead.
		// This must be a nonzero multiple of the system's page size.
		u32 bytes_per_blockread;
	};

private:

	struct MemoryDetails
	{
		u64 read_buffer_offset;

		u64 read_buffer_bytes;

		u64 filemap_offset;

		u64 filemap_bytes;

		u64 pqueue_offset;

		u64 pqueue_bytes;

		u64 fileread_offset;

		u64 fileread_bytes;

		u64 blockread_offset;

		u64 blockread_bytes;

		u64 remainder_offset;

		u64 remainder_bytes;

		u64 pending_filedata_offset;

		u64 pending_filedata_bytes;

		u64 bytes;

		u32 alignment;
	};



	FileMap m_filemap;

	FileReadPriorityQueue m_pqueue;

	FileRead* m_filereads;

	BlockRead* m_blockreads;

	byte* m_buffers;

	RemainderBuffer* m_remainders;

	u32* m_pending_filedata_index_buffer;

	u32 m_max_pending_filedata_count;

	u32 m_bytes_per_buffer;

	u32 m_max_blockreads_per_fileread;

	minos::CompletionHandle m_completion;

	minos::ThreadHandle m_completion_thread;

	ThreadsafeIndexStackListHeader<FileRead, offsetof(FileRead, freelist_next)> m_fileread_freelist;

	ThreadsafeIndexStackListHeader<BlockRead, offsetof(BlockRead, freelist_next)> m_blockread_freelist;

	ThreadsafeRingBufferHeader<u32> m_pending_filedata_queue;



	static MemoryDetails get_memory_details(const InitInfo& info) noexcept
	{
		const MemoryRequirements filemap_req = FileMap::get_memory_requirements(info.filemap);

		FileReadPriorityQueue::InitInfo pqueue_info;
		pqueue_info.max_active_fileread_count = info.max_fileread_count;
		pqueue_info.max_concurrent_blockread_count_per_fileread = info.max_concurrent_blockread_count_per_fileread;

		const MemoryRequirements pqueue_req = FileReadPriorityQueue::get_memory_requirements(pqueue_info);

		MemoryDetails off;

		off.read_buffer_offset = 0;
		off.read_buffer_bytes = info.bytes_per_blockread * info.max_blockread_count;

		off.filemap_offset = align_to(off.read_buffer_offset + off.read_buffer_bytes, filemap_req.alignment);
		off.filemap_bytes = filemap_req.bytes;

		off.pqueue_offset = align_to(off.filemap_offset + filemap_req.bytes, pqueue_req.alignment);
		off.pqueue_bytes = pqueue_req.bytes;

		off.fileread_offset = align_to(off.pqueue_offset + off.pqueue_bytes, alignof(FileRead));
		off.fileread_bytes = info.max_fileread_count * sizeof(FileRead);

		off.blockread_offset = align_to(off.fileread_offset + off.fileread_bytes, alignof(BlockRead));
		off.blockread_bytes = info.max_blockread_count * sizeof(BlockRead);

		off.remainder_offset = align_to(off.blockread_offset + off.blockread_bytes, alignof(RemainderBuffer));
		off.remainder_bytes = info.max_fileread_count * sizeof(RemainderBuffer);

		off.pending_filedata_offset = align_to(off.remainder_offset + off.remainder_bytes, alignof(u32));
		off.pending_filedata_bytes = info.max_pending_fileread_count * sizeof(u32);

		off.bytes = off.pending_filedata_offset + off.pending_filedata_bytes;

		// Minimum recommended alignment for read buffers to be used with
		// FILE_FLAG_UNBUFFERED, since it is larger than or equal to realistic
		// physical sector size (which is also always a power of two)
		off.alignment = minos::page_bytes();

		if (off.alignment < filemap_req.alignment)
			off.alignment = filemap_req.alignment;

		if (off.alignment < pqueue_req.alignment)
			off.alignment = pqueue_req.alignment;

		return off;
	}

	FileRead::Position claim_next_blockread_for_fileread(FileRead* fileread, BlockRead* blockread) noexcept
	{
		const u16 fileread_index = static_cast<u16>(fileread - m_filereads);

		const u16 blockread_index = static_cast<u16>(blockread - m_blockreads);

		FileRead::Position position = fileread->position.load(std::memory_order_seq_cst);

		while (true)
		{
			blockread->position.store({ fileread_index, position.issued_blockread_count, 0xFFFF, 0 }, std::memory_order_seq_cst);

			const FileRead::Position new_position = { static_cast<u16>(position.issued_blockread_count + 1), blockread_index };

			if (fileread->position.compare_exchange_strong(position, new_position, std::memory_order_seq_cst))
				return position;
		}
	}

	// @TODO: This should be a slow path; The usual case, when the FileRead
	// only requires a single BlockRead, can be done with way fewer atomics
	void issue_blockread_for_fileread(FileRead* fileread, BlockRead* blockread) noexcept
	{
		blockread->completion_state.store(0, std::memory_order_relaxed);

		const u16 fileread_index = static_cast<u16>(fileread - m_filereads);

		const u16 blockread_index = static_cast<u16>(blockread - m_blockreads);

		const FileRead::Position fileread_pos = claim_next_blockread_for_fileread(fileread, blockread);

		const u16 prev_blockread_index = fileread_pos.last_issued_blockread_index;

		const u16 index_in_fileread = fileread_pos.issued_blockread_count;

		if (prev_blockread_index == 0xFFFF)
		{
			blockread->completion_state.store(1, std::memory_order_relaxed);
		}
		else
		{
			BlockRead* const prev_blockread = m_blockreads + prev_blockread_index;

			BlockRead::Position prev_position = prev_blockread->position.load(std::memory_order_seq_cst);

			if (prev_position.fileread_index != fileread_index || prev_position.index_in_fileread + 1 != index_in_fileread)
			{
				blockread->completion_state.store(1, std::memory_order_relaxed);
			}
			else
			{
				const BlockRead::Position new_position = { fileread_index, static_cast<u16>(index_in_fileread - 1), blockread_index, 0 };

				if (!prev_blockread->position.compare_exchange_strong(prev_position, new_position, std::memory_order_seq_cst))
					blockread->completion_state.store(1, std::memory_order_relaxed);
			}
		}

		blockread->overlapped.offset = static_cast<u64>(index_in_fileread) * m_bytes_per_buffer;

		blockread->overlapped.unused_0 = 0;
		blockread->overlapped.unused_1 = 0;

		blockread->buffer = m_buffers + static_cast<u64>(blockread_index) * m_bytes_per_buffer;

		ASSERT_OR_EXIT(minos::file_read(
				fileread->filehandle,
				blockread->buffer,
				index_in_fileread == fileread->required_blockread_count ? fileread->bytes_in_final_blockread : m_bytes_per_buffer,
				&blockread->overlapped
		));
	}

	void pump_reads() noexcept
	{
		while (true)
		{
			BlockRead* const blockread = m_blockread_freelist.pop(m_blockreads);

			if (blockread == nullptr)
				return;

			FileRead* const fileread = m_pqueue.get_min_read_count_and_increase(m_filereads);

			if (fileread == nullptr)
			{
				m_blockread_freelist.push(m_blockreads, static_cast<u32>(blockread - m_blockreads));

				return;
			}

			issue_blockread_for_fileread(fileread, blockread);
		}
	}

	void dispatch_completed_blockread(FileData* filedata, FileRead* fileread, BlockRead* blockread, RemainderBuffer* remainder) noexcept
	{
		// @TODO: Implement
		switch (fileread->content_type)
		{
		case TaskType::Scan:
			scan_buffer(blockread->buffer, blockread->position.load(std::memory_order_relaxed).index_in_fileread == fileread->required_blockread_count - 1 ? fileread->bytes_in_final_blockread : m_bytes_per_buffer, remainder, filedata);
			break;

		default:
			ASSERT_UNREACHABLE;
		}
	}

	// @TODO: This should be a slow path; The usual case, when the FileRead
	// only requires a single BlockRead, can be done with way fewer atomics
	bool process_completed_blockread(BlockRead* blockread) noexcept
	{
		const u16 fileread_index = blockread->position.load(std::memory_order_relaxed).fileread_index;

		FileRead* const fileread = m_filereads + fileread_index;

		RemainderBuffer* const remainder = m_remainders + fileread_index;

		FileData* const filedata = m_filemap.filedata_from(fileread->file_index);

		if (blockread->completion_state.exchange(1, std::memory_order_relaxed) == 0)
			return false;

		while (true)
		{
			// @TODO: Process data in blockread
			dispatch_completed_blockread(filedata, fileread, blockread, remainder);

			const BlockRead::Position position = blockread->position.exchange({ 0xFFFF, 0xFFFF, 0, 0 }, std::memory_order_seq_cst);

			m_blockread_freelist.push(m_blockreads, static_cast<u32>(blockread - m_blockreads));

			// Check if we have reached the last currently active (and linked)
			// BlockReads in the FileRead
			if (position.next_blockread_index != 0xFFFF)
			{
				blockread = m_blockreads + position.next_blockread_index;

				if (blockread->completion_state.exchange(1, std::memory_order_relaxed) == 0)
					return true;

				continue;
			}

			// Check if we just processed the last BlockRead of the FileRead,
			// thus completing the FileRead
			if (position.index_in_fileread != fileread->required_blockread_count)
				return true;

			// If there are pending files, directly issue a new read.
			// Otherwise, add the FileRead to the list freelist.
			u32 new_filedata_index;

			if (m_pending_filedata_queue.dequeue(m_pending_filedata_index_buffer, m_max_pending_filedata_count, &new_filedata_index))
				initiate_fileread_for_filedata(m_filemap.filedata_from(new_filedata_index), new_filedata_index, fileread);
			else
				m_fileread_freelist.push(m_filereads, static_cast<u32>(fileread - m_filereads));

			// @TODO: Notify filedata that we're done

			return true;
		}

		ASSERT_UNREACHABLE;
	}

	static u32 completion_thread_proc(void* param) noexcept
	{
		FileFilet* const filet = static_cast<FileFilet*>(param);

		while (true)
		{
			minos::CompletionResult result;

			ASSERT_OR_EXIT(minos::completion_wait(filet->m_completion, &result));

			if (result.key == 2)
				return 0;

			ASSERT_OR_IGNORE(result.key == 1);

			if (filet->process_completed_blockread(reinterpret_cast<BlockRead*>(result.overlapped)))
				filet->pump_reads();
		}

		ASSERT_UNREACHABLE;
	}

	void initiate_fileread_for_filedata(FileData* filedata, u32 filedata_index, FileRead* fileread) noexcept
	{
		const u16 required_blockread_count = static_cast<u16>((filedata->file_bytes + m_bytes_per_buffer - 1) / m_bytes_per_buffer);

		fileread->filehandle = filedata->filehandle;
		fileread->file_index = filedata_index;
		fileread->bytes_in_final_blockread = static_cast<u32>(filedata->file_bytes - (required_blockread_count - 1) * m_bytes_per_buffer);
		fileread->position.store({ 0, 0xFFFF }, std::memory_order_relaxed);
		fileread->required_blockread_count = required_blockread_count;
		fileread->content_type = TaskType::Scan;

		RemainderBuffer* remainder = m_remainders + (fileread - m_filereads);
		remainder->valid_buffer_bytes = 0;

		m_pqueue.insert_at_min_read_count(m_filereads, fileread);

		pump_reads();
	}

public:

	static MemoryRequirements get_memory_requirements(const InitInfo& info) noexcept
	{
		const MemoryDetails details = get_memory_details(info);

		return { details.bytes, details.alignment };
	}

	bool init(const InitInfo& info, byte* memory) noexcept
	{
		const MemoryDetails details = get_memory_details(info);

		if (!minos::commit(memory + details.read_buffer_offset, details.read_buffer_bytes))
			return false;

		if (!minos::commit(memory + details.fileread_offset, details.fileread_bytes))
			return false;

		if (!minos::commit(memory + details.blockread_offset, details.blockread_bytes))
			return false;

		if (!minos::commit(memory + details.remainder_offset, details.remainder_bytes))
			return false;

		if (!minos::commit(memory + details.pending_filedata_offset, details.pending_filedata_bytes))
			return false;

		if (!m_filemap.init(info.filemap, memory + details.filemap_offset))
			return false;

		FileReadPriorityQueue::InitInfo pqueue_info;
		pqueue_info.max_active_fileread_count = info.max_fileread_count;
		pqueue_info.max_concurrent_blockread_count_per_fileread = info.max_concurrent_blockread_count_per_fileread;

		if (!m_pqueue.init(pqueue_info, memory + details.pqueue_offset))
			return false;

		if (!minos::completion_create(&m_completion))
			return false;

		if (!minos::thread_create(completion_thread_proc, this, range_from_cstring("completion worker"), &m_completion_thread))
			return false;

		m_buffers = memory + details.read_buffer_offset;

		m_filereads = reinterpret_cast<FileRead*>(memory + details.fileread_offset);

		m_blockreads = reinterpret_cast<BlockRead*>(memory + details.blockread_offset);

		m_remainders = reinterpret_cast<RemainderBuffer*>(memory + details.remainder_offset);

		m_pending_filedata_index_buffer = reinterpret_cast<u32*>(memory + details.pending_filedata_offset);

		m_bytes_per_buffer = info.bytes_per_blockread;

		m_max_blockreads_per_fileread = info.max_concurrent_blockread_count_per_fileread;

		m_max_pending_filedata_count = info.max_pending_fileread_count;

		m_blockread_freelist.init(m_blockreads, info.max_blockread_count);

		m_fileread_freelist.init(m_filereads, info.max_fileread_count);

		m_pending_filedata_queue.init();

		for (u32 i = 0; i != info.max_blockread_count; ++i)
			ASSERT_OR_EXIT(minos::event_create(&m_blockreads[i].overlapped.event));

		return true;
	}

	FileData* request_filedata(u32 thread_id, Range<char8> filepath) noexcept
	{
		bool is_new;

		FileData* const filedata = m_filemap.get_filedata(thread_id, filepath, is_new);

		const u32 filedata_index = m_filemap.index_from(filedata);

		if (!is_new)
			return filedata;

		FileRead* const fileread = m_fileread_freelist.pop(m_filereads);

		if (fileread == nullptr)
			ASSERT_OR_EXIT(m_pending_filedata_queue.enqueue(m_pending_filedata_index_buffer, m_max_pending_filedata_count, filedata_index));
		else
			initiate_fileread_for_filedata(filedata, filedata_index, fileread);

		return filedata;
	}
};

#pragma warning(pop)
