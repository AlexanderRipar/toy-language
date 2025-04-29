#include "pass_data.hpp"

#include "ast_helper.hpp"

struct Interpreter
{
	SourceReader* reader;

	Parser* parser;

	TypePool* types;

	AstPool* asts;

	IdentifierPool* identifiers;

	ErrorSink* errors;

	ReservedVec<u64> value_stack;

	ReservedVec<u32> value_stack_inds;

	ReservedVec<u64> activation_records;

	ReservedVec<u32> activation_record_inds;

	TypeId prelude_type_id;

	s32 context_top;

	TypeId contexts[256];

	TypeId builtin_type_ids[static_cast<u8>(Builtin::MAX) - 1];
};

struct FuncTypeParamHelper
{
	IdentifierId name;

	TypeId type;
};



static void push_typechecker_context(Interpreter* interp, TypeId context, bool is_root) noexcept
{
	ASSERT_OR_IGNORE(context.rep != INVALID_TYPE_ID.rep);

	u32 top = interp->context_top + 1;

	if (top + (is_root ? 2 : 0) >= array_count(interp->contexts))
		panic("Maximum active interpreter context limit of %u exceeded.\n", array_count(interp->contexts));

	if (is_root)
	{
		interp->contexts[top] = INVALID_TYPE_ID;

		interp->contexts[top + 1] = interp->prelude_type_id;

		top += 2;
	}

	interp->contexts[top] = context;

	interp->context_top = top;
}

static void pop_typechecker_context(Interpreter* interp, bool is_root) noexcept
{
	const s32 to_pop = is_root ? 3 : 1;

	ASSERT_OR_IGNORE(interp->context_top - to_pop >= -1);

	ASSERT_OR_IGNORE(!is_root || interp->contexts[interp->context_top - to_pop + 1].rep == INVALID_TYPE_ID.rep);

	interp->context_top -= to_pop;
}

static TypecheckerResumptionId get_typechecker_resumption(Interpreter* interp) noexcept
{
	// This should never be 0, as the first context must be a root, thus also
	// pushing a dummy `INVALID_TYPE_ID`, bumping `context_top` from -1 to 1.
	ASSERT_OR_IGNORE(interp->context_top > 0);

	return TypecheckerResumptionId{ static_cast<u32>(interp->context_top) };
}

static void apply_typechecker_resumption(Interpreter* interp, TypecheckerResumptionId resumption_id) noexcept
{
	const s32 resumption_top = static_cast<s32>(resumption_id.rep);

	ASSERT_OR_IGNORE(resumption_top <= interp->context_top);

	s32 resumption_bottom = resumption_top - 1;

	while (resumption_bottom >= -1 && interp->contexts[resumption_bottom].rep != INVALID_TYPE_ID.rep)
		resumption_bottom -= 1;

	ASSERT_OR_IGNORE(resumption_bottom >= 0);

	const s32 resumption_count = 1 + resumption_top - resumption_bottom;

	if (interp->context_top + resumption_count >= array_count(interp->contexts))
		panic("Maximum active interpreter context limit of %u exceeded.\n", array_count(interp->contexts));

	memcpy(interp->contexts + interp->context_top + 1, interp->contexts + resumption_bottom, resumption_count * sizeof(*interp->contexts));

	interp->context_top += resumption_count;
}

static void release_typechecker_resumption(Interpreter* interp) noexcept
{
	s32 new_top = interp->context_top;

	while (new_top >= 0 && interp->contexts[new_top].rep != INVALID_TYPE_ID.rep)
		new_top -= 1;

	ASSERT_OR_IGNORE(new_top >= 1);

	interp->context_top = new_top - 1;
}





static void* lookup_identifier_value(Interpreter* interp, IdentifierId identifier_id) noexcept
{
	(void) interp;

	(void) identifier_id;

	TODO("Implement.");
}

static MemberInfo lookup_identifier_definition(Interpreter* interp, IdentifierId identifier_id, SourceId lookup_source) noexcept
{
	s32 index = interp->context_top;

	ASSERT_OR_IGNORE(index >= 0);

	while (true)
	{
		const TypeId context = interp->contexts[index];

		if (context.rep == INVALID_TYPE_ID.rep)
			break;

		MemberInfo info;

		if (type_member_info_by_name(interp->types, context, identifier_id, &info))
			return info;

		index -= 1;
	}

	const Range<char8> name = identifier_name_from_id(interp->identifiers, identifier_id);

	source_error(interp->errors, lookup_source, "Could not find definition for identifier %.*s\n", static_cast<s32>(name.count()), name.begin());
}

static MemberInit member_init_from_definition(Interpreter* interp, AstNode* definition, DefinitionInfo info, u64 offset) noexcept
{
	ASSERT_OR_IGNORE(definition->tag == AstTag::Definition);

	const bool has_pending_type = definition->type_id.rep == INVALID_TYPE_ID.rep || definition->type_id.rep == CHECKING_TYPE_ID.rep;

	MemberInit member;
	member.name = attachment_of<AstDefinitionData>(definition)->identifier_id;
	member.source = definition->source_id;
	member.is_global = has_flag(definition, AstFlag::Definition_IsGlobal);
	member.is_pub = has_flag(definition, AstFlag::Definition_IsPub);
	member.is_use = has_flag(definition, AstFlag::Definition_IsUse);
	member.has_pending_type = has_pending_type;
	member.offset_or_global_value = offset;

	if (has_pending_type)
	{
		member.type.resumption_id = get_typechecker_resumption(interp);
		member.opt_type_node_id = is_some(info.type) ? id_from_ast_node(interp->asts, get_ptr(info.type)) : INVALID_AST_NODE_ID;
		member.opt_value_node_id = is_some(info.value) ? id_from_ast_node(interp->asts, get_ptr(info.value)) : INVALID_AST_NODE_ID;
	}
	else
	{
		member.type.id = definition->type_id;
	}

	return member;
}



static void* alloc_stack_value(Interpreter* interp, u32 bytes, u32 align) noexcept
{
	ASSERT_OR_IGNORE(is_pow2(align));

	const u32 old_top = interp->value_stack.used();

	interp->value_stack_inds.append(old_top);

	if (align > 8)
		interp->value_stack.pad_to_alignment(align);

	return interp->value_stack.reserve_padded(bytes);
}

static void pop_stack_value(Interpreter* interp) noexcept
{
	const u32 new_top = interp->value_stack_inds.top();

	interp->value_stack.pop_to(new_top);

	interp->value_stack_inds.pop_by(1);
}



