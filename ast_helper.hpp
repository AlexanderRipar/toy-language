#ifndef AST2_HELPER_INCLUDE_GUARD
#define AST2_HELPER_INCLUDE_GUARD

#include "infra/common.hpp"
#include "ast.hpp"
#include "ast_attach.hpp"
#include "pass_data.hpp"

static inline AstNode* last_child_of(AstNode* node) noexcept
{
	ASSERT_OR_IGNORE(has_children(node));

	AstNode* curr = first_child_of(node);
	
	while (has_next_sibling(curr))
		curr = next_sibling_of(curr);

	return curr;
}



struct FuncInfo
{
	AstNode* parameters;

	OptPtr<AstNode> return_type;

	OptPtr<AstNode> expects;

	OptPtr<AstNode> ensures;

	OptPtr<AstNode> body;
};

inline FuncInfo get_func_info(AstNode* func) noexcept
{
	ASSERT_OR_IGNORE(func->tag == AstTag::Func);

	ASSERT_OR_IGNORE(has_children(func));

	AstNode* curr = first_child_of(func);

	ASSERT_OR_IGNORE(curr->tag == AstTag::ParameterList);

	FuncInfo desc{};

	desc.parameters = curr;

	if (has_flag(func, AstFlag::Func_HasReturnType))
	{
		curr = next_sibling_of(curr);

		desc.return_type = some(curr);
	}

	if (has_flag(func, AstFlag::Func_HasExpects))
	{
		curr = next_sibling_of(curr);

		ASSERT_OR_IGNORE(curr->tag == AstTag::Expects);

		desc.expects = some(curr);
	}

	if (has_flag(func, AstFlag::Func_HasEnsures))
	{
		curr = next_sibling_of(curr);

		ASSERT_OR_IGNORE(curr->tag == AstTag::Ensures);

		desc.ensures = some(curr);
	}

	if (has_flag(func, AstFlag::Func_HasBody))
	{
		curr = next_sibling_of(curr);

		desc.body = some(curr);
	}

	return desc;
}



struct DefinitionInfo
{
	OptPtr<AstNode> type;

	OptPtr<AstNode> value;
};

inline DefinitionInfo get_definition_info(AstNode* definition) noexcept
{
	ASSERT_OR_IGNORE(definition->tag == AstTag::Definition);

	if (!has_children(definition))
		return {};

	if (has_flag(definition, AstFlag::Definition_HasType))
	{
		AstNode* const type = first_child_of(definition);

		return { some(type), has_next_sibling(type) ? some(next_sibling_of(type)) : none<AstNode>() };
	}

	return { none<AstNode>(), some(first_child_of(definition)) };
}

struct IfInfo
{
	AstNode* condition;

	AstNode* consequent;

	OptPtr<AstNode> alternative;

	OptPtr<AstNode> where;
};

inline IfInfo get_if_info(AstNode* if_node) noexcept
{
	ASSERT_OR_IGNORE(if_node->tag == AstTag::If);

	AstNode* curr = first_child_of(if_node);

	IfInfo info{};

	info.condition = curr;

	if (has_flag(if_node, AstFlag::If_HasWhere))
	{
		curr = next_sibling_of(curr);

		info.where = some(curr);
	}

	curr = next_sibling_of(curr);

	info.consequent = curr;

	if (has_flag(if_node, AstFlag::If_HasElse))
	{
		curr = next_sibling_of(curr);

		info.alternative = some(curr);
	}

	ASSERT_OR_IGNORE(!has_next_sibling(curr));

	return info;
}

#endif // AST2_HELPER_INCLUDE_GUARD
