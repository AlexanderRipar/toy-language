#include "pass_data.hpp"

#include "../infra/container.hpp"

struct GlobalValuePool
{
	TypePool* types;

	ReservedVec<u64> values;
};

GlobalValuePool* create_global_value_pool(AllocPool* alloc, TypePool* types) noexcept
{
	GlobalValuePool* const globals = static_cast<GlobalValuePool*>(alloc_from_pool(alloc, sizeof(GlobalValuePool), alignof(GlobalValuePool)));
	globals->types = types;
	globals->values.init(1 << 28, 1 << 11);

	return globals;
}

void release_global_value_pool(GlobalValuePool* globals) noexcept
{
	globals->values.release();
}

GlobalValueId make_global_value(GlobalValuePool* globals, u64 size, u32 align, const void* opt_initial_value) noexcept
{
	if (size >=  UINT32_MAX)
		panic("Size %u of type exceeds maximum supported global value size.\n", size);

	globals->values.pad_to_alignment(align);

	u64* const value = static_cast<u64*>(globals->values.reserve_padded(static_cast<u32>(size)));

	if (opt_initial_value != nullptr)
		memcpy(value, opt_initial_value, size);

	return GlobalValueId{ static_cast<u32>(value - globals->values.begin()) };
}

void* global_value_from_id(GlobalValuePool* globals, GlobalValueId value_id) noexcept
{
	ASSERT_OR_IGNORE(globals->values.used() >= value_id.rep);

	return globals->values.begin() + value_id.rep;
}