static void* evaluate_expr(Interpreter* interp, AstNode* node) noexcept
{
	ASSERT_OR_IGNORE(node->type_id.rep != INVALID_TYPE_ID.rep && node->type_id.rep != CHECKING_TYPE_ID.rep);

	switch (node->tag)
	{
	case AstTag::If:
	{
		IfInfo info = get_if_info(node);

		ASSERT_OR_IGNORE(is_some(info.alternative) || type_tag_from_id(interp->types, node->type_id) == TypeTag::Void);

		const bool condition_value = *static_cast<bool*>(evaluate_expr(interp, info.condition));

		pop_stack_value(interp);

		if (condition_value)
			return evaluate_expr(interp, info.consequent);
		else if (is_some(info.alternative))
			return evaluate_expr(interp, get_ptr(info.alternative));
		else
			return alloc_stack_value(interp, 0, 1); // Void
	}

	case AstTag::LitInteger:
	{
		CompIntegerValue* const stack_value = static_cast<CompIntegerValue*>(alloc_stack_value(interp, sizeof(CompIntegerValue), alignof(CompIntegerValue)));

		*stack_value = attachment_of<AstLitIntegerData>(node)->value;

		return stack_value;
	}

	case AstTag::LitFloat:
	{
		CompFloatValue* const stack_value = static_cast<CompFloatValue*>(alloc_stack_value(interp, sizeof(CompFloatValue), alignof(CompFloatValue)));

		*stack_value = attachment_of<AstLitFloatData>(node)->value;

		return stack_value;
	}

	case AstTag::LitChar:
	{
		CompIntegerValue* const stack_value = static_cast<CompIntegerValue*>(alloc_stack_value(interp, sizeof(CompIntegerValue), alignof(CompIntegerValue)));

		*stack_value = comp_integer_from_u64(attachment_of<AstLitCharData>(node)->codepoint);

		return stack_value;
	}

	case AstTag::Identifer:
	{
		void* const identifier_value = lookup_identifier_value(interp, attachment_of<AstIdentifierData>(node)->identifier_id);

		u64 size;

		u32 align;

		if (is_assignable(node->type_id))
		{
			size = 8;

			align = 8;
		}
		else
		{
			const TypeMetrics metrics = type_metrics_from_id(interp->types, node->type_id);

			size = metrics.size;

			align = metrics.align;
		}

		if (size > UINT32_MAX)
			source_error(interp->errors, node->source_id, "Size %" PRIu64 " of type exceeds maximum interpreter-stack-allocatable size %u.\n", UINT32_MAX);

		void* const stack_value = alloc_stack_value(interp, static_cast<u32>(size), align);

		if (is_assignable(node->type_id))
		{
			memcpy(stack_value, &identifier_value, 8);
		}
		else
		{
			TODO("Implement");
		}

		return stack_value;
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
	case AstTag::For:
	case AstTag::ForEach:
	case AstTag::Switch:
	case AstTag::Case:
	case AstTag::Func:
	case AstTag::Trait:
	case AstTag::Impl:
	case AstTag::Catch:
	case AstTag::LitString:
	case AstTag::Return:
	case AstTag::Leave:
	case AstTag::Yield:
	case AstTag::ParameterList:
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
		panic("Evaluation of AST node type %s is not yet implemented.\n", tag_name(node->tag));

	case AstTag::File:
	default:
		ASSERT_UNREACHABLE;
	}
}

static TypeId typecheck_expr(Interpreter* interp, AstNode* node) noexcept;

static TypeId delayed_typecheck_member(Interpreter* interp, const MemberInfo* member) noexcept
{
	if (member->opt_type.rep != INVALID_TYPE_ID.rep)
		return member->opt_type;

	ASSERT_OR_IGNORE(member->opt_type_resumption_id.rep != INVALID_RESUMPTION_ID.rep);

	apply_typechecker_resumption(interp, member->opt_type_resumption_id);

	TypeId defined_type_id;

	if (member->opt_type_node_id != INVALID_AST_NODE_ID)
	{
		AstNode* const type = ast_node_from_id(interp->asts, member->opt_type_node_id);

		const TypeId type_type_id = typecheck_expr(interp, type);

		const TypeTag type_type_tag = type_tag_from_id(interp->types, type_type_id);

		if (type_type_tag != TypeTag::Type)
			source_error(interp->errors, type->source_id, "Explicit type annotation of definition must be of type `Type`.\n");

		defined_type_id = *static_cast<TypeId*>(evaluate_expr(interp, type));

		pop_stack_value(interp);

		set_incomplete_type_member_type_by_rank(interp->types, member->surrounding_type_id, member->rank, defined_type_id);

		if (member->opt_value_node_id != INVALID_AST_NODE_ID)
		{
			AstNode* const value = ast_node_from_id(interp->asts, member->opt_value_node_id);

			const TypeId value_type_id = typecheck_expr(interp, value);

			if (!type_can_implicitly_convert_from_to(interp->types, value_type_id, defined_type_id))
				source_error(interp->errors, value->source_id, "Definition value cannot be implicitly converted to type of explicit type annotation.\n");
		}
	}
	else
	{
		ASSERT_OR_IGNORE(member->opt_value_node_id != INVALID_AST_NODE_ID);

		AstNode* const value = ast_node_from_id(interp->asts, member->opt_value_node_id);

		defined_type_id = typecheck_expr(interp, value);

		set_incomplete_type_member_type_by_rank(interp->types, member->surrounding_type_id, member->rank, defined_type_id);
	}

	release_typechecker_resumption(interp);

	return defined_type_id;
}

static void typecheck_where(Interpreter* interp, AstNode* node) noexcept
{
	TODO("Implement");

	(void) interp;

	(void) node;
}

static TypeId typecheck_expr_impl(Interpreter* interp, AstNode* node) noexcept
{
	switch (node->tag)
	{
	case AstTag::CompositeInitializer:
	case AstTag::ArrayInitializer:
	case AstTag::Wildcard:
	case AstTag::Expects:
	case AstTag::Ensures:
	case AstTag::Definition:
	case AstTag::ForEach:
	case AstTag::Switch:
	case AstTag::Func:
	case AstTag::Trait:
	case AstTag::Impl:
	case AstTag::Catch:
	case AstTag::Return:
	case AstTag::Leave:
	case AstTag::Yield:
	case AstTag::UOpTry:
	case AstTag::UOpDefer:
	case AstTag::UOpImpliedMember:
		panic("Typechecking of AST node type %s is not yet implemented.\n", tag_name(node->tag));

	case AstTag::Builtin:
	{
		const Builtin builtin = static_cast<Builtin>(node->flags);

		if (builtin == Builtin::AddTypeMember || builtin == Builtin::Offsetof)
			panic("Typechecking for builtin %s not yet supported.\n", tag_name(builtin));

		const u8 ordinal = static_cast<u8>(builtin);

		ASSERT_OR_IGNORE(ordinal < array_count(interp->builtin_type_ids) && interp->builtin_type_ids[ordinal].rep != INVALID_TYPE_ID.rep);

		return interp->builtin_type_ids[ordinal];
	}

	case AstTag::Block:
	{
		ASSERT_OR_IGNORE(attachment_of<AstBlockData>(node)->scope_type_id.rep == INVALID_TYPE_ID.rep);

		const TypeId scope_type_id = create_open_type(interp->types, node->source_id);

		attachment_of<AstBlockData>(node)->scope_type_id = scope_type_id;

		push_typechecker_context(interp, scope_type_id, false);

		AstDirectChildIterator it = direct_children_of(node);

		u64 offset = 0;

		u32 max_align = 1;

		TypeId result_type_id = INVALID_TYPE_ID;

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

					const TypeId type_type_id = typecheck_expr(interp, type);

					const TypeTag type_type_tag = type_tag_from_id(interp->types, type_type_id);

					if (type_type_tag != TypeTag::Type)
						source_error(interp->errors, type->source_id, "Explicit type annotation of definition must be of type `Type`.\n");

					defined_type_id = *static_cast<TypeId*>(evaluate_expr(interp, type));

					pop_stack_value(interp);
				}
				else
				{
					ASSERT_OR_IGNORE(is_some(info.value));

					AstNode* const value = get_ptr(info.value);

					defined_type_id = typecheck_expr(interp, value);
				}

				child->type_id = defined_type_id;

				const TypeMetrics metrics = type_metrics_from_id(interp->types, defined_type_id);

				offset = next_multiple(offset, static_cast<u64>(metrics.align));

				MemberInit member = member_init_from_definition(interp, child, info, offset);

				offset += metrics.size;

				if (metrics.align > max_align)
					max_align = metrics.align;

				add_open_type_member(interp->types, scope_type_id, member);

				if (is_some(info.type) && is_some(info.value))
				{
					AstNode* const value = get_ptr(info.value);

					const TypeId value_type_id = typecheck_expr(interp, value);

					if (!type_can_implicitly_convert_from_to(interp->types, value_type_id, defined_type_id))
						source_error(interp->errors, value->source_id, "Definition value cannot be implicitly converted to type of explicit type annotation.\n");
				}

				if (!has_next_sibling(child))
					result_type_id = defined_type_id;
			}
			else
			{
				const TypeId expr_type_id = typecheck_expr(interp, child);

				if (!has_next_sibling(node))
				{
					result_type_id = expr_type_id;
				}
				else
				{
					const TypeTag expr_type_tag = type_tag_from_id(interp->types, expr_type_id);

					if (expr_type_tag != TypeTag::Void && expr_type_tag != TypeTag::Definition)
						source_error(interp->errors, child->source_id, "Expression in non-terminal position in block must be a definition or of void type.\n");
				}
			}
		}

		pop_typechecker_context(interp, false);

		close_open_type(interp->types, scope_type_id, offset, max_align, next_multiple(offset, static_cast<u64>(max_align)));

		// Empty blocks are of type `Void`.
		if (result_type_id.rep == INVALID_TYPE_ID.rep)
			result_type_id = primitive_type(interp->types, TypeTag::Void, {});

		return result_type_id;
	}

	case AstTag::If:
	{
		IfInfo info = get_if_info(node);

		const TypeId condition_type_id = typecheck_expr(interp, info.condition);

		const TypeTag condition_type_tag = type_tag_from_id(interp->types, condition_type_id);

		if (condition_type_tag != TypeTag::Boolean)
			source_error(interp->errors, info.condition->source_id, "Condition of `if` expression must be of boolean type.\n");

		if (is_some(info.where))
			typecheck_where(interp, get_ptr(info.where));

		const TypeId consequent_type_id = typecheck_expr(interp, info.consequent);

		if (is_some(info.alternative))
		{
			AstNode* const alternative = get_ptr(info.alternative);

			const TypeId alternative_type_id = typecheck_expr(interp, alternative);

			const TypeId common_type_id = common_type(interp->types, consequent_type_id, alternative_type_id);

			if (common_type_id.rep == INVALID_TYPE_ID.rep)
				source_error(interp->errors, node->source_id, "Consequent and alternative of `if` have incompatible types.\n");

			return common_type_id;
		}
		else
		{
			const TypeTag consequent_type_tag = type_tag_from_id(interp->types, consequent_type_id);

			if (consequent_type_tag != TypeTag::Void)
				source_error(interp->errors, node->source_id, "Consequent of `if` must be of void type if no alternative is provided.\n");

			return consequent_type_id;
		}
	}

	case AstTag::For:
	{
		ForInfo info = get_for_info(node);

		const TypeId condition_type_id = typecheck_expr(interp, info.condition);

		const TypeTag condition_type_tag = type_tag_from_id(interp->types, condition_type_id);

		if (condition_type_tag != TypeTag::Boolean)
			source_error(interp->errors, info.condition->source_id, "Condition of `for` must be of boolean type.\n");

		if (is_some(info.step))
		{
			AstNode* const step = get_ptr(info.step);

			const TypeId step_type_id = typecheck_expr(interp, step);

			const TypeTag step_type_tag = type_tag_from_id(interp->types, step_type_id);

			if (step_type_tag != TypeTag::Void)
				source_error(interp->errors, step->source_id, "Step of `for` must be of void type.\n");
		}

		if (is_some(info.where))
		typecheck_where(interp, get_ptr(info.where));

		const TypeId body_type_id = typecheck_expr(interp, info.body);

		if (is_some(info.finally))
		{
			AstNode* const finally = get_ptr(info.finally);

			const TypeId finally_type_id = typecheck_expr(interp, finally);

			const TypeId common_type_id = common_type(interp->types, body_type_id, finally_type_id);

			if (common_type_id.rep == INVALID_TYPE_ID.rep)
				source_error(interp->errors, node->source_id, "Body and finally of `for` have incompatible types.\n");

			return common_type_id;
		}
		else
		{
			const TypeTag body_type_tag = type_tag_from_id(interp->types, body_type_id);

			if (body_type_tag != TypeTag::Void)
				source_error(interp->errors, node->source_id, "Consequent of `if` must be of void type if no alternative is provided.\n");

			return body_type_id;
		}
	}

	case AstTag::Identifer:
	{
		const IdentifierId identifier_id = attachment_of<AstIdentifierData>(node)->identifier_id;

		MemberInfo member = lookup_identifier_definition(interp, identifier_id, node->source_id);

		return delayed_typecheck_member(interp, &member);
	}

	case AstTag::LitInteger:
	{
		return primitive_type(interp->types, TypeTag::CompString, {});
	}

	case AstTag::LitFloat:
	{
		return primitive_type(interp->types, TypeTag::Float, {});
	}

	case AstTag::LitChar:
	{
		return primitive_type(interp->types, TypeTag::CompInteger, {});
	}

	case AstTag::LitString:
	{
		return primitive_type(interp->types, TypeTag::CompString, {});
	}

	case AstTag::Call:
	{
		// TODO: Variadics

		AstNode* const callee = first_child_of(node);

		const TypeId callee_type_id = typecheck_expr(interp, callee);

		const TypeTag callee_type_tag = type_tag_from_id(interp->types, callee_type_id);

		if (callee_type_tag != TypeTag::Func && callee_type_tag != TypeTag::Builtin)
			source_error(interp->errors, callee->source_id, "Left-hand-side of call operator must be of function or builtin type.\n");

		const FuncType* const func_type = static_cast<const FuncType*>(primitive_type_structure(interp->types, callee_type_id));

		const TypeId signature_type_id = func_type->signature_type_id;

		bool expect_named = false;

		u64 seen_argument_mask = 0;

		u16 seen_argument_count = 0;

		AstNode* argument = callee;

		while (has_next_sibling(argument))
		{
			argument = next_sibling_of(argument);

			MemberInfo argument_member;

			TypeId argument_type_id;

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

			ASSERT_OR_IGNORE(argument_member.opt_type.rep != INVALID_TYPE_ID.rep);

			if (!type_can_implicitly_convert_from_to(interp->types, argument_type_id, argument_member.opt_type))
				source_error(interp->errors, argument->source_id, "Cannot implicitly convert to expected argument type.\n");

			argument_type_id = typecheck_expr(interp, argument);
		}

		if (!expect_named)
			seen_argument_mask = (static_cast<u64>(1) << seen_argument_count) - 1;

		// TODO: Check missing arguments have a default value in the callee's signature.

		return func_type->return_type_id;
	}

	case AstTag::UOpTypeTailArray:
	{
		AstNode* const operand = first_child_of(node);

		const TypeId operand_type_id = typecheck_expr(interp, operand);

		const TypeTag operand_type_tag = type_tag_from_id(interp->types, operand_type_id);

		if (operand_type_tag != TypeTag::Type)
			source_error(interp->errors, operand->source_id, "Operand of `%s` must be of type `Type`.\n", tag_name(node->tag));

		ReferenceType tail_array_type{};
		tail_array_type.is_multi = false;
		tail_array_type.is_opt = false;
		tail_array_type.referenced_type_id = set_assignability(operand_type_id, true);

		return primitive_type(interp->types, TypeTag::Slice, range::from_object_bytes(&tail_array_type));
	}

	case AstTag::UOpTypeSlice:
	{
		AstNode* const operand = first_child_of(node);

		const TypeId operand_type_id = typecheck_expr(interp, operand);

		const TypeTag operand_type_tag = type_tag_from_id(interp->types, operand_type_id);

		if (operand_type_tag != TypeTag::Type)
			source_error(interp->errors, operand->source_id, "Operand of `%s` must be of type `Type`.\n", tag_name(node->tag));

		ReferenceType slice_type{};
		slice_type.is_multi = false;
		slice_type.is_opt = false;
		slice_type.referenced_type_id = set_assignability(operand_type_id, has_flag(node, AstFlag::Type_IsMut));

		return primitive_type(interp->types, TypeTag::Slice, range::from_object_bytes(&slice_type));
	}

	case AstTag::UOpEval:
	{
		AstNode* const operand = first_child_of(node);

		return typecheck_expr(interp, operand);
	}

	case AstTag::UOpDistinct:
	{
		AstNode* const operand = first_child_of(node);

		const TypeId operand_type_id = typecheck_expr(interp, operand);

		const TypeTag operand_type_tag = type_tag_from_id(interp->types, operand_type_id);

		if (operand_type_tag != TypeTag::Type)
			source_error(interp->errors, operand->source_id, "Operand of `%s` must be of type `Type`.\n", tag_name(node->tag));

		return alias_type(interp->types, operand_type_id, true, operand->source_id, INVALID_IDENTIFIER_ID);
	}

	case AstTag::UOpAddr:
	{
		AstNode* const operand = first_child_of(node);

		const TypeId operand_type_id = typecheck_expr(interp, operand);

		ReferenceType ptr_type{};
		ptr_type.is_multi = false;
		ptr_type.is_opt = false;
		ptr_type.referenced_type_id = operand_type_id;

		return primitive_type(interp->types, TypeTag::Ptr, range::from_object_bytes(&ptr_type));
	}

	case AstTag::UOpDeref:
	{
		AstNode* const operand = first_child_of(node);

		const TypeId operand_type_id = typecheck_expr(interp, operand);

		const TypeTag operand_type_tag = type_tag_from_id(interp->types, operand_type_id);

		if (operand_type_tag != TypeTag::Ptr)
			source_error(interp->errors, operand->source_id, "Operand of `%s` must be of pointer type.\n", tag_name(node->tag));

		const ReferenceType* const reference = static_cast<const ReferenceType*>(primitive_type_structure(interp->types, operand_type_id));

		return mask_assignability(reference->referenced_type_id, is_assignable(operand_type_id));
	}

	case AstTag::UOpBitNot:
	{
		AstNode* const operand = first_child_of(node);

		const TypeId operand_type_id = typecheck_expr(interp, operand);

		const TypeTag operand_type_tag = type_tag_from_id(interp->types, operand_type_id);

		if (operand_type_tag != TypeTag::Integer && operand_type_tag != TypeTag::CompInteger)
			source_error(interp->errors, operand->source_id, "Operand of `%s` must be of integral type.\n", tag_name(node->tag));

		return set_assignability(operand_type_id, false);
	}

	case AstTag::UOpLogNot:
	{
		AstNode* const operand = first_child_of(node);

		const TypeId operand_type_id = typecheck_expr(interp, operand);

		const TypeTag operand_type_tag = type_tag_from_id(interp->types, operand_type_id);

		if (operand_type_tag != TypeTag::Boolean)
			source_error(interp->errors, operand->source_id, "Operand of `%s` must be of boolean type.\n", tag_name(node->tag));

		return set_assignability(operand_type_id, false);
	}

	case AstTag::UOpTypeVar:
	{
		AstNode* const operand = first_child_of(node);

		const TypeId operand_type_id = typecheck_expr(interp, operand);

		const TypeTag operand_type_tag = type_tag_from_id(interp->types, operand_type_id);

		if (operand_type_tag != TypeTag::Type)
			source_error(interp->errors, operand->source_id, "Operand of `%s` must be of type `Type`.\n", tag_name(node->tag));

		ReferenceType variadic_type{};
		variadic_type.is_multi = false;
		variadic_type.is_opt = false;
		variadic_type.referenced_type_id = set_assignability(operand_type_id, false);

		return primitive_type(interp->types, TypeTag::Variadic, range::from_object_bytes(&variadic_type));
	}

	case AstTag::UOpTypePtr:
	case AstTag::UOpTypeOptPtr:
	case AstTag::UOpTypeMultiPtr:
	case AstTag::UOpTypeOptMultiPtr:
	{
		AstNode* const operand = first_child_of(node);

		const TypeId operand_type_id = typecheck_expr(interp, operand);

		const TypeTag operand_type_tag = type_tag_from_id(interp->types, operand_type_id);

		if (operand_type_tag != TypeTag::Type)
			source_error(interp->errors, operand->source_id, "Operand of `%s` must be of type `Type`.\n", tag_name(node->tag));

		ReferenceType ptr_type{};
		ptr_type.is_multi = node->tag == AstTag::UOpTypeMultiPtr || node->tag == AstTag::UOpTypeOptMultiPtr;
		ptr_type.is_opt = node->tag == AstTag::UOpTypeOptPtr || node->tag == AstTag::UOpTypeOptMultiPtr;
		ptr_type.referenced_type_id = set_assignability(operand_type_id, has_flag(node, AstFlag::Type_IsMut));

		return primitive_type(interp->types, TypeTag::Ptr, range::from_object_bytes(&ptr_type));
	}

	case AstTag::UOpNegate:
	case AstTag::UOpPos:
	{
		AstNode* const operand = first_child_of(node);

		const TypeId operand_type_id = typecheck_expr(interp, node);

		const TypeTag operand_type_tag = type_tag_from_id(interp->types, operand_type_id);

		if (operand_type_tag != TypeTag::Integer && operand_type_tag != TypeTag::CompInteger && operand_type_tag != TypeTag::Float && operand_type_tag != TypeTag::CompFloat)
			source_error(interp->errors, operand->source_id, "Operand of unary `%s` must be of integral or floating point type.\n");

		if (node->tag == AstTag::UOpNegate && (operand_type_tag == TypeTag::Integer || operand_type_tag == TypeTag::CompInteger))
		{
			const NumericType* const integer_type = static_cast<const NumericType*>(primitive_type_structure(interp->types, operand_type_id));

			if (!integer_type->is_signed)
				source_error(interp->errors, operand->source_id, "Operand of unary `%s` must be signed.\n");
		}

		return set_assignability(operand_type_id, false);
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

		const TypeId lhs_type_id = typecheck_expr(interp, lhs);

		const TypeTag lhs_type_tag = type_tag_from_id(interp->types, lhs_type_id);

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

		const TypeId rhs_type_id = typecheck_expr(interp, rhs);

		const TypeTag rhs_type_tag = type_tag_from_id(interp->types, rhs_type_id);

		if (node->tag != AstTag::OpSet && rhs_type_tag != TypeTag::Integer && rhs_type_tag != TypeTag::CompInteger)
		{
			if (node->tag != AstTag::OpSetAdd && node->tag != AstTag::OpSetSub && node->tag != AstTag::OpSetMul && node->tag != AstTag::OpSetDiv)
				source_error(interp->errors, rhs->source_id, "Right-hand-side of `%s` must be of integral type.\n", tag_name(node->tag));

			if (rhs_type_tag != TypeTag::Float && rhs_type_tag != TypeTag::CompFloat)
				source_error(interp->errors, rhs->source_id, "Right-hand-side of `%s` must be of integral or floating point type.\n", tag_name(node->tag));
		}

		const TypeId common_type_id = common_type(interp->types, lhs_type_id, rhs_type_id);

		if (common_type_id.rep == INVALID_TYPE_ID.rep)
			source_error(interp->errors, node->source_id, "Incompatible left-hand and right-hand side operands for `%s`.\n", tag_name(node->tag));

		return set_assignability(common_type_id, false);
	}

	case AstTag::OpShiftL:
	case AstTag::OpShiftR:
	{
		AstNode* const lhs = first_child_of(node);

		const TypeId lhs_type_id = typecheck_expr(interp, lhs);

		const TypeTag lhs_type_tag = type_tag_from_id(interp->types, lhs_type_id);

		if (lhs_type_tag != TypeTag::Integer && lhs_type_tag != TypeTag::CompInteger)
			source_error(interp->errors, lhs->source_id, "Left-hand-side of `%s` must be of integral type.\n", tag_name(node->tag));

		if (!is_assignable(lhs_type_id))
			source_error(interp->errors, lhs->source_id, "Left-hand-side of `%s` must be assignable.\n", tag_name(node->tag));

		AstNode* const rhs = next_sibling_of(lhs);

		const TypeId rhs_type_id = typecheck_expr(interp, rhs);

		const TypeTag rhs_type_tag = type_tag_from_id(interp->types, rhs_type_id);

		if (rhs_type_tag != TypeTag::Integer && rhs_type_tag != TypeTag::CompInteger)
			source_error(interp->errors, lhs->source_id, "Right-hand-side of `%s` must be of integral type.\n", tag_name(node->tag));

		return set_assignability(lhs_type_id, false);
	}

	case AstTag::OpLogAnd:
	case AstTag::OpLogOr:
	{
		AstNode* const lhs = first_child_of(node);

		const TypeId lhs_type_id = typecheck_expr(interp, lhs);

		const TypeTag lhs_type_tag = type_tag_from_id(interp->types, lhs_type_id);

		if (lhs_type_tag != TypeTag::Boolean)
			source_error(interp->errors, lhs->source_id, "Left-hand-side of `%s` must be of boolean type.\n", tag_name(node->tag));

		AstNode* const rhs = next_sibling_of(lhs);

		const TypeId rhs_type_id = typecheck_expr(interp, rhs);

		const TypeTag rhs_type_tag = type_tag_from_id(interp->types, rhs_type_id);

		if (rhs_type_tag != TypeTag::Boolean)
			source_error(interp->errors, lhs->source_id, "Right-hand-side of `%s` must be of boolean type.\n", tag_name(node->tag));

		const TypeId common_type_id = common_type(interp->types, lhs->type_id, rhs->type_id);

		if (common_type_id.rep == INVALID_TYPE_ID.rep)
			source_error(interp->errors, node->source_id, "Operands of `%s` are incompatible.\n", tag_name(node->tag));

		return set_assignability(common_type_id, false);
	}

	case AstTag::OpMember:
	{
		AstNode* const lhs = first_child_of(node);

		const TypeId lhs_type_id = typecheck_expr(interp, lhs);

		const TypeTag lhs_type_tag = type_tag_from_id(interp->types, lhs_type_id);

		if (lhs_type_tag != TypeTag::Composite && lhs_type_tag != TypeTag::Type)
			source_error(interp->errors, lhs->source_id, "Left-hand-side of `.` must be of type `Type` or a composite type.\n");

		AstNode* const rhs = next_sibling_of(lhs);

		if (rhs->tag != AstTag::Identifer)
			source_error(interp->errors, rhs->source_id, "Right-hand-side of `.` must be an identifier\n");

		rhs->type_id = NO_TYPE_TYPE_ID;

		if (lhs_type_tag == TypeTag::Composite)
		{
			const IdentifierId identifier_id = attachment_of<AstIdentifierData>(rhs)->identifier_id;

			MemberInfo member;

			if (!type_member_info_by_name(interp->types, lhs_type_id, identifier_id, &member))
			{
				const Range<char8> name = identifier_name_from_id(interp->identifiers, identifier_id);

				source_error(interp->errors, node->source_id, "Left-hand-side of `.` has no member \"%.*s\"", static_cast<s32>(name.count()), name.begin());
			}

			const TypeId member_type_id = delayed_typecheck_member(interp, &member);

			return mask_assignability(member_type_id, is_assignable(lhs_type_id));
		}
		else
		{
			ASSERT_OR_IGNORE(lhs_type_tag == TypeTag::Type);

			const TypeId defined_type_id = *static_cast<TypeId*>(evaluate_expr(interp, lhs));

			panic("Typechecking of AST node type %s with a `Type` as its left-hand-side is not yet implemented.\n", tag_name(node->tag));
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

		const TypeId lhs_type_id = typecheck_expr(interp, lhs);

		const TypeTag lhs_type_tag = type_tag_from_id(interp->types, lhs_type_id);

		if (lhs_type_tag == TypeTag::Array || lhs_type_tag == TypeTag::ArrayLiteral || lhs_type_tag == TypeTag::Composite || lhs_type_tag == TypeTag::CompositeLiteral)
			source_error(interp->errors, lhs->source_id, "Left-hand-side of %s must not be of composite or array type.\n");

		AstNode* const rhs = next_sibling_of(lhs);

		const TypeId rhs_type_id = typecheck_expr(interp, rhs);

		const TypeTag rhs_type_tag = type_tag_from_id(interp->types, rhs_type_id);

		if (rhs_type_tag == TypeTag::Array || rhs_type_tag == TypeTag::ArrayLiteral || rhs_type_tag == TypeTag::Composite || rhs_type_tag == TypeTag::CompositeLiteral)
			source_error(interp->errors, rhs->source_id, "Right-hand-side of %s must not be of composite or array type.\n");

		const TypeId common_type_id = common_type(interp->types, lhs_type_id, rhs_type_id);

		if (common_type_id.rep == INVALID_TYPE_ID.rep)
			source_error(interp->errors, node->source_id, "Incompatible left-hand and right-hand side operands for `%s`.\n", tag_name(node->tag));

		return primitive_type(interp->types, TypeTag::Boolean, {});
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

		const TypeId lhs_type_id = typecheck_expr(interp, lhs);

		const TypeTag lhs_type_tag = type_tag_from_id(interp->types, lhs_type_id);

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

		const TypeId rhs_type_id = typecheck_expr(interp, rhs);

		const TypeTag rhs_type_tag = type_tag_from_id(interp->types, rhs_type_id);

		if (node->tag != AstTag::OpSet && rhs_type_tag != TypeTag::Integer && rhs_type_tag != TypeTag::CompInteger)
		{
			if (node->tag != AstTag::OpSetAdd && node->tag != AstTag::OpSetSub && node->tag != AstTag::OpSetMul && node->tag != AstTag::OpSetDiv)
				source_error(interp->errors, rhs->source_id, "Right-hand-side of `%s` must be of integral type.\n", tag_name(node->tag));

			if (rhs_type_tag != TypeTag::Float && rhs_type_tag != TypeTag::CompFloat)
				source_error(interp->errors, rhs->source_id, "Right-hand-side of `%s` must be of integral or floating point type.\n", tag_name(node->tag));
		}

		const TypeId common_type_id = common_type(interp->types, lhs_type_id, rhs_type_id);

		if (common_type_id.rep == INVALID_TYPE_ID.rep)
			source_error(interp->errors, node->source_id, "Incompatible left-hand and right-hand side operands for `%s`.\n", tag_name(node->tag));

		return primitive_type(interp->types, TypeTag::Void, {});
	}

	case AstTag::OpSetShiftL:
	case AstTag::OpSetShiftR:
	{
		AstNode* const lhs = first_child_of(node);

		const TypeId lhs_type_id = typecheck_expr(interp, lhs);

		const TypeTag lhs_type_tag = type_tag_from_id(interp->types, lhs_type_id);

		if (lhs_type_tag != TypeTag::Integer && lhs_type_tag != TypeTag::CompInteger)
			source_error(interp->errors, lhs->source_id, "Left-hand-side of `%s` must be of integral type.\n", tag_name(node->tag));

		if (!is_assignable(lhs_type_id))
			source_error(interp->errors, lhs->source_id, "Left-hand-side of `%s` must be assignable.\n", tag_name(node->tag));

		AstNode* const rhs = next_sibling_of(lhs);

		const TypeId rhs_type_id = typecheck_expr(interp, rhs);

		const TypeTag rhs_type_tag = type_tag_from_id(interp->types, rhs_type_id);

		if (rhs_type_tag != TypeTag::Integer && rhs_type_tag != TypeTag::CompInteger)
			source_error(interp->errors, lhs->source_id, "Right-hand-side of `%s` must be of integral type.\n", tag_name(node->tag));

		return primitive_type(interp->types, TypeTag::Void, {});
	}

	case AstTag::OpTypeArray:
	{
		AstNode* const count = first_child_of(node);

		const TypeId count_type_id = typecheck_expr(interp, count);

		const TypeTag count_type_tag = type_tag_from_id(interp->types, count_type_id);

		if (count_type_tag != TypeTag::Integer && count_type_tag != TypeTag::CompInteger)
			source_error(interp->errors, count->source_id, "Expected array count expression to be of integral type.\n");

		AstNode* const type = next_sibling_of(count);

		const TypeId type_type_id = typecheck_expr(interp, type);

		const TypeTag type_type_tag = type_tag_from_id(interp->types, type_type_id);

		if (type_type_tag != TypeTag::Type)
			source_error(interp->errors, type->source_id, "Expected array type expression of be of type `Type`.\n");

		const u64 count_value = *static_cast<u64*>(evaluate_expr(interp, count));

		pop_stack_value(interp);

		ArrayType result_type;
		result_type.element_type = set_assignability(type_type_id, true);
		result_type.element_count = count_value;

		return primitive_type(interp->types, TypeTag::Array, range::from_object_bytes(&result_type));
	}

	case AstTag::OpArrayIndex:
	{
		AstNode* const arrayish = first_child_of(node);

		const TypeId arrayish_type_id = typecheck_expr(interp, arrayish);

		const TypeTag array_type_tag = type_tag_from_id(interp->types, arrayish_type_id);

		TypeId element_type_id;

		const void* const structure = primitive_type_structure(interp->types, arrayish_type_id);

		if (array_type_tag == TypeTag::Array)
			element_type_id = static_cast<const ArrayType*>(structure)->element_type;
		else if (array_type_tag == TypeTag::Slice || array_type_tag == TypeTag::Ptr)
			element_type_id = static_cast<const ReferenceType*>(structure)->referenced_type_id;
		else
			source_error(interp->errors, arrayish->source_id, "Left-hand-side of array dereference operator must be of array-, slice- or multi-pointer type.\n");

		AstNode* const index = next_sibling_of(arrayish);

		const TypeId index_type_id = typecheck_expr(interp, index);

		const TypeTag index_type_tag = type_tag_from_id(interp->types, index_type_id);

		if (index_type_tag != TypeTag::Integer && index_type_tag != TypeTag::CompInteger)
			source_error(interp->errors, index->source_id, "Index operand of array dereference operator must be of integral type.\n");

		return mask_assignability(element_type_id, is_assignable(arrayish_type_id));
	}

	case AstTag::ParameterList:
	case AstTag::Case:
	case AstTag::File:
	default:
		ASSERT_UNREACHABLE;
	}
}

