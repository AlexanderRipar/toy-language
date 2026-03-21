#include "diag.hpp"

#include "../infra/types.hpp"
#include "../infra/assert.hpp"
#include "../infra/panic.hpp"
#include "../infra/range.hpp"
#include "../infra/container/reserved_vec.hpp"

struct PrintResult
{
	const Opcode* next;

	s64 written;
};

static s64 print_opcodes_impl(PrintSink sink, CoreData* core, const Opcode* code, bool follow_refs) noexcept;



template<typename T>
static const Opcode* code_attach(const Opcode* code, T* out_attach)
{
	memcpy(out_attach, code, sizeof(T));

	return code + sizeof(T);
}



static PrintResult follow_ref_impl(PrintSink sink, CoreData* core, const Opcode* code) noexcept
{
	const u8 bits = static_cast<u8>(*code);

	const Opcode op = static_cast<Opcode>(bits & 0x7F);

	code += 1;

	switch (op)
	{
	case Opcode::INVALID:
	case Opcode::EndCode:
	case Opcode::Return:
	case Opcode::FileGlobalAllocComplete:
	case Opcode::FileGlobalAllocUntyped:
	case Opcode::ImplMemberAllocComplete:
	{
		return PrintResult{ nullptr, 0 };
	}

	case Opcode::SetWriteCtx:
	case Opcode::ScopeEnd:
	case Opcode::ScopeEndPreserveTop:
	case Opcode::FileGlobalAllocTyped:
	case Opcode::PopClosure:
	case Opcode::ExecArgs:
	case Opcode::Call:
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
	case Opcode::ImplSetSelf:
	case Opcode::ImplTraitCall:
	case Opcode::ImplMemberAllocExplicitType:
	case Opcode::ImplMemberAllocImplicitType:
	case Opcode::GetSelf:
	{
		return PrintResult{ code, 0 };
	}

	case Opcode::ScopeBegin:
	{
		return PrintResult{ code + sizeof(u16), 0 };
	}

	case Opcode::ScopeAllocTyped:
	case Opcode::ScopeAllocUntyped:
	{
		return PrintResult{ code + sizeof(bool), 0 };
	}

	case Opcode::FileGlobalAllocPrepare:
	{
		return PrintResult{ code + sizeof(bool) + sizeof(GlobalCompositeIndex) + sizeof(u16), 0 };
	}

	case Opcode::LoadScope:
	{
		return PrintResult{ code + sizeof(u16) + sizeof(u16), 0 };
	}

	case Opcode::LoadGlobal:
	{
		return PrintResult{ code + sizeof(GlobalCompositeIndex) + sizeof(u16), 0 };
	}

	case Opcode::LoadMember:
	{
		return PrintResult{ code + sizeof(IdentifierId), 0 };
	}

	case Opcode::LoadClosure:
	{
		return PrintResult{ code + sizeof(u16), 0 };
	}

	case Opcode::LoadBuiltin:
	case Opcode::ExecBuiltin:
	{
		return PrintResult{ code + sizeof(Builtin), 0 };
	}

	case Opcode::Signature:
	{
		code += sizeof(OpcodeSignatureFlags);

		u8 parameter_count;

		code = code_attach(code, &parameter_count);

		return PrintResult{ code + sizeof(u8) + parameter_count * (sizeof(IdentifierId) + sizeof(OpcodeSignaturePerParameterFlags)), 0 };
	}

	case Opcode::DynSignature:
	{
		s64 total_written = 0;

		OpcodeSignatureFlags signature_flags;

		code = code_attach(code, &signature_flags);

		u8 parameter_count;

		code = code_attach(code, &parameter_count);

		code += sizeof(u8) + sizeof(u16);

		if (signature_flags.has_templated_return_type)
		{
			OpcodeId return_completion;

			code = code_attach(code, &return_completion);

			const Opcode* const return_completion_code = opcode_from_id(core, return_completion);

			const s64 written = print_opcodes_impl(sink, core, return_completion_code, true);

			if (written < 0)
				return PrintResult{ nullptr, -1 };

			total_written += written;
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

				const Opcode* const parameter_completion_code = opcode_from_id(core, parameter_completion);

				const s64 written = print_opcodes_impl(sink, core, parameter_completion_code, true);

				if (written < 0)
					return PrintResult{ nullptr, -1 };

				total_written += written;
			}
		}

		return PrintResult{ code, total_written };
	}

	case Opcode::BindBody:
	{
		OpcodeId body;

		code = code_attach(code, &body);

		const Opcode* const body_code = opcode_from_id(core, body);

		const s64 written = print_opcodes_impl(sink, core, body_code, true);

		if (written < 0)
			return PrintResult{ nullptr, -1 };

		return PrintResult{ code, written };
	}

	case Opcode::PrepareArgs:
	{
		s64 total_written = 0;

		u8 argument_count;

		code = code_attach(code, &argument_count);

		code += sizeof(IdentifierId) * argument_count;

		const Opcode* const argument_callbacks = code;
		code += sizeof(IdentifierId) * argument_count;

		for (u8 i = 0; i != argument_count; ++i)
		{
			OpcodeId argument_callback;
			memcpy(&argument_callback, argument_callbacks + i * sizeof(OpcodeId), sizeof(OpcodeId));

			const Opcode* const argument_callback_code = opcode_from_id(core, argument_callback);

			const s64 written = print_opcodes_impl(sink, core, argument_callback_code, true);

			if (written < 0)
				return PrintResult{ nullptr, -1 };

			total_written += written;
		}

		return PrintResult{ code, total_written };
	}

	case Opcode::CompleteParamTypedNoDefault:
	case Opcode::CompleteParamTypedWithDefault:
	case Opcode::CompleteParamUntyped:
	{
		return PrintResult{ code + sizeof(u8), 0 };
	}

	case Opcode::ArrayPreInit:
	{
		u16 index_count;

		code = code_attach(code, &index_count);

		return PrintResult{ code + sizeof(u16) + index_count * sizeof(u16), 0 };
	}

	case Opcode::ArrayPostInit:
	{
		code += sizeof(u16);

		u16 index_count;

		code = code_attach(code, &index_count);

		return PrintResult{ code + sizeof(u16) + index_count * sizeof(u16), 0 };
	}

	case Opcode::CompositePreInit:
	{
		u16 names_count;

		code = code_attach(code, &names_count);

		return PrintResult{ code + sizeof(u16) + names_count * (sizeof(IdentifierId) + sizeof(u16)), 0 };
	}

	case Opcode::CompositePostInit:
	{
		u16 member_count;

		code = code_attach(code, &member_count);

		return PrintResult{ code + member_count * sizeof(IdentifierId), 0 };
	}

	case Opcode::If:
	{
		OpcodeId consequent;

		code = code_attach(code, &consequent);

		const Opcode* const consequent_code = opcode_from_id(core, consequent);

		const s64 written = print_opcodes_impl(sink, core, consequent_code, true);

		if (written < 0)
			return PrintResult{ nullptr, -1 };

		return PrintResult{ code, written };
	}

	case Opcode::IfElse:
	{
		OpcodeId consequent;

		code = code_attach(code, &consequent);

		OpcodeId alternative;

		code = code_attach(code, &alternative);

		const Opcode* const consequent_code = opcode_from_id(core, consequent);

		const s64 consequent_written = print_opcodes_impl(sink, core, consequent_code, true);

		if (consequent_written < 0)
			return PrintResult{ nullptr, -1 };

		const Opcode* const alternative_code = opcode_from_id(core, alternative);

		const s64 alternative_written = print_opcodes_impl(sink, core, alternative_code, true);

		if (alternative_written < 0)
			return PrintResult{ nullptr, -1 };

		return PrintResult{ code, consequent_written + alternative_written };
	}

	case Opcode::Loop:
	{
		OpcodeId condition;

		code = code_attach(code, &condition);

		OpcodeId body;

		code = code_attach(code, &body);

		const Opcode* const condition_code = opcode_from_id(core, condition);

		const s64 condition_written = print_opcodes_impl(sink, core, condition_code, true);

		if (condition_written < 0)
			return PrintResult{ nullptr, -1 };

		const Opcode* const body_code = opcode_from_id(core, body);

		const s64 body_written = print_opcodes_impl(sink, core, body_code, true);

		if (body_written < 0)
			return PrintResult{ nullptr, -1 };

		return PrintResult{ code, condition_written + body_written };
	}

	case Opcode::LoopFinally:
	{
		OpcodeId condition;

		code = code_attach(code, &condition);

		OpcodeId body;

		code = code_attach(code, &body);

		OpcodeId finally;

		code = code_attach(code, &finally);

		const Opcode* const condition_code = opcode_from_id(core, condition);

		const s64 condition_written = print_opcodes_impl(sink, core, condition_code, true);

		if (condition_written < 0)
			return PrintResult{ nullptr, -1 };

		const Opcode* const body_code = opcode_from_id(core, body);

		const s64 body_written = print_opcodes_impl(sink, core, body_code, true);

		if (body_written < 0)
			return PrintResult{ nullptr, -1 };

		const Opcode* const finally_code = opcode_from_id(core, finally);

		const s64 finally_written = print_opcodes_impl(sink, core, finally_code, true);

		if (finally_written < 0)
			return PrintResult{ nullptr, -1 };

		return PrintResult{ code, condition_written + body_written + finally_written };
	}

	case Opcode::Slice:
	{
		return PrintResult{ code + sizeof(OpcodeSliceKind), 0 };
	}

	case Opcode::BinaryArithmeticOp:
	{
		return PrintResult{ code + sizeof(OpcodeBinaryArithmeticOpKind), 0 };
	}

	case Opcode::Shift:
	{
		return PrintResult{ code + sizeof(OpcodeShiftKind), 0 };
	}

	case Opcode::BinaryBitwiseOp:
	{
		return PrintResult{ code + sizeof(OpcodeBinaryBitwiseOpKind), 0 };
	}

	case Opcode::Compare:
	{
		return PrintResult{ code + sizeof(OpcodeCompareKind), 0 };
	}

	case Opcode::ReferenceType:
	{
		return PrintResult{ code + sizeof(OpcodeReferenceTypeFlags), 0 };
	}

	case Opcode::ValueInteger:
	{
		return PrintResult{ code + sizeof(CompIntegerValue), 0 };
	}

	case Opcode::ValueFloat:
	{
		return PrintResult{ code + sizeof(CompFloatValue), 0 };
	}

	case Opcode::ValueString:
	{
		return PrintResult{ code + sizeof(ForeverValueId), 0 };
	}

	case Opcode::Trait:
	{
		u8 parameter_count;
		code = code_attach(code, &parameter_count);

		code += parameter_count * sizeof(IdentifierId);

		u16 member_count;
		code = code_attach(code, &member_count);

		s64 total_written = 0;

		for (u16 i = 0; i != member_count; ++i)
		{
			code += sizeof(IdentifierId) + sizeof(bool);

			OpcodeId type_completion;
			code = code_attach(code, &type_completion);

			const Opcode* const type_completion_code = opcode_from_id(core, type_completion);

			const s64 type_written = print_opcodes_impl(sink, core, type_completion_code, true);

			if (type_written < 0)
				return PrintResult{ nullptr, -1 };

			total_written += type_written;

			Maybe<OpcodeId> default_completion;
			code = code_attach(code, &default_completion);

			if (is_some(default_completion))
			{
				const Opcode* const default_completion_code = opcode_from_id(core, get(default_completion));

				const s64 default_written = print_opcodes_impl(sink, core, default_completion_code, true);

				if (default_written < 0)
					return PrintResult{ nullptr, -1 };

				total_written += default_written;
			}
		}

		return PrintResult{ code, total_written };
	}

	case Opcode::ImplBody:
	{
		u16 member_count;
		code = code_attach(code, &member_count);

		s64 total_written = 0;

		for (u16 i = 0; i != member_count; ++i)
		{
			code += sizeof(IdentifierId) + sizeof(bool);

			OpcodeId completion;
			code = code_attach(code, &completion);

			const Opcode* const completion_code = opcode_from_id(core, completion);

			const s64 written = print_opcodes_impl(sink, core, completion_code, true);

			if (written < 0)
				return PrintResult{ nullptr, -1 };

			total_written += written;
		}

		return PrintResult{ code, total_written };
	}

	case Opcode::ImplMemberAllocPrepare:
	{
		return PrintResult{ code + sizeof(bool) + sizeof(IdentifierId), 0 };
	}

	case Opcode::Switch:
		TODO("Implement");
	}

	ASSERT_UNREACHABLE;
}

