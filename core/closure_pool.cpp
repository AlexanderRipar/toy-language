#include "core.hpp"

#include "../infra/container/reserved_heap.hpp"
#include "../infra/container/reserved_vec.hpp"

static constexpr u32 MIN_CLOSURE_SIZE_LOG2 = 4;

static constexpr u32 MAX_CLOSURE_SIZE_LOG2 = 14;

struct alignas(8) Closure
{
	u16 used;

	u8 capacity_log2;

	u8 align_log2;

	TypeId type_id;

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
	alignas(8) byte attach[];
	#if COMPILER_MSVC
	#pragma warning(pop)
	#elif COMPILER_CLANG
	#pragma clang diagnostic pop
	#elif COMPILER_GCC
	#pragma GCC diagnostic pop
	#endif
};

struct ClosurePool
{
	TypePool* types;

	ReservedHeap<MIN_CLOSURE_SIZE_LOG2, MAX_CLOSURE_SIZE_LOG2> closures;

	MutRange<byte> memory;
};



static ClosureBuilderId id_from_closure(ClosurePool* closures, Closure* closure) noexcept
{
	const ClosureBuilderId id = static_cast<ClosureBuilderId>(reinterpret_cast<u64*>(closure) - reinterpret_cast<u64*>(closures->closures.begin()));

	ASSERT_OR_IGNORE(id != ClosureBuilderId::INVALID);

	return id;
}

static Closure* closure_from_id(ClosurePool* closures, ClosureBuilderId id) noexcept
{
	ASSERT_OR_IGNORE(id != ClosureBuilderId::INVALID);

	return reinterpret_cast<Closure*>(reinterpret_cast<u64*>(closures->closures.begin()) + static_cast<u32>(id));
}

static Closure* closure_from_id(ClosurePool* closures, ClosureId id) noexcept
{
	ASSERT_OR_IGNORE(id != ClosureId::INVALID);

	return reinterpret_cast<Closure*>(reinterpret_cast<u64*>(closures->closures.begin()) + static_cast<u32>(id));
}



ClosurePool* create_closure_pool(HandlePool* alloc, TypePool* types) noexcept
{
	static constexpr u32 CLOSURES_CAPACITIES[MAX_CLOSURE_SIZE_LOG2 - MIN_CLOSURE_SIZE_LOG2 + 1] = {
		16384, 8192, 4096, 2048, 1024,
		512,  256,  128,   64,   32,
		16,
	};

	static constexpr u32 CLOSURES_COMMITS[MAX_CLOSURE_SIZE_LOG2 - MIN_CLOSURE_SIZE_LOG2 + 1] = {
		1024, 512, 256, 128, 64,
		32,  16,   8,   4,  2,
		1
	};

	u64 closures_size = 0;

	for (u32 i = 0; i != array_count(CLOSURES_CAPACITIES); ++i)
		closures_size += CLOSURES_CAPACITIES[i] << (i + MIN_CLOSURE_SIZE_LOG2);

	byte* const memory = static_cast<byte*>(minos::mem_reserve(closures_size));

	if (memory == nullptr)
		panic("Could not reserve memory for ClosurePool (0x%X).\n", minos::last_error());

	ClosurePool* const closures = static_cast<ClosurePool*>(alloc_handle_from_pool(alloc, sizeof(ClosurePool), alignof(ClosurePool)));
	closures->types = types;
	closures->closures.init({ memory, closures_size }, Range{ CLOSURES_CAPACITIES }, Range{ CLOSURES_COMMITS });
	closures->memory = { memory, closures_size };

	// Reserve 0 as `ClosureId::INVALID` and `ClosureBuilderId::INVALID`.
	(void) closures->closures.alloc(1);

	return closures;
}

void release_closure_pool(ClosurePool* closures) noexcept
{
	minos::mem_unreserve(closures->memory.begin(), closures->memory.count());
}

