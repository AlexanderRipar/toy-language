#include "core.hpp"

#include "../infra/container.hpp"

struct alignas(8) ValueInfo
{
	u32 size;

	u32 offset;
};

struct GlobalValuePool
{
	ReservedVec<ValueInfo> values;

	MutRange<byte> memory;
};

GlobalValuePool* create_global_value_pool(AllocPool* alloc) noexcept
{
	static constexpr u64 VALUES_SIZE = (static_cast<u64>(1) << 28) * sizeof(ValueInfo);

	byte* const memory = static_cast<byte*>(minos::mem_reserve(VALUES_SIZE));

	if (memory == nullptr)
		panic("Could not reserve memory for GlobalValuePool (0x%X).\n", minos::last_error());

	GlobalValuePool* const globals = static_cast<GlobalValuePool*>(alloc_from_pool(alloc, sizeof(GlobalValuePool), alignof(GlobalValuePool)));
	globals->values.init(MutRange<byte>{ memory, VALUES_SIZE }, 1 << 11);
	globals->memory = MutRange<byte>{ memory, VALUES_SIZE };

	(void) globals->values.reserve_exact(sizeof(u64));

	return globals;
}

void release_global_value_pool(GlobalValuePool* globals) noexcept
{
	minos::mem_unreserve(globals->memory.begin(), globals->memory.count());
}

GlobalValueId alloc_global_value(GlobalValuePool* globals, u64 size, u32 align) noexcept
{
	if (size > (UINT32_MAX >> 1))
		panic("Size %u of type exceeds maximum supported global value size.\n", size);

	ValueInfo* const info = static_cast<ValueInfo*>(globals->values.reserve_exact(sizeof(ValueInfo)));

	globals->values.pad_to_alignment(align);

	void* const value = globals->values.reserve_padded(static_cast<u32>(size));

	info->size = static_cast<u32>(size);
	info->offset = static_cast<u32>(static_cast<u64*>(value) - reinterpret_cast<u64*>(info));

	return GlobalValueId{ static_cast<u32>(info - globals->values.begin()) };
}

static byte* address_from_info(ValueInfo* info) noexcept
{
	static_assert(sizeof(ValueInfo) == sizeof(u64));

	static_assert(alignof(ValueInfo) == alignof(u64));

	return reinterpret_cast<byte*>(info + info->offset);
}

static ValueInfo* info_from_id(GlobalValuePool* globals, GlobalValueId value_id) noexcept
{
	ASSERT_OR_IGNORE(value_id != GlobalValueId::INVALID);

	ASSERT_OR_IGNORE(static_cast<u32>(value_id) < globals->values.used());

	return globals->values.begin() + static_cast<u32>(value_id);
}

Range<byte> global_value_get(const GlobalValuePool* globals, GlobalValueId value_id) noexcept
{
	ValueInfo* const info = info_from_id(const_cast<GlobalValuePool*>(globals), value_id);

	byte* const address = static_cast<byte*>(address_from_info(info));

	return Range{ address, info->size };
}

MutRange<byte> global_value_get_mut(GlobalValuePool* globals, GlobalValueId value_id) noexcept
{
	ValueInfo* const info = info_from_id(globals, value_id);

	byte* const address = address_from_info(info);

	return MutRange{ address, info->size };
}

void global_value_set(GlobalValuePool* globals, GlobalValueId value_id, u64 offset, Range<byte> data) noexcept
{
	ValueInfo* const info = info_from_id(globals, value_id);

	ASSERT_OR_IGNORE(info->size >= data.count() + offset);

	byte* const address = address_from_info(info);

	memcpy(address + offset, data.begin(), data.count());
}
