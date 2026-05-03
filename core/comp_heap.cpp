#include "core.hpp"
#include "structure.hpp"

#include "../infra/minos/minos.hpp"
#include "../infra/panic.hpp"
#include "../infra/math.hpp"
#include "../infra/inplace_sort.hpp"

static constexpr u64 BYTES_PER_BITMAP_BYTE = 8 * COMP_HEAP_MIN_ALLOCATION_SIZE;

struct alignas(8) CompHeapAllocationHeader
{
	TypeId type_id;

	u64 size;
};



static void comp_heap_add_to_freelist(CoreData* core, MutRange<byte> memory) noexcept
{
	ASSERT_OR_IGNORE(memory.begin() >= core->heap.memory);

	ASSERT_OR_IGNORE(memory.end() <= core->heap.memory + core->heap.used);

	ASSERT_OR_IGNORE((reinterpret_cast<u64>(memory.begin()) & COMP_HEAP_ZERO_ADDRESS_MASK) == 0);

	u64 size = memory.count();

	u64 mask = COMP_HEAP_MIN_ALLOCATION_SIZE;

	u8 index = 0;

	byte* curr = memory.begin();

	ASSERT_OR_IGNORE(size >= mask);

	while (size != 0 && index != array_count(core->heap.freelists))
	{
		if ((size & mask) != 0)
		{
			*reinterpret_cast<u32*>(curr) = core->heap.freelists[index];

			core->heap.freelists[index] = static_cast<u32>((curr - core->heap.memory) / COMP_HEAP_MIN_ALLOCATION_SIZE);

			curr += mask;

			size -= mask;
		}

		mask <<= 1;

		index += 1;
	}

	while (size != 0)
	{
		ASSERT_OR_IGNORE(size >= COMP_HEAP_MAX_FREELIST_SIZE);

		*reinterpret_cast<u32*>(curr) = core->heap.freelists[array_count(core->heap.freelists) - 1]; 
		
		core->heap.freelists[array_count(core->heap.freelists) - 1] = static_cast<u32>((curr - core->heap.memory) / COMP_HEAP_MIN_ALLOCATION_SIZE);

		size -= COMP_HEAP_MAX_FREELIST_SIZE;

		curr += COMP_HEAP_MAX_FREELIST_SIZE;
	}
}

static bool comp_heap_ensure_commit(CoreData* core, u64 required) noexcept
{
	if (required <= core->heap.commit)
		return true;

	if (required > core->heap.reserve)
		return false;

	const u64 old_commit = core->heap.commit;

	const u64 new_commit = (required + core->heap.commit_increment - 1) & ~(core->heap.commit_increment - 1);

	ASSERT_OR_IGNORE(new_commit <= core->heap.reserve);

	const u64 commit_difference = new_commit - old_commit;

	if (!minos::mem_commit(core->heap.memory + old_commit, commit_difference))
		return false;

	const u64 old_bitmap_commit = old_commit / BYTES_PER_BITMAP_BYTE;

	const u64 new_bitmap_commit = new_commit / BYTES_PER_BITMAP_BYTE;

	ASSERT_OR_IGNORE(new_bitmap_commit != old_bitmap_commit);

	if (!minos::mem_commit(reinterpret_cast<byte*>(core->heap.leak_bitmap) + old_bitmap_commit, new_bitmap_commit - old_bitmap_commit))
		return false;

	if (!minos::mem_commit(reinterpret_cast<byte*>(core->heap.begin_bitmap) + old_bitmap_commit, new_bitmap_commit - old_bitmap_commit))
		return false;

	if (!minos::mem_commit(reinterpret_cast<byte*>(core->heap.header_bitmap) + old_bitmap_commit, new_bitmap_commit - old_bitmap_commit))
		return false;

	core->heap.commit = new_commit;

	return true;
}

static void comp_heap_mark_bitmap_bits(CoreData* core, u64* bitmap, MutRange<byte> memory) noexcept
{
	const u64 begin = (memory.begin() - core->heap.memory) / COMP_HEAP_MIN_ALLOCATION_SIZE;

	const u64 end = (memory.end() + COMP_HEAP_ZERO_ADDRESS_MASK - core->heap.memory) / COMP_HEAP_MIN_ALLOCATION_SIZE;

	for (u64 i = begin; i != end; ++i)
		bitmap[i >> 6] |= static_cast<u64>(1) << (i & 63);
}

