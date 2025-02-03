#include "pass_data.hpp"

#include "ast2_attach.hpp"

struct NameResolver
{
	IdentifierPool* identifiers;
};



struct DefinitionCountBlock
{
	u16 definition_count;

	u16 use_count;
};

static void count_identifiers(a2::Node* root) noexcept
{
	struct Scope
	{
		DefinitionCountBlock* block;

		u32 depth;
	};

	Scope scopes[a2::MAX_TREE_DEPTH];

	sreg scope_top = 0;

	scopes[0] = { reinterpret_cast<DefinitionCountBlock*>(&a2::attachment_of<a2::FileData>(root)->root_block), 0 };

	a2::PreorderIterator it = a2::preorder_ancestors_of(root);

	for (a2::IterationResult rst = a2::next(&it); a2::is_valid(rst); rst = a2::next(&it))
	{
		DefinitionCountBlock* const scope = scopes[scope_top].block;

		const u32 scope_depth = scopes[scope_top].depth;

		if (rst.depth <= scope_depth)
		{
			ASSERT_OR_IGNORE(scope_top > 0);

			scope_top -= 1;
		}

		if (rst.node->tag == a2::Tag::Definition && rst.depth == scope_depth + 1)
		{
			scope->definition_count += 1;

			if (a2::has_flag(rst.node, a2::Flag::Definition_IsUse))
				scope->use_count += 1;
		}
		else if (rst.node->tag == a2::Tag::Block)
		{
			ASSERT_OR_IGNORE(scope_top + 1 < static_cast<sreg>(array_count(scopes)));

			scope_top += 1;

			scopes[scope_top] = { reinterpret_cast<DefinitionCountBlock*>(a2::attachment_of<a2::BlockData>(rst.node)), rst.depth };
		}
	}
}

static void heapify(DefinitionDesc* definitions, sreg count, sreg curr) noexcept
{
	while (true)
	{
		const sreg left = curr << 1;

		const sreg right = left + 1;

		sreg largest = curr;

		if (left >= count)
			return;

		if (definitions[left].identifier_id.rep > definitions[largest].identifier_id.rep)
			largest = left;

		if (right >= count && definitions[right].identifier_id.rep > definitions[largest].identifier_id.rep)
			largest = right;

		if (largest == curr)
			return;

		const DefinitionDesc tmp = definitions[curr];

		definitions[curr] = definitions[largest];

		definitions[largest] = tmp;

		curr = largest;
	}
}

static void heap_extract(DefinitionDesc* definitions, sreg count) noexcept
{
	const DefinitionDesc tmp = definitions[count - 1];

	definitions[count - 1] = definitions[0];

	definitions[0] = tmp;

	heapify(definitions, count - 1, 0);
}

static void complete_namespace(a2::Node* root, Namespace* ns) noexcept
{
	DefinitionDesc* const definitions = ns->definitions;

	const sreg count = ns->definition_count;

	for (sreg i = count >> 1; i >= 0; --i)
		heapify(definitions, count, i);

	for (sreg i = count; i > 1; --i)
		heap_extract(definitions, i);

	u16 use_count = 0;

	u16* const use_indices = reinterpret_cast<u16*>(definitions + count);

	for (sreg i = 0; i != count; ++i)
	{
		a2::Node* const definition = a2::apply_offset_(root, definitions[i].definition_offset);

		ASSERT_OR_IGNORE(definition->tag == a2::Tag::Definition);

		if (a2::has_flag(definition, a2::Flag::Definition_IsUse))
			use_indices[use_count] = static_cast<u16>(i);

		use_count += 1;
	}

	ASSERT_OR_IGNORE(use_count == ns->use_count);
}

