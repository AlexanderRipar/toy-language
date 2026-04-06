#include "diag.hpp"

#include "../infra/types.hpp"
#include "../infra/range.hpp"
#include "../infra/minos/minos.hpp"
#include "../core/core.hpp"

static s64 print_node_header(PrintSink sink, CoreData* core, const AstNode* node, s32 depth) noexcept
{
	s64 total_written = print(sink, "%[< %]%", "", (depth + 1) * 2, tag_name(node->tag));

	if (total_written < 0)
		return -1;

	if (node->tag == AstTag::Identifier)
	{
		const AstIdentifierData* const attach = attachment_of<AstIdentifierData>(node);

		if (attach->identifier_id < IdentifierId::FirstNatural)
		{
			const s64 written = print(sink, " [_%] |", static_cast<u32>(attach->identifier_id));

			if (written < 0)
				return -1;

			total_written += written;
		}
		else
		{
			const Range<char8> name = identifier_name_from_id(core, attach->identifier_id);

			const s64 written = print(sink, " [% |", name);

			if (written < 0)
				return -1;

			total_written += written;
		}

		s64 written;

		if (attach->binding.kind == NameBindingKind::Global)
			written = print(sink, " g%@%", attach->binding.global.rank, static_cast<u32>(attach->binding.global.file_id));
		else if (attach->binding.kind == NameBindingKind::Scoped)
			written = print(sink, " s%@%", attach->binding.scoped.rank, attach->binding.scoped.out);
		else if (attach->binding.kind == NameBindingKind::Closed)
			written = print(sink, " c%", attach->binding.closed.rank_in_closure);
		else
			ASSERT_UNREACHABLE;

		if (written < 0)
			return -1;

		total_written += written;
	}
	else if (node->tag == AstTag::Definition || node->tag == AstTag::Parameter || node->tag == AstTag::Member || node->tag == AstTag::ImpliedMember)
	{
		IdentifierId identifier_id;

		if (node->tag == AstTag::Definition)
			identifier_id = attachment_of<AstDefinitionData>(node)->identifier_id;
		else if (node->tag == AstTag::Parameter)
			identifier_id = attachment_of<AstParameterData>(node)->identifier_id;
		else if (node->tag == AstTag::Member)
			identifier_id = attachment_of<AstMemberData>(node)->identifier_id;
		else
			identifier_id = attachment_of<AstImpliedMemberData>(node)->identifier_id;

		s64 written;

		if (identifier_id < IdentifierId::FirstNatural)
		{
			written = print(sink, " [_%]", static_cast<u32>(identifier_id));
		}
		else
		{
			const Range<char8> name = identifier_name_from_id(core, identifier_id);

			written = print(sink, " [%]", name);
		}

		if (written < 0)
			return -1;

		total_written += written;
	}
	else if (node->tag == AstTag::LitInteger)
	{
		s64 value;

		if (!s64_from_comp_integer(attachment_of<AstLitIntegerData>(node)->value, 64, &value))
			value = 0; // TODO: Print something nicer here.

		const s64 written = print(sink, " [%]", value);

		if (written < 0)
			return -1;

		total_written += written;
	}

	const s64 written = print(sink, " {%\n", has_children(node) ? "" : "}");

	if (written < 0)
		return -1;

	total_written += written;

	return total_written;
}

s64 diag::print_ast(PrintSink sink, CoreData* core, AstNode* root) noexcept
{
	AstPreorderIterator it = preorder_ancestors_of(root);

	s32 prev_depth = -1;

	s64 total_written = print_node_header(sink, core, root, -1);

	if (total_written < 0)
		return -1;

	while (has_next(&it))
	{
		const AstIterationResult result = next(&it);

		while (prev_depth >= static_cast<s32>(result.depth))
		{
			const s64 written = print(sink, "%[< %]}\n", "", (prev_depth + 1) * 2);

			if (written < 0)
				return -1;

			total_written += written;

			prev_depth -= 1;
		}

		prev_depth = result.depth;

		const s64 written = print_node_header(sink, core, result.node, result.depth);

		if (written < 0)
			return -1;

		total_written += written;

		if (!has_children(result.node))
			prev_depth -= 1;
	}

	while (prev_depth != -1)
	{
		const s64 written = print(sink, "%[< %]}\n", "", (prev_depth + 1) * 2);

		if (written < 0)
			return -1;

		total_written += written;

		prev_depth -= 1;
	}

	const s64 written = print(sink, "}\n\n");

	if (written < 0)
		return -1;

	total_written += written;

	return total_written;
}
