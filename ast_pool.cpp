#include "pass_data.hpp"

#include "ast2_attach.hpp"
#include "infra/common.hpp"
#include "infra/container.hpp"

struct AstPool
{
	ReservedVec<u32> pool;
};

// let typeinfo = eval func(t : type) -> TypeInfo
static void builtin_typeinfo(Interpreter* interpreter) noexcept
{
	interpreter;

	// TODO
}

// let import = eval func(pathspec: []u8) -> type
static void builtin_import(Interpreter* interpreter) noexcept
{
	interpreter;

	// TODO
}

static a2::AstBuilderToken push_builtin_def(a2::AstBuilder* builder, IdentifierPool* identifiers, Range<char8> name, a2::BuiltinData::BuiltinSignature function) noexcept
{
	const a2::AstBuilderToken val_token = a2::push_node(builder, a2::AstBuilder::NO_CHILDREN, a2::AstFlag::EMPTY, a2::BuiltinData{ function });

	return a2::push_node(builder, val_token, a2::AstFlag::EMPTY, a2::DefinitionData{ id_from_identifier(identifiers, name), /* TODO */ INVALID_TYPE_ID });
}

template<typename T>
static TypeId push_type_def(a2::AstBuilder* builder, IdentifierPool* identifiers, Range<char8> name, TypePool* types, ValuePool* values, TypeId type_type_id, TypeTag tag, TypeFlag flags, const T& type) noexcept
{
	const IdentifierId identifier_id = id_from_identifier(identifiers, name);

	const TypeId type_id = id_from_type(types, tag, flags, range::from_object_bytes(&type));

	const ValueLocation value = alloc_value(values, sizeof(TypeId));

	value.ptr->header.type_id = type_type_id == INVALID_TYPE_ID ? type_id : type_type_id;

	*access_value<TypeId>(value.ptr) = type_id;

	a2::push_node(builder, a2::AstBuilder::NO_CHILDREN, a2::AstFlag::EMPTY, a2::DefinitionData{ identifier_id, type_type_id == INVALID_TYPE_ID ? type_id : type_type_id, value.id });

	return type_id;
}

static TypeId push_type_def(a2::AstBuilder* builder, IdentifierPool* identifiers, Range<char8> name, TypePool* types, ValuePool* values, TypeId type_type_id, TypeTag tag, TypeFlag flags) noexcept
{
	const IdentifierId identifier_id = id_from_identifier(identifiers, name);

	const TypeId type_id = id_from_type(types, tag, flags, Range<byte>{});

	const ValueLocation value = alloc_value(values, sizeof(TypeId));

	value.ptr->header.type_id = type_type_id == INVALID_TYPE_ID ? type_id : type_type_id;

	*access_value<TypeId>(value.ptr) = type_id;

	a2::push_node(builder, a2::AstBuilder::NO_CHILDREN, a2::AstFlag::EMPTY, a2::DefinitionData{ identifier_id, type_type_id == INVALID_TYPE_ID ? type_id : type_type_id, value.id });

	return type_id;
}

static void push_std_import(a2::AstBuilder* builder, IdentifierPool* identifiers) noexcept
{
	const a2::AstBuilderToken import_identifier_token = a2::push_node(builder, a2::AstBuilder::NO_CHILDREN, a2::AstFlag::EMPTY, a2::ValIdentifierData{ id_from_identifier(identifiers, range::from_literal_string("import")) });

	a2::push_node(builder, a2::AstBuilder::NO_CHILDREN, a2::AstFlag::EMPTY, a2::ValStringData{ id_from_identifier(identifiers, range::from_literal_string("@std.src")) });

	const a2::AstBuilderToken import_call_token = a2::push_node(builder, import_identifier_token, a2::AstTag::Call, a2::AstFlag::EMPTY);

	a2::push_node(builder, import_call_token, a2::AstFlag::EMPTY, a2::DefinitionData{ id_from_identifier(identifiers, range::from_literal_string("std")), /* TODO */ INVALID_TYPE_ID });
}