static void comp_heap_mark_bitmap_bit(CoreData* core, u64* bitmap, byte* memory) noexcept
{
	const u64 byte_offset = memory - core->heap.memory;

	const u64 slot_offset = byte_offset / COMP_HEAP_MIN_ALLOCATION_SIZE;

	ASSERT_OR_IGNORE((bitmap[slot_offset >> 6] & (static_cast<u64>(1) << (slot_offset & 63))) == 0);

	bitmap[slot_offset >> 6] |= static_cast<u64>(1) << (slot_offset & 63);
}

static Maybe<void*> comp_heap_alloc_internal(CoreData* core, u64 size, u64 align, bool needs_header) noexcept
{
	ASSERT_OR_IGNORE(is_pow2(align));

	const u64 header_size = needs_header ? COMP_HEAP_MIN_ALLOCATION_SIZE : 0;

	// Try satisfying allocations with a suitable alignment and size from the
	// relevant freelist.
	if (align <= COMP_HEAP_MIN_ALLOCATION_SIZE && size + header_size <= COMP_HEAP_MAX_FREELIST_SIZE_LOG2)
	{
		const u64 allocation_size = size + header_size;

		const u8 size_log2_ceil = log2_ceil(allocation_size);

		const u8 index = size_log2_ceil < COMP_HEAP_MIN_ALLOCATION_SIZE_LOG2
			? 0
			: size_log2_ceil - COMP_HEAP_MIN_ALLOCATION_SIZE_LOG2;

		u32 const head = core->heap.freelists[index];

		if (head != 0)
		{
			byte* const entry_begin = core->heap.memory + head * COMP_HEAP_MIN_ALLOCATION_SIZE;

			core->heap.freelists[index] = *reinterpret_cast<u32*>(entry_begin);

			const u64 entry_size = static_cast<u64>(1) << size_log2_ceil;

			if (entry_size - allocation_size >= COMP_HEAP_MIN_ALLOCATION_SIZE)
				comp_heap_add_to_freelist(core, MutRange<byte>{ entry_begin + allocation_size, entry_begin + entry_size });

			comp_heap_mark_bitmap_bit(core, core->heap.begin_bitmap, entry_begin);

			return some<void*>(entry_begin + header_size);
		}
	}

	const u64 unaligned_begin = core->heap.used + header_size;

	const u64 aligned_begin = (unaligned_begin + align - 1) & ~(align - 1);

	const u64 new_used = (aligned_begin + size + COMP_HEAP_ZERO_ADDRESS_MASK) & ~COMP_HEAP_ZERO_ADDRESS_MASK;

	if (!comp_heap_ensure_commit(core, new_used))
		return none<void*>();

	core->heap.used = new_used;

	// If we had to insert padding to achieve the requested alignment, add it
	// to the relevant freelist.
	if (unaligned_begin != aligned_begin)
		comp_heap_add_to_freelist(core, MutRange<byte>{ core->heap.memory + unaligned_begin, core->heap.memory + aligned_begin });

	byte* const begin = core->heap.memory + aligned_begin;

	comp_heap_mark_bitmap_bit(core, core->heap.begin_bitmap, begin - header_size);

	return some<void*>(begin);
}

static Maybe<CompHeapAllocationHeader*> comp_heap_find_header(CoreData* core, byte* address) noexcept
{
	ASSERT_OR_IGNORE(address > core->heap.memory);

	ASSERT_OR_IGNORE(address < core->heap.memory + core->heap.used);

	const u64 slot_offset = (address - core->heap.memory) / COMP_HEAP_MIN_ALLOCATION_SIZE;

	const u64* const begin_bitmap = core->heap.begin_bitmap;

	const u64 lower_slots_mask = (~static_cast<u64>(0)) >> (slot_offset & 63);

	u64 i = slot_offset >> 6;

	u64 curr = begin_bitmap[i] & lower_slots_mask;

	while (curr == 0)
	{
		ASSERT_OR_IGNORE(i != 0);

		i -= 1;

		curr = begin_bitmap[i];
	}

	const u8 offset_in_qword = count_trailing_zeros_assume_one(curr);

	if ((core->heap.header_bitmap[i] & (static_cast<u64>(1) << offset_in_qword)) == 0)
		return none<CompHeapAllocationHeader*>();

	const u64 begin_byte_offset = i * BYTES_PER_BITMAP_BYTE * sizeof(u64) + offset_in_qword * COMP_HEAP_MIN_ALLOCATION_SIZE;

	return some<>(reinterpret_cast<CompHeapAllocationHeader*>(core->heap.memory + begin_byte_offset));
}



