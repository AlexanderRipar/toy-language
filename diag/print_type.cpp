#include "diag.hpp"

static const char8* optional_tag_name(TypeTag tag) noexcept
{
	if (tag == TypeTag::Composite
	 || tag == TypeTag::Func
	 || tag == TypeTag::Array
	 || tag == TypeTag::Slice
	 || tag == TypeTag::Ptr
	 || tag == TypeTag::Integer
	 || tag == TypeTag::Float)
		return "";

	return tag_name(tag);
}

static void print_type_impl(diag::PrintContext* ctx, IdentifierPool* identifiers, TypePool* types, TypeId type_id, u32 indent, bool skip_initial_indent) noexcept
{
	if (type_id == TypeId::INVALID)
	{
		diag::buf_printf(ctx, "%*s<INVALID>\n", skip_initial_indent ? 0 : indent * 2, "");

		return;
	}

	const TypeTag tag = type_tag_from_id(types, type_id);

	const IdentifierId name_id = type_name_from_id(types, type_id);

	const Range<char8> name = name_id == IdentifierId::INVALID ? range::from_literal_string("UNNAMED") : identifier_name_from_id(identifiers, name_id);

	const char8 name_opener = name_id == IdentifierId::INVALID ? '<' : '\"';

	const char8 name_closer = name_id == IdentifierId::INVALID ? '>' : '\"';

	const char8* tag_string = optional_tag_name(tag);

	diag::buf_printf(ctx, "%*s%c%.*s%c%s%s",
		skip_initial_indent ? 0 : indent * 2, "",
		name_opener,
		static_cast<s32>(name.count()), name.begin(),
		name_closer,
		*tag_string == '\0' ? "" : " ",
		tag_string
	);

	switch (tag)
	{
	case TypeTag::Void:
	case TypeTag::Type:
	case TypeTag::Definition:
	case TypeTag::CompInteger:
	case TypeTag::CompFloat:
	case TypeTag::Boolean:
	case TypeTag::Builtin:
	case TypeTag::CompositeLiteral:
	case TypeTag::ArrayLiteral:
	case TypeTag::TypeBuilder:
	case TypeTag::Variadic:
	case TypeTag::Divergent:
	case TypeTag::Trait:
	case TypeTag::TypeInfo:
	case TypeTag::TailArray:
	{
		diag::buf_printf(ctx, "\n");

		return;
	}

	case TypeTag::Integer:
	case TypeTag::Float:
	{
		const NumericType* numeric_type = static_cast<const NumericType*>(simple_type_structure_from_id(types, type_id));

		diag::buf_printf(ctx, " %s%u\n", tag == TypeTag::Integer ? numeric_type->is_signed ? "s" : "u" : "f", numeric_type->bits);

		return;
	}

	case TypeTag::Slice:
	case TypeTag::Ptr:
	{
		const ReferenceType* const reference = static_cast<const ReferenceType*>(simple_type_structure_from_id(types, type_id));

		const char8* introducer;

		if (tag == TypeTag::Slice)
			introducer = "[]";
		else if (reference->is_opt && reference->is_multi)
			introducer = "[?]";
		else if (reference->is_multi)
			introducer = "[*]";
		else if (reference->is_opt)
			introducer = "?";
		else
			introducer = "*";

		diag::buf_printf(ctx, " %s%s ", introducer, reference->is_mut ? " mut" : "");

		print_type_impl(ctx, identifiers, types, reference->referenced_type_id, indent + 1, true);

		return;
	}

	case TypeTag::Array:
	{
		const ArrayType* const array = static_cast<const ArrayType*>(simple_type_structure_from_id(types, type_id));

		diag::buf_printf(ctx, " :: [%" PRIu64 "]", array->element_count);

		print_type_impl(ctx, identifiers, types, array->element_type, indent + 1, true);

		return;
	}

	case TypeTag::Func:
	case TypeTag::Composite:
	{
		const SignatureType* signature_type;

		TypeId composite_type_id;

		if (tag == TypeTag::Func)
		{
			signature_type = static_cast<const SignatureType*>(simple_type_structure_from_id(types, type_id));

			composite_type_id = signature_type->parameter_list_type_id;
		}
		else
		{
			signature_type = nullptr;

			composite_type_id = type_id;
		}

		const TypeMetrics metrics = type_metrics_from_id(types, composite_type_id);

		diag::buf_printf(ctx, " %s (sz=%" PRIu64 ", al=%" PRIu32 ", st=%" PRIu64 ") {",
			tag == TypeTag::Func ? "Func" : "Composite",
			metrics.size,
			metrics.align,
			metrics.stride
		);

		bool has_members = false;

		if (composite_type_id == TypeId::INVALID)
		{
			diag::buf_printf(ctx, "%*s<INCOMPLETE>",
				(indent + 1) * 2, ""
			);
		}
		else
		{
			MemberIterator it = members_of(types, composite_type_id);

			while (has_next(&it))
			{
				const Member* member = next(&it);

				const Range<char8> member_name = identifier_name_from_id(identifiers, member->name);

				diag::buf_printf(ctx, "%s%*s%s%s%s\"%.*s\" ",
					has_members ? "" : "\n",
					(indent + 1) * 2, "",
					member->is_pub ? "pub " : "",
					member->is_mut ? "mut " : "",
					member->is_global ? "global " : "",
					static_cast<s32>(member_name.count()), member_name.begin()
				);

				if (member->is_global)
					diag::buf_printf(ctx, ":: ");
				else
					diag::buf_printf(ctx, "(%+" PRId64 ") :: ", member->offset);

				if (member->has_pending_type)
				{
					diag::buf_printf(ctx, "<INCOMPLETE>\n");
				}
				else
				{
					print_type_impl(ctx, identifiers, types, member->type.complete, indent + 1, true);
				}

				has_members = true;
			}
		}

		diag::buf_printf(ctx, "%*s}%s", has_members ? indent * 2 : 1, "", tag == TypeTag::Func ? " -> " : "\n");

		if (tag == TypeTag::Func)
		{
			if (signature_type->return_type_is_unbound)
				diag::buf_printf(ctx, "<INCOMPLETE>\n");
			else
				print_type_impl(ctx, identifiers, types, signature_type->return_type.complete, indent + 1, true);
		}

		return;
	}

	case TypeTag::INVALID:
		; // fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

void diag::print_type(minos::FileHandle out, IdentifierPool* identifiers, TypePool* types, TypeId type_id, const SourceLocation* source) noexcept
{
	PrintContext ctx;
	ctx.curr = ctx.buf;
	ctx.file = out;

	diag::buf_printf(&ctx, "\n#### TYPE [%.*s:%u:%u] ####\n\n",
		static_cast<s32>(source->filepath.count()), source->filepath.begin(),
		source->line_number,
		source->column_number
	);

	print_type_impl(&ctx, identifiers, types, type_id, 0, false);

	diag::buf_flush(&ctx);
}
