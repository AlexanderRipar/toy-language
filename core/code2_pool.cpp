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

struct alignas(8) MemberSortInfo
{
	u32 name_or_ordered_index;

	s32 ast_index;
};

struct MemberSortByName
{
	static s32 compare(MemberSortInfo a, MemberSortInfo b) noexcept
	{
		if (a.name_or_ordered_index < b.name_or_ordered_index)
			return -1;
		else if (a.name_or_ordered_index > b.name_or_ordered_index)
			return 1;
		else
			return 0;
	}
};

struct MemberSortByAst
{
	static s32 compare(MemberSortInfo a, MemberSortInfo b) noexcept
	{
		return static_cast<s32>(a.ast_index) - static_cast<s32>(b.ast_index);
	}
};

enum class Code2FixupTag : u8
{
	INVALID = 0,
	ExprSharedScope,
	ExprOwnScope,
	EmptyExpr,
	LoopBodyAndStep,
	FunctionBody,
};

struct Code2Fixup
{
	Code2FixupTag tag;

	u8 function_body_parameter_count;

	Maybe<AstNodeId> src_1;

	Maybe<AstNodeId> src_2;

	Code2Id* dst;

	Code2AttScopeBegin* scope;
};





static Code2Id id_from_code2_raw(CoreData* core, const void* raw_code) noexcept
{
	const s64 offset = static_cast<const u32*>(raw_code) - reinterpret_cast<const u32*>(core);

	ASSERT_OR_IGNORE(offset > 0 && offset < (1 << 24));

	return static_cast<Code2Id>(offset);
}

Code2Id id_from_code2(CoreData* core, const Code2Op* code) noexcept
{
	return id_from_code2_raw(core, code);
}

const Code2Op* code2_from_id(CoreData* core, Code2Id id) noexcept
{
	ASSERT_OR_IGNORE(id != Code2Id::INVALID);

	const Code2Op* const op = reinterpret_cast<const Code2Op*>(reinterpret_cast<u32*>(core) + static_cast<u32>(id));

	ASSERT_OR_IGNORE(reinterpret_cast<const u32*>(op) >= core->code2.codes.begin() && reinterpret_cast<const u32*>(op) < core->code2.codes.end());

	return op;
}



static void* emit_code2_raw(CoreData* core, u64 size) noexcept
{
	ASSERT_OR_IGNORE(size <= UINT32_MAX);

	return core->code2.codes.reserve_exact(static_cast<u32>(size));
}

template<typename T>
static T* emit_code2_sized(CoreData* core, T data, u64 size) noexcept
{
	ASSERT_OR_IGNORE(size >= sizeof(T));

	T* const attach = static_cast<T*>(emit_code2_raw(core, size));

	*attach = data;

	return attach;
}

template<typename T>
static T* emit_code2(CoreData* core, T data) noexcept
{
	return emit_code2_sized(core, data, sizeof(T));
}

template<typename T>
static Code2Id emit_code2_ref(CoreData* core, T data) noexcept
{
	void* const attach = emit_code2(core, data);

	return id_from_code2(core, static_cast<Code2Op*>(attach));
}





static void fixup_generic(CoreData* core, Code2FixupTag tag, u8 function_body_parameter_count, Code2Id* dst, Maybe<AstNode*> src_1, Maybe<AstNode*> src_2, Code2AttScopeBegin* scope) noexcept
{
	Code2Fixup* const fixup = core->code2.fixups.reserve();
	fixup->tag = tag;
	fixup->function_body_parameter_count = function_body_parameter_count;
	fixup->src_1 = is_some(src_1) ? some(id_from_ast_node(core, get(src_1))) : none<AstNodeId>();
	fixup->src_2 = is_some(src_2) ? some(id_from_ast_node(core, get(src_2))) : none<AstNodeId>();
	fixup->dst = dst;
	fixup->scope = scope;
}

static void fixup_expr_shared_scope(CoreData* core, Code2Id* dst, AstNode* src, Code2AttScopeBegin* scope) noexcept
{
	fixup_generic(core, Code2FixupTag::ExprSharedScope, 0, dst, some(src), none<AstNode*>(), scope);
}

static void fixup_expr_own_scope(CoreData* core, Code2Id* dst, AstNode* src, Code2AttScopeBegin* scope) noexcept
{
	fixup_generic(core, Code2FixupTag::ExprOwnScope, 0, dst, some(src), none<AstNode*>(), scope);
}

static void fixup_empty_expr(CoreData* core, Code2Id* dst, Code2AttScopeBegin* scope) noexcept
{
	fixup_generic(core, Code2FixupTag::EmptyExpr, 0, dst, none<AstNode*>(), none<AstNode*>(), scope);
}

static void fixup_loop_body_and_step(CoreData* core, Code2Id* dst, AstNode* body, Maybe<AstNode*> step, Code2AttScopeBegin* scope) noexcept
{
	fixup_generic(core, Code2FixupTag::LoopBodyAndStep, 0, dst, some(body), step, scope);
}

static void fixup_finally(CoreData* core, Code2Id* dst, AstNode* src, Code2AttScopeBegin* scope) noexcept
{
	fixup_generic(core, Code2FixupTag::ExprOwnScope, 0, dst, some(src), none<AstNode*>(), scope);
}

static void fixup_function_body(CoreData* core, Code2Id* dst, AstNode* src, u8 parameter_count, Code2AttScopeBegin* scope) noexcept
{
	fixup_generic(core, Code2FixupTag::FunctionBody, parameter_count, dst, some(src), none<AstNode*>(), scope);
}

static void fixup_argument(CoreData* core, Code2Id* dst, AstNode* src, Code2AttScopeBegin* scope) noexcept
{
	fixup_generic(core, Code2FixupTag::ExprSharedScope, 0, dst, some(src), none<AstNode*>(), scope);
}

static void fixup_trait_member_type(CoreData* core, Code2Id* dst, AstNode* src, Code2AttScopeBegin* scope) noexcept
{
	fixup_generic(core, Code2FixupTag::ExprOwnScope, 0, dst, some(src), none<AstNode*>(), scope);
}

static void fixup_trait_member_default_value(CoreData* core, Code2Id* dst, AstNode* src, Code2AttScopeBegin* scope) noexcept
{
	fixup_generic(core, Code2FixupTag::ExprOwnScope, 0, dst, some(src), none<AstNode*>(), scope);
}

static void fixup_impl_member_type(CoreData* core, Code2Id* dst, AstNode* src, Code2AttScopeBegin* scope) noexcept
{
	fixup_generic(core, Code2FixupTag::ExprOwnScope, 0, dst, some(src), none<AstNode*>(), scope);
}

