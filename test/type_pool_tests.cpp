#include "test_helpers.hpp"
#include "../core/pass_data.hpp"

struct DummyTypePool
{
	TypePool* types;

	SourceReader* reader;

	IdentifierPool* identifiers;

	ErrorSink* errors;

	AllocPool* alloc;
};

static DummyTypePool create_dummy_types() noexcept
{
	AllocPool* const alloc = create_alloc_pool(1 << 12, 1 << 12);

	SourceReader* reader = create_source_reader(alloc);

	IdentifierPool* identifiers = create_identifier_pool(alloc);

	ErrorSink* errors = create_error_sink(alloc, reader, identifiers);

	TypePool* const types = create_type_pool(alloc, errors);

	return { types, reader, identifiers, errors, alloc };

}

static void release_dummy_types(DummyTypePool dummy) noexcept
{
	release_type_pool(dummy.types);

	release_error_sink(dummy.errors);

	release_source_reader(dummy.reader);

	release_identifier_pool(dummy.identifiers);

	release_alloc_pool(dummy.alloc);
}


static void primitive_type_with_integer_returns_integer_type_structure() noexcept
{
	TEST_BEGIN;

	DummyTypePool dummy = create_dummy_types();

	NumericType u16_type{ 16, false };

	TypeId u16_id = simple_type(dummy.types, TypeTag::Integer, range::from_object_bytes(&u16_type));

	TEST_UNEQUAL(u16_id.rep, INVALID_TYPE_ID.rep);

	TEST_EQUAL(type_tag_from_id(dummy.types, u16_id), TypeTag::Integer);

	const NumericType* const interned = static_cast<const NumericType*>(primitive_type_structure(dummy.types, u16_id));

	TEST_EQUAL(interned->bits, 16);

	TEST_EQUAL(interned->is_signed, false);

	release_dummy_types(dummy);

	TEST_END;
}

static void primitive_type_with_integer_and_float_with_same_bit_pattern_returns_different_type_structures() noexcept
{
	TEST_BEGIN;

	DummyTypePool dummy = create_dummy_types();

	NumericType u32_type{ 32, false };

	const TypeId u32_id = simple_type(dummy.types, TypeTag::Integer, range::from_object_bytes(&u32_type));

	TEST_UNEQUAL(u32_id.rep, INVALID_TYPE_ID.rep);

	TEST_EQUAL(type_tag_from_id(dummy.types, u32_id), TypeTag::Integer);

	const NumericType* const interned_u32 = static_cast<const NumericType*>(primitive_type_structure(dummy.types, u32_id));

	TEST_EQUAL(interned_u32->bits, 32);

	TEST_EQUAL(interned_u32->is_signed, false);

	NumericType f32_type{ 32, false };

	const TypeId f32_id = simple_type(dummy.types, TypeTag::Float, range::from_object_bytes(&f32_type));

	TEST_UNEQUAL(f32_id.rep, INVALID_TYPE_ID.rep);

	TEST_EQUAL(type_tag_from_id(dummy.types, f32_id), TypeTag::Float);

	const NumericType* const interned_f32 = static_cast<const NumericType*>(primitive_type_structure(dummy.types, f32_id));

	TEST_EQUAL(interned_f32->bits, 32);

	TEST_UNEQUAL(u32_id.rep, f32_id.rep);

	TEST_UNEQUAL(interned_u32, interned_f32);

	release_dummy_types(dummy);

	TEST_END;
}

static void primitive_type_with_array_returns_array_type() noexcept
{
	TEST_BEGIN;

	DummyTypePool dummy = create_dummy_types();

	NumericType s32_type{ 32, true };

	const TypeId s32_id = simple_type(dummy.types, TypeTag::Integer, range::from_object_bytes(&s32_type));

	TEST_UNEQUAL(s32_id.rep, INVALID_TYPE_ID.rep);

	ArrayType array_type{ 128, s32_id };

	const TypeId array_id = simple_type(dummy.types, TypeTag::Array, range::from_object_bytes(&array_type));

	TEST_UNEQUAL(array_id.rep, INVALID_TYPE_ID.rep);

	TEST_EQUAL(type_tag_from_id(dummy.types, array_id), TypeTag::Array);

	const ArrayType* const interned = static_cast<const ArrayType*>(primitive_type_structure(dummy.types, array_id));

	TEST_EQUAL(interned->element_count, 128);

	TEST_EQUAL(interned->element_type.rep, s32_id.rep);

	release_dummy_types(dummy);

	TEST_END;
}



void type_pool_tests() noexcept
{
	TEST_MODULE_BEGIN;


	primitive_type_with_integer_returns_integer_type_structure();

	primitive_type_with_integer_and_float_with_same_bit_pattern_returns_different_type_structures();

	primitive_type_with_array_returns_array_type();

	TEST_MODULE_END;
}
