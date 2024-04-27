#ifndef CONTAINER_INCLUDE_GUARD
#define CONTAINER_INCLUDE_GUARD

#include "common.hpp"
#include "minos.hpp"
#include "memory.hpp"

template<typename Index = u32>
struct RawFixedBuffer
{
private:

	void* m_data;

public:

	bool init(MemorySubregion memory) noexcept
	{
		ASSERT_OR_IGNORE(memory.data() != nullptr);

		ASSERT_OR_IGNORE(memory.count() != 0 && memory.count() <= std::numeric_limits<Index>::max());

		if (!memory.commit(0, memory.count()))
			return false;

		m_data = memory.data();

		return true;
	}

	void* data() noexcept
	{
		return m_data;
	}
};

template<typename Index = u32>
struct RawGrowableBuffer
{
private:

	void* m_data;

	Index m_reserved_bytes;

	Index m_committed_bytes;

	Index m_commit_increment_bytes;

public:

	bool init(MemorySubregion memory, Index commit_increment_bytes, Index initial_commit_bytes) noexcept
	{
		ASSERT_OR_IGNORE(memory.count() != 0 && memory.count() <= std::numeric_limits<Index>::max());

		ASSERT_OR_IGNORE(memory->cdata() != nullptr);

		ASSERT_OR_IGNORE(commit_increment_bytes != 0);

		ASSERT_OR_IGNORE(initial_commit_bytes <= memory.count());

		if (initial_commit_bytes != 0)
		{
			if (!memory.commit(0, initial_commit_bytes))
				return false;
		}

		m_data = memory.data();

		m_reserved_bytes = static_cast<Index>(memory.count());

		m_committed_bytes = initial_commit_bytes;

		m_commit_increment_bytes = commit_increment_bytes;

		return true;
	}

	bool grow(Index extra_bytes) noexcept
	{
		Index additional_committed_bytes;

		additional_committed_bytes = m_commit_increment_bytes;

		while (additional_committed_bytes < extra_bytes)
			additional_committed_bytes += m_commit_increment_bytes;

		if (m_committed_bytes + additional_committed_bytes > m_reserved_bytes)
			return false;

		if (!impl::commit(static_cast<byte*>(m_data) + m_committed_bytes, additional_committed_bytes))
			return false;

		m_committed_bytes += additional_committed_bytes;

		return true;
	}

	void* data() noexcept
	{
		return m_data;
	}

	const void* data() const noexcept
	{
		return m_data;
	}

	Index committed_bytes() const noexcept
	{
		return m_committed_bytes;
	}
};

template<typename T, typename Index = u32>
struct FixedBuffer
{
private:

	RawFixedBuffer<Index> m_buf;

public:

	bool init(MemorySubregion memory) noexcept
	{
		return m_buf.init(memory);
	}

	T* data() noexcept
	{
		return static_cast<T*>(m_buf.data());
	}

	const T* data() const noexcept
	{
		return static_cast<const T*>(m_buf.data());
	}
};

template<typename T, typename Index = u32>
struct GrowableBuffer
{
private:

	RawGrowableBuffer<Index> m_buf;

public:

	bool init(MemorySubregion memory, Index commit_increment_count, Index initial_commit_count) noexcept
	{
		return m_buf.init(memory, commit_increment_count * sizeof(T), initial_commit_count * sizeof(T));
	}

	bool grow(Index extra_count) noexcept
	{
		return m_buf.grow(extra_count * sizeof(T));
	}

	T* data() noexcept
	{
		return static_cast<T*>(m_buf.data());
	}

	const T* data() const noexcept
	{
		return static_cast<const T*>(m_buf.data());
	}

	Index committed_count() const noexcept
	{
		return m_buf.committed_bytes() / sizeof(T);
	}
};

#endif // CONTAINER_INCLUDE_GUARD
