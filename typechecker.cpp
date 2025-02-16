#include "pass_data.hpp"

#include "ast2_attach.hpp"
#include "ast2_helper.hpp"

struct TypeBuilderMember
{
	IdentifierId identifier_id;

	u32 unused_;

	OptPtr<a2::AstNode> type_expr;

	OptPtr<a2::AstNode> value_expr;

	u64 offset : 60; // when is_global: offset into global data segment; otherwise offset inside instances of type.

	u64 is_mut : 1;

	u64 is_pub : 1;

	u64 is_global : 1;

	u64 is_use : 1;
};

struct TypeBuilder
{
	u32 used;

	s32 next_offset;

	s32 tail_offset;

	u32 unused_[5];

	TypeBuilderMember members[7];
};

static_assert(sizeof(TypeBuilder) == 256);

struct Typechecker
{
	Interpreter* interpreter;

	ScopePool* scopes;

	TypePool* types;

	IdentifierPool* identifiers;

	ReservedVec<TypeBuilder> builders;

	s32 first_free_builder_index;
};

static void release_type_builder(Typechecker* typechecker, TypeBuilder* builder) noexcept
{
	TypeBuilder* const tail = builder + builder->tail_offset;

	if (typechecker->first_free_builder_index < 0)
		tail->next_offset = 0;
	else
		tail->next_offset = static_cast<s32>(typechecker->builders.begin() + typechecker->first_free_builder_index - tail);

		typechecker->first_free_builder_index = static_cast<s32>(builder - typechecker->builders.begin());
}

static TypeId typecheck_parameter(Typechecker* typechecker, Scope* enclosing_scope, a2::AstNode* parameter) noexcept
{
	ASSERT_OR_IGNORE(parameter->tag == a2::AstTag::Definition);

	a2::DefinitionData* const definition_data = a2::attachment_of<a2::DefinitionData>(parameter);

	const a2::DefinitionInfo definition_info = a2::definition_info(parameter);

	if (is_none(definition_info.type))
		panic("Untyped parameter definitions are not currently supported\n");

	Value* const type_value = interpret_expr(typechecker->interpreter, enclosing_scope, a2::first_child_of(parameter));

	if (dealias_type_entry(typechecker->types, type_value->header.type_id)->tag != TypeTag::Type)
		panic("Expected type expression after ':'\n");

	const TypeId type_id = *access_value<TypeId>(type_value);

	release_interpretation_result(typechecker->interpreter, type_value);

	definition_data->type_id = type_id;

	return type_id;
}

static TypeId interpret_type_expr(Typechecker* typechecker, Scope* enclosing_scope, a2::AstNode* expr) noexcept
{
	Value* const type_value = interpret_expr(typechecker->interpreter, enclosing_scope, expr);

	if (dealias_type_entry(typechecker->types, type_value->header.type_id)->tag != TypeTag::Type)
		panic("Expected type expression\n");

	const TypeId type_id = *access_value<TypeId>(type_value);

	release_interpretation_result(typechecker->interpreter, type_value);

	return type_id;
}

Typechecker* create_typechecker(AllocPool* alloc, Interpreter* Interpreter, ScopePool* scopes, TypePool* types, IdentifierPool* identifiers) noexcept
{
	Typechecker* const typechecker = static_cast<Typechecker*>(alloc_from_pool(alloc, sizeof(Typechecker), alignof(Typechecker)));

	typechecker->interpreter = Interpreter;
	typechecker->scopes = scopes;
	typechecker->types = types;
	typechecker->identifiers = identifiers;

	typechecker->builders.init(1u << 16, 1u << 7);
	typechecker->first_free_builder_index = -1;

	return typechecker;
}

void release_typechecker(Typechecker* typechecker) noexcept
{
	typechecker->builders.release();
}

