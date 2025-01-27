#include "test_helpers.hpp"
#include "../ast2.hpp"

struct DummyTree
{
	u32 index;

	u32 dwords[32];

};

static constexpr u8 NODE_DWORDS = sizeof(a2::Node) / sizeof(u32);

static void push_node(DummyTree* tree, a2::Node node, u8 data_dwords = 0, const void* data = nullptr) noexcept
{
	const u32 required_dwords = NODE_DWORDS + data_dwords;

	if (tree->index + required_dwords > array_count(tree->dwords))
		panic("Testing dummy tree too large");

	memcpy(tree->dwords + tree->index, &node, sizeof(a2::Node));

	memcpy(tree->dwords + tree->index + NODE_DWORDS, data, data_dwords * sizeof(u32));

	tree->index += required_dwords;
}

static DummyTree single_node_dummy_tree() noexcept
{
	DummyTree tree;
	tree.index = 0;

	push_node(&tree, { a2::Tag::Program, a2::Flag::EMPTY, NODE_DWORDS, a2::Node::FLAG_FIRST_SIBLING | a2::Node::FLAG_LAST_SIBLING | a2::Node::FLAG_NO_CHILDREN, NODE_DWORDS });

	return tree;
}

static DummyTree unary_dummy_tree() noexcept
{
	DummyTree tree;
	tree.index = 0;

	push_node(&tree, { a2::Tag::Program, a2::Flag::EMPTY, NODE_DWORDS, a2::Node::FLAG_FIRST_SIBLING | a2::Node::FLAG_LAST_SIBLING, 2 * NODE_DWORDS });

	push_node(&tree, { a2::Tag::Block, a2::Flag::EMPTY, NODE_DWORDS, a2::Node::FLAG_FIRST_SIBLING | a2::Node::FLAG_LAST_SIBLING | a2::Node::FLAG_NO_CHILDREN, NODE_DWORDS });

	return tree;
}

static DummyTree binary_dummy_tree() noexcept
{
	DummyTree tree;
	tree.index = 0;

	push_node(&tree, { a2::Tag::OpBitAnd, a2::Flag::EMPTY, NODE_DWORDS, a2::Node::FLAG_FIRST_SIBLING | a2::Node::FLAG_LAST_SIBLING, 3 * NODE_DWORDS });

	push_node(&tree, { a2::Tag::ValChar, a2::Flag::EMPTY, NODE_DWORDS, a2::Node::FLAG_FIRST_SIBLING | a2::Node::FLAG_NO_CHILDREN, NODE_DWORDS });

	push_node(&tree, { a2::Tag::ValIdentifer, a2::Flag::EMPTY, NODE_DWORDS, a2::Node::FLAG_LAST_SIBLING | a2::Node::FLAG_NO_CHILDREN, NODE_DWORDS });

	return tree;
}

static DummyTree nary_dummy_tree(u32 n) noexcept
{
	ASSERT_OR_IGNORE(n != 0);

	DummyTree tree;
	tree.index = 0;

	push_node(&tree, { a2::Tag::Program, a2::Flag::EMPTY, NODE_DWORDS, a2::Node::FLAG_FIRST_SIBLING | a2::Node::FLAG_LAST_SIBLING, NODE_DWORDS * (n + 1) });

	for (u32 i = 0; i != n; ++i)
	{
		const u8 internal_flags = static_cast<u8>((i == 0 ? a2::Node::FLAG_FIRST_SIBLING : 0) | (i == n - 1 ? a2::Node::FLAG_LAST_SIBLING : 0) | a2::Node::FLAG_NO_CHILDREN);

		push_node(&tree, { a2::Tag::Block, a2::Flag::EMPTY, NODE_DWORDS, internal_flags, NODE_DWORDS });
	}

	return tree;
}

