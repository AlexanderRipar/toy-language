#include "test_helpers.hpp"

#include "../infra/types.hpp"

#include "../core/core.hpp"

static CoreData* create_tiny_core() noexcept
{
	Config config{};
	config.enable = { 0 };
	config.enable.type_pool = true;

	return create_core_data(&config);
}

static UserCompositeMemberInit dummy_user_member() noexcept
{
	UserCompositeMemberInit member{};
	member.name = static_cast<IdentifierId>(42);
	member.type_id = TypeId::INVALID;
	member.default_id = none<ForeverValueId>();
	member.is_pub = false;
	member.is_mut = false;
	member.offset = 0;

	return member;
}



static void type_create_numeric_with_integer_returns_integer_type_structure() noexcept
{
	TEST_BEGIN;

	CoreData* const core = create_tiny_core();

	TypeId u16_id = type_create_numeric(core, TypeTag::Integer, NumericType{ 16, false });

	TEST_UNEQUAL(u16_id, TypeId::INVALID);

	TEST_EQUAL(type_tag_from_id(core, u16_id), TypeTag::Integer);

	const NumericType* const interned = type_attachment_from_id<NumericType>(core, u16_id);

	TEST_EQUAL(interned->bits, 16);

	TEST_EQUAL(interned->is_signed, false);

	release_core_data(core);

	TEST_END;
}

static void type_create_numeric_with_integer_and_float_with_same_bit_pattern_returns_different_type_structures() noexcept
{
	TEST_BEGIN;

	CoreData* const core = create_tiny_core();

	const TypeId u32_id = type_create_numeric(core, TypeTag::Integer, NumericType{ 32, false });

	TEST_UNEQUAL(u32_id, TypeId::INVALID);

	TEST_EQUAL(type_tag_from_id(core, u32_id), TypeTag::Integer);

	const NumericType* const interned_u32 = type_attachment_from_id<NumericType>(core, u32_id);

	TEST_EQUAL(interned_u32->bits, 32);

	TEST_EQUAL(interned_u32->is_signed, false);

	const TypeId f32_id = type_create_numeric(core, TypeTag::Float, NumericType{ 32, false });

	TEST_UNEQUAL(f32_id, TypeId::INVALID);

	TEST_EQUAL(type_tag_from_id(core, f32_id), TypeTag::Float);

	const NumericType* const interned_f32 = type_attachment_from_id<NumericType>(core, f32_id);

	TEST_EQUAL(interned_f32->bits, 32);

	TEST_UNEQUAL(u32_id, f32_id);

	TEST_UNEQUAL(interned_u32, interned_f32);

	release_core_data(core);

	TEST_END;
}

static void type_create_numeric_with_same_integer_twice_returns_same_type_id() noexcept
{
	TEST_BEGIN;

	CoreData* const core = create_tiny_core();

	const TypeId s32_id_1 = type_create_numeric(core, TypeTag::Integer, NumericType{ 32, true });

	TEST_UNEQUAL(s32_id_1, TypeId::INVALID);

	TEST_EQUAL(type_tag_from_id(core, s32_id_1), TypeTag::Integer);

	const NumericType* const interned_s32_1 = type_attachment_from_id<NumericType>(core, s32_id_1);

	TEST_EQUAL(interned_s32_1->bits, 32);

	TEST_EQUAL(interned_s32_1->is_signed, true);

	const TypeId s32_id_2 = type_create_numeric(core, TypeTag::Integer, NumericType{ 32, true });

	TEST_UNEQUAL(s32_id_2, TypeId::INVALID);

	TEST_EQUAL(type_tag_from_id(core, s32_id_2), TypeTag::Integer);

	const NumericType* const interned_s32_2 = type_attachment_from_id<NumericType>(core, s32_id_2);

	TEST_EQUAL(interned_s32_2->bits, 32);

	TEST_EQUAL(interned_s32_2->is_signed, true);

	TEST_EQUAL(s32_id_1, s32_id_2);

	TEST_EQUAL(interned_s32_1, interned_s32_2);

	release_core_data(core);

	TEST_END;
}

