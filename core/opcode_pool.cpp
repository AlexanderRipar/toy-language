#include "core.hpp"

#include "../infra/container/reserved_vec.hpp"

struct SourceMapping
{
	OpcodeId code_begin;

	SourceId source;
};

enum class FixupKind : u8
{
	INVALID = 0,
	FunctionBody,
	Argument,
	TemplateParameter,
	TemplateReturnType,
	ValueVoid,
	IfBranch,
	DiscardedIfBranch,
	LoopBody,
	LoopFinally,
};

struct Fixup
{
	FixupKind kind;

	bool allow_return : 1;

	bool allow_valued_break : 1;

	bool expects_write_ctx : 1;

	bool loop_body_allow_valued_break : 1;

	bool template_parameter_has_type : 1;

	bool template_parameter_has_value : 1;

	bool function_body_has_closure : 1;

	bool if_branch_expect_void : 1;

	u8 template_parameter_rank;

	OpcodeId dst_id;

	AstNodeId node_id;

	Maybe<AstNodeId> second_node_id;

	OpcodeEffects return_adjust;
};

struct OpcodePool
{
	AstPool* asts;

	OpcodeEffects state;

	OpcodeEffects return_adjust;

	bool allow_return;

	ReservedVec<Opcode> codes;

	ReservedVec<SourceMapping> sources;

	ReservedVec<Fixup> fixups;

	MutRange<byte> memory;
};



static byte* emit_opcode_raw(OpcodePool* opcodes, Opcode code, bool expects_write_ctx, AstNode* node, u32 attach_size) noexcept
{
	const OpcodeId opcode_id = static_cast<OpcodeId>(opcodes->codes.used());

	const SourceId source_id = node == nullptr
		? SourceId::INVALID
		: source_id_of_ast_node(opcodes->asts, node);

	opcodes->sources.append(SourceMapping{ opcode_id, source_id });

	Opcode* const dst = opcodes->codes.reserve(static_cast<u32>(1 + attach_size));

	dst[0] = static_cast<Opcode>(static_cast<u8>(code) | (static_cast<u8>(expects_write_ctx) << 7));

	return reinterpret_cast<byte*>(dst + 1);
}

static void put_opcode_attachs([[maybe_unused]] byte* dst) noexcept
{
	// Base-case is a no-op
}

template<typename Attach, typename... Attachs>
static void put_opcode_attachs(byte* dst, Attach attach, Attachs... attachs) noexcept
{
	memcpy(dst, &attach, sizeof(Attach));

	put_opcode_attachs(dst + sizeof(Attach), attachs...);
}

template<typename ...Attachs>
static void emit_opcode(OpcodePool* opcodes, Opcode code, bool expects_write_ctx, AstNode* node, Attachs... attachs) noexcept
{
	constexpr u32 attach_size = (0 + ... + sizeof(Attachs));

	byte* const attach_dst = emit_opcode_raw(opcodes, code, expects_write_ctx, node, attach_size);

	put_opcode_attachs(attach_dst, attachs...);

	const OpcodeEffects effects = opcode_effects(reinterpret_cast<Opcode*>(attach_dst - 1));

	opcodes->state.values_diff += effects.values_diff;
	opcodes->state.scopes_diff += effects.scopes_diff;
	opcodes->state.write_ctxs_diff += effects.write_ctxs_diff;
	opcodes->state.closures_diff += effects.closures_diff;

	ASSERT_OR_IGNORE(
		opcodes->state.values_diff >= 0
	 && opcodes->state.scopes_diff >= 0
	 && opcodes->state.write_ctxs_diff >= 0
	 && opcodes->state.closures_diff >= 0
	);
}



static void emit_fixup_for_function_body(OpcodePool* opcodes, Opcode* fixup_dst, AstNode* node, bool has_closure) noexcept
{
	Fixup* const fixup = opcodes->fixups.reserve();
	fixup->kind = FixupKind::FunctionBody;
	fixup->allow_return = true;
	fixup->expects_write_ctx = true;
	fixup->function_body_has_closure = has_closure;
	fixup->dst_id = static_cast<OpcodeId>(fixup_dst - opcodes->codes.begin());
	fixup->node_id = id_from_ast_node(opcodes->asts, node);

	fixup->return_adjust.values_diff = 0;
	fixup->return_adjust.scopes_diff = 0;
	fixup->return_adjust.write_ctxs_diff = 0;
	fixup->return_adjust.closures_diff = has_closure ? 1 : 0;
}

static void emit_fixup_for_argument(OpcodePool* opcodes, Opcode* fixup_dst, AstNode* node) noexcept
{
	Fixup* const fixup = opcodes->fixups.reserve();
	fixup->kind = FixupKind::Argument;
	fixup->allow_return = false;
	fixup->expects_write_ctx = true;
	fixup->dst_id = static_cast<OpcodeId>(fixup_dst - opcodes->codes.begin());
	fixup->node_id = id_from_ast_node(opcodes->asts, node);
}

static void emit_fixup_for_template_parameter(OpcodePool* opcodes, AstNode* node, Maybe<AstNode*> second_node, bool has_type, bool has_value, u8 rank) noexcept
{
	Maybe<AstNodeId> second_node_id;

	if (is_some(second_node))
		second_node_id = some(id_from_ast_node(opcodes->asts, get(second_node)));
	else
		second_node_id = none<AstNodeId>();

	Fixup* const fixup = opcodes->fixups.reserve();
	fixup->kind = FixupKind::TemplateParameter;
	fixup->allow_return = false;
	fixup->expects_write_ctx = false;
	fixup->template_parameter_has_type = has_type;
	fixup->template_parameter_has_value = has_value;
	fixup->template_parameter_rank = rank;
	fixup->dst_id = OpcodeId::INVALID;
	fixup->node_id = id_from_ast_node(opcodes->asts, node);
	fixup->second_node_id = second_node_id;
}

static void emit_fixup_for_template_return_type(OpcodePool* opcodes, Opcode* fixup_dst, AstNode* node) noexcept
{
	Fixup* const fixup = opcodes->fixups.reserve();
	fixup->kind = FixupKind::TemplateReturnType;
	fixup->allow_return = false;
	fixup->expects_write_ctx = true;
	fixup->dst_id = static_cast<OpcodeId>(fixup_dst - opcodes->codes.begin());
	fixup->node_id = id_from_ast_node(opcodes->asts, node);
}

static void emit_fixup_for_value_void(OpcodePool* opcodes, Opcode* fixup_dst, AstNode* node) noexcept
{
	Fixup* const fixup = opcodes->fixups.reserve();
	fixup->kind = FixupKind::ValueVoid;
	fixup->allow_return = false;
	fixup->expects_write_ctx = true;
	fixup->dst_id = static_cast<OpcodeId>(fixup_dst - opcodes->codes.begin());
	fixup->node_id = id_from_ast_node(opcodes->asts, node);
}

static void emit_fixup_for_if_branch(OpcodePool* opcodes, Opcode* fixup_dst, AstNode* node, bool expects_write_ctx, bool expect_void) noexcept
{
	Fixup* const fixup = opcodes->fixups.reserve();
	fixup->kind = FixupKind::IfBranch;
	fixup->allow_return = opcodes->allow_return;
	fixup->expects_write_ctx = expects_write_ctx;
	fixup->if_branch_expect_void = expect_void;
	fixup->dst_id = static_cast<OpcodeId>(fixup_dst - opcodes->codes.begin());
	fixup->node_id = id_from_ast_node(opcodes->asts, node);
}

static void emit_fixup_for_discarded_if_branch(OpcodePool* opcodes, Opcode* fixup_dst, AstNode* node) noexcept
{
	Fixup* const fixup = opcodes->fixups.reserve();
	fixup->kind = FixupKind::DiscardedIfBranch;
	fixup->allow_return = opcodes->allow_return;
	fixup->expects_write_ctx = false;
	fixup->dst_id = static_cast<OpcodeId>(fixup_dst - opcodes->codes.begin());
	fixup->node_id = id_from_ast_node(opcodes->asts, node);
}

static void emit_fixup_for_loop_body(OpcodePool* opcodes, Opcode* fixup_dst, AstNode* node, Maybe<AstNode*> step_node, bool expects_write_ctx, bool expect_valued_breaks) noexcept
{
	Maybe<AstNodeId> step_node_id;

	if (is_some(step_node))
		step_node_id = some(id_from_ast_node(opcodes->asts, get(step_node)));
	else
		step_node_id = none<AstNodeId>();

	Fixup* const fixup = opcodes->fixups.reserve();
	fixup->kind = FixupKind::LoopBody;
	fixup->allow_return = opcodes->allow_return;
	fixup->allow_valued_break = expect_valued_breaks;
	fixup->expects_write_ctx = expects_write_ctx;
	fixup->dst_id = static_cast<OpcodeId>(fixup_dst - opcodes->codes.begin());
	fixup->node_id = id_from_ast_node(opcodes->asts, node);
	fixup->second_node_id = step_node_id;
}

static void emit_fixup_for_loop_finally(OpcodePool* opcodes, Opcode* fixup_dst, AstNode* node, bool expects_write_ctx) noexcept
{
	Fixup* const fixup = opcodes->fixups.reserve();
	fixup->kind = FixupKind::LoopFinally;
	fixup->allow_return = opcodes->allow_return;
	fixup->expects_write_ctx = expects_write_ctx;
	fixup->dst_id = static_cast<OpcodeId>(fixup_dst - opcodes->codes.begin());
	fixup->node_id = id_from_ast_node(opcodes->asts, node);
}



