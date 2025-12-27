#include "core.hpp"

#include <cstring>

#include "../infra/common.hpp"
#include "../infra/container/reserved_vec.hpp"

struct AstPool
{
	ReservedVec<AstNode> nodes;

	ReservedVec<SourceId> sources;

	ReservedVec<AstNode> node_builder;

	ReservedVec<SourceId> source_builder;

	ReservedVec<ClosureList> closure_lists;

	MutRange<byte> memory;
};

struct AstAllocation
{
	AstNode* nodes;

	SourceId* sources;
};

static void lower_tags_rec(AstPool* asts, AstNode* src_node, bool lower_where_expr) noexcept;

static AstAllocation alloc_ast(AstPool* asts, u32 qwords) noexcept
{
	AstNode* const nodes = static_cast<AstNode*>(asts->nodes.reserve_exact(qwords * sizeof(u64)));

	SourceId* const sources = static_cast<SourceId*>(asts->sources.reserve_exact(qwords * sizeof(SourceId)));

	return AstAllocation{ nodes, sources };
}

// Set FLAG_FIRST_SIBLING and FLAG_LAST_SIBLING (note that next_sibling_offset
// holds the offset to the first child at this point):
//
//   for each node do
//     if next_sibling_offset != NO_CHILDREN then
//         direct predecessor gets FLAG_LAST_SIBLING
//         predecessor at next_sibling_offset gets FLAG_FIRST_SIBLING
static void set_flags(AstPool* asts) noexcept
{
	AstNode* const begin = asts->node_builder.begin();

	AstNode* const end = asts->node_builder.end();

	AstNode* prev = nullptr;

	AstNode* curr = begin;

	while (curr != end)
	{
		AstNode* const next = curr + curr->own_qwords;

		if (curr->next_sibling_offset != static_cast<u32>(AstBuilderToken::NO_CHILDREN))
		{
			ASSERT_OR_IGNORE(prev != nullptr);

			AstNode* const first_child = begin + curr->next_sibling_offset;

			ASSERT_OR_IGNORE((first_child->structure_flags & AstNode::STRUCTURE_FIRST_SIBLING) == 0);

			first_child->structure_flags |= AstNode::STRUCTURE_FIRST_SIBLING;

			ASSERT_OR_IGNORE((first_child->structure_flags & AstNode::STRUCTURE_LAST_SIBLING) == 0);

			prev->structure_flags |= AstNode::STRUCTURE_LAST_SIBLING;
		}

		prev = curr;

		curr = next;
	}

	ASSERT_OR_IGNORE((prev->structure_flags & (AstNode::STRUCTURE_FIRST_SIBLING | AstNode::STRUCTURE_LAST_SIBLING)) == 0);

	prev->structure_flags |= AstNode::STRUCTURE_FIRST_SIBLING | AstNode::STRUCTURE_LAST_SIBLING;
}

// Create a linked list modelling a preorder traversal of all nodes.
static u32 build_traversal_list(AstPool* asts) noexcept
{
	s32 depth = -1;

	u32 recursively_last_child = static_cast<u32>(AstBuilderToken::NO_CHILDREN);

	u32 prev_sibling_indices[MAX_AST_DEPTH];

	AstNode* const begin = asts->node_builder.begin();

	AstNode* const end = asts->node_builder.end();

	AstNode* curr = begin;

	while (true)
	{
		const u32 curr_ind = static_cast<u32>(curr - begin);

		// Connect predecessor

		if ((curr->structure_flags & AstNode::STRUCTURE_FIRST_SIBLING) == 0)
		{
			ASSERT_OR_IGNORE(depth >= 0);

			const u32 prev_sibling_ind = prev_sibling_indices[depth];

			AstNode* prev_sibling = begin + prev_sibling_ind;

			prev_sibling->next_sibling_offset = curr_ind;
		}

		// Push something

		if ((curr->structure_flags & AstNode::STRUCTURE_LAST_SIBLING) == 0)
		{
			if ((curr->structure_flags & AstNode::STRUCTURE_FIRST_SIBLING) != 0)
			{
				if (depth + 1 >= MAX_AST_DEPTH)
					panic("Maximum parse tree depth of %u exceeded.\n", MAX_AST_DEPTH);

				depth += 1;
			}

			ASSERT_OR_IGNORE(depth >= 0);

			if ((curr->structure_flags & AstNode::STRUCTURE_NO_CHILDREN) == 0)
			{
				ASSERT_OR_IGNORE(recursively_last_child != static_cast<u32>(AstBuilderToken::NO_CHILDREN));

				prev_sibling_indices[depth] = recursively_last_child;
			}
			else
			{
				prev_sibling_indices[depth] = curr_ind;
			}
		}
		else // last sibling
		{
			if ((curr->structure_flags & AstNode::STRUCTURE_FIRST_SIBLING) == 0)
			{
				ASSERT_OR_IGNORE(depth >= 0);

				depth -= 1;
			}

			if ((curr->structure_flags & AstNode::STRUCTURE_NO_CHILDREN) != 0)
				recursively_last_child = curr_ind;
		}

		AstNode* const next = curr + curr->own_qwords;

		if (next == end)
			break;

		curr = next;
	}

	ASSERT_OR_IGNORE(depth == -1);

	ASSERT_OR_IGNORE(curr + curr->own_qwords == end);

	return static_cast<u32>(curr - begin);
}

