#ifndef UTIL_BYTEVEC_INCLUDE_GUARD
#define UTIL_BYTEVEC_INCLUDE_GUARD

#include "types.hpp"

#include <cstdlib>
#include <cstring>
#include <cassert>

struct ByteVec
{
private:

	static constexpr u32 INITIAL_CAPACITY = 32;

	void* m_data = nullptr;

	u32 m_used = 0;

	u32 m_capacity = 0;

public:

	ByteVec() noexcept = default;

	ByteVec(const ByteVec& other) noexcept = delete;

	ByteVec(ByteVec&& other) noexcept : m_data{ other.m_data } { other.reset(); }

	~ByteVec() noexcept { free(m_data); }

	bool reserve(u32 bytes) noexcept
	{
		if (m_used + bytes <= m_capacity)
			return true;

		if (m_data == nullptr)
		{
			u32 new_capacity = INITIAL_CAPACITY;

			while (new_capacity < bytes)
				new_capacity *= 2;
			
			m_data = malloc(new_capacity);

			if (m_data == nullptr)
				return false;

			m_capacity = new_capacity;

			return true;
		}

		u32 new_capacity = m_capacity * 2;

		while (new_capacity < m_used + bytes)
			new_capacity *= 2;

		void* tmp = realloc(m_data, new_capacity);

		if (tmp == nullptr)
			return false;

		m_data = tmp;

		m_capacity = new_capacity;

		return true;
	}

	bool append(u32 bytes, const void* data) noexcept
	{
		if (!reserve(bytes))
			return false;

		m_used += bytes;

		memcpy(static_cast<u8*>(m_data) + m_used, data, bytes);

		return true;
	}

	void append_unchecked(u32 bytes, void* data) noexcept
	{
		assert(m_used + bytes <= m_capacity);

		m_used += bytes;

		memcpy(static_cast<u8*>(m_data) + m_used, data, bytes);
	}

	bool get_append_region(u32 bytes, void** out) noexcept
	{
		if (!reserve(bytes))
			return false;

		m_used += bytes;

		*out = static_cast<u8*>(m_data) + m_used;

		return true;
	}

	void* get_append_region_unchecked(u32 bytes) noexcept
	{
		assert(m_used + bytes <= m_capacity);

		m_used += bytes;

		return static_cast<u8*>(m_data) + m_used;
	}

	void pop(u32 bytes) noexcept
	{
		assert(bytes < m_used);

		m_used -= bytes;
	}

	void reset() noexcept
	{
		free(m_data);
		
		m_data = nullptr;

		m_used = 0;

		m_capacity = 0;
	}

	u32 bytes() const noexcept { return m_used; }

	bool is_empty() const noexcept { return m_used == 0; }

	void* begin() noexcept { return m_data; }

	const void* begin() const noexcept { return m_data; }

	void* end() noexcept { return static_cast<u8*>(m_data) + m_used; }

	const void* end() const noexcept { return static_cast<const u8*>(m_data) + m_used; }
};

#endif // UTIL_BYTEVEC_INCLUDE_GUARD
