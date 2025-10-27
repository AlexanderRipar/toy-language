#include "test_helpers.hpp"
#include "../core/core.hpp"

struct DummyTree
{
	u32 index;

	u32 unused_;

	AstNode nodes[64];

};

static constexpr u8 NODE_QWORDS = sizeof(AstNode) / sizeof(u64);

static void push_node(DummyTree* tree, AstNode node, u8 data_qwords = 0, const void* data = nullptr) noexcept
{
	const u32 required_qwords = NODE_QWORDS + data_qwords;

	if (tree->index + required_qwords > array_count(tree->nodes))
		panic("Testing dummy tree too large");

	memcpy(tree->nodes + tree->index, &node, sizeof(AstNode));

	memcpy(tree->nodes + tree->index + NODE_QWORDS, data, data_qwords * sizeof(u32));

	tree->index += required_qwords;
}

// Tree:
//
// File
static DummyTree single_node_dummy_tree() noexcept
{
	DummyTree tree;
	tree.index = 0;

	push_node(&tree, { AstTag::OpAdd, AstFlag::EMPTY, NODE_QWORDS, AstNode::STRUCTURE_FIRST_SIBLING | AstNode::STRUCTURE_LAST_SIBLING | AstNode::STRUCTURE_NO_CHILDREN, NODE_QWORDS });

	return tree;
}

// Tree:
//
// File
// ` Block
static DummyTree unary_dummy_tree() noexcept
{
	DummyTree tree;
	tree.index = 0;

	push_node(&tree, { AstTag::File, AstFlag::EMPTY, NODE_QWORDS, AstNode::STRUCTURE_FIRST_SIBLING | AstNode::STRUCTURE_LAST_SIBLING, 2 * NODE_QWORDS });

	push_node(&tree, { AstTag::Block, AstFlag::EMPTY, NODE_QWORDS, AstNode::STRUCTURE_FIRST_SIBLING | AstNode::STRUCTURE_LAST_SIBLING | AstNode::STRUCTURE_NO_CHILDREN, NODE_QWORDS });

	return tree;
}

// Tree:
//
// OpBitAnd
// + LitChar
// ` Identifier
static DummyTree binary_dummy_tree() noexcept
{
	DummyTree tree;
	tree.index = 0;

	push_node(&tree, { AstTag::OpAdd, AstFlag::EMPTY, NODE_QWORDS, AstNode::STRUCTURE_FIRST_SIBLING | AstNode::STRUCTURE_LAST_SIBLING, 3 * NODE_QWORDS });

	push_node(&tree, { AstTag::OpSub, AstFlag::EMPTY, NODE_QWORDS, AstNode::STRUCTURE_FIRST_SIBLING | AstNode::STRUCTURE_NO_CHILDREN, NODE_QWORDS });

	push_node(&tree, { AstTag::OpMul, AstFlag::EMPTY, NODE_QWORDS, AstNode::STRUCTURE_LAST_SIBLING | AstNode::STRUCTURE_NO_CHILDREN, NODE_QWORDS });

	return tree;
}

// Tree:
//
// File
// + Block ... `n` times
// ` Block
static DummyTree nary_dummy_tree(u32 n) noexcept
{
	ASSERT_OR_IGNORE(n != 0);

	DummyTree tree;
	tree.index = 0;

	push_node(&tree, { AstTag::File, AstFlag::EMPTY, NODE_QWORDS, AstNode::STRUCTURE_FIRST_SIBLING | AstNode::STRUCTURE_LAST_SIBLING, NODE_QWORDS * (n + 1) });

	for (u32 i = 0; i != n; ++i)
	{
		u8 structure_flags;

		if (i == 0)
			structure_flags = AstNode::STRUCTURE_FIRST_SIBLING | AstNode::STRUCTURE_NO_CHILDREN;
		else if (i == n - 1)
			structure_flags = AstNode::STRUCTURE_LAST_SIBLING | AstNode::STRUCTURE_NO_CHILDREN;
		else
			structure_flags = AstNode::STRUCTURE_NO_CHILDREN;

		push_node(&tree, { AstTag::OpAdd, AstFlag::EMPTY, NODE_QWORDS, structure_flags, NODE_QWORDS });
	}

	return tree;
}

