#include "pass_data.hpp"

#include "ast2_attach.hpp"

struct ScopePool
{
	ReservedVec<u64> static_pool;

	ReservedVec<u64> dynamic_stack;

	Scope* builtins_scope;
};

ScopePool* create_scope_pool(AllocPool* alloc, a2::Node* builtins) noexcept
{
	ScopePool* const scopes = static_cast<ScopePool*>(alloc_from_pool(alloc, sizeof(ScopePool), alignof(ScopePool)));

	scopes->static_pool.init(1 << 24, 1 << 16);

	scopes->dynamic_stack.init(1 << 24, 1 << 16);

	(void) scopes->static_pool.reserve_exact(sizeof(u64));

	a2::BlockData* const builtins_block = a2::attachment_of<a2::BlockData>(builtins);

	Scope* const builtins_scope = alloc_static_scope(scopes, nullptr, builtins, builtins_block->definition_count);

	a2::DirectChildIterator it = a2::direct_children_of(builtins);

	for (OptPtr<a2::Node> rst = a2::next(&it); is_some(rst); rst = a2::next(&it))
	{
		a2::Node* const builtin_definition = get_ptr(rst);

		if (builtin_definition->tag == a2::Tag::Definition)
			add_definition_to_scope(builtins_scope, builtin_definition);
	}

	ASSERT_OR_IGNORE(builtins_scope->header.capacity == builtins_scope->header.used);

	builtins_block->scope_id = id_from_static_scope(scopes, builtins_scope);

	scopes->builtins_scope = builtins_scope;

	return scopes;
}

void release_scope_pool(ScopePool* scopes) noexcept
{
	scopes->static_pool.release();

	scopes->dynamic_stack.release();
}

Scope* alloc_file_scope(ScopePool* scopes, a2::Node* root) noexcept
{
	ASSERT_OR_IGNORE(root->tag == a2::Tag::File);

	a2::BlockData* const file_block_data = &a2::attachment_of<a2::FileData>(root)->root_block;

	Scope* const scope = static_cast<Scope*>(scopes->static_pool.reserve_exact(sizeof(Scope) + file_block_data->definition_count * sizeof(ScopeEntry)));
	scope->header.root = root;
	scope->header.parent_scope = scopes->builtins_scope;
	scope->header.capacity = file_block_data->definition_count;
	scope->header.used = 0;

	const ScopeId scope_id = id_from_static_scope(scopes, scope);

	a2::DirectChildIterator it = a2::direct_children_of(root);

	for (OptPtr<a2::Node> rst = a2::next(&it); is_some(rst); rst = a2::next(&it))
	{
		a2::Node* const node = get_ptr(rst);

		if (node->tag != a2::Tag::Definition)
			continue;

		add_definition_to_scope(scope, node);
	}

	return scope;
}

Scope* alloc_static_scope(ScopePool* scopes, Scope* parent_scope, a2::Node* root, u32 capacity) noexcept
{
	ASSERT_OR_IGNORE(parent_scope == nullptr || (reinterpret_cast<u64*>(parent_scope) >= scopes->static_pool.begin() && reinterpret_cast<u64*>(parent_scope) < scopes->static_pool.end()));

	Scope* const scope = static_cast<Scope*>(scopes->static_pool.reserve_exact(sizeof(ScopeHeader) + capacity * sizeof(ScopeEntry)));

	scope->header.root = root;
	scope->header.parent_scope = parent_scope;
	scope->header.capacity = capacity;
	scope->header.used = 0;

	return scope;
}

Scope* alloc_dynamic_scope(ScopePool* scopes, Scope* parent_scope, a2::Node* root, u32 capacity) noexcept
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

void add_definition_to_scope(Scope* scope, a2::Node* definition) noexcept
{
	ASSERT_OR_IGNORE(definition->tag == a2::Tag::Definition);

	ASSERT_OR_IGNORE(scope->header.used < scope->header.capacity);

	a2::DefinitionData* const definition_data = a2::attachment_of<a2::DefinitionData>(definition);

	ASSERT_OR_IGNORE(definition_data->identifier_id != INVALID_IDENTIFIER_ID);

	ASSERT_OR_IGNORE(definition > scope->header.root && definition < a2::apply_offset_(scope->header.root, scope->header.root->next_sibling_offset));

	ScopeEntry* const entry = scope->definitions + scope->header.used;

	entry->identifier_id = definition_data->identifier_id;
	entry->node_offset = static_cast<u32>(reinterpret_cast<u32*>(definition) - reinterpret_cast<u32*>(scope->header.root));

	scope->header.used += 1;
}

ScopeLookupResult lookup_identifier_recursive(Scope* scope, IdentifierId identifier_id) noexcept
{
	while (scope != nullptr)
	{
		const OptPtr<a2::Node> result = lookup_identifier_local(scope, identifier_id);

		if (is_some(result))
			return { get_ptr(result), scope };

		scope = scope->header.parent_scope;
	}

	return { nullptr, nullptr };
}

OptPtr<a2::Node> lookup_identifier_local(Scope* scope, IdentifierId identifier_id) noexcept
{
	for (u32 i = 0; i != scope->header.used; ++i)
	{
		if (scope->definitions[i].identifier_id == identifier_id)
			return some(a2::apply_offset_(scope->header.root, scope->definitions[i].node_offset));
	}

	return none<a2::Node>();
}
