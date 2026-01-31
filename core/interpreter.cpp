#include "core.hpp"

#include "../infra/container/reserved_vec.hpp"
#include "../diag/diag.hpp"

#include <cmath>

struct Scope
{
	u32 first_member_index;

	u32 temporary_data_used;
};

struct alignas(8) ScopeMember
{
	u32 offset;

	u32 size;

	u32 align : 31;

	u32 is_mut : 1;

	TypeId type;
};

struct alignas(8) Callable
{
	OpcodeId body_id;

	Maybe<ClosureId> closure_id;
};

enum class CompareTag : u8
{
	INVALID = 0,
	Ordering,
	Equality,
};

enum class CompareEquality : u8
{
	Equal,
	Unequal,
};

struct CompareResult
{
	CompareTag tag;

	union
	{
		WeakCompareOrdering ordering;

		CompareEquality equality;
	};

	explicit CompareResult() noexcept : tag{ CompareTag::INVALID }, ordering{} {}

	explicit CompareResult(WeakCompareOrdering ordering) : tag{ CompareTag::Ordering }, ordering{ ordering } {}

	explicit CompareResult(CompareEquality equality) : tag{ CompareTag::Equality }, equality{ equality } {}
};

struct alignas(8) ArgumentPack
{
	TypeId parameter_list_type;

	union
	{
		TypeId type;

		OpcodeId completion;
	} return_type;

	u32 scope_first_member_index;

	u8 index;

	u8 count;

	bool has_templated_parameter_list : 1;

	bool has_templated_return_type : 1;

	bool has_just_completed_template_parameter;
};

struct alignas(4) GlobalInitialization
{
	GlobalFileIndex file_index;

	u16 rank;
};

struct LoopInfo
{
	u32 activation_index;

	u32 scope_index;
};

struct BuiltinInfo
{
	OpcodeId body;

	TypeId signature_type;
};

struct BuiltinParamInfo
{
	IdentifierId name;

	TypeId type;

	bool is_comptime_known;
};

struct Interpreter
{
	AstPool* asts;

	TypePool* types;

	GlobalValuePool* globals;

	OpcodePool* opcodes;

	SourceReader* reader;

	Parser* parser;

	IdentifierPool* identifiers;

	LexicalAnalyser* lex;

	ErrorSink* errors;

	ReservedVec<Scope> scopes;

	ReservedVec<ScopeMember> scope_members;

	ReservedVec<byte> scope_data;

	ReservedVec<CTValue> values;

	ReservedVec<byte> temporary_data;

	ReservedVec<OpcodeId> activations;

	ReservedVec<u32> call_activation_indices;

	ReservedVec<LoopInfo> loop_stack;

	ReservedVec<CTValue> write_ctxs;

	ReservedVec<ClosureId> active_closures;

	ReservedVec<ScopeMember> closure_members;

	ReservedVec<OpcodeId> argument_callbacks;

	ReservedVec<ArgumentPack> argument_packs;

	ReservedVec<GlobalInitialization> global_initializations;

	bool is_ok;

	BuiltinInfo builtin_infos[static_cast<u8>(Builtin::MAX) - 1];

	MutRange<byte> memory;

	minos::FileHandle imported_asts_log_file;

	minos::FileHandle imported_opcodes_log_file;

	minos::FileHandle imported_types_log_file;
};

