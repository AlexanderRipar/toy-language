#include "pass_data.hpp"

#include "../infra/container.hpp"
#include "../infra/range.hpp"
#include "../infra/hash.hpp"

struct alignas(8) IdentifierEntry
{
	u32 m_hash;

	u16 m_length;

	u8 m_attachment;

	#if COMPILER_MSVC
	#pragma warning(push)
	#pragma warning(disable : 4200) // C4200: nonstandard extension used: zero-sized array in struct/union
	#elif COMPILER_CLANG
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Wc99-extensions" // flexible array members are a C99 feature
	#elif COMPILER_GCC
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wpedantic" // ISO C++ forbids flexible array member
	#endif
	char8 m_chars[];
	#if COMPILER_MSVC
	#pragma warning(pop)
	#elif COMPILER_CLANG
	#pragma clang diagnostic pop
	#elif COMPILER_GCC
	#pragma GCC diagnostic pop
	#endif

	static constexpr u32 stride() noexcept
	{
		return 8;
	}

	static u32 required_strides(Range<char8> key) noexcept
	{
		return static_cast<u32>((offsetof(IdentifierEntry, m_chars) + key.count() + stride() - 1) / stride());
	}

	u32 used_strides() const noexcept
	{
		return static_cast<u32>((offsetof(IdentifierEntry, m_chars) + m_length + stride() - 1) / stride());
	}

	u32 hash() const noexcept
	{
		return m_hash;
	}

	bool equal_to_key(Range<char8> key, u32 key_hash) const noexcept
	{
		return m_hash == key_hash && key.count() == m_length && memcmp(key.begin(), m_chars, m_length) == 0;
	}

	void init(Range<char8> key, u32 key_hash) noexcept
	{
		m_hash = key_hash;

		m_length = static_cast<u16>(key.count());

		m_attachment = 0;

		memcpy(m_chars, key.begin(), key.count());
	}
};

struct IdentifierPool
{
	IndexMap<Range<char8>, IdentifierEntry> map;
};

IdentifierPool* create_identifier_pool(AllocPool* pool) noexcept
{
	IdentifierPool* const identifiers = static_cast<IdentifierPool*>(alloc_from_pool(pool, sizeof(IdentifierPool), alignof(IdentifierPool)));

	identifiers->map.init(1u << 24, 1u << 15, 1u << 31, 1u << 18);

	// Occupy index 0 with a nonsense value so it can be used as INVALID_IDENTIFIER_ID
	(void) identifiers->map.value_from(Range<char8>{ nullptr, nullptr }, fnv1a(Range<byte>{ nullptr, nullptr }));

	return identifiers;
}

void release_identifier_pool(IdentifierPool* identifiers) noexcept
{
	identifiers->map.release();
}

IdentifierId id_from_identifier(IdentifierPool* identifiers, Range<char8> identifier) noexcept
{
	return { identifiers->map.index_from(identifier, fnv1a(identifier.as_byte_range())) };
}

IdentifierId id_and_attachment_from_identifier(IdentifierPool* identifiers, Range<char8> identifier, u8* out_token) noexcept
{
	const IdentifierEntry* const entry = identifiers->map.value_from(identifier, fnv1a(identifier.as_byte_range()));

	*out_token = entry->m_attachment;

	return { identifiers->map.index_from(entry) };
}

void identifier_set_attachment(IdentifierPool* identifiers, Range<char8> identifier, u8 attachment) noexcept
{
	ASSERT_OR_IGNORE(attachment != 0);

	IdentifierEntry* const entry = identifiers->map.value_from(identifier, fnv1a(identifier.as_byte_range()));

	ASSERT_OR_IGNORE(entry->m_attachment == 0);
}

Range<char8> identifier_name_from_id(const IdentifierPool* identifiers, IdentifierId id) noexcept
{
	const IdentifierEntry* const entry = identifiers->map.value_from(id.rep);

	return { entry->m_chars, entry->m_hash };
}
