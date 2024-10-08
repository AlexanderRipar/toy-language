#ifndef AST_FMT_INCLUDE_GUARD
#define AST_FMT_INCLUDE_GUARD

#include "ast_raw.hpp"

#include <cstdio>

namespace ast::raw
{
	namespace _impl
	{
		const Node* format_node(FILE* out, const Node* node, const IdentifierMap* identifiers, u32 nesting) noexcept
		{
			fprintf(out, "%*s%s", nesting * 2, "", type_name(node->type));

			if (node->type == ast::raw::Type::Definition)
			{
				const IdentifierMapEntry* entry = identifiers->value_from(*reinterpret_cast<const u32*>(node + 1));

				fprintf(out, " [%.*s]", entry->m_length, entry->m_chars);
			}
			else if (node->type == ast::raw::Type::ValIdentifer)
			{
				const IdentifierMapEntry* entry = identifiers->value_from(*reinterpret_cast<const u32*>(node + 1));
				
				fprintf(out, " [%.*s]", entry->m_length, entry->m_chars);
			}
			else if (node->type == ast::raw::Type::ValString)
			{
				const IdentifierMapEntry* entry = identifiers->value_from(*reinterpret_cast<const u32*>(node + 1));

				fprintf(out, " [\"%.*s\"]", entry->m_length, entry->m_chars);
			}
			else if (node->type == ast::raw::Type::ValChar)
			{
				u32 value = *reinterpret_cast<const u32*>(node + 1);

				if (value > 31 && value < 127)
					fprintf(out, " ['%c']", value);
				else
					fprintf(out, " [U+%06X]", value);
			}
			else if (node->type == ast::raw::Type::ValInteger)
			{
				u64 value;

				if (node->data_dwords == 0)
					value = node->flags;
				else if (node->data_dwords == 1)
					value = *reinterpret_cast<const u32*>(node + 1);
				else
					memcpy(&value, node + 1, sizeof(value));

				fprintf(out, " [%llu]", value);
			}
			else if (node->type == ast::raw::Type::ValFloat)
			{
				f64 value;

				memcpy(&value, node + 1, sizeof(value));

				fprintf(out, " [%f]", value);
			}

			if (node->child_count != 0)
				fprintf(stdout, " {\n");
			else
				fprintf(stdout, "\n");

			const Node* next = reinterpret_cast<const Node*>(reinterpret_cast<const u32*>(node) + 2 + node->data_dwords);

			for (u16 i = 0; i != node->child_count; ++i)
				next = format_node(out, next, identifiers, nesting + 1);

			if (node->child_count != 0)
				fprintf(out, "%*s}\n", nesting * 2, "");

			return next;
		}
	}

	void format(FILE* out, const Tree* tree, const IdentifierMap* identifiers) noexcept
	{
		_impl::format_node(out, tree->root(), identifiers, 0);
	}
}

#endif // AST_FMT_INCLUDE_GUARD
