#include "core.hpp"

#include <cstring>

#include "../infra/container/reserved_vec.hpp"
#include "../infra/container/reserved_heap.hpp"
#include "../infra/inplace_sort.hpp"

static constexpr u32 MIN_PARTIAL_VALUE_SIZE_LOG2 = 6;

static constexpr u32 MAX_PARTIAL_VALUE_SIZE_LOG2 = 16;

struct alignas(8) ValueHeader
{
	AstNode* root;

	u32 used;

	u32 capacity;

	u32 first_value_offset;

	u32 last_value_offset;
};

struct alignas(8) SubvalueHeader
{
	u16 value_size;

	u16 value_align;

	u32 offset_from_root;

	s32 next_value_offset;

	TypeId type_id;
};

struct SubvalueHeaderSortIdx
{
	u32 offset_from_root;

	u32 offset_from_header;
};

struct SubvalueHeaderCompare
{
	static s32 compare(SubvalueHeaderSortIdx a, SubvalueHeaderSortIdx b) noexcept
	{
		return static_cast<s32>(a.offset_from_header - b.offset_from_header);
	}
};

struct PartialValuePool
{
	u32 first_free_builder_ind;

	ReservedVec<u32> builder_inds;

	ReservedHeap<MIN_PARTIAL_VALUE_SIZE_LOG2, MAX_PARTIAL_VALUE_SIZE_LOG2> values;

	ReservedVec<SubvalueHeaderSortIdx> sorting_array;

	MutRange<byte> memory;
};



static u32 header_index(PartialValuePool* partials, ValueHeader* header) noexcept
{
	return static_cast<u32>(reinterpret_cast<const u64*>(header) - static_cast<const u64*>(partials->values.begin()));
}

static s32 subheader_index(SubvalueHeader* from, SubvalueHeader* to) noexcept
{
	return static_cast<s32>(reinterpret_cast<u64*>(to) - reinterpret_cast<u64*>(from));
}

static u32 subheader_index(ValueHeader* from, SubvalueHeader* to) noexcept
{
	return static_cast<u32>(reinterpret_cast<u64*>(to) - reinterpret_cast<u64*>(from));
}

static ValueHeader* header_at(PartialValuePool* partials, PartialValueId id) noexcept
{
	return reinterpret_cast<ValueHeader*>(static_cast<u64*>(partials->values.begin()) + static_cast<u32>(id));
}

static ValueHeader* header_at(PartialValuePool* partials, PartialValueBuilderId id, u32** out_indirection) noexcept
{
	u32* const indirection = partials->builder_inds.begin() + static_cast<u32>(id);

	const u32 builder_ind = *indirection;

	*out_indirection = indirection;

	return header_at(partials, static_cast<PartialValueId>(builder_ind));
}

static SubvalueHeader* subheader_at(ValueHeader* header, u32 offset)
{
	return reinterpret_cast<SubvalueHeader*>(reinterpret_cast<u64*>(header) + offset);
}

static SubvalueHeader* subheader_at(SubvalueHeader* header, s32 offset) noexcept
{
	return reinterpret_cast<SubvalueHeader*>(reinterpret_cast<u64*>(header) + offset);
}

static ValueHeader* alloc_header(PartialValuePool* partials, AstNode* root) noexcept
{
	MutRange<byte> memory = partials->values.alloc(sizeof(ValueHeader));

	ValueHeader* const header = reinterpret_cast<ValueHeader*>(memory.begin());
	header->root = root;
	header->used = sizeof(ValueHeader);
	header->capacity = static_cast<u32>(memory.count());
	header->first_value_offset = 0;
	header->last_value_offset = 0;

	return header;
}

static ValueHeader* realloc_header(PartialValuePool* partials, ValueHeader* old_header, u32* indirection, u32 extra_size) noexcept
{
	MutRange<byte> new_memory = partials->values.alloc(old_header->used + extra_size);

	ValueHeader* const new_header = reinterpret_cast<ValueHeader*>(new_memory.begin());
	memcpy(new_header, old_header, old_header->used);
	new_header->capacity = static_cast<u32>(new_memory.count());

	*indirection = header_index(partials, new_header);

	partials->values.dealloc({ reinterpret_cast<byte*>(old_header), old_header->capacity });

	return new_header;
}