using OpcodeHandlerFunc = const Opcode* (*) (Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept;



static void log_ast(Interpreter* interp, AstNode* node) noexcept
{
	const SourceId source_id = source_id_of_ast_node(interp->asts, node);

	const SourceLocation location = source_location_from_source_id(interp->reader, source_id);

	diag::print_header(interp->imported_asts_log_file, "%.*s:%u:%u",
		static_cast<s32>(location.filepath.count()), location.filepath.begin(),
		location.line_number,
		location.column_number
	);

	diag::print_ast(interp->imported_asts_log_file, interp->identifiers, node);
}

static void log_opcodes(Interpreter* interp, const Opcode* code) noexcept
{
	const SourceId source_id = source_id_of_opcode(interp->opcodes, code);

	SourceLocation location = source_location_from_source_id(interp->reader, source_id);

	const OpcodeId code_id = id_from_opcode(interp->opcodes, code);

	diag::print_header(interp->imported_opcodes_log_file, "%.*s:%u:%u (OpcodeId<%u>)",
			static_cast<s32>(location.filepath.count()), location.filepath.begin(),
			location.line_number,
			location.column_number,
			static_cast<u32>(code_id)
	);

	diag::print_opcodes(interp->imported_opcodes_log_file, interp->identifiers, interp->opcodes, code, true);
}



static const Opcode* record_interpreter_error(Interpreter* interp, const Opcode* code, CompileError error) noexcept
{
	const SourceId source_id = source_id_of_opcode(interp->opcodes, code - 1);

	record_error(interp->errors, source_id, error);

	interp->is_ok = false;

	return nullptr;
}



static const Opcode* convert_into(Interpreter* interp, const Opcode* code, CTValue src, CTValue dst) noexcept;

static CTValue alloc_temporary_value_uninit(Interpreter* interp, u64 size, u32 align, TypeId type) noexcept
{
	if (size >= UINT32_MAX)
		panic("Maximum size of temporary value exceeded.\n");

	interp->temporary_data.pad_to_alignment(align);

	byte* const bytes = interp->temporary_data.reserve(static_cast<u32>(size));

	return CTValue{ MutRange<byte>{ bytes, size }, align, true, type };
}

static CTValue alloc_temporary_value(Interpreter* interp, CTValue value) noexcept
{
	CTValue temporary_value = alloc_temporary_value_uninit(interp, value.bytes.count(), value.align, value.type);

	range::mem_copy(temporary_value.bytes, value.bytes.immut());
	
	return temporary_value;
}

static const Opcode* push_location_value(Interpreter* interp, const Opcode* code, CTValue* write_ctx, CTValue value) noexcept
{
	if (write_ctx != nullptr)
	{
		return convert_into(interp, code, value, *write_ctx);
	}
	else
	{
		interp->values.append(value);

		return code;
	}
}

static const Opcode* push_temporary_value(Interpreter* interp, const Opcode* code, CTValue* write_ctx, CTValue value) noexcept
{
	if (write_ctx != nullptr)
	{
		return convert_into(interp, code, value, *write_ctx);
	}
	else
	{
		CTValue temporary_value = alloc_temporary_value(interp, value);

		interp->values.append(temporary_value);

		return code;
	}
}

static const Opcode* poppush_location_value(Interpreter* interp, const Opcode* code, CTValue* write_ctx, CTValue value) noexcept
{
	ASSERT_OR_IGNORE(interp->values.used() >= 1);

	if (write_ctx != nullptr)
	{
		interp->values.pop_by(1);

		return convert_into(interp, code, value, *write_ctx);
	}
	else
	{
		CTValue* const top = interp->values.end() - 1;

		*top = value;

		return code;
	}
}

static const Opcode* poppush_temporary_value(Interpreter* interp, const Opcode* code, CTValue* write_ctx, CTValue value) noexcept
{
	ASSERT_OR_IGNORE(interp->values.used() >= 1);

	if (write_ctx != nullptr)
	{
		interp->values.pop_by(1);

		return convert_into(interp, code, value, *write_ctx);
	}
	else
	{
		CTValue temporary_value = alloc_temporary_value(interp, value);

		CTValue* const top = interp->values.end() - 1;

		*top = temporary_value;

		return code;
	}
}



struct SeenSet
{
	u64* bits;

	u16 count;
};

static SeenSet seen_set_init(Interpreter* interp, u16 count, u16 leading_1_count) noexcept
{
	ASSERT_OR_IGNORE(leading_1_count <= count);

	const u16 qword_count = (count + 63) / 64;

	interp->temporary_data.pad_to_alignment(alignof(u64));

	u64* const bits = reinterpret_cast<u64*>(interp->temporary_data.reserve(qword_count));

	const u16 all_set_qwords = leading_1_count / 64;

	u16 i = 0;

	while (i != all_set_qwords)
	{
		bits[i] = ~static_cast<u64>(0);

		i += 1;
	}

	const bool has_partially_set_qword = (leading_1_count & 63) != 0;

	if (has_partially_set_qword)
	{
		bits[i] = static_cast<u64>(1) << (leading_1_count & 63);

		i += 1;
	}

	while (i != qword_count)
	{
		bits[i] = 0;

		i += 1;
	}

	return SeenSet{ bits, count };
}

static bool seen_set_set(SeenSet seen, u16 begin, u16 count) noexcept
{
	ASSERT_OR_IGNORE(count != 0);

	ASSERT_OR_IGNORE(begin + count <= seen.count);

	if (begin / 64 == (begin + count - 1) / 64)
	{
		const u64 ones = ((static_cast<u64>(1) << count) - 1) << (begin & 63);

		if ((seen.bits[begin / 64] & ones) != 0)
			return false;

		seen.bits[begin / 64] |= ones;

		return true;
	}

	u16 i = begin / 64;

	const bool has_partial_first_qword = (begin & 63) != 0;

	if (has_partial_first_qword)
	{
		const u64 ones = ~(static_cast<u64>(1) << (begin & 63));

		if ((seen.bits[i] & ones) != 0)
			return false;

		seen.bits[i] |= ones;

		i += 1;
	}

	const u16 end = begin + count;

	const u16 last_full_qword = end / 64;

	while (i != last_full_qword)
	{
		if (seen.bits[i] != 0)
			return false;

		seen.bits[i] = ~static_cast<u64>(0);

		i += 1;
	}

	const bool has_partial_last_qword = (end & 63) != 0;

	if (has_partial_last_qword)
	{
		const u64 ones = static_cast<u64>(1) << (end & 63);

		if ((seen.bits[i] & ones) != 0)
			return false;

		seen.bits[i] |= ones;
	}

	return true;
}

static bool seen_set_next_unseen(SeenSet seen, u16 begin, u16* out_index) noexcept
{
	ASSERT_OR_IGNORE(begin <= seen.count);

	const u16 first_index = begin / 64;

	const u16 last_index = seen.count / 64;

	for (u16 i = first_index; i != last_index; ++i)
	{
		if (seen.bits[i] == ~static_cast<u64>(0))
			continue;

		const u8 first_zero = count_leading_ones_assume_zero(seen.bits[i]);

		seen.bits[i] |= (static_cast<u64>(1) << first_zero);

		*out_index = i * 64 + first_zero;

		return true;
	}
	
	return false;
}



static void push_activation(Interpreter* interp, OpcodeId id) noexcept
{
	interp->activations.append(id);
}

static void push_activation(Interpreter* interp, const Opcode* code) noexcept
{
	const OpcodeId id = id_from_opcode(interp->opcodes, code);

	push_activation(interp, id);
}



template<typename T>
static T* value_as(CTValue* value) noexcept
{
	ASSERT_OR_IGNORE(value->bytes.count() == sizeof(T) && value->align == alignof(T));

	return reinterpret_cast<T*>(value->bytes.begin());
}



static ClosureId create_closure(Interpreter* interp, u32 value_count) noexcept
{
	ASSERT_OR_IGNORE(interp->values.used() >= value_count);

	CTValue* const values = interp->values.end() - value_count;

	ScopeMember* const closure_members = interp->closure_members.reserve(value_count);

	for (u32 i = 0; i != value_count; ++i)
	{
		const CTValue src = values[i];

		const ForeverValueId value_id = forever_value_alloc_initialized(interp->globals, false, src);

		ScopeMember* const dst = closure_members + i;

		dst->offset = static_cast<u32>(value_id);
		dst->size = static_cast<u32>(src.bytes.count());
		dst->align = src.align;
		dst->is_mut = false;
		dst->type = src.type;
	}

	interp->values.pop_by(value_count);

	return static_cast<ClosureId>(closure_members - interp->closure_members.begin());
}

static const Opcode* convert_into_assume_convertible(Interpreter* interp, const Opcode* code, CTValue src, CTValue dst) noexcept
{
	const TypeTag src_type_tag = type_tag_from_id(interp->types, src.type);

	const TypeTag dst_type_tag = type_tag_from_id(interp->types, dst.type);

	if (dst_type_tag == TypeTag::TypeInfo)
	{
		range::mem_copy(dst.bytes, range::from_object_bytes(&src.type));

		return code;
	}
	else switch (src_type_tag)
	{
	case TypeTag::CompInteger:
	{
		ASSERT_OR_IGNORE(type_tag_from_id(interp->types, dst.type) == TypeTag::Integer);

		const NumericType* const integer_type = type_attachment_from_id<NumericType>(interp->types, dst.type);

		ASSERT_OR_IGNORE((static_cast<u64>(integer_type->bits) + 7) / 8 == dst.bytes.count());

		const CompIntegerValue src_value = *value_as<CompIntegerValue>(&src);

		if (!bits_from_comp_integer(src_value, integer_type->bits, integer_type->is_signed, dst.bytes.begin()))
			return record_interpreter_error(interp, code, CompileError::CompIntegerValueTooLarge);

		return code;
	}

	case TypeTag::CompFloat:
	{
		ASSERT_OR_IGNORE(type_tag_from_id(interp->types, dst.type) == TypeTag::Float);

		const NumericType* const float_type = type_attachment_from_id<NumericType>(interp->types, dst.type);

		const CompFloatValue src_value = *value_as<CompFloatValue>(&src);

		if (float_type->bits == 32)
			*value_as<f32>(&dst) = f32_from_comp_float(src_value);
		else if (float_type->bits == 64)
			*value_as<f64>(&dst) = f64_from_comp_float(src_value);
		else
			ASSERT_UNREACHABLE;

		return code;
	}

	case TypeTag::Undefined:
	{
		// Debug pattern for uninitialized memory.
		range::mem_set(dst.bytes, 0xA6);

		return code;
	}

	case TypeTag::Slice:
	case TypeTag::Ptr:
	{
		ASSERT_OR_IGNORE(type_tag_from_id(interp->types, dst.type) == src_type_tag);

		// Essentially a no-op, we are just adjusting permissions.
		range::mem_copy(dst.bytes, src.bytes.immut());

		return code;
	}

	case TypeTag::CompositeLiteral:
	{
		ASSERT_OR_IGNORE(type_tag_from_id(interp->types, dst.type) == TypeTag::Composite);

		const u32 dst_member_count = type_get_composite_member_count(interp->types, dst.type);

		const u32 seen_members_size = ((dst_member_count + 7) / 8 + sizeof(u64) - 1) & ~(sizeof(u64) - 1);

		CTValue seen_members_value = alloc_temporary_value_uninit(interp, seen_members_size, alignof(u64), TypeId::INVALID);

		u64* const seen_members = reinterpret_cast<u64*>(seen_members_value.bytes.begin());

		memset(seen_members, 0, seen_members_size);

		u32 rank = 0;

		MemberIterator it = members_of(interp->types, src.type);

		while (has_next(&it))
		{
			MemberInfo src_member_info;

			OpcodeId unused_src_initializer;

			if (!next(&it, &src_member_info, &unused_src_initializer))
				TODO("Figure out what to do when converting incomplete types and if this can even reasonably happen");

			const IdentifierId src_name = type_member_name_by_rank(interp->types, src.type, src_member_info.rank);

			MemberInfo dst_member;

			OpcodeId unused_initializer;

			if (src_name != IdentifierId::INVALID)
			{
				const MemberByNameRst rst = type_member_info_by_name(interp->types, dst.type, src_name, &dst_member, &unused_initializer);

				if (rst == MemberByNameRst::NotFound)
					return record_interpreter_error(interp, code, CompileError::CompositeLiteralTargetIsMissingMember);

				ASSERT_OR_IGNORE(rst == MemberByNameRst::Ok);

				rank = dst_member.rank;
			}
			else
			{
				if (rank == dst_member_count)
					return record_interpreter_error(interp, code, CompileError::CompositeLiteralTargetHasTooFewMembers);

				if (!type_member_info_by_rank(interp->types, dst.type, static_cast<u16>(rank), &dst_member, &unused_initializer))
					ASSERT_UNREACHABLE;
			}

			u64* const seen_members_elem = seen_members + dst_member.rank / 64;

			const u64 member_bit = static_cast<u64>(1) << (dst_member.rank % 64);

			if ((*seen_members_elem & member_bit) != 0)
				return record_interpreter_error(interp, code, CompileError::CompositeLiteralTargetMemberMappedTwice);

			*seen_members_elem |= member_bit;

			const TypeMetrics dst_metrics = type_metrics_from_id(interp->types, dst_member.type_id);

			const TypeMetrics src_metrics = type_metrics_from_id(interp->types, src_member_info.type_id);

			const CTValue dst_member_value{ dst.bytes.mut_subrange(dst_member.offset, dst_metrics.size), dst_metrics.align, true, dst_member.type_id };

			const CTValue src_member_value{ src.bytes.mut_subrange(src_member_info.offset, src_metrics.size), src_metrics.align, false, src_member_info.type_id };

			if (convert_into(interp, code, src_member_value, dst_member_value) == nullptr)
				return nullptr;

			rank += 1;
		}

		for (u32 i = 0; i != dst_member_count; ++i)
		{
			const u64 seen_members_elem = seen_members[i >> 6];

			const u64 member_bit = static_cast<u64>(1) << (i & 63);

			if ((seen_members_elem & member_bit) != 0)
				continue;

			MemberInfo member;
			
			OpcodeId unused_initializer;
			
			if (!type_member_info_by_rank(interp->types, dst.type, static_cast<u16>(i), &member, &unused_initializer))
				ASSERT_UNREACHABLE;

			if (is_none(member.value_or_default_id))
				return record_interpreter_error(interp, code, CompileError::CompositeLiteralSourceIsMissingMember);

			const TypeMetrics member_metrics = type_metrics_from_id(interp->types, member.type_id);

			const MutRange<byte> default_dst = dst.bytes.mut_subrange(member.offset, member_metrics.size);

			const CTValue default_src = forever_value_get(interp->globals, get(member.value_or_default_id));

			range::mem_copy(default_dst, default_src.bytes.immut());
		}

		return code;
	}

	case TypeTag::ArrayLiteral:
	{
		ASSERT_OR_IGNORE(type_tag_from_id(interp->types, dst.type) == TypeTag::Array);

		const ArrayType* const dst_attach = type_attachment_from_id<ArrayType>(interp->types, dst.type);

		// Early-out here to avoid `get`ting the element type of `src` which
		// may be `none` if there are no elements.
		if (dst_attach->element_count == 0)
			return code;

		const ArrayType* const src_attach = type_attachment_from_id<ArrayType>(interp->types, src.type);

		ASSERT_OR_IGNORE(dst_attach->element_count == src_attach->element_count);

		const TypeId dst_elem_type = get(dst_attach->element_type);

		const TypeId src_elem_type = get(src_attach->element_type);

		if (type_is_equal(interp->types, dst_elem_type, src_elem_type))
		{
			range::mem_copy(dst.bytes, src.bytes.immut());
		}
		else
		{
			ASSERT_OR_IGNORE(type_can_implicitly_convert_from_to(interp->types, src_elem_type, dst_elem_type));

			const TypeMetrics dst_elem_metrics = type_metrics_from_id(interp->types, dst_elem_type);

			const TypeMetrics src_elem_metrics = type_metrics_from_id(interp->types, src_elem_type);

			for (u64 i = 0; i != dst_attach->element_count; ++i)
			{
				MutRange<byte> dst_elem_bytes = dst.bytes.mut_subrange(dst_elem_metrics.stride * i, dst_elem_metrics.size);

				MutRange<byte> src_elem_bytes = src.bytes.mut_subrange(src_elem_metrics.stride * i, src_elem_metrics.size);

				const CTValue dst_elem_value{ dst_elem_bytes, dst_elem_metrics.align, true, dst_elem_type };

				const CTValue src_elem_value{ src_elem_bytes, src_elem_metrics.align, false, src_elem_type };

				if (convert_into_assume_convertible(interp, code, src_elem_value, dst_elem_value) == nullptr)
					return nullptr;
			}
		}

		return code;
	}

	case TypeTag::INVALID:
	case TypeTag::INDIRECTION:
	case TypeTag::Void:
	case TypeTag::Type:
	case TypeTag::Definition:
	case TypeTag::Boolean:
	case TypeTag::TypeInfo:
	case TypeTag::TypeBuilder:
	case TypeTag::Divergent:
	case TypeTag::Integer:
	case TypeTag::Float:
	case TypeTag::Array:
	case TypeTag::Func:
	case TypeTag::Builtin:
	case TypeTag::Composite:
	case TypeTag::TailArray:
	case TypeTag::Variadic:
	case TypeTag::Trait:
		; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

static const Opcode* convert_into(Interpreter* interp, const Opcode* code, CTValue src, CTValue dst) noexcept
{
	const TypeRelation relation = type_relation(interp->types, src.type, dst.type);

	if (relation == TypeRelation::Equal)
	{
		range::mem_copy(dst.bytes, src.bytes.immut());

		return code;
	}
	else if (relation == TypeRelation::FirstConvertsToSecond)
	{
		return convert_into_assume_convertible(interp, code, src, dst);
	}
	else
	{
		ASSERT_OR_IGNORE(relation == TypeRelation::Unrelated || relation == TypeRelation::SecondConvertsToFirst);

		(void) record_interpreter_error(interp, code, CompileError::TypesCannotConvert);

		return nullptr;
	}
}

static Maybe<TypeId> unify(Interpreter* interp, const Opcode* code, CTValue* inout_lhs, CTValue* inout_rhs) noexcept
{
	const TypeRelation relation = type_relation(interp->types, inout_lhs->type, inout_rhs->type);

	if (relation == TypeRelation::Equal)
	{
		return some(inout_lhs->type < inout_rhs->type ? inout_lhs->type : inout_rhs->type);
	}
	else if (relation == TypeRelation::FirstConvertsToSecond)
	{
		const CTValue tmp_value = alloc_temporary_value_uninit(interp, inout_rhs->bytes.count(), inout_rhs->align, inout_rhs->type);

		if (convert_into_assume_convertible(interp, code, *inout_lhs, tmp_value) == nullptr)
			return none<TypeId>();

		*inout_lhs = tmp_value;

		return some(inout_rhs->type);
	}
	else if (relation == TypeRelation::SecondConvertsToFirst)
	{
		const CTValue tmp_value = alloc_temporary_value_uninit(interp, inout_lhs->bytes.count(), inout_lhs->align, inout_lhs->type);

		if (convert_into_assume_convertible(interp, code, *inout_rhs, tmp_value) == nullptr)
			return none<TypeId>();

		*inout_rhs = tmp_value;

		return some(inout_lhs->type);
	}
	else
	{
		ASSERT_OR_IGNORE(relation == TypeRelation::Unrelated);

		(void) record_interpreter_error(interp, code, CompileError::NoCommonArgumentType);

		return none<TypeId>();
	}
}

static CompareResult compare(Interpreter* interp, const Opcode* code, TypeId type, Range<byte> lhs, Range<byte> rhs) noexcept
{
	const TypeTag type_tag = type_tag_from_id(interp->types, type);

	switch (type_tag)
	{
	case TypeTag::Void:
	{
		return CompareResult{ CompareEquality::Equal };
	}

	case TypeTag::Type:
	case TypeTag::TypeInfo:
	case TypeTag::TypeBuilder:
	{
		const TypeId lhs_value = *range::access_as<TypeId>(lhs);

		const TypeId rhs_value = *range::access_as<TypeId>(rhs);

		const bool is_equal = type_is_equal(interp->types, lhs_value, rhs_value);

		return CompareResult{ is_equal ? CompareEquality::Equal : CompareEquality::Unequal };
	}

	case TypeTag::CompInteger:
	{
		const CompIntegerValue lhs_value = *range::access_as<CompIntegerValue>(lhs);

		const CompIntegerValue rhs_value = *range::access_as<CompIntegerValue>(rhs);

		const WeakCompareOrdering ordering = static_cast<WeakCompareOrdering>(comp_integer_compare(lhs_value, rhs_value));

		return CompareResult{ ordering };
	}

	case TypeTag::CompFloat:
	{
		const CompFloatValue lhs_value = *range::access_as<CompFloatValue>(lhs);

		const CompFloatValue rhs_value = *range::access_as<CompFloatValue>(rhs);

		const WeakCompareOrdering ordering = comp_float_compare(lhs_value, rhs_value);

		return CompareResult{ ordering };
	}

	case TypeTag::Boolean:
	{
		const bool lhs_value = *range::access_as<bool>(lhs);

		const bool rhs_value = *range::access_as<bool>(rhs);

		return CompareResult{ lhs_value == rhs_value ? CompareEquality::Equal : CompareEquality::Unequal };
	}

	case TypeTag::Integer:
	{
		const NumericType integer_type = *type_attachment_from_id<NumericType>(interp->types, type);

		const s64 compare_size = static_cast<s64>(integer_type.bits >> 3);

		const u8 extra_bits = integer_type.bits & 7;

		if (extra_bits != 0)
		{
			const u8 mask = static_cast<u8>((1 << extra_bits) - 1);

			const u8 lhs_masked = lhs[compare_size] & mask;

			const u8 rhs_masked = rhs[compare_size] & mask;

			if (lhs_masked != rhs_masked)
			{
				bool lhs_is_greater;

				if (integer_type.is_signed)
				{
					const u8 msb_mask = static_cast<u8>(1 << (extra_bits - 1));

					const bool lhs_is_negative = (lhs_masked & msb_mask) != 0;

					const bool rhs_is_negative = (rhs_masked & msb_mask) != 0;

					if (lhs_is_negative && rhs_is_negative)
						lhs_is_greater = lhs_masked < rhs_masked;
					else if (lhs_is_negative)
						lhs_is_greater = false;
					else if (rhs_is_negative)
						lhs_is_greater = true;
					else
						lhs_is_greater = lhs_masked > rhs_masked;
				}
				else
				{
					lhs_is_greater = lhs_masked > rhs_masked;
				}

				return CompareResult{ lhs_is_greater ? WeakCompareOrdering::GreaterThan : WeakCompareOrdering::LessThan };
			}
		}

		if (compare_size == 0)
			return CompareResult{ WeakCompareOrdering::Equal };

		s64 i = compare_size - 1;

		bool negate_comparison = false;

		if (integer_type.is_signed)
		{
			const bool lhs_is_negative = (lhs[i] & 0x80) != 0;

			const bool rhs_is_negative = (rhs[i] & 0x80) != 0;
			
			if (lhs_is_negative && rhs_is_negative)
				negate_comparison = true;
			else if (lhs_is_negative)
				return CompareResult{ WeakCompareOrdering::LessThan };
			else if (rhs_is_negative)
				return CompareResult{ WeakCompareOrdering::GreaterThan };
		}

		do
		{
			const u8 lhs_byte = lhs[i];

			const u8 rhs_byte = rhs[i];

			i -= 1;

			if (lhs_byte == rhs_byte)
				continue;

			if (lhs_byte < rhs_byte)
				return CompareResult{ negate_comparison ? WeakCompareOrdering::GreaterThan : WeakCompareOrdering::LessThan };
			else if (rhs_byte > lhs_byte)
				return CompareResult{ negate_comparison ? WeakCompareOrdering::LessThan : WeakCompareOrdering::GreaterThan };
		}
		while (i >= 0);

		return CompareResult{ WeakCompareOrdering::Equal };
	}

	case TypeTag::Float:
	{
		const NumericType float_type = *type_attachment_from_id<NumericType>(interp->types, type);

		if (float_type.bits == 32)
		{
			const f32 lhs_value = *range::access_as<f32>(lhs);

			const f32 rhs_value = *range::access_as<f32>(rhs);

			if (std::isnan(lhs_value) || std::isnan(rhs_value))
				return CompareResult{ WeakCompareOrdering::Unordered };
			else if (lhs_value < rhs_value)
				return CompareResult{ WeakCompareOrdering::LessThan };
			else if (lhs_value > rhs_value)
				return CompareResult{ WeakCompareOrdering::GreaterThan };
			else
				return CompareResult{ WeakCompareOrdering::Equal };
		}
		else
		{
			ASSERT_OR_IGNORE(float_type.bits == 64);

			const f64 lhs_value = *range::access_as<f64>(lhs);

			const f64 rhs_value = *range::access_as<f64>(rhs);

			if (std::isnan(lhs_value) || std::isnan(rhs_value))
				return CompareResult{ WeakCompareOrdering::Unordered };
			else if (lhs_value < rhs_value)
				return CompareResult{ WeakCompareOrdering::LessThan };
			else if (lhs_value > rhs_value)
				return CompareResult{ WeakCompareOrdering::GreaterThan };
			else
				return CompareResult{ WeakCompareOrdering::Equal };
		}
	}

	case TypeTag::Slice:
	{
		const Range<byte> lhs_value = *range::access_as<Range<byte>>(lhs);

		const Range<byte> rhs_value = *range::access_as<Range<byte>>(rhs);

		const bool is_equal = lhs_value.begin() == rhs_value.begin() && lhs_value.end() == rhs_value.end();

		return CompareResult{ is_equal ? CompareEquality::Equal : CompareEquality::Unequal };
	}

	case TypeTag::Ptr:
	{
		const void* lhs_value = *range::access_as<void*>(lhs);

		const void* rhs_value = *range::access_as<void*>(rhs);

		return CompareResult{ lhs_value == rhs_value ? CompareEquality::Equal : CompareEquality::Unequal };
	}

	case TypeTag::Array:
	case TypeTag::ArrayLiteral:
	{
		const ArrayType array_type = *type_attachment_from_id<ArrayType>(interp->types, type);

		if (is_none(array_type.element_type))
			return CompareResult{ CompareEquality::Equal };

		const TypeId element_type = get(array_type.element_type);

		const TypeMetrics metrics = type_metrics_from_id(interp->types, element_type);

		for (u64 i = 0; i != array_type.element_count; ++i)
		{
			const Range<byte> lhs_elem{ lhs.begin() + i * metrics.stride, metrics.size };

			const Range<byte> rhs_elem{ rhs.begin() + i * metrics.stride, metrics.size };

			const CompareResult result = compare(interp, code, element_type, lhs_elem, rhs_elem);

			if (result.tag == CompareTag::INVALID)
				return result;
			else if (result.equality != CompareEquality::Equal)
				return CompareResult{ CompareEquality::Unequal };
		}

		return CompareResult{ CompareEquality::Equal };
	}

	case TypeTag::Composite:
	{
		MemberIterator it = members_of(interp->types, type);

		while (has_next(&it))
		{
			MemberInfo member;
			
			OpcodeId unused_initializer;
			
			if (!next(&it, &member, &unused_initializer))
				TODO("Figure out what to do when comparing incomplete types and if it can even reasonably happen");

			const TypeMetrics metrics = type_metrics_from_id(interp->types, member.type_id);

			const Range<byte> lhs_member{ lhs.begin() + member.offset, metrics.size };

			const Range<byte> rhs_member{ rhs.begin() + member.offset, metrics.size };

			const CompareResult result = compare(interp, code, member.type_id, lhs_member, rhs_member);

			if (result.tag == CompareTag::INVALID)
				return CompareResult{};
			else if (result.equality != CompareEquality::Equal)
				return CompareResult{ CompareEquality::Unequal };
		}

		return CompareResult{ CompareEquality::Equal };
	}

	case TypeTag::Definition:
	case TypeTag::Undefined:
	case TypeTag::Func:
	case TypeTag::Builtin:
	case TypeTag::TailArray:
	case TypeTag::CompositeLiteral:
	case TypeTag::Variadic:
	case TypeTag::Divergent:
	case TypeTag::Trait:
		return CompareResult{};

	case TypeTag::INVALID:
	case TypeTag::INDIRECTION:
		; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

static const Opcode* scope_alloc_typed_member(Interpreter* interp, const Opcode* code, bool is_mut, TypeId type) noexcept
{
	const TypeMetrics member_metrics = type_metrics_from_id(interp->types, type);

	if (member_metrics.size >= UINT32_MAX)
		panic("Exceeded maximum size of stack variable.\n");

	interp->scope_data.pad_to_alignment(member_metrics.align);

	byte* const member_value = interp->scope_data.reserve(static_cast<u32>(member_metrics.size));

	ScopeMember* const member = interp->scope_members.reserve();
	member->offset = static_cast<u32>(member_value - interp->scope_data.begin());
	member->size = static_cast<u32>(member_metrics.size);
	member->align = member_metrics.align;
	member->is_mut = is_mut;
	member->type = type;

	const MutRange<byte> bytes = MutRange<byte>{ member_value, member_metrics.size };

	interp->write_ctxs.append(CTValue{ bytes, member_metrics.align, is_mut, type });

	return code;
}

static void scope_pop(Interpreter* interp) noexcept
{
	const Scope scope = interp->scopes.end()[-1];

	interp->scopes.pop_by(1);

	interp->temporary_data.pop_to(scope.temporary_data_used);

	if (scope.first_member_index != interp->scope_members.used())
	{
		const u32 scope_data_begin = interp->scope_members.begin()[scope.first_member_index].offset;

		interp->scope_members.pop_to(scope.first_member_index);

		interp->scope_data.pop_to(scope_data_begin);
	}
}



static bool u64_from_value(Interpreter* interp, const Opcode* code, CTValue value, u64* out) noexcept
{
	const TypeTag type_tag = type_tag_from_id(interp->types, value.type);

	if (type_tag == TypeTag::Integer)
	{
		const NumericType integer_type = *type_attachment_from_id<NumericType>(interp->types, value.type);

		if ((integer_type.bits & 7) != 0)
			TODO("Implement u64 extraction from non-byte-sized integer types");

		const u32 size = integer_type.bits / 8;

		if (integer_type.is_signed && (value.bytes[size - 1] & 0x80) != 0)
		{
			// TODO: Create a proper error code for this.
			(void) record_interpreter_error(interp, code, CompileError::ArrayIndexRhsTooLarge);

			return false;
		}

		for (u32 i = size; i > sizeof(u64); --i)
		{
			if (value.bytes[i - 1] != 0)
			{
				// TODO: Create a proper error code for this.
				(void) record_interpreter_error(interp, code, CompileError::ArrayIndexRhsTooLarge);

				return false;
			}
		}

		u64 result = 0;

		memcpy(&result, value.bytes.begin(), size);

		*out = result;

		return true;
	}
	else if (type_tag == TypeTag::CompInteger)
	{
		const CompIntegerValue int_value = *value_as<CompIntegerValue>(&value);

		if (!u64_from_comp_integer(int_value, 64, out))
		{
			// TODO: Create a proper error code for this.
			(void) record_interpreter_error(interp, code, CompileError::ArrayIndexRhsTooLarge);

			return false;
		}

		return true;
	}
	else
	{
		(void) record_interpreter_error(interp, code, CompileError::TypesCannotConvert);

		return false;
	}
}



template<typename T>
static const Opcode* code_attach(const Opcode* code, T* out) noexcept
{
	memcpy(out, code, sizeof(T));

	return code + sizeof(T);
}

static CTValue get_builtin_param_raw(Interpreter* interp, u8 rank) noexcept
{
	ASSERT_OR_IGNORE(interp->scopes.used() >= 1);

	Scope* const scope = interp->scopes.end() - 1;

	ASSERT_OR_IGNORE(rank + scope->first_member_index < interp->scope_members.used());

	ScopeMember* const member = interp->scope_members.begin() + scope->first_member_index + rank;

	const MutRange<byte> bytes{ interp->scope_data.begin() + member->offset, member->size };

	return CTValue{ bytes, member->align, member->is_mut, member->type };
}

template<typename T>
static T get_builtin_param(Interpreter* interp, u8 rank) noexcept
{
	CTValue value = get_builtin_param_raw(interp, rank);

	ASSERT_OR_IGNORE(value.bytes.count() == sizeof(T) && value.align == alignof(T));

	return *reinterpret_cast<T*>(value.bytes.begin());
}



static const Opcode* builtin_integer(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	const u8 bits = get_builtin_param<u8>(interp, 0);

	const bool is_signed = get_builtin_param<bool>(interp, 1);

	const TypeId type_type = type_create_simple(interp->types, TypeTag::Type);

	TypeId integer_type = type_create_numeric(interp->types, TypeTag::Integer, NumericType{ bits, is_signed });

	const MutRange<byte> bytes = range::from_object_bytes_mut(&integer_type);

	return push_temporary_value(interp, code, write_ctx, CTValue{ bytes, alignof(TypeId), true, type_type });
}

static const Opcode* builtin_float(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	const u8 bits = get_builtin_param<u8>(interp, 0);

	const TypeId type_type = type_create_simple(interp->types, TypeTag::Type);

	TypeId float_type = type_create_numeric(interp->types, TypeTag::Integer, NumericType{ bits, true });

	const MutRange<byte> bytes = range::from_object_bytes_mut(&float_type);

	return push_temporary_value(interp, code, write_ctx, CTValue{ bytes, alignof(TypeId), true, type_type });
}

static const Opcode* builtin_type(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	TypeId type_type = type_create_simple(interp->types, TypeTag::Type);

	const MutRange<byte> bytes = range::from_object_bytes_mut(&type_type);

	return push_temporary_value(interp, code, write_ctx, CTValue{ bytes, alignof(TypeId), true, type_type });
}

static const Opcode* builtin_definition(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	const TypeId type_type = type_create_simple(interp->types, TypeTag::Type);

	TypeId definition_type = type_create_simple(interp->types, TypeTag::Definition);

	const MutRange<byte> bytes = range::from_object_bytes_mut(&definition_type);

	return push_temporary_value(interp, code, write_ctx, CTValue{ bytes, alignof(TypeId), true, type_type });
}

static const Opcode* builtin_typeinfo(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	const TypeId type_type = type_create_simple(interp->types, TypeTag::Type);

	TypeId typeinfo_type = type_create_simple(interp->types, TypeTag::TypeInfo);

	const MutRange<byte> bytes = range::from_object_bytes_mut(&typeinfo_type);

	return push_temporary_value(interp, code, write_ctx, CTValue{ bytes, alignof(TypeId), true, type_type });
}

static const Opcode* builtin_typeof(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	CTValue arg = get_builtin_param_raw(interp, 0);

	const TypeId type_type = type_create_simple(interp->types, TypeTag::Type);

	return push_temporary_value(interp, code, write_ctx, CTValue{ arg.bytes, alignof(TypeId), true, type_type });
}

static const Opcode* builtin_returntypeof(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	const TypeId type = get_builtin_param<TypeId>(interp, 0);

	const TypeId type_type = type_create_simple(interp->types, TypeTag::Type);

	const TypeTag type_tag = type_tag_from_id(interp->types, type);

	if (type_tag != TypeTag::Func)
		return record_interpreter_error(interp, code, CompileError::TypesCannotConvert);

	const SignatureType2* const signature = type_attachment_from_id<SignatureType2>(interp->types, type);

	if (signature->has_templated_return_type)
		return record_interpreter_error(interp, code, CompileError::ReturntypeOfTemplatedReturnType);

	TypeId return_type = signature->return_type.type_id;

	const MutRange<byte> bytes = range::from_object_bytes_mut(&return_type);

	return push_temporary_value(interp, code, write_ctx, CTValue{ bytes, alignof(TypeId), true, type_type });
}

static const Opcode* builtin_sizeof(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	CTValue arg = get_builtin_param_raw(interp, 0);

	const TypeTag type_tag = type_tag_from_id(interp->types, arg.type);

	u64 size;

	if (type_tag == TypeTag::Type)
	{
		const TypeId indirect_type = *value_as<TypeId>(&arg);

		const TypeTag indirect_type_tag = type_tag_from_id(interp->types, indirect_type);

		if (indirect_type_tag == TypeTag::Type)
		{
			size = 0;
		}
		else
		{
			size = type_metrics_from_id(interp->types, indirect_type).size;
		}
	}
	else
	{
		size = type_metrics_from_id(interp->types, arg.type).size;
	}

	const TypeId comp_integer_type = type_create_simple(interp->types, TypeTag::CompInteger);

	CompIntegerValue size_value = comp_integer_from_u64(size);

	const MutRange<byte> bytes = range::from_object_bytes_mut(&size_value);

	return push_temporary_value(interp, code, write_ctx, CTValue{ bytes, alignof(CompIntegerValue), true, comp_integer_type });
}

static const Opcode* builtin_alignof(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	CTValue arg = get_builtin_param_raw(interp, 0);

	const TypeTag type_tag = type_tag_from_id(interp->types, arg.type);

	u32 align;

	if (type_tag == TypeTag::Type)
	{
		const TypeId indirect_type = *value_as<TypeId>(&arg);

		const TypeTag indirect_type_tag = type_tag_from_id(interp->types, indirect_type);

		if (indirect_type_tag == TypeTag::Type)
		{
			align = 1;
		}
		else
		{
			align = type_metrics_from_id(interp->types, indirect_type).align;
		}
	}
	else
	{
		align = type_metrics_from_id(interp->types, arg.type).align;
	}

	const TypeId comp_integer_type = type_create_simple(interp->types, TypeTag::CompInteger);

	CompIntegerValue align_value = comp_integer_from_u64(align);

	const MutRange<byte> bytes = range::from_object_bytes_mut(&align_value);

	return push_temporary_value(interp, code, write_ctx, CTValue{ bytes, alignof(CompIntegerValue), true, comp_integer_type });
}

static const Opcode* builtin_strideof(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	CTValue arg = get_builtin_param_raw(interp, 0);

	const TypeTag type_tag = type_tag_from_id(interp->types, arg.type);

	u64 stride;

	if (type_tag == TypeTag::Type)
	{
		const TypeId indirect_type = *value_as<TypeId>(&arg);

		const TypeTag indirect_type_tag = type_tag_from_id(interp->types, indirect_type);

		if (indirect_type_tag == TypeTag::Type)
		{
			stride = 0;
		}
		else
		{
			stride = type_metrics_from_id(interp->types, indirect_type).stride;
		}
	}
	else
	{
		stride = type_metrics_from_id(interp->types, arg.type).stride;
	}

	const TypeId comp_integer_type = type_create_simple(interp->types, TypeTag::CompInteger);

	CompIntegerValue stride_value = comp_integer_from_u64(stride);

	const MutRange<byte> bytes = range::from_object_bytes_mut(&stride_value);

	return push_temporary_value(interp, code, write_ctx, CTValue{ bytes, alignof(CompIntegerValue), true, comp_integer_type });
}

static const Opcode* builtin_offsetof(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	(void) interp;

	(void) code;

	(void) write_ctx;

	TODO("Implement");
}

static const Opcode* builtin_nameof(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	(void) interp;

	(void) code;

	(void) write_ctx;

	TODO("Implement");
}

static const Opcode* builtin_import(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	const Range<char8> path = get_builtin_param<Range<char8>>(interp, 0);

	const bool is_std = get_builtin_param<bool>(interp, 1);

	const SourceId from = get_builtin_param<SourceId>(interp, 2);

	ASSERT_OR_IGNORE(from != SourceId::INVALID);

	char8 absolute_path_buf[8192];

	const Range<char8> path_base = source_file_path_from_source_id(interp->reader, from);

	char8 path_base_parent_buf[8192];

	const u32 path_base_parent_chars = minos::path_to_absolute_directory(path_base, MutRange{ path_base_parent_buf });

	if (path_base_parent_chars == 0 || path_base_parent_chars > array_count(path_base_parent_buf))
		panic("Failed to get parent directory from `from` source file (0x%X).\n", minos::last_error());

	const u32 absolute_path_chars = minos::path_to_absolute_relative_to(path, Range{ path_base_parent_buf , path_base_parent_chars }, MutRange{ absolute_path_buf });

	if (absolute_path_chars == 0 || absolute_path_chars > array_count(absolute_path_buf))
		panic("Failed to make `path` %.*s absolute relative to `from` %.*s (0x%X).\n", static_cast<s32>(path.count()), path.begin(), static_cast<s32>(path_base.count()), path_base.begin(), minos::last_error());

	const Range<char8> absolute_path{ absolute_path_buf, absolute_path_chars };

	Maybe<TypeId> file_type = import_file(interp, absolute_path, is_std);

	if (is_none(file_type))
		return nullptr;

	const TypeId type_type = type_create_simple(interp->types, TypeTag::Type);

	const MutRange<byte> bytes = range::from_object_bytes_mut(&file_type);

	return push_temporary_value(interp, code, write_ctx, CTValue{ bytes, alignof(TypeId), true, type_type });
}

static const Opcode* builtin_create_type_builder(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	const SourceId source_id = get_builtin_param<SourceId>(interp, 0);

	const TypeId type_builder_type = type_create_simple(interp->types, TypeTag::TypeBuilder);

	TypeId builder = type_create_composite(interp->types, TypeTag::Composite, TypeDisposition::User, source_id, 0, false);

	const MutRange<byte> bytes = range::from_object_bytes_mut(&builder);

	return push_temporary_value(interp, code, write_ctx, CTValue{ bytes, alignof(TypeId), true, type_builder_type });
}

static const Opcode* builtin_add_type_member(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	(void) interp;

	(void) code;

	(void) write_ctx;

	TODO("Implement");
}

static const Opcode* builtin_complete_type(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	const TypeId builder = get_builtin_param<TypeId>(interp, 0);

	const u64 size = get_builtin_param<u64>(interp, 1);

	const u64 align = get_builtin_param<u64>(interp, 2);

	const u64 stride = get_builtin_param<u64>(interp, 3);

	if (align == 0)
		return record_interpreter_error(interp, code, CompileError::BuiltinCompleteTypeAlignZero);

	if (!is_pow2(align))
		return record_interpreter_error(interp, code, CompileError::BuiltinCompleteTypeAlignNotPowTwo);

	if (align > UINT32_MAX)
		return record_interpreter_error(interp, code, CompileError::BuiltinCompleteTypeAlignTooLarge);

	TypeId type = type_seal_composite(interp->types, builder, size, static_cast<u32>(align), stride);

	const TypeId type_type = type_create_simple(interp->types, TypeTag::Type);

	const MutRange<byte> bytes = range::from_object_bytes_mut(&type);

	return push_temporary_value(interp, code, write_ctx, CTValue{ bytes, alignof(TypeId), true, type_type });
}

static const Opcode* builtin_source_id(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->call_activation_indices.used() >= 1);

	const TypeId source_id_type = type_create_numeric(interp->types, TypeTag::Integer, NumericType{ 32, false });

	const u32 caller_activation_index = interp->call_activation_indices.end()[-1];

	ASSERT_OR_IGNORE(caller_activation_index < interp->activations.used());

	const OpcodeId caller_activation = interp->activations.begin()[caller_activation_index];

	const Opcode* const caller_activation_code = opcode_from_id(interp->opcodes, caller_activation);

	SourceId source_id = source_id_of_opcode(interp->opcodes, caller_activation_code);

	ASSERT_OR_IGNORE(source_id != SourceId::INVALID);

	const MutRange<byte> bytes = range::from_object_bytes_mut(&source_id);

	return push_temporary_value(interp, code, write_ctx, CTValue{ bytes, alignof(SourceId), true, source_id_type });
}

static const Opcode* builtin_caller_source_id(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	(void) interp;

	(void) code;

	(void) write_ctx;

	TODO("Implement");
}

static const Opcode* builtin_definition_typeof(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	(void) interp;

	(void) code;

	(void) write_ctx;

	TODO("Implement");
}



static const Opcode* handle_end_code([[maybe_unused]] Interpreter* interp, [[maybe_unused]] const Opcode* code, [[maybe_unused]] CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(write_ctx == nullptr);

	ASSERT_OR_IGNORE(interp->is_ok);

	return nullptr;
}

static const Opcode* handle_set_write_ctx(Interpreter* interp, const Opcode* code, [[maybe_unused]] CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->values.used() >= 1);

	ASSERT_OR_IGNORE(write_ctx == nullptr);

	CTValue* const top = interp->values.end() - 1;

	if (!top->is_mut)
		return record_interpreter_error(interp, code, CompileError::SetLhsNotMutable);

	interp->write_ctxs.append(*top);

	interp->values.pop_by(1);

	return code;
}

static const Opcode* handle_scope_begin(Interpreter* interp, const Opcode* code, [[maybe_unused]] CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(write_ctx == nullptr);

	u16 member_count;

	code = code_attach(code, &member_count);

	Scope* const scope = interp->scopes.reserve();
	scope->first_member_index = interp->scope_members.used();
	scope->temporary_data_used = interp->temporary_data.used();

	return code;
}

static const Opcode* handle_scope_end(Interpreter* interp, const Opcode* code, [[maybe_unused]] CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->scopes.used() >= 1);

	ASSERT_OR_IGNORE(write_ctx == nullptr);

	scope_pop(interp);

	return code;
}

static const Opcode* handle_scope_end_preserve_top(Interpreter* interp, const Opcode* code, [[maybe_unused]] CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->values.used() >= 1);

	ASSERT_OR_IGNORE(interp->scopes.used() >= 1);

	ASSERT_OR_IGNORE(write_ctx == nullptr);

	Scope* const scope = interp->scopes.end() - 1;

	CTValue* const top = interp->values.end() - 1;

	byte* const scope_temporary_data_begin = interp->temporary_data.begin() + scope->temporary_data_used;

	if (top->bytes.begin() >= scope_temporary_data_begin && top->bytes.begin() < interp->temporary_data.end())
	{
		memmove(scope_temporary_data_begin, top->bytes.begin(), top->bytes.count());

		scope->temporary_data_used += static_cast<u32>(top->bytes.count());

		top->bytes = MutRange<byte>{ scope_temporary_data_begin, top->bytes.count() };
	}

	scope_pop(interp);

	return code;
}

static const Opcode* handle_scope_alloc_typed(Interpreter* interp, const Opcode* code, [[maybe_unused]] CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->scopes.used() >= 1);

	ASSERT_OR_IGNORE(interp->values.used() >= 1);

	ASSERT_OR_IGNORE(write_ctx == nullptr);

	bool is_mut;

	code = code_attach(code, &is_mut);

	CTValue* const top = interp->values.end() -1;

	const TypeId type = top->type;

	const TypeTag type_tag = type_tag_from_id(interp->types, type);

	if (type_tag != TypeTag::Type)
		return record_interpreter_error(interp, code, CompileError::TypesCannotConvert);

	const TypeId member_type = *value_as<TypeId>(top);

	interp->values.pop_by(1);

	return scope_alloc_typed_member(interp, code, is_mut, member_type);
}

