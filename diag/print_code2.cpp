#include "diag.hpp"

#include "diag_helpers.hpp"

#include "../core/core.hpp"

#include "../infra/panic.hpp"
#include "../infra/math.hpp"
#include "../infra/print/insert_helpers.hpp"



static PrintInsert print_make_insert(Code2Id value) noexcept
{
	return create_id_print_insert(static_cast<u32>(value), range::from_literal_string("@"), range::from_literal_string(""));
}

static PrintInsert print_make_insert(Maybe<Code2Id> value) noexcept
{
	return print_make_insert(is_some(value) ? get(value) : Code2Id::INVALID);
}

static PrintInsert print_make_insert(Code2Offset value) noexcept
{
	return create_id_print_insert(static_cast<u32>(value), range::from_literal_string("$"), range::from_literal_string(""));
}

static PrintInsert print_make_insert(Maybe<Code2Offset> value) noexcept
{
	return print_make_insert(is_some(value) ? get(value) : Code2Offset::INVALID);
}



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
		const Code2AttLeave* const attachment = attachment_of<Code2AttLeave>(code);

		s64 written = print(sink, " value=% out=%\n",
			attachment->operand,
			code->leave.out
		);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attachment + 1) };
	}

	case Code2Op::Repeat:
	{
		s64 written = print(sink, " out=%\n", code->repeat.out);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, code + 1 };
	}

	case Code2Op::Builtin:
	{
		s64 written = print(sink, " builtin=%\n", tag_name(code->builtin.builtin));

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, code + 1 };
	}

	case Code2Op::Composite:
	{
		const Code2AttComposite* const attachment = attachment_of<Code2AttComposite>(code);

		s64 written = print(sink, " members=%\n", code->composite.member_count);

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		for (u16 i = 0; i != code->composite.member_count; ++i)
		{
			const Code2AttComposite_Member* const member = attachment->members + i;

			const Range<char8> name = is_some(member->name)
				? identifier_name_from_id(core, get(member->name))
				: range::from_literal_string("<none>");

			written = print(sink, "         %[> 5] - name=\"%\" (%) value=%\n",
				i,
				name,
				is_some(member->name) ? static_cast<u32>(get(member->name)) : 0,
				member->value
			);

			if (written < 0)
				return { -1, nullptr };

			total_written += written;
		}

		return { total_written, reinterpret_cast<const Code2*>(attachment->members + code->composite.member_count) };
	}

	case Code2Op::Array:
	{
		const Code2AttArray* const attachment = attachment_of<Code2AttArray>(code);

		s64 written = print(sink, " elements=%\n", code->array.element_count);

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		for (u16 i = 0; i != code->array.element_count; ++i)
		{
			const Code2AttArray_Element* const member = attachment->elements + i;

			written = print(sink, "         %[> 5] - value=%\n", i, member->value);

			if (written < 0)
				return { -1, nullptr };

			total_written += written;
		}

		return { total_written, reinterpret_cast<const Code2*>(attachment->elements + code->array.element_count) };
	}

	case Code2Op::Definition:
	{
		const Code2AttDefinition* const attachment = attachment_of<Code2AttDefinition>(code);

		const s64 written = print(sink, "name=\"%\" (%) type=% value=% pub=% mut=% selfref=%\n",
			identifier_name_from_id(core, attachment->name),
			static_cast<u32>(attachment->name),
			attachment->type,
			attachment->value,
			attachment->header.definition.is_pub,
			attachment->header.definition.is_mut,
			attachment->header.definition.has_self_reference
		);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attachment + 1) };
	}

	case Code2Op::MakeLocation:
	{
		const Code2AttUnaryOp* const attachment = attachment_of<Code2AttUnaryOp>(code);

		s64 written = print(sink, " mut=% value=%\n",
			code->make_location.is_mut,
			attachment->operand
		);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attachment + 1) };
	}

	case Code2Op::DefineUntyped:
	{
		const Code2AttUnaryOp* const attachment = attachment_of<Code2AttUnaryOp>(code);

		const s64 written = print(sink, " value=% index=% mut=% selfref=%\n",
			attachment->operand,
			code->define.definition_index,
			code->define.is_mut,
			code->define.has_self_reference
		);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attachment + 1) };
	}

	case Code2Op::DefineTyped:
	{
		const Code2AttBinaryOp* const attachment = attachment_of<Code2AttBinaryOp>(code);

		const s64 written = print(sink, " type=% value=% index=% mut=% selfref=%\n",
			attachment->lhs,
			attachment->rhs,
			code->define.definition_index,
			code->define.is_mut,
			code->define.has_self_reference
		);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attachment + 1) };
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
		const Code2AttUnaryOp* const attachment = attachment_of<Code2AttUnaryOp>(code);

		s64 written = print(sink, " operand=%\n", attachment->operand);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attachment + 1) };
	}

	case Code2Op::Void:
	case Code2Op::Self:
	case Code2Op::Unreachable:
	case Code2Op::Undefined:
	{
		s64 written = print(sink, "\n");

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, code + 1 };
	}

	case Code2Op::If:
	{
		const Code2AttIf* const attachment = attachment_of<Code2AttIf>(code);

		s64 written = print(sink, " condition=% consequent=% alternative=%\n",
			attachment->condition,
			attachment->consequent,
			attachment->alternative
		);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attachment + 1) };
	}

	case Code2Op::Jump:
	case Code2Op::Loop:
	{
		const Code2AttJump* const attachment = attachment_of<Code2AttJump>(code);

		s64 written = print(sink, " body=%\n", attachment->body);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attachment + 1) };
	}

	case Code2Op::Func:
	{
		const Code2AttFunc* const attachment = attachment_of<Code2AttFunc>(code);

		s64 written = print(sink, " signature=% body=%\n",
			attachment->signature,
			attachment->body
		);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attachment + 1) };
	}

	case Code2Op::Signature:
	{
		const Code2AttSignature* const attachment = attachment_of<Code2AttSignature>(code);

		s64 written = print(sink, " kind=% parameters=% return_type=",
			code->signature.is_func ? range::from_literal_string("func") : range::from_literal_string("proc"),
			code->signature.parameter_count
		);

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		written = code->signature.has_templated_return_type
			? print(sink, "%\n", attachment->return_type.templated)
			: print(sink, "%\n", attachment->return_type.complete);

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		const u32 parameter_count = code->signature.parameter_count;

		for (u32 i = 0; i != parameter_count; ++i)
		{
			const Code2AttSignature_Parameter* const parameter = attachment->parameters + i;

			const Range<char8> name = identifier_name_from_id(core, parameter->name);

			written = print(sink, "            %[> 2] - name=\"%\" (%) mut=% eval=% template=%",
				i,
				name,
				static_cast<u32>(parameter->name),
				parameter->is_mut,
				parameter->is_eval,
				parameter->is_templated
			);

			if (written < 0)
				return { -1, nullptr };

			total_written += written;

			if (parameter->is_templated)
			{
				written = print(sink, " type=% default=%\n",
					parameter->templated.type,
					parameter->templated.default_value
				);

				if (written < 0)
					return { -1, nullptr };

				total_written += written;
			}
			else
			{
				written = print(sink, " type=% default=%\n",
					parameter->complete.type,
					parameter->complete.default_value
				);

				if (written < 0)
					return { -1, nullptr };

				total_written += written;
			}
		}

		return { total_written, reinterpret_cast<const Code2*>(attachment->parameters + parameter_count) };
	}

	case Code2Op::Trait:
	{
		const Code2AttTrait* const attachment = attachment_of<Code2AttTrait>(code);

		s64 written = print(sink, " parameters=% members=%\n", code->trait.parameter_count, code->trait.member_count);

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		for (u8 i = 0; i != code->trait.parameter_count; ++i)
		{
			const Range<char8> name = identifier_name_from_id(core, attachment->parameters[i]);

			written = print(sink, "            [%] name=%\n", i, name);

			if (written < 0)
				return { -1, nullptr };

			total_written += written;
		}

		written = print(sink, "\n");

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		const Code2AttTrait_Member* const members = reinterpret_cast<const Code2AttTrait_Member*>(attachment->parameters + code->trait.parameter_count);

		for (u16 i = 0; i != code->trait.member_count; ++i)
		{
			const Range<char8> name = identifier_name_from_id(core, members[i].name);

			written = print(sink, "            [%] name=\"%\" (%) pub=% mut=% type=% default=%",
				i,
				name,
				static_cast<u32>(members[i].name),
				members[i].is_pub,
				members[i].is_mut,
				members[i].type,
				members[i].default_value
			);

			if (written < 0)
				return { -1, nullptr };

			total_written += written;
		}

		return { total_written, reinterpret_cast<const Code2*>(members + code->trait.member_count) };
	}

	case Code2Op::Impl:
	{
		const Code2AttImpl* const attachment = attachment_of<Code2AttImpl>(code);

		s64 written = print(sink, " base=% traitcall=% members=%\n",
			attachment->base_type,
			attachment->trait_call,
			code->impl.member_count
		);

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		for (u16 i = 0; i != code->impl.member_count; ++i)
		{
			const Range<char8> name = identifier_name_from_id(core, attachment->members[i].name);

			written = print(sink, "         [>5 %] name=\"%\" (%) type=% value=% mut=% pub=%\n",
				i,
				name,
				static_cast<u32>(attachment->members[i].name),
				attachment->members[i].type,
				attachment->members[i].value,
				attachment->members[i].is_mut,
				attachment->members[i].is_pub
			);

			if (written < 0)
				return { -1, nullptr };

			total_written += written;
		}

		return { total_written, reinterpret_cast<const Code2*>(attachment->members + code->impl.member_count) };
	}

	case Code2Op::ImplTraitCall:
	case Code2Op::Call:
	{
		const Code2AttCall* const attachment = attachment_of<Code2AttCall>(code);

		s64 written = print(sink, " callee=% arguments=%\n", static_cast<u32>(attachment->callee), code->call.argument_count);

		if (written < 0)
			return { -1, nullptr };

		total_written += written;

		for (u8 i = 0; i != code->call.argument_count; ++i)
		{
			const Range<char8> name = is_some(attachment->arguments[i].name)
				? identifier_name_from_id(core, get(attachment->arguments[i].name))
				: range::from_literal_string("<none>");

			written = print(sink, "            %[> 2] - name=\"%\" (%) value=%\n",
				i,
				name,
				is_some(attachment->arguments[i].name) ? static_cast<u32>(get(attachment->arguments[i].name)) : 0,
				attachment->arguments[i].value
			);

			if (written < 0)
				return { -1, nullptr };

			total_written += written;
		}

		return { total_written, reinterpret_cast<const Code2*>(attachment->arguments + code->call.argument_count) };
	}

	case Code2Op::LocalValue:
	case Code2Op::LocalLocation:
	{
		s64 written = print(sink, " out=% rank=%\n", code->local_ref.out, code->local_ref.rank);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, code + 1 };
	}

	case Code2Op::GlobalValue:
	case Code2Op::GlobalLocation:
	{
		s64 written = print(sink, " rank=%\n", code->global_ref.rank);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, code + 1 };
	}

	case Code2Op::ClosedValue:
	case Code2Op::ClosedLocation:
	{
		const s64 written = print(sink, " rank=%\n", code->closed_ref.rank);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, code + 1 };
	}

	case Code2Op::TraitArgumentValue:
	case Code2Op::TraitArgumentLocation:
	{
		const s64 written = print(sink, " rank=%\n", code->trait_argument_ref.rank);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, code + 1 };
	}

	case Code2Op::Slice:
	{
		const Code2AttSlice* const attachment = attachment_of<Code2AttSlice>(code);

		const s64 written = print(sink, " sliced=% begin=% end=%\n",
			attachment->sliced,
			attachment->begin,
			attachment->end
		);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attachment + 1) };
	}

	case Code2Op::Integer16:
	{
		s64 written = print(sink, " value=%\n", code->integer16.value);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, code + 1 };
	}

	case Code2Op::Integer64:
	{
		const Code2AttInteger64* const attachment = attachment_of<Code2AttInteger64>(code);

		CompIntegerValue value;

		memcpy(&value, &attachment->value_lo, sizeof(CompIntegerValue));

		s64 written;
		
		u64 raw_value;
		
		if (u64_from_comp_integer(value, 64, &raw_value))
			written = print(sink, " value=%\n", raw_value);
		else
			written = print(sink, " value=<huge>\n");

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attachment + 1) };
	}

	case Code2Op::Float:
	{
		const Code2AttFloat* const attachment = attachment_of<Code2AttFloat>(code);

		CompFloatValue value;
		memcpy(&value, &attachment->value_lo, sizeof(CompFloatValue));

		s64 written = print(sink, " value=%\n", f64_from_comp_float(value));

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attachment + 1) };
	}

	case Code2Op::String:
	{
		const Code2AttString* const attachment = attachment_of<Code2AttString>(code);

		const char8* const begin = static_cast<char8*>(address_from_core_id(core, attachment->data));

		const Range<char8> value{ begin, attachment->size };

		s64 written = print(sink, " length=% value=\"%\"\n", attachment->size, value);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attachment + 1) };
	}

	case Code2Op::TypeTailArray:
	case Code2Op::TypeSlice:
	case Code2Op::TypePtr:
	{
		const Code2AttUnaryOp* const attachment = attachment_of<Code2AttUnaryOp>(code);

		s64 written = print(sink, " operand=% mut=% opt=% multi=%\n",
			attachment->operand,
			code->reference_type.is_mut,
			code->reference_type.is_opt,
			code->reference_type.is_multi
		);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attachment + 1) };
	}

	case Code2Op::BinaryArithmetic:
	{
		const Code2AttBinaryOp* const attachment = attachment_of<Code2AttBinaryOp>(code);

		s64 written = print(sink, " kind=% lhs=% rhs=%\n",
			tag_name(code->binary_arithmetic.kind),
			attachment->lhs,
			attachment->rhs
		);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attachment + 1) };
	}

	case Code2Op::BitShift:
	{
		const Code2AttBinaryOp* const attachment = attachment_of<Code2AttBinaryOp>(code);

		s64 written = print(sink, " kind=% lhs=% rhs=%\n",
			tag_name(code->bit_shift.kind),
			attachment->lhs,
			attachment->rhs
		);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attachment + 1) };
	}

	case Code2Op::TypeArray:
	case Code2Op::Index:
	case Code2Op::Store:
	{
		const Code2AttBinaryOp* const attachment = attachment_of<Code2AttBinaryOp>(code);

		s64 written = print(sink, " lhs=% rhs=%\n",
			attachment->lhs,
			attachment->rhs
		);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attachment + 1) };
	}

	case Code2Op::LogicalAnd:
	case Code2Op::LogicalOr:
	{
		const Code2AttLogicalOp* const attachment = attachment_of<Code2AttLogicalOp>(code);

		s64 written = print(sink, " lhs=% rhs=@%\n",
			attachment->lhs,
			attachment->rhs
		);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attachment + 1) };
	}

	case Code2Op::MemberValue:
	case Code2Op::MemberLocation:
	{
		const Code2AttMember* const attachment = attachment_of<Code2AttMember>(code);

		const Range<char8> name = identifier_name_from_id(core, attachment->name);

		const s64 written = print(sink, " name=\"%\" (%) operand=%\n",
			name,
			static_cast<u32>(attachment->name),
			attachment->composite.offset
		);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attachment + 1) };
	}

	case Code2Op::Compare:
	{
		const Code2AttBinaryOp* const attachment = attachment_of<Code2AttBinaryOp>(code);

		const s64 written = print(sink, " kind=% lhs=% rhs=%\n",
			tag_name(code->compare.kind),
			attachment->lhs,
			attachment->rhs
		);

		if (written < 0)
			return { -1, nullptr };

		return { total_written + written, reinterpret_cast<const Code2*>(attachment + 1) };
	}

	case Code2Op::INVALID:
		; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

