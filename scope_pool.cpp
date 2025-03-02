#include "pass_data.hpp"

#include "ast_attach.hpp"

struct ScopePool
{
	ReservedVec<u64> scopes;

	Scope* builtins_scope;
};

static Scope* alloc_scope_internal(ScopePool* scopes, Scope* parent_scope, AstNode* root, u32 capacity) noexcept
{
	Scope* const scope = static_cast<Scope*>(scopes->scopes.reserve_exact(sizeof(ScopeHeader) + capacity * sizeof(ScopeEntry)));

	scope->header.root = root;
	scope->header.parent_scope = parent_scope == nullptr ? scopes->builtins_scope : parent_scope;
	scope->header.capacity = capacity;
	scope->header.used = 0;

	return scope;
}

ScopePool* create_scope_pool(AllocPool* alloc, AstNode* builtins) noexcept
{
	ScopePool* const scopes = static_cast<ScopePool*>(alloc_from_pool(alloc, sizeof(ScopePool), alignof(ScopePool)));

	scopes->scopes.init(1 << 24, 1 << 16);

	(void) scopes->scopes.reserve_exact(sizeof(u64));

	BlockData* const builtins_block = attachment_of<BlockData>(builtins);

	Scope* const builtins_scope = alloc_scope_internal(scopes, nullptr, builtins, builtins_block->definition_count);

	builtins_block->scope_id = id_from_scope(scopes, builtins_scope);

	AstDirectChildIterator it = direct_children_of(builtins);

	for (OptPtr<AstNode> rst = next(&it); is_some(rst); rst = next(&it))
	{
		AstNode* const builtin_definition = get_ptr(rst);

		if (builtin_definition->tag == AstTag::Definition)
		{
			if (!add_definition_to_scope(builtins_scope, builtin_definition))
				panic("Duplicate builtin definition :(\n");
		}
	}

	ASSERT_OR_IGNORE(builtins_scope->header.capacity == builtins_scope->header.used);

	scopes->builtins_scope = builtins_scope;

	return scopes;
}

void release_scope_pool(ScopePool* scopes) noexcept
{
	scopes->scopes.release();
}

Scope* alloc_scope(ScopePool* scopes, Scope* parent_scope, AstNode* root, u32 capacity) noexcept
{
	ASSERT_OR_IGNORE((parent_scope == nullptr) == (root->tag == AstTag::File));

	return alloc_scope_internal(scopes, parent_scope, root, capacity);
}

ScopeId id_from_scope(ScopePool* scopes, Scope* scope) noexcept
{
	ASSERT_OR_IGNORE(reinterpret_cast<u64*>(scope) >= scopes->scopes.begin() && reinterpret_cast<u64*>(scope) < scopes->scopes.end());

	return ScopeId{ static_cast<u32>(reinterpret_cast<u64*>(scope) - scopes->scopes.begin()) };
}

Scope* scope_from_id(ScopePool* scopes, ScopeId id) noexcept
{
	return reinterpret_cast<Scope*>(scopes->scopes.begin() + id.rep);
}

bool add_definition_to_scope(Scope* scope, AstNode* definition) noexcept
{
	ASSERT_OR_IGNORE(definition->tag == AstTag::Definition);

	ASSERT_OR_IGNORE(scope->header.used < scope->header.capacity);

	DefinitionData* const definition_data = attachment_of<DefinitionData>(definition);

	ASSERT_OR_IGNORE(definition_data->identifier_id != INVALID_IDENTIFIER_ID);

	ASSERT_OR_IGNORE(definition > scope->header.root && definition < apply_offset_(scope->header.root, scope->header.root->next_sibling_offset));

	const IdentifierId new_identifier_id = definition_data->identifier_id;

	for (u32 i = 0; i != scope->header.used; ++i)
	{
		if (scope->definitions[i].identifier_id == new_identifier_id)
			return false;
	}

	ScopeEntry* const entry = scope->definitions + scope->header.used;

	entry->identifier_id = new_identifier_id;
	entry->node_offset = get_offset(scope->header.root, definition);

	scope->header.used += 1;

	return true;
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
