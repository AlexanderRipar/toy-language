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

	TypePool* const types = create_type_pool2(alloc, errors);

	return { types, reader, identifiers, errors, alloc };

}

static void release_dummy_types(DummyTypePool dummy) noexcept
{
	release_type_pool2(dummy.types);

	release_error_sink(dummy.errors);

	release_source_reader(dummy.reader);

	release_identifier_pool(dummy.identifiers);

	release_alloc_pool(dummy.alloc);
}


static void primitive_type_with_integer_returns_integer_type_structure() noexcept
{
	TEST_BEGIN;

	DummyTypePool dummy = create_dummy_types();

	IntegerType2 u16_type{ 16, false };

	TypeId u16_id = primitive_type(dummy.types, TypeTag::Integer, range::from_object_bytes(&u16_type));

	TEST_UNEQUAL(u16_id, INVALID_TYPE_ID_2);

	const OptPtr<TypeStructure> opt_u16_structure = type_structure_from_id(dummy.types, u16_id);

	TEST_EQUAL(is_some(opt_u16_structure), true);

	TypeStructure* const u16_structure = get_ptr(opt_u16_structure);

	TEST_EQUAL(u16_structure->tag, TypeTag::Integer);

	TEST_EQUAL(u16_structure->bytes, sizeof(IntegerType2));

	TEST_EQUAL(data<IntegerType2>(u16_structure)->bits, 16);

	TEST_EQUAL(data<IntegerType2>(u16_structure)->is_signed, false);

	release_dummy_types(dummy);

	TEST_END;
}

static void primitive_type_with_integer_and_float_with_same_bit_pattern_returns_different_type_structures() noexcept
{
	TEST_BEGIN;

	DummyTypePool dummy = create_dummy_types();

	IntegerType2 u32_type{ 32, false };

	const TypeId u32_id = primitive_type(dummy.types, TypeTag::Integer, range::from_object_bytes(&u32_type));

	TEST_UNEQUAL(u32_id, INVALID_TYPE_ID_2);

	const OptPtr<TypeStructure> opt_u32_structure = type_structure_from_id(dummy.types, u32_id);

	TEST_EQUAL(is_some(opt_u32_structure), true);

	TypeStructure* const u32_structure = get_ptr(opt_u32_structure);

	TEST_EQUAL(u32_structure->tag, TypeTag::Integer);

	TEST_EQUAL(u32_structure->bytes, sizeof(IntegerType2));

	TEST_EQUAL(data<IntegerType2>(u32_structure)->bits, 32);

	TEST_EQUAL(data<IntegerType2>(u32_structure)->is_signed, false);

	FloatType2 f32_type{ 32 };

	const TypeId f32_id = primitive_type(dummy.types, TypeTag::Float, range::from_object_bytes(&f32_type));

	TEST_UNEQUAL(f32_id, INVALID_TYPE_ID_2);

	OptPtr<TypeStructure> opt_f32_structure = type_structure_from_id(dummy.types, f32_id);

	TEST_EQUAL(is_some(opt_f32_structure), true);

	TypeStructure* const f32_structure = get_ptr(opt_f32_structure);

	TEST_EQUAL(f32_structure->tag, TypeTag::Float);

	TEST_EQUAL(f32_structure->bytes, 4);

	TEST_EQUAL(data<FloatType2>(f32_structure)->bits, 32);

	TEST_UNEQUAL(u32_id, f32_id);

	TEST_UNEQUAL(u32_structure, f32_structure);

	release_dummy_types(dummy);

	TEST_END;
}

static void primitive_type_with_array_returns_array_type() noexcept
{
	TEST_BEGIN;

	DummyTypePool dummy = create_dummy_types();

	IntegerType2 s32_type{ 32, true };

	const TypeId s32_id = primitive_type(dummy.types, TypeTag::Integer, range::from_object_bytes(&s32_type));

	TEST_UNEQUAL(s32_id, INVALID_TYPE_ID_2);

	ArrayType2 array_type{ s32_id, 128 };

	const TypeId array_id = primitive_type(dummy.types, TypeTag::Array, range::from_object_bytes(&array_type));

	TEST_UNEQUAL(array_id, INVALID_TYPE_ID_2);

	OptPtr<TypeStructure> opt_array_structure = type_structure_from_id(dummy.types, array_id);

	TEST_EQUAL(is_some(opt_array_structure), true);

	TypeStructure* const array_structure = get_ptr(opt_array_structure);

	TEST_EQUAL(array_structure->tag, TypeTag::Array);

	TEST_EQUAL(array_structure->bytes, 16);

	TEST_EQUAL(data<ArrayType2>(array_structure)->element_count, 128);

	TEST_EQUAL(data<ArrayType2>(array_structure)->element_type, s32_id);

	release_dummy_types(dummy);

	TEST_END;
}


static void create_type_builder_returns_type_builder() noexcept
{
	TEST_BEGIN;

	DummyTypePool dummy = create_dummy_types();

	TypeBuilder* const builder = create_type_builder(dummy.types, SourceId{ 1 });

	TEST_UNEQUAL(builder, nullptr);

	release_dummy_types(dummy);

	TEST_END;
}


void type_pool2_tests() noexcept
{
	TEST_MODULE_BEGIN;


	primitive_type_with_integer_returns_integer_type_structure();

	primitive_type_with_integer_and_float_with_same_bit_pattern_returns_different_type_structures();

	primitive_type_with_array_returns_array_type();


	create_type_builder_returns_type_builder();

	TEST_MODULE_END;
}
