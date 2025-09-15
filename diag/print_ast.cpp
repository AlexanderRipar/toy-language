#include "diag.hpp"

#include "../infra/minos/minos.hpp"
#include "../core/core.hpp"

static void print_node_header(diag::PrintContext* ctx, IdentifierPool* identifiers, const AstNode* node, s32 depth) noexcept
{
	diag::buf_printf(ctx, "%*s%s",
		(depth + 1) * 2, "",
		tag_name(node->tag)
	);

	if (node->tag == AstTag::Identifier)
	{
		const AstIdentifierData* const attach = attachment_of<AstIdentifierData>(node);

		const Range<char8> name = identifier_name_from_id(identifiers, attach->identifier_id);

		diag::buf_printf(ctx, " [%.*s | %c%u/%u]",
			static_cast<s32>(name.count()), name.begin(),
			attach->binding.is_global ? 'g' : attach->binding.is_closed_over_closure ? 'C' : attach->binding.is_closed_over ? 'c' : 's',
			attach->binding.out,
			attach->binding.rank
		);
	}
	else if (node->tag == AstTag::Definition || node->tag == AstTag::Parameter || node->tag == AstTag::Member)
	{
		IdentifierId identifier_id;

		if (node->tag == AstTag::Definition)
			identifier_id = attachment_of<AstDefinitionData>(node)->identifier_id;
		else if (node->tag == AstTag::Parameter)
			identifier_id = attachment_of<AstParameterData>(node)->identifier_id;
		else
			identifier_id = attachment_of<AstMemberData>(node)->identifier_id;

		const Range<char8> name = identifier_name_from_id(identifiers, identifier_id);

		diag::buf_printf(ctx, " [%.*s]",
			static_cast<s32>(name.count()), name.begin()
		);
	}
	else if (node->tag == AstTag::LitInteger)
	{
		s64 value;

		if (!s64_from_comp_integer(attachment_of<AstLitIntegerData>(node)->value, 64, &value))
			value = 0; // TODO: Print something nicer here.

		diag::buf_printf(ctx, " [%" PRId64 "]",
			value
		);
	}

	diag::buf_printf(ctx, " {%s\n",
		has_children(node) ? "" : "}"
	);
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
