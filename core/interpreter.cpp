#include "pass_data.hpp"

#include "../diag/diag.hpp"
#include "../infra/container.hpp"

using BuiltinFunc = void* (*) (Interpreter* interp, AstNode* call_node) noexcept;

struct Interpreter
{
	SourceReader* reader;

	Parser* parser;

	TypePool* types;

	AstPool* asts;

	IdentifierPool* identifiers;

	GlobalValuePool* globals;

	ErrorSink* errors;

	ReservedVec<u64> value_stack;

	ReservedVec<u32> value_stack_inds;

	ReservedVec<u64> activation_records;

	u32 activation_record_top;

	TypeId prelude_type_id;

	s32 context_top;

	TypeId contexts[256];

	u32 context_activation_record_limits[256];

	TypeId builtin_type_ids[static_cast<u8>(Builtin::MAX)];

	BuiltinFunc builtin_values[static_cast<u8>(Builtin::MAX)];

	minos::FileHandle log_file;

	bool log_prelude;
};

struct alignas(8) ActivationRecordDesc
{
	u32 prev_top : 31;

	u32 is_root : 1;

	TypeId type_id;

	#if COMPILER_MSVC
	#pragma warning(push)
	#pragma warning(disable : 4200) // C4200: nonstandard extension used: zero-sized array in struct/union
	#elif COMPILER_CLANG
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Wc99-extensions" // flexible array members are a C99 feature
	#elif COMPILER_GCC
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wpedantic" // ISO C++ forbids flexible array member
	#endif
	byte record[];
	#if COMPILER_MSVC
	#pragma warning(pop)
	#elif COMPILER_CLANG
	#pragma clang diagnostic pop
	#elif COMPILER_GCC
	#pragma GCC diagnostic pop
	#endif
};

struct FuncTypeParamHelper
{
	IdentifierId name;

	TypeId type;
};

struct alignas(8) Callable
{
	u32 type_id_bits : 31;

	u32 is_builtin : 1;

	union
	{
		AstNodeId ast;

		u8 ordinal;
	} code;
};

union ArrayValue
{
	void* value_ptr;

	u64 padding_qwords;
};



static void force_member_type(Interpreter* interp, MemberInfo* member) noexcept; 

static void force_member_value(Interpreter* interp, MemberInfo* member) noexcept; 

static void* evaluate_expr(Interpreter* interp, AstNode* node, TypeId target_type_id) noexcept;

static void* address_expr(Interpreter* interp, AstNode* node) noexcept;

static TypeIdWithAssignability typecheck_expr(Interpreter* interp, AstNode* node) noexcept;



static bool is_valid(TypeId id) noexcept
{
	return id != TypeId::INVALID && id != TypeId::CHECKING && id != TypeId::NO_TYPE;
}

static bool is_valid(TypeIdWithAssignability id) noexcept
{
	return is_valid(type_id(id));
}



static TypeId curr_typechecker_context(Interpreter* interp) noexcept
{
	ASSERT_OR_IGNORE(interp->context_top >= 0);

	return interp->contexts[interp->context_top];
}

static void push_typechecker_context(Interpreter* interp, TypeId context) noexcept
{
	ASSERT_OR_IGNORE(interp->context_top >= 0);

	ASSERT_OR_IGNORE(context != TypeId::INVALID);

	ASSERT_OR_IGNORE(interp->contexts[interp->context_top] == lexical_parent_type_from_id(interp->types, context));

	interp->contexts[interp->context_top] = context;
}

static void pop_typechecker_context(Interpreter* interp) noexcept
{
	ASSERT_OR_IGNORE(interp->context_top >= 0);

	const TypeId old_context = interp->contexts[interp->context_top];

	ASSERT_OR_IGNORE(old_context != TypeId::INVALID);

	const TypeId new_context = lexical_parent_type_from_id(interp->types, old_context);

	ASSERT_OR_IGNORE(new_context != TypeId::INVALID);

	interp->contexts[interp->context_top] = new_context;
}

static void set_typechecker_context(Interpreter* interp, TypeId context) noexcept
{
	if (interp->context_top + 1 == static_cast<s32>(array_count(interp->contexts)))
		panic("Maximum active interpreter context limit of %u exceeded.\n", array_count(interp->contexts));

	interp->context_top += 1;

	interp->contexts[interp->context_top] = context;

	interp->context_activation_record_limits[interp->context_top] = interp->activation_records.used();
}

static void unset_typechecker_context(Interpreter* interp) noexcept
{
	ASSERT_OR_IGNORE(interp->context_top >= 0);

	interp->context_top -= 1;
}



static bool has_activation_record(const Interpreter* interp) noexcept
{
	return interp->context_top >= 0
	    && interp->activation_records.used() > interp->context_activation_record_limits[interp->context_top];
}

static ActivationRecordDesc* curr_activation_record(Interpreter* interp) noexcept
{
	ASSERT_OR_IGNORE(has_activation_record(interp));

	return reinterpret_cast<ActivationRecordDesc*>(interp->activation_records.begin() + interp->activation_record_top);
}

static void* push_activation_record(Interpreter* interp, TypeId record_type_id, bool is_root) noexcept
{
	ASSERT_OR_IGNORE(type_tag_from_id(interp->types, record_type_id) == TypeTag::Composite);

	const TypeMetrics record_metrics = type_metrics_from_id(interp->types, record_type_id);

	// TODO: Make this properly aligned for over-aligned types.
	// That also needs to account for the 8-byte skew created by the
	// `ActivationRecordDesc`.

	ActivationRecordDesc* const desc = static_cast<ActivationRecordDesc*>(interp->activation_records.reserve_padded(static_cast<u32>(sizeof(ActivationRecordDesc) + record_metrics.size)));
	desc->prev_top = interp->activation_record_top;
	desc->is_root = is_root;
	desc->type_id = record_type_id;

	interp->activation_record_top = static_cast<u32>(reinterpret_cast<const u64*>(desc) - interp->activation_records.begin());

	return desc + 1;
}

static void pop_activation_record(Interpreter* interp) noexcept
{
	ASSERT_OR_IGNORE(has_activation_record(interp));

	const u32 new_top = curr_activation_record(interp)->prev_top;

	interp->activation_records.pop_to(interp->activation_record_top);

	interp->activation_record_top = new_top;
}

static bool has_parent_activation_record(Interpreter* interp, ActivationRecordDesc* record) noexcept
{
	return !record->is_root && record->prev_top > interp->context_activation_record_limits[interp->activation_record_top];
}

static ActivationRecordDesc* parent_activation_record(Interpreter* interp, ActivationRecordDesc* record) noexcept
{
	ASSERT_OR_IGNORE(!record->is_root);

	return reinterpret_cast<ActivationRecordDesc*>(interp->activation_records.begin() + record->prev_top);
}



static MemberInfo lookup_identifier_definition_in_context(Interpreter* interp, TypeId context, IdentifierId identifier_id, SourceId lookup_source) noexcept
{
	while (true)
	{
		if (context == TypeId::INVALID)
			break;

		MemberInfo info;

		if (type_member_info_by_name(interp->types, context, identifier_id, &info))
			return info;

		context = lexical_parent_type_from_id(interp->types, context);

		if (context == TypeId::INVALID)
			break;
	}

	const Range<char8> name = identifier_name_from_id(interp->identifiers, identifier_id);

	source_error(interp->errors, lookup_source, "Could not find definition for identifier %.*s\n", static_cast<s32>(name.count()), name.begin());
}

static MemberInfo lookup_identifier_definition(Interpreter* interp, IdentifierId identifier_id, SourceId lookup_source) noexcept
{
	TypeId context = curr_typechecker_context(interp);

	return lookup_identifier_definition_in_context(interp, context, identifier_id, lookup_source);
}

static void* lookup_identifier_value(Interpreter* interp, IdentifierId identifier_id, SourceId lookup_source) noexcept
{
	TypeId static_context;

	if (has_activation_record(interp))
	{
		ActivationRecordDesc* record = curr_activation_record(interp);

		while (true)
		{
			MemberInfo member;

			if (type_member_info_by_name(interp->types, record->type_id, identifier_id, &member))
			{
				if (member.is_global)
				{
					if (member.has_pending_value)
						TODO("Implement lazy value creation for globals upon name lookup.");

					TODO("Implement global value lookup.");
				}
				else
				{
					ASSERT_OR_IGNORE(!member.has_pending_value);

					return record->record + member.offset;
				}
			}

			if (!has_parent_activation_record(interp, record))
				break;

			record = parent_activation_record(interp, record);
		}

		static_context = lexical_parent_type_from_id(interp->types, record->type_id);
	}
	else
	{
		static_context = curr_typechecker_context(interp);
	}

	MemberInfo member = lookup_identifier_definition_in_context(interp, static_context, identifier_id, lookup_source);

	if (!member.is_global)
		source_error(interp->errors, lookup_source, "Cannot reference non-global member of lexical parent scope.\n");

	force_member_value(interp, &member);

	return global_value_from_id(interp->globals, member.value.complete).address;
}



static MemberInit member_init_from_definition(Interpreter* interp, TypeId lexical_parent_type_id, AstNode* definition, DefinitionInfo info, u64 offset) noexcept
{
	ASSERT_OR_IGNORE(definition->tag == AstTag::Definition);

	const bool has_pending_type = !is_valid(definition->type_id);

	// TODO
	const bool has_pending_value = true;

	MemberInit member;
	member.name = attachment_of<AstDefinitionData>(definition)->identifier_id;
	member.source = definition->source_id;
	member.lexical_parent_type_id = lexical_parent_type_id,
	member.is_global = has_flag(definition, AstFlag::Definition_IsGlobal);
	member.is_pub = has_flag(definition, AstFlag::Definition_IsPub);
	member.is_use = has_flag(definition, AstFlag::Definition_IsUse);
	member.is_mut = has_flag(definition, AstFlag::Definition_IsMut);
	member.has_pending_type = has_pending_type;
	member.has_pending_value = has_pending_value;
	member.offset = offset;

	if (has_pending_type)
		member.type.pending = is_some(info.type) ? id_from_ast_node(interp->asts, get_ptr(info.type)) : AstNodeId::INVALID;
	else
		member.type.complete = type_id(definition->type_id);

	if (has_pending_value)
		member.value.pending = is_some(info.value) ? id_from_ast_node(interp->asts, get_ptr(info.value)) : AstNodeId::INVALID;
	else
		TODO("Implement passing `ValueId` to `member_init_from_definition`.");

	return member;
}



static void* push_temporary(Interpreter* interp, u64 size, u32 align) noexcept
{
	ASSERT_OR_IGNORE(is_pow2(align));

	if (size > UINT32_MAX)
		panic("Temporary value exceeds maximum size.\n");

	const u32 old_top = interp->value_stack.used();

	interp->value_stack_inds.append(old_top);

	if (align > 8)
		interp->value_stack.pad_to_alignment(align);

	return interp->value_stack.reserve_padded(static_cast<u32>(size));
}

static void pop_temporary(Interpreter* interp) noexcept
{
	const u32 new_top = interp->value_stack_inds.top();

	interp->value_stack.pop_to(new_top);

	interp->value_stack_inds.pop_by(1);
}