TypeId typecheck_expr(Typechecker* typechecker, Scope* enclosing_scope, a2::AstNode* expr) noexcept
{
	switch (expr->tag)
	{
	case a2::AstTag::ValInteger:
	{
		return get_builtin_type_ids(typechecker->types)->comp_integer_type_id;
	}

	case a2::AstTag::ValFloat:
	{
		return get_builtin_type_ids(typechecker->types)->comp_float_type_id;
	}

	case a2::AstTag::ValChar:
	{
		return get_builtin_type_ids(typechecker->types)->comp_integer_type_id;
	}

	case a2::AstTag::ValString:
	{
		return get_builtin_type_ids(typechecker->types)->comp_string_type_id;
	}

	case a2::AstTag::ValIdentifer:
	{
		a2::ValIdentifierData* const identifier_data = a2::attachment_of<a2::ValIdentifierData>(expr);

		const ScopeLookupResult lookup = lookup_identifier_recursive(enclosing_scope, identifier_data->identifier_id);

		if (!is_valid(lookup))
		{
			const Range<char8> name = identifier_entry_from_id(typechecker->identifiers, identifier_data->identifier_id)->range();

			panic("Could not find definition for identifier '%.*s'\n", static_cast<s32>(name.count()), name.begin());
		}

		a2::AstNode* const definition = lookup.definition;

		a2::DefinitionData* const definition_data = a2::attachment_of<a2::DefinitionData>(definition);

		if (definition_data->type_id == INVALID_TYPE_ID)
			return typecheck_definition(typechecker, lookup.enclosing_scope, definition);

		return definition_data->type_id;
	}

	case a2::AstTag::OpLogAnd:
	case a2::AstTag::OpLogOr:
	{
		a2::AstNode* const lhs = a2::first_child_of(expr);

		a2::AstNode* const rhs = a2::next_sibling_of(lhs);

		const TypeId lhs_type_id = typecheck_expr(typechecker, enclosing_scope, lhs);

		const TypeId rhs_type_id = typecheck_expr(typechecker, enclosing_scope, rhs);

		if (dealias_type_entry(typechecker->types, lhs_type_id)->tag != TypeTag::Boolean)
			panic("Left-hand-side of '%s' must be of type bool\n", a2::tag_name(expr->tag));

		if (dealias_type_entry(typechecker->types, rhs_type_id)->tag != TypeTag::Boolean)
			panic("Right-hand-side of '%s' must be of type bool\n", a2::tag_name(expr->tag));

		return get_builtin_type_ids(typechecker->types)->bool_type_id;
	}

	case a2::AstTag::OpTypeArray:
	{
		a2::AstNode* const count = a2::first_child_of(expr);

		Value* const count_value = interpret_expr(typechecker->interpreter, enclosing_scope, count);

		TypeEntry* const count_type = dealias_type_entry(typechecker->types, count_value->header.type_id);

		u64 the_count;

		if (count_type->tag == TypeTag::CompInteger)
		{
			if (!comp_integer_as_u64(access_value<CompIntegerValue>(count_value), &the_count))
				panic("Array count expression value out of range [0, 2^64-1]\n");
		}
		else if (count_type->tag == TypeTag::Integer)
		{
			IntegerType* const integer_type = count_type->data<IntegerType>();

			if (integer_type->bits == 8)
				the_count = *access_value<u8>(count_value);
			else if (integer_type->bits == 16)
				the_count = *access_value<u16>(count_value);
			else if (integer_type->bits == 32)
				the_count = *access_value<u32>(count_value);
			else if (integer_type->bits == 64)
				the_count = *access_value<u64>(count_value);
			else
				panic("Integer bit width of %u in array count expression is not currently supported\n", integer_type->bits);

			if ((count_type->flags & TypeFlag::Integer_IsSigned) == TypeFlag::Integer_IsSigned && (the_count & (1ui64 << (integer_type->bits - 1))) != 0)
				panic("Array count expression value negative\n");
		}
		else
		{
			panic("Unexpected non-integer type in array count expression\n");
		}

		release_interpretation_result(typechecker->interpreter, count_value);

		a2::AstNode* const element_type = a2::next_sibling_of(count);

		Value* const element_type_value = interpret_expr(typechecker->interpreter, enclosing_scope, element_type);

		if (dealias_type_entry(typechecker->types, element_type_value->header.type_id)->tag != TypeTag::Type)
			panic("Expected type expression as array's element type\n");

		const TypeId element_type_id = *access_value<TypeId>(element_type_value);

		release_interpretation_result(typechecker->interpreter, element_type_value);

		ArrayType array_type{};
		array_type.count = the_count;
		array_type.element_id = element_type_id;

		const TypeId array_type_id = id_from_type(typechecker->types, TypeTag::Array, TypeFlag::EMPTY, range::from_object_bytes(&array_type));

		return id_from_type(typechecker->types, TypeTag::Type, TypeFlag::EMPTY, range::from_object_bytes(&array_type_id));
	}

	case a2::AstTag::UOpTypeSlice:
	case a2::AstTag::UOpTypeMultiPtr:
	case a2::AstTag::UOpTypeOptMultiPtr:
	case a2::AstTag::UOpTypeOptPtr:
	case a2::AstTag::UOpTypePtr:
	{
		Value* const pointer_type_value = interpret_expr(typechecker->interpreter, enclosing_scope, expr);

		ASSERT_OR_IGNORE(dealias_type_entry(typechecker->types, pointer_type_value->header.type_id)->tag == TypeTag::Type);

		const TypeId pointer_type_id = *access_value<TypeId>(pointer_type_value);

		release_interpretation_result(typechecker->interpreter, pointer_type_value);

		return id_from_type(typechecker->types, TypeTag::Type, TypeFlag::EMPTY, range::from_object_bytes(&pointer_type_id));
	}

	case a2::AstTag::OpArrayIndex:
	{
		a2::AstNode* const array = a2::first_child_of(expr);

		const TypeId array_type_id = typecheck_expr(typechecker, enclosing_scope, array);

		TypeEntry* const entry = dealias_type_entry(typechecker->types, array_type_id);

		TypeId element_type_id;

		if (entry->tag == TypeTag::Array)
		{
			element_type_id = entry->data<ArrayType>()->element_id;
		}
		else if (entry->tag == TypeTag::Slice)
		{
			element_type_id = entry->data<SliceType>()->element_id;
		}
		else if (entry->tag == TypeTag::Ptr && (entry->flags & TypeFlag::Ptr_IsMulti) == TypeFlag::Ptr_IsMulti)
		{
			element_type_id = entry->data<PtrType>()->pointee_id;
		}
		else
		{
			panic("Expected first operand of array index operation to be of array, slice or multi-pointer type\n");
		}

		a2::AstNode* const index = a2::next_sibling_of(array);

		const TypeId index_type_id = typecheck_expr(typechecker, enclosing_scope, index);

		TypeEntry* const index_type_entry = dealias_type_entry(typechecker->types, index_type_id);

		if (index_type_entry->tag != TypeTag::Integer && index_type_entry->tag != TypeTag::CompInteger)
			panic("Expected index operand of array index operation to be of integer type\n");

		return element_type_id;
	}

	case a2::AstTag::Block:
	{
		if (!a2::has_children(expr))
			return get_builtin_type_ids(typechecker->types)->void_type_id;

		a2::DirectChildIterator it = a2::direct_children_of(expr);

		for (OptPtr<a2::AstNode> rst = a2::next(&it); is_some(rst); rst = a2::next(&it))
		{
			a2::AstNode* const child = get_ptr(rst);

			const TypeId child_type_id = typecheck_expr(typechecker, enclosing_scope, child);

			if (!a2::has_next_sibling(child))
				return child_type_id;

			TypeEntry* const child_type_entry = dealias_type_entry(typechecker->types, child_type_id);

			if (child_type_entry->tag != TypeTag::Void)
				panic("Non-void expression at non-terminal position inside block\n");
		}

		ASSERT_UNREACHABLE;
	}

	case a2::AstTag::If:
	{
		const a2::IfInfo if_info = a2::if_info(expr);

		const TypeId condition_type_id = typecheck_expr(typechecker, enclosing_scope, if_info.condition);

		TypeEntry* const condition_type_entry = dealias_type_entry(typechecker->types, condition_type_id);

		if (condition_type_entry->tag != TypeTag::Boolean)
			panic("Expected if condition to be of bool type\n");

		// TODO: Typecheck where
		if (is_some(if_info.where))
			panic("Where clause not supported yet\n");

		const TypeId consequent_type_id = typecheck_expr(typechecker, enclosing_scope, if_info.consequent);

		TypeEntry* const consequent_type_entry = dealias_type_entry(typechecker->types, consequent_type_id);

		if (is_some(if_info.alternative))
		{
			const TypeId alternative_type_id = typecheck_expr(typechecker, enclosing_scope, get_ptr(if_info.alternative));

			TypeEntry* const alternative_type_entry = dealias_type_entry(typechecker->types, alternative_type_id);

			// TODO: Support non-exact matches, especially in case of chained if-elseif-...-else and switch

			const OptPtr<TypeEntry> common_type_entry = find_common_type_entry(typechecker->types, consequent_type_entry, alternative_type_entry);

			if (is_none(common_type_entry))
				panic("Incompatible types between if branches\n");

			return id_from_type_entry(typechecker->types, get_ptr(common_type_entry));
		}
		else if (consequent_type_entry->tag == TypeTag::Void)
		{
			return get_builtin_type_ids(typechecker->types)->void_type_id;
		}
		else
		{
			panic("Body of if without else must be of type void\n");
		}
	}

	case a2::AstTag::Func:
	{
		const a2::FuncInfo func_info = a2::func_info(expr);

		a2::FuncData* const func_data = a2::attachment_of<a2::FuncData>(expr);

		if (is_some(func_info.return_type))
		{
			Value* const return_type_value = interpret_expr(typechecker->interpreter, enclosing_scope, get_ptr(func_info.return_type));

			TypeEntry* return_type_entry = dealias_type_entry(typechecker->types, return_type_value->header.type_id);

			if (return_type_entry->tag != TypeTag::Type)
				panic("Expected type expression as %s's return type\n", a2::has_flag(expr, a2::AstFlag::Func_IsProc) ? "proc" : "func");

			const TypeId return_type_id = *access_value<TypeId>(return_type_value);

			release_interpretation_result(typechecker->interpreter, return_type_value);

			func_data->return_type_id = return_type_id;
		}
		else
		{
			func_data->return_type_id = get_builtin_type_ids(typechecker->types)->void_type_id;
		}

		a2::DirectChildIterator it = a2::direct_children_of(func_info.parameters);

		FuncTypeBuffer type_buf{};

		type_buf.header.return_type_id = func_data->return_type_id;
		type_buf.header.parameter_count = 0;

		for (OptPtr<a2::AstNode> parameter = a2::next(&it); is_some(parameter); parameter = a2::next(&it))
		{
			ASSERT_OR_IGNORE(type_buf.header.parameter_count + 1 < array_count(type_buf.parameter_type_ids));

			type_buf.parameter_type_ids[type_buf.header.parameter_count] = typecheck_parameter(typechecker, enclosing_scope, get_ptr(parameter));

			type_buf.header.parameter_count += 1;
		}

		const TypeFlag flags = a2::has_flag(expr, a2::AstFlag::Func_IsProc) ? TypeFlag::Func_IsProc : TypeFlag::EMPTY;

		const Range<byte> type = Range<byte>{ reinterpret_cast<const byte*>(&type_buf), sizeof(type_buf.header) + type_buf.header.parameter_count * sizeof(type_buf.parameter_type_ids[0]) };

		func_data->signature_type_id = id_from_type(typechecker->types, TypeTag::Func, flags, type);

		if (is_some(func_info.body))
		{
			TypeId const returned_type_id = typecheck_expr(typechecker, enclosing_scope, get_ptr(func_info.body));

			if (!can_implicity_convert_from_to(typechecker->types, returned_type_id, func_data->return_type_id))
				panic("Mismatch between declared and actual return type\n");
		}

		return func_data->signature_type_id;
	}

	case a2::AstTag::File:
	case a2::AstTag::ParameterList:
	case a2::AstTag::Case:
	{
		panic("Unexpected AST node type '%s' passed to typecheck_expr\n", a2::tag_name(expr->tag));
	}

	case a2::AstTag::Call:
	{
		a2::AstNode* const callee = a2::first_child_of(expr);

		const TypeId callee_type_id = typecheck_expr(typechecker, enclosing_scope, callee);

		TypeEntry* const entry = dealias_type_entry(typechecker->types, callee_type_id);

		if (entry->tag != TypeTag::Func)
			panic("Expected func or proc before call\n");

		FuncType* const func_type = entry->data<FuncType>();

		a2::AstNode* curr = callee;

		for (u32 i = 0; i != func_type->header.parameter_count; ++i)
		{
			if (!a2::has_next_sibling(curr))
				panic("Too few parameters in call (expected %u but got %u)\n", func_type->header.parameter_count, i);

			curr = a2::next_sibling_of(curr);

			const TypeId parameter_type_id = typecheck_expr(typechecker, enclosing_scope, curr);

			TypeEntry* const parameter_type_entry = dealias_type_entry(typechecker->types, parameter_type_id);

			TypeEntry* const expected_parameter_type_entry = dealias_type_entry(typechecker->types, func_type->parameter_type_ids[i]);

			if (parameter_type_entry != expected_parameter_type_entry)
				panic("Mismatch between expected and actual call parameter type\n");
		}

		if (a2::has_next_sibling(curr))
		{
			u32 actual_parameter_count = func_type->header.parameter_count;

			do
			{
				curr = a2::next_sibling_of(curr);

				actual_parameter_count += 1;
			}
			while (a2::has_next_sibling(curr));

			panic("Too many parameters in call (expected %u but got %u)\n", func_type->header.parameter_count, actual_parameter_count);
		}

		return func_type->header.return_type_id;
	}

	case a2::AstTag::OpAdd:
	case a2::AstTag::OpSub:
	case a2::AstTag::OpMul:
	case a2::AstTag::OpDiv:
	{
		// TODO
		panic("TODO: typecheck_expr(%u)\n", expr->tag);
	}

	case a2::AstTag::Builtin:
	case a2::AstTag::CompositeInitializer:
	case a2::AstTag::ArrayInitializer:
	case a2::AstTag::Wildcard:
	case a2::AstTag::Where:
	case a2::AstTag::Expects:
	case a2::AstTag::Ensures:
	case a2::AstTag::Definition:
	case a2::AstTag::For:
	case a2::AstTag::ForEach:
	case a2::AstTag::Switch:
	case a2::AstTag::Trait:
	case a2::AstTag::Impl:
	case a2::AstTag::Catch:
	case a2::AstTag::Return:
	case a2::AstTag::Leave:
	case a2::AstTag::Yield:
	case a2::AstTag::UOpTypeTailArray:
	case a2::AstTag::UOpEval:
	case a2::AstTag::UOpTry:
	case a2::AstTag::UOpDefer:
	case a2::AstTag::UOpAddr:
	case a2::AstTag::UOpDeref:
	case a2::AstTag::UOpBitNot:
	case a2::AstTag::UOpLogNot:
	case a2::AstTag::UOpTypeVar:
	case a2::AstTag::UOpImpliedMember:
	case a2::AstTag::UOpNegate:
	case a2::AstTag::UOpPos:
	case a2::AstTag::OpAddTC:
	case a2::AstTag::OpSubTC:
	case a2::AstTag::OpMulTC:
	case a2::AstTag::OpMod:
	case a2::AstTag::OpBitAnd:
	case a2::AstTag::OpBitOr:
	case a2::AstTag::OpBitXor:
	case a2::AstTag::OpShiftL:
	case a2::AstTag::OpShiftR:
	case a2::AstTag::OpMember:
	case a2::AstTag::OpCmpLT:
	case a2::AstTag::OpCmpGT:
	case a2::AstTag::OpCmpLE:
	case a2::AstTag::OpCmpGE:
	case a2::AstTag::OpCmpNE:
	case a2::AstTag::OpCmpEQ:
	case a2::AstTag::OpSet:
	case a2::AstTag::OpSetAdd:
	case a2::AstTag::OpSetSub:
	case a2::AstTag::OpSetMul:
	case a2::AstTag::OpSetDiv:
	case a2::AstTag::OpSetAddTC:
	case a2::AstTag::OpSetSubTC:
	case a2::AstTag::OpSetMulTC:
	case a2::AstTag::OpSetMod:
	case a2::AstTag::OpSetBitAnd:
	case a2::AstTag::OpSetBitOr:
	case a2::AstTag::OpSetBitXor:
	case a2::AstTag::OpSetShiftL:
	case a2::AstTag::OpSetShiftR:
		panic("Unimplemented AST node tag '%s' in typecheck_expr\n", a2::tag_name(expr->tag));

	default:
		ASSERT_UNREACHABLE;
	}
}

