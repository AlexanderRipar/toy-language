#include "diag.hpp"

#include "../pass_data.hpp"
#include "../ast_attach.hpp"

static void print_node_header(FILE* out, IdentifierPool* identifiers, const AstNode* node, s32 depth) noexcept
{
	if (node->tag == AstTag::ValIdentifer)
	{
		const Range<char8> name = identifier_entry_from_id(identifiers, attachment_of<ValIdentifierData>(node)->identifier_id)->range();

		fprintf(out, "%*s%s [%.*s] {%s\n", (depth + 1) * 2, "", ast_tag_name(node->tag), static_cast<s32>(name.count()), name.begin(), has_children(node) ? "" : "}");
	}
	else if (node->tag == AstTag::ValInteger)
	{
		fprintf(out, "%*s%s [%lld] {%s\n", (depth + 1) * 2, "", ast_tag_name(node->tag), attachment_of<ValIntegerData>(node)->value, has_children(node) ? "" : "}");
	}
	else
	{
		fprintf(out, "%*s%s {%s\n", (depth + 1) * 2, "", ast_tag_name(node->tag), has_children(node) ? "" : "}");
	}
}

void diag::print_ast(FILE* out, IdentifierPool* identifiers, AstNode* root) noexcept
{
	AstPreorderIterator it = preorder_ancestors_of(root);

	s32 prev_depth = -1;

	print_node_header(out, identifiers, root, -1);

	while (true)
	{
		AstIterationResult result = next(&it);

		if (!is_valid(result))
			break;

		while (prev_depth >= static_cast<s32>(result.depth))
		{
			fprintf(out, "%*s}\n", (prev_depth + 1) * 2, "");

			prev_depth -= 1;
		}

		prev_depth = result.depth;

		print_node_header(out, identifiers, result.node, result.depth);

		if (!has_children(result.node))
			prev_depth -= 1;
	}

	while (prev_depth != -1)
	{
		fprintf(out, "%*s}\n", (prev_depth + 1) * 2, "");

		prev_depth -= 1;
	}

	fprintf(out, "}\n");
}
