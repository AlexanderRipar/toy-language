#include "test_helpers.hpp"
#include "../ast2.hpp"
#include "../pass_data.hpp"

struct DummyTree
{
	u32 index;

	u32 dwords[32];

};

static constexpr u8 NODE_DWORDS = sizeof(a2::AstNode) / sizeof(u32);

static void push_node(DummyTree* tree, a2::AstNode node, u8 data_dwords = 0, const void* data = nullptr) noexcept
{
	const u32 required_dwords = NODE_DWORDS + data_dwords;

	if (tree->index + required_dwords > array_count(tree->dwords))
		panic("Testing dummy tree too large");

	memcpy(tree->dwords + tree->index, &node, sizeof(a2::AstNode));

	memcpy(tree->dwords + tree->index + NODE_DWORDS, data, data_dwords * sizeof(u32));

	tree->index += required_dwords;
}

static DummyTree single_node_dummy_tree() noexcept
{
	DummyTree tree;
	tree.index = 0;

	push_node(&tree, { a2::AstTag::File, a2::AstFlag::EMPTY, NODE_DWORDS, a2::AstNode::FLAG_FIRST_SIBLING | a2::AstNode::FLAG_LAST_SIBLING | a2::AstNode::FLAG_NO_CHILDREN, NODE_DWORDS });

	return tree;
}

static DummyTree unary_dummy_tree() noexcept
{
	DummyTree tree;
	tree.index = 0;

	push_node(&tree, { a2::AstTag::File, a2::AstFlag::EMPTY, NODE_DWORDS, a2::AstNode::FLAG_FIRST_SIBLING | a2::AstNode::FLAG_LAST_SIBLING, 2 * NODE_DWORDS });

	push_node(&tree, { a2::AstTag::Block, a2::AstFlag::EMPTY, NODE_DWORDS, a2::AstNode::FLAG_FIRST_SIBLING | a2::AstNode::FLAG_LAST_SIBLING | a2::AstNode::FLAG_NO_CHILDREN, NODE_DWORDS });

	return tree;
}

static DummyTree binary_dummy_tree() noexcept
{
	DummyTree tree;
	tree.index = 0;

	push_node(&tree, { a2::AstTag::OpBitAnd, a2::AstFlag::EMPTY, NODE_DWORDS, a2::AstNode::FLAG_FIRST_SIBLING | a2::AstNode::FLAG_LAST_SIBLING, 3 * NODE_DWORDS });

	push_node(&tree, { a2::AstTag::ValChar, a2::AstFlag::EMPTY, NODE_DWORDS, a2::AstNode::FLAG_FIRST_SIBLING | a2::AstNode::FLAG_NO_CHILDREN, NODE_DWORDS });

	push_node(&tree, { a2::AstTag::ValIdentifer, a2::AstFlag::EMPTY, NODE_DWORDS, a2::AstNode::FLAG_LAST_SIBLING | a2::AstNode::FLAG_NO_CHILDREN, NODE_DWORDS });

	return tree;
}

static DummyTree nary_dummy_tree(u32 n) noexcept
{
	ASSERT_OR_IGNORE(n != 0);

	DummyTree tree;
	tree.index = 0;

	push_node(&tree, { a2::AstTag::File, a2::AstFlag::EMPTY, NODE_DWORDS, a2::AstNode::FLAG_FIRST_SIBLING | a2::AstNode::FLAG_LAST_SIBLING, NODE_DWORDS * (n + 1) });

	for (u32 i = 0; i != n; ++i)
	{
		const u8 internal_flags = static_cast<u8>((i == 0 ? a2::AstNode::FLAG_FIRST_SIBLING : 0) | (i == n - 1 ? a2::AstNode::FLAG_LAST_SIBLING : 0) | a2::AstNode::FLAG_NO_CHILDREN);

		push_node(&tree, { a2::AstTag::Block, a2::AstFlag::EMPTY, NODE_DWORDS, internal_flags, NODE_DWORDS });
	}

	return tree;
}

