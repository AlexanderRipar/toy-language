#include "pass_data.hpp"

#include "ast2.hpp"
#include "ast2_helper.hpp"
#include "ast2_attach.hpp"
#include "infra/common.hpp"
#include "infra/hash.hpp"

struct ScopeEntry
{
	IdentifierId identifier_id;

	u32 node_offset;
};

struct Scope
{
	a2::Node* root;

	u32 definition_count;

	#pragma warning(push)
	#pragma warning(disable : 4200) // nonstandard extension used: zero-sized array in struct/union
	ScopeEntry definitions[];
	#pragma warning(pop)
};

struct Resolver
{
	IdentifierPool* identifiers;

	TypePool* types;

	ValuePool* values;

	s32 value_top;

	s32 scope_count;

	ReservedVec<u64> stack;

	ReservedVec<u64> scopes;

	u32 scope_offsets[a2::MAX_TREE_DEPTH + 1];
};

static Value* top_value(Resolver* resolver) noexcept
{
	return reinterpret_cast<Value*>(resolver->stack.begin() + resolver->value_top);
}

static Value* push_value(Resolver* resolver, u32 bytes) noexcept
{
	const s32 prev = resolver->value_top;

	const s32 curr = resolver->stack.used();

	resolver->value_top = curr;

	Value* const value = static_cast<Value*>(resolver->stack.reserve_exact((bytes + sizeof(Value) + 7) & ~7));

	memset(&value->header, 0, sizeof(value->header));

	value->header.prev_offset = static_cast<u32>(curr - prev);

	return value;
}

static void pop_value(Resolver* resolver) noexcept
{
	Value* const top = top_value(resolver);

	resolver->stack.pop(top->header.prev_offset);

	resolver->value_top -= top->header.prev_offset;
}



static TypeId follow_aliases(TypePool* types, TypeId id, TypeEntry** out_entry) noexcept
{
	while (true)
	{
		TypeEntry* const entry = type_entry_from_id(types, id);

		if (entry->tag != TypeTag::Alias)
		{
			*out_entry = entry;

			return id;
		}

		id = entry->data<AliasType>()->aliased_id;
	}
}

static TypeId follow_aliases(TypePool* types, TypeId id) noexcept
{
	while (true)
	{
		TypeEntry* const entry = type_entry_from_id(types, id);

		if (entry->tag != TypeTag::Alias)
			return id;

		id = entry->data<AliasType>()->aliased_id;
	}
}



struct TypeMemoryInfo
{
	u32 bytes;

	u32 alignment;
};