static u64 calc_commit_increment(const Config* config, u64 page_size) noexcept
{
	const u64 min_size = page_size * BYTES_PER_BITMAP_BYTE;

	return config->heap.commit_increment < min_size
		? min_size
		: next_pow2(config->heap.commit_increment);
}

static u64 calc_bitmap_reserve(u64 page_size, u64 heap_size) noexcept
{
	const u64 page_mask = page_size - 1;

	return (heap_size / BYTES_PER_BITMAP_BYTE + page_mask) & ~page_mask;
}

static u64 calc_memory_reserve(const Config* config, u64 commit_increment) noexcept
{
	const u64 commit_increment_mask = commit_increment - 1;

	return (config->heap.reserve + commit_increment_mask) & ~commit_increment_mask;
}



bool comp_heap_validate_config(const Config* config, PrintSink sink) noexcept
{
	if (config->heap.reserve < config->heap.commit_increment)
	{
		print(sink, "Configuration parameter `heap.reserve` (%) must be greater than or equal to `heap.commit_increment` (%).\n", config->heap.reserve, config->heap.commit_increment);

		return false;
	}

	return true;
}

MemoryRequirements comp_heap_memory_requirements(const Config* config) noexcept
{
	const u64 page_size = minos::page_bytes();

	const u64 commit_increment = calc_commit_increment(config, page_size);

	const u64 heap_size = calc_memory_reserve(config, commit_increment);

	const u64 bitmap_size = calc_bitmap_reserve(page_size, heap_size);

	MemoryRequirements reqs{};
	reqs.count = 2;
	reqs.ranges[0].size = heap_size;
	reqs.ranges[0].max_offset = static_cast<u64>(UINT32_MAX) * COMP_HEAP_MIN_ALLOCATION_SIZE;
	reqs.ranges[1].size = 4 * bitmap_size + page_size; // Overallocate a page for gc bitmap end sentinels.
	reqs.ranges[1].max_offset = UINT64_MAX;

	return reqs;
}

void comp_heap_init(CoreData* core, MemoryAllocation allocation) noexcept
{
	const u64 page_size = minos::page_bytes();

	const u64 commit_increment = calc_commit_increment(core->config, page_size);

	const u64 heap_size = calc_memory_reserve(core->config, commit_increment);

	ASSERT_OR_IGNORE(allocation.ranges[0].count() == heap_size);

	const u64 bitmap_size = calc_bitmap_reserve(page_size, heap_size);

	ASSERT_OR_IGNORE(allocation.ranges[1].count() == 4 * bitmap_size + page_size);

	const u64 bitmap_commit = commit_increment / BYTES_PER_BITMAP_BYTE;

	if (!minos::mem_commit(allocation.ranges[0].begin(), commit_increment))
		panic("Could not commit % bytes of memory for compile-time heap (0x%[|X]).\n", commit_increment, minos::last_error());

	if (!minos::mem_commit(allocation.ranges[1].begin(), bitmap_commit))
		panic("Could not commit % bytes of memory for compile-time heap leak bitmap (0x%[|X]).\n", bitmap_commit, minos::last_error());

	if (!minos::mem_commit(allocation.ranges[1].begin() + bitmap_size, bitmap_commit))
		panic("Could not commit % bytes of memory for compile-time heap allocation begin bitmap (0x%[|X]).\n", bitmap_commit, minos::last_error());

	if (!minos::mem_commit(allocation.ranges[1].begin() + 2 * bitmap_size, bitmap_commit))
		panic("Could not commit % bytes of memory for compile-time heap header bitmap (0x%[|X]).\n", bitmap_commit, minos::last_error());

	core->heap.memory = allocation.ranges[0].begin();
	core->heap.used = COMP_HEAP_MIN_ALLOCATION_SIZE; // Reserve the slot as a pseudo-null value for indices.
	core->heap.commit = commit_increment;
	core->heap.reserve = heap_size;
	core->heap.commit_increment = commit_increment;
	core->heap.leak_bitmap = reinterpret_cast<u64*>(allocation.ranges[1].begin());
	core->heap.begin_bitmap = reinterpret_cast<u64*>(allocation.ranges[1].begin() + bitmap_size);
	core->heap.header_bitmap = reinterpret_cast<u64*>(allocation.ranges[1].begin() + 2 * bitmap_size);
	core->heap.gc_bitmap = reinterpret_cast<u64*>(allocation.ranges[1].begin() + 3 * bitmap_size);

	memset(core->heap.freelists, 0, sizeof(core->heap.freelists));
}