// Traverse the linked list created by build_traversal_list, pushing nodes into
// `asts`.
static AstNode* copy_postorder_to_preorder(AstPool* asts, u32 src_root_index) noexcept
{
	u32 prev_sibling_indices[MAX_AST_DEPTH];

	s32 depth = -1;

	const AstAllocation allocation = alloc_ast(asts, asts->node_builder.used());

	const AstNode* const src_nodes = asts->node_builder.begin();

	const SourceId* const src_sources = asts->source_builder.begin();

	AstNode* const dst_nodes = allocation.nodes;

	SourceId* const dst_sources = allocation.sources;

	u32 src_index = src_root_index;

	u32 dst_index = 0;

	while (true)
	{
		// Copy node

		const AstNode* const curr_src_node = src_nodes + src_index;

		const SourceId* const curr_src_source = src_sources + src_index;

		AstNode* const curr_dst_node = dst_nodes + dst_index;

		SourceId* const curr_dst_source = dst_sources + dst_index;

		const u8 src_data_qwords = curr_src_node->own_qwords;

		memcpy(curr_dst_node, curr_src_node, src_data_qwords * sizeof(u64));

		*curr_dst_source = *curr_src_source;

		if ((curr_src_node->structure_flags & AstNode::STRUCTURE_FIRST_SIBLING) == 0)
		{
			while (true)
			{
				// Actually greater than and *not* equal to 0; root node should
				// never be popped here.
				ASSERT_OR_IGNORE(depth > 0);

				const u32 prev_sibling_index = prev_sibling_indices[depth];

				depth -= 1;

				AstNode* const prev_sibling = dst_nodes + prev_sibling_index;

				prev_sibling->next_sibling_offset = dst_index - prev_sibling_index;

				if ((prev_sibling->structure_flags & AstNode::STRUCTURE_LAST_SIBLING) == 0)
					break;
			}
		}

		ASSERT_OR_IGNORE(depth + 1 < MAX_AST_DEPTH);

		depth += 1;

		prev_sibling_indices[depth] = dst_index;

		if (curr_src_node->next_sibling_offset == static_cast<u32>(AstBuilderToken::NO_CHILDREN))
			break;

		dst_index += src_data_qwords;

		src_index = src_nodes[src_index].next_sibling_offset;
	}

	ASSERT_OR_IGNORE(depth != -1);

	while (depth >= 0)
	{
		const u32 prev_sibling_index = prev_sibling_indices[depth];

		depth -= 1;

		AstNode* const prev_sibling = dst_nodes + prev_sibling_index;

		prev_sibling->next_sibling_offset = asts->node_builder.used() - prev_sibling_index;
	}

	return allocation.nodes;
}



static AstNode* make_synth_node(AstPool* asts, AstTag tag, AstFlag flags, u8 structure_flags, SourceId source_id) noexcept
{
	AstNode* const node = asts->nodes.reserve();
	node->tag = tag;
	node->flags = flags;
	node->own_qwords = 1;
	node->structure_flags = structure_flags;
	node->next_sibling_offset = 0;

	asts->sources.append(source_id);

	return node;
}

template<typename T>
static AstNode* make_synth_node(AstPool* asts, T attach, AstFlag flags, u8 structure_flags, SourceId source_id) noexcept
{
	const u8 own_qwords = 1  + sizeof(T) / sizeof(AstNode);

	AstNode* const node = asts->nodes.reserve(own_qwords);
	node->tag = T::TAG;
	node->flags = flags;
	node->own_qwords = own_qwords;
	node->structure_flags = structure_flags;
	node->next_sibling_offset = 0;

	memcpy(node + 1, &attach, sizeof(attach));

	*asts->sources.reserve(own_qwords) = source_id;

	return node;
}

