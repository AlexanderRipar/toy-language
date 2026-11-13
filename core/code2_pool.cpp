#include "core.hpp"
#include "structure.hpp"

#include "../infra/container/reserved_vec.hpp"
#include "../infra/inplace_sort.hpp"



static constexpr u64 CODES_RESERVE_SIZE = (1 << 26) - (1 << 16);
static constexpr u64 CODES_COMMIT_INCREMENT_COUNT = 1 << 14;

static constexpr u64 FIXUPS_RESERVE_SIZE = 1 << 22;
static constexpr u64 FIXUPS_COMMIT_INCREMENT_COUNT = 1 << 8;



enum class LocationMode : u8
{
	Value,
	AssignmentDst,
	AddressSrc,
};

enum class Code2FixupTag : u8
{
	INVALID = 0,
	Expr,
	Where,
	LoopBody,
	FunctionBody,
	Empty,
};

struct Code2Fixup
{
	Code2FixupTag tag;

	u16 leave_out;

	Maybe<AstNodeId> src;

	Code2Id* dst;
};





NORETURN static void record_lowering_error(CoreData* core, const AstNode* location, CompileError error) noexcept
{
	record_error(core, location, error);

	longjmp(core->code2.error_jmp_buf, 1);
}



void* emit_raw(CoreData* core, u64 size) noexcept
{
	if (size > UINT32_MAX)
		panic("Size `%` of `Code2` allocation request exceeds the supported maximum of `%`.\n", size, UINT32_MAX);

	return core->code2.codes.reserve_exact(static_cast<u32>(size));
}

template<typename T>
static T* emit_code2(CoreData* core, u64 size, Code2Op op) noexcept
{
	ASSERT_OR_IGNORE(sizeof(Code2) <= size);

	void* const dst = emit_raw(core, size);

	Code2 header{};
	header.op = op;

	memcpy(dst, &header, sizeof(header));

	return static_cast<T*>(dst);
}

static Code2Block* emit_block_header(CoreData* core) noexcept
{
	return static_cast<Code2Block*>(emit_raw(core, sizeof(Code2Block)));
}

static Code2Id close_block_header(CoreData* core, Code2Block* block, u16 definition_count, Code2Offset leave_value_offset, u16 leave_out) noexcept
{
	Code2* const leave_value = code2_from_offset(block, leave_value_offset);

	if (leave_value->op != Code2Op::Leave && leave_value->op != Code2Op::Repeat)
	{
		Code2AttLeave* const leave = emit_code2<Code2AttLeave>(core, sizeof(Code2AttLeave), Code2Op::Leave);
		leave->header.leave.out = leave_out;
		leave->operand = leave_value_offset;
	}
	else
	{
		ASSERT_OR_IGNORE(leave_out == 0
		 && (leave_value->op != Code2Op::Leave || leave_value->leave.out == 0)
		 && (leave_value->op != Code2Op::Repeat || leave_value->repeat.out == 0)
		);
	}

	const u64 size = core->code2.codes.end() - reinterpret_cast<u32*>(block);

	if (size >= UINT32_MAX)
		panic("Size `%` of `Code2Block` exceeds the supported maximum of `%`.\n", size, UINT32_MAX);

	*block = Code2Block{};

	block->type = TypeId::INVALID;
	block->size = static_cast<u32>(size);
	block->definition_count = definition_count;

	return id_from_code2_block(core, block);
}



static void register_fixup_raw(CoreData* core, Code2Id* dst, Maybe<AstNode*> src, Code2FixupTag tag, u16 leave_out) noexcept
{
	Code2Fixup* const fixup = core->code2.fixups.reserve();
	fixup->tag = tag;
	fixup->leave_out = leave_out;
	fixup->src = is_some(src) ? some(id_from_ast_node(core, get(src))) : none<AstNodeId>();
	fixup->dst = dst;
}

static void register_fixup_expr(CoreData* core, Code2Id* dst, AstNode* src, u16 leave_out = 0) noexcept
{
	register_fixup_raw(core, dst, some(src), Code2FixupTag::Expr, leave_out);
}

static void register_fixup_where(CoreData* core, Code2Id* dst, AstNode* src) noexcept
{
	register_fixup_raw(core, dst, some(src), Code2FixupTag::Where, 0);
}

static void register_fixup_loop_body(CoreData* core, Code2Id* dst, AstNode* src) noexcept
{
	register_fixup_raw(core, dst, some(src), Code2FixupTag::LoopBody, 0);
}

static void register_fixup_function_body(CoreData* core, Code2Id* dst, AstNode* src) noexcept
{
	register_fixup_raw(core, dst, some(src), Code2FixupTag::FunctionBody, 0);
}

static void register_fixup_empty(CoreData* core, Code2Id* dst, u16 leave_out = 0) noexcept
{
	register_fixup_raw(core, dst, none<AstNode*>(), Code2FixupTag::Empty, leave_out);
}



static Code2* code2_from_expr_raw(CoreData* core, AstNode* expr, Code2Block* block, LocationMode loc = LocationMode::Value, bool ignore_where = false) noexcept;

static bool is_location_ast_tag(AstTag tag) noexcept
{
	return tag == AstTag::Identifier || tag == AstTag::UOpDeref || tag == AstTag::Member || tag == AstTag::OpArrayIndex;
}

static Code2Offset code2_from_expr(CoreData* core, AstNode* expr, Code2Block* block, LocationMode loc = LocationMode::Value, bool ignore_where = false) noexcept
{
	const AstTag tag = expr->tag;

	Code2* const code = code2_from_expr_raw(core, expr, block, loc, ignore_where);

	const Code2Offset code_offset = offset_from_code2(block, code);

	if (is_location_ast_tag(tag) || loc == LocationMode::Value)
	{
		return code_offset;
	}
	else if (loc == LocationMode::AddressSrc)
	{
		Code2AttUnaryOp* const make_loc = emit_code2<Code2AttUnaryOp>(core, sizeof(Code2AttUnaryOp), Code2Op::MakeLocation);
		make_loc->operand = code_offset;

		return offset_from_code2(block, &make_loc->header);
	}
	else
	{
		ASSERT_OR_IGNORE(loc == LocationMode::AssignmentDst);

		record_lowering_error(core, expr, CompileError::SetLhsNotMutable);
	}
}