static void fixup_impl_member_default_value(CoreData* core, Code2Id* dst, AstNode* src, Code2AttScopeBegin* scope) noexcept
{
	fixup_generic(core, Code2FixupTag::ExprOwnScope, 0, dst, some(src), none<AstNode*>(), scope);
}



static Code2Id code2_from_expr(CoreData* core, AstNode* node, Code2AttScopeBegin* scope, LocationMode location_mode) noexcept;

static Code2AttScopeBegin* code2_from_where_prefix(CoreData* core, AstNode* node) noexcept
{
	const u16 definition_count = attachment_of<AstWhereData>(node)->definition_count;

	Code2AttScopeBegin* const begin_attach = emit_code2(core, Code2AttScopeBegin{ code2_id_with(Code2Id::INVALID, Code2Op::ScopeBegin), definition_count });

	const Code2Id begin = id_from_code2_raw(core, begin_attach);

	AstDirectChildIterator it = direct_children_of(node);

	while (has_next(&it))
	{
		AstNode* const definition = next(&it);

		const DefinitionInfo info = get_definition_info(definition);

		ASSERT_OR_IGNORE(is_some(info.value));

		const bool is_mut = has_flag(definition, AstFlag::Definition_IsMut);

		const bool has_self_reference = has_flag(definition, AstFlag::Definition_HasSelfReference);

		const Code2Id type = is_some(info.type)
			? code2_from_expr(core, get(info.type), begin_attach, LocationMode::Value)
			: Code2Id::INVALID;

		const Code2Id value = code2_from_expr(core, get(info.value), begin_attach, LocationMode::Value);

		const Code2AttDefineLocal_Flags flags{ is_mut, has_self_reference };

		if (is_some(info.type))
			(void) emit_code2_ref(core, Code2AttDefineLocalTyped{ code2_id_with(begin, Code2Op::DefineLocalTyped), code2_id_with(value, flags), type, definition_count });
		else
			(void) emit_code2_ref(core, Code2AttDefineLocalUntyped{ code2_id_with(begin, Code2Op::DefineLocalUntyped), code2_id_with(value, flags), definition_count });
	}

	return begin_attach;
}

static void code2_from_where_suffix(CoreData* core, Code2AttScopeBegin* begin_attach) noexcept
{
	const Code2Id begin = id_from_code2_raw(core, begin_attach);

	const Code2Id end = emit_code2_ref(core, Code2AttScopeEnd{ code2_id_with(begin, Code2Op::ScopeEnd) });

	begin_attach->scope_end = code2_id_with(end, Code2Op::ScopeBegin);
}

static Code2Id code2_from_signature(CoreData* core, AstNode* node, Code2AttScopeBegin* scope) noexcept
{
	const SignatureInfo info = get_signature_info(node);

	const bool is_proc = has_flag(node, AstFlag::Signature_IsProc);

	const u8 parameter_count = attachment_of<AstSignatureData>(node)->parameter_count;

	const Code2Id signature = emit_code2_ref(core, Code2AttSignatureBegin{ Code2Op::SignatureBegin, parameter_count, is_proc });

	AstDirectChildIterator it = direct_children_of(info.parameters);

	while (has_next(&it))
	{
		AstNode* const parameter = next(&it);

		const DefinitionInfo parameter_info = get_definition_info(parameter);

		ASSERT_OR_IGNORE(is_some(parameter_info.type));

		const IdentifierId name = attachment_of<AstDefinitionData>(parameter)->identifier_id;

		const bool has_value = is_some(parameter_info.value);

		const bool is_mut = has_flag(parameter, AstFlag::Definition_IsMut);

		const bool is_eval = has_flag(parameter, AstFlag::Definition_IsEval);

		const bool is_templated = has_flag(parameter, AstFlag::Definition_IsTemplatedParam);

		AstNode* type_node = get(parameter_info.type);

		const bool is_variadic = type_node->tag == AstTag::UOpTypeVarArgs;

		if (is_variadic)
			type_node = first_child_of(type_node);

		const Code2Id type = code2_from_expr(core, type_node, scope, LocationMode::Value);

		const Maybe<Code2Id> value = has_value
			? some(code2_from_expr(core, get(parameter_info.value), scope, LocationMode::Value))
			: none<Code2Id>();

		const Code2AttParameter_Flags flags{ is_mut, is_eval, is_templated, is_variadic };

		(void) emit_code2(core, Code2AttParameter{ code2_id_with(signature, Code2Op::Parameter), code2_id_with(type, flags), value, name });
	}

	const Code2Id return_type = code2_from_expr(core, info.return_type, scope, LocationMode::Value);

	if (is_some(info.requires))
		TODO("Implement signature-level `requires`.");

	if (is_some(info.ensures))
		TODO("Implement signature-level `ensures`.");

	return emit_code2_ref(core, Code2AttSignatureEnd{ code2_id_with(signature, Code2Op::SignatureEnd), return_type });
}

static Code2Id code2_from_call_generic(CoreData* core, AstNode* node, Code2AttScopeBegin* scope, Code2Op op) noexcept
{
	AstNode* const callee_node = first_child_of(node);

	const Code2Id callee = code2_from_expr(core, callee_node, scope, LocationMode::Value);

	u8 argument_count = 0;

	for (AstNode* argument_node = callee_node; has_next_sibling(argument_node); argument_node = next_sibling_of(argument_node))
	{
		ASSERT_OR_IGNORE(argument_count < 64);

		argument_count += 1;
	}

	const u64 attach_size = sizeof(Code2AttCall) + argument_count * sizeof(Code2AttCall_Argument);

	Code2AttCall* const attach = static_cast<Code2AttCall*>(emit_code2_raw(core, attach_size));
	attach->op = op;
	attach->argument_count = argument_count;
	attach->callee = callee;

	u8 i = 0;

	AstNode* argument_node = callee_node;

	while (has_next_sibling(argument_node))
	{
		argument_node = next_sibling_of(argument_node);

		Maybe<IdentifierId> name;

		AstNode* value_node;

		if (argument_node->tag == AstTag::OpSet)
		{
			AstNode* const name_node = first_child_of(argument_node);

			name = some(attachment_of<AstImpliedMemberData>(name_node)->identifier_id);

			value_node = next_sibling_of(name_node);
		}
		else
		{
			name = none<IdentifierId>();

			value_node = argument_node;
		}

		ASSERT_OR_IGNORE(i < argument_count);

		attach->arguments[i] = Code2AttCall_Argument{ name, Code2Id::INVALID };

		fixup_argument(core, &attach->arguments[i].value, value_node, scope);

		i += 1;
	}

	ASSERT_OR_IGNORE(i == argument_count);

	return id_from_code2_raw(core, attach);
}

