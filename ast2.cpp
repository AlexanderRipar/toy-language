#include "ast2.hpp"

a2::Node* a2::complete_ast(Builder* builder, ReservedVec<u32>* dst) noexcept
{
	// Set FLAG_FIRST_SIBLING and FLAG_LAST_SIBLING (note that
	// next_sibling_offset holds the offset to the first child at this
	// point):
	//
	//   for each node do
	//     if next_sibling_offset != NO_CHILDREN then
	//         direct predecessor gets FLAG_LAST_SIBLING
	//         predecessor at next_sibling_offset gets FLAG_FIRST_SIBLING

	u32* const begin = builder->scratch.begin();

	Node* curr = reinterpret_cast<Node*>(begin);

	Node* prev = nullptr;

	Node* const end = reinterpret_cast<Node*>(builder->scratch.end());

	ASSERT_OR_IGNORE(curr != end);

	while (curr != end)
	{
		Node* const next = apply_offset_(curr, curr->data_dwords);

		if (curr->next_sibling_offset != Builder::NO_CHILDREN.rep)
		{
			ASSERT_OR_IGNORE(prev != nullptr);

			Node* const first_child = reinterpret_cast<Node*>(begin + curr->next_sibling_offset);

			ASSERT_OR_IGNORE((first_child->internal_flags & Node::FLAG_FIRST_SIBLING) == 0);

			first_child->internal_flags |= Node::FLAG_FIRST_SIBLING;

			ASSERT_OR_IGNORE((prev->internal_flags & Node::FLAG_LAST_SIBLING) == 0);

			prev->internal_flags |= Node::FLAG_LAST_SIBLING;
		}

		prev = curr;

		curr = next;
	}

	ASSERT_OR_IGNORE((prev->internal_flags & (Node::FLAG_FIRST_SIBLING | Node::FLAG_LAST_SIBLING)) == 0);

	prev->internal_flags |= Node::FLAG_FIRST_SIBLING | Node::FLAG_LAST_SIBLING;

	// Create a linked list modelling a preorder traversal of all nodes.
	// This is done by setting next_sibling_offset using a stack, a tracker
	// for the most recently seen last sibling and the following rules:
	//
	//   for each node do
	//     if not FLAG_FIRST_SIBLING then
	//       pop the stack, setting the popped node's next_sibling to the current node
	//
	//     if FLAG_LAST_SIBLING
	//       set the tracker to the current node
	//     else if the current node already has next_sibling_offset != NO_CHILDREN then
	//       push the tracker onto the stack
	//     else
	//       push the current node onto the stack

	sreg depth = -1;

	u32 prev_sibling_inds[MAX_TREE_DEPTH];

	curr = reinterpret_cast<Node*>(begin);

	u32 last_prev_sibling_ind = 0;

	while (curr != end)
	{
		const u32 curr_ind = static_cast<u32>(reinterpret_cast<u32*>(curr) - begin);

		if ((curr->internal_flags & Node::FLAG_FIRST_SIBLING) == 0)
		{
			ASSERT_OR_IGNORE(depth >= 0);

			Node* const prev_sibling = reinterpret_cast<Node*>(begin + prev_sibling_inds[depth]);

			prev_sibling->next_sibling_offset = curr_ind;

			depth -= 1;
		}

		if ((curr->internal_flags & Node::FLAG_LAST_SIBLING) != 0)
		{
			last_prev_sibling_ind = curr_ind;
		}
		else
		{
			depth += 1;

			if (depth >= MAX_TREE_DEPTH)
				panic("Maximum parse tree depth of %u exceeded.\n", MAX_TREE_DEPTH);

			if (curr->next_sibling_offset != Builder::NO_CHILDREN.rep)
				prev_sibling_inds[depth] = last_prev_sibling_ind;
			else
				prev_sibling_inds[depth] = curr_ind;
		}

		curr = apply_offset_(curr, curr->data_dwords);
	}

	// Traverse the created linked list, pushing nodes into dst

	u32* const dst_root = dst->end();

	ASSERT_OR_IGNORE(depth == -1);

	depth = -1;

	curr = reinterpret_cast<Node*>(begin + last_prev_sibling_ind);

	ASSERT_OR_IGNORE(reinterpret_cast<u32*>(curr) + curr->data_dwords == builder->scratch.end());

	while (curr->next_sibling_offset != Builder::NO_CHILDREN.rep)
	{
		// Copy node

		const u32 required_dwords = sizeof(Node) / sizeof(u32) + curr->data_dwords;

		Node* const dst_node = reinterpret_cast<Node*>(dst->reserve_exact(required_dwords));

		memcpy(dst_node, curr, required_dwords * 4);

		// Adjust next_sibling_offset; This is a functionally similar to
		// the code to create the linked list

		const u32 curr_ind = static_cast<u32>(reinterpret_cast<u32*>(dst_node) - dst_root);

		if ((curr->internal_flags & (Node::FLAG_FIRST_SIBLING | Node::FLAG_LAST_SIBLING)) == Node::FLAG_FIRST_SIBLING)
		{
			depth += 1;

			ASSERT_OR_IGNORE(depth < MAX_TREE_DEPTH);

			prev_sibling_inds[depth] = curr_ind;
		}
		else if ((curr->internal_flags & (Node::FLAG_FIRST_SIBLING | Node::FLAG_LAST_SIBLING)) != (Node::FLAG_FIRST_SIBLING | Node::FLAG_LAST_SIBLING))
		{
			ASSERT_OR_IGNORE(depth >= 0);

			const u32 prev_sibling_ind = prev_sibling_inds[depth];

			Node* const prev_sibling = reinterpret_cast<Node*>(dst_root + prev_sibling_ind);

			prev_sibling->next_sibling_offset = curr_ind - prev_sibling_ind;

			if ((curr->internal_flags & Node::FLAG_LAST_SIBLING) == 0)
			{
				prev_sibling_inds[depth] = curr_ind;
			}
			else
			{
				dst_node->next_sibling_offset = 0;

				depth -= 1;
			}
		}
		else
		{
			dst_node->next_sibling_offset = 0;
		}

		curr = reinterpret_cast<Node*>(begin + curr->next_sibling_offset);
	}

	// Clear builder and return

	builder->scratch.reset(1 << 20);

	return reinterpret_cast<Node*>(dst_root);
}