static TypeId typecheck_expr(Interpreter* interp, AstNode* node) noexcept
{
	if (node->type_id.rep == CHECKING_TYPE_ID.rep)
	{
		source_error(interp->errors, node->source_id, "Cyclic type dependency detected.\n");
	}
	else if (node->type_id.rep != INVALID_TYPE_ID.rep)
	{
		return node->type_id;
	}

	const TypeId result = typecheck_expr_impl(interp, node);

	ASSERT_OR_IGNORE(result.rep != INVALID_TYPE_ID.rep && result.rep != CHECKING_TYPE_ID.rep && result.rep != NO_TYPE_TYPE_ID.rep);

	node->type_id = result;

	return result;
}

static TypeId type_from_file_ast(Interpreter* interp, AstNode* file, SourceId file_type_source_id) noexcept
{
	ASSERT_OR_IGNORE(file->tag == AstTag::File);

	const TypeId file_type_id = create_open_type(interp->types, file_type_source_id);

	push_typechecker_context(interp, file_type_id, true);

	AstDirectChildIterator ast_it = direct_children_of(file);

	for (OptPtr<AstNode> rst = next(&ast_it); is_some(rst); rst = next(&ast_it))
	{
		AstNode* const node = get_ptr(rst);

		if (node->tag != AstTag::Definition)
			source_error(interp->errors, node->source_id, "Currently only definitions are supported on a file's top-level.\n");

		MemberInit member = member_init_from_definition(interp, node, get_definition_info(node), 0);

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

		(void) delayed_typecheck_member(interp, &member);
	}

	pop_typechecker_context(interp, true);

	return file_type_id;
}



