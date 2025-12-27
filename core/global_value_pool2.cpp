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

	u32 is_complete : 1;

	u32 is_mut : 1;

	u32 data_align : 30;
};

struct GlobalValuePool2
{
	ReservedVec<GlobalFile> files;

	ReservedVec<ForeverValue> forever_values;

	ReservedVec<byte> data;

	MutRange<byte> memory;
};

GlobalValuePool2* create_global_value_pool2(HandlePool* handles) noexcept
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
		panic("Failed to allocate memory for GlobalValuePool2 (0x%X).\n", minos::last_error());

	GlobalValuePool2* const globals = alloc_handle_from_pool<GlobalValuePool2>(handles);

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

void release_global_value_pool2(GlobalValuePool2* globals) noexcept
{
	minos::mem_unreserve(globals->memory.begin(), globals->memory.count());
}

GlobalFileIndex file_values_reserve2(GlobalValuePool2* globals, TypeId file_type_id, u16 definition_count) noexcept
{
	GlobalFile file;
	file.first_value_id = static_cast<ForeverValueId>(globals->forever_values.used());
	file.type_id = file_type_id;

	globals->files.append(file);

	globals->forever_values.reserve(definition_count);

	return static_cast<GlobalFileIndex>(globals->files.used() - 1);
}

void file_value_set_initializer(GlobalValuePool2* globals, GlobalFileIndex file_index, u16 rank, OpcodeId initializer) noexcept
{
	ASSERT_OR_IGNORE(file_index != GlobalFileIndex::INVALID);

	ASSERT_OR_IGNORE(static_cast<u16>(file_index) < globals->files.used());

	ASSERT_OR_IGNORE(initializer != OpcodeId::INVALID);

	const GlobalFile file = globals->files.begin()[static_cast<u16>(file_index)];

	const ForeverValueId value_id = static_cast<ForeverValueId>(static_cast<u32>(file.first_value_id) + rank);

	ASSERT_OR_IGNORE(static_cast<u32>(value_id) < globals->forever_values.used());

	ForeverValue* const value = globals->forever_values.begin() + static_cast<u32>(value_id);

	ASSERT_OR_IGNORE(!value->is_complete && value->initializer == OpcodeId::INVALID);

	value->initializer = initializer;
}

TypeId type_id_from_global_file_index(GlobalValuePool2* globals, GlobalFileIndex file_index) noexcept
{
	ASSERT_OR_IGNORE(file_index != GlobalFileIndex::INVALID);

	const GlobalFile file = globals->files.begin()[static_cast<u16>(file_index)];

	return file.type_id;
}

bool file_value_get2(GlobalValuePool2* globals, GlobalFileIndex file_index, u16 rank, ForeverCTValue* out_value, OpcodeId* out_code) noexcept
{
	ASSERT_OR_IGNORE(file_index != GlobalFileIndex::INVALID);

	ASSERT_OR_IGNORE(static_cast<u16>(file_index) < globals->files.used());

	const GlobalFile file = globals->files.begin()[static_cast<u16>(file_index)];

	const ForeverValueId value_id = static_cast<ForeverValueId>(static_cast<u32>(file.first_value_id) + rank);

	ASSERT_OR_IGNORE(static_cast<u32>(value_id) < globals->forever_values.used());

	const ForeverValue* const value = globals->forever_values.begin() + static_cast<u32>(value_id);

	if (value->is_complete)
	{
		const MutRange<byte> bytes{ globals->data.begin() + value->data_offset, value->data_size };

		const CTValue ct_value{ bytes, value->data_align, static_cast<bool>(value->is_mut), value->type };

		*out_value = ForeverCTValue{ ct_value, value_id };

		return true;
	}
	else
	{
		ASSERT_OR_IGNORE(value->initializer != OpcodeId::INVALID);

		*out_code = value->initializer;

		return false;
	}
}

ForeverValueId file_value_alloc_initialized2(GlobalValuePool2* globals, GlobalFileIndex file_index, u16 rank, bool is_mut, CTValue initializer) noexcept
{
	ASSERT_OR_IGNORE(file_index != GlobalFileIndex::INVALID && static_cast<u16>(file_index) < globals->files.used());

	ASSERT_OR_IGNORE(rank < globals->forever_values.used());

	const GlobalFile file = globals->files.begin()[static_cast<u16>(file_index)];

	const ForeverValueId value_id = static_cast<ForeverValueId>(static_cast<u32>(file.first_value_id) + rank);

	ForeverValue* const value = globals->forever_values.begin() + static_cast<u32>(value_id);

	ASSERT_OR_IGNORE(!value->is_complete);

	globals->data.pad_to_alignment(initializer.align);

	const u32 data_offset = globals->data.used();

	globals->data.reserve(static_cast<u32>(initializer.bytes.count()));

	value->type = initializer.type;
	value->data_offset = data_offset;
	value->data_size = static_cast<u32>(initializer.bytes.count());
	value->is_complete = true;
	value->is_mut = is_mut;
	value->data_align = initializer.align;

	memcpy(globals->data.begin() + data_offset, initializer.bytes.begin(), initializer.bytes.count());

	return value_id;
}

