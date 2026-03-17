#include "core.hpp"
#include "structure.hpp"

#include "../infra/types.hpp"
#include "../infra/assert.hpp"
#include "../infra/panic.hpp"
#include "../infra/range.hpp"
#include "../infra/container/reserved_vec.hpp"

struct alignas(8) GlobalFile
{
	ForeverValueId first_value_id;

	TypeId type_id;
};

struct alignas(8) ForeverValue
{
	union
	{
		TypeId type;

		OpcodeId initializer;
	};

	u32 data_offset;

	u32 data_size;

	u32 state : 2;

	u32 is_mut : 1;

	u32 data_align : 29;
};



static constexpr u32 FILES_RESERVE_SIZE = 65536;

static constexpr u32 FILES_COMMIT_INCREMENT_COUNT = 1024;

static constexpr u32 FOREVER_VALUES_RESERVE_SIZE = 1 << 22;

static constexpr u32 FOREVER_VALUES_COMMIT_INCREMENT_COUNT = 65536 / sizeof(ForeverValue);

static constexpr u32 DATA_RESERVE_SIZE = 1 << 28;

static constexpr u32 DATA_COMMIT_INCREMENT_COUNT = 65536;



static ForeverValue* forever_value_from_id(CoreData* core, ForeverValueId id) noexcept
{
	ASSERT_OR_IGNORE(id != ForeverValueId::INVALID && static_cast<u32>(id) < core->globals.forever_values.used());

	return core->globals.forever_values.begin() + static_cast<u32>(id);
}

static GlobalFile* global_file_from_index(CoreData* core, GlobalCompositeIndex index) noexcept
{
	ASSERT_OR_IGNORE(index != GlobalCompositeIndex::INVALID && static_cast<u16>(index) < core->globals.files.used());

	return core->globals.files.begin() + static_cast<u16>(index);
}

static ForeverValueId forever_value_id_from_file_and_rank([[maybe_unused]] CoreData* core, const GlobalFile* file, u16 rank) noexcept
{
	ASSERT_OR_IGNORE(static_cast<u32>(file->first_value_id) + rank < core->globals.forever_values.used());

	return static_cast<ForeverValueId>(static_cast<u32>(file->first_value_id) + rank);
}

static ForeverValue* forever_value_from_file_and_rank(CoreData* core, const GlobalFile* file, u16 rank) noexcept
{
	const ForeverValueId id = forever_value_id_from_file_and_rank(core, file, rank);

	return forever_value_from_id(core, id);
}

static ForeverValueId forever_value_id_from_global_composite_index_and_rank(CoreData* core, GlobalCompositeIndex index, u16 rank) noexcept
{
	const GlobalFile* const file = global_file_from_index(core, index);

	return forever_value_id_from_file_and_rank(core, file, rank);
}

static ForeverValue* forever_value_from_global_composite_index_and_rank(CoreData* core, GlobalCompositeIndex index, u16 rank) noexcept
{
	const GlobalFile* const file = global_file_from_index(core, index);

	return forever_value_from_file_and_rank(core, file, rank);
}



MemoryRequirements global_value_pool_memory_requirements([[maybe_unused]] const Config* config) noexcept
{
	MemoryRequirements reqs;

	reqs.private_reserve = DATA_RESERVE_SIZE;
	reqs.id_requirements_count = 2;
	reqs.id_requirements[0].reserve = FILES_RESERVE_SIZE;
	reqs.id_requirements[0].alignment = alignof(GlobalFile);
	reqs.id_requirements[1].reserve = FOREVER_VALUES_RESERVE_SIZE;
	reqs.id_requirements[1].alignment = alignof(ForeverValue);

	return reqs;
}

void global_value_pool_init(CoreData* core, MemoryAllocation allocation) noexcept
{
	ASSERT_OR_IGNORE(allocation.ids[0].count() == FILES_RESERVE_SIZE);
	
	ASSERT_OR_IGNORE(allocation.ids[1].count() == FOREVER_VALUES_RESERVE_SIZE);

	ASSERT_OR_IGNORE(allocation.private_data.count() == DATA_RESERVE_SIZE);

	GlobalValuePool* const globals = &core->globals;

	globals->files.init(allocation.ids[0], FILES_COMMIT_INCREMENT_COUNT);

	globals->forever_values.init(allocation.ids[1], FOREVER_VALUES_COMMIT_INCREMENT_COUNT);

	globals->data.init(allocation.private_data, DATA_COMMIT_INCREMENT_COUNT);

	// Reserve `GlobalCompositeIndex::INVALID`.
	(void) globals->files.reserve();

	// Reserve `ForeverValueId::INVALID`.
	(void) globals->forever_values.reserve();
}