static void type_create_array_with_integer_elements_returns_array_type() noexcept
{
	TEST_BEGIN;

	CoreData* const core = create_tiny_core();

	const TypeId s32_id = type_create_numeric(core, TypeTag::Integer, NumericType{ 32, true });

	TEST_UNEQUAL(s32_id, TypeId::INVALID);

	const TypeId array_id = type_create_array(core, TypeTag::Array, ArrayType{ 128, some(s32_id) });

	TEST_UNEQUAL(array_id, TypeId::INVALID);

	TEST_EQUAL(type_tag_from_id(core, array_id), TypeTag::Array);

	const ArrayType* const interned = type_attachment_from_id<ArrayType>(core, array_id);

	TEST_EQUAL(interned->element_count, 128);

	TEST_EQUAL(get(interned->element_type), s32_id);

	release_core_data(core);

	TEST_END;
}

static void type_create_array_with_integer_twice_returns_same_type_id() noexcept
{
	TEST_BEGIN;

	CoreData* const core = create_tiny_core();

	const TypeId s32_id = type_create_numeric(core, TypeTag::Integer, NumericType{ 32, true });

	TEST_UNEQUAL(s32_id, TypeId::INVALID);

	const TypeId array_id_1 = type_create_array(core, TypeTag::Array, ArrayType{ 128, some(s32_id) });

	TEST_UNEQUAL(array_id_1, TypeId::INVALID);

	TEST_EQUAL(type_tag_from_id(core, array_id_1), TypeTag::Array);

	const ArrayType* const interned_array_1 = type_attachment_from_id<ArrayType>(core, array_id_1);

	TEST_EQUAL(interned_array_1->element_count, 128);

	TEST_EQUAL(get(interned_array_1->element_type), s32_id);

	const TypeId array_id_2 = type_create_array(core, TypeTag::Array, ArrayType{ 128, some(s32_id) });

	TEST_UNEQUAL(array_id_2, TypeId::INVALID);

	TEST_EQUAL(type_tag_from_id(core, array_id_2), TypeTag::Array);

	const ArrayType* const interned_array_2 = type_attachment_from_id<ArrayType>(core, array_id_2);

	TEST_EQUAL(interned_array_2->element_count, 128);

	TEST_EQUAL(get(interned_array_2->element_type), s32_id);

	TEST_EQUAL(array_id_1, array_id_2);

	TEST_EQUAL(interned_array_1, interned_array_2);

	release_core_data(core);

	TEST_END;
}

static void type_create_composite_creates_composite_type_with_no_members() noexcept
{
	TEST_BEGIN;

	CoreData* const core = create_tiny_core();

	const TypeId composite = type_create_user_composite(core, TypeTag::Composite, static_cast<SourceId>(1));

	TEST_UNEQUAL(composite, TypeId::INVALID);

	UserCompositeSealInfo seal{};
	seal.size = 0;
	seal.stride = 0;
	seal.align = 1;

	type_seal_user_composite(core, composite, seal);

	MemberIterator it = members_of(core, composite);

	u32 member_count = 0;

	while (has_next(&it))
	{
		MemberInfo unused_member_info;

		OpcodeId unused_member_initializer;

		TEST_EQUAL(next(&it, &unused_member_info, &unused_member_initializer), true);

		member_count += 1;
	}

	TEST_EQUAL(member_count, 0);

	release_core_data(core);

	TEST_END;
}

