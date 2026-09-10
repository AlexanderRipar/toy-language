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
	Expr,
	EmptyExpr,
	Loop,
	Finally,
	FunctionBody,
};

struct Code2Fixup
{
	Code2FixupTag tag;

	u8 function_body_parameter_count;

	Maybe<AstNode*> src;

	Code2Id* dst;

	Code2AttScopeBegin* scope;
};





Code2Id id_from_code2_chunk(CoreData* core, const Code2Header* header) noexcept
{
	ASSERT_OR_IGNORE(header >= reinterpret_cast<Code2Header*>(core->code2.codes.begin()));

	ASSERT_OR_IGNORE(header < reinterpret_cast<Code2Header*>(core->code2.codes.end()));

	ASSERT_OR_IGNORE(header > reinterpret_cast<Code2Header*>(core));

	const u64 offset = reinterpret_cast<const u32*>(header) - reinterpret_cast<const u32*>(core);

	ASSERT_OR_IGNORE(offset > 0 && offset <= UINT32_MAX);

	return static_cast<Code2Id>(offset);
}

Code2Header* code2_chunk_from_id(CoreData* core, Code2Id id) noexcept
{
	ASSERT_OR_IGNORE(id != Code2Id::INVALID);

	Code2Header* const header = reinterpret_cast<Code2Header*>(reinterpret_cast<u32*>(core) + static_cast<u32>(id));

	ASSERT_OR_IGNORE(header >= reinterpret_cast<Code2Header*>(core->code2.codes.begin()));

	ASSERT_OR_IGNORE(header < reinterpret_cast<Code2Header*>(core->code2.codes.end()));

	ASSERT_OR_IGNORE(header > reinterpret_cast<Code2Header*>(core));

	return header;
}



Code2Offset offset_from_code2(const Code2Header* header, const Code2* data) noexcept
{
	ASSERT_OR_IGNORE(reinterpret_cast<const u32*>(header) < reinterpret_cast<const u32*>(data));

	return static_cast<Code2Offset>(reinterpret_cast<const u32*>(data) - reinterpret_cast<const u32*>(header));
}

Code2* code2_from_offset(Code2Header* header, Code2Offset offset) noexcept
{
	ASSERT_OR_IGNORE(offset != Code2Offset::INVALID);

	return reinterpret_cast<Code2*>(reinterpret_cast<u32*>(header) + static_cast<u32>(offset));
}

const Code2* code2_from_offset(const Code2Header* header, Code2Offset offset) noexcept
{
	return code2_from_offset(const_cast<Code2Header*>(header), offset);
}



Code2* code2_chunk_begin(Code2Header* header) noexcept
{
	return reinterpret_cast<Code2*>(header + 1);
}

const Code2* code2_chunk_begin(const Code2Header* header) noexcept
{
	return code2_chunk_begin(const_cast<Code2Header*>(header));
}

Code2* code2_chunk_end(Code2Header* header) noexcept
{
	return reinterpret_cast<Code2*>(reinterpret_cast<u32*>(header) + header->size);
}

