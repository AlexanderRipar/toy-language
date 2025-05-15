#include "pass_data.hpp"

#include "../infra/container.hpp"

struct alignas(8) ValueInfo
{
	TypeIdWithAssignability type;

	u32 size_times_two;
};

struct GlobalValuePool
{
	TypePool* types;

	ReservedVec<ValueInfo> values;
};

GlobalValuePool* create_global_value_pool(AllocPool* alloc) noexcept
{
	GlobalValuePool* const globals = static_cast<GlobalValuePool*>(alloc_from_pool(alloc, sizeof(GlobalValuePool), alignof(GlobalValuePool)));
	globals->values.init(1 << 28, 1 << 11);

	(void) globals->values.reserve_exact(sizeof(u64));

	return globals;
}

void release_global_value_pool(GlobalValuePool* globals) noexcept
{
	globals->values.release();
}

GlobalValueId make_global_value(GlobalValuePool* globals, TypeIdWithAssignability type, u64 size, u32 align, const void* opt_initial_value) noexcept
{
	if (size > (UINT32_MAX >> 1))
		panic("Size %u of type exceeds maximum supported global value size.\n", size);

	ValueInfo* const info = static_cast<ValueInfo*>(globals->values.reserve_exact(sizeof(ValueInfo)));
	info->type = type;

	globals->values.pad_to_alignment(align);

	void* const value = globals->values.reserve_padded(static_cast<u32>(size));

	if (value == info + 1)
	{
		info->size_times_two = static_cast<u32>(size << 1);
	}
	else
	{
		info->size_times_two = static_cast<u32>(size << 1) | 1;

		*reinterpret_cast<u64*>(info + 1) = static_cast<u64>(reinterpret_cast<u64*>(value) - reinterpret_cast<u64*>(info));
	}

	if (opt_initial_value != nullptr)
		memcpy(value, opt_initial_value, size);

	return GlobalValueId{ static_cast<u32>(info - globals->values.begin()) };
}

GlobalValue global_value_from_id(GlobalValuePool* globals, GlobalValueId value_id) noexcept
{
	ASSERT_OR_IGNORE(static_cast<u32>(value_id) < globals->values.used());

	ValueInfo* info = globals->values.begin() + static_cast<u32>(value_id);

	void* value;

	if ((info->size_times_two & 1) == 0)
		value = info + 1;
	else
		value = info + *reinterpret_cast<u64*>(info + 1);

	return { info->type, info->size_times_two >> 1, value };
}