static TypeId make_func_type_from_array(TypePool* types, TypeId return_type_id, u16 param_count, const FuncTypeParamHelper* params) noexcept
{
	const TypeId signature_type_id = create_open_type(types, INVALID_SOURCE_ID);

	u64 offset = 0;

	u32 max_align = 1;

	for (u16 i = 0; i != param_count; ++i)
	{
		const TypeMetrics metrics = type_metrics_from_id(types, params[i].type);

		offset = next_multiple(offset, static_cast<u64>(metrics.align));

		MemberInit init{};
		init.name = params[i].name;
		init.type.id = params[i].type;
		init.source = INVALID_SOURCE_ID;
		init.is_global = false;
		init.is_pub = false;
		init.is_use = false;
		init.has_pending_type = false;
		init.offset_or_global_value = offset;
		init.opt_type_node_id = INVALID_AST_NODE_ID;
		init.opt_value_node_id = INVALID_AST_NODE_ID;

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

	return primitive_type(types, TypeTag::Func, range::from_object_bytes(&func_type));
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

static void init_builtin_types(Interpreter* interp) noexcept
{
	const TypeId type_type_id = primitive_type(interp->types, TypeTag::Type, {});

	const TypeId comp_integer_type_id = primitive_type(interp->types, TypeTag::CompInteger, {});

	const TypeId comp_string_type_id = primitive_type(interp->types, TypeTag::CompString, {});

	const TypeId bool_type_id = primitive_type(interp->types, TypeTag::Boolean, {});

	const TypeId definition_type_id = primitive_type(interp->types, TypeTag::Definition, {});

	const TypeId type_builder_type_id = primitive_type(interp->types, TypeTag::TypeBuilder, {});

	const TypeId void_type_id = primitive_type(interp->types, TypeTag::Void, {});

	const TypeId type_info_type_id = primitive_type(interp->types, TypeTag::TypeInfo, {});

	ReferenceType ptr_to_type_builder_type{};
	ptr_to_type_builder_type.referenced_type_id = set_assignability(type_builder_type_id, true);
	ptr_to_type_builder_type.is_opt = false;
	ptr_to_type_builder_type.is_multi = false;

	const TypeId ptr_to_type_builder_type_id = primitive_type(interp->types, TypeTag::Ptr, range::from_object_bytes(&ptr_to_type_builder_type));



	interp->builtin_type_ids[static_cast<u8>(Builtin::Integer)] = make_func_type(interp->types, type_type_id,
		FuncTypeParamHelper{ id_from_identifier(interp->identifiers, range::from_literal_string("bits")), comp_integer_type_id },
		FuncTypeParamHelper{ id_from_identifier(interp->identifiers, range::from_literal_string("is_signed")), bool_type_id }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::Type)] = make_func_type(interp->types, type_type_id);

	interp->builtin_type_ids[static_cast<u8>(Builtin::Definition)] = make_func_type(interp->types, type_type_id);

	interp->builtin_type_ids[static_cast<u8>(Builtin::CompInteger)] = make_func_type(interp->types, type_type_id);

	interp->builtin_type_ids[static_cast<u8>(Builtin::CompFloat)] = make_func_type(interp->types, type_type_id);

	interp->builtin_type_ids[static_cast<u8>(Builtin::CompString)] = make_func_type(interp->types, type_type_id);

	interp->builtin_type_ids[static_cast<u8>(Builtin::TypeBuilder)] = make_func_type(interp->types, type_type_id);

	interp->builtin_type_ids[static_cast<u8>(Builtin::True)] = make_func_type(interp->types, bool_type_id);

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

	interp->builtin_type_ids[static_cast<u8>(Builtin::Nameof)] = make_func_type(interp->types, comp_string_type_id,
		FuncTypeParamHelper{ id_from_identifier(interp->identifiers, range::from_literal_string("arg")), type_info_type_id }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::Import)] = make_func_type(interp->types, type_type_id,
		FuncTypeParamHelper{ id_from_identifier(interp->identifiers, range::from_literal_string("path")), comp_string_type_id },
		FuncTypeParamHelper{ id_from_identifier(interp->identifiers, range::from_literal_string("is_std")), bool_type_id }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::CreateTypeBuilder)] = make_func_type(interp->types, type_builder_type_id);

	// TODO
	interp->builtin_type_ids[static_cast<u8>(Builtin::AddTypeMember)] = make_func_type(interp->types, void_type_id);

	interp->builtin_type_ids[static_cast<u8>(Builtin::CompleteType)] = make_func_type(interp->types, type_type_id,
		FuncTypeParamHelper{ id_from_identifier(interp->identifiers, range::from_literal_string("arg")), type_builder_type_id }
	);
}

