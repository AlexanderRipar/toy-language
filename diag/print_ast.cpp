#include "diag.hpp"

#include "../infra/minos.hpp"
#include "../core/core.hpp"

static void print_node_header(diag::PrintContext* ctx, IdentifierPool* identifiers, const AstNode* node, s32 depth) noexcept
{
	if (node->tag == AstTag::Identifer || node->tag == AstTag::Definition)
	{
		const IdentifierId identifier_id = node->tag == AstTag::Identifer
			? attachment_of<AstIdentifierData>(node)->identifier_id
			: attachment_of<AstDefinitionData>(node)->identifier_id;

		const Range<char8> name = identifier_name_from_id(identifiers, identifier_id);

		diag::buf_printf(ctx, "%*s%s [%.*s] {%s\n", (depth + 1) * 2, "", tag_name(node->tag), static_cast<s32>(name.count()), name.begin(), has_children(node) ? "" : "}");
	}
	else if (node->tag == AstTag::LitInteger)
	{
		s64 value;

		if (!s64_from_comp_integer(attachment_of<AstLitIntegerData>(node)->value, 64, &value))
			value = 0; // TODO: Print something nicer here.

		diag::buf_printf(ctx, "%*s%s [%" PRId64 "] {%s\n", (depth + 1) * 2, "", tag_name(node->tag), value, has_children(node) ? "" : "}");
	}
	else
	{
		diag::buf_printf(ctx, "%*s%s {%s\n", (depth + 1) * 2, "", tag_name(node->tag), has_children(node) ? "" : "}");
	}
}

void diag::print_ast(minos::FileHandle out, IdentifierPool* identifiers, AstNode* root, Range<char8> source) noexcept
{
	PrintContext ctx;
	ctx.file = out;
	ctx.curr = ctx.buf;

	buf_printf(&ctx, "\n#### AST [%.*s] ####\n\n", static_cast<s32>(source.count()), source.begin());

	AstPreorderIterator it = preorder_ancestors_of(root);

	s32 prev_depth = -1;

	print_node_header(&ctx, identifiers, root, -1);

	while (has_next(&it))
	{
		const AstIterationResult result = next(&it);

		while (prev_depth >= static_cast<s32>(result.depth))
		{
			buf_printf(&ctx, "%*s}\n", (prev_depth + 1) * 2, "");

			prev_depth -= 1;
		}

		prev_depth = result.depth;

		print_node_header(&ctx, identifiers, result.node, result.depth);

		if (!has_children(result.node))
			prev_depth -= 1;
	}

	while (prev_depth != -1)
	{
		buf_printf(&ctx, "%*s}\n", (prev_depth + 1) * 2, "");

		prev_depth -= 1;
	}

	buf_printf(&ctx, "}\n");

	buf_flush(&ctx);
}