static void type_create_composite_and_add_member_creates_composite_type_with_one_member() noexcept
{
	TEST_BEGIN;

	CoreData* const core = create_tiny_core();

	const TypeId composite = type_create_user_composite(core, TypeTag::Composite, static_cast<SourceId>(1));

	TEST_UNEQUAL(composite, TypeId::INVALID);

	UserCompositeMemberInit member_init = dummy_user_member();
	member_init.type_id = type_create_simple(core, TypeTag::Boolean);

	TEST_EQUAL(type_add_user_composite_member(core, composite, member_init), true);

	UserCompositeSealInfo seal{};
	seal.size = 1;
	seal.stride = 1;
	seal.align = 1;

	type_seal_user_composite(core, composite, seal);

	MemberIterator it = members_of(core, composite);

	u32 member_count = 0;

	while (has_next(&it))
	{
		MemberInfo member_info;

		OpcodeId member_initializer;

		TEST_EQUAL(next(&it, &member_info, &member_initializer), true);


		static_assert(sizeof(member_info) == sizeof(member_init));

		TEST_EQUAL(member_info.is_eval, false);

		TEST_EQUAL(member_info.is_mut, member_init.is_mut);

		TEST_EQUAL(member_info.is_pub, member_init.is_pub);

		TEST_EQUAL(member_info.offset, member_init.offset);

		TEST_EQUAL(member_info.type_id, member_init.type_id);

		TEST_EQUAL(member_info.value_or_default_id, member_init.default_id);

		member_count += 1;
	}

	TEST_EQUAL(member_count, 1);

	release_core_data(core);

	TEST_END;
}

// ```
// let A = Tuple()
// let B = Tuple()
//
// assert(A == B)
// ```
static void empty_composites_are_equal() noexcept
{
	TEST_BEGIN;

	CoreData* const core = create_tiny_core();

	const TypeId a = type_create_user_composite(core, TypeTag::Composite, static_cast<SourceId>(1));

	const TypeId b = type_create_user_composite(core, TypeTag::Composite, static_cast<SourceId>(1));

	const bool equal = type_is_equal(core, a, b);

	TEST_EQUAL(equal, true);

	release_core_data(core);

	TEST_END;
}

// ```
// let X = Tuple()
// let Y = Tuple()
//
// let A = Tuple(X)
// let B = Tuple(Y)
//
// assert(A == B)
// ```
static void composites_with_empty_composite_member_are_equal() noexcept
{
	TEST_BEGIN;

	CoreData* const core = create_tiny_core();

	const TypeId x = type_create_user_composite(core, TypeTag::Composite, static_cast<SourceId>(1));

	const TypeId y = type_create_user_composite(core, TypeTag::Composite, static_cast<SourceId>(1));

	const TypeId a = type_create_user_composite(core, TypeTag::Composite, static_cast<SourceId>(1));

	const TypeId b = type_create_user_composite(core, TypeTag::Composite, static_cast<SourceId>(1));

	UserCompositeMemberInit member = dummy_user_member();

	member.type_id = x;
	TEST_EQUAL(type_add_user_composite_member(core, a, member), true);

	member.type_id = y;
	TEST_EQUAL(type_add_user_composite_member(core, b, member), true);

	const bool equal = type_is_equal(core, a, b);

	TEST_EQUAL(equal, true);

	release_core_data(core);

	TEST_END;
}

static void pointers_to_same_composite_are_equal() noexcept
{
	TEST_BEGIN;

	CoreData* const core = create_tiny_core();

	const TypeId composite = type_create_user_composite(core, TypeTag::Composite, static_cast<SourceId>(1));

	UserCompositeSealInfo seal{};
	seal.size = 0;
	seal.stride = 0;
	seal.align = 1;

	type_seal_user_composite(core, composite, seal);

	ReferenceType reference{};
	reference.referenced_type_id = composite;
	reference.is_opt = false;
	reference.is_multi = false;
	reference.is_mut = false;

	const TypeId pointer_1 = type_create_reference(core, TypeTag::Ptr, reference);

	const TypeId pointer_2 = type_create_reference(core, TypeTag::Ptr, reference);

	const bool equal = type_is_equal(core, pointer_1, pointer_2);

	TEST_EQUAL(equal, true);

	release_core_data(core);

	TEST_END;
}

