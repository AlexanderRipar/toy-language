#ifndef INDEX_MAP_INCLUDE_GUARD
#define INDEX_MAP_INCLUDE_GUARD

#include "../types.hpp"
#include "../assert.hpp"
#include "../panic.hpp"
#include "../math.hpp"
#include "../minos/minos.hpp"

#include <type_traits>
#include <cstring>

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
				panic("Could not insert value into IndexMap as value storage capacity of % is exceeded by %\n", m_value_capacity, required_commit - m_value_capacity);

			u32 new_commit = m_value_commit + m_value_commit_increment;

			while (new_commit < required_commit)
				new_commit += m_value_commit_increment;

			if (new_commit > m_value_capacity)
				new_commit = m_value_capacity;

			if (!minos::mem_commit(reinterpret_cast<byte*>(m_values) + m_value_commit * V::stride(), (new_commit - m_value_commit) * V::stride()))
				panic("Could not commit additional memory for IndexMap values (0x%[|X])\n", minos::last_error());

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
			panic("Could not commit additional memory for IndexMap lookups and offsets (0x%[|X])\n", minos::last_error());

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

	static constexpr u64 lookups_memory_size(u32 lookup_capacity) noexcept
	{
		ASSERT_OR_IGNORE(is_pow2(lookup_capacity));

		return lookup_capacity * (sizeof(*m_lookups) + sizeof(*m_offsets));
	}

	void init(MutRange<byte> lookup_memory, u32 lookup_commit, MutRange<byte> value_memory, u32 value_commit_increment) noexcept
	{
		ASSERT_OR_IGNORE(lookup_memory.count() % (sizeof(*m_lookups) + sizeof(*m_offsets)) == 0);

		ASSERT_OR_IGNORE(is_pow2(lookup_memory.count() / (sizeof(*m_lookups) + sizeof(*m_offsets))));

		ASSERT_OR_IGNORE(lookup_memory.count() / (sizeof(*m_lookups) + sizeof(*m_offsets)) >= lookup_commit);

		ASSERT_OR_IGNORE(is_pow2(lookup_commit));

		ASSERT_OR_IGNORE(value_memory.count() >= value_commit_increment * V::stride() && value_memory.count() % (value_commit_increment * V::stride()) == 0);

		m_lookups = reinterpret_cast<u16*>(lookup_memory.begin());

		m_offsets = reinterpret_cast<u32*>(reinterpret_cast<u16*>(lookup_memory.begin()) + lookup_commit);

		m_values = reinterpret_cast<V*>(value_memory.begin());

		const u64 lookup_commit_bytes = static_cast<u64>(lookup_commit) * (sizeof(*m_lookups) + sizeof(*m_offsets));

		if (!minos::mem_commit(m_lookups, lookup_commit_bytes))
			panic("Could not commit initial % bytes of memory for IndexMap lookups and offsets (0x%[|X])\n", lookup_commit_bytes, minos::last_error());

		if (!minos::mem_commit(m_values, static_cast<u64>(value_commit_increment) * V::stride()))
			panic("Could not commit initial % bytes of memory for IndexMap values (0x%[|X])\n", static_cast<u64>(value_commit_increment) * V::stride(), minos::last_error());

		m_lookup_used = 0;

		m_value_used = 0;

		m_lookup_commit = lookup_commit;

		m_value_commit = value_commit_increment;

		m_lookup_capacity = static_cast<u32>(lookup_memory.count() / (sizeof(*m_lookups) + sizeof(*m_offsets)));

		m_value_capacity = static_cast<u32>(value_memory.count() / V::stride());

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
};

// Maps `K`s to `V`s, which get interned in `Alloc`.
//
// The required interface is as follows:
//
// K:             -
// V:             bool is_equal_to_key(K key, u32 key_hash) const noexcept;
//                u32 hash() const noexcept;
// Alloc:         V* value_from_id(u32 id) const noexcept;
//                V* value_from_id(u32 id) noexcept;
//                u32 id_from_value(const V* value) const noexcept;
//                AllocIterator values() noexcept;
//                V* alloc(K key, u32 key_hash) noexcept;
//                void dealloc(u32 id) noexcept;
//                static constexpr bool allows_dealloc() noexcept;
// AllocIterator: bool has_next() const noexcept;
//                V* next() noexcept;
template<typename K, typename V, typename Alloc>
struct IdMap
{
private:

	struct alignas(8) LookupEntry
	{
		u32 hash_fingerprint : 24;

		u32 psl : 8;

		u32 data_id;
	};

	static_assert(sizeof(LookupEntry) == 8);

	static constexpr u8 HASH_FINGERPRINT_SHIFT = 8;

	static constexpr u8 MAX_PSL = 64;

	LookupEntry* m_lookups;

	u32 m_lookup_used;

	u32 m_lookup_commit;

	u32 m_lookup_capacity;

	Alloc m_alloc;

	static bool is_empty_lookup(LookupEntry lookup) noexcept
	{
		return lookup.hash_fingerprint == 0;
	}

