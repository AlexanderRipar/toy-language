#ifndef RANGE_INCLUDE_GUARD
#define RANGE_INCLUDE_GUARD

#include "common.hpp"
#include <cassert>

template<typename T>
struct Range
{
private:

	const T* m_begin;

	const T* m_end;

public:

	constexpr Range() noexcept : m_begin{ nullptr }, m_end{ nullptr } {};

	constexpr Range(const T* begin, const T* end) : m_begin{ begin }, m_end{ end } {}

	constexpr Range(const T* begin, uint count) : m_begin{ begin }, m_end{ begin + count } {}

	template<uint COUNT>
	explicit constexpr Range(const T(&arr)[COUNT]) : m_begin{ arr }, m_end{ arr + COUNT } {}

	template<uint COUNT>
	explicit constexpr Range(T(&arr)[COUNT]) : m_begin{ arr }, m_end{ arr + COUNT } {}

	const T& operator[](uint i) const noexcept
	{
		assert(i < count());

		return m_begin[i];
	}

	T& operator[](uint i) noexcept
	{
		assert(i < count());

		return m_begin[i];
	}

	const T* begin() const noexcept
	{
		return m_begin;
	}

	const T* end() const noexcept
	{
		return m_end;
	}

	uint count() const noexcept
	{
		return m_end - m_begin;
	}
};

#endif // RANGE_INCLUDE_GUARD
