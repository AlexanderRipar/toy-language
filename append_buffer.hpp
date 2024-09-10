#ifndef APPEND_BUFFER_INCLUDE_GUARD
#define APPEND_BUFFER_INCLUDE_GUARD

#include <cstring>

#include "common.hpp"
#include "minos.hpp"

struct AppendBuffer
{
private:

	byte* m_begin;

	u32 m_used;

	u32 m_committed;
	
	u32 m_reserved;

	void ensure_commit(u32 extra) noexcept
	{
		if (m_used + extra <= m_committed)
			return;

		if (m_used + extra > m_reserved)
			minos::exit_process(101);

		u32 new_committed = m_committed * 2;

		while (new_committed < m_used + extra)
			new_committed *= 2;

		if (new_committed > m_reserved)
			new_committed = m_reserved;

		const u32 extra_commit = new_committed - extra_commit;

		if (!minos::commit(m_begin + m_committed, extra_commit))
			minos::exit_process(42);

		m_committed = new_committed;
	}

public:

	void init(byte* memory, u32 initial_commit, u32 reserved) noexcept
	{
		ASSERT_OR_IGNORE(memory != nullptr);

		ASSERT_OR_IGNORE(initial_commit <= reserved);

		m_begin = memory;

		m_used = 0;

		m_committed = initial_commit;

		m_reserved = reserved;

		if (initial_commit == 0)
			return;
			
		if (!minos::commit(m_begin, initial_commit))
			minos::exit_process(42);
	}

	template<typename T>
	void append(T data) noexcept
	{
		ensure_commit(sizeof(T));

		memcpy(m_begin + m_used, &data, sizeof(T));

		m_used += sizeof(T);
	}

	template<typename T>
	void append_buffer(T* data, u32 count) noexcept
	{
		ensure_commit(sizeof(T) * count);

		memcpy(m_begin + m_used, data, sizeof(T) * count);

		m_used += sizeof(T) * count;
	}

	void append_bytes(Range<byte> data) noexcept
	{
		ensure_commit(static_cast<u32>(data.count()));

		memcpy(m_begin + m_used, data.begin(), data.count());

		m_used += static_cast<u32>(data.count());
	}
};

#endif // APPEND_BUFFER_INCLUDE_GUARD