// Tree:
//
// 1
// + 2
// | | 3
// | ` 4
// ` 5
//   + 10 - Instead of 6, i.e. AstTag::Where, as it gets lowered into
//   | |    nothingness.
//   | ` 7
//   ` 8
//     ` 9
static DummyTree complex_dummy_tree() noexcept
{
	DummyTree tree;
	tree.index = 0;

	push_node(&tree, { static_cast<AstTag>(1), AstFlag::EMPTY, NODE_QWORDS, AstNode::STRUCTURE_FIRST_SIBLING | AstNode::STRUCTURE_LAST_SIBLING, 9 * NODE_QWORDS });

	push_node(&tree, { static_cast<AstTag>(2), AstFlag::EMPTY, NODE_QWORDS, AstNode::STRUCTURE_FIRST_SIBLING, 3 * NODE_QWORDS });

	push_node(&tree, { static_cast<AstTag>(3), AstFlag::EMPTY, NODE_QWORDS, AstNode::STRUCTURE_FIRST_SIBLING | AstNode::STRUCTURE_NO_CHILDREN, NODE_QWORDS });

	push_node(&tree, { static_cast<AstTag>(4), AstFlag::EMPTY, NODE_QWORDS, AstNode::STRUCTURE_LAST_SIBLING | AstNode::STRUCTURE_NO_CHILDREN, NODE_QWORDS });

	push_node(&tree, { static_cast<AstTag>(5), AstFlag::EMPTY, NODE_QWORDS, AstNode::STRUCTURE_LAST_SIBLING, 5 * NODE_QWORDS });

	push_node(&tree, { static_cast<AstTag>(10), AstFlag::EMPTY, NODE_QWORDS, AstNode::STRUCTURE_FIRST_SIBLING, 2 * NODE_QWORDS });

	push_node(&tree, { static_cast<AstTag>(7), AstFlag::EMPTY, NODE_QWORDS, AstNode::STRUCTURE_FIRST_SIBLING | AstNode::STRUCTURE_LAST_SIBLING | AstNode::STRUCTURE_NO_CHILDREN, NODE_QWORDS });

	push_node(&tree, { static_cast<AstTag>(8), AstFlag::EMPTY, NODE_QWORDS, AstNode::STRUCTURE_LAST_SIBLING, 2 * NODE_QWORDS });

	push_node(&tree, { static_cast<AstTag>(9), AstFlag::EMPTY, NODE_QWORDS, AstNode::STRUCTURE_FIRST_SIBLING | AstNode::STRUCTURE_LAST_SIBLING | AstNode::STRUCTURE_NO_CHILDREN, NODE_QWORDS });

	return tree;
}

// Tree:
//
// OpAdd
// + OpSub
// | + LitChar
// | ` OpDiv
// |   + LitFloat
// |   ` Identifier
// ` LitInteger
static DummyTree double_binary_dummy_tree() noexcept
{
	DummyTree tree;
	tree.index = 0;

	push_node(&tree, { AstTag::OpAdd, AstFlag::EMPTY, NODE_QWORDS, AstNode::STRUCTURE_FIRST_SIBLING | AstNode::STRUCTURE_LAST_SIBLING, 7 * NODE_QWORDS });

	push_node(&tree, { AstTag::OpSub, AstFlag::EMPTY, NODE_QWORDS, AstNode::STRUCTURE_FIRST_SIBLING, 5 * NODE_QWORDS });

	push_node(&tree, { AstTag::LitChar, AstFlag::EMPTY, NODE_QWORDS, AstNode::STRUCTURE_FIRST_SIBLING | AstNode::STRUCTURE_NO_CHILDREN, NODE_QWORDS });

	push_node(&tree, { AstTag::OpDiv, AstFlag::EMPTY, NODE_QWORDS, AstNode::STRUCTURE_LAST_SIBLING, 3 * NODE_QWORDS });

	push_node(&tree, { AstTag::LitFloat, AstFlag::EMPTY, NODE_QWORDS, AstNode::STRUCTURE_FIRST_SIBLING | AstNode::STRUCTURE_NO_CHILDREN, NODE_QWORDS });

	push_node(&tree, { AstTag::Identifier, AstFlag::EMPTY, NODE_QWORDS, AstNode::STRUCTURE_LAST_SIBLING | AstNode::STRUCTURE_NO_CHILDREN, NODE_QWORDS });

	push_node(&tree, { AstTag::LitInteger, AstFlag::EMPTY, NODE_QWORDS, AstNode::STRUCTURE_LAST_SIBLING | AstNode::STRUCTURE_NO_CHILDREN, NODE_QWORDS });

	return tree;
}