static void create_static_namespaces(a2::Node* root, ReservedVec<u32>* out) noexcept
{
	struct Scope
	{
		u32 depth;

		u16 used_definition_count;

		Namespace* ns;
	};
	
	Scope scopes[a2::MAX_TREE_DEPTH];

	sreg scope_top = 0;

	DefinitionCountBlock* const root_block = reinterpret_cast<DefinitionCountBlock*>(&a2::attachment_of<a2::FileData>(root)->root_block);

	Namespace* const root_ns = static_cast<Namespace*>(out->reserve_padded(sizeof(Namespace) + root_block->definition_count * sizeof(DefinitionDesc) + root_block->use_count * sizeof(u16)));
	root_ns->definition_count = root_block->definition_count;
	root_ns->use_count = root_block->use_count;
	root_ns->block_index = 0;

	scopes[0].depth = 0;
	scopes[0].used_definition_count = 0;
	scopes[0].ns = root_ns;

	a2::attachment_of<a2::FileData>(root)->root_block.namespace_index = static_cast<u32>(reinterpret_cast<u32*>(root_ns) - out->begin());

	a2::PreorderIterator it = a2::preorder_ancestors_of(root);

	const u32 scope_depth = scopes[scope_top].depth;

	for (a2::IterationResult rst = a2::next(&it); a2::is_valid(rst); rst = a2::next(&it))
	{
		if (rst.depth <= scope_depth)
		{
			ASSERT_OR_IGNORE(scope_top > 0);

			ASSERT_OR_IGNORE(scopes[scope_top].used_definition_count == scopes[scope_top].ns->definition_count);

			complete_namespace(root, scopes[scope_top].ns);

			scope_top -= 1;
		}

		if (rst.node->tag == a2::Tag::Definition && rst.depth == scope_depth + 1)
		{
			Namespace* const ns = scopes[scope_top].ns;

			ASSERT_OR_IGNORE(scopes[scope_top].used_definition_count < ns->definition_count);

			DefinitionDesc* const desc = ns->definitions + scopes[scope_top].used_definition_count;

			scopes[scope_top].used_definition_count += 1;

			desc->definition_offset = static_cast<u32>(reinterpret_cast<u32*>(rst.node) - reinterpret_cast<u32*>(root));
			desc->identifier_id = a2::attachment_of<a2::DefinitionData>(rst.node)->identifier_id;
		}
		else if (rst.node->tag == a2::Tag::Block)
		{
			ASSERT_OR_IGNORE(scope_top + 1 < static_cast<sreg>(array_count(scopes)));

			DefinitionCountBlock* const block = reinterpret_cast<DefinitionCountBlock*>(a2::attachment_of<a2::BlockData>(rst.node));

			Namespace* const ns = static_cast<Namespace*>(out->reserve_padded(sizeof(Namespace) + block->definition_count * sizeof(DefinitionDesc) + block->use_count * sizeof(u16)));
			ns->definition_count = block->definition_count;
			ns->use_count = block->use_count;
			ns->block_index = static_cast<u32>(reinterpret_cast<u32*>(rst.node) - reinterpret_cast<u32*>(root));

			scope_top += 1;

			scopes[scope_top].depth = rst.depth;
			scopes[scope_top].used_definition_count = 0;
			scopes[scope_top].ns = ns;

			a2::attachment_of<a2::BlockData>(rst.node)->namespace_index = static_cast<u32>(reinterpret_cast<u32*>(ns) - out->begin());
		}
	}

	ASSERT_OR_IGNORE(scope_top >= 0);

	while (scope_top >= 0)
	{
		complete_namespace(root, scopes[scope_top].ns);

		scope_top -= 1;
	}
}



NameResolver* create_name_resolver(AllocPool* pool, IdentifierPool* identifiers) noexcept
{
	NameResolver* const resolver = static_cast<NameResolver*>(alloc(pool, sizeof(NameResolver), alignof(NameResolver)));

	resolver->identifiers = identifiers;

	return resolver;
}

a2::Node* resolve_names(NameResolver* resolver, a2::Node* root, ReservedVec<u32>* out) noexcept
{
	count_identifiers(root);

	create_static_namespaces(root, out);

	return nullptr;
}