static void pointers_to_equal_composites_are_equal() noexcept
{
	TEST_BEGIN;

	CoreData* const core = create_tiny_core();

	const TypeId composite_1 = type_create_user_composite(core, TypeTag::Composite, static_cast<SourceId>(1));

	UserCompositeSealInfo seal{};
	seal.size = 0;
	seal.stride = 0;
	seal.align = 1;

	type_seal_user_composite(core, composite_1, seal);

	const TypeId composite_2 = type_create_user_composite(core, TypeTag::Composite, static_cast<SourceId>(1));

	type_seal_user_composite(core, composite_2, seal);

	ReferenceType reference{};
	reference.referenced_type_id = composite_1;
	reference.is_opt = false;
	reference.is_multi = false;
	reference.is_mut = false;

	const TypeId pointer_1 = type_create_reference(core, TypeTag::Ptr, reference);

	reference.referenced_type_id = composite_2;

	const TypeId pointer_2 = type_create_reference(core, TypeTag::Ptr, reference);

	const bool equal = type_is_equal(core, pointer_1, pointer_2);

	TEST_EQUAL(equal, true);

	release_core_data(core);

	TEST_END;
}

// ```
// let A = Tuple(*A)
// let B = Tuple(*B)
//
// assert(A == B)
// ```
static void composites_with_same_distinct_source_and_pointers_to_self_are_equal() noexcept
{
	TEST_BEGIN;

	CoreData* const core = create_tiny_core();

	UserCompositeMemberInit member = dummy_user_member();

	ReferenceType reference{};
	reference.is_opt = false;
	reference.is_multi = false;
	reference.is_mut = false;

	const TypeId a = type_create_user_composite(core, TypeTag::Composite, static_cast<SourceId>(1));

	const TypeId b = type_create_user_composite(core, TypeTag::Composite, static_cast<SourceId>(1));

	reference.referenced_type_id = a;
	const TypeId p_a = type_create_reference(core, TypeTag::Ptr, reference);

	reference.referenced_type_id = b;
	const TypeId p_b = type_create_reference(core, TypeTag::Ptr, reference);

	member.type_id = p_a;
	TEST_EQUAL(type_add_user_composite_member(core, a, member), true);

	member.type_id = p_b;
	TEST_EQUAL(type_add_user_composite_member(core, b, member), true);

	UserCompositeSealInfo seal{};
	seal.size = 8;
	seal.stride = 8;
	seal.align = 8;

	type_seal_user_composite(core, a, seal);

	type_seal_user_composite(core, b, seal);

	const bool equal = type_is_equal(core, a, b);

	TEST_EQUAL(equal, true);

	release_core_data(core);

	TEST_END;
}

// ```
// let A = Tuple(*B)
// let B = Tuple(*A)
//
// assert(A == B)
// ```
static void composites_with_same_distinct_source_and_pointers_to_each_other_are_equal() noexcept
{
	TEST_BEGIN;

	CoreData* const core = create_tiny_core();

	UserCompositeMemberInit member = dummy_user_member();

	ReferenceType reference{};
	reference.is_opt = false;
	reference.is_multi = false;
	reference.is_mut = false;

	const TypeId a = type_create_user_composite(core, TypeTag::Composite, static_cast<SourceId>(1));

	const TypeId b = type_create_user_composite(core, TypeTag::Composite, static_cast<SourceId>(1));

	reference.referenced_type_id = a;
	const TypeId p_a = type_create_reference(core, TypeTag::Ptr, reference);

	reference.referenced_type_id = b;
	const TypeId p_b = type_create_reference(core, TypeTag::Ptr, reference);

	member.type_id = p_b;
	TEST_EQUAL(type_add_user_composite_member(core, a, member), true);

	member.type_id = p_a;
	TEST_EQUAL(type_add_user_composite_member(core, b, member), true);

	UserCompositeSealInfo seal{};
	seal.size = 8;
	seal.stride = 8;
	seal.align = 8;

	type_seal_user_composite(core, a, seal);

	type_seal_user_composite(core, b, seal);

	const bool equal = type_is_equal(core, a, b);

	TEST_EQUAL(equal, true);

	release_core_data(core);

	TEST_END;
}

