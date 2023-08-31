#ifndef UTIL_SLICE_INCLUDE_GUARD
#define UTIL_SLICE_INCLUDE_GUARD

#include <cassert>

#include "types.hpp"

template<typename T>
struct Slice
{
        T* m_begin;

        T* m_end;

        Slice() noexcept = default;

        Slice(T* begin, T* end) noexcept : m_begin{ begin }, m_end{ end } {}

        Slice(T* begin, usz count) noexcept : m_begin{ begin }, m_end{ begin + count } {}

        T& operator[](usz i) noexcept
        {
                assert(m_begin + i < m_end);

                return m_begin[i];
        }
        
        const T& operator[](usz i) const noexcept
        {
                assert(m_begin + i < m_end);

                return m_begin[i];
        }

        usz count() const noexcept
        {
                return m_end - m_begin;
        }

        T* begin() noexcept
        {
                return m_begin;
        }

        const T* begin() const noexcept
        {
                return m_begin;
        }

        T* end() noexcept
        {
                return m_end;
        }

        const T* end() const noexcept
        {
                return m_end;
        }
};

#endif // UTIL_SLICE_INCLUDE_GUARD
