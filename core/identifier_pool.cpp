#include "core.hpp"
#include "structure.hpp"

#include <cstring>

#include "../infra/types.hpp"
#include "../infra/assert.hpp"
#include "../infra/range.hpp"
#include "../infra/hash.hpp"
#include "../infra/container/id_map.hpp"

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

	u32 hash() const noexcept
	{
		return m_hash;
	}

	bool is_equal_to_key(Range<char8> key, u32 key_hash) const noexcept
	{
		return m_hash == key_hash && key.count() == m_length && memcmp(key.begin(), m_chars, m_length) == 0;
	}
};

static constexpr u64 IDENTIFIER_LOOKUP_RESERVE = decltype(IdentifierPool::map)::lookups_memory_size(static_cast<u32>(1) << 21);

static constexpr u32 IDENTIFIER_LOOKUP_INITIAL_COMMIT_COUNT = static_cast<u32>(1) << 15;

static constexpr u64 IDENTIFIER_ENTRY_RESERVE = (static_cast<u64>(1) << 22) * alignof(IdentifierEntry);

static constexpr u32 IDENTIFIER_ENTRY_COMMIT_INCREMENT_COUNT = (static_cast<u32>(1) << 18) * alignof(IdentifierEntry);



bool IdentifierIterator::has_next() const noexcept
{
	ASSERT_OR_IGNORE(curr <= end);

	return curr != end;
}

IdentifierEntry* IdentifierIterator::next() noexcept
{
	ASSERT_OR_IGNORE(curr < end);

	IdentifierEntry* const result = reinterpret_cast<IdentifierEntry*>(reinterpret_cast<byte*>(core->identifiers.entries.begin()) + curr * alignof(IdentifierEntry));

	const u64 size = (offsetof(IdentifierEntry, m_chars) + result->m_length + alignof(IdentifierEntry) - 1) & ~(alignof(IdentifierEntry) - 1);

	curr += static_cast<u32>(size / alignof(IdentifierEntry));

	return result;
}



IdentifierEntry* IdentifierAlloc::value_from_id(u32 id) noexcept
{
	ASSERT_OR_IGNORE(id != 0);

	ASSERT_OR_IGNORE(id * alignof(IdentifierEntry) < core->identifiers.entries.used());

	return reinterpret_cast<IdentifierEntry*>(reinterpret_cast<byte*>(core->identifiers.entries.begin()) + id * alignof(IdentifierEntry));
}

const IdentifierEntry* IdentifierAlloc::value_from_id(u32 id) const noexcept
{
	ASSERT_OR_IGNORE(id != 0);

	ASSERT_OR_IGNORE(id * alignof(IdentifierEntry) < core->identifiers.entries.used());

	return reinterpret_cast<IdentifierEntry*>(reinterpret_cast<byte*>(core->identifiers.entries.begin()) + id * alignof(IdentifierEntry));
}

u32 IdentifierAlloc::id_from_value(const IdentifierEntry* value) const noexcept
{
	ASSERT_OR_IGNORE(reinterpret_cast<const byte*>(value) > core->identifiers.entries.begin());

	ASSERT_OR_IGNORE(reinterpret_cast<const byte*>(value) < core->identifiers.entries.end());

	return static_cast<u32>((reinterpret_cast<const byte*>(value) - core->identifiers.entries.begin()) / alignof(IdentifierEntry));
}

IdentifierIterator IdentifierAlloc::values() noexcept
{
	IdentifierIterator it;
	it.core = core;
	it.curr = 1;
	it.end = core->identifiers.entries.used() / alignof(IdentifierEntry);

	return it;
}

IdentifierEntry* IdentifierAlloc::alloc(Range<char8> key, u32 key_hash) noexcept
{
	const u32 raw_size = static_cast<u32>(offsetof(IdentifierEntry, m_chars) + key.count());

	const u32 size = (raw_size + alignof(IdentifierEntry) - 1) & ~(alignof(IdentifierEntry) - 1);

	IdentifierEntry* const result = reinterpret_cast<IdentifierEntry*>(core->identifiers.entries.reserve(size));
	result->m_hash = key_hash;
	result->m_length = static_cast<u16>(key.count());
	result->m_attachment = 0;
	memcpy(result->m_chars, key.begin(), key.count());

	return result;
}

void IdentifierAlloc::dealloc([[maybe_unused]] u32 id) noexcept
{
	ASSERT_UNREACHABLE;
}



bool identifier_pool_validate_config([[maybe_unused]] const Config* config, [[maybe_unused]] PrintSink sink) noexcept
{
	return true;
}

MemoryRequirements identifier_pool_memory_requirements([[maybe_unused]] const Config* config) noexcept
{
	MemoryRequirements reqs;
	reqs.count = 1;
	reqs.ranges[0].size = IDENTIFIER_LOOKUP_RESERVE + IDENTIFIER_ENTRY_RESERVE;
	reqs.ranges[0].max_offset = UINT64_MAX;

	return reqs;
}

void identifier_pool_init(CoreData* core, MemoryAllocation allocation) noexcept
{
	ASSERT_OR_IGNORE(allocation.ranges[0].count() == IDENTIFIER_LOOKUP_RESERVE + IDENTIFIER_ENTRY_RESERVE);

	const MutRange<byte> lookups_memory = allocation.ranges[0].mut_subrange(0, IDENTIFIER_LOOKUP_RESERVE);

	const MutRange<byte> entries_memory = allocation.ranges[0].mut_subrange(IDENTIFIER_LOOKUP_RESERVE, IDENTIFIER_ENTRY_RESERVE);

	core->identifiers.map.init(lookups_memory, IDENTIFIER_LOOKUP_INITIAL_COMMIT_COUNT, IdentifierAlloc{ core });

	core->identifiers.entries.init(entries_memory, IDENTIFIER_ENTRY_COMMIT_INCREMENT_COUNT);

	(void) core->identifiers.entries.reserve(sizeof(IdentifierEntry));
}



IdentifierId id_from_identifier(CoreData* core, Range<char8> identifier) noexcept
{
	return static_cast<IdentifierId>(core->identifiers.map.id_from(identifier, fnv1a(identifier.as_byte_range())) + static_cast<u32>(IdentifierId::FirstNatural));
}

IdentifierId id_and_attachment_from_identifier(CoreData* core, Range<char8> identifier, u8* out_token) noexcept
{
	const IdentifierEntry* const entry = core->identifiers.map.value_from(identifier, fnv1a(identifier.as_byte_range()));

	*out_token = entry->m_attachment;

	return static_cast<IdentifierId>(core->identifiers.map.id_from(entry) + static_cast<u32>(IdentifierId::FirstNatural));
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