TypeId typecheck_definition(Typechecker* typechecker, Scope* enclosing_scope, a2::AstNode* definition) noexcept
{
	ASSERT_OR_IGNORE(definition->tag == a2::AstTag::Definition);

	ASSERT_OR_IGNORE(a2::has_children(definition));

	const a2::DefinitionInfo info = a2::definition_info(definition);

	a2::DefinitionData* const definition_data = a2::attachment_of<a2::DefinitionData>(definition);

	TypeId definition_type_id = INVALID_TYPE_ID;

	if (is_some(info.type))
	{
		Value* const explicit_type_value = interpret_expr(typechecker->interpreter, enclosing_scope, get_ptr(info.type));

		if (dealias_type_entry(typechecker->types, explicit_type_value->header.type_id)->tag != TypeTag::Type)
			panic("Expected type expression following ':'\n");

		definition_type_id = *access_value<TypeId>(explicit_type_value);

		release_interpretation_result(typechecker->interpreter, explicit_type_value);
	}

	if (is_some(info.value))
	{
		const TypeId inferred_type_id = typecheck_expr(typechecker, enclosing_scope, get_ptr(info.value));

		if (definition_type_id == INVALID_TYPE_ID)
			definition_type_id = inferred_type_id;
		else if (!can_implicity_convert_from_to(typechecker->types, inferred_type_id, definition_type_id))
			panic("Incompatible types\n");
	}

	definition_data->type_id = definition_type_id;

	return definition_type_id;
}