static void init_prelude_type(Interpreter* interp, Config* config, AstBuilder* builder, IdentifierPool* identifiers, AstPool* asts) noexcept
{
	const AstBuilderToken import_builtin = push_node(builder, AstBuilder::NO_CHILDREN, INVALID_SOURCE_ID, static_cast<AstFlag>(Builtin::Import), AstTag::Builtin);

	push_node(builder, AstBuilder::NO_CHILDREN, INVALID_SOURCE_ID, AstFlag::EMPTY, AstLitStringData{ id_from_identifier(identifiers, config->std.filepath)});

	const AstBuilderToken import_call = push_node(builder, import_builtin, INVALID_SOURCE_ID, AstFlag::EMPTY, AstTag::Call);

	const AstBuilderToken std_definition = push_node(builder, import_call, INVALID_SOURCE_ID, AstFlag::EMPTY, AstDefinitionData{ id_from_identifier(identifiers, range::from_literal_string("std")) });

	const AstBuilderToken std_identifier = push_node(builder, AstBuilder::NO_CHILDREN, INVALID_SOURCE_ID, AstFlag::EMPTY, AstIdentifierData{ id_from_identifier(identifiers, range::from_literal_string("std")) });

	push_node(builder, AstBuilder::NO_CHILDREN, INVALID_SOURCE_ID, AstFlag::EMPTY, AstIdentifierData{ id_from_identifier(identifiers, range::from_literal_string("prelude")) });

	const AstBuilderToken prelude_member = push_node(builder, std_identifier, INVALID_SOURCE_ID, AstFlag::EMPTY, AstTag::OpMember);

	push_node(builder, prelude_member, INVALID_SOURCE_ID, AstFlag::Definition_IsUse, AstDefinitionData{ id_from_identifier(identifiers, range::from_literal_string("prelude"))} );

	push_node(builder, std_definition, INVALID_SOURCE_ID, AstFlag::EMPTY, AstTag::File);

	AstNode* const prelude_ast = complete_ast(builder, asts);

	interp->prelude_type_id = type_from_file_ast(interp, prelude_ast, INVALID_SOURCE_ID);
}