	bool is_equal_lookup(LookupEntry existing, LookupEntry to_insert, K key, u32 key_hash) const noexcept
	{
		if (existing.hash_fingerprint != to_insert.hash_fingerprint || existing.psl != to_insert.psl)
			return false;

		const V* const value = m_alloc.value_from_id(existing.data_id);

		return value->is_equal_to_key(key, key_hash);
	}

	static LookupEntry create_lookup(u32 key_hash) noexcept
	{
		LookupEntry lookup;
		lookup.hash_fingerprint = (key_hash >> HASH_FINGERPRINT_SHIFT) | 1;
		lookup.psl = 0;
		lookup.data_id = 0;

		return lookup;
	}

	u32 create_value(K key, u32 key_hash) noexcept
	{
		V* const value = m_alloc.alloc(key, key_hash);

		return m_alloc.id_from_value(value);
	}

	void rehash() noexcept
	{
		if (m_lookup_commit == m_lookup_capacity)
			panic("Could not rehash IdMap lookup as no additional capacity was available\n");

		const u32 lookups_size = m_lookup_commit * sizeof(*m_lookups);

		if (!minos::mem_commit(reinterpret_cast<byte*>(m_lookups) + lookups_size, lookups_size))
			panic("Could not commit additional memory for IdMap lookups and offsets (0x%[|X])\n", minos::last_error());

		memset(m_lookups, 0, m_lookup_commit * sizeof(*m_lookups));

		m_lookup_commit *= 2;

		auto iterator = m_alloc.values();

		while (iterator.has_next())
		{
			const V* const to_insert = iterator.next();

			const u32 data_id = m_alloc.id_from_value(to_insert);

			reinsert_value_into_lookup(data_id, to_insert->hash());
		}
	}

	void reinsert_value_into_lookup(u32 offset_to_insert, u32 key_hash) noexcept
	{
		u32 i = key_hash & (m_lookup_commit - 1);

		LookupEntry wanted_lookup = create_lookup(key_hash);
		wanted_lookup.data_id = offset_to_insert;

		while (true)
		{
			const LookupEntry curr_lookup = m_lookups[i];

			if (is_empty_lookup(curr_lookup))
			{
				m_lookups[i] = wanted_lookup;

				return;
			}
			else if (curr_lookup.psl < wanted_lookup.psl)
			{
				m_lookups[i] = wanted_lookup;

				wanted_lookup = curr_lookup;
			}

			if (i == m_lookup_commit - 1)
				i = 0;
			else
				i += 1;

			if (wanted_lookup.psl == MAX_PSL)
				panic("Could not insert IdMap entry, as the maximum proble sequence length was exceeded");

			wanted_lookup.psl += 1;
		}
	}

	bool find_by_key(K key, u32 key_hash, LookupEntry** out_lookup) noexcept
	{
		LookupEntry to_find = create_lookup(key_hash);

		u32 i = key_hash & (m_lookup_commit - 1);

		while (true)
		{
			const LookupEntry existing = m_lookups[i];

			if (is_empty_lookup(existing) || existing.psl < to_find.psl)
				return false;

			if (is_equal_lookup(existing, to_find, key, key_hash))
			{
				*out_lookup = m_lookups + i;

				return true;
			}

			if (to_find.psl == MAX_PSL)
				return false;

			to_find.psl += 1;

			i += 1;

			if (i == m_lookup_commit)
				i = 0;
		}
	}

	LookupEntry* find_by_value(const V* value) noexcept
	{
		const u32 hash = value->hash();

		LookupEntry to_find = create_lookup(hash);

		u32 i = hash & (m_lookup_commit - 1);

		while (true)
		{
			const LookupEntry existing = m_lookups[i];

			ASSERT_OR_IGNORE(!is_empty_lookup(existing) && existing.psl >= to_find.psl);

			if (existing.psl == to_find.psl && m_alloc.value_from_id(existing.data_id) == value)
				return m_lookups + i;

			ASSERT_OR_IGNORE(to_find.psl != MAX_PSL);

			to_find.psl += 1;

			i += 1;

			if (i == m_lookup_commit)
				i = 0;
		}
	}

	void remove_from_lookups(LookupEntry* to_remove) noexcept
	{
		m_lookup_used -= 1;

		u32 prev_i = static_cast<u32>(to_remove - m_lookups) & (m_lookup_commit - 1);

		u32 curr_i = (prev_i + 1) & (m_lookup_commit - 1);

		ASSERT_OR_IGNORE(!is_empty_lookup(m_lookups[prev_i]));

		while (m_lookups[curr_i].psl != 0)
		{
			LookupEntry curr = m_lookups[curr_i];
			curr.psl -= 1;

			m_lookups[prev_i] = curr;

			prev_i = curr_i;

			curr_i += 1;

			if (curr_i == m_lookup_commit)
				curr_i = 0;
		}

		m_lookups[prev_i] = LookupEntry{};
	}

public:

	static constexpr u64 lookups_memory_size(u32 lookup_capacity) noexcept
	{
		ASSERT_OR_IGNORE(is_pow2(lookup_capacity));

		return lookup_capacity * sizeof(*m_lookups);
	}

