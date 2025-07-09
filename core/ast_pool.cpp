#include "core.hpp"

#include "../infra/common.hpp"
#include "../infra/container.hpp"

struct AstPool
{
	ReservedVec<u32> pool;

	ReservedVec<u32> builder;
};

static AstNode* alloc_ast(AstPool* asts, u32 dwords) noexcept
{
	return static_cast<AstNode*>(asts->pool.reserve_exact(dwords * sizeof(u32)));
}

static AstNode* apply_offset_(AstNode* node, ureg offset) noexcept
{
	static_assert(sizeof(AstNode) % sizeof(u32) == 0 && alignof(AstNode) % sizeof(u32) == 0);

	return reinterpret_cast<AstNode*>(reinterpret_cast<u32*>(node) + offset);
}

// Set FLAG_FIRST_SIBLING and FLAG_LAST_SIBLING (note that next_sibling_offset
// holds the offset to the first child at this point):
//
//   for each node do
//     if next_sibling_offset != NO_CHILDREN then
//         direct predecessor gets FLAG_LAST_SIBLING
//         predecessor at next_sibling_offset gets FLAG_FIRST_SIBLING
static void set_flags(AstNode* begin, AstNode* end) noexcept
{
	ASSERT_OR_IGNORE(begin != end);

	AstNode* prev = nullptr;

	AstNode* curr = begin;

	while (curr != end)
	{
		AstNode* const next = apply_offset_(curr, curr->data_dwords);

		if (curr->next_sibling_offset != static_cast<u32>(AstBuilderToken::NO_CHILDREN))
		{
			ASSERT_OR_IGNORE(prev != nullptr);

			AstNode* const first_child = reinterpret_cast<AstNode*>(reinterpret_cast<u32*>(begin) + curr->next_sibling_offset);

			ASSERT_OR_IGNORE((first_child->flags & AstFlag::INTERNAL_FirstSibling) == AstFlag::EMPTY);

			first_child->flags |= AstFlag::INTERNAL_FirstSibling;

			ASSERT_OR_IGNORE((prev->flags & AstFlag::INTERNAL_LastSibling) == AstFlag::EMPTY);

			prev->flags |= AstFlag::INTERNAL_LastSibling;
		}

		prev = curr;

		curr = next;
	}

	ASSERT_OR_IGNORE((prev->flags & (AstFlag::INTERNAL_FirstSibling | AstFlag::INTERNAL_LastSibling)) == AstFlag::EMPTY);

	prev->flags |= AstFlag::INTERNAL_FirstSibling | AstFlag::INTERNAL_LastSibling;
}

// Create a linked list modelling a preorder traversal of all nodes.
static AstNode* build_traversal_list(AstNode* begin, AstNode* end) noexcept
{
	sreg depth = -1;

	u32 recursively_last_child = static_cast<u32>(AstBuilderToken::NO_CHILDREN);

	u32 prev_sibling_inds[MAX_AST_DEPTH];

	AstNode* curr = begin;

	while (true)
	{
		const u32 curr_ind = static_cast<u32>(reinterpret_cast<u32*>(curr) - reinterpret_cast<u32*>(begin));

		// Connect predecessor

		if ((curr->flags & AstFlag::INTERNAL_FirstSibling) == AstFlag::EMPTY)
		{
			ASSERT_OR_IGNORE(depth >= 0);

			const u32 prev_sibling_ind = prev_sibling_inds[depth];

			AstNode* prev_sibling = reinterpret_cast<AstNode*>(reinterpret_cast<u32*>(begin) + prev_sibling_ind);

			prev_sibling->next_sibling_offset = curr_ind;
		}

		// Push something

		if ((curr->flags & AstFlag::INTERNAL_LastSibling) == AstFlag::EMPTY)
		{
			if ((curr->flags & AstFlag::INTERNAL_FirstSibling) == AstFlag::INTERNAL_FirstSibling)
			{
				if (depth + 1 >= MAX_AST_DEPTH)
					panic("Maximum parse tree depth of %u exceeded.\n", MAX_AST_DEPTH);

				depth += 1;
			}

			ASSERT_OR_IGNORE(depth >= 0);

			if ((curr->flags & AstFlag::INTERNAL_NoChildren) == AstFlag::EMPTY)
			{
				ASSERT_OR_IGNORE(recursively_last_child != static_cast<u32>(AstBuilderToken::NO_CHILDREN));

				prev_sibling_inds[depth] = recursively_last_child;
			}
			else
			{
				prev_sibling_inds[depth] = curr_ind;
			}
		}
		else // last sibling
		{
			if ((curr->flags & AstFlag::INTERNAL_FirstSibling) == AstFlag::EMPTY)
			{
				ASSERT_OR_IGNORE(depth >= 0);

				depth -= 1;
			}

			if ((curr->flags & AstFlag::INTERNAL_NoChildren) == AstFlag::INTERNAL_NoChildren)
				recursively_last_child = curr_ind;
		}

		AstNode* const next = apply_offset_(curr, curr->data_dwords);

		if (next == end)
			break;

		curr = next;
	}

	ASSERT_OR_IGNORE(depth == -1);

	ASSERT_OR_IGNORE(reinterpret_cast<AstNode*>(reinterpret_cast<u32*>(curr) + curr->data_dwords) == end);

	return curr;
}