static const Opcode* handle_scope_alloc_untyped(Interpreter* interp, const Opcode* code, [[maybe_unused]] CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->scopes.used() >= 1);

	ASSERT_OR_IGNORE(interp->values.used() >= 1);

	ASSERT_OR_IGNORE(write_ctx == nullptr);

	bool is_mut;

	code = code_attach(code, &is_mut);

	ScopeMember* const member = interp->scope_members.reserve();

	CTValue* const top = interp->values.end() - 1;

	interp->scope_data.pad_to_alignment(top->align);

	byte* const member_value = interp->scope_data.reserve(static_cast<u32>(top->bytes.count()));

	member->offset = static_cast<u32>(member_value - interp->scope_data.begin());
	member->size = static_cast<u32>(top->bytes.count());
	member->align = top->align;
	member->is_mut = is_mut;
	member->type = top->type;

	memcpy(member_value, top->bytes.begin(), top->bytes.count());

	interp->values.pop_by(1);

	return code;
}

static const Opcode* handle_file_global_alloc_prepare(Interpreter* interp, const Opcode* code, [[maybe_unused]] CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(write_ctx == nullptr);

	bool is_mut;

	code = code_attach(code, &is_mut);

	GlobalFileIndex file_index;

	code = code_attach(code, &file_index);

	u16 rank;

	code = code_attach(code, &rank);

	file_value_alloc_prepare(interp->globals, file_index, rank, is_mut);

	GlobalInitialization* const init = interp->global_initializations.reserve();
	init->file_index = file_index;
	init->rank = rank;

	return code;
}

static const Opcode* handle_file_global_alloc_complete(Interpreter* interp, const Opcode* code, [[maybe_unused]] CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->global_initializations.used() >= 1);

	ASSERT_OR_IGNORE(write_ctx == nullptr);

	const GlobalInitialization init = interp->global_initializations.end()[-1];

	file_value_alloc_initialized_complete(interp->globals, init.file_index, init.rank);

	interp->global_initializations.pop_by(1);

	return code;
}

static const Opcode* handle_file_global_alloc_typed(Interpreter* interp, const Opcode* code, [[maybe_unused]] CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->values.used() >= 1);

	ASSERT_OR_IGNORE(interp->global_initializations.used() >= 1);

	ASSERT_OR_IGNORE(write_ctx == nullptr);

	const GlobalInitialization init = interp->global_initializations.end()[-1];

	CTValue* const top = interp->values.end() - 1;

	const TypeId type = top->type;

	const TypeTag type_tag = type_tag_from_id(interp->types, type);

	if (type_tag != TypeTag::Type)
		return record_interpreter_error(interp, code, CompileError::TypesCannotConvert);

	const TypeId member_type = *value_as<TypeId>(top);

	const TypeMetrics member_metrics = type_metrics_from_id(interp->types, member_type);

	TypeId file_type;

	const ForeverCTValue value = file_value_alloc_uninitialized(interp->globals, init.file_index, init.rank, member_type, member_metrics, &file_type);

	type_set_file_member_info(interp->types, file_type, init.rank, member_type, value.id);

	interp->values.pop_by(1);

	interp->write_ctxs.append(value.value);

	return code;
}

static const Opcode* handle_file_global_alloc_untyped(Interpreter* interp, const Opcode* code, [[maybe_unused]] CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->values.used() >= 1);

	ASSERT_OR_IGNORE(interp->global_initializations.used() >= 1);

	ASSERT_OR_IGNORE(write_ctx == nullptr);

	const GlobalInitialization init = interp->global_initializations.end()[-1];

	CTValue* const top = interp->values.end() - 1;

	TypeId file_type;

	const ForeverValueId value_id = file_value_alloc_initialized(interp->globals, init.file_index, init.rank, *top, &file_type);

	type_set_file_member_info(interp->types, file_type, init.rank, top->type, value_id);

	interp->global_initializations.pop_by(1);

	interp->values.pop_by(1);

	return code;
}

static const Opcode* handle_pop_closure(Interpreter* interp, const Opcode* code, [[maybe_unused]] CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->active_closures.used() >= 1);

	ASSERT_OR_IGNORE(write_ctx == nullptr);

	interp->active_closures.pop_by(1);

	return code;
}

static const Opcode* handle_load_scope(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->scopes.used() >= 1);

	u16 out;

	code = code_attach(code, &out);

	u16 rank;

	code = code_attach(code, &rank);

	ASSERT_OR_IGNORE(out < interp->scopes.used());

	Scope* const scope = interp->scopes.end() - out - 1;

	ASSERT_OR_IGNORE(rank + scope->first_member_index < interp->scope_members.used());

	ScopeMember* const member = interp->scope_members.begin() + scope->first_member_index + rank;

	CTValue loaded_value{MutRange<byte>{ interp->scope_data.begin() + member->offset, member->size }, member->align, member->is_mut, member->type };

	return push_location_value(interp, code, write_ctx, loaded_value);
}

static const Opcode* handle_load_global(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	const Opcode* const code_activation = code;

	GlobalFileIndex index;

	code = code_attach(code, &index);

	u16 rank;

	code = code_attach(code, &rank);

	ForeverCTValue global_value;
	
	OpcodeId global_code;

	const GlobalFileValueState state = file_value_get(interp->globals, index, rank, &global_value, &global_code);

	if (state == GlobalFileValueState::Complete)
	{
		return push_location_value(interp, code, write_ctx, global_value.value);
	}
	else if (state == GlobalFileValueState::Uninitialized)
	{
		// Push back the write_ctx if we have one, so it's still there on the
		// next go around.
		if (write_ctx != nullptr)
			interp->write_ctxs.append(*write_ctx);

		// We'll try again after having evaluated the global value's
		// initializer. Push this instruction as an activation.
		push_activation(interp, code_activation - 1);

		return opcode_from_id(interp->opcodes, global_code);
	}
	else
	{
		ASSERT_OR_IGNORE(state == GlobalFileValueState::Initializing);

		return record_interpreter_error(interp, code, CompileError::CyclicGlobalInitializerDependency);
	}
}

static const Opcode* handle_load_member(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->values.used() >= 1);

	const Opcode* const code_activation = code;

	IdentifierId name;

	code = code_attach(code, &name);

	CTValue* const top = interp->values.end() - 1;

	const TypeId type = top->type;

	const TypeTag type_tag = type_tag_from_id(interp->types, type);

	if (type_tag == TypeTag::Composite || type_tag == TypeTag::CompositeLiteral)
	{
		MemberInfo info;

		OpcodeId initializer_id;

		const MemberByNameRst rst = type_member_info_by_name(interp->types, type, name, &info, &initializer_id);

		if (rst == MemberByNameRst::NotFound)
		{
			return record_interpreter_error(interp, code, CompileError::MemberNoSuchName);
		}
		else if (rst == MemberByNameRst::Incomplete)
		{
			// Push back the write_ctx if we have one, so it's still there on the
			// next go around.
			if (write_ctx != nullptr)
				interp->write_ctxs.append(*write_ctx);

			// We'll try again after having evaluated the global value's
			// initializer. Push this instruction as an activation.
			push_activation(interp, code_activation - 1);

			return opcode_from_id(interp->opcodes, initializer_id);
		}
		else if (info.is_global)
		{
			ASSERT_OR_IGNORE(rst == MemberByNameRst::Ok);

			CTValue value = forever_value_get(interp->globals, get(info.value_or_default_id));

			return poppush_location_value(interp, code, write_ctx, value);
		}
		else
		{
			ASSERT_OR_IGNORE(rst == MemberByNameRst::Ok);

			const TypeMetrics metrics = type_metrics_from_id(interp->types, info.type_id);

			const MutRange<byte> bytes = top->bytes.mut_subrange(info.offset, metrics.size);

			return poppush_location_value(interp, code, write_ctx, CTValue{ bytes, metrics.align, info.is_mut, info.type_id });
		}
	}
	else if (type_tag == TypeTag::Type)
	{
		const TypeId type_value = *value_as<TypeId>(top);

		MemberInfo info;

		OpcodeId initializer_id;

		const MemberByNameRst rst = type_member_info_by_name(interp->types, type_value, name, &info, &initializer_id);

		if (rst == MemberByNameRst::NotFound)
		{
			return record_interpreter_error(interp, code, CompileError::MemberNoSuchName);
		}
		else if (rst == MemberByNameRst::Incomplete)
		{
			push_activation(interp, code_activation - 1);

			return opcode_from_id(interp->opcodes, initializer_id);
		}
		else if (info.is_global)
		{
			ASSERT_OR_IGNORE(rst == MemberByNameRst::Ok);

			CTValue value = forever_value_get(interp->globals, get(info.value_or_default_id));

			return poppush_location_value(interp, code, write_ctx, value);
		}
		else
		{
			ASSERT_OR_IGNORE(rst == MemberByNameRst::Ok);

			return record_interpreter_error(interp, code, CompileError::MemberNonGlobalAccessedThroughType);
		}
	}
	else
	{
		return record_interpreter_error(interp, code, CompileError::MemberInvalidLhsType);
	}
}

static const Opcode* handle_load_closure(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->active_closures.used() != 0);

	u16 rank;

	code = code_attach(code, &rank);

	const ClosureId closure = interp->active_closures.end()[-1];

	const ScopeMember* const member = interp->closure_members.begin() + static_cast<u32>(closure) + rank;

	const CTValue closure_value = forever_value_get(interp->globals, static_cast<ForeverValueId>(member->offset));

	return push_location_value(interp, code, write_ctx, closure_value);
}

static const Opcode* handle_load_builtin(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	u8 ordinal;

	code = code_attach(code, &ordinal);

	ASSERT_OR_IGNORE(ordinal != 0 && static_cast<u64>(ordinal - 1) < array_count(interp->builtin_infos));

	const BuiltinInfo info = interp->builtin_infos[ordinal - 1];

	ASSERT_OR_IGNORE(info.body != OpcodeId::INVALID);

	ASSERT_OR_IGNORE(info.signature_type != TypeId::INVALID);

	Callable body;
	body.body_id = info.body;
	body.closure_id = none<ClosureId>();

	const MutRange<byte> bytes = range::from_object_bytes_mut(&body);

	return push_temporary_value(interp, code, write_ctx, CTValue{ bytes, alignof(Callable), true, info.signature_type });
}

static const Opcode* handle_exec_builtin(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	static constexpr OpcodeHandlerFunc HANDLERS[] = {
		nullptr,
		&builtin_integer,
		&builtin_float,
		&builtin_type,
		&builtin_definition,
		&builtin_typeinfo,
		&builtin_typeof,
		&builtin_returntypeof,
		&builtin_sizeof,
		&builtin_alignof,
		&builtin_strideof,
		&builtin_offsetof,
		&builtin_nameof,
		&builtin_import,
		&builtin_create_type_builder,
		&builtin_add_type_member,
		&builtin_complete_type,
		&builtin_source_id,
		&builtin_caller_source_id,
		&builtin_definition_typeof,
	};

	static_assert(HANDLERS[static_cast<u8>(Builtin::Integer)]           == &builtin_integer);
	static_assert(HANDLERS[static_cast<u8>(Builtin::Float)]             == &builtin_float);
	static_assert(HANDLERS[static_cast<u8>(Builtin::Type)]              == &builtin_type);
	static_assert(HANDLERS[static_cast<u8>(Builtin::Definition)]        == &builtin_definition);
	static_assert(HANDLERS[static_cast<u8>(Builtin::TypeInfo)]          == &builtin_typeinfo);
	static_assert(HANDLERS[static_cast<u8>(Builtin::Typeof)]            == &builtin_typeof);
	static_assert(HANDLERS[static_cast<u8>(Builtin::Returntypeof)]      == &builtin_returntypeof);
	static_assert(HANDLERS[static_cast<u8>(Builtin::Sizeof)]            == &builtin_sizeof);
	static_assert(HANDLERS[static_cast<u8>(Builtin::Alignof)]           == &builtin_alignof);
	static_assert(HANDLERS[static_cast<u8>(Builtin::Strideof)]          == &builtin_strideof);
	static_assert(HANDLERS[static_cast<u8>(Builtin::Offsetof)]          == &builtin_offsetof);
	static_assert(HANDLERS[static_cast<u8>(Builtin::Nameof)]            == &builtin_nameof);
	static_assert(HANDLERS[static_cast<u8>(Builtin::Import)]            == &builtin_import);
	static_assert(HANDLERS[static_cast<u8>(Builtin::CreateTypeBuilder)] == &builtin_create_type_builder);
	static_assert(HANDLERS[static_cast<u8>(Builtin::AddTypeMember)]     == &builtin_add_type_member);
	static_assert(HANDLERS[static_cast<u8>(Builtin::CompleteType)]      == &builtin_complete_type);
	static_assert(HANDLERS[static_cast<u8>(Builtin::SourceId)]          == &builtin_source_id);
	static_assert(HANDLERS[static_cast<u8>(Builtin::CallerSourceId)]    == &builtin_caller_source_id);
	static_assert(HANDLERS[static_cast<u8>(Builtin::DefinitionTypeof)]  == &builtin_definition_typeof);

	u8 ordinal;

	code = code_attach(code, &ordinal);

	ASSERT_OR_IGNORE(ordinal != 0 && ordinal < array_count(HANDLERS));

	return HANDLERS[ordinal](interp, code, write_ctx);
}

static const Opcode* handle_signature(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	OpcodeSignatureFlags signature_flags;

	code = code_attach(code, &signature_flags);

	ASSERT_OR_IGNORE(!signature_flags.has_templated_parameter_list && !signature_flags.has_templated_return_type);

	u8 parameter_count;

	code = code_attach(code, &parameter_count);

	u8 value_count;

	code = code_attach(code, &value_count);

	ASSERT_OR_IGNORE(value_count >= 1 && interp->values.used() >= value_count);

	CTValue* value = interp->values.end() - value_count;

	const TypeId parameter_list_type = type_create_composite(interp->types, TypeTag::Composite, TypeDisposition::ParameterList, SourceId::INVALID, parameter_count, true);

	for (u32 i = 0; i != parameter_count; ++i)
	{
		IdentifierId parameter_name;

		code = code_attach(code, &parameter_name);

		OpcodeSignaturePerParameterFlags parameter_flags;

		code = code_attach(code, &parameter_flags);

		TypeId parameter_type;

		Maybe<ForeverValueId> parameter_default;

		if (parameter_flags.has_type && parameter_flags.has_default)
		{
			CTValue* const type_value = value;

			CTValue* const default_value = value + 1;

			value += 2;

			if (type_tag_from_id(interp->types, type_value->type) != TypeTag::Type)
				return record_interpreter_error(interp, code, CompileError::TypesCannotConvert);

			parameter_type = *value_as<TypeId>(type_value);

			ASSERT_OR_IGNORE(type_is_equal(interp->types, parameter_type, default_value->type));

			const ForeverValueId default_id = forever_value_alloc_initialized(interp->globals, false, *default_value);

			parameter_default = some(default_id);
		}
		else if (parameter_flags.has_type)
		{
			CTValue* const type_value = value;

			value += 1;

			if (type_tag_from_id(interp->types, type_value->type) != TypeTag::Type)
				return record_interpreter_error(interp, code, CompileError::TypesCannotConvert);

			parameter_type = *value_as<TypeId>(type_value);

			parameter_default = none<ForeverValueId>();
		}
		else
		{
			ASSERT_OR_IGNORE(parameter_flags.has_default);

			CTValue* const default_value = value;

			value += 1;

			parameter_type = default_value->type;

			const ForeverValueId default_id = forever_value_alloc_initialized(interp->globals, false, *default_value);

			parameter_default = some(default_id);
		}

		MemberInit init{};
		init.name = parameter_name;
		init.type_id = parameter_type;
		init.default_id = parameter_default;
		init.is_pub = false;
		init.is_mut = parameter_flags.is_mut;
		init.is_eval = parameter_flags.is_eval;
		init.offset = 0;

		if (!type_add_composite_member(interp->types, parameter_list_type, init))
			ASSERT_UNREACHABLE;
	}

	if (type_tag_from_id(interp->types, value->type) != TypeTag::Type)
		return record_interpreter_error(interp, code, CompileError::TypesCannotConvert);

	const TypeId return_type = *value_as<TypeId>(value);

	ASSERT_OR_IGNORE(value == interp->values.end() - 1);

	interp->values.pop_by(value_count - 1);

	SignatureType2 attach{};
	attach.parameter_list_type_id = parameter_list_type;
	attach.return_type.type_id = return_type;
	attach.closure_id = none<ClosureId>();
	attach.is_func = signature_flags.is_func;
	attach.has_templated_parameter_list = false;
	attach.has_templated_return_type = false;
	attach.parameter_count = parameter_count;

	TypeId signature_type = type_create_signature(interp->types, TypeTag::Func, attach);

	const MutRange<byte> bytes = range::from_object_bytes_mut(&signature_type);

	const TypeId type_type = type_create_simple(interp->types, TypeTag::Type);

	return poppush_temporary_value(interp, code, write_ctx, CTValue{ bytes, alignof(TypeId), true, type_type });
}

static const Opcode* handle_dyn_signature(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	OpcodeSignatureFlags signature_flags;

	code = code_attach(code, &signature_flags);

	u8 parameter_count;

	code = code_attach(code, &parameter_count);

	u8 value_count;

	code = code_attach(code, &value_count);

	u16 closed_over_value_count;

	code = code_attach(code, &closed_over_value_count);

	Maybe<OpcodeId> return_completion;

	if (signature_flags.has_templated_return_type)
		code = code_attach(code, &return_completion);
	else
		return_completion = none<OpcodeId>();

	const ClosureId closure = create_closure(interp, closed_over_value_count);

	CTValue* value = interp->values.end() - value_count;

	const TypeId parameter_list_type = type_create_composite(interp->types, TypeTag::Composite, TypeDisposition::ParameterList, SourceId::INVALID, parameter_count, true);

	for (u32 i = 0; i != parameter_count; ++i)
	{
		IdentifierId name;

		code = code_attach(code, &name);

		OpcodeSignaturePerParameterFlags parameter_flags;
		
		code = code_attach(code, &parameter_flags);

		if (parameter_flags.is_templated)
		{
			OpcodeId parameter_completion;

			code = code_attach(code, &parameter_completion);

			type_add_templated_parameter_list_member(interp->types, parameter_list_type, name, parameter_completion, parameter_flags.is_eval, parameter_flags.is_mut);
		}
		else
		{
			TypeId parameter_type;

			Maybe<ForeverValueId> parameter_default;

			if (parameter_flags.has_type && parameter_flags.has_default)
			{
				CTValue* const type_value = value;

				CTValue* const default_value = value + 1;

				value += 2;

				if (type_tag_from_id(interp->types, type_value->type) != TypeTag::Type)
					return record_interpreter_error(interp, code, CompileError::TypesCannotConvert);

				parameter_type = *value_as<TypeId>(type_value);

				const TypeMetrics parameter_metrics = type_metrics_from_id(interp->types, parameter_type);

				const ForeverCTValue default_dst = forever_value_alloc_uninitialized(interp->globals, false, parameter_type, parameter_metrics);

				if (convert_into(interp, code, *default_value, default_dst.value) == nullptr)
					return nullptr;

				parameter_default = some(default_dst.id);
			}
			else if (parameter_flags.has_type)
			{
				CTValue* const type_value = value;

				value += 1;

				if (type_tag_from_id(interp->types, type_value->type) != TypeTag::Type)
					return record_interpreter_error(interp, code, CompileError::TypesCannotConvert);

				parameter_type = *value_as<TypeId>(type_value);

				parameter_default = none<ForeverValueId>();
			}
			else
			{
				ASSERT_OR_IGNORE(parameter_flags.has_default);

				CTValue* const default_value = value;

				value += 1;

				parameter_type = default_value->type;

				const ForeverValueId default_id = forever_value_alloc_initialized(interp->globals, false, *default_value);

				parameter_default = some(default_id);
			}

			MemberInit init{};
			init.name = name;
			init.type_id = parameter_type;
			init.default_id = parameter_default;
			init.is_pub = false;
			init.is_mut = parameter_flags.is_mut;
			init.is_eval = parameter_flags.is_eval;
			init.offset = 0;

			if (!type_add_composite_member(interp->types, parameter_list_type, init))
				ASSERT_UNREACHABLE;	
		}
	}

	SignatureType2 attach{};
	attach.parameter_list_type_id = parameter_list_type;
	attach.closure_id = some(closure);
	attach.is_func = signature_flags.is_func;
	attach.has_templated_parameter_list = signature_flags.has_templated_parameter_list;
	attach.has_templated_return_type = signature_flags.has_templated_return_type;
	attach.parameter_count = parameter_count;

	if (signature_flags.has_templated_return_type)
	{
		attach.return_type.completion_id = get(return_completion);
	}
	else
	{
		if (type_tag_from_id(interp->types, value->type) != TypeTag::Type)
			return record_interpreter_error(interp, code, CompileError::TypesCannotConvert);

		attach.return_type.type_id = *value_as<TypeId>(value);
	}

	interp->values.pop_by(value_count);

	TypeId signature_type = type_create_signature(interp->types, TypeTag::Func, attach);

	const MutRange<byte> bytes = range::from_object_bytes_mut(&signature_type);

	const TypeId type_type = type_create_simple(interp->types, TypeTag::Type);

	return push_temporary_value(interp, code, write_ctx, CTValue{ bytes, alignof(TypeId), true, type_type });
}

static const Opcode* handle_bind_body(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	OpcodeId body_id;

	code = code_attach(code, &body_id);

	ASSERT_OR_IGNORE(interp->values.used() >= 1);

	CTValue* const top = interp->values.end() - 1;

	ASSERT_OR_IGNORE(type_tag_from_id(interp->types, top->type) == TypeTag::Type);

	const TypeId signature_type = *value_as<TypeId>(top);

	ASSERT_OR_IGNORE(type_tag_from_id(interp->types, signature_type) == TypeTag::Func);

	Callable callable{};
	callable.body_id = body_id;
	callable.closure_id = none<ClosureId>();

	const MutRange<byte> bytes = range::from_object_bytes_mut(&callable);

	return poppush_temporary_value(interp, code, write_ctx, CTValue{ bytes, alignof(Callable), true, signature_type });
}

