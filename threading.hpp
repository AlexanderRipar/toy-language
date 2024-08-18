#ifndef THREADING_INCLUDE_GUARD
#define THREADING_INCLUDE_GUARD

#include "minos.hpp"
#include "memory.hpp"
#include <atomic>

#pragma warning(push)
#pragma warning(disable : 4324) // structure was padded due to alignment specifier

struct Mutex
{
private:

	std::atomic<u16> m_rep;

public:

	void init() noexcept
	{
		m_rep.store(0, std::memory_order_relaxed);
	}

	void acquire(u32 spin_count = 0) noexcept
	{
		while (true)
		{
			u16 rep = m_rep.load(std::memory_order_relaxed);
			
			if ((rep & 1) == 0)
			{
				if (m_rep.compare_exchange_weak(rep, rep | 1, std::memory_order_acquire))
					return;
			}

			rep = m_rep.fetch_add(2, std::memory_order_relaxed) + 2;

			while (true)
			{
				if ((rep & 1) == 0)
				{
					if (m_rep.compare_exchange_weak(rep, rep - 1, std::memory_order_acquire))
						return;
				}

				rep = m_rep.load(std::memory_order_relaxed);

				if (spin_count == 0)
					break;

				spin_count -= 1;
			}

			minos::address_wait(&m_rep, &rep, sizeof(m_rep));
		}
	}

	void release() noexcept
	{
		if (m_rep.fetch_sub(1, std::memory_order_release) == 1)
			return;

		minos::address_wake_single(&m_rep);
	}
};

template<typename T, u32 MEMBER_ALIGNMENT = minos::CACHELINE_BYTES>
struct alignas(MEMBER_ALIGNMENT) ThreadsafeRingBufferHeader
{
private:

	static_assert(MEMBER_ALIGNMENT >= sizeof(u32) && is_pow2(MEMBER_ALIGNMENT));

	static constexpr u32 SEQUENCE_SHIFT = 40;

	static constexpr u32 COMPLETED_SHIFT = 20;

	static constexpr u32 PENDING_SHIFT = 0;


	static constexpr u32 SEQUENCE_BITS = 24;

	static constexpr u32 COMPLETED_BITS = 20;

	static constexpr u32 PENDING_BITS = 20;


	static constexpr u64 SEQUEUNCE_MASK = ((1ui64 << SEQUENCE_BITS) - 1) << SEQUENCE_SHIFT;

	static constexpr u64 COMPLETED_MASK = ((1ui64 << COMPLETED_BITS) - 1) << COMPLETED_SHIFT;

	static constexpr u64 PENDING_MASK = ((1ui64 << PENDING_BITS) - 1) << PENDING_SHIFT;


	static constexpr u64 SEQUENCE_ONE = 1ui64 << SEQUENCE_SHIFT;

	static constexpr u64 COMPLETED_ONE = 1ui64 << COMPLETED_SHIFT;

	static constexpr u64 PENDING_ONE = 1ui64 << PENDING_SHIFT;



	std::atomic<u64> alignas(MEMBER_ALIGNMENT) m_enqueue;

	std::atomic<u64> alignas(MEMBER_ALIGNMENT) m_dequeue;

public:

	void init() noexcept
	{
		m_enqueue.store(0, std::memory_order_relaxed);

		m_dequeue.store(0, std::memory_order_relaxed);
	}

	bool enqueue(T* queue, u32 capacity, T entry, u32* opt_out_sequence = nullptr) noexcept
	{
		ASSERT_OR_IGNORE(is_pow2(capacity));

		u64 enqueue_all = m_enqueue.load(std::memory_order_relaxed);

		while (true)
		{
			u64 dequeue_all = m_dequeue.load(std::memory_order_relaxed);

			const u64 dequeue_seq = (dequeue_all & SEQUEUNCE_MASK) >> SEQUENCE_SHIFT;

			const u64 enqueue_seq = (enqueue_all & SEQUEUNCE_MASK) >> SEQUENCE_SHIFT;

			const u64 enqueue_pending = (enqueue_all & PENDING_MASK) >> PENDING_SHIFT;

			const u64 insertion_seq = enqueue_seq + enqueue_pending;

			// Queue is full; No insertion possible
			if (insertion_seq == dequeue_seq + capacity)
				return false;

			ASSERT_OR_IGNORE(enqueue_pending != (PENDING_MASK >> PENDING_SHIFT));

			// Acquire slot by incrementing pending count
			if (!m_enqueue.compare_exchange_strong(enqueue_all, enqueue_all + PENDING_ONE, std::memory_order_acq_rel))
				continue;

			const u64 insertion_index = insertion_seq & (capacity - 1);

			queue[insertion_index] = entry;

			if (opt_out_sequence != nullptr)
				*opt_out_sequence = static_cast<u32>(insertion_seq);

			// If we think it's likely that there are no other running
			// enqueues, try to fast-path updating m_enqueue to publish our
			// operation
			if (enqueue_pending == 0)
			{
				u64 enqueue_expected = enqueue_all + PENDING_ONE;

				if (m_enqueue.compare_exchange_strong(enqueue_expected, (enqueue_seq + 1) << SEQUENCE_SHIFT, std::memory_order_release))
					return true;
			}

			// There are other ongoing enqueues; Increment the number of
			// completed enqueues, and, if it matches the number of pending
			// ones, reset both and add it to the sequence
			u64 published = m_enqueue.fetch_add(COMPLETED_ONE, std::memory_order_relaxed) + COMPLETED_ONE;

			const u64 published_seq = (published & SEQUEUNCE_MASK) >> SEQUENCE_SHIFT;

			const u64 published_completed = (published & COMPLETED_MASK) >> COMPLETED_SHIFT;

			const u64 published_pending = (published & PENDING_MASK) >> PENDING_SHIFT;

			if (published_completed == published_pending)
				m_enqueue.compare_exchange_strong(published, (published_seq + published_completed) << SEQUENCE_SHIFT, std::memory_order_release);

			return true;
		}
	}

