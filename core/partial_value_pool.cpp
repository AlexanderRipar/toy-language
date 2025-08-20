#include "core.hpp"

#include "../infra/container.hpp"
#include "../infra/hash.hpp"

static constexpr u32 MIN_ELEM_SIZE_LOG2 = 2;

static constexpr u32 MAX_ELEM_SIZE_LOG2 = 16;

struct alignas(8) ClosedValue;

static u32 strides_of(const ClosedValue* value) noexcept;

static bool partial_value_equality(const ClosedValue* a, const ClosedValue* b) noexcept;

struct alignas(8) ClosedElem
{
	u32 data_size;

	u32 data_offset;

	u32 node_offset;

	TypeId type_id;
};

struct ClosedValueHeader
{
	AstNode* root;

	u32 m_hash;

	u32 count;
};

struct alignas(8) ClosedValue
{
	ClosedValueHeader header;

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
	ClosedElem elems[];
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

	static u32 required_strides(const ClosedValue* key) noexcept
	{
		return strides_of(key);
	}

	u32 used_strides() const noexcept
	{
		return strides_of(this);
	}

	u32 hash() const noexcept
	{
		return header.m_hash;
	}

	bool equal_to_key(const ClosedValue* key, [[maybe_unused]] u32 key_hash) const noexcept
	{
		return partial_value_equality(key, this);
	}

	void init(const ClosedValue* key, [[maybe_unused]] u32 key_hash) noexcept
	{
		memcpy(this, key, strides_of(key) * stride());
	}
};

struct alignas(8) OpenElem
{
	ClosedElem value;

	u32 align;

	u32 next_index;
};

struct alignas(8) OpenValue
{
	u32 count;

	u32 first_index;

	AstNode* root;

	OpenElem* last;
};

struct PartialValuePool
{
	AstPool* asts;

	IndexMap<const ClosedValue*, ClosedValue> closed;

	ReservedVec2<ClosedValue> closed_buffer;

	ReservedVec2<OpenValue> open;

	ReservedVec2<OpenElem> open_values;

	u32 first_free_open_index;

	u32 first_free_open_value_index;

	ReservedHeap2<MIN_ELEM_SIZE_LOG2, MAX_ELEM_SIZE_LOG2> open_data;

	MutRange<byte> memory;
};



static PartialValueIterator iterator_from_ptr(const ClosedValue* partial) noexcept
{
	PartialValueIterator it;
	it.partial = partial;
	it.curr = partial->elems;
	it.end = partial->elems + partial->header.count;

	return it;
}

static u32 strides_of(const ClosedValue* partial) noexcept
{
	static_assert(sizeof(ClosedValue) % ClosedValue::stride() == 0);

	if (partial->header.count == 0)
		return sizeof(ClosedValue) / ClosedValue::stride();

	const u64 required_size = static_cast<u64>(sizeof(ClosedValue)) + partial->header.count * sizeof(ClosedElem) + partial->elems[partial->header.count - 1].data_offset;

	return static_cast<u32>(required_size / ClosedValue::stride());
}

static bool partial_value_equality(const ClosedValue* a, const ClosedValue* b) noexcept
{
	if (a->header.m_hash != b->header.m_hash || a->header.count != b->header.count)
		return false;

	const AstNode* node_a = a->header.root;

	const AstNode* node_b = b->header.root;

	PartialValueIterator it_a = iterator_from_ptr(a);

	PartialValueIterator it_b = iterator_from_ptr(b);

	while (has_next(&it_a))
	{
		const PartialValue val_a = next(&it_a);

		const PartialValue val_b = next(&it_b);

		if (val_a.type_id != val_b.type_id)
			return false;

		ASSERT_OR_IGNORE(val_a.data.count() == val_b.data.count());

		if (memcmp(val_a.data.begin(), val_b.data.begin(), val_a.data.count()) != 0)
			return false;

		while (node_a != val_a.node)
		{
			if (node_b == val_b.node)
				return false;

			if (node_a->own_qwords != node_b->own_qwords || memcmp(node_a, node_b, node_a->own_qwords * sizeof(u64)) != 0)
				return false;

			node_a += node_a->own_qwords;

			node_b += node_b->own_qwords;
		}
	}

	return true;
}