static s64 print_code2_block(PrintSink sink, CoreData* core, Code2Id block_id) noexcept
{
	const Code2Block* const block = code2_block_from_id(core, block_id);

	return diag::print_code2(sink, core, block);
}

static PrintResult print_one_code2_ref(PrintSink sink, CoreData* core, const Code2* code) noexcept
{
	switch (code->op)
	{
	case Code2Op::Leave:
	{
		const Code2AttLeave* const attachment = attachment_of<Code2AttLeave>(code);

		return { 0, reinterpret_cast<const Code2*>(attachment + 1) };
	}

	case Code2Op::Repeat:
	case Code2Op::Void:
	case Code2Op::Self:
	case Code2Op::Unreachable:
	case Code2Op::Undefined:
	case Code2Op::LocalValue:
	case Code2Op::LocalLocation:
	case Code2Op::GlobalValue:
	case Code2Op::GlobalLocation:
	case Code2Op::ClosedValue:
	case Code2Op::ClosedLocation:
	case Code2Op::TraitArgumentValue:
	case Code2Op::TraitArgumentLocation:
	case Code2Op::Integer16:
	{
		return { 0, code + 1 };
	}

	case Code2Op::Builtin:
	{
		const Code2AttBuiltin* const attachment = attachment_of<Code2AttBuiltin>(code);

		return { 0, reinterpret_cast<const Code2*>(attachment + 1) };
	}

	case Code2Op::Composite:
	{
		const Code2AttComposite* const attachment = attachment_of<Code2AttComposite>(code);

		return { 0, reinterpret_cast<const Code2*>(attachment->members + code->composite.member_count) };
	}

	case Code2Op::Array:
	{
		const Code2AttArray* const attachment = attachment_of<Code2AttArray>(code);

		return { 0, reinterpret_cast<const Code2*>(attachment->elements + code->array.element_count) };
	}

	case Code2Op::Definition:
	{
		const Code2AttDefinition* const attachment = attachment_of<Code2AttDefinition>(code);

		return { 0, reinterpret_cast<const Code2*>(attachment + 1) };
	}

	case Code2Op::MakeLocation:
	case Code2Op::DefineUntyped:
	case Code2Op::ConsumeVoid:
	case Code2Op::TypeTailArray:
	case Code2Op::TypeSlice:
	case Code2Op::TypePtr:
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
		const Code2AttUnaryOp* const attachment = attachment_of<Code2AttUnaryOp>(code);

		return { 0, reinterpret_cast<const Code2*>(attachment + 1) };
	}

	case Code2Op::If:
	{
		const Code2AttIf* const attachment = attachment_of<Code2AttIf>(code);

		const s64 consequent_written = print_code2_block(sink, core, attachment->consequent);

		if (consequent_written < 0)
			return { -1, nullptr };

		const s64 alternative_written = print_code2_block(sink, core, attachment->alternative);

		if (alternative_written < 0)
			return { -1, nullptr };

		return { consequent_written + alternative_written, reinterpret_cast<const Code2*>(attachment + 1) };
	}

	case Code2Op::Jump:
	case Code2Op::Loop:
	{
		const Code2AttJump* const attachment = attachment_of<Code2AttJump>(code);

		const s64 written = print_code2_block(sink, core, attachment->body);

		if (written < 0)
			return { -1, nullptr };

		return { written, reinterpret_cast<const Code2*>(attachment + 1) };
	}

	case Code2Op::Func:
	{
		const Code2AttFunc* const attachment = attachment_of<Code2AttFunc>(code);

		const s64 written = print_code2_block(sink, core, attachment->body);

		if (written < 0)
			return { -1, nullptr };

		return { written, reinterpret_cast<const Code2*>(attachment + 1) };
	}

	case Code2Op::Signature:
	{
		const Code2AttSignature* const attachment = attachment_of<Code2AttSignature>(code);

		const u32 parameter_count = code->signature.parameter_count;

		s64 total_written = 0;

		for (u32 i = 0; i != parameter_count; ++i)
		{
			const Code2AttSignature_Parameter* const parameter = attachment->parameters + i;

			if (!parameter->is_templated)
				continue;

			const s64 type_written = print_code2_block(sink, core, parameter->templated.type);

			if (type_written < 0)
				return { -1, nullptr };

			total_written += type_written;

			if (is_none(parameter->templated.default_value))
				continue;

			const s64 default_value_written = print_code2_block(sink, core, get(parameter->templated.default_value));

			if (default_value_written < 0)
				return { -1, nullptr };

			total_written += default_value_written;
		}

		if (code->signature.has_templated_return_type)
		{
			const s64 written = print_code2_block(sink, core, attachment->return_type.templated);

			if (written < 0)
				return { -1, nullptr };

			total_written += written;
		}

		return { total_written, reinterpret_cast<const Code2*>(attachment->parameters + code->signature.parameter_count) };
	}

	case Code2Op::Trait:
	{
		const Code2AttTrait* const attachment = attachment_of<Code2AttTrait>(code);

		const Code2AttTrait_Member* const members = reinterpret_cast<const Code2AttTrait_Member*>(attachment->parameters + code->trait.parameter_count);

		s64 total_written = 0;

		for (u16 i = 0; i != code->trait.member_count; ++i)
		{
			const Code2AttTrait_Member* const member = members + i;

			const s64 type_written = print_code2_block(sink, core, member->type);

			if (type_written < 0)
				return { -1, nullptr };

			total_written += type_written;

			if (is_none(members[i].default_value))
				continue;

			const s64 default_value_written = print_code2_block(sink, core, get(member->default_value));

			if (default_value_written < 0)
				return { -1, nullptr };

			total_written += default_value_written;
		}

		return { total_written, reinterpret_cast<const Code2*>(members + code->trait.member_count) };
	}

	case Code2Op::Impl:
	{
		const Code2AttImpl* const attachment = attachment_of<Code2AttImpl>(code);

		s64 total_written = 0;

		for (u16 i = 0; i != code->impl.member_count; ++i)
		{
			const Code2AttImpl_Member* const member = attachment->members + i;

			if (is_some(member->type))
			{
				const s64 type_written = print_code2_block(sink, core, get(member->type));

				if (type_written < 0)
					return { -1, nullptr };

				total_written += type_written;
			}

			const s64 value_written = print_code2_block(sink, core, member->value);

			if (value_written < 0)
				return { -1, nullptr };

			total_written += value_written;
		}

		return { total_written, reinterpret_cast<const Code2*>(attachment->members + code->impl.member_count) };
	}

	case Code2Op::ImplTraitCall:
	case Code2Op::Call:
	{
		const Code2AttCall* const attachment = attachment_of<Code2AttCall>(code);

		s64 total_written = 0;

		const u32 argument_count = code->call.argument_count;

		for (u32 i = 0; i != argument_count; ++i)
		{
			const s64 written = print_code2_block(sink, core, attachment->arguments[i].value);

			if (written < 0)
				return { -1, nullptr };

			total_written += written;
		}

		return { total_written, reinterpret_cast<const Code2*>(attachment->arguments + code->call.argument_count) };
	}

	case Code2Op::Slice:
	{
		const Code2AttSlice* const attachment = attachment_of<Code2AttSlice>(code);

		return { 0, reinterpret_cast<const Code2*>(attachment + 1) };
	}

	case Code2Op::Integer64:
	{
		const Code2AttInteger64* const attachment = attachment_of<Code2AttInteger64>(code);

		return { 0, reinterpret_cast<const Code2*>(attachment + 1) };
	}

	case Code2Op::Float:
	{
		const Code2AttFloat* const attachment = attachment_of<Code2AttFloat>(code);

		return { 0, reinterpret_cast<const Code2*>(attachment + 1) };
	}

	case Code2Op::String:
	{
		const Code2AttString* const attachment = attachment_of<Code2AttString>(code);

		return { 0, reinterpret_cast<const Code2*>(attachment + 1) };
	}

	case Code2Op::DefineTyped:
	case Code2Op::BinaryArithmetic:
	case Code2Op::BitShift:
	case Code2Op::TypeArray:
	case Code2Op::Index:
	case Code2Op::Store:
	case Code2Op::Compare:
	{
		const Code2AttBinaryOp* const attachment = attachment_of<Code2AttBinaryOp>(code);

		return { 0, reinterpret_cast<const Code2*>(attachment + 1) };
	}

	case Code2Op::LogicalAnd:
	case Code2Op::LogicalOr:
	{
		const Code2AttLogicalOp* const attachment = attachment_of<Code2AttLogicalOp>(code);

		const s64 written = print_code2_block(sink, core, attachment->rhs);

		if (written < 0)
			return { -1, nullptr };

		return { written, reinterpret_cast<const Code2*>(attachment + 1) };
	}

	case Code2Op::MemberValue:
	case Code2Op::MemberLocation:
	{
		const Code2AttMember* const attachment = attachment_of<Code2AttMember>(code);

		return { 0, reinterpret_cast<const Code2*>(attachment + 1) };
	}

	case Code2Op::INVALID:
		; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

s64 diag::print_code2(PrintSink sink, CoreData* core, const Code2Block* block) noexcept
{
	const Code2* const begin = code2_block_begin(block);

	const Code2* const end = code2_block_end(block);

	const Code2Id id = id_from_code2_block(core, block);

	s64 total_written = print(sink, "\n  %:\n", id);

	if (total_written < 0)
		return -1;

	PrintResult result{};

	const Code2* curr = begin;

	while (curr != end && result.written >= 0)
	{
		result = print_one_code2(sink, core, block, curr);

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
