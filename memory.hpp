#ifndef MEMORY_INCLUDE_GUARD
#define MEMORY_INCLUDE_GUARD
    
#include "common.hpp"
#include "minos.hpp"
#include <cstring>

struct MemorySubregion
{
private:

    void* m_ptr;

    u64 m_bytes;

public:

    MemorySubregion() noexcept = default;

    MemorySubregion(void* ptr, u64 bytes) noexcept : m_ptr{ ptr }, m_bytes{ bytes } {}

    bool commit(u64 offset, u64 bytes) noexcept
    {
        ASSERT_OR_IGNORE(bytes != 0);

        ASSERT_OR_IGNORE(offset + bytes <= m_bytes);

        return minos::commit(static_cast<byte*>(m_ptr) + offset, bytes);
    }

    u64 count() const noexcept
    {
        return m_bytes;
    }

    void* data() noexcept
    {
        return m_ptr;
    }

    const void* data() const noexcept
    {
        return m_ptr;
    }

    const void* cdata() const noexcept
    {
        return m_ptr;
    }
};

struct MemoryRegion
{
private:

    void* m_ptr;

public:

    bool init(u64 reserved_bytes) noexcept
    {
        ASSERT_OR_IGNORE(reserved_bytes != 0);

        const u64 page_mask = minos::page_bytes() - 1;

        const u64 actual_reserved_bytes = (reserved_bytes + page_mask) & ~page_mask;

        m_ptr = minos::reserve(actual_reserved_bytes);

        return m_ptr != nullptr;
    }

    bool deinit() noexcept
    {
        bool is_ok = true;

        if (m_ptr != nullptr)
            is_ok &= minos::unreserve(m_ptr);
            
        memset(this, 0, sizeof(*this));

        return is_ok;
    }

    bool commit(u64 offset, u64 bytes) noexcept
    {
        assert(bytes != 0);

        return minos::commit(static_cast<byte*>(m_ptr) + offset, bytes);
    }

    MemorySubregion subregion(u64 offset, u64 bytes) noexcept
    {
        assert(bytes != 0);

        const u64 page_mask = minos::page_bytes() - 1;

        const u64 actual_offset = (offset + page_mask) & ~page_mask;

        const u64 actual_bytes = (bytes + page_mask) & ~page_mask;

        return MemorySubregion{ static_cast<byte*>(m_ptr) + actual_offset, actual_bytes };
    }

    void* data() noexcept
    {
        return m_ptr;
    }

    const void* data() const noexcept
    {
        return m_ptr;
    }

    const void* cdata() const noexcept
    {
        return m_ptr;
    }
};

struct MemoryRegionStackAllocator
{
private:
	
	MemoryRegion m_region;

	u64 m_used_bytes;

public:

	bool init(u64 bytes) noexcept
	{
		if (!m_region.init(bytes))
			return false;

		m_used_bytes = 0;

		return true;
	}

	MemorySubregion push(u64 bytes)
	{
		MemorySubregion region = m_region.subregion(m_used_bytes, bytes);

		m_used_bytes += region.count();

		return region;
	}
};

#endif // MEMORY_INCLUDE_GUARD
