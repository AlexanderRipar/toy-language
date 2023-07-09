#ifndef UTIL_STRVIEW_INCLUDE_GUARD
#define UTIL_STRVIEW_INCLUDE_GUARD

#include <cstdint>
#include <cstring>

#include "types.hpp"
#include "strutil.hpp"

struct strview
{
	const char* m_beg;

	const char* m_end;

	template<size_t Bytes>
	static constexpr strview from_literal(const char (&lit)[Bytes]) noexcept
	{
		return { lit, Bytes - 1 };
	}

	constexpr strview() noexcept
		: m_beg{ nullptr }, m_end{ nullptr } {}

	constexpr strview(const char* beg, const char* end) noexcept
		: m_beg{ beg }, m_end{ end } {}

	constexpr strview(const char* beg, size_t len) noexcept
		: m_beg{ beg }, m_end{ beg + len } {}

	strview(const char* cstr) noexcept
		: m_beg{ cstr }, m_end{ cstr + strlen(cstr) } {}

	constexpr size_t len() const noexcept
	{
		return m_end - m_beg;
	}

	constexpr const char* begin() const noexcept
	{
		return m_beg;
	}

	constexpr const char* end() const noexcept
	{
		return m_end;
	}

	constexpr char operator[](size_t n) const noexcept
	{
		return m_beg[n];
	}
};

constexpr bool streqc(const strview& v1, const strview& v2) noexcept
{
	if (v1.len() != v2.len())
		return false;

	for (size_t i = 0; i != v1.len(); ++i)
		if (v1.m_beg[i] != v2.m_beg[i])
			return false;

	return true;
}

constexpr bool streqi(const strview& v1, const strview& v2) noexcept
{
	if (v1.len() != v2.len())
		return false;

	for (size_t i = 0; i != v1.len(); ++i)
	{
		char c1 = v1.m_beg[i];

		char c2 = v2.m_beg[i];

		if (c1 >= 'A' && c1 <= 'Z')
			c1 = c1 - 'A' + 'a';

		if (c2 >= 'A' && c2 <= 'Z')
			c2 = c2 - 'A' + 'a';

		if (c1 != c2)
			return false;
	}

	return true;
}

constexpr uint64_t strhashc64(const strview& v) noexcept
{
	uint64_t h = 14695981039346656037ui64;

	for (char c : v)
	{
		h ^= c;

		h *= 1099511628211ui64;
	}

	return h;
}

constexpr uint64_t strhashi64(const strview& v) noexcept
{
	uint64_t h = 14695981039346656037ui64;

	for (char c : v)
	{
		if (c >= 'A' && c <= 'Z')
			c = c - 'A' + 'a';

		h ^= c;

		h *= 1099511628211ui64;
	}

	return h;
}

constexpr uint32_t strhashc32(const strview& v) noexcept
{
	uint32_t h = 2166136261ui32;

	for (char c : v)
	{
		h ^= c;

		h *= 16777619ui32;
	}

	return h;
}

constexpr uint32_t strhashi32(const strview& v) noexcept
{
	uint32_t h = 2166136261ui32;

	for (char c : v)
	{
		if (c >= 'A' && c <= 'Z')
			c = c - 'A' + 'a';

		h ^= c;

		h *= 16777619ui32;
	}

	return h;
}

constexpr ptrdiff_t strcmpc(const strview& v1, const strview& v2) noexcept
{
	const size_t len1 = v1.len();

	const size_t len2 = v2.len();

	const size_t min_len = len1 < len2 ? len1 : len2;

	for (size_t i = 0; i != min_len; ++i)
	{
		const char c1 = v1[i];
		
		const char c2 = v2[i];

		if (c1 != c2)
			return c1 - c2;
	}

	return len1 - len2;
}

constexpr ptrdiff_t strcmpi(const strview& v1, const strview& v2) noexcept
{
	const size_t len1 = v1.len();

	const size_t len2 = v2.len();

	const size_t min_len = len1 < len2 ? len1 : len2;

	for (size_t i = 0; i != min_len; ++i)
	{
		const char c1 = ascii_lower(v1[i]);
		
		const char c2 = ascii_lower(v2[i]);

		if (c1 != c2)
			return c1 - c2;
	}

	return len1 - len2;
}

constexpr bool strstrc(const strview& v1, const strview& v2) noexcept
{
	const size_t len1 = v1.len();

	const size_t len2 = v2.len();

	if (len1 < len2)
		return false;

	for (size_t i = 0; i != len1 + 1 - len2; ++i)
	{
		bool is_match = true;

		for (uint32_t j = 0; j != len2; ++j)
			if (v1[i + j] != v2[j])
			{
				is_match = false;

				break;
			}

		if (is_match)
			return true;
	}

	return false;
}

constexpr bool strstri(const strview& v1, const strview& v2) noexcept
{
	const size_t len1 = v1.len();

	const size_t len2 = v2.len();

	if (len1 < len2)
		return false;

	for (size_t i = 0; i != len1 + 1 - len2; ++i)
	{
		bool is_match = true;

		for (uint32_t j = 0; j != len2; ++j)
			if (ascii_lower(v1[i + j]) != ascii_lower(v2[j]))
			{
				is_match = false;

				break;
			}

		if (is_match)
			return true;
	}

	return false;
}

struct hashed_strview
{
	const char8* m_beg;

	u32 m_len;

	u32 m_hash;

	template<size_t Bytes>
	static constexpr hashed_strview from_literal(const char (&lit)[Bytes]) noexcept
	{
		return hashed_strview{ lit, Bytes - 1, strhashc32(strview{ lit, Bytes - 1 }) };
	}

	constexpr hashed_strview() noexcept :
		m_beg{ nullptr }, m_len{ 0 }, m_hash{ 0 } {}

	constexpr hashed_strview(const char* beg, const char* end) noexcept :
		m_beg{ beg }, m_len{ static_cast<u32>(end - beg) }, m_hash{ strhashc32(strview{ beg, end }) } {}

	constexpr hashed_strview(const char* beg, u32 len) noexcept :
		m_beg{ beg }, m_len{ len }, m_hash{ strhashc32(strview{ beg, len }) } {}

	constexpr hashed_strview(const strview view) noexcept :
		m_beg{ view.begin() }, m_len{ static_cast<u32>(view.len()) }, m_hash{ strhashc32(view) } {}

	hashed_strview(const char* cstr) noexcept :
		m_beg{ cstr }, m_len{ static_cast<u32>(strlen(cstr)) }, m_hash{ strhashc32(strview{ cstr, m_len }) } {}

	constexpr strview to_strview() const noexcept
	{
		return strview{ m_beg, m_len };
	}

	constexpr size_t len() const noexcept
	{
		return m_len;
	}

	constexpr const char* begin() const noexcept
	{
		return m_beg;
	}

	constexpr const char* end() const noexcept
	{
		return m_beg + m_len;
	}

	constexpr char operator[](size_t n) const noexcept
	{
		return m_beg[n];
	}

	constexpr u32 hash() const noexcept
	{
		return m_hash;
	}
};

constexpr bool streqc(const hashed_strview& v1, const hashed_strview& v2) noexcept
{
	if (v1.hash() != v2.hash() || v1.len() != v2.len())
		return false;

	for (size_t i = 0; i != v1.len(); ++i)
		if (v1.m_beg[i] != v2.m_beg[i])
			return false;

	return true;
}

#endif // UTIL_STRVIEW_INCLUDE_GUARD
