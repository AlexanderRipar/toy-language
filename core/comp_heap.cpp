#include "core.hpp"
#include "structure.hpp"

#include "../infra/minos/minos.hpp"
#include "../infra/panic.hpp"
#include "../infra/math.hpp"
#include "../infra/inplace_sort.hpp"



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
			*reinterpret_cast<byte**>(curr) = core->heap.freelists[index];

			core->heap.freelists[index] = curr;

			curr += mask;

			size -= mask;
		}

		mask <<= 1;

		index += 1;
	}

	while (size != 0)
	{
		ASSERT_OR_IGNORE(size >= COMP_HEAP_MAX_ALLOCATION_SIZE);

		*reinterpret_cast<byte**>(curr) = core->heap.freelists[array_count(core->heap.freelists) - 1]; 
		
		core->heap.freelists[array_count(core->heap.freelists) - 1] = curr;

		size -= COMP_HEAP_MAX_ALLOCATION_SIZE;

		curr += COMP_HEAP_MAX_ALLOCATION_SIZE;
	}
}

static bool comp_heap_ensure_commit(CoreData* core, u64 required) noexcept
{
	if (required <= core->heap.commit)
		return true;

	if (required > core->heap.reserve)
		return false;

	u64 new_commit = (required + core->heap.commit_increment - 1) & ~(core->heap.commit_increment - 1);

	if (new_commit > core->heap.reserve)
		new_commit = core->heap.reserve;

	if (!minos::mem_commit(core->heap.memory + core->heap.commit, new_commit - core->heap.commit))
		return false;

	core->heap.commit = new_commit;

	return true;
}

static bool comp_heap_gc_mark_small_unchecked(CoreData* core, MutRange<byte> memory) noexcept
{
	const u64 begin_index = (memory.begin() - core->heap.memory) >> COMP_HEAP_MIN_ALLOCATION_SIZE_LOG2;

	const u64 end_index = (memory.end() + COMP_HEAP_ZERO_ADDRESS_MASK - core->heap.memory) >> COMP_HEAP_MIN_ALLOCATION_SIZE_LOG2;

	u8* const gc_bitmap = core->heap.gc_bitmap;

	if ((gc_bitmap[begin_index / 8] & static_cast<u8>(1 << (begin_index % 8))) != 0)
		return false;

	for (u64 i = begin_index; i != end_index; ++i)
		gc_bitmap[i >> 3] |= static_cast<u8>(1 << (i & 7));

	return true;
}



bool comp_heap_validate_config(const Config* config, PrintSink sink) noexcept
{
	ASSERT_OR_IGNORE(config->heap.reserve <= (static_cast<u64>(1) << 32) * COMP_HEAP_MIN_ALLOCATION_SIZE);

	if (config->heap.reserve >= config->heap.commit_increment)
		return true;

	print(sink, "Configuration parameter `heap.reserve` (%) must be greater than or equal to `heap.commit_increment` (%).\n", config->heap.reserve, config->heap.commit_increment);

	return false;
}

MemoryRequirements comp_heap_memory_requirements(const Config* config) noexcept
{
	const u64 page_size = minos::page_bytes();

	const u64 page_mask = page_size - 1;

	const u64 commit_increment = config->heap.commit_increment < page_size
		? page_size
		: next_pow2(config->heap.commit_increment);

	const u64 heap_size = (config->heap.reserve + commit_increment - 1) & ~(commit_increment - 1);

	const u64 gc_bitmap_size = (heap_size / (8 * COMP_HEAP_MIN_ALLOCATION_SIZE) + page_mask) & ~page_mask;

	MemoryRequirements reqs{};
	reqs.count = 2;
	reqs.ranges[0].size = heap_size;
	reqs.ranges[0].max_offset = static_cast<u64>(UINT32_MAX) * COMP_HEAP_MIN_ALLOCATION_SIZE;
	reqs.ranges[1].size = gc_bitmap_size;
	reqs.ranges[1].max_offset = UINT64_MAX;

	return reqs;
}