static SubvalueHeader* alloc_subheader(PartialValuePool* partials, ValueHeader* header, u32* indirection, AstNode* node, TypeId type_id, u16 size, u16 align) noexcept
{
	byte* free_begin = reinterpret_cast<byte*>(header) + header->used + sizeof(SubvalueHeader);

	byte* free_aligned = reinterpret_cast<byte*>(next_multiple(reinterpret_cast<u64>(free_begin), static_cast<u64>(align)));

	byte* const free_end = reinterpret_cast<byte*>(header) + header->capacity;

	const u32 free_size = static_cast<u32>(free_end - free_aligned);

	SubvalueHeader* subheader;

	if (free_size < size)
	{
		header = realloc_header(partials, header, indirection, size - free_size);

		free_begin = reinterpret_cast<byte*>(header) + header->used + sizeof(SubvalueHeader);

		free_aligned = reinterpret_cast<byte*>(next_multiple(reinterpret_cast<u64>(free_begin), static_cast<u64>(align)));

		ASSERT_OR_IGNORE(reinterpret_cast<byte*>(header) + header->capacity >= free_aligned + size);

		subheader = reinterpret_cast<SubvalueHeader*>(free_aligned - sizeof(SubvalueHeader));
	}
	else
	{
		subheader = reinterpret_cast<SubvalueHeader*>(free_aligned - sizeof(SubvalueHeader));
	}

	const u32 offset = subheader_index(header, subheader);

	if (header->last_value_offset == 0)
	{
		header->first_value_offset = offset;
	}
	else
	{
		SubvalueHeader* const prev = reinterpret_cast<SubvalueHeader*>(reinterpret_cast<byte*>(header) + header->last_value_offset);

		ASSERT_OR_IGNORE(prev->next_value_offset == 0);

		prev->next_value_offset = subheader_index(prev, subheader);
	}

	header->last_value_offset = offset;

	subheader->value_size = size;
	subheader->value_align = align;
	subheader->offset_from_root = static_cast<u32>(node - header->root);
	subheader->next_value_offset = 0;
	subheader->type_id = type_id;

	return subheader;
}

static PartialValueIterator iterator_from_header(ValueHeader* header) noexcept
{
	PartialValueIterator it;
	it.header = header;
	it.subheader = header->first_value_offset == 0
		? nullptr
		: subheader_at(header, header->first_value_offset);

	return it;
}

static void discard_header(PartialValuePool* partials, ValueHeader* header, u32* indirection, PartialValueBuilderId builder_id) noexcept
{
	*indirection = partials->first_free_builder_ind;

	partials->first_free_builder_ind = static_cast<u32>(builder_id);

	partials->values.dealloc({ reinterpret_cast<byte*>(header), header->capacity });
}

static void sort_subheaders_by_offset_from_root(PartialValuePool* partials, ValueHeader* header) noexcept
{
	if (header->first_value_offset == 0)
		return;

	SubvalueHeader* subheader = subheader_at(header, header->first_value_offset);

	while (true)
	{
		partials->sorting_array.append({ subheader->offset_from_root, subheader_index(header, subheader) });

		if (subheader->next_value_offset == 0)
			break;

		subheader = subheader_at(subheader, subheader->next_value_offset);
	}

	inplace_sort<SubvalueHeaderSortIdx, SubvalueHeaderCompare>({ partials->sorting_array.begin(), partials->sorting_array.used() });

	header->first_value_offset = partials->sorting_array.begin()->offset_from_header;

	u32 prev_offset = partials->sorting_array.begin()->offset_from_header;

	for (u32 i = 1; i != partials->sorting_array.used(); ++i)
	{
		SubvalueHeader* const prev_subheader = subheader_at(header, prev_offset);

		const u32 next_offset = partials->sorting_array.begin()[i].offset_from_header;

		prev_subheader->next_value_offset = static_cast<s32>(next_offset - prev_offset);

		prev_offset = next_offset;
	}

	header->last_value_offset = prev_offset;
}