// Tree:
//
// File
// + Definition
// | ` Identifier
// + Definition
// | ` LitChar
// + Definition
// | ` LitFloat
// ` Definition
//   ` LitString
static DummyTree flat_dummy_tree() noexcept
{
	DummyTree tree;
	tree.index = 0;

	push_node(&tree, { AstTag::File, AstFlag::EMPTY, NODE_QWORDS, AstNode::STRUCTURE_FIRST_SIBLING | AstNode::STRUCTURE_LAST_SIBLING, 9 * NODE_QWORDS });

	push_node(&tree, { AstTag::Definition, AstFlag::EMPTY, NODE_QWORDS, AstNode::STRUCTURE_FIRST_SIBLING, 2 * NODE_QWORDS });

	push_node(&tree, { AstTag::Identifier, AstFlag::EMPTY, NODE_QWORDS, AstNode::STRUCTURE_FIRST_SIBLING | AstNode::STRUCTURE_LAST_SIBLING | AstNode::STRUCTURE_NO_CHILDREN, NODE_QWORDS });

	push_node(&tree, { AstTag::Definition, AstFlag::EMPTY, NODE_QWORDS, 0, 2 * NODE_QWORDS });

	push_node(&tree, { AstTag::LitChar, AstFlag::EMPTY, NODE_QWORDS, AstNode::STRUCTURE_FIRST_SIBLING | AstNode::STRUCTURE_LAST_SIBLING | AstNode::STRUCTURE_NO_CHILDREN, NODE_QWORDS });

	push_node(&tree, { AstTag::Definition, AstFlag::EMPTY, NODE_QWORDS, 0, 2 * NODE_QWORDS });

	push_node(&tree, { AstTag::LitFloat, AstFlag::EMPTY, NODE_QWORDS, AstNode::STRUCTURE_FIRST_SIBLING | AstNode::STRUCTURE_LAST_SIBLING | AstNode::STRUCTURE_NO_CHILDREN, NODE_QWORDS });

	push_node(&tree, { AstTag::Definition, AstFlag::EMPTY, NODE_QWORDS, AstNode::STRUCTURE_LAST_SIBLING, 2 * NODE_QWORDS });

	push_node(&tree, { AstTag::LitString, AstFlag::EMPTY, NODE_QWORDS, AstNode::STRUCTURE_FIRST_SIBLING | AstNode::STRUCTURE_LAST_SIBLING | AstNode::STRUCTURE_NO_CHILDREN, NODE_QWORDS });

	return tree;
}



struct MockedPools
{
	AstPool* asts;

	AllocPool* alloc;
};

static MockedPools create_mocked_pools() noexcept
{
	AllocPool* const alloc = create_alloc_pool(4096, 4096);

	AstPool* const asts = create_ast_pool(alloc);

	return { asts, alloc };
}

static void release_mocked_pools(MockedPools mock) noexcept
{
	release_ast_pool(mock.asts);

	release_alloc_pool(mock.alloc);
}




static void has_children_on_single_node_is_false() noexcept
{
	TEST_BEGIN;

	DummyTree tree = single_node_dummy_tree();

	TEST_EQUAL(has_children(reinterpret_cast<AstNode*>(&tree.nodes)), false);

	TEST_END;
}

static void has_children_with_single_child_is_true() noexcept
{
	TEST_BEGIN;

	DummyTree tree = unary_dummy_tree();

	TEST_EQUAL(has_children(reinterpret_cast<AstNode*>(tree.nodes)), true);

	TEST_END;
}

static void has_children_with_two_children_is_true() noexcept
{
	TEST_BEGIN;

	DummyTree tree = binary_dummy_tree();

	TEST_EQUAL(has_children(reinterpret_cast<AstNode*>(tree.nodes)), true);

	TEST_END;
}



static void child_iterator_with_0_children_has_0_entries() noexcept
{
	TEST_BEGIN;

	DummyTree tree = single_node_dummy_tree();

	AstDirectChildIterator it = direct_children_of(reinterpret_cast<AstNode*>(tree.nodes));

	TEST_EQUAL(has_next(&it), false);

	TEST_END;
}

static void child_iterator_with_1_child_has_1_entry() noexcept
{
	TEST_BEGIN;

	DummyTree tree = unary_dummy_tree();

	AstDirectChildIterator it = direct_children_of(reinterpret_cast<AstNode*>(tree.nodes));

	TEST_EQUAL(has_next(&it), true);

	TEST_EQUAL(next(&it), reinterpret_cast<AstNode*>(tree.nodes) + 1);

	TEST_EQUAL(has_next(&it), false);

	TEST_END;
}

