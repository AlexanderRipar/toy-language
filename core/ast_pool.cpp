#include "core.hpp"
#include "structure.hpp"

#include "../infra/types.hpp"
#include "../infra/assert.hpp"
#include "../infra/panic.hpp"
#include "../infra/math.hpp"
#include "../infra/range.hpp"
#include "../infra/container/reserved_vec.hpp"

#include <cstring>

struct AstAllocation
{
	AstNode* nodes;

	SourceId* sources;
};

static AstAllocation alloc_ast(CoreData* core, u32 qwords) noexcept
{
	AstNode* const nodes = static_cast<AstNode*>(core->asts.nodes.reserve_exact(qwords * sizeof(u64)));

	SourceId* const sources = static_cast<SourceId*>(core->asts.sources.reserve_exact(qwords * sizeof(SourceId)));

	return AstAllocation{ nodes, sources };
}

// Set FLAG_FIRST_SIBLING and FLAG_LAST_SIBLING (note that next_sibling_offset
// holds the offset to the first child at this point):
//
//   for each node do
//     if next_sibling_offset != NO_CHILDREN then
//         direct predecessor gets FLAG_LAST_SIBLING
//         predecessor at next_sibling_offset gets FLAG_FIRST_SIBLING
static void set_flags(CoreData* core) noexcept
{
	AstNode* const begin = core->asts.node_builder.begin();

	AstNode* const end = core->asts.node_builder.end();

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
static u32 build_traversal_list(CoreData* core) noexcept
{
	s32 depth = -1;

	u32 recursively_last_child = static_cast<u32>(AstBuilderToken::NO_CHILDREN);

	u32 prev_sibling_indices[MAX_AST_DEPTH];

	AstNode* const begin = core->asts.node_builder.begin();

	AstNode* const end = core->asts.node_builder.end();

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
					panic("Maximum parse tree depth of % exceeded.\n", MAX_AST_DEPTH);

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
static AstNode* copy_postorder_to_preorder(CoreData* core, u32 src_root_index) noexcept
{
	u32 prev_sibling_indices[MAX_AST_DEPTH];

	s32 depth = -1;

	const AstAllocation allocation = alloc_ast(core, core->asts.node_builder.used());

	const AstNode* const src_nodes = core->asts.node_builder.begin();

	const SourceId* const src_sources = core->asts.source_builder.begin();

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

		prev_sibling->next_sibling_offset = core->asts.node_builder.used() - prev_sibling_index;
	}

	return allocation.nodes;
}



static constexpr u64 NODES_RESERVE_SIZE = (static_cast<u64>(1) << 28) * sizeof(AstNode);

static constexpr u64 SOURCES_RESERVE_SIZE = (static_cast<u64>(1) << 28) * sizeof(SourceId);

static constexpr u64 NODE_BUILDER_RESERVE_SIZE = (static_cast<u64>(1) << 26) * sizeof(AstNode);

static constexpr u64 SOURCE_BUILDER_RESERVE_SIZE = (static_cast<u64>(1) << 26) * sizeof(SourceId);

static constexpr u64 CLOSURE_LISTS_RESERVE_SIZE = (static_cast<u64>(1) << 24) * sizeof(ClosureListEntry);

bool ast_pool_validate_config([[maybe_unused]] const Config* config, [[maybe_unused]] PrintSink sink) noexcept
{
	return true;
}

MemoryRequirements ast_pool_memory_requirements([[maybe_unused]] const Config* config) noexcept
{
	MemoryRequirements reqs;
	reqs.private_reserve = SOURCES_RESERVE_SIZE + NODE_BUILDER_RESERVE_SIZE + SOURCE_BUILDER_RESERVE_SIZE;
	reqs.id_requirements_count = 2;
	reqs.id_requirements[0].reserve = NODES_RESERVE_SIZE;
	reqs.id_requirements[0].alignment = alignof(AstNode);
	reqs.id_requirements[1].reserve = CLOSURE_LISTS_RESERVE_SIZE;
	reqs.id_requirements[1].alignment = alignof(ClosureListEntry);

	return reqs;
}

void ast_pool_init(CoreData* core, MemoryAllocation allocation) noexcept
{
	ASSERT_OR_IGNORE(allocation.ids[0].count() == NODES_RESERVE_SIZE);
	
	ASSERT_OR_IGNORE(allocation.ids[1].count() == CLOSURE_LISTS_RESERVE_SIZE);

	AstPool* const asts = &core->asts;

	asts->nodes.init(allocation.ids[0], static_cast<u32>(1) << 18);

	asts->closure_lists.init(allocation.ids[1], static_cast<u32>(1) << 12);

	u64 private_data_offset = 0;

	asts->sources.init(allocation.private_data.mut_subrange(private_data_offset, SOURCES_RESERVE_SIZE), static_cast<u32>(1) << 18);
	private_data_offset += SOURCES_RESERVE_SIZE;

	asts->node_builder.init(allocation.private_data.mut_subrange(private_data_offset, NODE_BUILDER_RESERVE_SIZE), static_cast<u32>(1) << 16);
	private_data_offset += NODE_BUILDER_RESERVE_SIZE;

	asts->source_builder.init(allocation.private_data.mut_subrange(private_data_offset, SOURCE_BUILDER_RESERVE_SIZE), static_cast<u32>(1) << 16);
	private_data_offset += SOURCE_BUILDER_RESERVE_SIZE;

	ASSERT_OR_IGNORE(allocation.private_data.count() == private_data_offset);

	(void) asts->nodes.reserve();

	(void) asts->sources.reserve();

	(void) asts->closure_lists.reserve();
}



AstNodeId id_from_ast_node(CoreData* core, AstNode* node) noexcept
{
	return static_cast<AstNodeId>(static_cast<u32>(node - core->asts.nodes.begin()));
}

AstNode* ast_node_from_id(CoreData* core, AstNodeId id) noexcept
{
	ASSERT_OR_IGNORE(id != AstNodeId::INVALID);

	return core->asts.nodes.begin() + static_cast<u32>(id);
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

SourceId source_id_of_ast_node(const CoreData* core, const AstNode* node) noexcept
{
	const u64 index = node - core->asts.nodes.begin();

	ASSERT_OR_IGNORE(index <= static_cast<u64>(core->asts.sources.used()));

	return core->asts.sources.begin()[index];
}




AstBuilderToken push_node(CoreData* core, AstBuilderToken first_child, SourceId source_id, AstFlag flags, AstTag tag) noexcept
{
	static_assert(sizeof(AstNode) == sizeof(u64));

	AstNode* const node = static_cast<AstNode*>(core->asts.node_builder.reserve_exact(sizeof(AstNode)));
	node->tag = tag;
	node->flags = flags;
	node->own_qwords = 1;
	node->structure_flags = first_child == AstBuilderToken::NO_CHILDREN ? AstNode::STRUCTURE_NO_CHILDREN : 0;
	node->next_sibling_offset = static_cast<u32>(first_child);

	SourceId* const node_source = static_cast<SourceId*>(core->asts.source_builder.reserve_exact(sizeof(SourceId)));
	*node_source = source_id;

	return static_cast<AstBuilderToken>(node - core->asts.node_builder.begin());
}

AstBuilderToken push_node(CoreData* core, AstBuilderToken first_child, SourceId source_id, AstFlag flags, AstTag tag, u8 attachment_qwords, const void* attachment) noexcept
{
	static_assert(sizeof(AstNode) == sizeof(u64));

	const u8 required_qwords = static_cast<u8>(1 + attachment_qwords);

	AstNode* const node = static_cast<AstNode*>(core->asts.node_builder.reserve_exact(required_qwords * sizeof(u64)));
	node->tag = tag;
	node->flags = flags;
	node->own_qwords = required_qwords;
	node->structure_flags = first_child == AstBuilderToken::NO_CHILDREN ? AstNode::STRUCTURE_NO_CHILDREN : 0;
	node->next_sibling_offset = static_cast<u32>(first_child);

	SourceId* const node_source = static_cast<SourceId*>(core->asts.source_builder.reserve_exact(required_qwords * sizeof(SourceId)));
	*node_source = source_id;

	memcpy(node + 1, attachment, attachment_qwords * sizeof(u64));

	return static_cast<AstBuilderToken>(node - core->asts.node_builder.begin());
}

AstNode* complete_ast(CoreData* core) noexcept
{
	set_flags(core);

	const u32 src_root_index = build_traversal_list(core);

	AstNode* const root = copy_postorder_to_preorder(core, src_root_index);

	core->asts.node_builder.reset(1 << 17);

	core->asts.source_builder.reset(1 << 17);

	return root;
}



ClosureList* alloc_closure_list(CoreData* core, u16 entry_count) noexcept
{
	ClosureList* const list = core->asts.closure_lists.reserve(entry_count + 1);
	list->count = entry_count;
	list->unused_ = 0;

	return list;
}

ClosureListId id_from_closure_list(CoreData* core, ClosureList* closure_list) noexcept
{
	return static_cast<ClosureListId>(closure_list  - core->asts.closure_lists.begin());
}

ClosureList* closure_list_from_id(CoreData* core, ClosureListId id) noexcept
{
	ASSERT_OR_IGNORE(id != ClosureListId::INVALID);

	return core->asts.closure_lists.begin() + static_cast<u32>(id);
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

	curr = next_sibling_of(curr);

	desc.return_type = curr;

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
		"Self",
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
		"TraitParameterList",
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