Maybe<void*> comp_heap_alloc(CoreData* core, u64 size, u64 align) noexcept
{
	return comp_heap_alloc_internal(core, size, align, false);
}

Maybe<void*> comp_heap_alloc_global_member(CoreData* core, u64 size, u64 align, TypeId type_id) noexcept
{
	static_assert(sizeof(CompHeapAllocationHeader) <= COMP_HEAP_MIN_ALLOCATION_SIZE);

	const Maybe<void*> allocation = comp_heap_alloc_internal(core, size, align, true);

	if (is_none(allocation))
		return none<void*>();

	CompHeapAllocationHeader* const header = reinterpret_cast<CompHeapAllocationHeader*>(static_cast<byte*>(get(allocation)) - COMP_HEAP_MIN_ALLOCATION_SIZE);
	header->type_id = type_id;
	header->size = size;

	comp_heap_mark_bitmap_bit(core, core->heap.header_bitmap, static_cast<byte*>(get(allocation)) - COMP_HEAP_MIN_ALLOCATION_SIZE);

	return allocation;
}

TypeId comp_heap_global_member_type(CoreData* core, byte* address) noexcept
{
	const Maybe<CompHeapAllocationHeader*> header = comp_heap_find_header(core, address);

	if (is_none(header))
		ASSERT_UNREACHABLE;

	return get(header)->type_id;
}

bool comp_heap_leak(CoreData* core, MutRange<byte> memory) noexcept
{
	// If the address is outside the heap, there is nothing to leak for us.
	if (memory.end() <= core->heap.memory || memory.begin() >= core->heap.memory + core->heap.used)
		return true;

	ASSERT_OR_IGNORE(memory.begin() > core->heap.memory);
	
	ASSERT_OR_IGNORE(memory.end() < core->heap.memory + core->heap.used);

	const u64 slot = (memory.begin() - core->heap.memory) / COMP_HEAP_MIN_ALLOCATION_SIZE;

	// If the address is already leaked, there is nothing more to do.
	if ((core->heap.leak_bitmap[slot >> 6] & (static_cast<u64>(1) << (slot & 63))) != 0)
		return true;

	static_assert(sizeof(CompHeapAllocationHeader) <= COMP_HEAP_MIN_ALLOCATION_SIZE);

	Maybe<CompHeapAllocationHeader*> const header = comp_heap_find_header(core, memory.begin());

	// If there is no header for the given allocation, there is nothing to
	// leak.
	if (is_none(header))
		return true;

	byte* const allocation_begin = reinterpret_cast<byte*>(get(header)) + COMP_HEAP_MIN_ALLOCATION_SIZE;

	// If either the given memory is not entirely contained within the found
	// header, it is out-of-bounds from the guest's perspective, indicating
	// that there is an error and we should terminate with a diagnostic.
	if (allocation_begin + get(header)->size <= memory.begin() || allocation_begin + get(header)->size > memory.end())
		return false;

	const MutRange<byte> allocation{ allocation_begin, get(header)->size };

	comp_heap_mark_bitmap_bits(core, core->heap.leak_bitmap, allocation);

	return true;
}