	bool dequeue(T* queue, u32 capacity, T* out) noexcept
	{
		ASSERT_OR_IGNORE(is_pow2(capacity));

		u64 dequeue_all = m_dequeue.load(std::memory_order_relaxed);

		while (true)
		{
			const u64 enqueue_all = m_enqueue.load(std::memory_order_relaxed);

			const u64 enqueue_seq = (enqueue_all & SEQUEUNCE_MASK) >> SEQUENCE_SHIFT;

			const u64 dequeue_seq = (dequeue_all & SEQUEUNCE_MASK) >> SEQUENCE_SHIFT;

			const u64 dequeue_pending = (dequeue_all & PENDING_MASK) >> PENDING_SHIFT;

			const u64 retrieval_seq = dequeue_seq + dequeue_pending;

			// Queue is empty; Nothing to dequeue
			if (retrieval_seq == enqueue_seq)
				return false;

			ASSERT_OR_IGNORE(dequeue_pending != (PENDING_MASK >> PENDING_SHIFT));

			if (!m_dequeue.compare_exchange_strong(dequeue_all, dequeue_all + PENDING_ONE, std::memory_order_acq_rel))
				continue;

			const u64 retrieval_index = retrieval_seq & (capacity - 1);

			*out = queue[retrieval_index];

			if ((dequeue_all & PENDING_MASK) == 0)
			{
				u64 dequeue_expected = dequeue_all + PENDING_ONE;

				if (m_dequeue.compare_exchange_strong(dequeue_expected, (dequeue_seq + 1) << SEQUENCE_SHIFT, std::memory_order_release))
					return true;
			}

			u64 published = m_dequeue.fetch_add(COMPLETED_ONE, std::memory_order_relaxed) + COMPLETED_ONE;

			const u64 published_seq = (published & SEQUEUNCE_MASK) >> SEQUENCE_SHIFT;

			const u64 published_completed = (published & COMPLETED_MASK) >> COMPLETED_SHIFT;

			const u64 published_pending = (published & PENDING_MASK) >> PENDING_SHIFT;

			if (published_completed == published_pending)
				(void) m_dequeue.compare_exchange_strong(published, (published_seq + published_completed) << SEQUENCE_SHIFT, std::memory_order_release);

			return true;
		}
	}
};

template<typename T, u32 NEXT_FIELD_OFFSET>
struct ThreadsafeIndexStackListHeader
{
private:

	std::atomic<u64> m_all;

public:

	void init() noexcept
	{
		m_all.store(0x0000'0000'FFFF'FFFF, std::memory_order_relaxed);
	}

	void init(T* begin, u32 count) noexcept
	{
		if (count == 0)
		{
			init();

			return;
		}

		for (u32 i = 0; i != count - 1; ++i)
			*reinterpret_cast<u32*>(reinterpret_cast<byte*>(begin + i) + NEXT_FIELD_OFFSET) = i + 1;

		*reinterpret_cast<u32*>(reinterpret_cast<byte*>(begin + count - 1) + NEXT_FIELD_OFFSET) = ~0u;

		m_all.store(0, std::memory_order_relaxed);
	}

	T* pop(T* begin) noexcept
	{
		u64 all = m_all.load();

		while (true)
		{
			const u32 head = static_cast<u32>(all);

			if (head == ~0u)
				return nullptr;

			const u32* const next_ptr = reinterpret_cast<u32*>(reinterpret_cast<byte*>(begin + head) + NEXT_FIELD_OFFSET);

			const u32 next = *next_ptr;

			const u64 new_all = (all ^ next ^ head) + (1ui64 << 32);

			if (m_all.compare_exchange_weak(all, new_all))
				return begin + head;
		}
	}

	bool push(T* begin, u32 index) noexcept
	{
		u32* const next_ptr = reinterpret_cast<u32*>(reinterpret_cast<byte*>(begin + index) + NEXT_FIELD_OFFSET);

		u64 all = m_all.load();

		while (true)
		{
			const u32 head = static_cast<u32>(all);

			*next_ptr = head;

			const u64 new_all = all ^ index ^ head;

			if (m_all.compare_exchange_weak(all, new_all))
				return head == ~0u;
		}
	}

	T* pop_unsafe(T* begin) noexcept
	{
		const u64 all = m_all.load(std::memory_order_relaxed);

		const u32 head = static_cast<u32>(all);

		if (head == ~0u)
			return nullptr;

		const u32* const next_ptr = reinterpret_cast<u32*>(reinterpret_cast<byte*>(begin + head) + NEXT_FIELD_OFFSET);

		const u32 next = *next_ptr;

		m_all.store(all ^ next ^ head, std::memory_order_relaxed);

		return begin + head;
	}

	bool push_unsafe(T* begin, u32 index) noexcept
	{
		const u64 all = m_all.load(std::memory_order_relaxed);

		const u32 head = static_cast<u32>(all);

		u32* const next_ptr = reinterpret_cast<u32*>(reinterpret_cast<byte*>(begin + index) + NEXT_FIELD_OFFSET);

		*next_ptr = head;

		m_all.store(all ^ index ^ head, std::memory_order_relaxed);

		return head == ~0u;
	}
};

template<typename T, u32 NEXT_FIELD_OFFSET>
struct ThreadsafeStridedIndexStackListHeader
{
private:

	std::atomic<u64> m_all;

public:

	void init() noexcept
	{
		m_all.store(0x0000'0000'FFFF'FFFFui64, std::memory_order_relaxed);
	}

	void init(void* begin, u32 stride, u32 count) noexcept
	{
		if (count == 0)
		{
			init();

			return;
		}

		for (u32 i = 0; i != count - 1; ++i)
			*reinterpret_cast<u32*>(static_cast<byte*>(begin) + static_cast<u64>(stride) * i + NEXT_FIELD_OFFSET) = i + 1;

		*reinterpret_cast<u32*>(static_cast<byte*>(begin) + static_cast<u64>(stride) * (count - 1) + NEXT_FIELD_OFFSET) = ~0u;

		m_all.store(0, std::memory_order_relaxed);
	}

	T* pop(void* begin, u32 stride) noexcept
	{
		u64 all = m_all.load(std::memory_order_consume);

		while (true)
		{
			const u32 head = static_cast<u32>(all);

			if (head == ~0u)
				return nullptr;

			const u32* next_ptr = reinterpret_cast<u32*>(static_cast<byte*>(begin) + static_cast<u64>(head) * stride + NEXT_FIELD_OFFSET);

			const u32 next = *next_ptr;

			const u64 new_all = (all ^ next ^ head) + (1ui64 << 32);

			if (m_all.compare_exchange_weak(all, new_all, std::memory_order_acquire))
				return reinterpret_cast<T*>(static_cast<byte*>(begin) + static_cast<u64>(head) * stride);
		}
	}

	bool push(void* begin, u32 stride, u32 index) noexcept
	{
		u32* const new_next_ptr = reinterpret_cast<u32*>(static_cast<byte*>(begin) + static_cast<u64>(stride) * index + NEXT_FIELD_OFFSET);

		u64 all = m_all.load(std::memory_order_relaxed);

		while (true)
		{
			const u32 head = static_cast<u32>(all);

			*new_next_ptr = head;

			const u64 new_all = all ^ index ^ head;

			if (m_all.compare_exchange_weak(all, new_all, std::memory_order_release))
				return head == ~0u;
		}
	}

	T* pop_unsafe(void* begin, u32 stride) noexcept
	{
		u64 all = m_all.load(std::memory_order_relaxed);

		const u32 head = static_cast<u32>(all);

		if (head == 0xFFFF'FFFF)
			return nullptr;

		const u32* next_ptr = reinterpret_cast<u32*>(static_cast<byte*>(begin) + static_cast<u64>(head) * stride + NEXT_FIELD_OFFSET);

		const u32 next = *next_ptr;

		const u64 new_all = (all ^ next ^ head) + (1ui64 << 32);

		m_all.store(new_all, std::memory_order_relaxed);

		return reinterpret_cast<T*>(static_cast<byte*>(begin) + static_cast<u64>(head) * stride);
	}

	bool push_unsafe(void* begin, u32 stride, u32 index) noexcept
	{
		u32* const new_next_ptr = reinterpret_cast<u32*>(static_cast<byte*>(begin) + static_cast<u64>(stride) * index + NEXT_FIELD_OFFSET);

		u64 all = m_all.load(std::memory_order_relaxed);

		const u32 head = static_cast<u32>(all);
		
		*new_next_ptr = head;

		const u64 new_all = all ^ index ^ head;

		m_all.store(new_all, std::memory_order_relaxed);

		return head == ~0u;
	}
};

template<typename K, typename V>
struct alignas(minos::CACHELINE_BYTES) ThreadsafeMap2
{
public:

	struct InitInfo
	{
		// Number of distinct threads that will be accessing this map.
		// Each of these threads must be associated with a unique id in the
		// range [0..thread_count), which is passed to methods working on this
		// map. thread_count may not be 0.
		u32 thread_count;

		struct MapInitInfo
		{
			// Maximum size of the map's lookup array.
			// This must be a power of two that is greater than or equal to
			// half the system's page size in bytes and greater than or equal
			// to map.initial_commit_count.
			u32 reserve_count;

			// Initial size of the map's lookup array.
			// This must be a power of two that is greater than or equal to
			// half the system's page size in bytes and less than or equal to
			// map.reserve_count.
			u32 initial_commit_count;

			// Upper bound on the number of map entries that may be affected by
			// an insertion. If more entries are affected, a rehash is
			// triggered.
			// This is *not* equivalent to the entry's probe sequence length,
			// since multiple map entries may be moved during an insertion.
			// This must be at least 64.
			// Inserting may actually rehash before this threshold is reached
			// if the map is suffiently small. 
			u32 max_insertion_distance;
		} map;

		struct StoreInitInfo
		{
			// Maximum number of strides that can be held by the map's backing
			// store.
			// This must be a non-zero multiple of
			// store.per_thread_commit_increment_strides and must be greater
			// than or equal to
			// store.per_thread_initial_commit_strides * thread_count.
			u32 reserve_strides;

			// Initial number of strides assigned to each thread.
			// This must be a non-zero multiple of
			// store.per_thread_commit_increment_strides
			u32 per_thread_initial_commit_strides;

			// Number of strides that are added to a thread's claimed store
			// when it runs out of store.
			// This must be a non-zero multiple of the number of strides that
			// fit into a page on the current system.
			u32 per_thread_commit_increment_strides;
		} store;
	};

private:

	struct alignas(minos::CACHELINE_BYTES) PerThreadData
	{
		// Index of the first unused stride of the current store allocation.
		// Initially 0.
		u32 allocation_curr_stride;

		// Index one past the last usable stride of the current store
		// allocation. Initially 0.
		u32 allocation_end_stride;

		// Start of the first store allocation made for this thread.
		// Used as get_store()[head_stride * STRIDE]
		// Initially ~0u.
		u32 head_stride;

		std::atomic<u32> write_lock;
	};

	static constexpr u32 PSL_BITS =	6;

	static constexpr u16 PSL_MASK = (1 << PSL_BITS) - 1;

	static constexpr u32 STRIDE = V::stride();

	static constexpr u32 MAP_CACHELINE_MASK = (minos::CACHELINE_BYTES / sizeof(u16)) - 1;

	static constexpr u16 MAP_CACHELINE_LOCK_BIT = 0x8000;



	u16* m_map;

	u32* m_indirections;

	void* m_store;

	PerThreadData* m_thread_data;

	u32 m_thread_count;

	u32 m_map_reserved_count;

	u32 m_store_reserved_strides;

	u32 m_max_checked_cacheline_count;

	u32 m_store_commit_increment_strides;

	std::atomic<u32> m_map_committed_count;

	std::atomic<u32> alignas(minos::CACHELINE_BYTES) m_store_committed_strides;

	std::atomic<u32> m_rehash_lock;

	std::atomic<u32> m_awaited_write_count;



	static_assert(is_pow2(STRIDE));



	static u16 make_map_entry(u32 hash) noexcept
	{
		const u16 entry = (hash >> 16) & ~PSL_MASK;

		return entry == 0 ? 0x8000 : entry;
	}

	void insert_exclusive(u16* map, u32* indirections, u32 mask, u32 indirection_to_insert, u16 entry_to_insert, u32 insert_index) noexcept
	{
		while (true)
		{
			const u16 existing_entry = map[insert_index];

			ASSERT_OR_IGNORE(indirection_to_insert != 0);

			if (existing_entry == 0)
			{
				map[insert_index] = entry_to_insert;

				indirections[insert_index] = indirection_to_insert;

				return;
			}
			else if ((existing_entry & PSL_MASK) < (entry_to_insert & PSL_MASK))
			{
				const u32 existing_indirection = indirections[insert_index];

				indirections[insert_index] = indirection_to_insert;

				indirection_to_insert = existing_indirection;

				map[insert_index] = entry_to_insert;

				entry_to_insert = existing_entry;
			}

			if (insert_index == mask)
				insert_index = 1;
			else
				insert_index += 1 + ((insert_index & MAP_CACHELINE_MASK) == MAP_CACHELINE_MASK);

			entry_to_insert += 1;
		}
	}

	void rehash_exclusive() noexcept
	{
		const u32 old_map_committed_count = m_map_committed_count.load(std::memory_order_relaxed);

		ASSERT_OR_EXIT(old_map_committed_count != m_map_reserved_count);

		u16* const map = m_map;

		u32* const indirections = m_indirections;

		memset(map, 0, old_map_committed_count * sizeof(*map));

		ASSERT_OR_EXIT(minos::commit(map + old_map_committed_count, old_map_committed_count * sizeof(*map)));

		ASSERT_OR_EXIT(minos::commit(indirections + old_map_committed_count, old_map_committed_count * sizeof(*indirections)));

		const u32 new_map_committed_count = old_map_committed_count * 2;

		m_map_committed_count.store(new_map_committed_count, std::memory_order_relaxed);

		const u32 mask = new_map_committed_count - 1;

		for (u32 i = 0; i != m_thread_count; ++i)
		{
			u32 store_index = m_thread_data[i].head_stride;

			while (store_index != ~0u)
			{
				const V* const value = value_from(store_index);

				const u32 hash = value->get_hash();

				insert_exclusive(map, indirections, new_map_committed_count - 1, store_index, make_map_entry(hash), (hash & mask) + ((hash & MAP_CACHELINE_MASK) == 0));

				store_index = value->get_next();
			}
		}
	}

	bool try_acquire_rehash_lock() noexcept
	{
		u32 old_rehash_lock = m_rehash_lock.exchange(1, std::memory_order_acquire);

		if (old_rehash_lock != 0)
		{
			do
			{
				minos::address_wait(&m_rehash_lock, &old_rehash_lock, sizeof(m_rehash_lock));

				old_rehash_lock = m_rehash_lock.load(std::memory_order_relaxed);
			}
			while (old_rehash_lock != 0);

			return false;
		}

		m_awaited_write_count.store(0, std::memory_order_release);

		u32 active_write_count = 0;

		for (u32 i = 0; i != m_thread_count; ++i)
		{
			if (m_thread_data[i].write_lock.exchange(2, std::memory_order_acquire) != 0)
				active_write_count += 1;
		}

		u32 pending_write_count = m_awaited_write_count.fetch_add(active_write_count, std::memory_order_relaxed) + active_write_count;

		while (pending_write_count > 0)
		{
			ASSERT_OR_IGNORE(static_cast<s32>(pending_write_count) > 0);

			minos::address_wait(&m_awaited_write_count, &pending_write_count, sizeof(m_awaited_write_count));

			pending_write_count = m_awaited_write_count.load(std::memory_order_relaxed);
		}

		return true;
	}

	void release_rehash_lock() noexcept
	{
		for (u32 i = 0; i != m_thread_count; ++i)
			m_thread_data[i].write_lock.store(0, std::memory_order_relaxed);

		m_rehash_lock.store(0, std::memory_order_release);

		minos::address_wake_all(&m_rehash_lock);
	}

	void acquire_thread_write_lock(PerThreadData* thread_data) noexcept
	{
		// Acquire our thread's write lock. If it is currently taken by a
		// rehash, wait for that rehash to complete.
		while (true)
		{
			const u32 old_write_lock = thread_data->write_lock.exchange(1, std::memory_order_acquire);

			ASSERT_OR_IGNORE(old_write_lock == 0 || old_write_lock == 2);

			if (old_write_lock == 0)
				return;

			u32 rehash_lock = m_rehash_lock.load(std::memory_order_relaxed);

			while (rehash_lock != 0)
			{
				minos::address_wait(&m_rehash_lock, &rehash_lock, sizeof(m_rehash_lock));

				rehash_lock = m_rehash_lock.load(std::memory_order_relaxed);
			}
		}
	}

	void release_thread_write_lock(PerThreadData* thread_data) noexcept
	{
		const u32 old_write_lock = thread_data->write_lock.exchange(0, std::memory_order_release);

		ASSERT_OR_IGNORE(old_write_lock == 1 || old_write_lock == 2);

		if (old_write_lock != 1)
		{
			thread_data->write_lock.store(2, std::memory_order_relaxed);

			if (m_awaited_write_count.fetch_sub(1, std::memory_order_relaxed) - 1 == 0)
				minos::address_wake_single(&m_awaited_write_count);
		}
	}

	void acquire_map_cacheline_lock(u16* map, u32 index) noexcept
	{
		std::atomic<u16>* const lock = reinterpret_cast<std::atomic<u16>*>(map) + index;

		u16 old_lock = 0;

		if (lock->compare_exchange_strong(old_lock, MAP_CACHELINE_LOCK_BIT, std::memory_order_acquire))
			return;

		old_lock = lock->fetch_add(1, std::memory_order_relaxed);

		while (true)
		{
			while ((old_lock & MAP_CACHELINE_LOCK_BIT) == 0)
			{
				if (lock->compare_exchange_strong(old_lock, (old_lock - 1) | MAP_CACHELINE_LOCK_BIT, std::memory_order_acquire))
					return;
			}

			minos::address_wait(lock, &old_lock, sizeof(old_lock));

			old_lock = lock->load(std::memory_order_relaxed);
		}
	}

	void release_map_cacheline_locks(u16* map, u32 mask, u32 first_index, u32 last_index) noexcept
	{
		u32 index = last_index;

		while (true)
		{
			std::atomic<u16>* const lock = reinterpret_cast<std::atomic<u16>*>(map) + index;
	
			const u16 wait_count = lock->fetch_and(static_cast<u16>(~MAP_CACHELINE_LOCK_BIT)) & static_cast<u16>(~MAP_CACHELINE_LOCK_BIT);

			if (wait_count != 0)
				minos::address_wake_single(lock);

			if (index == first_index)
				return;

			index = (index - (MAP_CACHELINE_MASK + 1)) & mask;
		}
	}

	V* store_key(PerThreadData* thread_data, K key, u32 key_hash) noexcept
	{
		const u32 required_strides = V::get_required_strides(key);

		u32 allocation_curr_stride = thread_data->allocation_curr_stride;

		if (thread_data->allocation_end_stride < allocation_curr_stride + required_strides)
		{
			const u32 requested_strides = next_multiple(required_strides, m_store_commit_increment_strides);

			allocation_curr_stride = m_store_committed_strides.fetch_add(requested_strides);

			ASSERT_OR_EXIT(allocation_curr_stride + requested_strides <= m_store_reserved_strides);

			ASSERT_OR_EXIT(minos::commit(reinterpret_cast<byte*>(m_store) + allocation_curr_stride * STRIDE, requested_strides * STRIDE));

			thread_data->allocation_curr_stride = allocation_curr_stride + required_strides;

			thread_data->allocation_end_stride = allocation_curr_stride + requested_strides;
		}
		else
		{
			thread_data->allocation_curr_stride = allocation_curr_stride + required_strides;
		}

		V* value = value_from(allocation_curr_stride);

		value->init(key, key_hash);

		value->set_next(thread_data->head_stride);

		thread_data->head_stride = allocation_curr_stride;

		return value;
	}

	void rehash() noexcept
	{
		if (!try_acquire_rehash_lock())
		{
			return;
		}

		rehash_exclusive();

		release_rehash_lock();
	}

	V* find(K key, u32 key_hash) noexcept
	{
		const u16* const map = m_map;

		const u32* const indirections = m_indirections;

		const u32 mask = m_map_committed_count.load(std::memory_order_relaxed) - 1;

		u32 index = (key_hash & mask) + ((key_hash & MAP_CACHELINE_MASK) == 0);

		u16 expected_entry = make_map_entry(key_hash); 

		while (true)
		{
			if (const u16 entry = map[index]; entry == expected_entry)
			{
				const u32 indirection = indirections[index];

				if (indirection == 0)
					continue;

				V* const value = value_from(indirection);

				if (value->equal_to_key(key, key_hash))
					return value;
			}
			else if (entry == 0 || (expected_entry & PSL_MASK) >= (entry & PSL_MASK))
			{
				return nullptr;
			}

			ASSERT_OR_EXIT((expected_entry & PSL_MASK) != PSL_MASK);

			expected_entry += 1;

			if (index == mask)
				index = 1;
			else
				index += 1 + ((index & MAP_CACHELINE_MASK) == MAP_CACHELINE_MASK);
		}
	}

	V* insert(PerThreadData* thread_data, K key, u32 key_hash, bool& is_new) noexcept
	{
	RETRY:

		acquire_thread_write_lock(thread_data);

		const u32 map_committed_count = m_map_committed_count.load(std::memory_order_relaxed);

		const u32 mask = map_committed_count - 1;

		const u32 max_checked_cacheline_count = map_committed_count / 4 < m_max_checked_cacheline_count ? map_committed_count / 2 : m_max_checked_cacheline_count;

		u32 checked_cacheline_count = 0;

		u16* const map = m_map;

		u32* const indirections = m_indirections;

		const u32 initial_index = (key_hash & mask) + ((key_hash & MAP_CACHELINE_MASK) == 0);

		acquire_map_cacheline_lock(map, initial_index & ~MAP_CACHELINE_MASK);

		const u16 initial_entry = make_map_entry(key_hash);

		u32 find_index = initial_index;

		u16 entry_to_find = initial_entry;

		// Take all locks needed for inserting and check entry has not been inserted yet.
		while (true)
		{
			const u16 existing_entry = map[find_index];

			if (existing_entry == entry_to_find)
			{
				const u32 indirection = indirections[find_index];

				ASSERT_OR_IGNORE(indirection != 0);

				V* value = value_from(indirection);

				if (value->equal_to_key(key, key_hash))
				{
					// Since we haven't yet locked the current index, we must
					// only unlock up to the previous index
					const u32 last_locked_index = ((find_index - 1) & mask) & ~MAP_CACHELINE_MASK;

					release_map_cacheline_locks(map, mask, initial_index & ~MAP_CACHELINE_MASK, last_locked_index);

					release_thread_write_lock(thread_data);

					is_new = false;

					return value;
				}
			}
			else if (existing_entry == 0)
			{
				break;
			}
			else if ((existing_entry & PSL_MASK) < (entry_to_find & PSL_MASK))
			{
				entry_to_find = existing_entry;
			}

			if (find_index == mask)
				find_index = 0;
			else
				find_index += 1;

			if ((find_index & MAP_CACHELINE_MASK) == 0)
			{
				acquire_map_cacheline_lock(map, find_index);

				checked_cacheline_count += 1;

				find_index += 1;
			}

			if ((entry_to_find & PSL_MASK) == PSL_MASK || checked_cacheline_count == max_checked_cacheline_count)
			{
				release_map_cacheline_locks(map, mask, initial_index & ~MAP_CACHELINE_MASK, find_index & ~MAP_CACHELINE_MASK);

				release_thread_write_lock(thread_data);

				rehash();

				goto RETRY;
			}

			entry_to_find += 1;
		}

		// Relevant region is locked and does not contain searched value; Create it in the store and insert it.
		V* value = store_key(thread_data, key, key_hash);

		insert_exclusive(map, indirections, mask, index_from(value), initial_entry, initial_index);

		release_map_cacheline_locks(map, mask, initial_index & ~MAP_CACHELINE_MASK, find_index & ~MAP_CACHELINE_MASK);

		release_thread_write_lock(thread_data);

		is_new = true;

		return value;
	}

	V* find_or_insert(u32 thread_id, K key, u32 key_hash, bool& is_new) noexcept
	{
		V* value = find(key, key_hash);

		if (value != nullptr)
		{
			is_new = false;

			return value;
		}

		PerThreadData* const thread_data = m_thread_data + thread_id;

		value = insert(thread_data, key, key_hash, is_new);

		return value;
	}

	static void check_init_info(InitInfo info) noexcept
	{
		const u32 page_bytes = minos::page_bytes();

		const u32 strides_per_page = page_bytes / STRIDE;

		ASSERT_OR_EXIT(info.thread_count != 0);


		ASSERT_OR_EXIT(is_pow2(info.map.initial_commit_count));

		ASSERT_OR_EXIT(info.map.initial_commit_count >= page_bytes / 2);


		ASSERT_OR_EXIT(is_pow2(info.map.reserve_count));

		ASSERT_OR_EXIT(info.map.reserve_count >= page_bytes / 2);

		ASSERT_OR_EXIT(info.map.reserve_count >= info.map.initial_commit_count);


		ASSERT_OR_EXIT(info.store.per_thread_commit_increment_strides != 0);

		ASSERT_OR_EXIT(info.store.per_thread_commit_increment_strides % strides_per_page == 0);


		ASSERT_OR_EXIT(info.store.per_thread_initial_commit_strides % info.store.per_thread_commit_increment_strides == 0);

		ASSERT_OR_EXIT(info.store.per_thread_initial_commit_strides >= info.store.per_thread_commit_increment_strides);


		ASSERT_OR_EXIT(info.store.reserve_strides != 0);

		ASSERT_OR_EXIT(info.store.reserve_strides % info.store.per_thread_commit_increment_strides == 0);

		ASSERT_OR_EXIT(info.store.reserve_strides >= info.store.per_thread_initial_commit_strides * info.thread_count);

		ASSERT_OR_EXIT(info.map.max_insertion_distance >= 64);
	}

public:

	static MemoryRequirements get_memory_requirements(const InitInfo& info) noexcept
	{
		check_init_info(info);

		const u64 page_mask = minos::page_bytes() - 1;

		const u64 map_bytes = static_cast<u64>(info.map.reserve_count) * 6;

		const u64 store_bytes = static_cast<u64>(info.store.reserve_strides) * STRIDE;
		
		const u64 thread_bytes = (static_cast<u64>(info.thread_count) * sizeof(PerThreadData) + page_mask) & ~page_mask;

		return { map_bytes + store_bytes + thread_bytes, static_cast<u32>(page_mask + 1) };
	}

	bool init(const InitInfo& info, byte* memory) noexcept
	{
		check_init_info(info);

		memset(this, 0, sizeof(*this));

		u64 offset = 0;

		if (!minos::commit(memory + offset, info.map.initial_commit_count * sizeof(u16)))
			return false;

		offset += info.map.reserve_count * sizeof(u16);

		if (!minos::commit(memory + offset, info.map.initial_commit_count * sizeof(u32)))
			return false;
		
		offset += info.map.reserve_count * sizeof(u32);

		if (!minos::commit(memory + offset, info.thread_count * info.store.per_thread_initial_commit_strides * STRIDE))
			return false;

		offset += info.store.reserve_strides * STRIDE;

		if (!minos::commit(memory + offset, info.thread_count * sizeof(PerThreadData)))
			return false;

		m_map = reinterpret_cast<u16*>(memory);

		m_indirections = reinterpret_cast<u32*>(m_map + info.map.reserve_count);

		// Let store point to one before its actual beginning, so that valid
		// indirections / indices are never 0.
		m_store = reinterpret_cast<byte*>(m_indirections + info.map.reserve_count) - STRIDE;

		// Correct for above adjustment to m_store by adding an additional stride. 
		m_thread_data = reinterpret_cast<PerThreadData*>(reinterpret_cast<byte*>(m_store) + info.store.reserve_strides * STRIDE + STRIDE);

		m_thread_count = info.thread_count;

		m_map_reserved_count = info.map.reserve_count;

		m_store_reserved_strides = info.store.reserve_strides + 1;

		m_max_checked_cacheline_count = (info.map.max_insertion_distance + MAP_CACHELINE_MASK) / (MAP_CACHELINE_MASK + 1);

		m_store_commit_increment_strides = info.store.per_thread_commit_increment_strides;

		m_map_committed_count.store(info.map.initial_commit_count, std::memory_order_relaxed);

		m_store_committed_strides.store(info.thread_count * info.store.per_thread_initial_commit_strides + 1, std::memory_order_relaxed);

		m_rehash_lock.store(0, std::memory_order_relaxed);

		m_awaited_write_count.store(0, std::memory_order_relaxed);

		const u32 stride_increment = info.store.per_thread_initial_commit_strides;

		for (u32 i = 0; i != info.thread_count; ++i)
		{
			m_thread_data[i].head_stride = ~0u;

			m_thread_data[i].allocation_curr_stride = i * stride_increment + 1;

			m_thread_data[i].allocation_end_stride = (i + 1) * stride_increment + 1;
		}

		return true;
	}

	u32 index_from(u32 thread_id, K key, u32 key_hash, bool* opt_is_new = nullptr) noexcept
	{
		ASSERT_OR_IGNORE(thread_id < m_thread_count);

		bool is_new;

		V* const value = find_or_insert(thread_id, key, key_hash, is_new);

		if (opt_is_new != nullptr)
			*opt_is_new = is_new;

		return index_from(value);
	}

	u32 index_from(const V* value) const noexcept
	{
		return static_cast<u32>((reinterpret_cast<const byte*>(value) - reinterpret_cast<const byte*>(m_store)) / STRIDE);
	}

	V* value_from(u32 thread_id, K key, u32 key_hash, bool* opt_is_new = nullptr) noexcept
	{
		ASSERT_OR_IGNORE(thread_id < m_thread_count);

		bool is_new;

		V* const value = find_or_insert(thread_id, key, key_hash, is_new);

		if (opt_is_new != nullptr)
			*opt_is_new = is_new;

		return value;
	}

	V* value_from(u32 index) noexcept
	{
		return reinterpret_cast<V*>(static_cast<byte*>(m_store) + index * STRIDE);
	}

	const V* value_from(u32 index) const noexcept
	{
		return reinterpret_cast<const V*>(reinterpret_cast<const byte*>(m_store) + index * STRIDE);
	}
};

#pragma warning(pop)

#endif // THREADING_INCLUDE_GUARD