static Code2Id code2_from_expr_raw(CoreData* core, AstNode* node, Code2AttScopeBegin* scope, LocationMode location_mode) noexcept
{
	const AstTag tag = node->tag;

	switch (tag)
	{
	case AstTag::Builtin:
	{
		const Builtin builtin = attachment_of<AstBuiltinData>(node)->builtin;

		return emit_code2_ref(core, Code2AttBuiltin{ Code2Op::Builtin, builtin });
	}

	case AstTag::CompositeInitializer:
	{
		const u64 mark = temp_stack_mark(core);

		Code2AttComposite_Member* const members = temp_stack_typed_align<Code2AttComposite_Member>(core);

		u16 member_count = 0;

		AstDirectChildIterator it = direct_children_of(node);

		while (has_next(&it))
		{
			AstNode* member = next(&it);

			Maybe<IdentifierId> name;

			if (member->tag == AstTag::OpSet)
			{
				AstNode* const name_node = first_child_of(member);

				name = some(attachment_of<AstImpliedMemberData>(name_node)->identifier_id);

				member = next_sibling_of(name_node);
			}
			else
			{
				name = none<IdentifierId>();
			}

			ASSERT_OR_IGNORE(member_count < UINT16_MAX);

			member_count += 1;

			const Code2Id value = code2_from_expr(core, member, scope, LocationMode::Value);

			Code2AttComposite_Member* const member_dst = temp_stack_typed_alloc<Code2AttComposite_Member>(core);

			*member_dst = Code2AttComposite_Member{ name, value };
		}

		const u64 attach_size = sizeof(Code2AttComposite) + static_cast<u64>(member_count) * sizeof(Code2AttComposite_Member);

		Code2AttComposite* const attach = static_cast<Code2AttComposite*>(emit_code2_raw(core, attach_size));
		attach->op = Code2Op::Composite;
		attach->member_count = member_count;

		memcpy(attach->members, members, member_count * sizeof(Code2AttComposite_Member));

		temp_stack_release(core, mark);

		return id_from_code2_raw(core, attach);
	}

	case AstTag::ArrayInitializer:
	{
		const u64 mark = temp_stack_mark(core);

		Code2AttArray_Element* const elements = temp_stack_typed_align<Code2AttArray_Element>(core);

		u16 element_count = 0;

		AstDirectChildIterator it = direct_children_of(node);

		while (has_next(&it))
		{
			AstNode* const element = next(&it);

			ASSERT_OR_IGNORE(element_count < UINT16_MAX);

			element_count += 1;

			const Code2Id value = code2_from_expr(core, element, scope, LocationMode::Value);

			Code2AttArray_Element* const member_dst = temp_stack_typed_alloc<Code2AttArray_Element>(core);

			*member_dst = Code2AttArray_Element{ value };
		}

		const u64 attach_size = sizeof(Code2AttArray) + static_cast<u64>(element_count) * sizeof(Code2AttArray_Element);

		Code2AttArray* const attach = static_cast<Code2AttArray*>(emit_code2_raw(core, attach_size));
		attach->op = Code2Op::Array;
		attach->element_count = element_count;

		memcpy(attach->elements, elements, element_count * sizeof(Code2AttArray_Element));

		temp_stack_release(core, mark);

		return id_from_code2_raw(core, attach);
	}

	case AstTag::Wildcard:
	{
		TODO("Implement.");
	}

	case AstTag::Definition:
	{
		const DefinitionInfo info = get_definition_info(node);

		ASSERT_OR_IGNORE(is_some(info.type) || is_some(info.value));

		const bool is_mut = has_flag(node, AstFlag::Definition_IsMut);

		const bool is_pub = has_flag(node, AstFlag::Definition_IsPub);

		const bool has_self_reference = has_flag(node, AstFlag::Definition_HasSelfReference);

		const Code2AttDefinition_Flags flags{ is_mut, is_pub, has_self_reference };

		const Code2Id type = is_some(info.type)
			? code2_from_expr(core, get(info.type), scope, LocationMode::Value)
			: Code2Id::INVALID;

		const Code2Id default_value = is_some(info.value)
			? code2_from_expr(core, get(info.value), scope, LocationMode::Value)
			: Code2Id::INVALID;

		return emit_code2_ref(core, Code2AttDefinition{ code2_id_with( type, Code2Op::Definition ), code2_id_with(default_value, flags) });
	}

	case AstTag::Block:
	{
		const u16 definition_count = attachment_of<AstBlockData>(node)->definition_count;

		Code2AttScopeBegin* const begin_attach = emit_code2(core, Code2AttScopeBegin{ code2_id_with(Code2Id::INVALID, Code2Op::ScopeBegin), definition_count });

		const Code2Id begin = id_from_code2_raw(core, begin_attach);

		AstDirectChildIterator it = direct_children_of(node);

		if (!has_next(&it))
			return emit_code2_ref(core, Code2AttNonaryOp{ Code2Op::Void });

		do
		{
			AstNode* const statement = next(&it);

			const bool is_last = !has_next(&it);

			if (statement->tag == AstTag::Definition)
			{
				if (is_last)
					TODO("Decide how to handle trailing definitions in a block.");

				const DefinitionInfo info = get_definition_info(statement);

				ASSERT_OR_IGNORE(is_some(info.value));

				const bool is_mut = has_flag(statement, AstFlag::Definition_IsMut);

				const bool has_self_reference = has_flag(statement, AstFlag::Definition_HasSelfReference);

				const Code2Id type = is_some(info.type)
					? code2_from_expr(core, get(info.type), begin_attach, LocationMode::Value)
					: Code2Id::INVALID;

				const Code2Id value = code2_from_expr(core, get(info.value), begin_attach, LocationMode::Value);

				const Code2AttDefineLocal_Flags flags{ is_mut, has_self_reference };

				if (is_some(info.type))
					(void) emit_code2_ref(core, Code2AttDefineLocalTyped{ code2_id_with(begin, Code2Op::DefineLocalTyped), code2_id_with(value, flags), type, definition_count });
				else
					(void) emit_code2_ref(core, Code2AttDefineLocalUntyped{ code2_id_with(begin, Code2Op::DefineLocalUntyped), code2_id_with(value, flags), definition_count });
			}
			else
			{
				const Code2Id value = code2_from_expr(core, statement, begin_attach, LocationMode::Value);

				if (is_last)
				{
					const Code2Id end = emit_code2_ref(core, Code2AttScopeEnd{ code2_id_with(begin, Code2Op::ScopeEnd) });

					begin_attach->scope_end = code2_id_with(end, Code2Op::ScopeBegin);

					return value;
				}

				(void) emit_code2(core, Code2AttUnaryOp{ code2_id_with(value, Code2Op::ConsumeVoid) });
			}
		}
		while (has_next(&it));
	}

	case AstTag::If:
	{
		const IfInfo info = get_if_info(node);

		if (is_some(info.where))
			scope = code2_from_where_prefix(core, get(info.where));

		const Code2Id condition = code2_from_expr(core, info.condition, scope, LocationMode::Value);

		Code2AttIf* const attach = emit_code2(core, Code2AttIf{ code2_id_with(condition, Code2Op::If), Code2Id::INVALID, Code2Id::INVALID });

		fixup_expr_own_scope(core, &attach->consequent, info.consequent, scope);

		if (is_some(info.alternative))
			fixup_expr_own_scope(core, &attach->alternative, get(info.alternative), scope);
		else
			fixup_empty_expr(core, &attach->alternative, scope);

		if (is_some(info.where))
			code2_from_where_suffix(core, scope);

		return id_from_code2_raw(core, attach);
	}

	case AstTag::For:
	{
		const ForInfo info = get_for_info(node);

		if (is_some(info.where))
			scope = code2_from_where_prefix(core, get(info.where));

		Code2AttLoop* const attach = emit_code2(core, Code2AttLoop{ Code2Op::Loop, Code2Id::INVALID, Code2Id::INVALID, Code2Id::INVALID });

		fixup_expr_own_scope(core, &attach->condition, info.condition, scope);

		fixup_loop_body_and_step(core, &attach->body, info.body, info.step, scope);

		if (is_some(info.finally))
			fixup_finally(core, &attach->finally, get(info.finally), scope);
		else
			fixup_empty_expr(core, &attach->finally, scope);

		if (is_some(info.where))
			code2_from_where_suffix(core, scope);

		return id_from_code2_raw(core, attach);
	}

	case AstTag::ForEach:
	{
		TODO("Implement.");
	}

	case AstTag::Switch:
	{
		TODO("Implement.");
	}

	case AstTag::Func:
	{
		AstNode* const signature_node = first_child_of(node);

		const AstSignatureData* const node_attach = attachment_of<AstSignatureData>(signature_node);

		const Code2Id signature = code2_from_signature(core, signature_node, scope);

		AstNode* body_node = next_sibling_of(signature_node);

		Code2AttFunc* const attach = emit_code2(core, Code2AttFunc{ code2_id_with(signature, Code2Op::Func), Code2Id::INVALID });

		fixup_function_body(core, &attach->body, body_node, node_attach->parameter_count, scope);

		return id_from_code2_raw(core, attach);
	}

	case AstTag::Signature:
	{
		return code2_from_signature(core, node, scope);
	}

	case AstTag::Trait:
	{
		AstNode* const parameters = first_child_of(node);

		u8 parameter_count = 0;

		for (AstDirectChildIterator it = direct_children_of(parameters); has_next(&it); (void) next(&it))
		{
			ASSERT_OR_IGNORE(parameter_count < 64);

			parameter_count += 1;
		}

		u16 member_count = 0;

		AstNode* member = parameters;

		while (has_next_sibling(member))
		{
			member = next_sibling_of(member);

			ASSERT_OR_IGNORE(member->tag == AstTag::Definition);

			ASSERT_OR_IGNORE(member_count < UINT16_MAX);

			member_count += 1;
		}

		const u32 attach_size = sizeof(Code2AttTrait)
		                      + static_cast<u64>(parameter_count) * sizeof(IdentifierId)
		                      + static_cast<u64>(member_count) * sizeof(Code2AttTrait_Member);

		Code2AttTrait* const attach = static_cast<Code2AttTrait*>(emit_code2_raw(core, attach_size));
		attach->op = Code2Op::Trait;
		attach->parameter_count = parameter_count;
		attach->member_count = member_count;

		u8 parameter_index = 0;

		AstDirectChildIterator it = direct_children_of(parameters);

		while (has_next(&it))
		{
			AstNode* const parameter = next(&it);

			ASSERT_OR_IGNORE(parameter->tag == AstTag::Identifier);

			attach->parameters[parameter_index] = attachment_of<AstIdentifierData>(parameter)->identifier_id;

			parameter_index += 1;
		}

		ASSERT_OR_IGNORE(parameter_index == parameter_count);

		const u64 mark = temp_stack_mark(core);

		MemberSortInfo* const order = static_cast<MemberSortInfo*>(temp_stack_alloc(core, static_cast<u64>(member_count) * sizeof(MemberSortInfo), alignof(MemberSortInfo)));

		u16 member_index = 0;

		member = parameters;

		while (has_next_sibling(member))
		{
			member = next_sibling_of(member);

			const IdentifierId name = attachment_of<AstDefinitionData>(member)->identifier_id;

			order[member_index] = MemberSortInfo{ static_cast<u32>(name), member_index };

			member_index += 1;
		}

		ASSERT_OR_IGNORE(member_index == member_count);

		inplace_sort<MemberSortInfo, MemberSortByName>(MutRange{ order, member_count });

		for (u16 i = 0; i != member_count; ++i)
			order[i].name_or_ordered_index = i;

		inplace_sort<MemberSortInfo, MemberSortByAst>(MutRange{ order, member_count });

		Code2AttTrait_Member* const attach_members = reinterpret_cast<Code2AttTrait_Member*>(attach->parameters + parameter_count);

		u16 order_index = 0;

		member = parameters;

		while (has_next_sibling(member))
		{
			member = next_sibling_of(member);

			const DefinitionInfo info = get_definition_info(member);

			ASSERT_OR_IGNORE(is_some(info.type));

			ASSERT_OR_IGNORE(order_index < member_count);

			const u32 ordered_index = order[order_index].name_or_ordered_index;

			ASSERT_OR_IGNORE(ordered_index < member_count);

			order_index += 1;

			const IdentifierId name = attachment_of<AstDefinitionData>(member)->identifier_id;

			const bool is_mut = has_flag(member, AstFlag::Definition_IsMut);

			const bool is_pub = has_flag(member, AstFlag::Definition_IsPub);

			attach_members[ordered_index] = Code2AttTrait_Member{ name, is_mut, is_pub, Code2Id::INVALID, none<Code2Id>() };

			fixup_trait_member_type(core, &attach_members[ordered_index].type, get(info.type), scope);

			if (is_some(info.value))
				fixup_trait_member_default_value(core, reinterpret_cast<Code2Id*>(&attach_members[ordered_index].default_value), get(info.value), scope);
		}

		ASSERT_OR_IGNORE(order_index == member_count);

		temp_stack_release(core, mark);

		return id_from_code2_raw(core, attach);
	}

	case AstTag::Impl:
	{
		AstNode* const base_node = first_child_of(node);

		const Code2Id base = code2_from_expr(core, base_node, scope, LocationMode::Value);

		AstNode* const trait_call_node = next_sibling_of(base_node);

		const Code2Id trait_call = code2_from_call_generic(core, trait_call_node, scope, Code2Op::ImplTraitCall);

		u16 member_count = 0;

		AstNode* member = trait_call_node;

		while (has_next_sibling(member))
		{
			member = next_sibling_of(member);

			ASSERT_OR_IGNORE(member->tag == AstTag::Definition);

			ASSERT_OR_IGNORE(member_count < UINT16_MAX);

			member_count += 1;
		}

		const u64 attach_size = sizeof(Code2AttImpl) + member_count * sizeof(Code2AttImpl_Member);

		Code2AttImpl* const attach = static_cast<Code2AttImpl*>(emit_code2_raw(core, attach_size));
		attach->base = code2_id_with(base, Code2Op::Impl);
		attach->trait_call = trait_call;
		attach->member_count = member_count;

		const u64 mark = temp_stack_mark(core);

		MemberSortInfo* const order = static_cast<MemberSortInfo*>(temp_stack_alloc(core, static_cast<u64>(member_count) * sizeof(MemberSortInfo), alignof(MemberSortInfo)));

		u16 member_index = 0;

		member = trait_call_node;

		while (has_next_sibling(member))
		{
			member = next_sibling_of(member);

			const IdentifierId name = attachment_of<AstDefinitionData>(member)->identifier_id;

			order[member_index] = MemberSortInfo{ static_cast<u32>(name), member_index };

			member_index += 1;
		}

		ASSERT_OR_IGNORE(member_index == member_count);

		inplace_sort<MemberSortInfo, MemberSortByName>(MutRange{ order, member_count });

		for (u16 i = 0; i != member_count; ++i)
			order[i].name_or_ordered_index = i;

		inplace_sort<MemberSortInfo, MemberSortByAst>(MutRange{ order, member_count });

		u16 order_index = 0;

		member = trait_call_node;

		while (has_next_sibling(member))
		{
			member = next_sibling_of(member);

			const DefinitionInfo info = get_definition_info(member);

			ASSERT_OR_IGNORE(is_some(info.type));

			ASSERT_OR_IGNORE(order_index < member_count);

			const u32 ordered_index = order[order_index].name_or_ordered_index;

			ASSERT_OR_IGNORE(ordered_index < member_count);

			order_index += 1;

			const IdentifierId name = attachment_of<AstDefinitionData>(member)->identifier_id;

			const bool is_mut = has_flag(member, AstFlag::Definition_IsMut);

			const bool is_pub = has_flag(member, AstFlag::Definition_IsPub);

			attach->members[ordered_index] = Code2AttImpl_Member{ name, is_mut, is_pub, none<Code2Id>(), Code2Id::INVALID };

			if (is_some(info.type))
				fixup_impl_member_type(core, reinterpret_cast<Code2Id*>(&attach->members[ordered_index].type), get(info.type), scope);

			fixup_impl_member_default_value(core, reinterpret_cast<Code2Id*>(&attach->members[ordered_index].value), get(info.value), scope);
		}

		ASSERT_OR_IGNORE(order_index == member_count);

		temp_stack_release(core, mark);

		return id_from_code2_raw(core, attach);
	}

	case AstTag::Self:
	{
		return emit_code2_ref(core, Code2AttNonaryOp{ Code2Op::Self });
	}

	case AstTag::Catch:
	{
		TODO("Implement.");
	}

	case AstTag::Unreachable:
	{
		return emit_code2_ref(core, Code2AttNonaryOp{ Code2Op::Unreachable });
	}

	case AstTag::Undefined:
	{
		return emit_code2_ref(core, Code2AttNonaryOp{ Code2Op::Undefined });
	}

	case AstTag::Identifier:
	{
		const AstIdentifierData* const node_attach = attachment_of<AstIdentifierData>(node);

		const NameBinding binding = node_attach->binding;

		if (has_flag(node, AstFlag::Identifier_IsTemplatedParamRef))
		{
			ASSERT_OR_IGNORE(binding.kind == NameBindingKind::Scoped);

			const Code2Op op = location_mode == LocationMode::Value ? Code2Op::TemplatedParamValue : Code2Op::TemplatedParamLocation;

			return emit_code2_ref(core, Code2AttTemplatedParamRef{ op, binding.scoped.rank });
		}
		else if (binding.kind == NameBindingKind::Scoped)
		{
			const Code2Op op = location_mode == LocationMode::Value ? Code2Op::LocalValue : Code2Op::LocalLocation;

			return emit_code2_ref(core, Code2AttLocalRef{ op, binding.scoped.out, binding.scoped.rank });
		}
		else if (binding.kind == NameBindingKind::Global)
		{
			const Code2Op op = location_mode == LocationMode::Value ? Code2Op::GlobalValue : Code2Op::GlobalLocation;

			return emit_code2_ref(core, Code2AttGlobalRef{ op, binding.global.rank });
		}
		else if (binding.kind == NameBindingKind::Closed)
		{
			const Code2Op op = location_mode == LocationMode::Value ? Code2Op::ClosedValue : Code2Op::ClosedLocation;

			return emit_code2_ref(core, Code2AttClosedRef{ op, binding.closed.rank_in_closure });
		}
		else
		{
			ASSERT_OR_IGNORE(binding.kind == NameBindingKind::TraitArgument);

			const Code2Op op = location_mode == LocationMode::Value ? Code2Op::TraitArgumentValue : Code2Op::TraitArgumentLocation;

			return emit_code2_ref(core, Code2AttTraitArgumentRef{ op, binding.trait_argument.rank });
		}
	}

	case AstTag::LitInteger:
	{
		const CompIntegerValue value = attachment_of<AstLitIntegerData>(node)->value;

		ASSERT_OR_IGNORE(comp_integer_compare(value, comp_integer_from_u64(0)) != StrongCompareOrdering::LessThan);

		u64 raw_value;

		if (u64_from_comp_integer(value, 16, &raw_value))
		{
			return emit_code2_ref(core, Code2AttInteger16{ Code2Op::Integer16, static_cast<u16>(raw_value) });
		}
		else if (u64_from_comp_integer(value, 64, &raw_value))
		{
			return emit_code2_ref(core, Code2AttInteger64{ Code2Op::Integer64, static_cast<u32>(raw_value), static_cast<u32>(raw_value >> 32) });
		}
		else
		{
			Code2AttIntegerHuge* const attach = emit_code2(core, Code2AttIntegerHuge{ Code2Op::IntegerHuge, {} });

			memcpy(attach->value_bytes, &value, sizeof(CompIntegerValue));

			return id_from_code2_raw(core, attach);
		}
	}

	case AstTag::LitFloat:
	{
		const u64 raw_value = *reinterpret_cast<const u64*>(&attachment_of<AstLitFloatData>(node)->value);

		return emit_code2_ref(core, Code2AttFloat{ Code2Op::Float, static_cast<u32>(raw_value), static_cast<u32>(raw_value >> 32) });
	}

	case AstTag::LitChar:
	{
		const u32 value = attachment_of<AstLitCharData>(node)->codepoint;

		return value <= UINT16_MAX
			? emit_code2_ref(core, Code2AttInteger16{ Code2Op::Integer16, static_cast<u16>(value) })
			: emit_code2_ref(core, Code2AttInteger64{ Code2Op::Integer64, value });
	}

	case AstTag::LitString:
	{
		const AstLitStringData* const attach = attachment_of<AstLitStringData>(node);

		return emit_code2_ref(core, Code2AttString{ Code2Op::String, attach->value_id, attach->value_size });
	}

	case AstTag::OpSliceOf:
	{
		const OpSliceOfInfo info = get_op_slice_of_info(node);

		const Code2Id sliced = code2_from_expr(core, info.sliced, scope, LocationMode::AddressSrc);

		const Maybe<Code2Id> begin = is_some(info.begin)
			? some(code2_from_expr(core, get(info.begin), scope, LocationMode::Value))
			: none<Code2Id>();

		const Maybe<Code2Id> end = is_some(info.end)
			? some(code2_from_expr(core, get(info.end), scope, LocationMode::Value))
			: none<Code2Id>();

		return emit_code2_ref(core, Code2AttSlice{ code2_id_with(sliced, Code2Op::Slice), begin, end });
	}

	case AstTag::Return:
	{
		AstNode* const return_value_node = first_child_of(node);

		const Code2Id return_value = code2_from_expr(core, return_value_node, scope, LocationMode::Value);

		return emit_code2_ref(core, Code2AttUnaryOp{ code2_id_with(return_value, Code2Op::Return) });
	}

	case AstTag::Leave:
	{
		TODO("Implement.");
	}

	case AstTag::Yield:
	{
		TODO("Implement.");
	}

	case AstTag::Call:
	{
		return code2_from_call_generic(core, node, scope, Code2Op::Call);
	}

	case AstTag::UOpTypeTailArray:
	case AstTag::UOpTypeSlice:
	case AstTag::UOpTypeMultiPtr:
	case AstTag::UOpTypeOptMultiPtr:
	case AstTag::UOpTypeOptPtr:
	case AstTag::UOpTypePtr:
	{
		const bool is_mut = has_flag(node, AstFlag::Type_IsMut);

		const bool is_opt = tag == AstTag::UOpTypeOptPtr || tag == AstTag::UOpTypeOptMultiPtr;

		const bool is_multi = tag == AstTag::UOpTypeMultiPtr || tag == AstTag::UOpTypeOptMultiPtr;

		AstNode* const operand_node = first_child_of(node);

		const Code2Id operand = code2_from_expr(core, operand_node, scope, LocationMode::Value);

		const Code2Op op = tag == AstTag::UOpTypeSlice
			? tag == AstTag::UOpTypeTailArray ? Code2Op::TypeTailArray : Code2Op::TypeSlice
			: Code2Op::TypePtr;

		return emit_code2_ref(core, Code2AttReferenceType{ code2_id_with(operand, op), is_mut, is_opt, is_multi });
	}

	case AstTag::UOpTry:
	{
		TODO("Implement.");
	}

	case AstTag::UOpEval:
	case AstTag::UOpDefer:
	case AstTag::UOpDistinct:
	case AstTag::UOpAddr:
	case AstTag::UOpDeref:
	case AstTag::UOpBitNot:
	case AstTag::UOpLogNot:
	case AstTag::UOpNegate:
	case AstTag::UOpPos:
	{
		AstNode* const operand_node = first_child_of(node);

		const LocationMode operand_location_mode = tag == AstTag::UOpAddr ? LocationMode::AddressSrc : LocationMode::Value;

		const Code2Id operand = code2_from_expr(core, operand_node, scope, operand_location_mode);

		Code2Op op;

		if (tag >= AstTag::UOpDefer && tag <= AstTag::UOpLogNot)
			op = static_cast<Code2Op>(static_cast<u8>(Code2Op::Defer) + static_cast<u8>(tag) - static_cast<u8>(AstTag::UOpDefer));
		else if (tag == AstTag::UOpEval)
			op = Code2Op::Eval;
		else if (tag == AstTag::UOpNegate)
			op = Code2Op::Negate;
		else if (tag == AstTag::UOpPos)
			op = Code2Op::UnaryPlus;
		else
			ASSERT_UNREACHABLE;

		const Code2Id dereferenced = emit_code2_ref(core, Code2AttUnaryOp{ code2_id_with(operand, op) });

		return tag == AstTag::UOpDeref && location_mode == LocationMode::Value
			? emit_code2_ref(core, Code2AttUnaryOp{ code2_id_with(dereferenced, Code2Op::LoadLocation) })
			: dereferenced;
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
	case AstTag::OpShiftL:
	case AstTag::OpShiftR:
	case AstTag::OpTypeArray:
	case AstTag::OpArrayIndex:
	{
		AstNode* const lhs_node = first_child_of(node);

		const LocationMode lhs_location_mode = tag == AstTag::OpArrayIndex ? location_mode : LocationMode::Value;

		const Code2Id lhs = code2_from_expr(core, lhs_node, scope, lhs_location_mode);

		AstNode* const rhs_node = next_sibling_of(lhs_node);

		const Code2Id rhs = code2_from_expr(core, rhs_node, scope, LocationMode::Value);

		Code2Op op;

		if (tag >= AstTag::OpAdd && tag <= AstTag::OpShiftR)
			op = static_cast<Code2Op>(static_cast<u8>(Code2Op::Add) + static_cast<u8>(tag) - static_cast<u8>(AstTag::OpAdd));
		else if (tag == AstTag::OpTypeArray)
			op = Code2Op::TypeArray;
		else if (tag == AstTag::OpArrayIndex)
			op = Code2Op::Index;
		else
			ASSERT_UNREACHABLE;

		const Code2Id result = emit_code2_ref(core, Code2AttBinaryOp{ code2_id_with(lhs, op), rhs });

		return tag == AstTag::OpArrayIndex && location_mode == LocationMode::Value
			? emit_code2_ref(core, Code2AttUnaryOp{ code2_id_with(result, Code2Op::LoadLocation) })
			: result;
	}

	case AstTag::OpLogAnd:
	case AstTag::OpLogOr:
	{
		AstNode* const lhs_node = first_child_of(node);

		const Code2Id lhs = code2_from_expr(core, lhs_node, scope, LocationMode::Value);

		AstNode* const rhs_node = next_sibling_of(lhs_node);

		const Code2Op op = tag == AstTag::OpLogAnd ? Code2Op::LogicalAnd : Code2Op::LogicalOr;

		Code2AttLogicalOp* const attach = emit_code2(core, Code2AttLogicalOp{ code2_id_with(lhs, op), Code2Id::INVALID });

		fixup_expr_shared_scope(core, &attach->rhs, rhs_node, scope);

		return id_from_code2_raw(core, attach);
	}

	case AstTag::Member:
	{
		const IdentifierId name = attachment_of<AstMemberData>(node)->identifier_id;

		AstNode* const composite_node = first_child_of(node);

		const Code2Id composite = code2_from_expr(core, composite_node, scope, location_mode);

		const Code2Op op = location_mode == LocationMode::Value ? Code2Op::MemberValue : Code2Op::MemberLocation;

		return emit_code2_ref(core, Code2AttMember{ code2_id_with(composite, op), name });
	}

	case AstTag::OpCmpLT:
	case AstTag::OpCmpGT:
	case AstTag::OpCmpLE:
	case AstTag::OpCmpGE:
	case AstTag::OpCmpNE:
	case AstTag::OpCmpEQ:
	{
		AstNode* const lhs_node = first_child_of(node);

		const Code2Id lhs = code2_from_expr(core, lhs_node, scope, LocationMode::Value);

		AstNode* const rhs_node = next_sibling_of(lhs_node);

		const Code2Id rhs = code2_from_expr(core, rhs_node, scope, LocationMode::Value);

		const Code2AttCompare_Kind kind = static_cast<Code2AttCompare_Kind>(1 + static_cast<u8>(tag) - static_cast<u8>(AstTag::OpCmpLT));

		return emit_code2_ref(core, Code2AttCompare{ code2_id_with(lhs, Code2Op::Compare), code2_id_with(rhs, kind) });
	}

	case AstTag::OpSet:
	{
		AstNode* const lhs_node = first_child_of(node);

		const Code2Id lhs = code2_from_expr(core, lhs_node, scope, LocationMode::AssignmentDst);

		AstNode* const rhs_node = next_sibling_of(lhs_node);

		const Code2Id rhs = code2_from_expr(core, rhs_node, scope, LocationMode::Value);

		return emit_code2_ref(core, Code2AttBinaryOp{ code2_id_with(lhs, Code2Op::Set), rhs });
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
		AstNode* const lhs_node = first_child_of(node);

		const Code2Id lhs_location = code2_from_expr(core, lhs_node, scope, LocationMode::AssignmentDst);

		const Code2Id lhs = emit_code2_ref(core, Code2AttUnaryOp{ code2_id_with(lhs_location, Code2Op::LoadLocation) });

		AstNode* const rhs_node = next_sibling_of(lhs_node);

		const Code2Id rhs = code2_from_expr(core, rhs_node, scope, LocationMode::Value);

		const Code2Op op = static_cast<Code2Op>(static_cast<u8>(Code2Op::Add) + static_cast<u8>(tag) - static_cast<u8>(AstTag::OpSetAdd));

		const Code2Id result = emit_code2_ref(core, Code2AttBinaryOp{ code2_id_with(lhs, op), rhs });

		return emit_code2_ref(core, Code2AttBinaryOp{ code2_id_with(lhs, Code2Op::Set), result });
	}

	case AstTag::INVALID:
	case AstTag::File:
	case AstTag::Where:
	case AstTag::Requires:
	case AstTag::Ensures:
	case AstTag::Case:
	case AstTag::ParameterList:
	case AstTag::TraitParameterList:
	case AstTag::UOpTypeVarArgs:
	case AstTag::ImpliedMember:
	case AstTag::MAX:
		; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

static Code2Id code2_from_expr(CoreData* core, AstNode* node, Code2AttScopeBegin* scope, LocationMode location_mode) noexcept
{
	const AstTag tag = node->tag;

	if (location_mode == LocationMode::AssignmentDst)
	{
		if (tag != AstTag::Identifier && tag != AstTag::UOpDeref && tag != AstTag::Member && tag != AstTag::OpArrayIndex)
		{
			record_error(core, node, CompileError::INVALID); // TODO: Error code.

			TODO("Set error flag.");
		}
	}

	const Code2Id ref = code2_from_expr_raw(core, node, scope, location_mode);

	if (location_mode != LocationMode::AddressSrc
	 || tag == AstTag::Identifier
	 || tag == AstTag::UOpDeref
	 || tag == AstTag::Member
	 || tag == AstTag::OpArrayIndex
	) {
		return ref;
	}

	const Code2AttDefineLocal_Flags flags{ false, false };

	const Code2Id begin = id_from_code2_raw(core, scope);

	const u16 definition_index = scope->definition_count;

	ASSERT_OR_IGNORE(scope->definition_count != UINT32_MAX);

	scope->definition_count += 1;

	(void) emit_code2(core, Code2AttDefineLocalUntyped{ code2_id_with(begin, Code2Op::DefineLocalUntyped), code2_id_with(ref, flags), definition_index });

	return emit_code2_ref(core, Code2AttLocalRef{ Code2Op::LocalLocation, 0, definition_index });
}



static void code2_from_fixup(CoreData* core, Code2Fixup fixup) noexcept
{
	switch (fixup.tag)
	{
	case Code2FixupTag::ExprSharedScope:
	{
		AstNode* const node = ast_node_from_id(core, get(fixup.src_1));

		*fixup.dst = code2_from_expr(core, node, fixup.scope, LocationMode::Value);

		(void) emit_code2(core, Code2AttNonaryOp{ Code2Op::End });

		return;
	}

	case Code2FixupTag::EmptyExpr:
	{
		*fixup.dst = emit_code2_ref(core, Code2AttNonaryOp{ Code2Op::Void });

		(void) emit_code2(core, Code2AttNonaryOp{ Code2Op::End });

		return;
	}

	case Code2FixupTag::LoopBodyAndStep:
	{
		Code2AttScopeBegin* const begin_attach = emit_code2(core, Code2AttScopeBegin{ code2_id_with(Code2Id::INVALID, Code2Op::ScopeBegin), 0 });

		AstNode* const body_node = ast_node_from_id(core, get(fixup.src_1));

		(void) code2_from_expr(core, body_node, begin_attach, LocationMode::Value);

		if (is_some(fixup.src_2))
		{
			AstNode* const step_node = ast_node_from_id(core, get(fixup.src_2));

			(void) code2_from_expr(core, step_node, begin_attach, LocationMode::Value);
		}

		const Code2Id begin = id_from_code2_raw(core, begin_attach);

		const Code2Id end = emit_code2_ref(core, Code2AttScopeEnd{ code2_id_with(begin, Code2Op::ScopeEnd) });

		begin_attach->scope_end = code2_id_with(end, Code2Op::ScopeBegin);

		(void) emit_code2(core, Code2AttNonaryOp{ Code2Op::End });

		*fixup.dst = id_from_code2_raw(core, begin_attach);

		return;
	}

	case Code2FixupTag::ExprOwnScope:
	case Code2FixupTag::FunctionBody:
	{
		Code2AttScopeBegin* const begin_attach = emit_code2(core, Code2AttScopeBegin{ code2_id_with(Code2Id::INVALID, Code2Op::ScopeBegin), fixup.function_body_parameter_count });

		AstNode* const node = ast_node_from_id(core, get(fixup.src_1));

		(void) code2_from_expr(core, node, begin_attach, LocationMode::Value);

		const Code2Id begin = id_from_code2_raw(core, begin_attach);

		const Code2Id end = emit_code2_ref(core, Code2AttScopeEnd{ code2_id_with(begin, Code2Op::ScopeEnd) });

		begin_attach->scope_end = code2_id_with(end, Code2Op::ScopeBegin);

		(void) emit_code2(core, Code2AttNonaryOp{ Code2Op::End });

		*fixup.dst = id_from_code2_raw(core, begin_attach);

		return;
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
		const Code2Fixup fixup = core->code2.fixups.end()[-1];

		core->code2.fixups.pop_by(1);

		code2_from_fixup(core, fixup);
	}
}



bool type2_validate_config([[maybe_unused]] const Config* config, [[maybe_unused]] PrintSink sink) noexcept
{
	return true;
}

MemoryRequirements type2_memory_requirements([[maybe_unused]] const Config* config) noexcept
{
	MemoryRequirements reqs;
	reqs.count = 2;
	reqs.ranges[0].size = CODES_RESERVE_SIZE;
	reqs.ranges[0].max_offset = 1 << 26;
	reqs.ranges[1].size = FIXUPS_RESERVE_SIZE;
	reqs.ranges[1].max_offset = UINT64_MAX;

	return reqs;
}

void type2_init(CoreData* core, MemoryAllocation allocation) noexcept
{
	ASSERT_OR_IGNORE(allocation.ranges[0].count() == CODES_RESERVE_SIZE);

	ASSERT_OR_IGNORE(allocation.ranges[1].count() == FIXUPS_RESERVE_SIZE);

	core->code2.codes.init(allocation.ranges[0], CODES_COMMIT_INCREMENT_COUNT);

	core->code2.fixups.init(allocation.ranges[1], FIXUPS_COMMIT_INCREMENT_COUNT);
}



Code2GlobalDefinition code2_from_top_level_definition(CoreData* core, AstNode* node, SourceFileId file) noexcept
{
	const DefinitionInfo info = get_definition_info(node);

	ASSERT_OR_IGNORE(is_some(info.value));

	Code2AttScopeBegin* const begin_attach = emit_code2(core, Code2AttScopeBegin{ code2_id_with(Code2Id::INVALID, Code2Op::ScopeBegin), 0 });

	const Maybe<Code2Id> type = is_some(info.type)
		? some(code2_from_expr(core, get(info.type), begin_attach, LocationMode::Value))
		: none<Code2Id>();

	const Code2Id value = code2_from_expr(core, get(info.value), begin_attach, LocationMode::Value);

	const Code2Id begin = id_from_code2_raw(core, begin_attach);

	const Code2Id end = emit_code2_ref(core, Code2AttScopeEnd{ code2_id_with(begin, Code2Op::ScopeEnd) });

	begin_attach->scope_end = code2_id_with(end, Code2Op::ScopeBegin);

	(void) emit_code2(core, Code2AttNonaryOp{ Code2Op::End });

	code2_from_fixups(core);

	return Code2GlobalDefinition{ file, begin, type, value };
}



const char8* tag_name(Code2Op op) noexcept
{
	static constexpr char8* TAG_NAMES[] = { 
		"[Unknown]",
		"End",
		"ScopeBegin",
		"ScopeEnd",
		"Builtin",
		"Composite",
		"Array",
		"Definition",
		"DefineLocalTyped",
		"DefineLocalUntyped",
		"ConsumeVoid",
		"Void",
		"If",
		"Loop",
		"Func",
		"SignatureBegin",
		"SignatureEnd",
		"Parameter",
		"Trait",
		"Impl",
		"ImplTraitCall",
		"Self",
		"Unreachable",
		"Undefined",
		"TemplatedParamValue",
		"TemplatedParamLocation",
		"LocalValue",
		"LocalLocation",
		"GlobalValue",
		"GlobalLocation",
		"ClosedValue",
		"ClosedLocation",
		"TraitArgumentValue",
		"TraitArgumentLocation",
		"Slice",
		"Return",
		"Integer16",
		"Integer64",
		"IntegerHuge",
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
		"ShiftLeft",
		"ShiftRight",
		"TypeArray",
		"Index",
		"LogicalAnd",
		"LogicalOr",
		"MemberValue",
		"MemberLocation",
		"Compare",
		"Set",
		"LoadLocation",
	};

	u8 ordinal = static_cast<u8>(op);

	if (ordinal >= array_count(TAG_NAMES))
		ordinal = 0;

	return TAG_NAMES[ordinal];
}

const char8* tag_name(Code2AttCompare_Kind kind) noexcept
{
	static constexpr char8* TAG_NAMES[] = {
		"[Unknown]",
		"LessThan",
		"GreaterThan",
		"LessThanOrEqual",
		"GreaterThanOrEqual",
		"NotEqual",
		"Equal",
	};

	u8 ordinal = static_cast<u8>(kind);

	if (ordinal >= array_count(TAG_NAMES))
		ordinal = 0;

	return TAG_NAMES[ordinal];
}