static void child_iterator_with_5_children_has_5_entries() noexcept
{
	TEST_BEGIN;

	DummyTree tree = nary_dummy_tree(5);

	AstDirectChildIterator it = direct_children_of(reinterpret_cast<AstNode*>(tree.nodes));

	for (u32 i = 0; i != 5; ++i)
	{
		TEST_EQUAL(has_next(&it), true);

		TEST_EQUAL(next(&it), reinterpret_cast<AstNode*>(tree.nodes) + i + 1);
	}

	TEST_EQUAL(has_next(&it), false);

	TEST_END;
}

static void child_iterator_with_grandchildren_only_iterates_direct_children() noexcept
{
	TEST_BEGIN;

	DummyTree tree = complex_dummy_tree();

	AstDirectChildIterator it = direct_children_of(reinterpret_cast<AstNode*>(tree.nodes));

	TEST_EQUAL(has_next(&it), true);

	TEST_EQUAL(next(&it), reinterpret_cast<AstNode*>(tree.nodes) + 1);

	TEST_EQUAL(has_next(&it), true);

	TEST_EQUAL(next(&it), reinterpret_cast<AstNode*>(tree.nodes) + 4);

	TEST_EQUAL(has_next(&it), false);

	TEST_END;
}



static void preorder_iterator_with_0_children_has_0_entries() noexcept
{
	TEST_BEGIN;

	DummyTree tree = single_node_dummy_tree();

	AstPreorderIterator it = preorder_ancestors_of(reinterpret_cast<AstNode*>(tree.nodes));

	TEST_EQUAL(has_next(&it), false);

	TEST_END;
}

static void preorder_iterator_with_1_child_has_1_entry() noexcept
{
	TEST_BEGIN;

	DummyTree tree = unary_dummy_tree();

	AstPreorderIterator it = preorder_ancestors_of(reinterpret_cast<AstNode*>(tree.nodes));

	TEST_EQUAL(has_next(&it), true);

	const AstIterationResult result = next(&it);

	TEST_EQUAL(result.node, reinterpret_cast<AstNode*>(tree.nodes) + 1);

	TEST_EQUAL(result.depth, 0);

	TEST_EQUAL(has_next(&it), false);

	TEST_END;
}

static void preorder_iterator_with_5_children_has_5_entries() noexcept
{
	TEST_BEGIN;

	DummyTree tree = nary_dummy_tree(5);

	AstPreorderIterator it = preorder_ancestors_of(reinterpret_cast<AstNode*>(tree.nodes));

	for (u32 i = 0; i != 5; ++i)
	{
		TEST_EQUAL(has_next(&it), true);

		const AstIterationResult result = next(&it);

		TEST_EQUAL(result.node, reinterpret_cast<AstNode*>(tree.nodes) + i + 1);

		TEST_EQUAL(result.depth, 0);
	}

	TEST_EQUAL(has_next(&it), false);

	TEST_END;
}

static void preorder_iterator_with_grandchildren_iterates_grandchildren() noexcept
{
	TEST_BEGIN;

	DummyTree tree = complex_dummy_tree();

	AstPreorderIterator it = preorder_ancestors_of(reinterpret_cast<AstNode*>(tree.nodes));

	static constexpr u32 expected_depths[] = { 0, 1, 1, 0, 1, 2, 1, 2 };

	for (u32 i = 0; i != 8; ++i)
	{
		TEST_EQUAL(has_next(&it), true);

		const AstIterationResult result = next(&it);

		TEST_EQUAL(result.node, reinterpret_cast<AstNode*>(tree.nodes) + i + 1);

		TEST_EQUAL(result.depth, expected_depths[i]);
	}

	TEST_EQUAL(has_next(&it), false);

	TEST_END;
}

static void preorder_iterator_with_flat_tree_iterates_subtrees() noexcept
{
	TEST_BEGIN;

	DummyTree tree = flat_dummy_tree();

	AstPreorderIterator it = preorder_ancestors_of(reinterpret_cast<AstNode*>(tree.nodes));

	static constexpr u32 expected_depths[] = {
		0, 1,
		0, 1,
		0, 1,
		0, 1
	};

	for (u32 i = 0; i != 8; ++i)
	{
		TEST_EQUAL(has_next(&it), true);

		const AstIterationResult result = next(&it);

		TEST_EQUAL(result.node, reinterpret_cast<AstNode*>(tree.nodes) + i + 1);

		TEST_EQUAL(result.depth, expected_depths[i]);
	}

	TEST_EQUAL(has_next(&it), false);

	TEST_END;
}