static void* implicit_convert(Interpreter* interp, void* stack_top, TypeId source_type_id, TypeId target_type_id, TypeTag target_type_tag, SourceId source_id) noexcept
{
	switch (target_type_tag)
	{
	case TypeTag::Type:
	case TypeTag::Definition:
	case TypeTag::CompInteger:
	case TypeTag::CompFloat:
	case TypeTag::Boolean:
	case TypeTag::Ptr:
	case TypeTag::Func:
	case TypeTag::Builtin:
	case TypeTag::CompositeLiteral:
	case TypeTag::ArrayLiteral:
	case TypeTag::TypeBuilder:
	case TypeTag::Variadic:
	case TypeTag::Trait:
	case TypeTag::TailArray:
	{
		ASSERT_OR_IGNORE(target_type_tag == type_tag_from_id(interp->types, source_type_id));

		return stack_top;
	}

	case TypeTag::Void:
	{
		if (source_type_id == target_type_id)
			return stack_top;

		const TypeTag source_type_tag = type_tag_from_id(interp->types, source_type_id);

		if (source_type_tag == TypeTag::Void)
			return stack_top;

		ASSERT_OR_IGNORE(source_type_tag == TypeTag::Definition);

		pop_temporary(interp);

		return push_temporary(interp, 0, 1);
	}

	case TypeTag::Integer:
	{
		const TypeTag source_type_tag = type_tag_from_id(interp->types, source_type_id);

		if (source_type_tag == TypeTag::CompInteger)
		{
			const CompIntegerValue v = *static_cast<CompIntegerValue*>(stack_top);

			memset(stack_top, 0, sizeof(v));

			const NumericType* const target_type = static_cast<const NumericType*>(simple_type_structure_from_id(interp->types, target_type_id));

			if (target_type->is_signed)
			{
				if (!s64_from_comp_integer(v, static_cast<u8>(target_type->bits), static_cast<s64*>(stack_top)))
					source_error(interp->errors, source_id, "`CompInteger` value does not fit into signed %u bit integer.\n", target_type->bits);
			}
			else
			{
				if (!u64_from_comp_integer(v, static_cast<u8>(target_type->bits), static_cast<u64*>(stack_top)))
					source_error(interp->errors, source_id, "`CompInteger` value does not fit into unsigned %u bit integer.\n", target_type->bits);
			}
		}
		#ifndef _NDEBUG
		else
		{
			ASSERT_OR_IGNORE(source_type_tag == TypeTag::Integer);

			const NumericType* const target_type = static_cast<const NumericType*>(simple_type_structure_from_id(interp->types, target_type_id));

			const NumericType* const source_type = static_cast<const NumericType*>(simple_type_structure_from_id(interp->types, source_type_id));

			ASSERT_OR_IGNORE(target_type->bits == source_type->bits && target_type->is_signed == source_type->is_signed);
		}
		#endif

		return stack_top;
	}

	case TypeTag::Float:
	{
		const TypeTag source_type_tag = type_tag_from_id(interp->types, source_type_id);

		if (source_type_tag == TypeTag::CompFloat)
		{
			const CompFloatValue v = *static_cast<CompFloatValue*>(stack_top);

			memset(stack_top, 0, sizeof(v));

			const NumericType* const target_type = static_cast<const NumericType*>(simple_type_structure_from_id(interp->types, target_type_id));

			if (target_type->bits == 32)
			{
				*static_cast<f32*>(stack_top) = f32_from_comp_float(v);
			}
			else
			{
				ASSERT_OR_IGNORE(target_type->bits == 64);

				*static_cast<f64*>(stack_top) = f64_from_comp_float(v);
			}
		}
		#ifndef _NDEBUG
		else
		{
			ASSERT_OR_IGNORE(source_type_tag == TypeTag::Float);

			const NumericType* const target_type = static_cast<const NumericType*>(simple_type_structure_from_id(interp->types, target_type_id));

			const NumericType* const source_type = static_cast<const NumericType*>(simple_type_structure_from_id(interp->types, source_type_id));

			ASSERT_OR_IGNORE(target_type->bits == source_type->bits);
		}
		#endif

		return stack_top;
	}

	case TypeTag::Slice:
	{
		const TypeTag source_type_tag = type_tag_from_id(interp->types, source_type_id);

		if (source_type_tag == TypeTag::Array)
		{
			const ArrayType* const source_type = static_cast<const ArrayType*>(simple_type_structure_from_id(interp->types, source_type_id));

			const ArrayValue v = *static_cast<ArrayValue*>(stack_top);

			if ((v.padding_qwords & 1) == 1)
				TODO("Implement implicit conversion of non-global");

			pop_temporary(interp);

			Range<byte>* const new_stack_value = static_cast<Range<byte>*>(push_temporary(interp, 16, 8));

			*new_stack_value = Range{ static_cast<byte*>(v.value_ptr), source_type->element_count };

			return new_stack_value;
		}
		#ifndef _NDEBUG
		else
		{
			ASSERT_OR_IGNORE(source_type_tag == TypeTag::Slice);

			const ReferenceType* const target_type = static_cast<const ReferenceType*>(simple_type_structure_from_id(interp->types, target_type_id));

			const ReferenceType* const source_type = static_cast<const ReferenceType*>(simple_type_structure_from_id(interp->types, source_type_id));

			ASSERT_OR_IGNORE(is_same_type(interp->types, target_type->referenced_type_id, source_type->referenced_type_id));

			ASSERT_OR_IGNORE(!target_type->is_mut || source_type->is_mut);

			ASSERT_OR_IGNORE(!target_type->is_multi || source_type->is_multi);

			ASSERT_OR_IGNORE(target_type->is_opt || !source_type->is_opt);
		}
		#endif
		
		return stack_top;
	}

	case TypeTag::Array:
	{
		TODO("Implement literal-to-array conversion");
	}

	case TypeTag::Composite:
	{
		TODO("Implement literal-to-composite conversion");
	}

	case TypeTag::INVALID:
	case TypeTag::Divergent:
	case TypeTag::TypeInfo:
		ASSERT_UNREACHABLE;
	}

	ASSERT_UNREACHABLE;
}