static const Opcode* handle_bind_body_with_closure(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	OpcodeId body_id;

	code = code_attach(code, &body_id);

	u16 closed_over_value_count;

	code = code_attach(code, &closed_over_value_count);

	const ClosureId closure_id = create_closure(interp, closed_over_value_count);

	ASSERT_OR_IGNORE(interp->values.used() >= 1);

	CTValue* const top = interp->values.end() - 1;

	ASSERT_OR_IGNORE(type_tag_from_id(interp->types, top->type) == TypeTag::Type);

	const TypeId signature_type = *value_as<TypeId>(top);

	ASSERT_OR_IGNORE(type_tag_from_id(interp->types, signature_type) == TypeTag::Func);

	Callable callable{};
	callable.body_id = body_id;
	callable.closure_id = some(closure_id);

	const MutRange<byte> bytes = range::from_object_bytes_mut(&callable);

	return poppush_temporary_value(interp, code, write_ctx, CTValue{ bytes, alignof(Callable), true, signature_type });
}

static const Opcode* handle_prepare_args(Interpreter* interp, const Opcode* code, [[maybe_unused]] CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->values.used() >= 1);

	ASSERT_OR_IGNORE(write_ctx == nullptr);

	u8 argument_count;

	code = code_attach(code, &argument_count);

	const IdentifierId* const argument_names = reinterpret_cast<const IdentifierId*>(code);

	code += sizeof(IdentifierId) * argument_count;

	const OpcodeId* const argument_callbacks = reinterpret_cast<const OpcodeId*>(code);

	code += sizeof(OpcodeId) * argument_count;

	CTValue* const top = interp->values.end() - 1;

	const TypeId top_type = top->type;

	const TypeTag top_type_tag = type_tag_from_id(interp->types, top_type);

	if (top_type_tag != TypeTag::Func && top_type_tag != TypeTag::Builtin)
		return record_interpreter_error(interp, code, CompileError::TypesCannotConvert);

	SignatureType2 signature = *type_attachment_from_id<SignatureType2>(interp->types, top_type);

	OpcodeId* const ordered_callbacks = interp->argument_callbacks.reserve(signature.parameter_count);

	u64 templated_mask = 0;

	u64 seen_parameters = 0;

	u8 parameter_index = 0;

	for (u8 i = 0; i != argument_count; ++i)
	{
		const IdentifierId name = argument_names[i];

		if (name == IdentifierId::INVALID)
		{
			if (parameter_index >= signature.parameter_count)
				return record_interpreter_error(interp, code, CompileError::CallTooManyArgs);
		}
		else
		{
			MemberInfo parameter_info;

			OpcodeId unused_initializer;

			const MemberByNameRst rst = type_member_info_by_name(interp->types, signature.parameter_list_type_id, name, &parameter_info, &unused_initializer);

			if (rst == MemberByNameRst::NotFound)
				return record_interpreter_error(interp, code, CompileError::CallNoSuchNamedParameter);

			ASSERT_OR_IGNORE(rst == MemberByNameRst::Ok || rst == MemberByNameRst::Incomplete);

			parameter_index = static_cast<u8>(parameter_info.rank);

			if (rst == MemberByNameRst::Incomplete)
				templated_mask |= static_cast<u64>(1) << parameter_info.rank;
		}

		const u64 parameter_mask = static_cast<u64>(1) << parameter_index;

		if ((seen_parameters & parameter_mask) != 0)
			return record_interpreter_error(interp, code, CompileError::CallArgumentMappedTwice);

		seen_parameters |= parameter_mask;

		ordered_callbacks[parameter_index] = argument_callbacks[i];

		parameter_index += 1;
	}

	// Set up defaults for missing arguments.
	// Default "callbacks" receive the negation of their `ForeverValueId` to
	// distinguish them from - always positive, or rather less-than-2^31 -
	// `OpcodeId`s used for normal callbacks.
	for (u8 i = 0; i != signature.parameter_count; ++i)
	{
		const u64 parameter_mask = static_cast<u64>(1) << i;

		if ((seen_parameters & parameter_mask) != 0)
			continue;

		MemberInfo parameter_info;

		OpcodeId unused_initializer;

		if (!type_member_info_by_rank(interp->types, signature.parameter_list_type_id, i, &parameter_info, &unused_initializer))
			ASSERT_UNREACHABLE;

		if (is_none(parameter_info.value_or_default_id))
			return record_interpreter_error(interp, code, CompileError::CallMissingArg);

		ordered_callbacks[i] = static_cast<OpcodeId>(-static_cast<s32>(get(parameter_info.value_or_default_id)));
	}

	ArgumentPack* const argument_pack = interp->argument_packs.reserve();
	argument_pack->parameter_list_type = templated_mask == 0
		? signature.parameter_list_type_id
		: type_copy_composite(interp->types, signature.parameter_list_type_id, signature.parameter_count, true);
	argument_pack->return_type.completion = signature.return_type.completion_id;
	argument_pack->scope_first_member_index = interp->scope_members.used();
	argument_pack->count = signature.parameter_count;
	argument_pack->index = 0;
	argument_pack->has_templated_parameter_list = signature.has_templated_parameter_list;
	argument_pack->has_templated_return_type = signature.has_templated_return_type;
	argument_pack->has_just_completed_template_parameter = false;

	return code;
}

static const Opcode* handle_exec_args(Interpreter* interp, const Opcode* code, [[maybe_unused]] CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(write_ctx == nullptr);

	ASSERT_OR_IGNORE(interp->values.used() >= 1);

	ASSERT_OR_IGNORE(interp->argument_packs.used() >= 1);

	const Opcode* const code_activation = code;

	ArgumentPack* const argument_pack = interp->argument_packs.end() - 1;

	if (argument_pack->has_just_completed_template_parameter)
	{
		argument_pack->has_just_completed_template_parameter = false;

		interp->scopes.pop_by(1);
	}

	if (argument_pack->index < argument_pack->count)
	{
		// If there are still arguments that have not been processed, then
		// we check whether it is templated. If so, we transfer control to
		// the template callback and mark the parameter as non-templated so
		// we know not to do so again on the next round through. Otherwise
		// we transfer control to the argument value callback, and advance
		// the argument pack's index since we are done with the current
		// argument.

		MemberInfo parameter_info;

		OpcodeId parameter_initializer_id;

		OpcodeId callback_id;

		// We loop until we get to a parameter that has an argument. For all
		// the preceding ones, we set them to their default.
		// Note that default values are recorded by their negated
		// `ForeverValueId` being put into the initializer `OpcodeId`.
		while (true)
		{
			// If the parameter is incomplete it is templated, so we evaluate
			// its initializer in the callee's scope.
			// Since the initializer must not pop the callee scope as we need
			// it later, we set `has_just_completed_template_member` so that we
			// deactivate the scope on the next round through `handle_call`.
			if (!type_member_info_by_rank(interp->types, argument_pack->parameter_list_type, argument_pack->index, &parameter_info, &parameter_initializer_id))
			{
				// Set `temporary_data_used` to the nonsense value 0, since
				// this will never really be properly "popped", just
				// temporarily deactivated by removing it from the scope stack
				// without any effect on the scope members and temporary data
				// stacks.
				Scope* const signature_scope = interp->scopes.reserve();
				signature_scope->first_member_index = argument_pack->scope_first_member_index;
				signature_scope->temporary_data_used = 0;

				argument_pack->has_just_completed_template_parameter = true;

				push_activation(interp, code_activation - 1);

				return opcode_from_id(interp->opcodes, parameter_initializer_id);
			}

			if (scope_alloc_typed_member(interp, code, parameter_info.is_mut, parameter_info.type_id) == nullptr)
				return nullptr;

			callback_id = interp->argument_callbacks.end()[-argument_pack->count + argument_pack->index];

			argument_pack->index += 1;

			if (static_cast<s32>(callback_id) >= 0)
				break;

			CTValue default_value = forever_value_get(interp->globals, static_cast<ForeverValueId>(-static_cast<s32>(callback_id)));

			if (convert_into(interp, code, default_value, interp->write_ctxs.end()[-1]) == nullptr)
				ASSERT_UNREACHABLE;
		}

		push_activation(interp, code_activation - 1);

		return opcode_from_id(interp->opcodes, callback_id);
	}
	else if (argument_pack->has_templated_return_type)
	{
		// If the return type is templated, we run its initializer in the
		// callee scope, with the argument pack's return type as its write
		// context.

		// Set `temporary_data_used` to the nonsense value 0, since
		// this will never really be properly "popped", just
		// temporarily deactivated by removing it from the scope stack
		// without any effect on the scope members and temporary data
		// stacks.
		Scope* const signature_scope = interp->scopes.reserve();
		signature_scope->first_member_index = argument_pack->scope_first_member_index;
		signature_scope->temporary_data_used = interp->temporary_data.used();

		argument_pack->has_just_completed_template_parameter = true;

		argument_pack->has_templated_return_type = false;

		const TypeId type_type = type_create_simple(interp->types, TypeTag::Type);

		const MutRange<byte> bytes = range::from_object_bytes_mut(&argument_pack->return_type.type);

		interp->write_ctxs.append(CTValue{ bytes, alignof(TypeId), true, type_type });

		push_activation(interp, code_activation - 1);

		return opcode_from_id(interp->opcodes, argument_pack->return_type.completion);
	}
	else
	{
		// Look ahead to the following opcode, which must always be an
		// `Opcode::Call`. If it does not expects a write context, we need
		// to push one based on the callee's return type, since function
		// bodies always expect to be called with a write context on the
		// stack.
		// Note that we need to slip the return value onto the value stack
		// as well, but we must do so underneath the current top value,
		// which is the callee and is required on top by `handle_call`.
		// Note also that if the call expects its own write context, then
		// `handle_call` takes care of re-pushing for the callee.

		const Opcode* const call = code;

		ASSERT_OR_IGNORE(static_cast<Opcode>(static_cast<u8>(*call) & 0x7F) == Opcode::Call);

		const bool call_expects_write_ctx = (static_cast<u8>(*call) & 0x80) != 0;

		if (!call_expects_write_ctx)
		{
			const TypeMetrics return_type_metrics = type_metrics_from_id(interp->types, argument_pack->return_type.type);

			const CTValue return_value = alloc_temporary_value_uninit(interp, return_type_metrics.size, return_type_metrics.align, argument_pack->return_type.type);

			CTValue* const old_top = interp->values.end() - 1;

			interp->values.append(*old_top);

			*old_top = return_value;

			interp->write_ctxs.append(return_value);
		}

		// This time the signature scope will actually be popped by a
		// `Return` in the callee, so we properly set up its
		// `temporary_data_used` value.
		// Since all argument and return type data that has been put onto
		// the temporary stack so far has callee lifetime, we need to put
		// all of it into the callee scope, leaving the caller (signature)
		// scope with no initial data.
		Scope* const signature_scope = interp->scopes.reserve();
		signature_scope->first_member_index = argument_pack->scope_first_member_index;
		signature_scope->temporary_data_used = interp->temporary_data.used();

		// Finally, we get to pop the argument pack and callbacks before
		// proceeding to the actual call.
		interp->argument_callbacks.pop_by(argument_pack->count);

		interp->argument_packs.pop_by(1);

		return code;
	}
}

static const Opcode* handle_call(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->values.used() >= 1);

	ASSERT_OR_IGNORE(type_tag_from_id(interp->types, interp->values.end()[-1].type) == TypeTag::Func);

	const Callable callable = *value_as<Callable>(interp->values.end() - 1);

	interp->values.pop_by(1);

	// If we were called with a write context, re-push it for the callee's use.
	// Note that if we were called without a write context, `handle_exec_args`
	// has already taken care of pushing an artificial write context based on
	// the caller's return type.
	if (write_ctx != nullptr)
		interp->write_ctxs.append(*write_ctx);

	if (is_some(callable.closure_id))
		interp->active_closures.append(get(callable.closure_id));

	interp->call_activation_indices.append(interp->activations.used());

	push_activation(interp, code);

	return opcode_from_id(interp->opcodes, callable.body_id);
}

static const Opcode* handle_return(Interpreter* interp, [[maybe_unused]] const Opcode* code, [[maybe_unused]] CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->call_activation_indices.used() >= 1);

	ASSERT_OR_IGNORE(write_ctx == nullptr);

	scope_pop(interp);

	const u32 callee_activation = interp->call_activation_indices.end()[-1];

	interp->call_activation_indices.pop_by(1);

	interp->activations.pop_to(callee_activation + 1);

	return nullptr;
}

static const Opcode* handle_complete_param_typed_no_default(Interpreter* interp, const Opcode* code, [[maybe_unused]] CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->values.used() >= 1);

	ASSERT_OR_IGNORE(write_ctx == nullptr);

	ASSERT_OR_IGNORE(interp->argument_packs.used() >= 1);

	u8 rank;

	code = code_attach(code, &rank);

	CTValue* top = interp->values.end() - 1;

	const TypeId type = top->type;

	const TypeTag type_tag = type_tag_from_id(interp->types, type);

	if (type_tag != TypeTag::Type)
		return record_interpreter_error(interp, code, CompileError::TypesCannotConvert);

	const TypeId member_type = *value_as<TypeId>(top);

	interp->values.pop_by(1);

	ArgumentPack* const argument_pack = interp->argument_packs.end() - 1;

	const TypeId parameter_list_type = argument_pack->parameter_list_type;

	type_set_templated_parameter_list_member_info(interp->types, parameter_list_type, rank, member_type, none<ForeverValueId>());

	return code;
}

static const Opcode* handle_complete_param_typed_with_default(Interpreter* interp, const Opcode* code, [[maybe_unused]] CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->values.used() >= 2);

	ASSERT_OR_IGNORE(write_ctx == nullptr);

	ASSERT_OR_IGNORE(interp->argument_packs.used() >= 1);

	u8 rank;

	code = code_attach(code, &rank);

	CTValue* const type_value = interp->values.end() - 2;

	CTValue* const default_value = interp->values.end() - 1;

	const TypeId type = type_value->type;

	const TypeTag type_tag = type_tag_from_id(interp->types, type);

	if (type_tag != TypeTag::Type)
		return record_interpreter_error(interp, code, CompileError::TypesCannotConvert);

	const TypeId parameter_type = *value_as<TypeId>(type_value);

	const TypeMetrics parameter_metrics = type_metrics_from_id(interp->types, parameter_type);

	ArgumentPack* const argument_pack = interp->argument_packs.end() - 1;

	const TypeId parameter_list_type = argument_pack->parameter_list_type;

	ForeverCTValue default_dst = forever_value_alloc_uninitialized(interp->globals, false, parameter_type, parameter_metrics);

	if (convert_into(interp, code, *default_value, default_dst.value) == nullptr)
		return nullptr;

	type_set_templated_parameter_list_member_info(interp->types, parameter_list_type, rank, parameter_type, some(default_dst.id));

	interp->write_ctxs.append(default_dst.value);

	interp->values.pop_by(2);

	return code;
}

static const Opcode* handle_complete_param_untyped(Interpreter* interp, const Opcode* code, [[maybe_unused]] CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->values.used() >= 1);

	ASSERT_OR_IGNORE(write_ctx == nullptr);

	ASSERT_OR_IGNORE(interp->argument_packs.used() >= 1);

	u8 rank;

	code = code_attach(code, &rank);

	CTValue* const default_value = interp->values.end() - 1;

	ArgumentPack* const argument_pack = interp->argument_packs.end() - 1;

	const TypeId parameter_list_type = argument_pack->parameter_list_type;

	const ForeverValueId default_id = forever_value_alloc_initialized(interp->globals, false, *default_value);

	type_set_templated_parameter_list_member_info(interp->types, parameter_list_type, rank, default_value->type, some(default_id));

	interp->values.pop_by(1);

	return code;
}

static const Opcode* handle_array_preinit(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(write_ctx != nullptr);

	u16 index_count;

	code = code_attach(code, &index_count);

	ASSERT_OR_IGNORE(interp->values.used() >= index_count);

	u16 leading_element_count;

	code = code_attach(code, &leading_element_count);

	const TypeId dst_type = write_ctx->type;

	const TypeTag type_tag = type_tag_from_id(interp->types, dst_type);

	if (type_tag != TypeTag::Array && type_tag != TypeTag::ArrayLiteral)
		return record_interpreter_error(interp, code, CompileError::TypesCannotConvert);

	const ArrayType* const array_type = type_attachment_from_id<ArrayType>(interp->types, dst_type);

	if (is_none(array_type->element_type))
	{
		if (leading_element_count != 0 || index_count != 0)
			return record_interpreter_error(interp, code, CompileError::TypesCannotConvert);

		return code;
	}

	const TypeId dst_elem_type = get(array_type->element_type);

	const TypeMetrics dst_elem_metrics = type_metrics_from_id(interp->types, dst_elem_type);

	CTValue* const indices = interp->values.end() - index_count;

	const u64 dst_element_count = array_type->element_count;

	if (dst_element_count > UINT16_MAX)
		return record_interpreter_error(interp, code, CompileError::ArrayInitializerTooManyElements);

	if (dst_element_count < leading_element_count)
		return record_interpreter_error(interp, code, CompileError::TypesCannotConvert);

	for (u16 i = 0; i != leading_element_count; ++i)
	{
		const MutRange<byte> bytes = write_ctx->bytes.mut_subrange(i * dst_elem_metrics.stride, dst_elem_metrics.size);

		interp->write_ctxs.append(CTValue{ bytes, dst_elem_metrics.align, true, dst_elem_type });
	}

	if (index_count == 0)
	{
		return code;
	}

	SeenSet seen = seen_set_init(interp, static_cast<u16>(dst_element_count), leading_element_count);

	for (u16 i = 0; i != index_count; ++i)
	{
		u16 following_element_count;

		code = code_attach(code, &following_element_count);

		u64 index;

		if (!u64_from_value(interp, code, indices[i], &index))
			return nullptr;

		if (index + following_element_count > dst_element_count)
			return record_interpreter_error(interp, code, CompileError::TypesCannotConvert);

		if (!seen_set_set(seen, static_cast<u16>(index), following_element_count))
			return record_interpreter_error(interp, code, CompileError::ArrayInitializerDuplicateElement);

		for (u16 j = 0; j != following_element_count; ++j)
		{
			const MutRange<byte> bytes = write_ctx->bytes.mut_subrange((index + j) * dst_elem_metrics.stride, dst_elem_metrics.size);

			interp->write_ctxs.append(CTValue{ bytes, dst_elem_metrics.align, true, dst_elem_type });
		}
	}

	u16 unused_unseen_index;

	if (seen_set_next_unseen(seen, leading_element_count, &unused_unseen_index))
		return record_interpreter_error(interp, code, CompileError::ArrayInitializerMissingElement);

	return code;
}

static const Opcode* handle_array_postinit(Interpreter* interp, const Opcode* code, [[maybe_unused]] CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(write_ctx == nullptr);

	u16 total_element_count;

	code = code_attach(code, &total_element_count);

	u16 index_count;

	code = code_attach(code, &index_count);

	u16 leading_element_count;

	code = code_attach(code, &leading_element_count);

	if (total_element_count == 0)
	{
		ASSERT_OR_IGNORE(index_count == 0);

		const TypeId array_type = type_create_array(interp->types, TypeTag::ArrayLiteral, ArrayType{ 0, none<TypeId>() });

		CTValue rst = alloc_temporary_value_uninit(interp, 0, 1, array_type);

		return push_location_value(interp, code, write_ctx, rst);
	}

	ASSERT_OR_IGNORE(static_cast<u32>(total_element_count) + index_count <= interp->values.used());

	CTValue* const indices = interp->values.end() - total_element_count - index_count;

	CTValue* const element_values = interp->values.end() - total_element_count;

	// First, find the common type of all elements, or fail if there is none.

	TypeId element_type = element_values[0].type;

	for (u16 i = 1; i != total_element_count; ++i)
	{
		Maybe<TypeId> common_type = type_unify(interp->types, element_type, element_values[i].type);

		if (is_none(common_type))
			return record_interpreter_error(interp, code, CompileError::NoCommonArrayElementType);

		element_type = get(common_type);
	}

	const TypeMetrics element_metrics = type_metrics_from_id(interp->types, element_type);


	// Next, work out how large the array needs to be.

	u64 max_element_index = leading_element_count;

	const Opcode* const code_before_indices = code;

	for (u16 i = 0; i != index_count; ++i)
	{
		u64 index;

		if (!u64_from_value(interp, code, indices[i], &index))
			return record_interpreter_error(interp, code, CompileError::TypesCannotConvert);

		u16 following_element_count;

		code = code_attach(code, &following_element_count);

		if (index + following_element_count > max_element_index)
		{
			if (max_element_index > UINT16_MAX)
				return record_interpreter_error(interp, code, CompileError::ArrayInitializerTooManyElements);

			max_element_index = index + following_element_count;
		}
	}

	code = code_before_indices;

	const TypeId array_type = type_create_array(interp->types, TypeTag::ArrayLiteral, ArrayType{ max_element_index, some(element_type) });

	CTValue rst = alloc_temporary_value_uninit(interp, element_metrics.stride * (max_element_index - 1) + element_metrics.size, element_metrics.align, array_type);

	for (u16 i = 0; i != leading_element_count; ++i)
	{
		const MutRange<byte> bytes = rst.bytes.mut_subrange(element_metrics.stride * i, element_metrics.size);

		if (convert_into_assume_convertible(interp, code, element_values[i], CTValue{ bytes, element_metrics.align, true, element_type }) == nullptr)
			return nullptr;
	}

	if (index_count == 0)
	{
		interp->values.pop_by(total_element_count + index_count);

		return push_location_value(interp, code, write_ctx, rst);
	}

	SeenSet seen = seen_set_init(interp, static_cast<u16>(max_element_index), leading_element_count);

	u16 value_index = 0;

	for (u16 i = 0; i != index_count; ++i)
	{
		u64 base_index;

		if (!u64_from_value(interp, code, indices[i], &base_index))
			return nullptr;

		u16 following_element_count;

		code = code_attach(code, &following_element_count);

		ASSERT_OR_IGNORE(base_index + following_element_count <= max_element_index);

		if (!seen_set_set(seen, static_cast<u16>(base_index), following_element_count))
			return record_interpreter_error(interp, code, CompileError::ArrayInitializerDuplicateElement);

		for (u16 j = 0; j != following_element_count; ++i)
		{
			const MutRange<byte> bytes = rst.bytes.mut_subrange((base_index + j) * element_metrics.stride, element_metrics.size);

			if (convert_into_assume_convertible(interp, code, element_values[value_index + j], CTValue{ bytes, element_metrics.align, true, element_type }) == nullptr)
				return nullptr;
		}

		value_index += following_element_count;
	}

	interp->values.pop_by(total_element_count + index_count);

	u16 unseen_index;
	
	while (seen_set_next_unseen(seen, leading_element_count, &unseen_index))
	{
		TODO("Implement array literals with default value");
	}

	return push_location_value(interp, code, write_ctx, rst);
}

