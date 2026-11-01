#ifndef RESERVED_BYTE_STACK_INCLUDE_GUARD
#define RESERVED_BYTE_STACK_INCLUDE_GUARD

#include "../types.hpp"
#include "../assert.hpp"
#include "../panic.hpp"
#include "../math.hpp"
#include "../range.hpp"
#include "../minos/minos.hpp"

// A stack of bytes reserved at initialization time via `minos::mem_reserve`
// and then incrementally committed via `minos::mem_commit`.
// Just like the program stack, a `ReservedByteStack` grows from higher
// addresses to lower addresses. Thus, `base >= top`.
// A `ReservedByteStack` may address at most 4 GB of memory, meaning indices
// into it can be stored in a `u32`.
struct ReservedByteStack
{
	byte* base;

	byte* top;

	byte* commit_top;

	u32 reserve_size;

	u32 commit_increment_size;
};

static inline void reserved_stack_ensure_space(ReservedByteStack* stack, u64 free_size) noexcept
{
	if (static_cast<u64>(stack->top - stack->commit_top) >= free_size)
		return;

	u64 extra_commit_size = stack->commit_increment_size;

	while (extra_commit_size < free_size)
		extra_commit_size += stack->commit_increment_size;

	byte* const new_commit_top = stack->commit_top - extra_commit_size;

	if (new_commit_top < stack->base - stack->reserve_size)
		panic("Exhausted %u bytes of reserved memory for ReservedByteStack.\n", stack->reserve_size);

	if (!minos::mem_commit(stack->commit_top, extra_commit_size))
		panic("Failed to commit %u bytes for ReservedByteStack.\n", extra_commit_size);

	stack->commit_top = new_commit_top;
}

static inline ReservedByteStack init_reserved_stack(MutRange<byte> reserved, u64 commit_increment_size) noexcept
{
	ASSERT_OR_IGNORE(commit_increment_size >= minos::page_bytes());

	ASSERT_OR_IGNORE((commit_increment_size & (minos::page_bytes() - 1)) == 0);

	ASSERT_OR_IGNORE(commit_increment_size <= UINT32_MAX);

	ASSERT_OR_IGNORE((reserved.count() & static_cast<u64>(minos::page_bytes() - 1)) == 0);

	ASSERT_OR_IGNORE(reserved.count() <= UINT32_MAX);

	ASSERT_OR_IGNORE(reserved.count() >= commit_increment_size);

	if (!minos::mem_commit(reserved.end() - commit_increment_size, commit_increment_size))
		panic("Failed to commit %u bytes for ReservedByteStack.\n", commit_increment_size);

	ReservedByteStack stack;
	stack.base = reserved.end();
	stack.top = reserved.end();
	stack.commit_top = reserved.end() - commit_increment_size;
	stack.reserve_size = static_cast<u32>(reserved.count());
	stack.commit_increment_size = static_cast<u32>(commit_increment_size);

	return stack;
}

static inline void* push(ReservedByteStack* stack, u64 size) noexcept
{
	reserved_stack_ensure_space(stack, size);

	stack->top -= size;

	return stack->top;
}

static inline void* push_aligned(ReservedByteStack* stack, u64 size, u64 align) noexcept
{
	size = (size + align - 1) & ~(align - 1);

	reserved_stack_ensure_space(stack, size);

	stack->top -= size;

	return stack->top;
}

static inline void pop_by(ReservedByteStack* stack, u64 size) noexcept
{
	ASSERT_OR_IGNORE(stack->top + size >= stack->base);

	stack->top += size;
}

static inline void pop_to(ReservedByteStack* stack, u64 new_top) noexcept
{
	ASSERT_OR_IGNORE(stack->base + new_top >= stack->top);

	stack->top = stack->base + new_top;
}

static inline void pad_to_alignment(ReservedByteStack* stack, u64 padded_align) noexcept
{
	ASSERT_OR_IGNORE(padded_align != 0 && is_pow2(padded_align));

	const u64 top = reinterpret_cast<u64>(stack->top);

	const u64 new_top = top & (padded_align - 1);

	reserved_stack_ensure_space(stack, top - new_top);

	stack->top = reinterpret_cast<byte*>(new_top);
}

static inline void* top(ReservedByteStack* stack) noexcept
{
	return stack->top;
}

static inline const void* top(const ReservedByteStack* stack) noexcept
{
	return stack->top;
}

static inline void* from_top(ReservedByteStack* stack, u64 index) noexcept
{
	ASSERT_OR_IGNORE(static_cast<u64>(stack->base - stack->top) >= index);

	return stack->top + index;
}

static inline const void* from_top(const ReservedByteStack* stack, u64 index) noexcept
{
	ASSERT_OR_IGNORE(static_cast<u64>(stack->base - stack->top) >= index);

	return stack->top + index;
}

static inline u32 index_from_base(const ReservedByteStack* stack, const void* pointer) noexcept
{
	ASSERT_OR_IGNORE(pointer <= stack->base && pointer >= stack->top);

	return static_cast<u32>(static_cast<const byte*>(pointer) - stack->top);
}

static inline void* base(ReservedByteStack* stack) noexcept
{
	return stack->base;
}

static inline const void* base(const ReservedByteStack* stack) noexcept
{
	return stack->base;
}

static inline void* from_base(ReservedByteStack* stack, u64 index) noexcept
{
	ASSERT_OR_IGNORE(static_cast<u64>(stack->base - stack->top) >= index);

	return stack->base - index;
}

static inline const void* from_base(const ReservedByteStack* stack, u64 index) noexcept
{
	ASSERT_OR_IGNORE(static_cast<u64>(stack->base - stack->top) >= index);

	return stack->base - index;
}

static inline bool contains(const ReservedByteStack* stack, const void* pointer) noexcept
{
	return pointer <= stack->base && pointer >= stack->top;
}

static inline u32 used(const ReservedByteStack* stack) noexcept
{
	return static_cast<u32>(stack->base - stack->top);
}

#endif // RESERVED_BYTE_STACK_INCLUDE_GUARD
