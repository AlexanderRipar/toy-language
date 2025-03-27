#include "pass_data.hpp"

#include "ast_attach.hpp"
#include "ast_helper.hpp"
#include "infra/container.hpp"

struct ValueStack
{
	ReservedVec<u64> values;

	ReservedVec<u32> indices;
};

struct CallFrame
{
	u32 arg_count;

	u32 cleanup_index;

	#if COMPILER_MSVC
	#pragma warning(push)
	#pragma warning(disable : 4200) // C4200: nonstandard extension used: zero-sized array in struct/union
	#elif COMPILER_CLANG
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Wc99-extensions" // flexible array members are a C99 feature
	#endif
	Value* args[];
	#if COMPILER_MSVC
	#pragma warning(pop)
	#elif COMPILER_CLANG
	#pragma clang diagnostic pop
	#endif
};

struct Interpreter
{
	ScopePool* scopes;

	TypePool* types;

	ValuePool* values;

	Typechecker* typechecker;

	IdentifierPool* identifiers;

	SourceReader* reader;

	Parser* parser;

	AstPool* asts;

	ValueStack stack;

	ReservedVec<u64> return_scratch;
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

	memset(&value->header, 0, sizeof(value->header));

	stack->indices.append(index);

	return value;
}

static CallFrame* push_callframe(Interpreter* interpreter, TypeEntry* callee_type_entry) noexcept
{
	ASSERT_OR_IGNORE(callee_type_entry->tag == TypeTag::Func || callee_type_entry->tag == TypeTag::Builtin);

	FuncType* const func_type = callee_type_entry->data<FuncType>();

	Value* const result = push_value(&interpreter->stack, sizeof(CallFrame) + func_type->header.parameter_count * sizeof(Value*));

	result->header.type_id = id_from_type(interpreter->types, TypeTag::CallFrame, TypeFlag::EMPTY, {});

	CallFrame* const frame = data<CallFrame>(result);

	memset(frame, 0, sizeof(*frame));

	frame->arg_count = callee_type_entry->data<FuncType>()->header.parameter_count;

	frame->cleanup_index = interpreter->stack.indices.used() - 1;

	return data<CallFrame>(result);
};

static void pop_callframe(ValueStack* stack, CallFrame* frame) noexcept
{
	stack->indices.pop_to(frame->cleanup_index);
}

static Value* value_at(ValueStack* stack, u32 index) noexcept
{
	const u32 count = stack->indices.used();

	ASSERT_OR_IGNORE(count > index);

	const u32 qword_index = stack->indices.begin()[count - index - 1];

	ASSERT_OR_IGNORE(qword_index < stack->values.used());

	return reinterpret_cast<Value*>(stack->values.begin() + qword_index);
}

static Value* set_return(Interpreter* interpreter, TypeId type_id, u32 bytes) noexcept
{
	interpreter->return_scratch.reset();

	interpreter->return_scratch.reserve_padded(sizeof(Value) + bytes);

	Value* const scratch = reinterpret_cast<Value*>(interpreter->return_scratch.begin());

	memset(&scratch->header, 0, sizeof(scratch->header));

	scratch->header.type_id = type_id;

	return scratch;
}

static Value* get_return(Interpreter* interpreter) noexcept
{
	return reinterpret_cast<Value*>(interpreter->return_scratch.begin());
}


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





using BuiltinImpl = void (*) (Interpreter*);