TypeBuilder* alloc_type_builder(Typechecker* typechecker) noexcept
{
	TypeBuilder* builder;

	if (typechecker->first_free_builder_index < 0)
	{
		builder = static_cast<TypeBuilder*>(typechecker->builders.reserve_exact(sizeof(TypeBuilder)));
	}
	else
	{
		builder = typechecker->builders.begin() + typechecker->first_free_builder_index;

		if (builder->next_offset == 0)
			typechecker->first_free_builder_index = -1;
		else
			typechecker->first_free_builder_index += builder->next_offset;
	}

	builder->next_offset = 0;
	builder->tail_offset = 0;
	builder->used = 0;

	return builder;
}

void add_type_member(Typechecker* typechecker, TypeBuilder* builder, IdentifierId identifier_id, OptPtr<a2::AstNode> const type_expr, OptPtr<a2::AstNode> const value_expr, u64 offset, bool is_mut, bool is_pub, bool is_global, bool is_use) noexcept
{
	ASSERT_OR_IGNORE(is_some(type_expr) || is_some(value_expr));

	TypeBuilder* const head = builder;

	builder = typechecker->builders.begin() + builder->tail_offset;

	if (builder->used == array_count(builder->members))
	{
		TypeBuilder* const next = alloc_type_builder(typechecker);

		builder->next_offset = static_cast<s32>(next - builder);

		head->tail_offset = static_cast<s32>(next - head);

		builder = next;
	}

	TypeBuilderMember* const member = builder->members + builder->used;

	builder->used += 1;

	memset(member, 0, sizeof(*member));

	member->identifier_id = identifier_id;
	member->type_expr = type_expr;
	member->value_expr = value_expr;
	member->offset = offset;
	member->is_mut = is_mut;
	member->is_pub = is_pub;
	member->is_global = is_global;
	member->is_use = is_use;
}

