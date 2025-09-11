#ifndef RESERVED_HEAP_INCLUDE_GUARD
#define RESERVED_HEAP_INCLUDE_GUARD

#include "../common.hpp"
#include "../minos.hpp"

template<u32 MinSizeLog2, u32 MaxSizeLog2>
struct ReservedHeap
{
private:

	static constexpr u32 CategoryCount = MaxSizeLog2 - MinSizeLog2 + 1;

	static_assert(MinSizeLog2 >= 2);

	static_assert(MaxSizeLog2 <= 31);

	static_assert(MinSizeLog2 <= MaxSizeLog2);

	void* m_memory;

	s32 m_freelist_heads[CategoryCount];

	s32 m_unused_counts[CategoryCount];

	u32 m_commit_heads[CategoryCount];

	u32 m_ends[CategoryCount];

	u32 m_commit_increments[CategoryCount];

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

			m_freelist_heads[i] = -1;

			m_unused_counts[i] = 0;

			m_ends[i] = static_cast<u32>((total_size + curr_bytes));

			m_commit_heads[i] = static_cast<u32>(total_size);

			m_commit_increments[i] = commits[i] * unit_size;

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

		byte* alloc_begin;

		if (m_freelist_heads[category] >= 0)
		{
			alloc_begin = static_cast<byte*>(m_memory) + m_freelist_heads[category];

			m_freelist_heads[category] = *reinterpret_cast<s32*>(alloc_begin);
		}
		else if (m_unused_counts[category] != 0)
		{
			alloc_begin = static_cast<byte*>(m_memory) + m_commit_heads[category] - m_unused_counts[category];

			m_unused_counts[category] -= alloc_size;
		}
		else
		{
			if (m_commit_heads[category] == m_ends[category])
				panic("Exceeded storage for %u byte entries in ReservedHeap.\n", static_cast<u32>(1) << (category + MinSizeLog2));

			void* const head = static_cast<byte*>(m_memory) + m_commit_heads[category];

			if (!minos::mem_commit(head, m_commit_increments[category]))
				panic("Failed to allocate additional storage for %u byte entries in ReservedHead (0x%X).\n", static_cast<u32>(1) << (category + MinSizeLog2), minos::last_error());

			alloc_begin = static_cast<byte*>(m_memory) + m_commit_heads[category];

			m_commit_heads[category] += m_commit_increments[category];

			m_unused_counts[category] = m_commit_increments[category] - alloc_size;
		}

		return MutRange<byte>{ alloc_begin, alloc_size };
	}

	void dealloc(MutRange<byte> memory) noexcept
	{
		ASSERT_OR_IGNORE(memory.count() <= (static_cast<u64>(1) << MaxSizeLog2));

		const u64 bytes = static_cast<u32>(memory.count());

		const u8 leading_zeros = count_leading_zeros(static_cast<u32>(bytes - 1));

		const u8 category = (32 - MinSizeLog2) < leading_zeros
			? 0
			: (32 - MinSizeLog2) - leading_zeros;

		ASSERT_OR_IGNORE(memory.begin() >= (category == 0 ? static_cast<byte*>(m_memory) : static_cast<byte*>(m_memory) + m_ends[category - 1]));

		ASSERT_OR_IGNORE(memory.end() <= static_cast<byte*>(m_memory) + m_ends[category]);

		*reinterpret_cast<s32*>(memory.begin()) = m_freelist_heads[category];

		m_freelist_heads[category] = static_cast<s32>(memory.begin() - static_cast<byte*>(m_memory));

		ASSERT_OR_IGNORE(m_freelist_heads[category] >= 0);
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

#endif // RESERVED_HEAP_INCLUDE_GUARD