void comp_heap_dealloc(CoreData* core, MutRange<byte> memory) noexcept
{
	ASSERT_OR_IGNORE(memory.begin() > core->heap.memory);

	ASSERT_OR_IGNORE(memory.end() <= core->heap.memory + core->heap.used);

	const u64 begin_slot = (memory.begin() - core->heap.memory) / COMP_HEAP_MIN_ALLOCATION_SIZE;

	ASSERT_OR_IGNORE((core->heap.begin_bitmap[begin_slot >> 6] & (static_cast<u64>(1) << (begin_slot & 63))) != 0);

	ASSERT_OR_IGNORE((core->heap.header_bitmap[begin_slot >> 6] & (static_cast<u64>(1) << (begin_slot & 63))) == 0);

	ASSERT_OR_IGNORE((core->heap.leak_bitmap[begin_slot >> 6] & (static_cast<u64>(1) << (begin_slot & 63))) == 0);

	core->heap.begin_bitmap[begin_slot >> 6] &= ~(static_cast<u64>(1) << (begin_slot & 63));

	const u64 end_index = memory.end() - core->heap.memory;

	if (core->heap.used - end_index < COMP_HEAP_MIN_ALLOCATION_SIZE)
		core->heap.used = memory.begin() - core->heap.memory;
	else
		comp_heap_add_to_freelist(core, memory);
}



void comp_heap_gc_begin(CoreData* core) noexcept
{
	const u64 page_mask = minos::page_bytes() - 1;

	// Allocate one extra byte so that we can fit a trailing `0` bit to ease
	// scanning of the bitmap.
	const u64 gc_bitmap_commit = ((core->heap.used + BYTES_PER_BITMAP_BYTE - 1) / BYTES_PER_BITMAP_BYTE + 1 + page_mask) & ~page_mask;

	if (!minos::mem_commit(core->heap.gc_bitmap, gc_bitmap_commit))
		panic("Could not allocate % bytes for compile-time heap GC bitmap (0x%[|X]).\n", gc_bitmap_commit, minos::last_error());

	// Insert a `1` sentinel bit right after the end of the gc bitmap. A `0`
	// sentinel right after it is implied by the over-allocation by at least
	// one byte.
	// Since this is effectively just past the used small heap memory, we need
	// to explicitly mark it via the small heap marking function, as it would
	// otherwise not be recognized as part of the normal heap.
	comp_heap_mark_bitmap_bits(core, core->heap.gc_bitmap, MutRange<byte>{ core->heap.memory + core->heap.used, 1 });

	// Preserve the first slot, since it acts as a `null` for ids.
	comp_heap_mark_bitmap_bits(core, core->heap.gc_bitmap, MutRange<byte>{ core->heap.memory, 1 });

	// Forget our old freelists. Their contents will be collected and coalesced
	// by the GC.
	memset(core->heap.freelists, 0, sizeof(core->heap.freelists));
}

bool comp_heap_gc_mark(CoreData* core, MutRange<byte> memory) noexcept
{
	ASSERT_OR_IGNORE((reinterpret_cast<u64>(memory.begin()) & COMP_HEAP_ZERO_ADDRESS_MASK) == 0);

	ASSERT_OR_IGNORE(memory.begin() >= core->heap.memory);
	
	ASSERT_OR_IGNORE(memory.end() <= core->heap.memory + core->heap.used);

	const u64 begin = (memory.begin() - core->heap.memory) / COMP_HEAP_MIN_ALLOCATION_SIZE;

	if ((core->heap.gc_bitmap[begin >> 6] & (static_cast<u64>(1) << (begin & 63))) != 0)
		return false;

	if ((core->heap.begin_bitmap[begin >> 6] & (static_cast<u64>(1) << (begin & 63))) == 0)
	{
		memory = MutRange<byte>{ memory.begin() - COMP_HEAP_MIN_ALLOCATION_SIZE, memory.end() };

		ASSERT_OR_IGNORE((core->heap.begin_bitmap[(begin - 1) >> 6] & (static_cast<u64>(1) << ((begin - 1) & 63))) != 0);

		ASSERT_OR_IGNORE((core->heap.header_bitmap[(begin - 1) >> 6] & (static_cast<u64>(1) << ((begin - 1) & 63))) != 0);
	}

	comp_heap_mark_bitmap_bits(core, core->heap.gc_bitmap, memory);

	return true;
}