static void close_synth_node(AstPool* asts, AstNode* node) noexcept
{
	node->next_sibling_offset = static_cast<u32>(asts->nodes.end() - node);
}

static IdentifierId next_synth_id(IdentifierId synth_id) noexcept
{
	if (synth_id >= IdentifierId::FirstNatural)
		panic("Exceeded maximum of %u synthetic identifiers in a block.\n", static_cast<u32>(IdentifierId::FirstNatural) - static_cast<u32>(IdentifierId::FirstSynth));

	return static_cast<IdentifierId>(static_cast<u32>(synth_id) + 1);
}



static IdentifierId lower_locs_rec(AstPool* asts, AstNode* node, IdentifierId synth_id) noexcept;

static bool needs_location(AstTag tag) noexcept
{
	return tag == AstTag::UOpAddr
	    || tag == AstTag::OpSliceOf
	    || tag == AstTag::OpArrayIndex
	    || tag == AstTag::Member;
}

static bool provides_location(AstTag tag) noexcept
{
	return tag == AstTag::UOpDeref
		|| tag == AstTag::OpArrayIndex
		|| tag == AstTag::Member
		|| tag == AstTag::Identifier;
}

static IdentifierId lower_locs_rec_promote_value_to_definition(AstPool* asts, AstNode* node, IdentifierId synth_id) noexcept
{
	ASSERT_OR_IGNORE(node->tag != AstTag::File);

	if (node->tag == AstTag::Block)
		return synth_id;

	AstDirectChildIterator it = direct_children_of(node);

	if (needs_location(node->tag) && !provides_location(first_child_of(node)->tag))
	{
		if (!has_next(&it))
			ASSERT_UNREACHABLE;

		// Skip `node`'s first child in during further promotion. It has just
		// been lowered into `synth_definition`.
		AstNode* const child = next(&it);

		AstNode* const synth_definition = make_synth_node(asts, AstDefinitionData{ synth_id }, AstFlag::EMPTY, node->structure_flags & ~AstNode::STRUCTURE_LAST_SIBLING, source_id_of_ast_node(asts, node));

		synth_id = next_synth_id(synth_id);

		synth_id = lower_locs_rec(asts, child, synth_id);

		close_synth_node(asts, synth_definition);
	}

	while (has_next(&it))
	{
		AstNode* const child = next(&it);

		synth_id = lower_locs_rec_promote_value_to_definition(asts, child, synth_id);
	}

	return synth_id;
}

// {
//   <prev-exprs>
//
//   <surrounding-expr> <value-expr>.& <surrounding-expr>
//
//   <next-exprs>
// }
//
// becomes
//
// {
//   <prev-exprs>
//
//   let _unnamed = <value-expr>
//
//   <surrounding-expr> _unnamed.& <surrounding-expr>
//
//   <next-exprs>
// }
//
// Here, <value-expr> is anything that is not either
// - An identifier - `x`
// - A pointer dereference - `<e>.*`
// - A slice dereference - `<e>[n]`
// - An array dereference whose left-hand-side is itself *not* a <value-expr> - `<e>[n]`
// - A member access whose left-hand-side is itself *not* a <value-expr> - `<e>.y`
static IdentifierId lower_locs_rec(AstPool* asts, AstNode* node, IdentifierId synth_id) noexcept
{
	AstNode* const dst_node = asts->nodes.reserve(node->own_qwords);
	memcpy(dst_node, node, node->own_qwords * sizeof(u64));

	SourceId* const dst_source = asts->sources.reserve(node->own_qwords);
	*dst_source = asts->sources.begin()[node - asts->nodes.begin()];

	AstDirectChildIterator it = direct_children_of(node);

	if (needs_location(node->tag) && !provides_location(first_child_of(node)->tag))
	{
		AstNode* const synth_identifier = make_synth_node(asts, AstIdentifierData{ synth_id, NameBinding{} }, AstFlag::EMPTY, node->structure_flags | AstNode::STRUCTURE_NO_CHILDREN, source_id_of_ast_node(asts, node));

		synth_id = next_synth_id(synth_id);

		close_synth_node(asts, synth_identifier);

		// Skip `node`'s first child. It is outlined and referenced by
		// `synth_identifier`.

		if (!has_next(&it))
			ASSERT_UNREACHABLE;

		(void) next(&it);
	}

	const bool is_lifting_root = node->tag == AstTag::Block || node->tag == AstTag::File;

	IdentifierId child_synth_id = is_lifting_root ? IdentifierId::SecondSynth : synth_id;

	while (has_next(&it))
	{
		AstNode* const child = next(&it);

		const bool has_promoted = is_lifting_root && lower_locs_rec_promote_value_to_definition(asts, child, child_synth_id) != child_synth_id;

		AstNode* const lowered_first = asts->nodes.end();

		child_synth_id = lower_locs_rec(asts, child, child_synth_id);

		if (has_promoted)
			lowered_first->structure_flags &= ~AstNode::STRUCTURE_FIRST_SIBLING;
	}

	dst_node->next_sibling_offset = static_cast<u32>(asts->nodes.end() - dst_node);

	return is_lifting_root ? synth_id : child_synth_id;
}