static u32 hash_closed_value(const ClosedValue* partial) noexcept
{
	const byte* data_begin = reinterpret_cast<const byte*>(partial->elems + partial->header.count);

	u64 data_size;

	if (partial->header.count == 0)
	{
		data_size = 0;
	}
	else
	{
		const ClosedElem* const last_elem = partial->elems + partial->header.count - 1;

		data_size = last_elem->data_offset - sizeof(ClosedElem);
	}

	return fnv1a({ data_begin, data_size });
}

static Range<byte> data_of(const ClosedElem* partial) noexcept
{
	return { reinterpret_cast<const byte*>(partial) + partial->data_offset, partial->data_size };
};



PartialValuePool* create_partial_value_pool(AllocPool* alloc, AstPool* asts) noexcept
{
	static constexpr u32 CLOSED_SIZE = (1 << 20) * sizeof(ClosedValue);

	static constexpr u32 OPEN_SIZE = (1 << 16) * sizeof(OpenValue);

	static constexpr u32 OPEN_VALUES_SIZE = (1 << 20) * sizeof(OpenElem);

	static constexpr u32 OPEN_DATA_CAPACITIES[MAX_ELEM_SIZE_LOG2 - MIN_ELEM_SIZE_LOG2 + 1] = {
		131072, 65536, 32768, 16384, 8192,
		  4096,  2048,  1024,   512,  512,
		   256,   256,   128,   128,   64,
	};

	static constexpr u32 OPEN_DATA_COMMITS[MAX_ELEM_SIZE_LOG2 - MIN_ELEM_SIZE_LOG2 + 1] = {
		4096, 2048, 1024, 512, 256,
		 128,   64,   32,  16,   8,
		   4,    2,    1,   1,   1,
	};

	u64 open_data_size = 0;

	for (u32 i = 0; i != array_count(OPEN_DATA_CAPACITIES); ++i)
		open_data_size += static_cast<u64>(OPEN_DATA_CAPACITIES[i]) << (i + MIN_ELEM_SIZE_LOG2);

	ASSERT_OR_IGNORE(open_data_size <= UINT32_MAX);

	PartialValuePool* const partials = static_cast<PartialValuePool*>(alloc_from_pool(alloc, sizeof(PartialValuePool), alignof(PartialValuePool)));

	byte* const memory = static_cast<byte*>(minos::mem_reserve(CLOSED_SIZE + OPEN_SIZE + OPEN_VALUES_SIZE + open_data_size));

	if (memory == nullptr)
		panic("Could not reserve memory for PartialValuePool (0x%X).\n", minos::last_error());

	partials->asts = asts;

	partials->closed.init(1 << 24, 1 << 9, 1 << 26, 1 << 10);

	partials->closed_buffer.init({ memory, CLOSED_SIZE }, static_cast<u32>(1) << 11);

	u64 offset = CLOSED_SIZE;

	partials->open.init({ memory + offset, OPEN_SIZE }, static_cast<u32>(1) << 10);

	offset += OPEN_SIZE;

	partials->open_values.init({ memory + offset, OPEN_VALUES_SIZE }, static_cast<u32>(1) << 11);

	offset += OPEN_VALUES_SIZE;

	partials->open_data.init({ memory + offset, open_data_size }, Range{ OPEN_DATA_CAPACITIES }, Range{ OPEN_DATA_COMMITS });

	offset += open_data_size;

	partials->memory = { memory, offset };

	partials->first_free_open_index = 0;

	partials->first_free_open_value_index = 0;

	ClosedValueHeader dummy_closed{};
	(void) partials->closed.value_from(reinterpret_cast<ClosedValue*>(&dummy_closed), hash_closed_value(reinterpret_cast<ClosedValue*>(&dummy_closed)));

	(void) partials->open.reserve();

	return partials;
}

void release_partial_value_pool(PartialValuePool* partials) noexcept
{
	minos::mem_unreserve(partials->memory.begin(), partials->memory.count());
}

PartialValueBuilderId create_partial_value_builder(PartialValuePool* partials, AstNode* root) noexcept
{
	OpenValue* const partial = partials->open.reserve();
	partial->count = 0;
	partial->first_index = 0;
	partial->root = root;
	partial->last = nullptr;

	return static_cast<PartialValueBuilderId>(partial - partials->open.begin()); 
}