void comp_heap_init(CoreData* core, MemoryAllocation allocation) noexcept
{
	const u64 page_size = minos::page_bytes();

	const u64 commit_increment = core->config->heap.commit_increment < page_size
		? page_size
		: next_pow2(core->config->heap.commit_increment);

	const u64 heap_size = (core->config->heap.reserve + commit_increment - 1) & ~(commit_increment - 1);

	if (!minos::mem_commit(allocation.ranges[0].begin(), commit_increment))
		panic("Could not commit % bytes of memory for compile-time heap header and initial commit (0x%[|X]).\n", commit_increment, minos::last_error());

	core->heap.memory = allocation.ranges[0].begin();
	core->heap.used = COMP_HEAP_MIN_ALLOCATION_SIZE; // Reserve the slot as a pseudo-null value for indices.
	core->heap.commit = commit_increment;
	core->heap.reserve = heap_size;
	core->heap.commit_increment = commit_increment;
	core->heap.arena_count = 0;
	core->heap.arena_begin = 0;
	core->heap.gc_bitmap = reinterpret_cast<u8*>(allocation.ranges[1].begin()) + heap_size;

	memset(core->heap.freelists, 0, sizeof(core->heap.freelists));
}



Maybe<void*> comp_heap_alloc(CoreData* core, u64 size, u64 align) noexcept
{
	ASSERT_OR_IGNORE(core->heap.arena_count == 0);

	ASSERT_OR_IGNORE(is_pow2(align));

	if (size > COMP_HEAP_MAX_ALLOCATION_SIZE)
		return none<void*>();

	// Try satisfying allocations with an alignment of at most
	// `COMP_HEAP_MIN_ALLOCATION_SIZE` from the relevant freelist if they are
	// small enough (at most `COMP_HEAP_MAX_ALLOCATION_SIZE`).
	if (align <= COMP_HEAP_MIN_ALLOCATION_SIZE && size <= COMP_HEAP_MIN_ALLOCATION_SIZE)
	{
		const u8 size_log2_ceil = log2_ceil(size);

		const u8 index = size_log2_ceil < COMP_HEAP_MIN_ALLOCATION_SIZE_LOG2
			? 0
			: size_log2_ceil - COMP_HEAP_MIN_ALLOCATION_SIZE_LOG2;

		byte* const head = core->heap.freelists[index];

		if (head != 0)
		{
			core->heap.freelists[index] = *reinterpret_cast<byte**>(head);

			const u64 entry_size = static_cast<u64>(1) << size_log2_ceil;

			if (entry_size - size >= COMP_HEAP_MIN_ALLOCATION_SIZE)
				comp_heap_add_to_freelist(core, MutRange<byte>{ head + size, entry_size - size });

			return some<void*>(head);
		}
	}

	const u64 unaligned_begin = core->heap.used;

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

	return some<void*>(begin);
}



u64 comp_heap_arena_mark(CoreData* core) noexcept
{
	ASSERT_OR_IGNORE(core->heap.arena_count == 0 || core->heap.arena_begin <= core->heap.used);

	if (core->heap.arena_count == 0)
		core->heap.arena_begin = core->heap.used;

	core->heap.arena_count += 1;

	return core->heap.used;
}

void* comp_heap_arena_alloc(CoreData* core, u64 size, u64 align) noexcept
{
	ASSERT_OR_IGNORE(core->heap.arena_count != 0);

	ASSERT_OR_IGNORE(is_pow2(align));

	const u64 aligned_begin = (core->heap.used + align - 1) & ~(align - 1);

	const u64 new_used = aligned_begin + size;

	// Arena allocations are not padded to slot boundaries, to allow
	// consecutive allocations to use contiguous addresses.
	core->heap.used = new_used;

	if (!comp_heap_ensure_commit(core, new_used))
		panic("Failed to allocate % bytes in global arena.\n", size);

	return core->heap.memory + aligned_begin;
}

void comp_heap_arena_release(CoreData* core, u64 arena_mark) noexcept
{
	ASSERT_OR_IGNORE(core->heap.arena_count != 0);

	ASSERT_OR_IGNORE(core->heap.arena_begin <= arena_mark);

	ASSERT_OR_IGNORE(core->heap.arena_count != 1 || core->heap.arena_begin == arena_mark);

	core->heap.arena_count -= 1;

	core->heap.used = arena_mark;
}