// ```
// let A = Tuple(*B, u32)
// let B = Tuple(*A, u64)
//
// assert(A != B)
// ```
static void composites_with_same_distinct_source_and_pointers_to_self_and_different_second_member_are_unequal() noexcept
{
	TEST_BEGIN;

	CoreData* const core = create_tiny_core();

	UserCompositeMemberInit member = dummy_user_member();

	ReferenceType reference{};
	reference.is_opt = false;
	reference.is_multi = false;
	reference.is_mut = false;

	const TypeId a = type_create_user_composite(core, TypeTag::Composite, static_cast<SourceId>(1));

	const TypeId b = type_create_user_composite(core, TypeTag::Composite, static_cast<SourceId>(1));

	reference.referenced_type_id = a;
	const TypeId p_a = type_create_reference(core, TypeTag::Ptr, reference);

	reference.referenced_type_id = b;
	const TypeId p_b = type_create_reference(core, TypeTag::Ptr, reference);

	member.name = static_cast<IdentifierId>(1);
	member.type_id = p_b;
	TEST_EQUAL(type_add_user_composite_member(core, a, member), true);

	member.name = static_cast<IdentifierId>(2);
	member.type_id = type_create_numeric(core, TypeTag::Integer, NumericType{ 8, false });
	TEST_EQUAL(type_add_user_composite_member(core, a, member), true);

	member.name = static_cast<IdentifierId>(1);
	member.type_id = p_a;
	TEST_EQUAL(type_add_user_composite_member(core, b, member), true);

	member.name = static_cast<IdentifierId>(2);
	member.type_id = type_create_numeric(core, TypeTag::Integer, NumericType{ 64, false });
	TEST_EQUAL(type_add_user_composite_member(core, b, member), true);

	UserCompositeSealInfo seal{};
	seal.size = 8;
	seal.stride = 8;
	seal.align = 8;

	type_seal_user_composite(core, a, seal);

	type_seal_user_composite(core, b, seal);

	const bool equal = type_is_equal(core, a, b);

	TEST_EQUAL(equal, false);

	release_core_data(core);

	TEST_END;
}

// ```
// let A1 = Tuple(*A2, u32)
// let A2 = Tuple(*A1, u64)
// let B1 = Tuple(*B2, u32)
// let B2 = Tuple(*B1, u64)
//
// assert(A1 == B1)
// assert(A2 == B2)
// ```
static void mutually_referencing_pairs_of_composites_with_different_second_member_are_considered_equal_in_expected_positions() noexcept
{
	TEST_BEGIN;

	CoreData* const core = create_tiny_core();

	ReferenceType reference{};
	reference.is_opt = false;
	reference.is_multi = false;
	reference.is_mut = false;

	UserCompositeMemberInit member = dummy_user_member();

	const TypeId a1 = type_create_user_composite(core, TypeTag::Composite, static_cast<SourceId>(1));

	const TypeId a2 = type_create_user_composite(core, TypeTag::Composite, static_cast<SourceId>(1));

	const TypeId b1 = type_create_user_composite(core, TypeTag::Composite, static_cast<SourceId>(1));

	const TypeId b2 = type_create_user_composite(core, TypeTag::Composite, static_cast<SourceId>(1));

	reference.referenced_type_id = a1;
	const TypeId p_a1 = type_create_reference(core, TypeTag::Ptr, reference);

	reference.referenced_type_id = a2;
	const TypeId p_a2 = type_create_reference(core, TypeTag::Ptr, reference);

	reference.referenced_type_id = b1;
	const TypeId p_b1 = type_create_reference(core, TypeTag::Ptr, reference);

	reference.referenced_type_id = b2;
	const TypeId p_b2 = type_create_reference(core, TypeTag::Ptr, reference);

	member.type_id = p_a2;
	TEST_EQUAL(type_add_user_composite_member(core, a1, member), true);

	member.type_id = p_a1;
	TEST_EQUAL(type_add_user_composite_member(core, a2, member), true);

	member.type_id = p_b2;
	TEST_EQUAL(type_add_user_composite_member(core, b1, member), true);

	member.type_id = p_b1;
	TEST_EQUAL(type_add_user_composite_member(core, b2, member), true);

	member.name = static_cast<IdentifierId>(404);

	member.type_id = type_create_numeric(core, TypeTag::Integer, NumericType{ 32, false });
	TEST_EQUAL(type_add_user_composite_member(core, a1, member), true);
	TEST_EQUAL(type_add_user_composite_member(core, b1, member), true);

	member.type_id = type_create_numeric(core, TypeTag::Integer, NumericType{ 64, false });
	TEST_EQUAL(type_add_user_composite_member(core, a2, member), true);
	TEST_EQUAL(type_add_user_composite_member(core, b2, member), true);

	UserCompositeSealInfo seal{};
	seal.size = 8;
	seal.stride = 8;
	seal.align = 8;

	type_seal_user_composite(core, a1, seal);

	type_seal_user_composite(core, a2, seal);

	type_seal_user_composite(core, b1, seal);

	type_seal_user_composite(core, b2, seal);

	const bool a1_b1_equal = type_is_equal(core, a1, b1);

	TEST_EQUAL(a1_b1_equal, true);

	const bool a2_b2_equal = type_is_equal(core, a2, b2);

	TEST_EQUAL(a2_b2_equal, true);

	release_core_data(core);

	TEST_END;
}