GlobalCompositeIndex global_composite_reserve(CoreData* core, TypeId type_id, u16 definition_count) noexcept
{
	GlobalFile file;
	file.first_value_id = static_cast<ForeverValueId>(core->globals.forever_values.used());
	file.type_id = type_id;

	core->globals.files.append(file);

	core->globals.forever_values.reserve(definition_count);

	return static_cast<GlobalCompositeIndex>(core->globals.files.used() - 1);
}

void global_composite_value_set_initializer(CoreData* core, GlobalCompositeIndex index, u16 rank, OpcodeId initializer) noexcept
{
	ForeverValue* const value = forever_value_from_global_composite_index_and_rank(core, index, rank);

	ASSERT_OR_IGNORE(value->initializer == OpcodeId::INVALID);

	value->initializer = initializer;
	value->state = static_cast<u8>(GlobalFileValueState::Uninitialized);
}

GlobalFileValueState global_composite_value_get(CoreData* core, GlobalCompositeIndex index, u16 rank, ForeverCTValue* out_value, OpcodeId* out_code) noexcept
{
	const ForeverValueId value_id = forever_value_id_from_global_composite_index_and_rank(core, index, rank);

	const ForeverValue* const value = forever_value_from_id(core, value_id);

	const GlobalFileValueState state = static_cast<GlobalFileValueState>(value->state);

	if (state == GlobalFileValueState::Complete)
	{
		const MutRange<byte> bytes{ core->globals.data.begin() + value->data_offset, value->data_size };

		const CTValue ct_value{ bytes, value->data_align, static_cast<bool>(value->is_mut), value->type };

		*out_value = ForeverCTValue{ ct_value, value_id };
	}
	else if (state == GlobalFileValueState::Uninitialized)
	{
		ASSERT_OR_IGNORE(value->initializer != OpcodeId::INVALID);

		*out_code = value->initializer;
	}
	else
	{
		ASSERT_OR_IGNORE(state == GlobalFileValueState::Initializing);
	}

	return state;
}

void global_composite_value_alloc_prepare(CoreData* core, GlobalCompositeIndex index, u16 rank, bool is_mut) noexcept
{
	ForeverValue* const value = forever_value_from_global_composite_index_and_rank(core, index, rank);

	ASSERT_OR_IGNORE(static_cast<GlobalFileValueState>(value->state) == GlobalFileValueState::Uninitialized);

	value->state = static_cast<u8>(GlobalFileValueState::Initializing);
	value->is_mut = is_mut;
}

ForeverValueId global_composite_value_alloc_initialized(CoreData* core, GlobalCompositeIndex index, u16 rank, CTValue initializer, TypeId* out_file_type) noexcept
{
	const GlobalFile* const file = global_file_from_index(core, index);

	const ForeverValueId value_id = forever_value_id_from_file_and_rank(core, file, rank);

	ForeverValue* const value = forever_value_from_id(core, value_id);

	ASSERT_OR_IGNORE(static_cast<GlobalFileValueState>(value->state) == GlobalFileValueState::Initializing);

	core->globals.data.pad_to_alignment(initializer.align);

	const u32 data_offset = core->globals.data.used();

	core->globals.data.reserve(static_cast<u32>(initializer.bytes.count()));

	value->type = initializer.type;
	value->data_offset = data_offset;
	value->data_size = static_cast<u32>(initializer.bytes.count());
	value->state = static_cast<u8>(GlobalFileValueState::Complete);
	value->data_align = initializer.align;

	*out_file_type = file->type_id;

	memcpy(core->globals.data.begin() + data_offset, initializer.bytes.begin(), initializer.bytes.count());

	return value_id;
}

