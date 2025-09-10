#ifndef CONTAINER_INCLUDE_GUARD
#define CONTAINER_INCLUDE_GUARD

#include <cstring>
#include <cstddef>
#include <limits>

#include "common.hpp"
#include "minos.hpp"

template<typename T, typename Index = u32>
struct ReservedVec
{
private:

	T* m_memory;

	Index m_used;

	Index m_committed;

	Index m_commit_increment;

	Index m_reserved;

	void ensure_capacity(Index extra_used) noexcept
	{
		const u64 required_commit = m_used + extra_used;

		if (required_commit <= m_committed)
			return;

		if (required_commit > m_reserved)
			panic("Could not allocate additional memory, as the required memory (%llu bytes) exceeds the reserve of %llu bytes\n", required_commit * sizeof(T), m_reserved * sizeof(T));

		const Index new_commit = next_multiple(static_cast<Index>(required_commit), m_commit_increment);

		if (!minos::mem_commit(m_memory + m_committed, (new_commit - m_committed) * sizeof(T)))
			panic("Could not allocate additional memory (%llu bytes - error 0x%X)\n", (new_commit - m_committed) * sizeof(T), minos::last_error());

		m_committed = new_commit;
	}

public:

	void init(MutRange<byte> memory, Index commit_increment) noexcept
	{
		const u32 page_bytes = minos::page_bytes();

		ASSERT_OR_IGNORE(reinterpret_cast<u64>(memory.begin()) % alignof(T) == 0 && memory.count() % sizeof(T) == 0);

		ASSERT_OR_IGNORE((reinterpret_cast<u64>(memory.begin()) & (page_bytes - 1)) == 0 && (memory.count() & (page_bytes - 1)) == 0);

		ASSERT_OR_IGNORE(memory.count() >= commit_increment * sizeof(T));

		m_memory = reinterpret_cast<T*>(memory.begin());

		if (!minos::mem_commit(m_memory, commit_increment * sizeof(T)))
			panic("Could not commit initial memory (%llu bytes - error 0x%X)\n", commit_increment * sizeof(T), minos::last_error());

		m_used = 0;

		m_committed = commit_increment;

		m_commit_increment = commit_increment;

		ASSERT_OR_IGNORE(memory.count() % sizeof(T) == 0 && memory.count() / sizeof(T) <= std::numeric_limits<Index>::max());

		m_reserved = static_cast<Index>(memory.count() / sizeof(T));
	}

	void append(const T& data) noexcept
	{
		append(&data, 1);
	}

	void append(const T* data, Index count) noexcept
	{
		ensure_capacity(count);

		memcpy(m_memory + m_used, data, count * sizeof(T));

		m_used += count;
	}

	void append_exact(const void* data, Index bytes) noexcept
	{
		ASSERT_OR_IGNORE(bytes % sizeof(T) == 0);

		const Index count = bytes / sizeof(T);

		ensure_capacity(count);

		memcpy(m_memory + m_used, data, count * sizeof(T));

		m_used += count;
	}

	void append_padded(const void* data, Index bytes) noexcept
	{
		const Index count = (bytes + sizeof(T) - 1) / sizeof(T);

		ensure_capacity(count);

		memcpy(m_memory + m_used, data, count * sizeof(T));

		m_used += count;
	}

	T* reserve() noexcept
	{
		ensure_capacity(1);

		m_used += 1;

		return m_memory + m_used - 1;
	}

	T* reserve(Index count) noexcept
	{
		ensure_capacity(count);

		m_used += count;

		return m_memory + m_used - count;
	}

	void* reserve_exact(Index bytes) noexcept
	{
		ASSERT_OR_IGNORE(bytes % sizeof(T) == 0);

		const Index count = bytes / sizeof(T);

		ensure_capacity(count);

		void* const result = m_memory + m_used;

		m_used += count;

		return result;
	}

	void* reserve_padded(Index bytes) noexcept
	{
		const Index count = (bytes + sizeof(T) - 1) / sizeof(T);

		ensure_capacity(count);

		void* const result = m_memory + m_used;

		m_used += count;

		return result;
	}

	void pad_to_alignment(u32 alignment) noexcept
	{
		static_assert(is_pow2(sizeof(T)));

		ASSERT_OR_IGNORE(is_pow2(alignment));

		if (alignment < sizeof(T))
			return;

		const u32 new_used = next_multiple(m_used, static_cast<Index>(alignment / sizeof(T)));

		ensure_capacity(new_used - m_used);

		m_used = new_used;
	}