// if|for|foreach|switch <header> where <def1> <def2> ... <defn> <body>
//
// becomes
//
// {
//   <def1>
//   <def2>
//   ...
//   <defn>
//
//   if|for|foreach|switch <header> <body>
// }
static void lower_tags_expr_with_where(AstPool* asts, AstNode* src_node, AstNode* src_where) noexcept
{
	ASSERT_OR_IGNORE((src_node->tag == AstTag::If || src_node->tag == AstTag::For || src_node->tag == AstTag::ForEach || src_node->tag == AstTag::Switch) && src_where->tag == AstTag::Where);

	const SourceId src_where_source = asts->sources.begin()[src_where - asts->nodes.begin()];

	AstNode* const dst_block = make_synth_node(asts, AstTag::Block, AstFlag::EMPTY, src_node->structure_flags, src_where_source);

	AstDirectChildIterator where_defs = direct_children_of(src_where);

	while (has_next(&where_defs))
	{
		AstNode* const src_where_def = next(&where_defs);

		ASSERT_OR_IGNORE(src_where_def->tag == AstTag::Definition);

		lower_tags_rec(asts, src_where_def, true);
	}

	// Lower without special-casing `where` by setting `lower_where_expr` to `false`.
	lower_tags_rec(asts, src_node, false);

	close_synth_node(asts, dst_block);
}

// <lhs> <op>= <rhs>
//
// becomes
//
// {
//   let _unnamed = <lhs>.&
//
//   _unnamed.* = _unnamed.* <op> <rhs>
// }
static void lower_tags_set_op(AstPool* asts, AstNode* src_node) noexcept
{
	ASSERT_OR_IGNORE(src_node->tag >= AstTag::OpSetAdd && src_node->tag <= AstTag::OpSetShiftR);

	const SourceId src_source = asts->sources.begin()[src_node - asts->nodes.begin()];

	AstNode* const src_lhs = first_child_of(src_node);

	AstNode* const src_rhs = next_sibling_of(src_lhs);

	AstNode* const dst_block = make_synth_node(asts, AstTag::Block, AstFlag::EMPTY, src_node->structure_flags, src_source);

	// let _unnamed = <lhs>.&

	AstNode* const dst_definition = make_synth_node(asts, AstDefinitionData{ IdentifierId::FirstSynth }, AstFlag::EMPTY, AstNode::STRUCTURE_FIRST_SIBLING, src_source);

	AstNode* const dst_addrof = make_synth_node(asts, AstTag::UOpAddr, AstFlag::EMPTY, AstNode::STRUCTURE_FIRST_SIBLING | AstNode::STRUCTURE_LAST_SIBLING, src_source);

	lower_tags_rec(asts, src_lhs, true);

	dst_addrof[1].structure_flags |= AstNode::STRUCTURE_LAST_SIBLING;

	close_synth_node(asts, dst_addrof);

	close_synth_node(asts, dst_definition);

	// _unnamed.* = _unnamed.* <op> <rhs>

	AstNode* const dst_set = make_synth_node(asts, AstTag::OpSet, AstFlag::EMPTY, AstNode::STRUCTURE_LAST_SIBLING, src_source);

	AstNode* const dst_set_lhs_deref = make_synth_node(asts, AstTag::UOpDeref, AstFlag::EMPTY, AstNode::STRUCTURE_FIRST_SIBLING, src_source);

	AstNode* const dst_set_lhs_identifier = make_synth_node(asts, AstIdentifierData{ IdentifierId::FirstSynth, NameBinding{} }, AstFlag::EMPTY, AstNode::STRUCTURE_FIRST_SIBLING | AstNode::STRUCTURE_LAST_SIBLING | AstNode::STRUCTURE_NO_CHILDREN, src_source);

	close_synth_node(asts, dst_set_lhs_identifier);

	close_synth_node(asts, dst_set_lhs_deref);

	const AstTag op_tag = static_cast<AstTag>(static_cast<u8>(src_node->tag) - static_cast<u8>(AstTag::OpSetAdd) + static_cast<u8>(AstTag::OpAdd));

	AstNode* const dst_set_op = make_synth_node(asts, op_tag, AstFlag::EMPTY, AstNode::STRUCTURE_LAST_SIBLING, src_source);

	AstNode* const dst_set_rhs_deref = make_synth_node(asts, AstTag::UOpDeref, AstFlag::EMPTY, AstNode::STRUCTURE_FIRST_SIBLING, src_source);

	AstNode* const dst_set_rhs_identifier = make_synth_node(asts, AstIdentifierData{ IdentifierId::FirstSynth, NameBinding{} }, AstFlag::EMPTY, AstNode::STRUCTURE_FIRST_SIBLING | AstNode::STRUCTURE_LAST_SIBLING | AstNode::STRUCTURE_NO_CHILDREN, src_source);

	close_synth_node(asts, dst_set_rhs_identifier);
	
	close_synth_node(asts, dst_set_rhs_deref);

	lower_tags_rec(asts, src_rhs, true);

	dst_set_rhs_identifier[1 + sizeof(AstIdentifierData) / sizeof(u64)].structure_flags |= AstNode::STRUCTURE_LAST_SIBLING;

	close_synth_node(asts, dst_set_op);

	close_synth_node(asts, dst_set);

	close_synth_node(asts, dst_block);
}

