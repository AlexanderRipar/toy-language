#include "diag.hpp"

static void print_type_impl(diag::PrintContext* ctx, IdentifierPool* identifiers, TypePool* types, TypeId type_id, u32 indent, bool skip_initial_indent) noexcept
{
	if (type_id.rep == INVALID_TYPE_ID.rep)
	{
		diag::buf_printf(ctx, "%*s<INVALID-TYPE-ID>\n", skip_initial_indent ? 0 : indent * 2, "");

		return;
	}

	const TypeTag tag = type_tag_from_id(types, type_id);

	const IdentifierId name_id = type_name_from_id(types, type_id);

	const Range<char8> name = name_id == INVALID_IDENTIFIER_ID ? range::from_literal_string("UNNAMED") : identifier_entry_from_id(identifiers, name_id)->range();

	const char8 name_opener = name_id == INVALID_IDENTIFIER_ID ? '<' : '\"';

	const char8 name_closer = name_id == INVALID_IDENTIFIER_ID ? '>' : '\"';

	diag::buf_printf(ctx, "%*s%c%.*s%c = %s",
		skip_initial_indent ? 0 : indent * 2, "",
		name_opener,
		static_cast<s32>(name.count()), name.begin(),
		name_closer,
		tag_name(tag)
	);

	switch (tag)
	{
	case TypeTag::Type:
	case TypeTag::Definition:
	case TypeTag::CompInteger:
	case TypeTag::CompFloat:
	case TypeTag::CompString:
	case TypeTag::Integer:
	case TypeTag::Float:
	case TypeTag::Boolean:
	case TypeTag::Builtin:
	case TypeTag::CompositeLiteral:
	case TypeTag::ArrayLiteral:
	case TypeTag::TypeBuilder:
	{
		diag::buf_printf(ctx, "\n");

		break;
	}

	case TypeTag::Slice:
	case TypeTag::Ptr:
	{
		const ReferenceType* const reference = static_cast<const ReferenceType*>(primitive_type_structure(types, type_id));

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

		diag::buf_printf(ctx, " :: %s%s", introducer, is_assignable(reference->referenced_type_id) ? " mut " : "");

		print_type_impl(ctx, identifiers, types, reference->referenced_type_id, indent + 1, true);

		break;
	}

	case TypeTag::Array:
	{
		const ArrayType* const array = static_cast<const ArrayType*>(primitive_type_structure(types, type_id));

		diag::buf_printf(ctx, " :: [%" PRIu64 "]", array->element_count);

		print_type_impl(ctx, identifiers, types, array->element_type, indent + 1, true);

		break;
	}

	case TypeTag::Func:
	{
		// TODO

		diag::buf_printf(ctx, "\n");

		break;
	}

	case TypeTag::Composite:
	{
		diag::buf_printf(ctx, " :: {");

		MemberIterator it = members_of(types, type_id);

		bool has_members = false;

		while (has_next(&it))
		{
			MemberInfo member = next(&it);

			const Range<char8> member_name = identifier_entry_from_id(identifiers, member.name)->range();

			diag::buf_printf(ctx, "%s%*s%s%s%s\"%.*s\": ", has_members ? "" : "\n",
				(indent + 1) * 2, "",
				member.is_pub ? "pub " : "",
				is_assignable(member.opt_type) ? "mut " : "",
				member.is_global ? "global " : "",
				static_cast<s32>(member_name.count()), member_name.begin()
			);

			if (!member.is_global)
				diag::buf_printf(ctx, "@%" PRId64, member.offset_or_global_value);

			print_type_impl(ctx, identifiers, types, member.opt_type, indent + 1, true);

			has_members = true;
		}

		diag::buf_printf(ctx, "%*s}\n", has_members ? indent * 2 : 1, "");

		break;
	}

	default:
		ASSERT_UNREACHABLE;
	}
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