static DummyTree complex_dummy_tree() noexcept
{
	DummyTree tree;
	tree.index = 0;

	push_node(&tree, { static_cast<a2::AstTag>(1), a2::AstFlag::EMPTY, NODE_DWORDS, a2::AstNode::FLAG_FIRST_SIBLING | a2::AstNode::FLAG_LAST_SIBLING, 9 * NODE_DWORDS });
	
	push_node(&tree, { static_cast<a2::AstTag>(2), a2::AstFlag::EMPTY, NODE_DWORDS, a2::AstNode::FLAG_FIRST_SIBLING, 3 * NODE_DWORDS });

	push_node(&tree, { static_cast<a2::AstTag>(3), a2::AstFlag::EMPTY, NODE_DWORDS, a2::AstNode::FLAG_FIRST_SIBLING | a2::AstNode::FLAG_NO_CHILDREN, NODE_DWORDS });

	push_node(&tree, { static_cast<a2::AstTag>(4), a2::AstFlag::EMPTY, NODE_DWORDS, a2::AstNode::FLAG_LAST_SIBLING | a2::AstNode::FLAG_NO_CHILDREN, NODE_DWORDS });

	push_node(&tree, { static_cast<a2::AstTag>(5), a2::AstFlag::EMPTY, NODE_DWORDS, a2::AstNode::FLAG_LAST_SIBLING, 5 * NODE_DWORDS });

	push_node(&tree, { static_cast<a2::AstTag>(6), a2::AstFlag::EMPTY, NODE_DWORDS, a2::AstNode::FLAG_FIRST_SIBLING, 2 * NODE_DWORDS });

	push_node(&tree, { static_cast<a2::AstTag>(7), a2::AstFlag::EMPTY, NODE_DWORDS, a2::AstNode::FLAG_FIRST_SIBLING | a2::AstNode::FLAG_LAST_SIBLING | a2::AstNode::FLAG_NO_CHILDREN, NODE_DWORDS });

	push_node(&tree, { static_cast<a2::AstTag>(8), a2::AstFlag::EMPTY, NODE_DWORDS, a2::AstNode::FLAG_LAST_SIBLING, 2 * NODE_DWORDS });

	push_node(&tree, { static_cast<a2::AstTag>(9), a2::AstFlag::EMPTY, NODE_DWORDS, a2::AstNode::FLAG_FIRST_SIBLING | a2::AstNode::FLAG_LAST_SIBLING | a2::AstNode::FLAG_NO_CHILDREN, NODE_DWORDS });

	return tree;
}

static DummyTree double_binary_dummy_tree() noexcept
{
	DummyTree tree;
	tree.index = 0;

	push_node(&tree, { a2::AstTag::OpSub, a2::AstFlag::EMPTY, NODE_DWORDS, a2::AstNode::FLAG_FIRST_SIBLING | a2::AstNode::FLAG_LAST_SIBLING, 7 * NODE_DWORDS });

	push_node(&tree, { a2::AstTag::OpAdd, a2::AstFlag::EMPTY, NODE_DWORDS, a2::AstNode::FLAG_FIRST_SIBLING, 5 * NODE_DWORDS });

	push_node(&tree, { a2::AstTag::ValChar, a2::AstFlag::EMPTY, NODE_DWORDS, a2::AstNode::FLAG_FIRST_SIBLING | a2::AstNode::FLAG_NO_CHILDREN, NODE_DWORDS });

	push_node(&tree, { a2::AstTag::OpMul, a2::AstFlag::EMPTY, NODE_DWORDS, a2::AstNode::FLAG_LAST_SIBLING, 3 * NODE_DWORDS });

	push_node(&tree, { a2::AstTag::ValFloat, a2::AstFlag::EMPTY, NODE_DWORDS, a2::AstNode::FLAG_FIRST_SIBLING | a2::AstNode::FLAG_NO_CHILDREN, NODE_DWORDS });

	push_node(&tree, { a2::AstTag::ValInteger, a2::AstFlag::EMPTY, NODE_DWORDS, a2::AstNode::FLAG_LAST_SIBLING | a2::AstNode::FLAG_NO_CHILDREN, NODE_DWORDS });

	push_node(&tree, { a2::AstTag::ValString, a2::AstFlag::EMPTY, NODE_DWORDS, a2::AstNode::FLAG_LAST_SIBLING | a2::AstNode::FLAG_NO_CHILDREN, NODE_DWORDS });

	return tree;
}