struct CalcStrideof
{
	static u64 calc(TypePool* types, TypeEntry* type) noexcept
	{
		switch(type->tag)
		{
		case TypeTag::Void:
			return 0;
	
		case TypeTag::Type:
			return sizeof(TypeId);
	
		case TypeTag::Definition:
			panic("sizeof(Definition) not yet implemented\n");
	
		case TypeTag::CompInteger:
			return sizeof(CompIntegerValue);
	
		case TypeTag::CompFloat:
			return sizeof(f64);
	
		case TypeTag::CompString:
			return sizeof(Range<char8>);
	
		case TypeTag::Integer:
			return (type->data<IntegerType>()->bits + 7) / 8;
	
		case TypeTag::Float:
			return (type->data<FloatType>()->bits + 7) / 8;
	
		case TypeTag::Boolean:
			return 1;
	
		case TypeTag::Slice:
			return 16;
	
		case TypeTag::Ptr:
			return 8;
	
		case TypeTag::Array:
		{
			const ArrayType* const array_type = type->data<ArrayType>();
	
			TypeEntry* const element_entry = dealias_type_entry(types, array_type->element_id);
	
			const u64 element_size = CalcStrideof::calc(types, element_entry);
	
			return element_size * array_type->count;
		}
	
		case TypeTag::Func:
			panic("sizeof(Func) not yet implemented\n");
	
		case TypeTag::Composite:
			return type->data<CompositeType>()->header.stride;
	
		case TypeTag::CompositeLiteral:
			panic("Cannot take size of composite literal\n");
	
		case TypeTag::ArrayLiteral:
			panic("Cannot take size of array literal\n");
	
		case TypeTag::TypeBuilder:
			return sizeof(CompositeTypeBuilder*);
	
		default:
			ASSERT_UNREACHABLE;
		}
	}
};

struct CalcSizeof
{
	static u64 calc(TypePool* types, TypeEntry* type) noexcept
	{
		switch(type->tag)
		{
		case TypeTag::Void:
			return 0;
	
		case TypeTag::Type:
			return sizeof(TypeId);
	
		case TypeTag::Definition:
			panic("sizeof(Definition) not yet implemented\n");
	
		case TypeTag::CompInteger:
			return sizeof(CompIntegerValue);
	
		case TypeTag::CompFloat:
			return sizeof(f64);
	
		case TypeTag::CompString:
			return sizeof(Range<char8>);
	
		case TypeTag::Integer:
			return (type->data<IntegerType>()->bits + 7) / 8;
	
		case TypeTag::Float:
			return (type->data<FloatType>()->bits + 7) / 8;
	
		case TypeTag::Boolean:
			return 1;
	
		case TypeTag::Slice:
			return 16;
	
		case TypeTag::Ptr:
			return 8;
	
		case TypeTag::Array:
		{
			const ArrayType* const array_type = type->data<ArrayType>();
	
			TypeEntry* const element_entry = dealias_type_entry(types, array_type->element_id);
	
			const u64 element_size = CalcStrideof::calc(types, element_entry);
	
			return element_size * array_type->count;
		}
	
		case TypeTag::Func:
			panic("sizeof(Func) not yet implemented\n");
	
		case TypeTag::Composite:
			return type->data<CompositeType>()->header.size;
	
		case TypeTag::CompositeLiteral:
			panic("Cannot take size of composite literal\n");
	
		case TypeTag::ArrayLiteral:
			panic("Cannot take size of array literal\n");
	
		case TypeTag::TypeBuilder:
			return sizeof(CompositeTypeBuilder*);
	
		default:
			ASSERT_UNREACHABLE;
		}
	}
};

struct CalcAlignof
{
	static u64 calc(TypePool* types, TypeEntry* type) noexcept
	{
		switch (type->tag)
		{
		case TypeTag::Void:
			return 1;
	
		case TypeTag::Type:
			return alignof(TypeId);
	
		case TypeTag::Definition:
			panic("sizeof(Definition) not yet implemented\n");
	
		case TypeTag::CompInteger:
			return alignof(CompIntegerValue);
	
		case TypeTag::CompFloat:
			return alignof(f64);
	
		case TypeTag::CompString:
			return alignof(Range<char8>);
	
		case TypeTag::Integer:
			return (type->data<IntegerType>()->bits + 7) / 8;
	
		case TypeTag::Float:
			return (type->data<FloatType>()->bits + 7) / 8;
	
		case TypeTag::Boolean:
			return 1;
	
		case TypeTag::Slice:
			return 8;
	
		case TypeTag::Ptr:
			return 8;
	
		case TypeTag::Array:
		{
			const ArrayType* const array_type = type->data<ArrayType>();
	
			TypeEntry* const element_entry = dealias_type_entry(types, array_type->element_id);
	
			return CalcAlignof::calc(types, element_entry);
		}
	
		case TypeTag::Func:
			panic("align(Func) not yet implemented\n");
	
		case TypeTag::Composite:
			return type->data<CompositeType>()->header.alignment;
	
		case TypeTag::CompositeLiteral:
			panic("Cannot take align of composite literal\n");
	
		case TypeTag::ArrayLiteral:
			panic("Cannot take align of array literal\n");
	
		case TypeTag::TypeBuilder:
			return alignof(CompositeTypeBuilder*);
	
		default:
			ASSERT_UNREACHABLE;
		}
	}
};

