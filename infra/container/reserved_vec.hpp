#ifndef RESERVED_VEC_INCLUDE_GUARD
#define RESERVED_VEC_INCLUDE_GUARD

#include <limits>
#include <cstring>

#include "../common.hpp"
#include "../minos/minos.hpp"

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

#endif // RESERVED_VEC_INCLUDE_GUARD