// Traverse the linked list created by build_traversal_list, pushing nodes into
// dst.
static AstNode* copy_postorder_to_preorder(const AstNode* begin, const AstNode* end, const AstNode* src_root, AstPool* dst) noexcept
{
	u32 prev_sibling_inds[MAX_AST_DEPTH];

	s32 depth = -1;

	const u32 end_ind = static_cast<u32>(reinterpret_cast<const u32*>(end) - reinterpret_cast<const u32*>(begin));

	AstNode* const dst_root = alloc_ast(dst, end_ind);

	AstNode* dst_curr = dst_root;

	const AstNode* src_curr = src_root;

	while (true)
	{
		// Copy node

		AstNode* const dst_node = dst_curr;

		dst_curr = apply_offset_(dst_curr, src_curr->data_dwords);

		memcpy(dst_node, src_curr, src_curr->data_dwords * sizeof(u32));

		const u32 curr_ind = static_cast<u32>(reinterpret_cast<u32*>(dst_node) - reinterpret_cast<u32*>(dst_root));

		if ((src_curr->flags & AstFlag::INTERNAL_FirstSibling) == AstFlag::EMPTY)
		{
			while (true)
			{
				ASSERT_OR_IGNORE(depth > 0); // Actually greater than and *not* equal to 0; root node should never be popped here

				const u32 prev_sibling_ind = prev_sibling_inds[depth];

				depth -= 1;

				AstNode* const prev_sibling = reinterpret_cast<AstNode*>(reinterpret_cast<u32*>(dst_root) + prev_sibling_ind);

				prev_sibling->next_sibling_offset = curr_ind - prev_sibling_ind;

				if ((prev_sibling->flags & AstFlag::INTERNAL_LastSibling) == AstFlag::EMPTY)
					break;
			}
		}

		ASSERT_OR_IGNORE(depth + 1 < MAX_AST_DEPTH);

		depth += 1;

		prev_sibling_inds[depth] = curr_ind;

		if (src_curr->next_sibling_offset == static_cast<u32>(AstBuilderToken::NO_CHILDREN))
			break;

		src_curr = reinterpret_cast<const AstNode*>(reinterpret_cast<const u32*>(begin) + src_curr->next_sibling_offset);
	}

	ASSERT_OR_IGNORE(depth != -1);

	while (depth >= 0)
	{
		const u32 prev_sibling_ind = prev_sibling_inds[depth];

		depth -= 1;

		AstNode* const prev_sibling = reinterpret_cast<AstNode*>(reinterpret_cast<u32*>(dst_root) + prev_sibling_ind);

		prev_sibling->next_sibling_offset = end_ind - prev_sibling_ind;
	}

	return dst_root;
}



AstPool* create_ast_pool(AllocPool* alloc) noexcept
{
	AstPool* const asts = static_cast<AstPool*>(alloc_from_pool(alloc, sizeof(AstPool), alignof(AstPool)));

	asts->pool.init(1u << 30, 1u << 18);

	asts->builder.init(1u << 31, 1u << 18);

	(void) asts->pool.reserve_exact(sizeof(*asts->pool.begin()));

	return asts;
}

void release_ast_pool(AstPool* asts) noexcept
{
	asts->pool.release();
}

AstNodeId id_from_ast_node(AstPool* asts, AstNode* node) noexcept
{
	return AstNodeId{ static_cast<u32>(reinterpret_cast<u32*>(node) - asts->pool.begin()) };
}

AstNode* ast_node_from_id(AstPool* asts, AstNodeId id) noexcept
{
	ASSERT_OR_IGNORE(id != AstNodeId::INVALID);

	return reinterpret_cast<AstNode*>(asts->pool.begin() + static_cast<u32>(id));
}



bool has_children(const AstNode* node) noexcept
{
	return (node->flags & AstFlag::INTERNAL_NoChildren) == AstFlag::EMPTY;
}

