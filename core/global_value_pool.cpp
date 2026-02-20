#include "core.hpp"

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

struct GlobalValuePool
{
	ReservedVec<GlobalFile> files;

	ReservedVec<ForeverValue> forever_values;

	ReservedVec<byte> data;

	MutRange<byte> memory;
};



static ForeverValue* forever_value_from_id(GlobalValuePool* globals, ForeverValueId id) noexcept
{
	ASSERT_OR_IGNORE(id != ForeverValueId::INVALID && static_cast<u32>(id) < globals->forever_values.used());

	return globals->forever_values.begin() + static_cast<u32>(id);
}

static GlobalFile* global_file_from_index(GlobalValuePool* globals, GlobalFileIndex file_index) noexcept
{
	ASSERT_OR_IGNORE(file_index != GlobalFileIndex::INVALID && static_cast<u16>(file_index) < globals->files.used());

	return globals->files.begin() + static_cast<u16>(file_index);
}

static ForeverValueId forever_value_id_from_file_and_rank([[maybe_unused]] GlobalValuePool* globals, const GlobalFile* file, u16 rank) noexcept
{
	ASSERT_OR_IGNORE(static_cast<u32>(file->first_value_id) + rank < globals->forever_values.used());

	return static_cast<ForeverValueId>(static_cast<u32>(file->first_value_id) + rank);
}

static ForeverValue* forever_value_from_file_and_rank(GlobalValuePool* globals, const GlobalFile* file, u16 rank) noexcept
{
	const ForeverValueId id = forever_value_id_from_file_and_rank(globals, file, rank);

	return forever_value_from_id(globals, id);
}

static ForeverValueId forever_value_id_from_file_index_and_rank(GlobalValuePool* globals, GlobalFileIndex file_index, u16 rank) noexcept
{
	const GlobalFile* const file = global_file_from_index(globals, file_index);

	return forever_value_id_from_file_and_rank(globals, file, rank);
}

static ForeverValue* forever_value_from_file_index_and_rank(GlobalValuePool* globals, GlobalFileIndex file_index, u16 rank) noexcept
{
	const GlobalFile* const file = global_file_from_index(globals, file_index);

	return forever_value_from_file_and_rank(globals, file, rank);
}



GlobalValuePool* create_global_value_pool(HandlePool* handles) noexcept
{
	static constexpr u32 FILES_RESERVE_SIZE = 65536;

	static constexpr u32 FILE_OFFSETS_COMMIT_INCREMENT_COUNT = 1024;

	static constexpr u32 FOREVER_VALUES_RESERVE_SIZE = 1 << 22;

	static constexpr u32 FOREVER_VALUES_COMMIT_INCREMENT_COUNT = 65536 / sizeof(ForeverValue);

	static constexpr u32 DATA_RESERVE_SIZE = 1 << 28;

	static constexpr u32 DATA_COMMIT_INCREMENT_COUNT = 65536;

	static constexpr u64 TOTAL_RESERVE_SIZE = static_cast<u64>(FILES_RESERVE_SIZE)
											+ static_cast<u64>(FOREVER_VALUES_RESERVE_SIZE)
											+ static_cast<u64>(DATA_RESERVE_SIZE);

	byte* const memory = static_cast<byte*>(minos::mem_reserve(TOTAL_RESERVE_SIZE));

	if (memory == nullptr)
		panic("Failed to allocate memory for GlobalValuePool (0x%X).\n", minos::last_error());

	GlobalValuePool* const globals = alloc_handle_from_pool<GlobalValuePool>(handles);

	u64 offset = 0;

	globals->files.init({ memory + offset, FILES_RESERVE_SIZE }, FILE_OFFSETS_COMMIT_INCREMENT_COUNT);
	offset += FILES_RESERVE_SIZE;

	globals->forever_values.init({ memory + offset, FOREVER_VALUES_RESERVE_SIZE }, FOREVER_VALUES_COMMIT_INCREMENT_COUNT);
	offset += FOREVER_VALUES_RESERVE_SIZE;

	globals->data.init({ memory + offset, DATA_RESERVE_SIZE }, DATA_COMMIT_INCREMENT_COUNT);
	offset += DATA_RESERVE_SIZE;

	ASSERT_OR_IGNORE(offset == TOTAL_RESERVE_SIZE);

	globals->memory = MutRange<byte>{ memory, TOTAL_RESERVE_SIZE };

	// Reserve `GlobalFileIndex::INVALID`.
	(void) globals->files.reserve();

	// Reserve `ForeverValueId::INVALID`.
	(void) globals->forever_values.reserve();

	return globals;
}

