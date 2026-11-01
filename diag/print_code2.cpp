#include "diag.hpp"
#include "../core/core.hpp"

#include "../infra/panic.hpp"

struct PrintResult
{
	s64 written;

	const Code2* next;
};

static PrintResult print_one_code2(PrintSink sink, CoreData* core, const Code2Block* base, const Code2* code) noexcept
{
	const Code2Op op = code->op;

	const char8* const op_name = tag_name(op);

	const Code2Offset offset = offset_from_code2(base, code);

	s64 total_written = print(sink, "%[> 6] %", static_cast<u32>(offset), op_name);

	if (total_written < 0)
		return { -1, nullptr };

	switch (op)
	{
	case Code2Op::Leave:
	{
		const Code2AttLeave* const attach = attachment_of<Code2AttLeave>(code);

		s64 written = print(sink, " out=% value=%",
			attach->header.leave.out,
			static_cast<u32>(attach->value)
		);

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		written = is_some(attach->prev)
			? print(sink, " prev=%\n", static_cast<u32>(get(attach->prev)))
			: print(sink, " prev=<none>\n");

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::Repeat:
	{
		const Code2AttRepeat* const attach = attachment_of<Code2AttRepeat>(code);

		s64 written = print(sink, " out=%\n", attach->header.repeat.out);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::ScopeBegin:
	{
		const Code2AttScopeBegin* const attach = attachment_of<Code2AttScopeBegin>(code);

		s64 written = print(sink, " definitions=%\n", attach->header.scope_begin.definition_count);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::Builtin:
	{
		const Code2AttBuiltin* const attach = attachment_of<Code2AttBuiltin>(code);

		s64 written = print(sink, " builtin=%\n", tag_name(attach->header.builtin.builtin));

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::Composite:
	{
		const Code2AttComposite* const attach = attachment_of<Code2AttComposite>(code);

		s64 written = print(sink, " members=%\n", attach->header.composite.member_count);

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		for (u16 i = 0; i != attach->header.composite.member_count; ++i)
		{
			const Code2AttComposite_Member* const member = attach->members + i;

			const Range<char8> name = is_some(member->name)
				? identifier_name_from_id(core, get(member->name))
				: range::from_literal_string("<none>");

			written = print(sink, "            [%] name=% value=%\n", i, name, static_cast<u32>(member->value));

			if (written < 0)
			return { -1, nullptr };

			total_written += written;
		}

		return { total_written, reinterpret_cast<const Code2*>(attach->members + attach->header.composite.member_count) };
	}

	case Code2Op::Array:
	{
		const Code2AttArray* const attach = attachment_of<Code2AttArray>(code);

		s64 written = print(sink, " elements=%\n", attach->header.array.element_count);

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		for (u16 i = 0; i != attach->header.array.element_count; ++i)
		{
			const Code2AttArray_Element* const member = attach->elements + i;

			written = print(sink, "            [%] value=%\n", i, static_cast<u32>(member->value));

			if (written < 0)
			return { -1, nullptr };

			total_written += written;
		}

		return { total_written, reinterpret_cast<const Code2*>(attach->elements + attach->header.array.element_count) };
	}

	case Code2Op::Definition:
	{
		const Code2AttDefinition* const attach = attachment_of<Code2AttDefinition>(code);

		s64 written = is_some(attach->type)
			? print(sink, " type=%", static_cast<u32>(get(attach->type)))
			: print(sink, " type=<none>");

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		written = is_some(attach->value)
			? print(sink, " value=%\n", static_cast<u32>(get(attach->value)))
			: print(sink, " value=<none>\n");

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::MakeLocation:
	{
		const Code2AttMakeLocation* const attach = attachment_of<Code2AttMakeLocation>(code);

		s64 written = print(sink, " mut=% value=%\n",
			attach->header.make_location.is_mut,
			static_cast<u32>(attach->value)
		);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::DefineUntyped:
	{
		const Code2AttDefineLocal* const attach = attachment_of<Code2AttDefineLocal>(code);

		s64 written = print(sink, " index=% value=% mut=% selfref=%\n",
			attach->header.define_local.definition_index,
			static_cast<u32>(attach->value),
			attach->header.define_local.is_mut,
			attach->header.define_local.has_self_reference
		);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::ConsumeVoid:
	case Code2Op::Eval:
	case Code2Op::Defer:
	case Code2Op::Distinct:
	case Code2Op::Address:
	case Code2Op::Dereference:
	case Code2Op::BitNot:
	case Code2Op::LogicalNot:
	case Code2Op::Negate:
	case Code2Op::UnaryPlus:
	case Code2Op::DupAndLoadLocation:
	{
		const Code2AttUnaryOp* const attach = attachment_of<Code2AttUnaryOp>(code);

		s64 written = print(sink, " operand=%\n", static_cast<u32>(attach->operand));

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::ScopeEnd:
	case Code2Op::Void:
	case Code2Op::Self:
	case Code2Op::Unreachable:
	case Code2Op::Undefined:
	{
		const Code2AttNonaryOp* const attach = attachment_of<Code2AttNonaryOp>(code);

		s64 written = print(sink, "\n");

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::If:
	{
		const Code2AttIf* const attach = attachment_of<Code2AttIf>(code);

		s64 written = print(sink, " condition=% consequent=@% alternative=@%\n",
			static_cast<u32>(attach->condition),
			static_cast<u32>(attach->consequent),
			static_cast<u32>(attach->alternative)
		);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::Loop:
	{
		const Code2AttLoop* const attach = attachment_of<Code2AttLoop>(code);

		s64 written = print(sink, " body=@%\n", static_cast<u32>(attach->body));

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::Func:
	{
		const Code2AttFunc* const attach = attachment_of<Code2AttFunc>(code);

		s64 written = print(sink, " signature=% body=@%\n",
			static_cast<u32>(attach->signature),
			static_cast<u32>(attach->body)
		);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::Signature:
	{
		const Code2AttSignatureBegin* const attach = attachment_of<Code2AttSignatureBegin>(code);

		s64 written = print(sink, " kind=% parameters=%\n",
			attach->header.signature_begin.is_proc ? range::from_literal_string("proc") : range::from_literal_string("func"),
			attach->header.signature_begin.parameter_count
		);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::SignatureEnd:
	{
		const Code2AttSignatureEnd* const attach = attachment_of<Code2AttSignatureEnd>(code);

		s64 written = print(sink, " signature=% rettype=%\n",
			static_cast<u32>(attach->signature),
			static_cast<u32>(attach->return_type)
		);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::Parameter:
	{
		const Code2AttParameter* const attach = attachment_of<Code2AttParameter>(code);

		const Range<char8> name = identifier_name_from_id(core, attach->name);

		s64 written = print(sink, " signature=% name=% mut=% eval=% templated=% variadic=% type=%",
			static_cast<u32>(attach->signature),
			name,
			attach->header.parameter.is_mut,
			attach->header.parameter.is_eval,
			attach->header.parameter.is_templated,
			attach->header.parameter.is_variadic,
			static_cast<u32>(attach->type)
		);

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		written = is_some(attach->default_value)
			? print(sink, " value=%\n", static_cast<u32>(get(attach->default_value)))
			: print(sink, " value=<none>\n");

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::Trait:
	{
		const Code2AttTrait* const attach = attachment_of<Code2AttTrait>(code);

		s64 written = print(sink, " parameters=% members=%\n", attach->header.trait.parameter_count, attach->header.trait.member_count);

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		for (u8 i = 0; i != attach->header.trait.parameter_count; ++i)
		{
			const Range<char8> name = identifier_name_from_id(core, attach->parameters[i]);

			written = print(sink, "            [%] name=%\n", i, name);

			if (written < 0)
				return { -1, nullptr };

			total_written += written;
		}

		written = print(sink, "\n");

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		const Code2AttTrait_Member* const members = reinterpret_cast<const Code2AttTrait_Member*>(attach->parameters + attach->header.trait.parameter_count);

		for (u16 i = 0; i != attach->header.trait.member_count; ++i)
		{
			const Range<char8> name = identifier_name_from_id(core, members[i].name);

			written = print(sink, "            [%] name=% mut=% pub=% type=@%",
				i,
				name,
				members[i].is_mut,
				members[i].is_pub,
				static_cast<u32>(members[i].type)
			);

			if (written < 0)
				return { -1, nullptr };

			total_written += written;

			written = is_some(members[i].default_value)
				? print(sink, " default=@%\n", static_cast<u32>(get(members[i].default_value)))
				: print(sink, " default=<none>\n");

			if (written < 0)
				return { -1, nullptr };

			total_written += written;
		}

		return { total_written, reinterpret_cast<const Code2*>(members + attach->header.trait.member_count) };
	}

	case Code2Op::Impl:
	{
		const Code2AttImpl* const attach = attachment_of<Code2AttImpl>(code);

		s64 written = print(sink, " base=% traitcall=% members=%\n", static_cast<u32>(attach->base_type), static_cast<u32>(attach->trait_call), attach->header.impl.member_count);

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		for (u16 i = 0; i != attach->header.impl.member_count; ++i)
		{
			const Range<char8> name = identifier_name_from_id(core, attach->members[i].name);

			written = print(sink, "            [%] name=% mut=% pub=%",
				i,
				name,
				attach->members[i].is_mut,
				attach->members[i].is_pub
			);

			if (written < 0)
				return { -1, nullptr };

			total_written += written;

			written = is_some(attach->members[i].type)
				? print(sink, " type=@%\n", static_cast<u32>(get(attach->members[i].type)))
				: print(sink, " type=<none>\n");

			if (written < 0)
				return { -1, nullptr };

			total_written += written;

			written = print(sink, " type=@%\n", static_cast<u32>(attach->members[i].value));

			if (written < 0)
				return { -1, nullptr };

			total_written += written;
		}

		return { total_written, reinterpret_cast<const Code2*>(attach->members + attach->header.impl.member_count) };
	}

	case Code2Op::ImplTraitCall:
	case Code2Op::Call:
	{
		const Code2AttCall* const attach = attachment_of<Code2AttCall>(code);

		s64 written = print(sink, " callee=% arguments=%\n", static_cast<u32>(attach->callee), attach->header.call.argument_count);

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		for (u8 i = 0; i != attach->header.call.argument_count; ++i)
		{
			const Range<char8> name = is_some(attach->arguments[i].name)
				? identifier_name_from_id(core, get(attach->arguments[i].name))
				: range::from_literal_string("<none>");

			written = print(sink, "            [%] name=% value=@%\n",
				i,
				name,
				static_cast<u32>(attach->arguments[i].value)
			);

			if (written < 0)
				return { -1, nullptr };

			total_written += written;
		}

		return { total_written, reinterpret_cast<const Code2*>(attach->arguments + attach->header.call.argument_count) };
	}

	case Code2Op::TemplatedParamValue:
	case Code2Op::TemplatedParamLocation:
	{
		const Code2AttTemplatedParamRef* const attach = attachment_of<Code2AttTemplatedParamRef>(code);

		s64 written = print(sink, " rank=%\n", attach->header.templated_parameter_ref.rank);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::LocalValue:
	case Code2Op::LocalLocation:
	{
		const Code2AttLocalRef* const attach = attachment_of<Code2AttLocalRef>(code);

		s64 written = print(sink, " out=% rank=%\n", attach->header.local_ref.out, attach->header.local_ref.rank);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::GlobalValue:
	case Code2Op::GlobalLocation:
	{
		const Code2AttGlobalRef* const attach = attachment_of<Code2AttGlobalRef>(code);

		s64 written = print(sink, " rank=%\n", attach->header.global_ref.rank);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::ClosedValue:
	case Code2Op::ClosedLocation:
	{
		const Code2AttClosedRef* const attach = attachment_of<Code2AttClosedRef>(code);

		s64 written = print(sink, " rank=%\n", attach->header.closed_ref.rank);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::TraitArgumentValue:
	case Code2Op::TraitArgumentLocation:
	{
		const Code2AttTraitArgumentRef* const attach = attachment_of<Code2AttTraitArgumentRef>(code);

		s64 written = print(sink, " rank=%\n", attach->header.trait_argument_ref.rank);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::Slice:
	{
		const Code2AttSlice* const attach = attachment_of<Code2AttSlice>(code);

		s64 written = print(sink, " sliced=%", static_cast<u32>(attach->sliced));

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		if (is_some(attach->begin))
		{
			written = print(sink, " begin=%", static_cast<u32>(get(attach->begin)));

			if (written < 0)
				return { -1, nullptr };

			total_written += written;
		}

		written = is_some(attach->end)
			? print(sink, " end=%\n", static_cast<u32>(get(attach->end)))
			: print(sink, "\n");

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::Integer16:
	{
		const Code2AttInteger16* const attach = attachment_of<Code2AttInteger16>(code);

		s64 written = print(sink, " value=%\n", attach->header.integer16.value);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::Integer64:
	{
		const Code2AttInteger64* const attach = attachment_of<Code2AttInteger64>(code);

		CompIntegerValue value;

		memcpy(&value, &attach->value_lo, sizeof(CompIntegerValue));

		s64 written;
		
		u64 raw_value;
		
		if (u64_from_comp_integer(value, 64, &raw_value))
			written = print(sink, " value=%\n", raw_value);
		else
			written = print(sink, " value=<huge>\n");

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::Float:
	{
		const Code2AttFloat* const attach = attachment_of<Code2AttFloat>(code);

		CompFloatValue value;
		memcpy(&value, &attach->value_lo, sizeof(CompFloatValue));

		s64 written = print(sink, " value=%\n", f64_from_comp_float(value));

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::String:
	{
		const Code2AttString* const attach = attachment_of<Code2AttString>(code);

		const char8* const begin = static_cast<char8*>(address_from_core_id(core, attach->data));

		const Range<char8> value{ begin, attach->size };

		s64 written = print(sink, " length=% value=\"%\"\n", attach->size, value);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::TypeTailArray:
	case Code2Op::TypeSlice:
	case Code2Op::TypePtr:
	{
		const Code2AttReferenceType* const attach = attachment_of<Code2AttReferenceType>(code);

		s64 written = print(sink, " mut=% opt=% multi=% operand=%\n",
			attach->header.reference_type.is_mut,
			attach->header.reference_type.is_opt,
			attach->header.reference_type.is_multi,
			static_cast<u32>(attach->operand)
		);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::WithType:
	case Code2Op::Add:
	case Code2Op::Sub:
	case Code2Op::Mul:
	case Code2Op::Div:
	case Code2Op::AddTC:
	case Code2Op::SubTC:
	case Code2Op::MulTC:
	case Code2Op::Mod:
	case Code2Op::BitAnd:
	case Code2Op::BitOr:
	case Code2Op::BitXor:
	case Code2Op::ShiftLeft:
	case Code2Op::ShiftRight:
	case Code2Op::TypeArray:
	case Code2Op::Index:
	case Code2Op::Store:
	{
		const Code2AttBinaryOp* const attach = attachment_of<Code2AttBinaryOp>(code);

		s64 written = print(sink, " lhs=% rhs=%\n",
			static_cast<u32>(attach->lhs),
			static_cast<u32>(attach->rhs)
		);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::LogicalAnd:
	case Code2Op::LogicalOr:
	{
		const Code2AttLogicalOp* const attach = attachment_of<Code2AttLogicalOp>(code);

		s64 written = print(sink, " lhs=% rhs=@%\n",
			static_cast<u32>(attach->lhs),
			static_cast<u32>(attach->rhs)
		);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::MemberValue:
	case Code2Op::MemberLocation:
	{
		const Code2AttMember* const attach = attachment_of<Code2AttMember>(code);

		const Range<char8> name = identifier_name_from_id(core, attach->name);

		s64 written = print(sink, " operand=% name=%\n", static_cast<u32>(attach->composite), name);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::Compare:
	{
		const Code2AttCompare* const attach = attachment_of<Code2AttCompare>(code);

		const char8* const kind = tag_name(attach->header.compare.kind);

		s64 written = print(sink, " kind=% lhs=% rhs=%\n",
			kind,
			static_cast<u32>(attach->lhs),
			static_cast<u32>(attach->rhs)
		);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::INVALID:
		; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

static PrintResult print_one_code2_ref(PrintSink sink, CoreData* core, const Code2* code) noexcept
{
	switch (code->op)
	{
	case Code2Op::Leave:
	{
		const Code2AttLeave* const attach = attachment_of<Code2AttLeave>(code);

		return { 0, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::Repeat:
	{
		const Code2AttRepeat* const attach = attachment_of<Code2AttRepeat>(code);

		return { 0, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::ScopeBegin:
	{
		const Code2AttScopeBegin* const attach = attachment_of<Code2AttScopeBegin>(code);

		return { 0, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::Builtin:
	{
		const Code2AttBuiltin* const attach = attachment_of<Code2AttBuiltin>(code);

		return { 0, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::Composite:
	{
		const Code2AttComposite* const attach = attachment_of<Code2AttComposite>(code);

		return { 0, reinterpret_cast<const Code2*>(attach->members + attach->header.composite.member_count) };
	}

	case Code2Op::Array:
	{
		const Code2AttArray* const attach = attachment_of<Code2AttArray>(code);

		return { 0, reinterpret_cast<const Code2*>(attach->elements + attach->header.array.element_count) };
	}

	case Code2Op::Definition:
	{
		const Code2AttDefinition* const attach = attachment_of<Code2AttDefinition>(code);

		return { 0, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::MakeLocation:
	{
		const Code2AttMakeLocation* const attach = attachment_of<Code2AttMakeLocation>(code);

		return { 0, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::DefineUntyped:
	{
		const Code2AttDefineLocal* const attach = attachment_of<Code2AttDefineLocal>(code);

		return { 0, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::ConsumeVoid:
	case Code2Op::Eval:
	case Code2Op::Defer:
	case Code2Op::Distinct:
	case Code2Op::Address:
	case Code2Op::Dereference:
	case Code2Op::BitNot:
	case Code2Op::LogicalNot:
	case Code2Op::Negate:
	case Code2Op::UnaryPlus:
	case Code2Op::DupAndLoadLocation:
	{
		const Code2AttUnaryOp* const attach = attachment_of<Code2AttUnaryOp>(code);

		return { 0, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::ScopeEnd:
	case Code2Op::Void:
	case Code2Op::Self:
	case Code2Op::Unreachable:
	case Code2Op::Undefined:
	{
		const Code2AttNonaryOp* const attach = attachment_of<Code2AttNonaryOp>(code);

		return { 0, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::If:
	{
		s64 total_written = 0;

		const Code2AttIf* const attach = attachment_of<Code2AttIf>(code);

		s64 written = print(sink, "\n");

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		const Code2Block* const consequent = code2_block_from_id(core, attach->consequent);

		written = diag::print_code2(sink, core, consequent);

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		written = print(sink, "\n");

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		const Code2Block* const alternative = code2_block_from_id(core, attach->alternative);

		written = diag::print_code2(sink, core, alternative);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::Loop:
	{
		s64 total_written = 0;

		const Code2AttLoop* const attach = attachment_of<Code2AttLoop>(code);

		s64 written = print(sink, "\n");

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		const Code2Block* const body = code2_block_from_id(core, attach->body);

		written = diag::print_code2(sink, core, body);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::Func:
	{
		const Code2AttFunc* const attach = attachment_of<Code2AttFunc>(code);

		const s64 newline_written = print(sink, "\n");

		if (newline_written < 0)
			return { -1, nullptr };

		const Code2Block* const body = code2_block_from_id(core, attach->body);

		const s64 body_written = diag::print_code2(sink, core, body);

		if (body_written < 0)
			return { -1, nullptr };

		return { newline_written + body_written, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::Signature:
	{
		const Code2AttSignatureBegin* const attach = attachment_of<Code2AttSignatureBegin>(code);

		return { 0, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::SignatureEnd:
	{
		const Code2AttSignatureEnd* const attach = attachment_of<Code2AttSignatureEnd>(code);

		return { 0, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::Parameter:
	{
		const Code2AttParameter* const attach = attachment_of<Code2AttParameter>(code);

		return { 0, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::Trait:
	{
		const Code2AttTrait* const attach = attachment_of<Code2AttTrait>(code);

		const Code2AttTrait_Member* const members = reinterpret_cast<const Code2AttTrait_Member*>(attach->parameters + attach->header.trait.parameter_count);

		s64 total_written = 0;

		for (u16 i = 0; i != attach->header.trait.member_count; ++i)
		{
			const s64 type_newline_written = print(sink, "\n");

			if (type_newline_written < 0)
				return { -1, nullptr };

			total_written += type_newline_written;

			const Code2Block* const type = code2_block_from_id(core, members[i].type);

			const s64 type_written = diag::print_code2(sink, core, type);

			if (type_written < 0)
				return { -1, nullptr };

			total_written += type_written;

			if (is_some(members[i].default_value))
			{
				const s64 default_value_newline_written = print(sink, "\n");

				if (default_value_newline_written < 0)
					return { -1, nullptr };

				total_written += default_value_newline_written;

				const Code2Block* const default_value = code2_block_from_id(core, get(members[i].default_value));

				const s64 default_value_written = diag::print_code2(sink, core, default_value);

				if (default_value_written < 0)
					return { -1, nullptr };

				total_written += default_value_written;
			}
		}

		return { total_written, reinterpret_cast<const Code2*>(members + attach->header.trait.member_count) };
	}

	case Code2Op::Impl:
	{
		const Code2AttImpl* const attach = attachment_of<Code2AttImpl>(code);

		s64 total_written = 0;

		for (u16 i = 0; i != attach->header.impl.member_count; ++i)
		{
			if (is_some(attach->members[i].type))
			{
				const s64 type_newline_written = print(sink, "\n");

				if (type_newline_written < 0)
					return { -1, nullptr };

				total_written += type_newline_written;

				const Code2Block* const type = code2_block_from_id(core, get(attach->members[i].type));

				const s64 type_written = diag::print_code2(sink, core, type);

				if (type_written < 0)
					return { -1, nullptr };

				total_written += type_written;
			}

			const s64 value_newline_written = print(sink, "\n");

			if (value_newline_written < 0)
				return { -1, nullptr };

			total_written += value_newline_written;

			const Code2Block* const value = code2_block_from_id(core, attach->members[i].value);

			const s64 value_written = diag::print_code2(sink, core, value);

			if (value_written < 0)
				return { -1, nullptr };

			total_written += value_written;
		}

		return { total_written, reinterpret_cast<const Code2*>(attach->members + attach->header.impl.member_count) };
	}

	case Code2Op::ImplTraitCall:
	case Code2Op::Call:
	{
		const Code2AttCall* const attach = attachment_of<Code2AttCall>(code);

		s64 total_written = 0;

		for (u16 i = 0; i != attach->header.call.argument_count; ++i)
		{
			const s64 newline_written = print(sink, "\n");

			if (newline_written < 0)
				return { -1, nullptr };

			total_written += newline_written;

			const Code2Block* const value = code2_block_from_id(core, attach->arguments[i].value);

			const s64 value_written = diag::print_code2(sink, core, value);

			if (value_written < 0)
				return { -1, nullptr };

			total_written += value_written;
		}

		return { total_written, reinterpret_cast<const Code2*>(attach->arguments + attach->header.call.argument_count) };
	}

	case Code2Op::TemplatedParamValue:
	case Code2Op::TemplatedParamLocation:
	{
		const Code2AttTemplatedParamRef* const attach = attachment_of<Code2AttTemplatedParamRef>(code);

		return { 0, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::LocalValue:
	case Code2Op::LocalLocation:
	{
		const Code2AttLocalRef* const attach = attachment_of<Code2AttLocalRef>(code);

		return { 0, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::GlobalValue:
	case Code2Op::GlobalLocation:
	{
		const Code2AttGlobalRef* const attach = attachment_of<Code2AttGlobalRef>(code);

		return { 0, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::ClosedValue:
	case Code2Op::ClosedLocation:
	{
		const Code2AttClosedRef* const attach = attachment_of<Code2AttClosedRef>(code);

		return { 0, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::TraitArgumentValue:
	case Code2Op::TraitArgumentLocation:
	{
		const Code2AttTraitArgumentRef* const attach = attachment_of<Code2AttTraitArgumentRef>(code);

		return { 0, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::Slice:
	{
		const Code2AttSlice* const attach = attachment_of<Code2AttSlice>(code);

		return { 0, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::Integer16:
	{
		const Code2AttInteger16* const attach = attachment_of<Code2AttInteger16>(code);

		return { 0, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::Integer64:
	{
		const Code2AttInteger64* const attach = attachment_of<Code2AttInteger64>(code);

		return { 0, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::Float:
	{
		const Code2AttFloat* const attach = attachment_of<Code2AttFloat>(code);

		return { 0, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::String:
	{
		const Code2AttString* const attach = attachment_of<Code2AttString>(code);

		return { 0, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::TypeTailArray:
	case Code2Op::TypeSlice:
	case Code2Op::TypePtr:
	{
		const Code2AttReferenceType* const attach = attachment_of<Code2AttReferenceType>(code);

		return { 0, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::WithType:
	case Code2Op::Add:
	case Code2Op::Sub:
	case Code2Op::Mul:
	case Code2Op::Div:
	case Code2Op::AddTC:
	case Code2Op::SubTC:
	case Code2Op::MulTC:
	case Code2Op::Mod:
	case Code2Op::BitAnd:
	case Code2Op::BitOr:
	case Code2Op::BitXor:
	case Code2Op::ShiftLeft:
	case Code2Op::ShiftRight:
	case Code2Op::TypeArray:
	case Code2Op::Index:
	case Code2Op::Store:
	{
		const Code2AttBinaryOp* const attach = attachment_of<Code2AttBinaryOp>(code);

		return { 0, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::LogicalAnd:
	case Code2Op::LogicalOr:
	{
		const Code2AttLogicalOp* const attach = attachment_of<Code2AttLogicalOp>(code);

		const s64 newline_written = print(sink, "\n");

		if (newline_written < 0)
			return { -1, nullptr };

		const Code2Block* const rhs = code2_block_from_id(core, attach->rhs);

		const s64 rhs_written = diag::print_code2(sink, core, rhs);

		if (rhs_written < 0)
			return { -1, nullptr };

		return { newline_written + rhs_written, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::MemberValue:
	case Code2Op::MemberLocation:
	{
		const Code2AttMember* const attach = attachment_of<Code2AttMember>(code);

		return { 0, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::Compare:
	{
		const Code2AttCompare* const attach = attachment_of<Code2AttCompare>(code);

		return { 0, reinterpret_cast<const Code2*>(attach + 1) };
	}

	case Code2Op::INVALID:
		; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

s64 diag::print_code2(PrintSink sink, CoreData* core, const Code2Block* header) noexcept
{
	const Code2* const begin = code2_block_begin(header);

	const Code2* const end = code2_block_end(header);

	const Code2Id id = id_from_code2_block(core, header);

	s64 total_written = print(sink, " @%\n", static_cast<u32>(id));

	if (total_written < 0)
		return -1;

	PrintResult result{};

	const Code2* curr = begin;

	while (curr != end && result.written >= 0)
	{
		result = print_one_code2(sink, core, header, curr);

		curr = result.next;

		total_written += result.written;
	}

	if (result.written < 0)
		return -1;

	curr = begin;

	while (curr != end && result.written >= 0)
	{
		result = print_one_code2_ref(sink, core, curr);

		curr = result.next;

		total_written += result.written;
	}
	
	if (result.written < 0)
		return -1;

	return total_written;
}
