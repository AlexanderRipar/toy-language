#ifndef THREADING_INCLUDE_GUARD
#define THREADING_INCLUDE_GUARD

#include <atomic>

#include "minos/minos.hpp"

struct Mutex
{
private:

	std::atomic<u16> m_rep;

public:

	void init() noexcept
	{
		m_rep.store(0, std::memory_order_relaxed);
	}

	void acquire(u32 spin_count = 0) noexcept
	{
		while (true)
		{
			u16 rep = m_rep.load(std::memory_order_relaxed);

			if ((rep & 1) == 0)
			{
				if (m_rep.compare_exchange_weak(rep, rep | 1, std::memory_order_acquire))
					return;
			}

			rep = m_rep.fetch_add(2, std::memory_order_relaxed) + 2;

			while (true)
			{
				if ((rep & 1) == 0)
				{
					if (m_rep.compare_exchange_weak(rep, rep - 1, std::memory_order_acquire))
						return;
				}

				rep = m_rep.load(std::memory_order_relaxed);

				if (spin_count == 0)
					break;

				spin_count -= 1;
			}

			minos::address_wait(&m_rep, &rep, sizeof(m_rep));
		}
	}

	void release() noexcept
	{
		if (m_rep.fetch_sub(1, std::memory_order_release) == 1)
			return;

		minos::address_wake_single(&m_rep);
	}
};

template<typename T, u32 NEXT_FIELD_OFFSET>
struct ThreadsafeIndexStackListHeader
{
private:

	std::atomic<u64> m_all;

public:

	void init() noexcept
	{
		m_all = 0x0000'0000'FFFF'FFFF;
	}

	void init(T* begin, u32 count) noexcept
	{
		if (count == 0)
		{
			m_all = 0x0000'0000'FFFF'FFFF;

			return;
		}

		for (u32 i = 0; i != count - 1; ++i)
			*reinterpret_cast<u32*>(reinterpret_cast<byte*>(begin + i) + NEXT_FIELD_OFFSET) = i + 1;

		*reinterpret_cast<u32*>(reinterpret_cast<byte*>(begin + count - 1) + NEXT_FIELD_OFFSET) = ~0u;

		m_all.store(0, std::memory_order_relaxed);
	}

	T* pop(T* begin) noexcept
	{
		u64 all = m_all.load();

		while (true)
		{
			const u32 head = static_cast<u32>(all);

			if (head == ~0u)
				return nullptr;

			const u32* const next_ptr = reinterpret_cast<u32*>(reinterpret_cast<byte*>(begin + head) + NEXT_FIELD_OFFSET);

			const u32 next = *next_ptr;

			const u64 new_all = (all ^ next ^ head) + (static_cast<u64>(1) << 32);

			if (m_all.compare_exchange_weak(all, new_all))
				return begin + head;
		}
	}

	bool push(T* begin, u32 index) noexcept
	{
		u32* const next_ptr = reinterpret_cast<u32*>(reinterpret_cast<byte*>(begin + index) + NEXT_FIELD_OFFSET);

		u64 all = m_all.load();

		while (true)
		{
			const u32 head = static_cast<u32>(all);

			*next_ptr = head;

			const u64 new_all = all ^ index ^ head;

			if (m_all.compare_exchange_weak(all, new_all))
				return head == ~0u;
		}
	}

	T* pop_unsafe(T* begin) noexcept
	{
		const u64 all = m_all.load(std::memory_order_relaxed);

		const u32 head = static_cast<u32>(all);

		if (head == ~0u)
			return nullptr;

		const u32* const next_ptr = reinterpret_cast<u32*>(reinterpret_cast<byte*>(begin + head) + NEXT_FIELD_OFFSET);

		const u32 next = *next_ptr;

		m_all.store(all ^ next ^ head, std::memory_order_relaxed);

		return begin + head;
	}

	bool push_unsafe(T* begin, u32 index) noexcept
	{
		const u64 all = m_all.load(std::memory_order_relaxed);

		const u32 head = static_cast<u32>(all);

		u32* const next_ptr = reinterpret_cast<u32*>(reinterpret_cast<byte*>(begin + index) + NEXT_FIELD_OFFSET);

		*next_ptr = head;

		m_all.store(all ^ index ^ head, std::memory_order_relaxed);

		return head == ~0u;
	}
};

#endif // THREADING_INCLUDE_GUARD
