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

#endif // CONTAINER_INCLUDE_GUARD