TypeMemoryInfo get_type_memory_info(TypePool* types, TypeId type_id) noexcept
{
	TypeEntry* entry;

	type_id = follow_aliases(types, type_id, &entry);

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




struct FuncDesc
{
	a2::Node* parameters;

	OptPtr<a2::Node> return_type;

	OptPtr<a2::Node> expects;

	OptPtr<a2::Node> ensures;

	OptPtr<a2::Node> body;
};

static FuncDesc get_func_desc(a2::Node* func) noexcept
{
	ASSERT_OR_IGNORE(func->tag == a2::Tag::Func);

	ASSERT_OR_IGNORE(a2::has_children(func));

	a2::Node* curr = a2::first_child_of(func);

	ASSERT_OR_IGNORE(curr->tag == a2::Tag::ParameterList);

	FuncDesc desc{};

	desc.parameters = curr;

	if (a2::has_flag(func, a2::Flag::Func_HasReturnType))
	{
		curr = a2::next_sibling_of(curr);

		desc.return_type = some(curr);
	}

	if (a2::has_flag(func, a2::Flag::Func_HasExpects))
	{
		curr = a2::next_sibling_of(curr);

		ASSERT_OR_IGNORE(curr->tag == a2::Tag::Expects);

		desc.expects = some(curr);
	}

	if (a2::has_flag(func, a2::Flag::Func_HasEnsures))
	{
		curr = a2::next_sibling_of(curr);

		ASSERT_OR_IGNORE(curr->tag == a2::Tag::Ensures);

		desc.ensures = some(curr);
	}

	if (a2::has_flag(func, a2::Flag::Func_HasBody))
	{
		curr = a2::next_sibling_of(curr);

		desc.body = some(curr);
	}

	return desc;
}



static OptPtr<a2::Node> lookup_identifier(Resolver* resolver, IdentifierId id) noexcept
{
	for (sreg i = resolver->scope_count - 1; i >= 0; --i)
	{
		Scope* const scope = reinterpret_cast<Scope*>(resolver->scopes.begin() + resolver->scope_offsets[i]);

		for (sreg j = 0; j != scope->definition_count; ++j)
		{
			if (scope->definitions[j].identifier_id == id)
				return some(a2::apply_offset_(scope->root, scope->definitions[j].node_offset));
		}
	}

	return none<a2::Node>();
}

static bool can_implicity_convert_from_to(TypePool* types, TypeId from, TypeId to) noexcept
{
	TypeEntry* from_entry;

	from = follow_aliases(types, from, &from_entry);

	TypeEntry* to_entry;

	to = follow_aliases(types, to, &to_entry);

	if (from == to)
		return true;

	switch (from_entry->tag)
	{
	case TypeTag::Array:
	{
		const TypeId from_element_id = follow_aliases(types, from_entry->data<ArrayType>()->element_id);

		if (to_entry->tag == TypeTag::Slice)
		{
			const TypeId to_element_id = follow_aliases(types, to_entry->data<SliceType>()->element_id);

			return from_element_id == to_element_id;
		}
		else if (to_entry->tag == TypeTag::Ptr && (to_entry->flags & TypeFlag::Ptr_IsMulti) != TypeFlag::EMPTY)
		{
			const TypeId to_element_id = follow_aliases(types, to_entry->data<PtrType>()->pointee_id);

			return from_element_id == to_element_id;
		}
		else if (to_entry->tag == TypeTag::Array)
		{
			if (from_entry->data<ArrayType>()->count != to_entry->data<ArrayType>()->count)
				return false;

			const TypeId to_element_id = follow_aliases(types, to_entry->data<ArrayType>()->element_id);

			return from_element_id == to_element_id;
		}
		else
		{
			return false;
		}
	}

	case TypeTag::Slice:
	{
		TypeId to_element_id;

		if (to_entry->tag == TypeTag::Ptr && (to_entry->flags & TypeFlag::Ptr_IsMulti) != TypeFlag::EMPTY)
			to_element_id = to_entry->data<PtrType>()->pointee_id;
		else if (to_entry->tag == TypeTag::Slice)
			to_element_id = to_entry->data<SliceType>()->element_id;
		else
			return false;

		to_element_id = follow_aliases(types, to_element_id);
		
		const TypeId from_element_id = follow_aliases(types, from_entry->data<SliceType>()->element_id);
		
		return from_element_id == to_element_id;
	}

	case TypeTag::CompInteger:
		return to_entry->tag == TypeTag::Integer;

	case TypeTag::CompFloat:
		return to_entry->tag == TypeTag::Float;

	case TypeTag::CompString:
	{
		TypeId to_element_id;

		if (to_entry->tag == TypeTag::Array)
			to_element_id = to_entry->data<ArrayType>()->element_id;
		else if (to_entry->tag == TypeTag::Slice)
			to_element_id = to_entry->data<SliceType>()->element_id;
		else if (to_entry->tag == TypeTag::Ptr && (to_entry->flags & TypeFlag::Ptr_IsMulti) != TypeFlag::EMPTY)
			to_element_id = to_entry->data<PtrType>()->pointee_id;
		else
			return false;

		TypeEntry* const to_element_entry = type_entry_from_id(types, follow_aliases(types, to_element_id));

		return to_element_entry->tag == TypeTag::Integer
			&& to_element_entry->data<IntegerType>()->bits == 8
			&& (to_element_entry->flags & TypeFlag::Integer_IsSigned) == TypeFlag::EMPTY;
	}

	default:
		return false;
	}
}



static void eval_expr(Resolver* resolver, a2::Node* node) noexcept;

static TypeId eval_type_expr(Resolver* resolver, a2::Node* node)
{
	eval_expr(resolver, node);

	Value* const type_value = top_value(resolver);

	const TypeId type_id = follow_aliases(resolver->types, type_value->header.type);

	pop_value(resolver);

	TypeEntry* const type_entry = type_entry_from_id(resolver->types, type_id);

	if (type_entry->tag != TypeTag::Type)
		INVALID_TYPE_ID;

	return *access_value<TypeId>(type_value);
}

static void eval_expr(Resolver* resolver, a2::Node* node) noexcept
{
	switch (node->tag)
	{
	case a2::Tag::Builtin:
		ASSERT_UNREACHABLE;

	case a2::Tag::File:
		ASSERT_UNREACHABLE;

	case a2::Tag::CompositeInitializer:
		ASSERT_UNREACHABLE;

	case a2::Tag::ArrayInitializer:
		ASSERT_UNREACHABLE;

	case a2::Tag::Wildcard:
		ASSERT_UNREACHABLE;

	case a2::Tag::Where:
		ASSERT_UNREACHABLE;

	case a2::Tag::Expects:
		ASSERT_UNREACHABLE;

	case a2::Tag::Ensures:
		ASSERT_UNREACHABLE;

	case a2::Tag::Definition:
		ASSERT_UNREACHABLE;

	case a2::Tag::Block:
		ASSERT_UNREACHABLE;

	case a2::Tag::If:
		ASSERT_UNREACHABLE;

	case a2::Tag::For:
		ASSERT_UNREACHABLE;

	case a2::Tag::ForEach:
		ASSERT_UNREACHABLE;

	case a2::Tag::Switch:
		ASSERT_UNREACHABLE;

	case a2::Tag::Case:
		ASSERT_UNREACHABLE;

	case a2::Tag::Func:
		ASSERT_UNREACHABLE;

	case a2::Tag::Trait:
		ASSERT_UNREACHABLE;

	case a2::Tag::Impl:
		ASSERT_UNREACHABLE;

	case a2::Tag::Catch:
		ASSERT_UNREACHABLE;

	case a2::Tag::ValIdentifer:
	{
		ASSERT_OR_IGNORE(!a2::has_children(node));

		const IdentifierId identifier_id = a2::attachment_of<a2::ValIdentifierData>(node)->identifier_id;

		const OptPtr<a2::Node> opt_definition = lookup_identifier(resolver, identifier_id);

		if (is_none(opt_definition))
		{
			const Range<char8> name = identifier_entry_from_id(resolver->identifiers, identifier_id)->range();

			panic("Could not find definition for identifier '%.*s'\n", static_cast<s32>(name.count()), name.begin());
		}

		a2::Node* const definition = get_ptr(opt_definition);

		a2::DefinitionData* const definition_data = a2::attachment_of<a2::DefinitionData>(definition);

		if (definition_data->type_id == INVALID_TYPE_ID)
			resolve_definition(resolver, definition);

		Value* definition_value = nullptr;

		if (definition_data->value_id == INVALID_VALUE_ID)
		{
			const TypeMemoryInfo memory_info = get_type_memory_info(resolver->types, definition_data->type_id);

			const ValueLocation location = alloc_value(resolver->values, memory_info.bytes, memory_info.alignment);

			location.ptr->header.type = definition_data->type_id;

			definition_data->value_id = location.id;

			const OptPtr<a2::Node> opt_body = a2::get_definition_body(definition);

			if (is_some(opt_body))
			{
				eval_expr(resolver, get_ptr(opt_body));

				Value* const evaluated = top_value(resolver);

				memcpy(location.ptr->value, evaluated->value, memory_info.bytes);
			}
			else
			{
				location.ptr->header.is_undefined = true;
			}

			definition_value = location.ptr;
		}
		else
		{
			definition_value = value_from_id(resolver->values, definition_data->value_id);
		}

		Value* const stack_value = push_value(resolver, sizeof(ReferenceValue));

		stack_value->header.is_ref = true;

		stack_value->header.type = definition_data->type_id;

		reinterpret_cast<ReferenceValue*>(stack_value->value)->referenced = definition_value;

		break;
	}

	case a2::Tag::ValInteger:
		ASSERT_UNREACHABLE;

	case a2::Tag::ValFloat:
		ASSERT_UNREACHABLE;

	case a2::Tag::ValChar:
		ASSERT_UNREACHABLE;

	case a2::Tag::ValString:
		ASSERT_UNREACHABLE;

	case a2::Tag::Return:
		ASSERT_UNREACHABLE;

	case a2::Tag::Leave:
		ASSERT_UNREACHABLE;

	case a2::Tag::Yield:
		ASSERT_UNREACHABLE;

	case a2::Tag::ParameterList:
		ASSERT_UNREACHABLE;

	case a2::Tag::Call:
		ASSERT_UNREACHABLE;

	case a2::Tag::UOpTypeTailArray:
		ASSERT_UNREACHABLE;

	case a2::Tag::UOpTypeMultiPtr:
	case a2::Tag::UOpTypeOptMultiPtr:
	case a2::Tag::UOpTypeSlice:
	case a2::Tag::UOpTypeOptPtr:
	case a2::Tag::UOpTypePtr:
	{
		ASSERT_OR_IGNORE(a2::has_children(node));

		a2::Node* const element_type_node = a2::first_child_of(node);

		ASSERT_OR_IGNORE(!a2::has_next_sibling(element_type_node));

		const TypeId element_type_id = eval_type_expr(resolver, element_type_node);

		SliceType type{};
		type.element_id = element_type_id;

		Value* const value = push_value(resolver, sizeof(TypeId));

		value->header.type = get_builtin_type_ids(resolver->types)->type_type_id;

		TypeFlag flags = TypeFlag::EMPTY;

		TypeTag tag = TypeTag::Ptr;

		if (node->tag == a2::Tag::UOpTypeSlice)
			tag = TypeTag::Slice;
		else if (node->tag == a2::Tag::UOpTypeMultiPtr)
			flags = TypeFlag::Ptr_IsMulti;
		else if (node->tag == a2::Tag::UOpTypeOptMultiPtr)
			flags = TypeFlag::Ptr_IsOpt | TypeFlag::Ptr_IsMulti;
		else if (node->tag == a2::Tag::UOpTypeOptPtr)
			flags = TypeFlag::Ptr_IsOpt;
		else
			ASSERT_OR_IGNORE(node->tag == a2::Tag::UOpTypePtr);

		if (a2::has_flag(node, a2::Flag::Type_IsMut))
			flags |= TypeFlag::SliceOrPtr_IsMut;

		*reinterpret_cast<TypeId*>(value->value) = id_from_type(resolver->types, tag, flags, range::from_object_bytes(&type));

		break;
	}

	case a2::Tag::UOpEval:
		ASSERT_UNREACHABLE;

	case a2::Tag::UOpTry:
		ASSERT_UNREACHABLE;

	case a2::Tag::UOpDefer:
		ASSERT_UNREACHABLE;

	case a2::Tag::UOpAddr:
		ASSERT_UNREACHABLE;

	case a2::Tag::UOpDeref:
		ASSERT_UNREACHABLE;

	case a2::Tag::UOpBitNot:
		ASSERT_UNREACHABLE;

	case a2::Tag::UOpLogNot:
		ASSERT_UNREACHABLE;

	case a2::Tag::UOpTypeVar:
		ASSERT_UNREACHABLE;

	case a2::Tag::UOpImpliedMember:
		ASSERT_UNREACHABLE;

	case a2::Tag::UOpNegate:
		ASSERT_UNREACHABLE;

	case a2::Tag::UOpPos:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpAdd:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpSub:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpMul:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpDiv:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpAddTC:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpSubTC:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpMulTC:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpMod:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpBitAnd:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpBitOr:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpBitXor:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpShiftL:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpShiftR:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpLogAnd:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpLogOr:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpMember:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpCmpLT:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpCmpGT:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpCmpLE:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpCmpGE:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpCmpNE:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpCmpEQ:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpSet:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpSetAdd:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpSetSub:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpSetMul:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpSetDiv:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpSetAddTC:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpSetSubTC:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpSetMulTC:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpSetMod:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpSetBitAnd:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpSetBitOr:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpSetBitXor:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpSetShiftL:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpSetShiftR:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpTypeArray:
		ASSERT_UNREACHABLE;

	case a2::Tag::OpArrayIndex:
		ASSERT_UNREACHABLE;

	default:
		ASSERT_UNREACHABLE;
	}
}

static TypeId type_parameter(Resolver* resolver, a2::Node* node) noexcept
{
	ASSERT_OR_IGNORE(node->tag == a2::Tag::Definition);

	if (!a2::has_flag(node, a2::Flag::Definition_HasType))
		panic("Untyped parameter definitions are not currently supported\n");

	ASSERT_OR_IGNORE(a2::has_children(node));

	a2::DefinitionData* const definition_data = a2::attachment_of<a2::DefinitionData>(node);

	const TypeId type_id = eval_type_expr(resolver, a2::first_child_of(node));

	if (type_id == INVALID_TYPE_ID)
		panic("Expected type expression after ':'\n");
		
	definition_data->type_id = type_id;

	return type_id;
}

static TypeId type_expr(Resolver* resolver, a2::Node* node) noexcept
{
	switch (node->tag)
	{
	case a2::Tag::ValFloat:
	{
		return get_builtin_type_ids(resolver->types)->comp_float_type_id;
	}
	
	case a2::Tag::ValInteger:
	case a2::Tag::ValChar:
	{
		return get_builtin_type_ids(resolver->types)->comp_integer_type_id;
	}

	case a2::Tag::ValString:
	{
		return get_builtin_type_ids(resolver->types)->comp_string_type_id;
	}

	case a2::Tag::ValIdentifer:
	{
		const IdentifierId identifier_id = a2::attachment_of<a2::ValIdentifierData>(node)->identifier_id;

		const OptPtr<a2::Node> opt_definition = lookup_identifier(resolver, identifier_id);

		if (is_none(opt_definition))
		{
			const Range<char8> name = identifier_entry_from_id(resolver->identifiers, identifier_id)->range();

			panic("Could not find definition for identifier '%.*s'\n", static_cast<s32>(name.count()), name.begin());
		}

		a2::Node* const definition = get_ptr(opt_definition);

		ASSERT_OR_IGNORE(definition->tag == a2::Tag::Definition);

		a2::DefinitionData* const attachment = a2::attachment_of<a2::DefinitionData>(definition);

		if (attachment->type_id == INVALID_TYPE_ID)
			resolve_definition(resolver, definition);

		return attachment->type_id;
	}

	case a2::Tag::Func:
	{
		FuncDesc func_desc = get_func_desc(node);

		a2::FuncData* const func_data = a2::attachment_of<a2::FuncData>(node);

		if (is_some(func_desc.return_type))
		{		
			func_data->return_type_id = eval_type_expr(resolver, get_ptr(func_desc.return_type));
			
			if (func_data->return_type_id == INVALID_TYPE_ID)
				panic("Expected type expression following ':'\n");
		}
		else
		{
			func_data->return_type_id = get_builtin_type_ids(resolver->types)->void_type_id;
		}

		a2::DirectChildIterator it = a2::direct_children_of(func_desc.parameters);

		FuncTypeBuffer type_buf{};

		type_buf.header.return_type = func_data->return_type_id;
		type_buf.header.parameter_count = 0;

		for (OptPtr<a2::Node> parameter = a2::next(&it); is_some(parameter); parameter = a2::next(&it))
		{
			ASSERT_OR_IGNORE(type_buf.header.parameter_count + 1 < array_count(type_buf.parameter_type_ids));

			type_buf.parameter_type_ids[type_buf.header.parameter_count] = type_parameter(resolver, get_ptr(parameter));

			type_buf.header.parameter_count += 1;
		}

		const TypeFlag flags = a2::has_flag(node, a2::Flag::Func_IsProc) ? TypeFlag::Func_IsProc : TypeFlag::EMPTY;

		const Range<byte> type = Range<byte>{ reinterpret_cast<const byte*>(&type_buf), sizeof(type_buf.header) + type_buf.header.parameter_count * sizeof(type_buf.parameter_type_ids[0]) };

		func_data->signature_type_id = id_from_type(resolver->types, TypeTag::Func, flags, type);

		if (is_some(func_desc.body))
		{
			TypeId const returned_type_id = type_expr(resolver, get_ptr(func_desc.body));

			if (!can_implicity_convert_from_to(resolver->types, returned_type_id, func_data->return_type_id))
				panic("Mismatch between declared and actual return type\n");
		}

		return func_data->signature_type_id;
	}

	default:
		ASSERT_UNREACHABLE;
	}
}



static Scope* push_scope(Resolver* resolver, a2::Node* scope_root, u32 definition_count) noexcept
{
	Scope* const scope = static_cast<Scope*>(resolver->scopes.reserve_exact(sizeof(Scope) + definition_count * sizeof(Scope::definitions[0])));

	scope->root = scope_root;

	scope->definition_count = 0;

	ASSERT_OR_IGNORE(resolver->scope_count < array_count(resolver->scope_offsets));

	resolver->scope_offsets[resolver->scope_count] = static_cast<u32>(reinterpret_cast<u64*>(scope) - resolver->scopes.begin());

	resolver->scope_count += 1;

	return scope;
}

static void pop_scope(Resolver* resolver) noexcept
{
	ASSERT_OR_IGNORE(resolver->scope_count != 0);

	resolver->scopes.pop(resolver->scopes.used() - resolver->scope_offsets[resolver->scope_count - 1]);

	resolver->scope_count -= 1;
}

static void add_definition(Scope* scope, a2::Node* definition) noexcept
{
	ASSERT_OR_IGNORE(definition->tag == a2::Tag::Definition);

	ScopeEntry* const entry = scope->definitions + scope->definition_count;

	entry->identifier_id = a2::attachment_of<a2::DefinitionData>(definition)->identifier_id;
	entry->node_offset = static_cast<u32>(reinterpret_cast<u32*>(definition) - reinterpret_cast<u32*>(scope->root));
	
	scope->definition_count += 1;
}



Resolver* create_resolver(AllocPool* pool, IdentifierPool* identifiers, TypePool* types, ValuePool* values, a2::Node* builtin_definitions) noexcept
{
	ASSERT_OR_IGNORE(builtin_definitions->tag == a2::Tag::Block);

	Resolver* const resolver = static_cast<Resolver*>(alloc_from_pool(pool, sizeof(Resolver), alignof(Resolver)));

	resolver->identifiers = identifiers;

	resolver->types = types;

	resolver->values = values;

	resolver->scopes.init(1u << 22, 1u << 18);

	resolver->stack.init(1u << 30, 1u << 18);

	const u32 builtin_definition_count = a2::attachment_of<a2::BlockData>(builtin_definitions)->definition_count;

	Scope* const builtin_scope = push_scope(resolver, builtin_definitions, builtin_definition_count);

	a2::DirectChildIterator it = a2::direct_children_of(builtin_definitions);

	for (OptPtr<a2::Node> rst = a2::next(&it); is_some(rst); rst = a2::next(&it))
	{
		a2::Node* const node = get_ptr(rst);

		if (node->tag != a2::Tag::Definition)
			continue;

		ASSERT_OR_IGNORE(builtin_scope->definition_count < builtin_definition_count);

		add_definition(builtin_scope, node);
	}

	ASSERT_OR_IGNORE(builtin_scope->definition_count == builtin_definition_count);

	return resolver;
}

void set_file_scope(Resolver* resolver, a2::Node* file_root) noexcept
{
	ASSERT_OR_IGNORE(resolver->scope_count != 0);

	ASSERT_OR_IGNORE(file_root->tag == a2::Tag::File);

	const u32 definition_count = a2::attachment_of<a2::FileData>(file_root)->root_block.definition_count;

	if (resolver->scope_count >= 2)
		resolver->scopes.pop(resolver->scopes.used() - resolver->scope_offsets[1]);

	resolver->scope_count = 1;

	Scope* const scope = push_scope(resolver, file_root, a2::attachment_of<a2::FileData>(file_root)->root_block.definition_count);

	a2::DirectChildIterator it = a2::direct_children_of(file_root);

	for (OptPtr<a2::Node> rst = a2::next(&it); is_some(rst); rst = a2::next(&it))
	{
		a2::Node* const node = get_ptr(rst);

		if (node->tag != a2::Tag::Definition)
			continue;

		ASSERT_OR_IGNORE(scope->definition_count < definition_count);

		add_definition(scope, node);
	}

	ASSERT_OR_IGNORE(scope->definition_count == definition_count);
}

void resolve_definition(Resolver* resolver, a2::Node* node) noexcept
{
	ASSERT_OR_IGNORE(node->tag == a2::Tag::Definition);

	ASSERT_OR_IGNORE(a2::has_children(node));

	a2::Node* value = a2::first_child_of(node);

	a2::Node* type = nullptr;

	if (a2::has_flag(node, a2::Flag::Definition_HasType))
	{
		type = value;

		value = a2::has_next_sibling(value) ? a2::next_sibling_of(value) : nullptr;
	}

	TypeId type_id = INVALID_TYPE_ID;

	if (type != nullptr)
	{
		type_id = eval_type_expr(resolver, type);

		if (type_id == INVALID_TYPE_ID)
			panic("Expected type expression following ':'\n");
	}

	if (value != nullptr)
	{
		const TypeId inferred_type_id = type_expr(resolver, value);

		if (type == nullptr)
		{
			type_id = inferred_type_id;			
		}
		else if (!can_implicity_convert_from_to(resolver->types, inferred_type_id, type_id))
			panic("Incompatible types\n");

		a2::DefinitionData* const attach = a2::attachment_of<a2::DefinitionData>(node);

		attach->type_id = type_id;
	}
}
