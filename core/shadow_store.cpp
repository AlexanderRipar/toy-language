#include "core.hpp"
#include "structure.hpp"

struct ShadowStoreKey
{
	void* address;

	TypeId type;

	u32 attach_size;

	u32 attach_align;
};

struct ShadowStoreEntry
{
	union
	{
		struct
		{
			void* key_address;

			TypeId attach_type;

			CoreId attach_id;
		} data;

		struct
		{
			void* null_padding_;

			Maybe<ShadowStoreEntry*> next;
		} freelist;
	};

	bool is_equal_to_key(ShadowStoreKey key, [[maybe_unused]] u32 key_hash) const noexcept
	{
		return data.key_address == key.address;
	}

	u32 hash() const noexcept
	{
		return fnv1a(range::from_object_bytes(&data.key_address));
	}
};



ShadowStoreEntry* ShadowStoreAlloc::value_from_id(u32 id) noexcept
{
	ASSERT_OR_IGNORE(id < core->shadow.entries.used());

	ShadowStoreEntry* const value = core->shadow.entries.begin() + id;

	ASSERT_OR_IGNORE(value->freelist.null_padding_ != nullptr);

	return value;
}

const ShadowStoreEntry* ShadowStoreAlloc::value_from_id(u32 id) const noexcept
{
	ASSERT_OR_IGNORE(id < core->shadow.entries.used());

	ShadowStoreEntry* const value = core->shadow.entries.begin() + id;

	ASSERT_OR_IGNORE(value->freelist.null_padding_ != nullptr);

	return value;
}

u32 ShadowStoreAlloc::id_from_value(const ShadowStoreEntry* value) const noexcept
{
	ASSERT_OR_IGNORE(value >= core->shadow.entries.begin() && value < core->shadow.entries.end());

	ASSERT_OR_IGNORE(value->freelist.null_padding_ != nullptr);

	return static_cast<u32>(value - core->shadow.entries.begin());
}

ShadowStoreIterator ShadowStoreAlloc::values() noexcept
{
	ShadowStoreEntry* const entries = core->shadow.entries.begin();

	const u32 end = core->shadow.entries.used();

	u32 curr = 0;

	while (curr != end && entries[curr].data.key_address == nullptr)
		curr += 1;

	ShadowStoreIterator it;
	it.core = core;
	it.curr = curr;
	it.end = end;

	return it;
}

ShadowStoreEntry* ShadowStoreAlloc::alloc(ShadowStoreKey key, [[maybe_unused]] u32 key_hash) noexcept
{
	ShadowStoreEntry* entry;
	
	if (is_some(core->shadow.freelist_head))
	{
		entry = get(core->shadow.freelist_head);

		core->shadow.freelist_head = entry->freelist.next;
	}
	else
	{
		entry = core->shadow.entries.reserve();
	}

	const Maybe<void*> allocation = comp_heap_alloc(core, key.attach_size, key.attach_align);

	if (is_none(allocation))
		TODO("Implement GC traversal.");

	entry->data.key_address = key.address;
	entry->data.attach_type = key.type;
	entry->data.attach_id = core_id_from_address(core, get(allocation));

	return entry;
}

void ShadowStoreAlloc::dealloc(u32 id) noexcept
{
	ASSERT_OR_IGNORE(id < core->shadow.entries.used());

	ShadowStoreEntry* const entry = core->shadow.entries.begin() + id;

	entry->freelist.null_padding_ = nullptr;
	entry->freelist.next = core->shadow.freelist_head;

	core->shadow.freelist_head = some(entry);
}



bool ShadowStoreIterator::has_next() const noexcept
{
	ASSERT_OR_IGNORE(curr <= end);

	return curr != end;
}

ShadowStoreEntry* ShadowStoreIterator::next() noexcept
{
	ASSERT_OR_IGNORE(curr < end);

	ShadowStoreEntry* const entries = core->shadow.entries.begin();

	ShadowStoreEntry* const result = entries + curr;

	u32 next = curr + 1;

	while (next != end && entries[next].data.key_address == nullptr)
		next += 1;
		
	curr = next;

	return result;
}



static u64 calc_lookups_size(const Config* config, u64 page_size) noexcept
{
	u64 lookups_count = next_pow2(config->shadow_store.reserve * 3 / 2);

	if (lookups_count < 1024)
		lookups_count = 1024;

	ASSERT_OR_IGNORE(lookups_count <= UINT32_MAX);

	const u64 size = decltype(ShadowStore::address_map)::lookups_memory_size(static_cast<u32>(lookups_count));

	return size < page_size
		? page_size
		: size;
}

