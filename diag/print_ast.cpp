#ifndef AST_FMT_INCLUDE_GUARD
#define AST_FMT_INCLUDE_GUARD

#include "inc.hpp"

static void format_node(FILE* out, const ast::Node* node, const Globals* data, u32 nesting) noexcept
{
	fprintf(out, "%*s%s", nesting * 2, "", ast::tag_name(node->tag));

	if (node->tag == ast::Tag::Definition)
	{
		const ast::data::Definition* const attach = node->data<ast::data::Definition>();

		const Range<char8> identifier = data->identifiers.value_from(attach->identifier_index)->range();

		fprintf(out, " [%.*s]", static_cast<u32>(identifier.count()), identifier.begin());
	}
	else if (node->tag == ast::Tag::ValIdentifer)
	{
		const ast::data::ValIdentifier* const attach = node->data<ast::data::ValIdentifier>();

		const Range<char8> identifier = data->identifiers.value_from(attach->identifier_index)->range();

		fprintf(out, " [%.*s]", static_cast<u32>(identifier.count()), identifier.begin());
	}
	else if (node->tag == ast::Tag::ValString)
	{
		const ast::data::ValString* const attach = node->data<ast::data::ValString>();

		const Range<char8> identifier = data->identifiers.value_from(attach->string_index)->range();

		fprintf(out, " [\"%.*s\"]", static_cast<u32>(identifier.count()), identifier.begin());
	}
	else if (node->tag == ast::Tag::ValChar)
	{
		const ast::data::ValChar* const attach = node->data<ast::data::ValChar>();

		if (attach->codepoint > 31 && attach->codepoint < 127)
			fprintf(out, " ['%c']", attach->codepoint);
		else
			fprintf(out, " [U+%06X]", attach->codepoint);
	}
	else if (node->tag == ast::Tag::ValInteger)
	{
		const ast::data::ValInteger* const attach = node->data<ast::data::ValInteger>();

		fprintf(out, " [%llu]", attach->get());
	}
	else if (node->tag == ast::Tag::ValFloat)
	{
		const ast::data::ValFloat* const attach = node->data<ast::data::ValFloat>();

		fprintf(out, " [%f]", attach->get());
	}

	fprintf(out, "%s\n", node->child_count == 0 ? "" : " {");

	const ast::Node* next = node->first_child();

	for (u16 i = 0; i != node->child_count; ++i)
	{
		format_node(out, next, data, nesting + 1);

		next = next->next_sibling();
	}

	if (node->child_count != 0)
		fprintf(out, "%*s}\n", nesting * 2, "");
}

void diag::print_ast(FILE* out, const ast::Tree* tree, const Globals* data) noexcept
{
	format_node(out, tree->root(), data, 0);
}

#endif // AST_FMT_INCLUDE_GUARD
