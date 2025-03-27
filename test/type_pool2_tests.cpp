#include "test_helpers.hpp"
#include "../pass_data.hpp"

struct DummyTypePool
{
	TypePool2* types;

	AllocPool* alloc;
};

static DummyTypePool create_dummy_types() noexcept
{
	AllocPool* const alloc = create_alloc_pool(1 << 12, 1 << 12);

	TypePool2* const types = create_type_pool2(alloc);

	return { types, alloc };

}

static void release_dummy_types(DummyTypePool dummy) noexcept
{
	release_type_pool2(dummy.types);

	release_alloc_pool(dummy.alloc);
}


static void create_ast_pool_returns_ast_pool() noexcept
{
	TEST_BEGIN;

	AllocPool* const alloc = create_alloc_pool(1 << 12, 1 << 12);

	TypePool2* const types = create_type_pool2(alloc);

	TEST_UNEQUAL(types, nullptr);

	release_type_pool2(types);

	release_alloc_pool(alloc);

	TEST_END;
}

static void type_entry_from_primitive_type_with_integer_returns_integer_type() noexcept
{
	TEST_BEGIN;

	DummyTypePool dummy = create_dummy_types();

	IntegerType2 u16_type{ 16, false };

	TypeEntry2* const entry = type_entry_from_primitive_type(dummy.types, TypeTag::Integer, range::from_object_bytes(&u16_type));

	TEST_UNEQUAL(id_from_type_entry(dummy.types, entry), INVALID_TYPE_ID_2);

	TEST_EQUAL(entry->tag, TypeTag::Integer);

	TEST_EQUAL(entry->bytes, 0);

	TEST_EQUAL(data<IntegerType2>(entry)->bits, 16);

	TEST_EQUAL(data<IntegerType2>(entry)->is_signed, false);

	release_dummy_types(dummy);

	TEST_END;
}

static void type_entry_from_primitive_type_with_integer_twice_returns_same_type_twice() noexcept
{
	TEST_BEGIN;

	DummyTypePool dummy = create_dummy_types();

	IntegerType2 s32_a{ 32, true };

	TypeEntry2* const entry_a = type_entry_from_primitive_type(dummy.types, TypeTag::Integer, range::from_object_bytes(&s32_a));

	TEST_EQUAL(entry_a->tag, TypeTag::Integer);

	TEST_EQUAL(entry_a->bytes, 0);

	TEST_EQUAL(data<IntegerType2>(entry_a)->bits, 32);

	TEST_EQUAL(data<IntegerType2>(entry_a)->is_signed, true);

	IntegerType2 s32_b{ 32, true };

	TypeEntry2* const entry_b = type_entry_from_primitive_type(dummy.types, TypeTag::Integer, range::from_object_bytes(&s32_b));

	TEST_EQUAL(entry_b->tag, TypeTag::Integer);

	TEST_EQUAL(entry_b->bytes, 0);

	TEST_EQUAL(data<IntegerType2>(entry_b)->bits, 32);

	TEST_EQUAL(data<IntegerType2>(entry_b)->is_signed, true);

	TEST_EQUAL(entry_a, entry_b);

	release_dummy_types(dummy);

	TEST_END;
}

static void type_entry_from_primitive_type_with_integer_and_float_with_same_bit_pattern_returns_different_types() noexcept
{
	TEST_BEGIN;

	DummyTypePool dummy = create_dummy_types();

	IntegerType2 u32_type{ 32 };

	TypeEntry2* const u32_entry = type_entry_from_primitive_type(dummy.types, TypeTag::Integer, range::from_object_bytes(&u32_type));

	TEST_EQUAL(u32_entry->tag, TypeTag::Integer);

	TEST_EQUAL(u32_entry->bytes, 0);

	TEST_EQUAL(data<IntegerType2>(u32_entry)->bits, 32);

	TEST_EQUAL(data<IntegerType2>(u32_entry)->is_signed, false);

	FloatType2 f32_type{ 32 };

	TypeEntry2* const f32_entry = type_entry_from_primitive_type(dummy.types, TypeTag::Float, range::from_object_bytes(&f32_type));

	TEST_EQUAL(f32_entry->tag, TypeTag::Float);

	TEST_EQUAL(f32_entry->bytes, 0);

	TEST_EQUAL(data<FloatType2>(f32_entry)->bits, 32);

	TEST_UNEQUAL(u32_entry, f32_entry);

	release_dummy_types(dummy);

	TEST_END;
}

