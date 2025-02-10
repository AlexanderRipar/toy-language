#ifndef AST2_HELPER_INCLUDE_GUARD
#define AST2_HELPER_INCLUDE_GUARD

#include "infra/common.hpp"
#include "ast2.hpp"
#include "ast2_attach.hpp"
#include "pass_data.hpp"

namespace a2
{
	static inline OptPtr<Node> try_find_definition(a2::Node* node, IdentifierId id) noexcept
	{
		DirectChildIterator it = direct_children_of(node);

		for (OptPtr<Node> rst = next(&it); is_some(rst); rst = next(&it))
		{
			Node* const curr = get_ptr(rst);

			if (curr->tag == Tag::Definition && attachment_of<DefinitionData>(curr)->identifier_id == id)
				return some(curr);
		}

		return none<Node>();
	}

	static inline Node* find_definition(a2::Node* node, IdentifierId id) noexcept
	{
		const OptPtr<Node> definition = try_find_definition(node, id);

		if (is_some(definition))
			return get_ptr(definition);

		panic("Could not find definition\n");
	}

	static inline Node* last_child_of(Node* node) noexcept
	{
		ASSERT_OR_IGNORE(has_children(node));

		Node* curr = first_child_of(node);
		
		while (has_next_sibling(curr))
			curr = next_sibling_of(curr);

		return curr;
	}

	static inline OptPtr<Node> get_definition_body(Node* definition) noexcept
	{
		ASSERT_OR_IGNORE(definition->tag == Tag::Definition);

		if (!has_children(definition))
			return none<Node>();

		Node* const curr = first_child_of(definition);

		if (!has_flag(definition, Flag::Definition_HasType))
			return some(curr);

		return has_next_sibling(curr) ? some(next_sibling_of(curr)) : none<Node>();
	}
}

#endif // AST2_HELPER_INCLUDE_GUARD
