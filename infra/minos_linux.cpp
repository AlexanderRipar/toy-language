#ifndef _WIN32

#include "minos.hpp"

#include "threading.hpp"

#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sched.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/eventfd.h>
#include <sys/syscall.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <linux/futex.h>
#include <linux/io_uring.h>
#include <linux/prctl.h>
#include <limits.h>
#include <time.h>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <poll.h>
#include <signal.h>
#include <dirent.h>

// TODO: Remove
#if COMPILER_CLANG
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Wunused-parameter" // unused parameter
#elif COMPILER_GCC
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wunused-parameter" // unused parameter
#else
	#error("Unsupported compiler")
#endif


static constexpr u32 MINOS_IO_URING_MAX_COUNT_LOG2 = 9;

static constexpr u32 MINOS_IO_URING_MAX_COUNT = 1 << MINOS_IO_URING_MAX_COUNT_LOG2;

static constexpr u32 MINOS_IO_URING_ENTRY_COUNT = 4096;

static constexpr u32 MINOS_IO_URING_REGISTERED_FILES_MAX = 1024 * 1024;

static constexpr u32 MINOS_IO_URING_REGISTERED_FILES_INCREMENT = 1024;

template<typename T>
struct MinosRingBufferDesc
{
	T* head;

	T* tail;

	u32 mask;
};

struct MinosIoUringData
{
	// Submission

	std::atomic<u32>* submit_head;

	std::atomic<u32>* submit_tail;

	std::atomic<u32>* submit_begin;

	std::atomic<u32>* complete_head;

	std::atomic<u32>* complete_tail;

	io_uring_cqe* complete_begin;

	u32 submit_mask;

	u32 complete_mask;

	io_uring_sqe* submit_entries;

	u32 submit_entry_count;

	s32 ring_fd;

	std::atomic<u32> registered_file_count;

	std::atomic<s32>* registered_files;
};

struct MinosIoUringLock
{
	thd::Mutex mutex;
	
	// Bookkeeping for teardown. Put here in one cacheline with mutex since it
	// is only accessed on creation and teardown.

	void* submit_memory;

	void* complete_memory;

	u64 submit_memory_bytes;

	u64 complete_memory_bytes;
};

struct MinosIoUringSQEFreelist
{
	thd::IndexStackListHeader<io_uring_sqe, 0> sqes;
};

struct MinosIoUring
{
	union
	{
		MinosIoUringData data;

		byte unused_data_[next_multiple(sizeof(MinosIoUringData), static_cast<u64>(minos::CACHELINE_BYTES))];
	};

	union
	{
		MinosIoUringLock lock;

		byte unused_lock_[next_multiple(sizeof(MinosIoUringLock), static_cast<u64>(minos::CACHELINE_BYTES))];
	};

	union
	{
		MinosIoUringSQEFreelist freelist;

		byte unused_freelist_[next_multiple(sizeof(MinosIoUringSQEFreelist), static_cast<u64>(minos::CACHELINE_BYTES))];
	};
	
};

struct MinosGlobalIoUrings
{
	thd::IndexStackListHeader<MinosIoUring, 0> freelist;

	MinosIoUring rings[MINOS_IO_URING_MAX_COUNT];
};

MinosGlobalIoUrings g_io_urings;

[[nodiscard]] static s32 syscall_io_uring_setup(u32 entry_count, io_uring_params* params) noexcept
{
	return static_cast<s32>(syscall(__NR_io_uring_setup, entry_count, params));
}

[[nodiscard]] static s32 syscall_io_uring_enter(s32 ring_fd, u32 to_submit, u32 min_complete, u32 flags) noexcept
{
	return static_cast<s32>(syscall(__NR_io_uring_enter, ring_fd, to_submit, min_complete, flags, nullptr, 0));
}

[[nodiscard]] static s32 syscall_io_uring_register(s32 ring_fd, u32 op, void* arg, u32 arg_count) noexcept
{
	return static_cast<s32>(syscall(__NR_io_uring_register, ring_fd, op, arg, arg_count));
}

[[nodiscard]] static u32 m_io_uring_load_acquire(std::atomic<u32>* src) noexcept
{
	return src->load(std::memory_order_acquire);
}

[[nodiscard]] static bool m_io_uring_try_store_increment_cqe_head(std::atomic<u32>* head, u32 old_head) noexcept
{
	return head->compare_exchange_strong(old_head, old_head + 1, std::memory_order_release);
}