const Code2* code2_chunk_end(const Code2Header* header) noexcept
{
	return code2_chunk_end(const_cast<Code2Header*>(header));
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
static Code2Offset emit_code2_offset(CoreData* core, const Code2Header* header, T data) noexcept
{
	void* const attach = emit_code2(core, data);

	return static_cast<Code2Offset>(static_cast<const u32*>(attach) - reinterpret_cast<const u32*>(header));
}

static Code2Header* emit_header(CoreData* core) noexcept
{
	return emit_code2(core, Code2Header{});
}

static Code2Id complete_header_delayed_leave(CoreData* core, Code2Header* header) noexcept
{
	header->size = static_cast<u32>(core->code2.codes.end() - reinterpret_cast<u32*>(header));
	header->last_leave = Code2Offset::INVALID;

	return id_from_code2_chunk(core, header);
}

static Code2Id complete_header_with_leave(CoreData* core, Code2Header* header, Code2Offset last_leave) noexcept
{
	header->size = static_cast<u32>(core->code2.codes.end() - reinterpret_cast<u32*>(header));
	header->last_leave = last_leave;

	return id_from_code2_chunk(core, header);
}





static void fixup_generic(CoreData* core, Code2FixupTag tag, u8 function_body_parameter_count, Code2Id* dst, Maybe<AstNode*> src, Code2AttScopeBegin* scope) noexcept
{
	Code2Fixup* const fixup = core->code2.fixups.reserve();
	fixup->tag = tag;
	fixup->function_body_parameter_count = function_body_parameter_count;
	fixup->src = src;
	fixup->dst = dst;
	fixup->scope = scope;
}

static void fixup_expr_shared_scope(CoreData* core, Code2Id* dst, AstNode* src, Code2AttScopeBegin* scope) noexcept
{
	fixup_generic(core, Code2FixupTag::Expr, 0, dst, some(src), scope);
}

static void fixup_expr_own_scope(CoreData* core, Code2Id* dst, AstNode* src, Code2AttScopeBegin* scope) noexcept
{
	fixup_generic(core, Code2FixupTag::Expr, 0, dst, some(src), scope);
}

static void fixup_empty_expr(CoreData* core, Code2Id* dst, Code2AttScopeBegin* scope) noexcept
{
	fixup_generic(core, Code2FixupTag::EmptyExpr, 0, dst, none<AstNode*>(), scope);
}

static void fixup_loop(CoreData* core, Code2Id* dst, AstNode* src, Code2AttScopeBegin* scope) noexcept
{
	fixup_generic(core, Code2FixupTag::Loop, 0, dst, some(src), scope);
}

static void fixup_finally(CoreData* core, Code2Id* dst, Maybe<AstNode*> src, Code2AttScopeBegin* scope) noexcept
{
	fixup_generic(core, Code2FixupTag::Finally, 0, dst, src, scope);
}

static void fixup_function_body(CoreData* core, Code2Id* dst, AstNode* src, u8 parameter_count, Code2AttScopeBegin* scope) noexcept
{
	fixup_generic(core, Code2FixupTag::FunctionBody, parameter_count, dst, some(src), scope);
}

static void fixup_argument(CoreData* core, Code2Id* dst, AstNode* src, Code2AttScopeBegin* scope) noexcept
{
	fixup_generic(core, Code2FixupTag::Expr, 0, dst, some(src), scope);
}

static void fixup_trait_member_type(CoreData* core, Code2Id* dst, AstNode* src, Code2AttScopeBegin* scope) noexcept
{
	fixup_generic(core, Code2FixupTag::Expr, 0, dst, some(src), scope);
}

static void fixup_trait_member_default_value(CoreData* core, Code2Id* dst, AstNode* src, Code2AttScopeBegin* scope) noexcept
{
	fixup_generic(core, Code2FixupTag::Expr, 0, dst, some(src), scope);
}

static void fixup_impl_member_type(CoreData* core, Code2Id* dst, AstNode* src, Code2AttScopeBegin* scope) noexcept
{
	fixup_generic(core, Code2FixupTag::Expr, 0, dst, some(src), scope);
}

static void fixup_impl_member_default_value(CoreData* core, Code2Id* dst, AstNode* src, Code2AttScopeBegin* scope) noexcept
{
	fixup_generic(core, Code2FixupTag::Expr, 0, dst, some(src), scope);
}


static Code2Offset code2_from_expr(CoreData* core, AstNode* node, const Code2Header* header, Code2AttScopeBegin* scope, LocationMode location_mode) noexcept;

static Code2AttScopeBegin* code2_from_where_prefix(CoreData* core, AstNode* node, const Code2Header* header) noexcept
{
	const u16 definition_count = attachment_of<AstWhereData>(node)->definition_count;

	Code2AttScopeBegin attach_value{};
	attach_value.header.scope_begin.op_ = Code2Op::ScopeBegin;
	attach_value.header.scope_begin.definition_count = definition_count;

	Code2AttScopeBegin* const begin_attach = emit_code2(core, attach_value);

	AstDirectChildIterator it = direct_children_of(node);

	while (has_next(&it))
	{
		AstNode* const definition = next(&it);

		const DefinitionInfo info = get_definition_info(definition);

		ASSERT_OR_IGNORE(is_some(info.value));

		const bool is_mut = has_flag(definition, AstFlag::Definition_IsMut);

		const bool has_self_reference = has_flag(definition, AstFlag::Definition_HasSelfReference);

		const Code2Offset type = is_some(info.type)
			? code2_from_expr(core, get(info.type), header, begin_attach, LocationMode::Value)
			: Code2Offset::INVALID;

		const Code2Offset untyped_value = code2_from_expr(core, get(info.value), header, begin_attach, LocationMode::Value);

		Code2Offset value;

		if (is_some(info.type))
		{
			Code2AttBinaryOp with_type_attach_value{};
			with_type_attach_value.header.op = Code2Op::WithType;
			with_type_attach_value.lhs = type;
			with_type_attach_value.rhs = untyped_value;

			value = emit_code2_offset(core, header, with_type_attach_value);
		}
		else
		{
			value = untyped_value;
		}

		Code2AttDefineLocal define_attach_value{};
		define_attach_value.header.define_local.op_ = Code2Op::DefineLocal;
		define_attach_value.header.define_local.definition_index = definition_count;
		define_attach_value.header.define_local.is_mut = is_mut;
		define_attach_value.header.define_local.has_self_reference = has_self_reference;
		define_attach_value.value = value;

		(void) emit_code2(core, define_attach_value);
	}

	return begin_attach;
}

static void code2_from_where_suffix(CoreData* core) noexcept
{
	Code2AttNonaryOp attach_value{};
	attach_value.header.op = Code2Op::ScopeEnd;

	(void) emit_code2(core, attach_value);
}

static Code2Offset code2_from_signature(CoreData* core, AstNode* node, const Code2Header* header, Code2AttScopeBegin* scope) noexcept
{
	const SignatureInfo info = get_signature_info(node);

	const bool is_proc = has_flag(node, AstFlag::Signature_IsProc);

	const u8 parameter_count = attachment_of<AstSignatureData>(node)->parameter_count;

	Code2AttSignatureBegin begin_attach_value{};
	begin_attach_value.header.signature_begin.op_ = Code2Op::SignatureBegin;
	begin_attach_value.header.signature_begin.parameter_count = parameter_count;
	begin_attach_value.header.signature_begin.is_proc = is_proc;

	const Code2Offset signature = emit_code2_offset(core, header, begin_attach_value);

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

		const Code2Offset type = code2_from_expr(core, type_node, header, scope, LocationMode::Value);

		const Maybe<Code2Offset> value = has_value
			? some(code2_from_expr(core, get(parameter_info.value), header, scope, LocationMode::Value))
			: none<Code2Offset>();

		Code2AttParameter parameter_attach_value{};
		parameter_attach_value.header.parameter.op_ = Code2Op::Parameter;
		parameter_attach_value.header.parameter.is_mut = is_mut;
		parameter_attach_value.header.parameter.is_eval = is_eval;
		parameter_attach_value.header.parameter.is_templated = is_templated;
		parameter_attach_value.header.parameter.is_variadic = is_variadic;
		parameter_attach_value.signature = signature;
		parameter_attach_value.type = type;
		parameter_attach_value.default_value = value;
		parameter_attach_value.name = name;

		(void) emit_code2(core, parameter_attach_value);
	}

	const Code2Offset return_type = code2_from_expr(core, info.return_type, header, scope, LocationMode::Value);

	if (is_some(info.requires))
		TODO("Implement signature-level `requires`.");

	if (is_some(info.ensures))
		TODO("Implement signature-level `ensures`.");

	Code2AttSignatureEnd end_attach_value{};
	end_attach_value.header.op = Code2Op::SignatureEnd;
	end_attach_value.signature= signature;
	end_attach_value.return_type = return_type;

	return emit_code2_offset(core, header, end_attach_value);
}