static DummyTree flat_dummy_tree() noexcept
{
	DummyTree tree;
	tree.index = 0;

	push_node(&tree, { a2::AstTag::File, a2::AstFlag::EMPTY, NODE_DWORDS, a2::AstNode::FLAG_FIRST_SIBLING | a2::AstNode::FLAG_LAST_SIBLING, 9 * NODE_DWORDS });

	push_node(&tree, { a2::AstTag::Definition, a2::AstFlag::EMPTY, NODE_DWORDS, a2::AstNode::FLAG_FIRST_SIBLING, 2 * NODE_DWORDS });

	push_node(&tree, { a2::AstTag::ValIdentifer, a2::AstFlag::EMPTY, NODE_DWORDS, a2::AstNode::FLAG_FIRST_SIBLING | a2::AstNode::FLAG_LAST_SIBLING | a2::AstNode::FLAG_NO_CHILDREN, NODE_DWORDS });

	push_node(&tree, { a2::AstTag::Definition, a2::AstFlag::EMPTY, NODE_DWORDS, 0, 2 * NODE_DWORDS });

	push_node(&tree, { a2::AstTag::ValChar, a2::AstFlag::EMPTY, NODE_DWORDS, a2::AstNode::FLAG_FIRST_SIBLING | a2::AstNode::FLAG_LAST_SIBLING | a2::AstNode::FLAG_NO_CHILDREN, NODE_DWORDS });

	push_node(&tree, { a2::AstTag::Definition, a2::AstFlag::EMPTY, NODE_DWORDS, 0, 2 * NODE_DWORDS });

	push_node(&tree, { a2::AstTag::ValFloat, a2::AstFlag::EMPTY, NODE_DWORDS, a2::AstNode::FLAG_FIRST_SIBLING | a2::AstNode::FLAG_LAST_SIBLING | a2::AstNode::FLAG_NO_CHILDREN, NODE_DWORDS });

	push_node(&tree, { a2::AstTag::Definition, a2::AstFlag::EMPTY, NODE_DWORDS, a2::AstNode::FLAG_LAST_SIBLING, 2 * NODE_DWORDS });

	push_node(&tree, { a2::AstTag::ValString, a2::AstFlag::EMPTY, NODE_DWORDS, a2::AstNode::FLAG_FIRST_SIBLING | a2::AstNode::FLAG_LAST_SIBLING | a2::AstNode::FLAG_NO_CHILDREN, NODE_DWORDS });

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

	TEST_EQUAL(a2::has_children(reinterpret_cast<a2::AstNode*>(&tree.dwords)), false);

	TEST_END;
}

static void has_children_with_single_child_is_true() noexcept
{
	TEST_BEGIN;

	DummyTree tree = unary_dummy_tree();

	TEST_EQUAL(a2::has_children(reinterpret_cast<a2::AstNode*>(tree.dwords)), true);

	TEST_END;
}

static void has_children_with_two_children_is_true() noexcept
{
	TEST_BEGIN;

	DummyTree tree = binary_dummy_tree();

	TEST_EQUAL(a2::has_children(reinterpret_cast<a2::AstNode*>(tree.dwords)), true);

	TEST_END;
}



static void child_iterator_with_0_children_has_0_entries() noexcept
{
	TEST_BEGIN;

	DummyTree tree = single_node_dummy_tree();

	a2::AstDirectChildIterator it = a2::direct_children_of(reinterpret_cast<a2::AstNode*>(tree.dwords));

	TEST_EQUAL(a2::next(&it), none<a2::AstNode>());

	TEST_END;
}