static u16 emit_func_closure_values(OpcodePool* opcodes, AstNode* node) noexcept
{
	const Maybe<ClosureListId> list_id = attachment_of<AstFuncData>(node)->closure_list_id;

	if (is_none(list_id))
		return 0;

	const ClosureList* list = closure_list_from_id(opcodes->asts, get(list_id));

	for (u16 i = 0; i != list->count; ++i)
	{
		const ClosureListEntry entry = list->entries[i];

		if (entry.source_is_closure)
			emit_opcode(opcodes, Opcode::LoadClosure, false, node, entry.source_rank);
		else
			emit_opcode(opcodes, Opcode::LoadScope, false, node, entry.source_out, entry.source_rank);
	}

	return list->count;
}

static bool is_templated(AstNode* node) noexcept
{
	AstFlatIterator it = flat_ancestors_of(node);

	while (has_next(&it))
	{
		AstNode* const curr = next(&it);

		// Blocks cannot be handled here as they introduce a scope, meaning our
		// binding's `out` would become meaningless in the flat iteration
		// pattern used here.
		ASSERT_OR_IGNORE(curr->tag != AstTag::Block);

		if (curr->tag != AstTag::Identifier)
			continue;

		const NameBinding binding = attachment_of<AstIdentifierData>(curr)->binding;

		if (!binding.is_global && (!binding.is_scoped || binding.scoped.out == 0))
			return true;
	}

	return false;
}



static bool opcodes_from_expression(OpcodePool* opcodes, AstNode* node, bool expects_write_ctx) noexcept;

static bool opcodes_from_scope_definition(OpcodePool* opcodes, AstNode* node) noexcept
{
	DefinitionInfo info = get_definition_info(node);

	ASSERT_OR_IGNORE(is_some(info.value));

	const bool has_type = is_some(info.type);

	const bool is_mut = has_flag(node, AstFlag::Definition_IsMut);

	if (has_type)
	{
		if (!opcodes_from_expression(opcodes, get(info.type), false))
			return false;

		emit_opcode(opcodes, Opcode::ScopeAllocTyped, false, node, is_mut);
	}

	if (!opcodes_from_expression(opcodes, get(info.value), has_type))
		return false;

	if (!has_type)
		emit_opcode(opcodes, Opcode::ScopeAllocUntyped, false, node, is_mut);

	return true;
}

static bool opcodes_from_where(OpcodePool* opcodes, AstNode* node) noexcept
{
	AstDirectChildIterator it = direct_children_of(node);

	while (has_next(&it))
	{
		AstNode* const definition = next(&it);

		if (!opcodes_from_scope_definition(opcodes, definition))
			return false;
	}

	return true;
}

static bool opcodes_from_parameter(OpcodePool* opcodes, AstNode* node, u8 rank, IdentifierId* out_name, OpcodeSignaturePerParameterFlags* out_flags, u32* out_fixup_index) noexcept
{
	const bool is_templated_parameter = is_templated(node);

	DefinitionInfo info = get_definition_info(node);

	if (is_templated_parameter)
	{
		*out_fixup_index = opcodes->fixups.used();

		// The emitted fixup's `dst_id` - which is initially set to
		// `OpcodeId::INVALID` - gets set by `opcodes_from_signature` via
		// `out_fixup_index`. This is necessary since it is not actually known
		// at this point, as the signature opcode has not been emitted yet.
		if (is_some(info.type) && is_some(info.value))
			emit_fixup_for_template_parameter(opcodes, get(info.type), info.value, true, true, rank);
		else if (is_some(info.type))
			emit_fixup_for_template_parameter(opcodes, get(info.type), none<AstNode*>(), true, false, rank);
		else
			emit_fixup_for_template_parameter(opcodes, get(info.value), none<AstNode*>(), false, true, rank);
	}
	else
	{
		if (is_some(info.type))
		{
			if (!opcodes_from_expression(opcodes, get(info.type), false))
				return false;
		}

		if (is_some(info.value))
		{
			if (!opcodes_from_expression(opcodes, get(info.value), false))
				return false;
		}
	}

	*out_name = attachment_of<AstParameterData>(node)->identifier_id;

	OpcodeSignaturePerParameterFlags flags;
	flags.has_type = is_some(info.type);
	flags.has_default = is_some(info.value);
	flags.is_mut = has_flag(node, AstFlag::Definition_IsMut);
	flags.is_eval = has_flag(node, AstFlag::Definition_IsEval);
	flags.is_templated = is_templated_parameter;
	flags.unused_ = 0;

	*out_flags = flags;

	return true;
}

static bool opcodes_from_signature(OpcodePool* opcodes, AstNode* node, bool expects_write_ctx) noexcept
{
	SignatureInfo info = get_signature_info(node);

	if (is_some(info.expects))
		TODO("Implement opcode generation for signature-level `expects`");

	if (is_some(info.ensures))
		TODO("Implement opcode generation for signature-level `ensures`");

	if (is_none(info.return_type))
		TODO("Implement (or remove) opcode generation for implicit return types");

	AstNode* const parameters = info.parameters;

	AstDirectChildIterator it = direct_children_of(parameters);

	IdentifierId parameter_names[64];

	OpcodeSignaturePerParameterFlags parameter_flags[64];

	u32 fixup_indices[64];

	u8 parameter_rank = 0;

	while (has_next(&it))
	{
		ASSERT_OR_IGNORE(parameter_rank < 64);

		AstNode* const parameter = next(&it);

		if (!opcodes_from_parameter(opcodes, parameter, parameter_rank, parameter_names + parameter_rank, parameter_flags + parameter_rank, fixup_indices + parameter_rank))
			return false;

		parameter_rank += 1;
	}

	const u8 parameter_count = parameter_rank;

	u8 templated_parameter_count = 0;

	u8 value_count = 0;

	for (u8 i = 0; i != parameter_count; ++i)
	{
		if (parameter_flags[i].is_templated)
			templated_parameter_count += 1;
		else if (parameter_flags[i].has_type && parameter_flags[i].has_default)
			value_count += 2;
		else
			value_count += 1;
	}

	const bool has_templated_return_type = is_templated(get(info.return_type));

	if (!has_templated_return_type)
	{
		if (!opcodes_from_expression(opcodes, get(info.return_type), false))
			return false;

		value_count += 1;
	}

	if (templated_parameter_count == 0 && !has_templated_return_type)
	{
		const u32 attach_size = sizeof(OpcodeSignatureFlags)
		                      + 2 * sizeof(u8)
		                      + parameter_count * (sizeof(IdentifierId) + sizeof(OpcodeSignaturePerParameterFlags));

		// Since we use `emit_opcode_raw` and not `emit_opcode`, we need to
		// manually adjust and check the current state.
		if (expects_write_ctx)
		{
			ASSERT_OR_IGNORE(opcodes->state.write_ctxs_diff != 0);

			opcodes->state.values_diff -= value_count;
			opcodes->state.write_ctxs_diff -= 1;

			ASSERT_OR_IGNORE(
				opcodes->state.values_diff >= 0
			 && opcodes->state.write_ctxs_diff >= 0
			);
		}
		else
		{
			ASSERT_OR_IGNORE(opcodes->state.values_diff != 0);

			opcodes->state.values_diff -= value_count - 1;

			ASSERT_OR_IGNORE(opcodes->state.values_diff >= 0);
		}

		byte* attach = emit_opcode_raw(opcodes, Opcode::Signature, expects_write_ctx, node, attach_size);

		OpcodeSignatureFlags flags;
		flags.is_func = !has_flag(node, AstFlag::Signature_IsProc);
		flags.has_templated_parameter_list = false;
		flags.has_templated_return_type = false;
		flags.unused_ = 0;

		memcpy(attach, &flags, sizeof(OpcodeSignatureFlags));
		attach += sizeof(OpcodeSignatureFlags);

		memcpy(attach, &parameter_count, sizeof(u8));
		attach += sizeof(u8);

		memcpy(attach, &value_count, sizeof(u8));
		attach += sizeof(u8);

		for (u8 i = 0; i != parameter_count; ++i)
		{
			memcpy(attach, parameter_names + i, sizeof(IdentifierId));
			attach += sizeof(IdentifierId);

			memcpy(attach, parameter_flags + i, sizeof(OpcodeSignaturePerParameterFlags));
			attach += sizeof(OpcodeSignaturePerParameterFlags);
		}
	}
	else
	{
		TODO("This is currently broken as we don't deal with signature-level closed-over values at all. They are actually expected in their own u16 field in the attachment.");

		const u32 attach_size = sizeof(OpcodeSignatureFlags)
		                      + 2 * sizeof(u8)
		                      + parameter_count * (sizeof(IdentifierId) + sizeof(OpcodeSignaturePerParameterFlags))
							  + templated_parameter_count * sizeof(OpcodeId)
							  + (has_templated_return_type ? sizeof(OpcodeId) : 0);

		byte* attach = emit_opcode_raw(opcodes, Opcode::DynSignature, expects_write_ctx, node, attach_size);

		OpcodeSignatureFlags flags;
		flags.is_func = !has_flag(node, AstFlag::Signature_IsProc);
		flags.has_templated_parameter_list = templated_parameter_count != 0;
		flags.has_templated_return_type = has_templated_return_type;
		flags.unused_ = 0;

		memcpy(attach, &flags, sizeof(OpcodeSignatureFlags));
		attach += sizeof(OpcodeSignatureFlags);

		memcpy(attach, &parameter_count, sizeof(u8));
		attach += sizeof(u8);

		memcpy(attach, &value_count, sizeof(u8));
		attach += sizeof(u8);

		// If the signature's return type is templated, reserve space for its
		// completion callback in the attachment and emit a fixup for it.
		if (has_templated_return_type)
		{
			emit_fixup_for_template_return_type(opcodes, reinterpret_cast<Opcode*>(attach), get(info.return_type));

			attach += sizeof(OpcodeId);
		}

		for (u8 i = 0; i != parameter_count; ++i)
		{
			memcpy(attach, parameter_names + i, sizeof(IdentifierId));
			attach += sizeof(IdentifierId);

			memcpy(attach, parameter_flags + i, sizeof(OpcodeSignaturePerParameterFlags));
			attach += sizeof(OpcodeSignaturePerParameterFlags);

			// If the parameter is templated, reserve space for its completion
			// callback in the attachment and retroactively change the dummy
			// fixup emitted for it in `opcodes_from_parameter` to point to
			// that space.
			if (parameter_flags[i].is_templated)
			{
				ASSERT_OR_IGNORE(fixup_indices[i] < opcodes->fixups.used());

				Fixup* const fixup = opcodes->fixups.begin() + fixup_indices[i];
				fixup->dst_id = static_cast<OpcodeId>(reinterpret_cast<Opcode*>(attach) - opcodes->codes.begin());

				attach += sizeof(OpcodeId);
			}
		}
	}

	return true;
}