static void postorder_iterator_with_0_children_has_0_entries() noexcept
{
	TEST_BEGIN;

	DummyTree tree = single_node_dummy_tree();

	AstPostorderIterator it = postorder_ancestors_of(reinterpret_cast<AstNode*>(tree.nodes));

	TEST_EQUAL(has_next(&it), false);

	TEST_END;
}

static void postorder_iterator_with_1_child_has_1_entry() noexcept
{
	TEST_BEGIN;

	DummyTree tree = unary_dummy_tree();

	AstPostorderIterator it = postorder_ancestors_of(reinterpret_cast<AstNode*>(tree.nodes));

	TEST_EQUAL(has_next(&it), true);

	const AstIterationResult result = next(&it);

	TEST_EQUAL(result.node, reinterpret_cast<AstNode*>(tree.nodes) + 1);

	TEST_EQUAL(result.depth, 0);

	TEST_EQUAL(has_next(&it), false);

	TEST_END;
}

static void postorder_iterator_with_5_children_has_5_entries() noexcept
{
	TEST_BEGIN;

	DummyTree tree = nary_dummy_tree(5);

	AstPostorderIterator it = postorder_ancestors_of(reinterpret_cast<AstNode*>(tree.nodes));

	for (u32 i = 0; i != 5; ++i)
	{
		TEST_EQUAL(has_next(&it), true);

		const AstIterationResult result = next(&it);

		TEST_EQUAL(result.node, reinterpret_cast<AstNode*>(tree.nodes) + i + 1);

		TEST_EQUAL(result.depth, 0);
	}

	TEST_EQUAL(has_next(&it), false);

	TEST_END;
}

static void postorder_iterator_with_grandchildren_iterates_grandchildren() noexcept
{
	TEST_BEGIN;

	static constexpr u8 expected_offsets[] = {
		2, 3, 1, 6, 5, 8, 7, 4,
	};

	static constexpr u8 expected_depths[] = {
		1, 1, 0, 2, 1, 2, 1, 0
	};

	DummyTree tree = complex_dummy_tree();

	AstPostorderIterator it = postorder_ancestors_of(reinterpret_cast<AstNode*>(tree.nodes));

	for (u32 i = 0; i != 8; ++i)
	{
		TEST_EQUAL(has_next(&it), true);

		const AstIterationResult result = next(&it);

		TEST_EQUAL(result.node, reinterpret_cast<AstNode*>(tree.nodes) + expected_offsets[i]);

		TEST_EQUAL(result.depth, expected_depths[i]);
	}

	TEST_EQUAL(has_next(&it), false);

	TEST_END;
}



static void push_node_once_and_complete_appends_node() noexcept
{
	TEST_BEGIN;

	MockedPools pools = create_mocked_pools();

	push_node(pools.asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, AstFlag::EMPTY, AstTag::OpAdd);

	AstNode* const root = complete_ast(pools.asts);

	DummyTree expected = single_node_dummy_tree();

	TEST_MEM_EQUAL(root, expected.nodes, sizeof(AstNode));

	release_mocked_pools(pools);

	TEST_END;
}

static void push_node_with_unary_op_and_complete_reverses_tree() noexcept
{
	TEST_BEGIN;

	MockedPools pools = create_mocked_pools();

	const AstBuilderToken token = push_node(pools.asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, AstFlag::EMPTY, AstTag::Block);

	push_node(pools.asts, token, SourceId::INVALID, AstFlag::EMPTY, AstTag::File);

	AstNode* const root = complete_ast(pools.asts);

	DummyTree expected = unary_dummy_tree();

	TEST_MEM_EQUAL(root, expected.nodes, 2 * sizeof(AstNode));

	release_mocked_pools(pools);

	TEST_END;
}

static void push_node_with_binary_op_and_complete_reverses_tree() noexcept
{
	TEST_BEGIN;

	MockedPools pools = create_mocked_pools();

	const AstBuilderToken token = push_node(pools.asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, AstFlag::EMPTY, AstTag::OpSub);

	push_node(pools.asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, AstFlag::EMPTY, AstTag::OpMul);

	push_node(pools.asts, token, SourceId::INVALID, AstFlag::EMPTY, AstTag::OpAdd);

	AstNode* const root = complete_ast(pools.asts);

	DummyTree expected = binary_dummy_tree();

	TEST_MEM_EQUAL(root, expected.nodes, 3 * sizeof(AstNode));

	release_mocked_pools(pools);

	TEST_END;
}