template<typename T>
static void builtin_type_to_numeric(Interpreter* interpreter) noexcept
{
	Value* const frame_value = value_at(&interpreter->stack, 0);

	ASSERT_OR_IGNORE(type_entry_from_id(interpreter->types, frame_value->header.type_id)->tag == TypeTag::CallFrame);

	CallFrame* const frame = data<CallFrame>(frame_value);

	ASSERT_OR_IGNORE(frame->arg_count == 1);

	Value* const type_value = frame->args[0];

	ASSERT_OR_IGNORE(dealias_type_entry(interpreter->types, type_value->header.type_id)->tag == TypeTag::Type);

	TypeEntry* const type = dealias_type_entry(interpreter->types, *data<TypeId>(type_value));

	Value* const result = set_return(interpreter, id_from_type(interpreter->types, TypeTag::CompInteger, TypeFlag::EMPTY, {}), sizeof(CompIntegerValue));

	*data<CompIntegerValue>(result) = create_comp_integer(T::calc(interpreter->types, type));
}

static void builtin_unit_type(Interpreter* interpreter, TypeTag tag) noexcept
{
	Value* const frame_value = value_at(&interpreter->stack, 0);

	ASSERT_OR_IGNORE(type_entry_from_id(interpreter->types, frame_value->header.type_id)->tag == TypeTag::CallFrame);

	CallFrame* const frame = data<CallFrame>(frame_value);

	ASSERT_OR_IGNORE(frame->arg_count == 0);

	const TypeId type_id = id_from_type(interpreter->types, tag, TypeFlag::EMPTY, {});

	Value* const value = set_return(interpreter, id_from_type(interpreter->types, TypeTag::Type, TypeFlag::EMPTY, {}), sizeof(TypeId));

	*data<TypeId>(value) = type_id;
}

static void builtin_integer(Interpreter* interpreter) noexcept
{
	Value* const frame_value = value_at(&interpreter->stack, 0);

	ASSERT_OR_IGNORE(type_entry_from_id(interpreter->types, frame_value->header.type_id)->tag == TypeTag::CallFrame);

	CallFrame* const frame = data<CallFrame>(frame_value);

	ASSERT_OR_IGNORE(frame->arg_count == 2);

	Value* const bits_value = frame->args[0];

	Value* const is_signed_value = frame->args[1];

	ASSERT_OR_IGNORE(type_entry_from_id(interpreter->types, bits_value->header.type_id)->tag == TypeTag::CompInteger);

	ASSERT_OR_IGNORE(type_entry_from_id(interpreter->types, is_signed_value->header.type_id)->tag == TypeTag::Boolean);

	const u64 bits = data<CompIntegerValue>(bits_value)->value;

	if (!is_pow2(bits) || bits == 0 || bits > 64)
		panic("Only integer types of bit width 8, 16, 32 or 64 are currently supported\n");

	const IntegerType integer_type{ static_cast<u8>(bits) };

	const bool is_signed = *data<bool>(is_signed_value);

	const TypeId integer_type_id = id_from_type(interpreter->types, TypeTag::Integer, is_signed ? TypeFlag::Integer_IsSigned : TypeFlag::EMPTY, range::from_object_bytes(&integer_type));

	Value* const result = set_return(interpreter, id_from_type(interpreter->types, TypeTag::Type, TypeFlag::EMPTY, {}), sizeof(TypeId));

	*data<TypeId>(result) = integer_type_id;
}

static void builtin_type(Interpreter* interpreter) noexcept
{
	builtin_unit_type(interpreter, TypeTag::Type);
}

static void builtin_comp_integer(Interpreter* interpreter) noexcept
{
	builtin_unit_type(interpreter, TypeTag::CompInteger);
}