ForeverCTValue global_composite_value_alloc_uninitialized(CoreData* core, GlobalCompositeIndex index, u16 rank, TypeId type, TypeMetrics metrics, TypeId* out_file_type) noexcept
{
	const GlobalFile* const file = global_file_from_index(core, index);

	const ForeverValueId value_id = forever_value_id_from_file_and_rank(core, file, rank);

	ForeverValue* const value = forever_value_from_id(core, value_id);

	ASSERT_OR_IGNORE(static_cast<GlobalFileValueState>(value->state) == GlobalFileValueState::Initializing);

	core->globals.data.pad_to_alignment(metrics.align);

	const u32 data_offset = core->globals.data.used();

	core->globals.data.reserve(static_cast<u32>(metrics.size));

	value->type = type;
	value->data_offset = data_offset;
	value->data_size = static_cast<u32>(metrics.size);
	value->data_align = metrics.align;

	*out_file_type = file->type_id;

	const MutRange<byte> bytes{ core->globals.data.begin() + data_offset, metrics.size };

	const CTValue ct_value{ bytes, metrics.align, value->is_mut, type };

	return ForeverCTValue{ ct_value, value_id };
}

void global_composite_value_alloc_initialized_complete(CoreData* core, GlobalCompositeIndex index, u16 rank) noexcept
{
	ForeverValue* const value = forever_value_from_global_composite_index_and_rank(core, index, rank);

	ASSERT_OR_IGNORE(static_cast<GlobalFileValueState>(value->state) == GlobalFileValueState::Initializing);

	value->state = static_cast<u8>(GlobalFileValueState::Complete);
}

ForeverValueId forever_value_alloc_initialized(CoreData* core, bool is_mut, CTValue initializer) noexcept
{
	const ForeverValueId value_id = static_cast<ForeverValueId>(core->globals.forever_values.used());

	ForeverValue* const value = core->globals.forever_values.reserve();

	core->globals.data.pad_to_alignment(initializer.align);

	const u32 data_offset = core->globals.data.used();

	core->globals.data.reserve(static_cast<u32>(initializer.bytes.count()));

	value->type = initializer.type;
	value->data_offset = data_offset;
	value->data_size = static_cast<u32>(initializer.bytes.count());
	value->state = static_cast<u8>(GlobalFileValueState::Complete);
	value->is_mut = is_mut;
	value->data_align = initializer.align;
	core->globals.forever_values.reserve(static_cast<u32>(initializer.bytes.count()));

	const MutRange<byte> bytes{ core->globals.data.begin() + data_offset, initializer.bytes.count() };

	range::mem_copy(bytes, initializer.bytes.immut());

	return value_id;
}

ForeverCTValue forever_value_alloc_uninitialized(CoreData* core, bool is_mut, TypeId type, TypeMetrics metrics) noexcept
{
	const ForeverValueId forever_value_id = static_cast<ForeverValueId>(core->globals.forever_values.used());

	ForeverValue* const value = core->globals.forever_values.reserve();

	core->globals.data.pad_to_alignment(metrics.align);

	const u32 data_offset = core->globals.data.used();

	core->globals.data.reserve(static_cast<u32>(metrics.size));

	value->type = type;
	value->data_offset = data_offset;
	value->data_size = static_cast<u32>(metrics.size);
	value->state = static_cast<u8>(GlobalFileValueState::Complete);
	value->is_mut = is_mut;
	value->data_align = metrics.align;
	core->globals.forever_values.reserve(static_cast<u32>(metrics.size));

	const MutRange<byte> bytes{ core->globals.data.begin() + data_offset, metrics.size };

	const CTValue ct_value{ bytes, metrics.align, is_mut, type };

	return ForeverCTValue{ ct_value, forever_value_id };
}

CTValue forever_value_get(CoreData* core, ForeverValueId id) noexcept
{
	ASSERT_OR_IGNORE(id != ForeverValueId::INVALID && static_cast<u32>(id) < core->globals.forever_values.used());

	const ForeverValue* const value = core->globals.forever_values.begin() + static_cast<u32>(id);

	const MutRange<byte> bytes{ core->globals.data.begin() + value->data_offset, value->data_size };

	return CTValue{ bytes, value->data_align, static_cast<bool>(value->is_mut), value->type };
}