static DummyTree complex_dummy_tree() noexcept
{
	DummyTree tree;
	tree.index = 0;

	push_node(&tree, { static_cast<a2::Tag>(1), a2::Flag::EMPTY, NODE_DWORDS, a2::Node::FLAG_FIRST_SIBLING | a2::Node::FLAG_LAST_SIBLING, 9 * NODE_DWORDS });
	
	push_node(&tree, { static_cast<a2::Tag>(2), a2::Flag::EMPTY, NODE_DWORDS, a2::Node::FLAG_FIRST_SIBLING, 3 * NODE_DWORDS });

	push_node(&tree, { static_cast<a2::Tag>(3), a2::Flag::EMPTY, NODE_DWORDS, a2::Node::FLAG_FIRST_SIBLING | a2::Node::FLAG_NO_CHILDREN, NODE_DWORDS });

	push_node(&tree, { static_cast<a2::Tag>(4), a2::Flag::EMPTY, NODE_DWORDS, a2::Node::FLAG_LAST_SIBLING | a2::Node::FLAG_NO_CHILDREN, NODE_DWORDS });

	push_node(&tree, { static_cast<a2::Tag>(5), a2::Flag::EMPTY, NODE_DWORDS, a2::Node::FLAG_LAST_SIBLING, 5 * NODE_DWORDS });

	push_node(&tree, { static_cast<a2::Tag>(6), a2::Flag::EMPTY, NODE_DWORDS, a2::Node::FLAG_FIRST_SIBLING, 2 * NODE_DWORDS });

	push_node(&tree, { static_cast<a2::Tag>(7), a2::Flag::EMPTY, NODE_DWORDS, a2::Node::FLAG_FIRST_SIBLING | a2::Node::FLAG_LAST_SIBLING | a2::Node::FLAG_NO_CHILDREN, NODE_DWORDS });

	push_node(&tree, { static_cast<a2::Tag>(8), a2::Flag::EMPTY, NODE_DWORDS, a2::Node::FLAG_LAST_SIBLING, 2 * NODE_DWORDS });

	push_node(&tree, { static_cast<a2::Tag>(9), a2::Flag::EMPTY, NODE_DWORDS, a2::Node::FLAG_FIRST_SIBLING | a2::Node::FLAG_LAST_SIBLING | a2::Node::FLAG_NO_CHILDREN, NODE_DWORDS });

	return tree;
}

static DummyTree double_binary_dummy_tree() noexcept
{
	DummyTree tree;
	tree.index = 0;

	push_node(&tree, { a2::Tag::OpSub, a2::Flag::EMPTY, NODE_DWORDS, a2::Node::FLAG_FIRST_SIBLING | a2::Node::FLAG_LAST_SIBLING, 7 * NODE_DWORDS });

	push_node(&tree, { a2::Tag::OpAdd, a2::Flag::EMPTY, NODE_DWORDS, a2::Node::FLAG_FIRST_SIBLING, 5 * NODE_DWORDS });

	push_node(&tree, { a2::Tag::ValChar, a2::Flag::EMPTY, NODE_DWORDS, a2::Node::FLAG_FIRST_SIBLING | a2::Node::FLAG_NO_CHILDREN, NODE_DWORDS });

	push_node(&tree, { a2::Tag::OpMul, a2::Flag::EMPTY, NODE_DWORDS, a2::Node::FLAG_LAST_SIBLING, 3 * NODE_DWORDS });

	push_node(&tree, { a2::Tag::ValFloat, a2::Flag::EMPTY, NODE_DWORDS, a2::Node::FLAG_FIRST_SIBLING | a2::Node::FLAG_NO_CHILDREN, NODE_DWORDS });

	push_node(&tree, { a2::Tag::ValInteger, a2::Flag::EMPTY, NODE_DWORDS, a2::Node::FLAG_LAST_SIBLING | a2::Node::FLAG_NO_CHILDREN, NODE_DWORDS });

	push_node(&tree, { a2::Tag::ValString, a2::Flag::EMPTY, NODE_DWORDS, a2::Node::FLAG_LAST_SIBLING | a2::Node::FLAG_NO_CHILDREN, NODE_DWORDS });

	return tree;
}



static void has_children_on_single_node_is_false() noexcept
{
	TEST_BEGIN;

	DummyTree tree = single_node_dummy_tree();

	TEST_EQUAL(a2::has_children(reinterpret_cast<a2::Node*>(&tree.dwords)), false);

	TEST_END;
}

static void has_children_with_single_child_is_true() noexcept
{
	TEST_BEGIN;

	DummyTree tree = unary_dummy_tree();

	TEST_EQUAL(a2::has_children(reinterpret_cast<a2::Node*>(tree.dwords)), true);

	TEST_END;
}

static void has_children_with_two_children_is_true() noexcept
{
	TEST_BEGIN;

	DummyTree tree = binary_dummy_tree();

	TEST_EQUAL(a2::has_children(reinterpret_cast<a2::Node*>(tree.dwords)), true);

	TEST_END;
}



static void child_iterator_with_0_children_has_0_entries() noexcept
{
	TEST_BEGIN;

	DummyTree tree = single_node_dummy_tree();

	a2::NodeFlatIterator it = a2::direct_children_of(reinterpret_cast<a2::Node*>(tree.dwords));

	TEST_EQUAL(a2::next(&it), none<a2::Node>());

	TEST_END;
}

