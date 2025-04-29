#include "diag.hpp"

#include "../infra/minos.hpp"
#include "../core/pass_data.hpp"

static void print_node_header(diag::PrintContext* ctx, IdentifierPool* identifiers, const AstNode* node, s32 depth) noexcept
{
	if (node->tag == AstTag::Identifer)
	{
		const Range<char8> name = identifier_name_from_id(identifiers, attachment_of<AstIdentifierData>(node)->identifier_id);

		diag::buf_printf(ctx, "%*s%s [%.*s] {%s\n", (depth + 1) * 2, "", tag_name(node->tag), static_cast<s32>(name.count()), name.begin(), has_children(node) ? "" : "}");
	}
	else if (node->tag == AstTag::LitInteger)
	{
		diag::buf_printf(ctx, "%*s%s [%" PRId64 "] {%s\n", (depth + 1) * 2, "", tag_name(node->tag), attachment_of<AstLitIntegerData>(node)->value, has_children(node) ? "" : "}");
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

	while (true)
	{
		AstIterationResult result = next(&it);

		if (!is_valid(result))
			break;

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
