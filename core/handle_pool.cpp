#include "core.hpp"

struct HandlePool
{
	u32 reserve;

	u32 commit_increment;

	u32 commit;

	u32 used;
};

HandlePool* create_handle_pool(u32 reserve, u32 commit_increment) noexcept
{
	ASSERT_OR_IGNORE(commit_increment != 0);

	ASSERT_OR_IGNORE(reserve >= commit_increment);

	const u32 page_bytes = minos::page_bytes();

	commit_increment = (commit_increment + page_bytes - 1) & ~(page_bytes - 1);

	reserve = next_multiple(reserve, commit_increment);

	HandlePool* const pool = static_cast<HandlePool*>(minos::mem_reserve(reserve));

	if (pool == nullptr)
		panic("Could not reserve %u bytes of memory for HandlePool (0x%X)\n", reserve, minos::last_error());

	if (!minos::mem_commit(pool, commit_increment))
		panic("Could not commit initial %u bytes of memory for HandlePool (0x%X)", commit_increment, minos::last_error());

	pool->reserve = reserve;
	pool->commit_increment = commit_increment;
	pool->commit = commit_increment;
	pool->used = sizeof(HandlePool);

	return pool;
}

void release_handle_pool(HandlePool* pool) noexcept
{
	minos::mem_unreserve(pool, pool->reserve);
}

void* alloc_handle_from_pool(HandlePool* pool, u32 bytes, u32 alignment) noexcept
{
	ASSERT_OR_IGNORE(is_pow2(alignment));

	const u64 alloc_begin = (static_cast<u64>(pool->used) + alignment - 1) & ~(static_cast<u64>(alignment - 1));

	const u64 new_pool_used = static_cast<u64>(alloc_begin) + bytes;

	if (new_pool_used > pool->commit)
	{
		if (new_pool_used > pool->reserve)
		panic("Could not allocate %u bytes from HandlePool of size %u as it was already full\n", bytes, pool->reserve);

		const u32 new_pool_commit = next_multiple(static_cast<u32>(new_pool_used), pool->commit_increment);

		if (!minos::mem_commit(reinterpret_cast<byte*>(pool) + pool->commit, new_pool_commit - pool->commit))
			panic("Could not commit %u bytes of memory at offset %u in HandlePool of size %u (0x%X)\n", new_pool_commit - pool->commit, pool->commit, pool->reserve, minos::last_error());

		pool->commit = new_pool_commit;
	}

	pool->used = static_cast<u32>(new_pool_used);

	return reinterpret_cast<byte*>(pool) + alloc_begin;
}