static void child_iterator_with_1_child_has_1_entry() noexcept
{
	TEST_BEGIN;

	DummyTree tree = unary_dummy_tree();

	a2::NodeFlatIterator it = a2::direct_children_of(reinterpret_cast<a2::Node*>(tree.dwords));

	TEST_EQUAL(a2::next(&it), some(reinterpret_cast<a2::Node*>(tree.dwords) + 1));

	TEST_EQUAL(a2::next(&it), none<a2::Node>());

	TEST_END;
}

static void child_iterator_with_5_children_has_5_entries() noexcept
{
	TEST_BEGIN;

	DummyTree tree = nary_dummy_tree(5);

	a2::NodeFlatIterator it = a2::direct_children_of(reinterpret_cast<a2::Node*>(tree.dwords));

	for (u32 i = 0; i != 5; ++i)
		TEST_EQUAL(a2::next(&it), some(reinterpret_cast<a2::Node*>(tree.dwords) + i + 1));

	TEST_EQUAL(a2::next(&it), none<a2::Node>());

	TEST_END;
}

static void child_iterator_with_grandchildren_only_iterates_direct_children() noexcept
{
	TEST_BEGIN;

	DummyTree tree = complex_dummy_tree();

	a2::NodeFlatIterator it = a2::direct_children_of(reinterpret_cast<a2::Node*>(tree.dwords));

	TEST_EQUAL(a2::next(&it), some(reinterpret_cast<a2::Node*>(tree.dwords) + 1));

	TEST_EQUAL(a2::next(&it), some(reinterpret_cast<a2::Node*>(tree.dwords) + 4));

	TEST_EQUAL(a2::next(&it), none<a2::Node>());

	TEST_END;
}



static void preorder_iterator_with_0_children_has_0_entries() noexcept
{
	TEST_BEGIN;

	DummyTree tree = single_node_dummy_tree();

	a2::NodePreorderIterator it = a2::preorder_ancestors_of(reinterpret_cast<a2::Node*>(tree.dwords));

	TEST_EQUAL(a2::is_valid(a2::next(&it)), false);

	TEST_END;
}

static void preorder_iterator_with_1_child_has_1_entry() noexcept
{
	TEST_BEGIN;

	DummyTree tree = unary_dummy_tree();

	a2::NodePreorderIterator it = a2::preorder_ancestors_of(reinterpret_cast<a2::Node*>(tree.dwords));

	const a2::IterationResult result = a2::next(&it);

	TEST_EQUAL(a2::is_valid(result), true);

	TEST_EQUAL(result.node, reinterpret_cast<a2::Node*>(tree.dwords) + 1);

	TEST_EQUAL(result.depth, 0);

	TEST_EQUAL(a2::is_valid(a2::next(&it)), false);

	TEST_END;
}

static void preorder_iterator_with_5_children_has_5_entries() noexcept
{
	TEST_BEGIN;

	DummyTree tree = nary_dummy_tree(5);

	a2::NodePreorderIterator it = a2::preorder_ancestors_of(reinterpret_cast<a2::Node*>(tree.dwords));

	for (u32 i = 0; i != 5; ++i)
	{
		const a2::IterationResult result = a2::next(&it);

		TEST_EQUAL(a2::is_valid(result), true);

		TEST_EQUAL(result.node, reinterpret_cast<a2::Node*>(tree.dwords) + i + 1);

		TEST_EQUAL(result.depth, 0);
	}

	TEST_EQUAL(a2::is_valid(a2::next(&it)), false);

	TEST_END;
}

static void preorder_iterator_with_grandchildren_iterates_grandchildren() noexcept
{
	TEST_BEGIN;

	DummyTree tree = complex_dummy_tree();

	a2::NodePreorderIterator it = a2::preorder_ancestors_of(reinterpret_cast<a2::Node*>(tree.dwords));

	static constexpr u32 expected_depths[] = { 0, 1, 1, 0, 1, 2, 1, 2 };

	for (u32 i = 0; i != 8; ++i)
	{
		const a2::IterationResult result = a2::next(&it);

		TEST_EQUAL(a2::is_valid(result), true);

		TEST_EQUAL(result.node, reinterpret_cast<a2::Node*>(tree.dwords) + i + 1);

		TEST_EQUAL(result.depth, expected_depths[i]);
	}

	TEST_EQUAL(a2::is_valid(a2::next(&it)), false);

	TEST_END;
}



static void postorder_iterator_with_0_children_has_0_entries() noexcept
{
	TEST_BEGIN;

	DummyTree tree = single_node_dummy_tree();

	a2::NodePostorderIterator it = a2::postorder_ancestors_of(reinterpret_cast<a2::Node*>(tree.dwords));

	TEST_EQUAL(a2::is_valid(a2::next(&it)), false);

	TEST_END;
}