	void reset(Index preserved_commit = std::numeric_limits<Index>::max()) noexcept
	{
		m_used = 0;

		if (preserved_commit >= m_committed)
			return;

		const u32 page_bytes = minos::page_bytes();

		const u32 target_commit = (preserved_commit + page_bytes - 1) & ~(page_bytes - 1);

		minos::mem_decommit(reinterpret_cast<byte*>(m_memory) + target_commit, m_committed - target_commit);

		m_committed = target_commit;
	}

	T& top() noexcept
	{
		ASSERT_OR_IGNORE(m_used != 0);

		return m_memory[m_used - 1];
	}

	const T& top() const noexcept
	{
		ASSERT_OR_IGNORE(m_used != 0);

		return m_memory[m_used - 1];
	}

	void pop_by(Index count) noexcept
	{
		ASSERT_OR_IGNORE(count <= m_used);

		m_used -= count;
	}

	void pop_to(Index count) noexcept
	{
		ASSERT_OR_IGNORE(count <= m_used);

		m_used = count;
	}

	void free_region(void* begin, Index count) noexcept
	{
		ASSERT_OR_IGNORE(begin >= m_memory && static_cast<byte*>(begin) + count < m_memory + m_committed);

		minos::mem_decommit(begin, count);
	}

	void free_region(void* begin, void* end) noexcept
	{
		free_region(begin, static_cast<Index>(static_cast<byte*>(end) - static_cast<byte*>(begin)));
	}

	T* begin() noexcept
	{
		return m_memory;
	}

	const T* begin() const noexcept
	{
		return m_memory;
	}

	T* end() noexcept
	{
		return m_memory + m_used;
	}

	const T* end() const noexcept
	{
		return m_memory + m_used;
	}

	Index used() const noexcept
	{
		return m_used;
	}

	Index committed() const noexcept
	{
		return m_committed;
	}

	Index reserved() const noexcept
	{
		return m_reserved;
	}
};

template<typename K, typename V>
struct IndexMap
{
private:

	static constexpr u16 LOOKUP_DISTANCE_BITS = 6;

	static constexpr u16 LOOKUP_DISTANCE_ONE = 1 << (16 - LOOKUP_DISTANCE_BITS);

	static constexpr u16 LOOKUP_DISTANCE_MASK = ((1 << LOOKUP_DISTANCE_BITS) - 1) << (16 - LOOKUP_DISTANCE_BITS);

	static constexpr u16 LOOKUP_HASH_SHIFT = 16 + LOOKUP_DISTANCE_BITS;

	static constexpr u16 LOOKUP_HASH_MASK = static_cast<u16>(~LOOKUP_DISTANCE_MASK);

	u16* m_lookups;

	u32* m_offsets;

	V* m_values;

	u32 m_lookup_used;

	u32 m_value_used;

	u32 m_lookup_commit;

	u32 m_value_commit;

	u32 m_lookup_capacity;

	u32 m_value_capacity;

	u32 m_value_commit_increment;

	static bool is_empty_lookup(u16 lookup) noexcept
	{
		return lookup == 0;
	}

	static u16 create_lookup(u32 key_hash) noexcept
	{
		const u16 lookup = static_cast<u16>(key_hash >> LOOKUP_HASH_SHIFT) & LOOKUP_HASH_MASK;

		return lookup == 0 ? 1 : lookup;
	}

	u32 create_value(K key, u32 key_hash) noexcept
	{
		const u32 value_strides = V::required_strides(key);

		const u32 required_commit = m_value_used + value_strides;

		if (required_commit > m_value_commit)
		{
			if (required_commit > m_value_capacity)
				panic("Could not insert value into IndexMap as value storage capacity of %u is exceeded by %u\n", m_value_capacity, required_commit - m_value_capacity);

			u32 new_commit = m_value_commit + m_value_commit_increment;

			while (new_commit < required_commit)
				new_commit += m_value_commit_increment;

			if (new_commit > m_value_capacity)
				new_commit = m_value_capacity;

			if (!minos::mem_commit(reinterpret_cast<byte*>(m_values) + m_value_commit * V::stride(), (new_commit - m_value_commit) * V::stride()))
				panic("Could not commit additional memory for IndexMap values (0x%X)\n", minos::last_error());

			m_value_commit = new_commit;
		}

		const u32 value_offset = m_value_used;

		V* const value = reinterpret_cast<V*>(reinterpret_cast<byte*>(m_values) + value_offset * V::stride());

		m_value_used += value_strides;

		value->init(key, key_hash);

		return value_offset;
	}

