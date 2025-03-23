#include "pass_data.hpp"

#include "ast_attach.hpp"
#include "infra/common.hpp"
#include "infra/container.hpp"

struct AstPool
{
	ReservedVec<u32> pool;
};

AstPool* create_ast_pool(AllocPool* pool) noexcept
{
	AstPool* const asts = static_cast<AstPool*>(alloc_from_pool(pool, sizeof(AstPool), alignof(AstPool)));

	asts->pool.init(1u << 30, 1u << 18);

	(void) asts->pool.reserve_exact(sizeof(*asts->pool.begin()));

	return asts;
}

void release_ast_pool(AstPool* asts) noexcept
{
	asts->pool.release();
}

AstNode* alloc_ast(AstPool* asts, u32 dwords) noexcept
{
	return static_cast<AstNode*>(asts->pool.reserve_exact(dwords * sizeof(u32)));
}

AstNodeId id_from_ast_node(AstPool* asts, AstNode* node) noexcept
{
	return AstNodeId{ static_cast<u32>(reinterpret_cast<u32*>(node) - asts->pool.begin()) };
}

AstNode* ast_node_from_id(AstPool* asts, AstNodeId id) noexcept
{
	return reinterpret_cast<AstNode*>(asts->pool.begin() + id.rep);
}
