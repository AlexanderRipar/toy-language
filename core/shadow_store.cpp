#include "core.hpp"
#include "structure.hpp"

struct alignas(8) ShadowLayoutMember
{
	TypeId type_id;

	u32 offset;
};

struct alignas(8) ShadowLayoutHeader
{
	u16 member_count;

	u32 align;

	u32 hash;

	u32 size;
};

struct alignas(8) ShadowLayout
{
	ShadowLayoutHeader header;

	#if COMPILER_GCC
		#pragma GCC diagnostic push
		#pragma GCC diagnostic ignored "-Wpedantic" // ISO C++ forbids flexible array member
	#endif
	ShadowLayoutMember members[];
	#if COMPILER_GCC
		#pragma GCC diagnostic pop
	#endif
};



struct ShadowStoreKey
{
	void* address;

	ShadowLayoutId layout_id;

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

			ShadowLayoutId attach_layout_id;

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



struct ShadowLayoutKey
{
	ShadowLayout* layout;

	CoreData* core;
};

struct ShadowLayoutEntry
{
	ShadowLayoutHeader layout;

	u32 hash() const noexcept
	{
		return layout.hash;
	}

	bool is_equal_to_key(ShadowLayoutKey key, u32 key_hash) const noexcept
	{
		if (layout.hash != key_hash)
			return false;

		if (layout.member_count != key.layout->header.member_count)
			return false;

		const ShadowLayoutMember* const members = reinterpret_cast<const ShadowLayoutMember*>(&layout + 1);

		for (u32 i = 0; i != layout.member_count; ++i)
		{
			if (type_is_equal(key.core, members[i].type_id, key.layout->members[i].type_id) != TypeEquality::Equal)
				return false;
		}

		return true;
	}
};



ShadowLayoutEntry* ShadowLayoutAlloc::value_from_id(u32 id) noexcept
{
	return static_cast<ShadowLayoutEntry*>(address_from_core_id(core, static_cast<CoreId>(id)));
}

const ShadowLayoutEntry* ShadowLayoutAlloc::value_from_id(u32 id) const noexcept
{
	return static_cast<ShadowLayoutEntry*>(address_from_core_id(core, static_cast<CoreId>(id)));
}

u32 ShadowLayoutAlloc::id_from_value(const ShadowLayoutEntry* value) const noexcept
{
	return static_cast<u32>(core_id_from_address(core, value));
}

ShadowLayoutIterator ShadowLayoutAlloc::values() noexcept
{
	ShadowLayoutIterator it;
	it.core = core;
	it.curr = 0;
	it.end = core->shadow.layout_ids.used();

	return it;
}

ShadowLayoutEntry* ShadowLayoutAlloc::alloc(ShadowLayoutKey key, [[maybe_unused]] u32 key_hash) noexcept
{
	const CoreId id = core_id_from_address(core, key.layout);

	core->shadow.layout_ids.append(id);

	return reinterpret_cast<ShadowLayoutEntry*>(key.layout);
}

void ShadowLayoutAlloc::dealloc([[maybe_unused]] u32 id) noexcept
{
	// no-op.
}



bool ShadowLayoutIterator::has_next() const noexcept
{
	ASSERT_OR_IGNORE(curr <= end);

	return curr != end;
}

ShadowLayoutEntry* ShadowLayoutIterator::next() noexcept
{
	ASSERT_OR_IGNORE(curr < end);

	const CoreId id = core->shadow.layout_ids.begin()[curr];

	curr += 1;

	return static_cast<ShadowLayoutEntry*>(address_from_core_id(core, id));
}



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
	
	if (is_some(core->shadow.entries_freelist_head))
	{
		entry = get(core->shadow.entries_freelist_head);

		core->shadow.entries_freelist_head = entry->freelist.next;
	}
	else
	{
		entry = core->shadow.entries.reserve();
	}

	const Maybe<void*> allocation = comp_heap_alloc(core, key.attach_size, key.attach_align);

	if (is_none(allocation))
		TODO("Implement GC traversal.");

	entry->data.key_address = key.address;
	entry->data.attach_layout_id = key.layout_id;
	entry->data.attach_id = core_id_from_address(core, get(allocation));

	return entry;
}