static const Opcode* handle_composite_preinit(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(write_ctx != nullptr);

	const TypeId dst_type = write_ctx->type;

	const TypeTag type_tag = type_tag_from_id(interp->types, dst_type);

	u16 names_count;

	code = code_attach(code, &names_count);

	if (type_tag != TypeTag::CompositeLiteral && type_tag != TypeTag::Composite)
		return record_interpreter_error(interp, code, CompileError::TypesCannotConvert);

	const u32 member_count = type_get_composite_member_count(interp->types, dst_type);

	u16 leading_member_count;

	code = code_attach(code, &leading_member_count);

	if (member_count < leading_member_count)
		return record_interpreter_error(interp, code, CompileError::CompositeLiteralTargetHasTooFewMembers);

	for (u16 i = 0; i != leading_member_count; ++i)
	{
		MemberInfo member_info;

		OpcodeId unused_initializer;

		if (!type_member_info_by_rank(interp->types, dst_type, i, &member_info, &unused_initializer))
			ASSERT_UNREACHABLE;

		ASSERT_OR_IGNORE(!member_info.is_global);

		const TypeMetrics member_metrics = type_metrics_from_id(interp->types, member_info.type_id);

		const MutRange<byte> bytes = write_ctx->bytes.mut_subrange(member_info.offset, member_metrics.size);

		interp->write_ctxs.append(CTValue{ bytes, member_metrics.align, write_ctx->is_mut, member_info.type_id });
	}

	if (names_count == 0)
	{
		for (u16 i = leading_member_count; i != member_count; ++i)
		{
			MemberInfo defaulted_member_info;

			OpcodeId unused_defaulted_member_initializer;

			if (!type_member_info_by_rank(interp->types, dst_type, i, &defaulted_member_info, &unused_defaulted_member_initializer))
				ASSERT_UNREACHABLE;

			if (defaulted_member_info.is_global)
				continue;

			if (is_none(defaulted_member_info.value_or_default_id))
				return record_interpreter_error(interp, code, CompileError::CompositeLiteralSourceIsMissingMember);

			const TypeMetrics defaulted_member_metrics = type_metrics_from_id(interp->types, defaulted_member_info.type_id);

			const MutRange<byte> default_dst = write_ctx->bytes.mut_subrange(defaulted_member_info.offset, defaulted_member_metrics.size);

			const Range<byte> default_src = forever_value_get(interp->globals, get(defaulted_member_info.value_or_default_id)).bytes.immut();

			range::mem_copy(default_dst, default_src);
		}

		return code;
	}

	SeenSet seen = seen_set_init(interp, static_cast<u16>(member_count), leading_member_count);

	for (u16 i = 0; i != names_count; ++i)
	{
		IdentifierId name;

		code = code_attach(code, &name);

		u16 following_member_count;

		code = code_attach(code, &following_member_count);

		MemberInfo named_member_info;

		OpcodeId unused_named_initializer;

		const MemberByNameRst rst = type_member_info_by_name(interp->types, dst_type, name, &named_member_info, &unused_named_initializer);

		if (rst == MemberByNameRst::NotFound)
			return record_interpreter_error(interp, code, CompileError::CompositeLiteralTargetIsMissingMember);

		ASSERT_OR_IGNORE(rst == MemberByNameRst::Ok);

		ASSERT_OR_IGNORE(!named_member_info.is_global);

		if (member_count < static_cast<u32>(named_member_info.rank) + 1 + following_member_count)
			return record_interpreter_error(interp, code, CompileError::CompositeLiteralTargetHasTooFewMembers);

		if (!seen_set_set(seen, named_member_info.rank, following_member_count + 1))
			return record_interpreter_error(interp, code, CompileError::CompositeLiteralTargetMemberMappedTwice);

		const TypeMetrics named_member_metrics = type_metrics_from_id(interp->types, named_member_info.type_id);

		const MutRange<byte> named_bytes = write_ctx->bytes.mut_subrange(named_member_info.offset, named_member_metrics.size);

		interp->write_ctxs.append(CTValue{ named_bytes, named_member_metrics.align, write_ctx->is_mut, named_member_info.type_id });

		for (u16 j = 0; j != following_member_count; ++j)
		{
			MemberInfo following_member_info;

			OpcodeId unused_following_initializer;

			if (!type_member_info_by_rank(interp->types, dst_type, named_member_info.rank + 1 + j, &following_member_info, &unused_following_initializer))
				ASSERT_UNREACHABLE;

			ASSERT_OR_IGNORE(!following_member_info.is_global);

			const TypeMetrics following_member_metrics = type_metrics_from_id(interp->types, following_member_info.type_id);

			const MutRange<byte> bytes = write_ctx->bytes.mut_subrange(following_member_info.offset, following_member_metrics.size);

			interp->write_ctxs.append(CTValue{ bytes, following_member_metrics.align, write_ctx->is_mut, following_member_info.type_id });
		}
	}

	u16 unseen_index = leading_member_count;

	while (seen_set_next_unseen(seen, unseen_index, &unseen_index))
	{
		MemberInfo defaulted_member_info;

		OpcodeId unused_defaulted_member_initializer;

		if (!type_member_info_by_rank(interp->types, dst_type, unseen_index, &defaulted_member_info, &unused_defaulted_member_initializer))
			ASSERT_UNREACHABLE;

		if (defaulted_member_info.is_global)
			continue;

		if (is_none(defaulted_member_info.value_or_default_id))
			return record_interpreter_error(interp, code, CompileError::CompositeLiteralSourceIsMissingMember);

		const TypeMetrics defaulted_member_metrics = type_metrics_from_id(interp->types, defaulted_member_info.type_id);

		const MutRange<byte> default_dst = write_ctx->bytes.mut_subrange(defaulted_member_info.offset, defaulted_member_metrics.size);

		const Range<byte> default_src = forever_value_get(interp->globals, get(defaulted_member_info.value_or_default_id)).bytes.immut();

		range::mem_copy(default_dst, default_src);
	}

	return code;
}

static const Opcode* handle_composite_postinit(Interpreter* interp, const Opcode* code, [[maybe_unused]] CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(write_ctx == nullptr);

	u16 total_member_count;

	code = code_attach(code, &total_member_count);

	ASSERT_OR_IGNORE(interp->values.used() >= total_member_count);

	CTValue* const values = interp->values.end() - total_member_count;

	const TypeId initializer_type = type_create_composite(interp->types, TypeTag::CompositeLiteral, TypeDisposition::Initializer, SourceId::INVALID, total_member_count, true);

	for (u16 i = 0; i != total_member_count; ++i)
	{
		IdentifierId name;

		code = code_attach(code, &name);

		MemberInit init{};
		init.name = name;
		init.type_id = values[i].type;
		init.default_id = none<ForeverValueId>();
		init.is_pub = false;
		init.is_mut = true;
		init.is_eval = false;
		init.offset = 0;

		if (!type_add_composite_member(interp->types, initializer_type, init))
			ASSERT_UNREACHABLE;
	}

	type_seal_composite(interp->types, initializer_type, 0, 0, 0);

	const TypeMetrics metrics = type_metrics_from_id(interp->types, initializer_type);

	MemberIterator it = members_of(interp->types, initializer_type);

	CTValue initializer = alloc_temporary_value_uninit(interp, metrics.size, metrics.align, initializer_type);

	for (u16 i = 0; i != total_member_count; ++i)
	{
		if (!has_next(&it))
			ASSERT_UNREACHABLE;

		MemberInfo member_info;
		
		OpcodeId unused_initializer;
		
		if (!next(&it, &member_info, &unused_initializer))
			TODO("Figure out what to do when post-initializing incomplete types and if it can even reasonably happen");

		CTValue value = values[i];

		range::mem_copy(initializer.bytes.mut_subrange(member_info.offset, value.bytes.count()), value.bytes.immut());
	}

	return push_location_value(interp, code, write_ctx, initializer);
}

static const Opcode* handle_if(Interpreter* interp, const Opcode* code, [[maybe_unused]] CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->values.used() >= 1);

	ASSERT_OR_IGNORE(write_ctx == nullptr);

	OpcodeId consequent;

	code = code_attach(code, &consequent);

	CTValue* const top = interp->values.end() - 1;

	const TypeId type = top->type;

	const TypeTag type_tag = type_tag_from_id(interp->types, type);

	if (type_tag != TypeTag::Boolean)
		return record_interpreter_error(interp, code, CompileError::TypesCannotConvert);

	const bool condition = *value_as<bool>(top);

	interp->values.pop_by(1);

	if (condition)
	{
		const Opcode* const next = opcode_from_id(interp->opcodes, consequent);

		push_activation(interp, code);

		return next;
	}
	else
	{
		return code;
	}
}

static const Opcode* handle_if_else(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->values.used() >= 1);

	OpcodeId consequent;

	code = code_attach(code, &consequent);

	OpcodeId alternative;

	code = code_attach(code, &alternative);

	if (write_ctx != nullptr)
		interp->write_ctxs.append(*write_ctx);

	CTValue* const top = interp->values.end() - 1;

	const TypeId type = top->type;

	const TypeTag type_tag = type_tag_from_id(interp->types, type);

	if (type_tag != TypeTag::Boolean)
		return record_interpreter_error(interp, code, CompileError::TypesCannotConvert);

	const bool condition = *value_as<bool>(top);

	interp->values.pop_by(1);

	const Opcode* const next = opcode_from_id(interp->opcodes, condition ? consequent : alternative);

	push_activation(interp, code);

	return next;
}

static const Opcode* handle_loop(Interpreter* interp, const Opcode* code, [[maybe_unused]] CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(write_ctx == nullptr);

	ASSERT_OR_IGNORE(interp->values.used() >= 1);

	OpcodeId condition_id;

	code = code_attach(code, &condition_id);

	OpcodeId body_id;

	code = code_attach(code, &body_id);

	CTValue* const top = interp->values.end() - 1;

	const TypeId type = top->type;

	const TypeTag type_tag = type_tag_from_id(interp->types, type);

	if (type_tag != TypeTag::Boolean)
		return record_interpreter_error(interp, code, CompileError::TypesCannotConvert);

	const bool condition = *value_as<bool>(top);

	if (condition)
	{
		push_activation(interp, condition_id);

		return opcode_from_id(interp->opcodes, body_id);
	}
	else
	{
		return code;
	}
}

static const Opcode* handle_loop_finally(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->values.used() >= 1);

	OpcodeId condition_id;

	code = code_attach(code, &condition_id);

	OpcodeId body_id;

	code = code_attach(code, &body_id);

	OpcodeId finally_id;

	code = code_attach(code, &finally_id);

	if (write_ctx != nullptr)
		interp->write_ctxs.append(*write_ctx);

	CTValue* const top = interp->values.end() - 1;

	const TypeId type = top->type;

	const TypeTag type_tag = type_tag_from_id(interp->types, type);

	if (type_tag != TypeTag::Boolean)
		return record_interpreter_error(interp, code, CompileError::TypesCannotConvert);

	const bool condition = *value_as<bool>(top);

	if (condition)
	{
		push_activation(interp, condition_id);

		return opcode_from_id(interp->opcodes, body_id);
	}
	else
	{
		push_activation(interp, code);

		return opcode_from_id(interp->opcodes, finally_id);
	}
}

static const Opcode* handle_switch(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	(void) interp;

	(void) code;

	(void) write_ctx;

	TODO("Implement");
}

static const Opcode* handle_address_of(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->values.used() >= 1);

	CTValue* const top = interp->values.end() - 1;

	const TypeId type = top->type;

	byte* ptr = top->bytes.begin();

	ReferenceType ptr_type{};
	ptr_type.referenced_type_id = type;
	ptr_type.is_opt = false;
	ptr_type.is_multi = false;
	ptr_type.is_mut = top->is_mut;

	const TypeId result_type = type_create_reference(interp->types, TypeTag::Ptr, ptr_type);

	const MutRange<byte> bytes = range::from_object_bytes_mut(&ptr);

	return poppush_temporary_value(interp, code, write_ctx, CTValue{ bytes, alignof(byte*), true, result_type });
}

static const Opcode* handle_dereference(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->values.used() >= 1);

	CTValue* const top = interp->values.end() - 1;

	const TypeId type = top->type;

	const TypeTag type_tag = type_tag_from_id(interp->types, type);

	if (type_tag != TypeTag::Ptr)
		return record_interpreter_error(interp, code, CompileError::TypesCannotConvert);

	byte* const top_value = *value_as<byte*>(top);

	const ReferenceType ptr_type = *type_attachment_from_id<ReferenceType>(interp->types, type);

	if (ptr_type.is_opt)
		return record_interpreter_error(interp, code, CompileError::DerefInvalidOperandType);

	const TypeMetrics metrics = type_metrics_from_id(interp->types, ptr_type.referenced_type_id);

	const MutRange<byte> bytes{ top_value, metrics.size };

	return poppush_location_value(interp, code, write_ctx, CTValue{ bytes, metrics.align, ptr_type.is_mut, ptr_type.referenced_type_id });
}

static const Opcode* handle_slice(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	OpcodeSliceKind kind;

	code = code_attach(code, &kind);

	const u8 argument_count = kind == OpcodeSliceKind::NoBounds ? 1 : kind == OpcodeSliceKind::BothBounds ? 3 : 2;

	ASSERT_OR_IGNORE(interp->values.used() >= argument_count);

	CTValue* const lhs = interp->values.end() - argument_count;

	const TypeId type = lhs->type;

	const TypeTag type_tag = type_tag_from_id(interp->types, type);

	u64 begin_index;

	u64 end_index;

	bool has_end_index;

	if (kind == OpcodeSliceKind::NoBounds)
	{
		begin_index = 0;

		end_index = 0;

		has_end_index = false;
	}
	else if (kind == OpcodeSliceKind::BeginBound)
	{
		CTValue* const begin = lhs + 1;

		if (!u64_from_value(interp, code, *begin, &begin_index))
			return record_interpreter_error(interp, code, CompileError::SliceOperatorIndexTooLarge);

		end_index = 0;

		has_end_index = false;
	}
	else if (kind == OpcodeSliceKind::EndBound)
	{
		CTValue* const end = lhs + 1;

		begin_index = 0;

		if (!u64_from_value(interp, code, *end, &end_index))
			return record_interpreter_error(interp, code, CompileError::SliceOperatorIndexTooLarge);

		has_end_index = true;
	}
	else
	{
		ASSERT_OR_IGNORE(kind == OpcodeSliceKind::BothBounds);

		CTValue* const begin = lhs + 1;

		CTValue* const end = lhs + 2;

		if (!u64_from_value(interp, code, *begin, &begin_index))
			return record_interpreter_error(interp, code, CompileError::SliceOperatorIndexTooLarge);

		if (!u64_from_value(interp, code, *end, &end_index))
			return record_interpreter_error(interp, code, CompileError::SliceOperatorIndexTooLarge);

		has_end_index = true;

		if (begin_index > end_index)
			return record_interpreter_error(interp, code, CompileError::SliceOperatorIndicesReversed);
	}

	if (type_tag == TypeTag::Array || type_tag == TypeTag::ArrayLiteral)
	{
		const ArrayType array_type = *type_attachment_from_id<ArrayType>(interp->types, type);

		if (is_none(array_type.element_type))
			return record_interpreter_error(interp, code, CompileError::SliceOperatorUntypedArrayLiteral);

		const u64 max_index = has_end_index ? end_index : begin_index;

		if (max_index >= array_type.element_count)
			return record_interpreter_error(interp, code, CompileError::SliceOperatorIndexOutOfBounds);


		const TypeId elem_type = get(array_type.element_type);

		const TypeMetrics elem_metrics = type_metrics_from_id(interp->types, elem_type);

		byte* const begin_ptr = lhs->bytes.begin() + begin_index * elem_metrics.stride;

		byte* const end_ptr = has_end_index ? lhs->bytes.begin() + end_index * elem_metrics.stride : lhs->bytes.end();

		MutRange<byte> slice{ begin_ptr, end_ptr };

		ReferenceType slice_type{};
		slice_type.referenced_type_id = elem_type;
		slice_type.is_opt = false;
		slice_type.is_multi = false;
		slice_type.is_mut = lhs->is_mut;

		const TypeId result_type = type_create_reference(interp->types, TypeTag::Slice, slice_type);

		const MutRange<byte> bytes = range::from_object_bytes_mut(&slice);

		interp->values.pop_by(argument_count - 1);

		return poppush_temporary_value(interp, code, write_ctx, CTValue{ bytes, alignof(MutRange<byte>), true, result_type });
	}
	else if (type_tag == TypeTag::Slice)
	{
		const ReferenceType slice_type = *type_attachment_from_id<ReferenceType>(interp->types, type);

		const TypeMetrics elem_metrics = type_metrics_from_id(interp->types, slice_type.referenced_type_id);

		MutRange<byte> lhs_value = *value_as<MutRange<byte>>(lhs);

		const u64 max_index = has_end_index ? end_index : begin_index + 1;

		if (max_index * elem_metrics.stride > lhs_value.count())
			return record_interpreter_error(interp, code, CompileError::SliceOperatorIndexOutOfBounds);

		byte* const begin_ptr = lhs_value.begin() + begin_index * elem_metrics.stride;

		byte* const end_ptr = has_end_index ? lhs_value.begin() + end_index * elem_metrics.stride : lhs_value.end();

		MutRange<byte> slice{ begin_ptr, end_ptr };

		const MutRange<byte> bytes = range::from_object_bytes_mut(&slice);

		interp->values.pop_by(argument_count - 1);

		return poppush_temporary_value(interp, code, write_ctx, CTValue{ bytes, alignof(MutRange<byte>), true, type });
	}
	else if (type_tag == TypeTag::Ptr)
	{
		const ReferenceType ptr_type = *type_attachment_from_id<ReferenceType>(interp->types, type);

		if (!ptr_type.is_multi)
			return record_interpreter_error(interp, code, CompileError::SliceOperatorInvalidLhsType);

		if (!has_end_index)
			return record_interpreter_error(interp, code, CompileError::SliceOperatorMultiPtrElidedEndIndex);

		const TypeMetrics elem_metrics = type_metrics_from_id(interp->types, ptr_type.referenced_type_id);

		byte* const lhs_value = *value_as<byte*>(lhs);

		MutRange<byte> slice{ lhs_value + begin_index * elem_metrics.stride, lhs_value + end_index * elem_metrics.stride };

		ReferenceType slice_type{};
		slice_type.referenced_type_id = ptr_type.referenced_type_id;
		slice_type.is_opt = false;
		slice_type.is_multi = false;
		slice_type.is_mut = ptr_type.is_mut;

		const TypeId result_type = type_create_reference(interp->types, TypeTag::Slice, slice_type);

		const MutRange<byte> bytes = range::from_object_bytes_mut(&slice);

		interp->values.pop_by(argument_count - 1);

		return poppush_temporary_value(interp, code, write_ctx, CTValue{ bytes, alignof(MutRange<byte>), true, result_type });
	}
	else
	{
		return record_interpreter_error(interp, code, CompileError::SliceOperatorInvalidLhsType);
	}
}

static const Opcode* handle_index(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->values.used() >= 2);

	CTValue* const lhs = interp->values.end() - 2;

	u64 index;

	if (!u64_from_value(interp, code, lhs[1], &index))
		return record_interpreter_error(interp, code, CompileError::ArrayIndexRhsTooLarge);

	const TypeId type = lhs->type;

	const TypeTag type_tag = type_tag_from_id(interp->types, type);

	if (type_tag == TypeTag::Array || type_tag == TypeTag::ArrayLiteral)
	{
		const ArrayType array_type = *type_attachment_from_id<ArrayType>(interp->types, type);

		if (is_none(array_type.element_type))
			return record_interpreter_error(interp, code, CompileError::SliceOperatorUntypedArrayLiteral);

		if (array_type.element_count <= index)
			return record_interpreter_error(interp, code, CompileError::ArrayIndexOutOfBounds);

		const TypeId element_type = get(array_type.element_type);

		const TypeMetrics element_metrics = type_metrics_from_id(interp->types, element_type);

		interp->values.pop_by(1);

		const MutRange<byte> bytes = lhs->bytes.mut_subrange(index * element_metrics.stride, element_metrics.size);

		return poppush_temporary_value(interp, code, write_ctx, CTValue{ bytes, element_metrics.align, true, element_type });
	}
	else if (type_tag == TypeTag::Slice)
	{
		const ReferenceType slice_type = *type_attachment_from_id<ReferenceType>(interp->types, type);

		const TypeId element_type = slice_type.referenced_type_id;

		const TypeMetrics element_metrics = type_metrics_from_id(interp->types, element_type);

		const MutRange<byte> slice = *value_as<MutRange<byte>>(lhs);

		if (index * element_metrics.stride > slice.count())
			return record_interpreter_error(interp, code, CompileError::ArrayIndexOutOfBounds);

		interp->values.pop_by(1);

		const MutRange<byte> bytes = lhs->bytes.mut_subrange(index * element_metrics.stride, element_metrics.size);

		return poppush_temporary_value(interp, code, write_ctx, CTValue{ bytes, element_metrics.align, true, element_type });
	}
	else if (type_tag == TypeTag::Ptr)
	{
		const ReferenceType ptr_type = *type_attachment_from_id<ReferenceType>(interp->types, type);

		if (!ptr_type.is_multi)
			return record_interpreter_error(interp, code, CompileError::TypesCannotConvert);

		const TypeId element_type = ptr_type.referenced_type_id;

		const TypeMetrics element_metrics = type_metrics_from_id(interp->types, element_type);

		byte* const ptr = *value_as<byte*>(lhs);

		MutRange<byte> bytes{ ptr + index * element_metrics.stride, element_metrics.size };

		return poppush_temporary_value(interp, code, write_ctx, CTValue{ bytes, element_metrics.align, true, element_type });
	}
	else
	{
		return record_interpreter_error(interp, code, CompileError::TypesCannotConvert);
	}
}

