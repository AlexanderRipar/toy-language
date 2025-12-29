#include "diag.hpp"

#include "../infra/container/reserved_vec.hpp"

static void print_opcodes_impl(diag::PrintContext* ctx, IdentifierPool* identifiers, OpcodePool* opcodes, const Opcode* code, bool follow_refs) noexcept;



template<typename T>
static const Opcode* code_attach(const Opcode* code, T* out_attach)
{
	memcpy(out_attach, code, sizeof(T));

	return code + sizeof(T);
}



static const Opcode* follow_ref_impl(diag::PrintContext* ctx, IdentifierPool* identifiers, OpcodePool* opcodes, const Opcode* code) noexcept
{
	const u8 bits = static_cast<u8>(*code);

	const Opcode op = static_cast<Opcode>(bits & 0x7F);

	code += 1;

	switch (op)
	{
	case Opcode::INVALID:
	case Opcode::EndCode:
	{
		return nullptr;
	}

	case Opcode::SetWriteCtx:
	case Opcode::ScopeEnd:
	case Opcode::PopClosure:
	case Opcode::ExecArgs:
	case Opcode::Call:
	case Opcode::Return:
	case Opcode::AddressOf:
	case Opcode::Dereference:
	case Opcode::Index:
	case Opcode::BitNot:
	case Opcode::LogicalAnd:
	case Opcode::LogicalOr:
	case Opcode::LogicalNot:
	case Opcode::Negate:
	case Opcode::UnaryPlus:
	case Opcode::ArrayType:
	case Opcode::Undefined:
	case Opcode::Unreachable:
	case Opcode::ValueVoid:
	case Opcode::DiscardVoid:
	case Opcode::CheckTopVoid:
	case Opcode::CheckWriteCtxVoid:
	{
		return code;
	}

	case Opcode::ScopeBegin:
	{
		return code + sizeof(u16);
	}

	case Opcode::ScopeAllocTyped:
	case Opcode::ScopeAllocUntyped:
	{
		return code + sizeof(bool);
	}

	case Opcode::FileGlobalAllocTyped:
	case Opcode::FileGlobalAllocUntyped:
	{
		return code + sizeof(bool) + sizeof(GlobalFileIndex) + sizeof(u16);
	}

	case Opcode::LoadScope:
	{
		return code + sizeof(u16) + sizeof(u16);
	}

	case Opcode::LoadGlobal:
	{
		return code + sizeof(GlobalFileIndex) + sizeof(u16);
	}

	case Opcode::LoadMember:
	{
		return code + sizeof(IdentifierId);
	}

	case Opcode::LoadClosure:
	{
		return code + sizeof(u16);
	}

	case Opcode::LoadBuiltin:
	case Opcode::ExecBuiltin:
	{
		return code + sizeof(Builtin);
	}

	case Opcode::Signature:
	{
		code += sizeof(OpcodeSignatureFlags);

		u8 parameter_count;

		code = code_attach(code, &parameter_count);

		return code + sizeof(u8) + parameter_count * (sizeof(IdentifierId) + sizeof(OpcodeSignaturePerParameterFlags));
	}

	case Opcode::DynSignature:
	{
		OpcodeSignatureFlags signature_flags;

		code = code_attach(code, &signature_flags);

		u8 parameter_count;

		code = code_attach(code, &parameter_count);

		code += sizeof(u8) + sizeof(u16);

		if (signature_flags.has_templated_return_type)
		{
			OpcodeId return_completion;

			code = code_attach(code, &return_completion);

			const Opcode* const return_completion_code = opcode_from_id(opcodes, return_completion);

			print_opcodes_impl(ctx, identifiers, opcodes, return_completion_code, true);
		}

		for (u8 i = 0; i != parameter_count; ++i)
		{
			code += sizeof(IdentifierId);

			OpcodeSignaturePerParameterFlags parameter_flags;

			code = code_attach(code, &parameter_flags);

			if (parameter_flags.is_templated)
			{
				OpcodeId parameter_completion;

				code = code_attach(code, &parameter_completion);

				const Opcode* const parameter_completion_code = opcode_from_id(opcodes, parameter_completion);

				print_opcodes_impl(ctx, identifiers, opcodes, parameter_completion_code, true);
			}
		}

		return code;
	}

	case Opcode::BindBody:
	{
		OpcodeId body;

		code = code_attach(code, &body);

		const Opcode* const body_code = opcode_from_id(opcodes, body);

		print_opcodes_impl(ctx, identifiers, opcodes, body_code, true);

		return code;
	}

	case Opcode::BindBodyWithClosure:
	{
		OpcodeId body;

		code = code_attach(code, &body);

		const Opcode* const body_code = opcode_from_id(opcodes, body);

		print_opcodes_impl(ctx, identifiers, opcodes, body_code, true);

		return code + sizeof(u16);
	}

	case Opcode::PrepareArgs:
	{
		u8 argument_count;

		code = code_attach(code, &argument_count);

		code += sizeof(IdentifierId) * argument_count;

		const Opcode* const argument_callbacks = code;
		code += sizeof(IdentifierId) * argument_count;

		for (u8 i = 0; i != argument_count; ++i)
		{
			OpcodeId argument_callback;
			memcpy(&argument_callback, argument_callbacks + i * sizeof(OpcodeId), sizeof(OpcodeId));

			const Opcode* const argument_callback_code = opcode_from_id(opcodes, argument_callback);

			print_opcodes_impl(ctx, identifiers, opcodes, argument_callback_code, true);
		}

		return code;
	}

	case Opcode::CompleteParamTypedNoDefault:
	case Opcode::CompleteParamTypedWithDefault:
	case Opcode::CompleteParamUntyped:
	{
		return code + sizeof(u8);
	}

	case Opcode::ArrayPreInit:
	{
		u16 index_count;

		code = code_attach(code, &index_count);

		return code + sizeof(u16) + index_count * sizeof(u16);
	}

	case Opcode::ArrayPostInit:
	{
		code += sizeof(u16);

		u16 index_count;

		code = code_attach(code, &index_count);

		return code + sizeof(u16) + index_count * sizeof(u16);
	}

	case Opcode::CompositePreInit:
	{
		u16 names_count;

		code = code_attach(code, &names_count);

		return code + sizeof(u16) + names_count * (sizeof(IdentifierId) + sizeof(u16));
	}

	case Opcode::CompositePostInit:
	{
		u16 member_count;

		code = code_attach(code, &member_count);

		return code + member_count * sizeof(IdentifierId);
	}

	case Opcode::If:
	{
		OpcodeId consequent;

		code = code_attach(code, &consequent);

		const Opcode* const consequent_code = opcode_from_id(opcodes, consequent);

		print_opcodes_impl(ctx, identifiers, opcodes, consequent_code, true);

		return code;
	}

	case Opcode::IfElse:
	{
		OpcodeId consequent;

		code = code_attach(code, &consequent);

		OpcodeId alternative;

		code = code_attach(code, &alternative);

		const Opcode* const consequent_code = opcode_from_id(opcodes, consequent);

		print_opcodes_impl(ctx, identifiers, opcodes, consequent_code, true);

		const Opcode* const alternative_code = opcode_from_id(opcodes, alternative);

		print_opcodes_impl(ctx, identifiers, opcodes, alternative_code, true);

		return code;
	}

	case Opcode::Loop:
	{
		OpcodeId condition;

		code = code_attach(code, &condition);

		OpcodeId body;

		code = code_attach(code, &body);

		const Opcode* const condition_code = opcode_from_id(opcodes, condition);

		print_opcodes_impl(ctx, identifiers, opcodes, condition_code, true);

		const Opcode* const body_code = opcode_from_id(opcodes, body);

		print_opcodes_impl(ctx, identifiers, opcodes, body_code, true);

		return code;
	}

	case Opcode::LoopFinally:
	{
		OpcodeId condition;

		code = code_attach(code, &condition);

		OpcodeId body;

		code = code_attach(code, &body);

		OpcodeId finally;

		code = code_attach(code, &finally);

		const Opcode* const condition_code = opcode_from_id(opcodes, condition);

		print_opcodes_impl(ctx, identifiers, opcodes, condition_code, true);

		const Opcode* const body_code = opcode_from_id(opcodes, body);

		print_opcodes_impl(ctx, identifiers, opcodes, body_code, true);

		const Opcode* const finally_code = opcode_from_id(opcodes, finally);

		print_opcodes_impl(ctx, identifiers, opcodes, finally_code, true);

		return code;
	}

	case Opcode::Slice:
	{
		return code + sizeof(OpcodeSliceKind);
	}

	case Opcode::BinaryArithmeticOp:
	{
		return code + sizeof(OpcodeBinaryArithmeticOpKind);
	}

	case Opcode::Shift:
	{
		return code + sizeof(OpcodeShiftKind);
	}

	case Opcode::BinaryBitwiseOp:
	{
		return code + sizeof(OpcodeBinaryBitwiseOpKind);
	}

	case Opcode::Compare:
	{
		return code + sizeof(OpcodeCompareKind);
	}

	case Opcode::ReferenceType:
	{
		return code + sizeof(OpcodeReferenceTypeFlags);
	}

	case Opcode::ValueInteger:
	{
		return code + sizeof(CompIntegerValue);
	}

	case Opcode::ValueFloat:
	{
		return code + sizeof(CompFloatValue);
	}

	case Opcode::ValueString:
	{
		return code + sizeof(ForeverValueId);
	}

	case Opcode::Switch:
		TODO("Implement");
	}

	ASSERT_UNREACHABLE;
}

