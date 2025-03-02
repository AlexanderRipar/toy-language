#include "diag.hpp"

static void print_type(FILE* out, IdentifierPool* identifiers, TypePool* types, TypeId type_id, u32 depth, bool continue_line) noexcept
{
	if (type_id == INVALID_TYPE_ID)
	{
		fprintf(out, "%*sINVALID_TYPE\n", continue_line ? 0 : depth * 2, "");

		return;
	}

	TypeEntry* const entry = type_entry_from_id(types, type_id);

	switch (entry->tag)
	{
		case TypeTag::Void:
		{
			fprintf(out, "%*svoid\n", continue_line ? 0 : depth * 2, "");

			break;
		}

		case TypeTag::Type:
		{
			fprintf(out, "%*stype\n", continue_line ? 0 : depth * 2, "");

			break;
		}

		case TypeTag::CompInteger:
		{
			fprintf(out, "%*scomp_integer\n", continue_line ? 0 : depth * 2, "");

			break;
		}

		case TypeTag::CompFloat:
		{
			fprintf(out, "%*scomp_float\n", continue_line ? 0 : depth * 2, "");

			break;
		}

		case TypeTag::CompString:
		{
			fprintf(out, "%*scomp_string\n", continue_line ? 0 : depth * 2, "");

			break;
		}

		case TypeTag::Integer:
		{
			IntegerType* const integer_type = entry->data<IntegerType>();

			fprintf(out, "%*s%c%u\n", continue_line ? 0 : depth * 2, "", (entry->flags & TypeFlag::Integer_IsSigned) == TypeFlag::Integer_IsSigned ? 's' : 'u', integer_type->bits);

			break;
		}

		case TypeTag::Float:
		{
			FloatType* const float_type = entry->data<FloatType>();

			fprintf(out, "%*sf%u\n", continue_line ? 0 : depth * 2, "", float_type->bits);

			break;
		}

		case TypeTag::Boolean:
		{
			fprintf(out, "%*sbool\n", continue_line ? 0 : depth * 2, "");

			break;
		}

		case TypeTag::Slice:
		{
			SliceType* const slice_type = entry->data<SliceType>();

			fprintf(out, "%*s[]%s", continue_line ? 0 : depth * 2, "", (entry->flags & TypeFlag::SliceOrPtr_IsMut) == TypeFlag::SliceOrPtr_IsMut ? "mut " : "");

			print_type(out, identifiers, types, slice_type->element_id, depth, true);

			break;
		}

		case TypeTag::Ptr:
		{
			PtrType* const ptr_type = entry->data<PtrType>();

			fprintf(out, "%*s%s%s", continue_line ? 0 : depth * 2, "",
				(entry->flags & (TypeFlag::Ptr_IsMulti | TypeFlag::Ptr_IsOpt)) == (TypeFlag::Ptr_IsMulti | TypeFlag::Ptr_IsOpt)
					? "[?]"
					: (entry->flags & TypeFlag::Ptr_IsOpt) == TypeFlag::Ptr_IsOpt
					? "?"
					: (entry->flags & TypeFlag::Ptr_IsMulti) == TypeFlag::Ptr_IsMulti
					?
					"[*]"
					: "*",
				(entry->flags & TypeFlag::SliceOrPtr_IsMut) == TypeFlag::SliceOrPtr_IsMut
					? "mut "
					: "");

			print_type(out, identifiers, types, ptr_type->pointee_id, depth, true);

			break;
		}

		case TypeTag::Alias:
		{
			AliasType* const alias_type = entry->data<AliasType>();

			fprintf(out, "%*s(alias) ", continue_line ? 0 : depth * 2, "");

			print_type(out, identifiers, types, alias_type->aliased_id, depth, true);

			break;
		}

		case TypeTag::Array:
		{
			ArrayType* const array_type = entry->data<ArrayType>();

			fprintf(out, "%*s[%llu] ", continue_line ? 0 : depth * 2, "", array_type->count);
	
			print_type(out, identifiers, types, array_type->element_id, depth, true);

			break;
		}

		case TypeTag::Func:
		{
			FuncType* const func_type = entry->data<FuncType>();

			fprintf(out, "%*s%s(\n", continue_line ? 0 : depth * 2, "", func_type->header.is_proc ? "proc" : "func");

			for (u32 i = 0; i != func_type->header.parameter_count; ++i)
				print_type(out, identifiers, types, func_type->params[i].type, depth + 1, false);

			fprintf(out, "%*s) -> ", depth * 2, "");

			print_type(out, identifiers, types, func_type->header.return_type_id, depth, true);

			break;
		}

		case TypeTag::Composite:
		{
			CompositeType* const composite_type = entry->data<CompositeType>();

			fprintf(out, "%*scomposite (%u@%u) {\n", continue_line ? 0 : depth * 2, "", composite_type->header.size, composite_type->header.alignment);

			for (u32 i = 0; i != composite_type->header.member_count; ++i)
			{
				CompositeTypeMember* const member = composite_type->members + i;

				const Range<char8> member_name = identifier_entry_from_id(identifiers, member->identifier_id)->range();

				fprintf(out, "%*s%.*s (%+llu): ", (depth + 1) * 2, "", static_cast<s32>(member_name.count()), member_name.begin(), member->offset);

				print_type(out, identifiers, types, member->type_id, depth + 1, true);
			}

			fprintf(out, "%*s}\n", depth * 2, "");

			break;
		}

		case TypeTag::CompositeLiteral:
		{
			fprintf(out, "%*s.{literal}\n", continue_line ? 0 : depth * 2, "");

			break;
		}

		case TypeTag::ArrayLiteral:
		{
			fprintf(out, "%*s.[literal]\n", continue_line ? 0 : depth * 2, "");

			break;
		}

		default:
			ASSERT_UNREACHABLE;
	}
}

void diag::print_type(FILE* out, IdentifierPool* identifiers, TypePool* types, TypeId type_id) noexcept
{
	print_type(out, identifiers, types, type_id, 0, false);
}
