#include "diag.hpp"
#include "../core/core.hpp"

#include "../infra/panic.hpp"

struct PrintResult
{
	s64 written;

	const Code2Op* next;
};

template<typename T>
static const T* attachment_of(const Code2Op* op) noexcept
{
	return reinterpret_cast<const T*>(op);
}

static PrintResult print_one_code2(PrintSink sink, CoreData* core, const Code2Op* op) noexcept
{
	const char8* const op_name = tag_name(*op);

	const Code2Id id = id_from_code2(core, op);

	s64 total_written = print(sink, "  %[> 6] %", static_cast<u32>(id), op_name);

	if (total_written < 0)
		return { -1, nullptr };

	switch (*op)
	{
	case Code2Op::End:
	{
		s64 written = print(sink, "\n");

		if (written < 0)
			return { -1, nullptr };

		return { total_written, nullptr };
	}

	case Code2Op::ScopeBegin:
	{
		const Code2AttScopeBegin* const attach = attachment_of<Code2AttScopeBegin>(op);

		s64 written = print(sink, " definitions=% end=%\n", attach->definition_count, static_cast<u32>(code2_id_of(attach->scope_end)));

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::ScopeEnd:
	{
		const Code2AttScopeEnd* const attach = attachment_of<Code2AttScopeEnd>(op);

		s64 written = print(sink, " begin=%\n", static_cast<u32>(code2_id_of(attach->scope_begin)));

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::Builtin:
	{
		const Code2AttBuiltin* const attach = attachment_of<Code2AttBuiltin>(op);

		s64 written = print(sink, " builtin=%\n", tag_name(attach->builtin));

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::Composite:
	{
		const Code2AttComposite* const attach = attachment_of<Code2AttComposite>(op);

		s64 written = print(sink, " members=%\n", attach->member_count);

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		for (u16 i = 0; i != attach->member_count; ++i)
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

		return { total_written, reinterpret_cast<const Code2Op*>(attach->members + attach->member_count) };
	}

	case Code2Op::Array:
	{
		const Code2AttArray* const attach = attachment_of<Code2AttArray>(op);

		s64 written = print(sink, " elements=%\n", attach->element_count);

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		for (u16 i = 0; i != attach->element_count; ++i)
		{
			const Code2AttArray_Element* const member = attach->elements + i;

			written = print(sink, "            [%] value=%\n", i, static_cast<u32>(member->value));

			if (written < 0)
			return { -1, nullptr };

			total_written += written;
		}

		return { total_written, reinterpret_cast<const Code2Op*>(attach->elements + attach->element_count) };
	}

	case Code2Op::Definition:
	{
		const Code2AttDefinition* const attach = attachment_of<Code2AttDefinition>(op);

		s64 written = code2_id_of(attach->opt_type) != Code2Id::INVALID
			? print(sink, " type=%", static_cast<u32>(code2_id_of(attach->opt_type)))
			: print(sink, " type=<none>");

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		written = code2_id_of(attach->opt_value) != Code2Id::INVALID
			? print(sink, " value=%\n", static_cast<u32>(code2_id_of(attach->opt_value)))
			: print(sink, " value=<none>\n");

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::DefineLocalTyped:
	{
		const Code2AttDefineLocalTyped* const attach = attachment_of<Code2AttDefineLocalTyped>(op);

		s64 written = print(sink, " scope=% index=% type=% value=% mut=% selfref=%\n",
			static_cast<u32>(code2_id_of(attach->scope_begin)),
			attach->definition_index,
			static_cast<u32>(attach->type),
			static_cast<u32>(code2_id_of(attach->value)),
			code2_with_of(attach->value).is_mut,
			code2_with_of(attach->value).has_self_reference
		);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::DefineLocalUntyped:
	{
		const Code2AttDefineLocalUntyped* const attach = attachment_of<Code2AttDefineLocalUntyped>(op);

		s64 written = print(sink, " scope=% index=% value=% mut=% selfref=%\n",
			static_cast<u32>(code2_id_of(attach->scope_begin)),
			attach->definition_index,
			static_cast<u32>(code2_id_of(attach->value)),
			code2_with_of(attach->value).is_mut,
			code2_with_of(attach->value).has_self_reference
		);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::ConsumeVoid:
	case Code2Op::Return:
	case Code2Op::Eval:
	case Code2Op::Defer:
	case Code2Op::Distinct:
	case Code2Op::Address:
	case Code2Op::Dereference:
	case Code2Op::BitNot:
	case Code2Op::LogicalNot:
	case Code2Op::Negate:
	case Code2Op::UnaryPlus:
	case Code2Op::LoadLocation:
	{
		const Code2AttUnaryOp* const attach = attachment_of<Code2AttUnaryOp>(op);

		s64 written = print(sink, " operand=%\n", static_cast<u32>(code2_id_of(attach->operand)));

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::Void:
	case Code2Op::Self:
	case Code2Op::Unreachable:
	case Code2Op::Undefined:
	{
		const Code2AttNonaryOp* const attach = attachment_of<Code2AttNonaryOp>(op);

		s64 written = print(sink, "\n");

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::If:
	{
		const Code2AttIf* const attach = attachment_of<Code2AttIf>(op);

		s64 written = print(sink, " condition=% consequent=@% alternative=@%\n", static_cast<u32>(code2_id_of(attach->condition)), static_cast<u32>(attach->consequent), static_cast<u32>(attach->alternative));

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::Loop:
	{
		const Code2AttLoop* const attach = attachment_of<Code2AttLoop>(op);

		s64 written = print(sink, " condition=@% body=@% finally=@%\n", static_cast<u32>(attach->condition), static_cast<u32>(attach->body), static_cast<u32>(attach->finally));

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::Func:
	{
		const Code2AttFunc* const attach = attachment_of<Code2AttFunc>(op);

		s64 written = print(sink, "signature=% body=@%\n", static_cast<u32>(code2_id_of(attach->signature)), static_cast<u32>(attach->body));

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::SignatureBegin:
	{
		const Code2AttSignatureBegin* const attach = attachment_of<Code2AttSignatureBegin>(op);

		s64 written = print(sink, " kind=% parameters=%\n",
			attach->is_proc ? range::from_literal_string("proc") : range::from_literal_string("func"),
			attach->parameter_count
		);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::SignatureEnd:
	{
		const Code2AttSignatureEnd* const attach = attachment_of<Code2AttSignatureEnd>(op);

		s64 written = print(sink, " signature=% rettype=%\n",
			static_cast<u32>(code2_id_of(attach->signature)),
			static_cast<u32>(attach->return_value)
		);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::Parameter:
	{
		const Code2AttParameter* const attach = attachment_of<Code2AttParameter>(op);

		const Range<char8> name = identifier_name_from_id(core, attach->name);

		s64 written = print(sink, " signature=% name=% mut=% eval=% templated=% variadic=% type=%",
			static_cast<u32>(code2_id_of(attach->signature)),
			name,
			code2_with_of(attach->type).is_mut,
			code2_with_of(attach->type).is_eval,
			code2_with_of(attach->type).is_templated,
			code2_with_of(attach->type).is_variadic,
			static_cast<u32>(code2_id_of(attach->type))
		);

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		written = is_some(attach->default_value)
			? print(sink, " value=%\n", static_cast<u32>(get(attach->default_value)))
			: print(sink, " value=<none>\n");

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::Trait:
	{
		const Code2AttTrait* const attach = attachment_of<Code2AttTrait>(op);

		s64 written = print(sink, " parameters=% members=%\n", attach->parameter_count, attach->member_count);

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		for (u8 i = 0; i != attach->parameter_count; ++i)
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

		const Code2AttTrait_Member* const members = reinterpret_cast<const Code2AttTrait_Member*>(attach->parameters + attach->parameter_count);

		for (u16 i = 0; i != attach->member_count; ++i)
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

		return { total_written, reinterpret_cast<const Code2Op*>(members + attach->member_count) };
	}

	case Code2Op::Impl:
	{
		const Code2AttImpl* const attach = attachment_of<Code2AttImpl>(op);

		s64 written = print(sink, " base=% traitcall=% members=%\n", static_cast<u32>(code2_id_of(attach->base)), static_cast<u32>(attach->trait_call), attach->member_count);

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		for (u16 i = 0; i != attach->member_count; ++i)
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

		return { total_written, reinterpret_cast<const Code2Op*>(attach->members + attach->member_count) };
	}

	case Code2Op::ImplTraitCall:
	case Code2Op::Call:
	{
		const Code2AttCall* const attach = attachment_of<Code2AttCall>(op);

		s64 written = print(sink, " callee=% arguments=%\n", static_cast<u32>(attach->callee), attach->argument_count);

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		for (u8 i = 0; i != attach->argument_count; ++i)
		{
			const Range<char8> name = is_some(attach->arguments[i].name)
				? identifier_name_from_id(core, get(attach->arguments[i].name))
				: range::from_literal_string("<none>");

			written = print(sink, "            [%] name=% value=@%\n", i, name, static_cast<u32>(attach->arguments[i].value));

			if (written < 0)
				return { -1, nullptr };

			total_written += written;
		}

		return { total_written, reinterpret_cast<const Code2Op*>(attach->arguments + attach->argument_count) };
	}

	case Code2Op::TemplatedParamValue:
	case Code2Op::TemplatedParamLocation:
	{
		const Code2AttTemplatedParamRef* const attach = attachment_of<Code2AttTemplatedParamRef>(op);

		s64 written = print(sink, " rank=%\n", attach->rank);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::LocalValue:
	case Code2Op::LocalLocation:
	{
		const Code2AttLocalRef* const attach = attachment_of<Code2AttLocalRef>(op);

		s64 written = print(sink, " out=% rank=%\n", attach->out, attach->rank);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::GlobalValue:
	case Code2Op::GlobalLocation:
	{
		const Code2AttGlobalRef* const attach = attachment_of<Code2AttGlobalRef>(op);

		s64 written = print(sink, " rank=%\n", attach->rank);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::ClosedValue:
	case Code2Op::ClosedLocation:
	{
		const Code2AttClosedRef* const attach = attachment_of<Code2AttClosedRef>(op);

		s64 written = print(sink, " rank=%\n", attach->rank);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::TraitArgumentValue:
	case Code2Op::TraitArgumentLocation:
	{
		const Code2AttTraitArgumentRef* const attach = attachment_of<Code2AttTraitArgumentRef>(op);

		s64 written = print(sink, " rank=%\n", attach->rank);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::Slice:
	{
		const Code2AttSlice* const attach = attachment_of<Code2AttSlice>(op);

		s64 written = print(sink, " sliced=%", static_cast<u32>(code2_id_of(attach->sliced)));

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

		return { total_written + written, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::Integer16:
	{
		const Code2AttInteger16* const attach = attachment_of<Code2AttInteger16>(op);

		s64 written = print(sink, " value=%\n", attach->value);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::Integer64:
	{
		const Code2AttInteger64* const attach = attachment_of<Code2AttInteger64>(op);

		s64 written = print(sink, " value=%\n", attach->value_lo | (static_cast<u64>(attach->value_hi) << 32));

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::IntegerHuge:
	{
		TODO("Implement `print_one_code2(IntegerHuge)`.");
	}

	case Code2Op::Float:
	{
		const Code2AttFloat* const attach = attachment_of<Code2AttFloat>(op);

		CompFloatValue value;
		memcpy(&value, &attach->lo, sizeof(CompFloatValue));

		s64 written = print(sink, " value=%\n", value.rep);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::String:
	{
		const Code2AttString* const attach = attachment_of<Code2AttString>(op);

		const char8* const begin = static_cast<char8*>(address_from_core_id(core, attach->data));

		const Range<char8> value{ begin, attach->size };

		s64 written = print(sink, " length=% value=\"%\"\n", attach->size, value);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::TypeTailArray:
	case Code2Op::TypeSlice:
	case Code2Op::TypePtr:
	{
		const Code2AttReferenceType* const attach = attachment_of<Code2AttReferenceType>(op);

		s64 written = print(sink, " mut=% opt=% multi=% operand=%\n", attach->is_mut, attach->is_opt, attach->is_multi, static_cast<u32>(code2_id_of(attach->lhs)));

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

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
	case Code2Op::Set:
	{
		const Code2AttBinaryOp* const attach = attachment_of<Code2AttBinaryOp>(op);

		s64 written = print(sink, " lhs=% rhs=%\n", static_cast<u32>(code2_id_of(attach->lhs)), static_cast<u32>(attach->rhs));

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::LogicalAnd:
	case Code2Op::LogicalOr:
	{
		const Code2AttLogicalOp* const attach = attachment_of<Code2AttLogicalOp>(op);

		s64 written = print(sink, " lhs=% rhs=@%\n", static_cast<u32>(code2_id_of(attach->lhs)), static_cast<u32>(attach->rhs));

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::MemberValue:
	case Code2Op::MemberLocation:
	{
		const Code2AttMember* const attach = attachment_of<Code2AttMember>(op);

		const Range<char8> name = identifier_name_from_id(core, attach->name);

		s64 written = print(sink, " operand=% name=%\n", static_cast<u32>(code2_id_of(attach->composite)), name);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::Compare:
	{
		const Code2AttCompare* const attach = attachment_of<Code2AttCompare>(op);

		const char8* const name = tag_name(code2_with_of(attach->rhs));

		s64 written = print(sink, " kind=% lhs=% rhs=%\n", name, static_cast<u32>(code2_id_of(attach->lhs)), static_cast<u32>(code2_id_of(attach->rhs)));

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::INVALID:
		; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

static PrintResult print_one_code2_ref(PrintSink sink, CoreData* core, const Code2Op* op) noexcept
{
	switch (*op)
	{
	case Code2Op::End:
	{
		return { 0, nullptr };
	}

	case Code2Op::ScopeBegin:
	{
		const Code2AttScopeBegin* const attach = attachment_of<Code2AttScopeBegin>(op);

		return { 0, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::ScopeEnd:
	{
		const Code2AttScopeEnd* const attach = attachment_of<Code2AttScopeEnd>(op);

		return { 0, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::Builtin:
	{
		const Code2AttBuiltin* const attach = attachment_of<Code2AttBuiltin>(op);

		return { 0, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::Composite:
	{
		const Code2AttComposite* const attach = attachment_of<Code2AttComposite>(op);

		return { 0, reinterpret_cast<const Code2Op*>(attach->members + attach->member_count) };
	}

	case Code2Op::Array:
	{
		const Code2AttArray* const attach = attachment_of<Code2AttArray>(op);

		return { 0, reinterpret_cast<const Code2Op*>(attach->elements + attach->element_count) };
	}

	case Code2Op::Definition:
	{
		const Code2AttDefinition* const attach = attachment_of<Code2AttDefinition>(op);

		return { 0, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::DefineLocalTyped:
	{
		const Code2AttDefineLocalTyped* const attach = attachment_of<Code2AttDefineLocalTyped>(op);

		return { 0, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::DefineLocalUntyped:
	{
		const Code2AttDefineLocalUntyped* const attach = attachment_of<Code2AttDefineLocalUntyped>(op);

		return { 0, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::ConsumeVoid:
	case Code2Op::Return:
	case Code2Op::Eval:
	case Code2Op::Defer:
	case Code2Op::Distinct:
	case Code2Op::Address:
	case Code2Op::Dereference:
	case Code2Op::BitNot:
	case Code2Op::LogicalNot:
	case Code2Op::Negate:
	case Code2Op::UnaryPlus:
	case Code2Op::LoadLocation:
	{
		const Code2AttUnaryOp* const attach = attachment_of<Code2AttUnaryOp>(op);

		return { 0, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::Void:
	case Code2Op::Self:
	case Code2Op::Unreachable:
	case Code2Op::Undefined:
	{
		const Code2AttNonaryOp* const attach = attachment_of<Code2AttNonaryOp>(op);

		return { 0, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::If:
	{
		s64 total_written = 0;

		const Code2AttIf* const attach = attachment_of<Code2AttIf>(op);

		s64 written = print(sink, "\n");

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		const Code2Op* const consequent = code2_from_id(core, attach->consequent);

		written = diag::print_code2(sink, core, consequent);

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		written = print(sink, "\n");

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		const Code2Op* const alternative = code2_from_id(core, attach->alternative);

		written = diag::print_code2(sink, core, alternative);

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		return { total_written, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::Loop:
	{
		s64 total_written = 0;

		const Code2AttLoop* const attach = attachment_of<Code2AttLoop>(op);

		s64 written = print(sink, "\n");

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		const Code2Op* const condition = code2_from_id(core, attach->condition);

		written = diag::print_code2(sink, core, condition);

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		const Code2Op* const body = code2_from_id(core, attach->body);

		written = diag::print_code2(sink, core, body);

		if (written < 0)
			return { -1, nullptr };

		written = print(sink, "\n");

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		const Code2Op* const finally = code2_from_id(core, attach->finally);

		written = diag::print_code2(sink, core, finally);

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		return { total_written, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::Func:
	{
		const Code2AttFunc* const attach = attachment_of<Code2AttFunc>(op);

		const s64 newline_written = print(sink, "\n");

		if (newline_written < 0)
			return { -1, nullptr };

		const Code2Op* const body = code2_from_id(core, attach->body);

		const s64 body_written = diag::print_code2(sink, core, body);

		if (body_written < 0)
			return { -1, nullptr };

		return { newline_written + body_written, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::SignatureBegin:
	{
		const Code2AttSignatureBegin* const attach = attachment_of<Code2AttSignatureBegin>(op);

		return { 0, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::SignatureEnd:
	{
		const Code2AttSignatureEnd* const attach = attachment_of<Code2AttSignatureEnd>(op);

		return { 0, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::Parameter:
	{
		const Code2AttParameter* const attach = attachment_of<Code2AttParameter>(op);

		return { 0, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::Trait:
	{
		const Code2AttTrait* const attach = attachment_of<Code2AttTrait>(op);

		const Code2AttTrait_Member* const members = reinterpret_cast<const Code2AttTrait_Member*>(attach->parameters + attach->parameter_count);

		s64 total_written = 0;

		for (u16 i = 0; i != attach->member_count; ++i)
		{
			const s64 type_newline_written = print(sink, "\n");

			if (type_newline_written < 0)
				return { -1, nullptr };

			total_written += type_newline_written;

			const Code2Op* const type = code2_from_id(core, members[i].type);

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

				const Code2Op* const default_value = code2_from_id(core, get(members[i].default_value));

				const s64 default_value_written = diag::print_code2(sink, core, default_value);

				if (default_value_written < 0)
					return { -1, nullptr };

				total_written += default_value_written;
			}
		}

		return { total_written, reinterpret_cast<const Code2Op*>(members + attach->member_count) };
	}

	case Code2Op::Impl:
	{
		const Code2AttImpl* const attach = attachment_of<Code2AttImpl>(op);

		s64 total_written = 0;

		for (u16 i = 0; i != attach->member_count; ++i)
		{
			if (is_some(attach->members[i].type))
			{
				const s64 type_newline_written = print(sink, "\n");

				if (type_newline_written < 0)
					return { -1, nullptr };

				total_written += type_newline_written;

				const Code2Op* const type = code2_from_id(core, get(attach->members[i].type));

				const s64 type_written = diag::print_code2(sink, core, type);

				if (type_written < 0)
					return { -1, nullptr };

				total_written += type_written;
			}

			const s64 value_newline_written = print(sink, "\n");

			if (value_newline_written < 0)
				return { -1, nullptr };

			total_written += value_newline_written;

			const Code2Op* const value = code2_from_id(core, attach->members[i].value);

			const s64 value_written = diag::print_code2(sink, core, value);

			if (value_written < 0)
				return { -1, nullptr };

			total_written += value_written;
		}

		return { total_written, reinterpret_cast<const Code2Op*>(attach->members + attach->member_count) };
	}

	case Code2Op::ImplTraitCall:
	case Code2Op::Call:
	{
		const Code2AttCall* const attach = attachment_of<Code2AttCall>(op);

		s64 total_written = 0;

		for (u16 i = 0; i != attach->argument_count; ++i)
		{
			const s64 newline_written = print(sink, "\n");

			if (newline_written < 0)
				return { -1, nullptr };

			total_written += newline_written;

			const Code2Op* const value = code2_from_id(core, attach->arguments[i].value);

			const s64 value_written = diag::print_code2(sink, core, value);

			if (value_written < 0)
				return { -1, nullptr };

			total_written += value_written;
		}

		return { total_written, reinterpret_cast<const Code2Op*>(attach->arguments + attach->argument_count) };
	}

	case Code2Op::TemplatedParamValue:
	case Code2Op::TemplatedParamLocation:
	{
		const Code2AttTemplatedParamRef* const attach = attachment_of<Code2AttTemplatedParamRef>(op);

		return { 0, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::LocalValue:
	case Code2Op::LocalLocation:
	{
		const Code2AttLocalRef* const attach = attachment_of<Code2AttLocalRef>(op);

		return { 0, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::GlobalValue:
	case Code2Op::GlobalLocation:
	{
		const Code2AttGlobalRef* const attach = attachment_of<Code2AttGlobalRef>(op);

		return { 0, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::ClosedValue:
	case Code2Op::ClosedLocation:
	{
		const Code2AttClosedRef* const attach = attachment_of<Code2AttClosedRef>(op);

		return { 0, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::TraitArgumentValue:
	case Code2Op::TraitArgumentLocation:
	{
		const Code2AttTraitArgumentRef* const attach = attachment_of<Code2AttTraitArgumentRef>(op);

		return { 0, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::Slice:
	{
		const Code2AttSlice* const attach = attachment_of<Code2AttSlice>(op);

		return { 0, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::Integer16:
	{
		const Code2AttInteger16* const attach = attachment_of<Code2AttInteger16>(op);

		return { 0, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::Integer64:
	{
		const Code2AttInteger64* const attach = attachment_of<Code2AttInteger64>(op);

		return { 0, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::IntegerHuge:
	{
		const Code2AttIntegerHuge* const attach = attachment_of<Code2AttIntegerHuge>(op);

		return { 0, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::Float:
	{
		const Code2AttFloat* const attach = attachment_of<Code2AttFloat>(op);

		return { 0, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::String:
	{
		const Code2AttString* const attach = attachment_of<Code2AttString>(op);

		return { 0, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::TypeTailArray:
	case Code2Op::TypeSlice:
	case Code2Op::TypePtr:
	{
		const Code2AttReferenceType* const attach = attachment_of<Code2AttReferenceType>(op);

		return { 0, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

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
	case Code2Op::Set:
	{
		const Code2AttBinaryOp* const attach = attachment_of<Code2AttBinaryOp>(op);

		return { 0, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::LogicalAnd:
	case Code2Op::LogicalOr:
	{
		const Code2AttLogicalOp* const attach = attachment_of<Code2AttLogicalOp>(op);

		const s64 newline_written = print(sink, "\n");

		if (newline_written < 0)
			return { -1, nullptr };

		const Code2Op* const rhs = code2_from_id(core, attach->rhs);

		const s64 rhs_written = diag::print_code2(sink, core, rhs);

		if (rhs_written < 0)
			return { -1, nullptr };

		return { newline_written + rhs_written, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::MemberValue:
	case Code2Op::MemberLocation:
	{
		const Code2AttMember* const attach = attachment_of<Code2AttMember>(op);

		return { 0, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::Compare:
	{
		const Code2AttCompare* const attach = attachment_of<Code2AttCompare>(op);

		return { 0, reinterpret_cast<const Code2Op*>(attach + 1) };
	}

	case Code2Op::INVALID:
		; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

s64 diag::print_code2(PrintSink sink, CoreData* core, const Code2Op* op) noexcept
{
	const Code2Op* const begin = op;

	s64 total_written = 0;

	PrintResult result;

	do
	{
		result = print_one_code2(sink, core, op);

		op = result.next;

		total_written += result.written;
	}
	while (op != nullptr);

	if (result.written < 0)
		return -1;

	op = begin;

	do
	{
		result = print_one_code2_ref(sink, core, op);

		op = result.next;

		total_written += result.written;
	}
	while (op != nullptr);
	
	if (result.written < 0)
		return -1;

	return total_written;
}
