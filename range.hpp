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
		return { reinterpret_cast<const byte*>(m_begin), count() * sizeof(T) };
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
		return { reinterpret_cast<byte*>(m_begin), count() * sizeof(T) };
	}

	Range<byte> as_byte_range() const noexcept
	{
		return { reinterpret_cast<const byte*>(m_begin), count() * sizeof(T) };
	}
};

template<typename T, typename Attach>
struct AttachmentRange
{
private:

	const T* m_begin;

	u32 m_count;

	Attach m_attachment;

	static_assert(sizeof(Attach) <= 4);

public:

	constexpr AttachmentRange(T* begin, T* end) noexcept :
		m_begin{ begin },
		m_count{ end - begin },
		m_attachment{}
	{}

	constexpr AttachmentRange(T* begin, u32 count) noexcept :
		m_begin{ begin },
		m_count{ count },
		m_attachment{}
	{}

	template<u32 COUNT>
	explicit constexpr AttachmentRange(const T(&arr)[COUNT]) noexcept :
		m_begin{ arr },
		m_count{ COUNT },
		m_attachment{}
	{}

	template<u32 COUNT>
	explicit constexpr AttachmentRange(T(&arr)[COUNT]) noexcept :
		m_begin{ arr },
		m_count{ COUNT },
		m_attachment{}
	{}

	constexpr AttachmentRange(const T* begin, const T* end, Attach attachment) noexcept :
		m_begin{ begin },
		m_count{ end - begin },
		m_attachment{ attachment }
	{}

	constexpr AttachmentRange(const T* begin, u32 count, Attach attachment) noexcept :
		m_begin{ begin },
		m_count{ count },
		m_attachment{ attachment }
	{}
	
	template<u32 COUNT>
	explicit constexpr AttachmentRange(const T(&arr)[COUNT], Attach attachment) noexcept :
		m_begin{ arr },
		m_count{ COUNT },
		m_attachment{ attachment }
	{}

	template<u32 COUNT>
	explicit constexpr AttachmentRange(T(&arr)[COUNT], Attach attachment) noexcept :
		m_begin{ arr },
		m_count{ COUNT },
		m_attachment{ attachment }
	{}

	const T& operator[](u32 i) const noexcept
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

	u32 count() const noexcept
	{
		return m_count;
	}

	Attach attachment() const noexcept
	{
		return m_attachment;
	}

	Range<T> range() const noexcept
	{
		return { m_begin, m_count };
	}

	Range<byte> as_byte_range() const noexcept
	{
		return { reinterpret_cast<const byte*>(m_begin), count() * sizeof(T) };
	}
};

namespace range
{
	template<typename T>
	inline Range<byte> from_object_bytes(const T* t) noexcept
	{
		return { reinterpret_cast<const byte*>(t), sizeof(T) };
	}
	
	inline Range<char8> from_cstring(const char8* str) noexcept
	{
		u64 len = 0;

		while (str[len] != '\0')
			++len;

		return { str, len };
	}

	template<uint N>
	inline constexpr Range<char8> from_literal_string(const char8 (&arr)[N]) noexcept
	{
		return { arr, N - 1 };
	}
	
	template<typename Attach>
	inline AttachmentRange<char8, Attach> from_cstring(const char8* str, Attach attachment) noexcept
	{
		u32 len = 0;

		while (str[len] != '\0')
			++len;

		return { str, len, attachment };
	}

	template<typename Attach, u32 N>
	inline constexpr AttachmentRange<char8, Attach> from_literal_string(const char8 (&arr)[N], Attach attachment) noexcept
	{
		return { arr, N - 1, attachment };
	}
}

#endif // RANGE_INCLUDE_GUARD