static TypeId push_typeinfo_def(a2::AstBuilder* builder, IdentifierPool* identifiers, TypePool* types, TypeId u64_type_id, TypeId type_type_id) noexcept
{
	static constexpr u32 TYPEINFO_MEMBER_COUNT = 3;

	byte typeinfo_buf[sizeof(CompositeTypeHeader) + sizeof(CompositeTypeMember) * TYPEINFO_MEMBER_COUNT];
	*reinterpret_cast<CompositeTypeHeader*>(typeinfo_buf) = { 24, 8, 3 };

	CompositeTypeMember* const members = reinterpret_cast<CompositeTypeMember*>(typeinfo_buf + sizeof(CompositeTypeHeader));
	members[0] = { id_from_identifier(identifiers, range::from_literal_string("size")), u64_type_id, 0, false, true, false, false };
	members[1] = { id_from_identifier(identifiers, range::from_literal_string("alignment")), u64_type_id, 8, false, true, false, false };
	members[2] = { id_from_identifier(identifiers, range::from_literal_string("type")), type_type_id, 16, false, true, false, false };

	const TypeId typeinfo_type_id = id_from_type(types, TypeTag::Composite, TypeFlag::EMPTY, Range{ typeinfo_buf });

	const a2::AstBuilderToken std_token = a2::push_node(builder, a2::AstBuilder::NO_CHILDREN, a2::AstFlag::EMPTY, a2::ValIdentifierData{ id_from_identifier(identifiers, range::from_literal_string("std")) });

	a2::push_node(builder, a2::AstBuilder::NO_CHILDREN, a2::AstFlag::EMPTY, a2::ValIdentifierData{ id_from_identifier(identifiers, range::from_literal_string("build_typeinfo_type")) });

	const a2::AstBuilderToken member_token = a2::push_node(builder, std_token, a2::AstTag::OpMember, a2::AstFlag::EMPTY);

	const a2::AstBuilderToken call_token = a2::push_node(builder, member_token, a2::AstTag::Call, a2::AstFlag::EMPTY);

	a2::push_node(builder, call_token, a2::AstFlag::EMPTY, a2::DefinitionData{ id_from_identifier(identifiers, range::from_literal_string("typeinfo")), type_type_id });

	return typeinfo_type_id;
}

AstPool* create_ast_pool(AllocPool* pool) noexcept
{
	AstPool* const asts = static_cast<AstPool*>(alloc_from_pool(pool, sizeof(AstPool), alignof(AstPool)));

	asts->pool.init(1u << 30, 1u << 18);

	return asts;
}

void release_ast_pool(AstPool* asts) noexcept
{
	asts->pool.release();
}

a2::AstNode* alloc_ast(AstPool* asts, u32 dwords) noexcept
{
	return static_cast<a2::AstNode*>(asts->pool.reserve_exact(dwords * sizeof(u32)));
}

a2::AstNode* create_builtin_definitions(AstPool* asts, IdentifierPool* identifiers, TypePool* types, ValuePool* values, a2::AstBuilder* builder) noexcept
{
	const a2::AstBuilderToken first_child_token = push_builtin_def(builder, identifiers, range::from_literal_string("typeinfo"), &builtin_typeinfo);

	push_builtin_def(builder, identifiers, range::from_literal_string("import"), &builtin_import);

	push_std_import(builder, identifiers);

	const TypeId type_type_id = push_type_def(builder, identifiers, range::from_literal_string("type"), types, values, INVALID_TYPE_ID, TypeTag::Type, TypeFlag::EMPTY);

	push_type_def(builder, identifiers, range::from_literal_string("bool"), types, values, type_type_id, TypeTag::Boolean, TypeFlag::EMPTY);

	push_type_def(builder, identifiers, range::from_literal_string("void"), types, values, type_type_id, TypeTag::Void, TypeFlag::EMPTY);

	push_type_def(builder, identifiers, range::from_literal_string("f32"), types, values, type_type_id, TypeTag::Float, TypeFlag::EMPTY, FloatType{ 32 });

	push_type_def(builder, identifiers, range::from_literal_string("f64"), types, values, type_type_id, TypeTag::Float, TypeFlag::EMPTY, FloatType{ 64 });

	push_type_def(builder, identifiers, range::from_literal_string("u8"), types, values, type_type_id, TypeTag::Integer, TypeFlag::EMPTY, IntegerType{ 8 });

	push_type_def(builder, identifiers, range::from_literal_string("u16"), types, values, type_type_id, TypeTag::Integer, TypeFlag::EMPTY, IntegerType{ 16 });

	push_type_def(builder, identifiers, range::from_literal_string("u32"), types, values, type_type_id, TypeTag::Integer, TypeFlag::EMPTY, IntegerType{ 32 });

	const TypeId u64_type_id = push_type_def(builder, identifiers, range::from_literal_string("u64"), types, values, type_type_id, TypeTag::Integer, TypeFlag::EMPTY, IntegerType{ 64 });

	push_type_def(builder, identifiers, range::from_literal_string("s8"), types, values, type_type_id, TypeTag::Integer, TypeFlag::Integer_IsSigned, IntegerType{ 8 });

	push_type_def(builder, identifiers, range::from_literal_string("s16"), types, values, type_type_id, TypeTag::Integer, TypeFlag::Integer_IsSigned, IntegerType{ 16 });

	push_type_def(builder, identifiers, range::from_literal_string("s32"), types, values, type_type_id, TypeTag::Integer, TypeFlag::Integer_IsSigned, IntegerType{ 32 });

	push_type_def(builder, identifiers, range::from_literal_string("s64"), types, values, type_type_id, TypeTag::Integer, TypeFlag::Integer_IsSigned, IntegerType{ 64 });

	push_typeinfo_def(builder, identifiers, types, u64_type_id, type_type_id);

	a2::push_node(builder, first_child_token, a2::AstFlag::EMPTY, a2::BlockData{ 17 });

	return a2::complete_ast(builder, asts);
}
