#include "diag.hpp"

#include "../infra/types.hpp"
#include "../infra/assert.hpp"
#include "../infra/range.hpp"
#include "../infra/print/print.hpp"

static s64 print_type_impl(PrintSink sink, CoreData* core, TypeId type_id, u32 indent, bool skip_initial_indent) noexcept
{
	if (type_id == TypeId::INVALID)
		return print(sink, "%[< %]<Invalid>\n", "", skip_initial_indent ? 0 : indent * 2);

	s64 indent_written = 0;

	if (!skip_initial_indent)
	{
		indent_written = print(sink, "%[< %]", "", indent * 2);

		if (indent_written < 0)
			return -1;
	}

	const TypeTag tag = type_tag_from_id(core, type_id);

	switch (tag)
	{
	case TypeTag::Void:
	case TypeTag::Type:
	case TypeTag::Definition:
	case TypeTag::CompInteger:
	case TypeTag::CompFloat:
	case TypeTag::Boolean:
	case TypeTag::TypeBuilder:
	case TypeTag::Divergent:
	case TypeTag::Undefined:
	case TypeTag::TypeInfo:
	case TypeTag::TailArray:
	{
		const s64 written = print(sink, "%\n", tag_name(tag));

		if (written < 0)
			return -1;

		return indent_written + written;
	}

	case TypeTag::Integer:
	case TypeTag::Float:
	{
		const NumericType* numeric_type = type_attachment_from_id<NumericType>(core, type_id);

		const s64 written = print(sink, "%[]%\n", tag == TypeTag::Integer ? numeric_type->is_signed ? "s" : "u" : "f", numeric_type->bits);

		if (written < 0)
			return -1;

		return indent_written + written;
	}

	case TypeTag::Slice:
	case TypeTag::Ptr:
	case TypeTag::Variadic:
	{
		const ReferenceType* const reference = type_attachment_from_id<ReferenceType>(core, type_id);

		const char8* introducer;

		if (tag == TypeTag::Slice)
			introducer = "[]";
		else if (tag == TypeTag::Variadic)
			introducer = "...";
		else if (reference->is_opt && reference->is_multi)
			introducer = "[?]";
		else if (reference->is_multi)
			introducer = "[*]";
		else if (reference->is_opt)
			introducer = "?";
		else
			introducer = "*";

		const s64 written = print(sink, "%[]%", introducer, reference->is_mut ? "mut " : "");

		if (written < 0)
			return -1;

		const s64 referenced_written = print_type_impl(sink, core, reference->referenced_type_id, indent + 1, true);

		if (referenced_written < 0)
			return -1;

		return indent_written + written + referenced_written;
	}

	case TypeTag::ArrayLiteral:
	case TypeTag::Array:
	{
		const ArrayType* const array = type_attachment_from_id<ArrayType>(core, type_id);

		const s64 written = print(sink, "%[][%]", tag == TypeTag::ArrayLiteral ? "." : "", array->element_count);

		if (written < 0)
			return -1;

		const s64 element_written = print_type_impl(sink, core, is_some(array->element_type) ? get(array->element_type) : TypeId::INVALID, indent + 1, true);

		if (element_written < 0)
			return -1;

		return indent_written + written + element_written;
	}

	case TypeTag::Signature:
	case TypeTag::Trait:
	case TypeTag::Composite:
	case TypeTag::CompositeLiteral:
	{
		s64 total_written = 0;

		if (type_has_metrics(core, type_id))
		{
		const TypeMetrics metrics = type_metrics_from_id(core, type_id);

			const s64 written = print(sink, " (size=% align=% stride=%) {",
				metrics.size,
				metrics.align,
				metrics.stride
			);

			if (written < 0)
				return -1;

			total_written += written;
		}
		else
		{
			const s64 written = print(sink, " (size=? align=? stride=?) {");

			if (written < 0)
				return -1;

			total_written += written;
		}

		bool has_members = false;

		MemberIterator it = members_of(core, type_id);

		while (has_next(&it))
		{
			MemberInfo member_info;

			OpcodeId member_initializer;

			const bool is_complete = next(&it, &member_info, &member_initializer);

			const IdentifierId member_name = type_member_name_by_rank(core, type_id, member_info.rank);

			const s64 flags_written = print(sink, "\n%[< %] global=% pub=% mut=% eval=% ",
				"", (indent + 1) * 2,
				member_info.is_global,
				member_info.is_pub,
				member_info.is_mut,
				member_info.is_eval
			);

			if (flags_written < 0)
				return -1;

			total_written += flags_written;

			if (is_complete && !member_info.is_global)
			{
				const s64 offset_written = print(sink, "offset=% ", member_info.offset);

				if (offset_written < 0)
					return -1;

				total_written += offset_written;
			}

			s64 name_written;

			if (member_name < IdentifierId::FirstNatural)
			{
				name_written = print(sink, "name=\"_%\" :: ", static_cast<u32>(member_name));
			}
			else
			{
				const Range<char8> name = identifier_name_from_id(core, member_name);

				name_written = print(sink, "name=\"%\" :: ", name);
			}

			if (name_written < 0)
				return -1;

			total_written += name_written;

			s64 type_written;

			if (is_complete)
			{
				type_written = print_type_impl(sink, core, member_info.type_id, indent + 1, true);
			}
			else
			{
				type_written = print(sink, "OpcodeId<%u>", static_cast<u32>(member_initializer));
			}

			if (type_written < 0)
				return -1;

			total_written += type_written;

			has_members = true;
		}

		const s64 end_written = print(sink, "%[< %]}", "", has_members ? indent * 2 : 1, "");

		if (end_written < 0)
			return -1;

		total_written += end_written;

		if (tag == TypeTag::Signature)
		{
			const SignatureTypeInfo info = type_signature_info_from_id(core, type_id);

			s64 return_type_written;

			if (info.has_templated_return_type)
			{
				return_type_written = print(sink, " -> OpcodeId<%>", static_cast<u32>(info.return_type.templated.completion_id));
			}
			else
			{
				const s64 arrow_written = print(sink, " -> ");

				if (arrow_written < 0)
					return -1;

				total_written += arrow_written;

				return_type_written = print_type_impl(sink, core, info.return_type.complete.type_id, indent + 1, true);
			}

			if (return_type_written < 0)
				return -1;

			total_written += return_type_written;
		}

		return indent_written + total_written;
	}

	case TypeTag::INVALID:
	case TypeTag::INDIRECTION:
		; // fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

s64 diag::print_type(PrintSink sink, CoreData* core, TypeId type_id) noexcept
{
	const s64 type_written = print_type_impl(sink, core, type_id, 0, false);

	if (type_written < 0)
		return -1;

	const s64 end_written = print(sink, "\n");

	if (end_written < 0)
		return -1;

	return type_written + end_written;
}