static void type_entry_from_primitive_type_with_array_returns_array_type() noexcept
{
	TEST_BEGIN;

	DummyTypePool dummy = create_dummy_types();

	IntegerType2 s32_a{ 32, true };

	TypeEntry2* const integer_entry = type_entry_from_primitive_type(dummy.types, TypeTag::Integer, range::from_object_bytes(&s32_a));

	const TypeId2 integer_type_id = id_from_type_entry(dummy.types, integer_entry);

	ArrayTypeInitializer2 array_initializer{ 0, integer_type_id, 128 };

	TypeEntry2* const entry = type_entry_from_primitive_type(dummy.types, TypeTag::Array, array_type_initializer_bytes(&array_initializer));

	TEST_EQUAL(entry->tag, TypeTag::Array);

	TEST_EQUAL(entry->bytes, 8);

	TEST_EQUAL(data<ArrayType2>(entry)->element_count, 128);

	TEST_EQUAL(TypeId2{ entry->inline_data }, integer_type_id);

	release_dummy_types(dummy);

	TEST_END;
}

static void type_entry_from_primitive_type_with_array_twice_returns_same_type_twice() noexcept
{
	TEST_BEGIN;

	DummyTypePool dummy = create_dummy_types();

	IntegerType2 s32_a{ 32, true };

	TypeEntry2* const integer_entry = type_entry_from_primitive_type(dummy.types, TypeTag::Integer, range::from_object_bytes(&s32_a));

	const TypeId2 integer_type_id = id_from_type_entry(dummy.types, integer_entry);

	ArrayTypeInitializer2 array_initializer_a{ 0, integer_type_id, 128 };

	TypeEntry2* const entry_a = type_entry_from_primitive_type(dummy.types, TypeTag::Array, array_type_initializer_bytes(&array_initializer_a));

	TEST_EQUAL(entry_a->tag, TypeTag::Array);

	TEST_EQUAL(entry_a->bytes, 8);

	TEST_EQUAL(data<ArrayType2>(entry_a)->element_count, 128);

	TEST_EQUAL(TypeId2{ entry_a->inline_data }, integer_type_id);

	ArrayTypeInitializer2 array_initializer_b{ array_initializer_a };

	TypeEntry2* const entry_b = type_entry_from_primitive_type(dummy.types, TypeTag::Array, array_type_initializer_bytes(&array_initializer_b));

	TEST_EQUAL(entry_b->tag, TypeTag::Array);

	TEST_EQUAL(entry_b->bytes, 8);

	TEST_EQUAL(data<ArrayType2>(entry_b)->element_count, 128);

	TEST_EQUAL(TypeId2{ entry_b->inline_data }, integer_type_id);

	TEST_EQUAL(entry_a, entry_b);

	release_dummy_types(dummy);

	TEST_END;
}

static void type_entry_from_primitive_type_with_different_sized_arrays_returns_different_types() noexcept
{
	TEST_BEGIN;

	DummyTypePool dummy = create_dummy_types();

	IntegerType2 s32_a{ 32, true };

	TypeEntry2* const integer_entry = type_entry_from_primitive_type(dummy.types, TypeTag::Integer, range::from_object_bytes(&s32_a));

	const TypeId2 integer_type_id = id_from_type_entry(dummy.types, integer_entry);

	ArrayTypeInitializer2 array_initializer_a{ 0, integer_type_id, 128 };

	TypeEntry2* const entry_a = type_entry_from_primitive_type(dummy.types, TypeTag::Array, array_type_initializer_bytes(&array_initializer_a));

	TEST_EQUAL(entry_a->tag, TypeTag::Array);

	TEST_EQUAL(entry_a->bytes, 8);

	TEST_EQUAL(data<ArrayType2>(entry_a)->element_count, 128);

	TEST_EQUAL(TypeId2{ entry_a->inline_data }, integer_type_id);

	ArrayTypeInitializer2 array_initializer_b{ 0, integer_type_id, 42 };

	TypeEntry2* const entry_b = type_entry_from_primitive_type(dummy.types, TypeTag::Array, array_type_initializer_bytes(&array_initializer_b));

	TEST_EQUAL(entry_b->tag, TypeTag::Array);

	TEST_EQUAL(entry_b->bytes, 8);

	TEST_EQUAL(data<ArrayType2>(entry_b)->element_count, 42);

	TEST_EQUAL(TypeId2{ entry_b->inline_data }, integer_type_id);

	TEST_UNEQUAL(entry_a, entry_b);

	release_dummy_types(dummy);

	TEST_END;
}