static void push_node_with_complex_tree_and_complete_reverses_tree() noexcept
{
	TEST_BEGIN;

	MockedPools pools = create_mocked_pools();

	const AstBuilderToken t3 = push_node(pools.asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, AstFlag::EMPTY, static_cast<AstTag>(3));

	push_node(pools.asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, AstFlag::EMPTY, static_cast<AstTag>(4));

	const AstBuilderToken t2 = push_node(pools.asts, t3, SourceId::INVALID, AstFlag::EMPTY, static_cast<AstTag>(2));

	const AstBuilderToken t7 = push_node(pools.asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, AstFlag::EMPTY, static_cast<AstTag>(7));

	const AstBuilderToken t6 = push_node(pools.asts, t7, SourceId::INVALID, AstFlag::EMPTY, static_cast<AstTag>(10));

	const AstBuilderToken t9 = push_node(pools.asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, AstFlag::EMPTY, static_cast<AstTag>(9));

	push_node(pools.asts, t9, SourceId::INVALID, AstFlag::EMPTY, static_cast<AstTag>(8));

	push_node(pools.asts, t6, SourceId::INVALID, AstFlag::EMPTY, static_cast<AstTag>(5));

	push_node(pools.asts, t2, SourceId::INVALID, AstFlag::EMPTY, static_cast<AstTag>(1));

	AstNode* const root = complete_ast(pools.asts);

	DummyTree expected = complex_dummy_tree();

	TEST_MEM_EQUAL(root, expected.nodes, 9 * sizeof(AstNode));

	release_mocked_pools(pools);

	TEST_END;
}

static void push_node_with_double_binary_tree_and_complete_reverses_tree() noexcept
{
	TEST_BEGIN;

	MockedPools pools = create_mocked_pools();

// OpAdd
// + OpSub
// | + LitChar
// | ` OpDiv
// |   + LitFloat
// |   ` Identifier
// ` LitInteger
	const AstBuilderToken lit_char = push_node(pools.asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, AstFlag::EMPTY, AstTag::LitChar);

	const AstBuilderToken lit_float = push_node(pools.asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, AstFlag::EMPTY, AstTag::LitFloat);

	push_node(pools.asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, AstFlag::EMPTY, AstTag::Identifier);

	push_node(pools.asts, lit_float, SourceId::INVALID, AstFlag::EMPTY, AstTag::OpDiv);

	const AstBuilderToken sub = push_node(pools.asts, lit_char, SourceId::INVALID, AstFlag::EMPTY, AstTag::OpSub);

	push_node(pools.asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, AstFlag::EMPTY, AstTag::LitInteger);

	push_node(pools.asts, sub, SourceId::INVALID, AstFlag::EMPTY, AstTag::OpAdd);

	AstNode* const root = complete_ast(pools.asts);

	DummyTree expected = double_binary_dummy_tree();

	TEST_MEM_EQUAL(root, expected.nodes, 7 * sizeof(AstNode));

	release_mocked_pools(pools);

	TEST_END;
}


void ast_tests() noexcept
{
	TEST_MODULE_BEGIN;

	has_children_on_single_node_is_false();

	has_children_with_single_child_is_true();

	has_children_with_two_children_is_true();


	child_iterator_with_0_children_has_0_entries();

	child_iterator_with_1_child_has_1_entry();

	child_iterator_with_5_children_has_5_entries();

	child_iterator_with_grandchildren_only_iterates_direct_children();


	preorder_iterator_with_0_children_has_0_entries();

	preorder_iterator_with_1_child_has_1_entry();

	preorder_iterator_with_5_children_has_5_entries();

	preorder_iterator_with_grandchildren_iterates_grandchildren();

	preorder_iterator_with_flat_tree_iterates_subtrees();


	postorder_iterator_with_0_children_has_0_entries();

	postorder_iterator_with_1_child_has_1_entry();

	postorder_iterator_with_5_children_has_5_entries();

	postorder_iterator_with_grandchildren_iterates_grandchildren();


	push_node_once_and_complete_appends_node();

	push_node_with_unary_op_and_complete_reverses_tree();

	push_node_with_binary_op_and_complete_reverses_tree();

	push_node_with_complex_tree_and_complete_reverses_tree();

	push_node_with_double_binary_tree_and_complete_reverses_tree();

	TEST_MODULE_END;
}