bool has_next_sibling(const AstNode* node) noexcept
{
	return (node->flags & AstFlag::INTERNAL_LastSibling) == AstFlag::EMPTY;
}

bool has_flag(const AstNode* node, AstFlag flag) noexcept
{
	return (static_cast<u16>(node->flags) & static_cast<u16>(flag)) != 0;
}

TypeKind type_kind_of(const AstNode* node) noexcept
{
	const TypeKind kind = static_cast<TypeKind>(static_cast<u16>(node->flags) >> 14);

	ASSERT_OR_IGNORE(kind != TypeKind::INVALID);

	return kind;
}

void set_type_kind(AstNode* node, TypeKind kind) noexcept
{
	ASSERT_OR_IGNORE(static_cast<TypeKind>(static_cast<u16>(node->flags) >> 14) == TypeKind::INVALID);

	node->flags |= static_cast<AstFlag>(static_cast<u16>(kind) << 14);
}

AstNode* next_sibling_of(AstNode* node) noexcept
{
	ASSERT_OR_IGNORE(has_next_sibling(node));

	return apply_offset_(node, node->next_sibling_offset);
}

AstNode* first_child_of(AstNode* node) noexcept
{
	ASSERT_OR_IGNORE(has_children(node));

	return apply_offset_(node, node->data_dwords);
}



AstBuilderToken push_node(AstPool* asts, AstBuilderToken first_child, SourceId source_id, AstFlag flags, AstTag tag) noexcept
{
	static_assert(sizeof(AstNode) % sizeof(u32) == 0);

	AstNode* const node = reinterpret_cast<AstNode*>(asts->builder.reserve_exact(sizeof(AstNode)));

	if (first_child == AstBuilderToken::NO_CHILDREN)
		flags |= AstFlag::INTERNAL_NoChildren;

	node->next_sibling_offset = static_cast<u32>(first_child);
	node->tag = tag;
	node->data_dwords = sizeof(AstNode) / sizeof(u32);
	node->flags = flags;
	node->type = DependentTypeId::INVALID;
	node->source_id = source_id;

	return AstBuilderToken{ static_cast<u32>(reinterpret_cast<u32*>(node) - asts->builder.begin()) };
}

AstBuilderToken push_node(AstPool* asts, AstBuilderToken first_child, SourceId source_id, AstFlag flags, AstTag tag, u8 attachment_dwords, const void* attachment) noexcept
{
	static_assert(sizeof(AstNode) % sizeof(u32) == 0);

	const u8 required_dwords = static_cast<u8>(sizeof(AstNode) / sizeof(u32) + attachment_dwords);

	AstNode* const node = reinterpret_cast<AstNode*>(asts->builder.reserve_exact(required_dwords * sizeof(u32)));

	if (first_child == AstBuilderToken::NO_CHILDREN)
		flags |= AstFlag::INTERNAL_NoChildren;

	node->next_sibling_offset = static_cast<u32>(first_child);
	node->tag = tag;
	node->data_dwords = required_dwords;
	node->flags = flags;
	node->type = DependentTypeId::INVALID;
	node->source_id = source_id;

	memcpy(node + 1, attachment, attachment_dwords * sizeof(u32));

	return AstBuilderToken{ static_cast<u32>(reinterpret_cast<u32*>(node) - asts->builder.begin()) };
}

AstNode* complete_ast(AstPool* asts) noexcept
{
	AstNode* const begin = reinterpret_cast<AstNode*>(asts->builder.begin());

	AstNode* const end = reinterpret_cast<AstNode*>(asts->builder.end());

	set_flags(begin, end);

	AstNode* const src_root = build_traversal_list(begin, end);

	AstNode* const dst_root = copy_postorder_to_preorder(begin, end, src_root, asts);

	asts->builder.reset(1 << 20);

	return dst_root;
}



AstDirectChildIterator direct_children_of(AstNode* node) noexcept
{
	return { has_children(node) ? first_child_of(node) : nullptr };
}

AstNode* next(AstDirectChildIterator* iterator) noexcept
{
	ASSERT_OR_IGNORE(iterator->curr != nullptr);

	AstNode* const result = iterator->curr;

	iterator->curr = has_next_sibling(result) ? next_sibling_of(result) : nullptr;
	
	return result;
}

bool has_next(const AstDirectChildIterator* iterator) noexcept
{
	return iterator->curr != nullptr;
}