static void builtin_comp_float(Interpreter* interpreter) noexcept
{
	builtin_unit_type(interpreter, TypeTag::CompFloat);
}

static void builtin_comp_string(Interpreter* interpreter) noexcept
{
	builtin_unit_type(interpreter, TypeTag::CompString);
}

static void builtin_type_builder(Interpreter* interpreter) noexcept
{
	builtin_unit_type(interpreter, TypeTag::TypeBuilder);
}

static void builtin_true(Interpreter* interpreter) noexcept
{
	Value* const result = set_return(interpreter, id_from_type(interpreter->types, TypeTag::Boolean, TypeFlag::EMPTY, {}), sizeof(bool));

	*data<bool>(result) = true;
}

static void builtin_sizeof(Interpreter* interpreter) noexcept
{
	builtin_type_to_numeric<CalcSizeof>(interpreter);
}

static void builtin_alignof(Interpreter* interpreter) noexcept
{
	builtin_type_to_numeric<CalcAlignof>(interpreter);
}

static void builtin_strideof(Interpreter* interpreter) noexcept
{
	builtin_type_to_numeric<CalcStrideof>(interpreter);
}

static void builtin_import(Interpreter* interpreter) noexcept
{
	(void) interpreter;
	panic("Builtin '_import' not yet implemented\n");
}

static void builtin_create_type_builder(Interpreter* interpreter) noexcept
{
	(void) interpreter;

	panic("Builtin '_tb_creat' not yet implemented\n");
}

static void builtin_add_type_member(Interpreter* interpreter) noexcept
{
	(void) interpreter;

	panic("Builtin '_tb_add' not yet implemented\n");
}

static void builtin_complete_type(Interpreter* interpreter) noexcept
{
	(void) interpreter;

	panic("Builtin '_tb_compl' not yet implemented\n");
}

static BuiltinImpl lookup_builtin_impl(Builtin builtin) noexcept
{
	switch (builtin)
	{
	case Builtin::Integer:
		return &builtin_integer;

	case Builtin::Type:
		return &builtin_type;

	case Builtin::CompInteger:
		return &builtin_comp_integer;

	case Builtin::CompFloat:
	return &builtin_comp_float;

	case Builtin::CompString:
	return &builtin_comp_string;

	case Builtin::TypeBuilder:
		return &builtin_type_builder;

	case Builtin::True:
		return &builtin_true;

	case Builtin::Typeof:
		panic("Builtin '_typeof' not yet interpretable\n");

	case Builtin::Sizeof:
		return &builtin_sizeof;

	case Builtin::Alignof:
		return &builtin_alignof;

	case Builtin::Strideof:
		return &builtin_strideof;

	case Builtin::Offsetof:
		panic("Builtin '_offsetof' not yet interpretable\n");

	case Builtin::Nameof:
	panic("Builtin '_nameof' not yet interpretable\n");

	case Builtin::Import:
		return &builtin_import;

	case Builtin::CreateTypeBuilder:
		return &builtin_create_type_builder;

	case Builtin::AddTypeMember:
		return &builtin_add_type_member;

	case Builtin::CompleteType:
		return &builtin_complete_type;

	default:
		ASSERT_UNREACHABLE;
	}
}



