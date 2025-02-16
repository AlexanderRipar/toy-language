#include "pass_data.hpp"

#include "ast2_attach.hpp"
#include "ast2_helper.hpp"
#include "infra/container.hpp"

struct ValueStack
{
	ReservedVec<u64> values;

	ReservedVec<u32> indices;
};

static void init_value_stack(ValueStack* stack) noexcept
{
	stack->values.init(1u << 31, 1u << 16);

	stack->indices.init(1u << 24, 1u << 16);
}

static void release_value_stack(ValueStack* stack) noexcept
{
	stack->values.release();

	stack->indices.release();
}

static Value* push_value(ValueStack* stack, u32 bytes) noexcept
{
	const u32 index = stack->values.used();

	Value* const value = static_cast<Value*>(stack->values.reserve_padded(sizeof(Value) + bytes));

	stack->indices.append(index);

	return value;
}

static void pop_value(ValueStack* stack) noexcept
{
	ASSERT_OR_IGNORE(stack->indices.used() != 0);

	const u32 new_used = stack->indices.top();

	stack->values.pop(stack->values.used() - new_used);

	stack->indices.pop(1);
}



struct Interpreter
{
	ScopePool* scopes;

	TypePool* types;

	ValuePool* values;

	Typechecker* typechecker;

	IdentifierPool* identifiers;

	ValueStack stack;
};

struct TypeMemoryInfo
{
	u32 bytes;

	u32 alignment;
};

static TypeMemoryInfo get_type_memory_info(TypePool* types, TypeId type_id) noexcept
{
	TypeEntry* const entry = dealias_type_entry(types, type_id);

	switch (entry->tag)
	{
	case TypeTag::Void:
		return { 0, 1 };

	case TypeTag::Type:
		return { sizeof(TypeId), alignof(TypeId) };

	case TypeTag::CompInteger:
		return { sizeof(u64), alignof(u64) };

	case TypeTag::CompFloat:
		return { sizeof(f64), alignof(f64) };

	case TypeTag::CompString:
		panic("get_type_memory_info not yet implemented for CompString\n");

	case TypeTag::Integer:
	{
		const u32 bytes = next_pow2((entry->data<IntegerType>()->bits + 7) / 8);

		if (bytes > 8)
			panic("Integer sizes above 64 are not currently supported\n");

		return { bytes, bytes };
	}

	case TypeTag::Float:
	{
		const u32 bits = entry->data<FloatType>()->bits;

		if (bits == 32)
			return { sizeof(f32), alignof(f32) };
		else if (bits == 64)
			return { sizeof(f64), alignof(f64) };
		else
			panic("Floats may only be 32 or 64 bits in size\n");
	}

	case TypeTag::Boolean:
		return { 1, 1 };

	case TypeTag::Slice:
		static_assert(sizeof(Range<byte>) == sizeof(MutRange<byte>) && alignof(Range<byte>) == alignof(MutRange<byte>));

		return { sizeof(Range<byte>), alignof(Range<byte>) };

	case TypeTag::Ptr:
		return { sizeof(void*), alignof(void*) };

	case TypeTag::Array:
	{
		const ArrayType* const array_info = entry->data<ArrayType>();

		TypeMemoryInfo element_info = get_type_memory_info(types, array_info->element_id);

		return { static_cast<u32>(element_info.bytes * array_info->count), element_info.alignment };
	}

	case TypeTag::Func:
		panic("get_type_memory_info not yet implemented for Func\n");

	case TypeTag::Composite:
	{
		const CompositeType* const composite_info = entry->data<CompositeType>();

		return { composite_info->header.size, composite_info->header.alignment };
	}

	case TypeTag::CompositeLiteral:
		panic("get_type_memory_info not yet implemented for CompositeLiteral\n");

	case TypeTag::ArrayLiteral:
		panic("get_type_memory_info not yet implemented for ArrayLiteral\n");

	default:
		ASSERT_UNREACHABLE;
	}
}



Interpreter* create_interpreter(AllocPool* alloc, ScopePool* scopes, TypePool* types, ValuePool* values, IdentifierPool* identifiers) noexcept
{
	Interpreter* const interpreter = static_cast<Interpreter*>(alloc_from_pool(alloc, sizeof(Interpreter), alignof(Interpreter)));

	interpreter->scopes = scopes;
	interpreter->types = types;
	interpreter->values = values;
	interpreter->identifiers = identifiers;
	
	init_value_stack(&interpreter->stack);

	return interpreter;
}

void release_interpreter(Interpreter* interpreter) noexcept
{
	release_value_stack(&interpreter->stack);
}

