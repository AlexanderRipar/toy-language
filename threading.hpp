#ifndef THREADING_INCLUDE_GUARD
#define THREADING_INCLUDE_GUARD

#include "minos.hpp"
#include "container.hpp"
#include "memory.hpp"
#include <atomic>

#pragma warning(push)
#pragma warning(disable : 4324) // structure was padded due to alignment specifier

template<typename K, typename V>
struct ThreadsafeMap
{
private:

	static constexpr u32 PSL_BITS = 6;

	static constexpr u16 PSL_MASK = (1 << PSL_BITS) - 1;

	ReadWriteLock m_lock;

	RawExponentialBuffer<u32> m_lookup;

	RawGrowableBuffer<u32> m_values;

	u32 m_used_count;

	static u16 make_local_hash(u32 hash) noexcept
	{
		const u16 h = (hash >> 16) & ~PSL_MASK;

		return h == 0 ? 0x8000 : h;
	}

	void create_lookup_entry(const V* v) noexcept
	{
		const u32 lookup_count = m_lookup.committed_bytes() / 6;

		u16* lookups = static_cast<u16*>(m_lookup.data());

		u32* indices = reinterpret_cast<u32*>(lookups + lookup_count);

		const u32 hash = v->get_hash();

		const u32 lookup_mask = lookup_count - 1;

		u32 i = hash & lookup_mask;

		u16 lookup_to_insert = make_local_hash(hash);

		u32 index_to_insert = static_cast<u32>((reinterpret_cast<const byte*>(v) - static_cast<const byte*>(m_values.data())) / V::stride());

		while (true)
		{
			if (lookups[i] == 0)
			{
				lookups[i] = lookup_to_insert;

				indices[i] = index_to_insert;

				return; 
			}
			else if ((lookup_to_insert & PSL_MASK) > (lookups[i] & PSL_MASK))
			{
				const u16 next_lookup_to_insert = lookups[i];

				const u32 next_index_to_insert = indices[i];

				lookups[i] = lookup_to_insert;

				indices[i] = index_to_insert;

				lookup_to_insert = next_lookup_to_insert;

				index_to_insert = next_index_to_insert;
			}

			ASSERT_OR_EXIT((lookup_to_insert & PSL_MASK) != PSL_MASK);

			lookup_to_insert += 1;

			if (i == lookup_mask)
				i = 0;
			else
				i += 1;
		}
	}

	bool grow_lookup() noexcept
	{
		u32 lookup_commit = m_lookup.committed_bytes();

		if (!m_lookup.grow())
			return false;

		memset(m_lookup.data(), 0, lookup_commit);

		const V* v = static_cast<const V*>(m_values.data());

		const V* end = reinterpret_cast<const V*>(static_cast<const byte*>(m_values.data()) + m_values.used_bytes());

		while (v != end)
		{
			create_lookup_entry(v);

			v = reinterpret_cast<const V*>(reinterpret_cast<const byte*>(v) + v->get_used_bytes());
		}

		return true;
	}

	V* find_value(K key, u32 hash) noexcept
	{
		const u32 lookup_count = m_lookup.committed_bytes() / 6;

		const u16* lookups = static_cast<const u16*>(m_lookup.data());

		const u32* indices = reinterpret_cast<const u32*>(lookups + lookup_count);

		const u32 lookup_mask = lookup_count - 1;

		u16 lookup_to_find = make_local_hash(hash);

		u32 i = hash & lookup_mask;

		while (true)
		{
			const u16 curr_lookup = lookups[i];

			if (curr_lookup == lookup_to_find)
			{
				V* value = value_from(indices[i]);

				if (value->equal_to_key(key, hash))
					return value;
			}
			else if (curr_lookup == 0 || (lookup_to_find & PSL_MASK) > (curr_lookup & PSL_MASK))
			{
				return nullptr;
			}

			lookup_to_find += 1;

			if (i == lookup_mask)
				i = 0;
			else
				i += 1;
		}
	}

public:

	bool init(MemorySubregion value_memory, u32 value_commit_increment_bytes, u32 value_initial_commit_bytes, MemorySubregion lookup_memory, u32 lookup_initial_commit_count) noexcept
	{
		m_lock.init();

		if (!m_values.init(value_memory, value_commit_increment_bytes, value_initial_commit_bytes))
			goto ERROR;

		if (!m_lookup.init(lookup_memory, lookup_initial_commit_count))
			goto ERROR;

		m_used_count = 0;

		return true;

	ERROR:

		ASSERT_OR_EXECUTE(m_values.deinit());

		ASSERT_OR_EXECUTE(m_lookup.deinit());

		return false;
	}

	u32 index_from(K key, u32 hash) noexcept
	{
		const V* value = value_from(key, hash);
		
		if (value == nullptr)
			return ~0u;

		return index_from(value);
	}

	u32 index_from(const V* value) const noexcept
	{
		return static_cast<u32>((reinterpret_cast<const byte*>(value) - static_cast<const byte*>(m_values.data())) / V::stride()); 
	}

	V* value_from(K key, u32 hash) noexcept
	{
		// Optimistally assume that key is already present in the map.
		// For this case, a shared lock is sufficient, since we aren't
		// modifying anything.
		m_lock.acquire_shared();

		V* shared_find = find_value(key, hash);

		m_lock.release_shared();

		if (shared_find != nullptr)
			return shared_find;

		// Our optimistic assumption was wrong.
		// Release shared lock and acquire exclusive since we are about
		// to modify the map.
		m_lock.acquire_exclusive();

		// Check whether key was inserted while we did not hold the
		// lock. Since find_value takes this as volatile, this check
		// cannot accidentally be optimized away by reusing
		// shared_find.
		V* exclusive_find = find_value(key, hash);

		if (exclusive_find != nullptr)
		{
			m_lock.release_exclusive();

			return exclusive_find;
		}

		const u32 value_bytes = V::get_required_bytes(key);

		V* value_dst = static_cast<V*>(m_values.get_tail_ptr(value_bytes));

		if (value_dst == nullptr)
		{
			m_lock.release_exclusive();

			return nullptr;
		}

		value_dst->init(key, hash);

		// Since m_lookup never gets full, inserting into it without
		// checking whether it is full is fine. This is ensured by the
		// below check, which grows m_lookup 'prematurely', thus also
		// ensuring our load factor does not get out of hand.
		create_lookup_entry(value_dst);

		m_used_count += 1;

		if (m_used_count * 6 * 6 > m_lookup.committed_bytes() * 5)
		{
			if (!grow_lookup())
			{
				m_lock.release_exclusive();

				return nullptr;
			}
		}

		m_lock.release_exclusive();

		return value_dst;
	}

	V* value_from(u32 index) noexcept
	{
		ASSERT_OR_IGNORE(index < m_values.used_bytes() / V::stride());

		return reinterpret_cast<V*>(static_cast<byte*>(m_values.data()) + index * V::stride());
	}

	const V* value_from(u32 index) const noexcept
	{
		ASSERT_OR_IGNORE(index < m_values.used_bytes() / V::stride());

		return reinterpret_cast<const V*>(static_cast<const byte*>(m_values.data()) + index * V::stride());
	}

	u32 value_used_bytes() const noexcept
	{
		return m_values.used_bytes();
	}

	u32 value_committed_bytes() const noexcept
	{
		return m_values.committed_bytes();
	}

	u32 lookup_committed_count() const noexcept
	{
		return m_lookup.committed_bytes() / 6;
	}
	
	u32 used_count() const noexcept
	{
		return m_used_count;
	}

	u32 get_probe_seq_len_distribution(u32 probe_seq_len_counts_count, u32* out_probe_seq_len_counts) const noexcept
	{
		memset(out_probe_seq_len_counts, 0, probe_seq_len_counts_count * sizeof(*out_probe_seq_len_counts));

		const u16* lookups = static_cast<const u16*>(m_lookup.data());

		u16 max_psl = 0;

		for (u32 i = 0; i != lookup_committed_count(); ++i)
		{
			const u16 psl = lookups[i] & PSL_MASK;

			if (psl < probe_seq_len_counts_count)
				out_probe_seq_len_counts[psl] += 1;

			if (psl > max_psl)
				max_psl = psl;
		}

		return max_psl;
	}
};

template<typename K, typename V>
struct alignas(minos::CACHELINE_BYTES) ThreadsafeMap2
{
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
				V* const value = value_from(indirections[index]);

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

