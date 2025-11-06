#include "test_helpers.hpp"
#include "../core/core.hpp"

struct DummyTypePool
{
	TypePool* types;

	HandlePool* alloc;
};

static DummyTypePool create_dummy_types() noexcept
{
	HandlePool* const alloc = create_handle_pool(1 << 12, 1 << 12);

	TypePool* const types = create_type_pool(alloc);

	return { types, alloc };

}

static void release_dummy_types(DummyTypePool dummy) noexcept
{
	release_type_pool(dummy.types);

	release_handle_pool(dummy.alloc);
}

static MemberInfo dummy_member() noexcept
{
	MemberInfo member{};
	member.name = static_cast<IdentifierId>(42);
	member.type.complete = TypeId::INVALID;
	member.value.complete = GlobalValueId::INVALID;
	member.is_global = false;
	member.is_pub = true;
	member.is_mut = true;
	member.has_pending_type = false;
	member.has_pending_value = false;
	member.is_comptime_known = true;
	member.rank = 0;
	member.type_completion_arec_id = ArecId::INVALID;
	member.value_completion_arec_id = ArecId::INVALID;
	member.offset = 0;

	return member;
}



static void type_create_numeric_with_integer_returns_integer_type_structure() noexcept
{
	TEST_BEGIN;

	DummyTypePool dummy = create_dummy_types();

	TypeId u16_id = type_create_numeric(dummy.types, TypeTag::Integer, NumericType{ 16, false });

	TEST_UNEQUAL(u16_id, TypeId::INVALID);

	TEST_EQUAL(type_tag_from_id(dummy.types, u16_id), TypeTag::Integer);

	const NumericType* const interned = type_attachment_from_id<NumericType>(dummy.types, u16_id);

	TEST_EQUAL(interned->bits, 16);

	TEST_EQUAL(interned->is_signed, false);

	release_dummy_types(dummy);

	TEST_END;
}

static void type_create_numeric_with_integer_and_float_with_same_bit_pattern_returns_different_type_structures() noexcept
{
	TEST_BEGIN;

	DummyTypePool dummy = create_dummy_types();

	const TypeId u32_id = type_create_numeric(dummy.types, TypeTag::Integer, NumericType{ 32, false });

	TEST_UNEQUAL(u32_id, TypeId::INVALID);

	TEST_EQUAL(type_tag_from_id(dummy.types, u32_id), TypeTag::Integer);

	const NumericType* const interned_u32 = type_attachment_from_id<NumericType>(dummy.types, u32_id);

	TEST_EQUAL(interned_u32->bits, 32);

	TEST_EQUAL(interned_u32->is_signed, false);

	const TypeId f32_id = type_create_numeric(dummy.types, TypeTag::Float, NumericType{ 32, false });

	TEST_UNEQUAL(f32_id, TypeId::INVALID);

	TEST_EQUAL(type_tag_from_id(dummy.types, f32_id), TypeTag::Float);

	const NumericType* const interned_f32 = type_attachment_from_id<NumericType>(dummy.types, f32_id);

	TEST_EQUAL(interned_f32->bits, 32);

	TEST_UNEQUAL(u32_id, f32_id);

	TEST_UNEQUAL(interned_u32, interned_f32);

	release_dummy_types(dummy);

	TEST_END;
}

static void type_create_numeric_with_same_integer_twice_returns_same_type_id() noexcept
{
	TEST_BEGIN;

	DummyTypePool dummy = create_dummy_types();

	const TypeId s32_id_1 = type_create_numeric(dummy.types, TypeTag::Integer, NumericType{ 32, true });

	TEST_UNEQUAL(s32_id_1, TypeId::INVALID);

	TEST_EQUAL(type_tag_from_id(dummy.types, s32_id_1), TypeTag::Integer);

	const NumericType* const interned_s32_1 = type_attachment_from_id<NumericType>(dummy.types, s32_id_1);

	TEST_EQUAL(interned_s32_1->bits, 32);

	TEST_EQUAL(interned_s32_1->is_signed, true);

	const TypeId s32_id_2 = type_create_numeric(dummy.types, TypeTag::Integer, NumericType{ 32, true });

	TEST_UNEQUAL(s32_id_2, TypeId::INVALID);

	TEST_EQUAL(type_tag_from_id(dummy.types, s32_id_2), TypeTag::Integer);

	const NumericType* const interned_s32_2 = type_attachment_from_id<NumericType>(dummy.types, s32_id_2);

	TEST_EQUAL(interned_s32_2->bits, 32);

	TEST_EQUAL(interned_s32_2->is_signed, true);

	TEST_EQUAL(s32_id_1, s32_id_2);

	TEST_EQUAL(interned_s32_1, interned_s32_2);

	release_dummy_types(dummy);

	TEST_END;
}