[[nodiscard]] static bool m_io_uring_create(MinosIoUring** out) noexcept
{
	MinosIoUring* const ring = g_io_urings.freelist.pop(g_io_urings.rings);

	if (ring == nullptr)
	{
		errno = ENOMEM;

		return false;
	}

	s32 ring_fd = -1;

	u64 submit_memory_bytes;

	u64 complete_memory_bytes;

	u64 submit_entry_bytes;

	void* submit_memory = MAP_FAILED;

	void* complete_memory = MAP_FAILED;

	void* submit_entries = MAP_FAILED;

	void* registered_files = MAP_FAILED;

	// Create the io_uring

	io_uring_params params{};

	// In case MINOS_IO_URING_ENTRY_COUNT is greater than IORING_MAX_ENTRIES,
	// clamp it to that value.
	params.flags = IORING_SETUP_CLAMP;

	ring_fd = syscall_io_uring_setup(MINOS_IO_URING_ENTRY_COUNT, &params);

	if (ring_fd < 0)
	{
		errno = -ring_fd;

		goto ERROR;
	}

	// Get the submit and complete ring memories and mmap them

	submit_memory_bytes = params.sq_off.array + params.sq_entries * sizeof(u32);

	complete_memory_bytes = params.cq_off.cqes + params.cq_entries * sizeof(io_uring_cqe);

	if ((params.features & IORING_FEAT_SINGLE_MMAP) != 0)
	{
		const u64 max_ring_bytes = submit_memory_bytes > complete_memory_bytes ? submit_memory_bytes : complete_memory_bytes;

		submit_memory_bytes = max_ring_bytes;

		submit_memory = mmap(nullptr, max_ring_bytes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, ring_fd, IORING_OFF_SQ_RING);

		if (submit_memory == MAP_FAILED)
			goto ERROR;
	}
	else
	{
		submit_memory = mmap(nullptr, submit_memory_bytes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, ring_fd, IORING_OFF_SQ_RING);

		if (submit_memory == MAP_FAILED)
			goto ERROR;

		complete_memory = mmap(nullptr, complete_memory_bytes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, ring_fd, IORING_OFF_CQ_RING);

		if (complete_memory == MAP_FAILED)
			goto ERROR;
	}

	// Get the submit entries array and mmap it

	submit_entry_bytes = params.sq_entries * sizeof(io_uring_sqe);

	submit_entries = mmap(nullptr, submit_entry_bytes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, ring_fd, IORING_OFF_SQES);

	if (submit_entries == MAP_FAILED)
		goto ERROR;

	// Register a large empty fd array initially for later calls to io_uring_register(IORING_REGISTER_FILES_UPDATE)

	registered_files = mmap(nullptr, MINOS_IO_URING_REGISTERED_FILES_MAX * (sizeof(s32) + sizeof(u64)), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (registered_files == MAP_FAILED)
		goto ERROR;

	memset(registered_files, 0xFF, MINOS_IO_URING_REGISTERED_FILES_INCREMENT * sizeof(s32));

	if (const s32 register_ok = syscall_io_uring_register(ring_fd, IORING_REGISTER_FILES, registered_files, MINOS_IO_URING_REGISTERED_FILES_INCREMENT); register_ok < 0)
	{
		errno = -register_ok;

		goto ERROR;
	}

	// Scope to avoid `goto`-related compiler shenanigans about skipped initializations
	{
		byte* const submit_base = static_cast<byte*>(submit_memory);

		byte* const complete_base = static_cast<byte*>((params.features & IORING_FEAT_SINGLE_MMAP) != 0 ? submit_memory : complete_memory);

		ring->data.submit_head = reinterpret_cast<std::atomic<u32>*>(submit_base + params.sq_off.head);
		ring->data.submit_tail = reinterpret_cast<std::atomic<u32>*>(submit_base + params.sq_off.tail);
		ring->data.submit_begin = reinterpret_cast<std::atomic<u32>*>(submit_base + params.sq_off.array);

		ring->data.complete_head = reinterpret_cast<std::atomic<u32>*>(complete_base + params.cq_off.head);
		ring->data.complete_tail = reinterpret_cast<std::atomic<u32>*>(complete_base + params.cq_off.tail);
		ring->data.complete_begin = reinterpret_cast<io_uring_cqe*>(complete_base + params.cq_off.cqes);

		ring->data.submit_mask = *reinterpret_cast<u32*>(submit_base + params.sq_off.ring_mask);
		ring->data.complete_mask = *reinterpret_cast<u32*>(complete_base + params.cq_off.ring_mask);

		ring->data.submit_entries = static_cast<io_uring_sqe*>(submit_entries);
		ring->data.submit_entry_count = params.sq_entries;

		ring->data.ring_fd = ring_fd;

		ring->data.registered_file_count.store(MINOS_IO_URING_REGISTERED_FILES_INCREMENT, std::memory_order_relaxed);
		ring->data.registered_files = static_cast<std::atomic<s32>*>(registered_files);

		ring->lock.mutex.init();
		ring->lock.submit_memory = submit_memory;
		ring->lock.submit_memory_bytes = submit_memory_bytes;
		ring->lock.complete_memory = complete_memory;
		ring->lock.complete_memory_bytes = complete_memory_bytes;

		ring->freelist.sqes.init(static_cast<io_uring_sqe*>(submit_entries), params.sq_entries);

		*out = ring;

		return true;
	}

ERROR:

	if (registered_files != MAP_FAILED)
	{
		if (munmap(registered_files, MINOS_IO_URING_REGISTERED_FILES_MAX * sizeof(s32)) != 0)
			panic("munmap(io_uring registered_files) failed after io_uring setup error (0x%X - %s)\n", minos::last_error(), strerror(minos::last_error()));
	}

	if (submit_entries != MAP_FAILED)
	{
		if (munmap(submit_entries, submit_entry_bytes) != 0)
			panic("munmap(io_uring submit_entries) failed after io_uring setup error (0x%X - %s)\n", minos::last_error(), strerror(minos::last_error()));
	}

	if (complete_memory != MAP_FAILED)
	{
		if (munmap(complete_memory, complete_memory_bytes) != 0)
			panic("munmap(io_uring complete_memory) failed after io_uring setup error (0x%X - %s)\n", minos::last_error(), strerror(minos::last_error()));
	}

	if (submit_memory != MAP_FAILED)
	{
		u64 bytes;

		if ((params.features & IORING_FEAT_SINGLE_MMAP) != 0)
			bytes = submit_memory_bytes > complete_memory_bytes ? submit_memory_bytes : complete_memory_bytes;
		else
			bytes = submit_memory_bytes;

		if (munmap(submit_memory, bytes) != 0)
			panic("munmap(io_uring submit_memory) failed after io_uring setup error (0x%X - %s)\n", minos::last_error(), strerror(minos::last_error()));
	}

	if (ring_fd >= 0)
	{
		if (close(ring_fd) != 0)
			panic("close(ring_fd) failed after io_uring setup error (0x%X - %s)\n", minos::last_error(), strerror(minos::last_error()));	
	}

	g_io_urings.freelist.push(g_io_urings.rings, ring - g_io_urings.rings);

	return false;
}

[[nodiscard]] static u32 m_io_uring_find_file_slot(MinosIoUring* ring, s32 file_fd) noexcept
{
	std::atomic<s32>* const registered_files = ring->data.registered_files;

	const u32 registered_file_count = ring->data.registered_file_count.load(std::memory_order_acquire);

	ASSERT_OR_IGNORE(registered_file_count + 1 < (static_cast<u64>(1) << (32 - MINOS_IO_URING_MAX_COUNT_LOG2)));

	for (u32 i = 0; i != registered_file_count; ++i)
	{
		if (registered_files[i].load(std::memory_order_relaxed) == -1)
		{
			s32 expected_value = -1;

			if (registered_files[i].compare_exchange_strong(expected_value, file_fd, std::memory_order_release))
				return i + 1;
		}
	}

	return 0;
}

[[nodiscard]] static bool m_io_uring_grow_registered_files(MinosIoUring* ring, s32 file_fd, u32* out_slot) noexcept
{
	ring->lock.mutex.acquire();

	const u32 locked_slot = m_io_uring_find_file_slot(ring, file_fd);

	*out_slot = locked_slot;

	if (locked_slot != 0)
	{
		ring->lock.mutex.release();

		return true;
	}

	const u32 old_count = ring->data.registered_file_count.load(std::memory_order_relaxed);

	if (old_count == MINOS_IO_URING_REGISTERED_FILES_MAX)
	{
		ring->lock.mutex.release();

		errno = ENOMEM;

		return false;
	}

	memset(static_cast<void*>(ring->data.registered_files + old_count), 0xFF, MINOS_IO_URING_REGISTERED_FILES_INCREMENT * sizeof(s32));

	const u32 new_count = old_count + MINOS_IO_URING_REGISTERED_FILES_INCREMENT;

	ring->data.registered_file_count.store(new_count, std::memory_order_release);

	const s32 unregister_ok = syscall_io_uring_register(ring->data.ring_fd, IORING_UNREGISTER_FILES, nullptr, 0);

	if (unregister_ok < 0)
		panic("syscall_io_uring_register(IORING_UNREGISTER_FILES) failed (0x%X - %s)\n", -unregister_ok, strerror(-unregister_ok));

	const s32 register_ok = syscall_io_uring_register(ring->data.ring_fd, IORING_REGISTER_FILES, ring->data.registered_files, new_count);

	if (register_ok < 0)
		panic("syscall_io_uring_register(IORING_REGISTER_FILES) failed (0x%X - %s)\n", -register_ok, strerror(-register_ok));

	ring->lock.mutex.release();

	return true;
}

[[nodiscard]] static minos::FileHandle m_io_uring_register_file(MinosIoUring* ring, s32 file_fd, u64 key) noexcept
{
	u32 slot;

	bool needs_register = true;

	while (true)
	{
		slot = m_io_uring_find_file_slot(ring, file_fd);

		if (slot != 0)
			break;

		if (!m_io_uring_grow_registered_files(ring, file_fd, &slot))
			return { nullptr };

		if (slot != 0)
		{
			needs_register = false;

			break;
		}
	}

	ASSERT_OR_IGNORE(slot - 1 < (static_cast<u64>(1) << (32 - MINOS_IO_URING_MAX_COUNT_LOG2)));

	io_uring_files_update update;
	update.offset = slot - 1;
	update.resv = 0;
	update.fds = reinterpret_cast<u64>(ring->data.registered_files + slot - 1);

	if (needs_register)
	{
		const s32 update_ok = syscall_io_uring_register(ring->data.ring_fd, IORING_REGISTER_FILES_UPDATE, &update, 1);
	
		if (update_ok < 0)
		{
			(void) ring->data.registered_files[slot - 1].compare_exchange_strong(file_fd, -1, std::memory_order_release);
	
			errno = -update_ok;
	
			return { nullptr };
		}
	}

	reinterpret_cast<u64*>(ring->data.registered_files + MINOS_IO_URING_REGISTERED_FILES_MAX)[slot - 1] = key;

	const u64 full_value = static_cast<u64>(file_fd) | (static_cast<u64>(ring - g_io_urings.rings) << 32) | (static_cast<u64>(slot) << (32 + MINOS_IO_URING_MAX_COUNT_LOG2));

	return { reinterpret_cast<void*>(full_value) };
}

static void m_io_uring_unregister_file(minos::FileHandle file) noexcept
{
	MinosIoUring* const ring = g_io_urings.rings + ((reinterpret_cast<u64>(file.m_rep) >> 32) & (MINOS_IO_URING_MAX_COUNT - 1));

	const u32 slot = (reinterpret_cast<u64>(file.m_rep) >> (32 + MINOS_IO_URING_MAX_COUNT_LOG2));

#ifndef _NDEBUG
	const s32 expected_fd = static_cast<s32>(reinterpret_cast<u64>(file.m_rep));

	const s32 actual_fd = ring->data.registered_files[slot - 1].exchange(-1, std::memory_order_release);

	ASSERT_OR_IGNORE(expected_fd == actual_fd);
#else
	ring->data.registered_flies[slot - 1].store(-1, std::memory_order_release);
#endif

	io_uring_files_update update;
	update.offset = slot - 1;
	update.resv = 0;
	update.fds = reinterpret_cast<u64>(ring->data.registered_files + slot - 1);

	const s32 update_ok = syscall_io_uring_register(ring->data.ring_fd, IORING_REGISTER_FILES_UPDATE, &update, 1);

	if (update_ok < 0)
		panic("syscall_io_uring_register(IORING_REGISTER_FILES_UPDATE) failed (0x%X - %s)\n", -update_ok, strerror(-update_ok));
}

static bool m_io_uring_submit_io(u32 opcode, minos::FileHandle handle, minos::Overlapped* overlapped, u32 bytes, void* buffer) noexcept
{
	MinosIoUring* const ring = g_io_urings.rings + ((reinterpret_cast<u64>(handle.m_rep) >> 32) & (MINOS_IO_URING_MAX_COUNT - 1));

	const s32 file_slot = (reinterpret_cast<u64>(handle.m_rep) >> (32 + MINOS_IO_URING_MAX_COUNT_LOG2));

	ring->lock.mutex.acquire();

	overlapped->reserved_0 = reinterpret_cast<const u64*>(ring->data.registered_files + MINOS_IO_URING_REGISTERED_FILES_MAX)[file_slot - 1];

	// Claim an sqe
	io_uring_sqe* const sqe = ring->freelist.sqes.pop(ring->data.submit_entries);

	if (sqe == nullptr)
		panic("Too many threads performed I/O simultaneously on minos::Completion backed by io_uring, resulting in no available SQEs. This is basically impossible, but hey. glhf.\n");

	const u32 sqe_slot = static_cast<u32>(sqe - ring->data.submit_entries);

	sqe->opcode = opcode;
	sqe->fd = file_slot - 1;
	sqe->off = overlapped->offset;
	sqe->addr = reinterpret_cast<u64>(buffer);
	sqe->len = bytes;
	sqe->user_data = reinterpret_cast<u64>(overlapped);
	sqe->flags = IOSQE_FIXED_FILE;

	const u32 tail = ring->data.submit_tail->load(std::memory_order_acquire);

	if (tail + ring->data.submit_mask + 1 == ring->data.submit_head->load(std::memory_order_acquire))
		panic("Too many threads performed I/O simultaneously on minos::Completion backed by io_uring, resulting in full submission ringbuffer. This is basically impossible, but hey. glhf.\n");

	const u32 index = tail & ring->data.submit_mask;

	ring->data.submit_begin[index] = sqe_slot;

	ring->data.submit_tail->store(tail + 1, std::memory_order_release);

	const s32 enter_ok = syscall_io_uring_enter(ring->data.ring_fd, 1, 0, 0);

	if (enter_ok < 0)
	{
		errno = -enter_ok;

		return false;
	}

	// Relase the sqe since the kernel is finished with it after
	// syscall_io_uring_enter.
	ring->freelist.sqes.push(ring->data.submit_entries, sqe_slot);

	ring->lock.mutex.release();

	return true;
}

static bool m_io_uring_read(minos::FileHandle handle, minos::Overlapped* overlapped, u32 bytes, void* buffer) noexcept
{
	return m_io_uring_submit_io(IORING_OP_READ, handle, overlapped, bytes, buffer);
}

static bool m_io_uring_write(minos::FileHandle handle, minos::Overlapped* overlapped, u32 bytes, const void* buffer) noexcept
{
	// Since we're doing a file write, buffer is only read, so const_cast is
	// fine.
	return m_io_uring_submit_io(IORING_OP_WRITE, handle, overlapped, bytes, const_cast<void*>(buffer));
}

// Waits for an io to complete on `ring`, storing its result in `*out`.
// Returns 0 in case of success, nonzero in case of an error.
[[nodiscard]] static bool m_io_uring_wait(MinosIoUring* ring, io_uring_cqe* out) noexcept
{
	while (true)
	{
		const u32 complete_head = m_io_uring_load_acquire(ring->data.complete_head);
	
		// If there are no available completion events, enter the kernel by
		// specifying IORING_ENTER_GETEVENTS in io_uring_enter.
		while (complete_head == ring->data.complete_tail->load(std::memory_order_acquire))
		{
			const s32 enter_result = syscall_io_uring_enter(ring->data.ring_fd, 0, 1, IORING_ENTER_GETEVENTS);

			// Negative results indicate an error of -error_code
			if (enter_result < 0)
			{
				errno = -enter_result;

				return false;
			}
		}
	
		// Tentatively copy the completion event. This may however already have been claimed.
		*out = ring->data.complete_begin[complete_head & ring->data.complete_mask];
	
		// Try to now actually claim it; Return success if we manage to do so.
		// If another thread raced ahead of us, try again.
		if (m_io_uring_try_store_increment_cqe_head(ring->data.complete_head, complete_head))
			return true;
	}
}

void minos::init() noexcept
{
	g_io_urings.freelist.init(g_io_urings.rings, array_count(g_io_urings.rings));
}

void minos::deinit() noexcept
{
	// No-op
}

u32 minos::last_error() noexcept
{
	return errno;
}

void* minos::mem_reserve(u64 bytes) noexcept
{
	void* const ptr = mmap(nullptr, bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	return ptr == MAP_FAILED ? nullptr : ptr;
}

bool minos::mem_commit(void* ptr, u64 bytes) noexcept
{
	const u64 page_mask = ~static_cast<u64>(page_bytes() - 1);

	void* const aligned_ptr = reinterpret_cast<void*>(reinterpret_cast<u64>(ptr) & page_mask);

	const u64 extra_bytes = static_cast<byte*>(ptr) - static_cast<byte*>(aligned_ptr);

	return mprotect(aligned_ptr, bytes + extra_bytes, PROT_READ | PROT_WRITE) == 0;
}

void minos::mem_unreserve(void* ptr, u64 bytes) noexcept
{
	if (munmap(ptr, bytes) != 0)
		panic("munmap failed (0x%X -%s)\n", last_error(), strerror(last_error()));
}

void minos::mem_decommit(void* ptr, u64 bytes) noexcept
{
	const u64 page_mask = page_bytes() - 1;

	ASSERT_OR_IGNORE((reinterpret_cast<u64>(ptr) & page_mask) == 0);

	ASSERT_OR_IGNORE((bytes & page_mask) == 0);

	if (mprotect(ptr, bytes, PROT_NONE) != 0)
		panic("mprotect(PROT_NONE) failed (0x%X - %s)\n", last_error(), strerror(last_error()));
}

u32 minos::page_bytes() noexcept
{
	return static_cast<u32>(getpagesize());
}

static s64 syscall_futex(const u32* address, s32 futex_op, u32 undesired_value_or_wakeup, const timespec* timeout) noexcept
{
	return syscall(SYS_futex, const_cast<u32*>(address), futex_op, undesired_value_or_wakeup, timeout, nullptr /* uaddr2 */, 0 /* val3 */);
}

static bool address_wait_impl(const void* address, const void* undesired, u32 bytes, const timespec* timeout) noexcept
{
	ASSERT_OR_IGNORE(bytes == 1
	             || (bytes == 2 && (reinterpret_cast<u64>(address) & 1) == 0)
	             || (bytes == 4 && (reinterpret_cast<u64>(address) & 3) == 0));

	const u32 undesired_value =
	    bytes == 1 ? *static_cast<const u8*>(undesired) :
	    bytes == 2 ? *static_cast<const u16*>(undesired) :
	                 *static_cast<const u32*>(undesired);

	const void* const aligned_address = reinterpret_cast<const void*>(reinterpret_cast<u64>(address) & ~3);

	while (true)
	{
		const u32 observed_value =
		    bytes == 1 ? static_cast<const std::atomic<u8>*>(address)->load(std::memory_order_relaxed) :
		    bytes == 2 ? static_cast<const std::atomic<u16>*>(address)->load(std::memory_order_relaxed) :
		                 static_cast<const std::atomic<u32>*>(address)->load(std::memory_order_relaxed);

		if (observed_value != undesired_value)
			break;

		const u32 observed_value_4_byte = bytes == 4 ? observed_value : static_cast<const std::atomic<u32>*>(aligned_address)->load(std::memory_order_relaxed);

		if (syscall_futex(static_cast<const u32*>(aligned_address), FUTEX_WAIT | FUTEX_PRIVATE_FLAG, observed_value_4_byte, timeout) != 0)
		{
			if (timeout != nullptr && errno == ETIMEDOUT)
				return false;

			// *address was not equal to observed_value.
			// Someone raced ahead of us and changed it, so we're already done
			// here.
			if (errno == EAGAIN)
				return true;

			panic("syscall_futex(FUTEX_WAIT) failed (0x%X - %s)\n", minos::last_error(), strerror(minos::last_error()));
		}
	}

	return true;
}

void minos::address_wait(const void* address, const void* undesired, u32 bytes) noexcept
{
	(void) address_wait_impl(address, undesired, bytes, nullptr);
}

bool minos::address_wait_timeout(const void* address, const void* undesired, u32 bytes, u32 milliseconds) noexcept
{
	timespec timeout;
	timeout.tv_sec = static_cast<time_t>(milliseconds / 1000);
	timeout.tv_nsec = static_cast<s64>(milliseconds % 1000) * 1'000'000;

	return address_wait_impl(address, undesired, bytes, &timeout);
}

void minos::address_wake_single(const void* address) noexcept
{
	if (syscall_futex(reinterpret_cast<const u32*>(reinterpret_cast<u64>(address) & ~3), FUTEX_WAKE | FUTEX_PRIVATE_FLAG, 1, nullptr) == -1)
		panic("syscall_futex(FUTEX_WAKE, 1) failed (0x%X - %s)\n", last_error(), strerror(last_error()));
}

void minos::address_wake_all(const void* address) noexcept
{
	if (syscall_futex(reinterpret_cast<const u32*>(reinterpret_cast<u64>(address) & ~3), FUTEX_WAKE | FUTEX_PRIVATE_FLAG, INT32_MAX, nullptr) == -1)
		panic("syscall_futex(FUTEX_WAKE, 1) failed (0x%X - %s)\n", last_error(), strerror(last_error()));
}

void minos::thread_yield() noexcept
{
	#if defined(__arm__) || defined(__aarch64__)
		asm volatile("yield");
	#else // Assume x86 / x64
		asm volatile("pause");
	#endif
}

NORETURN void minos::exit_process(u32 exit_code) noexcept
{
	exit(exit_code);
}

u32 minos::logical_processor_count() noexcept
{
	cpu_set_t set;

	// TODO / FIXME: This can fail with EINVAL in case of more than 1024 cpus present
	// on the system
	// See https://linux.die.net/man/2/sched_getaffinity
	if (sched_getaffinity(0, sizeof(set), &set) != 0)
		panic("sched_getaffinity(0) failed (0x%X - %s)\n", last_error(), strerror(last_error()));

	return CPU_COUNT(&set);
}

struct TrampolineThreadData
{
	minos::thread_proc proc;

	void* param;
};

static void* trampoline_thread_proc(void* param) noexcept
{
	TrampolineThreadData data = *static_cast<TrampolineThreadData*>(param);

	free(param);

	return reinterpret_cast<void*>(data.proc(data.param));
}

bool minos::thread_create(thread_proc proc, void* param, Range<char8> thread_name, ThreadHandle* opt_out) noexcept
{
	pthread_t thread;

	pthread_attr_t attr;

	if (pthread_attr_init(&attr) != 0)
		return false;

	TrampolineThreadData* const trampoline_data = static_cast<TrampolineThreadData*>(malloc(sizeof(TrampolineThreadData)));
	trampoline_data->proc = proc;
	trampoline_data->param = param;

	const s32 result = pthread_create(&thread, &attr, trampoline_thread_proc, trampoline_data);

	if (pthread_attr_destroy(&attr) != 0)
		panic("pthread_attr_destroy failed (0x%X - %s)\n", last_error(), strerror(last_error()));

	if (result != 0)
		return false;

	if (opt_out != nullptr)
		*opt_out = { reinterpret_cast<void*>(thread) };

	if (thread_name.count())
	{
		char8 name_buf[16];

		const u64 name_chars = thread_name.count() < 15 ? thread_name.count() : 15;

		memcpy(name_buf, thread_name.begin(), name_chars);

		name_buf[name_chars] = '\0';

		// Even though it seemingly isn't documented, ENOENT appears to mean
		// that the thread has already exited.
		if (pthread_setname_np(thread, name_buf) != 0 && errno != ENOENT)
			panic("pthread_setname_np failed (0x%X - %s)\n", last_error(), strerror(last_error()));
	}

	return true;
}

void minos::thread_close([[maybe_unused]] ThreadHandle handle) noexcept
{
	// No-op
}

void minos::thread_wait(ThreadHandle handle, u32* opt_out_result) noexcept
{
	void* retval;

	if (pthread_join(reinterpret_cast<pthread_t>(handle.m_rep), &retval) != 0)
		panic("thread_join failed (0x%X - %s)\n", last_error(), strerror(last_error()));

	const u64 retval_int = reinterpret_cast<u64>(retval);

	ASSERT_OR_IGNORE((retval_int >> 32) == 0);

	if (opt_out_result != nullptr)
		*opt_out_result = static_cast<u32>(retval_int);
}

bool minos::thread_wait_timeout(ThreadHandle handle, u32 milliseconds, u32* opt_out_result) noexcept
{
	void* retval;

	timespec ts;

	if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
		panic("clock_gettime failed while calculating absolute time for thread_wait_timeout (0x%X - %s)\n", last_error(), strerror(last_error()));

	ts.tv_sec += milliseconds / 1000;
	ts.tv_nsec += (milliseconds % 1000) * 1'000'000;

	if (ts.tv_nsec > 1'000'000'000)
	{
		ASSERT_OR_IGNORE(ts.tv_nsec < 2'000'000'000);

		ts.tv_nsec -= 1'000'000'000;

		ts.tv_sec += 1;
	}

	const s32 join_ok = pthread_timedjoin_np(reinterpret_cast<pthread_t>(handle.m_rep), &retval, &ts);

	if (join_ok != 0)
	{
		if (join_ok == EBUSY || join_ok == ETIMEDOUT)
			return false;

		panic("thread_timedjoin_np failed (0x%X - %s)\n", join_ok, strerror(join_ok));
	}

	const u64 retval_int = reinterpret_cast<u64>(retval);

	ASSERT_OR_IGNORE((retval_int >> 32) == 0);

	if (opt_out_result != nullptr)
		*opt_out_result = static_cast<u32>(retval_int);

	return true;
}

bool minos::file_create(Range<char8> filepath, Access access, ExistsMode exists_mode, NewMode new_mode, AccessPattern pattern, const CompletionInitializer* opt_completion, bool inheritable, FileHandle* out) noexcept
{
	if (filepath.count() > PATH_MAX)
		return false;

	char8 terminated_filepath[PATH_MAX + 1];

	memcpy(terminated_filepath, filepath.begin(), filepath.count());

	terminated_filepath[filepath.count()] = '\0';

	s32 oflag = O_CLOEXEC;

	if ((access & (Access::Read | Access::Write)) == (Access::Read | Access::Write))
	{
		oflag |= O_RDWR;
	}
	else if ((access & (Access::Read | Access::Write)) == (Access::Read))
	{
		oflag |= O_RDONLY;
	}
	else if ((access & (Access::Read | Access::Write)) == Access::Write)
	{
		oflag |= O_WRONLY;
	}
	else if (access == Access::None)
	{
		oflag |= O_PATH;
	}
	else
	{
		ASSERT_UNREACHABLE;
	}

	ASSERT_OR_IGNORE(new_mode != NewMode::Fail || exists_mode != ExistsMode::Fail);

	if (exists_mode == ExistsMode::Truncate)
		oflag |= O_TRUNC;

	// AccessPattern::Sequential and RandomAccess are not supported,
	// so just ignore them since they are only hints anyways.
	if (pattern == AccessPattern::Unbuffered)
		oflag |= O_DIRECT;

	s32 fd;

	if (new_mode == NewMode::Create)
	{
		oflag |= O_CREAT;
	
		if (exists_mode == ExistsMode::Fail)
			oflag |= O_EXCL;

		fd = open(terminated_filepath, oflag, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
	}
	else
	{
		fd = open(terminated_filepath, oflag);
	}

	if (fd == -1)
		return false;

	if (opt_completion != nullptr)
	{
		MinosIoUring* const ring = static_cast<MinosIoUring*>(opt_completion->completion.m_rep);

		const FileHandle handle = m_io_uring_register_file(ring, fd, opt_completion->key);

		if (handle.m_rep == nullptr)
		{
			if (close(fd) != 0)
				panic("Failed to close fd after failing to register it with io_uring (0x%X - %s)\n", errno, strerror(errno));

			return false;
		}

		*out = handle;
	}
	else
	{
		*out = { reinterpret_cast<void*>(fd) };
	}

	return true;
}

void minos::file_close(FileHandle handle) noexcept
{
	const u64 handle_value = reinterpret_cast<u64>(handle.m_rep);

	if ((handle_value >> 32) != 0)
		m_io_uring_unregister_file(handle);

	if (close(static_cast<s32>(handle_value)) != 0)
		panic("close(filefd) failed (0x%X - %s)\n", last_error(), strerror(last_error()));
}

bool minos::file_read(FileHandle handle, MutRange<byte> buffer, u64 offset, u32* out_bytes_read) noexcept
{
	ASSERT_OR_IGNORE((reinterpret_cast<u64>(handle.m_rep)) >> 32 == 0);

	const u32 bytes_to_read = buffer.count() < UINT32_MAX ? static_cast<u32>(buffer.count()) : UINT32_MAX;

	const s64 result = pread(static_cast<s32>(reinterpret_cast<u64>(handle.m_rep)), buffer.begin(), bytes_to_read, offset);

	if (result < 0)
		return false;

	ASSERT_OR_IGNORE(result <= UINT32_MAX);

	*out_bytes_read = static_cast<u32>(result);

	return true;
}

bool minos::file_read_async(FileHandle handle, MutRange<byte> buffer, Overlapped* overlapped) noexcept
{
	ASSERT_OR_IGNORE((reinterpret_cast<u64>(handle.m_rep)) >> 32 != 0);

	const u32 bytes_to_read = buffer.count() < UINT32_MAX ? static_cast<u32>(buffer.count()) : UINT32_MAX;

	return m_io_uring_read(handle, overlapped, bytes_to_read, buffer.begin());
}

bool minos::file_write(FileHandle handle, Range<byte> buffer, u64 offset) noexcept
{
	ASSERT_OR_IGNORE((reinterpret_cast<u64>(handle.m_rep) >> 32) == 0);

	if (buffer.count() > UINT32_MAX)
	{
		errno = EINVAL;

		return false;
	}

	return pwrite(static_cast<s32>(reinterpret_cast<u64>(handle.m_rep)), buffer.begin(), buffer.count(), offset) == static_cast<s64>(buffer.count());
}

bool minos::file_write_async(FileHandle handle, Range<byte> buffer, Overlapped* overlapped) noexcept
{
	ASSERT_OR_IGNORE((reinterpret_cast<u64>(handle.m_rep) >> 32) == 0);

	if (buffer.count() > UINT32_MAX)
	{
		errno = EINVAL;

		return false;
	}

	return m_io_uring_write(handle, overlapped, buffer.count(), buffer.begin());
}

bool minos::file_get_info(FileHandle handle, FileInfo* out) noexcept
{
	struct stat info;

	if (fstat(static_cast<s32>(reinterpret_cast<u64>(handle.m_rep)), &info) != 0)
		return false;

	out->identity.volume_serial = info.st_dev;
	out->identity.index = info.st_ino;
	out->bytes = info.st_size;
	out->creation_time = 0; // This is not supported under *nix
	out->last_modified_time = info.st_mtime; // Use mtime instead of ctime, as metadata changes likely do not matter (?)
	out->last_access_time = info.st_atime;
	out->is_directory = S_ISDIR(info.st_mode);

	return true;
}

bool minos::file_resize(FileHandle handle, u64 new_bytes) noexcept
{
	return ftruncate(static_cast<s32>(reinterpret_cast<u64>(handle.m_rep)), new_bytes) == 0;
}

static s32 event_create_impl(bool is_semaphore, u32 initial_value) noexcept
{
	return eventfd(initial_value, EFD_CLOEXEC | EFD_NONBLOCK | (is_semaphore ? EFD_SEMAPHORE : 0));
}

[[nodiscard]] static bool event_wait_impl(s32 fd, const timespec* opt_timeout) noexcept
{
	timespec end_time;

	timespec timeout;

	timespec* actual_timeout;

	if (opt_timeout != nullptr)
	{
		if (clock_gettime(CLOCK_MONOTONIC, &end_time) != 0)
			panic("clock_gettime failed (0x%X - %s)\n", minos::last_error(), strerror(minos::last_error()));

		end_time.tv_sec += opt_timeout->tv_sec;
		end_time.tv_nsec += opt_timeout->tv_nsec;

		if (end_time.tv_nsec >= 1'000'000'000)
		{
			ASSERT_OR_IGNORE(end_time.tv_nsec < 2'000'000'000);

			end_time.tv_sec += 1;
			end_time.tv_nsec -= 1'000'000'000;
		}

		timeout = *opt_timeout;

		actual_timeout = &timeout;
	}
	else
	{
		actual_timeout = nullptr;
	}

	while (true)
	{
		u64 event_value;

		const s64 read_result = read(fd, &event_value, sizeof(event_value));

		if (read_result == 8)
			return true;
		else if (read_result != -1)
			panic("read(eventfd) returned unexpected read count %d (expected 8)\n", read_result);
		else if (errno != EAGAIN)
			panic("read(eventfd) failed (0x%X - %s)\n", minos::last_error(), strerror(minos::last_error()));
		else if (opt_timeout != nullptr && opt_timeout->tv_sec == 0 && opt_timeout->tv_nsec == 0)
			return false;

		pollfd pfd;
		pfd.fd = fd;
		pfd.events = POLLIN;
		pfd.revents = 0;

		const s32 poll_result = ppoll(&pfd, 1, actual_timeout, nullptr);

		if (poll_result == -1)
			panic("poll(eventfd) failed (0x%X - %s)\n", minos::last_error(), strerror(minos::last_error()));
		else if (poll_result == 0)
			return false;
		else if (pfd.revents != POLLIN)
			panic("poll(eventfd) returned with non-POLLIN event 0x%X\n", pfd.revents);

		if (opt_timeout != nullptr)
		{
			timespec curr_time;

			if (clock_gettime(CLOCK_MONOTONIC, &curr_time) != 0)
				panic("clock_gettime failed (0x%X - %s)\n", minos::last_error(), strerror(minos::last_error()));

			timeout.tv_sec = end_time.tv_sec - curr_time.tv_sec;
			timeout.tv_nsec = end_time.tv_nsec - curr_time.tv_nsec;

			if (timeout.tv_nsec < 0)
			{
				ASSERT_OR_IGNORE(timeout.tv_nsec > -1'000'000'000);

				timeout.tv_sec -= 1;
				timeout.tv_nsec += 1'000'000'000;
			}
		}
	}
}

bool minos::event_create([[maybe_unused]] bool inheritable, EventHandle* out) noexcept
{
	const s32 fd = event_create_impl(false, 0);

	if (fd == -1)
		return false;

	*out = { reinterpret_cast<void*>(static_cast<u64>(fd)) };

	return true;
}

void minos::event_close(EventHandle handle) noexcept
{
	if (close(static_cast<s32>(reinterpret_cast<u64>(handle.m_rep))) != 0)
		panic("close(eventfd) failed (0x%X - %s)\n", last_error(), strerror(last_error()));
}

void minos::event_wake(EventHandle handle) noexcept
{
	u64 increment = 1;

	if (write(static_cast<s32>(reinterpret_cast<u64>(handle.m_rep)), &increment, sizeof(increment)) < 0)
		panic("write(eventfd) failed (0x%X - %s)\n", last_error(), strerror(last_error()));
}

void minos::event_wait(EventHandle handle) noexcept
{
	(void) event_wait_impl(static_cast<s32>(reinterpret_cast<u64>(handle.m_rep)), nullptr);
}

bool minos::event_wait_timeout(EventHandle handle, u32 milliseconds) noexcept
{
	timespec ts;
	ts.tv_sec = milliseconds / 1000;
	ts.tv_nsec = (milliseconds % 1000) * 1'000'000;

	return event_wait_impl(static_cast<s32>(reinterpret_cast<u64>(handle.m_rep)), &ts);
}

bool minos::completion_create(CompletionHandle* out) noexcept
{
	MinosIoUring* ring;

	if (!m_io_uring_create(&ring))
		return false;

	out->m_rep = ring;

	return true;
}

void minos::completion_close(CompletionHandle handle) noexcept
{
	MinosIoUring* const ring = static_cast<MinosIoUring*>(handle.m_rep);

	if (munmap(ring->lock.submit_memory, ring->lock.submit_memory_bytes) != 0)
		panic("munmap(io_uring submit_memory) failed (0x%X - %s)\n", last_error(), strerror(last_error()));
		
	if (ring->lock.complete_memory != MAP_FAILED)
	{
		if (munmap(ring->lock.complete_memory, ring->lock.complete_memory_bytes) != 0)
			panic("munmap(io_uring complete_memory) failed (0x%X - %s)\n", last_error(), strerror(last_error()));
	}

	if (munmap(ring->data.submit_entries, ring->data.submit_entry_count * sizeof(io_uring_sqe)) != 0)
		panic("munmap(io_uring submit_entries) failed (0x%X - %s)\n", last_error(), strerror(last_error()));

	if (close(ring->data.ring_fd) != 0)
		panic("close(io_uring fd) failed (0x%X - %s)\n", last_error(), strerror(last_error()));

	g_io_urings.freelist.push(g_io_urings.rings, ring - g_io_urings.rings);
}

bool minos::completion_wait(CompletionHandle completion, CompletionResult* out) noexcept
{
	MinosIoUring* const ring = static_cast<MinosIoUring*>(completion.m_rep);

	io_uring_cqe result;

	const bool wait_ok = m_io_uring_wait(ring, &result);

	if (!wait_ok)
		return false;

	Overlapped* const overlapped = reinterpret_cast<Overlapped*>(result.user_data);

	ASSERT_OR_IGNORE(result.flags == 0);

	out->key = overlapped->reserved_0;
	out->overlapped = overlapped;
	out->bytes = result.res;

	return true;
}

void minos::sleep(u32 milliseconds) noexcept
{
	if (usleep(milliseconds * 1000) != 0)
		panic("usleep failed (0x%X - %s)\n", last_error(), strerror(last_error()));
}

static char8** prepare_command_line_for_exec(char8* exe_path, u64 exe_path_chars, Range<Range<char8>> command_line) noexcept
{
	const u64 pointer_bytes = (command_line.count() + 2) * sizeof(char8*);

	u64 command_line_bytes = exe_path_chars + 1;

	for (Range<char8> arg : command_line)
		command_line_bytes += arg.count() + 1;

	void* const memory = malloc(pointer_bytes + command_line_bytes);

	if (memory == nullptr)
		panic("malloc failed (0x%X - %s)\n", minos::last_error(), strerror(minos::last_error()));

	char8** const arg_ptrs = static_cast<char8**>(memory);

	char8* arg_buf = reinterpret_cast<char8*>(memory) + pointer_bytes;

	arg_ptrs[0] = exe_path;

	u32 arg_index = 1;

	for (Range<char8> arg : command_line)
	{
		memcpy(arg_buf, arg.begin(), arg.count());

		arg_ptrs[arg_index] = arg_buf;

		arg_index += 1;

		arg_buf += arg.count();

		*arg_buf = '\0';

		arg_buf += 1;
	}

	arg_ptrs[command_line.count() + 1] = nullptr;

	return arg_ptrs;
}

static void prepare_fds_for_exec(Range<minos::GenericHandle> inherited_handles) noexcept
{
	for (minos::GenericHandle handle : inherited_handles)
	{
		const s32 fd = static_cast<s32>(reinterpret_cast<u64>(handle.m_rep));

		const s32 flags = fcntl(fd, F_GETFD);

		if (flags == -1)
			panic("fcntl(%s) to unset FD_CLOEXEC failed on fd %d (0x%X - %s)\n", "F_GETFD", fd, minos::last_error(), strerror(minos::last_error()));

		if (fcntl(fd, F_SETFD, flags & ~FD_CLOEXEC) != 0)
			panic("fcntl(%s) to unset FD_CLOEXEC failed on fd %d (0x%X - %s)\n", "F_SETFD", fd, minos::last_error(), strerror(minos::last_error()));
	}
}

static s32 syscall_pidfd_open(pid_t pid, u32 flags) noexcept
{
	return static_cast<s32>(syscall(SYS_pidfd_open, pid, flags));
}

bool minos::process_create(Range<char8> exe_path, Range<Range<char8>> command_line, Range<char8> working_directory, Range<GenericHandle> inherited_handles, bool inheritable, ProcessHandle* out) noexcept
{
	const pid_t parent_pid = getpid();

	const pid_t child_pid = fork();

	if (child_pid == -1)
		return false;

	if (child_pid != 0)
	{
		const s32 child_fd = syscall_pidfd_open(child_pid, 0);

		if (child_fd == -1)
			panic("syscall_pdifd_open failed (0x%X - %s)", last_error(), strerror(last_error()));

		// Since we haven't set any flags, we can simply set FD_CLOEXEC without
		// or'ing with previous flags. Note that PIDFD_NONBLOCK would be set
		// using F_SETFL, not F_SETFD, thus remaining unaffected by this call.
		if (fcntl(child_fd, F_SETFD, FD_CLOEXEC) != 0)
			panic("fcntl(pidfd) failed (0x%X - %s)", last_error(), strerror(last_error()));

		*out = { reinterpret_cast<void*>(static_cast<u64>(child_fd)) };

		return true;
	}

	const s32 parent_deathsig_ok = prctl(PR_SET_PDEATHSIG, SIGKILL);

	if (parent_deathsig_ok != 0)
		panic("prctl(PR_SET_DEATHSIG, SIGKILL) failed in newly spawned child process (0x%X - %s)\n", last_error(), strerror(last_error()));

	// Avoid race when parent is exited before prctl is called in the child.
	// Note that the `parent_pid` is taken before `fork`ing to make this
	// reliable. Also note that `getppid` returns a different pid when the
	// parent has exited, as we get reparented.
	if (parent_pid != getppid())
		exit(1);

	const char8* relative_exe_path;

	u32 relative_exe_path_chars;

	char8 own_exe[PATH_MAX + 1];

	if (exe_path.count() != 0)
	{
		relative_exe_path = exe_path.begin();

		relative_exe_path_chars = static_cast<u32>(exe_path.count());
	}
	else
	{
		const s64 readlink_result = readlink("/proc/self/exe", own_exe, array_count(own_exe) - 1);

		if (readlink_result < 0 || readlink_result == array_count(own_exe) - 1)
			panic("readlink(\"/proc/self/exe\") failed or lead to truncation (0x%X - %s)", last_error(), strerror(last_error()));

		relative_exe_path = own_exe;

		relative_exe_path_chars = static_cast<u32>(readlink_result);
	}

	char8 absolute_exe_path[PATH_MAX + 1];

	const u32 absolute_exe_path_chars = path_to_absolute(Range{ relative_exe_path, relative_exe_path_chars }, MutRange{ absolute_exe_path, array_count(absolute_exe_path) - 1 });

	if (absolute_exe_path_chars == 0 || absolute_exe_path_chars > array_count(absolute_exe_path) - 1)
		panic("Failed to get absolute path of executable file in newly spawned child process (0x%X - %s)\n", last_error(), strerror(last_error()));

	absolute_exe_path[absolute_exe_path_chars] = '\0';

	char8** const terminated_args = prepare_command_line_for_exec(absolute_exe_path, absolute_exe_path_chars, command_line);

	char8 terminated_working_directory[PATH_MAX + 1];

	if (working_directory.count() != 0)
	{
		if (working_directory.count() > array_count(terminated_working_directory) - 1)
			panic("working_directory passed to minos::process_create is longer than the supported maximum of %u characters", array_count(terminated_working_directory) - 1);

		memcpy(terminated_working_directory, working_directory.begin(), working_directory.count());

		terminated_working_directory[working_directory.count()] = '\0';

		if (chdir(terminated_working_directory) != 0)
			panic("Could not set working directory of newly spawned process (0x%X - %s)\n", last_error(), strerror(last_error()));
	}

	prepare_fds_for_exec(inherited_handles);

	execvp(absolute_exe_path, terminated_args);

	panic("execvp failed in newly spawned process (0x%X - %s)\n", last_error(), strerror(last_error()));
}

void minos::process_close(ProcessHandle handle) noexcept
{
	if (close(static_cast<s32>(reinterpret_cast<u64>(handle.m_rep))) != 0)
		panic("close(pidfd) failed (0x%X - %s)\n", last_error(), strerror(last_error()));
}

[[nodiscard]] static bool process_wait_impl(minos::ProcessHandle handle, const timespec* opt_timeout, u32* opt_out_result) noexcept
{
	if (opt_timeout != nullptr)
	{
		pollfd fd;
		fd.fd = static_cast<s32>(reinterpret_cast<u64>(handle.m_rep));
		fd.events = POLLIN;
		fd.revents = 0;

		const s32 poll_result = ppoll(&fd, 1, opt_timeout, nullptr);

		ASSERT_OR_IGNORE(poll_result != 0 || opt_timeout != nullptr);

		if (poll_result == 0)
			return false;
		else if (poll_result == -1)
			panic("ppoll(procfd) failed (0x%X - %s)\n", minos::last_error(), strerror(minos::last_error()));

		ASSERT_OR_IGNORE(poll_result == 1);
	}

	siginfo_t exit_info;

	// WEXITED: Only wait for exited, not temporarily stopped processes.
	// WNOWAIT: This allows multiple waits. Otherwise, only the first one would
	//          succeed.
	if (waitid(P_PIDFD, static_cast<s32>(reinterpret_cast<u64>(handle.m_rep)), &exit_info, WEXITED | WNOWAIT) != 0)
		panic("waitid(pidfd) failed (0x%X - %s)\n", minos::last_error(), strerror(minos::last_error()));

	if (opt_out_result != nullptr)
		*opt_out_result = exit_info.si_status;

	return true;
}

void minos::process_wait(ProcessHandle handle, u32* opt_out_result) noexcept
{
	(void) process_wait_impl(handle, nullptr, opt_out_result);
}

bool minos::process_wait_timeout(ProcessHandle handle, u32 milliseconds, u32* opt_out_result) noexcept
{
	timespec ts;
	ts.tv_sec = milliseconds / 1000;
	ts.tv_nsec = (milliseconds % 1000) * 1'000'000;

	return process_wait_impl(handle, &ts, opt_out_result);
}

bool minos::shm_create(Access access, u64 bytes, ShmHandle* out) noexcept
{
	const s32 fd = memfd_create("minos_memfd", MFD_CLOEXEC);

	if (fd == -1)
		return false;

	if (ftruncate(fd, bytes) != 0)
	{
		if (close(fd) != 0)
			panic("close(memfd) failed (0x%X - %s)\n", last_error(), strerror(last_error()));

		return false;
	}

	*out = { reinterpret_cast<void*>(static_cast<u64>(fd)) };

	return true;
}

void minos::shm_close(ShmHandle handle) noexcept
{
	if (close(static_cast<s32>(reinterpret_cast<u64>(handle.m_rep))) != 0)
		panic("close(memfd) failed (0x%X - %s)\n", last_error(), strerror(last_error()));
}

void* minos::shm_map(ShmHandle handle, Access access, u64 offset, u64 bytes) noexcept
{
	const s32 fd = static_cast<s32>(reinterpret_cast<u64>(handle.m_rep));

	s32 native_access = 0;

	if ((access & Access::Read) != Access::None)
		native_access |= PROT_READ;

	if ((access & Access::Write) != Access::None)
		native_access |= PROT_WRITE;

	if ((access & Access::Execute) != Access::None)
		native_access |= PROT_EXEC;

	if (access == Access::None)
		native_access = PROT_NONE;

	void* const address = mmap(nullptr, bytes, native_access, MAP_SHARED, fd, offset);

	if (address == MAP_FAILED)
		return nullptr;

	return address;
}

void minos::shm_unmap(void* address, u64 bytes) noexcept
{
	if (munmap(address, bytes) != 0)
		panic("munmap(shm) failed (0x%X - %s)\n", last_error(), strerror(last_error()));
}

bool minos::sempahore_create(u32 initial_count, [[maybe_unused]] bool inheritable, SemaphoreHandle* out) noexcept
{
	const s32 fd = event_create_impl(true, initial_count);

	if (fd == -1)
		return false;

	*out = { reinterpret_cast<void*>(fd) };

	return true;
}

void minos::semaphore_close(SemaphoreHandle handle) noexcept
{
	if (!close(static_cast<s32>(reinterpret_cast<u64>(handle.m_rep))))
		panic("close(eventfd semaphore) failed (0x%X - %s)\n", last_error(), strerror(last_error()));
}

void minos::semaphore_post(SemaphoreHandle handle, u32 count) noexcept
{
	u64 increment = count;

	if (write(static_cast<s32>(reinterpret_cast<u64>(handle.m_rep)), &increment, sizeof(increment)) < 0)
		panic("write(eventfd semaphore) failed (0x%X - %s)\n", last_error(), strerror(last_error()));
}

void minos::semaphore_wait(SemaphoreHandle handle) noexcept
{
	(void) event_wait_impl(static_cast<s32>(reinterpret_cast<u64>(handle.m_rep)), nullptr);
}

bool minos::semaphore_wait_timeout(SemaphoreHandle handle, u32 milliseconds) noexcept
{
	timespec ts;
	ts.tv_sec = milliseconds / 1000;
	ts.tv_nsec = (milliseconds % 1000) * 1'000'000;

	return event_wait_impl(static_cast<s32>(reinterpret_cast<u64>(handle.m_rep)), &ts);
}

minos::DirectoryEnumerationStatus minos::directory_enumeration_create(Range<char8> directory_path, DirectoryEnumerationHandle* out, DirectoryEnumerationResult* out_first) noexcept
{
	char8 terminated_path[PATH_MAX + 1];

	if (directory_path.count() > array_count(terminated_path) - 1)
	{
		errno = ENAMETOOLONG;

		return DirectoryEnumerationStatus::Error;
	}

	terminated_path[directory_path.count()] = '\0';

	DIR* const dir = opendir(terminated_path);

	if (dir == nullptr)
		return DirectoryEnumerationStatus::Error;

	*out = { dir };

	errno = 0;

	const DirectoryEnumerationStatus first_ok = directory_enumeration_next({ dir }, out_first);

	if (first_ok == DirectoryEnumerationStatus::Error)
		directory_enumeration_close({ dir });

	return first_ok;
}

minos::DirectoryEnumerationStatus minos::directory_enumeration_next(DirectoryEnumerationHandle handle, DirectoryEnumerationResult* out) noexcept
{
	// Reset errno to determine whether readdir failed with an error or end-of-stream
	errno = 0;

	dirent* const entry = readdir(static_cast<DIR*>(handle.m_rep));

	if (entry == nullptr)
		return errno == 0 ? DirectoryEnumerationStatus::NoMoreFiles : DirectoryEnumerationStatus::Error;

	const s32 dir_fd = dirfd(static_cast<DIR*>(handle.m_rep));

	if (dir_fd == -1)
		return DirectoryEnumerationStatus::Error;

	const s32 entry_fd = openat(dir_fd, entry->d_name, O_RDONLY | O_PATH);

	if (entry_fd == -1)
		return DirectoryEnumerationStatus::Error;

	struct stat info;

	if (fstat(entry_fd, &info) != 0)
	{
		if (close(entry_fd) != 0)
			panic("close(direntry_fd) failed (0x%X - %s)\n", last_error(), strerror(last_error()));
	}

	out->creation_time = 0; // This is not supported under *nix
	out->last_access_time = info.st_atime;
	out->last_write_time = info.st_mtime; // Use mtime instead of ctime, as metadata changes likely do not matter (?)
	out->bytes = info.st_size;
	out->is_directory = S_ISDIR(info.st_mode);

	static_assert(sizeof(entry->d_name) <= sizeof(out->filename));

	memcpy(out->filename, entry->d_name, sizeof(entry->d_name));

	return DirectoryEnumerationStatus::Ok;
}

void minos::directory_enumeration_close(DirectoryEnumerationHandle handle) noexcept
{
	if (closedir(static_cast<DIR*>(handle.m_rep)) != 0)
		panic("closedir failed (0x%X - %s)\n", last_error(), strerror(last_error()));
}

bool minos::directory_create(Range<char8> path) noexcept
{
	char8 terminated_path[PATH_MAX + 1];

	if (path.count() > array_count(terminated_path) - 1)
	{
		errno = ENAMETOOLONG;

		return false;
	}

	memcpy(terminated_path, path.begin(), path.count());

	return mkdir(terminated_path, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) == 0;
}

bool minos::path_remove_file(Range<char8> path) noexcept
{
	char8 terminated_path[PATH_MAX + 1];

	if (path.count() > array_count(terminated_path) - 1)
	{
		errno = ENAMETOOLONG;

		return false;
	}

	memcpy(terminated_path, path.begin(), path.count());

	terminated_path[path.count()] = '\0';

	return unlink(terminated_path) == 0;
}

bool minos::path_remove_directory(Range<char8> path) noexcept
{
	char8 terminated_path[PATH_MAX + 1];

	if (path.count() > array_count(terminated_path) - 1)
	{
		errno = ENAMETOOLONG;

		return false;
	}

	memcpy(terminated_path, path.begin(), path.count());

	terminated_path[path.count()] = '\0';

	return rmdir(terminated_path) == 0;
}

bool minos::path_is_directory(Range<char8> path) noexcept
{
	char8 terminated_path[PATH_MAX + 1];

	if (path.count() > array_count(terminated_path) - 1)
	{
		errno = ENAMETOOLONG;

		return false;
	}

	memcpy(terminated_path, path.begin(), path.count());

	terminated_path[path.count()] = '\0';

	struct stat info;

	if (stat(terminated_path, &info) != 0)
		return false;

	return S_ISDIR(info.st_mode);
}

bool minos::path_is_file(Range<char8> path) noexcept
{
	if (path.count() > PATH_MAX)
		return false;

	char8 terminated_path[PATH_MAX + 1];

	memcpy(terminated_path, path.begin(), path.count());

	terminated_path[path.count()] = '\0';

	struct stat info;

	if (stat(terminated_path, &info) != 0)
		return false;

	return S_ISREG(info.st_mode);
}

static u64 remove_last_path_elem(MutRange<char8> out_buf, u64 out_index) noexcept
{
	if (out_index <= 1)
		return 0;

	ASSERT_OR_IGNORE(out_buf[0] == '/');

	out_index -= 1;

	while (out_buf[out_index] != '/')
		out_index -= 1;

	return out_index;
}

static u32 append_relative_path(Range<char8> path, MutRange<char8> out_buf, u64 out_index) noexcept
{
	bool is_element_start = true;

	for (u64 i = 0; i != path.count(); ++i)
	{
		if (path[i] == '/')
		{
			is_element_start = true;
		}
		else if (is_element_start && path[i] == '.')
		{
			if (i + 1 == path.count() || path[i + 1] == '/')
			{
				i += 1;

				continue;
			}
			else if (i + 2 == path.count() || (i + 2 < path.count() && path[i + 1] == '.' && path[i + 2] == '/'))
			{
				out_index = remove_last_path_elem(out_buf, out_index);

				if (out_index == 0)
					return 0;

				i += 2;

				continue;
			}
		}

		if (is_element_start)
		{
			if (out_index == out_buf.count())
				return 0;

			is_element_start = false;

			out_buf[out_index] = '/';

			out_index += 1;
		}

		if (out_index == out_buf.count())
			return 0;

		out_buf[out_index] = path[i];

		out_index += 1;
	}

	if (out_index > 1 && out_buf[out_index - 1] == '/')
		out_index -= 1;

	return out_index;
}

u32 minos::path_to_absolute(Range<char8> path, MutRange<char8> out_buf) noexcept
{
	if (path.count() > 0 && path[0] == '/')
	{
		if (path.count() < out_buf.count())
			memcpy(out_buf.begin(), path.begin(), path.count());
		
		return path.count();
	}

	if (getcwd(out_buf.begin(), out_buf.count()) == nullptr)
		return 0;

	u64 out_index = 0;

	while (out_buf[out_index] != '\0')
		out_index += 1;
	
	return append_relative_path(path, out_buf, out_index);
}

u32 minos::path_to_absolute_relative_to(Range<char8> path, Range<char8> base, MutRange<char8> out_buf) noexcept
{
	if (path.count() != 0 && path[0] == '/')
	{
		if (path.count() <= out_buf.count())
			memcpy(out_buf.begin(), path.begin(), path.count());

		return path.count();
	}

	const u64 out_index = path_to_absolute(base, out_buf);

	if (out_index == 0 || out_index > out_buf.count())
		return 0;

	return append_relative_path(path, out_buf, out_index);
}

u32 minos::path_to_absolute_directory(Range<char8> path, MutRange<char8> out_buf) noexcept
{
	const u64 out_index = path_to_absolute(path, out_buf);

	if (out_index == 0 || out_index > out_buf.count())
		return 0;

	return remove_last_path_elem(out_buf, out_index);
}

bool minos::path_get_info(Range<char8> path, FileInfo* out) noexcept
{
	if (path.count() > PATH_MAX)
		return false;

	char8 terminated_path[PATH_MAX + 1];

	memcpy(terminated_path, path.begin(), path.count());

	terminated_path[path.count()] = '\0';

	struct stat info;

	if (stat(terminated_path, &info) != 0)
		return false;

	out->identity.volume_serial = info.st_dev;
	out->identity.index = info.st_ino;
	out->bytes = info.st_size;
	out->creation_time = 0; // This is not supported under *nix
	out->last_modified_time = info.st_mtime; // Use mtime instead of ctime, as metadata changes likely do not matter (?)
	out->last_access_time = info.st_atime;
	out->is_directory = S_ISDIR(info.st_mode);

	return true;
}

u64 minos::timestamp_utc() noexcept
{
	const time_t t = time(nullptr);

	if (t == static_cast<time_t>(-1))
		panic("time failed (0x%X - %s)\n", last_error(), strerror(last_error()));
		
	return t;
}

u64 minos::timestamp_ticks_per_second() noexcept
{
	return 1;
}

u64 minos::exact_timestamp() noexcept
{
	timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		panic("clock_gettime failed (0x%X - %s)\n", last_error(), strerror(last_error()));

	return static_cast<u64>(ts.tv_nsec) + static_cast<u64>(ts.tv_sec) * static_cast<u64>(1'000'000'000);
}

u64 minos::exact_timestamp_ticks_per_second() noexcept
{
	timespec ts;

	if (clock_getres(CLOCK_MONOTONIC, &ts) != 0)
		panic("clock_getres failed (0x%X - %s)\n", last_error(), strerror(last_error()));

	ASSERT_OR_IGNORE(ts.tv_sec == 0);

	return static_cast<u64>(1'000'000'000) / static_cast<u64>(ts.tv_nsec);
}

// TODO: Remove
#if COMPILER_CLANG
	#pragma clang diagnostic pop
#elif COMPILER_GCC
#pragma GCC diagnostic pop
#else
	#error("Unknown compiler")
#endif

#endif