static void child_iterator_with_1_child_has_1_entry() noexcept
{
	TEST_BEGIN;

	DummyTree tree = unary_dummy_tree();

	a2::AstDirectChildIterator it = a2::direct_children_of(reinterpret_cast<a2::AstNode*>(tree.dwords));

	TEST_EQUAL(a2::next(&it), some(reinterpret_cast<a2::AstNode*>(tree.dwords) + 1));

	TEST_EQUAL(a2::next(&it), none<a2::AstNode>());

	TEST_END;
}

static void child_iterator_with_5_children_has_5_entries() noexcept
{
	TEST_BEGIN;

	DummyTree tree = nary_dummy_tree(5);

	a2::AstDirectChildIterator it = a2::direct_children_of(reinterpret_cast<a2::AstNode*>(tree.dwords));

	for (u32 i = 0; i != 5; ++i)
		TEST_EQUAL(a2::next(&it), some(reinterpret_cast<a2::AstNode*>(tree.dwords) + i + 1));

	TEST_EQUAL(a2::next(&it), none<a2::AstNode>());

	TEST_END;
}

static void child_iterator_with_grandchildren_only_iterates_direct_children() noexcept
{
	TEST_BEGIN;

	DummyTree tree = complex_dummy_tree();

	a2::AstDirectChildIterator it = a2::direct_children_of(reinterpret_cast<a2::AstNode*>(tree.dwords));

	TEST_EQUAL(a2::next(&it), some(reinterpret_cast<a2::AstNode*>(tree.dwords) + 1));

	TEST_EQUAL(a2::next(&it), some(reinterpret_cast<a2::AstNode*>(tree.dwords) + 4));

	TEST_EQUAL(a2::next(&it), none<a2::AstNode>());

	TEST_END;
}



static void preorder_iterator_with_0_children_has_0_entries() noexcept
{
	TEST_BEGIN;

	DummyTree tree = single_node_dummy_tree();

	a2::AstPreorderIterator it = a2::preorder_ancestors_of(reinterpret_cast<a2::AstNode*>(tree.dwords));

	TEST_EQUAL(a2::is_valid(a2::next(&it)), false);

	TEST_END;
}

static void preorder_iterator_with_1_child_has_1_entry() noexcept
{
	TEST_BEGIN;

	DummyTree tree = unary_dummy_tree();

	a2::AstPreorderIterator it = a2::preorder_ancestors_of(reinterpret_cast<a2::AstNode*>(tree.dwords));

	const a2::AstIterationResult result = a2::next(&it);

	TEST_EQUAL(a2::is_valid(result), true);

	TEST_EQUAL(result.node, reinterpret_cast<a2::AstNode*>(tree.dwords) + 1);

	TEST_EQUAL(result.depth, 0);

	TEST_EQUAL(a2::is_valid(a2::next(&it)), false);

	TEST_END;
}

static void preorder_iterator_with_5_children_has_5_entries() noexcept
{
	TEST_BEGIN;

	DummyTree tree = nary_dummy_tree(5);

	a2::AstPreorderIterator it = a2::preorder_ancestors_of(reinterpret_cast<a2::AstNode*>(tree.dwords));

	for (u32 i = 0; i != 5; ++i)
	{
		const a2::AstIterationResult result = a2::next(&it);

		TEST_EQUAL(a2::is_valid(result), true);

		TEST_EQUAL(result.node, reinterpret_cast<a2::AstNode*>(tree.dwords) + i + 1);

		TEST_EQUAL(result.depth, 0);
	}

	TEST_EQUAL(a2::is_valid(a2::next(&it)), false);

	TEST_END;
}