static void type_create_array_with_integer_elements_returns_array_type() noexcept
{
	TEST_BEGIN;

	DummyTypePool dummy = create_dummy_types();

	const TypeId s32_id = type_create_numeric(dummy.types, TypeTag::Integer, NumericType{ 32, true });

	TEST_UNEQUAL(s32_id, TypeId::INVALID);

	const TypeId array_id = type_create_array(dummy.types, TypeTag::Array, ArrayType{ 128, s32_id });

	TEST_UNEQUAL(array_id, TypeId::INVALID);

	TEST_EQUAL(type_tag_from_id(dummy.types, array_id), TypeTag::Array);

	const ArrayType* const interned = type_attachment_from_id<ArrayType>(dummy.types, array_id);

	TEST_EQUAL(interned->element_count, 128);

	TEST_EQUAL(interned->element_type, s32_id);

	release_dummy_types(dummy);

	TEST_END;
}

static void type_create_array_with_integer_twice_returns_same_type_id() noexcept
{
	TEST_BEGIN;

	DummyTypePool dummy = create_dummy_types();

	const TypeId s32_id = type_create_numeric(dummy.types, TypeTag::Integer, NumericType{ 32, true });

	TEST_UNEQUAL(s32_id, TypeId::INVALID);

	const TypeId array_id_1 = type_create_array(dummy.types, TypeTag::Array, ArrayType{ 128, s32_id });

	TEST_UNEQUAL(array_id_1, TypeId::INVALID);

	TEST_EQUAL(type_tag_from_id(dummy.types, array_id_1), TypeTag::Array);

	const ArrayType* const interned_array_1 = type_attachment_from_id<ArrayType>(dummy.types, array_id_1);

	TEST_EQUAL(interned_array_1->element_count, 128);

	TEST_EQUAL(interned_array_1->element_type, s32_id);

	const TypeId array_id_2 = type_create_array(dummy.types, TypeTag::Array, ArrayType{ 128, s32_id });

	TEST_UNEQUAL(array_id_2, TypeId::INVALID);

	TEST_EQUAL(type_tag_from_id(dummy.types, array_id_2), TypeTag::Array);

	const ArrayType* const interned_array_2 = type_attachment_from_id<ArrayType>(dummy.types, array_id_2);

	TEST_EQUAL(interned_array_2->element_count, 128);

	TEST_EQUAL(interned_array_2->element_type, s32_id);

	TEST_EQUAL(array_id_1, array_id_2);

	TEST_EQUAL(interned_array_1, interned_array_2);

	release_dummy_types(dummy);

	TEST_END;
}

static void type_create_composite_creates_composite_type_with_no_members() noexcept
{
	TEST_BEGIN;

	DummyTypePool dummy = create_dummy_types();

	const TypeId composite = type_create_composite(dummy.types, TypeTag::Composite, TypeId::INVALID, TypeDisposition::User, SourceId::INVALID, 0, true);

	TEST_UNEQUAL(composite, TypeId::INVALID);

	MemberIterator it = members_of(dummy.types, composite);

	u32 member_count = 0;

	while (has_next(&it))
	{
		(void) next(&it);

		member_count += 1;
	}

	TEST_EQUAL(member_count, 0);

	release_dummy_types(dummy);

	TEST_END;
}