void release_global_value_pool(GlobalValuePool* globals) noexcept
{
	minos::mem_unreserve(globals->memory.begin(), globals->memory.count());
}

GlobalFileIndex file_values_reserve(GlobalValuePool* globals, TypeId file_type_id, u16 definition_count) noexcept
{
	GlobalFile file;
	file.first_value_id = static_cast<ForeverValueId>(globals->forever_values.used());
	file.type_id = file_type_id;

	globals->files.append(file);

	globals->forever_values.reserve(definition_count);

	return static_cast<GlobalFileIndex>(globals->files.used() - 1);
}

void file_value_set_initializer(GlobalValuePool* globals, GlobalFileIndex file_index, u16 rank, OpcodeId initializer) noexcept
{
	ForeverValue* const value = forever_value_from_file_index_and_rank(globals, file_index, rank);

	ASSERT_OR_IGNORE(value->initializer == OpcodeId::INVALID);

	value->initializer = initializer;
	value->state = static_cast<u8>(GlobalFileValueState::Uninitialized);
}

GlobalFileValueState file_value_get(GlobalValuePool* globals, GlobalFileIndex file_index, u16 rank, ForeverCTValue* out_value, OpcodeId* out_code) noexcept
{
	const ForeverValueId value_id = forever_value_id_from_file_index_and_rank(globals, file_index, rank);

	const ForeverValue* const value = forever_value_from_id(globals, value_id);

	const GlobalFileValueState state = static_cast<GlobalFileValueState>(value->state);

	if (state == GlobalFileValueState::Complete)
	{
		const MutRange<byte> bytes{ globals->data.begin() + value->data_offset, value->data_size };

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

void file_value_alloc_prepare(GlobalValuePool* globals, GlobalFileIndex file_index, u16 rank, bool is_mut) noexcept
{
	ForeverValue* const value = forever_value_from_file_index_and_rank(globals, file_index, rank);

	ASSERT_OR_IGNORE(static_cast<GlobalFileValueState>(value->state) == GlobalFileValueState::Uninitialized);

	value->state = static_cast<u8>(GlobalFileValueState::Initializing);
	value->is_mut = is_mut;
}

ForeverValueId file_value_alloc_initialized(GlobalValuePool* globals, GlobalFileIndex file_index, u16 rank, CTValue initializer, TypeId* out_file_type) noexcept
{
	const GlobalFile* const file = global_file_from_index(globals, file_index);

	const ForeverValueId value_id = forever_value_id_from_file_and_rank(globals, file, rank);

	ForeverValue* const value = forever_value_from_id(globals, value_id);

	ASSERT_OR_IGNORE(static_cast<GlobalFileValueState>(value->state) == GlobalFileValueState::Initializing);

	globals->data.pad_to_alignment(initializer.align);

	const u32 data_offset = globals->data.used();

	globals->data.reserve(static_cast<u32>(initializer.bytes.count()));

	value->type = initializer.type;
	value->data_offset = data_offset;
	value->data_size = static_cast<u32>(initializer.bytes.count());
	value->state = static_cast<u8>(GlobalFileValueState::Complete);
	value->data_align = initializer.align;

	*out_file_type = file->type_id;

	memcpy(globals->data.begin() + data_offset, initializer.bytes.begin(), initializer.bytes.count());

	return value_id;
}

ForeverCTValue file_value_alloc_uninitialized(GlobalValuePool* globals, GlobalFileIndex file_index, u16 rank, TypeId type, TypeMetrics metrics, TypeId* out_file_type) noexcept
{
	const GlobalFile* const file = global_file_from_index(globals, file_index);

	const ForeverValueId value_id = forever_value_id_from_file_and_rank(globals, file, rank);

	ForeverValue* const value = forever_value_from_id(globals, value_id);

	ASSERT_OR_IGNORE(static_cast<GlobalFileValueState>(value->state) == GlobalFileValueState::Initializing);

	globals->data.pad_to_alignment(metrics.align);

	const u32 data_offset = globals->data.used();

	globals->data.reserve(static_cast<u32>(metrics.size));

	value->type = type;
	value->data_offset = data_offset;
	value->data_size = static_cast<u32>(metrics.size);
	value->data_align = metrics.align;

	*out_file_type = file->type_id;

	const MutRange<byte> bytes{ globals->data.begin() + data_offset, metrics.size };

	const CTValue ct_value{ bytes, metrics.align, value->is_mut, type };

	return ForeverCTValue{ ct_value, value_id };
}

void file_value_alloc_initialized_complete(GlobalValuePool* globals, GlobalFileIndex file_index, u16 rank) noexcept
{
	ForeverValue* const value = forever_value_from_file_index_and_rank(globals, file_index, rank);

	ASSERT_OR_IGNORE(static_cast<GlobalFileValueState>(value->state) == GlobalFileValueState::Initializing);

	value->state = static_cast<u8>(GlobalFileValueState::Complete);
}

ForeverValueId forever_value_alloc_initialized(GlobalValuePool* globals, bool is_mut, CTValue initializer) noexcept
{
	const ForeverValueId value_id = static_cast<ForeverValueId>(globals->forever_values.used());

	ForeverValue* const value = globals->forever_values.reserve();

	globals->data.pad_to_alignment(initializer.align);

	const u32 data_offset = globals->data.used();

	globals->data.reserve(static_cast<u32>(initializer.bytes.count()));

	value->type = initializer.type;
	value->data_offset = data_offset;
	value->data_size = static_cast<u32>(initializer.bytes.count());
	value->state = static_cast<u8>(GlobalFileValueState::Complete);
	value->is_mut = is_mut;
	value->data_align = initializer.align;
	globals->forever_values.reserve(static_cast<u32>(initializer.bytes.count()));

	const MutRange<byte> bytes{ globals->data.begin() + data_offset, initializer.bytes.count() };

	range::mem_copy(bytes, initializer.bytes.immut());

	return value_id;
}

ForeverCTValue forever_value_alloc_uninitialized(GlobalValuePool* globals, bool is_mut, TypeId type, TypeMetrics metrics) noexcept
{
	const ForeverValueId forever_value_id = static_cast<ForeverValueId>(globals->forever_values.used());

	ForeverValue* const value = globals->forever_values.reserve();

	globals->data.pad_to_alignment(metrics.align);

	const u32 data_offset = globals->data.used();

	globals->data.reserve(static_cast<u32>(metrics.size));

	value->type = type;
	value->data_offset = data_offset;
	value->data_size = static_cast<u32>(metrics.size);
	value->state = static_cast<u8>(GlobalFileValueState::Complete);
	value->is_mut = is_mut;
	value->data_align = metrics.align;
	globals->forever_values.reserve(static_cast<u32>(metrics.size));

	const MutRange<byte> bytes{ globals->data.begin() + data_offset, metrics.size };

	const CTValue ct_value{ bytes, metrics.align, is_mut, type };

	return ForeverCTValue{ ct_value, forever_value_id };
}

CTValue forever_value_get(GlobalValuePool* globals, ForeverValueId id) noexcept
{
	ASSERT_OR_IGNORE(id != ForeverValueId::INVALID && static_cast<u32>(id) < globals->forever_values.used());

	const ForeverValue* const value = globals->forever_values.begin() + static_cast<u32>(id);

	const MutRange<byte> bytes{ globals->data.begin() + value->data_offset, value->data_size };

	return CTValue{ bytes, value->data_align, static_cast<bool>(value->is_mut), value->type };
}