Interpreter* create_interpreter(AllocPool* alloc, Config* config, SourceReader* reader, Parser* parser, TypePool* types, AstPool* asts, IdentifierPool* identifiers, ErrorSink* errors) noexcept
{
	Interpreter* const interp = static_cast<Interpreter*>(alloc_from_pool(alloc, sizeof(Interpreter), alignof(Interpreter)));

	interp->reader = reader;
	interp->parser = parser;
	interp->types = types;
	interp->asts = asts;
	interp->identifiers = identifiers;
	interp->errors = errors;
	interp->value_stack.init(1 << 20, 1 << 9);
	interp->value_stack_inds.init(1 << 20, 1 << 10);
	interp->activation_records.init(1 << 20, 1 << 9);
	interp->activation_record_inds.init(1 << 20, 1 << 10);
	interp->prelude_type_id = INVALID_TYPE_ID;
	interp->context_top = -1;

	init_builtin_types(interp);

	init_prelude_type(interp, config, get_ast_builder(parser), identifiers, asts);

	return interp;
}

void release_interpreter([[maybe_unused]] Interpreter* interp) noexcept
{
	// No-op
}

TypeId import_file(Interpreter* interp, Range<char8> filepath, bool is_std) noexcept
{
	SourceFileRead read = read_source_file(interp->reader, filepath);

	AstNode* root;

	if (read.source_file->ast_root == INVALID_AST_NODE_ID)
	{
		root = parse(interp->parser, read.content, read.source_file->source_id_base, is_std, interp->asts, filepath);

		read.source_file->ast_root = id_from_ast_node(interp->asts, root);
	}
	else
	{
		root = ast_node_from_id(interp->asts, read.source_file->ast_root);
	}

	const TypeId file_type_id = type_from_file_ast(interp, root, read.source_file->source_id_base);

	return file_type_id;
}

const char8* tag_name(Builtin builtin) noexcept
{
	static constexpr const char8* BUILTIN_NAMES[] = {
		"[Unknown]",
		"_integer",
		"_type",
		"_definition",
		"_comp_integer",
		"_comp_float",
		"_comp_string",
		"_bype_builder",
		"_true",
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
	};

	u8 ordinal = static_cast<u8>(builtin);

	if (ordinal >= array_count(BUILTIN_NAMES))
		ordinal = 0;

	return BUILTIN_NAMES[ordinal];
}