static void type_create_composite_and_add_member_creates_composite_type_with_one_member() noexcept
{
	TEST_BEGIN;

	DummyTypePool dummy = create_dummy_types();

	const TypeId composite = type_create_composite(dummy.types, TypeTag::Composite, TypeId::INVALID, TypeDisposition::User, SourceId::INVALID, 0, false);

	TEST_UNEQUAL(composite, TypeId::INVALID);

	MemberInfo member = dummy_member();
	member.type.complete = type_create_simple(dummy.types, TypeTag::Boolean);

	TEST_EQUAL(type_add_composite_member(dummy.types, composite, member), true);

	MemberIterator it = members_of(dummy.types, composite);

	u32 member_count = 0;

	while (has_next(&it))
	{
		const MemberInfo interned_member = next(&it);

		static_assert(sizeof(interned_member) == sizeof(member));

		TEST_MEM_EQUAL(&member, &interned_member, sizeof(member));

		member_count += 1;
	}

	TEST_EQUAL(member_count, 1);

	release_dummy_types(dummy);

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

	DummyTypePool dummy = create_dummy_types();

	const TypeId a = type_create_composite(dummy.types, TypeTag::Composite, TypeId::INVALID, TypeDisposition::User, SourceId::INVALID, 0, false);

	const TypeId b = type_create_composite(dummy.types, TypeTag::Composite, TypeId::INVALID, TypeDisposition::User, SourceId::INVALID, 0, false);

	const bool equal = type_is_equal(dummy.types, a, b);

	TEST_EQUAL(equal, true);

	release_dummy_types(dummy);

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

	DummyTypePool dummy = create_dummy_types();

	const TypeId x = type_create_composite(dummy.types, TypeTag::Composite, TypeId::INVALID, TypeDisposition::User, SourceId::INVALID, 0, false);

	const TypeId y = type_create_composite(dummy.types, TypeTag::Composite, TypeId::INVALID, TypeDisposition::User, SourceId::INVALID, 0, false);

	const TypeId a = type_create_composite(dummy.types, TypeTag::Composite, TypeId::INVALID, TypeDisposition::User, SourceId::INVALID, 0, false);

	const TypeId b = type_create_composite(dummy.types, TypeTag::Composite, TypeId::INVALID, TypeDisposition::User, SourceId::INVALID, 0, false);

	MemberInfo member = dummy_member();

	member.type.complete = x;
	TEST_EQUAL(type_add_composite_member(dummy.types, a, member), true);

	member.type.complete = y;
	TEST_EQUAL(type_add_composite_member(dummy.types, b, member), true);
	
	const bool equal = type_is_equal(dummy.types, a, b);

	TEST_EQUAL(equal, true);

	release_dummy_types(dummy);

	TEST_END;
}

static void pointers_to_same_composite_are_equal() noexcept
{
	TEST_BEGIN;

	DummyTypePool dummy = create_dummy_types();

	const TypeId composite = type_create_composite(dummy.types, TypeTag::Composite, TypeId::INVALID, TypeDisposition::User, SourceId::INVALID, 0, false);

	type_seal_composite(dummy.types, composite, 0, 1, 0);

	ReferenceType reference{};
	reference.referenced_type_id = composite;
	reference.is_opt = false;
	reference.is_multi = false;
	reference.is_mut = false;

	const TypeId pointer_1 = type_create_reference(dummy.types, TypeTag::Ptr, reference);

	const TypeId pointer_2 = type_create_reference(dummy.types, TypeTag::Ptr, reference);

	const bool equal = type_is_equal(dummy.types, pointer_1, pointer_2);

	TEST_EQUAL(equal, true);

	release_dummy_types(dummy);

	TEST_END;
}