ForeverCTValue file_value_alloc_uninitialized2(GlobalValuePool2* globals, GlobalFileIndex file_index, u16 rank, bool is_mut, TypeId type, TypeMetrics metrics) noexcept
{
	ASSERT_OR_IGNORE(file_index != GlobalFileIndex::INVALID && static_cast<u16>(file_index) < globals->files.used());

	ASSERT_OR_IGNORE(rank < globals->forever_values.used());

	const GlobalFile file = globals->files.begin()[static_cast<u16>(file_index)];

	const ForeverValueId value_id = static_cast<ForeverValueId>(static_cast<u32>(file.first_value_id) + rank);

	ForeverValue* const value = globals->forever_values.begin() + static_cast<u32>(value_id);

	ASSERT_OR_IGNORE(!value->is_complete);

	globals->data.pad_to_alignment(metrics.align);

	const u32 data_offset = globals->data.used();

	globals->data.reserve(static_cast<u32>(metrics.size));

	value->type = type;
	value->data_offset = data_offset;
	value->data_size = static_cast<u32>(metrics.size);
	value->is_complete = true;
	value->is_mut = is_mut;
	value->data_align = metrics.align;

	const MutRange<byte> bytes{ globals->data.begin() + data_offset, metrics.size };

	const CTValue ct_value{ bytes, metrics.align, is_mut, type };

	return ForeverCTValue{ ct_value, value_id };
}

ForeverValueId forever_value_alloc_initialized2(GlobalValuePool2* globals, bool is_mut, CTValue initializer) noexcept
{
	const ForeverValueId value_id = static_cast<ForeverValueId>(globals->forever_values.used());

	ForeverValue* const value = globals->forever_values.reserve();

	globals->data.pad_to_alignment(initializer.align);

	const u32 data_offset = globals->data.used();

	globals->data.reserve(static_cast<u32>(initializer.bytes.count()));

	value->type = initializer.type;
	value->data_offset = data_offset;
	value->data_size = static_cast<u32>(initializer.bytes.count());
	value->is_complete = true;
	value->is_mut = is_mut;
	value->data_align = initializer.align;
	globals->forever_values.reserve(static_cast<u32>(initializer.bytes.count()));

	const MutRange<byte> bytes{ globals->data.begin() + data_offset, initializer.bytes.count() };

	range::mem_copy(bytes, initializer.bytes.immut());

	return value_id;
}

ForeverCTValue forever_value_alloc_uninitialized2(GlobalValuePool2* globals, bool is_mut, TypeId type, TypeMetrics metrics) noexcept
{
	const ForeverValueId forever_value_id = static_cast<ForeverValueId>(globals->forever_values.used());

	ForeverValue* const value = globals->forever_values.reserve();

	globals->data.pad_to_alignment(metrics.align);

	const u32 data_offset = globals->data.used();

	globals->data.reserve(static_cast<u32>(metrics.size));

	value->type = type;
	value->data_offset = data_offset;
	value->data_size = static_cast<u32>(metrics.size);
	value->is_complete = true;
	value->is_mut = is_mut;
	value->data_align = metrics.align;
	globals->forever_values.reserve(static_cast<u32>(metrics.size));

	const MutRange<byte> bytes{ globals->data.begin() + data_offset, metrics.size };

	const CTValue ct_value{ bytes, metrics.align, is_mut, type };

	return ForeverCTValue{ ct_value, forever_value_id };
}

CTValue forever_value_get2(GlobalValuePool2* globals, ForeverValueId id) noexcept
{
	ASSERT_OR_IGNORE(id != ForeverValueId::INVALID && static_cast<u32>(id) < globals->forever_values.used());

	const ForeverValue* const value = globals->forever_values.begin() + static_cast<u32>(id);

	ASSERT_OR_IGNORE(value->is_complete);

	const MutRange<byte> bytes{ globals->data.begin() + value->data_offset, value->data_size };

	return CTValue{ bytes, value->data_align, static_cast<bool>(value->is_mut), value->type };
}