static void* evaluate_expr_impl(Interpreter* interp, AstNode* node) noexcept
{
	ASSERT_OR_IGNORE(is_valid(node->type_id));

	switch (node->tag)
	{
	case AstTag::Builtin:
	{
		const u8 ordinal = static_cast<u8>(node->flags);

		Callable* const dst = static_cast<Callable*>(push_temporary(interp, 8, 8));
		dst->type_id_bits = static_cast<u32>(interp->builtin_type_ids[ordinal]);
		dst->is_builtin = true;
		dst->code.ordinal = ordinal;

		return dst;
	}

	case AstTag::If:
	{
		IfInfo info = get_if_info(node);

		ASSERT_OR_IGNORE(is_some(info.alternative) || type_tag_from_id(interp->types, type_id(node->type_id)) == TypeTag::Void);

		const bool condition_value = *static_cast<bool*>(evaluate_expr(interp, info.condition, type_id(info.condition->type_id)));

		pop_temporary(interp);

		if (condition_value)
			return evaluate_expr(interp, info.consequent, type_id(node->type_id));
		else if (is_some(info.alternative))
			return evaluate_expr(interp, get_ptr(info.alternative), type_id(node->type_id));
		else
			return push_temporary(interp, 0, 1); // Void
	}

	case AstTag::Identifer:
	{
		// TODO: Implicitly convert to target_type_id

		const void* const identifier_value = lookup_identifier_value(interp, attachment_of<AstIdentifierData>(node)->identifier_id, node->source_id);

		const TypeMetrics metrics = type_metrics_from_id(interp->types, type_id(node->type_id));

		void* const stack_value = push_temporary(interp, metrics.size, metrics.align);

		memcpy(stack_value, identifier_value, metrics.size);

		return stack_value;
	}

	case AstTag::LitInteger:
	{
		const CompIntegerValue value = attachment_of<AstLitIntegerData>(node)->value;

		// We overallocate in case of non-64-bit integers. That's fine, as
		// stack slots are 8-byte aligned anyways.
		CompIntegerValue* const stack_value = static_cast<CompIntegerValue*>(push_temporary(interp, 8, 8));

		*stack_value = value;

		return stack_value;
	}

	case AstTag::LitFloat:
	{
		const CompFloatValue value = attachment_of<AstLitFloatData>(node)->value;

		// We overallocate in case of f32. That's fine, as stack slots are
		// 8-byte aligned anyways.
		CompFloatValue* const stack_value = static_cast<CompFloatValue*>(push_temporary(interp, 8, 8));

		*stack_value = value;

		return stack_value;
	}

	case AstTag::LitChar:
	{
		const u32 value = attachment_of<AstLitCharData>(node)->codepoint;

		// We overallocate in case of non-64-bit integers. That's fine, as
		// stack slots are 8-byte aligned anyways.
		CompIntegerValue* const stack_value = static_cast<CompIntegerValue*>(push_temporary(interp, 8, 8));

		*stack_value = comp_integer_from_u64(value);

		return stack_value;
	}

	case AstTag::LitString:
	{
		ArrayValue* const stack_value = static_cast<ArrayValue*>(push_temporary(interp, sizeof(ArrayValue), alignof(ArrayValue)));

		stack_value->value_ptr = global_value_from_id(interp->globals, attachment_of<AstLitStringData>(node)->string_value_id).address;

		ASSERT_OR_IGNORE((reinterpret_cast<u64>(stack_value->value_ptr) & 1) == 0);

		return stack_value;
	}

	case AstTag::Call:
	{
		AstNode* const callee = first_child_of(node);

		const Callable callable = *static_cast<Callable*>(evaluate_expr(interp, callee, type_id(callee->type_id)));

		pop_temporary(interp);

		const TypeId func_type_id = TypeId{ callable.type_id_bits };

		const FuncType* const func_type = static_cast<const FuncType*>(simple_type_structure_from_id(interp->types, func_type_id));

		const TypeId signature_type_id = func_type->signature_type_id;

		const TypeMetrics signature_metrics = type_metrics_from_id(interp->types, signature_type_id);

		void* const temp_activation_record = push_temporary(interp, signature_metrics.size, signature_metrics.align);

		AstNode* argument = callee;

		u16 rank = 0;

		u64 seen_argument_mask = 0;

		while (has_next_sibling(argument))
		{
			argument = next_sibling_of(argument);

			MemberInfo member;

			const void* member_value;

			if (argument->tag == AstTag::OpSet)
			{
				AstNode* const argument_name = first_child_of(argument);

				AstNode* const argument_value = next_sibling_of(argument_name);

				if (!type_member_info_by_name(interp->types, signature_type_id, attachment_of<AstIdentifierData>(argument_name)->identifier_id, &member))
					ASSERT_UNREACHABLE;

				member_value = evaluate_expr(interp, argument_value, member.type.complete);
			}
			else
			{
				if (!type_member_info_by_rank(interp->types, signature_type_id, rank, &member))
					ASSERT_UNREACHABLE;

				member_value = evaluate_expr(interp, argument, member.type.complete);

				rank += 1;
			}

			ASSERT_OR_IGNORE(member.is_global == false);

			const TypeMetrics member_metrics = type_metrics_from_id(interp->types, member.type.complete);

			memcpy(static_cast<byte*>(temp_activation_record) + member.offset, member_value, member_metrics.size);

			pop_temporary(interp);

			seen_argument_mask |= static_cast<u64>(1) << member.rank;
		}

		for (u16 i = 0; i != func_type->param_count; ++i)
		{
			const u64 curr_argument_bit = static_cast<u64>(1) << i;

			if ((seen_argument_mask & curr_argument_bit) != 0)
				continue;

			TODO("Copy default value to activation record.");
		}

		void* const activation_record = push_activation_record(interp, signature_type_id, true);

		memcpy(activation_record, temp_activation_record, signature_metrics.size);

		void* result;

		if (callable.is_builtin)
			result = interp->builtin_values[callable.code.ordinal](interp, node);
		else
			result = evaluate_expr(interp, ast_node_from_id(interp->asts, callable.code.ast), type_id(node->type_id));

		pop_activation_record(interp);

		return result;
	}

	case AstTag::UOpTypeTailArray:
	{
		AstNode* const operand = first_child_of(node);

		ASSERT_OR_IGNORE(type_tag_from_id(interp->types, type_id(operand->type_id)) == TypeTag::Type);

		const TypeId defined_operand_type_id = *static_cast<TypeId*>(evaluate_expr(interp, operand, type_id(operand->type_id)));

		ReferenceType tail_array_type{};
		tail_array_type.is_multi = false;
		tail_array_type.is_opt = false;
		tail_array_type.is_mut = true;
		tail_array_type.referenced_type_id = defined_operand_type_id;

		const TypeId defined_type_id = simple_type(interp->types, TypeTag::TailArray, range::from_object_bytes(&tail_array_type));

		TypeId* const stack_value = static_cast<TypeId*>(push_temporary(interp, 4, 4));

		*stack_value = defined_type_id;

		return stack_value;
	}

	case AstTag::UOpTypeSlice:
	{
		AstNode* const operand = first_child_of(node);

		ASSERT_OR_IGNORE(type_tag_from_id(interp->types, type_id(operand->type_id)) == TypeTag::Type);

		const TypeId defined_operand_type_id = *static_cast<TypeId*>(evaluate_expr(interp, operand, type_id(operand->type_id)));

		pop_temporary(interp);

		ReferenceType slice_type{};
		slice_type.is_multi = false;
		slice_type.is_opt = false;
		slice_type.is_mut = has_flag(node, AstFlag::Type_IsMut);
		slice_type.referenced_type_id = defined_operand_type_id;

		const TypeId defined_type_id = simple_type(interp->types, TypeTag::Slice, range::from_object_bytes(&slice_type));

		TypeId* const stack_value = static_cast<TypeId*>(push_temporary(interp, 4, 4));

		*stack_value = defined_type_id;

		return stack_value;
	}

	case AstTag::UOpLogNot:
	{
		AstNode* const operand = first_child_of(node);

		bool* const operand_value = static_cast<bool*>(evaluate_expr(interp, operand, type_id(node->type_id)));

		*operand_value = !*operand_value;

		return operand_value;
	}

	case AstTag::OpMember:
	{
		AstNode* const lhs = first_child_of(node);

		const TypeTag lhs_type_tag = type_tag_from_id(interp->types, type_id(lhs->type_id));

		AstNode* const rhs = next_sibling_of(lhs);

		ASSERT_OR_IGNORE(rhs->tag == AstTag::Identifer);

		const IdentifierId member_name = attachment_of<AstIdentifierData>(rhs)->identifier_id;

		const void* member_address;

		TypeMetrics member_metrics;

		if (lhs_type_tag == TypeTag::Type)
		{
			const TypeId defined_type_id = *static_cast<TypeId*>(evaluate_expr(interp, lhs, type_id(lhs->type_id)));

			pop_temporary(interp);

			MemberInfo member;
	
			if (!type_member_info_by_name(interp->types, defined_type_id, member_name, &member))
			{
				const Range<char8> name = identifier_name_from_id(interp->identifiers, member_name);

				source_error(interp->errors, node->source_id, "Left-hand-side of member operator has member named '%.*s'", static_cast<s32>(name.count()), name.begin());
			}
	
			if (!member.is_global)
			{
				const Range<char8> name = identifier_name_from_id(interp->identifiers, member_name);

				source_error(interp->errors, node->source_id, "'%.*s' is not a global member of type-valued left-hand-side of member operator\n", static_cast<s32>(name.count()), name.begin());
			}

			force_member_value(interp, &member);

			member_metrics = type_metrics_from_id(interp->types, member.type.complete);

			member_address = global_value_from_id(interp->globals, member.value.complete).address;
		}
		else
		{
			MemberInfo member;

			if (!type_member_info_by_name(interp->types, type_id(lhs->type_id), member_name, &member))
			{
				const Range<char8> name = identifier_name_from_id(interp->identifiers, member_name);

				source_error(interp->errors, node->source_id, "Left-hand-side of member operator has member named '%.*s'", static_cast<s32>(name.count()), name.begin());
			}

			member_metrics = type_metrics_from_id(interp->types, member.type.complete);

			if (member.is_global)
			{
				force_member_value(interp, &member);

				member_address = global_value_from_id(interp->globals, member.value.complete).address;
			}
			else
			{
				void* const base_address = address_expr(interp, lhs);
	
				member_address = static_cast<byte*>(base_address) + member.offset;
			}
		}

		void* const stack_value = push_temporary(interp, member_metrics.size, member_metrics.align);

		memcpy(stack_value, member_address, member_metrics.size);

		return stack_value;
	}

	case AstTag::OpCmpEQ:
	{
		AstNode* const lhs = first_child_of(node);

		AstNode* const rhs = next_sibling_of(lhs);

		const void* const lhs_value = evaluate_expr(interp, lhs, type_id(lhs->type_id));

		const void* const rhs_value = evaluate_expr(interp, rhs, type_id(rhs->type_id));

		const TypeTag operand_type_tag = type_tag_from_id(interp->types, type_id(lhs->type_id));

		bool result;

		if (operand_type_tag == TypeTag::CompInteger)
		{
			result = comp_integer_equal(*static_cast<const CompIntegerValue*>(lhs_value), *static_cast<const CompIntegerValue*>(rhs_value));
		}
		else
		{
			TODO("Implement `OpCmpEq` for non-`CompInteger` types.\n");
		}

		pop_temporary(interp);

		pop_temporary(interp);

		bool *const stack_value = static_cast<bool*>(push_temporary(interp, 1, 1));

		*stack_value = result;

		return stack_value;
	}

	case AstTag::CompositeInitializer:
	case AstTag::ArrayInitializer:
	case AstTag::Wildcard:
	case AstTag::Where:
	case AstTag::Expects:
	case AstTag::Ensures:
	case AstTag::Definition:
	case AstTag::Block:
	case AstTag::For:
	case AstTag::ForEach:
	case AstTag::Switch:
	case AstTag::Func:
	case AstTag::Trait:
	case AstTag::Impl:
	case AstTag::Catch:
	case AstTag::Return:
	case AstTag::Leave:
	case AstTag::Yield:
	case AstTag::UOpTypeMultiPtr:
	case AstTag::UOpTypeOptMultiPtr:
	case AstTag::UOpEval:
	case AstTag::UOpTry:
	case AstTag::UOpDefer:
	case AstTag::UOpDistinct:
	case AstTag::UOpAddr:
	case AstTag::UOpDeref:
	case AstTag::UOpBitNot:
	case AstTag::UOpTypeOptPtr:
	case AstTag::UOpTypeVar:
	case AstTag::UOpImpliedMember:
	case AstTag::UOpTypePtr:
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
	case AstTag::OpCmpLT:
	case AstTag::OpCmpGT:
	case AstTag::OpCmpLE:
	case AstTag::OpCmpGE:
	case AstTag::OpCmpNE:
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
		panic("Evaluation of AST node type %s is not yet implemented.\n", tag_name(node->tag));

	case AstTag::INVALID:
	case AstTag::MAX:
	case AstTag::File:
	case AstTag::Case:
	case AstTag::ParameterList:
		; // fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

static void* evaluate_expr(Interpreter* interp, AstNode* node, TypeId target_type_id) noexcept
{
	ASSERT_OR_IGNORE(is_valid(target_type_id));

	const TypeTag target_type_tag = type_tag_from_id(interp->types, target_type_id);

	if (target_type_tag == TypeTag::TypeInfo)
	{
		TypeId* stack_top = static_cast<TypeId*>(push_temporary(interp, 4, 4));

		*stack_top = type_id(node->type_id);

		return stack_top;
	}
	else
	{
		void* const stack_top = evaluate_expr_impl(interp, node);

		return implicit_convert(interp, stack_top, type_id(node->type_id), target_type_id, target_type_tag, node->source_id);

	}
}

static void* address_expr(Interpreter* interp, AstNode* node) noexcept
{
	ASSERT_OR_IGNORE(is_valid(node->type_id));

	switch (node->tag)
	{
	case AstTag::Identifer:
	{
		return lookup_identifier_value(interp, attachment_of<AstIdentifierData>(node)->identifier_id, node->source_id);
	}
	
	case AstTag::OpMember:
	{
		AstNode* const lhs = first_child_of(node);

		void* const base_address = address_expr(interp, lhs);

		AstNode* const rhs = next_sibling_of(lhs);

		ASSERT_OR_IGNORE(rhs->tag == AstTag::Identifer);

		const IdentifierId member_name = attachment_of<AstIdentifierData>(rhs)->identifier_id;

		MemberInfo member;

		if (!type_member_info_by_name(interp->types, type_id(lhs->type_id), member_name, &member))
		{
			const Range<char8> name = identifier_name_from_id(interp->identifiers, member_name);
			
			source_error(interp->errors, node->source_id, "Left-hand-side of member operator has no member named '%.*s'.\n", static_cast<s32>(name.count()), name.begin());
		}

		if (member.is_global)
		{
			force_member_value(interp, &member);

			return global_value_from_id(interp->globals, member.value.complete).address;
		}
		else
		{
			return static_cast<byte*>(base_address) + member.offset;
		}
	}

	case AstTag::OpArrayIndex:
	{
		AstNode* const lhs = first_child_of(node);

		const TypeId lhs_type_id = type_id(lhs->type_id);

		const TypeTag lhs_type_tag = type_tag_from_id(interp->types, lhs_type_id);

		void* base_address;

		u64 stride;

		u64 index_limit;

		if (lhs_type_tag == TypeTag::Array)
		{
			const ArrayType* const array_type = static_cast<const ArrayType*>(simple_type_structure_from_id(interp->types, lhs_type_id));

			base_address = address_expr(interp, lhs);

			stride = type_metrics_from_id(interp->types, array_type->element_type).stride;

			index_limit = array_type->element_count;
		}
		else if (lhs_type_tag == TypeTag::Ptr)
		{
			const ReferenceType* const ptr_type = static_cast<const ReferenceType*>(simple_type_structure_from_id(interp->types, lhs_type_id));

			ASSERT_OR_IGNORE(ptr_type->is_multi && !ptr_type->is_opt);

			base_address = evaluate_expr(interp, lhs, type_id(lhs->type_id));

			pop_temporary(interp);

			stride = type_metrics_from_id(interp->types, ptr_type->referenced_type_id).stride;

			index_limit = UINT64_MAX;
		}
		else
		{
			ASSERT_OR_IGNORE(lhs_type_tag == TypeTag::Slice);

			const ReferenceType* const slice_type = static_cast<const ReferenceType*>(simple_type_structure_from_id(interp->types, lhs_type_id));

			MutRange<byte> slice_value = *static_cast<MutRange<byte>*>(evaluate_expr(interp, lhs, type_id(lhs->type_id)));

			pop_temporary(interp);
			
			base_address = slice_value.begin();

			stride = type_metrics_from_id(interp->types, slice_type->referenced_type_id).stride;

			const u64 slice_bytes = (reinterpret_cast<u64>(slice_value.end()) - reinterpret_cast<u64>(slice_value.begin()));

			ASSERT_OR_IGNORE(slice_bytes % stride == 0);

			index_limit = slice_bytes / stride;
		}

		AstNode* const rhs = next_sibling_of(lhs);

		NumericType u64_type{};
		u64_type.bits = 64;
		u64_type.is_signed = false;

		const TypeId u64_type_id = simple_type(interp->types, TypeTag::Integer, range::from_object_bytes(&u64_type));

		const u64 index = *static_cast<const u64*>(evaluate_expr(interp, rhs, u64_type_id));

		pop_temporary(interp);

		if (index >= index_limit)
			source_error(interp->errors, node->source_id, "Index %" PRIu64 " exceeds maximum of %" PRIu64 ".\n", index, index_limit);

		u64 byte_offset;

		u64 address;

		if (!mul_checked(stride, index, &byte_offset) || add_checked(reinterpret_cast<u64>(base_address), byte_offset, &address))
			source_error(interp->errors, node->source_id, "Address calculation overflowed.\n");

		return reinterpret_cast<void*>(address);
	}

	case AstTag::Builtin:
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
	case AstTag::Func:
	case AstTag::Trait:
	case AstTag::Impl:
	case AstTag::Catch:
	case AstTag::LitInteger:
	case AstTag::LitFloat:
	case AstTag::LitChar:
	case AstTag::LitString:
	case AstTag::Return:
	case AstTag::Leave:
	case AstTag::Yield:
	case AstTag::Call:
	case AstTag::UOpTypeTailArray:
	case AstTag::UOpTypeSlice:
	case AstTag::UOpTypeMultiPtr:
	case AstTag::UOpTypeOptMultiPtr:
	case AstTag::UOpEval:
	case AstTag::UOpTry:
	case AstTag::UOpDefer:
	case AstTag::UOpDistinct:
	case AstTag::UOpAddr:
	case AstTag::UOpDeref:
	case AstTag::UOpBitNot:
	case AstTag::UOpLogNot:
	case AstTag::UOpTypeOptPtr:
	case AstTag::UOpTypeVar:
	case AstTag::UOpImpliedMember:
	case AstTag::UOpTypePtr:
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
		panic("Addressation of AST node type %s is not yet implemented.\n", tag_name(node->tag));

	case AstTag::INVALID:
	case AstTag::MAX:
	case AstTag::File:
	case AstTag::Case:
	case AstTag::ParameterList:
		; // fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

static void force_member_value(Interpreter* interp, MemberInfo* member) noexcept
{
	if (!member->has_pending_value)
		return;

	set_typechecker_context(interp, member->completion_context_type_id);

	force_member_type(interp, member);

	const TypeMetrics member_metrics = type_metrics_from_id(interp->types, member->type.complete);

	void* const src = evaluate_expr(interp, ast_node_from_id(interp->asts, member->value.pending), member->type.complete);

	const GlobalValueId value_id = make_global_value(interp->globals, with_assignability(member->type.complete, member->is_global? member->is_mut : false), member_metrics.size, member_metrics.align, src);

	set_incomplete_type_member_value_by_rank(interp->types, member->surrounding_type_id, member->rank, value_id);

	pop_temporary(interp);

	unset_typechecker_context(interp);

	member->has_pending_value = false;
	member->value.complete = value_id;
}

static void force_member_type(Interpreter* interp, MemberInfo* member) noexcept
{
	if (!member->has_pending_type)
		return;

	ASSERT_OR_IGNORE(member->has_pending_value);

	set_typechecker_context(interp, member->completion_context_type_id);

	TypeId defined_type_id;

	if (member->type.pending != AstNodeId::INVALID)
	{
		AstNode* const type = ast_node_from_id(interp->asts, member->type.pending);

		const TypeIdWithAssignability type_type_id = typecheck_expr(interp, type);

		const TypeTag type_type_tag = type_tag_from_id(interp->types, type_id(type_type_id));

		if (type_type_tag != TypeTag::Type)
			source_error(interp->errors, type->source_id, "Explicit type annotation of definition must be of type `Type`.\n");

		defined_type_id = *static_cast<TypeId*>(evaluate_expr(interp, type, type_id(type_type_id)));

		pop_temporary(interp);

		set_incomplete_type_member_type_by_rank(interp->types, member->surrounding_type_id, member->rank, defined_type_id);

		if (member->value.pending != AstNodeId::INVALID)
		{
			AstNode* const value = ast_node_from_id(interp->asts, member->value.pending);

			const TypeIdWithAssignability value_type_id = typecheck_expr(interp, value);

			if (!type_can_implicitly_convert_from_to(interp->types, type_id(value_type_id), defined_type_id))
				source_error(interp->errors, value->source_id, "Definition value cannot be implicitly converted to type of explicit type annotation.\n");
		}
	}
	else
	{
		ASSERT_OR_IGNORE(member->value.pending != AstNodeId::INVALID);

		AstNode* const value = ast_node_from_id(interp->asts, member->value.pending);

		defined_type_id = type_id(typecheck_expr(interp, value));

		set_incomplete_type_member_type_by_rank(interp->types, member->surrounding_type_id, member->rank, defined_type_id);
	}

	unset_typechecker_context(interp);

	member->has_pending_type = false;
	member->type.complete = defined_type_id;
}

static void typecheck_where(Interpreter* interp, AstNode* node) noexcept
{
	TODO("Implement");

	(void) interp;

	(void) node;
}

static TypeIdWithAssignability typecheck_expr_impl(Interpreter* interp, AstNode* node) noexcept
{
	switch (node->tag)
	{
	case AstTag::CompositeInitializer:
	case AstTag::ArrayInitializer:
	case AstTag::Wildcard:
	case AstTag::Where:
	case AstTag::Expects:
	case AstTag::Ensures:
	case AstTag::Definition:
	case AstTag::ForEach:
	case AstTag::Switch:
	case AstTag::Trait:
	case AstTag::Impl:
	case AstTag::Catch:
	case AstTag::Return:
	case AstTag::Leave:
	case AstTag::Yield:
	case AstTag::UOpTry:
	case AstTag::UOpDefer:
	case AstTag::UOpImpliedMember:
		source_error(interp->errors, node->source_id, "Typechecking of AST node type %s is not yet implemented.\n", tag_name(node->tag));

	case AstTag::Builtin:
	{
		const Builtin builtin = static_cast<Builtin>(node->flags);

		if (builtin == Builtin::Offsetof)
			panic("Typechecking for builtin %s not yet supported.\n", tag_name(builtin));

		const u8 ordinal = static_cast<u8>(builtin);

		ASSERT_OR_IGNORE(ordinal < array_count(interp->builtin_type_ids) && interp->builtin_type_ids[ordinal] != TypeId::INVALID);

		return with_assignability(interp->builtin_type_ids[ordinal], false);
	}

	case AstTag::Block:
	{
		ASSERT_OR_IGNORE(attachment_of<AstBlockData>(node)->scope_type_id == TypeId::INVALID);

		const TypeId scope_type_id = create_open_type(interp->types, curr_typechecker_context(interp), node->source_id);

		attachment_of<AstBlockData>(node)->scope_type_id = scope_type_id;

		push_typechecker_context(interp, scope_type_id);

		AstDirectChildIterator it = direct_children_of(node);

		u64 offset = 0;

		u32 max_align = 1;

		TypeIdWithAssignability result_type_id = with_assignability(TypeId::INVALID, false);

		for (OptPtr<AstNode> rst = next(&it); is_some(rst); rst = next(&it))
		{
			AstNode* const child = get_ptr(rst);

			if (child->tag == AstTag::Definition)
			{
				DefinitionInfo info = get_definition_info(child);

				TypeId defined_type_id;

				if (is_some(info.type))
				{
					AstNode* const type = get_ptr(info.type);

					const TypeIdWithAssignability type_type_id = typecheck_expr(interp, type);

					const TypeTag type_type_tag = type_tag_from_id(interp->types, type_id(type_type_id));

					if (type_type_tag != TypeTag::Type)
						source_error(interp->errors, type->source_id, "Explicit type annotation of definition must be of type `Type`.\n");

					defined_type_id = *static_cast<TypeId*>(evaluate_expr(interp, type, type_id(type_type_id)));

					pop_temporary(interp);
				}
				else
				{
					ASSERT_OR_IGNORE(is_some(info.value));

					AstNode* const value = get_ptr(info.value);

					defined_type_id = type_id(typecheck_expr(interp, value));
				}

				child->type_id = with_assignability(defined_type_id, has_flag(child, AstFlag::Definition_IsMut));

				const TypeMetrics metrics = type_metrics_from_id(interp->types, defined_type_id);

				offset = next_multiple(offset, static_cast<u64>(metrics.align));

				MemberInit member = member_init_from_definition(interp, scope_type_id, child, info, offset);

				offset += metrics.size;

				if (metrics.align > max_align)
					max_align = metrics.align;

				add_open_type_member(interp->types, scope_type_id, member);

				if (is_some(info.type) && is_some(info.value))
				{
					AstNode* const value = get_ptr(info.value);

					const TypeId value_type_id = type_id(typecheck_expr(interp, value));

					if (!type_can_implicitly_convert_from_to(interp->types, value_type_id, defined_type_id))
						source_error(interp->errors, value->source_id, "Definition value cannot be implicitly converted to type of explicit type annotation.\n");
				}

				if (!has_next_sibling(child))
					result_type_id = with_assignability(simple_type(interp->types, TypeTag::Definition, {}), false);
			}
			else
			{
				const TypeIdWithAssignability expr_type_id = typecheck_expr(interp, child);

				if (!has_next_sibling(node))
				{
					result_type_id = expr_type_id;
				}
				else
				{
					const TypeTag expr_type_tag = type_tag_from_id(interp->types, type_id(expr_type_id));

					if (expr_type_tag != TypeTag::Void && expr_type_tag != TypeTag::Definition)
						source_error(interp->errors, child->source_id, "Expression in non-terminal position in block must be a definition or of void type.\n");
				}
			}
		}

		pop_typechecker_context(interp);

		close_open_type(interp->types, scope_type_id, offset, max_align, next_multiple(offset, static_cast<u64>(max_align)));

		// Empty blocks are of type `Void`.
		if (type_id(result_type_id) == TypeId::INVALID)
			result_type_id = with_assignability(simple_type(interp->types, TypeTag::Void, {}), false);

		return result_type_id;
	}

	case AstTag::If:
	{
		IfInfo info = get_if_info(node);

		const TypeId condition_type_id = type_id(typecheck_expr(interp, info.condition));

		const TypeTag condition_type_tag = type_tag_from_id(interp->types, condition_type_id);

		if (condition_type_tag != TypeTag::Boolean)
			source_error(interp->errors, info.condition->source_id, "Condition of `if` expression must be of boolean type.\n");

		if (is_some(info.where))
			typecheck_where(interp, get_ptr(info.where));

		const TypeIdWithAssignability consequent_type_id = typecheck_expr(interp, info.consequent);

		if (is_some(info.alternative))
		{
			AstNode* const alternative = get_ptr(info.alternative);

			const TypeIdWithAssignability alternative_type_id = typecheck_expr(interp, alternative);

			const TypeId common_type_id = common_type(interp->types, type_id(consequent_type_id), type_id(alternative_type_id));

			if (common_type_id == TypeId::INVALID)
				source_error(interp->errors, node->source_id, "Consequent and alternative of `if` have incompatible types.\n");

			return with_assignability(common_type_id, is_assignable(consequent_type_id) && is_assignable(alternative_type_id));
		}
		else
		{
			const TypeTag consequent_type_tag = type_tag_from_id(interp->types, type_id(consequent_type_id));

			if (consequent_type_tag != TypeTag::Void)
				source_error(interp->errors, node->source_id, "Consequent of `if` must be of void type if no alternative is provided.\n");

			return consequent_type_id;
		}
	}

	case AstTag::For:
	{
		ForInfo info = get_for_info(node);

		const TypeId condition_type_id = type_id(typecheck_expr(interp, info.condition));

		const TypeTag condition_type_tag = type_tag_from_id(interp->types, condition_type_id);

		if (condition_type_tag != TypeTag::Boolean)
			source_error(interp->errors, info.condition->source_id, "Condition of `for` must be of boolean type.\n");

		if (is_some(info.step))
		{
			AstNode* const step = get_ptr(info.step);

			const TypeId step_type_id = type_id(typecheck_expr(interp, step));

			const TypeTag step_type_tag = type_tag_from_id(interp->types, step_type_id);

			if (step_type_tag != TypeTag::Void)
				source_error(interp->errors, step->source_id, "Step of `for` must be of void type.\n");
		}

		if (is_some(info.where))
		typecheck_where(interp, get_ptr(info.where));

		const TypeIdWithAssignability body_type_id = typecheck_expr(interp, info.body);

		if (is_some(info.finally))
		{
			AstNode* const finally = get_ptr(info.finally);

			const TypeIdWithAssignability finally_type_id = typecheck_expr(interp, finally);

			const TypeId common_type_id = common_type(interp->types, type_id(body_type_id), type_id(finally_type_id));

			if (common_type_id == TypeId::INVALID)
				source_error(interp->errors, node->source_id, "Body and finally of `for` have incompatible types.\n");

			return with_assignability(common_type_id, is_assignable(body_type_id) && is_assignable(finally_type_id));
		}
		else
		{
			const TypeTag body_type_tag = type_tag_from_id(interp->types, type_id(body_type_id));

			if (body_type_tag != TypeTag::Void)
				source_error(interp->errors, node->source_id, "Consequent of `if` must be of void type if no alternative is provided.\n");

			return body_type_id;
		}
	}

	case AstTag::Func:
	{
		FuncInfo info = get_func_info(node);

		// TODO: This is a throwaway type only used as a scope for type
		//       checking, which is a bit silly. Maybe a version of
		//       `create_open_type` that deletes the type as soon as it is
		//       instructed to do so through an additional function could be
		//       nice. In this case the type should also never be put into
		//       `structural_types`, instead staying in `builders`.
		const TypeId pseudo_signature_type_id = create_open_type(interp->types, curr_typechecker_context(interp), node->source_id);

		AstDirectChildIterator parameters = direct_children_of(info.parameters);

		u16 param_count = 0;

		for (OptPtr<AstNode> rst = next(&parameters); is_some(rst); rst = next(&parameters))
		{
			AstNode* const parameter = get_ptr(rst);

			if (param_count == 64)
				source_error(interp->errors, parameter->source_id, "Exceeded maximum of 64 function parameters.\n");

			param_count += 1;

			ASSERT_OR_IGNORE(parameter->tag == AstTag::Definition);

			MemberInit init = member_init_from_definition(interp, pseudo_signature_type_id, parameter, get_definition_info(parameter), 0);

			add_open_type_member(interp->types, pseudo_signature_type_id, init);
		}

		close_open_type(interp->types, pseudo_signature_type_id, 0, 1, 0);

		IncompleteMemberIterator incomplete_members = incomplete_members_of(interp->types, pseudo_signature_type_id);

		push_typechecker_context(interp, pseudo_signature_type_id);

		while (has_next(&incomplete_members))
		{
			MemberInfo member = next(&incomplete_members);

			force_member_type(interp, &member);
		}

		pop_typechecker_context(interp);

		const TypeId signature_type_id = create_open_type(interp->types, curr_typechecker_context(interp), node->source_id);

		MemberIterator members = members_of(interp->types, pseudo_signature_type_id);

		u64 offset = 0;

		u32 max_align = 1;

		while (has_next(&members))
		{
			MemberInfo member = next(&members);

			ASSERT_OR_IGNORE(!member.has_pending_type);

			const TypeMetrics member_metrics = type_metrics_from_id(interp->types, member.type.complete);

			offset = next_multiple(offset, static_cast<u64>(member_metrics.align));

			MemberInit init;
			init.name = member.name;
			init.source = member.source;
			init.type = member.type;
			init.value = member.value;
			init.lexical_parent_type_id = signature_type_id;
			init.is_global = member.is_global;
			init.is_pub = member.is_pub;
			init.is_use = member.is_use;
			init.is_mut = member.is_mut;
			init.has_pending_type = member.has_pending_type;
			init.has_pending_value = member.has_pending_value;
			init.offset = offset;

			offset += member_metrics.size;

			if (max_align < member_metrics.align)
				max_align = member_metrics.align;

			add_open_type_member(interp->types, signature_type_id, init);
		}

		close_open_type(interp->types, signature_type_id, offset, max_align, next_multiple(offset, static_cast<u64>(max_align)));

		if (is_none(info.return_type))
			source_error(interp->errors, node->source_id, "Function definitions without an explicit return type are not yet supported.\n");

		AstNode* const return_type = get_ptr(info.return_type);

		const TypeId return_type_id = type_id(typecheck_expr(interp, return_type));

		FuncType func_type{};
		func_type.return_type_id = return_type_id;
		func_type.param_count = param_count;
		func_type.is_proc = has_flag(node, AstFlag::Func_IsProc);
		func_type.signature_type_id = signature_type_id;

		return with_assignability(simple_type(interp->types, TypeTag::Func, range::from_object_bytes(&func_type)), false);
	}

	case AstTag::Identifer:
	{
		const IdentifierId identifier_id = attachment_of<AstIdentifierData>(node)->identifier_id;

		MemberInfo member = lookup_identifier_definition(interp, identifier_id, node->source_id);

		force_member_type(interp, &member);

		return with_assignability(member.type.complete, member.is_mut);
	}

	case AstTag::LitInteger:
	{
		return with_assignability(simple_type(interp->types, TypeTag::CompInteger, {}), false);
	}

	case AstTag::LitFloat:
	{
		return with_assignability(simple_type(interp->types, TypeTag::CompFloat, {}), false);
	}

	case AstTag::LitChar:
	{
		return with_assignability(simple_type(interp->types, TypeTag::CompInteger, {}), false);
	}

	case AstTag::LitString:
	{
		const GlobalValueId string_value_id = attachment_of<AstLitStringData>(node)->string_value_id;

		const GlobalValue string_value = global_value_from_id(interp->globals, string_value_id);

		ASSERT_OR_IGNORE(type_tag_from_id(interp->types, type_id(string_value.type)) == TypeTag::Array);

		return string_value.type;
	}

	case AstTag::Call:
	{
		// TODO: Variadics

		AstNode* const callee = first_child_of(node);

		const TypeId callee_type_id = type_id(typecheck_expr(interp, callee));

		const TypeTag callee_type_tag = type_tag_from_id(interp->types, callee_type_id);

		if (callee_type_tag != TypeTag::Func && callee_type_tag != TypeTag::Builtin)
			source_error(interp->errors, callee->source_id, "Left-hand-side of call operator must be of function or builtin type.\n");

		const FuncType* const func_type = static_cast<const FuncType*>(simple_type_structure_from_id(interp->types, callee_type_id));

		const TypeId signature_type_id = func_type->signature_type_id;

		bool expect_named = false;

		u64 seen_argument_mask = 0;

		u16 seen_argument_count = 0;

		AstNode* argument = callee;

		while (has_next_sibling(argument))
		{
			argument = next_sibling_of(argument);

			MemberInfo argument_member;

			TypeIdWithAssignability argument_type_id;

			if (argument->tag == AstTag::OpSet)
			{
				if (!expect_named)
				{
					seen_argument_mask = (static_cast<u64>(1) << seen_argument_count) - 1;

					expect_named = true;
				}

				AstNode* const argument_lhs = first_child_of(argument);

				AstNode* const argument_rhs = next_sibling_of(argument_lhs);

				// TODO: Enforce this in parser
				ASSERT_OR_IGNORE(argument_lhs->tag == AstTag::UOpImpliedMember);

				AstNode* const argument_name = first_child_of(argument_lhs);

				// TODO: Enforce this in parser
				ASSERT_OR_IGNORE(argument_name->tag == AstTag::Identifer);

				const IdentifierId argument_identifier = attachment_of<AstIdentifierData>(argument_name)->identifier_id;

				if (!type_member_info_by_name(interp->types, callee_type_id, argument_identifier, &argument_member))
				{
					const Range<char8> name = identifier_name_from_id(interp->identifiers, argument_identifier);

					source_error(interp->errors, argument_lhs->source_id, "`%.*s` is not an argument of the called function.\n", static_cast<s32>(name.count()), name.begin());
				}

				ASSERT_OR_IGNORE(argument_member.rank < 64);

				const u64 curr_argument_bit = static_cast<u64>(1) << argument_member.rank;

				if ((seen_argument_mask & curr_argument_bit) != 0)
				{
					const Range<char8> name = identifier_name_from_id(interp->identifiers, argument_identifier);

					source_error(interp->errors, argument_lhs->source_id, "Function argument `%.*s` set more than once.\n", static_cast<s32>(name.count()), name.begin());
				}

				seen_argument_mask |= curr_argument_bit;

				argument_type_id = typecheck_expr(interp, argument_rhs);
			}
			else
			{
				ASSERT_OR_IGNORE(seen_argument_count < 64);

				if (expect_named)
					source_error(interp->errors, argument->source_id, "Positional arguments must not follow named arguments.\n");

				if (seen_argument_count >= func_type->param_count)
					source_error(interp->errors, argument->source_id, "Call supplies more than the expeceted %d arguments.\n", func_type->param_count);

				if (!type_member_info_by_rank(interp->types, signature_type_id, seen_argument_count, &argument_member))
					source_error(interp->errors, argument->source_id, "Too many arguments in function call.\n");

				argument_type_id = typecheck_expr(interp, argument);

				seen_argument_count += 1;
			}

			ASSERT_OR_IGNORE(!argument_member.has_pending_type);

			if (!type_can_implicitly_convert_from_to(interp->types, type_id(argument_type_id), argument_member.type.complete))
				source_error(interp->errors, argument->source_id, "Cannot implicitly convert to expected argument type.\n");
		}

		if (!expect_named)
			seen_argument_mask = (static_cast<u64>(1) << seen_argument_count) - 1;

		for (u16 i = 0; i != func_type->param_count; ++i)
		{
			const u64 curr_argument_mask = static_cast<u64>(1) << i;

			if ((seen_argument_mask & curr_argument_mask) == 0)
			{
				MemberInfo member;

				if (!type_member_info_by_rank(interp->types, signature_type_id, i, &member))
					ASSERT_UNREACHABLE;

				if (member.value.pending == AstNodeId::INVALID)
				{
					const Range<char8> name = identifier_name_from_id(interp->identifiers, member.name);

					source_error(interp->errors, node->source_id, "Missing value for argument %.*s in call.\n", static_cast<s32>(name.count()), name.begin());
				}
			}
		}

		return with_assignability(func_type->return_type_id, true);
	}

	case AstTag::UOpTypeTailArray:
	{
		AstNode* const operand = first_child_of(node);

		const TypeId operand_type_id = type_id(typecheck_expr(interp, operand));

		const TypeTag operand_type_tag = type_tag_from_id(interp->types, operand_type_id);

		if (operand_type_tag != TypeTag::Type)
			source_error(interp->errors, operand->source_id, "Operand of `%s` must be of type `Type`.\n", tag_name(node->tag));

		return with_assignability(simple_type(interp->types, TypeTag::Type, {}), false);
	}

	case AstTag::UOpTypeSlice:
	{
		AstNode* const operand = first_child_of(node);

		const TypeId operand_type_id = type_id(typecheck_expr(interp, operand));

		const TypeTag operand_type_tag = type_tag_from_id(interp->types, operand_type_id);

		if (operand_type_tag != TypeTag::Type)
			source_error(interp->errors, operand->source_id, "Operand of `%s` must be of type `Type`.\n", tag_name(node->tag));

		return with_assignability(simple_type(interp->types, TypeTag::Type, {}), false);
	}

	case AstTag::UOpEval:
	{
		AstNode* const operand = first_child_of(node);

		return typecheck_expr(interp, operand);
	}

	case AstTag::UOpDistinct:
	{
		AstNode* const operand = first_child_of(node);

		const TypeId operand_type_id = type_id(typecheck_expr(interp, operand));

		const TypeTag operand_type_tag = type_tag_from_id(interp->types, operand_type_id);

		if (operand_type_tag != TypeTag::Type)
			source_error(interp->errors, operand->source_id, "Operand of `%s` must be of type `Type`.\n", tag_name(node->tag));

		return with_assignability(alias_type(interp->types, operand_type_id, true, operand->source_id, IdentifierId::INVALID), false);
	}

	case AstTag::UOpAddr:
	{
		AstNode* const operand = first_child_of(node);

		const TypeIdWithAssignability operand_type_id = typecheck_expr(interp, operand);

		ReferenceType ptr_type{};
		ptr_type.is_multi = false;
		ptr_type.is_opt = false;
		ptr_type.is_mut = is_assignable(operand_type_id);
		ptr_type.referenced_type_id = type_id(operand_type_id);

		return with_assignability(simple_type(interp->types, TypeTag::Ptr, range::from_object_bytes(&ptr_type)), false);
	}

	case AstTag::UOpDeref:
	{
		AstNode* const operand = first_child_of(node);

		const TypeId operand_type_id = type_id(typecheck_expr(interp, operand));

		const TypeTag operand_type_tag = type_tag_from_id(interp->types, operand_type_id);

		if (operand_type_tag != TypeTag::Ptr)
			source_error(interp->errors, operand->source_id, "Operand of `%s` must be of pointer type.\n", tag_name(node->tag));

		const ReferenceType* const reference = static_cast<const ReferenceType*>(simple_type_structure_from_id(interp->types, operand_type_id));

		return with_assignability(reference->referenced_type_id, reference->is_mut);
	}

	case AstTag::UOpBitNot:
	{
		AstNode* const operand = first_child_of(node);

		const TypeId operand_type_id = type_id(typecheck_expr(interp, operand));

		const TypeTag operand_type_tag = type_tag_from_id(interp->types, operand_type_id);

		if (operand_type_tag != TypeTag::Integer && operand_type_tag != TypeTag::CompInteger)
			source_error(interp->errors, operand->source_id, "Operand of `%s` must be of integral type.\n", tag_name(node->tag));

		return with_assignability(operand_type_id, false);
	}

	case AstTag::UOpLogNot:
	{
		AstNode* const operand = first_child_of(node);

		const TypeId operand_type_id = type_id(typecheck_expr(interp, operand));

		const TypeTag operand_type_tag = type_tag_from_id(interp->types, operand_type_id);

		if (operand_type_tag != TypeTag::Boolean)
			source_error(interp->errors, operand->source_id, "Operand of `%s` must be of boolean type.\n", tag_name(node->tag));

		return with_assignability(operand_type_id, false);
	}

	case AstTag::UOpTypeVar:
	{
		AstNode* const operand = first_child_of(node);

		const TypeId operand_type_id = type_id(typecheck_expr(interp, operand));

		const TypeTag operand_type_tag = type_tag_from_id(interp->types, operand_type_id);

		if (operand_type_tag != TypeTag::Type)
			source_error(interp->errors, operand->source_id, "Operand of `%s` must be of type `Type`.\n", tag_name(node->tag));

		return with_assignability(simple_type(interp->types, TypeTag::Type, {}), false);
	}

	case AstTag::UOpTypePtr:
	case AstTag::UOpTypeOptPtr:
	case AstTag::UOpTypeMultiPtr:
	case AstTag::UOpTypeOptMultiPtr:
	{
		AstNode* const operand = first_child_of(node);

		const TypeId operand_type_id = type_id(typecheck_expr(interp, operand));

		const TypeTag operand_type_tag = type_tag_from_id(interp->types, operand_type_id);

		if (operand_type_tag != TypeTag::Type)
			source_error(interp->errors, operand->source_id, "Operand of `%s` must be of type `Type`.\n", tag_name(node->tag));

		ReferenceType ptr_type{};
		ptr_type.is_multi = node->tag == AstTag::UOpTypeMultiPtr || node->tag == AstTag::UOpTypeOptMultiPtr;
		ptr_type.is_opt = node->tag == AstTag::UOpTypeOptPtr || node->tag == AstTag::UOpTypeOptMultiPtr;
		ptr_type.is_mut = has_flag(node, AstFlag::Type_IsMut);
		ptr_type.referenced_type_id = operand_type_id;

		return with_assignability(simple_type(interp->types, TypeTag::Ptr, range::from_object_bytes(&ptr_type)), false);
	}

	case AstTag::UOpNegate:
	case AstTag::UOpPos:
	{
		AstNode* const operand = first_child_of(node);

		const TypeId operand_type_id = type_id(typecheck_expr(interp, node));

		const TypeTag operand_type_tag = type_tag_from_id(interp->types, operand_type_id);

		if (operand_type_tag != TypeTag::Integer && operand_type_tag != TypeTag::CompInteger && operand_type_tag != TypeTag::Float && operand_type_tag != TypeTag::CompFloat)
			source_error(interp->errors, operand->source_id, "Operand of unary `%s` must be of integral or floating point type.\n");

		if (node->tag == AstTag::UOpNegate && (operand_type_tag == TypeTag::Integer || operand_type_tag == TypeTag::CompInteger))
		{
			const NumericType* const integer_type = static_cast<const NumericType*>(simple_type_structure_from_id(interp->types, operand_type_id));

			if (!integer_type->is_signed)
				source_error(interp->errors, operand->source_id, "Operand of unary `%s` must be signed.\n");
		}

		return with_assignability(operand_type_id, false);
	}

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
	{
		AstNode* const lhs = first_child_of(node);

		const TypeId lhs_type_id = type_id(typecheck_expr(interp, lhs));

		const TypeTag lhs_type_tag = type_tag_from_id(interp->types, lhs_type_id);

		if (node->tag != AstTag::OpSet && lhs_type_tag != TypeTag::Integer && lhs_type_tag != TypeTag::CompInteger)
		{
			if (node->tag != AstTag::OpSetAdd && node->tag != AstTag::OpSetSub && node->tag != AstTag::OpSetMul && node->tag != AstTag::OpSetDiv)
				source_error(interp->errors, lhs->source_id, "Left-hand-side of `%s` must be of integral type.\n", tag_name(node->tag));

			if (lhs_type_tag != TypeTag::Float && lhs_type_tag != TypeTag::CompFloat)
				source_error(interp->errors, lhs->source_id, "Left-hand-side of `%s` must be of integral or floating point type.\n", tag_name(node->tag));
		}

		AstNode* const rhs = next_sibling_of(lhs);

		const TypeId rhs_type_id = type_id(typecheck_expr(interp, rhs));

		const TypeTag rhs_type_tag = type_tag_from_id(interp->types, rhs_type_id);

		if (node->tag != AstTag::OpSet && rhs_type_tag != TypeTag::Integer && rhs_type_tag != TypeTag::CompInteger)
		{
			if (node->tag != AstTag::OpSetAdd && node->tag != AstTag::OpSetSub && node->tag != AstTag::OpSetMul && node->tag != AstTag::OpSetDiv)
				source_error(interp->errors, rhs->source_id, "Right-hand-side of `%s` must be of integral type.\n", tag_name(node->tag));

			if (rhs_type_tag != TypeTag::Float && rhs_type_tag != TypeTag::CompFloat)
				source_error(interp->errors, rhs->source_id, "Right-hand-side of `%s` must be of integral or floating point type.\n", tag_name(node->tag));
		}

		const TypeId common_type_id = common_type(interp->types, lhs_type_id, rhs_type_id);

		if (common_type_id == TypeId::INVALID)
			source_error(interp->errors, node->source_id, "Incompatible left-hand and right-hand side operands for `%s`.\n", tag_name(node->tag));

		return with_assignability(common_type_id, false);
	}

	case AstTag::OpShiftL:
	case AstTag::OpShiftR:
	{
		AstNode* const lhs = first_child_of(node);

		const TypeId lhs_type_id = type_id(typecheck_expr(interp, lhs));

		const TypeTag lhs_type_tag = type_tag_from_id(interp->types, lhs_type_id);

		if (lhs_type_tag != TypeTag::Integer && lhs_type_tag != TypeTag::CompInteger)
			source_error(interp->errors, lhs->source_id, "Left-hand-side of `%s` must be of integral type.\n", tag_name(node->tag));

		AstNode* const rhs = next_sibling_of(lhs);

		const TypeId rhs_type_id = type_id(typecheck_expr(interp, rhs));

		const TypeTag rhs_type_tag = type_tag_from_id(interp->types, rhs_type_id);

		if (rhs_type_tag != TypeTag::Integer && rhs_type_tag != TypeTag::CompInteger)
			source_error(interp->errors, lhs->source_id, "Right-hand-side of `%s` must be of integral type.\n", tag_name(node->tag));

		return with_assignability(lhs_type_id, false);
	}

	case AstTag::OpLogAnd:
	case AstTag::OpLogOr:
	{
		AstNode* const lhs = first_child_of(node);

		const TypeId lhs_type_id = type_id(typecheck_expr(interp, lhs));

		const TypeTag lhs_type_tag = type_tag_from_id(interp->types, lhs_type_id);

		if (lhs_type_tag != TypeTag::Boolean)
			source_error(interp->errors, lhs->source_id, "Left-hand-side of `%s` must be of boolean type.\n", tag_name(node->tag));

		AstNode* const rhs = next_sibling_of(lhs);

		const TypeId rhs_type_id = type_id(typecheck_expr(interp, rhs));

		const TypeTag rhs_type_tag = type_tag_from_id(interp->types, rhs_type_id);

		if (rhs_type_tag != TypeTag::Boolean)
			source_error(interp->errors, lhs->source_id, "Right-hand-side of `%s` must be of boolean type.\n", tag_name(node->tag));

		const TypeId common_type_id = common_type(interp->types, lhs_type_id, rhs_type_id);

		if (common_type_id == TypeId::INVALID)
			source_error(interp->errors, node->source_id, "Operands of `%s` are incompatible.\n", tag_name(node->tag));

		return with_assignability(common_type_id, false);
	}

	case AstTag::OpMember:
	{
		AstNode* const lhs = first_child_of(node);

		const TypeIdWithAssignability lhs_type_id = typecheck_expr(interp, lhs);

		const TypeTag lhs_type_tag = type_tag_from_id(interp->types, type_id(lhs_type_id));

		if (lhs_type_tag != TypeTag::Composite && lhs_type_tag != TypeTag::Type)
			source_error(interp->errors, lhs->source_id, "Left-hand-side of `.` must be of type `Type` or a composite type.\n");

		AstNode* const rhs = next_sibling_of(lhs);

		if (rhs->tag != AstTag::Identifer)
			source_error(interp->errors, rhs->source_id, "Right-hand-side of `.` must be an identifier\n");

		rhs->type_id = with_assignability(TypeId::NO_TYPE, false);

		const IdentifierId identifier_id = attachment_of<AstIdentifierData>(rhs)->identifier_id;

		if (lhs_type_tag == TypeTag::Composite)
		{
			MemberInfo member;

			if (!type_member_info_by_name(interp->types, type_id(lhs_type_id), identifier_id, &member))
			{
				const Range<char8> name = identifier_name_from_id(interp->identifiers, identifier_id);

				source_error(interp->errors, node->source_id, "Left-hand-side of `.` has no member \"%.*s\"", static_cast<s32>(name.count()), name.begin());
			}

			force_member_type(interp, &member);

			return with_assignability(member.type.complete, member.is_mut && is_assignable(lhs_type_id));
		}
		else
		{
			ASSERT_OR_IGNORE(lhs_type_tag == TypeTag::Type);

			const TypeId defined_type_id = *static_cast<TypeId*>(evaluate_expr(interp, lhs, type_id(lhs_type_id)));

			pop_temporary(interp);

			MemberInfo member;

			if (!type_member_info_by_name(interp->types, defined_type_id, identifier_id, &member))
			{
				const Range<char8> name = identifier_name_from_id(interp->identifiers, identifier_id);

				source_error(interp->errors, node->source_id, "Left-hand-side of `.` has no member \"%.*s\"", static_cast<s32>(name.count()), name.begin());
			}

			if (!member.is_global)
			{
				const Range<char8> name = identifier_name_from_id(interp->identifiers, identifier_id);

				source_error(interp->errors, node->source_id, "Non-global member %.*s cannot be referenced by its parent type.\n", static_cast<s32>(name.count()), name.begin());
			}

			force_member_type(interp, &member);

			return with_assignability(member.type.complete, member.is_mut && is_assignable(lhs_type_id));
		}
	}

	case AstTag::OpCmpLT:
	case AstTag::OpCmpGT:
	case AstTag::OpCmpLE:
	case AstTag::OpCmpGE:
	case AstTag::OpCmpNE:
	case AstTag::OpCmpEQ:
	{
		AstNode* const lhs = first_child_of(node);

		const TypeId lhs_type_id = type_id(typecheck_expr(interp, lhs));

		const TypeTag lhs_type_tag = type_tag_from_id(interp->types, lhs_type_id);

		if (lhs_type_tag == TypeTag::Array || lhs_type_tag == TypeTag::ArrayLiteral || lhs_type_tag == TypeTag::Composite || lhs_type_tag == TypeTag::CompositeLiteral)
			source_error(interp->errors, lhs->source_id, "Left-hand-side of %s must not be of composite or array type.\n");

		AstNode* const rhs = next_sibling_of(lhs);

		const TypeId rhs_type_id = type_id(typecheck_expr(interp, rhs));

		const TypeTag rhs_type_tag = type_tag_from_id(interp->types, rhs_type_id);

		if (rhs_type_tag == TypeTag::Array || rhs_type_tag == TypeTag::ArrayLiteral || rhs_type_tag == TypeTag::Composite || rhs_type_tag == TypeTag::CompositeLiteral)
			source_error(interp->errors, rhs->source_id, "Right-hand-side of %s must not be of composite or array type.\n");

		const TypeId common_type_id = common_type(interp->types, lhs_type_id, rhs_type_id);

		if (common_type_id == TypeId::INVALID)
			source_error(interp->errors, node->source_id, "Incompatible left-hand and right-hand side operands for `%s`.\n", tag_name(node->tag));

		return with_assignability(simple_type(interp->types, TypeTag::Boolean, {}), false);
	}

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
	{
		AstNode* const lhs = first_child_of(node);

		const TypeIdWithAssignability lhs_type_id = typecheck_expr(interp, lhs);

		const TypeTag lhs_type_tag = type_tag_from_id(interp->types, type_id(lhs_type_id));

		if (node->tag != AstTag::OpSet && lhs_type_tag != TypeTag::Integer && lhs_type_tag != TypeTag::CompInteger)
		{
			if (node->tag != AstTag::OpSetAdd && node->tag != AstTag::OpSetSub && node->tag != AstTag::OpSetMul && node->tag != AstTag::OpSetDiv)
				source_error(interp->errors, lhs->source_id, "Left-hand-side of `%s` must be of integral type.\n", tag_name(node->tag));

			if (lhs_type_tag != TypeTag::Float && lhs_type_tag != TypeTag::CompFloat)
				source_error(interp->errors, lhs->source_id, "Left-hand-side of `%s` must be of integral or floating point type.\n", tag_name(node->tag));
		}

		if (!is_assignable(lhs_type_id))
			source_error(interp->errors, lhs->source_id, "Left-hand-side of `%s` must be assignable.\n", tag_name(node->tag));

		AstNode* const rhs = next_sibling_of(lhs);

		const TypeId rhs_type_id = type_id(typecheck_expr(interp, rhs));

		const TypeTag rhs_type_tag = type_tag_from_id(interp->types, rhs_type_id);

		if (node->tag != AstTag::OpSet && rhs_type_tag != TypeTag::Integer && rhs_type_tag != TypeTag::CompInteger)
		{
			if (node->tag != AstTag::OpSetAdd && node->tag != AstTag::OpSetSub && node->tag != AstTag::OpSetMul && node->tag != AstTag::OpSetDiv)
				source_error(interp->errors, rhs->source_id, "Right-hand-side of `%s` must be of integral type.\n", tag_name(node->tag));

			if (rhs_type_tag != TypeTag::Float && rhs_type_tag != TypeTag::CompFloat)
				source_error(interp->errors, rhs->source_id, "Right-hand-side of `%s` must be of integral or floating point type.\n", tag_name(node->tag));
		}

		const TypeId common_type_id = common_type(interp->types, type_id(lhs_type_id), rhs_type_id);

		if (common_type_id == TypeId::INVALID)
			source_error(interp->errors, node->source_id, "Incompatible left-hand and right-hand side operands for `%s`.\n", tag_name(node->tag));

		return with_assignability(simple_type(interp->types, TypeTag::Void, {}), false);
	}

	case AstTag::OpSetShiftL:
	case AstTag::OpSetShiftR:
	{
		AstNode* const lhs = first_child_of(node);

		const TypeIdWithAssignability lhs_type_id = typecheck_expr(interp, lhs);

		const TypeTag lhs_type_tag = type_tag_from_id(interp->types, type_id(lhs_type_id));

		if (lhs_type_tag != TypeTag::Integer && lhs_type_tag != TypeTag::CompInteger)
			source_error(interp->errors, lhs->source_id, "Left-hand-side of `%s` must be of integral type.\n", tag_name(node->tag));

		if (!is_assignable(lhs_type_id))
			source_error(interp->errors, lhs->source_id, "Left-hand-side of `%s` must be assignable.\n", tag_name(node->tag));

		AstNode* const rhs = next_sibling_of(lhs);

		const TypeId rhs_type_id = type_id(typecheck_expr(interp, rhs));

		const TypeTag rhs_type_tag = type_tag_from_id(interp->types, rhs_type_id);

		if (rhs_type_tag != TypeTag::Integer && rhs_type_tag != TypeTag::CompInteger)
			source_error(interp->errors, lhs->source_id, "Right-hand-side of `%s` must be of integral type.\n", tag_name(node->tag));

		return with_assignability(simple_type(interp->types, TypeTag::Void, {}), false);
	}

	case AstTag::OpTypeArray:
	{
		AstNode* const count = first_child_of(node);

		const TypeId count_type_id = type_id(typecheck_expr(interp, count));

		const TypeTag count_type_tag = type_tag_from_id(interp->types, count_type_id);

		if (count_type_tag != TypeTag::Integer && count_type_tag != TypeTag::CompInteger)
			source_error(interp->errors, count->source_id, "Expected array count expression to be of integral type.\n");

		AstNode* const type = next_sibling_of(count);

		const TypeId type_type_id = type_id(typecheck_expr(interp, type));

		const TypeTag type_type_tag = type_tag_from_id(interp->types, type_type_id);

		if (type_type_tag != TypeTag::Type)
			source_error(interp->errors, type->source_id, "Expected array type expression of be of type `Type`.\n");

		NumericType u64_type{};
		u64_type.bits = 64;
		u64_type.is_signed = false;

		const TypeId u64_type_id = simple_type(interp->types, TypeTag::Integer, range::from_object_bytes(&u64_type));

		const u64 count_value = *static_cast<u64*>(evaluate_expr(interp, count, u64_type_id));

		pop_temporary(interp);

		ArrayType result_type;
		result_type.element_type = type_type_id;
		result_type.element_count = count_value;

		return with_assignability(simple_type(interp->types, TypeTag::Array, range::from_object_bytes(&result_type)), false);
	}

	case AstTag::OpArrayIndex:
	{
		AstNode* const arrayish = first_child_of(node);

		const TypeId arrayish_type_id = type_id(typecheck_expr(interp, arrayish));

		const TypeTag array_type_tag = type_tag_from_id(interp->types, arrayish_type_id);

		const void* const structure = simple_type_structure_from_id(interp->types, arrayish_type_id);

		TypeId element_type_id;

		bool result_is_assignable;

		if (array_type_tag == TypeTag::Array)
		{
			const ArrayType* const array = static_cast<const ArrayType*>(structure);

			element_type_id = array->element_type;

			result_is_assignable = true;
		}
		else if (array_type_tag == TypeTag::Slice || array_type_tag == TypeTag::Ptr)
		{
			const ReferenceType* const  reference = static_cast<const ReferenceType*>(structure);

			element_type_id = reference->referenced_type_id;

			result_is_assignable = reference->is_mut;
		}
		else
			source_error(interp->errors, arrayish->source_id, "Left-hand-side of array dereference operator must be of array-, slice- or multi-pointer type.\n");

		AstNode* const index = next_sibling_of(arrayish);

		const TypeId index_type_id = type_id(typecheck_expr(interp, index));

		const TypeTag index_type_tag = type_tag_from_id(interp->types, index_type_id);

		if (index_type_tag != TypeTag::Integer && index_type_tag != TypeTag::CompInteger)
			source_error(interp->errors, index->source_id, "Index operand of array dereference operator must be of integral type.\n");

		return with_assignability(element_type_id, result_is_assignable);
	}

	case AstTag::INVALID:
	case AstTag::MAX:
	case AstTag::File:
	case AstTag::Case:
	case AstTag::ParameterList:
		; // fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

static TypeIdWithAssignability typecheck_expr(Interpreter* interp, AstNode* node) noexcept
{
	ASSERT_OR_IGNORE(type_id(node->type_id) != TypeId::NO_TYPE);

	if (type_id(node->type_id) == TypeId::CHECKING)
	{
		source_error(interp->errors, node->source_id, "Cyclic type dependency detected.\n");
	}
	else if (type_id(node->type_id) != TypeId::INVALID)
	{
		return node->type_id;
	}

	node->type_id = with_assignability(TypeId::CHECKING, false);

	const TypeIdWithAssignability result = typecheck_expr_impl(interp, node);

	ASSERT_OR_IGNORE(is_valid(result));

	node->type_id = result;

	return result;
}

static TypeId type_from_file_ast(Interpreter* interp, AstNode* file, SourceId file_type_source_id) noexcept
{
	ASSERT_OR_IGNORE(file->tag == AstTag::File);

	// Note that `interp->prelude_type_id` is `INVALID_TYPE_ID` if we are
	// called from `init_prelude_type`, so the prelude itself has no lexical
	// parent.
	const TypeId file_type_id = create_open_type(interp->types, interp->prelude_type_id, file_type_source_id);

	set_typechecker_context(interp, file_type_id);

	AstDirectChildIterator ast_it = direct_children_of(file);

	for (OptPtr<AstNode> rst = next(&ast_it); is_some(rst); rst = next(&ast_it))
	{
		AstNode* const node = get_ptr(rst);

		if (node->tag != AstTag::Definition)
			source_error(interp->errors, node->source_id, "Currently only definitions are supported on a file's top-level.\n");

		MemberInit member = member_init_from_definition(interp, file_type_id, node, get_definition_info(node), 0);

		if (member.is_global)
			source_warning(interp->errors, node->source_id, "Redundant 'global' modifier. Top-level definitions are implicitly global.\n");
		else
			member.is_global = true;

		add_open_type_member(interp->types, file_type_id, member);
	}

	close_open_type(interp->types, file_type_id, 0, 1, 0);

	IncompleteMemberIterator member_it = incomplete_members_of(interp->types, file_type_id);

	while (has_next(&member_it))
	{
		MemberInfo member = next(&member_it);

		(void) force_member_value(interp, &member);
	}

	unset_typechecker_context(interp);

	return file_type_id;
}



static TypeId make_func_type_from_array(TypePool* types, TypeId return_type_id, u16 param_count, const FuncTypeParamHelper* params) noexcept
{
	const TypeId signature_type_id = create_open_type(types, TypeId::INVALID, SourceId::INVALID);

	u64 offset = 0;

	u32 max_align = 1;

	for (u16 i = 0; i != param_count; ++i)
	{
		const TypeMetrics metrics = type_metrics_from_id(types, params[i].type);

		offset = next_multiple(offset, static_cast<u64>(metrics.align));

		MemberInit init{};
		init.name = params[i].name;
		init.type.complete = params[i].type;
		init.value.complete = GlobalValueId::INVALID;
		init.source = SourceId::INVALID;
		init.is_global = false;
		init.is_pub = false;
		init.is_use = false;
		init.has_pending_type = false;
		init.has_pending_value = false;
		init.offset = offset;


		offset += metrics.size;

		if (metrics.align > max_align)
			max_align = metrics.align;

		add_open_type_member(types, signature_type_id, init);
	}

	close_open_type(types, signature_type_id, offset, max_align, next_multiple(offset, static_cast<u64>(max_align)));

	FuncType func_type{};
	func_type.return_type_id = return_type_id;
	func_type.param_count = param_count;
	func_type.is_proc = false;
	func_type.signature_type_id = signature_type_id;

	return simple_type(types, TypeTag::Func, range::from_object_bytes(&func_type));
}

template<typename... Params>
static TypeId make_func_type(TypePool* types, TypeId return_type_id, Params... params) noexcept
{
	if constexpr (sizeof...(params) == 0)
	{
		return make_func_type_from_array(types, return_type_id, 0, nullptr);
	}
	else
	{
		const FuncTypeParamHelper params_array[] = { params... };

		return make_func_type_from_array(types, return_type_id, sizeof...(params), params_array);
	}
}



static void* builtin_integer(Interpreter* interp, [[maybe_unused]] AstNode* call_node) noexcept
{
	const u8 bits = *static_cast<u8*>(lookup_identifier_value(interp, id_from_identifier(interp->identifiers, range::from_literal_string("bits")), SourceId::INVALID));

	const bool is_signed = *static_cast<bool*>(lookup_identifier_value(interp, id_from_identifier(interp->identifiers, range::from_literal_string("is_signed")), SourceId::INVALID));

	NumericType integer_type{};
	integer_type.bits = bits;
	integer_type.is_signed = is_signed;

	TypeId* const dst = static_cast<TypeId*>(push_temporary(interp, 4, 4));

	const TypeId integer_type_id = simple_type(interp->types, TypeTag::Integer, range::from_object_bytes(&integer_type));

	*dst = integer_type_id;

	return dst;
}

static void* builtin_float(Interpreter* interp, [[maybe_unused]] AstNode* call_node) noexcept
{
	const u8 bits = *static_cast<u8*>(lookup_identifier_value(interp, id_from_identifier(interp->identifiers, range::from_literal_string("bits")), SourceId::INVALID));

	NumericType float_type{};
	float_type.bits = bits;
	float_type.is_signed = true;

	TypeId* const dst = static_cast<TypeId*>(push_temporary(interp, 4, 4));

	const TypeId float_type_id = simple_type(interp->types, TypeTag::Float, range::from_object_bytes(&float_type));

	*dst = float_type_id;

	return dst;
}

static void* builtin_type(Interpreter* interp, [[maybe_unused]] AstNode* call_node) noexcept
{
	TypeId* const dst = static_cast<TypeId*>(push_temporary(interp, 4, 4));

	const TypeId type_type_id = simple_type(interp->types, TypeTag::Type, {});

	*dst = type_type_id;

	return dst;
}

static void* builtin_typeof(Interpreter* interp, [[maybe_unused]] AstNode* call_node) noexcept
{
	const TypeId arg_type_id = *static_cast<TypeId*>(lookup_identifier_value(interp, id_from_identifier(interp->identifiers, range::from_literal_string("arg")), SourceId::INVALID));

	TypeId* const dst = static_cast<TypeId*>(push_temporary(interp, 4, 4));

	*dst = arg_type_id;

	return dst;
}

static void* builtin_returntypeof(Interpreter* interp, [[maybe_unused]] AstNode* call_node) noexcept
{
	const TypeId arg_type_id = *static_cast<TypeId*>(lookup_identifier_value(interp, id_from_identifier(interp->identifiers, range::from_literal_string("arg")), SourceId::INVALID));

	const TypeTag arg_type_tag = type_tag_from_id(interp->types, arg_type_id);

	if (arg_type_tag != TypeTag::Func && arg_type_tag != TypeTag::Builtin)
		panic("Passed non-function, non-builtin argument to `_returntypeof`.\n");

	const FuncType* const func_type = static_cast<const FuncType*>(simple_type_structure_from_id(interp->types, arg_type_id));

	TypeId* const dst = static_cast<TypeId*>(push_temporary(interp, 4, 4));

	*dst = func_type->return_type_id;

	return dst;
}

static void* builtin_sizeof(Interpreter* interp, [[maybe_unused]] AstNode* call_node) noexcept
{
	const TypeId arg_type_id = *static_cast<TypeId*>(lookup_identifier_value(interp, id_from_identifier(interp->identifiers, range::from_literal_string("arg")), SourceId::INVALID));

	const TypeMetrics metrics = type_metrics_from_id(interp->types, arg_type_id);

	CompIntegerValue* const dst = static_cast<CompIntegerValue*>(push_temporary(interp, 8, 8));

	*dst = comp_integer_from_u64(metrics.size);

	return dst;
}

static void* builtin_alignof(Interpreter* interp, [[maybe_unused]] AstNode* call_node) noexcept
{
	const TypeId arg_type_id = *static_cast<TypeId*>(lookup_identifier_value(interp, id_from_identifier(interp->identifiers, range::from_literal_string("arg")), SourceId::INVALID));

	const TypeMetrics metrics = type_metrics_from_id(interp->types, arg_type_id);

	CompIntegerValue* const dst = static_cast<CompIntegerValue*>(push_temporary(interp, 8, 8));

	*dst = comp_integer_from_u64(metrics.align);

	return dst;
}

static void* builtin_strideof(Interpreter* interp, [[maybe_unused]] AstNode* call_node) noexcept
{
	const TypeId arg_type_id = *static_cast<TypeId*>(lookup_identifier_value(interp, id_from_identifier(interp->identifiers, range::from_literal_string("arg")), SourceId::INVALID));

	const TypeMetrics metrics = type_metrics_from_id(interp->types, arg_type_id);

	CompIntegerValue* const dst = static_cast<CompIntegerValue*>(push_temporary(interp, 8, 8));

	*dst = comp_integer_from_u64(metrics.stride);

	return dst;
}

static void* builtin_offsetof(Interpreter* interp, [[maybe_unused]] AstNode* call_node) noexcept
{
	(void) interp;

	TODO("Implement.");
}

static void* builtin_nameof(Interpreter* interp, [[maybe_unused]] AstNode* call_node) noexcept
{
	(void) interp;

	TODO("Implement.");
}

static void* builtin_import(Interpreter* interp, AstNode* call_node) noexcept
{
	const Range<char8> path = *static_cast<Range<char8>*>(lookup_identifier_value(interp, id_from_identifier(interp->identifiers, range::from_literal_string("path")), SourceId::INVALID));

	const bool is_std = *static_cast<bool*>(lookup_identifier_value(interp, id_from_identifier(interp->identifiers, range::from_literal_string("is_std")), SourceId::INVALID));

	const SourceId from = *static_cast<SourceId*>(lookup_identifier_value(interp, id_from_identifier(interp->identifiers, range::from_literal_string("from")), SourceId::INVALID));

	TypeId* const dst = static_cast<TypeId*>(push_temporary(interp, 4, 4));

	char8 absolute_path_buf[8192];

	Range<char8> absolute_path;

	if (from != SourceId::INVALID)
	{
		SourceFile* const source_file = source_file_from_source_id(interp->reader, from);

		const Range<char8> path_base = source_file_path(interp->reader, source_file);

		char8 path_base_parent_buf[8192];

		const u32 path_base_parent_chars = minos::path_to_absolute_directory(path_base, MutRange{ path_base_parent_buf });

		if (path_base_parent_chars == 0 || path_base_parent_chars > array_count(path_base_parent_buf))
			source_error(interp->errors, call_node->source_id, "Failed to make get parent directory from `from` source file (0x%X).\n", minos::last_error());

		const u32 absolute_path_chars = minos::path_to_absolute_relative_to(path, Range{ path_base_parent_buf , path_base_parent_chars }, MutRange{ absolute_path_buf });

		if (absolute_path_chars == 0 || absolute_path_chars > array_count(absolute_path_buf))
			source_error(interp->errors, call_node->source_id, "Failed to make `path` %.*s absolute relative to `from` %.*s (0x%X).\n", static_cast<s32>(path.count()), path.begin(), static_cast<s32>(path_base.count()), path_base.begin(), minos::last_error());

		absolute_path = Range{ absolute_path_buf, absolute_path_chars };
	}
	else
	{
		// This makes the prelude import of the configured standard library
		// (which is an absolute path) work.
		absolute_path = path;
	}

	*dst = import_file(interp, absolute_path, is_std);

	return dst;
}

static void* builtin_create_type_builder(Interpreter* interp, [[maybe_unused]] AstNode* call_node) noexcept
{
	(void) interp;

	TODO("Implement.");
}

static void* builtin_add_type_member(Interpreter* interp, [[maybe_unused]] AstNode* call_node) noexcept
{
	(void) interp;

	TODO("Implement.");
}

static void* builtin_complete_type(Interpreter* interp, [[maybe_unused]] AstNode* call_node) noexcept
{
	(void) interp;

	TODO("Implement.");
}

static void* builtin_source_id(Interpreter* interp, AstNode* call_node) noexcept
{
	SourceId* const dst = static_cast<SourceId*>(push_temporary(interp, 4, 4));

	*dst = call_node->source_id;

	return dst;
}



static void init_builtin_types(Interpreter* interp) noexcept
{
	const TypeId type_type_id = simple_type(interp->types, TypeTag::Type, {});

	const TypeId comp_integer_type_id = simple_type(interp->types, TypeTag::CompInteger, {});

	const TypeId bool_type_id = simple_type(interp->types, TypeTag::Boolean, {});

	const TypeId definition_type_id = simple_type(interp->types, TypeTag::Definition, {});

	const TypeId type_builder_type_id = simple_type(interp->types, TypeTag::TypeBuilder, {});

	const TypeId void_type_id = simple_type(interp->types, TypeTag::Void, {});

	const TypeId type_info_type_id = simple_type(interp->types, TypeTag::TypeInfo, {});

	ReferenceType ptr_to_type_builder_type{};
	ptr_to_type_builder_type.is_opt = false;
	ptr_to_type_builder_type.is_multi = false;
	ptr_to_type_builder_type.is_mut = true;
	ptr_to_type_builder_type.referenced_type_id = type_builder_type_id;

	const TypeId ptr_to_mut_type_builder_type_id = simple_type(interp->types, TypeTag::Ptr, range::from_object_bytes(&ptr_to_type_builder_type));

	NumericType s64_type{};
	s64_type.bits = 64;
	s64_type.is_signed = true;

	const TypeId s64_type_id = simple_type(interp->types, TypeTag::Integer, range::from_object_bytes(&s64_type));

	NumericType u8_type{};
	u8_type.bits = 8;
	u8_type.is_signed = false;

	const TypeId u8_type_id = simple_type(interp->types, TypeTag::Integer, range::from_object_bytes(&u8_type));

	ReferenceType slice_of_u8_type{};
	slice_of_u8_type.is_opt = false;
	slice_of_u8_type.is_multi = false;
	slice_of_u8_type.is_mut = false;
	slice_of_u8_type.referenced_type_id = u8_type_id;

	const TypeId slice_of_u8_type_id = simple_type(interp->types, TypeTag::Slice, range::from_object_bytes(&slice_of_u8_type));

	NumericType u32_type{};
	u32_type.bits = 32;
	u32_type.is_signed = false;

	const TypeId u32_type_id = simple_type(interp->types, TypeTag::Integer, range::from_object_bytes(&u32_type));



	interp->builtin_type_ids[static_cast<u8>(Builtin::Integer)] = make_func_type(interp->types, type_type_id,
		FuncTypeParamHelper{ id_from_identifier(interp->identifiers, range::from_literal_string("bits")), u8_type_id },
		FuncTypeParamHelper{ id_from_identifier(interp->identifiers, range::from_literal_string("is_signed")), bool_type_id }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::Float)] = make_func_type(interp->types, type_type_id,
		FuncTypeParamHelper{ id_from_identifier(interp->identifiers, range::from_literal_string("bits")), u8_type_id }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::Type)] = make_func_type(interp->types, type_type_id);

	interp->builtin_type_ids[static_cast<u8>(Builtin::Typeof)] = make_func_type(interp->types, type_type_id,
		FuncTypeParamHelper{ id_from_identifier(interp->identifiers, range::from_literal_string("arg")), type_info_type_id }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::Returntypeof)] = make_func_type(interp->types, type_type_id,
		FuncTypeParamHelper{ id_from_identifier(interp->identifiers, range::from_literal_string("arg")), type_info_type_id }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::Sizeof)] = make_func_type(interp->types, comp_integer_type_id,
		FuncTypeParamHelper{ id_from_identifier(interp->identifiers, range::from_literal_string("arg")), type_info_type_id }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::Alignof)] = make_func_type(interp->types, comp_integer_type_id,
		FuncTypeParamHelper{ id_from_identifier(interp->identifiers, range::from_literal_string("arg")), type_info_type_id }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::Strideof)] = make_func_type(interp->types, comp_integer_type_id,
		FuncTypeParamHelper{ id_from_identifier(interp->identifiers, range::from_literal_string("arg")), type_info_type_id }
	);

	// TODO: Figure out what type this takes as its argument. A member? If so,
	//       how do you effectively get that?
	interp->builtin_type_ids[static_cast<u8>(Builtin::Offsetof)] = make_func_type(interp->types, comp_integer_type_id);

	interp->builtin_type_ids[static_cast<u8>(Builtin::Nameof)] = make_func_type(interp->types, slice_of_u8_type_id,
		FuncTypeParamHelper{ id_from_identifier(interp->identifiers, range::from_literal_string("arg")), type_info_type_id }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::Import)] = make_func_type(interp->types, type_type_id,
		FuncTypeParamHelper{ id_from_identifier(interp->identifiers, range::from_literal_string("path")), slice_of_u8_type_id },
		FuncTypeParamHelper{ id_from_identifier(interp->identifiers, range::from_literal_string("is_std")), bool_type_id },
		FuncTypeParamHelper{ id_from_identifier(interp->identifiers, range::from_literal_string("from")), u32_type_id }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::CreateTypeBuilder)] = make_func_type(interp->types, type_builder_type_id);

	interp->builtin_type_ids[static_cast<u8>(Builtin::AddTypeMember)] = make_func_type(interp->types, void_type_id,
		FuncTypeParamHelper{ id_from_identifier(interp->identifiers, range::from_literal_string("builder")), ptr_to_mut_type_builder_type_id },
		FuncTypeParamHelper{ id_from_identifier(interp->identifiers, range::from_literal_string("definition")), definition_type_id },
		FuncTypeParamHelper{ id_from_identifier(interp->identifiers, range::from_literal_string("offset")), s64_type_id }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::CompleteType)] = make_func_type(interp->types, type_type_id,
		FuncTypeParamHelper{ id_from_identifier(interp->identifiers, range::from_literal_string("arg")), type_builder_type_id }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::SourceId)] = make_func_type(interp->types, u32_type_id);
}