Interpreter* create_interpreter(AllocPool* alloc, SourceReader* reader, Parser* parser, AstPool* asts, ScopePool* scopes, TypePool* types, ValuePool* values, IdentifierPool* identifiers) noexcept
{
	Interpreter* const interpreter = static_cast<Interpreter*>(alloc_from_pool(alloc, sizeof(Interpreter), alignof(Interpreter)));

	interpreter->scopes = scopes;
	interpreter->types = types;
	interpreter->values = values;
	interpreter->identifiers = identifiers;
	interpreter->reader = reader;
	interpreter->parser = parser;
	interpreter->asts = asts;
	
	init_value_stack(&interpreter->stack);

	interpreter->return_scratch.init(1 << 20, 1 << 14);

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

		const TypeId element_type_id = *data<TypeId>(element_type_value);

		release_interpretation_result(interpreter, element_type_value);

		Value* const stack_value = push_value(&interpreter->stack, sizeof(TypeId));

		stack_value->header.type_id = id_from_type(interpreter->types, TypeTag::Type, TypeFlag::EMPTY, {});

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

		const FuncType* const callee_func = callee_type_entry->data<FuncType>();

		CallFrame* const frame = push_callframe(interpreter, callee_type_entry);

		u32 arg_index = 0;

		AstNode* argument = callee;

		while (has_next_sibling(argument))
		{
			argument = next_sibling_of(argument);

			if (argument->tag == AstTag::OpSet)
			{
				AstNode* const lhs = first_child_of(argument);

				if (lhs->tag == AstTag::UOpImpliedMember)
				{
					AstNode* const arg_name = first_child_of(lhs);

					if (arg_name->tag != AstTag::ValIdentifer)
						panic("Implied members in function calls must be identifiers\n");

					argument = next_sibling_of(lhs);

					const IdentifierId arg_name_id = attachment_of<ValIdentifierData>(arg_name)->identifier_id;

					for (u16 i = 0; i != callee_func->header.parameter_count; ++i)
					{
						if (callee_func->params[i].name == arg_name_id)
						{
							arg_index = i;

							break;
						}
					}
				}
			}

			Value* const arg_value = interpret_expr(interpreter, enclosing_scope /* TODO: This should actually be the call scope instead */, argument);

			ASSERT_OR_IGNORE(arg_index < frame->arg_count);

			if (frame->args[arg_index] != nullptr)
			{
				const Range<char8> name = identifier_entry_from_id(interpreter->identifiers, callee_func->params[arg_index].name)->range();

				panic("Argument %.*s at position %u bound more than once\n", static_cast<s32>(name.count()), name.begin(), arg_index);
			}

			frame->args[arg_index] = arg_value;

			arg_index += 1;
		}

		if (callee_type_entry->tag == TypeTag::Builtin)
		{
			const BuiltinImpl builtin_ptr = *data<BuiltinImpl>(callee_value);

			(*builtin_ptr)(interpreter);
		}
		else
		{
			panic("Non-builtin calls are not yet implemented\n");
		}

		pop_callframe(&interpreter->stack, frame);

		Value* const returned = get_return(interpreter);

		const u32 returned_bytes = interpreter->return_scratch.used() * sizeof(*interpreter->return_scratch.begin());

		Value* const stack_dst = push_value(&interpreter->stack, returned_bytes);

		memcpy(stack_dst, returned, returned_bytes);

		return stack_dst;
	}

	case AstTag::Builtin:
	{
		const Builtin builtin = static_cast<Builtin>(expr->flags);

		Value* const value = push_value(&interpreter->stack, sizeof(BuiltinImpl));

		value->header.type_id = typecheck_builtin(interpreter->typechecker, builtin);

		*data<BuiltinImpl>(value) = lookup_builtin_impl(builtin);

		return value;
	}

	case AstTag::ValString:
	{
		const IdentifierId string_id = attachment_of<ValStringData>(expr)->string_id;

		Value* const result = push_value(&interpreter->stack, sizeof(Range<char8>));

		result->header.type_id = id_from_type(interpreter->types, TypeTag::CompString, TypeFlag::EMPTY, {});

		*data<Range<char8>>(result) = identifier_entry_from_id(interpreter->identifiers, string_id)->range();

		return result;
	}

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

	interpreter->stack.indices.pop_by(1);
}

TypeId import_file(Interpreter* interpreter, Range<char8> filepath, bool is_std) noexcept
{
	const IdentifierId filepath_id = id_from_identifier(interpreter->identifiers, filepath);

	request_read(interpreter->reader, filepath, filepath_id);

	SourceFile source;

	// TODO: Redesign SourceReader to simply block
	// TODO: Cache ASTs
	const bool read_success = await_completed_read(interpreter->reader, &source);

	ASSERT_OR_IGNORE(read_success);

	AstNode* const root = parse(interpreter->parser, source, is_std, interpreter->asts);

	release_read(interpreter->reader, source);

	return typecheck_file(interpreter->typechecker, root);
}