AstPreorderIterator preorder_ancestors_of(AstNode* node) noexcept
{
	AstPreorderIterator iterator;

	if (has_children(node))
	{
		iterator.curr = first_child_of(node);
		iterator.depth = 0;
		iterator.top = -1;
	}
	else
	{
		iterator.curr = nullptr;
		iterator.depth = 0;
		iterator.top = -1;
	}

	return iterator;
}

AstIterationResult next(AstPreorderIterator* iterator) noexcept
{
	ASSERT_OR_IGNORE(iterator->curr != nullptr);

	const AstIterationResult result = { iterator->curr, iterator->depth };

	AstNode* const curr = iterator->curr;

	iterator->curr = apply_offset_(curr, curr->data_dwords);

	if ((curr->flags & AstFlag::INTERNAL_NoChildren) == AstFlag::EMPTY)
	{
		if ((curr->flags & AstFlag::INTERNAL_LastSibling) == AstFlag::EMPTY)
		{
			ASSERT_OR_IGNORE(iterator->top + 1 < MAX_AST_DEPTH);

			iterator->top += 1;

			iterator->prev_depths[iterator->top] = iterator->depth;
		}

		ASSERT_OR_IGNORE(iterator->depth + 1 < MAX_AST_DEPTH);

		iterator->depth += 1;
	}
	else if ((curr->flags & AstFlag::INTERNAL_LastSibling) == AstFlag::INTERNAL_LastSibling)
	{
		if (iterator->top == -1)
		{
			iterator->curr = nullptr;
		}
		else
		{
			iterator->depth = iterator->prev_depths[iterator->top];

			iterator->top -= 1;
		}
	}

	return result;
}

bool has_next(const AstPreorderIterator* iterator) noexcept
{
	return iterator->curr != nullptr;
}


AstPostorderIterator postorder_ancestors_of(AstNode* node) noexcept
{
	AstPostorderIterator iterator;

	iterator.base = node;
	iterator.depth = -1;

	while (has_children(node))
	{
		ASSERT_OR_IGNORE(iterator.depth < MAX_AST_DEPTH);

		node = first_child_of(node);

		iterator.depth += 1;

		iterator.offsets[iterator.depth] = static_cast<u32>(reinterpret_cast<u32*>(node) - reinterpret_cast<u32*>(iterator.base));
	}

	return iterator;
}

AstIterationResult next(AstPostorderIterator* iterator) noexcept
{
	ASSERT_OR_IGNORE(iterator->depth >= 0);

	AstNode* const ret_node = reinterpret_cast<AstNode*>(reinterpret_cast<u32*>(iterator->base) + iterator->offsets[iterator->depth]);

	const u32 ret_depth = static_cast<u32>(iterator->depth);

	AstNode* curr = ret_node;

	if (has_next_sibling(curr))
	{
		curr = next_sibling_of(curr);

		iterator->offsets[iterator->depth] = static_cast<u32>(reinterpret_cast<u32*>(curr) - reinterpret_cast<u32*>(iterator->base));

		while (has_children(curr))
		{
			curr = first_child_of(curr);

			iterator->depth += 1;

			ASSERT_OR_IGNORE(iterator->depth < MAX_AST_DEPTH);

			iterator->offsets[iterator->depth] = static_cast<u32>(reinterpret_cast<u32*>(curr) - reinterpret_cast<u32*>(iterator->base));
		}
	}
	else
	{
		iterator->depth -= 1;

		if (iterator->depth >= 0)
			curr = reinterpret_cast<AstNode*>(reinterpret_cast<u32*>(iterator->base) + iterator->offsets[iterator->depth]);
	}

	return { ret_node, ret_depth };
}

bool has_next(const AstPostorderIterator* iterator) noexcept
{
	return iterator->depth >= 0;
}



FuncInfo get_func_info(AstNode* func) noexcept
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

DefinitionInfo get_definition_info(AstNode* definition) noexcept
{
	ASSERT_OR_IGNORE(definition->tag == AstTag::Definition || definition->tag == AstTag::Parameter);

	if (!has_children(definition))
		return {};

	if (has_flag(definition, AstFlag::Definition_HasType))
	{
		AstNode* const type = first_child_of(definition);

		return { some(type), has_next_sibling(type) ? some(next_sibling_of(type)) : none<AstNode>() };
	}

	return { none<AstNode>(), some(first_child_of(definition)) };
}