void set_interpreter_typechecker(Interpreter* interpreter, Typechecker* typechecker) noexcept
{
	interpreter->typechecker = typechecker;
}

Value* interpret_expr(Interpreter* interpreter, Scope* enclosing_scope, AstNode* expr) noexcept
{
	switch (expr->tag)
	{
	case AstTag::ValIdentifer:
	{
		ASSERT_OR_IGNORE(!has_children(expr));

		const IdentifierId identifier_id = attachment_of<ValIdentifierData>(expr)->identifier_id;

		const ScopeLookupResult lookup = lookup_identifier_recursive(enclosing_scope, identifier_id);

		if (!is_valid(lookup))
		{
			const Range<char8> name = identifier_entry_from_id(interpreter->identifiers, identifier_id)->range();

			panic("Could not find definition for identifier '%.*s'\n", static_cast<s32>(name.count()), name.begin());
		}

		AstNode* const definition = lookup.definition;

		DefinitionData* const definition_data = attachment_of<DefinitionData>(definition);

		if (definition_data->type_id == INVALID_TYPE_ID)
			typecheck_definition(interpreter->typechecker, lookup.enclosing_scope, definition);

		Value* definition_value = nullptr;

		if (definition_data->value_id == INVALID_VALUE_ID)
		{
			const TypeMemoryInfo memory_info = get_type_memory_info(interpreter->types, definition_data->type_id);

			if (memory_info.alignment > 8)
				panic("Alignments above 8 are not currently supported during interpretation\n");

			const ValueLocation location = alloc_value(interpreter->values, memory_info.bytes);

			location.ptr->header.type_id = definition_data->type_id;

			definition_data->value_id = location.id;

			const DefinitionInfo definition_info = get_definition_info(definition);

			if (is_none(definition_info.value))
				panic("Attempted to evaluate definition without value\n");

			Value* const evaluated_definition_value = interpret_expr(interpreter, lookup.enclosing_scope, get_ptr(definition_info.value));

			Value* const new_definition_value = evaluated_definition_value->header.is_ref ? reinterpret_cast<ReferenceValue*>(evaluated_definition_value->value)->referenced : evaluated_definition_value;

			memcpy(location.ptr->value, new_definition_value->value, memory_info.bytes);

			release_interpretation_result(interpreter, evaluated_definition_value);

			definition_value = location.ptr;
		}
		else
		{
			definition_value = value_from_id(interpreter->values, definition_data->value_id);
		}

		Value* const stack_value = push_value(&interpreter->stack, sizeof(ReferenceValue));

		stack_value->header.is_ref = true;

		stack_value->header.type_id = definition_data->type_id;

		reinterpret_cast<ReferenceValue*>(stack_value->value)->referenced = definition_value;

		return stack_value;
	}

	case AstTag::UOpTypeMultiPtr:
	case AstTag::UOpTypeOptMultiPtr:
	case AstTag::UOpTypeSlice:
	case AstTag::UOpTypeOptPtr:
	case AstTag::UOpTypePtr:
	{
		ASSERT_OR_IGNORE(has_children(expr));

		AstNode* const element_type_node = first_child_of(expr);

		ASSERT_OR_IGNORE(!has_next_sibling(element_type_node));

		Value* const element_type_value = interpret_expr(interpreter, enclosing_scope, element_type_node);

		if (dealias_type_entry(interpreter->types, element_type_value->header.type_id)->tag != TypeTag::Type)
			panic("Expected type expression following ':'\n");

		const TypeId element_type_id = *access_value<TypeId>(element_type_value);

		release_interpretation_result(interpreter, element_type_value);

		Value* const stack_value = push_value(&interpreter->stack, sizeof(TypeId));

		stack_value->header.type_id = get_builtin_type_ids(interpreter->types)->type_type_id;

		TypeTag tag;

		TypeFlag flags;

		SliceType slice_type{};

		PtrType ptr_type{};

		Range<byte> type_bytes;

		if (expr->tag == AstTag::UOpTypeSlice)
		{
			tag = TypeTag::Slice;

			flags = TypeFlag::EMPTY;

			slice_type.element_id = element_type_id;

			type_bytes = range::from_object_bytes(&slice_type);
		}
		else
		{
			tag = TypeTag::Ptr;

			if (expr->tag == AstTag::UOpTypeMultiPtr)
				flags = TypeFlag::Ptr_IsMulti;
			else if (expr->tag == AstTag::UOpTypeOptMultiPtr)
				flags = TypeFlag::Ptr_IsOpt | TypeFlag::Ptr_IsMulti;
			else if (expr->tag == AstTag::UOpTypeOptPtr)
				flags = TypeFlag::Ptr_IsOpt;
			else
			{
				ASSERT_OR_IGNORE(expr->tag == AstTag::UOpTypePtr);

				flags = TypeFlag::EMPTY;
			}

			ptr_type.pointee_id = element_type_id;

			type_bytes = range::from_object_bytes(&ptr_type);
		}

		if (has_flag(expr, AstFlag::Type_IsMut))
			flags |= TypeFlag::SliceOrPtr_IsMut;

		*reinterpret_cast<TypeId*>(stack_value->value) = id_from_type(interpreter->types, tag, flags, type_bytes);

		return stack_value;
	}

	case AstTag::Call:
	{
		AstNode* const callee = first_child_of(expr);

		Value* const callee_value = interpret_expr(interpreter, enclosing_scope, callee);

		TypeEntry* const callee_type_entry = dealias_type_entry(interpreter->types, callee_value->header.type_id);

		ASSERT_OR_IGNORE(callee_type_entry->tag == TypeTag::Func);

		// TODO
		return nullptr;
	}

	case AstTag::Builtin:
	case AstTag::File:
	case AstTag::CompositeInitializer:
	case AstTag::ArrayInitializer:
	case AstTag::Wildcard:
	case AstTag::Where:
	case AstTag::Expects:
	case AstTag::Ensures:
	case AstTag::Definition:
	case AstTag::Block:
	case AstTag::If:
	case AstTag::For:
	case AstTag::ForEach:
	case AstTag::Switch:
	case AstTag::Case:
	case AstTag::Func:
	case AstTag::Trait:
	case AstTag::Impl:
	case AstTag::Catch:
	case AstTag::ValInteger:
	case AstTag::ValFloat:
	case AstTag::ValChar:
	case AstTag::ValString:
	case AstTag::Return:
	case AstTag::Leave:
	case AstTag::Yield:
	case AstTag::ParameterList:
	case AstTag::UOpTypeTailArray:
	case AstTag::UOpEval:
	case AstTag::UOpTry:
	case AstTag::UOpDefer:
	case AstTag::UOpAddr:
	case AstTag::UOpDeref:
	case AstTag::UOpBitNot:
	case AstTag::UOpLogNot:
	case AstTag::UOpTypeVar:
	case AstTag::UOpImpliedMember:
	case AstTag::UOpNegate:
	case AstTag::UOpPos:
	case AstTag::OpAdd:
	case AstTag::OpSub:
	case AstTag::OpMul:
	case AstTag::OpDiv:
	case AstTag::OpAddTC:
	case AstTag::OpSubTC:
	case AstTag::OpMulTC:
	case AstTag::OpMod:
	case AstTag::OpBitAnd:
	case AstTag::OpBitOr:
	case AstTag::OpBitXor:
	case AstTag::OpShiftL:
	case AstTag::OpShiftR:
	case AstTag::OpLogAnd:
	case AstTag::OpLogOr:
	case AstTag::OpMember:
	case AstTag::OpCmpLT:
	case AstTag::OpCmpGT:
	case AstTag::OpCmpLE:
	case AstTag::OpCmpGE:
	case AstTag::OpCmpNE:
	case AstTag::OpCmpEQ:
	case AstTag::OpSet:
	case AstTag::OpSetAdd:
	case AstTag::OpSetSub:
	case AstTag::OpSetMul:
	case AstTag::OpSetDiv:
	case AstTag::OpSetAddTC:
	case AstTag::OpSetSubTC:
	case AstTag::OpSetMulTC:
	case AstTag::OpSetMod:
	case AstTag::OpSetBitAnd:
	case AstTag::OpSetBitOr:
	case AstTag::OpSetBitXor:
	case AstTag::OpSetShiftL:
	case AstTag::OpSetShiftR:
	case AstTag::OpTypeArray:
	case AstTag::OpArrayIndex:
		panic("Unimplemented AST node tag '%s' in interpret_expr\n", ast_tag_name(expr->tag));

	default:
		ASSERT_UNREACHABLE;
	}
}

void release_interpretation_result(Interpreter* interpreter, Value* result) noexcept
{
	ASSERT_OR_IGNORE(interpreter->stack.indices.used() != 0);

	ASSERT_OR_IGNORE(reinterpret_cast<u64*>(result) == interpreter->stack.values.begin() + interpreter->stack.indices.top());

	interpreter->stack.indices.pop(1);
}