		const u32 mask = m_map_committed_count.load(std::memory_order_relaxed) - 1;

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
				V* value = value_from(indirections[find_index]);

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

				find_index += 1;
			}

			if ((entry_to_find & PSL_MASK) == PSL_MASK)
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

public:

	struct InitInfo
	{
		u32 thread_count;

		struct
		{
			u32 reserve_count;

			u32 initial_commit_count;
		} map;

		struct
		{
			u32 reserve_strides;

			u32 per_thread_initial_commit_strides;

			u32 per_thread_commit_increment_strides;
		} store;
	};

	// @TODO: Add checks for nonsensical inputs
	bool adjust_init_info(InitInfo* info) noexcept
	{
		const u32 page_bytes = minos::page_bytes();

		const u32 map_reserve_count = next_pow2(info->map.reserve_count, page_bytes / 2);

		const u32 map_initial_commit_count = next_pow2(info->map.initial_commit_count, page_bytes / 2);

		const u32 strides_per_page = page_bytes / STRIDE;

		const u32 store_increment_strides = next_multiple(info->store.per_thread_commit_increment_strides, strides_per_page);

		const u32 store_init_strides = next_multiple(info->store.per_thread_initial_commit_strides, store_increment_strides);

		const u32 store_reserve_strides = next_multiple(info->store.reserve_strides, store_increment_strides);

		info->map.reserve_count = map_reserve_count;

		info->map.initial_commit_count = map_initial_commit_count;

		info->store.reserve_strides = store_reserve_strides;

		info->store.per_thread_initial_commit_strides = store_init_strides;

		info->store.per_thread_commit_increment_strides = store_increment_strides;

		return true;
	}

	static u64 required_bytes(InitInfo info) noexcept
	{
		const u64 page_mask = minos::page_bytes() - 1;

		const u64 map_bytes = static_cast<u64>(info.map.reserve_count) * 6;

		const u64 store_bytes = static_cast<u64>(info.store.reserve_strides) * STRIDE;
		
		const u64 thread_bytes = (static_cast<u64>(info.thread_count) * sizeof(PerThreadData) + page_mask) & ~page_mask;

		return map_bytes + store_bytes + thread_bytes;
	}

	bool init(InitInfo info, MemorySubregion memory) noexcept
	{
		memset(this, 0, sizeof(*this));

		u64 offset = 0;

		if (!memory.commit(0, info.map.initial_commit_count * sizeof(u16)))
			return false;

		offset += info.map.reserve_count * sizeof(u16);

		if (!memory.commit(offset, info.map.initial_commit_count * sizeof(u32)))
			return false;
		
		offset += info.map.reserve_count * sizeof(u32);

		if (!memory.commit(offset, info.thread_count * info.store.per_thread_initial_commit_strides * STRIDE))
			return false;

		offset += info.store.reserve_strides * STRIDE;

		if (!memory.commit(offset, info.thread_count * sizeof(PerThreadData)))
			return false;

		m_map = static_cast<u16*>(memory.data());

		m_indirections = reinterpret_cast<u32*>(m_map + info.map.reserve_count);

		m_store = m_indirections + info.map.reserve_count;

		m_thread_data = reinterpret_cast<PerThreadData*>(reinterpret_cast<byte*>(m_store) + info.store.reserve_strides * STRIDE);

		m_thread_count = info.thread_count;

		m_map_reserved_count = info.map.reserve_count;

		m_store_reserved_strides = info.store.reserve_strides;

		m_store_commit_increment_strides = info.store.per_thread_commit_increment_strides;

		m_map_committed_count.store(info.map.initial_commit_count, std::memory_order_relaxed);

		m_store_committed_strides.store(info.thread_count * info.store.per_thread_initial_commit_strides, std::memory_order_relaxed);

		m_rehash_lock.store(0, std::memory_order_relaxed);

		m_awaited_write_count.store(0, std::memory_order_relaxed);

		const u32 stride_increment = info.store.per_thread_initial_commit_strides;

		for (u32 i = 0; i != info.thread_count; ++i)
		{
			m_thread_data[i].head_stride = ~0u;

			m_thread_data[i].allocation_curr_stride = i * stride_increment;

			m_thread_data[i].allocation_end_stride = (i + 1) * stride_increment;
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