static void lower_tags_rec(AstPool* asts, AstNode* src_node, bool lower_where_expr) noexcept
{
	if (lower_where_expr && src_node->tag == AstTag::If && has_flag(src_node, AstFlag::If_HasWhere))
	{
		IfInfo info = get_if_info(src_node);

		lower_tags_expr_with_where(asts, src_node, get(info.where));
	}
	else if (lower_where_expr && src_node->tag == AstTag::For && has_flag(src_node, AstFlag::For_HasWhere))
	{
		ForInfo info = get_for_info(src_node);

		lower_tags_expr_with_where(asts, src_node, get(info.where));
	}
	else if (lower_where_expr && src_node->tag == AstTag::ForEach && has_flag(src_node, AstFlag::ForEach_HasWhere))
	{
		ForEachInfo info = get_foreach_info(src_node);

		lower_tags_expr_with_where(asts, src_node, get(info.where));
	}
	else if (lower_where_expr && src_node->tag == AstTag::Switch && has_flag(src_node, AstFlag::Switch_HasWhere))
	{
		SwitchInfo info = get_switch_info(src_node);

		lower_tags_expr_with_where(asts, src_node, get(info.where));
	}
	else if (src_node->tag >= AstTag::OpSetAdd && src_node->tag <= AstTag::OpSetShiftR)
	{
		lower_tags_set_op(asts, src_node);
	}
	else if (src_node->tag != AstTag::Where)
	{
		const u64 src_index = src_node - asts->nodes.begin();

		AstNode* const dst_node = asts->nodes.reserve(src_node->own_qwords);
		memcpy(dst_node, src_node, src_node->own_qwords * sizeof(u64));

		*asts->sources.reserve(src_node->own_qwords) = asts->sources.begin()[src_index];

		AstDirectChildIterator it = direct_children_of(src_node);

		while (has_next(&it))
			lower_tags_rec(asts, next(&it), true);

		close_synth_node(asts, dst_node);
	}
}



static void lower_ast(AstPool* asts, AstNode* src_root) noexcept
{
	ASSERT_OR_IGNORE(src_root >= asts->nodes.begin() && src_root < asts->nodes.end());

	SourceId* const src_sources = asts->sources.begin() + (src_root - asts->nodes.begin());

	// Tags

	AstNode* const tags_result = asts->nodes.end();

	lower_tags_rec(asts, src_root, true);

	ASSERT_OR_IGNORE(asts->nodes.used() == asts->sources.used());

	// Values -> Locations

	AstNode* const locs_result = asts->nodes.end();

	(void) lower_locs_rec(asts, tags_result, IdentifierId::SecondSynth);

	// Move back and shrink

	ASSERT_OR_IGNORE(asts->nodes.used() == asts->sources.used());

	memcpy(src_root, locs_result, locs_result->next_sibling_offset * sizeof(u64));

	memcpy(src_sources, asts->sources.begin() + (locs_result - asts->nodes.begin()), locs_result->next_sibling_offset * sizeof(SourceId));

	const u32 new_used = static_cast<u32>(src_root - asts->nodes.begin()) + locs_result->next_sibling_offset;

	asts->nodes.pop_to(new_used);

	asts->sources.pop_to(new_used);
}



