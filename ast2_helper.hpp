#ifndef AST2_HELPER_INCLUDE_GUARD
#define AST2_HELPER_INCLUDE_GUARD

#include "infra/common.hpp"
#include "ast2.hpp"
#include "ast2_attach.hpp"
#include "pass_data.hpp"

namespace a2
{
	static inline Node* last_child_of(Node* node) noexcept
	{
		ASSERT_OR_IGNORE(has_children(node));

		Node* curr = first_child_of(node);
		
		while (has_next_sibling(curr))
			curr = next_sibling_of(curr);

		return curr;
	}



	struct FuncInfo
	{
		a2::Node* parameters;

		OptPtr<a2::Node> return_type;

		OptPtr<a2::Node> expects;

		OptPtr<a2::Node> ensures;

		OptPtr<a2::Node> body;
	};

	inline FuncInfo func_info(Node* func) noexcept
	{
		ASSERT_OR_IGNORE(func->tag == Tag::Func);

		ASSERT_OR_IGNORE(has_children(func));

		Node* curr = first_child_of(func);

		ASSERT_OR_IGNORE(curr->tag == Tag::ParameterList);

		FuncInfo desc{};

		desc.parameters = curr;

		if (has_flag(func, Flag::Func_HasReturnType))
		{
			curr = next_sibling_of(curr);

			desc.return_type = some(curr);
		}

		if (has_flag(func, Flag::Func_HasExpects))
		{
			curr = next_sibling_of(curr);

			ASSERT_OR_IGNORE(curr->tag == Tag::Expects);

			desc.expects = some(curr);
		}

		if (has_flag(func, Flag::Func_HasEnsures))
		{
			curr = next_sibling_of(curr);

			ASSERT_OR_IGNORE(curr->tag == Tag::Ensures);

			desc.ensures = some(curr);
		}

		if (has_flag(func, Flag::Func_HasBody))
		{
			curr = next_sibling_of(curr);

			desc.body = some(curr);
		}

		return desc;
	}



	struct DefinitionInfo
	{
		OptPtr<a2::Node> type;

		OptPtr<a2::Node> value;
	};

	inline DefinitionInfo definition_info(Node* definition) noexcept
	{
		ASSERT_OR_IGNORE(definition->tag == Tag::Definition);

		if (!has_children(definition))
			return {};

		if (has_flag(definition, Flag::Definition_HasType))
		{
			Node* const type = first_child_of(definition);

			return { some(type), has_next_sibling(type) ? some(next_sibling_of(type)) : none<Node>() };
		}

		return { none<Node>(), some(first_child_of(definition)) };
	}

	struct IfInfo
	{
		Node* condition;

		Node* consequent;

		OptPtr<Node> alternative;

		OptPtr<Node> where;
	};

	inline IfInfo if_info(Node* if_node) noexcept
	{
		ASSERT_OR_IGNORE(if_node->tag == Tag::If);

		Node* curr = first_child_of(if_node);

		IfInfo info{};

		info.condition = curr;

		if (has_flag(if_node, Flag::If_HasWhere))
		{
			curr = next_sibling_of(curr);

			info.where = some(curr);
		}

		curr = next_sibling_of(curr);

		info.consequent = curr;

		if (has_flag(if_node, Flag::If_HasElse))
		{
			curr = next_sibling_of(curr);

			info.alternative = some(curr);
		}

		ASSERT_OR_IGNORE(!has_next_sibling(curr));

		return info;
	}
}

#endif // AST2_HELPER_INCLUDE_GUARD