static bool opcodes_from_expression(OpcodePool* opcodes, AstNode* node, bool expects_write_ctx) noexcept
{
	switch (node->tag)
	{
	case AstTag::Builtin:
	{
		emit_opcode(opcodes, Opcode::LoadBuiltin, expects_write_ctx, node, static_cast<Builtin>(node->flags));

		return true;
	}

	case AstTag::CompositeInitializer:
	{
		// This is treated separately depending on whether there is a write
		// context.
		// If there is one, then `Opcode::CompositePreInit` is used to
		// split it into write contexts corresponding to its members in
		// initializer order. Member initializers are then evaluated directly
		// into these.
		// If there is no write context, then `Opcode::CompositePostInit` is
		// used instead. This expects its member initializers already on the
		// stack, and combines them into an instance of a new
		// `CompositeLiteral` type.

		if (expects_write_ctx)
		{
			u16 named_member_count = 0;

			s32 write_ctxs_diff = -1;

			AstDirectChildIterator it = direct_children_of(node);

			while (has_next(&it))
			{
				AstNode* const member = next(&it);

				if (member->tag == AstTag::OpSet)
					named_member_count += 1;

				write_ctxs_diff += 1;
			}

			// Since we use `emit_opcode_raw` and not `emit_opcode`, we need to
			// manually adjust and check the current state.
			{
				ASSERT_OR_IGNORE(opcodes->state.write_ctxs_diff != 0);

				opcodes->state.write_ctxs_diff = write_ctxs_diff;
			}

			const u32 attach_size = 2 * sizeof(u16) + named_member_count * (sizeof(IdentifierId) + sizeof(u16));

			byte* attach = emit_opcode_raw(opcodes, Opcode::CompositePreInit, true, node, attach_size);

			memcpy(attach, &named_member_count, sizeof(u16));

			attach += sizeof(u16);

			u16 following_member_count = 0;

			it = direct_children_of(node);

			while (has_next(&it))
			{
				AstNode* const member = next(&it);

				AstNode* value;

				if (member->tag == AstTag::OpSet)
				{
					AstNode* const implied_member = first_child_of(member);

					if (member->tag != AstTag::ImpliedMember)
						return false; // TODO: Error message.

					value = next_sibling_of(implied_member);

					memcpy(attach, &following_member_count, sizeof(u16));

					attach += sizeof(u16);

					memcpy(attach, &attachment_of<AstImpliedMemberData>(member)->identifier_id, sizeof(IdentifierId));

					attach += sizeof(IdentifierId);

					following_member_count = 1;
				}
				else
				{
					value = member;

					following_member_count += 1;
				}

				if (!opcodes_from_expression(opcodes, value, true))
					return false;
			}

			memcpy(attach, &following_member_count, sizeof(u16));
		}
		else
		{
			u16 total_member_count = 0;

			AstDirectChildIterator it = direct_children_of(node);

			while (has_next(&it))
			{
				AstNode* const member = next(&it);

				AstNode* value;

				if (member->tag == AstTag::OpSet)
				{
					AstNode* const implied_member = first_child_of(member);

					if (member->tag != AstTag::ImpliedMember)
						return false; // TODO: Error message.

					value = next_sibling_of(implied_member);
				}
				else
				{
					value = member;
				}

				if (!opcodes_from_expression(opcodes, value, false))
					return false;

				total_member_count += 1;
			}


			// Since we use `emit_opcode_raw` and not `emit_opcode`, we need to
			// manually adjust and check the current state.
			{
				ASSERT_OR_IGNORE(opcodes->state.values_diff >= total_member_count);

				opcodes->state.values_diff -= total_member_count - 1;
			}

			byte* attach = emit_opcode_raw(opcodes, Opcode::CompositePostInit, false, node, sizeof(u16) + total_member_count * sizeof(IdentifierId));

			memcpy(attach, &total_member_count, sizeof(u16));

			attach += sizeof(u16);

			it = direct_children_of(node);

			while (has_next(&it))
			{
				AstNode* const member = next(&it);

				IdentifierId name;

				if (member->tag == AstTag::OpSet)
				{
					AstNode* const implied_member = first_child_of(member);

					name = attachment_of<AstImpliedMemberData>(implied_member)->identifier_id;
				}
				else
				{
					name = IdentifierId::INVALID;
				}

				memcpy(attach, &name, sizeof(IdentifierId));

				attach += sizeof(IdentifierId);
			}
		}

		return true;
	}

	case AstTag::ArrayInitializer:
	{
		// This is treated separately depending on whether there is a write
		// context.
		// If there is one, then `Opcode::ArrayPreInit` is used to
		// split it into write contexts corresponding to its elements. Element
		// initializers are then evaluated directly into these.
		// If there is no write context, then `Opcode::ArrayPostInit` is used
		// instead. This expects its member initializers already on the stack,
		// and combines them into an instance of a new `ArrayLiteral` type.

		if (expects_write_ctx)
		{
			AstDirectChildIterator it = direct_children_of(node);

			u16 element_count = 0;

			while (has_next(&it))
			{
				(void) next(&it);

				element_count += 1;
			}

			// TODO: Allow for element indices in initializers in the AST.
			const u16 index_count = 0;

			emit_opcode(opcodes, Opcode::ArrayPreInit, true, node, index_count, element_count);

			it = direct_children_of(node);

			while (has_next(&it))
			{
				AstNode* const element = next(&it);

				if (!opcodes_from_expression(opcodes, element, true))
					return false;
			}
		}
		else
		{
			AstDirectChildIterator it = direct_children_of(node);

			u16 element_count = 0;

			while (has_next(&it))
			{
				AstNode* const element = next(&it);

				if (!opcodes_from_expression(opcodes, element, false))
					return false;

				element_count += 1;
			}

			// TODO: Allow for element indices in initializers in the AST.
			const u16 total_element_count = element_count;

			// TODO: Allow for element indices in initializers in the AST.
			const u16 index_count = 0;

			emit_opcode(opcodes, Opcode::ArrayPostInit, false, node, total_element_count, index_count, element_count);
		}

		return true;
	}

	case AstTag::Block:
	{
		// Since we use `emit_opcode_raw` and not `emit_opcode`, we need to
		// manually adjust and check the current state.
		opcodes->state.scopes_diff += 1;

		byte* const attach = emit_opcode_raw(opcodes, Opcode::ScopeBegin, false, node, sizeof(u16));

		u16 definition_count = 0;

		AstDirectChildIterator it = direct_children_of(node);

		while (has_next(&it))
		{
			AstNode* const child = next(&it);

			if (child->tag == AstTag::Definition)
			{
				if (!has_next_sibling(child))
					TODO("Think about what to do here");

				opcodes_from_scope_definition(opcodes, child);

				definition_count += 1;
			}
			else
			{
				const s32 values_depth_before_expr = opcodes->state.values_diff;

				const bool is_last = !has_next_sibling(child);

				opcodes_from_expression(opcodes, child, is_last && expects_write_ctx);

				if (!is_last && opcodes->state.values_diff == values_depth_before_expr + 1)
					emit_opcode(opcodes, Opcode::DiscardVoid, false, child);

				ASSERT_OR_IGNORE(opcodes->state.values_diff == values_depth_before_expr + (is_last && !expects_write_ctx ? 1 : 0));
			}
		}

		if ((expects_write_ctx && opcodes->state.write_ctxs_diff == 1)
		 || (!expects_write_ctx && opcodes->state.values_diff == 0))
			emit_opcode(opcodes, Opcode::ValueVoid, expects_write_ctx, node);

		memcpy(attach, &definition_count, sizeof(u16));

		emit_opcode(opcodes, Opcode::ScopeEnd, false, node);

		return true;
	}

	case AstTag::If:
	{
		const IfInfo info = get_if_info(node);

		if (is_some(info.where))
		{
			emit_opcode(opcodes, Opcode::ScopeBegin, false, get(info.where));

			if (!opcodes_from_where(opcodes, get(info.where)))
				return false;
		}

		if (!opcodes_from_expression(opcodes, info.condition, false))
			return false;

		if (expects_write_ctx || is_some(info.alternative))
		{
			emit_fixup_for_if_branch(opcodes, opcodes->codes.end() + 1, info.consequent, expects_write_ctx, is_none(info.alternative));

			if (is_some(info.alternative))
				emit_fixup_for_if_branch(opcodes, opcodes->codes.end() + 1 + sizeof(OpcodeId), get(info.alternative), expects_write_ctx, false);
			else
				emit_fixup_for_value_void(opcodes, opcodes->codes.end() + 1 + sizeof(OpcodeId), node);

			emit_opcode(opcodes, Opcode::IfElse, expects_write_ctx, node, OpcodeId::INVALID, OpcodeId::INVALID);
		}
		else
		{
			emit_fixup_for_discarded_if_branch(opcodes, opcodes->codes.end() + 1, info.consequent);

			emit_opcode(opcodes, Opcode::If, false, node, OpcodeId::INVALID);
		}

		if (is_some(info.where))
			emit_opcode(opcodes, Opcode::ScopeEnd, false, get(info.where));

		return true;
	}

	case AstTag::For:
	{
		ForInfo info = get_for_info(node);

		if (is_none(info.finally) && expects_write_ctx)
			return false; // TODO: Error messge.

		if (is_some(info.where))
		{
			emit_opcode(opcodes, Opcode::ScopeBegin, false, get(info.where));

			opcodes_from_where(opcodes, get(info.where));
		}

		const OpcodeId condition_id = static_cast<OpcodeId>(opcodes->codes.used());

		if (!opcodes_from_expression(opcodes, info.condition, false))
			return false;

		emit_fixup_for_loop_body(opcodes, opcodes->codes.end() + 1 + sizeof(OpcodeId), info.body, info.step, expects_write_ctx, is_some(info.finally));

		if (is_some(info.finally))
		{
			emit_fixup_for_loop_finally(opcodes, opcodes->codes.end() + 1 + 2 * sizeof(OpcodeId), get(info.finally), expects_write_ctx);

			emit_opcode(opcodes, Opcode::LoopFinally, expects_write_ctx, node, condition_id, OpcodeId::INVALID, OpcodeId::INVALID);
		}
		else
		{
			emit_opcode(opcodes, Opcode::Loop, false, node, condition_id, OpcodeId::INVALID);
		}

		if (is_some(info.where))
			emit_opcode(opcodes, Opcode::ScopeEnd, false, get(info.where));

		return true;
	}

	case AstTag::Func:
	{
		AstNode* const signature = first_child_of(node);

		if (!opcodes_from_signature(opcodes, signature, false))
			return false;

		AstNode* const body = next_sibling_of(signature);

		const u16 closed_over_value_count = emit_func_closure_values(opcodes, node);

		Opcode* const body_fixup_dst = opcodes->codes.end() + 1;

		if (closed_over_value_count != 0)
			emit_opcode(opcodes, Opcode::BindBodyWithClosure, expects_write_ctx, node, OpcodeId::INVALID, closed_over_value_count);
		else
			emit_opcode(opcodes, Opcode::BindBody, expects_write_ctx, node, OpcodeId::INVALID);

		emit_fixup_for_function_body(opcodes, body_fixup_dst, body, closed_over_value_count != 0);

		return true;
	}

	case AstTag::Signature:
	{
		return opcodes_from_signature(opcodes, node, expects_write_ctx);
	}

	case AstTag::Unreachable:
	{
		emit_opcode(opcodes, Opcode::Unreachable, expects_write_ctx, node);

		return true;
	}

	case AstTag::Undefined:
	{
		emit_opcode(opcodes, Opcode::Undefined, expects_write_ctx, node);

		return true;
	}

	case AstTag::Identifier:
	{
		const NameBinding binding = attachment_of<AstIdentifierData>(node)->binding;

		if (binding.is_global)
			emit_opcode(opcodes, Opcode::LoadGlobal, expects_write_ctx, node, static_cast<GlobalFileIndex>(binding.global.file_index_bits), binding.global.rank);
		else if (binding.is_scoped)
			emit_opcode(opcodes, Opcode::LoadScope, expects_write_ctx, node, binding.scoped.out, binding.scoped.rank);
		else
			emit_opcode(opcodes, Opcode::LoadClosure, expects_write_ctx, node, binding.closed.rank_in_closure);

		return true;
	}

	case AstTag::LitInteger:
	{
		emit_opcode(opcodes, Opcode::ValueInteger, expects_write_ctx, node, attachment_of<AstLitIntegerData>(node)->value);

		return true;
	}

	case AstTag::LitFloat:
	{
		emit_opcode(opcodes, Opcode::ValueFloat, expects_write_ctx, node, attachment_of<AstLitFloatData>(node)->value);

		return true;
	}

	case AstTag::LitChar:
	{
		CompIntegerValue value = comp_integer_from_u64(attachment_of<AstLitCharData>(node)->codepoint);

		emit_opcode(opcodes, Opcode::ValueInteger, expects_write_ctx, node, value);

		return true;
	}

	case AstTag::LitString:
	{
		emit_opcode(opcodes, Opcode::ValueString, expects_write_ctx, node, attachment_of<AstLitStringData>(node)->string_value_id);

		return true;
	}

	case AstTag::OpSliceOf:
	{
		OpSliceOfInfo info = get_op_slice_of_info(node);

		if (!opcodes_from_expression(opcodes, info.sliced, false))
			return false;

		if (is_some(info.begin))
		{
			if (!opcodes_from_expression(opcodes, get(info.begin), false))
				return false;
		}

		if (is_some(info.end))
		{
			if (!opcodes_from_expression(opcodes, get(info.end), false))
				return false;
		}

		OpcodeSliceKind kind = is_some(info.begin) && is_some(info.end)
			? OpcodeSliceKind::BothBounds
			: is_some(info.begin)
			? OpcodeSliceKind::BeginBound
			: is_some(info.end)
			? OpcodeSliceKind::EndBound
			: OpcodeSliceKind::NoBounds;

		emit_opcode(opcodes, Opcode::Slice, expects_write_ctx, node, kind);

		return true;
	}

	case AstTag::Return:
	{
		if (!opcodes->allow_return)
			return false; // TODO: Error message.

		const s32 closures_diff = opcodes->state.closures_diff != 0 || opcodes->return_adjust.closures_diff != 0;

		ASSERT_OR_IGNORE(closures_diff == 0 || closures_diff == 1);

		if (closures_diff != 0)
			emit_opcode(opcodes, Opcode::PopClosure, false, node);

		const s32 values_diff = opcodes->state.values_diff + opcodes->return_adjust.values_diff;
		const s32 scopes_diff = opcodes->state.scopes_diff + opcodes->return_adjust.scopes_diff;
		const s32 write_ctxs_diff = opcodes->state.write_ctxs_diff + opcodes->return_adjust.write_ctxs_diff;

		if (values_diff != 0 || scopes_diff != 0 || write_ctxs_diff != 0)
			TODO("Discard extra elements");

		AstNode* const operand = first_child_of(node);

		if (!opcodes_from_expression(opcodes, operand, true))
			return false;

		emit_opcode(opcodes, Opcode::Return, false, node);

		return true;
	}

	case AstTag::Call:
	{
		AstNode* const callee = first_child_of(node);

		if (!opcodes_from_expression(opcodes, callee, false))
			return false;

		u8 argument_count = 0;

		for (AstNode* argument = callee; has_next_sibling(argument); argument = next_sibling_of(argument))
			argument_count += 1;

		// Since we use `emit_opcode_raw` and not `emit_opcode`, we need would
		// normally need to manually adjust and check the current state.
		// However, in this case there is nothing to update, as `Opcode::PrepareArgs`
		// has no direct effect on (the recorded part of) the state.
		byte* const attach = emit_opcode_raw(opcodes, Opcode::PrepareArgs, false, node, sizeof(u8) + argument_count * (sizeof(IdentifierId) + sizeof(OpcodeId)));

		memcpy(attach, &argument_count, sizeof(u8));

		if (argument_count != 0)
		{
			byte* const names_attach = attach + 1;

			byte* const callbacks_attach = attach + 1 + argument_count * sizeof(IdentifierId);

			u8 argument_index = 0;

			AstNode* argument = next_sibling_of(callee);

			while (true)
			{
				IdentifierId argument_name;

				AstNode* argument_value;

				if (argument->tag == AstTag::OpSet)
				{
					AstNode* const name = first_child_of(argument);

					const AstImpliedMemberData* const name_attach = attachment_of<AstImpliedMemberData>(name);

					argument_name = name_attach->identifier_id;

					argument_value = next_sibling_of(name);
				}
				else
				{
					argument_name = IdentifierId::INVALID;

					argument_value = argument;
				}

				memcpy(names_attach + argument_index * sizeof(IdentifierId), &argument_name, sizeof(IdentifierId));

				emit_fixup_for_argument(opcodes, reinterpret_cast<Opcode*>(callbacks_attach + argument_index * sizeof(OpcodeId)), argument_value);

				if (!has_next_sibling(argument))
					break;

				argument = next_sibling_of(argument);

				argument_index += 1;
			}
		}

		emit_opcode(opcodes, Opcode::ExecArgs, false, node);

		emit_opcode(opcodes, Opcode::Call, expects_write_ctx, node);

		return true;
	}

	case AstTag::UOpTypeSlice:
	{
		AstNode* const operand = first_child_of(node);

		if (!opcodes_from_expression(opcodes, operand, false))
			return false;

		OpcodeReferenceTypeFlags flags{};
		flags.tag = static_cast<u8>(TypeTag::Slice);
		flags.is_opt = false;
		flags.is_multi = false;
		flags.is_mut = has_flag(node, AstFlag::Type_IsMut);

		emit_opcode(opcodes, Opcode::ReferenceType, expects_write_ctx, node, flags);

		return true;
	}

	case AstTag::UOpTypeMultiPtr:
	{
		AstNode* const operand = first_child_of(node);

		if (!opcodes_from_expression(opcodes, operand, false))
			return false;

		OpcodeReferenceTypeFlags flags{};
		flags.tag = static_cast<u8>(TypeTag::Ptr);
		flags.is_opt = false;
		flags.is_multi = true;
		flags.is_mut = has_flag(node, AstFlag::Type_IsMut);

		emit_opcode(opcodes, Opcode::ReferenceType, expects_write_ctx, node, flags);

		return true;
	}

	case AstTag::UOpTypeOptMultiPtr:
	{
		AstNode* const operand = first_child_of(node);

		if (!opcodes_from_expression(opcodes, operand, false))
			return false;

		OpcodeReferenceTypeFlags flags{};
		flags.tag = static_cast<u8>(TypeTag::Ptr);
		flags.is_opt = true;
		flags.is_multi = true;
		flags.is_mut = has_flag(node, AstFlag::Type_IsMut);

		emit_opcode(opcodes, Opcode::ReferenceType, expects_write_ctx, node, flags);

		return true;
	}

	case AstTag::UOpAddr:
	{
		AstNode* const operand = first_child_of(node);

		if (!opcodes_from_expression(opcodes, operand, false))
			return false;

		emit_opcode(opcodes, Opcode::AddressOf, expects_write_ctx, node);

		return true;
	}

	case AstTag::UOpDeref:
	{
		AstNode* const operand = first_child_of(node);

		if (!opcodes_from_expression(opcodes, operand, false))
			return false;

		emit_opcode(opcodes, Opcode::Dereference, expects_write_ctx, node);

		return true;
	}

	case AstTag::UOpBitNot:
	{
		AstNode* const operand = first_child_of(node);

		if (!opcodes_from_expression(opcodes, operand, false))
			return false;

		emit_opcode(opcodes, Opcode::BitNot, expects_write_ctx, node);

		return true;
	}

	case AstTag::UOpLogNot:
	{
		AstNode* const operand = first_child_of(node);

		if (!opcodes_from_expression(opcodes, operand, false))
			return false;

		emit_opcode(opcodes, Opcode::LogicalNot, expects_write_ctx, node);

		return true;
	}

	case AstTag::UOpTypeOptPtr:
	{
		AstNode* const operand = first_child_of(node);

		if (!opcodes_from_expression(opcodes, operand, false))
			return false;

		OpcodeReferenceTypeFlags flags{};
		flags.tag = static_cast<u8>(TypeTag::Ptr);
		flags.is_opt = true;
		flags.is_multi = false;
		flags.is_mut = has_flag(node, AstFlag::Type_IsMut);

		emit_opcode(opcodes, Opcode::ReferenceType, expects_write_ctx, node, flags);

		return true;
	}

	case AstTag::UOpTypePtr:
	{
		AstNode* const operand = first_child_of(node);

		if (!opcodes_from_expression(opcodes, operand, false))
			return false;

		OpcodeReferenceTypeFlags flags{};
		flags.tag = static_cast<u8>(TypeTag::Ptr);
		flags.is_opt = false;
		flags.is_multi = false;
		flags.is_mut = has_flag(node, AstFlag::Type_IsMut);

		emit_opcode(opcodes, Opcode::ReferenceType, expects_write_ctx, node, flags);

		return true;
	}

	case AstTag::UOpNegate:
	{
		AstNode* const operand = first_child_of(node);

		if (!opcodes_from_expression(opcodes, operand, false))
			return false;

		emit_opcode(opcodes, Opcode::Negate, expects_write_ctx, node);

		return true;
	}

	case AstTag::UOpPos:
	{
		AstNode* const operand = first_child_of(node);

		if (!opcodes_from_expression(opcodes, operand, false))
			return false;

		emit_opcode(opcodes, Opcode::UnaryPlus, expects_write_ctx, node);

		return true;
	}

	case AstTag::OpAdd:
	case AstTag::OpSub:
	case AstTag::OpMul:
	case AstTag::OpDiv:
	case AstTag::OpAddTC:
	case AstTag::OpSubTC:
	case AstTag::OpMulTC:
	case AstTag::OpMod:
	{
		AstNode* const lhs = first_child_of(node);

		if (!opcodes_from_expression(opcodes, lhs, false))
			return false;

		AstNode* const rhs = next_sibling_of(lhs);

		if (!opcodes_from_expression(opcodes, rhs, false))
			return false;

		const OpcodeBinaryArithmeticOpKind kind = static_cast<OpcodeBinaryArithmeticOpKind>(static_cast<u8>(node->tag) - static_cast<u8>(AstTag::OpAdd));

		emit_opcode(opcodes, Opcode::BinaryArithmeticOp, expects_write_ctx, node, kind);

		return true;
	}

	case AstTag::OpBitAnd:
	case AstTag::OpBitOr:
	case AstTag::OpBitXor:
	{
		AstNode* const lhs = first_child_of(node);

		if (!opcodes_from_expression(opcodes, lhs, false))
			return false;

		AstNode* const rhs = next_sibling_of(lhs);

		if (!opcodes_from_expression(opcodes, rhs, false))
			return false;

		const OpcodeBinaryBitwiseOpKind kind = static_cast<OpcodeBinaryBitwiseOpKind>(static_cast<u8>(node->tag) - static_cast<u8>(AstTag::OpBitAnd));

		emit_opcode(opcodes, Opcode::BinaryBitwiseOp, expects_write_ctx, node, kind);

		return true;
	}

	case AstTag::OpShiftL:
	case AstTag::OpShiftR:
	{
		AstNode* const lhs = first_child_of(node);

		if (!opcodes_from_expression(opcodes, lhs, false))
			return false;

		AstNode* const rhs = next_sibling_of(lhs);

		if (!opcodes_from_expression(opcodes, rhs, false))
			return false;

		const OpcodeShiftKind kind = static_cast<OpcodeShiftKind>(static_cast<u8>(node->tag) - static_cast<u8>(AstTag::OpShiftL));

		emit_opcode(opcodes, Opcode::Shift, expects_write_ctx, node, kind);

		return true;
	}

	case AstTag::OpLogAnd:
	{
		AstNode* const lhs = first_child_of(node);

		if (!opcodes_from_expression(opcodes, lhs, false))
			return false;

		AstNode* const rhs = next_sibling_of(lhs);

		if (!opcodes_from_expression(opcodes, rhs, false))
			return false;

		emit_opcode(opcodes, Opcode::LogicalAnd, expects_write_ctx, node);

		return true;
	}

	case AstTag::OpLogOr:
	{
		AstNode* const lhs = first_child_of(node);

		if (!opcodes_from_expression(opcodes, lhs, false))
			return false;

		AstNode* const rhs = next_sibling_of(lhs);

		if (!opcodes_from_expression(opcodes, rhs, false))
			return false;

		emit_opcode(opcodes, Opcode::LogicalOr, expects_write_ctx, node);

		return true;
	}

	case AstTag::Member:
	{
		AstNode* const operand = first_child_of(node);

		if (!opcodes_from_expression(opcodes, operand, false))
			return false;

		const IdentifierId member_name = attachment_of<AstMemberData>(node)->identifier_id;

		emit_opcode(opcodes, Opcode::LoadMember, expects_write_ctx, node, member_name);

		return true;
	}

	case AstTag::OpCmpLT:
	case AstTag::OpCmpGT:
	case AstTag::OpCmpLE:
	case AstTag::OpCmpGE:
	case AstTag::OpCmpNE:
	case AstTag::OpCmpEQ:
	{
		AstNode* const lhs = first_child_of(node);

		if (!opcodes_from_expression(opcodes, lhs, false))
			return false;

		AstNode* const rhs = next_sibling_of(lhs);

		if (!opcodes_from_expression(opcodes, rhs, false))
			return false;

		const OpcodeCompareKind kind = static_cast<OpcodeCompareKind>(static_cast<u8>(node->tag) - static_cast<u8>(AstTag::OpCmpLT));

		emit_opcode(opcodes, Opcode::Compare, expects_write_ctx, node, kind);

		return true;
	}

	case AstTag::OpSet:
	{
		ASSERT_OR_IGNORE(!expects_write_ctx);

		AstNode* const lhs = first_child_of(node);

		if (!opcodes_from_expression(opcodes, lhs, false))
			return false;

		emit_opcode(opcodes, Opcode::SetWriteCtx, false, node);

		AstNode* const rhs = next_sibling_of(lhs);

		if (!opcodes_from_expression(opcodes, rhs, true))
			return false;

		return true;
	}

	case AstTag::OpTypeArray:
	{
		AstNode* const lhs = first_child_of(node);

		if (!opcodes_from_expression(opcodes, lhs, false))
			return false;

		AstNode* const rhs = next_sibling_of(lhs);

		if (!opcodes_from_expression(opcodes, rhs, false))
			return false;

		emit_opcode(opcodes, Opcode::ArrayType, expects_write_ctx, node);

		return true;
	}

	case AstTag::OpArrayIndex:
	{
		AstNode* const lhs = first_child_of(node);

		if (!opcodes_from_expression(opcodes, lhs, false))
			return false;

		AstNode* const rhs = next_sibling_of(lhs);

		if (!opcodes_from_expression(opcodes, rhs, false))
			return false;

		emit_opcode(opcodes, Opcode::Index, expects_write_ctx, node);

		return true;
	}

	case AstTag::Wildcard:
	case AstTag::Expects:
	case AstTag::Ensures:
	case AstTag::Definition:
	case AstTag::ForEach:
	case AstTag::Switch:
	case AstTag::Trait:
	case AstTag::Impl:
	case AstTag::Catch:
	case AstTag::Leave:
	case AstTag::Yield:
	case AstTag::UOpTypeTailArray:
	case AstTag::UOpEval:
	case AstTag::UOpTry:
	case AstTag::UOpDefer:
	case AstTag::UOpDistinct:
	case AstTag::UOpTypeVarArgs:

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
		TODO("Implement");

	case AstTag::INVALID:
	case AstTag::File:
	case AstTag::Where:
	case AstTag::Parameter:
	case AstTag::Case:
	case AstTag::ParameterList:
	case AstTag::ImpliedMember:
	case AstTag::MAX:
		; // Fallthrough to unreachable
	}

	ASSERT_UNREACHABLE;
}

