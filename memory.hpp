#ifndef MEMORY_INCLUDE_GUARD
#define MEMORY_INCLUDE_GUARD
    
#include "common.hpp"
#include "minos.hpp"
#include <cstring>

struct MemoryRequirements
{
    u64 bytes;

    u32 alignment;
};

struct MemorySubregion
{
private:

    void* m_ptr;

    u64 m_bytes;

public:

    MemorySubregion() noexcept = default;

    MemorySubregion(void* ptr, u64 bytes) noexcept : m_ptr{ ptr }, m_bytes{ bytes } {}

    MemorySubregion partition_head(u64 bytes) noexcept
    {
        ASSERT_OR_IGNORE(bytes <= m_bytes);

        MemorySubregion head{ m_ptr, bytes };

        m_ptr = static_cast<byte*>(m_ptr) + bytes;

        m_bytes -= bytes;

        return head;
    }

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

#endif // MEMORY_INCLUDE_GUARD