static void init_builtin_values(Interpreter* interp) noexcept
{
	interp->builtin_values[static_cast<u8>(Builtin::Integer)] = &builtin_integer;
	interp->builtin_values[static_cast<u8>(Builtin::Float)] = &builtin_float;
	interp->builtin_values[static_cast<u8>(Builtin::Type)] = &builtin_type;
	interp->builtin_values[static_cast<u8>(Builtin::Typeof)] = &builtin_typeof;
	interp->builtin_values[static_cast<u8>(Builtin::Returntypeof)] = &builtin_returntypeof;
	interp->builtin_values[static_cast<u8>(Builtin::Sizeof)] = &builtin_sizeof;
	interp->builtin_values[static_cast<u8>(Builtin::Alignof)] = &builtin_alignof;
	interp->builtin_values[static_cast<u8>(Builtin::Strideof)] = &builtin_strideof;
	interp->builtin_values[static_cast<u8>(Builtin::Offsetof)] = &builtin_offsetof;
	interp->builtin_values[static_cast<u8>(Builtin::Nameof)] = &builtin_nameof;
	interp->builtin_values[static_cast<u8>(Builtin::Import)] = &builtin_import;
	interp->builtin_values[static_cast<u8>(Builtin::CreateTypeBuilder)] = &builtin_create_type_builder;
	interp->builtin_values[static_cast<u8>(Builtin::AddTypeMember)] = &builtin_add_type_member;
	interp->builtin_values[static_cast<u8>(Builtin::CompleteType)] = &builtin_complete_type;
	interp->builtin_values[static_cast<u8>(Builtin::SourceId)] = &builtin_source_id;
}