static void postorder_iterator_with_1_child_has_1_entry() noexcept
{
	TEST_BEGIN;

	DummyTree tree = unary_dummy_tree();

	a2::NodePostorderIterator it = a2::postorder_ancestors_of(reinterpret_cast<a2::Node*>(tree.dwords));

	TEST_EQUAL(a2::next(&it).node, reinterpret_cast<a2::Node*>(tree.dwords) + 1);

	TEST_EQUAL(a2::is_valid(a2::next(&it)), false);

	TEST_END;
}

static void postorder_iterator_with_5_children_has_5_entries() noexcept
{
	TEST_BEGIN;

	DummyTree tree = nary_dummy_tree(5);

	a2::NodePostorderIterator it = a2::postorder_ancestors_of(reinterpret_cast<a2::Node*>(tree.dwords));

	for (u32 i = 0; i != 5; ++i)
		TEST_EQUAL(a2::next(&it).node, reinterpret_cast<a2::Node*>(tree.dwords) + i + 1);

	TEST_EQUAL(a2::is_valid(a2::next(&it)), false);

	TEST_END;
}

static void postorder_iterator_with_grandchildren_iterates_grandchildren() noexcept
{
	TEST_BEGIN;

	DummyTree tree = complex_dummy_tree();

	a2::NodePostorderIterator it = a2::postorder_ancestors_of(reinterpret_cast<a2::Node*>(tree.dwords));

	TEST_EQUAL(a2::next(&it).node, reinterpret_cast<a2::Node*>(tree.dwords) + 2);

	TEST_EQUAL(a2::next(&it).node, reinterpret_cast<a2::Node*>(tree.dwords) + 3);

	TEST_EQUAL(a2::next(&it).node, reinterpret_cast<a2::Node*>(tree.dwords) + 1);

	TEST_EQUAL(a2::next(&it).node, reinterpret_cast<a2::Node*>(tree.dwords) + 6);

	TEST_EQUAL(a2::next(&it).node, reinterpret_cast<a2::Node*>(tree.dwords) + 5);

	TEST_EQUAL(a2::next(&it).node, reinterpret_cast<a2::Node*>(tree.dwords) + 8);

	TEST_EQUAL(a2::next(&it).node, reinterpret_cast<a2::Node*>(tree.dwords) + 7);

	TEST_EQUAL(a2::next(&it).node, reinterpret_cast<a2::Node*>(tree.dwords) + 4);

	TEST_EQUAL(a2::is_valid(a2::next(&it)), false);

	TEST_END;
}



static void push_node_once_appends_node() noexcept
{
	TEST_BEGIN;

	a2::Builder builder = a2::create_ast_builder();

	a2::push_node(&builder, a2::Builder::NO_CHILDREN, a2::Tag::Program, a2::Flag::EMPTY);

	DummyTree expected_tree = single_node_dummy_tree();

	a2::Node* const actual = reinterpret_cast<a2::Node*>(builder.scratch.begin());

	a2::Node* const expected = reinterpret_cast<a2::Node*>(expected_tree.dwords);

	TEST_EQUAL(actual->tag, expected->tag);

	TEST_EQUAL(actual->flags, expected->flags);

	TEST_EQUAL(actual->data_dwords, expected->data_dwords);

	TEST_MEM_EQUAL(actual + 1, expected + 1, actual->data_dwords * sizeof(u32) - sizeof(a2::Node));

	builder.scratch.release();

	TEST_END;
}

static void push_node_once_and_complete_appends_node() noexcept
{
	TEST_BEGIN;

	a2::Builder builder = a2::create_ast_builder();

	a2::push_node(&builder, a2::Builder::NO_CHILDREN, a2::Tag::Program, a2::Flag::EMPTY);

	ReservedVec<u32> dst;
	dst.init(4096, 4096);

	a2::Node* const root = a2::complete_ast(&builder, &dst);

	DummyTree expected = single_node_dummy_tree();

	TEST_MEM_EQUAL(root, expected.dwords, sizeof(a2::Node));

	builder.scratch.release();

	dst.release();

	TEST_END;
}

static void push_node_with_unary_op_and_complete_reverses_tree() noexcept
{
	TEST_BEGIN;

	a2::Builder builder = a2::create_ast_builder();

	const a2::BuilderToken token = a2::push_node(&builder, a2::Builder::NO_CHILDREN, a2::Tag::Block, a2::Flag::EMPTY);

	a2::push_node(&builder, token, a2::Tag::Program, a2::Flag::EMPTY);

	ReservedVec<u32> dst;
	dst.init(4096, 4096);

	a2::Node* const root = a2::complete_ast(&builder, &dst);

	DummyTree expected = unary_dummy_tree();

	TEST_MEM_EQUAL(root, expected.dwords, 2 * sizeof(a2::Node));

	builder.scratch.release();

	dst.release();

	TEST_END;
}

