#ifndef STRUCTURE_INCLUDE_GUARD
#define STRUCTURE_INCLUDE_GUARD

#include <cstring>

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

		if (!minos::commit(m_memory + m_committed, (new_commit - m_committed) * sizeof(T)))
			panic("Could not allocate additional memory (%llu bytes - error 0x%X)\n", (new_commit - m_committed) * sizeof(T), minos::last_error());

		m_committed = new_commit;
	}

public:

	ReservedVec() noexcept :
		m_memory{ nullptr },
		m_used{ 0 },
		m_committed{ 0 },
		m_commit_increment{ 0 },
		m_reserved{ 0 }
	{}

	ReservedVec(Index reserve, Index commit_increment) noexcept :
		m_memory{ static_cast<T*>(minos::reserve(reserve * sizeof(T))) },
		m_used{ 0 },
		m_committed{ commit_increment },
		m_commit_increment{ commit_increment },
		m_reserved{ reserve }
	{
		ASSERT_OR_IGNORE(reserve >= commit_increment);

		if (m_memory == nullptr)
			panic("Could not reserve memory (%llu bytes - error 0x%X)\n", reserve * sizeof(T), minos::last_error());

		if (!minos::commit(m_memory, m_committed * sizeof(T)))
			panic("Could not commit initial memory (%llu bytes - error 0x%X)\n", m_committed * sizeof(T), minos::last_error());
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

	void reset() noexcept
	{
		m_used = 0;
	}

	void pop(Index count) noexcept
	{
		ASSERT_OR_IGNORE(count <= m_used);

		m_used -= count;
	}

	void release() noexcept
	{
		ASSERT_OR_IGNORE(m_memory != nullptr);

		minos::unreserve(m_memory);

		m_memory = nullptr;
	}

	void free_region(void* begin, Index count) noexcept
	{
		ASSERT_OR_IGNORE(begin >= m_memory && static_cast<byte*>(begin) + count < m_memory + m_committed);

		minos::decommit(begin, count);
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

	u16* const m_lookups;

	u32* m_offsets;

	V* const m_values;

	u32 m_lookup_used;

	u32 m_value_used;

	u32 m_lookup_commit;

	u32 m_value_commit;

	const u32 m_lookup_capacity;

	const u32 m_value_capacity;

	const u32 m_value_commit_increment;

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

			if (!minos::commit(reinterpret_cast<byte*>(m_values) + m_value_commit * V::stride(), (new_commit - m_value_commit) * V::stride()))
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

		if (!minos::commit(reinterpret_cast<byte*>(m_lookups) + lookup_and_offset_bytes, lookup_and_offset_bytes))
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

	IndexMap(u32 lookup_capacity, u32 lookup_commit, u32 value_capacity, u32 value_commit, u32 value_commit_increment) noexcept :
		m_lookups{ static_cast<u16*>(minos::reserve(lookup_capacity * (sizeof(*m_lookups) + sizeof(*m_offsets)) + value_capacity * V::stride())) },
		m_offsets{ reinterpret_cast<u32*>(m_lookups + lookup_commit) },
		m_values{ reinterpret_cast<V*>(reinterpret_cast<byte*>(m_lookups) + lookup_capacity * (sizeof(*m_lookups) + sizeof(*m_offsets))) },
		m_lookup_used{ 0 },
		m_value_used{ 0 },
		m_lookup_commit{ lookup_commit },
		m_value_commit{ value_commit },
		m_lookup_capacity{ lookup_capacity },
		m_value_capacity{ value_capacity },
		m_value_commit_increment { value_commit_increment }
	{
		if (!is_pow2(lookup_capacity))
			panic("Could not create IndexMap with non-power-of-two lookup capacity %u\n", lookup_capacity);

		if (!is_pow2(lookup_commit))
			panic("Could not create IndexMap with non-power-of-two initial lookup commit %u\n", lookup_commit);

		if (lookup_commit > lookup_capacity)
			panic("Could not create IndexMap with initial lookup commit %u greater than lookup capacity %u\n", lookup_commit, lookup_capacity);

		if (value_commit > value_capacity)
			panic("Could not create IndexMap with initial value commit %u greater than value capacity %u\n", value_commit, value_capacity);

		if (m_lookups == nullptr)
			panic("Could not reserve memory for IndexMap (0x%X)\n", minos::last_error());

		if (!minos::commit(m_lookups, lookup_commit * (sizeof(*m_lookups) + sizeof(*m_offsets))))
			panic("Could not commit initial memory for IndexMap lookups and offsets (0x%X)\n", minos::last_error());

		if (!minos::commit(m_values, value_commit * V::stride()))
			panic("Could not commit initial memory for IndexMap offsets (0x%X)\n", minos::last_error());
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
		return (reinterpret_cast<const byte*>(value) - reinterpret_cast<const byte*>(m_values)) / V::stride();
	}

	V* value_from(K key, u32 key_hash) noexcept
	{
		return value_from(index_from(key, key_hash));
	}

	V* value_from(u32 index) noexcept
	{
		return reinterpret_cast<V*>(reinterpret_cast<byte*>(m_values) + index * V::stride());
	}

	const V* value_from(u32 index) const noexcept
	{
		return reinterpret_cast<const V*>(reinterpret_cast<const byte*>(m_values) + index * V::stride());
	}
};

template<typename Length, u32 STRIDE>
struct IndexMapStringKey
{
	u32 m_hash;

	Length m_length;

#pragma warning(push)
#pragma warning(disable : 4200) // C4200: nonstandard extension used: zero-sized array in struct/union
	char8 m_chars[];
#pragma warning(pop)

	static constexpr u32 stride() noexcept
	{
		return STRIDE;
	}

	static u32 required_strides(Range<char8> key) noexcept
	{
		return static_cast<u32>((offsetof(IndexMapStringKey, m_chars) + key.count() + stride() - 1) / stride());
	}

	u32 used_strides() const noexcept
	{
		return static_cast<u32>((offsetof(IndexMapStringKey, m_chars) + m_length + stride() - 1) / stride());
	}

	bool equal_to_key(Range<char8> key, u32 key_hash) const noexcept
	{
		return m_hash == key_hash && key.count() == m_length && memcmp(key.begin(), m_chars, m_length) == 0;
	}

	void init(Range<char8> key, u32 key_hash) noexcept
	{
		m_hash = key_hash;

		m_length = static_cast<Length>(key.count());

		memcpy(m_chars, key.begin(), key.count());
	}

	Range<char8> range() const noexcept
	{
		return Range<char8>{ m_chars, m_length };
	}
};

#endif // STRUCTURE_INCLUDE_GUARD
