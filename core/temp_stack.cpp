#include "core.hpp"
#include "structure.hpp"

static constexpr u64 TEMP_STACK_RESERVE = 1 << 29;

static constexpr u64 TEMP_STACK_COMMIT_INCREMENT = 1 << 14;

bool temp_stack_validate_config([[maybe_unused]] const Config* config, [[maybe_unused]] PrintSink sink) noexcept
{
	return true;
}

MemoryRequirements temp_stack_memory_requirements([[maybe_unused]] const Config* config) noexcept
{
	MemoryRequirements reqs;
	reqs.count = 1;
	reqs.ranges[0].size = TEMP_STACK_RESERVE;
	reqs.ranges[0].max_offset = UINT64_MAX;

	return reqs;
}

void temp_stack_init(CoreData* core, MemoryAllocation allocation) noexcept
{
	ASSERT_OR_IGNORE(allocation.ranges[0].count() == TEMP_STACK_RESERVE);

	core->temp.memory.init(allocation.ranges[0], TEMP_STACK_COMMIT_INCREMENT);
}



u64 temp_stack_mark(CoreData* core) noexcept
{
	return core->temp.memory.used();
}

void* temp_stack_alloc(CoreData* core, u64 size, u64 align) noexcept
{
	ASSERT_OR_IGNORE(size < UINT32_MAX);

	ASSERT_OR_IGNORE(align < UINT32_MAX);

	core->temp.memory.pad_to_alignment(static_cast<u32>(align));

	return core->temp.memory.reserve_exact(static_cast<u32>(size));
}

void temp_stack_release(CoreData* core, u64 mark) noexcept
{
	ASSERT_OR_IGNORE(mark <= core->temp.memory.used());

	core->temp.memory.pop_to(static_cast<u32>(mark));
}
