#ifndef THREADING_INCLUDE_GUARD
#define THREADING_INCLUDE_GUARD

#include <atomic>

#include "minos.hpp"

namespace thd
{
	struct Semaphore
	{
	private:

		static constexpr u32 AVAILABLE_MASK = 0x0000'FFFF;

		static constexpr u32 PENDING_MASK = 0xFFFF'0000;

		static constexpr u32 AVAILABLE_ONE = 0x0000'0001;

		static constexpr u32 PENDING_ONE = 0x0001'0000;

		std::atomic<u32> m_rep;

	public:

		void init(u32 initial_tokens) noexcept
		{
			m_rep = initial_tokens;
		}

		void post() noexcept
		{
			const u32 prev = m_rep.fetch_add(AVAILABLE_ONE, std::memory_order_release);

			if ((prev & AVAILABLE_MASK) == AVAILABLE_MASK)
				panic("Too many tokens available in Semaphore (65536)\n");

			if ((prev & PENDING_MASK) != 0)
				minos::address_wake_single(&m_rep);
		}

		void await() noexcept
		{
			u32 prev = m_rep.load(std::memory_order_relaxed);

			u32 delta = AVAILABLE_ONE;

			while (true)
			{
				if ((prev & AVAILABLE_MASK) == 0)
				{
					if ((delta & PENDING_ONE) == 0)
					{
						delta += PENDING_ONE;

						prev = m_rep.fetch_add(PENDING_ONE, std::memory_order_relaxed) + PENDING_ONE;

						if ((prev & PENDING_MASK) == 0)
							panic("Too many waiters on Semaphore (65536)\n");
					}

					do
					{
						minos::address_wait(&m_rep, &prev, sizeof(m_rep));

						prev = m_rep.load(std::memory_order_relaxed);
					}
					while ((prev & AVAILABLE_MASK) == 0);
				}

				if (m_rep.compare_exchange_strong(prev, prev - delta, std::memory_order_acquire))
					return;
			}
		}

		[[nodiscard]] bool try_claim() noexcept
		{
			u32 prev = m_rep.load(std::memory_order_acquire);

			while (true)
			{
				if ((prev & AVAILABLE_MASK) == 0)
					return false;

				if (m_rep.compare_exchange_strong(prev, prev - AVAILABLE_ONE))
					return true;
			}
		}
	};

	template<typename T, u32 NEXT_FIELD_OFFSET>
	struct IndexStackListHeader
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
}

#endif // THREADING_INCLUDE_GUARD