static void type_entry_from_primitive_type_with_different_typed_arrays_returns_different_types() noexcept
{
	TEST_BEGIN;

	DummyTypePool dummy = create_dummy_types();

	IntegerType2 s32_a{ 32, true };

	TypeEntry2* const integer_entry = type_entry_from_primitive_type(dummy.types, TypeTag::Integer, range::from_object_bytes(&s32_a));

	const TypeId2 integer_type_id = id_from_type_entry(dummy.types, integer_entry);

	ArrayTypeInitializer2 array_initializer_a{ 0, integer_type_id, 128 };

	TypeEntry2* const entry_a = type_entry_from_primitive_type(dummy.types, TypeTag::Array, array_type_initializer_bytes(&array_initializer_a));

	TEST_EQUAL(entry_a->tag, TypeTag::Array);

	TEST_EQUAL(entry_a->bytes, 8);

	TEST_EQUAL(data<ArrayType2>(entry_a)->element_count, 128);

	TEST_EQUAL(TypeId2{ entry_a->inline_data }, integer_type_id);


	FloatType2 f32_a{ 32 };

	TypeEntry2* const float_entry = type_entry_from_primitive_type(dummy.types, TypeTag::Float, range::from_object_bytes(&f32_a));

	const TypeId2 float_type_id = id_from_type_entry(dummy.types, float_entry);

	ArrayTypeInitializer2 array_initializer_b{ 0, float_type_id, 128 };

	TypeEntry2* const entry_b = type_entry_from_primitive_type(dummy.types, TypeTag::Array, array_type_initializer_bytes(&array_initializer_b));

	TEST_EQUAL(entry_b->tag, TypeTag::Array);

	TEST_EQUAL(entry_b->bytes, 8);

	TEST_EQUAL(data<ArrayType2>(entry_b)->element_count, 128);

	TEST_EQUAL(TypeId2{ entry_b->inline_data }, float_type_id);

	TEST_UNEQUAL(entry_a, entry_b);

	release_dummy_types(dummy);

	TEST_END;
}


static void create_type_builder_returns_type_builder() noexcept
{
	TEST_BEGIN;

	DummyTypePool dummy = create_dummy_types();

	TypeBuilder2* const builder = create_type_builder(dummy.types);

	TEST_UNEQUAL(builder, nullptr);

	release_dummy_types(dummy);

	TEST_END;
}

static void type_builder_with_no_members_creates_empty_type() noexcept
{
	TEST_BEGIN;

	DummyTypePool dummy = create_dummy_types();

	TypeBuilder2* const builder = create_type_builder(dummy.types);

	TypeEntry2* const entry = complete_type(builder, 3, 1, 4);

	TEST_EQUAL(entry->tag, TypeTag::Composite);

	TEST_EQUAL(entry->bytes, sizeof(CompositeType2));

	TEST_EQUAL(data<CompositeType2>(entry)->header.size, 3);

	TEST_EQUAL(data<CompositeType2>(entry)->header.align, 1);

	TEST_EQUAL(data<CompositeType2>(entry)->header.stride, 4);

	TEST_EQUAL(data<CompositeType2>(entry)->header.is_complete, false);

	TEST_EQUAL(data<CompositeType2>(entry)->header.member_count, 0);

	release_dummy_types(dummy);

	TEST_END;
}

static void type_builder_with_one_member_creates_type_with_one_member() noexcept
{
	TEST_BEGIN;

	DummyTypePool dummy = create_dummy_types();

	TypeBuilder2* const builder = create_type_builder(dummy.types);

	Member2 member{};
	member.definition.is_global = false;
	member.definition.is_mut = true;
	member.definition.is_pub = true;
	member.definition.type_id_bits = 0;
	member.definition.name = IdentifierId{ 5 };
	member.definition.opt_type = INVALID_AST_NODE_ID;
	member.definition.opt_value = AstNodeId{ 7 };
	member.offset = 0;

	add_type_member(builder, member);

	TypeEntry2* const entry = complete_type(builder, 1, 2, 3);

	TEST_EQUAL(entry->tag, TypeTag::Composite);

	TEST_EQUAL(entry->bytes, sizeof(CompositeType2) + sizeof(Member2));

	TEST_EQUAL(data<CompositeType2>(entry)->header.size, 1);

	TEST_EQUAL(data<CompositeType2>(entry)->header.align, 2);

	TEST_EQUAL(data<CompositeType2>(entry)->header.stride, 3);

	TEST_EQUAL(data<CompositeType2>(entry)->header.is_complete, false);

	TEST_EQUAL(data<CompositeType2>(entry)->header.member_count, 1);

	TEST_MEM_EQUAL(&data<CompositeType2>(entry)->members[0], &member, sizeof(member));

	release_dummy_types(dummy);

	TEST_END;
}