void ShadowStoreAlloc::dealloc(u32 id) noexcept
{
	ASSERT_OR_IGNORE(id < core->shadow.entries.used());

	ShadowStoreEntry* const entry = core->shadow.entries.begin() + id;

	entry->freelist.null_padding_ = nullptr;
	entry->freelist.next = core->shadow.entries_freelist_head;

	core->shadow.entries_freelist_head = some(entry);
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



static u32 hash_shadow_layout(CoreData* core, const ShadowLayout* layout) noexcept
{
	const u16 member_count = layout->header.member_count;

	u32 hash = FNV1A_SEED;

	for (u16 i = 0; i != member_count; ++i)
	{
		TypeId holotype_id;

		if (!type_get_holotype(core, layout->members[i].type_id, &holotype_id))
			ASSERT_UNREACHABLE;

		hash = fnv1a_step(hash, range::from_object_bytes(&holotype_id));
	}

	return hash;
}

static ShadowLayoutId intern_shadow_layout(CoreData* core, ShadowLayout* layout) noexcept
{
	ShadowLayoutKey key;
	key.layout = layout;
	key.core = core;

	const ShadowLayoutEntry* const interned = core->shadow.layouts.value_from(key, layout->header.hash);
	
	if (interned != reinterpret_cast<ShadowLayoutEntry*>(layout))
	{
		const u64 size = sizeof(ShadowLayout) + layout->header.member_count * sizeof(ShadowLayoutMember);

		comp_heap_dealloc(core, MutRange<byte>{ reinterpret_cast<byte*>(layout), size });
	}

	return static_cast<ShadowLayoutId>(core_id_from_address(core, interned));
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

	core->shadow.entries_freelist_head = none<ShadowStoreEntry*>();
}



ShadowLayoutId shadow_create_layout(CoreData* core, Range<ShadowLayoutMemberInitializer> initializers) noexcept
{
	const u64 size = sizeof(ShadowLayout) + initializers.count() * sizeof(ShadowLayoutMember);

	const Maybe<void*> allocation = comp_heap_alloc(core, size, alignof(ShadowLayout));

	if (is_none(allocation))
		TODO("Implement GC traversal.");

	ShadowLayout* const layout = static_cast<ShadowLayout*>(get(allocation));

	u32 align = 0;

	u32 offset = 0;

	for (u16 i = 0; i != initializers.count(); ++i)
	{
		const ShadowLayoutMemberInitializer* const src = initializers.begin() + i;

		ShadowLayoutMember* const dst = layout->members + i;

		TypeMetrics metrics;
		
		if (!type_metrics_from_id(core, src->type_id, &metrics))
			ASSERT_UNREACHABLE;

		ASSERT_OR_IGNORE(metrics.is_shadow);

		if (align < metrics.align)
			align = metrics.align;

		offset = (offset + metrics.align - 1) & ~(metrics.align - 1);

		dst->type_id = src->type_id;
		dst->offset = offset;

		offset += static_cast<u32>(metrics.size);
		
		dst->type_id = src->type_id;
		dst->offset = offset;
	}

	layout->header.member_count = static_cast<u16>(initializers.count());
	layout->header.align = align;
	layout->header.hash = hash_shadow_layout(core, layout);
	layout->header.size = offset;

	return intern_shadow_layout(core, layout);
}

u32 shadow_layout_offset(CoreData* core, ShadowLayoutId layout_id, u16 rank) noexcept
{
	ShadowLayout* const layout = static_cast<ShadowLayout*>(address_from_core_id(core, static_cast<CoreId>(layout_id)));

	ASSERT_OR_IGNORE(rank < layout->header.member_count);

	return layout->members[rank].offset;
}

Maybe<byte*> shadow_get(CoreData* core, byte* address, ShadowLayoutId layout_id, u16 rank) noexcept
{
	ShadowStoreKey key{};
	key.address = address;

	const Maybe<ShadowStoreEntry*> maybe_entry = core->shadow.address_map.try_value_from(key, fnv1a(range::from_object_bytes(&address)));

	if (is_none(maybe_entry))
		return none<byte*>();

	ShadowStoreEntry* const entry = get(maybe_entry);

	if (entry->data.attach_layout_id != layout_id)
		return none<byte*>();

	const u32 offset = shadow_layout_offset(core, layout_id, rank);

	byte* const shadow_base = static_cast<byte*>(address_from_core_id(core, entry->data.attach_id));

	return some(shadow_base + offset);
}

void shadow_set(CoreData* core, byte* address, ShadowLayoutId layout_id, u16 rank, Range<byte> value) noexcept
{
	ShadowLayout* const layout = static_cast<ShadowLayout*>(address_from_core_id(core, static_cast<CoreId>(layout_id)));

	ShadowStoreKey key;
	key.address = address;
	key.attach_size = layout->header.size;
	key.attach_align = layout->header.align;
	key.layout_id = layout_id;

	ShadowStoreEntry* entry = core->shadow.address_map.value_from(key, fnv1a(range::from_object_bytes(&address)));

	if (entry->data.attach_layout_id != layout_id)
	{
		const Maybe<void*> allocation = comp_heap_alloc(core, layout->header.size, layout->header.align);

		if (is_none(allocation))
			TODO("Implement GC traversal.");

		entry->data.attach_id = core_id_from_address(core, get(allocation));
		entry->data.attach_layout_id = layout_id;
	}

	const u32 offset = shadow_layout_offset(core, layout_id, rank);

	byte* const attach = static_cast<byte*>(address_from_core_id(core, entry->data.attach_id));

	memcpy(attach + offset, value.begin(), value.count());
}

void shadow_clear(CoreData* core, byte* address) noexcept
{
	ShadowStoreKey key{};
	key.address = address;

	(void) core->shadow.address_map.try_remove(key, fnv1a(range::from_object_bytes(&address)));
}