static void init_prelude_type(Interpreter* interp, Config* config, IdentifierPool* identifiers, AstPool* asts) noexcept
{
	NumericType u8_type{};
	u8_type.bits = 8;
	u8_type.is_signed = false;

	const TypeId u8_type_id = simple_type(interp->types, TypeTag::Integer, range::from_object_bytes(&u8_type));

	ArrayType array_of_u8_type{};
	array_of_u8_type.element_type = u8_type_id;
	array_of_u8_type.element_count = config->std.filepath.count();

	const TypeId array_of_u8_type_id = simple_type(interp->types, TypeTag::Array, range::from_object_bytes(&array_of_u8_type));

	const GlobalValueId std_filepath_value_id = make_global_value(interp->globals, with_assignability(array_of_u8_type_id, false), config->std.filepath.count(), 1, config->std.filepath.begin());

	const AstBuilderToken import_builtin = push_node(asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, static_cast<AstFlag>(Builtin::Import), AstTag::Builtin);

	push_node(asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, AstFlag::EMPTY, AstLitStringData{ std_filepath_value_id });

	const AstBuilderToken literal_zero = push_node(asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, AstFlag::EMPTY, AstLitIntegerData{ comp_integer_from_u64(0) });

	push_node(asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, AstFlag::EMPTY, AstLitIntegerData{ comp_integer_from_u64(0) });

	push_node(asts, literal_zero, SourceId::INVALID, AstFlag::EMPTY, AstTag::OpCmpEQ);

	const AstBuilderToken source_id_builtin = push_node(asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, static_cast<AstFlag>(Builtin::SourceId), AstTag::Builtin);

	push_node(asts, source_id_builtin, SourceId::INVALID, AstFlag::EMPTY, AstTag::Call);

	const AstBuilderToken import_call = push_node(asts, import_builtin, SourceId::INVALID, AstFlag::EMPTY, AstTag::Call);

	const AstBuilderToken std_definition = push_node(asts, import_call, SourceId::INVALID, AstFlag::EMPTY, AstDefinitionData{ id_from_identifier(identifiers, range::from_literal_string("std")) });

	const AstBuilderToken std_identifier = push_node(asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, AstFlag::EMPTY, AstIdentifierData{ id_from_identifier(identifiers, range::from_literal_string("std")) });

	push_node(asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, AstFlag::EMPTY, AstIdentifierData{ id_from_identifier(identifiers, range::from_literal_string("prelude")) });

	const AstBuilderToken prelude_member = push_node(asts, std_identifier, SourceId::INVALID, AstFlag::EMPTY, AstTag::OpMember);

	push_node(asts, prelude_member, SourceId::INVALID, AstFlag::Definition_IsUse, AstDefinitionData{ id_from_identifier(identifiers, range::from_literal_string("prelude"))} );

	push_node(asts, std_definition, SourceId::INVALID, AstFlag::EMPTY, AstTag::File);

	AstNode* const prelude_ast = complete_ast(asts);

	interp->prelude_type_id = type_from_file_ast(interp, prelude_ast, SourceId::INVALID);

	if (interp->log_file.m_rep != nullptr && interp->log_prelude)
	{
		const SourceId file_type_source = type_source_from_id(interp->types, interp->prelude_type_id);

		const SourceLocation file_type_location = source_location_from_source_id(interp->reader, file_type_source);

		diag::print_type(interp->log_file, interp->identifiers, interp->types, interp->prelude_type_id, &file_type_location);
	}
}