static Code2* code2_from_expr_raw(CoreData* core, AstNode* expr, Code2Block* block, LocationMode loc, bool ignore_where) noexcept
{
	switch (expr->tag)
	{
	case AstTag::Builtin:
	{
		const AstBuiltinData* const attach = attachment_of<AstBuiltinData>(expr);

		Code2* const dst = emit_code2<Code2>(core, sizeof(Code2), Code2Op::Builtin);
		dst->builtin.builtin = attach->builtin;

		return dst;
	}

	case AstTag::CompositeInitializer:
	{
		AstDirectChildIterator it = direct_children_of(expr);

		u16 member_count = 0;

		const u64 mark = temp_stack_mark(core);

		Code2AttComposite_Member* const members = static_cast<Code2AttComposite_Member*>(temp_stack_alloc(core, 0, alignof(Code2AttComposite_Member)));

		while (has_next(&it))
		{
			ASSERT_OR_IGNORE(member_count != UINT16_MAX);

			AstNode* const member = next(&it);

			Maybe<IdentifierId> name;
			AstNode* member_value;

			if (member->tag == AstTag::OpSet)
			{
				AstNode* const implied_member = first_child_of(member);

				name = some(attachment_of<AstImpliedMemberData>(implied_member)->identifier_id);
				member_value = next_sibling_of(implied_member);
			}
			else
			{
				name = none<IdentifierId>();
				member_value = member;
			}

			Code2AttComposite_Member* const member_dst = static_cast<Code2AttComposite_Member*>(temp_stack_alloc(core, sizeof(Code2AttComposite_Member), alignof(Code2AttComposite_Member)));
			member_dst->name = name;
			member_dst->value = code2_from_expr(core, member_value, block);

			member_count += 1;
		}

		const u64 size = sizeof(Code2AttComposite) + static_cast<u64>(member_count) * sizeof(Code2AttComposite_Member);

		Code2AttComposite* const dst = emit_code2<Code2AttComposite>(core, size, Code2Op::Composite);
		dst->header.composite.member_count = member_count;

		memcpy(dst->members, members, static_cast<u64>(member_count) * sizeof(Code2AttComposite_Member));

		temp_stack_release(core, mark);

		return &dst->header;
	}

	case AstTag::ArrayInitializer:
	{
		AstDirectChildIterator it = direct_children_of(expr);

		u16 element_count = 0;

		const u64 mark = temp_stack_mark(core);

		Code2AttArray_Element* const elements = static_cast<Code2AttArray_Element*>(temp_stack_alloc(core, 0, alignof(Code2AttArray_Element)));

		while (has_next(&it))
		{
			ASSERT_OR_IGNORE(element_count != UINT16_MAX);

			AstNode* const element = next(&it);

			Code2AttArray_Element* const element_dst = static_cast<Code2AttArray_Element*>(temp_stack_alloc(core, sizeof(Code2AttArray_Element), alignof(Code2AttArray_Element)));
			element_dst->value = code2_from_expr(core, element, block);

			element_count += 1;
		}

		const u64 size = sizeof(Code2AttArray) + static_cast<u64>(element_count) * sizeof(Code2AttArray_Element);

		Code2AttArray* const dst = emit_code2<Code2AttArray>(core, size, Code2Op::Array);
		dst->header.array.element_count = element_count;

		memcpy(dst->elements, elements, static_cast<u64>(element_count) * sizeof(Code2AttArray_Element));

		temp_stack_release(core, mark);

		return &dst->header;
	}

	case AstTag::Wildcard:
	{
		TODO("Implement Ast -> Code2 transition for `AstTag::%`.", tag_name(expr->tag));
	}

	case AstTag::Definition:
	{
		const DefinitionInfo info = get_definition_info(expr);

		const Maybe<Code2Offset> type = is_some(info.type)
			? some(code2_from_expr(core, get(info.type), block))
			: none<Code2Offset>();

		const Maybe<Code2Offset> value = is_some(info.value)
			? some(code2_from_expr(core, get(info.value), block))
			: none<Code2Offset>();

		Code2AttDefinition* const dst = emit_code2<Code2AttDefinition>(core, sizeof(Code2AttDefinition), Code2Op::Definition);
		dst->header.definition.is_mut = has_flag(expr, AstFlag::Definition_IsMut);
		dst->header.definition.is_pub = has_flag(expr, AstFlag::Definition_IsPub);
		dst->header.definition.has_self_reference = has_flag(expr, AstFlag::Definition_HasSelfReference);
		dst->name = attachment_of<AstDefinitionData>(expr)->identifier_id;
		dst->type = type;
		dst->value = value;

		return &dst->header;
	}

	case AstTag::Block:
	{
		Code2AttJump* const dst = emit_code2<Code2AttJump>(core, sizeof(Code2AttJump), Code2Op::Jump);

		register_fixup_expr(core, &dst->body, expr);

		return &dst->header;
	}

	case AstTag::If:
	{
		if (!has_flag(expr, AstFlag::If_HasWhere) || ignore_where)
		{
			const IfInfo info = get_if_info(expr);

			const Code2Offset condition = code2_from_expr(core, info.consequent, block);

			Code2AttIf* const dst = emit_code2<Code2AttIf>(core, sizeof(Code2AttIf), Code2Op::If);
			dst->condition = condition;

			register_fixup_expr(core, &dst->consequent, info.consequent);

			if (is_some(info.alternative))
				register_fixup_expr(core, &dst->alternative, get(info.alternative));
			else
				register_fixup_empty(core, &dst->alternative);
				
			return &dst->header;
		}
		else
		{
			Code2AttJump* const dst = emit_code2<Code2AttJump>(core, sizeof(Code2AttJump), Code2Op::Jump);

			register_fixup_where(core, &dst->body, expr);

			return &dst->header;
		}
	}

	case AstTag::For:
	{
		if (!has_flag(expr, AstFlag::For_HasWhere) || ignore_where)
		{
			Code2AttJump* const dst = emit_code2<Code2AttJump>(core, sizeof(Code2AttJump), Code2Op::Loop);
			dst->header.loop.is_type_eval = false; // TODO

			register_fixup_loop_body(core, &dst->body, expr);

			return &dst->header;
		}
		else
		{
			Code2AttJump* const dst = emit_code2<Code2AttJump>(core, sizeof(Code2AttJump), Code2Op::Jump);

			register_fixup_where(core, &dst->body, expr);

			return &dst->header;
		}
	}

	case AstTag::ForEach:
	{
		if (!has_flag(expr, AstFlag::ForEach_HasWhere) || ignore_where)
		{
			Code2AttJump* const dst = emit_code2<Code2AttJump>(core, sizeof(Code2AttJump), Code2Op::Loop);
			dst->header.loop.is_type_eval = false; // TODO

			register_fixup_loop_body(core, &dst->body, expr);

			return &dst->header;
		}
		else
		{
			Code2AttJump* const dst = emit_code2<Code2AttJump>(core, sizeof(Code2AttJump), Code2Op::Jump);

			register_fixup_where(core, &dst->body, expr);

			return &dst->header;
		}
	}

	case AstTag::Switch:
	{
		TODO("Implement Ast -> Code2 transition for `AstTag::%`.", tag_name(expr->tag));
	}

	case AstTag::Func:
	{
		AstNode* const signature_node = first_child_of(expr);

		const Code2Offset signature = code2_from_expr(core, signature_node, block);

		AstNode* const body_node = next_sibling_of(signature_node);

		Code2AttFunc* const dst = emit_code2<Code2AttFunc>(core, sizeof(Code2AttFunc), Code2Op::Func);
		dst->signature = signature;

		register_fixup_function_body(core, &dst->body, body_node);

		return &dst->header;
	}

	case AstTag::Signature:
	{
		const SignatureInfo info = get_signature_info(expr);

		if (is_some(info.requires_))
			TODO("Implement signature-level `requires`.");

		if (is_some(info.ensures))
			TODO("Implement signature-level `ensures`.");

		const u64 mark = temp_stack_mark(core);

		Code2AttSignature_Parameter* const parameters = static_cast<Code2AttSignature_Parameter*>(temp_stack_alloc(core, 0, alignof(Code2AttSignature_Parameter)));

		AstDirectChildIterator it = direct_children_of(info.parameters);

		bool is_variadic = false;

		u8 parameter_count = 0;

		while (has_next(&it))
		{
			ASSERT_OR_IGNORE(parameter_count < 64);

			AstNode* const parameter = next(&it);

			const DefinitionInfo parameter_info = get_definition_info(parameter);

			Code2AttSignature_Parameter* const parameter_dst = static_cast<Code2AttSignature_Parameter*>(temp_stack_alloc(core, sizeof(Code2AttSignature_Parameter), alignof(Code2AttSignature_Parameter)));

			const bool is_templated = has_flag(parameter, AstFlag::Definition_IsTemplatedParam);

			parameter_dst->name = attachment_of<AstDefinitionData>(parameter)->identifier_id;
			parameter_dst->is_mut = has_flag(parameter, AstFlag::Definition_IsMut);
			parameter_dst->is_eval = has_flag(parameter, AstFlag::Definition_IsEval);
			parameter_dst->is_templated = is_templated;

			AstNode* type = get(parameter_info.type);

			if (type->tag == AstTag::UOpTypeVarArgs)
			{
				ASSERT_OR_IGNORE(!has_next_sibling(parameter));

				is_variadic = true;

				type = first_child_of(get(parameter_info.type));
			}

			if (is_templated)
			{
				// Just quickly stash this so we can later register its
				// completion with the correct destination address.
				parameter_dst->templated.type = static_cast<Code2Id>(id_from_ast_node(core, type));
				
				if (is_some(parameter_info.value))
					parameter_dst->templated.default_value = some(static_cast<Code2Id>(id_from_ast_node(core, get(parameter_info.value))));
				else
					parameter_dst->templated.default_value = none<Code2Id>();
			}
			else
			{
				parameter_dst->complete.type = code2_from_expr(core, type, block);

				if (is_some(parameter_info.value))
					parameter_dst->complete.default_value = some(code2_from_expr(core, get(parameter_info.value), block));
				else
					parameter_dst->complete.default_value = none<Code2Offset>();
			}

			parameter_count += 1;
		}

		const bool has_templated_return_type = has_flag(expr, AstFlag::Signature_HasTemplatedReturnType);

		const Code2Offset return_type = has_templated_return_type
			? Code2Offset::INVALID
			: code2_from_expr(core, info.return_type, block);

		const u64 size = sizeof(Code2AttSignature) + static_cast<u64>(parameter_count) * sizeof(Code2AttSignature_Parameter);

		Code2AttSignature* const dst = emit_code2<Code2AttSignature>(core, size, Code2Op::Signature);
		dst->header.signature.parameter_count = parameter_count;
		dst->header.signature.is_func = !has_flag(expr, AstFlag::Signature_IsProc);
		dst->header.signature.is_variadic = is_variadic;
		dst->header.signature.has_templated_return_type = has_templated_return_type;
		dst->header.signature.closed_count = 0; // TODO

		if (!has_templated_return_type)
			dst->return_type.complete = return_type;

		memcpy(dst->parameters, parameters, static_cast<u64>(parameter_count) * sizeof(Code2AttSignature_Parameter));

		temp_stack_release(core, mark);

		for (u8 i = 0; i != parameter_count; ++i)
		{
			Code2AttSignature_Parameter* const parameter_dst = dst->parameters + i;

			if (parameter_dst->is_templated)
			{
				AstNode* const type = ast_node_from_id(core, static_cast<AstNodeId>(parameter_dst->templated.type));

				const Maybe<AstNode*> default_value = is_some(parameter_dst->templated.default_value)
					? some(ast_node_from_id(core, static_cast<AstNodeId>(get(parameter_dst->templated.default_value))))
					: none<AstNode*>();

				register_fixup_expr(core, &parameter_dst->templated.type, type);

				if (is_some(default_value))
					register_fixup_expr(core, reinterpret_cast<Code2Id*>(&parameter_dst->templated.default_value), get(default_value));
			}
		}

		if (has_templated_return_type)
			register_fixup_expr(core, &dst->return_type.templated, info.return_type);

		return &dst->header;
	}

	case AstTag::Trait:
	{
		TODO("Implement Ast -> Code2 transition for `AstTag::%`.", tag_name(expr->tag));
	}

	case AstTag::Impl:
	{
		TODO("Implement Ast -> Code2 transition for `AstTag::%`.", tag_name(expr->tag));
	}

	case AstTag::Self:
	{
		TODO("Implement Ast -> Code2 transition for `AstTag::%`.", tag_name(expr->tag));
	}

	case AstTag::Catch:
	{
		TODO("Implement Ast -> Code2 transition for `AstTag::%`.", tag_name(expr->tag));
	}

	case AstTag::Unreachable:
	{
		return emit_code2<Code2>(core, sizeof(Code2), Code2Op::Unreachable);
	}

	case AstTag::Undefined:
	{
		return emit_code2<Code2>(core, sizeof(Code2), Code2Op::Undefined);
	}

	case AstTag::Identifier:
	{
		const AstIdentifierData attachment = *attachment_of<AstIdentifierData>(expr);

		Code2* dst;

		if (attachment.binding.kind == NameBindingKind::Scoped)
		{
			const Code2Op op = loc == LocationMode::Value ? Code2Op::LocalValue : Code2Op::LocalLocation;

			dst = emit_code2<Code2>(core, sizeof(Code2), op);
			dst->local_ref.out = attachment.binding.scoped.out;
			dst->local_ref.rank = attachment.binding.scoped.rank;
		}
		else if (attachment.binding.kind == NameBindingKind::Global)
		{
			const Code2Op op = loc == LocationMode::Value ? Code2Op::GlobalValue : Code2Op::GlobalLocation;

			dst = emit_code2<Code2>(core, sizeof(Code2), op);
			dst->global_ref.rank = attachment.binding.global.rank;
		}
		else if (attachment.binding.kind == NameBindingKind::Closed)
		{
			const Code2Op op = loc == LocationMode::Value ? Code2Op::ClosedValue : Code2Op::ClosedLocation;

			dst = emit_code2<Code2>(core, sizeof(Code2), op);
			dst->closed_ref.rank = attachment.binding.closed.rank_in_closure;
		}
		else
		{
			ASSERT_OR_IGNORE(attachment.binding.kind == NameBindingKind::TraitArgument);

			const Code2Op op = loc == LocationMode::Value ? Code2Op::ClosedValue : Code2Op::ClosedLocation;

			dst = emit_code2<Code2>(core, sizeof(Code2), op);
			dst->trait_argument_ref.rank = attachment.binding.trait_argument.rank;
		}

		return dst;
	}

	case AstTag::LitInteger:
	{
		const CompIntegerValue value = attachment_of<AstLitIntegerData>(expr)->value;

		if (comp_integer_compare(comp_integer_from_u64(UINT16_MAX), value) == StrongCompareOrdering::GreaterThan)
		{
			u64 uint_value;
			if (!u64_from_comp_integer(value, 16, &uint_value))
				ASSERT_UNREACHABLE;

			Code2* const dst = emit_code2<Code2>(core, sizeof(Code2), Code2Op::Integer16);
			dst->integer16.value = static_cast<u16>(uint_value);

			return dst;
		}
		else
		{
			Code2AttInteger64* dst = emit_code2<Code2AttInteger64>(core, sizeof(Code2AttInteger64), Code2Op::Integer64);

			static_assert(sizeof(CompIntegerValue) == sizeof(Code2AttInteger64::value_hi) + sizeof(Code2AttInteger64::value_lo));

			memcpy(&dst->value_lo, &value, sizeof(CompIntegerValue));

			return &dst->header;
		}
	}

	case AstTag::LitFloat:
	{
		const CompFloatValue value = attachment_of<AstLitFloatData>(expr)->value;

		Code2AttFloat* const dst = emit_code2<Code2AttFloat>(core, sizeof(Code2AttFloat), Code2Op::Float);

		static_assert(sizeof(CompFloatValue) == sizeof(Code2AttFloat::value_hi) + sizeof(Code2AttFloat::value_lo));

		memcpy(&dst->value_lo, &value, sizeof(CompFloatValue));

		return &dst->header;
	}

	case AstTag::LitChar:
	{
		const u32 codepoint = attachment_of<AstLitCharData>(expr)->codepoint;

		if (codepoint <= UINT16_MAX)
		{
			Code2* const dst = emit_code2<Code2>(core, sizeof(Code2), Code2Op::Integer16);
			dst->integer16.value = static_cast<u16>(codepoint);

			return dst;
		}
		else
		{
			const CompIntegerValue value = comp_integer_from_u64(codepoint);

			Code2AttInteger64* dst = emit_code2<Code2AttInteger64>(core, sizeof(Code2AttInteger64), Code2Op::Integer64);

			static_assert(sizeof(CompIntegerValue) == sizeof(Code2AttInteger64::value_hi) + sizeof(Code2AttInteger64::value_lo));

			memcpy(&dst->value_lo, &value, sizeof(CompIntegerValue));

			return &dst->header;
		}
	}

	case AstTag::LitString:
	{
		const AstLitStringData* const attachment = attachment_of<AstLitStringData>(expr);

		Code2AttString* const dst = emit_code2<Code2AttString>(core, sizeof(Code2AttString), Code2Op::String);
		dst->data = attachment->value_id;
		dst->size = attachment->value_size;

		return &dst->header;
	}

	case AstTag::OpSliceOf:
	{
		const OpSliceOfInfo info = get_op_slice_of_info(expr);

		const Code2Offset sliced = code2_from_expr(core, info.sliced, block, LocationMode::AddressSrc);

		const Maybe<Code2Offset> begin = is_some(info.begin)
			? some(code2_from_expr(core, get(info.begin), block))
			: none<Code2Offset>();

		const Maybe<Code2Offset> end = is_some(info.end)
			? some(code2_from_expr(core, get(info.end), block))
			: none<Code2Offset>();

		Code2AttSlice* const dst = emit_code2<Code2AttSlice>(core, sizeof(Code2AttSlice), Code2Op::Slice);
		dst->sliced = sliced;
		dst->begin = begin;
		dst->end = end;

		return &dst->header;
	}

	case AstTag::Return:
	{
		const Code2Offset operand = code2_from_expr(core, first_child_of(expr), block);

		Code2AttLeave* const dst = emit_code2<Code2AttLeave>(core, sizeof(Code2AttLeave), Code2Op::Leave);
		dst->header.leave.out = core->code2.return_out;
		dst->operand = operand;

		return &dst->header;
	}

	case AstTag::Break:
	{
		const Code2Offset operand = code2_from_expr(core, first_child_of(expr), block);

		Code2AttLeave* const dst = emit_code2<Code2AttLeave>(core, sizeof(Code2AttLeave), Code2Op::Leave);
		dst->header.leave.out = core->code2.break_out;
		dst->operand = operand;

		return &dst->header;
	}

	case AstTag::Call:
	{
		AstNode* const callee_node = first_child_of(expr);

		const Code2Offset callee = code2_from_expr(core, callee_node, block);

		u8 argument_count = 0;

		for (AstNode* argument = callee_node; has_next_sibling(argument); argument = next_sibling_of(argument))
		{
			ASSERT_OR_IGNORE(argument_count < 64);

			argument_count += 1;
		}

		const u64 size = sizeof(Code2AttCall) + static_cast<u64>(argument_count) * sizeof(Code2AttCall_Argument);

		Code2AttCall* const dst = emit_code2<Code2AttCall>(core, size, Code2Op::Call);
		dst->header.call.argument_count = argument_count;
		dst->callee = callee;

		AstNode* argument = callee_node;

		u8 i = 0;

		while (has_next_sibling(argument))
		{
			ASSERT_OR_IGNORE(i < argument_count);

			argument = next_sibling_of(argument);

			Maybe<IdentifierId> name;
			AstNode* argument_value;

			if (argument->tag == AstTag::OpSet)
			{
				AstNode* const implied_member = first_child_of(argument);

				name = some(attachment_of<AstImpliedMemberData>(implied_member)->identifier_id);
				argument_value = next_sibling_of(implied_member);
			}
			else
			{
				name = none<IdentifierId>();
				argument_value = argument;
			}

			dst->arguments[i].name = name;
			register_fixup_expr(core, &dst->arguments[i].value, argument_value);

			i += 1;
		}

		ASSERT_OR_IGNORE(i == argument_count);

		return &dst->header;
	}

	case AstTag::UOpTypeTailArray:
	case AstTag::UOpTypeSlice:
	case AstTag::UOpTypeMultiPtr:
	case AstTag::UOpTypeOptMultiPtr:
	case AstTag::UOpTypeOptPtr:
	case AstTag::UOpTypePtr:
	{
		Code2Op op;
		
		if (expr->tag == AstTag::UOpTypeTailArray)
			op = Code2Op::TypeTailArray;
		else if (expr->tag == AstTag::UOpTypeSlice)
			op = Code2Op::TypeSlice;
		else if (expr->tag == AstTag::UOpTypeMultiPtr || expr->tag == AstTag::UOpTypeOptMultiPtr || expr->tag == AstTag::UOpTypeOptPtr || expr->tag == AstTag::UOpTypePtr)
			op = Code2Op::TypePtr;
		else
			ASSERT_UNREACHABLE;

		const Code2Offset operand = code2_from_expr(core, first_child_of(expr), block);

		Code2AttUnaryOp* const dst = emit_code2<Code2AttUnaryOp>(core, sizeof(Code2AttUnaryOp), op);
		dst->header.reference_type.is_opt = expr->tag == AstTag::UOpTypeOptPtr || expr->tag == AstTag::UOpTypeOptMultiPtr;
		dst->header.reference_type.is_multi = expr->tag == AstTag::UOpTypeMultiPtr || expr->tag == AstTag::UOpTypeOptMultiPtr;
		dst->header.reference_type.is_mut = has_flag(expr, AstFlag::Type_IsMut);
		dst->operand = operand;

		return &dst->header;
	}

	case AstTag::UOpEval:
	{
		Code2AttJump* const dst = emit_code2<Code2AttJump>(core, sizeof(Code2AttJump), Code2Op::Eval);

		register_fixup_expr(core, &dst->body, first_child_of(expr));

		return &dst->header;
	}

	case AstTag::UOpTry:
	{
		TODO("Implement Ast -> Code2 transition for `AstTag::%`.", tag_name(expr->tag));
	}

	case AstTag::UOpDefer:
	{
		TODO("Implement Ast -> Code2 transition for `AstTag::%`.", tag_name(expr->tag));
	}

	case AstTag::UOpDistinct:
	{
		TODO("Implement Ast -> Code2 transition for `AstTag::%`.", tag_name(expr->tag));
	}

	case AstTag::UOpAddr:
	case AstTag::UOpDeref:
	case AstTag::UOpBitNot:
	case AstTag::UOpLogNot:
	case AstTag::UOpNegate:
	case AstTag::UOpPos:
	{
		const LocationMode operand_loc = expr->tag == AstTag::UOpAddr
			? LocationMode::AddressSrc
			: LocationMode::Value;

		const Code2Offset operand = code2_from_expr(core, first_child_of(expr), block, operand_loc);

		Code2Op op;

		if (expr->tag == AstTag::UOpAddr)
			op = Code2Op::Address;
		else if (expr->tag == AstTag::UOpDeref)
			op = Code2Op::Dereference;
		else if (expr->tag == AstTag::UOpBitNot)
			op = Code2Op::BitNot;
		else if (expr->tag == AstTag::UOpLogNot)
			op = Code2Op::LogicalNot;
		else if (expr->tag == AstTag::UOpNegate)
			op = Code2Op::Negate;
		else if (expr->tag == AstTag::UOpPos)
			op = Code2Op::UnaryPlus;
		else
			ASSERT_UNREACHABLE;

		Code2AttUnaryOp* const dst = emit_code2<Code2AttUnaryOp>(core, sizeof(Code2AttUnaryOp), op);
		dst->operand = operand;

		return &dst->header;
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
		AstNode* const lhs_node = first_child_of(expr);

		const Code2Offset lhs = code2_from_expr(core, lhs_node, block);

		AstNode* const rhs_node = next_sibling_of(lhs_node);

		const Code2Offset rhs = code2_from_expr(core, rhs_node, block);

		Code2AttBinaryOp* const dst = emit_code2<Code2AttBinaryOp>(core, sizeof(Code2AttBinaryOp), Code2Op::BinaryArithmetic);
		dst->header.binary_arithmetic.kind = static_cast<Code2AttBinaryArithmetic_Kind>(static_cast<u8>(expr->tag) - static_cast<u8>(AstTag::OpAdd));
		dst->lhs = lhs;
		dst->rhs = rhs;

		return &dst->header;
	}

	case AstTag::OpShiftL:
	case AstTag::OpShiftR:
	{
		AstNode* const lhs_node = first_child_of(expr);

		const Code2Offset lhs = code2_from_expr(core, lhs_node, block);

		AstNode* const rhs_node = next_sibling_of(lhs_node);

		const Code2Offset rhs = code2_from_expr(core, rhs_node, block);

		Code2AttBinaryOp* const dst = emit_code2<Code2AttBinaryOp>(core, sizeof(Code2AttBinaryOp), Code2Op::BitShift);
		dst->header.bit_shift.kind = static_cast<Code2AttBitShift_Kind>(static_cast<u8>(expr->tag) - static_cast<u8>(AstTag::OpShiftL));
		dst->lhs = lhs;
		dst->rhs = rhs;

		return &dst->header;
	}

	case AstTag::OpLogAnd:
	case AstTag::OpLogOr:
	{
		AstNode* const lhs_node = first_child_of(expr);

		const Code2Offset lhs = code2_from_expr(core, lhs_node, block);

		AstNode* const rhs_node = next_sibling_of(lhs_node);

		const Code2Op op = expr->tag == AstTag::OpLogAnd
			? Code2Op::LogicalAnd
			: Code2Op::LogicalOr;

		Code2AttLogicalOp* const dst = emit_code2<Code2AttLogicalOp>(core, sizeof(Code2AttLogicalOp), op);
		dst->lhs = lhs;

		register_fixup_expr(core, &dst->rhs, rhs_node);

		return &dst->header;
	}

	case AstTag::Member:
	{
		const Code2Offset composite = code2_from_expr(core, first_child_of(expr), block, loc);

		const Code2Op op = loc == LocationMode::Value ? Code2Op::MemberValue : Code2Op::MemberLocation;

		Code2AttMember* const dst = emit_code2<Code2AttMember>(core, sizeof(Code2AttMember), op);
		dst->composite.offset = composite;
		dst->name = attachment_of<AstMemberData>(expr)->identifier_id;

		return &dst->header;
	}

	case AstTag::OpCmpLT:
	case AstTag::OpCmpGT:
	case AstTag::OpCmpLE:
	case AstTag::OpCmpGE:
	case AstTag::OpCmpNE:
	case AstTag::OpCmpEQ:
	{
		AstNode* const lhs_node = first_child_of(expr);

		const Code2Offset lhs = code2_from_expr(core, lhs_node, block);

		AstNode* const rhs_node = next_sibling_of(lhs_node);

		const Code2Offset rhs = code2_from_expr(core, rhs_node, block);

		Code2AttBinaryOp* const dst = emit_code2<Code2AttBinaryOp>(core, sizeof(Code2AttBinaryOp), Code2Op::Compare);
		dst->header.compare.kind = static_cast<Code2AttCompare_Kind>(static_cast<u8>(expr->tag) - static_cast<u8>(AstTag::OpCmpLT));
		dst->lhs = lhs;
		dst->rhs = rhs;

		return &dst->header;
	}

	case AstTag::OpSet:
	{
		AstNode* const lhs_node = first_child_of(expr);

		const Code2Offset lhs = code2_from_expr(core, lhs_node, block, LocationMode::AssignmentDst);

		AstNode* const rhs_node = next_sibling_of(lhs_node);

		const Code2Offset rhs = code2_from_expr(core, rhs_node, block);

		Code2AttBinaryOp* const dst = emit_code2<Code2AttBinaryOp>(core, sizeof(Code2AttBinaryOp), Code2Op::Store);
		dst->lhs = lhs;
		dst->rhs = rhs;

		return &dst->header;
	}

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
	{
		AstNode* const lhs_node = first_child_of(expr);

		const Code2Offset lhs = code2_from_expr(core, lhs_node, block, LocationMode::AssignmentDst);

		Code2AttUnaryOp* const dup = emit_code2<Code2AttUnaryOp>(core, sizeof(Code2AttUnaryOp), Code2Op::DupAndLoadLocation);
		dup->operand = lhs;

		AstNode* const rhs_node = next_sibling_of(lhs_node);

		const Code2Offset rhs = code2_from_expr(core, rhs_node, block);

		const Code2Op op = expr->tag < AstTag::OpShiftL
			? Code2Op::BinaryArithmetic
			: Code2Op::BitShift;

		Code2AttBinaryOp* const operation = emit_code2<Code2AttBinaryOp>(core, sizeof(Code2AttBinaryOp), op);

		if (op == Code2Op::BinaryArithmetic)
			operation->header.binary_arithmetic.kind = static_cast<Code2AttBinaryArithmetic_Kind>(static_cast<u8>(expr->tag) - static_cast<u8>(AstTag::OpSetAdd));
		else if (op == Code2Op::BitShift)
			operation->header.bit_shift.kind = static_cast<Code2AttBitShift_Kind>(static_cast<u8>(expr->tag) - static_cast<u8>(AstTag::OpSetShiftL));

		operation->lhs = offset_from_code2(block, &dup->header);
		operation->rhs = rhs;

		Code2AttBinaryOp* const dst = emit_code2<Code2AttBinaryOp>(core, sizeof(Code2AttBinaryOp), Code2Op::Store);
		dst->lhs = lhs;
		dst->rhs = offset_from_code2(block, &operation->header);

		return &dst->header;
	}

	case AstTag::OpTypeArray:
	{
		AstNode* const lhs_node = first_child_of(expr);

		const Code2Offset lhs = code2_from_expr(core, lhs_node, block);

		AstNode* const rhs_node = next_sibling_of(lhs_node);

		const Code2Offset rhs = code2_from_expr(core, rhs_node, block);

		Code2AttBinaryOp* const dst = emit_code2<Code2AttBinaryOp>(core, sizeof(Code2AttBinaryOp), Code2Op::TypeArray);
		dst->lhs = lhs;
		dst->rhs = rhs;

		return &dst->header;
	}

	case AstTag::OpArrayIndex:
	{
		AstNode* const lhs_node = first_child_of(expr);

		const Code2Offset lhs = code2_from_expr(core, lhs_node, block, loc);

		AstNode* const rhs_node = next_sibling_of(lhs_node);

		const Code2Offset rhs = code2_from_expr(core, rhs_node, block);

		Code2AttBinaryOp* const dst = emit_code2<Code2AttBinaryOp>(core, sizeof(Code2AttBinaryOp), Code2Op::Index);
		dst->lhs = lhs;
		dst->rhs = rhs;

		return &dst->header;
	}

	case AstTag::INVALID:
	case AstTag::File:
	case AstTag::Where:
	case AstTag::Requires:
	case AstTag::Ensures:
	case AstTag::Case:
	case AstTag::ParameterList:
	case AstTag::TraitParameterList:
	case AstTag::ImpliedMember:
	case AstTag::UOpTypeVarArgs:
	case AstTag::MAX:
		; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

static void code2_from_local_definition(CoreData* core, AstNode* expr, Code2Block* block, u32 definition_index) noexcept
{
	ASSERT_OR_IGNORE(expr->tag == AstTag::Definition);

	ASSERT_OR_IGNORE(definition_index <= UINT16_MAX);

	const DefinitionInfo info = get_definition_info(expr);

	ASSERT_OR_IGNORE(is_some(info.value));

	const Maybe<Code2Offset> type = is_some(info.type)
		? some(code2_from_expr(core, get(info.type), block))
		: none<Code2Offset>();

	const Code2Offset value = code2_from_expr(core, get(info.value), block);

	if (is_some(type))
	{
		Code2AttBinaryOp* const define = emit_code2<Code2AttBinaryOp>(core, sizeof(Code2AttBinaryOp), Code2Op::DefineTyped);
		define->header.define.is_mut = has_flag(expr, AstFlag::Definition_IsMut);
		define->header.define.has_self_reference = has_flag(expr, AstFlag::Definition_HasSelfReference);
		define->header.define.definition_index = static_cast<u16>(definition_index);
		define->lhs = get(type);
		define->rhs = value;
	}
	else
	{
		Code2AttUnaryOp* const define = emit_code2<Code2AttUnaryOp>(core, sizeof(Code2AttUnaryOp), Code2Op::DefineUntyped);
		define->header.define.is_mut = has_flag(expr, AstFlag::Definition_IsMut);
		define->header.define.has_self_reference = has_flag(expr, AstFlag::Definition_HasSelfReference);
		define->header.define.definition_index = static_cast<u16>(definition_index);
		define->operand = value;
	}
}

static Code2Offset code2_from_fixup(CoreData* core, Maybe<AstNode*> src, Code2Block* block, Code2FixupTag tag) noexcept
{
	switch (tag)
	{
	case Code2FixupTag::Expr:
	case Code2FixupTag::FunctionBody:
	{
		if (get(src)->tag != AstTag::Block)
			return code2_from_expr(core, get(src), block, LocationMode::Value, true);

		AstDirectChildIterator it = direct_children_of(get(src));

		if (!has_next(&it))
		{
			Code2* const void_value = emit_code2<Code2>(core, sizeof(Code2), Code2Op::Void);

			return offset_from_code2(block, void_value);
		}

		u32 definition_index = 0;

		while (true)
		{
			AstNode* const statement = next(&it);

			const bool is_last = !has_next(&it);

			if (statement->tag == AstTag::Definition)
			{
				ASSERT_OR_IGNORE(definition_index <= UINT16_MAX);

				if (is_last)
					TODO("Decide what to do with definitions that are the last statement in a block.");

				code2_from_local_definition(core, statement, block, definition_index);

				definition_index += 1;
			}
			else
			{
				const Code2Offset offset = code2_from_expr(core, statement, block);

				if (is_last)
					return offset;

				Code2AttUnaryOp* const consume_void = emit_code2<Code2AttUnaryOp>(core, sizeof(Code2AttUnaryOp), Code2Op::ConsumeVoid);
				consume_void->operand = offset;
			}
		}
	}

	case Code2FixupTag::Where:
	{
		const AstTag src_tag = get(src)->tag;

		AstNode* where;

		if (src_tag == AstTag::If)
		{
			const IfInfo info = get_if_info(get(src));

			where = get(info.where);
		}
		else if (src_tag == AstTag::For)
		{
			const ForInfo info = get_for_info(get(src));

			where = get(info.where);
		}
		else if (src_tag == AstTag::ForEach)
		{
			const ForEachInfo info = get_foreach_info(get(src));

			where = get(info.where);
		}
		else
		{
			ASSERT_OR_IGNORE(src_tag == AstTag::Switch);

			const SwitchInfo info = get_switch_info(get(src));

			where = get(info.where);
		}

		u32 definition_index = 0;

		AstDirectChildIterator it = direct_children_of(where);

		while (has_next(&it))
		{
			AstNode* const definition = next(&it);

			code2_from_local_definition(core, definition, block, definition_index);

			definition_index += 1;
		}

		return code2_from_expr(core, get(src), block, LocationMode::Value, true);
	}

	case Code2FixupTag::LoopBody:
	{
		const ForInfo info = get_for_info(get(src));

		const Code2Offset condition = code2_from_expr(core, info.condition, block);

		Code2AttIf* const conditional_leave = emit_code2<Code2AttIf>(core, sizeof(Code2AttIf), Code2Op::If);
		conditional_leave->header.if_.is_type_eval = false;
		conditional_leave->condition = condition;

		register_fixup_empty(core, &conditional_leave->consequent);

		if (is_some(info.finally))
			register_fixup_expr(core, &conditional_leave->alternative, get(info.finally), 1);
		else
			register_fixup_empty(core, &conditional_leave->alternative, 1);

		const Code2Offset body = code2_from_expr(core, info.body, block);

		Code2AttUnaryOp* const body_consume_void = emit_code2<Code2AttUnaryOp>(core, sizeof(Code2AttUnaryOp), Code2Op::ConsumeVoid);
		body_consume_void->operand = body;

		if (is_some(info.step))
		{
			const Code2Offset step = code2_from_expr(core, get(info.step), block);

			Code2AttUnaryOp* const step_consume_void = emit_code2<Code2AttUnaryOp>(core, sizeof(Code2AttUnaryOp), Code2Op::ConsumeVoid);
			step_consume_void->operand = step;
		}

		Code2* const repeat = emit_code2<Code2>(core, sizeof(Code2), Code2Op::Repeat);
		repeat->repeat.out = 0;

		return offset_from_code2(block, repeat);
	}

	case Code2FixupTag::Empty:
	{
		Code2* const dst = emit_code2<Code2>(core, sizeof(Code2), Code2Op::Void);

		return offset_from_code2(block, dst);
	}

	case Code2FixupTag::INVALID:
		; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

static void code2_from_fixups(CoreData* core) noexcept
{
	while (core->code2.fixups.used() != 0)
	{
		const Code2Fixup fixup = core->code2.fixups.top();

		core->code2.fixups.pop_by(1);

		const Maybe<AstNode*> src = is_some(fixup.src)
			? some(ast_node_from_id(core, get(fixup.src)))
			: none<AstNode*>();

		Code2Block* const block = emit_block_header(core);

		const Code2Offset leave_value = code2_from_fixup(core, src, block, fixup.tag);

		*fixup.dst = close_block_header(core, block, 0, leave_value, fixup.leave_out);
	}
}

bool code2_from_top_level_definition(CoreData* core, AstNode* definition, Maybe<Code2Id>* out_type, Code2Id* out_value) noexcept
{
	const DefinitionInfo info = get_definition_info(definition);

	ASSERT_OR_IGNORE(is_some(info.value));

	if (setjmp(core->code2.error_jmp_buf) == 0)
	{
		Maybe<Code2Id> type;

		if (is_some(info.type))
		{
			Code2Block* const type_block = emit_block_header(core);

			const Code2Offset type_leave = code2_from_expr(core, get(info.type), type_block);

			type = some(close_block_header(core, type_block, 0, type_leave, 0));
		}
		else
		{
			type = none<Code2Id>();
		}

		Code2Block* const value_block = emit_block_header(core);

		const Code2Offset value_leave = code2_from_expr(core, get(info.value), value_block);

		const Code2Id value = close_block_header(core, value_block, 0, value_leave, 0);

		code2_from_fixups(core);

		*out_type = type;
		*out_value = value;

		return true;
	}
	else
	{
		return false;
	}
}





Code2Id id_from_code2_block(CoreData* core, const Code2Block* block) noexcept
{
	ASSERT_OR_IGNORE(block >= reinterpret_cast<Code2Block*>(core->code2.codes.begin()));

	ASSERT_OR_IGNORE(block < reinterpret_cast<Code2Block*>(core->code2.codes.end()));

	ASSERT_OR_IGNORE(block > reinterpret_cast<Code2Block*>(core));

	const u64 offset = reinterpret_cast<const u32*>(block) - reinterpret_cast<const u32*>(core);

	ASSERT_OR_IGNORE(offset > 0 && offset <= UINT32_MAX);

	return static_cast<Code2Id>(offset);
}

Code2Block* code2_block_from_id(CoreData* core, Code2Id id) noexcept
{
	ASSERT_OR_IGNORE(id != Code2Id::INVALID);

	Code2Block* const block = reinterpret_cast<Code2Block*>(reinterpret_cast<u32*>(core) + static_cast<u32>(id));

	ASSERT_OR_IGNORE(block >= reinterpret_cast<Code2Block*>(core->code2.codes.begin()));

	ASSERT_OR_IGNORE(block < reinterpret_cast<Code2Block*>(core->code2.codes.end()));

	ASSERT_OR_IGNORE(block > reinterpret_cast<Code2Block*>(core));

	return block;
}



Code2Offset offset_from_code2(const Code2Block* block, const Code2* data) noexcept
{
	ASSERT_OR_IGNORE(reinterpret_cast<const u32*>(block) < reinterpret_cast<const u32*>(data));

	return static_cast<Code2Offset>(reinterpret_cast<const u32*>(data) - reinterpret_cast<const u32*>(block));
}

Code2* code2_from_offset(Code2Block* block, Code2Offset offset) noexcept
{
	ASSERT_OR_IGNORE(offset != Code2Offset::INVALID);

	return reinterpret_cast<Code2*>(reinterpret_cast<u32*>(block) + static_cast<u32>(offset));
}

const Code2* code2_from_offset(const Code2Block* block, Code2Offset offset) noexcept
{
	return code2_from_offset(const_cast<Code2Block*>(block), offset);
}



Code2* code2_block_begin(Code2Block* block) noexcept
{
	return reinterpret_cast<Code2*>(block + 1);
}

const Code2* code2_block_begin(const Code2Block* block) noexcept
{
	return code2_block_begin(const_cast<Code2Block*>(block));
}

Code2* code2_block_end(Code2Block* block) noexcept
{
	return reinterpret_cast<Code2*>(reinterpret_cast<u32*>(block) + block->size);
}

const Code2* code2_block_end(const Code2Block* block) noexcept
{
	return code2_block_end(const_cast<Code2Block*>(block));
}





const char8* tag_name(Code2Op op) noexcept
{
	static constexpr const char8* TAG_NAMES[] = { 
		"[Unknown]",
		"Leave",
		"Repeat",
		"Jump",
		"Builtin",
		"Composite",
		"Array",
		"Definition",
		"MakeLocation",
		"DefineUntyped",
		"DefineTyped",
		"ConsumeVoid",
		"Void",
		"If",
		"Loop",
		"Func",
		"Signature",
		"Trait",
		"Impl",
		"ImplTraitCall",
		"Self",
		"Unreachable",
		"Undefined",
		"LocalValue",
		"LocalLocation",
		"GlobalValue",
		"GlobalLocation",
		"ClosedValue",
		"ClosedLocation",
		"TraitArgumentValue",
		"TraitArgumentLocation",
		"Slice",
		"Integer16",
		"Integer64",
		"Float",
		"String",
		"Call",
		"TypeTailArray",
		"TypeSlice",
		"TypePtr",
		"Eval",
		"Defer",
		"Distinct",
		"Address",
		"Dereference",
		"BitNot",
		"LogicalNot",
		"Negate",
		"UnaryPlus",
		"BinaryArithmetic",
		"BitShift",
		"TypeArray",
		"Index",
		"LogicalAnd",
		"LogicalOr",
		"MemberValue",
		"MemberLocation",
		"Compare",
		"Store",
		"DupAndLoadLocation",
	};

	u8 ordinal = static_cast<u8>(op);

	if (ordinal >= array_count(TAG_NAMES))
		ordinal = 0;

	return TAG_NAMES[ordinal];
}

const char8* tag_name(Code2AttBinaryArithmetic_Kind kind) noexcept
{
	static constexpr const char8* TAG_NAMES[] = {
		"[Unknown]",
		"Add",
		"Sub",
		"Mul",
		"Div",
		"AddTC",
		"SubTC",
		"MulTC",
		"Mod",
		"BitAnd",
		"BitOr",
		"BitXor",
	};

	u8 ordinal = static_cast<u8>(kind) + 1;

	if (ordinal >= array_count(TAG_NAMES))
		ordinal = 0;

	return TAG_NAMES[ordinal];
}

const char8* tag_name(Code2AttBitShift_Kind kind) noexcept
{
	static constexpr const char8* TAG_NAMES[] = {
		"[Unknown]",
		"Left",
		"Right",
	};

	u8 ordinal = static_cast<u8>(kind) + 1;

	if (ordinal >= array_count(TAG_NAMES))
		ordinal = 0;

	return TAG_NAMES[ordinal];
}

const char8* tag_name(Code2AttCompare_Kind kind) noexcept
{
	static constexpr const char8* TAG_NAMES[] = {
		"[Unknown]",
		"LessThan",
		"GreaterThan",
		"LessThanOrEqual",
		"GreaterThanOrEqual",
		"NotEqual",
		"Equal",
	};

	u8 ordinal = static_cast<u8>(kind) + 1;

	if (ordinal >= array_count(TAG_NAMES))
		ordinal = 0;

	return TAG_NAMES[ordinal];
}



bool code2_validate_config([[maybe_unused]] const Config* config, [[maybe_unused]] PrintSink sink) noexcept
{
	return true;
}

MemoryRequirements code2_memory_requirements([[maybe_unused]] const Config* config) noexcept
{
	MemoryRequirements reqs;
	reqs.count = 2;
	reqs.ranges[0].size = CODES_RESERVE_SIZE;
	reqs.ranges[0].max_offset = 1 << 26;
	reqs.ranges[1].size = FIXUPS_RESERVE_SIZE;
	reqs.ranges[1].max_offset = UINT64_MAX;

	return reqs;
}

void code2_init(CoreData* core, MemoryAllocation allocation) noexcept
{
	ASSERT_OR_IGNORE(allocation.ranges[0].count() == CODES_RESERVE_SIZE);

	ASSERT_OR_IGNORE(allocation.ranges[1].count() == FIXUPS_RESERVE_SIZE);

	core->code2.codes.init(allocation.ranges[0], CODES_COMMIT_INCREMENT_COUNT);

	core->code2.fixups.init(allocation.ranges[1], FIXUPS_COMMIT_INCREMENT_COUNT);
}