static void type_builder_with_two_members_creates_type_with_two_members() noexcept
{
	TEST_BEGIN;

	DummyTypePool dummy = create_dummy_types();

	TypeBuilder2* const builder = create_type_builder(dummy.types);

	Member2 member_1{};
	member_1.definition.is_global = false;
	member_1.definition.is_mut = true;
	member_1.definition.is_pub = true;
	member_1.definition.type_id_bits = 0;
	member_1.definition.name = IdentifierId{ 5 };
	member_1.definition.opt_type = INVALID_AST_NODE_ID;
	member_1.definition.opt_value = AstNodeId{ 7 };
	member_1.offset = 0;

	Member2 member_2{};
	member_2.definition.is_global = false;
	member_2.definition.is_mut = true;
	member_2.definition.is_pub = true;
	member_2.definition.type_id_bits = 0;
	member_2.definition.name = IdentifierId{ 7 };
	member_2.definition.opt_type = AstNodeId{ 20 };
	member_2.definition.opt_value = AstNodeId{ 100 };
	member_2.offset = 0;

	add_type_member(builder, member_1);

	add_type_member(builder, member_2);

	TypeEntry2* const entry = complete_type(builder, 1, 2, 3);

	TEST_EQUAL(entry->tag, TypeTag::Composite);

	TEST_EQUAL(entry->bytes, sizeof(CompositeType2) + sizeof(Member2) * 2);

	TEST_EQUAL(data<CompositeType2>(entry)->header.size, 1);

	TEST_EQUAL(data<CompositeType2>(entry)->header.align, 2);

	TEST_EQUAL(data<CompositeType2>(entry)->header.stride, 3);

	TEST_EQUAL(data<CompositeType2>(entry)->header.is_complete, false);

	TEST_EQUAL(data<CompositeType2>(entry)->header.member_count, 2);

	TEST_MEM_EQUAL(&data<CompositeType2>(entry)->members[0], &member_1, sizeof(member_1));

	TEST_MEM_EQUAL(&data<CompositeType2>(entry)->members[1], &member_2, sizeof(member_2));

	release_dummy_types(dummy);

	TEST_END;
}

static void type_builder_with_20_members_creates_type_with_20_members() noexcept
{
	TEST_BEGIN;

	DummyTypePool dummy = create_dummy_types();

	TypeBuilder2* const builder = create_type_builder(dummy.types);

	Member2 members[20]{};

	for (u32 i = 0; i != array_count(members); ++i)
	{
		members[i].definition.is_global = (i & 3) == 0;
		members[i].definition.is_mut = i == 5;
		members[i].definition.is_pub = i == 6 || i > 16;
		members[i].definition.type_id_bits = 0;
		members[i].definition.name = IdentifierId{ 1 + i * 2 };
		members[i].definition.opt_type = (i & 2) == 0 ? INVALID_AST_NODE_ID : AstNodeId{ i + 7 };
		members[i].definition.opt_value = AstNodeId{ 7 };
		members[i].offset = i * 20;
	}

	for (u32 i = 0; i != array_count(members); ++i)
		add_type_member(builder, members[i]);

	TypeEntry2* const entry = complete_type(builder, 1, 2, 3);

	TEST_EQUAL(entry->tag, TypeTag::Composite);

	TEST_EQUAL(entry->bytes, sizeof(CompositeType2) + sizeof(Member2) * 20);

	TEST_EQUAL(data<CompositeType2>(entry)->header.size, 1);

	TEST_EQUAL(data<CompositeType2>(entry)->header.align, 2);

	TEST_EQUAL(data<CompositeType2>(entry)->header.stride, 3);

	TEST_EQUAL(data<CompositeType2>(entry)->header.is_complete, false);

	TEST_EQUAL(data<CompositeType2>(entry)->header.member_count, 20);

	for (u32 i = 0; i != array_count(members); ++i)
		TEST_MEM_EQUAL(&members[i], &data<CompositeType2>(entry)->members[i], sizeof(Member2));

	release_dummy_types(dummy);

	TEST_END;
}


void type_pool2_tests() noexcept
{
	TEST_MODULE_BEGIN;

	create_ast_pool_returns_ast_pool();


	type_entry_from_primitive_type_with_integer_returns_integer_type();

	type_entry_from_primitive_type_with_integer_twice_returns_same_type_twice();

	type_entry_from_primitive_type_with_integer_and_float_with_same_bit_pattern_returns_different_types();

	type_entry_from_primitive_type_with_array_returns_array_type();

	type_entry_from_primitive_type_with_array_twice_returns_same_type_twice();

	type_entry_from_primitive_type_with_different_sized_arrays_returns_different_types();

	type_entry_from_primitive_type_with_different_typed_arrays_returns_different_types();


	create_type_builder_returns_type_builder();

	type_builder_with_no_members_creates_empty_type();

	type_builder_with_one_member_creates_type_with_one_member();

	type_builder_with_two_members_creates_type_with_two_members();

	type_builder_with_20_members_creates_type_with_20_members();

	TEST_MODULE_END;
}