AstPool* create_ast_pool(HandlePool* alloc) noexcept
{
	AstPool* const asts = alloc_handle_from_pool<AstPool>(alloc);

	static constexpr u64 nodes_reserve_size = (static_cast<u64>(1) << 30) * sizeof(AstNode);

	static constexpr u64 sources_reserve_size = (static_cast<u64>(1) << 30) * sizeof(SourceId);

	static constexpr u64 node_builder_reserve_size = (static_cast<u64>(1) << 26) * sizeof(AstNode);

	static constexpr u64 source_builder_reserve_size = (static_cast<u64>(1) << 26) * sizeof(SourceId);

	static constexpr u64 closure_lists_reserve_size = (static_cast<u64>(1) << 24) * sizeof(ClosureListEntry);

	static constexpr u64 total_size = nodes_reserve_size
	                     + sources_reserve_size
	                     + node_builder_reserve_size
	                     + source_builder_reserve_size
	                     + closure_lists_reserve_size;

	byte* const memory = static_cast<byte*>(minos::mem_reserve(total_size));

	if (memory == nullptr)
		panic("Could not reserve memory for AstPool (0x%X).\n", minos::last_error());

	u64 byte_offset = 0;

	asts->nodes.init({ memory + byte_offset, nodes_reserve_size }, static_cast<u32>(1) << 18);
	byte_offset += nodes_reserve_size;

	asts->sources.init({ memory + byte_offset, sources_reserve_size }, static_cast<u32>(1) << 18);
	byte_offset += sources_reserve_size;

	asts->node_builder.init({ memory + byte_offset, node_builder_reserve_size }, static_cast<u32>(1) << 16);
	byte_offset += node_builder_reserve_size;

	asts->source_builder.init({ memory + byte_offset, source_builder_reserve_size }, static_cast<u32>(1) << 16);
	byte_offset += source_builder_reserve_size;

	asts->closure_lists.init({ memory + byte_offset, closure_lists_reserve_size }, static_cast<u32>(1) << 12);
	byte_offset += closure_lists_reserve_size;

	ASSERT_OR_IGNORE(byte_offset == total_size);

	asts->memory = { memory, total_size };

	(void) asts->nodes.reserve();

	(void) asts->sources.reserve();

	(void) asts->closure_lists.reserve();

	return asts;
}

void release_ast_pool(AstPool* asts) noexcept
{
	minos::mem_unreserve(asts->memory.begin(), asts->memory.count());
}

AstNodeId id_from_ast_node(AstPool* asts, AstNode* node) noexcept
{
	return static_cast<AstNodeId>(static_cast<u32>(node - asts->nodes.begin()));
}

AstNode* ast_node_from_id(AstPool* asts, AstNodeId id) noexcept
{
	ASSERT_OR_IGNORE(id != AstNodeId::INVALID);

	return asts->nodes.begin() + static_cast<u32>(id);
}



bool has_children(const AstNode* node) noexcept
{
	return (node->structure_flags & AstNode::STRUCTURE_NO_CHILDREN) == 0;
}

bool has_next_sibling(const AstNode* node) noexcept
{
	return (node->structure_flags & AstNode::STRUCTURE_LAST_SIBLING) == 0;
}

bool has_flag(const AstNode* node, AstFlag flag) noexcept
{
	return (node->flags & flag) != AstFlag::EMPTY;
}

bool is_descendant_of(const AstNode* parent, const AstNode* child) noexcept
{
	return child >= parent && child < parent + parent->next_sibling_offset;
}

AstNode* next_sibling_of(AstNode* node) noexcept
{
	ASSERT_OR_IGNORE(has_next_sibling(node));

	return node + node->next_sibling_offset;
}

AstNode* first_child_of(AstNode* node) noexcept
{
	ASSERT_OR_IGNORE(has_children(node));

	return node + node->own_qwords;
}

