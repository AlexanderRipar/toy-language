#ifndef INDEX_MAP_INCLUDE_GUARD
#define INDEX_MAP_INCLUDE_GUARD

#include "../common.hpp"
#include "../minos.hpp"

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

#endif INDEX_MAP_INCLUDE_GUARD