Interpreter* create_interpreter(AllocPool* alloc, Config* config, SourceReader* reader, Parser* parser, TypePool* types, AstPool* asts, IdentifierPool* identifiers, GlobalValuePool* globals, ErrorSink* errors, minos::FileHandle log_file, bool log_prelude) noexcept
{
	Interpreter* const interp = static_cast<Interpreter*>(alloc_from_pool(alloc, sizeof(Interpreter), alignof(Interpreter)));

	interp->reader = reader;
	interp->parser = parser;
	interp->types = types;
	interp->asts = asts;
	interp->identifiers = identifiers;
	interp->globals = globals;
	interp->errors = errors;
	interp->value_stack.init(1 << 20, 1 << 9);
	interp->value_stack_inds.init(1 << 20, 1 << 10);
	interp->activation_records.init(1 << 20, 1 << 9);
	interp->activation_record_top = 0;
	interp->prelude_type_id = TypeId::INVALID;
	interp->context_top = -1;
	interp->log_file = log_file;
	interp->log_prelude = log_prelude;

	init_builtin_types(interp);

	init_builtin_values(interp);

	init_prelude_type(interp, config, identifiers, asts);

	return interp;
}

void release_interpreter(Interpreter* interp) noexcept
{
	interp->value_stack.release();

	interp->value_stack_inds.release();

	interp->activation_records.release();
}