// ```
// let A1 = Tuple(*A2, u32)
// let A2 = Tuple(*A1, u64)
// let B1 = Tuple(*B2, u32)
// let B2 = Tuple(*B1, u64)
//
// assert(A1 != B2)
// assert(A2 != B1)
// ```
static void mutually_referencing_pairs_of_composites_with_different_second_member_are_considered_unequal_in_expected_positions() noexcept
{
	TEST_BEGIN;

	CoreData* const core = create_tiny_core();

	ReferenceType reference{};
	reference.is_opt = false;
	reference.is_multi = false;
	reference.is_mut = false;

	UserCompositeMemberInit member = dummy_user_member();

	const TypeId a1 = type_create_user_composite(core, TypeTag::Composite, static_cast<SourceId>(1));

	const TypeId a2 = type_create_user_composite(core, TypeTag::Composite, static_cast<SourceId>(1));

	const TypeId b1 = type_create_user_composite(core, TypeTag::Composite, static_cast<SourceId>(1));

	const TypeId b2 = type_create_user_composite(core, TypeTag::Composite, static_cast<SourceId>(1));

	reference.referenced_type_id = a1;
	const TypeId p_a1 = type_create_reference(core, TypeTag::Ptr, reference);

	reference.referenced_type_id = a2;
	const TypeId p_a2 = type_create_reference(core, TypeTag::Ptr, reference);

	reference.referenced_type_id = b1;
	const TypeId p_b1 = type_create_reference(core, TypeTag::Ptr, reference);

	reference.referenced_type_id = b2;
	const TypeId p_b2 = type_create_reference(core, TypeTag::Ptr, reference);

	member.type_id = p_a2;
	TEST_EQUAL(type_add_user_composite_member(core, a1, member), true);

	member.type_id = p_a1;
	TEST_EQUAL(type_add_user_composite_member(core, a2, member), true);

	member.type_id = p_b2;
	TEST_EQUAL(type_add_user_composite_member(core, b1, member), true);

	member.type_id = p_b1;
	TEST_EQUAL(type_add_user_composite_member(core, b2, member), true);

	member.name = static_cast<IdentifierId>(404);

	member.type_id = type_create_numeric(core, TypeTag::Integer, NumericType{ 32, false });
	TEST_EQUAL(type_add_user_composite_member(core, a1, member), true);
	TEST_EQUAL(type_add_user_composite_member(core, b1, member), true);

	member.type_id = type_create_numeric(core, TypeTag::Integer, NumericType{ 64, false });
	TEST_EQUAL(type_add_user_composite_member(core, a2, member), true);
	TEST_EQUAL(type_add_user_composite_member(core, b2, member), true);

	UserCompositeSealInfo seal{};
	seal.size = 8;
	seal.stride = 8;
	seal.align = 8;

	type_seal_user_composite(core, a1, seal);

	type_seal_user_composite(core, a2, seal);

	type_seal_user_composite(core, b1, seal);

	type_seal_user_composite(core, b2, seal);

	const bool a1_a2_equal = type_is_equal(core, a1, a2);

	TEST_EQUAL(a1_a2_equal, false);

	const bool b1_b2_equal = type_is_equal(core, b1, b2);

	TEST_EQUAL(b1_b2_equal, false);

	const bool a1_b2_equal = type_is_equal(core, a1, b2);

	TEST_EQUAL(a1_b2_equal, false);

	const bool a2_b1_equal = type_is_equal(core, a2, b1);

	TEST_EQUAL(a2_b1_equal, false);

	release_core_data(core);

	TEST_END;
}