static u64 calc_entries_size(const Config* config, u64 page_size) noexcept
{
	const u64 page_mask = page_size - 1;

	return (config->shadow_store.reserve * sizeof(ShadowStoreEntry) + page_mask) & ~page_mask;
}

bool shadow_store_validate_config(const Config* config, PrintSink sink) noexcept
{
	if (config->shadow_store.reserve < config->shadow_store.commit_increment)
	{
		print(sink, "Configuration parameter `shadow-store.reserve` (%) must be greater than or equal to `shadow-store.commit_increment` (%).\n", config->shadow_store.reserve, config->shadow_store.commit_increment);

		return false;
	}

	return true;
}

MemoryRequirements shadow_store_memory_requirements(const Config* config) noexcept
{
	const u64 page_size = minos::page_bytes();

	MemoryRequirements reqs;
	reqs.count = 1;
	reqs.ranges[0].size = calc_lookups_size(config, page_size) + calc_entries_size(config, page_size);
	reqs.ranges[0].max_offset = UINT64_MAX;

	return reqs;
}

void shadow_store_init(CoreData* core, MemoryAllocation allocation) noexcept
{
	const u64 page_size = minos::page_bytes();

	const u64 page_mask = page_size - 1;

	const u64 lookups_size = calc_lookups_size(core->config, page_size);

	ASSERT_OR_IGNORE(allocation.ranges[0].count() == lookups_size + calc_entries_size(core->config, page_size));

	const u32 entries_commit_increment = static_cast<u32>((core->config->shadow_store.commit_increment + page_mask) & ~page_mask);

	core->shadow.address_map.init(allocation.ranges[0].mut_subrange(0, lookups_size), 512, ShadowStoreAlloc{ core });

	core->shadow.entries.init(allocation.ranges[0].mut_subrange(lookups_size), entries_commit_increment);

	core->shadow.freelist_head = none<ShadowStoreEntry*>();
}



bool shadow_get(CoreData* core, byte* address, TypeId shadow_type, u16 shadow_rank, CompValue* out) noexcept
{
	ShadowStoreKey key{};
	key.address = address;

	const Maybe<ShadowStoreEntry*> maybe_entry = core->shadow.address_map.try_value_from(key, fnv1a(range::from_object_bytes(&address)));

	if (is_none(maybe_entry))
		return false;

	ShadowStoreEntry* const entry = get(maybe_entry);

	if (!type_is_equal(core, entry->data.attach_type, shadow_type))
		return false;

	MemberInfo info;

	OpcodeId unused_initializer;

	if (!type_member_info_by_rank(core, shadow_type, shadow_rank, &info, &unused_initializer))
		ASSERT_UNREACHABLE;

	const TypeMetrics metrics = type_metrics_from_id(core, info.type_id);

	byte* const attach = static_cast<byte*>(address_from_core_id(core, entry->data.attach_id));

	const MutRange<byte> bytes{ attach + info.offset, metrics.size };

	*out = CompValue{ bytes, metrics.align, info.is_mut, info.type_id };

	return true;
}

void shadow_set(CoreData* core, byte* address, TypeId shadow_type, u16 shadow_rank, CompValue value) noexcept
{
	const TypeMetrics metrics = type_metrics_from_id(core, shadow_type);

	if (metrics.size > UINT32_MAX)
		TODO("Handle huge shadow data.");

	ShadowStoreKey key;
	key.address = address;
	key.attach_size = static_cast<u32>(metrics.size);
	key.attach_align = metrics.align;
	key.type = shadow_type;

	ShadowStoreEntry* entry = core->shadow.address_map.value_from(key, fnv1a(range::from_object_bytes(&address)));

	if (!type_is_equal(core, entry->data.attach_type, shadow_type))
	{
		const Maybe<void*> allocation = comp_heap_alloc(core, metrics.size, metrics.align);

		if (is_none(allocation))
			TODO("Implement GC traversal.");

		entry->data.attach_id = core_id_from_address(core, get(allocation));
		entry->data.attach_type = shadow_type;
	}

	u32 offset = type_shadow_member_offset(core, shadow_type, shadow_rank);

	byte* const attach = static_cast<byte*>(address_from_core_id(core, entry->data.attach_id));

	memcpy(attach + offset, value.bytes.begin(), value.bytes.count());
}

void shadow_clear(CoreData* core, byte* address) noexcept
{
	ShadowStoreKey key{};
	key.address = address;

	(void) core->shadow.address_map.try_remove(key, fnv1a(range::from_object_bytes(&address)));
}