static const Opcode* handle_binary_arithmetic_op(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->values.used() >= 2);

	OpcodeBinaryArithmeticOpKind kind;

	code = code_attach(code, &kind);

	CTValue* const lhs = interp->values.end() - 2;
	
	CTValue* const rhs = lhs + 1;

	const Maybe<TypeId> unified_type = unify(interp, code, lhs, rhs);

	if (is_none(unified_type))
		return nullptr;

	const TypeId type = get(unified_type);

	const TypeTag type_tag = type_tag_from_id(interp->types, type);

	if (type_tag == TypeTag::Integer)
	{
		const NumericType integer_type = *type_attachment_from_id<NumericType>(interp->types, type);

		if (integer_type.bits <= 64 && is_pow2(integer_type.bits))
		{
			ASSERT_OR_IGNORE(integer_type.bits != 0);

			u64 lhs_value = 0;

			memcpy(&lhs_value, lhs->bytes.begin(), integer_type.bits / 8);

			u64 rhs_value = 0;

			memcpy(&rhs_value, rhs->bytes.begin(), integer_type.bits / 8);

			u64 result;

			if (integer_type.is_signed)
			{
				// Sign extend.
				const s64 lhs_value_signed = (static_cast<s64>(lhs_value) << (64 - integer_type.bits)) >> (64 - integer_type.bits);

				const s64 rhs_value_signed = (static_cast<s64>(rhs_value) << (64 - integer_type.bits)) >> (64 - integer_type.bits);

				s64 result_signed;

				if (kind == OpcodeBinaryArithmeticOpKind::Add || kind == OpcodeBinaryArithmeticOpKind::AddTC)
				{
					if (!add_checked_s64(lhs_value_signed, rhs_value_signed, &result_signed) && kind == OpcodeBinaryArithmeticOpKind::Add)
						return record_interpreter_error(interp, code, CompileError::ArithmeticOverflow);
				}
				else if (kind == OpcodeBinaryArithmeticOpKind::Sub || kind == OpcodeBinaryArithmeticOpKind::SubTC)
				{
					if (!sub_checked_s64(lhs_value_signed, rhs_value_signed, &result_signed) && kind == OpcodeBinaryArithmeticOpKind::Sub)
						return record_interpreter_error(interp, code, CompileError::ArithmeticOverflow);
				}
				else if (kind == OpcodeBinaryArithmeticOpKind::Mul || kind == OpcodeBinaryArithmeticOpKind::MulTC)
				{
					if (!mul_checked_s64(lhs_value_signed, rhs_value_signed, &result_signed) && kind == OpcodeBinaryArithmeticOpKind::Mul)
						return record_interpreter_error(interp, code, CompileError::ArithmeticOverflow);
				}
				else if (kind == OpcodeBinaryArithmeticOpKind::Div)
				{
					if (rhs_value_signed == 0)
						return record_interpreter_error(interp, code, CompileError::DivideByZero);

					result_signed = lhs_value_signed / rhs_value_signed;
				}
				else
				{
					ASSERT_OR_IGNORE(kind == OpcodeBinaryArithmeticOpKind::Mod);

					if (rhs_value_signed == 0)
						return record_interpreter_error(interp, code, CompileError::ModuloByZero);

					result_signed = lhs_value_signed % rhs_value_signed;
				}

				const s64 max_value = static_cast<s64>((static_cast<u64>(1) << (integer_type.bits - 1)) - 1);

				const s64 min_value = -max_value - 1;

				if (result_signed > max_value || result_signed < min_value)
					return record_interpreter_error(interp, code, CompileError::ArithmeticOverflow);

				result = static_cast<u64>(result_signed);
			}
			else
			{
				if (kind == OpcodeBinaryArithmeticOpKind::Add || kind == OpcodeBinaryArithmeticOpKind::AddTC)
				{
					if (!add_checked_u64(lhs_value, rhs_value, &result) && kind == OpcodeBinaryArithmeticOpKind::Add)
						return record_interpreter_error(interp, code, CompileError::ArithmeticOverflow);
				}
				else if (kind == OpcodeBinaryArithmeticOpKind::Sub || kind == OpcodeBinaryArithmeticOpKind::SubTC)
				{
					if (!sub_checked_u64(lhs_value, rhs_value, &result) && kind == OpcodeBinaryArithmeticOpKind::Sub)
						return record_interpreter_error(interp, code, CompileError::ArithmeticOverflow);
				}
				else if (kind == OpcodeBinaryArithmeticOpKind::Mul || kind == OpcodeBinaryArithmeticOpKind::MulTC)
				{
					if (!mul_checked_u64(lhs_value, rhs_value, &result) && kind == OpcodeBinaryArithmeticOpKind::Mul)
						return record_interpreter_error(interp, code, CompileError::ArithmeticOverflow);
				}
				else if (kind == OpcodeBinaryArithmeticOpKind::Div)
				{
					if (rhs_value == 0)
						return record_interpreter_error(interp, code, CompileError::DivideByZero);

					result = lhs_value / rhs_value;
				}
				else
				{
					ASSERT_OR_IGNORE(kind == OpcodeBinaryArithmeticOpKind::Mod);

					if (rhs_value == 0)
						return record_interpreter_error(interp, code, CompileError::ModuloByZero);

					result = lhs_value % rhs_value;
				}

				if (integer_type.bits != 64)
				{
					const u64 max_value = (static_cast<u64>(1) << integer_type.bits) - 1;

					if (result > max_value)
						return record_interpreter_error(interp, code, CompileError::ArithmeticOverflow);
				}
			}

			const MutRange<byte> bytes{ reinterpret_cast<byte*>(&result), static_cast<u64>(integer_type.bits / 8) };

			interp->values.pop_by(1);

			return poppush_temporary_value(interp, code, write_ctx, CTValue{ bytes, rhs->align, true, type });
		}
		else
		{
			CTValue result = alloc_temporary_value_uninit(interp, (integer_type.bits + 7 / 8), (integer_type.bits + 7 / 8), type);

			if (kind == OpcodeBinaryArithmeticOpKind::Add)
			{
				if (!bitwise_add(integer_type.bits, integer_type.is_signed, result.bytes, lhs->bytes.immut(), rhs->bytes.immut()))
					return record_interpreter_error(interp, code, CompileError::ArithmeticOverflow);
			}
			else if (kind == OpcodeBinaryArithmeticOpKind::Sub)
			{
				if (!bitwise_sub(integer_type.bits, integer_type.is_signed, result.bytes, lhs->bytes.immut(), rhs->bytes.immut()))
					return record_interpreter_error(interp, code, CompileError::ArithmeticOverflow);
			}
			else if (kind == OpcodeBinaryArithmeticOpKind::Mul)
			{
				if (!bitwise_mul(integer_type.bits, integer_type.is_signed, result.bytes, lhs->bytes.immut(), rhs->bytes.immut()))
					return record_interpreter_error(interp, code, CompileError::ArithmeticOverflow);
			}
			else if (kind == OpcodeBinaryArithmeticOpKind::Div)
			{
				if (!bitwise_div(integer_type.bits, integer_type.is_signed, result.bytes, lhs->bytes.immut(), rhs->bytes.immut()))
					return record_interpreter_error(interp, code, CompileError::DivideByZero);
			}
			else
			{
				ASSERT_OR_IGNORE(kind == OpcodeBinaryArithmeticOpKind::Mod);

				if (!bitwise_mod(integer_type.bits, integer_type.is_signed, result.bytes, lhs->bytes.immut(), rhs->bytes.immut()))
					return record_interpreter_error(interp, code, CompileError::ModuloByZero);
			}

			interp->values.pop_by(1);

			return poppush_temporary_value(interp, code, write_ctx, result);
		}
	}
	else if (type_tag == TypeTag::Float)
	{
		const NumericType float_type = *type_attachment_from_id<NumericType>(interp->types, type);

		if (float_type.bits == 32)
		{
			const f32 lhs_value = *value_as<f32>(lhs);

			const f32 rhs_value = *value_as<f32>(rhs);

			f32 result;

			if (kind == OpcodeBinaryArithmeticOpKind::Add)
				result = lhs_value + rhs_value;
			else if (kind == OpcodeBinaryArithmeticOpKind::Sub)
				result = lhs_value - rhs_value;
			else if (kind == OpcodeBinaryArithmeticOpKind::Mul)
				result = lhs_value * rhs_value;
			else if (kind == OpcodeBinaryArithmeticOpKind::Div)
				result = lhs_value / rhs_value;
			else if (kind == OpcodeBinaryArithmeticOpKind::Mod
			      || kind == OpcodeBinaryArithmeticOpKind::AddTC
			      || kind == OpcodeBinaryArithmeticOpKind::SubTC
			      || kind == OpcodeBinaryArithmeticOpKind::MulTC)
				return record_interpreter_error(interp, code, CompileError::BinaryOperatorIntegerInvalidArgumentType);
			else
				ASSERT_UNREACHABLE;

			const MutRange<byte> bytes = range::from_object_bytes_mut(&result);

			interp->values.pop_by(1);

			return poppush_temporary_value(interp, code, write_ctx, CTValue{ bytes, alignof(f32), true, type });
		}
		else
		{
			ASSERT_OR_IGNORE(float_type.bits == 64);

			const f64 lhs_value = *value_as<f64>(lhs);

			const f64 rhs_value = *value_as<f64>(rhs);

			f64 result;

			if (kind == OpcodeBinaryArithmeticOpKind::Add)
				result = lhs_value + rhs_value;
			else if (kind == OpcodeBinaryArithmeticOpKind::Sub)
				result = lhs_value - rhs_value;
			else if (kind == OpcodeBinaryArithmeticOpKind::Mul)
				result = lhs_value * rhs_value;
			else if (kind == OpcodeBinaryArithmeticOpKind::Div)
				result = lhs_value / rhs_value;
			else if (kind == OpcodeBinaryArithmeticOpKind::Mod
			      || kind == OpcodeBinaryArithmeticOpKind::AddTC
			      || kind == OpcodeBinaryArithmeticOpKind::SubTC
			      || kind == OpcodeBinaryArithmeticOpKind::MulTC)
				return record_interpreter_error(interp, code, CompileError::BinaryOperatorIntegerInvalidArgumentType);
			else
				ASSERT_UNREACHABLE;

			const MutRange<byte> bytes = range::from_object_bytes_mut(&result);

			interp->values.pop_by(1);

			return poppush_temporary_value(interp, code, write_ctx, CTValue{ bytes, alignof(f64), true, type });
		}
	}
	else if (type_tag == TypeTag::CompInteger)
	{
		const CompIntegerValue lhs_value = *value_as<CompIntegerValue>(lhs);

		const CompIntegerValue rhs_value = *value_as<CompIntegerValue>(rhs);

		CompIntegerValue result;

		if (kind == OpcodeBinaryArithmeticOpKind::Add || kind == OpcodeBinaryArithmeticOpKind::AddTC)
		{
			result = comp_integer_add(lhs_value, rhs_value);
		}
		else if (kind == OpcodeBinaryArithmeticOpKind::Sub || kind == OpcodeBinaryArithmeticOpKind::SubTC)
		{
			result = comp_integer_sub(lhs_value, rhs_value);
		}
		else if (kind == OpcodeBinaryArithmeticOpKind::Mul || kind == OpcodeBinaryArithmeticOpKind::MulTC)
		{
			result = comp_integer_mul(lhs_value, rhs_value);
		}
		else if (kind == OpcodeBinaryArithmeticOpKind::Div)
		{
			if (!comp_integer_div(lhs_value, rhs_value, &result))
				return record_interpreter_error(interp, code, CompileError::DivideByZero);
		}
		else
		{
			ASSERT_OR_IGNORE(kind == OpcodeBinaryArithmeticOpKind::Mod);

			if (!comp_integer_mod(lhs_value, rhs_value, &result))
				return record_interpreter_error(interp, code, CompileError::ModuloByZero);
		}

		const MutRange<byte> bytes = range::from_object_bytes_mut(&result);

		interp->values.pop_by(1);

		return poppush_temporary_value(interp, code, write_ctx, CTValue{ bytes, alignof(CompIntegerValue), true, type });
	}
	else if (type_tag == TypeTag::CompFloat)
	{
		const CompFloatValue lhs_value = *value_as<CompFloatValue>(lhs);

		const CompFloatValue rhs_value = *value_as<CompFloatValue>(rhs);

		CompFloatValue result;

		if (kind == OpcodeBinaryArithmeticOpKind::Add)
			result = comp_float_add(lhs_value, rhs_value);
		else if (kind == OpcodeBinaryArithmeticOpKind::Sub)
			result = comp_float_sub(lhs_value, rhs_value);
		else if (kind == OpcodeBinaryArithmeticOpKind::Mul)
			result = comp_float_mul(lhs_value, rhs_value);
		else if (kind == OpcodeBinaryArithmeticOpKind::Div)
			result = comp_float_div(lhs_value, rhs_value);
		else if (kind == OpcodeBinaryArithmeticOpKind::Mod
		      || kind == OpcodeBinaryArithmeticOpKind::AddTC
		      || kind == OpcodeBinaryArithmeticOpKind::SubTC
		      || kind == OpcodeBinaryArithmeticOpKind::MulTC)
			return record_interpreter_error(interp, code, CompileError::BinaryOperatorIntegerInvalidArgumentType);
		else
			ASSERT_UNREACHABLE;

		const MutRange<byte> bytes = range::from_object_bytes_mut(&result);

		interp->values.pop_by(1);

		return poppush_temporary_value(interp, code, write_ctx, CTValue{ bytes, alignof(CompFloatValue), true, type });
	}
	else
	{
		const bool allows_float = kind != OpcodeBinaryArithmeticOpKind::Mod
		                       && kind != OpcodeBinaryArithmeticOpKind::AddTC
		                       && kind != OpcodeBinaryArithmeticOpKind::SubTC
		                       && kind != OpcodeBinaryArithmeticOpKind::MulTC;

		const CompileError error = allows_float
			? CompileError::BinaryOperatorNumericInvalidArgumentType
			: CompileError::BinaryOperatorIntegerInvalidArgumentType;

		return record_interpreter_error(interp, code, error);
	}
}

static const Opcode* handle_shift(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->values.used() >= 2);

	OpcodeShiftKind kind;

	code = code_attach(code, &kind);

	CTValue* const lhs = interp->values.end() - 2;

	CTValue* const rhs = lhs + 1;

	const TypeId type = lhs->type;

	const TypeTag type_tag = type_tag_from_id(interp->types, type);

	u64 shift_amount;

	if (!u64_from_value(interp, code, *rhs, &shift_amount))
		return nullptr;

	if (type_tag == TypeTag::Integer)
	{
		const NumericType integer_type = *type_attachment_from_id<NumericType>(interp->types, type);

		if (shift_amount >= integer_type.bits)
			return record_interpreter_error(interp, code, CompileError::ShiftRHSTooLarge);

		CTValue result = alloc_temporary_value_uninit(interp, (integer_type.bits + 7) / 8, (integer_type.bits + 7) / 8, type);

		if (kind == OpcodeShiftKind::Left)
			bitwise_shift_left(integer_type.bits, result.bytes, lhs->bytes.immut(), shift_amount);
		else if (kind == OpcodeShiftKind::Right)
			bitwise_shift_right(integer_type.bits, result.bytes, lhs->bytes.immut(), shift_amount, integer_type.is_signed);
		else
			ASSERT_UNREACHABLE;

		interp->values.pop_by(1);

		return poppush_location_value(interp, code, write_ctx, result);
	}
	else if (type_tag == TypeTag::CompInteger)
	{
		const CompIntegerValue lhs_value = *value_as<CompIntegerValue>(lhs);

		const CompIntegerValue rhs_value = *value_as<CompIntegerValue>(rhs);

		CompIntegerValue result;

		if (kind == OpcodeShiftKind::Left)
		{
			if (!comp_integer_shift_left(lhs_value, comp_integer_from_u64(shift_amount), &result))
				return record_interpreter_error(interp, code, CompileError::ShiftRHSTooLarge);
		}
		else if (kind == OpcodeShiftKind::Right)
		{
			if (!comp_integer_shift_right(lhs_value, rhs_value, &result))
					return record_interpreter_error(interp, code, CompileError::ShiftRHSTooLarge);
		}
		else
		{
			ASSERT_UNREACHABLE;
		}

		const MutRange<byte> bytes = range::from_object_bytes_mut(&result);

		interp->values.pop_by(1);

		return poppush_temporary_value(interp, code, write_ctx, CTValue{ bytes, alignof(CompIntegerValue), true, type });
	}
	else
	{
		return record_interpreter_error(interp, code, CompileError::TypesCannotConvert);
	}
}

static const Opcode* handle_binary_bitwise_op(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->values.used() >= 2);

	OpcodeBinaryBitwiseOpKind kind;

	code = code_attach(code, &kind);

	CTValue* const lhs = interp->values.end() - 2;

	CTValue* const rhs = lhs + 1;

	const Maybe<TypeId> unified_type = unify(interp, code, lhs, rhs);

	if (is_none(unified_type))
		return nullptr;

	const TypeId type = get(unified_type);

	const TypeTag type_tag = type_tag_from_id(interp->types, type);

	if (type_tag == TypeTag::Integer)
	{
		const NumericType* const integer_type = type_attachment_from_id<NumericType>(interp->types, type);

		if (integer_type->bits <= 64 && is_pow2(integer_type->bits))
		{
			ASSERT_OR_IGNORE(integer_type->bits != 0);

			const u8 size = static_cast<u8>(integer_type->bits / 8);

			u64 lhs_value = 0;

			memcpy(&lhs_value, lhs->bytes.begin(), size);

			u64 rhs_value = 0;

			memcpy(&rhs_value, rhs->bytes.begin(), size);

			u64 result;
			
			if (kind == OpcodeBinaryBitwiseOpKind::And)
				result = lhs_value & rhs_value;
			else if (kind == OpcodeBinaryBitwiseOpKind::Or)
				result = lhs_value | rhs_value;
			else if (kind == OpcodeBinaryBitwiseOpKind::Xor)
				result = lhs_value ^ rhs_value;
			else
				ASSERT_UNREACHABLE;

			const MutRange<byte> bytes{ reinterpret_cast<byte*>(&result), size };

			interp->values.pop_by(1);

			return poppush_temporary_value(interp, code, write_ctx, CTValue{ bytes, alignof(u16), true, type });
		}
		else
		{
			TODO("Implement non-power-of-two-sized binary bitwise ops");
		}
	}
	else if (type_tag == TypeTag::CompInteger)
	{
		const CompIntegerValue lhs_value = *value_as<CompIntegerValue>(lhs);

		const CompIntegerValue rhs_value = *value_as<CompIntegerValue>(rhs);

		CompIntegerValue result;

		if (kind == OpcodeBinaryBitwiseOpKind::And)
		{
			if (!comp_integer_bit_and(lhs_value, rhs_value, &result))
				TODO("Fix comp_integer_bit_xor to work with negative values");
		}
		else if (kind == OpcodeBinaryBitwiseOpKind::Or)
		{
			if (!comp_integer_bit_or(lhs_value, rhs_value, &result))
				TODO("Fix comp_integer_bit_xor to work with negative values");
		}
		else if (kind == OpcodeBinaryBitwiseOpKind::Xor)
		{
			if (!comp_integer_bit_xor(lhs_value, rhs_value, &result))
				TODO("Fix comp_integer_bit_xor to work with negative values");
		}
		else
		{
			ASSERT_UNREACHABLE;
		}

		const MutRange<byte> bytes = range::from_object_bytes_mut(&result);

		interp->values.pop_by(1);

		return poppush_temporary_value(interp, code, write_ctx, CTValue{ bytes, alignof(CompIntegerValue), true, type });
	}
	else if (type_tag == TypeTag::Boolean)
	{
		const bool lhs_value = *value_as<bool>(lhs);

		const bool rhs_value = *value_as<bool>(rhs);

		bool result;
		
		if (kind == OpcodeBinaryBitwiseOpKind::And)
			result = lhs_value && rhs_value;
		else if (kind == OpcodeBinaryBitwiseOpKind::Or)
			result = lhs_value || rhs_value;
		else if (kind == OpcodeBinaryBitwiseOpKind::Xor)
			result = lhs_value ^ rhs_value;
		else
			ASSERT_UNREACHABLE;

		const MutRange<byte> bytes = range::from_object_bytes_mut(&result);

		interp->values.pop_by(1);

		return poppush_location_value(interp, code, write_ctx, CTValue{ bytes, alignof(bool), true, type });
	}
	else
	{
		return record_interpreter_error(interp, code, CompileError::BinaryOperatorIntegerOrBoolInvalidArgumentType);
	}
}

static const Opcode* handle_bit_not(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->values.used() >= 1);

	CTValue* const top = interp->values.end() - 1;

	const TypeId type = top->type;

	const TypeTag type_tag = type_tag_from_id(interp->types, type);

	if (type_tag == TypeTag::Integer)
	{
		const NumericType* const integer_type = type_attachment_from_id<NumericType>(interp->types, type);

		if (integer_type->bits <= 64 && is_pow2(integer_type->bits))
		{
			if (integer_type->bits == 8)
			{
				u8 value = ~*value_as<u8>(top);

				const MutRange<byte> bytes = range::from_object_bytes_mut(&value);

				return poppush_temporary_value(interp, code, write_ctx, CTValue{ bytes, top->align, true, type });
			}
			else if (integer_type->bits == 16)
			{
				u16 value = ~*value_as<u16>(top);

				const MutRange<byte> bytes = range::from_object_bytes_mut(&value);

				return poppush_temporary_value(interp, code, write_ctx, CTValue{ bytes, top->align, true, type });
			}
			else if (integer_type->bits == 32)
			{
				u32 value = static_cast<u32>(-*value_as<s32>(top));

				const MutRange<byte> bytes = range::from_object_bytes_mut(&value);

				return poppush_temporary_value(interp, code, write_ctx, CTValue{ bytes, top->align, true, type });
			}
			else
			{
				ASSERT_OR_IGNORE(integer_type->bits == 64);

				u64 value = static_cast<u64>(-*value_as<s64>(top));

				const MutRange<byte> bytes = range::from_object_bytes_mut(&value);

				return poppush_temporary_value(interp, code, write_ctx, CTValue{ bytes, top->align, true, type });
			}
		}
		else
		{
			TODO("Implement non-power-of-two-sized bitwise integer negation");
		}
	}
	else if (type_tag == TypeTag::CompInteger)
	{
		CompIntegerValue value = comp_integer_bit_not(*value_as<CompIntegerValue>(top));

		const MutRange<byte> bytes = range::from_object_bytes_mut(&value);

		return poppush_temporary_value(interp, code, write_ctx, CTValue{ bytes, top->align, true, type });
	}
	else
	{
		return record_interpreter_error(interp, code, CompileError::BitNotInvalidOperandType);
	}
}

static const Opcode* handle_logical_and(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->values.used() >= 2);

	CTValue* const lhs = interp->values.end() - 2;

	CTValue* const rhs = lhs + 1;

	const TypeId lhs_type = lhs->type;

	const TypeId rhs_type = rhs->type;

	const TypeTag lhs_type_tag = type_tag_from_id(interp->types, lhs_type);

	const TypeTag rhs_type_tag = type_tag_from_id(interp->types, rhs_type);

	if (lhs_type_tag != TypeTag::Boolean || rhs_type_tag != TypeTag::Boolean)
		return record_interpreter_error(interp, code, CompileError::TypesCannotConvert);

	const bool lhs_value = *value_as<bool>(lhs);

	const bool rhs_value = *value_as<bool>(rhs);

	bool value = lhs_value && rhs_value;

	const MutRange<byte> bytes = range::from_object_bytes_mut(&value);

	interp->values.pop_by(1);

	return poppush_temporary_value(interp, code, write_ctx, CTValue{ bytes, lhs->align, true, lhs_type });
}

static const Opcode* handle_logical_or(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->values.used() >= 2);

	ASSERT_OR_IGNORE(interp->values.used() >= 2);

	CTValue* const lhs = interp->values.end() - 2;

	CTValue* const rhs = lhs + 1;

	const TypeId lhs_type = lhs->type;

	const TypeId rhs_type = rhs->type;

	const TypeTag lhs_type_tag = type_tag_from_id(interp->types, lhs_type);

	const TypeTag rhs_type_tag = type_tag_from_id(interp->types, rhs_type);

	if (lhs_type_tag != TypeTag::Boolean || rhs_type_tag != TypeTag::Boolean)
		return record_interpreter_error(interp, code, CompileError::TypesCannotConvert);

	const bool lhs_value = *value_as<bool>(lhs);

	const bool rhs_value = *value_as<bool>(rhs);

	bool value = lhs_value || rhs_value;

	const MutRange<byte> bytes = range::from_object_bytes_mut(&value);

	interp->values.pop_by(1);

	return poppush_temporary_value(interp, code, write_ctx, CTValue{ bytes, lhs->align, true, lhs_type });
}

static const Opcode* handle_logical_not(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->values.used() >= 1);

	CTValue* const top = interp->values.end() - 1;

	const TypeId type = top->type;

	const TypeTag type_tag = type_tag_from_id(interp->types, type);

	if (type_tag != TypeTag::Boolean)
		return record_interpreter_error(interp, code, CompileError::TypesCannotConvert);

	bool value = !*value_as<bool>(top);

	const MutRange<byte> bytes = range::from_object_bytes_mut(&value);

	return poppush_temporary_value(interp, code, write_ctx, CTValue{ bytes, alignof(bool), true, type });
}

static const Opcode* handle_compare(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->values.used() >= 2);

	OpcodeCompareKind kind;

	code = code_attach(code, &kind);

	CTValue* const lhs = interp->values.end() - 2;

	CTValue* const rhs = lhs + 1;

	const Maybe<TypeId> unified_type = unify(interp, code, lhs, rhs);

	if (is_none(unified_type))
		return nullptr;

	const TypeId type = get(unified_type);

	const Range<byte> lhs_bytes = lhs->bytes.immut();

	const Range<byte> rhs_bytes = rhs->bytes.immut();

	const CompareResult compare_result = compare(interp, code, type, lhs_bytes, rhs_bytes);

	interp->values.pop_by(1);

	if (compare_result.tag == CompareTag::INVALID)
		return nullptr;

	bool result;

	if (kind == OpcodeCompareKind::Equal)
	{
		result = compare_result.equality == CompareEquality::Equal;
	}
	else if (kind == OpcodeCompareKind::NotEqual)
	{
		result = compare_result.equality != CompareEquality::Equal;
	}
	else
	{
		if (compare_result.tag == CompareTag::Equality)
			return record_interpreter_error(interp, code, CompileError::CompareUnorderedType);

		if (kind == OpcodeCompareKind::LessThan)
			result = compare_result.ordering == WeakCompareOrdering::LessThan;
		if (kind == OpcodeCompareKind::LessThanOrEqual)
			result = compare_result.ordering == WeakCompareOrdering::LessThan || compare_result.ordering == WeakCompareOrdering::Equal;
		else if (kind == OpcodeCompareKind::GreaterThan)
			result = compare_result.ordering == WeakCompareOrdering::GreaterThan;
		else if (kind == OpcodeCompareKind::GreaterThanOrEqual)
			result = compare_result.ordering == WeakCompareOrdering::GreaterThan || compare_result.ordering == WeakCompareOrdering::Equal;
		else
			ASSERT_UNREACHABLE;
	}

	const MutRange<byte> bytes = range::from_object_bytes_mut(&result);

	const TypeId bool_type = type_create_simple(interp->types, TypeTag::Boolean);

	return poppush_temporary_value(interp, code, write_ctx, CTValue{ bytes, alignof(bool), true, bool_type });
}