	void rehash() noexcept
	{
		if (m_lookup_commit == m_lookup_capacity)
			panic("Could not rehash IndexMap lookup as no additional capacity was available\n");

		const u32 lookup_and_offset_bytes = m_lookup_commit * (sizeof(*m_lookups) + sizeof(*m_offsets));

		if (!minos::mem_commit(reinterpret_cast<byte*>(m_lookups) + lookup_and_offset_bytes, lookup_and_offset_bytes))
			panic("Could not commit additional memory for IndexMap lookups and offsets (0x%X)\n", minos::last_error());

		memset(m_lookups, 0, m_lookup_commit * (sizeof(*m_lookups) + sizeof(*m_offsets)));

		m_lookup_commit *= 2;

		u32 offset_to_insert = 0;

		while (offset_to_insert != m_value_used)
		{
			const V* const curr_value = reinterpret_cast<V*>(reinterpret_cast<byte*>(m_values) + offset_to_insert * V::stride());

			reinsert_value_into_lookup(offset_to_insert, curr_value->hash());

			offset_to_insert += curr_value->used_strides();
		}
	}

	void reinsert_value_into_lookup(u32 offset_to_insert, u32 key_hash) noexcept
	{
		u32 index = key_hash & (m_lookup_commit - 1);

		u16 wanted_lookup = create_lookup(key_hash);

		while (true)
		{
			const u16 curr_lookup = m_lookups[index];

			if (is_empty_lookup(curr_lookup))
			{
				m_lookups[index] = wanted_lookup;

				m_offsets[index] = offset_to_insert;

				return;
			}
			else if ((curr_lookup & LOOKUP_DISTANCE_MASK) < (wanted_lookup & LOOKUP_DISTANCE_MASK))
			{
				const u32 curr_offset = m_offsets[index];

				m_lookups[index] = wanted_lookup;

				m_offsets[index] = offset_to_insert;

				wanted_lookup = curr_lookup;

				offset_to_insert = curr_offset;
			}

			if (index == m_lookup_commit - 1)
				index = 0;
			else
				index += 1;

			if ((wanted_lookup & LOOKUP_DISTANCE_MASK) == LOOKUP_DISTANCE_MASK)
				panic("Could not insert IndexMap entry, as the maximum proble sequence length was exceeded");

			wanted_lookup += LOOKUP_DISTANCE_ONE;
		}
	}

public:

	void init(u32 lookup_capacity, u32 lookup_commit, u32 value_capacity, u32 value_commit_increment) noexcept
	{
		if (!is_pow2(lookup_capacity))
			panic("Could not create IndexMap with non-power-of-two lookup capacity %u\n", lookup_capacity);

		if (!is_pow2(lookup_commit))
			panic("Could not create IndexMap with non-power-of-two initial lookup commit %u\n", lookup_commit);

		if (lookup_commit > lookup_capacity)
			panic("Could not create IndexMap with initial lookup commit %u greater than lookup capacity %u\n", lookup_commit, lookup_capacity);

		if (value_commit_increment > value_capacity)
			panic("Could not create IndexMap with initial value commit %u greater than value capacity %u\n", value_commit_increment, value_capacity);

		const u64 lookup_bytes = static_cast<u64>(lookup_capacity) * sizeof(*m_lookups);

		const u64 offset_bytes = static_cast<u64>(lookup_capacity) * sizeof(*m_offsets);

		const u64 value_bytes = static_cast<u64>(value_capacity) * V::stride();

		const u64 total_bytes = lookup_bytes + offset_bytes + value_bytes;

		byte* const mem = static_cast<byte*>(minos::mem_reserve(total_bytes));

		if (mem == nullptr)
			panic("Could not reserve %llu bytes of memory for IndexMap (0x%X)\n", total_bytes, minos::last_error());

		m_lookups = reinterpret_cast<u16*>(mem);

		m_offsets = reinterpret_cast<u32*>(mem + static_cast<u64>(lookup_commit) * sizeof(*m_lookups));

		m_values = reinterpret_cast<V*>(mem + lookup_bytes + offset_bytes);

		const u64 lookup_commit_bytes = static_cast<u64>(lookup_commit) * (sizeof(*m_lookups) + sizeof(*m_offsets));

		if (!minos::mem_commit(m_lookups, lookup_commit_bytes))
			panic("Could not commit initial %llu bytes of memory for IndexMap lookups and offsets (0x%X)\n", lookup_commit_bytes, minos::last_error());

		if (!minos::mem_commit(m_values, static_cast<u64>(value_commit_increment) * V::stride()))
			panic("Could not commit initial %llu bytes of memory for IndexMap values (0x%X)\n", static_cast<u64>(value_commit_increment) * V::stride(), minos::last_error());

		m_lookup_used = 0;

		m_value_used = 0;

		m_lookup_commit = lookup_commit;

		m_value_commit = value_commit_increment;

		m_lookup_capacity = lookup_capacity;

		m_value_capacity = value_capacity;

		m_value_commit_increment = value_commit_increment;
	}