static void follow_refs_impl(diag::PrintContext* ctx, IdentifierPool* identifiers, OpcodePool* opcodes, const Opcode* code) noexcept
{
	do
	{
		code = follow_ref_impl(ctx, identifiers, opcodes, code);
	}
	while (code != nullptr);
}

static const Opcode* print_opcode_impl(diag::PrintContext* ctx, IdentifierPool* identifiers, OpcodePool* opcodes, const Opcode* code) noexcept
{
	const u8 bits = static_cast<u8>(*code);

	const bool consumes_write_ctx = (bits & 0x80) != 0;

	const Opcode op = static_cast<Opcode>(bits & 0x7F);

	const char8* op_name = tag_name(op);

	const OpcodeId code_id = id_from_opcode(opcodes, code);

	diag::buf_printf(ctx, "%6u  %c %s",
		static_cast<u32>(code_id),
		consumes_write_ctx ? '@' : ' ',
		op_name
	);

	code += 1;

	switch (op)
	{
	case Opcode::INVALID:
	case Opcode::EndCode:
	{
		return nullptr;
	}

	case Opcode::SetWriteCtx:
	case Opcode::ScopeEnd:
	case Opcode::PopClosure:
	case Opcode::ExecArgs:
	case Opcode::Call:
	case Opcode::Return:
	case Opcode::AddressOf:
	case Opcode::Dereference:
	case Opcode::Index:
	case Opcode::BitNot:
	case Opcode::LogicalAnd:
	case Opcode::LogicalOr:
	case Opcode::LogicalNot:
	case Opcode::Negate:
	case Opcode::UnaryPlus:
	case Opcode::ArrayType:
	case Opcode::Undefined:
	case Opcode::Unreachable:
	case Opcode::ValueVoid:
	case Opcode::DiscardVoid:
	case Opcode::CheckTopVoid:
	case Opcode::CheckWriteCtxVoid:
	{
		return code;
	}

	case Opcode::ScopeBegin:
	{
		u16 member_count;

		code = code_attach(code, &member_count);

		diag::buf_printf(ctx, " member_count=%u", member_count);

		return code;
	}

	case Opcode::ScopeAllocTyped:
	case Opcode::ScopeAllocUntyped:
	{
		bool is_mut;

		code = code_attach(code, &is_mut);

		diag::buf_printf(ctx, " is_mut=%s", is_mut ? "true" : "false");

		return code;
	}

	case Opcode::FileGlobalAllocTyped:
	case Opcode::FileGlobalAllocUntyped:
	{
		bool is_mut;

		code = code_attach(code, &is_mut);

		GlobalFileIndex index;

		code = code_attach(code, &index);

		u16 rank;

		code = code_attach(code, &rank);

		diag::buf_printf(ctx, " is_mut=%s file_index=%u rank=%u", is_mut ? "true" : "false", static_cast<u32>(index), rank);

		return code;
	}

	case Opcode::LoadScope:
	{
		u16 out;

		code = code_attach(code, &out);

		u16 rank;

		code = code_attach(code, &rank);

		diag::buf_printf(ctx, " out=%u rank=%u", out, rank);

		return code;
	}

	case Opcode::LoadGlobal:
	{
		GlobalFileIndex index;

		code = code_attach(code, &index);

		u16 rank;

		code = code_attach(code, &rank);

		diag::buf_printf(ctx, " file_index=%u rank=%u", static_cast<u32>(index), rank);

		return code;
	}

	case Opcode::LoadMember:
	{
		IdentifierId name;

		code = code_attach(code, &name);

		const Range<char8> name_str = identifier_name_from_id(identifiers, name);

		diag::buf_printf(ctx, " name=IdentifierId<%u> (%.*s)", static_cast<u32>(name), static_cast<s32>(name_str.count()), name_str.begin());

		return code;
	}

	case Opcode::LoadClosure:
	{
		u16 rank;

		code = code_attach(code, &rank);

		diag::buf_printf(ctx, " rank=%u", rank);

		return code;
	}

	case Opcode::LoadBuiltin:
	case Opcode::ExecBuiltin:
	{
		Builtin builtin;

		code = code_attach(code, &builtin);

		diag::buf_printf(ctx, " %s", tag_name(builtin));

		return code;
	}

	case Opcode::Signature:
	{
		OpcodeSignatureFlags signature_flags;

		code = code_attach(code, &signature_flags);

		u8 parameter_count;

		code = code_attach(code, &parameter_count);

		u8 value_count;

		code = code_attach(code, &value_count);

		diag::buf_printf(ctx, " %s param_count=%u value_count=%u",
			signature_flags.is_func ? "func" : "proc",
			parameter_count,
			value_count
		);

		for (u8 i = 0; i != parameter_count; ++i)
		{
			IdentifierId parameter_name;

			code = code_attach(code, &parameter_name);

			OpcodeSignaturePerParameterFlags parameter_flags;

			code = code_attach(code, &parameter_flags);

			const Range<char8> parameter_name_str = identifier_name_from_id(identifiers, parameter_name);
	
			diag::buf_printf(ctx, "\n     -        %2u: mut=%s eval=%s type=%s default=%s name=IdentifierId<%u> (%.*s) ",
				i,
				parameter_flags.is_mut ? "true" : "false",
				parameter_flags.is_eval ? "true" : "false",
				parameter_flags.has_type ? "true" : "false",
				parameter_flags.has_default ? "true" : "false",
				static_cast<u32>(parameter_name),
				static_cast<s32>(parameter_name_str.count()), parameter_name_str.begin()
			);
		}

		return code;
	}

	case Opcode::DynSignature:
	{
		OpcodeSignatureFlags signature_flags;

		code = code_attach(code, &signature_flags);

		u8 parameter_count;

		code = code_attach(code, &parameter_count);

		u8 value_count;

		code = code_attach(code, &value_count);

		u16 closed_over_value_count;

		code = code_attach(code, &closed_over_value_count);

		diag::buf_printf(ctx, " %s param_count=%u value_count=%u closed_count=%u",
			signature_flags.is_func ? "func" : "proc",
			parameter_count,
			value_count,
			closed_over_value_count
		);

		if (signature_flags.has_templated_return_type)
		{
			OpcodeId return_completion;

			code = code_attach(code, &return_completion);

			diag::buf_printf(ctx, " return_completion=OpcodeId<%u>", static_cast<u32>(return_completion));
		}

		for (u8 i = 0; i != parameter_count; ++i)
		{
			IdentifierId parameter_name;

			code = code_attach(code, &parameter_name);

			OpcodeSignaturePerParameterFlags parameter_flags;

			code = code_attach(code, &parameter_flags);

			const Range<char8> parameter_name_str = identifier_name_from_id(identifiers, parameter_name);
	
			diag::buf_printf(ctx, "\n     -        %2u: mut=%s eval=%s type=%s default=%s name=IdentifierId<%u> (%.*s) ",
				i,
				parameter_flags.is_mut ? "true" : "false",
				parameter_flags.is_eval ? "true" : "false",
				parameter_flags.has_type ? "true" : "false",
				parameter_flags.has_default ? "true" : "false",
				static_cast<u32>(parameter_name),
				static_cast<s32>(parameter_name_str.count()), parameter_name_str.begin()
			);

			if (parameter_flags.is_templated)
			{
				OpcodeId parameter_completion;

				code = code_attach(code, &parameter_completion);

				diag::buf_printf(ctx, " completion=OpcodeId<%u>", static_cast<u32>(parameter_completion));
			}
		}

		return code;
	}

	case Opcode::BindBody:
	{
		OpcodeId body_id;

		code = code_attach(code, &body_id);

		diag::buf_printf(ctx, " body=OpcodeId<%u>", static_cast<u32>(body_id));

		return code;
	}

	case Opcode::BindBodyWithClosure:
	{
		OpcodeId body_id;

		code = code_attach(code, &body_id);

		u16 closed_over_value_count;

		code = code_attach(code, &closed_over_value_count);

		diag::buf_printf(ctx, " body=OpcodeId<%u> closed_value_count=%u", static_cast<u32>(body_id), closed_over_value_count);

		return code;
	}

	case Opcode::PrepareArgs:
	{
		u8 argument_count;

		code = code_attach(code, &argument_count);

		diag::buf_printf(ctx, " count=%u", argument_count);

		const Opcode* const argument_names = code;
		code += sizeof(IdentifierId) * argument_count;

		const Opcode* const argument_callbacks = code;
		code += sizeof(IdentifierId) * argument_count;

		for (u8 i = 0; i != argument_count; ++i)
		{
			IdentifierId argument_name;
			memcpy(&argument_name, argument_names + i * sizeof(IdentifierId), sizeof(IdentifierId));

			OpcodeId argument_callback;
			memcpy(&argument_callback, argument_callbacks + i * sizeof(OpcodeId), sizeof(OpcodeId));

			const Range<char8> argument_name_str = argument_name == IdentifierId::INVALID
				? range::from_literal_string("<unnamed>")
				: identifier_name_from_id(identifiers, argument_name);

			diag::buf_printf(ctx, "\n     -        %2u: callback=OpcodeId<%u> name=IdentifierId<%u> (%.*s)",
				i,
				static_cast<u32>(argument_callback),
				static_cast<u32>(argument_name),
				static_cast<s32>(argument_name_str.count()), argument_name_str.begin()
			);
		}

		return code;
	}

	case Opcode::CompleteParamTypedNoDefault:
	case Opcode::CompleteParamTypedWithDefault:
	case Opcode::CompleteParamUntyped:
	{
		u8 rank;

		code = code_attach(code, &rank);

		diag::buf_printf(ctx, " rank=%u", rank);

		return code;
	}

	case Opcode::ArrayPreInit:
	{
		u16 index_count;

		code = code_attach(code, &index_count);

		u16 leading_element_count;

		code = code_attach(code, &leading_element_count);

		diag::buf_printf(ctx, " index_count=%u leading_elem_count=%u", index_count, leading_element_count);

		for (u16 i = 0; i != index_count; ++i)
		{
			u16 following_element_count;

			code = code_attach(code, &following_element_count);

			diag::buf_printf(ctx, "\n     -              following_elem_count=%u", following_element_count);
		}

		return code;
	}

	case Opcode::ArrayPostInit:
	{
		u16 total_element_count;

		code = code_attach(code, &total_element_count);

		u16 index_count;

		code = code_attach(code, &index_count);

		u16 leading_element_count;

		code = code_attach(code, &leading_element_count);

		diag::buf_printf(ctx, " index_count=%u leading_elem_count=%u total_elem_count=%u",
			index_count,
			leading_element_count,
			total_element_count
		);

		for (u16 i = 0; i != index_count; ++i)
		{
			u16 following_element_count;

			code = code_attach(code, &following_element_count);

			diag::buf_printf(ctx, "\n     -        following_elem_count=%u", following_element_count);
		}

		return code;
	}

	case Opcode::CompositePreInit:
	{
		u16 names_count;

		code = code_attach(code, &names_count);

		u16 leading_initializer_count;

		code = code_attach(code, &leading_initializer_count);

		diag::buf_printf(ctx, " names_count=%u leading_elem_count=%u",
			names_count,
			leading_initializer_count
		);

		for (u16 i = 0; i != names_count; ++i)
		{
			IdentifierId name;

			code = code_attach(code, &name);

			u16 following_initializer_count;

			code = code_attach(code, &following_initializer_count);

			const Range<char8> name_str = identifier_name_from_id(identifiers, name);

			diag::buf_printf(ctx, "\n     -        following_elem_count=%u name=IdentifierId<%u> (%.*s)",
				following_initializer_count,
				static_cast<u32>(name),
				static_cast<s32>(name_str.count()), name_str.begin()
			);
		}

		return code;
	}

	case Opcode::CompositePostInit:
	{
		u16 member_count;

		code = code_attach(code, &member_count);

		diag::buf_printf(ctx, " total_elem_count=%u",
			member_count
		);

		for (u16 i = 0; i != member_count; ++i)
		{
			IdentifierId name;

			code = code_attach(code, &name);

			const Range<char8> name_str = identifier_name_from_id(identifiers, name);

			diag::buf_printf(ctx, "\n     -        name=IdentifierId<%u> (%.*s)",
				static_cast<u32>(name),
				static_cast<s32>(name_str.count()), name_str.begin()
			);
		}

		return code;
	}

	case Opcode::If:
	{
		OpcodeId consequent;

		code = code_attach(code, &consequent);

		diag::buf_printf(ctx, "consequent=OpcodeId<%u>",
			static_cast<u32>(consequent)
		);

		return code;
	}

	case Opcode::IfElse:
	{
		OpcodeId consequent;

		code = code_attach(code, &consequent);

		OpcodeId alternative;

		code = code_attach(code, &alternative);

		diag::buf_printf(ctx, "consequent=OpcodeId<%u> alternative=OpcodeId<%u>",
			static_cast<u32>(consequent),
			static_cast<u32>(alternative)
		);

		return code;
	}

	case Opcode::Loop:
	{
		OpcodeId condition_id;

		code = code_attach(code, &condition_id);

		OpcodeId body_id;

		code = code_attach(code, &body_id);

		diag::buf_printf(ctx, "cond=OpcodeId<%u> body=OpcodeId<%u>",
			static_cast<u32>(condition_id),
			static_cast<u32>(body_id)
		);

		return code;
	}

	case Opcode::LoopFinally:
	{
		OpcodeId condition_id;

		code = code_attach(code, &condition_id);

		OpcodeId body_id;

		code = code_attach(code, &body_id);

		OpcodeId finally_id;

		code = code_attach(code, &finally_id);

		diag::buf_printf(ctx, "cond=OpcodeId<%u> body=OpcodeId<%u> finally=OpcodeId<%u>",
			static_cast<u32>(condition_id),
			static_cast<u32>(body_id),
			static_cast<u32>(finally_id)
		);

		return code;
	}

	case Opcode::Slice:
	{
		OpcodeSliceKind kind;

		code = code_attach(code, &kind);

		const char8* kind_name;

		if (kind == OpcodeSliceKind::NoBounds)
			kind_name = "no_bounds";
		else if (kind == OpcodeSliceKind::BeginBound)
			kind_name = "begin_bound";
		else if (kind == OpcodeSliceKind::EndBound)
			kind_name = "end_bound";
		else if (kind == OpcodeSliceKind::BothBounds)
			kind_name = "both_bound";
		else
			ASSERT_UNREACHABLE;

		diag::buf_printf(ctx, " %s", kind_name);

		return code;
	}

	case Opcode::BinaryArithmeticOp:
	{
		OpcodeBinaryArithmeticOpKind kind;

		code = code_attach(code, &kind);

		const char8* kind_name;

		if (kind == OpcodeBinaryArithmeticOpKind::Add)
			kind_name = "add";
		else if (kind == OpcodeBinaryArithmeticOpKind::Sub)
			kind_name = "sub";
		else if (kind == OpcodeBinaryArithmeticOpKind::Mul)
			kind_name = "mul";
		else if (kind == OpcodeBinaryArithmeticOpKind::Div)
			kind_name = "div";
		else if (kind == OpcodeBinaryArithmeticOpKind::AddTC)
			kind_name = "add_tc";
		else if (kind == OpcodeBinaryArithmeticOpKind::SubTC)
			kind_name = "sub_tc";
		else if (kind == OpcodeBinaryArithmeticOpKind::MulTC)
			kind_name = "mul_tc";
		else if (kind == OpcodeBinaryArithmeticOpKind::Mod)
			kind_name = "mod";
		else
			ASSERT_UNREACHABLE;

		diag::buf_printf(ctx, " %s", kind_name);

		return code;
	}

	case Opcode::Shift:
	{
		OpcodeShiftKind kind;

		code = code_attach(code, &kind);

		const char8* kind_name;

		if (kind == OpcodeShiftKind::Left)
			kind_name = "left";
		else if (kind == OpcodeShiftKind::Right)
			kind_name = "right";
		else
			ASSERT_UNREACHABLE;

		diag::buf_printf(ctx, " %s", kind_name);

		return code;
	}

	case Opcode::BinaryBitwiseOp:
	{
		OpcodeBinaryBitwiseOpKind kind;

		code = code_attach(code, &kind);

		const char8* kind_name;

		if (kind == OpcodeBinaryBitwiseOpKind::And)
			kind_name = "and";
		else if (kind == OpcodeBinaryBitwiseOpKind::Or)
			kind_name = "or";
		else if (kind == OpcodeBinaryBitwiseOpKind::Xor)
			kind_name = "xor";
		else
			ASSERT_UNREACHABLE;

		diag::buf_printf(ctx, " %s", kind_name);
		return code;
	}

	case Opcode::Compare:
	{
		OpcodeCompareKind kind;

		code = code_attach(code, &kind);

		const char8* kind_name;

		if (kind == OpcodeCompareKind::LessThan)
			kind_name = "less_than";
		else if (kind == OpcodeCompareKind::GreaterThan)
			kind_name = "greater_than";
		else if (kind == OpcodeCompareKind::LessThanOrEqual)
			kind_name = "less_or_equal";
		else if (kind == OpcodeCompareKind::GreaterThanOrEqual)
			kind_name = "greater_or_equal";
		else if (kind == OpcodeCompareKind::NotEqual)
			kind_name = "not_equal";
		else if (kind == OpcodeCompareKind::Equal)
			kind_name = "equal";
		else
			ASSERT_UNREACHABLE;

		diag::buf_printf(ctx, " %s", kind_name);

		return code;
	}

	case Opcode::ReferenceType:
	{
		OpcodeReferenceTypeFlags flags;

		code = code_attach(code, &flags);

		const TypeTag tag = static_cast<TypeTag>(flags.tag);

		if (tag == TypeTag::Ptr)
		{
			diag::buf_printf(ctx, " Ptr is_mut=%s is_multi=%s is_opt=%s",
				flags.is_mut ? "true" : "false",
				flags.is_multi ? "true" : "false",
				flags.is_opt ? "true" : "false"
			);
		}
		else if (tag == TypeTag::Slice)
		{
			diag::buf_printf(ctx, " Slice is_mut=%s",
				flags.is_mut ? "true" : "false"
			);
		}
		else
		{
			ASSERT_UNREACHABLE;
		}

		return code;
	}

	case Opcode::ValueInteger:
	{
		CompIntegerValue value;

		code = code_attach(code, &value);

		bool is_negative = comp_integer_compare(value, comp_integer_from_u64(0)) == StrongCompareOrdering::LessThan;

		if (is_negative)
			value = comp_integer_neg(value);

		u64 u64_value;

		if (u64_from_comp_integer(value, 64, &u64_value))
			diag::buf_printf(ctx, " CompIntegerValue<%s%" PRIu64 ">", is_negative ? "-" : "", u64_value);
		else
			diag::buf_printf(ctx, " CompIntegerValue<%sBIG>", is_negative ? "-" : "");

		return code;
	}

	case Opcode::ValueFloat:
	{
		CompFloatValue value;

		code = code_attach(code, &value);

		diag::buf_printf(ctx, " CompFloatValue<%d>", f64_from_comp_float(value));

		return code;
	}

	case Opcode::ValueString:
	{
		ForeverValueId value;

		code = code_attach(code, &value);

		diag::buf_printf(ctx, " ForeverValueId<%u>", static_cast<u32>(value));

		return code;
	}

	case Opcode::Switch:
		TODO("Implement");
	}

	ASSERT_UNREACHABLE;
}

static void print_opcodes_impl(diag::PrintContext* ctx, IdentifierPool* identifiers, OpcodePool* opcodes, const Opcode* code, bool follow_refs) noexcept
{
	const Opcode* const code_begin = code;

	do
	{
		code = print_opcode_impl(ctx, identifiers, opcodes, code);

		buf_printf(ctx, "\n");
	}
	while (code != nullptr);

	buf_printf(ctx, "\n");

	if (follow_refs)
		follow_refs_impl(ctx, identifiers, opcodes, code_begin);
}



void diag::print_opcodes(minos::FileHandle out, IdentifierPool* identifiers, OpcodePool* opcodes, const Opcode* code, bool follow_refs) noexcept
{
	PrintContext ctx;
	ctx.curr = ctx.buf;
	ctx.file = out;

	print_opcodes_impl(&ctx, identifiers, opcodes, code, follow_refs);

	buf_flush(&ctx);
}