static const Opcode* handle_negate(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->values.used() >= 1);

	CTValue* const top = interp->values.end() - 1;

	const TypeId type = top->type;

	const TypeTag type_tag = type_tag_from_id(interp->types, type);

	if (type_tag == TypeTag::Integer)
	{
		const NumericType* const integer_type = type_attachment_from_id<NumericType>(interp->types, type);

		if (!integer_type->is_signed)
			return record_interpreter_error(interp, code, CompileError::NegateInvalidOperandType);

		if (integer_type->bits <= 64 && is_pow2(integer_type->bits))
		{
			if (integer_type->bits == 8)
			{
				s8 value = -*value_as<s8>(top);

				const MutRange<byte> bytes = range::from_object_bytes_mut(&value);

				return poppush_temporary_value(interp, code, write_ctx, CTValue{ bytes, top->align, true, type });
			}
			else if (integer_type->bits == 16)
			{
				s16 value = -*value_as<s16>(top);

				const MutRange<byte> bytes = range::from_object_bytes_mut(&value);

				return poppush_temporary_value(interp, code, write_ctx, CTValue{ bytes, top->align, true, type });
			}
			else if (integer_type->bits == 32)
			{
				s32 value = -*value_as<s32>(top);

				const MutRange<byte> bytes = range::from_object_bytes_mut(&value);

				return poppush_temporary_value(interp, code, write_ctx, CTValue{ bytes, top->align, true, type });
			}
			else
			{
				ASSERT_OR_IGNORE(integer_type->bits == 64);

				s64 value = -*value_as<s64>(top);

				const MutRange<byte> bytes = range::from_object_bytes_mut(&value);

				return poppush_temporary_value(interp, code, write_ctx, CTValue{ bytes, top->align, true, type });
			}
		}
		else
		{
			TODO("Implement non-power-of-two-sized arithmetic integer negation");
		}
	}
	else if (type_tag == TypeTag::Float)
	{
		const NumericType* const float_type = type_attachment_from_id<NumericType>(interp->types, type);

		if (float_type->bits == 32)
		{
			f32 value = -*value_as<f32>(top);

			const MutRange<byte> bytes = range::from_object_bytes_mut(&value);

			return poppush_temporary_value(interp, code, write_ctx, CTValue{ bytes, top->align, true, type });
		}
		else
		{
			ASSERT_OR_IGNORE(float_type->bits == 64);

			f64 value = -*value_as<f64>(top);

			const MutRange<byte> bytes = range::from_object_bytes_mut(&value);

			return poppush_temporary_value(interp, code, write_ctx, CTValue{ bytes, top->align, true, type });
		}
	}
	else if (type_tag == TypeTag::CompInteger)
	{
		CompIntegerValue value = comp_integer_neg(*value_as<CompIntegerValue>(top));

		const MutRange<byte> bytes = range::from_object_bytes_mut(&value);

		return poppush_temporary_value(interp, code, write_ctx, CTValue{ bytes, top->align, true, type });
	}
	else if (type_tag == TypeTag::CompFloat)
	{
		CompFloatValue value = comp_float_neg(*value_as<CompFloatValue>(top));

		const MutRange<byte> bytes = range::from_object_bytes_mut(&value);

		return poppush_temporary_value(interp, code, write_ctx, CTValue{ bytes, top->align, true, type });
	}
	else
	{
		return record_interpreter_error(interp, code, CompileError::NegateInvalidOperandType);
	}
}

static const Opcode* handle_unary_plus(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->values.used() >= 1);

	const CTValue* const top = interp->values.end() - 1;

	const TypeId type = top->type;

	const TypeTag type_tag = type_tag_from_id(interp->types, type);

	if (type_tag != TypeTag::Integer && type_tag != TypeTag::Float && type_tag != TypeTag::CompInteger && type_tag != TypeTag::CompFloat)
		return record_interpreter_error(interp, code, CompileError::UnaryPlusInvalidOperandType);

	return poppush_temporary_value(interp, code, write_ctx, *top);
}

static const Opcode* handle_array_type(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->values.used() >= 2);

	CTValue* const element_count_value = interp->values.end() - 1;

	CTValue* const element_type_value = element_count_value - 1;

	u64 element_count;

	if (!u64_from_value(interp, code, *element_count_value, &element_count))
		return record_interpreter_error(interp, code, CompileError::TypesCannotConvert);

	const TypeId element_type = *value_as<TypeId>(element_type_value);

	TypeId array_type = type_create_array(interp->types, TypeTag::Array, ArrayType{ element_count, some(element_type) });

	const MutRange<byte> bytes = range::from_object_bytes_mut(&array_type);

	const TypeId type_type = type_create_simple(interp->types, TypeTag::Type);

	return push_temporary_value(interp, code, write_ctx, CTValue{ bytes, alignof(TypeId), true, type_type });
}

static const Opcode* handle_reference_type(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(interp->values.used() >= 2);

	OpcodeReferenceTypeFlags flags;

	code = code_attach(code, &flags);

	CTValue* const referenced_type_value = interp->values.end() - 1;

	const TypeId referenced_type = *value_as<TypeId>(referenced_type_value);

	ReferenceType attach{};
	attach.referenced_type_id = referenced_type;
	attach.is_opt = flags.is_opt;
	attach.is_multi = flags.is_multi;
	attach.is_mut = flags.is_mut;

	TypeId reference_type = type_create_reference(interp->types, static_cast<TypeTag>(flags.tag), attach);

	const MutRange<byte> bytes = range::from_object_bytes_mut(&reference_type);

	const TypeId type_type = type_create_simple(interp->types, TypeTag::Type);

	return push_temporary_value(interp, code, write_ctx, CTValue{ bytes, alignof(TypeId), true, type_type });
}

static const Opcode* handle_undefined(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	if (write_ctx == nullptr)
	{
		const TypeId undefined_type = type_create_simple(interp->types, TypeTag::Undefined);

		return push_temporary_value(interp, code, write_ctx, CTValue{ {}, 1, true, undefined_type });
	}
	else
	{
		// Debug pattern for uninitialized memory.
		range::mem_set(write_ctx->bytes, 0xA6);

		return code;
	}
}

static const Opcode* handle_unreachable([[maybe_unused]] Interpreter* interp, [[maybe_unused]] const Opcode* code, [[maybe_unused]] CTValue* write_ctx) noexcept
{
	DEBUGBREAK;

	return record_interpreter_error(interp, code, CompileError::UnreachableReached);
}

static const Opcode* handle_value_integer(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	CompIntegerValue value;

	code = code_attach(code, &value);

	const MutRange<byte> bytes = range::from_object_bytes_mut(&value);

	const TypeId comp_integer_type = type_create_simple(interp->types, TypeTag::CompInteger);

	return push_temporary_value(interp, code, write_ctx, CTValue{ bytes, alignof(CompIntegerValue), true, comp_integer_type });
}

static const Opcode* handle_value_float(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	CompFloatValue value;

	code = code_attach(code, &value);

	const MutRange<byte> bytes = range::from_object_bytes_mut(&value);

	const TypeId comp_float_type = type_create_simple(interp->types, TypeTag::CompFloat);

	return push_temporary_value(interp, code, write_ctx, CTValue{ bytes, alignof(CompFloatValue), true, comp_float_type });
}

static const Opcode* handle_value_string(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	ForeverValueId value_id;

	code = code_attach(code, &value_id);

	CTValue value = forever_value_get(interp->globals, value_id);

	return push_temporary_value(interp, code, write_ctx, value);
}

static const Opcode* handle_value_void(Interpreter* interp, const Opcode* code, CTValue* write_ctx) noexcept
{
	const TypeId void_type = type_create_simple(interp->types, TypeTag::Void);

	const MutRange<byte> bytes{interp->temporary_data.end(), static_cast<u64>(0) };

	return push_temporary_value(interp, code, write_ctx, CTValue{ bytes, 1, true, void_type });
}

static const Opcode* handle_discard_void(Interpreter* interp, const Opcode* code, [[maybe_unused]] CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(write_ctx == nullptr);

	ASSERT_OR_IGNORE(interp->values.used() >= 1);

	CTValue* const top = interp->values.end() - 1;

	const TypeTag type_tag = type_tag_from_id(interp->types, top->type);

	if (type_tag != TypeTag::Void)
		return record_interpreter_error(interp, code, CompileError::ExpectedVoid);

	interp->values.pop_by(1);

	return code;
}

static const Opcode* handle_check_top_void(Interpreter* interp, const Opcode* code, [[maybe_unused]] CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(write_ctx == nullptr);

	ASSERT_OR_IGNORE(interp->values.used() >= 1);

	CTValue* const top = interp->values.end() - 1;

	const TypeTag type_tag = type_tag_from_id(interp->types, top->type);

	if (type_tag != TypeTag::Void)
		return record_interpreter_error(interp, code, CompileError::ExpectedVoid);

	return code;
}

static const Opcode* handle_check_write_ctx_void(Interpreter* interp, const Opcode* code, [[maybe_unused]] CTValue* write_ctx) noexcept
{
	ASSERT_OR_IGNORE(write_ctx == nullptr);

	ASSERT_OR_IGNORE(interp->write_ctxs.used() >= 1);

	CTValue* const top_write_ctx = interp->write_ctxs.end() - 1;

	const TypeTag type_tag = type_tag_from_id(interp->types, top_write_ctx->type);

	if (type_tag != TypeTag::Void)
		return record_interpreter_error(interp, code, CompileError::ExpectedVoid);

	return code;
}



static bool type_from_ast(Interpreter* interp, AstNode* ast, TypeId file_type, GlobalFileIndex file_index) noexcept
{
	AstDirectChildIterator it = direct_children_of(ast);

	u16 rank = 0;

	bool is_ok = true;

	while (has_next(&it))
	{
		AstNode* const node = next(&it);

		if (node->tag == AstTag::Impl)
			TODO("Handle impls");

		ASSERT_OR_IGNORE(node->tag == AstTag::Definition);

		const Maybe<Opcode*> initializer = opcodes_from_file_member_ast(interp->opcodes, node, file_index, rank);

		if (is_none(initializer))
		{
			is_ok = false;

			continue;
		}

		const OpcodeId initializer_id = id_from_opcode(interp->opcodes, get(initializer));

		const IdentifierId identifier_id = attachment_of<AstDefinitionData>(node)->identifier_id;

		if (interp->imported_opcodes_log_file.m_rep != nullptr)
			log_opcodes(interp, get(initializer));

		const bool is_pub = has_flag(node, AstFlag::Definition_IsPub);

		const bool is_mut = has_flag(node, AstFlag::Definition_IsMut);

		type_add_file_member(interp->types, file_type, identifier_id, initializer_id, is_pub, is_mut);

		file_value_set_initializer(interp->globals, file_index, rank, initializer_id);

		rank += 1;
	}

	return is_ok;
}

static Maybe<TypeId> import_file_or_prelude(Interpreter* interp, Range<char8> path, bool is_prelude, bool is_std, SourceFile** out_file) noexcept
{
	SourceFileRead read = read_source_file(interp->reader, path);

	if (read.source_file->has_error)
		return none<TypeId>();

	if (read.content.begin() == nullptr)
	{
		*out_file = read.source_file;

		return some(read.source_file->type);
	}

	const Maybe<AstNode*> maybe_ast = parse(interp->parser, read.content, read.source_file->source_id_base, is_std);

	if (is_none(maybe_ast))
	{
		read.source_file->has_error = true;

		return none<TypeId>();
	}

	AstNode* const ast = get(maybe_ast);

	const SourceId root_source_id = source_id_of_ast_node(interp->asts, ast);

	const AstFileData* const root_data = attachment_of<AstFileData>(ast);

	const TypeId type = type_create_composite(interp->types, TypeTag::Composite, TypeDisposition::File, root_source_id, root_data->member_count, true);

	const GlobalFileIndex file_index = file_values_reserve(interp->globals, type, static_cast<u16>(root_data->member_count));

	read.source_file->ast = id_from_ast_node(interp->asts, ast);
	read.source_file->type = type;
	read.source_file->file_index = file_index;

	if (is_prelude)
	{
		if (!set_prelude_scope(interp->lex, ast, file_index))
		{
			read.source_file->has_error = true;

			return none<TypeId>();
		}
	}
	else
	{
		if (!resolve_names(interp->lex, ast, file_index))
		{
			read.source_file->has_error = true;

			return none<TypeId>();
		}
	}

	if (interp->imported_asts_log_file.m_rep != nullptr)
		log_ast(interp, ast);

	if (!type_from_ast(interp, ast, type, file_index))
	{
		read.source_file->has_error = true;

		return none<TypeId>();
	}

	*out_file = read.source_file;

	return some(type);
}

static bool interpret_opcodes(Interpreter* interp, const Opcode* ops) noexcept
{
	static constexpr OpcodeHandlerFunc HANDLERS[] = {
		nullptr,                                   // INVALID
		&handle_end_code,                          // EndCode
		&handle_set_write_ctx,                     // SetWriteCtx
		&handle_scope_begin,                       // ScopeBegin
		&handle_scope_end,                         // ScopeEnd
		&handle_scope_end_preserve_top,            // ScopeEndPreserveTop
		&handle_scope_alloc_typed,                 // ScopeAllocTyped
		&handle_scope_alloc_untyped,               // ScopeAllocUntyped
		&handle_file_global_alloc_prepare,         // FileGlobalAllocPrepare
		&handle_file_global_alloc_complete,        // FileGlobalAllocComplete
		&handle_file_global_alloc_typed,           // FileGlobalAllocTyped
		&handle_file_global_alloc_untyped,         // FileGlobalAllocUntyped
		&handle_pop_closure,                       // PopClosure
		&handle_load_scope,                        // LoadScope
		&handle_load_global,                       // LoadGlobal
		&handle_load_member,                       // LoadMember
		&handle_load_closure,                      // LoadClosure
		&handle_load_builtin,                      // LoadBuiltin
		&handle_exec_builtin,                      // ExecBuiltin
		&handle_signature,                         // Signature
		&handle_dyn_signature,                     // DynSignature
		&handle_bind_body,                         // BindBody,
		&handle_bind_body_with_closure,            // BindBodyWithClosure
		&handle_prepare_args,                      // PrepareArgs
		&handle_exec_args,                         // ExecArgs
		&handle_call,                              // Call
		&handle_return,                            // Return
		&handle_complete_param_typed_no_default,   // CompleteParamTypedNoDefault
		&handle_complete_param_typed_with_default, // CompleteParamTypedWithDefault
		&handle_complete_param_untyped,            // CompleteParamUntyped
		&handle_array_preinit,                     // ArrayPreInit
		&handle_array_postinit,                    // ArrayPostInit
		&handle_composite_preinit,                 // CompositePreInit
		&handle_composite_postinit,                // CompositePostInit
		&handle_if,                                // If
		&handle_if_else,                           // IfElse
		&handle_loop,                              // Loop
		&handle_loop_finally,                      // LoopFinally
		&handle_switch,                            // Switch
		&handle_address_of,                        // AddressOf
		&handle_dereference,                       // Dereference
		&handle_slice,                             // Slice
		&handle_index,                             // Index
		&handle_binary_arithmetic_op,              // BinaryArithmeticOp
		&handle_shift,                             // Shift
		&handle_binary_bitwise_op,                 // BinaryBitwiseOp
		&handle_bit_not,                           // BitNot
		&handle_logical_and,                       // LogicalAnd
		&handle_logical_or,                        // LogicalOr
		&handle_logical_not,                       // LogicalNot
		&handle_compare,                           // Compare
		&handle_negate,                            // Negate
		&handle_unary_plus,                        // UnaryPlus
		&handle_array_type,                        // ArrayType
		&handle_reference_type,                    // ReferenceType
		&handle_undefined,                         // Undefined
		&handle_unreachable,                       // Unreachable
		&handle_value_integer,                     // ValueInteger
		&handle_value_float,                       // ValueFloat
		&handle_value_string,                      // ValueString
		&handle_value_void,                        // ValueVoid
		&handle_discard_void,                      // DiscardVoid
		&handle_check_top_void,                    // CheckTopVoid
		&handle_check_write_ctx_void,              // CheckWriteCtxVoid
	};

	static_assert(HANDLERS[static_cast<u8>(Opcode::EndCode)]                       == &handle_end_code);
	static_assert(HANDLERS[static_cast<u8>(Opcode::SetWriteCtx)]                   == &handle_set_write_ctx);
	static_assert(HANDLERS[static_cast<u8>(Opcode::ScopeBegin)]                    == &handle_scope_begin);
	static_assert(HANDLERS[static_cast<u8>(Opcode::ScopeEnd)]                      == &handle_scope_end);
	static_assert(HANDLERS[static_cast<u8>(Opcode::ScopeEndPreserveTop)]           == &handle_scope_end_preserve_top);
	static_assert(HANDLERS[static_cast<u8>(Opcode::ScopeAllocTyped)]               == &handle_scope_alloc_typed);
	static_assert(HANDLERS[static_cast<u8>(Opcode::ScopeAllocUntyped)]             == &handle_scope_alloc_untyped);
	static_assert(HANDLERS[static_cast<u8>(Opcode::FileGlobalAllocPrepare)]        == &handle_file_global_alloc_prepare);
	static_assert(HANDLERS[static_cast<u8>(Opcode::FileGlobalAllocComplete)]       == &handle_file_global_alloc_complete);
	static_assert(HANDLERS[static_cast<u8>(Opcode::FileGlobalAllocTyped)]          == &handle_file_global_alloc_typed);
	static_assert(HANDLERS[static_cast<u8>(Opcode::FileGlobalAllocUntyped)]        == &handle_file_global_alloc_untyped);
	static_assert(HANDLERS[static_cast<u8>(Opcode::PopClosure)]                    == &handle_pop_closure);
	static_assert(HANDLERS[static_cast<u8>(Opcode::LoadScope)]                     == &handle_load_scope);
	static_assert(HANDLERS[static_cast<u8>(Opcode::LoadGlobal)]                    == &handle_load_global);
	static_assert(HANDLERS[static_cast<u8>(Opcode::LoadMember)]                    == &handle_load_member);
	static_assert(HANDLERS[static_cast<u8>(Opcode::LoadClosure)]                   == &handle_load_closure);
	static_assert(HANDLERS[static_cast<u8>(Opcode::LoadBuiltin)]                   == &handle_load_builtin);
	static_assert(HANDLERS[static_cast<u8>(Opcode::ExecBuiltin)]                   == &handle_exec_builtin);
	static_assert(HANDLERS[static_cast<u8>(Opcode::Signature)]                     == &handle_signature);
	static_assert(HANDLERS[static_cast<u8>(Opcode::DynSignature)]                  == &handle_dyn_signature);
	static_assert(HANDLERS[static_cast<u8>(Opcode::BindBody)]                      == &handle_bind_body);
	static_assert(HANDLERS[static_cast<u8>(Opcode::BindBodyWithClosure)]           == &handle_bind_body_with_closure);
	static_assert(HANDLERS[static_cast<u8>(Opcode::PrepareArgs)]                   == &handle_prepare_args);
	static_assert(HANDLERS[static_cast<u8>(Opcode::ExecArgs)]                      == &handle_exec_args);
	static_assert(HANDLERS[static_cast<u8>(Opcode::Call)]                          == &handle_call);
	static_assert(HANDLERS[static_cast<u8>(Opcode::Return)]                        == &handle_return);
	static_assert(HANDLERS[static_cast<u8>(Opcode::CompleteParamTypedNoDefault)]   == &handle_complete_param_typed_no_default);
	static_assert(HANDLERS[static_cast<u8>(Opcode::CompleteParamTypedWithDefault)] == &handle_complete_param_typed_with_default);
	static_assert(HANDLERS[static_cast<u8>(Opcode::CompleteParamUntyped)]          == &handle_complete_param_untyped);
	static_assert(HANDLERS[static_cast<u8>(Opcode::ArrayPreInit)]                  == &handle_array_preinit);
	static_assert(HANDLERS[static_cast<u8>(Opcode::ArrayPostInit)]                 == &handle_array_postinit);
	static_assert(HANDLERS[static_cast<u8>(Opcode::CompositePreInit)]              == &handle_composite_preinit);
	static_assert(HANDLERS[static_cast<u8>(Opcode::CompositePostInit)]             == &handle_composite_postinit);
	static_assert(HANDLERS[static_cast<u8>(Opcode::If)]                            == &handle_if);
	static_assert(HANDLERS[static_cast<u8>(Opcode::IfElse)]                        == &handle_if_else);
	static_assert(HANDLERS[static_cast<u8>(Opcode::Loop)]                          == &handle_loop);
	static_assert(HANDLERS[static_cast<u8>(Opcode::LoopFinally)]                   == &handle_loop_finally);
	static_assert(HANDLERS[static_cast<u8>(Opcode::Switch)]                        == &handle_switch);
	static_assert(HANDLERS[static_cast<u8>(Opcode::AddressOf)]                     == &handle_address_of);
	static_assert(HANDLERS[static_cast<u8>(Opcode::Dereference)]                   == &handle_dereference);
	static_assert(HANDLERS[static_cast<u8>(Opcode::Slice)]                         == &handle_slice);
	static_assert(HANDLERS[static_cast<u8>(Opcode::Index)]                         == &handle_index);
	static_assert(HANDLERS[static_cast<u8>(Opcode::BinaryArithmeticOp)]            == &handle_binary_arithmetic_op);
	static_assert(HANDLERS[static_cast<u8>(Opcode::Shift)]                         == &handle_shift);
	static_assert(HANDLERS[static_cast<u8>(Opcode::BinaryBitwiseOp)]               == &handle_binary_bitwise_op);
	static_assert(HANDLERS[static_cast<u8>(Opcode::BitNot)]                        == &handle_bit_not);
	static_assert(HANDLERS[static_cast<u8>(Opcode::LogicalAnd)]                    == &handle_logical_and);
	static_assert(HANDLERS[static_cast<u8>(Opcode::LogicalOr)]                     == &handle_logical_or);
	static_assert(HANDLERS[static_cast<u8>(Opcode::LogicalNot)]                    == &handle_logical_not);
	static_assert(HANDLERS[static_cast<u8>(Opcode::Compare)]                       == &handle_compare);
	static_assert(HANDLERS[static_cast<u8>(Opcode::Negate)]                        == &handle_negate);
	static_assert(HANDLERS[static_cast<u8>(Opcode::UnaryPlus)]                     == &handle_unary_plus);
	static_assert(HANDLERS[static_cast<u8>(Opcode::ArrayType)]                     == &handle_array_type);
	static_assert(HANDLERS[static_cast<u8>(Opcode::ReferenceType)]                 == &handle_reference_type);
	static_assert(HANDLERS[static_cast<u8>(Opcode::Undefined)]                     == &handle_undefined);
	static_assert(HANDLERS[static_cast<u8>(Opcode::Unreachable)]                   == &handle_unreachable);
	static_assert(HANDLERS[static_cast<u8>(Opcode::ValueInteger)]                  == &handle_value_integer);
	static_assert(HANDLERS[static_cast<u8>(Opcode::ValueFloat)]                    == &handle_value_float);
	static_assert(HANDLERS[static_cast<u8>(Opcode::ValueString)]                   == &handle_value_string);
	static_assert(HANDLERS[static_cast<u8>(Opcode::ValueVoid)]                     == &handle_value_void);
	static_assert(HANDLERS[static_cast<u8>(Opcode::DiscardVoid)]                   == &handle_discard_void);
	static_assert(HANDLERS[static_cast<u8>(Opcode::CheckTopVoid)]                  == &handle_check_top_void);
	static_assert(HANDLERS[static_cast<u8>(Opcode::CheckWriteCtxVoid)]             == &handle_check_write_ctx_void);

	interp->is_ok = true;

	while (true)
	{
		const u8 bits = static_cast<u8>(*ops);

		const bool consumes_write_ctx = (bits & 0x80) != 0;

		CTValue write_ctx_value;

		CTValue* write_ctx;

		if (consumes_write_ctx)
		{
			ASSERT_OR_IGNORE(interp->write_ctxs.used() >= 1);

			write_ctx_value = interp->write_ctxs.end()[-1];

			interp->write_ctxs.pop_by(1);

			write_ctx = &write_ctx_value;
		}
		else
		{
			write_ctx = nullptr;
		}

		const u8 ordinal = bits & 0x7F;

		ASSERT_OR_IGNORE(ordinal != 0 && ordinal < array_count(HANDLERS));

		// A crude helper for looking through the opcode emission logs for the
		// currently executing operation by its id.
		// This is actually super-duper helpful for debugging.
		[[maybe_unused]] const OpcodeId debug_op_id = id_from_opcode(interp->opcodes, ops);

		const OpcodeHandlerFunc handler = HANDLERS[ordinal];

		ops = handler(interp, ops + 1, write_ctx);

		if (ops == nullptr)
		{
			if (!interp->is_ok)
				return false;

			if (interp->activations.used() == 0)
				return true;

			ops = opcode_from_id(interp->opcodes, interp->activations.end()[-1]);

			interp->activations.pop_by(1);
		}
	}
}