MutRange<byte> partial_value_builder_add_value(PartialValuePool* partials, PartialValueBuilderId id, AstNode* node, TypeId type_id, u64 size, u32 align) noexcept
{
	if (size > UINT32_MAX)
		panic("Size %" PRIu64 " of partial value element exceeds maximum of %u bytes.\n", size, UINT32_MAX);

	if (align > size)
		panic("Overaligned partial values not yet implemented.\n");

	OpenValue* const partial = partials->open.begin() + static_cast<u32>(id);

	ASSERT_OR_IGNORE(is_descendant_of(partial->root, node));

	OpenElem* const elem = partials->open_values.reserve();

	const u32 elem_index = static_cast<u32>(reinterpret_cast<u64*>(elem) - static_cast<u64*>(partials->open_data.begin()));

	u32* const prev = partial->last == nullptr ? &partial->first_index : &partial->last->next_index;

	*prev = elem_index;

	partial->last = elem;
	partial->count += 1;

	MutRange<byte> data = partials->open_data.alloc(sizeof(OpenElem));

	elem->value.data_size = static_cast<u32>(size);
	elem->value.data_offset = static_cast<u32>(data.begin() - static_cast<byte*>(partials->open_data.begin()));
	elem->value.node_offset = static_cast<u32>(node - partial->root);
	elem->value.type_id = type_id;
	elem->align = align;
	elem->next_index = 0;

	return data;
}

PartialValueId complete_partial_value_builder(PartialValuePool* partials, PartialValueBuilderId id) noexcept
{
	OpenValue* const open = partials->open.begin() + static_cast<u32>(id);

	ClosedValue* const closed = static_cast<ClosedValue*>(partials->closed_buffer.reserve_exact(sizeof(ClosedValue) + open->count * sizeof(ClosedElem)));

	OpenElem* src = open->first_index == 0
		? nullptr
		: reinterpret_cast<OpenElem*>(static_cast<u64*>(partials->open_data.begin()) + open->first_index);

	ClosedElem* dst = closed->elems;

	while (src != nullptr)
	{
		partials->closed_buffer.pad_to_alignment(src->align);

		void* const data_dst = partials->closed_buffer.reserve_padded(src->value.data_size);

		void* const data_src = static_cast<byte*>(partials->open_data.begin()) + src->value.data_offset;

		memcpy(data_dst, data_src, src->value.data_size);

		dst->data_size = src->value.data_size;
		dst->data_offset = static_cast<u32>(static_cast<u64*>(data_dst) - reinterpret_cast<u64*>(dst));
		dst->node_offset = src->value.node_offset;
		dst->type_id = src->value.type_id;

		OpenElem* const next = reinterpret_cast<OpenElem*>(static_cast<u64*>(partials->open_data.begin()) + src->next_index);

		partials->open_data.dealloc({ static_cast<byte*>(data_src), src->value.data_size });

		*reinterpret_cast<u32*>(src) = partials->first_free_open_value_index;

		partials->first_free_open_value_index = static_cast<u32>(reinterpret_cast<u64*>(src) - reinterpret_cast<u64*>(partials->open_values.begin()));

		src = next;

		dst += 1;
	}

	closed->header.count = open->count;
	closed->header.m_hash = hash_closed_value(closed);
	closed->header.root = open->root;

	*reinterpret_cast<u32*>(open) = partials->first_free_open_index;

	partials->first_free_open_index = static_cast<u32>(reinterpret_cast<u64*>(open) - reinterpret_cast<u64*>(partials->open.begin()));

	return static_cast<PartialValueId>(partials->closed.index_from(closed, closed->header.m_hash));
}



AstNode* root_of(PartialValuePool* partials, PartialValueId id) noexcept
{
	return partials->closed.value_from(static_cast<u32>(id))->header.root;
}

PartialValueIterator values_of(PartialValuePool* partials, PartialValueId id) noexcept
{
	ClosedValue* const partial = partials->closed.value_from(static_cast<u32>(id));

	return iterator_from_ptr(partial);
}

bool has_next(const PartialValueIterator* it) noexcept
{
	ASSERT_OR_IGNORE(it->curr <= it->end);

	return it->curr != it->end;
}

PartialValue next(PartialValueIterator* it) noexcept
{
	ASSERT_OR_IGNORE(has_next(it));

	const ClosedElem* const elem = static_cast<const ClosedElem*>(it->curr);

	const ClosedValue* const partial =  static_cast<const ClosedValue*>(it->partial);

	PartialValue result;
	result.node = partial->header.root + elem->node_offset;
	result.type_id = elem->type_id;
	result.data = data_of(elem);

	it->curr = elem + 1;

	ASSERT_OR_IGNORE(it->curr <= it->end);

	return result;
}