IfInfo get_if_info(AstNode* node) noexcept
{
	ASSERT_OR_IGNORE(node->tag == AstTag::If);

	AstNode* curr = first_child_of(node);

	IfInfo info{};

	info.condition = curr;

	if (has_flag(node, AstFlag::If_HasWhere))
	{
		curr = next_sibling_of(curr);

		info.where = some(curr);
	}

	curr = next_sibling_of(curr);

	info.consequent = curr;

	if (has_flag(node, AstFlag::If_HasElse))
	{
		curr = next_sibling_of(curr);

		info.alternative = some(curr);
	}

	ASSERT_OR_IGNORE(!has_next_sibling(curr));

	return info;
}

ForInfo get_for_info(AstNode* node) noexcept
{
	ASSERT_OR_IGNORE(node->tag == AstTag::If);

	AstNode* curr = first_child_of(node);

	ForInfo info{};

	info.condition = curr;

	if (has_flag(node, AstFlag::For_HasStep))
	{
		info.step = some(curr);

		curr = next_sibling_of(curr);
	}

	if (has_flag(node, AstFlag::For_HasWhere))
	{
		info.where = some(curr);

		curr = next_sibling_of(curr);
	}

	info.body = curr;

	if (has_flag(node, AstFlag::For_HasFinally))
	{
		curr = next_sibling_of(curr);

		info.finally = some(curr);
	}

	ASSERT_OR_IGNORE(!has_next_sibling(curr));

	return info;
}

ForEachInfo get_foreach_info(AstNode* node) noexcept
{
	ASSERT_OR_IGNORE(node->tag == AstTag::ForEach);

	AstNode* curr = first_child_of(node);

	ForEachInfo info{};

	info.element = curr;

	curr = next_sibling_of(curr);

	if (has_flag(node, AstFlag::ForEach_HasIndex))
	{
		info.index = some(curr);

		curr = next_sibling_of(curr);
	}

	info.iterated = curr;

	curr = next_sibling_of(curr);

	if (has_flag(node, AstFlag::ForEach_HasWhere))
	{
		info.where = some(curr);

		curr = next_sibling_of(curr);
	}

	info.body = curr;

	if (has_flag(node, AstFlag::ForEach_HasFinally))
	{
		curr = next_sibling_of(curr);

		info.finally = some(curr);
	}

	ASSERT_OR_IGNORE(!has_next_sibling(curr));

	return info;
}



const char8* tag_name(AstTag tag) noexcept
{
	static constexpr const char8* AST_TAG_NAMES[] = {
		"[unknown]",
		"Builtin",
		"File",
		"CompositeInitializer",
		"ArrayInitializer",
		"Wildcard",
		"Where",
		"Expects",
		"Ensures",
		"Definition",
		"Parameter",
		"Block",
		"If",
		"For",
		"ForEach",
		"Switch",
		"Case",
		"Func",
		"Trait",
		"Impl",
		"Catch",
		"Identifier",
		"LitInteger",
		"LitFloat",
		"LitChar",
		"LitString",
		"Return",
		"Leave",
		"Yield",
		"ParameterList",
		"Call",
		"UOpTypeTailArray",
		"UOpTypeSlice",
		"UOpTypeMultiPtr",
		"UOpTypeOptMultiPtr",
		"UOpEval",
		"UOpTry",
		"UOpDefer",
		"UOpDistinct",
		"UOpAddr",
		"UOpDeref",
		"UOpBitNot",
		"UOpLogNot",
		"UOpTypeOptPtr",
		"UOpTypeVar",
		"UOpImpliedMember",
		"UOpTypePtr",
		"UOpNegate",
		"UOpPos",
		"OpAdd",
		"OpSub",
		"OpMul",
		"OpDiv",
		"OpAddTC",
		"OpSubTC",
		"OpMulTC",
		"OpMod",
		"OpBitAnd",
		"OpBitOr",
		"OpBitXor",
		"OpShiftL",
		"OpShiftR",
		"OpLogAnd",
		"OpLogOr",
		"OpMember",
		"OpCmpLT",
		"OpCmpGT",
		"OpCmpLE",
		"OpCmpGE",
		"OpCmpNE",
		"OpCmpEQ",
		"OpSet",
		"OpSetAdd",
		"OpSetSub",
		"OpSetMul",
		"OpSetDiv",
		"OpSetAddTC",
		"OpSetSubTC",
		"OpSetMulTC",
		"OpSetMod",
		"OpSetBitAnd",
		"OpSetBitOr",
		"OpSetBitXor",
		"OpSetShiftL",
		"OpSetShiftR",
		"OpTypeArray",
		"OpArrayIndex",
	};

	u8 ordinal = static_cast<u8>(tag);

	if (ordinal >= array_count(AST_TAG_NAMES))
		ordinal = 0;

	return AST_TAG_NAMES[ordinal];
}