PartialValuePool* create_partial_value_pool(HandlePool* alloc) noexcept
{
	static constexpr u32 BUILDER_INDS_SIZE = (1 << 14) * sizeof(u32);

	static constexpr u32 SORTING_ARRAY_SIZE = 1024 * sizeof(SubvalueHeaderSortIdx);

	static constexpr u32 VALUES_CAPACITIES[MAX_PARTIAL_VALUE_SIZE_LOG2 - MIN_PARTIAL_VALUE_SIZE_LOG2 + 1] = {
		131072, 65536, 32768, 16384,
		  8192,  4096,  2048,  1024,
		   512,  256,    128,
	};

	static constexpr u32 VALUES_COMMITS[MAX_PARTIAL_VALUE_SIZE_LOG2 - MIN_PARTIAL_VALUE_SIZE_LOG2 + 1] = {
		1024, 512, 256, 128,
		  64,   32,  16,  8,
		   4,    2,   1,
	};

	u64 total_values_size = 0;

	for (u32 i = 0; i != array_count(VALUES_CAPACITIES); ++i)
		total_values_size += static_cast<u64>(VALUES_CAPACITIES[i]) << (i + MIN_PARTIAL_VALUE_SIZE_LOG2);

	ASSERT_OR_IGNORE(total_values_size <= UINT32_MAX);

	byte* const memory = static_cast<byte*>(minos::mem_reserve(total_values_size + BUILDER_INDS_SIZE + SORTING_ARRAY_SIZE));

	if (memory == nullptr)
		panic("Could not reserve memory for PartialValuePool (0x%X).\n", minos::last_error());

	PartialValuePool* const partials = static_cast<PartialValuePool*>(alloc_handle_from_pool(alloc, sizeof(PartialValuePool), alignof(PartialValuePool)));
	partials->values.init({ memory, total_values_size }, Range{ VALUES_CAPACITIES }, Range{ VALUES_COMMITS });
	partials->builder_inds.init({ memory + total_values_size, BUILDER_INDS_SIZE }, 4096 / sizeof(u32));
	partials->sorting_array.init({ memory + total_values_size + BUILDER_INDS_SIZE, SORTING_ARRAY_SIZE }, 4096 / sizeof(SubvalueHeaderSortIdx));
	partials->memory = { memory, total_values_size + BUILDER_INDS_SIZE + SORTING_ARRAY_SIZE };

	// Reserve `PartialValueId::INVALID`.
	(void) partials->values.alloc(1);

	// Reserve `PartialValueBuilderId::INVALID`.
	(void) partials->builder_inds.reserve();

	return partials;
}

void release_partial_value_pool(PartialValuePool* partials) noexcept
{
	minos::mem_unreserve(partials->memory.begin(), partials->memory.count());
}

PartialValueBuilderId create_partial_value_builder(PartialValuePool* partials, AstNode* root) noexcept
{
	ValueHeader* const header = alloc_header(partials, root);

	u32* builder;

	if (partials->first_free_builder_ind == 0)
	{
		builder = partials->builder_inds.reserve();
	}
	else
	{
		builder = partials->builder_inds.begin() + partials->first_free_builder_ind;

		partials->first_free_builder_ind = *builder;
	}

	*builder = header_index(partials, header);

	return static_cast<PartialValueBuilderId>(builder - partials->builder_inds.begin());
}

MutRange<byte> partial_value_builder_add_value(PartialValuePool* partials, PartialValueBuilderId id, AstNode* node, TypeId type_id, u64 size, u32 align) noexcept
{
	ASSERT_OR_IGNORE(id != PartialValueBuilderId::INVALID);

	if (size > UINT16_MAX)
		panic("Size %" PRIu64 " of partial value element exceeds maximum of %u bytes.\n", size, UINT32_MAX);

	if (align > static_cast<u32>(1) << MIN_PARTIAL_VALUE_SIZE_LOG2)
		TODO("Implement overaligned partial values");

	u32* indirection;

	ValueHeader* header = header_at(partials, id, &indirection);

	ASSERT_OR_IGNORE(is_descendant_of(header->root, node));

	SubvalueHeader* const subheader = alloc_subheader(partials, header, indirection, node, type_id, static_cast<u16>(size), static_cast<u16>(align));

	return { reinterpret_cast<byte*>(subheader + 1), size };
}