	u32 index_from(K key, u32 key_hash) noexcept
	{
		if (m_lookup_used * 4 > m_lookup_commit * 3)
			rehash();

		u32 index = key_hash & (m_lookup_commit - 1);

		u16 wanted_lookup = create_lookup(key_hash);

		u32 offset_to_insert = 0; // Does not matter; gets overwritten anyways

		u32 new_value_offset = ~0u;

		while (true)
		{
			const u16 curr_lookup = m_lookups[index];

			if (is_empty_lookup(curr_lookup))
			{
				m_lookups[index] = wanted_lookup;

				if (new_value_offset == ~0u)
				{
					new_value_offset = create_value(key, key_hash);

					offset_to_insert = new_value_offset;
				}

				m_offsets[index] = offset_to_insert;

				m_lookup_used += 1;

				return new_value_offset;
			}
			else if (curr_lookup == wanted_lookup)
			{
				const u32 existing_value_offset = m_offsets[index];

				V* const existing_value = reinterpret_cast<V*>(reinterpret_cast<byte*>(m_values) + existing_value_offset * V::stride());

				if (existing_value->equal_to_key(key, key_hash))
					return existing_value_offset;
			}
			else if ((curr_lookup & LOOKUP_DISTANCE_MASK) < (wanted_lookup & LOOKUP_DISTANCE_MASK))
			{
				const u32 curr_offset = m_offsets[index];

				m_lookups[index] = wanted_lookup;

				if (new_value_offset == ~0u)
				{
					new_value_offset = create_value(key, key_hash);

					offset_to_insert = new_value_offset;
				}

				m_offsets[index] = offset_to_insert;

				wanted_lookup = curr_lookup;

				offset_to_insert = curr_offset;
			}

			if (index == m_lookup_commit - 1)
				index = 0;
			else
				index += 1;

			if ((wanted_lookup & LOOKUP_DISTANCE_MASK) == LOOKUP_DISTANCE_MASK)
			{
				rehash();

				return new_value_offset == ~0u ? index_from(key, key_hash) : new_value_offset;
			}

			wanted_lookup += LOOKUP_DISTANCE_ONE;
		}
	}

	u32 index_from(const V* value) const noexcept
	{
		return static_cast<u32>((reinterpret_cast<const byte*>(value) - reinterpret_cast<const byte*>(m_values)) / V::stride());
	}

	V* value_from(K key, u32 key_hash) noexcept
	{
		return value_from(index_from(key, key_hash));
	}

	V* value_from(u32 index) noexcept
	{
		ASSERT_OR_IGNORE(index < m_value_used);

		return reinterpret_cast<V*>(reinterpret_cast<byte*>(m_values) + index * V::stride());
	}

	const V* value_from(u32 index) const noexcept
	{
		ASSERT_OR_IGNORE(index < m_value_used);

		return reinterpret_cast<const V*>(reinterpret_cast<const byte*>(m_values) + index * V::stride());
	}

	void release() noexcept
	{
		const u64 lookup_bytes = static_cast<u64>(m_lookup_capacity) * sizeof(*m_lookups);

		const u64 offset_bytes = static_cast<u64>(m_lookup_capacity) * sizeof(*m_offsets);

		const u64 value_bytes = static_cast<u64>(m_value_capacity) * V::stride();

		const u64 total_bytes = lookup_bytes + offset_bytes + value_bytes;

		minos::mem_unreserve(m_lookups, total_bytes);
	}
};

template<u32 MinSizeLog2, u32 MaxSizeLog2>
struct ReservedHeap
{
private:

	static constexpr u32 CategoryCount = MaxSizeLog2 - MinSizeLog2 + 1;

	static_assert(MinSizeLog2 >= 2);

	static_assert(MaxSizeLog2 <= 31);

	static_assert(MinSizeLog2 <= MaxSizeLog2);

	void* m_memory;

	s32 m_first_frees[CategoryCount];

	u32 m_ends[CategoryCount];

	u32 m_heads[CategoryCount];

	u32 m_commit_increment_bytes[CategoryCount];

public:

	void init(MutRange<byte> memory, Range<u32> capacities, Range<u32> commits) noexcept
	{
		ASSERT_OR_IGNORE(capacities.count() == CategoryCount && commits.count() == CategoryCount);

		u64 total_size = 0;

		for (u32 i = 0; i != CategoryCount; ++i)
		{
			const u32 unit_size = static_cast<u32>(1) << (i + MinSizeLog2);

			ASSERT_OR_IGNORE(capacities[i] != 0 && is_pow2(capacities[i]));

			ASSERT_OR_IGNORE(commits[i] != 0 && commits[i] <= capacities[i] && is_pow2(commits[i]));

			const u64 curr_bytes = static_cast<u64>(unit_size) * capacities[i];

			if (curr_bytes > static_cast<u32>(INT32_MAX) || total_size + curr_bytes > static_cast<u32>(INT32_MAX))
				panic("Exceeded maximum size of ReservedHeap (2^32 - 1).\n");

			m_first_frees[i] = -1;

			m_ends[i] = static_cast<u32>((total_size + curr_bytes) / sizeof(u32));

			m_heads[i] = static_cast<u32>(total_size / sizeof(u32));

			m_commit_increment_bytes[i] = commits[i] * unit_size;

			total_size += curr_bytes;
		}

		ASSERT_OR_IGNORE(memory.count() == total_size);

		m_memory = memory.begin();
	}

	MutRange<byte> alloc(u32 bytes) noexcept
	{
		ASSERT_OR_IGNORE(bytes != 0 && bytes <= (static_cast<u32>(1) << MaxSizeLog2));

		const u8 leading_zeros = count_leading_zeros(bytes - 1);

		const u8 category = (32 - MinSizeLog2) < leading_zeros
			? 0
			: (32 - MinSizeLog2) - leading_zeros;

		const u32 alloc_size = static_cast<u32>(1) << (category + MinSizeLog2);

		s32 free_index = m_first_frees[category];

		const u32 unit_size = (static_cast<u32>(1) << (category + MinSizeLog2));

		if (free_index < 0)
		{
			if (m_heads[category] == m_ends[category])
				panic("Exceeded storage for %u byte entries in ReservedHeap.\n", static_cast<u32>(1) << (category + MinSizeLog2));

			void* const head = static_cast<byte*>(m_memory) + m_heads[category] * sizeof(u32);

			if (!minos::mem_commit(head, m_commit_increment_bytes[category]))
				panic("Failed to allocate additional storage for %u byte entries in ReservedHead (0x%X).\n", static_cast<u32>(1) << (category + MinSizeLog2), minos::last_error());

			s32* curr = reinterpret_cast<s32*>(head);

			const u32 committed_dwords = m_commit_increment_bytes[category] / sizeof(u32);

			const u32 next_unit_offset_dwords = m_heads[category] + unit_size / sizeof(u32);

			for (u32 i = 0; i != committed_dwords; i += unit_size / sizeof(u32))
				curr[i] = static_cast<s32>(i + next_unit_offset_dwords);

			curr[committed_dwords - unit_size / sizeof(u32)] = -1;

			free_index = m_heads[category];

			m_heads[category] += m_commit_increment_bytes[category] / sizeof(u32);
		}
		
		m_first_frees[category] = static_cast<s32*>(m_memory)[free_index];

		byte* const begin = static_cast<byte*>(m_memory) + free_index * sizeof(u32);

		return MutRange<byte>{ begin, alloc_size };
	}

	void dealloc(MutRange<byte> memory) noexcept
	{
		ASSERT_OR_IGNORE(memory.count() <= (static_cast<u64>(1) << MaxSizeLog2));

		const u64 bytes = static_cast<u32>(memory.count());

		const u8 leading_zeros = count_leading_zeros_assume_one(bytes);

		const u8 category = (32 - MinSizeLog2) < leading_zeros
			? 0
			: (32 - MinSizeLog2) - leading_zeros;

		ASSERT_OR_IGNORE(memory.begin() >= (category == 0 ? static_cast<byte*>(m_memory) : reinterpret_cast<byte*>(static_cast<u32*>(m_memory) + m_ends[category - 1])));

		ASSERT_OR_IGNORE(memory.end() <= reinterpret_cast<byte*>(static_cast<u32*>(m_memory) + m_ends[category]));

		if (m_first_frees[category] >= 0)
			*reinterpret_cast<s32*>(memory.begin()) = m_first_frees[category];

		m_first_frees[category] = static_cast<s32>(reinterpret_cast<u32*>(memory.begin()) - static_cast<u32*>(m_memory));

		ASSERT_OR_IGNORE(m_first_frees[category] >= 0);
	}

	void* begin() noexcept
	{
		return m_memory;
	}

	const void* begin() const noexcept
	{
		return m_memory;
	}
};

#endif // CONTAINER_INCLUDE_GUARD
