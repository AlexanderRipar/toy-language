#include "ast2.hpp"

#include "pass_data.hpp"

constexpr inline const char8* const NODE_TYPE_NAMES[] = {
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
	"ValIdentifier",
	"ValInteger",
	"ValFloat",
	"ValChar",
	"ValString",
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

// Set FLAG_FIRST_SIBLING and FLAG_LAST_SIBLING (note that next_sibling_offset
// holds the offset to the first child at this point):
//
//   for each node do
//     if next_sibling_offset != NO_CHILDREN then
//         direct predecessor gets FLAG_LAST_SIBLING
//         predecessor at next_sibling_offset gets FLAG_FIRST_SIBLING
static void set_internal_flags(a2::AstNode* begin, a2::AstNode* end) noexcept
{
	ASSERT_OR_IGNORE(begin != end);

	a2::AstNode* prev = nullptr;

	a2::AstNode* curr = begin;

	while (curr != end)
	{
		a2::AstNode* const next = apply_offset_(curr, curr->data_dwords);

		if (curr->next_sibling_offset != a2::Builder::NO_CHILDREN.rep)
		{
			ASSERT_OR_IGNORE(prev != nullptr);

			a2::AstNode* const first_child = reinterpret_cast<a2::AstNode*>(reinterpret_cast<u32*>(begin) + curr->next_sibling_offset);

			ASSERT_OR_IGNORE((first_child->internal_flags & a2::AstNode::FLAG_FIRST_SIBLING) == 0);

			first_child->internal_flags |= a2::AstNode::FLAG_FIRST_SIBLING;

			ASSERT_OR_IGNORE((prev->internal_flags & a2::AstNode::FLAG_LAST_SIBLING) == 0);

			prev->internal_flags |= a2::AstNode::FLAG_LAST_SIBLING;
		}

		prev = curr;

		curr = next;
	}

	ASSERT_OR_IGNORE((prev->internal_flags & (a2::AstNode::FLAG_FIRST_SIBLING | a2::AstNode::FLAG_LAST_SIBLING)) == 0);

	prev->internal_flags |= a2::AstNode::FLAG_FIRST_SIBLING | a2::AstNode::FLAG_LAST_SIBLING;
}

// Create a linked list modelling a preorder traversal of all nodes.
static a2::AstNode* build_traversal_list(a2::AstNode* begin, a2::AstNode* end) noexcept
{
	sreg depth = -1;

	u32 recursively_last_child = a2::Builder::NO_CHILDREN.rep;

	u32 prev_sibling_inds[a2::MAX_TREE_DEPTH];

	a2::AstNode* curr = begin;

	while (true)
	{
		const u32 curr_ind = static_cast<u32>(reinterpret_cast<u32*>(curr) - reinterpret_cast<u32*>(begin));

		// Connect predecessor

		if ((curr->internal_flags & a2::AstNode::FLAG_FIRST_SIBLING) == 0)
		{
			ASSERT_OR_IGNORE(depth >= 0);

			const u32 prev_sibling_ind = prev_sibling_inds[depth];

			a2::AstNode* prev_sibling = reinterpret_cast<a2::AstNode*>(reinterpret_cast<u32*>(begin) + prev_sibling_ind);

			prev_sibling->next_sibling_offset = curr_ind;
		}

		// Push something

		if ((curr->internal_flags & a2::AstNode::FLAG_LAST_SIBLING) == 0)
		{
			if ((curr->internal_flags & a2::AstNode::FLAG_FIRST_SIBLING) == a2::AstNode::FLAG_FIRST_SIBLING)
			{
				if (depth + 1 >= a2::MAX_TREE_DEPTH)
					panic("Maximum parse tree depth of %u exceeded.\n", a2::MAX_TREE_DEPTH);

				depth += 1;
			}

			ASSERT_OR_IGNORE(depth >= 0);

			if ((curr->internal_flags & a2::AstNode::FLAG_NO_CHILDREN) == 0)
			{
				ASSERT_OR_IGNORE(recursively_last_child != a2::Builder::NO_CHILDREN.rep);

				prev_sibling_inds[depth] = recursively_last_child;
			}
			else
			{
				prev_sibling_inds[depth] = curr_ind;
			}
		}
		else // last sibling
		{
			if ((curr->internal_flags & a2::AstNode::FLAG_FIRST_SIBLING) == 0)
			{
				ASSERT_OR_IGNORE(depth >= 0);

				depth -= 1;
			}

			if ((curr->internal_flags & a2::AstNode::FLAG_NO_CHILDREN) == a2::AstNode::FLAG_NO_CHILDREN)
				recursively_last_child = curr_ind;
		}

		a2::AstNode* const next = apply_offset_(curr, curr->data_dwords);

		if (next == end)
			break;

		curr = next;
	}
	
	ASSERT_OR_IGNORE(depth == -1);

	ASSERT_OR_IGNORE(reinterpret_cast<a2::AstNode*>(reinterpret_cast<u32*>(curr) + curr->data_dwords) == end);

	return curr;
}

// Traverse the linked list created by build_traversal_list, pushing nodes into
// dst.
static a2::AstNode* copy_postorder_to_preorder(const a2::AstNode* begin, const a2::AstNode* end, const a2::AstNode* src_root, AstPool* dst) noexcept
{
	u32 prev_sibling_inds[a2::MAX_TREE_DEPTH];

	s32 depth = -1;

	const u32 end_ind = static_cast<u32>(reinterpret_cast<const u32*>(end) - reinterpret_cast<const u32*>(begin));

	a2::AstNode* const dst_root = alloc_ast(dst, end_ind);

	a2::AstNode* dst_curr = dst_root;

	const a2::AstNode* src_curr = src_root;

	while (true)
	{
		// Copy node

		a2::AstNode* const dst_node = dst_curr;
		
		dst_curr = a2::apply_offset_(dst_curr, src_curr->data_dwords);

		memcpy(dst_node, src_curr, src_curr->data_dwords * sizeof(u32));

		const u32 curr_ind = static_cast<u32>(reinterpret_cast<u32*>(dst_node) - reinterpret_cast<u32*>(dst_root));

		if ((src_curr->internal_flags & a2::AstNode::FLAG_FIRST_SIBLING) == 0)
		{
			while (true)
			{
				ASSERT_OR_IGNORE(depth > 0); // Actually greater than and *not* equal to 0; root node should never be popped here

				const u32 prev_sibling_ind = prev_sibling_inds[depth];

				depth -= 1;

				a2::AstNode* const prev_sibling = reinterpret_cast<a2::AstNode*>(reinterpret_cast<u32*>(dst_root) + prev_sibling_ind);

				prev_sibling->next_sibling_offset = curr_ind - prev_sibling_ind;

				if ((prev_sibling->internal_flags & a2::AstNode::FLAG_LAST_SIBLING) == 0)
					break;
			}
		}

		ASSERT_OR_IGNORE(depth + 1 < a2::MAX_TREE_DEPTH);

		depth += 1;

		prev_sibling_inds[depth] = curr_ind;

		if (src_curr->next_sibling_offset == a2::Builder::NO_CHILDREN.rep)
			break;

		src_curr = reinterpret_cast<const a2::AstNode*>(reinterpret_cast<const u32*>(begin) + src_curr->next_sibling_offset);
	}

	ASSERT_OR_IGNORE(depth != -1);

	while (depth >= 0)
	{
		const u32 prev_sibling_ind = prev_sibling_inds[depth];

		depth -= 1;

		a2::AstNode* const prev_sibling = reinterpret_cast<a2::AstNode*>(reinterpret_cast<u32*>(dst_root) + prev_sibling_ind);

		prev_sibling->next_sibling_offset = end_ind - prev_sibling_ind;
	}

	return dst_root;
}

a2::AstNode* a2::complete_ast(Builder* builder, AstPool* dst) noexcept
{
	AstNode* const begin = reinterpret_cast<AstNode*>(builder->scratch.begin());

	AstNode* const end = reinterpret_cast<AstNode*>(builder->scratch.end());

	set_internal_flags(begin, end);

	AstNode* const src_root = build_traversal_list(begin, end);

	AstNode* const dst_root = copy_postorder_to_preorder(begin, end, src_root, dst);

	builder->scratch.reset(1 << 20);

	return dst_root;
}

const char8* a2::tag_name(AstTag tag) noexcept
{
	if (static_cast<u8>(tag) < array_count(NODE_TYPE_NAMES))
		return NODE_TYPE_NAMES[static_cast<u8>(tag)];

	return NODE_TYPE_NAMES[0];
}
