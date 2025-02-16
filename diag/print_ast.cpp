#include "diag.hpp"

#include "../pass_data.hpp"
#include "../ast2_attach.hpp"

static void print_node_header(FILE* out, IdentifierPool* identifiers, const a2::AstNode* node, s32 depth) noexcept
{
	if (node->tag == a2::AstTag::ValIdentifer)
	{
		const Range<char8> name = identifier_entry_from_id(identifiers, a2::attachment_of<a2::ValIdentifierData>(node)->identifier_id)->range();

		fprintf(out, "%*s%s [%.*s] {%s\n", (depth + 1) * 2, "", a2::tag_name(node->tag), static_cast<s32>(name.count()), name.begin(), a2::has_children(node) ? "" : "}");
	}
	else if (node->tag == a2::AstTag::ValInteger)
	{
		fprintf(out, "%*s%s [%lld] {%s\n", (depth + 1) * 2, "", a2::tag_name(node->tag), a2::attachment_of<a2::ValIntegerData>(node)->value, a2::has_children(node) ? "" : "}");
	}
	else
	{
		fprintf(out, "%*s%s {%s\n", (depth + 1) * 2, "", a2::tag_name(node->tag), a2::has_children(node) ? "" : "}");
	}
}

void diag::print_ast(FILE* out, IdentifierPool* identifiers, a2::AstNode* root) noexcept
{
	a2::PreorderIterator it = a2::preorder_ancestors_of(root);

	s32 prev_depth = -1;

	print_node_header(out, identifiers, root, -1);

	while (true)
	{
		a2::IterationResult result = next(&it);

		if (!a2::is_valid(result))
			break;

		while (prev_depth >= static_cast<s32>(result.depth))
		{
			fprintf(out, "%*s}\n", (prev_depth + 1) * 2, "");

			prev_depth -= 1;
		}

		prev_depth = result.depth;

		print_node_header(out, identifiers, result.node, result.depth);

		if (!a2::has_children(result.node))
			prev_depth -= 1;
	}

	while (prev_depth != -1)
	{
		fprintf(out, "%*s}\n", (prev_depth + 1) * 2, "");

		prev_depth -= 1;
	}

	fprintf(out, "}\n");
}