// ```
// let A = Pair(u32, u32)
// let B = Pair(u32, u32)
//
// assert(A == B)
// ```
static void pair_types_with_same_element_types_are_considered_equal() noexcept
{
	TEST_BEGIN;

	CoreData* const core = create_tiny_core();

	const TypeId a = type_create_user_composite(core, TypeTag::Composite, static_cast<SourceId>(1));

	const TypeId b = type_create_user_composite(core, TypeTag::Composite, static_cast<SourceId>(1));

	UserCompositeMemberInit member = dummy_user_member();
	member.type_id = type_create_numeric(core, TypeTag::Integer, NumericType{ 32, false });

	TEST_EQUAL(type_add_user_composite_member(core, a, member), true);
	TEST_EQUAL(type_add_user_composite_member(core, b, member), true);

	member.name = static_cast<IdentifierId>(9001);

	TEST_EQUAL(type_add_user_composite_member(core, a, member), true);
	TEST_EQUAL(type_add_user_composite_member(core, b, member), true);

	UserCompositeSealInfo seal{};
	seal.size = 8;
	seal.stride = 8;
	seal.align = 8;

	type_seal_user_composite(core, a, seal);

	type_seal_user_composite(core, b, seal);

	const bool equal = type_is_equal(core, a, b);

	TEST_EQUAL(equal, true);

	release_core_data(core);

	TEST_END;
}



void type_pool_tests() noexcept
{
	TEST_MODULE_BEGIN;

	type_create_numeric_with_integer_returns_integer_type_structure();

	type_create_numeric_with_integer_and_float_with_same_bit_pattern_returns_different_type_structures();

	type_create_numeric_with_same_integer_twice_returns_same_type_id();

	type_create_array_with_integer_elements_returns_array_type();

	type_create_array_with_integer_twice_returns_same_type_id();

	type_create_composite_creates_composite_type_with_no_members();

	type_create_composite_and_add_member_creates_composite_type_with_one_member();

	empty_composites_are_equal();

	composites_with_empty_composite_member_are_equal();

	pointers_to_same_composite_are_equal();

	pointers_to_equal_composites_are_equal();

	composites_with_same_distinct_source_and_pointers_to_self_are_equal();

	composites_with_same_distinct_source_and_pointers_to_each_other_are_equal();

	composites_with_same_distinct_source_and_pointers_to_self_and_different_second_member_are_unequal();

	mutually_referencing_pairs_of_composites_with_different_second_member_are_considered_equal_in_expected_positions();

	mutually_referencing_pairs_of_composites_with_different_second_member_are_considered_unequal_in_expected_positions();

	pair_types_with_same_element_types_are_considered_equal();

	TEST_MODULE_END;
}