void* comp_heap_arena_release_and_preserve(CoreData* core, u64 arena_mark, MutRange<byte> preserve) noexcept
{
	ASSERT_OR_IGNORE(core->heap.arena_count == 1);

	ASSERT_OR_IGNORE(core->heap.arena_begin == arena_mark);

	ASSERT_OR_IGNORE(preserve.begin() >= core->heap.memory + core->heap.arena_begin);

	ASSERT_OR_IGNORE(preserve.end() <= core->heap.memory + core->heap.used);

	core->heap.arena_count -= 1;

	byte* const preserved_begin = core->heap.memory + arena_mark;

	memmove(preserved_begin, preserve.begin(), preserve.count());

	core->heap.used = arena_mark + preserve.count();

	return preserved_begin;
}



void comp_heap_gc_begin(CoreData* core) noexcept
{
	const u64 page_mask = minos::page_bytes() - 1;

	// Allocate one extra byte so that we can fit an trailing `0` bit to ease
	// scanning of the bitmap.
	const u64 gc_bitmap_commit = ((core->heap.used + 8 * COMP_HEAP_MIN_ALLOCATION_SIZE - 1) / (8 * COMP_HEAP_MIN_ALLOCATION_SIZE) + 1 + page_mask) & ~page_mask;

	if (!minos::mem_commit(core->heap.gc_bitmap, gc_bitmap_commit))
		panic("Could not allocate % bytes for compile-time heap GC bitmap (0x%[|X]).\n", gc_bitmap_commit, minos::last_error());

	// Insert a `1` sentinel bit right after the end of the gc bitmap. A `0`
	// sentinel right after it is implied by the over-allocation by at least
	// one byte.
	// Since this is effectively just past the used small heap memory, we need
	// to explicitly mark it via the small heap marking function, as it would
	// otherwise not be recognized as part of the normal heap.
	(void) comp_heap_gc_mark_small_unchecked(core, MutRange<byte>{ core->heap.memory + core->heap.used, 1 });

	// Preserve the first slot, since it acts as a `null` for ids.
	(void) comp_heap_gc_mark_small_unchecked(core, MutRange<byte>{ core->heap.memory, 1 });

	// Forget our old freelists. Their contents will be collected and coalesced
	// by the GC.
	memset(core->heap.freelists, 0, sizeof(core->heap.freelists));
}

bool comp_heap_gc_mark(CoreData* core, MutRange<byte> memory) noexcept
{
	ASSERT_OR_IGNORE((reinterpret_cast<u64>(memory.begin()) & COMP_HEAP_ZERO_ADDRESS_MASK) == 0);

	ASSERT_OR_IGNORE(memory.begin() >= core->heap.memory);
	
	ASSERT_OR_IGNORE(memory.end() <= core->heap.memory + core->heap.used);

	return comp_heap_gc_mark_small_unchecked(core, memory);
}

void comp_heap_gc_end(CoreData* core) noexcept
{
	const u64 page_mask = minos::page_bytes() - 1;

	const u64 gc_bitmap_commit = (core->heap.used / (8 * COMP_HEAP_MIN_ALLOCATION_SIZE) + page_mask) & ~page_mask;

	u8* const gc_bitmap = core->heap.gc_bitmap;

	const u64 end_index = core->heap.used / COMP_HEAP_MIN_ALLOCATION_SIZE;

	u64 bit_index = 0;

	u64 last_alive_index;

	while (true)
	{
		// Skip alive segment.
		// No need to check for overrun here thanks to the `0` sentinel bit
		// just after the bitmap's end.
		while ((gc_bitmap[bit_index >> 3] & (static_cast<u8>(1 << (bit_index & 7)))) != 0)
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
		while ((gc_bitmap[bit_index >> 3] & static_cast<u8>(1 << (bit_index & 8))) == 0)
			bit_index += 1;

		const u64 free_end = bit_index * COMP_HEAP_MIN_ALLOCATION_SIZE;

		const MutRange<byte> free{ core->heap.memory + free_begin, free_end - free_begin };

		comp_heap_add_to_freelist(core, free);
	}

	const u64 new_commit = (core->heap.commit + core->heap.commit_increment - 1) & ~(core->heap.commit_increment - 1);

	if (new_commit != core->heap.commit)
		minos::mem_decommit(core->heap.memory + new_commit, core->heap.commit - new_commit);

	core->heap.commit = new_commit;
	core->heap.used = last_alive_index;

	minos::mem_decommit(core->heap.gc_bitmap, gc_bitmap_commit);
}
