#ifndef RANGE_INCLUDE_GUARD
#define RANGE_INCLUDE_GUARD

#include "common.hpp"

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

	Range<byte> as_byte_range() const noexcept
	{
		return { reinterpret_cast<const byte*>(m_begin), reinterpret_cast<const byte*>(m_end) };
	}
};

template<typename T>
static inline Range<byte> byte_range_from(const T* t) noexcept
{
	return { reinterpret_cast<const byte*>(t), sizeof(T) };
}

static inline Range<char8> range_from_cstring(const char8* str) noexcept
{
	u64 len = 0;

	while (str[len] != '\0')
		++len;

	return { str, len };
}

template<size_t N>
static inline constexpr Range<char8> range_from_literal_string(const char8 (& arr)[N]) noexcept
{
	return { arr, N - 1 };
}

template<typename T>
struct MutRange
{
private:

	T* m_begin;

	T* m_end;

public:

	constexpr MutRange() noexcept : m_begin{ nullptr }, m_end{ nullptr } {}
	
	constexpr MutRange(T* begin, T* end) : m_begin{ begin }, m_end{ end } {}

	constexpr MutRange(T* begin, uint count) : m_begin{ begin }, m_end{ begin + count } {}

	template<uint COUNT>
	explicit constexpr MutRange(T(&arr)[COUNT]) : m_begin{ arr }, m_end{ arr + COUNT } {}

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

	T* begin() noexcept
	{
		return m_begin;
	}

	T* end() noexcept
	{
		return m_end;
	}

	uint count() const noexcept
	{
		return m_end - m_begin;
	}

	MutRange<byte> as_mut_byte_range() noexcept
	{
		return { reinterpret_cast<byte*>(m_begin), reinterpret_cast<byte*>(m_end) };
	}

	Range<byte> as_byte_range() const noexcept
	{
		return { reinterpret_cast<const byte*>(m_begin), reinterpret_cast<const byte*>(m_end) };
	}
};

#endif // RANGE_INCLUDE_GUARD