static Code2Offset code2_from_call_generic(CoreData* core, AstNode* node, const Code2Header* header, Code2AttScopeBegin* scope, Code2Op op) noexcept
{
	AstNode* const callee_node = first_child_of(node);

	const Code2Offset callee = code2_from_expr(core, callee_node, header, scope, LocationMode::Value);

	u8 argument_count = 0;

	for (AstNode* argument_node = callee_node; has_next_sibling(argument_node); argument_node = next_sibling_of(argument_node))
	{
		ASSERT_OR_IGNORE(argument_count < 64);

		argument_count += 1;
	}

	const u64 attach_size = sizeof(Code2AttCall) + argument_count * sizeof(Code2AttCall_Argument);

	Code2AttCall* const attach = static_cast<Code2AttCall*>(emit_code2_raw(core, attach_size));
	attach->header.call.op_ = op;
	attach->header.call.argument_count = argument_count;
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

	return offset_from_code2(header, &attach->header);
}

static bool is_location_ast_tag(AstTag tag) noexcept
{
	return tag == AstTag::Identifier || tag == AstTag::UOpDeref || tag == AstTag::Member || tag == AstTag::OpArrayIndex;
}

static Code2Offset code2_from_expr_raw(CoreData* core, AstNode* node, const Code2Header* header, Code2AttScopeBegin* scope, LocationMode location_mode) noexcept
{
	const AstTag tag = node->tag;

	switch (tag)
	{
	case AstTag::Builtin:
	{
		const Builtin builtin = attachment_of<AstBuiltinData>(node)->builtin;

		Code2AttBuiltin attach_value{};
		attach_value.header.builtin.op_ = Code2Op::Builtin;
		attach_value.header.builtin.builtin = builtin;

		return emit_code2_offset(core, header, attach_value);
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

			const Code2Offset value = code2_from_expr(core, member, header, scope, LocationMode::Value);

			Code2AttComposite_Member* const member_dst = temp_stack_typed_alloc<Code2AttComposite_Member>(core);

			*member_dst = Code2AttComposite_Member{ name, value };
		}

		const u64 attach_size = sizeof(Code2AttComposite) + static_cast<u64>(member_count) * sizeof(Code2AttComposite_Member);

		Code2AttComposite* const attach = static_cast<Code2AttComposite*>(emit_code2_raw(core, attach_size));
		attach->header.composite.op_ = Code2Op::Composite;
		attach->header.composite.member_count = member_count;

		memcpy(attach->members, members, member_count * sizeof(Code2AttComposite_Member));

		temp_stack_release(core, mark);

		return offset_from_code2(header, &attach->header);
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

			const Code2Offset value = code2_from_expr(core, element, header, scope, LocationMode::Value);

			Code2AttArray_Element* const member_dst = temp_stack_typed_alloc<Code2AttArray_Element>(core);

			*member_dst = Code2AttArray_Element{ value };
		}

		const u64 attach_size = sizeof(Code2AttArray) + static_cast<u64>(element_count) * sizeof(Code2AttArray_Element);

		Code2AttArray* const attach = static_cast<Code2AttArray*>(emit_code2_raw(core, attach_size));
		attach->header.array.op_ = Code2Op::Array;
		attach->header.array.element_count = element_count;

		memcpy(attach->elements, elements, element_count * sizeof(Code2AttArray_Element));

		temp_stack_release(core, mark);

		return offset_from_code2(header, &attach->header);
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

		const Maybe<Code2Offset> type = is_some(info.type)
			? some(code2_from_expr(core, get(info.type), header, scope, LocationMode::Value))
			: none<Code2Offset>();

		const Maybe<Code2Offset> value = is_some(info.value)
			? some(code2_from_expr(core, get(info.value), header, scope, LocationMode::Value))
			: none<Code2Offset>();

		Code2AttDefinition attach_value{};
		attach_value.header.definition.op_ = Code2Op::Definition;
		attach_value.header.definition.is_mut = is_mut;
		attach_value.header.definition.is_pub = is_pub;
		attach_value.header.definition.has_self_reference = has_self_reference;
		attach_value.type = type;
		attach_value.value = value;

		return emit_code2_offset(core, header, attach_value);
	}

	case AstTag::Block:
	{
		const u16 definition_count = attachment_of<AstBlockData>(node)->definition_count;

		Code2AttScopeBegin begin_attach_value{};
		begin_attach_value.header.scope_begin.op_ = Code2Op::ScopeBegin;
		begin_attach_value.header.scope_begin.definition_count = definition_count;

		Code2AttScopeBegin* const begin_attach = emit_code2(core, begin_attach_value);

		AstDirectChildIterator it = direct_children_of(node);

		if (!has_next(&it))
		{
			Code2AttNonaryOp void_attach_value{};
			void_attach_value.header.op = Code2Op::Void;

			const Code2Offset value = emit_code2_offset(core, header, void_attach_value);

			Code2AttNonaryOp end_attach_value{};
			end_attach_value.header.op = Code2Op::ScopeEnd;

			(void) emit_code2(core, end_attach_value);

			return value;
		}

		u16 definition_index = 0;

		while (true)
		{
			ASSERT_OR_IGNORE(has_next(&it));

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

				const Code2Offset type = is_some(info.type)
					? code2_from_expr(core, get(info.type), header, begin_attach, LocationMode::Value)
					: Code2Offset::INVALID;

				const Code2Offset untyped_value = code2_from_expr(core, get(info.value), header, begin_attach, LocationMode::Value);

				Code2Offset value;

				if (is_some(info.type))
				{
					Code2AttBinaryOp with_type_attach_value{};
					with_type_attach_value.header.op = Code2Op::WithType;
					with_type_attach_value.lhs = type;
					with_type_attach_value.rhs = untyped_value;

					value = emit_code2_offset(core, header, with_type_attach_value);
				}
				else
				{
					value = untyped_value;
				}

				Code2AttDefineLocal define_attach_value{};
				define_attach_value.header.define_local.op_ = Code2Op::DefineLocal;
				define_attach_value.header.define_local.is_mut = is_mut;
				define_attach_value.header.define_local.has_self_reference = has_self_reference;
				define_attach_value.header.define_local.definition_index = definition_index;
				define_attach_value.value = value;

				(void) emit_code2(core, define_attach_value);

				definition_index += 1;
			}
			else
			{
				const Code2Offset value = code2_from_expr(core, statement, header, begin_attach, LocationMode::Value);

				if (is_last)
				{
					Code2AttNonaryOp end_attach_value{};
					end_attach_value.header.op = Code2Op::ScopeEnd;

					(void) emit_code2(core, end_attach_value);

					return value;
				}
				else
				{
					Code2AttUnaryOp consume_attach_value{};
					consume_attach_value.header.op = Code2Op::ConsumeVoid;
					consume_attach_value.operand = value;

					(void) emit_code2(core, consume_attach_value);
				}
			}
		}
	}

	case AstTag::If:
	{
		const IfInfo info = get_if_info(node);

		if (is_some(info.where))
			scope = code2_from_where_prefix(core, get(info.where), header);

		const Code2Offset condition = code2_from_expr(core, info.condition, header, scope, LocationMode::Value);

		Code2AttIf attach_value{};
		attach_value.header.op = Code2Op::If;
		attach_value.condition = condition;

		Code2AttIf* const attach = emit_code2(core, attach_value);

		fixup_expr_own_scope(core, &attach->consequent, info.consequent, scope);

		if (is_some(info.alternative))
			fixup_expr_own_scope(core, &attach->alternative, get(info.alternative), scope);
		else
			fixup_empty_expr(core, &attach->alternative, scope);

		if (is_some(info.where))
			code2_from_where_suffix(core);

		return offset_from_code2(header, &attach->header);
	}

	case AstTag::For:
	{
		const ForInfo info = get_for_info(node);

		if (is_some(info.where))
			scope = code2_from_where_prefix(core, get(info.where), header);

		Code2AttLoop attach_value{};
		attach_value.header.op = Code2Op::Loop;

		Code2AttLoop* const attach = emit_code2(core, attach_value);

		fixup_loop(core, &attach->body, node, scope);

		if (is_some(info.where))
			code2_from_where_suffix(core);

		return offset_from_code2(header, &attach->header);
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

		const Code2Offset signature = code2_from_signature(core, signature_node, header, scope);

		AstNode* body_node = next_sibling_of(signature_node);

		Code2AttFunc attach_value{};
		attach_value.header.op = Code2Op::Func;
		attach_value.signature = signature;

		Code2AttFunc* const attach = emit_code2(core, attach_value);

		fixup_function_body(core, &attach->body, body_node, node_attach->parameter_count, scope);

		return offset_from_code2(header, &attach->header);
	}

	case AstTag::Signature:
	{
		return code2_from_signature(core, node, header, scope);
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
		attach->header.trait.op_ = Code2Op::Trait;
		attach->header.trait.parameter_count = parameter_count;
		attach->header.trait.member_count = member_count;

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

		return offset_from_code2(header, &attach->header);
	}

	case AstTag::Impl:
	{
		AstNode* const base_node = first_child_of(node);

		const Code2Offset base_type = code2_from_expr(core, base_node, header, scope, LocationMode::Value);

		AstNode* const trait_call_node = next_sibling_of(base_node);

		const Code2Offset trait_call = code2_from_call_generic(core, trait_call_node, header, scope, Code2Op::ImplTraitCall);

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
		attach->header.impl.op_ = Code2Op::Impl;
		attach->header.impl.member_count = member_count;
		attach->base_type = base_type;
		attach->trait_call = trait_call;

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

			fixup_impl_member_default_value(core, &attach->members[ordered_index].value, get(info.value), scope);
		}

		ASSERT_OR_IGNORE(order_index == member_count);

		temp_stack_release(core, mark);

		return offset_from_code2(header, &attach->header);
	}

	case AstTag::Self:
	{
		Code2AttNonaryOp attach_value{};
		attach_value.header.op = Code2Op::Self;

		return emit_code2_offset(core, header, attach_value);
	}

	case AstTag::Catch:
	{
		TODO("Implement.");
	}

	case AstTag::Unreachable:
	{
		Code2AttNonaryOp attach_value{};
		attach_value.header.op = Code2Op::Unreachable;

		return emit_code2_offset(core, header, attach_value);
	}

	case AstTag::Undefined:
	{
		Code2AttNonaryOp attach_value{};
		attach_value.header.op = Code2Op::Undefined;

		return emit_code2_offset(core, header, attach_value);
	}

	case AstTag::Identifier:
	{
		const AstIdentifierData* const node_attach = attachment_of<AstIdentifierData>(node);

		const NameBinding binding = node_attach->binding;

		if (has_flag(node, AstFlag::Identifier_IsTemplatedParamRef))
		{
			ASSERT_OR_IGNORE(binding.kind == NameBindingKind::Scoped);

			const Code2Op op = location_mode == LocationMode::Value ? Code2Op::TemplatedParamValue : Code2Op::TemplatedParamLocation;

			Code2AttTemplatedParamRef attach_value{};
			attach_value.header.templated_parameter_ref.op_ = op;
			attach_value.header.templated_parameter_ref.rank = binding.scoped.rank;

			return emit_code2_offset(core, header, attach_value);
		}
		else if (binding.kind == NameBindingKind::Scoped)
		{
			const Code2Op op = location_mode == LocationMode::Value ? Code2Op::LocalValue : Code2Op::LocalLocation;

			Code2AttLocalRef attach_value{};
			attach_value.header.local_ref.op_ = op;
			attach_value.header.local_ref.out = binding.scoped.out;
			attach_value.header.local_ref.rank = binding.scoped.rank;

			return emit_code2_offset(core, header, attach_value);
		}
		else if (binding.kind == NameBindingKind::Global)
		{
			const Code2Op op = location_mode == LocationMode::Value ? Code2Op::GlobalValue : Code2Op::GlobalLocation;

			Code2AttGlobalRef attach_value{};
			attach_value.header.global_ref.op_ = op;
			attach_value.header.global_ref.rank = binding.global.rank;

			return emit_code2_offset(core, header, attach_value);
		}
		else if (binding.kind == NameBindingKind::Closed)
		{
			const Code2Op op = location_mode == LocationMode::Value ? Code2Op::ClosedValue : Code2Op::ClosedLocation;

			Code2AttClosedRef attach_value{};
			attach_value.header.closed_ref.op_ = op;
			attach_value.header.closed_ref.rank = binding.closed.rank_in_closure;

			return emit_code2_offset(core, header, attach_value);
		}
		else
		{
			ASSERT_OR_IGNORE(binding.kind == NameBindingKind::TraitArgument);

			const Code2Op op = location_mode == LocationMode::Value ? Code2Op::TraitArgumentValue : Code2Op::TraitArgumentLocation;

			Code2AttTraitArgumentRef attach_value{};
			attach_value.header.trait_argument_ref.op_ = op;
			attach_value.header.trait_argument_ref.rank = binding.trait_argument.rank;

			return emit_code2_offset(core, header, attach_value);
		}
	}

	case AstTag::LitInteger:
	{
		const CompIntegerValue value = attachment_of<AstLitIntegerData>(node)->value;

		ASSERT_OR_IGNORE(comp_integer_compare(value, comp_integer_from_u64(0)) != StrongCompareOrdering::LessThan);

		u64 raw_value;

		if (u64_from_comp_integer(value, 16, &raw_value))
		{
			Code2AttInteger16 attach_value{};
			attach_value.header.integer16.op_ = Code2Op::Integer16;
			attach_value.header.integer16.value = static_cast<u16>(raw_value);

			return emit_code2_offset(core, header, attach_value);
		}
		else
		{
			Code2AttInteger64 attach_value{};
			attach_value.header.op = Code2Op::Integer64;

			memcpy(&attach_value.value_lo, &value, sizeof(CompIntegerValue));

			return emit_code2_offset(core, header, attach_value);
		}
	}

	case AstTag::LitFloat:
	{
		const CompFloatValue value = attachment_of<AstLitFloatData>(node)->value;

		Code2AttFloat attach_value{};
		attach_value.header.op = Code2Op::Float;

		memcpy(&attach_value.value_lo, &value, sizeof(CompFloatValue));

		return emit_code2_offset(core, header, attach_value);
	}

	case AstTag::LitChar:
	{
		const u32 value = attachment_of<AstLitCharData>(node)->codepoint;

		if (value <= UINT16_MAX)
		{
			Code2AttInteger16 attach_value{};
			attach_value.header.integer16.op_ = Code2Op::Integer16;
			attach_value.header.integer16.value = static_cast<u16>(value);

			return emit_code2_offset(core, header, attach_value);
		}
		else
		{
			Code2AttInteger64 attach_value{};
			attach_value.header.op = Code2Op::Integer64;

			const CompIntegerValue comp_integer_value = comp_integer_from_u64(value);

			memcpy(&attach_value.value_lo, &comp_integer_value, sizeof(CompIntegerValue));

			return emit_code2_offset(core, header, attach_value);
		}
	}

	case AstTag::LitString:
	{
		const AstLitStringData* const attach = attachment_of<AstLitStringData>(node);

		Code2AttString attach_value{};
		attach_value.header.op = Code2Op::String;
		attach_value.size = attach->value_size;
		attach_value.data = attach->value_id;

		return emit_code2_offset(core, header, attach_value);
	}

	case AstTag::OpSliceOf:
	{
		const OpSliceOfInfo info = get_op_slice_of_info(node);

		const Code2Offset sliced = code2_from_expr(core, info.sliced, header, scope, LocationMode::AddressSrc);

		const Maybe<Code2Offset> begin = is_some(info.begin)
			? some(code2_from_expr(core, get(info.begin), header, scope, LocationMode::Value))
			: none<Code2Offset>();

		const Maybe<Code2Offset> end = is_some(info.end)
			? some(code2_from_expr(core, get(info.end), header, scope, LocationMode::Value))
			: none<Code2Offset>();

		Code2AttSlice attach_value{};
		attach_value.header.op = Code2Op::Slice;
		attach_value.sliced = sliced;
		attach_value.begin = begin;
		attach_value.end = end;

		return emit_code2_offset(core, header, attach_value);
	}

	case AstTag::Return:
	{
		TODO("Implement.");
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
		return code2_from_call_generic(core, node, header, scope, Code2Op::Call);
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

		const Code2Offset operand = code2_from_expr(core, operand_node, header, scope, LocationMode::Value);

		const Code2Op op = tag == AstTag::UOpTypeSlice
			? tag == AstTag::UOpTypeTailArray ? Code2Op::TypeTailArray : Code2Op::TypeSlice
			: Code2Op::TypePtr;

		Code2AttReferenceType attach_value{};
		attach_value.header.reference_type.op_ = op;
		attach_value.header.reference_type.is_mut = is_mut;
		attach_value.header.reference_type.is_opt = is_opt;
		attach_value.header.reference_type.is_multi = is_multi;
		attach_value.operand = operand;

		return emit_code2_offset(core, header, attach_value);
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

		const Code2Offset operand = code2_from_expr(core, operand_node, header, scope, operand_location_mode);

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

		Code2AttUnaryOp attach_value{};
		attach_value.header.op = op;
		attach_value.operand = operand;

		const Code2Offset result = emit_code2_offset(core, header, attach_value);

		if (tag == AstTag::UOpDeref && location_mode == LocationMode::Value)
		{
			Code2AttUnaryOp load_attach_value{};
			load_attach_value.header.op = Code2Op::LoadLocation;
			load_attach_value.operand = result;

			return emit_code2_offset(core, header, load_attach_value);
		}
		else
		{
			return result;
		}
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

		const Code2Offset lhs = code2_from_expr(core, lhs_node, header, scope, lhs_location_mode);

		AstNode* const rhs_node = next_sibling_of(lhs_node);

		const Code2Offset rhs = code2_from_expr(core, rhs_node, header, scope, LocationMode::Value);

		Code2Op op;

		if (tag >= AstTag::OpAdd && tag <= AstTag::OpShiftR)
			op = static_cast<Code2Op>(static_cast<u8>(Code2Op::Add) + static_cast<u8>(tag) - static_cast<u8>(AstTag::OpAdd));
		else if (tag == AstTag::OpTypeArray)
			op = Code2Op::TypeArray;
		else if (tag == AstTag::OpArrayIndex)
			op = Code2Op::Index;
		else
			ASSERT_UNREACHABLE;

		Code2AttBinaryOp attach_value{};
		attach_value.header.op = op;
		attach_value.lhs = lhs;
		attach_value.rhs = rhs;

		const Code2Offset result = emit_code2_offset(core, header, attach_value);

		if (tag == AstTag::OpArrayIndex && location_mode == LocationMode::Value)
		{
			Code2AttUnaryOp load_attach_value{};
			load_attach_value.header.op = Code2Op::LoadLocation;
			load_attach_value.operand = result;

			return emit_code2_offset(core, header, load_attach_value);
		}
		else
		{
			return result;
		}
	}

	case AstTag::OpLogAnd:
	case AstTag::OpLogOr:
	{
		AstNode* const lhs_node = first_child_of(node);

		const Code2Offset lhs = code2_from_expr(core, lhs_node, header, scope, LocationMode::Value);

		AstNode* const rhs_node = next_sibling_of(lhs_node);

		const Code2Op op = tag == AstTag::OpLogAnd ? Code2Op::LogicalAnd : Code2Op::LogicalOr;

		Code2AttLogicalOp attach_value{};
		attach_value.header.op = op;
		attach_value.lhs = lhs;

		Code2AttLogicalOp* const attach = emit_code2(core, attach_value);

		fixup_expr_shared_scope(core, &attach->rhs, rhs_node, scope);

		return offset_from_code2(header, &attach->header);
	}

	case AstTag::Member:
	{
		const IdentifierId name = attachment_of<AstMemberData>(node)->identifier_id;

		AstNode* const composite_node = first_child_of(node);

		const Code2Offset composite = code2_from_expr(core, composite_node, header, scope, location_mode);

		const Code2Op op = location_mode == LocationMode::Value ? Code2Op::MemberValue : Code2Op::MemberLocation;

		Code2AttMember attach_value{};
		attach_value.header.op = op;
		attach_value.composite = composite;
		attach_value.name = name;

		return emit_code2_offset(core, header, attach_value);
	}

	case AstTag::OpCmpLT:
	case AstTag::OpCmpGT:
	case AstTag::OpCmpLE:
	case AstTag::OpCmpGE:
	case AstTag::OpCmpNE:
	case AstTag::OpCmpEQ:
	{
		AstNode* const lhs_node = first_child_of(node);

		const Code2Offset lhs = code2_from_expr(core, lhs_node, header, scope, LocationMode::Value);

		AstNode* const rhs_node = next_sibling_of(lhs_node);

		const Code2Offset rhs = code2_from_expr(core, rhs_node, header , scope, LocationMode::Value);

		const Code2AttCompare_Kind kind = static_cast<Code2AttCompare_Kind>(1 + static_cast<u8>(tag) - static_cast<u8>(AstTag::OpCmpLT));

		Code2AttCompare attach_value{};
		attach_value.header.compare.op_ = Code2Op::Compare;
		attach_value.header.compare.kind = kind;
		attach_value.lhs = lhs;
		attach_value.rhs = rhs;

		return emit_code2_offset(core, header, attach_value);
	}

	case AstTag::OpSet:
	{
		AstNode* const lhs_node = first_child_of(node);

		const Code2Offset lhs = code2_from_expr(core, lhs_node, header, scope, LocationMode::AssignmentDst);

		AstNode* const rhs_node = next_sibling_of(lhs_node);

		const Code2Offset rhs = code2_from_expr(core, rhs_node, header, scope, LocationMode::Value);

		Code2AttBinaryOp attach_value{};
		attach_value.header.op = Code2Op::Set;
		attach_value.lhs = lhs;
		attach_value.rhs = rhs;

		return emit_code2_offset(core, header, attach_value);
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

		const Code2Offset lhs_location = code2_from_expr(core, lhs_node, header, scope, LocationMode::AssignmentDst);

		Code2AttUnaryOp lhs_attach_value{};
		lhs_attach_value.header.op = Code2Op::LoadLocation;
		lhs_attach_value.operand = lhs_location;

		const Code2Offset lhs = emit_code2_offset(core, header, lhs_attach_value);

		AstNode* const rhs_node = next_sibling_of(lhs_node);

		const Code2Offset rhs = code2_from_expr(core, rhs_node, header, scope, LocationMode::Value);

		const Code2Op op = static_cast<Code2Op>(static_cast<u8>(Code2Op::Add) + static_cast<u8>(tag) - static_cast<u8>(AstTag::OpSetAdd));

		Code2AttBinaryOp op_attach_value{};
		op_attach_value.header.op = op;
		op_attach_value.lhs = lhs;
		op_attach_value.rhs = rhs;

		const Code2Offset op_result = emit_code2_offset(core, header, op_attach_value);

		Code2AttBinaryOp set_attach_value{};
		set_attach_value.header.op = Code2Op::Set;
		set_attach_value.lhs = lhs;
		set_attach_value.rhs = op_result;

		return emit_code2_offset(core, header, set_attach_value);
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

static Code2Offset code2_from_expr(CoreData* core, AstNode* node, const Code2Header* header, Code2AttScopeBegin* scope, LocationMode location_mode) noexcept
{
	const AstTag tag = node->tag;

	const Code2Offset result = code2_from_expr_raw(core, node, header, scope, location_mode);

	if (is_location_ast_tag(tag) || location_mode == LocationMode::Value)
	{
		return result;
	}
	else if (location_mode == LocationMode::AddressSrc)
	{
		Code2AttMakeLocation location_attach_value{};
		location_attach_value.header.make_location.op_ = Code2Op::MakeLocation;
		location_attach_value.header.make_location.is_mut = false;
		location_attach_value.value = result;

		return emit_code2_offset(core, header, location_attach_value);
	}
	else
	{
		ASSERT_OR_IGNORE(location_mode == LocationMode::AssignmentDst);

		record_error(core, node, CompileError::INVALID); // TODO: Error code.

		TODO("Set error flag.");
	}
}



static void code2_from_fixup(CoreData* core, Code2Fixup fixup, Code2Header* header) noexcept
{
	switch (fixup.tag)
	{
	case Code2FixupTag::Expr:
	{
		AstNode* const node = get(fixup.src);

		const Code2Offset value = code2_from_expr(core, node, header, fixup.scope, LocationMode::Value);

		Code2AttLeave leave_attach_value{};
		leave_attach_value.header.leave.op_ = Code2Op::Leave;
		leave_attach_value.header.leave.out = 0;
		leave_attach_value.value = value;
		leave_attach_value.prev = none<Code2Offset>();

		const Code2Offset leave = emit_code2_offset(core, header, leave_attach_value);

		*fixup.dst = complete_header_with_leave(core, header, leave);

		return;
	}

	case Code2FixupTag::EmptyExpr:
	{
		Code2AttNonaryOp void_attach_value{};
		void_attach_value.header.op = Code2Op::Void;

		const Code2Offset value = emit_code2_offset(core, header, void_attach_value);


		Code2AttLeave leave_attach_value{};
		leave_attach_value.header.leave.op_ = Code2Op::Leave;
		leave_attach_value.header.leave.out = 0;
		leave_attach_value.value = value;
		leave_attach_value.prev = none<Code2Offset>();

		const Code2Offset leave = emit_code2_offset(core, header, leave_attach_value);

		*fixup.dst = complete_header_with_leave(core, header, leave);

		return;
	}

	case Code2FixupTag::Loop:
	{
		AstNode* const loop_node = get(fixup.src);

		const ForInfo info = get_for_info(loop_node);

		const Code2Offset condition = code2_from_expr(core, info.condition, header, fixup.scope, LocationMode::Value);

		Code2AttIf if_attach_value{};
		if_attach_value.header.op = Code2Op::If;
		if_attach_value.condition = condition;
		if_attach_value.consequent = Code2Id::INVALID;
		if_attach_value.alternative = Code2Id::INVALID;

		Code2AttIf* const if_attach = emit_code2(core, if_attach_value);

		fixup_finally(core, &if_attach->consequent, info.finally, fixup.scope);

		fixup_empty_expr(core, &if_attach->alternative, fixup.scope);

		const Code2Offset body = code2_from_expr(core, info.body, header, fixup.scope, LocationMode::Value);

		Code2AttUnaryOp consume_body_attach_value{};
		consume_body_attach_value.header.op = Code2Op::ConsumeVoid;
		consume_body_attach_value.operand = body;

		(void) emit_code2(core, consume_body_attach_value);

		if (is_some(info.step))
		{
			const Code2Offset step = code2_from_expr(core, get(info.step), header, fixup.scope, LocationMode::Value);

			Code2AttUnaryOp consume_step_attach_value{};
			consume_step_attach_value.header.op = Code2Op::ConsumeVoid;
			consume_step_attach_value.operand = step;

			(void) emit_code2(core, consume_step_attach_value);
		}

		Code2AttRepeat repeat_attach_value{};
		repeat_attach_value.header.repeat.op_ = Code2Op::Repeat;
		repeat_attach_value.header.repeat.out = 0;

		(void) emit_code2(core, repeat_attach_value);

		*fixup.dst = complete_header_delayed_leave(core, header);

		return;
	}

	case Code2FixupTag::Finally:
	{
		Code2Offset value;

		if (is_some(fixup.src))
		{
			AstNode* const node = get(fixup.src);

			value = code2_from_expr(core, node, header, fixup.scope, LocationMode::Value);
		}
		else
		{
			Code2AttNonaryOp void_attach_value{};
			void_attach_value.header.op = Code2Op::Void;

			value = emit_code2_offset(core, header, void_attach_value);
		}

		Code2AttLeave loop_leave_attach_value{};
		loop_leave_attach_value.header.leave.op_ = Code2Op::Leave;
		loop_leave_attach_value.header.leave.out = 1;
		loop_leave_attach_value.value = value;
		loop_leave_attach_value.prev = none<Code2Offset>();

		const Code2Offset loop_leave = emit_code2_offset(core, header, loop_leave_attach_value);

		Code2AttLeave leave_attach_value{};
		leave_attach_value.header.leave.op_ = Code2Op::Leave;
		leave_attach_value.header.leave.out = 0;
		leave_attach_value.value = loop_leave;
		leave_attach_value.prev = none<Code2Offset>();

		const Code2Offset leave = emit_code2_offset(core, header, leave_attach_value);

		*fixup.dst = complete_header_with_leave(core, header, leave);

		return;
	}

	case Code2FixupTag::FunctionBody:
	{
		Code2AttScopeBegin begin_attach_value{};
		begin_attach_value.header.scope_begin.op_ = Code2Op::ScopeBegin;
		begin_attach_value.header.scope_begin.definition_count = fixup.function_body_parameter_count;

		Code2AttScopeBegin* const begin_attach = emit_code2(core, begin_attach_value);


		AstNode* const node = get(fixup.src);

		const Code2Offset value = code2_from_expr(core, node, header, begin_attach, LocationMode::Value);


		Code2AttNonaryOp end_attach_value{};
		end_attach_value.header.op = Code2Op::ScopeEnd;

		(void) emit_code2(core, end_attach_value);


		Code2AttLeave leave_attach_value{};
		leave_attach_value.header.leave.op_ = Code2Op::Leave;
		leave_attach_value.header.leave.out = 0;
		leave_attach_value.value = value;
		leave_attach_value.prev = none<Code2Offset>();

		const Code2Offset leave = emit_code2_offset(core, header, leave_attach_value);

		*fixup.dst = complete_header_with_leave(core, header, leave);

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

		Code2Header* const header = emit_header(core);

		code2_from_fixup(core, fixup, header);
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



Code2Id code2_from_top_level_definition(CoreData* core, AstNode* node) noexcept
{
	const DefinitionInfo info = get_definition_info(node);

	ASSERT_OR_IGNORE(is_some(info.value));

	Code2Header* const header = emit_header(core);

	
	const Code2Offset type = is_some(info.type)
		? code2_from_expr(core, get(info.type), header, nullptr, LocationMode::Value)
		: Code2Offset::INVALID;


	const Code2Offset untyped_value = code2_from_expr(core, get(info.value), header, nullptr, LocationMode::Value);


	Code2Offset value;

	if (is_some(info.type))
	{
		Code2AttBinaryOp with_type_attach_value{};
		with_type_attach_value.header.op = Code2Op::WithType;
		with_type_attach_value.lhs = type;
		with_type_attach_value.rhs = untyped_value;

		value = emit_code2_offset(core, header, with_type_attach_value);
	}
	else
	{
		value = untyped_value;
	}


	Code2AttLeave leave_attach_value{};
	leave_attach_value.header.leave.op_ = Code2Op::Leave;
	leave_attach_value.header.leave.out = 0;
	leave_attach_value.value = value;
	leave_attach_value.prev = none<Code2Offset>();

	const Code2Offset leave = emit_code2_offset(core, header, leave_attach_value);


	const Code2Id result = complete_header_with_leave(core, header, leave);

	code2_from_fixups(core);

	return result;
}



const char8* tag_name(Code2Op op) noexcept
{
	static constexpr char8* TAG_NAMES[] = { 
		"[Unknown]",
		"Leave",
		"Repeat",
		"ScopeBegin",
		"ScopeEnd",
		"Builtin",
		"Composite",
		"Array",
		"Definition",
		"MakeLocation",
		"WithType",
		"DefineLocal",
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