SourceId source_id_of_ast_node(const AstPool* asts, const AstNode* node) noexcept
{
	const u64 index = node - asts->nodes.begin();

	ASSERT_OR_IGNORE(index <= static_cast<u64>(asts->sources.used()));

	return asts->sources.begin()[index];
}




AstBuilderToken push_node(AstPool* asts, AstBuilderToken first_child, SourceId source_id, AstFlag flags, AstTag tag) noexcept
{
	static_assert(sizeof(AstNode) == sizeof(u64));

	AstNode* const node = static_cast<AstNode*>(asts->node_builder.reserve_exact(sizeof(AstNode)));
	node->tag = tag;
	node->flags = flags;
	node->own_qwords = 1;
	node->structure_flags = first_child == AstBuilderToken::NO_CHILDREN ? AstNode::STRUCTURE_NO_CHILDREN : 0;
	node->next_sibling_offset = static_cast<u32>(first_child);

	SourceId* const node_source = static_cast<SourceId*>(asts->source_builder.reserve_exact(sizeof(SourceId)));
	*node_source = source_id;

	return static_cast<AstBuilderToken>(node - asts->node_builder.begin());
}

AstBuilderToken push_node(AstPool* asts, AstBuilderToken first_child, SourceId source_id, AstFlag flags, AstTag tag, u8 attachment_qwords, const void* attachment) noexcept
{
	static_assert(sizeof(AstNode) == sizeof(u64));

	const u8 required_qwords = static_cast<u8>(1 + attachment_qwords);

	AstNode* const node = static_cast<AstNode*>(asts->node_builder.reserve_exact(required_qwords * sizeof(u64)));
	node->tag = tag;
	node->flags = flags;
	node->own_qwords = required_qwords;
	node->structure_flags = first_child == AstBuilderToken::NO_CHILDREN ? AstNode::STRUCTURE_NO_CHILDREN : 0;
	node->next_sibling_offset = static_cast<u32>(first_child);

	SourceId* const node_source = static_cast<SourceId*>(asts->source_builder.reserve_exact(required_qwords * sizeof(SourceId)));
	*node_source = source_id;

	memcpy(node + 1, attachment, attachment_qwords * sizeof(u64));

	return static_cast<AstBuilderToken>(node - asts->node_builder.begin());
}

AstNode* complete_ast(AstPool* asts) noexcept
{
	set_flags(asts);

	const u32 src_root_index = build_traversal_list(asts);

	AstNode* const root = copy_postorder_to_preorder(asts, src_root_index);

	asts->node_builder.reset(1 << 17);

	asts->source_builder.reset(1 << 17);

	lower_ast(asts, root);

	asts->node_builder.reset(1 << 17);

	asts->source_builder.reset(1 << 17);

	return root;
}



ClosureList* alloc_closure_list(AstPool* asts, u16 entry_count) noexcept
{
	ClosureList* const list = asts->closure_lists.reserve(entry_count + 1);
	list->count = entry_count;
	list->unused_ = 0;

	return list;
}

ClosureListId id_from_closure_list(AstPool* asts, ClosureList* closure_list) noexcept
{
	return static_cast<ClosureListId>(closure_list  - asts->closure_lists.begin());
}

ClosureList* closure_list_from_id(AstPool* asts, ClosureListId id) noexcept
{
	ASSERT_OR_IGNORE(id != ClosureListId::INVALID);

	return asts->closure_lists.begin() + static_cast<u32>(id);
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

	iterator->curr = curr + curr->own_qwords;

	if ((curr->structure_flags & AstNode::STRUCTURE_NO_CHILDREN) == 0)
	{
		if ((curr->structure_flags & AstNode::STRUCTURE_LAST_SIBLING) == 0)
		{
			ASSERT_OR_IGNORE(iterator->top + 1 < MAX_AST_DEPTH);

			iterator->top += 1;

			iterator->prev_depths[iterator->top] = iterator->depth;
		}

		ASSERT_OR_IGNORE(iterator->depth + 1 < MAX_AST_DEPTH);

		iterator->depth += 1;
	}
	else if ((curr->structure_flags & AstNode::STRUCTURE_LAST_SIBLING) != 0)
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

		iterator.offsets[iterator.depth] = static_cast<u32>(node - iterator.base);
	}

	return iterator;
}

