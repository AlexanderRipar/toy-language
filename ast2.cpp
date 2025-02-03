#include "ast2.hpp"

constexpr inline const char8* const NODE_TYPE_NAMES[] = {
	"[unknown]",
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
	"Call",
	"UOpTypeTailArray",
	"UOpTypeSlice",
	"UOpTypeMultiPtr",
	"UOpTypeOptMultiPtr",
	"UOpMut",
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
static void set_internal_flags(a2::Node* begin, a2::Node* end) noexcept
{
	ASSERT_OR_IGNORE(begin != end);

	a2::Node* prev = nullptr;

	a2::Node* curr = begin;

	while (curr != end)
	{
		a2::Node* const next = apply_offset_(curr, curr->data_dwords);

		if (curr->next_sibling_offset != a2::Builder::NO_CHILDREN.rep)
		{
			ASSERT_OR_IGNORE(prev != nullptr);

			a2::Node* const first_child = reinterpret_cast<a2::Node*>(reinterpret_cast<u32*>(begin) + curr->next_sibling_offset);

			ASSERT_OR_IGNORE((first_child->internal_flags & a2::Node::FLAG_FIRST_SIBLING) == 0);

			first_child->internal_flags |= a2::Node::FLAG_FIRST_SIBLING;

			ASSERT_OR_IGNORE((prev->internal_flags & a2::Node::FLAG_LAST_SIBLING) == 0);

			prev->internal_flags |= a2::Node::FLAG_LAST_SIBLING;
		}

		prev = curr;

		curr = next;
	}

	ASSERT_OR_IGNORE((prev->internal_flags & (a2::Node::FLAG_FIRST_SIBLING | a2::Node::FLAG_LAST_SIBLING)) == 0);

	prev->internal_flags |= a2::Node::FLAG_FIRST_SIBLING | a2::Node::FLAG_LAST_SIBLING;
}

// Create a linked list modelling a preorder traversal of all nodes.
static a2::Node* build_traversal_list(a2::Node* begin, a2::Node* end) noexcept
{
	sreg depth = -1;

	u32 recursively_last_child = a2::Builder::NO_CHILDREN.rep;

	u32 prev_sibling_inds[a2::MAX_TREE_DEPTH];

	a2::Node* curr = begin;

	while (true)
	{
		const u32 curr_ind = static_cast<u32>(reinterpret_cast<u32*>(curr) - reinterpret_cast<u32*>(begin));

		// Connect predecessor

		if ((curr->internal_flags & a2::Node::FLAG_FIRST_SIBLING) == 0)
		{
			ASSERT_OR_IGNORE(depth >= 0);

			const u32 prev_sibling_ind = prev_sibling_inds[depth];

			a2::Node* prev_sibling = reinterpret_cast<a2::Node*>(reinterpret_cast<u32*>(begin) + prev_sibling_ind);

			prev_sibling->next_sibling_offset = curr_ind;
		}

		// Push something

		if ((curr->internal_flags & a2::Node::FLAG_LAST_SIBLING) == 0)
		{
			if ((curr->internal_flags & a2::Node::FLAG_FIRST_SIBLING) == a2::Node::FLAG_FIRST_SIBLING)
			{
				if (depth + 1 >= a2::MAX_TREE_DEPTH)
					panic("Maximum parse tree depth of %u exceeded.\n", a2::MAX_TREE_DEPTH);

				depth += 1;
			}

			ASSERT_OR_IGNORE(depth >= 0);

			if ((curr->internal_flags & a2::Node::FLAG_NO_CHILDREN) == 0)
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
			if ((curr->internal_flags & a2::Node::FLAG_FIRST_SIBLING) == 0)
			{
				ASSERT_OR_IGNORE(depth >= 0);

				depth -= 1;
			}

			if ((curr->internal_flags & a2::Node::FLAG_NO_CHILDREN) == a2::Node::FLAG_NO_CHILDREN)
				recursively_last_child = curr_ind;
		}

		a2::Node* const next = apply_offset_(curr, curr->data_dwords);

		if (next == end)
			break;

		curr = next;
	}
	
	ASSERT_OR_IGNORE(depth == -1);

	ASSERT_OR_IGNORE(reinterpret_cast<a2::Node*>(reinterpret_cast<u32*>(curr) + curr->data_dwords) == end);

	return curr;
}

// Traverse the linked list created by build_traversal_list, pushing nodes into
// dst.
static a2::Node* copy_postorder_to_preorder(const a2::Node* begin, const a2::Node* src_root, ReservedVec<u32>* dst) noexcept
{
	u32 prev_sibling_inds[a2::MAX_TREE_DEPTH];

	s32 depth = -1;

	a2::Node* const dst_root = reinterpret_cast<a2::Node*>(dst->end());

	const a2::Node* curr = src_root;

	while (true)
	{
		// Copy node

		a2::Node* const dst_node = static_cast<a2::Node*>(dst->reserve_exact(curr->data_dwords * sizeof(u32)));

		memcpy(dst_node, curr, curr->data_dwords * sizeof(u32));

		const u32 curr_ind = static_cast<u32>(reinterpret_cast<u32*>(dst_node) - reinterpret_cast<u32*>(dst_root));

		if ((curr->internal_flags & a2::Node::FLAG_FIRST_SIBLING) == 0)
		{
			while (true)
			{
				ASSERT_OR_IGNORE(depth > 0); // Actually greater than and *not* equal to 0; root node should never be popped here

				const u32 prev_sibling_ind = prev_sibling_inds[depth];

				depth -= 1;

				a2::Node* const prev_sibling = reinterpret_cast<a2::Node*>(reinterpret_cast<u32*>(dst_root) + prev_sibling_ind);

				prev_sibling->next_sibling_offset = curr_ind - prev_sibling_ind;

				if ((prev_sibling->internal_flags & a2::Node::FLAG_LAST_SIBLING) == 0)
					break;
			}
		}

		ASSERT_OR_IGNORE(depth + 1 < a2::MAX_TREE_DEPTH);

		depth += 1;

		prev_sibling_inds[depth] = curr_ind;

		if (curr->next_sibling_offset == a2::Builder::NO_CHILDREN.rep)
			break;

		curr = reinterpret_cast<const a2::Node*>(reinterpret_cast<const u32*>(begin) + curr->next_sibling_offset);
	}

	ASSERT_OR_IGNORE(depth != -1);

	const u32 end_ind = static_cast<u32>(dst->end() - reinterpret_cast<u32*>(dst_root));

	while (depth >= 0)
	{
		const u32 prev_sibling_ind = prev_sibling_inds[depth];

		depth -= 1;

		a2::Node* const prev_sibling = reinterpret_cast<a2::Node*>(reinterpret_cast<u32*>(dst_root) + prev_sibling_ind);

		prev_sibling->next_sibling_offset = end_ind - prev_sibling_ind;
	}

	return dst_root;
}

a2::Node* a2::complete_ast(Builder* builder, ReservedVec<u32>* dst) noexcept
{
	Node* const begin = reinterpret_cast<Node*>(builder->scratch.begin());

	Node* const end = reinterpret_cast<Node*>(builder->scratch.end());

	set_internal_flags(begin, end);

	Node* const src_root = build_traversal_list(begin, end);

	Node* const dst_root = copy_postorder_to_preorder(begin, src_root, dst);

	builder->scratch.reset(1 << 20);

	return dst_root;
}

const char8* a2::tag_name(Tag tag) noexcept
{
	if (static_cast<u8>(tag) < array_count(NODE_TYPE_NAMES))
		return NODE_TYPE_NAMES[static_cast<u8>(tag)];

	return NODE_TYPE_NAMES[0];
}