static void pointers_to_equal_composites_are_equal() noexcept
{
	TEST_BEGIN;

	DummyTypePool dummy = create_dummy_types();

	const TypeId composite_1 = type_create_composite(dummy.types, TypeTag::Composite, TypeId::INVALID, TypeDisposition::User, SourceId::INVALID, 0, false);

	type_seal_composite(dummy.types, composite_1, 0, 1, 0);

	const TypeId composite_2 = type_create_composite(dummy.types, TypeTag::Composite, TypeId::INVALID, TypeDisposition::User, SourceId::INVALID, 0, false);

	type_seal_composite(dummy.types, composite_2, 0, 1, 0);

	ReferenceType reference{};
	reference.referenced_type_id = composite_1;
	reference.is_opt = false;
	reference.is_multi = false;
	reference.is_mut = false;

	const TypeId pointer_1 = type_create_reference(dummy.types, TypeTag::Ptr, reference);

	reference.referenced_type_id = composite_2;

	const TypeId pointer_2 = type_create_reference(dummy.types, TypeTag::Ptr, reference);

	const bool equal = type_is_equal(dummy.types, pointer_1, pointer_2);

	TEST_EQUAL(equal, true);

	release_dummy_types(dummy);

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

	DummyTypePool dummy = create_dummy_types();

	MemberInfo member = dummy_member();

	ReferenceType reference{};
	reference.is_opt = false;
	reference.is_multi = false;
	reference.is_mut = false;

	const TypeId a = type_create_composite(dummy.types, TypeTag::Composite, TypeId::INVALID, TypeDisposition::User, SourceId::INVALID, 0, false);

	const TypeId b = type_create_composite(dummy.types, TypeTag::Composite, TypeId::INVALID, TypeDisposition::User, SourceId::INVALID, 0, false);

	reference.referenced_type_id = a;
	const TypeId p_a = type_create_reference(dummy.types, TypeTag::Ptr, reference);

	reference.referenced_type_id = b;
	const TypeId p_b = type_create_reference(dummy.types, TypeTag::Ptr, reference);

	member.type.complete = p_a;
	TEST_EQUAL(type_add_composite_member(dummy.types, a, member), true);

	member.type.complete = p_b;
	TEST_EQUAL(type_add_composite_member(dummy.types, b, member), true);

	type_seal_composite(dummy.types, a, 8, 1, 8);

	type_seal_composite(dummy.types, b, 8, 1, 8);

	const bool equal = type_is_equal(dummy.types, a, b);

	TEST_EQUAL(equal, true);

	release_dummy_types(dummy);

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

	DummyTypePool dummy = create_dummy_types();

	MemberInfo member = dummy_member();

	ReferenceType reference{};
	reference.is_opt = false;
	reference.is_multi = false;
	reference.is_mut = false;

	const TypeId a = type_create_composite(dummy.types, TypeTag::Composite, TypeId::INVALID, TypeDisposition::User, SourceId::INVALID, 0, false);

	const TypeId b = type_create_composite(dummy.types, TypeTag::Composite, TypeId::INVALID, TypeDisposition::User, SourceId::INVALID, 0, false);

	reference.referenced_type_id = a;
	const TypeId p_a = type_create_reference(dummy.types, TypeTag::Ptr, reference);

	reference.referenced_type_id = b;
	const TypeId p_b = type_create_reference(dummy.types, TypeTag::Ptr, reference);

	member.type.complete = p_b;
	TEST_EQUAL(type_add_composite_member(dummy.types, a, member), true);

	member.type.complete = p_a;
	TEST_EQUAL(type_add_composite_member(dummy.types, b, member), true);

	type_seal_composite(dummy.types, a, 8, 8, 8);

	type_seal_composite(dummy.types, b, 8, 8, 8);

	const bool equal = type_is_equal(dummy.types, a, b);

	TEST_EQUAL(equal, true);

	release_dummy_types(dummy);

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

	DummyTypePool dummy = create_dummy_types();

	MemberInfo member = dummy_member();

	ReferenceType reference{};
	reference.is_opt = false;
	reference.is_multi = false;
	reference.is_mut = false;

	const TypeId a = type_create_composite(dummy.types, TypeTag::Composite, TypeId::INVALID, TypeDisposition::User, SourceId::INVALID, 0, false);

	const TypeId b = type_create_composite(dummy.types, TypeTag::Composite, TypeId::INVALID, TypeDisposition::User, SourceId::INVALID, 0, false);

	reference.referenced_type_id = a;
	const TypeId p_a = type_create_reference(dummy.types, TypeTag::Ptr, reference);

	reference.referenced_type_id = b;
	const TypeId p_b = type_create_reference(dummy.types, TypeTag::Ptr, reference);

	member.name = static_cast<IdentifierId>(1);
	member.type.complete = p_b;
	TEST_EQUAL(type_add_composite_member(dummy.types, a, member), true);

	member.name = static_cast<IdentifierId>(2);
	member.type.complete = type_create_numeric(dummy.types, TypeTag::Integer, NumericType{ 8, false });
	TEST_EQUAL(type_add_composite_member(dummy.types, a, member), true);

	member.name = static_cast<IdentifierId>(1);
	member.type.complete = p_a;
	TEST_EQUAL(type_add_composite_member(dummy.types, b, member), true);

	member.name = static_cast<IdentifierId>(2);
	member.type.complete = type_create_numeric(dummy.types, TypeTag::Integer, NumericType{ 64, false });
	TEST_EQUAL(type_add_composite_member(dummy.types, b, member), true);

	type_seal_composite(dummy.types, a, 8, 1, 8);

	type_seal_composite(dummy.types, b, 8, 1, 8);

	const bool equal = type_is_equal(dummy.types, a, b);

	TEST_EQUAL(equal, false);

	release_dummy_types(dummy);

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

	DummyTypePool dummy = create_dummy_types();

	ReferenceType reference{};
	reference.is_opt = false;
	reference.is_multi = false;
	reference.is_mut = false;

	MemberInfo member = dummy_member();

	const TypeId a1 = type_create_composite(dummy.types, TypeTag::Composite, TypeId::INVALID, TypeDisposition::User, SourceId::INVALID, 0, false);

	const TypeId a2 = type_create_composite(dummy.types, TypeTag::Composite, TypeId::INVALID, TypeDisposition::User, SourceId::INVALID, 0, false);

	const TypeId b1 = type_create_composite(dummy.types, TypeTag::Composite, TypeId::INVALID, TypeDisposition::User, SourceId::INVALID, 0, false);

	const TypeId b2 = type_create_composite(dummy.types, TypeTag::Composite, TypeId::INVALID, TypeDisposition::User, SourceId::INVALID, 0, false);

	reference.referenced_type_id = a1;
	const TypeId p_a1 = type_create_reference(dummy.types, TypeTag::Ptr, reference);

	reference.referenced_type_id = a2;
	const TypeId p_a2 = type_create_reference(dummy.types, TypeTag::Ptr, reference);

	reference.referenced_type_id = b1;
	const TypeId p_b1 = type_create_reference(dummy.types, TypeTag::Ptr, reference);

	reference.referenced_type_id = b2;
	const TypeId p_b2 = type_create_reference(dummy.types, TypeTag::Ptr, reference);

	member.type.complete = p_a2;
	TEST_EQUAL(type_add_composite_member(dummy.types, a1, member), true);

	member.type.complete = p_a1;
	TEST_EQUAL(type_add_composite_member(dummy.types, a2, member), true);

	member.type.complete = p_b2;
	TEST_EQUAL(type_add_composite_member(dummy.types, b1, member), true);

	member.type.complete = p_b1;
	TEST_EQUAL(type_add_composite_member(dummy.types, b2, member), true);

	member.name = static_cast<IdentifierId>(404);

	member.type.complete = type_create_numeric(dummy.types, TypeTag::Integer, NumericType{ 32, false });
	TEST_EQUAL(type_add_composite_member(dummy.types, a1, member), true);
	TEST_EQUAL(type_add_composite_member(dummy.types, b1, member), true);
	
	member.type.complete = type_create_numeric(dummy.types, TypeTag::Integer, NumericType{ 64, false });
	TEST_EQUAL(type_add_composite_member(dummy.types, a2, member), true);
	TEST_EQUAL(type_add_composite_member(dummy.types, b2, member), true);

	type_seal_composite(dummy.types, a1, 8, 8, 8);

	type_seal_composite(dummy.types, a2, 8, 8, 8);

	type_seal_composite(dummy.types, b1, 8, 8, 8);

	type_seal_composite(dummy.types, b2, 8, 8, 8);

	const bool a1_b1_equal = type_is_equal(dummy.types, a1, b1);

	TEST_EQUAL(a1_b1_equal, true);

	const bool a2_b2_equal = type_is_equal(dummy.types, a2, b2);

	TEST_EQUAL(a2_b2_equal, true);

	release_dummy_types(dummy);

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

	DummyTypePool dummy = create_dummy_types();

	ReferenceType reference{};
	reference.is_opt = false;
	reference.is_multi = false;
	reference.is_mut = false;

	MemberInfo member = dummy_member();

	const TypeId a1 = type_create_composite(dummy.types, TypeTag::Composite, TypeId::INVALID, TypeDisposition::User, SourceId::INVALID, 0, false);

	const TypeId a2 = type_create_composite(dummy.types, TypeTag::Composite, TypeId::INVALID, TypeDisposition::User, SourceId::INVALID, 0, false);

	const TypeId b1 = type_create_composite(dummy.types, TypeTag::Composite, TypeId::INVALID, TypeDisposition::User, SourceId::INVALID, 0, false);

	const TypeId b2 = type_create_composite(dummy.types, TypeTag::Composite, TypeId::INVALID, TypeDisposition::User, SourceId::INVALID, 0, false);

	reference.referenced_type_id = a1;
	const TypeId p_a1 = type_create_reference(dummy.types, TypeTag::Ptr, reference);

	reference.referenced_type_id = a2;
	const TypeId p_a2 = type_create_reference(dummy.types, TypeTag::Ptr, reference);

	reference.referenced_type_id = b1;
	const TypeId p_b1 = type_create_reference(dummy.types, TypeTag::Ptr, reference);

	reference.referenced_type_id = b2;
	const TypeId p_b2 = type_create_reference(dummy.types, TypeTag::Ptr, reference);

	member.type.complete = p_a2;
	TEST_EQUAL(type_add_composite_member(dummy.types, a1, member), true);

	member.type.complete = p_a1;
	TEST_EQUAL(type_add_composite_member(dummy.types, a2, member), true);

	member.type.complete = p_b2;
	TEST_EQUAL(type_add_composite_member(dummy.types, b1, member), true);

	member.type.complete = p_b1;
	TEST_EQUAL(type_add_composite_member(dummy.types, b2, member), true);

	member.name = static_cast<IdentifierId>(404);

	member.type.complete = type_create_numeric(dummy.types, TypeTag::Integer, NumericType{ 32, false });
	TEST_EQUAL(type_add_composite_member(dummy.types, a1, member), true);
	TEST_EQUAL(type_add_composite_member(dummy.types, b1, member), true);
	
	member.type.complete = type_create_numeric(dummy.types, TypeTag::Integer, NumericType{ 64, false });
	TEST_EQUAL(type_add_composite_member(dummy.types, a2, member), true);
	TEST_EQUAL(type_add_composite_member(dummy.types, b2, member), true);

	type_seal_composite(dummy.types, a1, 8, 8, 8);

	type_seal_composite(dummy.types, a2, 8, 8, 8);

	type_seal_composite(dummy.types, b1, 8, 8, 8);

	type_seal_composite(dummy.types, b2, 8, 8, 8);

	const bool a1_a2_equal = type_is_equal(dummy.types, a1, a2);

	TEST_EQUAL(a1_a2_equal, false);

	const bool b1_b2_equal = type_is_equal(dummy.types, b1, b2);

	TEST_EQUAL(b1_b2_equal, false);

	const bool a1_b2_equal = type_is_equal(dummy.types, a1, b2);

	TEST_EQUAL(a1_b2_equal, false);

	const bool a2_b1_equal = type_is_equal(dummy.types, a2, b1);

	TEST_EQUAL(a2_b1_equal, false);

	release_dummy_types(dummy);

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

	DummyTypePool dummy = create_dummy_types();

	const TypeId a = type_create_composite(dummy.types, TypeTag::Composite, TypeId::INVALID, TypeDisposition::User, SourceId::INVALID, 0, false);

	const TypeId b = type_create_composite(dummy.types, TypeTag::Composite, TypeId::INVALID, TypeDisposition::User, SourceId::INVALID, 0, false);

	MemberInfo member = dummy_member();
	member.type.complete = type_create_numeric(dummy.types, TypeTag::Integer, NumericType{ 32, false });

	TEST_EQUAL(type_add_composite_member(dummy.types, a, member), true);
	TEST_EQUAL(type_add_composite_member(dummy.types, b, member), true);

	member.name = static_cast<IdentifierId>(9001);

	TEST_EQUAL(type_add_composite_member(dummy.types, a, member), true);
	TEST_EQUAL(type_add_composite_member(dummy.types, b, member), true);

	type_seal_composite(dummy.types, a, 8, 8, 8);

	type_seal_composite(dummy.types, b, 8, 8, 8);

	const bool equal = type_is_equal(dummy.types, a, b);

	TEST_EQUAL(equal, true);

	release_dummy_types(dummy);

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
