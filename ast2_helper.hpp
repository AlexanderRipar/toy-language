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

			if (curr->tag == Tag::Definition && attachment_of<DefinitionData>(curr)->identifier_id.rep == id.rep)
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
}

#endif // AST2_HELPER_INCLUDE_GUARD
