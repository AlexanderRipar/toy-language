#include "core.hpp"
#include "structure.hpp"

#include "../infra/types.hpp"
#include "../infra/assert.hpp"
#include "../infra/panic.hpp"
#include "../infra/range.hpp"
#include "../infra/container/reserved_vec.hpp"

struct alignas(8) ForeverValue
{
	void* data_begin;

	u32 data_size;

	u32 data_align;

	union
	{
		TypeId type;

		OpcodeId initializer;
	};

	GlobalCompositeValueState state;

	bool is_mut;
};

struct alignas(8) GlobalFile
{
	TypeId type_id;

	u32 member_count;
	
	u64 unused_[2];

	ForeverValue members[];
};

static_assert(sizeof(ForeverValue) == 24);

static_assert(sizeof(GlobalFile) == 24);



static constexpr u32 FILES_RESERVE_SIZE = sizeof(ForeverValue) << 20;

static constexpr u32 FILES_COMMIT_INCREMENT_COUNT = 4096;



static GlobalFile* global_file_from_index(CoreData* core, GlobalCompositeId id) noexcept
{
	ASSERT_OR_IGNORE(id != GlobalCompositeId::INVALID && static_cast<u32>(id) < core->globals.files.used());

	return core->globals.files.begin() + static_cast<u16>(id);
}

static ForeverValue* forever_value_from_file_and_rank(GlobalFile* file, u16 rank) noexcept
{
	ASSERT_OR_IGNORE(rank < file->member_count);

	return &file->members[rank];
}

static ForeverValue* forever_value_from_global_composite_index_and_rank(CoreData* core, GlobalCompositeId id, u16 rank) noexcept
{
	GlobalFile* const file = global_file_from_index(core, id);

	return forever_value_from_file_and_rank(file, rank);
}



bool global_value_pool_validate_config([[maybe_unused]] const Config* config, [[maybe_unused]] PrintSink sink) noexcept
{
	return true;
}

MemoryRequirements global_value_pool_memory_requirements([[maybe_unused]] const Config* config) noexcept
{
	MemoryRequirements reqs;

	reqs.private_reserve = 0;
	reqs.id_requirements_count = 1;
	reqs.id_requirements[0].reserve = FILES_RESERVE_SIZE;
	reqs.id_requirements[0].alignment = alignof(GlobalFile);

	return reqs;
}

void global_value_pool_init(CoreData* core, MemoryAllocation allocation) noexcept
{
	ASSERT_OR_IGNORE(allocation.private_data.count() == 0);

	ASSERT_OR_IGNORE(allocation.ids[0].count() == FILES_RESERVE_SIZE);

	GlobalValuePool* const globals = &core->globals;

	globals->files.init(allocation.ids[0], FILES_COMMIT_INCREMENT_COUNT);

	// Reserve `GlobalCompositeIndex::INVALID`.
	(void) globals->files.reserve();
}



GlobalCompositeId global_composite_reserve(CoreData* core, TypeId type_id, u16 definition_count) noexcept
{
	GlobalFile* const file = core->globals.files.reserve(1 + definition_count);

	file->type_id = type_id;
	file->member_count = definition_count;

	return static_cast<GlobalCompositeId>(file - core->globals.files.begin());
}

void global_composite_value_set_initializer(CoreData* core, GlobalCompositeId id, u16 rank, OpcodeId initializer) noexcept
{
	ForeverValue* const value = forever_value_from_global_composite_index_and_rank(core, id, rank);

	ASSERT_OR_IGNORE(value->initializer == OpcodeId::INVALID);

	value->initializer = initializer;
	value->state = GlobalCompositeValueState::Uninitialized;
}

GlobalCompositeValueState global_composite_value_get(CoreData* core, GlobalCompositeId id, u16 rank, CTValue* out_value, OpcodeId* out_code) noexcept
{
	ForeverValue* const value = forever_value_from_global_composite_index_and_rank(core, id, rank);

	const GlobalCompositeValueState state = static_cast<GlobalCompositeValueState>(value->state);

	if (state == GlobalCompositeValueState::Complete)
	{
		const MutRange<byte> bytes{ static_cast<byte*>(value->data_begin), value->data_size };

		*out_value = CTValue{ bytes, value->data_align, static_cast<bool>(value->is_mut), value->type };
	}
	else if (state == GlobalCompositeValueState::Uninitialized)
	{
		ASSERT_OR_IGNORE(value->initializer != OpcodeId::INVALID);

		*out_code = value->initializer;
	}
	else
	{
		ASSERT_OR_IGNORE(state == GlobalCompositeValueState::Initializing);
	}

	return state;
}

void global_composite_value_alloc_prepare(CoreData* core, GlobalCompositeId id, u16 rank, bool is_mut) noexcept
{
	ForeverValue* const value = forever_value_from_global_composite_index_and_rank(core, id, rank);

	ASSERT_OR_IGNORE(static_cast<GlobalCompositeValueState>(value->state) == GlobalCompositeValueState::Uninitialized);

	value->state = GlobalCompositeValueState::Initializing;
	value->is_mut = is_mut;
}

CTValue global_composite_value_alloc_initialized(CoreData* core, GlobalCompositeId id, u16 rank, CTValue initializer, TypeId* out_file_type) noexcept
{
	GlobalFile* const file = global_file_from_index(core, id);

	ForeverValue* const value = forever_value_from_file_and_rank(file, rank);

	ASSERT_OR_IGNORE(static_cast<GlobalCompositeValueState>(value->state) == GlobalCompositeValueState::Initializing);

	const Maybe<void*> allocation = comp_heap_alloc(core, initializer.bytes.count(), initializer.align);

	if (is_none(allocation))
		TODO("Implement GC traversal.");

	value->type = initializer.type;
	value->data_begin = get(allocation);
	value->data_size = static_cast<u32>(initializer.bytes.count());
	value->state = GlobalCompositeValueState::Complete;
	value->data_align = initializer.align;

	*out_file_type = file->type_id;

	memcpy(get(allocation), initializer.bytes.begin(), initializer.bytes.count());

	const MutRange<byte> bytes{ static_cast<byte*>(get(allocation)), initializer.bytes.count() };

	return CTValue{ bytes, value->data_align, value->is_mut, value->type };
}

CTValue global_composite_value_alloc_uninitialized(CoreData* core, GlobalCompositeId id, u16 rank, TypeId type, TypeMetrics metrics, TypeId* out_file_type) noexcept
{
	GlobalFile* const file = global_file_from_index(core, id);

	ForeverValue* const value = forever_value_from_file_and_rank(file, rank);

	ASSERT_OR_IGNORE(static_cast<GlobalCompositeValueState>(value->state) == GlobalCompositeValueState::Initializing);

	const Maybe<void*> allocation = comp_heap_alloc(core, metrics.size, metrics.align);

	if (is_none(allocation))
		TODO("Implement GC traversal.");

	value->type = type;
	value->data_begin = get(allocation);
	value->data_size = static_cast<u32>(metrics.size);
	value->data_align = metrics.align;

	*out_file_type = file->type_id;

	const MutRange<byte> bytes{ static_cast<byte*>(get(allocation)), metrics.size };

	return CTValue{ bytes, metrics.align, value->is_mut, type };
}

void global_composite_value_alloc_uninitialized_complete(CoreData* core, GlobalCompositeId id, u16 rank) noexcept
{
	ForeverValue* const value = forever_value_from_global_composite_index_and_rank(core, id, rank);

	ASSERT_OR_IGNORE(static_cast<GlobalCompositeValueState>(value->state) == GlobalCompositeValueState::Initializing);

	value->state = GlobalCompositeValueState::Complete;
}