	void init(MutRange<byte> lookup_memory, u32 lookup_commit, Alloc alloc) noexcept
	{
		ASSERT_OR_IGNORE(lookup_memory.count() % sizeof(*m_lookups) == 0);

		ASSERT_OR_IGNORE(is_pow2(lookup_memory.count() / sizeof(*m_lookups)));

		ASSERT_OR_IGNORE(lookup_memory.count() / sizeof(*m_lookups) >= lookup_commit);

		ASSERT_OR_IGNORE(is_pow2(lookup_commit));

		const u64 lookup_commit_bytes = static_cast<u64>(lookup_commit) * sizeof(*m_lookups);

		if (!minos::mem_commit(lookup_memory.begin(), lookup_commit_bytes))
			panic("Could not commit initial % bytes of memory for IdMap lookups and offsets (0x%[|X])\n", lookup_commit_bytes, minos::last_error());

		m_lookups = reinterpret_cast<LookupEntry*>(lookup_memory.begin());
		m_lookup_used = 0;
		m_lookup_commit = lookup_commit;
		m_lookup_capacity = static_cast<u32>(lookup_memory.count() / sizeof(*m_lookups));
		m_alloc = alloc;
	}

	u32 id_from(K key, u32 key_hash) noexcept
	{
		if (m_lookup_used * 4 > m_lookup_commit * 3)
			rehash();

		LookupEntry to_insert = create_lookup(key_hash);

		u32 i = key_hash & (m_lookup_commit - 1);

		bool needs_allocation = true;

		// This initial value does not matter, as it is always overwritten in
		// the loop before being used, as it is tied to the value of
		// `needs_allocation`.
		u32 new_data_offset = 0; 

		while (true)
		{
			const LookupEntry existing = m_lookups[i];

			if (is_empty_lookup(existing))
			{
				if (needs_allocation)
				{
					new_data_offset = create_value(key, key_hash);

					to_insert.data_id = new_data_offset;
				}

				m_lookups[i] = to_insert;

				return new_data_offset;
			}
			else if (is_equal_lookup(existing, to_insert, key, key_hash))
			{
				return existing.data_id;
			}
			else if (existing.psl < to_insert.psl)
			{
				if (needs_allocation)
				{
					needs_allocation = false;

					new_data_offset = create_value(key, key_hash);

					to_insert.data_id = new_data_offset;
				}

				m_lookups[i] = to_insert;

				to_insert = existing;
			}

			if (to_insert.psl == MAX_PSL)
			{
				rehash();

				return needs_allocation
					? id_from(key, key_hash)
					: new_data_offset;
			}

			to_insert.psl += 1;
			
			i += 1;

			if (i == m_lookup_commit)
				i = 0;
		}
	}

	u32 id_from(const V* value) const noexcept
	{
		return m_alloc.id_from_value(value);
	}

	V* value_from(K key, u32 key_hash) noexcept
	{
		const u32 id = id_from(key, key_hash);

		return value_from(id);
	}

	V* value_from(u32 id) noexcept
	{
		return m_alloc.value_from_id(id);
	}

	bool try_id_from(K key, u32 key_hash, u32* out) noexcept
	{
		LookupEntry* lookup;

		if (!find_by_key(key, key_hash, &lookup))
			return false;

		*out = lookup->data_id;

		return true;
	}

	Maybe<V*> try_value_from(K key, u32 key_hash) noexcept
	{
		LookupEntry* lookup;

		if (!find_by_key(key, key_hash, &lookup))
			return none<V*>();

		V* const value = m_alloc.value_from_id(lookup->data_id);

		return some(value);
	}

	Maybe<const V*> try_value_from(K key, u32 key_hash) const noexcept
	{
		LookupEntry* lookup;

		if (!find_by_key(key, key_hash, &lookup))
			return none<const V*>();

		const V* const value = m_alloc.value_from_id(lookup->data_id);

		return some(value);
	}

	const V* value_from(u32 id) const noexcept
	{
		return m_alloc.value_from_id(id);
	}

	void remove(K key, u32 key_hash) noexcept
	{
		LookupEntry* lookup;

		if (!find_by_key(key, key_hash, &lookup))
			ASSERT_UNREACHABLE;

		m_alloc.dealloc(lookup->data_id);
		
		remove_from_lookups(lookup);
	}

	void remove(u32 id) noexcept
	{
		const V* const value = m_alloc.value_from_id(id);

		LookupEntry* const lookup = find_by_value(value);

		m_alloc.dealloc(id);

		remove_from_lookups(lookup);
	}

	void remove(V* value) noexcept
	{
		const u32 id = m_alloc.id_from_value(value);

		LookupEntry* const lookup = find_by_value(value);

		m_alloc.dealloc(id);

		remove_from_lookups(lookup);
	}

	bool try_remove(K key, u32 key_hash) noexcept
	{
		LookupEntry* lookup;

		if (!find_by_key(key, key_hash, &lookup))
			return false;

		m_alloc.dealloc(lookup->data_id);
		
		remove_from_lookups(lookup);

		return true;
	}
};

#endif // INDEX_MAP_INCLUDE_GUARD