static void preorder_iterator_with_grandchildren_iterates_grandchildren() noexcept
{
	TEST_BEGIN;

	DummyTree tree = complex_dummy_tree();

	a2::AstPreorderIterator it = a2::preorder_ancestors_of(reinterpret_cast<a2::AstNode*>(tree.dwords));

	static constexpr u32 expected_depths[] = { 0, 1, 1, 0, 1, 2, 1, 2 };

	for (u32 i = 0; i != 8; ++i)
	{
		const a2::AstIterationResult result = a2::next(&it);

		TEST_EQUAL(a2::is_valid(result), true);

		TEST_EQUAL(result.node, reinterpret_cast<a2::AstNode*>(tree.dwords) + i + 1);

		TEST_EQUAL(result.depth, expected_depths[i]);
	}

	TEST_EQUAL(a2::is_valid(a2::next(&it)), false);

	TEST_END;
}

static void preorder_iterator_with_flat_tree_iterates_subtrees() noexcept
{
	TEST_BEGIN;

	DummyTree tree = flat_dummy_tree();

	a2::AstPreorderIterator it = a2::preorder_ancestors_of(reinterpret_cast<a2::AstNode*>(tree.dwords));

	static constexpr u32 expected_depths[] = {
		0, 1,
		0, 1,
		0, 1,
		0, 1
	};

	for (u32 i = 0; i != 8; ++i)
	{
		const a2::AstIterationResult result = a2::next(&it);

		TEST_EQUAL(a2::is_valid(result), true);

		TEST_EQUAL(result.node, reinterpret_cast<a2::AstNode*>(tree.dwords) + i + 1);

		TEST_EQUAL(result.depth, expected_depths[i]);
	}

	TEST_EQUAL(a2::is_valid(a2::next(&it)), false);

	TEST_END;
}



static void postorder_iterator_with_0_children_has_0_entries() noexcept
{
	TEST_BEGIN;

	DummyTree tree = single_node_dummy_tree();

	a2::AstPostorderIterator it = a2::postorder_ancestors_of(reinterpret_cast<a2::AstNode*>(tree.dwords));

	TEST_EQUAL(a2::is_valid(a2::next(&it)), false);

	TEST_END;
}

static void postorder_iterator_with_1_child_has_1_entry() noexcept
{
	TEST_BEGIN;

	DummyTree tree = unary_dummy_tree();

	a2::AstPostorderIterator it = a2::postorder_ancestors_of(reinterpret_cast<a2::AstNode*>(tree.dwords));

	TEST_EQUAL(a2::next(&it).node, reinterpret_cast<a2::AstNode*>(tree.dwords) + 1);

	TEST_EQUAL(a2::is_valid(a2::next(&it)), false);

	TEST_END;
}

static void postorder_iterator_with_5_children_has_5_entries() noexcept
{
	TEST_BEGIN;

	DummyTree tree = nary_dummy_tree(5);

	a2::AstPostorderIterator it = a2::postorder_ancestors_of(reinterpret_cast<a2::AstNode*>(tree.dwords));

	for (u32 i = 0; i != 5; ++i)
		TEST_EQUAL(a2::next(&it).node, reinterpret_cast<a2::AstNode*>(tree.dwords) + i + 1);

	TEST_EQUAL(a2::is_valid(a2::next(&it)), false);

	TEST_END;
}

static void postorder_iterator_with_grandchildren_iterates_grandchildren() noexcept
{
	TEST_BEGIN;

	DummyTree tree = complex_dummy_tree();

	a2::AstPostorderIterator it = a2::postorder_ancestors_of(reinterpret_cast<a2::AstNode*>(tree.dwords));

	TEST_EQUAL(a2::next(&it).node, reinterpret_cast<a2::AstNode*>(tree.dwords) + 2);

	TEST_EQUAL(a2::next(&it).node, reinterpret_cast<a2::AstNode*>(tree.dwords) + 3);

	TEST_EQUAL(a2::next(&it).node, reinterpret_cast<a2::AstNode*>(tree.dwords) + 1);

	TEST_EQUAL(a2::next(&it).node, reinterpret_cast<a2::AstNode*>(tree.dwords) + 6);

	TEST_EQUAL(a2::next(&it).node, reinterpret_cast<a2::AstNode*>(tree.dwords) + 5);

	TEST_EQUAL(a2::next(&it).node, reinterpret_cast<a2::AstNode*>(tree.dwords) + 8);

	TEST_EQUAL(a2::next(&it).node, reinterpret_cast<a2::AstNode*>(tree.dwords) + 7);

	TEST_EQUAL(a2::next(&it).node, reinterpret_cast<a2::AstNode*>(tree.dwords) + 4);

	TEST_EQUAL(a2::is_valid(a2::next(&it)), false);

	TEST_END;
}



