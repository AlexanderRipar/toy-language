#include "diag.hpp"

static void print_type_impl(diag::PrintContext* ctx, IdentifierPool* identifiers, TypePool* types, TypeId type_id, u32 indent, bool skip_initial_indent) noexcept
{
	if (type_id == TypeId::INVALID)
	{
		diag::buf_printf(ctx, "%*s<Invalid>\n", skip_initial_indent ? 0 : indent * 2, "");

		return;
	}

	if (!skip_initial_indent)
		diag::buf_printf(ctx, "%*s", indent * 2, "");

	const TypeTag tag = type_tag_from_id(types, type_id);

	switch (tag)
	{
	case TypeTag::Void:
	case TypeTag::Type:
	case TypeTag::Definition:
	case TypeTag::CompInteger:
	case TypeTag::CompFloat:
	case TypeTag::Boolean:
	case TypeTag::Builtin:
	case TypeTag::TypeBuilder:
	case TypeTag::Divergent:
	case TypeTag::Trait:
	case TypeTag::TypeInfo:
	case TypeTag::TailArray:
	{
		diag::buf_printf(ctx, "%s\n", tag_name(tag));

		return;
	}

	case TypeTag::Integer:
	case TypeTag::Float:
	{
		const NumericType* numeric_type = type_attachment_from_id<NumericType>(types, type_id);

		diag::buf_printf(ctx, "%s%u\n", tag == TypeTag::Integer ? numeric_type->is_signed ? "s" : "u" : "f", numeric_type->bits);

		return;
	}

	case TypeTag::Slice:
	case TypeTag::Ptr:
	case TypeTag::Variadic:
	{
		const ReferenceType* const reference = type_attachment_from_id<ReferenceType>(types, type_id);

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

		diag::buf_printf(ctx, "%s%s", introducer, reference->is_mut ? "mut " : "");

		print_type_impl(ctx, identifiers, types, reference->referenced_type_id, indent + 1, true);

		return;
	}

	case TypeTag::ArrayLiteral:
	case TypeTag::Array:
	{
		const ArrayType* const array = type_attachment_from_id<ArrayType>(types, type_id);

		diag::buf_printf(ctx, "%s[%" PRIu64 "]", tag == TypeTag::ArrayLiteral ? "." : "", array->element_count);

		print_type_impl(ctx, identifiers, types, array->element_type, indent + 1, true);

		return;
	}

	case TypeTag::Func:
	case TypeTag::Composite:
	case TypeTag::CompositeLiteral:
	{
		const SignatureType* signature_type;

		TypeId composite_type_id;

		if (tag == TypeTag::Func)
		{
			signature_type = type_attachment_from_id<SignatureType>(types, type_id);

			composite_type_id = signature_type->parameter_list_type_id;
		}
		else
		{
			signature_type = nullptr;

			composite_type_id = type_id;
		}

		const TypeMetrics metrics = type_has_metrics(types, composite_type_id)
			? type_metrics_from_id(types, composite_type_id)
			: TypeMetrics{ 0, 0, 0 };

		diag::buf_printf(ctx, "%s (sz=%" PRIu64 ", al=%" PRIu32 ", st=%" PRIu64 ") {",
			tag == TypeTag::Func ? "Func" : tag == TypeTag::Composite ? "Composite" : "CompositeLiteral",
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
				const MemberInfo member = next(&it);

				diag::buf_printf(ctx, "%s%*s%s%s",
					has_members ? "" : "\n",
					(indent + 1) * 2, "",
					member.is_pub ? "pub " : "",
					member.is_mut ? "mut " : ""
				);

				if (member.name < IdentifierId::FirstNatural)
				{
					diag::buf_printf(ctx, "\"_%u\" ",
						static_cast<u32>(member.name)
					);
				}
				else
				{
					const Range<char8> name = identifier_name_from_id(identifiers, member.name);

					diag::buf_printf(ctx, "\"%.*s\" ",
						static_cast<s32>(name.count()), name.begin()
					);
				}

				diag::buf_printf(ctx, "(%+" PRId64 ") :: ", member.offset);

				if (member.has_pending_type)
				{
					diag::buf_printf(ctx, "<INCOMPLETE>\n");
				}
				else
				{
					print_type_impl(ctx, identifiers, types, member.type.complete, indent + 1, true);
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
	case TypeTag::INDIRECTION:
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
