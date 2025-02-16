#include "pass_data.hpp"

#include "ast_attach.hpp"

struct ScopePool
{
	ReservedVec<u64> static_pool;

	ReservedVec<u64> dynamic_stack;

	Scope* builtins_scope;
};

ScopePool* create_scope_pool(AllocPool* alloc, AstNode* builtins) noexcept
{
	ScopePool* const scopes = static_cast<ScopePool*>(alloc_from_pool(alloc, sizeof(ScopePool), alignof(ScopePool)));

	scopes->static_pool.init(1 << 24, 1 << 16);

	scopes->dynamic_stack.init(1 << 24, 1 << 16);

	(void) scopes->static_pool.reserve_exact(sizeof(u64));

	BlockData* const builtins_block = attachment_of<BlockData>(builtins);

	Scope* const builtins_scope = alloc_static_scope(scopes, nullptr, builtins, builtins_block->definition_count);

	AstDirectChildIterator it = direct_children_of(builtins);

	for (OptPtr<AstNode> rst = next(&it); is_some(rst); rst = next(&it))
	{
		AstNode* const builtin_definition = get_ptr(rst);

		if (builtin_definition->tag == AstTag::Definition)
			add_definition_to_scope(builtins_scope, builtin_definition);
	}

	ASSERT_OR_IGNORE(builtins_scope->header.capacity == builtins_scope->header.used);

	scopes->builtins_scope = builtins_scope;

	return scopes;
}

void release_scope_pool(ScopePool* scopes) noexcept
{
	scopes->static_pool.release();

	scopes->dynamic_stack.release();
}

Scope* alloc_file_scope(ScopePool* scopes, AstNode* root) noexcept
{
	ASSERT_OR_IGNORE(root->tag == AstTag::File);

	BlockData* const file_block_data = &attachment_of<FileData>(root)->root_block;

	Scope* const scope = static_cast<Scope*>(scopes->static_pool.reserve_exact(sizeof(Scope) + file_block_data->definition_count * sizeof(ScopeEntry)));
	scope->header.root = root;
	scope->header.parent_scope = scopes->builtins_scope;
	scope->header.capacity = file_block_data->definition_count;
	scope->header.used = 0;

	const ScopeId scope_id = id_from_static_scope(scopes, scope);

	AstDirectChildIterator it = direct_children_of(root);

	for (OptPtr<AstNode> rst = next(&it); is_some(rst); rst = next(&it))
	{
		AstNode* const node = get_ptr(rst);

		if (node->tag != AstTag::Definition)
			continue;

		add_definition_to_scope(scope, node);
	}

	return scope;
}

Scope* alloc_static_scope(ScopePool* scopes, Scope* parent_scope, AstNode* root, u32 capacity) noexcept
{
	ASSERT_OR_IGNORE(parent_scope == nullptr || (reinterpret_cast<u64*>(parent_scope) >= scopes->static_pool.begin() && reinterpret_cast<u64*>(parent_scope) < scopes->static_pool.end()));

	Scope* const scope = static_cast<Scope*>(scopes->static_pool.reserve_exact(sizeof(ScopeHeader) + capacity * sizeof(ScopeEntry)));

	scope->header.root = root;
	scope->header.parent_scope = parent_scope;
	scope->header.capacity = capacity;
	scope->header.used = 0;

	return scope;
}

Scope* alloc_dynamic_scope(ScopePool* scopes, Scope* parent_scope, AstNode* root, u32 capacity) noexcept
{
	Scope* const scope = static_cast<Scope*>(scopes->dynamic_stack.reserve_exact(sizeof(ScopeHeader) + capacity * sizeof(ScopeEntry)));

	scope->header.root = root;
	scope->header.parent_scope = parent_scope;
	scope->header.capacity = capacity;
	scope->header.used = 0;

	return scope;
}

void release_dynamic_scope(ScopePool* scopes, Scope* scope) noexcept
{
	ASSERT_OR_IGNORE(reinterpret_cast<u64*>(scope) >= scopes->dynamic_stack.begin() && reinterpret_cast<u64*>(scope) < scopes->dynamic_stack.end());

	scopes->dynamic_stack.pop(static_cast<u32>(scopes->dynamic_stack.end() - reinterpret_cast<u64*>(scope)));
}

ScopeId id_from_static_scope(ScopePool* scopes, Scope* scope) noexcept
{
	ASSERT_OR_IGNORE(reinterpret_cast<u64*>(scope) >= scopes->static_pool.begin() && reinterpret_cast<u64*>(scope) < scopes->static_pool.end());

	return ScopeId{ static_cast<u32>(reinterpret_cast<u64*>(scope) - scopes->static_pool.begin()) };
}

Scope* scope_from_id(ScopePool* scopes, ScopeId id) noexcept
{
	return reinterpret_cast<Scope*>(scopes->static_pool.begin() + id.rep);
}

void add_definition_to_scope(Scope* scope, AstNode* definition) noexcept
{
	ASSERT_OR_IGNORE(definition->tag == AstTag::Definition);

	ASSERT_OR_IGNORE(scope->header.used < scope->header.capacity);

	DefinitionData* const definition_data = attachment_of<DefinitionData>(definition);

	ASSERT_OR_IGNORE(definition_data->identifier_id != INVALID_IDENTIFIER_ID);

	ASSERT_OR_IGNORE(definition > scope->header.root && definition < apply_offset_(scope->header.root, scope->header.root->next_sibling_offset));

	ScopeEntry* const entry = scope->definitions + scope->header.used;

	entry->identifier_id = definition_data->identifier_id;
	entry->node_offset = static_cast<u32>(reinterpret_cast<u32*>(definition) - reinterpret_cast<u32*>(scope->header.root));

	scope->header.used += 1;
}

ScopeLookupResult lookup_identifier_recursive(Scope* scope, IdentifierId identifier_id) noexcept
{
	while (scope != nullptr)
	{
		const OptPtr<AstNode> result = lookup_identifier_local(scope, identifier_id);

		if (is_some(result))
			return { get_ptr(result), scope };

		scope = scope->header.parent_scope;
	}

	return { nullptr, nullptr };
}

OptPtr<AstNode> lookup_identifier_local(Scope* scope, IdentifierId identifier_id) noexcept
{
	for (u32 i = 0; i != scope->header.used; ++i)
	{
		if (scope->definitions[i].identifier_id == identifier_id)
			return some(apply_offset_(scope->header.root, scope->definitions[i].node_offset));
	}

	return none<AstNode>();
}