TypeId complete_type(Typechecker* typechecker, TypeBuilder* builder, u32 size, u32 alignment, u32 stride) noexcept
{
	struct
	{
		CompositeTypeHeader header;

		CompositeTypeMember members[512];
	} buf;

	TypeBuilder* const head = builder;

	u32 member_count = 0;

	while (true)
	{
		if (member_count + builder->used > array_count(buf.members))
			panic("Maximum of %llu members in composite type exceeded\n", array_count(buf.members));

		for (u32 i = 0; i != builder->used; ++i)
		{
			CompositeTypeMember* const dst = buf.members + member_count;

			member_count += 1;

			TypeBuilderMember* const src = builder->members + i;

			ASSERT_OR_IGNORE(is_some(src->type_expr) || is_some(src->value_expr));

			dst->identifier_id = src->identifier_id;
			dst->offset = src->offset;
			dst->is_mut = src->is_mut;
			dst->is_pub = src->is_pub;
			dst->is_global = src->is_global;
			dst->is_use = src->is_use;

			TypeId explicit_type_id = INVALID_TYPE_ID;

			if (is_some(src->type_expr))
			{
				explicit_type_id = interpret_type_expr(typechecker, nullptr /* TOOD */, get_ptr(src->type_expr));

				dst->type_id = explicit_type_id;
			}

			if (is_some(src->value_expr))
			{
				const TypeId implied_type_id = typecheck_expr(typechecker, nullptr /* TODO */, get_ptr(src->value_expr));

				if (is_none(src->type_expr))
					dst->type_id = implied_type_id;
				else if (!can_implicity_convert_from_to(typechecker->types, implied_type_id, explicit_type_id))
					panic("Incompatible declared and inferred types\n");
			}
		}

		if (builder->next_offset == 0)
			break;

		builder += builder->next_offset;
	}

	ASSERT_OR_IGNORE(builder == head + head->tail_offset);

	release_type_builder(typechecker, head);

	buf.header.size = size;
	buf.header.alignment = alignment;
	buf.header.stride = stride;
	buf.header.member_count = member_count;

	return id_from_type(typechecker->types, TypeTag::Composite, TypeFlag::EMPTY, Range<byte>{ reinterpret_cast<byte*>(&buf), sizeof(buf.header) + member_count * sizeof(buf.members[0]) });
}