PartialValueId complete_partial_value_builder(PartialValuePool* partials, PartialValueBuilderId id) noexcept
{
	ASSERT_OR_IGNORE(id != PartialValueBuilderId::INVALID);

	u32* indirection;

	ValueHeader* const header = header_at(partials, id, &indirection);

	*indirection = partials->first_free_builder_ind;

	partials->first_free_builder_ind = static_cast<u32>(id);

	sort_subheaders_by_offset_from_root(partials, header);

	return static_cast<PartialValueId>(header_index(partials, header));
}

void discard_partial_value_builder(PartialValuePool* partials, PartialValueBuilderId id) noexcept
{
	ASSERT_OR_IGNORE(id != PartialValueBuilderId::INVALID);

	u32* indirection;

	ValueHeader* const header = header_at(partials, id, &indirection);

	return discard_header(partials, header, indirection, id);
}

void merge_partial_value_builders(PartialValuePool* partials, PartialValueBuilderId dst_id, PartialValueBuilderId src_id) noexcept
{
	ASSERT_OR_IGNORE(dst_id != PartialValueBuilderId::INVALID && src_id != PartialValueBuilderId::INVALID);

	u32* dst_indirection;

	u32* src_indirection;

	ValueHeader* dst_header = header_at(partials, dst_id, &dst_indirection);

	ValueHeader* const src_header = header_at(partials, src_id, &src_indirection);

	ASSERT_OR_IGNORE(is_descendant_of(dst_header->root, src_header->root));

	// This is just an estimate; Due to alignment-induced padding, we may end
	// up actually allocating more, but estimating first leads to fewer
	// reallocations, and thus fewer moves.
	// Also note that we subtract `sizeof(ValueHeader)` to avoid
	// double-counting, as it is incuded in both `dst_header->used` and
	// `src_header->used`.
	if (dst_header->capacity < dst_header->used + src_header->used - sizeof(ValueHeader))
		realloc_header(partials, dst_header, dst_indirection, dst_header->used + src_header->used - sizeof(ValueHeader) - dst_header->capacity);

	SubvalueHeader* src_subheader = src_header->first_value_offset == 0
		? nullptr
		: subheader_at(src_header, src_header->first_value_offset);

	while (src_subheader != nullptr)
	{
		SubvalueHeader* const dst_subheader = alloc_subheader(partials, dst_header, dst_indirection, src_header->root + src_subheader->offset_from_root, src_subheader->type_id, src_subheader->value_size, src_subheader->value_align);

		memcpy(dst_subheader + 1, src_subheader + 1, src_subheader->value_size);

		src_subheader = src_subheader->next_value_offset == 0
			? nullptr
			: subheader_at(src_subheader, src_subheader->next_value_offset);
	}

	discard_header(partials, src_header, src_indirection, src_id);
}



AstNode* root_of(PartialValuePool* partials, PartialValueId id) noexcept
{
	ASSERT_OR_IGNORE(id != PartialValueId::INVALID);

	ValueHeader* const header = header_at(partials, id);

	return header->root;
}

PartialValueIterator values_of(PartialValuePool* partials, PartialValueId id) noexcept
{
	ASSERT_OR_IGNORE(id != PartialValueId::INVALID);

	ValueHeader* const header = header_at(partials, id);

	return iterator_from_header(header);
}

bool has_next(const PartialValueIterator* it) noexcept
{
	return it->subheader != nullptr;
}

PartialValue next(PartialValueIterator* it) noexcept
{
	ASSERT_OR_IGNORE(has_next(it));

	const ValueHeader* const header = static_cast<const ValueHeader*>(it->header);

	const SubvalueHeader* const subheader = static_cast<const SubvalueHeader*>(it->subheader);

	PartialValue rst;
	rst.node = header->root + subheader->offset_from_root;
	rst.type_id = subheader->type_id;
	rst.data = { reinterpret_cast<const byte*>(subheader + 1), subheader->value_size };

	it->subheader = subheader->next_value_offset == 0
		? nullptr
		: subheader_at(const_cast<SubvalueHeader*>(subheader), subheader->next_value_offset);

	return rst;
}
