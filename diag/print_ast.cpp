#ifndef AST_FMT_INCLUDE_GUARD
#define AST_FMT_INCLUDE_GUARD

#include "inc.hpp"

void diag::print_ast(FILE* out, a2::Node* root) noexcept
{
	a2::NodePreorderIterator it = a2::preorder_ancestors_of(root);

	s32 prev_depth = -1;

	while (true)
	{
		a2::IterationResult result = next(&it);

		if (!a2::is_valid(result))
			break;

		while (prev_depth >= static_cast<s32>(result.depth))
		{
			fprintf(out, "%*s}\n", prev_depth * 2, "");

			prev_depth -= 1;
		}

		prev_depth = result.depth;

		fprintf(out, "%*s%s {%s\n", result.depth * 2, "", a2::tag_name(result.node->tag), a2::has_children(result.node) ? "" : "}");

		if (!a2::has_children(result.node))
			prev_depth -= 1;
	}

	while (prev_depth != -1)
	{
		fprintf(out, "%*s}\n", prev_depth * 2, "");

		prev_depth -= 1;
	}
}

#endif // AST_FMT_INCLUDE_GUARD