static s64 follow_refs_impl(PrintSink sink, CoreData* core, const Opcode* code) noexcept
{
	s64 total_written = 0;

	PrintResult result;

	while (true)
	{
		result = follow_ref_impl(sink, core, code);

		if (result.next == nullptr)
			break;

		total_written += result.written;

		code = result.next;
	}

	return result.written < 0 ? -1 : total_written + result.written;
}

static PrintResult print_opcode_impl(PrintSink sink, CoreData* core, const Opcode* code) noexcept
{
	const u8 bits = static_cast<u8>(*code);

	const bool consumes_write_ctx = (bits & 0x80) != 0;

	const Opcode op = static_cast<Opcode>(bits & 0x7F);

	const char8* op_name = tag_name(op);

	const OpcodeId code_id = id_from_opcode(core, code);

	const s64 header_written = print(sink, "%[> 6]  % %",
		static_cast<u32>(code_id),
		consumes_write_ctx ? "@" : " ",
		op_name
	);

	if (header_written < 0)
		return PrintResult{ nullptr, -1 };

	code += 1;

	switch (op)
	{
	case Opcode::INVALID:
	case Opcode::EndCode:
	case Opcode::Return:
	case Opcode::FileGlobalAllocComplete:
	case Opcode::FileGlobalAllocUntyped:
	case Opcode::ImplMemberAllocComplete:
	{
		return PrintResult{ nullptr, header_written };
	}

	case Opcode::SetWriteCtx:
	case Opcode::ScopeEnd:
	case Opcode::ScopeEndPreserveTop:
	case Opcode::FileGlobalAllocTyped:
	case Opcode::PopClosure:
	case Opcode::ExecArgs:
	case Opcode::Call:
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
	case Opcode::ImplSetSelf:
	case Opcode::ImplTraitCall:
	case Opcode::ImplMemberAllocExplicitType:
	case Opcode::ImplMemberAllocImplicitType:
	case Opcode::GetSelf:
	{
		return PrintResult{ code, header_written };
	}

	case Opcode::ScopeBegin:
	{
		u16 member_count;

		code = code_attach(code, &member_count);

		const s64 written = print(sink, " member_count=%", member_count);

		if (written < 0)
			return PrintResult{ nullptr, -1 };

		return PrintResult{ code, header_written + written };
	}

	case Opcode::ScopeAllocTyped:
	case Opcode::ScopeAllocUntyped:
	{
		bool is_mut;

		code = code_attach(code, &is_mut);

		const s64 written = print(sink, " is_mut=%", is_mut);

		if (written < 0)
			return PrintResult{ nullptr, -1 };

		return PrintResult{ code, header_written + written };
	}

	case Opcode::FileGlobalAllocPrepare:
	{
		bool is_mut;

		code = code_attach(code, &is_mut);

		GlobalCompositeIndex file_index;

		code = code_attach(code, &file_index);

		u16 rank;

		code = code_attach(code, &rank);

		const s64 written = print(sink, " is_mut=% file_index=% rank=%", is_mut, static_cast<u16>(file_index), rank);

		if (written < 0)
			return PrintResult{ nullptr, -1 };

		return PrintResult{ code, header_written + written };
	}

	case Opcode::LoadScope:
	{
		u16 out;

		code = code_attach(code, &out);

		u16 rank;

		code = code_attach(code, &rank);

		const s64 written = print(sink, " out=% rank=%", out, rank);

		if (written < 0)
			return PrintResult{ nullptr, -1 };

		return PrintResult{ code, header_written + written };
	}

	case Opcode::LoadGlobal:
	{
		GlobalCompositeIndex index;

		code = code_attach(code, &index);

		u16 rank;

		code = code_attach(code, &rank);

		const s64 written = print(sink, " file_index=% rank=%", static_cast<u16>(index), rank);

		if (written < 0)
			return PrintResult{ nullptr, -1 };

		return PrintResult{ code, header_written + written };
	}

	case Opcode::LoadMember:
	{
		IdentifierId name;

		code = code_attach(code, &name);

		const Range<char8> name_str = identifier_name_from_id(core, name);

		const s64 written = print(sink, " name=IdentifierId<%> (%)", static_cast<u32>(name), name_str);

		if (written < 0)
			return PrintResult{ nullptr, -1 };

		return PrintResult{ code, header_written + written };
	}

	case Opcode::LoadClosure:
	{
		u16 rank;

		code = code_attach(code, &rank);

		const s64 written = print(sink, " rank=%", rank);

		if (written < 0)
			return PrintResult{ nullptr, -1 };

		return PrintResult{ code, header_written + written };
	}

	case Opcode::LoadBuiltin:
	case Opcode::ExecBuiltin:
	{
		Builtin builtin;

		code = code_attach(code, &builtin);

		const s64 written = print(sink, " %", tag_name(builtin));

		if (written < 0)
			return PrintResult{ nullptr, -1 };

		return PrintResult{ code, header_written + written };
	}

	case Opcode::Signature:
	{
		OpcodeSignatureFlags signature_flags;

		code = code_attach(code, &signature_flags);

		u8 parameter_count;

		code = code_attach(code, &parameter_count);

		u8 value_count;

		code = code_attach(code, &value_count);

		s64 total_written = print(sink, " % param_count=% value_count=%",
			signature_flags.is_func ? "func" : "proc",
			parameter_count,
			value_count
		);

		if (total_written < 0)
			return PrintResult{ nullptr, -1 };

		for (u8 i = 0; i != parameter_count; ++i)
		{
			IdentifierId parameter_name;

			code = code_attach(code, &parameter_name);

			OpcodeSignaturePerParameterFlags parameter_flags;

			code = code_attach(code, &parameter_flags);

			const Range<char8> parameter_name_str = identifier_name_from_id(core, parameter_name);

			const s64 written = print(sink, "\n     -        %[> 2]: mut=% eval=% type=% default=% name=IdentifierId<%> (%)",
				i,
				static_cast<bool>(parameter_flags.is_mut),
				static_cast<bool>(parameter_flags.is_eval),
				static_cast<bool>(parameter_flags.has_type),
				static_cast<bool>(parameter_flags.has_default),
				static_cast<u32>(parameter_name),
				parameter_name_str
			);

			if (written < 0)
				return PrintResult{ nullptr, -1 };

			total_written += written;
		}

		return PrintResult{ code, header_written + total_written };
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

		s64 total_written = print(sink, " % param_count=% value_count=% closed_count=%",
			signature_flags.is_func ? "func" : "proc",
			parameter_count,
			value_count,
			closed_over_value_count
		);

		if (total_written < 0)
			return PrintResult{ nullptr, -1 };

		if (signature_flags.has_templated_return_type)
		{
			OpcodeId return_completion;

			code = code_attach(code, &return_completion);

			const s64 written = print(sink, " return_completion=OpcodeId<%>", static_cast<u32>(return_completion));

			if (written < 0)
				return PrintResult{ nullptr, -1 };

			total_written += written;
		}

		for (u8 i = 0; i != parameter_count; ++i)
		{
			IdentifierId parameter_name;

			code = code_attach(code, &parameter_name);

			OpcodeSignaturePerParameterFlags parameter_flags;

			code = code_attach(code, &parameter_flags);

			const Range<char8> parameter_name_str = identifier_name_from_id(core, parameter_name);

			const s64 written = print(sink, "\n     -        %[> 2]: mut=% eval=% type=% default=% name=IdentifierId<%> (%)",
				i,
				static_cast<bool>(parameter_flags.is_mut),
				static_cast<bool>(parameter_flags.is_eval),
				static_cast<bool>(parameter_flags.has_type),
				static_cast<bool>(parameter_flags.has_default),
				static_cast<u32>(parameter_name),
				parameter_name_str
			);

			if (written < 0)
				return PrintResult{ nullptr, -1 };

			total_written += written;

			if (parameter_flags.is_templated)
			{
				OpcodeId parameter_completion;

				code = code_attach(code, &parameter_completion);

				const s64 completion_written = print(sink, " completion=OpcodeId<%>", static_cast<u32>(parameter_completion));
				
				if (completion_written < 0)
					return PrintResult{ nullptr, -1 };

				total_written += completion_written;
			}
		}

		return PrintResult{ code, header_written + total_written };
	}

	case Opcode::BindBody:
	{
		OpcodeId body_id;

		code = code_attach(code, &body_id);

		const s64 written = print(sink, " body=OpcodeId<%>", static_cast<u32>(body_id));

		if (written < 0)
			return PrintResult{ nullptr, -1 };

		return PrintResult{ code, header_written + written };
	}

	case Opcode::PrepareArgs:
	{
		u8 argument_count;

		code = code_attach(code, &argument_count);

		s64 total_written = print(sink, " count=%", argument_count);

		if (total_written < 0)
			return PrintResult{ nullptr, -1 };

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
				: identifier_name_from_id(core, argument_name);

			const s64 written = print(sink, "\n     -        %[> 2]: callback=OpcodeId<%> name=IdentifierId<%> (%)",
				i,
				static_cast<u32>(argument_callback),
				static_cast<u32>(argument_name),
				argument_name_str
			);

			if (written < 0)
				return PrintResult{ nullptr, -1 };

			total_written += written;
		}

		return PrintResult{ code, header_written + total_written };
	}

	case Opcode::CompleteParamTypedNoDefault:
	case Opcode::CompleteParamTypedWithDefault:
	case Opcode::CompleteParamUntyped:
	{
		u8 rank;

		code = code_attach(code, &rank);

		const s64 written = print(sink, " rank=%", rank);

		if (written < 0)
			return PrintResult{ nullptr, -1 };

		return PrintResult{ code, header_written + written };
	}

	case Opcode::ArrayPreInit:
	{
		u16 index_count;

		code = code_attach(code, &index_count);

		u16 leading_element_count;

		code = code_attach(code, &leading_element_count);

		s64 total_written = print(sink, " index_count=% leading_elem_count=%", index_count, leading_element_count);

		if (total_written < 0)
			return PrintResult{ nullptr, -1 };

		for (u16 i = 0; i != index_count; ++i)
		{
			u16 following_element_count;

			code = code_attach(code, &following_element_count);

			const s64 written = print(sink, "\n     -              following_elem_count=%", following_element_count);

			if (written < 0)
				return PrintResult{ nullptr, -1 };

			total_written += written;
		}

		return PrintResult{ code, header_written + total_written };
	}

	case Opcode::ArrayPostInit:
	{
		u16 total_element_count;

		code = code_attach(code, &total_element_count);

		u16 index_count;

		code = code_attach(code, &index_count);

		u16 leading_element_count;

		code = code_attach(code, &leading_element_count);

		s64 total_written = print(sink, " index_count=% leading_elem_count=% total_elem_count=%",
			index_count,
			leading_element_count,
			total_element_count
		);

		if (total_written < 0)
			return PrintResult{ nullptr, -1 };

		for (u16 i = 0; i != index_count; ++i)
		{
			u16 following_element_count;

			code = code_attach(code, &following_element_count);

			const s64 written = print(sink, "\n     -        following_elem_count=%", following_element_count);

			if (written < 0)
				return PrintResult{ nullptr, -1 };

			total_written += written;
		}

		return PrintResult{ code, header_written + total_written };
	}

	case Opcode::CompositePreInit:
	{
		u16 names_count;

		code = code_attach(code, &names_count);

		u16 leading_initializer_count;

		code = code_attach(code, &leading_initializer_count);

		s64 total_written = print(sink, " names_count=% leading_elem_count=%",
			names_count,
			leading_initializer_count
		);

		if (total_written < 0)
			return PrintResult{ nullptr, -1 };

		for (u16 i = 0; i != names_count; ++i)
		{
			IdentifierId name;

			code = code_attach(code, &name);

			u16 following_initializer_count;

			code = code_attach(code, &following_initializer_count);

			const Range<char8> name_str = identifier_name_from_id(core, name);

			const s64 written = print(sink, "\n     -        following_elem_count=% name=IdentifierId<%> (%)",
				following_initializer_count,
				static_cast<u32>(name),
				name_str
			);

			if (written < 0)
				return PrintResult{ nullptr, -1 };

			total_written += written;
		}

		return PrintResult{ code, header_written + total_written };
	}

	case Opcode::CompositePostInit:
	{
		u16 member_count;

		code = code_attach(code, &member_count);

		s64 total_written = print(sink, " total_elem_count=%", member_count);

		if (total_written < 0)
			return PrintResult{ nullptr, -1 };

		for (u16 i = 0; i != member_count; ++i)
		{
			IdentifierId name;

			code = code_attach(code, &name);

			const Range<char8> name_str = identifier_name_from_id(core, name);

			const s64 written = print(sink, "\n     -        name=IdentifierId<%> (%)",
				static_cast<u32>(name),
				name_str
			);

			if (written < 0)
				return PrintResult{ nullptr, -1 };

			total_written += written;
		}

		return PrintResult{ code, header_written + total_written };
	}

	case Opcode::If:
	{
		OpcodeId consequent;

		code = code_attach(code, &consequent);

		const s64 written = print(sink, " consequent=OpcodeId<%>",
			static_cast<u32>(consequent)
		);

		if (written < 0)
			return PrintResult{ nullptr, -1 };

		return PrintResult{ code, header_written + written };
	}

	case Opcode::IfElse:
	{
		OpcodeId consequent;

		code = code_attach(code, &consequent);

		OpcodeId alternative;

		code = code_attach(code, &alternative);

		const s64 written = print(sink, " consequent=OpcodeId<%> alternative=OpcodeId<%>",
			static_cast<u32>(consequent),
			static_cast<u32>(alternative)
		);

		if (written < 0)
			return PrintResult{ nullptr, -1 };

		return PrintResult{ code, header_written + written };
	}

	case Opcode::Loop:
	{
		OpcodeId condition_id;

		code = code_attach(code, &condition_id);

		OpcodeId body_id;

		code = code_attach(code, &body_id);

		const s64 written = print(sink, " cond=OpcodeId<%> body=OpcodeId<%>",
			static_cast<u32>(condition_id),
			static_cast<u32>(body_id)
		);

		if (written < 0)
			return PrintResult{ nullptr, -1 };

		return PrintResult{ code, header_written + written };
	}

	case Opcode::LoopFinally:
	{
		OpcodeId condition_id;

		code = code_attach(code, &condition_id);

		OpcodeId body_id;

		code = code_attach(code, &body_id);

		OpcodeId finally_id;

		code = code_attach(code, &finally_id);

		const s64 written = print(sink, " cond=OpcodeId<%> body=OpcodeId<%> finally=OpcodeId<%>",
			static_cast<u32>(condition_id),
			static_cast<u32>(body_id),
			static_cast<u32>(finally_id)
		);

		if (written < 0)
			return PrintResult{ nullptr, -1 };

		return PrintResult{ code, header_written + written };
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

		const s64 written = print(sink, " %", kind_name);

		if (written < 0)
			return PrintResult{ nullptr, -1 };

		return PrintResult{ code, header_written + written };
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

		const s64 written = print(sink, " %", kind_name);

		if (written < 0)
			return PrintResult{ nullptr, -1 };

		return PrintResult{ code, header_written + written };
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

		const s64 written = print(sink, " %", kind_name);

		if (written < 0)
			return PrintResult{ nullptr, -1 };

		return PrintResult{ code, header_written + written };
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

		const s64 written = print(sink, " %", kind_name);

		if (written < 0)
			return PrintResult{ nullptr, -1 };

		return PrintResult{ code, header_written + written };
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

		const s64 written = print(sink, " %", kind_name);

		if (written < 0)
			return PrintResult{ nullptr, -1 };

		return PrintResult{ code, header_written + written };
	}

	case Opcode::ReferenceType:
	{
		OpcodeReferenceTypeFlags flags;

		code = code_attach(code, &flags);

		const TypeTag tag = static_cast<TypeTag>(flags.tag);

		s64 written;

		if (tag == TypeTag::Ptr)
		{
			written = print(sink, " Ptr is_mut=% is_multi=% is_opt=%",
				static_cast<bool>(flags.is_mut),
				static_cast<bool>(flags.is_multi),
				static_cast<bool>(flags.is_opt)
			);
		}
		else if (tag == TypeTag::Slice)
		{
			written = print(sink, " Slice is_mut=%",
				static_cast<bool>(flags.is_mut)
			);
		}
		else
		{
			ASSERT_UNREACHABLE;
		}

		if (written < 0)
			return PrintResult{ nullptr, -1 };

		return PrintResult{ code, header_written + written };
	}

	case Opcode::ValueInteger:
	{
		CompIntegerValue value;

		code = code_attach(code, &value);

		bool is_negative = comp_integer_compare(value, comp_integer_from_u64(0)) == StrongCompareOrdering::LessThan;

		if (is_negative)
			value = comp_integer_neg(value);

		s64 written;

		u64 u64_value;

		if (u64_from_comp_integer(value, 64, &u64_value))
			written = print(sink, " CompIntegerValue<%[]%>", is_negative ? "-" : "", u64_value);
		else
			written = print(sink, " CompIntegerValue<%BIG>", is_negative ? "-" : "");

		if (written < 0)
			return PrintResult{ nullptr, -1 };

		return PrintResult{ code, header_written + written };
	}

	case Opcode::ValueFloat:
	{
		CompFloatValue value;

		code = code_attach(code, &value);

		const s64 written = print(sink, " CompFloatValue<%>", f64_from_comp_float(value));

		if (written < 0)
			return PrintResult{ nullptr, -1 };

		return PrintResult{ code, header_written + written };
	}

	case Opcode::ValueString:
	{
		ForeverValueId value;

		code = code_attach(code, &value);

		const s64 written = print(sink, " ForeverValueId<%>", static_cast<u32>(value));

		if (written < 0)
			return PrintResult{ nullptr, -1 };

		return PrintResult{ code, header_written + written };
	}

	case Opcode::Trait:
	{
		u8 parameter_count;
		code = code_attach(code, &parameter_count);

		s64 written = print(sink, " parameter_count=%", parameter_count);

		if (written < 0)
			return PrintResult{ nullptr, -1 };

		for (u8 i = 0; i != parameter_count; ++i)
		{
			IdentifierId parameter_name_id;

			code = code_attach(code, &parameter_name_id);

			const Range<char8> parameter_name_str = identifier_name_from_id(core, parameter_name_id);

			const s64 parameter_written = print(sink, "\n     -        %[> 2]: name=IdentifierId<%> (%)",
				i,
				static_cast<u32>(parameter_name_id),
				parameter_name_str
			);

			if (parameter_written < 0)
				return PrintResult{ nullptr, -1 };

			written += parameter_written;
		}

		u16 member_count;
		code = code_attach(code, &member_count);

		const s64 member_header_written = print(sink, "\n     -     member_count=%", member_count);

		if (member_header_written < 0)
			return PrintResult{ nullptr, -1 };

		written += member_header_written;

		for (u16 i = 0; i != member_count; ++i)
		{
			IdentifierId name_id;
			code = code_attach(code, &name_id);

			bool is_mut;
			code = code_attach(code, &is_mut);

			OpcodeId type_completion;
			code = code_attach(code, &type_completion);

			Maybe<OpcodeId> default_completion;
			code = code_attach(code, &default_completion);

			const Range<char8> name_str = identifier_name_from_id(core, name_id);

			const s64 member_written = print(sink, "\n     -        %[> 2]: is_mut=% type=OpcodeId<%> default=",
				i,
				is_mut,
				static_cast<u32>(type_completion)
			);

			if (member_written < 0)
				return PrintResult{ nullptr, -1 };

			written += member_written;

			s64 default_written;

			if (is_some(default_completion))
				default_written = print(sink, "OpcodeId<%>", static_cast<u32>(get(default_completion)));
			else
				default_written = print(sink, "none");

			if (default_written < 0)
				return PrintResult{ nullptr, -1 };

			written += default_written;

			const s64 name_written = print(sink, " name=IdentifierId<%> (%)",
				static_cast<u32>(name_id),
				name_str
			);

			if (name_written < 0)
				return PrintResult{ nullptr, -1 };

			written += name_written;
		}

		return PrintResult{ code, header_written + written };
	}

	case Opcode::ImplBody:
	{
		u16 member_count;
		code = code_attach(code, &member_count);

		s64 written = print(sink, " member_count=%", member_count);

		if (written < 0)
			return PrintResult{ nullptr, -1 };

		for (u16 i = 0; i != member_count; ++i)
		{
			IdentifierId name_id;
			code = code_attach(code, &name_id);

			bool is_mut;
			code = code_attach(code, &is_mut);

			OpcodeId completion_id;
			code = code_attach(code, &completion_id);

			const Range<char8> name_str = identifier_name_from_id(core, name_id);

			const s64 member_written = print(sink, "\n     -        is_mut=% completion=OpcodeId<%> name=IdentifierId<%> (%)",
				is_mut,
				static_cast<u32>(completion_id),
				static_cast<u32>(name_id),
				name_str
			);

			if (member_written < 0)
				return PrintResult{ nullptr, -1 };

			written += member_written;
		}

		return PrintResult{ code, header_written + written };
	}
 
	case Opcode::ImplMemberAllocPrepare:
	{
		IdentifierId name_id;
		code = code_attach(code, &name_id);

		bool is_mut;
		code = code_attach(code, &is_mut);

		const Range<char8> name_str = identifier_name_from_id(core, name_id);

		const s64 written = print(sink, " is_mut=% name=IdentifierId<%> (%)",
			is_mut,
			static_cast<u32>(name_id),
			name_str
		);
		
		if (written < 0)
			return PrintResult{ nullptr, -1 };

		return PrintResult{ code, header_written + written };
	}

	case Opcode::Switch:
		TODO("Implement");
	}

	ASSERT_UNREACHABLE;
}

static s64 print_opcodes_impl(PrintSink sink, CoreData* core, const Opcode* code, bool follow_refs) noexcept
{
	const Opcode* const code_begin = code;

	s64 total_written = 0;

	PrintResult result;

	while (true)
	{
		result = print_opcode_impl(sink, core, code);

		if (result.next == nullptr)
			break;

		code = result.next;

		total_written += result.written;

		const s64 written = print(sink, "\n");

		if (written < 0)
			return -1;

		total_written += written;
	}

	if (result.written < 0)
		return -1;

	total_written += result.written;

	const s64 written = print(sink, "\n\n");

	if (written < 0)
		return -1;

	total_written += written;

	if (follow_refs)
	{
		const s64 refs_written = follow_refs_impl(sink, core, code_begin);

		if (refs_written < 0)
			return -1;

		total_written += refs_written;
	}

	return total_written;
}



s64 diag::print_opcodes(PrintSink sink, CoreData* core, const Opcode* code, bool follow_refs) noexcept
{
	return print_opcodes_impl(sink, core, code, follow_refs);
}