TypeId import_file(Interpreter* interp, Range<char8> filepath, bool is_std) noexcept
{
	SourceFileRead read = read_source_file(interp->reader, filepath);

	AstNode* root;

	if (read.source_file->ast_root == AstNodeId::INVALID)
	{
		root = parse(interp->parser, read.content, read.source_file->source_id_base, is_std, filepath);

		read.source_file->ast_root = id_from_ast_node(interp->asts, root);
	}
	else
	{
		root = ast_node_from_id(interp->asts, read.source_file->ast_root);
	}

	const TypeId file_type_id = type_from_file_ast(interp, root, read.source_file->source_id_base);

	if (interp->log_file.m_rep != nullptr)
	{
		const SourceId file_type_source = type_source_from_id(interp->types, file_type_id);

		const SourceLocation file_type_location = source_location_from_source_id(interp->reader, file_type_source);

		diag::print_type(interp->log_file, interp->identifiers, interp->types, file_type_id, &file_type_location);
	}

	return file_type_id;
}

const char8* tag_name(Builtin builtin) noexcept
{
	static constexpr const char8* BUILTIN_NAMES[] = {
		"[Unknown]",
		"_integer",
		"_float",
		"_type",
		"_definition",
		"_typeof",
		"_returntypeof",
		"_sizeof",
		"_alignof",
		"_strideof",
		"_offsetof",
		"_nameof",
		"_import",
		"_create_type_builder",
		"_add_type_member",
		"_complete_type",
		"_source_id",
	};

	u8 ordinal = static_cast<u8>(builtin);

	if (ordinal >= array_count(BUILTIN_NAMES))
		ordinal = 0;

	return BUILTIN_NAMES[ordinal];
}