static void push_node_once_appends_node() noexcept
{
	TEST_BEGIN;

	a2::AstBuilder builder = a2::create_ast_builder();

	a2::push_node(&builder, a2::AstBuilder::NO_CHILDREN, a2::AstTag::File, a2::AstFlag::EMPTY);

	DummyTree expected_tree = single_node_dummy_tree();

	a2::AstNode* const actual = reinterpret_cast<a2::AstNode*>(builder.scratch.begin());

	a2::AstNode* const expected = reinterpret_cast<a2::AstNode*>(expected_tree.dwords);

	TEST_EQUAL(actual->tag, expected->tag);

	TEST_EQUAL(actual->flags, expected->flags);

	TEST_EQUAL(actual->data_dwords, expected->data_dwords);

	TEST_MEM_EQUAL(actual + 1, expected + 1, actual->data_dwords * sizeof(u32) - sizeof(a2::AstNode));

	builder.scratch.release();

	TEST_END;
}

static void push_node_once_and_complete_appends_node() noexcept
{
	TEST_BEGIN;

	a2::AstBuilder builder = a2::create_ast_builder();

	a2::push_node(&builder, a2::AstBuilder::NO_CHILDREN, a2::AstTag::File, a2::AstFlag::EMPTY);

	MockedPools pools = create_mocked_pools();

	a2::AstNode* const root = a2::complete_ast(&builder, pools.asts);

	DummyTree expected = single_node_dummy_tree();

	TEST_MEM_EQUAL(root, expected.dwords, sizeof(a2::AstNode));

	builder.scratch.release();

	release_mocked_pools(pools);

	TEST_END;
}

static void push_node_with_unary_op_and_complete_reverses_tree() noexcept
{
	TEST_BEGIN;

	a2::AstBuilder builder = a2::create_ast_builder();

	const a2::AstBuilderToken token = a2::push_node(&builder, a2::AstBuilder::NO_CHILDREN, a2::AstTag::Block, a2::AstFlag::EMPTY);

	a2::push_node(&builder, token, a2::AstTag::File, a2::AstFlag::EMPTY);

	MockedPools pools = create_mocked_pools();

	a2::AstNode* const root = a2::complete_ast(&builder, pools.asts);

	DummyTree expected = unary_dummy_tree();

	TEST_MEM_EQUAL(root, expected.dwords, 2 * sizeof(a2::AstNode));

	builder.scratch.release();

	release_mocked_pools(pools);

	TEST_END;
}

static void push_node_with_binary_op_and_complete_reverses_tree() noexcept
{
	TEST_BEGIN;

	a2::AstBuilder builder = a2::create_ast_builder();

	const a2::AstBuilderToken token = a2::push_node(&builder, a2::AstBuilder::NO_CHILDREN, a2::AstTag::ValChar, a2::AstFlag::EMPTY);

	a2::push_node(&builder, a2::AstBuilder::NO_CHILDREN, a2::AstTag::ValIdentifer, a2::AstFlag::EMPTY);

	a2::push_node(&builder, token, a2::AstTag::OpBitAnd, a2::AstFlag::EMPTY);

	MockedPools pools = create_mocked_pools();

	a2::AstNode* const root = a2::complete_ast(&builder, pools.asts);

	DummyTree expected = binary_dummy_tree();

	TEST_MEM_EQUAL(root, expected.dwords, 3 * sizeof(a2::AstNode));

	builder.scratch.release();

	release_mocked_pools(pools);

	TEST_END;
}

