#include "pass_data.hpp"

#include "infra/container.hpp"

struct ValuePool
{
	ReservedVec<u64> pool;
};

static_assert(alignof(Value) == alignof(u64));

ValuePool* create_value_pool(AllocPool* alloc) noexcept
{
	ValuePool* const values = static_cast<ValuePool*>(alloc_from_pool(alloc, sizeof(ValuePool), alignof(ValuePool)));

	values->pool.init(1 << 30, 1 << 16);

	(void) values->pool.reserve_exact(sizeof(u64));

	return values;
}

void release_value_pool(ValuePool* values) noexcept
{
	values->pool.release();
}

ValueLocation alloc_value(ValuePool* values, u32 bytes, u32 alignment) noexcept
{
	ASSERT_OR_IGNORE(bytes < 1u << 30);

	ASSERT_OR_IGNORE(is_pow2(alignment));

	if (alignment > 4096)
		panic("Could not allocate interpreter value, as requested alignment %u exceeds maximum supported alignment of 4096\n", alignment);

	if (alignment > alignof(Value))
		values->pool.pad_to_alignment(alignment);

	Value* const value = static_cast<Value*>(values->pool.reserve_padded(sizeof(Value) + bytes));

	return { value, static_cast<u32>(reinterpret_cast<u64*>(value) - values->pool.begin()) };
}

Value* value_from_id(ValuePool* values, ValueId id) noexcept
{
	ASSERT_OR_IGNORE(id != INVALID_VALUE_ID);

	return reinterpret_cast<Value*>(values->pool.begin() + id.rep);
}