static void push_node_with_binary_op_and_complete_reverses_tree() noexcept
{
	TEST_BEGIN;

	a2::Builder builder = a2::create_ast_builder();

	const a2::BuilderToken token = a2::push_node(&builder, a2::Builder::NO_CHILDREN, a2::Tag::ValChar, a2::Flag::EMPTY);

	a2::push_node(&builder, a2::Builder::NO_CHILDREN, a2::Tag::ValIdentifer, a2::Flag::EMPTY);

	a2::push_node(&builder, token, a2::Tag::OpBitAnd, a2::Flag::EMPTY);

	ReservedVec<u32> dst;
	dst.init(4096, 4096);

	a2::Node* const root = a2::complete_ast(&builder, &dst);

	DummyTree expected = binary_dummy_tree();

	TEST_MEM_EQUAL(root, expected.dwords, 3 * sizeof(a2::Node));

	builder.scratch.release();

	dst.release();

	TEST_END;
}

static void push_node_with_complex_tree_and_complete_reverses_tree() noexcept
{
	TEST_BEGIN;

	a2::Builder builder = a2::create_ast_builder();

	const a2::BuilderToken t3 = a2::push_node(&builder, a2::Builder::NO_CHILDREN, static_cast<a2::Tag>(3), a2::Flag::EMPTY);

	a2::push_node(&builder, a2::Builder::NO_CHILDREN, static_cast<a2::Tag>(4), a2::Flag::EMPTY);

	const a2::BuilderToken t2 = a2::push_node(&builder, t3, static_cast<a2::Tag>(2), a2::Flag::EMPTY);

	const a2::BuilderToken t7 = a2::push_node(&builder, a2::Builder::NO_CHILDREN, static_cast<a2::Tag>(7), a2::Flag::EMPTY);

	const a2::BuilderToken t6 = a2::push_node(&builder, t7, static_cast<a2::Tag>(6), a2::Flag::EMPTY);

	const a2::BuilderToken t9 = a2::push_node(&builder, a2::Builder::NO_CHILDREN, static_cast<a2::Tag>(9), a2::Flag::EMPTY);

	a2::push_node(&builder, t9, static_cast<a2::Tag>(8), a2::Flag::EMPTY);

	a2::push_node(&builder, t6, static_cast<a2::Tag>(5), a2::Flag::EMPTY);

	a2::push_node(&builder, t2, static_cast<a2::Tag>(1), a2::Flag::EMPTY);

	ReservedVec<u32> dst;
	dst.init(4096, 4096);

	a2::Node* const root = a2::complete_ast(&builder, &dst);

	DummyTree expected = complex_dummy_tree();

	TEST_MEM_EQUAL(root, expected.dwords, 9 * sizeof(a2::Node));

	builder.scratch.release();

	dst.release();

	TEST_END;
}

static void push_node_with_double_binary_tree_and_complete_reverses_tree() noexcept
{
	TEST_BEGIN;

	a2::Builder builder = a2::create_ast_builder();

	const a2::BuilderToken add = a2::push_node(&builder, a2::Builder::NO_CHILDREN, a2::Tag::ValChar, a2::Flag::EMPTY);

	const a2::BuilderToken mul = a2::push_node(&builder, a2::Builder::NO_CHILDREN, a2::Tag::ValFloat, a2::Flag::EMPTY);

	a2::push_node(&builder, a2::Builder::NO_CHILDREN, a2::Tag::ValInteger, a2::Flag::EMPTY);

	a2::push_node(&builder, mul, a2::Tag::OpMul, a2::Flag::EMPTY);

	const a2::BuilderToken sub = a2::push_node(&builder, add, a2::Tag::OpAdd, a2::Flag::EMPTY);

	a2::push_node(&builder, a2::Builder::NO_CHILDREN, a2::Tag::ValString, a2::Flag::EMPTY);

	a2::push_node(&builder, sub, a2::Tag::OpSub, a2::Flag::EMPTY);

	ReservedVec<u32> dst;
	dst.init(4096, 4096);

	a2::Node* const root = a2::complete_ast(&builder, &dst);

	DummyTree expected = double_binary_dummy_tree();

	TEST_MEM_EQUAL(root, expected.dwords, 7 * sizeof(a2::Node));

	builder.scratch.release();

	dst.release();

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
