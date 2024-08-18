#ifndef TAGGED_PTR_INCLUDE_GUARD
#define TAGGED_PTR_INCLUDE_GUARD

#include "common.hpp"

template<typename T>
struct TaggedPtr
{
private:

    s64 m_data;

public:

    TaggedPtr() noexcept = default;

    TaggedPtr(T* ptr, u16 n) noexcept : m_data{ reinterpret_cast<s64>(ptr) << 16 | n } {}

    T* ptr() const noexcept
    {
        return reinterpret_cast<T*>(m_data >> 16);
    }

    u16 tag() const noexcept
    {
        return static_cast<u16>(m_data);
    }

    void* raw_value() const noexcept
    {
        return reinterpret_cast<T*>(m_data);
    }

    static TaggedPtr<T> from_raw_value(void* value)
    {
        TaggedPtr ptr;

        ptr.m_data = reinterpret_cast<s64>(value);

        return ptr;
    }
};

#endif // TAGGED_PTR_INCLUDE_GUARD