static void push_node_with_complex_tree_and_complete_reverses_tree() noexcept
{
	TEST_BEGIN;

	a2::AstBuilder builder = a2::create_ast_builder();

	const a2::AstBuilderToken t3 = a2::push_node(&builder, a2::AstBuilder::NO_CHILDREN, static_cast<a2::AstTag>(3), a2::AstFlag::EMPTY);

	a2::push_node(&builder, a2::AstBuilder::NO_CHILDREN, static_cast<a2::AstTag>(4), a2::AstFlag::EMPTY);

	const a2::AstBuilderToken t2 = a2::push_node(&builder, t3, static_cast<a2::AstTag>(2), a2::AstFlag::EMPTY);

	const a2::AstBuilderToken t7 = a2::push_node(&builder, a2::AstBuilder::NO_CHILDREN, static_cast<a2::AstTag>(7), a2::AstFlag::EMPTY);

	const a2::AstBuilderToken t6 = a2::push_node(&builder, t7, static_cast<a2::AstTag>(6), a2::AstFlag::EMPTY);

	const a2::AstBuilderToken t9 = a2::push_node(&builder, a2::AstBuilder::NO_CHILDREN, static_cast<a2::AstTag>(9), a2::AstFlag::EMPTY);

	a2::push_node(&builder, t9, static_cast<a2::AstTag>(8), a2::AstFlag::EMPTY);

	a2::push_node(&builder, t6, static_cast<a2::AstTag>(5), a2::AstFlag::EMPTY);

	a2::push_node(&builder, t2, static_cast<a2::AstTag>(1), a2::AstFlag::EMPTY);

	MockedPools pools = create_mocked_pools();

	a2::AstNode* const root = a2::complete_ast(&builder, pools.asts);

	DummyTree expected = complex_dummy_tree();

	TEST_MEM_EQUAL(root, expected.dwords, 9 * sizeof(a2::AstNode));

	builder.scratch.release();

	release_mocked_pools(pools);

	TEST_END;
}

static void push_node_with_double_binary_tree_and_complete_reverses_tree() noexcept
{
	TEST_BEGIN;

	a2::AstBuilder builder = a2::create_ast_builder();

	const a2::AstBuilderToken add = a2::push_node(&builder, a2::AstBuilder::NO_CHILDREN, a2::AstTag::ValChar, a2::AstFlag::EMPTY);

	const a2::AstBuilderToken mul = a2::push_node(&builder, a2::AstBuilder::NO_CHILDREN, a2::AstTag::ValFloat, a2::AstFlag::EMPTY);

	a2::push_node(&builder, a2::AstBuilder::NO_CHILDREN, a2::AstTag::ValInteger, a2::AstFlag::EMPTY);

	a2::push_node(&builder, mul, a2::AstTag::OpMul, a2::AstFlag::EMPTY);

	const a2::AstBuilderToken sub = a2::push_node(&builder, add, a2::AstTag::OpAdd, a2::AstFlag::EMPTY);

	a2::push_node(&builder, a2::AstBuilder::NO_CHILDREN, a2::AstTag::ValString, a2::AstFlag::EMPTY);

	a2::push_node(&builder, sub, a2::AstTag::OpSub, a2::AstFlag::EMPTY);

	MockedPools pools = create_mocked_pools();

	a2::AstNode* const root = a2::complete_ast(&builder, pools.asts);

	DummyTree expected = double_binary_dummy_tree();

	TEST_MEM_EQUAL(root, expected.dwords, 7 * sizeof(a2::AstNode));

	builder.scratch.release();

	release_mocked_pools(pools);

	TEST_END;
}


void ast2_tests() noexcept
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


	push_node_once_appends_node();

	push_node_once_and_complete_appends_node();

	push_node_with_unary_op_and_complete_reverses_tree();

	push_node_with_binary_op_and_complete_reverses_tree();

	push_node_with_complex_tree_and_complete_reverses_tree();

	push_node_with_double_binary_tree_and_complete_reverses_tree();

	TEST_MODULE_END;
}