static bool complete_fixup(OpcodePool* opcodes, Fixup fixup) noexcept
{
	switch (fixup.kind)
	{
	case FixupKind::FunctionBody:
	{
		opcodes->state = OpcodeEffects{};
		opcodes->state.write_ctxs_diff = 1;
		opcodes->state.closures_diff = fixup.function_body_has_closure ? 1 : 0;

		opcodes->return_adjust = OpcodeEffects{};

		opcodes->allow_return = true;

		AstNode* const node = ast_node_from_id(opcodes->asts, fixup.node_id);

		if (!opcodes_from_expression(opcodes, node, true))
			return false;

		if (fixup.function_body_has_closure)
			emit_opcode(opcodes, Opcode::PopClosure, false, node);

		emit_opcode(opcodes, Opcode::Return, false, node);

		ASSERT_OR_IGNORE(
			opcodes->state.values_diff == 0
		 && opcodes->state.scopes_diff == 0
		 && opcodes->state.write_ctxs_diff == 0
		 && opcodes->state.closures_diff == 0
		);

		return true;
	}

	case FixupKind::Argument:
	{
		opcodes->state = OpcodeEffects{};
		opcodes->state.write_ctxs_diff = 1;

		opcodes->allow_return = false;

		AstNode* const node = ast_node_from_id(opcodes->asts, fixup.node_id);

		if (!opcodes_from_expression(opcodes, node, true))
			return false;

		emit_opcode(opcodes, Opcode::EndCode, false, node);

		ASSERT_OR_IGNORE(
			opcodes->state.values_diff == 0
		 && opcodes->state.scopes_diff == 0
		 && opcodes->state.write_ctxs_diff == 0
		 && opcodes->state.closures_diff == 0
		);

		return true;
	}

	case FixupKind::TemplateParameter:
	{
		opcodes->state = OpcodeEffects{};

		opcodes->allow_return = false;

		AstNode* const node = ast_node_from_id(opcodes->asts, fixup.node_id);

		if (!opcodes_from_expression(opcodes, node, false))
			return false;

		if (is_some(fixup.second_node_id))
		{
			AstNode* const second_node = ast_node_from_id(opcodes->asts, get(fixup.second_node_id));

			if (!opcodes_from_expression(opcodes, second_node, false))
				return false;

			ASSERT_OR_IGNORE(
				opcodes->state.values_diff == 1
			 && opcodes->state.scopes_diff == 0
			 && opcodes->state.write_ctxs_diff == 0
			 && opcodes->state.closures_diff == 0
			);
		}

		if (!fixup.template_parameter_has_type)
			emit_opcode(opcodes, Opcode::CompleteParamTypedNoDefault, false, node, fixup.template_parameter_rank);
		else if (!fixup.template_parameter_has_value)
			emit_opcode(opcodes, Opcode::CompleteParamUntyped, false, node, fixup.template_parameter_rank);
		else
			emit_opcode(opcodes, Opcode::CompleteParamTypedWithDefault, false, node, fixup.template_parameter_rank);

		emit_opcode(opcodes, Opcode::EndCode, false, node);

		ASSERT_OR_IGNORE(
			opcodes->state.values_diff == 0
		 && opcodes->state.scopes_diff == 0
		 && opcodes->state.write_ctxs_diff == 0
		 && opcodes->state.closures_diff == 0
		);

		return true;
	}

	case FixupKind::TemplateReturnType:
	{
		opcodes->state = OpcodeEffects{};
		opcodes->state.write_ctxs_diff = 1;

		opcodes->allow_return = false;

		AstNode* const node = ast_node_from_id(opcodes->asts, fixup.node_id);

		if (!opcodes_from_expression(opcodes, node, true))
			return false;

		emit_opcode(opcodes, Opcode::EndCode, false, node);

		ASSERT_OR_IGNORE(
			opcodes->state.values_diff == 0
		 && opcodes->state.scopes_diff == 0
		 && opcodes->state.write_ctxs_diff == 0
		 && opcodes->state.closures_diff == 0
		);

		return true;
	}

	case FixupKind::ValueVoid:
	{
		opcodes->state = OpcodeEffects{};
		opcodes->state.write_ctxs_diff = 1;

		opcodes->allow_return = false;

		AstNode* const node = ast_node_from_id(opcodes->asts, fixup.node_id);

		emit_opcode(opcodes, Opcode::ValueVoid, fixup.expects_write_ctx, node);

		emit_opcode(opcodes, Opcode::EndCode, false, node);

		return true;
	}

	case FixupKind::IfBranch:
	{
		opcodes->state = OpcodeEffects{};
		opcodes->state.write_ctxs_diff = fixup.expects_write_ctx ? 1 : 0;

		if (fixup.allow_return)
			opcodes->return_adjust = fixup.return_adjust;

		opcodes->allow_return = fixup.allow_return;

		AstNode* const node = ast_node_from_id(opcodes->asts, fixup.node_id);

		if (fixup.if_branch_expect_void && fixup.expects_write_ctx)
			emit_opcode(opcodes, Opcode::CheckWriteCtxVoid, false, node);

		if (!opcodes_from_expression(opcodes, node, true))
			return false;

		if (fixup.if_branch_expect_void && !fixup.expects_write_ctx)
			emit_opcode(opcodes, Opcode::CheckTopVoid, false, node);

		emit_opcode(opcodes, Opcode::EndCode, false, node);

		ASSERT_OR_IGNORE(
			opcodes->state.values_diff == (fixup.expects_write_ctx ? 0 : 1)
		 && opcodes->state.scopes_diff == 0
		 && opcodes->state.write_ctxs_diff == 0
		 && opcodes->state.closures_diff == 0
		);

		return true;
	}

	case FixupKind::DiscardedIfBranch:
	{
		opcodes->state = OpcodeEffects{};

		if (fixup.allow_return)
			opcodes->return_adjust = fixup.return_adjust;

		opcodes->allow_return = fixup.allow_return;

		AstNode* const node = ast_node_from_id(opcodes->asts, fixup.node_id);

		if (!opcodes_from_expression(opcodes, node, true))
			return false;

		emit_opcode(opcodes, Opcode::DiscardVoid, false, node);

		emit_opcode(opcodes, Opcode::EndCode, false, node);

		ASSERT_OR_IGNORE(
			opcodes->state.values_diff == 0
		 && opcodes->state.scopes_diff == 0
		 && opcodes->state.write_ctxs_diff == 0
		 && opcodes->state.closures_diff == 0
		);

		return true;
	}

	case FixupKind::LoopBody:
	{
		opcodes->state = OpcodeEffects{};
		opcodes->state.write_ctxs_diff = fixup.expects_write_ctx ? 1 : 0;

		if (fixup.allow_return)
			opcodes->return_adjust = fixup.return_adjust;

		opcodes->allow_return = fixup.allow_return;

		if (is_some(fixup.second_node_id))
		{
			AstNode* const second_node = ast_node_from_id(opcodes->asts, get(fixup.second_node_id));

			if (!opcodes_from_expression(opcodes, second_node, false))
				return false;

			if (opcodes->state.values_diff == 1)
				emit_opcode(opcodes, Opcode::DiscardVoid, false, second_node);

			ASSERT_OR_IGNORE(
				opcodes->state.values_diff == 0
			 && opcodes->state.scopes_diff == 0
			 && opcodes->state.write_ctxs_diff == (fixup.expects_write_ctx ? 1 : 0)
			 && opcodes->state.closures_diff == 0
			);
		}

		AstNode* const node = ast_node_from_id(opcodes->asts, fixup.node_id);

		if (!opcodes_from_expression(opcodes, node, fixup.expects_write_ctx))
			return false;

		emit_opcode(opcodes, Opcode::DiscardVoid, false, node);

		emit_opcode(opcodes, Opcode::EndCode, false, node);

		ASSERT_OR_IGNORE(
			opcodes->state.values_diff == 0
		 && opcodes->state.scopes_diff == 0
		 && opcodes->state.write_ctxs_diff == 0
		 && opcodes->state.closures_diff == 0
		);

		return true;
	}

	case FixupKind::LoopFinally:
	{
		opcodes->state = OpcodeEffects{};
		opcodes->state.write_ctxs_diff = fixup.expects_write_ctx ? 1 : 0;

		if (fixup.allow_return)
			opcodes->return_adjust = fixup.return_adjust;

		opcodes->allow_return = fixup.allow_return;

		AstNode* const node = ast_node_from_id(opcodes->asts, fixup.node_id);

		if (!opcodes_from_expression(opcodes, node, fixup.expects_write_ctx))
			return false;

		emit_opcode(opcodes, Opcode::EndCode, false, node);

		ASSERT_OR_IGNORE(
			opcodes->state.values_diff == (fixup.expects_write_ctx ? 0 : 1)
		 && opcodes->state.scopes_diff == 0
		 && opcodes->state.write_ctxs_diff == 0
		 && opcodes->state.closures_diff == 0
		);

		return true;
	}

	case FixupKind::INVALID:
		; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

static bool complete_fixups(OpcodePool* opcodes) noexcept
{
	while (opcodes->fixups.used() != 0)
	{
		const Fixup fixup = opcodes->fixups.end()[-1];

		opcodes->fixups.pop_by(1);

		const OpcodeId fixup_loc = static_cast<OpcodeId>(opcodes->codes.used());

		byte* const fixup_dst = reinterpret_cast<byte*>(opcodes->codes.begin()) + static_cast<u32>(fixup.dst_id);
		memcpy(fixup_dst, &fixup_loc, sizeof(OpcodeId));

		if (!complete_fixup(opcodes, fixup))
			return false;

		ASSERT_OR_IGNORE(
			opcodes->state.values_diff == 0
		 && opcodes->state.scopes_diff == 0
		 && opcodes->state.write_ctxs_diff == 0
		 && opcodes->state.closures_diff == 0
		);
	}

	return true;
}



OpcodePool* create_opcode_pool(HandlePool* handles, AstPool* asts) noexcept
{
	static constexpr u32 OPCODES_RESERVE_SIZE = 1 << 26;

	static constexpr u32 OPCODES_COMMIT_INCREMENT_COUNT = 1 << 16;

	static constexpr u32 SOURCES_RESERVERE_SIZE = 1 << 26;

	static constexpr u32 SOURCES_COMMIT_INCREMENT_COUNT = 1 << 13;

	static constexpr u32 FIXUPS_RESERVERE_SIZE = (1 << 20) * 3;

	static constexpr u32 FIXUPS_COMMIT_INCREMENT_COUNT = 1 << 12;

	static constexpr u64 TOTAL_RESERVE_SIZE = static_cast<u64>(OPCODES_RESERVE_SIZE)
	                                        + SOURCES_RESERVERE_SIZE
	                                        + FIXUPS_RESERVERE_SIZE;

	byte* const memory = static_cast<byte*>(minos::mem_reserve(TOTAL_RESERVE_SIZE));

	if (memory == nullptr)
		panic("Failed to allocate memory for OpcodePool (0x%X).\n", minos::last_error());

	OpcodePool* const opcodes = alloc_handle_from_pool<OpcodePool>(handles);
	opcodes->asts = asts;

	u64 offset = 0;

	opcodes->codes.init({ memory + offset, OPCODES_RESERVE_SIZE }, OPCODES_COMMIT_INCREMENT_COUNT);
	offset += OPCODES_RESERVE_SIZE;

	opcodes->sources.init({ memory + offset, SOURCES_RESERVERE_SIZE }, SOURCES_COMMIT_INCREMENT_COUNT);
	offset += SOURCES_RESERVERE_SIZE;

	opcodes->fixups.init({ memory + offset, FIXUPS_RESERVERE_SIZE }, FIXUPS_COMMIT_INCREMENT_COUNT);
	offset += FIXUPS_RESERVERE_SIZE;

	ASSERT_OR_IGNORE(offset == TOTAL_RESERVE_SIZE);

	opcodes->memory = MutRange<byte>{ memory, TOTAL_RESERVE_SIZE };

	// Reserve `OpcodeId::INVALID`.
	(void) opcodes->codes.reserve();

	return opcodes;
}

void release_opcode_pool(OpcodePool* opcodes) noexcept
{
	minos::mem_unreserve(opcodes->memory.begin(), opcodes->memory.count());
}

const Maybe<Opcode*> opcodes_from_file_member_ast(OpcodePool* opcodes, AstNode* node, GlobalFileIndex file_index, u16 rank) noexcept
{
	opcodes->state.values_diff = 0;
	opcodes->state.scopes_diff = 0;
	opcodes->state.write_ctxs_diff = 0;
	opcodes->state.closures_diff = 0;

	Opcode* const first_opcode = opcodes->codes.end();

	DefinitionInfo info = get_definition_info(node);

	ASSERT_OR_IGNORE(is_some(info.value));

	const bool has_type = is_some(info.type);

	const bool is_mut = has_flag(node, AstFlag::Definition_IsMut);

	emit_opcode(opcodes, Opcode::FileGlobalAllocPrepare, false, node, is_mut, file_index, rank);

	if (has_type)
	{
		if (!opcodes_from_expression(opcodes, get(info.type), false))
			return none<Opcode*>();

		emit_opcode(opcodes, Opcode::FileGlobalAllocTyped, false, node);
	}

	if (!opcodes_from_expression(opcodes, get(info.value), has_type))
		return none<Opcode*>();

	if (has_type)
		emit_opcode(opcodes, Opcode::FileGlobalAllocComplete, false, node);
	else
		emit_opcode(opcodes, Opcode::FileGlobalAllocUntyped, false, node);

	emit_opcode(opcodes, Opcode::EndCode, false, node);

	if (!complete_fixups(opcodes))
		return none<Opcode*>();

	ASSERT_OR_IGNORE(
		opcodes->state.values_diff == 0
	 && opcodes->state.scopes_diff == 0
	 && opcodes->state.write_ctxs_diff == 0
	 && opcodes->state.closures_diff == 0
	);

	return some(first_opcode);
}

OpcodeId opcode_id_from_builtin(OpcodePool* opcodes, Builtin builtin) noexcept
{
	opcodes->state.values_diff = 0;
	opcodes->state.scopes_diff = 0;
	opcodes->state.write_ctxs_diff = 1;
	opcodes->state.closures_diff = 0;

	opcodes->allow_return = false;

	const OpcodeId first_opcode_id = static_cast<OpcodeId>(opcodes->codes.used());

	emit_opcode(opcodes, Opcode::ExecBuiltin, true, nullptr, builtin);

	emit_opcode(opcodes, Opcode::Return, false, nullptr);

	ASSERT_OR_IGNORE(
		opcodes->state.values_diff == 0
	 && opcodes->state.scopes_diff == 0
	 && opcodes->state.write_ctxs_diff == 0
	 && opcodes->state.closures_diff == 0
	);

	return first_opcode_id;
}

OpcodeId id_from_opcode(OpcodePool* opcodes, const Opcode* code)
{
	ASSERT_OR_IGNORE(code >= opcodes->codes.begin() && code < opcodes->codes.end());

	return static_cast<OpcodeId>(code - opcodes->codes.begin());
}

const Opcode* opcode_from_id(OpcodePool* opcodes, OpcodeId id) noexcept
{
	ASSERT_OR_IGNORE(id != OpcodeId::INVALID && static_cast<u32>(id) < opcodes->codes.used());

	return opcodes->codes.begin() + static_cast<u32>(id);
}

OpcodeEffects opcode_effects(const Opcode* code) noexcept
{
	const u8 op_bits = static_cast<u8>(*code);

	const bool expects_write_ctx = ((op_bits & 0x80) != 0);

	const Opcode op = static_cast<Opcode>(op_bits & 0x7F);

	OpcodeEffects rst{};

	switch (op)
	{
	case Opcode::EndCode:
	case Opcode::FileGlobalAllocPrepare:
	case Opcode::FileGlobalAllocComplete:
	case Opcode::PrepareArgs:
	case Opcode::ExecArgs:
	case Opcode::Return:
	case Opcode::CheckTopVoid:
	case Opcode::CheckWriteCtxVoid:
	{
		ASSERT_OR_IGNORE(!expects_write_ctx);

		return rst;
	}

	case Opcode::SetWriteCtx:
	case Opcode::ScopeAllocTyped:
	case Opcode::FileGlobalAllocTyped:
	case Opcode::CompleteParamTypedNoDefault:
	case Opcode::CompleteParamTypedWithDefault:
	{
		ASSERT_OR_IGNORE(!expects_write_ctx);

		rst.values_diff = -1;
		rst.write_ctxs_diff = 1;
		
		return rst;
	}

	case Opcode::ScopeBegin:
	{
		ASSERT_OR_IGNORE(!expects_write_ctx);

		rst.scopes_diff = 1;

		return rst;
	}

	case Opcode::ScopeEnd:
	{
		ASSERT_OR_IGNORE(!expects_write_ctx);

		rst.scopes_diff = -1;

		return rst;
	}

	case Opcode::ScopeAllocUntyped:
	case Opcode::FileGlobalAllocUntyped:
	case Opcode::CompleteParamUntyped:
	{
		ASSERT_OR_IGNORE(!expects_write_ctx);

		rst.values_diff = -1;

		return rst;
	}

	case Opcode::PopClosure:
	{
		ASSERT_OR_IGNORE(!expects_write_ctx);

		rst.closures_diff = -1;

		return rst;
	}

	case Opcode::LoadScope:
	case Opcode::LoadGlobal:
	case Opcode::LoadClosure:
	case Opcode::LoadBuiltin:
	case Opcode::LoopFinally:
	case Opcode::Undefined:
	case Opcode::Unreachable:
	case Opcode::ValueInteger:
	case Opcode::ValueFloat:
	case Opcode::ValueString:
	case Opcode::ValueVoid:
	{
		if (expects_write_ctx)
			rst.write_ctxs_diff = -1;
		else
			rst.values_diff += 1;

		return rst;
	}

	case Opcode::ExecBuiltin:
	{
		ASSERT_OR_IGNORE(expects_write_ctx);

		rst.write_ctxs_diff -= 1;

		return rst;
	}

	case Opcode::Signature:
	{
		u8 value_count;
		memcpy(&value_count, code + 3, sizeof(value_count));

		if (expects_write_ctx)
		{
			rst.values_diff = -value_count;
			rst.write_ctxs_diff = -1;
		}
		else
		{
			rst.values_diff = -(value_count - 1);
		}

		return rst;
	}

	case Opcode::DynSignature:
	{
		u8 value_count;
		memcpy(&value_count, code + 3, sizeof(value_count));

		u16 closed_over_value_count;
		memcpy(&closed_over_value_count, code + 4, sizeof(closed_over_value_count));

		const s32 total_value_count = value_count + closed_over_value_count;

		if (expects_write_ctx)
		{
			rst.values_diff = -total_value_count;
			rst.write_ctxs_diff = -1;
		}
		else
		{
			rst.values_diff = -(total_value_count - 1);
		}

		return rst;
	}

	case Opcode::LoadMember:
	case Opcode::BindBody:
	case Opcode::BindBodyWithClosure:
	case Opcode::Call:
	case Opcode::IfElse:
	case Opcode::AddressOf:
	case Opcode::Dereference:
	case Opcode::BitNot:
	case Opcode::LogicalNot:
	case Opcode::Negate:
	case Opcode::UnaryPlus:
	case Opcode::ReferenceType:
	{
		if (expects_write_ctx)
		{
			rst.values_diff = -1;
			rst.write_ctxs_diff = -1;
		}
		
		return rst;
	}

	case Opcode::ArrayPreInit:
	{
		ASSERT_OR_IGNORE(expects_write_ctx);

		u16 index_count;
		memcpy(&index_count, code + 1, sizeof(index_count));

		u16 leading_element_count;
		memcpy(&leading_element_count, code + 3, sizeof(leading_element_count));

		u32 total_element_count = leading_element_count;

		for (u16 i = 0; i != index_count; ++i)
		{
			u16 following_element_count;
			memcpy(&following_element_count, code + 3 + i * sizeof(following_element_count), sizeof(following_element_count));

			total_element_count += following_element_count;
		}

		rst.values_diff = -static_cast<s32>(index_count);
		rst.write_ctxs_diff = static_cast<s32>(total_element_count) - 1;

		return rst;
	}

	case Opcode::ArrayPostInit:
	{
		ASSERT_OR_IGNORE(!expects_write_ctx);

		u16 total_element_count;
		memcpy(&total_element_count, code + 1, sizeof(total_element_count));

		u16 index_count;
		memcpy(&index_count, code + 3, sizeof(index_count));

		rst.values_diff = 1 - static_cast<s32>(total_element_count + index_count);

		return rst;
	}

	case Opcode::CompositePreInit:
	{
		ASSERT_OR_IGNORE(expects_write_ctx);

		u16 names_count;
		memcpy(&names_count, code + 1, sizeof(names_count));

		u16 leading_member_count;
		memcpy(&leading_member_count, code + 3, sizeof(leading_member_count));

		u32 total_member_count = leading_member_count;

		for (u16 i = 0; i != names_count; ++i)
		{
			u16 following_member_count;
			memcpy(&following_member_count, code + 3 + i * (sizeof(IdentifierId) + sizeof(u16)) + sizeof(IdentifierId), sizeof(following_member_count));

			total_member_count += following_member_count;
		}

		rst.write_ctxs_diff = static_cast<u32>(total_member_count) - 1;

		return rst;
	}

	case Opcode::CompositePostInit:
	{
		ASSERT_OR_IGNORE(!expects_write_ctx);

		u16 total_member_count;
		memcpy(&total_member_count, code + 1, sizeof(total_member_count));

		rst.values_diff = 1 - static_cast<s32>(total_member_count);

		return rst;
	}

	case Opcode::If:
	case Opcode::Loop:
	case Opcode::DiscardVoid:
	{
		ASSERT_OR_IGNORE(!expects_write_ctx);

		rst.values_diff = -1;

		return rst;
	}

	case Opcode::Slice:
	{
		OpcodeSliceKind kind;
		memcpy(&kind, code + 1, sizeof(kind));

		const s32 values_diff = kind == OpcodeSliceKind::NoBounds
			? 0
			: kind == OpcodeSliceKind::BothBounds
			? -2
			: -1;

		if (expects_write_ctx)
		{
			rst.values_diff = values_diff - 1;
			rst.write_ctxs_diff = -1;
		}
		else
		{
			rst.values_diff = values_diff;
		}

		return rst;
	}

	case Opcode::Index:
	case Opcode::BinaryArithmeticOp:
	case Opcode::Shift:
	case Opcode::BinaryBitwiseOp:
	case Opcode::LogicalAnd:
	case Opcode::LogicalOr:
	case Opcode::Compare:
	case Opcode::ArrayType:
	{
		if (expects_write_ctx)
		{
			rst.values_diff = -2;
			rst.write_ctxs_diff = -1;
		}
		else
		{
			rst.values_diff = -1;
		}

		return rst;
	}

	case Opcode::Switch:
		TODO("Implement");

	case Opcode::INVALID:
		; // Fallthrough to unreachable
	}

	ASSERT_UNREACHABLE;
}

SourceId source_id_of_opcode(OpcodePool* opcodes, const Opcode* code) noexcept
{
	ASSERT_OR_IGNORE(code > opcodes->codes.begin() && code < opcodes->codes.end());

	ASSERT_OR_IGNORE(opcodes->sources.used() != 0);

	SourceMapping* const sources = opcodes->sources.begin();

	const OpcodeId target = id_from_opcode(opcodes, code);

	u32 lo = 0;

	u32 hi = opcodes->sources.used() - 1;

	if (sources[hi].code_begin <= target)
		return sources[hi].source;

	// We already checked the last entry; Ignore it from here on.
	hi -= 1;

	while (lo <= hi)
	{
		const u32 mid = (lo + hi) >> 1;

		const SourceMapping* const curr = sources + mid;

		const SourceMapping* const next = curr + 1;

		if (curr->code_begin > target)
		{
			hi = mid - 1;
		}
		else if (next->code_begin <= target)
		{
			lo = mid + 1;
		}
		else
		{
			return curr->source;	
		}
	}

	return sources[lo].source;
}

const char8* tag_name(Opcode op) noexcept
{
	static constexpr const char8* TAG_NAMES[] = {
		"INVALID",
		"EndCode",
		"SetWriteCtx",
		"ScopeBegin",
		"ScopeEnd",
		"ScopeAllocTyped",
		"ScopeAllocUntyped",
		"FileGlobalAllocPrepare",
		"FileGlobalAllocComplete",
		"FileGlobalAllocTyped",
		"FileGlobalAllocUntyped",
		"PopClosure",
		"LoadScope",
		"LoadGlobal",
		"LoadMember",
		"LoadClosure",
		"LoadBuiltin",
		"ExecBuiltin",
		"Signature",
		"DynSignature",
		"BindBody",
		"BindBodyWithClosure",
		"PrepareArgs",
		"ExecArgs",
		"Call",
		"Return",
		"CompleteParamTypedNoDefault",
		"CompleteParamTypedWithDefault",
		"CompleteParamUntyped",
		"ArrayPreInit",
		"ArrayPostInit",
		"CompositePreInit",
		"CompositePostInit",
		"If",
		"IfElse",
		"Loop",
		"LoopFinally",
		"Switch",
		"AddressOf",
		"Dereference",
		"Slice",
		"Index",
		"BinaryArithmeticOp",
		"Shift",
		"BinaryBitwiseOp",
		"BitNot",
		"LogicalAnd",
		"LogicalOr",
		"LogicalNot",
		"Compare",
		"Negate",
		"UnaryPlus",
		"ArrayType",
		"ReferenceType",
		"Undefined",
		"Unreachable",
		"ValueInteger",
		"ValueFloat",
		"ValueString",
		"ValueVoid",
		"DiscardVoid",
		"CheckTopVoid",
		"CheckWriteCtxVoid",
	};

	u8 ordinal = static_cast<u8>(op);

	if (ordinal >= array_count(TAG_NAMES))
		ordinal = 0;

	return TAG_NAMES[ordinal];
}
