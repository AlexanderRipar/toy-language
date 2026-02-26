#include "core.hpp"
#include "structure.hpp"

#include <cstring>

#include "../infra/types.hpp"
#include "../infra/assert.hpp"
#include "../infra/range.hpp"
#include "../infra/hash.hpp"
#include "../infra/container/index_map.hpp"

#include <cstddef>

struct alignas(8) IdentifierEntry
{
	u32 m_hash;

	u16 m_length;

	u8 m_attachment;

	#if COMPILER_GCC
		#pragma GCC diagnostic push
		#pragma GCC diagnostic ignored "-Wpedantic" // ISO C++ forbids flexible array member
	#endif
	char8 m_chars[];
	#if COMPILER_GCC
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

static constexpr u64 IDENTIFIER_LOOKUP_RESERVE = decltype(IdentifierPool::map)::lookups_memory_size(static_cast<u32>(1) << 21);

static constexpr u32 IDENTIFIER_LOOKUP_INITIAL_COMMIT_COUNT = static_cast<u32>(1) << 15;

static constexpr u64 IDENTIFIER_ENTRY_RESERVE = (static_cast<u64>(1) << 22) * IdentifierEntry::stride();

static constexpr u32 IDENTIFIER_ENTRY_COMMIT_INCREMENT_COUNT = static_cast<u32>(1) << 18;



MemoryRequirements identifier_pool_memory_requirements([[maybe_unused]] const Config* config) noexcept
{
	MemoryRequirements reqs;

	reqs.private_reserve = IDENTIFIER_LOOKUP_RESERVE;
	reqs.id_requirements_count = 1;
	reqs.id_requirements[0].reserve = IDENTIFIER_ENTRY_RESERVE;
	reqs.id_requirements[0].alignment = alignof(IdentifierEntry);

	return reqs;
}

void identifier_pool_init(CoreData* core, MemoryAllocation allocation) noexcept
{
	IdentifierPool* const identifiers = &core->identifiers;

	identifiers->map.init(
		{ allocation.private_data, IDENTIFIER_LOOKUP_RESERVE }, IDENTIFIER_LOOKUP_INITIAL_COMMIT_COUNT,
		{ allocation.ids[0], IDENTIFIER_ENTRY_RESERVE }, IDENTIFIER_ENTRY_COMMIT_INCREMENT_COUNT);
}



IdentifierId id_from_identifier(CoreData* core, Range<char8> identifier) noexcept
{
	return static_cast<IdentifierId>(core->identifiers.map.index_from(identifier, fnv1a(identifier.as_byte_range())) + static_cast<u32>(IdentifierId::FirstNatural));
}

IdentifierId id_and_attachment_from_identifier(CoreData* core, Range<char8> identifier, u8* out_token) noexcept
{
	const IdentifierEntry* const entry = core->identifiers.map.value_from(identifier, fnv1a(identifier.as_byte_range()));

	*out_token = entry->m_attachment;

	return static_cast<IdentifierId>(core->identifiers.map.index_from(entry) + static_cast<u32>(IdentifierId::FirstNatural));
}

void identifier_set_attachment(CoreData* core, Range<char8> identifier, u8 attachment) noexcept
{
	ASSERT_OR_IGNORE(attachment != 0);

	IdentifierEntry* const entry = core->identifiers.map.value_from(identifier, fnv1a(identifier.as_byte_range()));

	ASSERT_OR_IGNORE(entry->m_attachment == 0);

	entry->m_attachment = attachment;
}

Range<char8> identifier_name_from_id(const CoreData* core, IdentifierId id) noexcept
{
	ASSERT_OR_IGNORE(id >= IdentifierId::FirstNatural);

	const IdentifierEntry* const entry = core->identifiers.map.value_from(static_cast<u32>(id) - static_cast<u32>(IdentifierId::FirstNatural));

	return Range<char8>{ entry->m_chars, entry->m_length };
}