static TypeId make_func_type_from_array(TypePool* types, TypeId return_type, u8 parameter_count, const BuiltinParamInfo* params) noexcept
{
	const TypeId parameter_list_type = type_create_composite(types, TypeTag::Composite, TypeDisposition::ParameterList, SourceId::INVALID, parameter_count, true);

	for (u8 i = 0; i != parameter_count; ++i)
	{
		MemberInit member_init{};
		member_init.name = params[i].name;
		member_init.type_id = params[i].type;
		member_init.default_id = none<ForeverValueId>();
		member_init.is_pub = false;
		member_init.is_mut = false;
		member_init.is_eval = params[i].is_comptime_known;
		member_init.is_eval = params[i].is_comptime_known;
		member_init.offset = 0;

		if (!type_add_composite_member(types, parameter_list_type, member_init))
			ASSERT_UNREACHABLE;
	}

	type_seal_composite(types, parameter_list_type, 0, 0, 0);


	SignatureType2 signature_type{};
	signature_type.parameter_list_type_id = parameter_list_type;
	signature_type.return_type.type_id = return_type;
	signature_type.closure_id = none<ClosureId>();
	signature_type.is_func = true;
	signature_type.has_templated_parameter_list = false;
	signature_type.has_templated_return_type = false;
	signature_type.parameter_count = parameter_count;

	return type_create_signature(types, TypeTag::Func, signature_type);
}

template<typename... Params>
static TypeId make_func_type(TypePool* types, TypeId return_type, Params... params) noexcept
{
	if constexpr (sizeof...(params) == 0)
	{
		return make_func_type_from_array(types, return_type, 0, nullptr);
	}
	else
	{
		const BuiltinParamInfo params_array[] = { params... };

		return make_func_type_from_array(types, return_type, sizeof...(params), params_array);
	}
}

static void init_builtin_infos(Interpreter* interp) noexcept
{
	const TypeId type_type = type_create_simple(interp->types, TypeTag::Type);

	const TypeId comp_integer_type = type_create_simple(interp->types, TypeTag::CompInteger);

	const TypeId bool_type = type_create_simple(interp->types, TypeTag::Boolean);

	// TODO: This is currently unused and replaced by dummies, since
	//       `type_metrics_from_id` does not yet support `TypeTag::Definition`.
	// const TypeId definition_type = type_create_simple(interp->types, TypeTag::Definition);

	const TypeId type_builder_type = type_create_simple(interp->types, TypeTag::TypeBuilder);

	const TypeId void_type = type_create_simple(interp->types, TypeTag::Void);

	const TypeId type_info_type = type_create_simple(interp->types, TypeTag::TypeInfo);

	const TypeId u8_type = type_create_numeric(interp->types, TypeTag::Integer, NumericType{ 8, false });

	const TypeId u32_type = type_create_numeric(interp->types, TypeTag::Integer, NumericType{ 32, false });

	const TypeId u64_type = type_create_numeric(interp->types, TypeTag::Integer, NumericType{ 64, false });

	const TypeId s64_type = type_create_numeric(interp->types, TypeTag::Integer, NumericType{ 64, true });

	ReferenceType slice_of_u8_attach{};
	slice_of_u8_attach.is_opt = false;
	slice_of_u8_attach.is_multi = false;
	slice_of_u8_attach.is_mut = false;
	slice_of_u8_attach.referenced_type_id = u8_type;

	const TypeId slice_of_u8_type = type_create_reference(interp->types, TypeTag::Slice, slice_of_u8_attach);


	
	const OpcodeId integer_body = opcode_id_from_builtin(interp->opcodes, Builtin::Integer);

	const TypeId integer_signature = make_func_type(interp->types, type_type,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("bits")), u8_type, true },
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("is_signed")), bool_type, true }
	);

	interp->builtin_infos[static_cast<u8>(Builtin::Integer) - 1] = BuiltinInfo{ integer_body, integer_signature };



	const OpcodeId float_body = opcode_id_from_builtin(interp->opcodes, Builtin::Float);

	const TypeId float_signature = make_func_type(interp->types, type_type,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("bits")), u8_type, true }
	);

	interp->builtin_infos[static_cast<u8>(Builtin::Float) - 1] = BuiltinInfo{ float_body, float_signature };



	const OpcodeId type_body = opcode_id_from_builtin(interp->opcodes, Builtin::Type);

	const TypeId type_signature = make_func_type(interp->types, type_type);

	interp->builtin_infos[static_cast<u8>(Builtin::Type) - 1] = BuiltinInfo{ type_body, type_signature };



	const OpcodeId definition_body = opcode_id_from_builtin(interp->opcodes, Builtin::Definition);

	const TypeId definition_signature = make_func_type(interp->types, type_type);

	interp->builtin_infos[static_cast<u8>(Builtin::Definition) - 1] = BuiltinInfo{ definition_body, definition_signature };



	const OpcodeId typeinfo_body = opcode_id_from_builtin(interp->opcodes, Builtin::TypeInfo);

	const TypeId typeinfo_signature = make_func_type(interp->types, type_type);

	interp->builtin_infos[static_cast<u8>(Builtin::TypeInfo) - 1] = BuiltinInfo{ typeinfo_body, typeinfo_signature };



	const OpcodeId typeof_body = opcode_id_from_builtin(interp->opcodes, Builtin::Typeof);

	const TypeId typeof_signature = make_func_type(interp->types, type_type,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("arg")), type_info_type, true }
	);

	interp->builtin_infos[static_cast<u8>(Builtin::Typeof) - 1] = BuiltinInfo{ typeof_body, typeof_signature };



	const OpcodeId returntypeof_body = opcode_id_from_builtin(interp->opcodes, Builtin::Returntypeof);

	const TypeId returntypeof_signature = make_func_type(interp->types, type_type,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("arg")), type_info_type, true }
	);

	interp->builtin_infos[static_cast<u8>(Builtin::Returntypeof) - 1] = BuiltinInfo{ returntypeof_body, returntypeof_signature };



	const OpcodeId sizeof_body = opcode_id_from_builtin(interp->opcodes, Builtin::Sizeof);

	const TypeId sizeof_signature = make_func_type(interp->types, comp_integer_type,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("arg")), type_type, true }
	);

	interp->builtin_infos[static_cast<u8>(Builtin::Sizeof) - 1] = BuiltinInfo{ sizeof_body, sizeof_signature };



	const OpcodeId alignof_body = opcode_id_from_builtin(interp->opcodes, Builtin::Alignof);

	const TypeId alignof_signature = make_func_type(interp->types, comp_integer_type,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("arg")), type_type, true }
	);

	interp->builtin_infos[static_cast<u8>(Builtin::Alignof) - 1] = BuiltinInfo{ alignof_body, alignof_signature };



	const OpcodeId strideof_body = opcode_id_from_builtin(interp->opcodes, Builtin::Strideof);

	const TypeId strideof_signature = make_func_type(interp->types, comp_integer_type,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("arg")), type_type, true }
	);

	interp->builtin_infos[static_cast<u8>(Builtin::Strideof) - 1] = BuiltinInfo{ strideof_body, strideof_signature };



	const OpcodeId offsetof_body = opcode_id_from_builtin(interp->opcodes, Builtin::Offsetof);

	const TypeId offsetof_signature = make_func_type(interp->types, comp_integer_type);

	interp->builtin_infos[static_cast<u8>(Builtin::Offsetof) - 1] = BuiltinInfo{ offsetof_body, offsetof_signature };



	const OpcodeId nameof_body = opcode_id_from_builtin(interp->opcodes, Builtin::Nameof);

	const TypeId nameof_signature = make_func_type(interp->types, slice_of_u8_type,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("arg")), type_info_type, true }
	);

	interp->builtin_infos[static_cast<u8>(Builtin::Nameof) - 1] = BuiltinInfo{ nameof_body, nameof_signature };



	const OpcodeId import_body = opcode_id_from_builtin(interp->opcodes, Builtin::Import);

	const TypeId import_signature = make_func_type(interp->types, type_type,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("path")), slice_of_u8_type, true },
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("is_std")), bool_type, true },
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("from")), u32_type, true }
	);

	interp->builtin_infos[static_cast<u8>(Builtin::Import) - 1] = BuiltinInfo{ import_body, import_signature };



	const OpcodeId create_type_builder_body = opcode_id_from_builtin(interp->opcodes, Builtin::CreateTypeBuilder);

	const TypeId create_type_builder_signature = make_func_type(interp->types, type_builder_type,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("source_id")), u32_type, true }
	);

	interp->builtin_infos[static_cast<u8>(Builtin::CreateTypeBuilder) - 1] = BuiltinInfo{ create_type_builder_body, create_type_builder_signature };



	const OpcodeId add_type_member_body = opcode_id_from_builtin(interp->opcodes, Builtin::AddTypeMember);

	// TODO: "definition" should be of type `definition_type`, not `type_type`.
	//       The current behaviour is due to `TypeTag::Definition` not being
	//       supported by `type_metrics_from_id` yet, as its actual layout is
	//       not defined yet.
	const TypeId add_type_member_signature = make_func_type(interp->types, void_type,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("builder")), type_builder_type, true },
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("definition")), type_type, true },
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("offset")), s64_type, true }
	);

	interp->builtin_infos[static_cast<u8>(Builtin::AddTypeMember) - 1] = BuiltinInfo{ add_type_member_body, add_type_member_signature };



	const OpcodeId complete_type_body = opcode_id_from_builtin(interp->opcodes, Builtin::CompleteType);

	const TypeId complete_type_signature = make_func_type(interp->types, type_type,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("builder")), type_builder_type, true },
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("size")), u64_type, true },
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("align")), u64_type, true },
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("stride")), u64_type, true }
	);

	interp->builtin_infos[static_cast<u8>(Builtin::CompleteType) - 1] = BuiltinInfo{ complete_type_body, complete_type_signature };



	const OpcodeId source_id_body = opcode_id_from_builtin(interp->opcodes, Builtin::SourceId);

	const TypeId source_id_signature = make_func_type(interp->types, u32_type);

	interp->builtin_infos[static_cast<u8>(Builtin::SourceId) - 1] = BuiltinInfo{ source_id_body, source_id_signature };



	const OpcodeId caller_source_id_body = opcode_id_from_builtin(interp->opcodes, Builtin::CallerSourceId);

	const TypeId caller_source_id_signature = make_func_type(interp->types, u32_type);

	interp->builtin_infos[static_cast<u8>(Builtin::CallerSourceId) - 1] = BuiltinInfo{ caller_source_id_body, caller_source_id_signature };



	const OpcodeId definition_typeof_body = opcode_id_from_builtin(interp->opcodes, Builtin::DefinitionTypeof);

	// TODO: "definition" should be of type `definition_type`, not `type_type`.
	//       The current behaviour is due to `TypeTag::Definition` not being
	//       supported by `type_metrics_from_id` yet, as its actual layout is
	//       not defined yet.
	const TypeId definition_typeof_signature = make_func_type(interp->types, type_type,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("definition")), type_type, true }
	);

	interp->builtin_infos[static_cast<u8>(Builtin::DefinitionTypeof) - 1] = BuiltinInfo{ definition_typeof_body, definition_typeof_signature };

	if (interp->imported_opcodes_log_file.m_rep != nullptr)
	{
		for (u32 i = 0; i != array_count(interp->builtin_infos); ++i)
		{
			const Opcode* body_code = opcode_from_id(interp->opcodes, interp->builtin_infos[i].body);

			log_opcodes(interp, body_code);
		}
	}
}



Interpreter* create_interpreter(HandlePool* handles, AstPool* asts, TypePool* types, GlobalValuePool* globals, OpcodePool* opcodes, SourceReader* reader, Parser* parser, IdentifierPool* identifiers, LexicalAnalyser* lex, ErrorSink* errors, minos::FileHandle imported_asts_log_file, minos::FileHandle imported_opcodes_log_file, minos::FileHandle imported_types_log_file) noexcept
{
	static constexpr u32 SCOPES_RESERVE_SIZE = sizeof(Scope) << 18;
	static constexpr u32 SCOPES_COMMIT_INCREMENT_COUNT = 2048;
	
	static constexpr u32 SCOPE_MEMBERS_RESERVE_SIZE = sizeof(ScopeMember) << 18;
	static constexpr u32 SCOPE_MEMBERS_COMMIT_INCREMENT_COUNT = 8192 / sizeof(ScopeMember);

	static constexpr u32 SCOPE_DATA_RESERVE_SIZE = 1 << 28;
	static constexpr u32 SCOPE_DATA_COMMIT_INCREMENT_COUNT = 65536;

	static constexpr u32 VALUES_RESERVE_SIZE = sizeof(CTValue) << 20;
	static constexpr u32 VALUES_COMMIT_INCREMENT_COUNT = 1024;

	static constexpr u32 TEMPORARY_DATA_RESERVE_SIZE = 1 << 28;
	static constexpr u32 TEMPORARY_DATA_COMMIT_INCREMENT_COUNT = 65536;

	static constexpr u32 ACTIVATIONS_RESERVE_SIZE = sizeof(OpcodeId) << 22;
	static constexpr u32 ACTIVATIONS_COMMIT_INCREMENT_COUNT = 4096;

	static constexpr u32 CALL_ACTIVATION_INDICES_RESERVE_SIZE = sizeof(u32) << 18;
	static constexpr u32 CALL_ACTIVATION_INDICES_COMMIT_INCREMENT_COUNT = 1024;

	static constexpr u32 LOOP_STACK_RESERVE_SIZE = sizeof(LoopInfo) << 18;
	static constexpr u32 LOOP_STACK_COMMIT_INCREMENT_COUNT = 1024;

	static constexpr u32 WRITE_CTXS_RESERVE_SIZE = sizeof(CTValue) << 17;
	static constexpr u32 WRITE_CTXS_COMMIT_INCREMENT_COUNT = 512;

	static constexpr u32 ACTIVE_CLOSURES_RESERVE_SIZE = sizeof(ClosureId) << 15;
	static constexpr u32 ACTIVE_CLOSURES_COMMIT_INCREMENT_COUNT = 1024;

	static constexpr u32 CLOSURE_MEMBERS_RESERVE_SIZE = sizeof(ScopeMember) << 22;
	static constexpr u32 CLOSURE_MEMBERS_COMMIT_INCREMENT_COUNT = 8192;

	static constexpr u32 ARGUMENT_CALLBACKS_RESERVE_SIZE = sizeof(OpcodeId) << 20;
	static constexpr u32 ARGUMENT_CALLBACKS_COMMIT_INCREMENT_COUNT = 2048;

	static constexpr u32 ARGUMENT_PACKS_RESERVE_SIZE = sizeof(ArgumentPack) << 18;
	static constexpr u32 ARGUMENT_PACKS_COMMIT_INCREMENT_COUNT = 512;

	static constexpr u32 GLOBAL_INITIALIZATIONS_RESERVE_SIZE = sizeof(GlobalInitialization) << 16;
	static constexpr u32 GLOBAL_INITIALIZATIONS_COMMIT_INCREMENT_COUNT = 4096 / sizeof(GlobalInitialization);

	static constexpr u64 TOTAL_RESERVE_SIZE = static_cast<u64>(SCOPES_RESERVE_SIZE)
	                                        + SCOPE_MEMBERS_RESERVE_SIZE
	                                        + SCOPE_DATA_RESERVE_SIZE
	                                        + VALUES_RESERVE_SIZE
	                                        + TEMPORARY_DATA_RESERVE_SIZE
	                                        + ACTIVATIONS_RESERVE_SIZE
											+ CALL_ACTIVATION_INDICES_RESERVE_SIZE
											+ LOOP_STACK_RESERVE_SIZE
	                                        + WRITE_CTXS_RESERVE_SIZE
	                                        + ACTIVE_CLOSURES_RESERVE_SIZE
	                                        + CLOSURE_MEMBERS_RESERVE_SIZE
	                                        + ARGUMENT_CALLBACKS_RESERVE_SIZE
	                                        + ARGUMENT_PACKS_RESERVE_SIZE
	                                        + GLOBAL_INITIALIZATIONS_RESERVE_SIZE;

	byte* const memory = static_cast<byte*>(minos::mem_reserve(TOTAL_RESERVE_SIZE));

	if (memory == nullptr)
		panic("Failed to allocate memory for Interpreter (0x%X).\n", minos::last_error());

	Interpreter* const interp = alloc_handle_from_pool<Interpreter>(handles);

	interp->asts = asts;
	interp->types = types;
	interp->globals = globals;
	interp->opcodes = opcodes;
	interp->reader = reader;
	interp->parser = parser;
	interp->identifiers = identifiers;
	interp->lex = lex;
	interp->errors = errors;
	interp->imported_asts_log_file = imported_asts_log_file;
	interp->imported_opcodes_log_file = imported_opcodes_log_file;
	interp->imported_types_log_file = imported_types_log_file;

	u64 offset = 0;

	interp->scopes.init({ memory + offset, SCOPES_RESERVE_SIZE }, SCOPES_COMMIT_INCREMENT_COUNT);
	offset += SCOPES_RESERVE_SIZE;

	interp->scope_members.init({ memory + offset, SCOPE_MEMBERS_RESERVE_SIZE }, SCOPE_MEMBERS_COMMIT_INCREMENT_COUNT);
	offset += SCOPE_MEMBERS_RESERVE_SIZE;

	interp->scope_data.init({ memory + offset, SCOPE_DATA_RESERVE_SIZE }, SCOPE_DATA_COMMIT_INCREMENT_COUNT);
	offset += SCOPE_DATA_RESERVE_SIZE;

	interp->values.init({ memory + offset, VALUES_RESERVE_SIZE }, VALUES_COMMIT_INCREMENT_COUNT);
	offset += VALUES_RESERVE_SIZE;

	interp->temporary_data.init({ memory + offset, TEMPORARY_DATA_RESERVE_SIZE }, TEMPORARY_DATA_COMMIT_INCREMENT_COUNT);
	offset += TEMPORARY_DATA_RESERVE_SIZE;

	interp->activations.init({ memory + offset, ACTIVATIONS_RESERVE_SIZE }, ACTIVATIONS_COMMIT_INCREMENT_COUNT);
	offset += ACTIVATIONS_RESERVE_SIZE;

	interp->call_activation_indices.init({ memory + offset, CALL_ACTIVATION_INDICES_RESERVE_SIZE }, CALL_ACTIVATION_INDICES_COMMIT_INCREMENT_COUNT);
	offset += CALL_ACTIVATION_INDICES_RESERVE_SIZE;

	interp->loop_stack.init({ memory + offset, LOOP_STACK_RESERVE_SIZE }, LOOP_STACK_COMMIT_INCREMENT_COUNT);
	offset += LOOP_STACK_RESERVE_SIZE;

	interp->write_ctxs.init({ memory + offset, WRITE_CTXS_RESERVE_SIZE }, WRITE_CTXS_COMMIT_INCREMENT_COUNT);
	offset += WRITE_CTXS_RESERVE_SIZE;

	interp->active_closures.init({ memory + offset, ACTIVE_CLOSURES_RESERVE_SIZE }, ACTIVE_CLOSURES_COMMIT_INCREMENT_COUNT);
	offset += ACTIVE_CLOSURES_RESERVE_SIZE;

	interp->closure_members.init({ memory + offset, CLOSURE_MEMBERS_RESERVE_SIZE }, CLOSURE_MEMBERS_COMMIT_INCREMENT_COUNT);
	offset += CLOSURE_MEMBERS_RESERVE_SIZE;

	interp->argument_callbacks.init({ memory + offset, ARGUMENT_CALLBACKS_RESERVE_SIZE }, ARGUMENT_CALLBACKS_COMMIT_INCREMENT_COUNT);
	offset += ARGUMENT_CALLBACKS_RESERVE_SIZE;

	interp->argument_packs.init({ memory + offset, ARGUMENT_PACKS_RESERVE_SIZE }, ARGUMENT_PACKS_COMMIT_INCREMENT_COUNT);
	offset += ARGUMENT_PACKS_RESERVE_SIZE;

	interp->global_initializations.init({ memory + offset, GLOBAL_INITIALIZATIONS_RESERVE_SIZE }, GLOBAL_INITIALIZATIONS_COMMIT_INCREMENT_COUNT);
	offset += GLOBAL_INITIALIZATIONS_RESERVE_SIZE;

	ASSERT_OR_IGNORE(offset == TOTAL_RESERVE_SIZE);

	interp->memory = MutRange<byte>{ memory, TOTAL_RESERVE_SIZE };

	// Reserve `ClosureId::INVALID`.
	interp->closure_members.reserve();

	init_builtin_infos(interp);

	return interp;
}

void release_interpreter(Interpreter* interp) noexcept
{
	minos::mem_unreserve(interp->memory.begin(), interp->memory.count());
}

bool import_prelude(Interpreter* interp, Range<char8> path) noexcept
{
	SourceFile* prelude_file;

	const Maybe<TypeId> maybe_prelude = import_file_or_prelude(interp, path, true, true, &prelude_file);

	return is_some(maybe_prelude);
}

Maybe<TypeId> import_file(Interpreter* interp, Range<char8> path, bool is_std) noexcept
{
	SourceFile* unused_file;

	return import_file_or_prelude(interp, path, false, is_std, &unused_file);
}

bool evaluate_file_definition_by_name(Interpreter* interp, TypeId file_type, IdentifierId name) noexcept
{
	MemberInfo unused_member_info;

	OpcodeId member_initializer;

	const MemberByNameRst rst = type_member_info_by_name(interp->types, file_type, name, &unused_member_info, &member_initializer);

	if (rst == MemberByNameRst::Ok)
	{
		return true;
	}
	else if (rst == MemberByNameRst::NotFound)
	{
		record_error(interp->errors, SourceId::INVALID, CompileError::GlobalNameNotDefined);

		return false;
	}
	else
	{
		ASSERT_OR_IGNORE(rst == MemberByNameRst::Incomplete);

		const Opcode* const initializer_code = opcode_from_id(interp->opcodes, member_initializer);

		return interpret_opcodes(interp, initializer_code);
	}
}

bool evaluate_all_file_definitions(Interpreter* interp, TypeId file_type) noexcept
{
	MemberIterator it = members_of(interp->types, file_type);

	bool is_ok = true;

	while (has_next(&it))
	{
		MemberInfo unused_member_info;
		
		OpcodeId member_initializer;
		
		if (next(&it, &unused_member_info, &member_initializer))
			continue;

		const Opcode* const initializer_code = opcode_from_id(interp->opcodes, member_initializer);

		if (!interpret_opcodes(interp, initializer_code))
			is_ok = false;
	}

	return is_ok;
}

const char8* tag_name(Builtin builtin) noexcept
{
	static constexpr const char8* BUILTIN_NAMES[] = {
		"INVALID",
		"Integer",
		"Float",
		"Type",
		"Definition",
		"TypeInfo",
		"Typeof",
		"Returntypeof",
		"Sizeof",
		"Alignof",
		"Strideof",
		"Offsetof",
		"Nameof",
		"Import",
		"CreateTypeBuilder",
		"AddTypeMember",
		"CompleteType",
		"SourceId",
		"CallerSourceId",
		"DefinitionTypeof",
	};

	u8 ordinal = static_cast<u8>(builtin);

	if (ordinal >= array_count(BUILTIN_NAMES))
		ordinal = 0;

	return BUILTIN_NAMES[ordinal];
}