void comp_heap_gc_end(CoreData* core) noexcept
{
	const u64 page_mask = minos::page_bytes() - 1;

	const u64 gc_bitmap_commit = (core->heap.used / BYTES_PER_BITMAP_BYTE + page_mask) & ~page_mask;

	u64* const gc_bitmap = core->heap.gc_bitmap;

	// Remove allocation begin bitmap entries that have been collected.

	u64* const begin_bitmap = core->heap.begin_bitmap;

	for (u64 i = 0; i != gc_bitmap_commit / sizeof(u64); ++i)
		begin_bitmap[i] &= gc_bitmap[i];

	// Collect free runs into freelists.

	const u64 end_index = core->heap.used / COMP_HEAP_MIN_ALLOCATION_SIZE;

	u64 bit_index = 0;

	u64 last_alive_index;

	while (true)
	{
		// Skip alive segment.
		// No need to check for overrun here thanks to the `0` sentinel bit
		// just after the bitmap's end.
		while ((gc_bitmap[bit_index >> 6] & (static_cast<u64>(1) << (bit_index & 63))) != 0)
			bit_index += 1;

		last_alive_index = bit_index * COMP_HEAP_MIN_ALLOCATION_SIZE;

		if (bit_index >= end_index)
			break;

		const u64 free_begin = bit_index * COMP_HEAP_MIN_ALLOCATION_SIZE;

		// Skip dead segment.
		// Since there is a `1` sentinel bit right after the bitmap's end, we
		// don't need to check for running off its end. If we got here.
		// We still process the last free segment though. On the next time
		// through the loop, we break upon encountering the following `0`
		// sentinel bit.
		while ((gc_bitmap[bit_index >> 6] & (static_cast<u64>(1) << (bit_index & 63))) == 0)
			bit_index += 1;

		const u64 free_end = bit_index * COMP_HEAP_MIN_ALLOCATION_SIZE;

		const MutRange<byte> free{ core->heap.memory + free_begin, free_end - free_begin };

		comp_heap_add_to_freelist(core, free);
	}

	// Shrink commit to fit last marked address.

	const u64 new_commit = (core->heap.commit + core->heap.commit_increment - 1) & ~(core->heap.commit_increment - 1);

	if (new_commit != core->heap.commit)
	{
		const u64 commit_difference = core->heap.commit - new_commit;

		minos::mem_decommit(core->heap.memory + new_commit, commit_difference);

		minos::mem_decommit(core->heap.leak_bitmap + new_commit / BYTES_PER_BITMAP_BYTE, commit_difference / BYTES_PER_BITMAP_BYTE);

		minos::mem_decommit(core->heap.begin_bitmap + new_commit / BYTES_PER_BITMAP_BYTE, commit_difference / BYTES_PER_BITMAP_BYTE);
	}

	core->heap.commit = new_commit;
	core->heap.used = last_alive_index;

	minos::mem_decommit(core->heap.gc_bitmap, gc_bitmap_commit);
}

bool comp_heap_next_leak(CoreData* core, Maybe<byte*> prev, MutRange<byte>* out_memory, TypeId* out_type_id) noexcept
{
	const byte* const address = is_some(prev) ? get(prev) : core->heap.memory;

	ASSERT_OR_IGNORE(address >= core->heap.memory && address < core->heap.memory + core->heap.used);

	const u64* const begin_bitmap = core->heap.begin_bitmap;

	const u64* const leak_bitmap = core->heap.leak_bitmap;

	const u64 end = (core->heap.used / COMP_HEAP_MIN_ALLOCATION_SIZE) >> 6;

	const u64 prev_slot_offset = (address - core->heap.memory) / COMP_HEAP_MIN_ALLOCATION_SIZE;

	const u64 higher_slots_mask = (~static_cast<u64>(0)) << (prev_slot_offset & 63);

	u64 i = prev_slot_offset >> 6;

	u64 curr = begin_bitmap[i] & leak_bitmap[i] & higher_slots_mask;

	while (curr == 0)
	{
		if (i == end)
			return false;

		i += 1;

		curr = begin_bitmap[i] & leak_bitmap[i];
	}

	const u8 offset_in_qword = count_trailing_zeros_assume_one(curr);

	ASSERT_OR_IGNORE((core->heap.header_bitmap[i] & (static_cast<u64>(1) << offset_in_qword)) != 0);

	byte* const leak_begin = core->heap.memory + i * BYTES_PER_BITMAP_BYTE * sizeof(u64) + offset_in_qword * COMP_HEAP_MIN_ALLOCATION_SIZE;

	const CompHeapAllocationHeader* const header = reinterpret_cast<const CompHeapAllocationHeader*>(leak_begin);

	*out_memory = MutRange<byte>{ leak_begin + COMP_HEAP_MIN_ALLOCATION_SIZE, header->size };

	*out_type_id = header->type_id;

	return true;
}