ClosureBuilderId closure_create(ClosurePool* closures) noexcept
{
	MutRange<byte> memory = closures->closures.alloc(sizeof(Closure));

	Closure* const closure = reinterpret_cast<Closure*>(memory.begin());
	closure->used = sizeof(Closure);
	closure->capacity_log2 = count_trailing_zeros_assume_one(static_cast<u16>(memory.count()));
	closure->align_log2 = 0;
	closure->type_id = type_create_composite(closures->types, TypeTag::Composite, TypeId::INVALID, TypeDisposition::User, SourceId::INVALID, 1, false);

	return id_from_closure(closures, closure);
}

ClosureBuilderId closure_add_value(ClosurePool* closures, ClosureBuilderId builder_id, IdentifierId name, TypeId value_type_id, Range<byte> value) noexcept
{
	Closure* closure = closure_from_id(closures, builder_id);

	const TypeMetrics value_metrics = type_metrics_from_id(closures->types, value_type_id);

	if (value_metrics.align > 8)
		TODO("Implement over-aligned closure values.");

	const u64 aligned_begin = next_multiple(closure->used, static_cast<u16>(value_metrics.align));

	MemberInfo member_init{};
	member_init.name = name;
	member_init.type = value_type_id;
	member_init.value.complete = GlobalValueId::INVALID;
	member_init.is_global = false;
	member_init.is_pub = false;
	member_init.is_mut = false;
	member_init.has_pending_type = false;
	member_init.has_pending_value = false;
	member_init.is_comptime_known = false;
	member_init.rank = 0;
	member_init.type_completion_arec_id = ArecId::INVALID;
	member_init.value_completion_arec_id = ArecId::INVALID;
	member_init.offset = aligned_begin;

	if (!type_add_composite_member(closures->types, closure->type_id, member_init))
		return id_from_closure(closures, closure);

	if (value_metrics.align > static_cast<u32>(1) << closure->align_log2)
		closure->align_log2 = count_trailing_zeros_assume_one(value_metrics.align);

	const u64 required_capacity = aligned_begin + value.count();

	if (required_capacity > static_cast<u64>(1) << closure->capacity_log2)
	{
		if (required_capacity > static_cast<u64>(1) << MAX_CLOSURE_SIZE_LOG2)
			panic("Required closure size %" PRIu64 " exceeds the supported maximum of %" PRIu64 ".\n", required_capacity, static_cast<u64>(1) << MAX_CLOSURE_SIZE_LOG2);

		MutRange<byte> memory = closures->closures.alloc(static_cast<u32>(required_capacity));

		memcpy(memory.begin(), closure, closure->used);

		closure = reinterpret_cast<Closure*>(memory.begin());

		closure->capacity_log2 = count_trailing_zeros_assume_one(static_cast<u16>(memory.count()));
	}

	memcpy(reinterpret_cast<byte*>(closure) + aligned_begin, value.begin(), value.count());

	closure->used = static_cast<u16>(required_capacity);

	return id_from_closure(closures, closure);
}

ClosureId closure_seal(ClosurePool* closures, ClosureBuilderId builder_id) noexcept
{
	Closure* const closure = closure_from_id(closures, builder_id);

	if (closure->used == sizeof(Closure))
	{
		closures->closures.dealloc({ reinterpret_cast<byte*>(closure), static_cast<u64>(1) << closure->capacity_log2 });

		type_discard(closures->types, closure->type_id);

		return ClosureId::INVALID;
	}

	const u16 align = static_cast<u16>(1) << closure->align_log2;

	const u64 stride = next_multiple(closure->used, align);

	type_seal_composite(closures->types, closure->type_id, closure->used, align, stride);

	return static_cast<ClosureId>(builder_id);
}

ClosureInstance closure_instance(ClosurePool* closures, ClosureId closure_id) noexcept
{
	Closure* closure = closure_from_id(closures, closure_id);

	return { closure->type_id, static_cast<u32>(1) << closure->align_log2, { reinterpret_cast<byte*>(closure), closure->used } };
}