AstIterationResult next(AstPostorderIterator* iterator) noexcept
{
	ASSERT_OR_IGNORE(iterator->depth >= 0);

	AstNode* const ret_node = iterator->base + iterator->offsets[iterator->depth];

	const u32 ret_depth = static_cast<u32>(iterator->depth);

	AstNode* curr = ret_node;

	if (has_next_sibling(curr))
	{
		curr = next_sibling_of(curr);

		iterator->offsets[iterator->depth] = static_cast<u32>(curr - iterator->base);

		while (has_children(curr))
		{
			curr = first_child_of(curr);

			iterator->depth += 1;

			ASSERT_OR_IGNORE(iterator->depth < MAX_AST_DEPTH);

			iterator->offsets[iterator->depth] = static_cast<u32>(curr - iterator->base);
		}
	}
	else
	{
		iterator->depth -= 1;

		if (iterator->depth >= 0)
			curr = iterator->base + iterator->offsets[iterator->depth];
	}

	return { ret_node, ret_depth };
}

bool has_next(const AstPostorderIterator* iterator) noexcept
{
	return iterator->depth >= 0;
}


AstFlatIterator flat_ancestors_of(AstNode* node) noexcept
{
	return { node, node + node->next_sibling_offset };
}

AstNode* next(AstFlatIterator* iterator) noexcept
{
	ASSERT_OR_IGNORE(has_next(iterator));

	AstNode* const result = iterator->curr;

	iterator->curr += iterator->curr->own_qwords;

	return result;
}

bool has_next(const AstFlatIterator* iterator) noexcept
{
	return iterator->curr != iterator->end;
}



SignatureInfo get_signature_info(AstNode* signature) noexcept
{
	ASSERT_OR_IGNORE(signature->tag == AstTag::Signature);

	SignatureInfo desc{};

	AstNode* curr = first_child_of(signature);

	ASSERT_OR_IGNORE(curr->tag == AstTag::ParameterList);

	desc.parameters = curr;

	if (has_flag(signature, AstFlag::Signature_HasReturnType))
	{
		curr = next_sibling_of(curr);

		desc.return_type = some(curr);
	}

	if (has_flag(signature, AstFlag::Signature_HasExpects))
	{
		curr = next_sibling_of(curr);

		ASSERT_OR_IGNORE(curr->tag == AstTag::Expects);

		desc.expects = some(curr);
	}

	if (has_flag(signature, AstFlag::Signature_HasEnsures))
	{
		curr = next_sibling_of(curr);

		ASSERT_OR_IGNORE(curr->tag == AstTag::Ensures);

		desc.ensures = some(curr);
	}

	ASSERT_OR_IGNORE(!has_next_sibling(curr));

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

		return { some(type), has_next_sibling(type) ? some(next_sibling_of(type)) : none<AstNode*>() };
	}

	return { none<AstNode*>(), some(first_child_of(definition)) };
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
	ASSERT_OR_IGNORE(node->tag == AstTag::For);

	AstNode* curr = first_child_of(node);

	ForInfo info{};

	info.condition = curr;

	curr = next_sibling_of(curr);

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

SwitchInfo get_switch_info(AstNode* node) noexcept
{
	ASSERT_OR_IGNORE(node->tag == AstTag::Switch);

	AstNode* curr = first_child_of(node);

	SwitchInfo info{};

	info.switched = curr;

	curr = next_sibling_of(node);

	if (has_flag(node, AstFlag::Switch_HasWhere))
	{
		info.where = some(curr);

		curr = next_sibling_of(node);
	}
	else
	{
		info.where = none<AstNode*>();
	}

	info.first_case = curr;

	return info;
}

OpSliceOfInfo get_op_slice_of_info(AstNode* node) noexcept
{
	ASSERT_OR_IGNORE(node->tag == AstTag::OpSliceOf);

	AstNode* curr = first_child_of(node);

	OpSliceOfInfo info{};

	info.sliced = curr;

	if (has_flag(node, AstFlag::OpSliceOf_HasBegin))
	{
		curr = next_sibling_of(curr);

		info.begin = some(curr);
	}
	else
	{
		info.begin = none<AstNode*>();
	}

	if (has_flag(node, AstFlag::OpSliceOf_HasEnd))
	{
		curr = next_sibling_of(curr);

		info.end = some(curr);
	}
	else
	{
		info.end = none<AstNode*>();
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
		"Signature",
		"Trait",
		"Impl",
		"Catch",
		"Unreachable",
		"Undefined",
		"Identifier",
		"LitInteger",
		"LitFloat",
		"LitChar",
		"LitString",
		"OpSliceOf",
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
		"UOpTypeVarArgs",
		"ImpliedMember",
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
		"Member",
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