TypeId typecheck_file(Typechecker* typechecker, a2::AstNode* root) noexcept
{
	ASSERT_OR_IGNORE(root->tag == a2::AstTag::File);

	TypeBuilder* const builder = alloc_type_builder(typechecker);

	a2::DirectChildIterator it = a2::direct_children_of(root);

	for (OptPtr<a2::AstNode> rst = a2::next(&it); is_some(rst); rst = a2::next(&it))
	{
		a2::AstNode* const definition = get_ptr(rst);

		if (definition->tag != a2::AstTag::Definition)
			continue;

		if (a2::has_flag(definition, a2::AstFlag::Definition_IsGlobal))
			fprintf(stderr, "WARN: Redundant 'global' definition modifier, as top level definitions are implicitly global\n");

		a2::DefinitionData* const definition_data = a2::attachment_of<a2::DefinitionData>(definition);

		const a2::DefinitionInfo definition_info = a2::definition_info(definition);

		add_type_member(typechecker, builder, definition_data->identifier_id, definition_info.type, definition_info.value, 0, a2::has_flag(definition, a2::AstFlag::Definition_IsMut), a2::has_flag(definition, a2::AstFlag::Definition_IsPub), true, a2::has_flag(definition, a2::AstFlag::Definition_IsUse));
	}

	return complete_type(typechecker, builder, 0, 1, 0);
}
