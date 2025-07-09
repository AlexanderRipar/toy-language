#include "core.hpp"

#include "../diag/diag.hpp"
#include "../infra/container.hpp"

// Activation record.
// This is allocated in `Interpreter.arecs`, and acts somewhat like a stack
// frame. However, an activation record is created not just for every function
// invocation, but for every scope containing definitions. This includes
// blocks, function signature types instantiated which are instantiated on each
// call, and `where` clauses.
struct alignas(8) Arec
{
	// Id of the `Arec` that is the lexical parent of this one.
	// Note that this differs from `prev_stack_index` in two cases:
	// Firstly, when there are other activation records between this one and
	// its parent, as is the case when there is a block inside a call, feeding
	// its result into the call's signature record.
	// Secondly, when this is a root activation record, meaning it has no
	// lexical predecessor. In this case, `parent_index` is set to `-1`.
	ArecId surrounding_arec_id;

	// Index of the `Arec` preceding this one on the stack, in
	// qwords. If there is no previous record on the stack, this is set to `0`.
	// While this is ambiguous with the case when there is only a single
	// record, this is fine, as debug assertions can instead check
	// `Interpreter.arecs.used()` when wanting to ensure there
	// further records.
	ArecId prev_top_id;

	// id of the type of this activation record's `attachment`. This is always
	// a valid `TypeId` referencing a composite type.
	TypeId type_id;

	u32 end_index;

	#if COMPILER_MSVC
	#pragma warning(push)
	#pragma warning(disable : 4200) // C4200: nonstandard extension used: zero-sized array in struct/union
	#elif COMPILER_CLANG
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Wc99-extensions" // flexible array members are a C99 feature
	#elif COMPILER_GCC
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wpedantic" // ISO C++ forbids flexible array member
	#endif
	// The actual data in this activation record. The size and layout are
	// defined by `type_id`.
	byte attachment[];
	#if COMPILER_MSVC
	#pragma warning(pop)
	#elif COMPILER_CLANG
	#pragma clang diagnostic pop
	#elif COMPILER_GCC
	#pragma GCC diagnostic pop
	#endif
};

struct ArecRestoreInfo
{
	ArecId old_selected;

	u32 old_used;
};

struct LocationHeader
{
	bool is_dependent;

	bool is_mut;
};

using Location = MutAttachmentRange<byte, LocationHeader>;

using BuiltinFunc = void (*) (Interpreter* interp, Arec* arec, AstNode* call_node, Location into) noexcept;

// Representation of a callable, meaning either a builtin or a user-defined
// function or procedure.
struct alignas(8) Callable
{
	// `TypeId` of the type of the function being called. Always references a
	// `FuncType`.
	u32 func_type_id_bits : 31;

	// `1` when referring to a builtin, `0` otherwise.
	// See `code.ordinal` and `code.ast` for further information.
	u32 is_builtin : 1;

	// Reference to the function implementation. The representation depends on
	// the value of `is_builtin`.
	union
	{
		// Active only if `is_builtin` is `0`. In this case, it refers to the
		// function's body expression.
		AstNodeId ast;

		// Active only if `is_builtin` is `1`. In this case, it holds the
		// ordinal of the builtin (i.e., `static_cast<u8>(builtin)`).
		// This is used to look up the builtin implementation in
		// `Interpreter.builtin_values`.
		u8 ordinal;
	} code;
};

// Representation of an instance of a dependent type in an `Arec`.
// Stores the resolved `TypeId` along with the offset in quad-words from this
// to the actual value and its size in bytes.
struct alignas(8) DependentValue
{
	TypeId resolved_type_id;

	u32 value_offset;

	u32 value_size;

	u32 unused_;
};

// Utility for creating built-in functions types.
struct BuiltinParamInfo
{
	IdentifierId name;

	TypeId type;

	bool is_comptime_known;
};

struct Interpreter
{
	SourceReader* reader;

	Parser* parser;

	TypePool* types;

	AstPool* asts;

	IdentifierPool* identifiers;

	GlobalValuePool* globals;

	ErrorSink* errors;

	ReservedVec<u64> arecs;

	ArecId top_arec_id;

	ArecId active_arec_id;

	TypeId prelude_type_id;

	TypeId builtin_type_ids[static_cast<u8>(Builtin::MAX)];

	BuiltinFunc builtin_values[static_cast<u8>(Builtin::MAX)];

	minos::FileHandle log_file;

	bool log_prelude;
};



static void evaluate_dependent_expr(Interpreter* interp, AstNode* node, Location into) noexcept;

static void evaluate_independent_expr(Interpreter* interp, AstNode* node, TypeId context, Location into) noexcept;

static TypeId typecheck_dependent_expr(Interpreter* interp, AstNode* node) noexcept;

static void typecheck_independent_expr(Interpreter* interp, AstNode* node, TypeId context) noexcept;





static DependentValue* dependent_value_from_dependent_loc(Location loc) noexcept
{
	ASSERT_OR_IGNORE(loc.attachment().is_dependent);

	ASSERT_OR_IGNORE(loc.count() == sizeof(DependentValue));

	DependentValue* const indirection = reinterpret_cast<DependentValue*>(loc.begin());

	ASSERT_OR_IGNORE(indirection->value_offset != 0);

	ASSERT_OR_IGNORE(indirection->resolved_type_id != TypeId::INVALID);

	return indirection;
}

static MutRange<byte> bytes_from_dependent_loc(Location loc) noexcept
{
	const DependentValue* const indirection = dependent_value_from_dependent_loc(loc);

	return MutRange{ loc.begin() + indirection->value_offset, indirection->value_size };
}

static TypeId type_id_from_dependent_loc(Location loc) noexcept
{
	return dependent_value_from_dependent_loc(loc)->resolved_type_id;
}

static void copy_loc(Location dst, Location src) noexcept
{
	MutRange<byte> dst_bytes = dst.attachment().is_dependent
		? bytes_from_dependent_loc(dst)
		: dst.as_mut_byte_range();
		
	MutRange<byte> src_bytes = src.attachment().is_dependent
		? bytes_from_dependent_loc(src)
		: src.as_mut_byte_range();

	ASSERT_OR_IGNORE(dst_bytes.count() == src_bytes.count());

	memcpy(dst_bytes.begin(), src_bytes.begin(), dst_bytes.count());
}

template<typename T>
static T load_loc(Location src) noexcept
{
	MutRange<byte> src_bytes = src.attachment().is_dependent
		? bytes_from_dependent_loc(src)
		: src.as_mut_byte_range();

	ASSERT_OR_IGNORE(src_bytes.count() == sizeof(T));

	return *reinterpret_cast<T*>(src_bytes.begin());
}

static void store_loc_raw(Location dst, Range<byte> src_bytes) noexcept
{
	MutRange<byte> dst_bytes = dst.attachment().is_dependent
		? bytes_from_dependent_loc(dst)
		: dst.as_mut_byte_range();

	ASSERT_OR_IGNORE(dst_bytes.count() == src_bytes.count());

	memcpy(dst_bytes.begin(), src_bytes.begin(), src_bytes.count());
}

template<typename T>
static void store_loc(Location dst, T src) noexcept
{
	store_loc_raw(dst, range::from_object_bytes(&src));
}

template<typename T>
static Location make_loc(T* t) noexcept
{
	return Location{ range::from_object_bytes_mut(t), LocationHeader{ false, true } };
}



static ArecRestoreInfo activate_arec_id(Interpreter* interp, ArecId arec_id) noexcept
{
	ASSERT_OR_IGNORE(arec_id != ArecId::INVALID && arec_id < interp->top_arec_id);

	const ArecId old_selected = interp->active_arec_id;

	const u32 old_used = interp->arecs.used();

	interp->active_arec_id = arec_id;

	return { old_selected, old_used };
}

static void restore_arec(Interpreter* interp, ArecRestoreInfo info) noexcept
{
	ASSERT_OR_IGNORE(info.old_selected <= interp->top_arec_id);

	ASSERT_OR_IGNORE(info.old_selected < static_cast<ArecId>(info.old_used));

	interp->arecs.pop_to(info.old_used);

	interp->active_arec_id = info.old_selected;
}

static Arec* arec_from_id(Interpreter* interp, ArecId arec_id) noexcept
{
	ASSERT_OR_IGNORE(arec_id != ArecId::INVALID);

	return reinterpret_cast<Arec*>(interp->arecs.begin() + static_cast<s32>(arec_id));
}

static ArecId push_arec(Interpreter* interp, TypeId record_type_id, ArecId lexical_parent) noexcept
{
	ASSERT_OR_IGNORE(type_tag_from_id(interp->types, record_type_id) == TypeTag::Composite);

	const TypeMetrics record_metrics = type_metrics_from_id(interp->types, record_type_id);

	// TODO: Make this properly aligned for over-aligned types.
	// That also needs to account for the 8-byte skew created by the
	// `Arec`.

	Arec* const arec = static_cast<Arec*>(interp->arecs.reserve_padded(static_cast<u32>(sizeof(Arec) + record_metrics.size)));
	arec->prev_top_id = interp->top_arec_id;
	arec->surrounding_arec_id = lexical_parent;
	arec->type_id = record_type_id;
	arec->end_index = interp->arecs.used();

	const ArecId arec_id = static_cast<ArecId>(reinterpret_cast<const u64*>(arec) - interp->arecs.begin());

	interp->top_arec_id = arec_id;

	ASSERT_OR_IGNORE(lexical_parent == ArecId::INVALID || interp->active_arec_id == lexical_parent);

	interp->active_arec_id = arec_id;

	return arec_id;
}

static void pop_arec(Interpreter* interp, ArecId arec_id) noexcept
{
	ASSERT_OR_IGNORE(arec_id != ArecId::INVALID && interp->top_arec_id == arec_id && interp->active_arec_id == arec_id);

	const Arec* const popped = reinterpret_cast<Arec*>(interp->arecs.begin() + static_cast<s32>(arec_id));

	interp->active_arec_id = popped->surrounding_arec_id == ArecId::INVALID ? popped->prev_top_id : popped->surrounding_arec_id;

	interp->top_arec_id = popped->prev_top_id;

	interp->arecs.pop_to(static_cast<u32>(arec_id));
}

static MutRange<byte> alloc_in_arec(Interpreter* interp, u64 size, u32 align) noexcept
{
	if (size > UINT32_MAX)
		panic("Tried allocating local storage exceeding allowed maximum size in activation record.\n");

	ASSERT_OR_IGNORE(interp->active_arec_id != ArecId::INVALID && interp->active_arec_id == interp->top_arec_id);

	interp->arecs.pad_to_alignment(align);

	return MutRange{ static_cast<byte*>(interp->arecs.reserve_padded(static_cast<u32>(size))), size };
}

static void trim_arec(Interpreter* interp, ArecId arec_id) noexcept
{
	ASSERT_OR_IGNORE(arec_id != ArecId::INVALID);

	ASSERT_OR_IGNORE(arec_id == interp->active_arec_id && arec_id == interp->top_arec_id);

	Arec* const arec = reinterpret_cast<Arec*>(interp->arecs.begin() + static_cast<u32>(arec_id));

	interp->arecs.pop_to(arec->end_index);
}



static bool lookup_identifier(Interpreter* interp, TypeId context, IdentifierId name, SourceId source, MemberInfo* out) noexcept
{
	while (true)
	{
		if (type_member_info_by_name(interp->types, context, name, source, out))
			return true;

		context = lexical_parent_type_from_id(interp->types, context);

		if (context == TypeId::INVALID)
			return false;
	}
}



static void location_from_member_info_and_arec(Interpreter* interp, OptPtr<Arec> arec, MemberInfo* info, Location* out_location, TypeId* out_resolved_type_id) noexcept
{
	ASSERT_OR_IGNORE(info->is_global || is_none(arec) || !info->has_pending_type);

	if (info->is_global)
	{
		if (info->has_pending_value)
		{
			TODO("Lazily initialize global value");
		}

		*out_location = Location{ global_value_get_mut(interp->globals, info->value.complete), LocationHeader{ false, info->is_mut } };

		*out_resolved_type_id = global_value_type(interp->globals, info->value.complete);
	}
	else if (is_none(arec))
	{
		ASSERT_UNREACHABLE;
	}
	else if (is_dependent(info->type.complete))
	{
		DependentValue* const dependent_value =  reinterpret_cast<DependentValue*>(get_ptr(arec)->attachment + info->offset);

		ASSERT_OR_IGNORE(dependent_value->resolved_type_id != TypeId::INVALID);

		byte* const location_begin = get_ptr(arec)->attachment + info->offset;

		*out_location = Location{ location_begin, sizeof(DependentValue), LocationHeader{ is_dependent(info->type.complete), info->is_mut } };

		*out_resolved_type_id = dependent_value->resolved_type_id;
	}
	else
	{
		const u64 size = type_metrics_from_id(interp->types, independent(info->type.complete)).size;

		if (size > UINT32_MAX)
			source_error(interp->errors, info->source, "Size of dependent type instance must not exceed 2^32 - 1 bytes.\n");

		*out_location = Location{ get_ptr(arec)->attachment + info->offset, static_cast<u32>(size), LocationHeader{ false, info->is_mut } };

		*out_resolved_type_id = independent(info->type.complete);
	}
}

static void lookup_location_local(Interpreter* interp, Arec* arec, IdentifierId name, SourceId source, Location* out_location, TypeId* out_resolved_type_id) noexcept
{
	MemberInfo info;

	if (!type_member_info_by_name(interp->types, arec->type_id, name, source, &info))
		ASSERT_UNREACHABLE;

	location_from_member_info_and_arec(interp, some(arec), &info, out_location, out_resolved_type_id);
}

static void lookup_location_local_by_rank(Interpreter* interp, Arec* arec, u16 rank, Location* out_location, TypeId* out_resolved_type_id) noexcept
{
	MemberInfo info;

	type_member_info_by_rank(interp->types, arec->type_id, rank, &info);

	location_from_member_info_and_arec(interp, some(arec), &info, out_location, out_resolved_type_id);
}

static bool lookup_location(Interpreter* interp, IdentifierId name, SourceId source, Location* out_location, TypeId* out_resolved_type_id) noexcept
{
	MemberInfo info;

	ASSERT_OR_IGNORE(interp->active_arec_id != ArecId::INVALID);

	ArecId curr = interp->active_arec_id;

	Arec* arec = arec_from_id(interp, curr);

	while (true)
	{
		if (type_member_info_by_name(interp->types, arec->type_id, name, source, &info))
		{
			location_from_member_info_and_arec(interp, some(arec), &info, out_location, out_resolved_type_id);

			return true;
		}

		if (arec->surrounding_arec_id == ArecId::INVALID)
			break;

		curr = arec->surrounding_arec_id;

		arec = arec_from_id(interp, curr);
	}

	for (TypeId lex_scope = lexical_parent_type_from_id(interp->types, arec->type_id); lex_scope != TypeId::INVALID; lex_scope = lexical_parent_type_from_id(interp->types, lex_scope))
	{
		if (type_member_info_by_name(interp->types, lex_scope, name, source, &info))
		{
			if (!info.is_global)
				return false;

			location_from_member_info_and_arec(interp, none<Arec>(), &info, out_location, out_resolved_type_id);

			return true;
		}
	}

	return false;
}



static Location prepare_load_and_convert(Interpreter* interp, AstNode* src_node, Location dst) noexcept
{
	if (has_flag(src_node, AstFlag::Any_LoadResult))
	{
		return Location{ alloc_in_arec(interp, sizeof(Location), alignof(Location)), LocationHeader{ false, true } };
	}
	else if (has_flag(src_node, AstFlag::Any_ConvertResult))
	{
		const TypeMetrics metrics = type_metrics_from_id(interp->types, independent(src_node->type));

		return Location{ alloc_in_arec(interp, metrics.size, metrics.align), LocationHeader{ false, true } };
	}
	else
	{
		return dst;
	}
}

static void load_and_convert(Interpreter* interp, SourceId source_id, Location dst, TypeId dst_type_id, Location src, TypeId src_type_id, AstFlag src_flags) noexcept
{
	if (src.begin() == dst.begin())
		return;

	if ((src_flags & AstFlag::Any_ConvertResult) != AstFlag::EMPTY)
	{
		if ((src_flags & AstFlag::Any_LoadResult) != AstFlag::EMPTY)
			src = load_loc<Location>(src);

		const TypeTag dst_type_tag = type_tag_from_id(interp->types, dst_type_id);

		switch (dst_type_tag)
		{
		case TypeTag::Integer:
		{
			ASSERT_OR_IGNORE(type_tag_from_id(interp->types, src_type_id) == TypeTag::CompInteger);

			const NumericType dst_type_structure = *static_cast<const NumericType*>(simple_type_structure_from_id(interp->types, dst_type_id));

			ASSERT_OR_IGNORE(dst_type_structure.bits != 0 && dst_type_structure.bits % 8 == 0);

			const CompIntegerValue src_value = load_loc<CompIntegerValue>(src);

			u64 dst_value;

			if (dst_type_structure.is_signed)
			{
				s64 signed_dst_value;

				if (!s64_from_comp_integer(src_value, static_cast<u8>(dst_type_structure.bits), &signed_dst_value))
					source_error(interp->errors, source_id, "Compile-time integer constant does not fit into %u-bit signed integer.\n", dst_type_structure.bits);

				dst_value = static_cast<u64>(signed_dst_value);
			}
			else
			{
				if (!u64_from_comp_integer(src_value, static_cast<u8>(dst_type_structure.bits), &dst_value))
					source_error(interp->errors, source_id, "Compile-time integer constant does not fit into %u-bit unsigned integer.\n", dst_type_structure.bits);
			}

			store_loc_raw(dst, Range<byte>{ reinterpret_cast<const byte*>(&dst_value), static_cast<u64>(dst_type_structure.bits / 8) });

			return;
		}

		case TypeTag::Float:
		{
			ASSERT_OR_IGNORE(type_tag_from_id(interp->types, src_type_id) == TypeTag::CompFloat);

			const NumericType dst_type_structure = *static_cast<const NumericType*>(simple_type_structure_from_id(interp->types, dst_type_id));

			const CompFloatValue src_value = load_loc<CompFloatValue>(src);

			if (dst_type_structure.bits == 32)
			{
				store_loc(dst, f32_from_comp_float(src_value));
			}
			else
			{
				ASSERT_OR_IGNORE(dst_type_structure.bits == 64);

				store_loc(dst, f64_from_comp_float(src_value));
			}

			return;
		}

		case TypeTag::Slice:
		{
			ASSERT_OR_IGNORE(type_tag_from_id(interp->types, src_type_id) == TypeTag::Array);

			store_loc(dst, src.as_mut_byte_range());

			return;
		}
		
		case TypeTag::TypeInfo:
		{
			store_loc(dst, src_type_id);

			return;
		}

		case TypeTag::Void:
		case TypeTag::Type:
		case TypeTag::Definition:
		case TypeTag::CompInteger:
		case TypeTag::CompFloat:
		case TypeTag::Boolean:
		case TypeTag::Array:
		case TypeTag::Func:
		case TypeTag::Builtin:
		case TypeTag::Composite:
		case TypeTag::CompositeLiteral:
		case TypeTag::ArrayLiteral:
		case TypeTag::TypeBuilder:
		case TypeTag::Variadic:
		case TypeTag::Divergent:
		case TypeTag::Trait:
		case TypeTag::TailArray:
		case TypeTag::INVALID:
			; // Fallthrough to unreachable.
		}

		ASSERT_UNREACHABLE;
	}
	else if ((src_flags & AstFlag::Any_LoadResult) != AstFlag::EMPTY)
	{
		copy_loc(dst, load_loc<Location>(src));
	}
}



static void store_typecheck_result(AstNode* node, DependentTypeId type, TypeKind type_kind, bool is_comptime_known) noexcept
{
	ASSERT_OR_IGNORE(independent(node->type) == TypeId::CHECKING && !has_flag(node, AstFlag::Any_IsComptimeKnown | AstFlag::Any_TypeKindLoBit | AstFlag::Any_TypeKindHiBit));

	node->type = type;

	set_type_kind(node, type_kind);

	if (is_comptime_known)
		node->flags |= AstFlag::Any_IsComptimeKnown;
}

static void set_load_only(Interpreter* interp, AstNode* node, TypeKind desired_type_kind) noexcept
{
	const TypeKind actual_type_kind = type_kind_of(node);

	if (actual_type_kind != desired_type_kind)
	{
		if (actual_type_kind < desired_type_kind)
			source_error(interp->errors, node->source_id, "Cannot convert from %s to %s.\n", tag_name(actual_type_kind), tag_name(desired_type_kind));

		if (desired_type_kind == TypeKind::Value)
			node->flags |= AstFlag::Any_LoadResult;
	}
}

static void set_load_and_convert(Interpreter* interp, AstNode* node, TypeKind desired_type_kind, DependentTypeId desired_type) noexcept
{
	ASSERT_OR_IGNORE(!has_flag(node, AstFlag::Any_SkipEvaluation | AstFlag::Any_ConvertResult | AstFlag::Any_LoadResult));

	if (is_dependent(node->type) || is_dependent(desired_type))
	{
		node->flags |= AstFlag::Any_ConvertResult;
	}
	else if (!is_same_type(interp->types, independent(node->type), independent(desired_type)))
	{
		if (!type_can_implicitly_convert_from_to(interp->types, independent(node->type), independent(desired_type)))
			source_error(interp->errors, node->source_id, "Cannot implicitly convert to desired type.\n");

		node->flags |= AstFlag::Any_ConvertResult;
	}

	if (type_tag_from_id(interp->types, independent(desired_type)) == TypeTag::TypeInfo)
		node->flags |= AstFlag::Any_SkipEvaluation;

	set_load_only(interp, node, desired_type_kind);
}



static void complete_independent_member_type(Interpreter* interp, MemberInfo* member) noexcept
{
	ASSERT_OR_IGNORE(member->has_pending_type);

	TypeId member_type_id;

	if (member->type.pending == AstNodeId::INVALID)
	{
		ASSERT_OR_IGNORE(member->has_pending_value);

		AstNode* const value = ast_node_from_id(interp->asts, member->value.pending);

		if (value->type == DependentTypeId::INVALID)
		{
			typecheck_independent_expr(interp, value, member->completion_context);

			set_load_only(interp, value, TypeKind::Value);
		}

		member_type_id = independent(value->type);
	}
	else
	{
		AstNode* const type = ast_node_from_id(interp->asts, member->type.pending);

		if (type->type == DependentTypeId::INVALID)
		{
			typecheck_independent_expr(interp, type, member->completion_context);

			if (!has_flag(type, AstFlag::Any_IsComptimeKnown))
				source_error(interp->errors, type->source_id, "Explicit type annotation must have compile-time known value.\n");

			if (type_tag_from_id(interp->types, independent(type->type)) != TypeTag::Type)
				source_error(interp->errors, type->source_id, "Explicit type annotation must be of type `Type`\n");

			set_load_only(interp, type, TypeKind::Value);
		}

		if (static_cast<ArecId>(member->completion_arec) == ArecId::INVALID)
		{
			DependentTypeId dependent_member_type;

			Location dependent_member_type_loc = make_loc(&dependent_member_type);

			Location mapped_dependent_member_type_loc = prepare_load_and_convert(interp, type, dependent_member_type_loc);

			evaluate_independent_expr(interp, type, member->completion_context, mapped_dependent_member_type_loc);

			load_and_convert(interp, type->source_id, dependent_member_type_loc, simple_type(interp->types, TypeTag::Type, {}), mapped_dependent_member_type_loc, independent(type->type), type->flags);

			member_type_id = independent(dependent_member_type);
		}
		else
		{
			const ArecRestoreInfo restore_info = activate_arec_id(interp, static_cast<ArecId>(member->completion_arec));

			evaluate_dependent_expr(interp, type, make_loc(&member_type_id));

			restore_arec(interp, restore_info);
		}
	}

	member->type.complete = independent_type_id(member_type_id);
	member->has_pending_type = false;

	set_incomplete_type_member_type_by_rank(interp->types, member->surrounding_type_id, member->rank, independent_type_id(member_type_id));
}

static bool complete_independent_member_value(Interpreter* interp, MemberInfo* member) noexcept
{
	ASSERT_OR_IGNORE(!member->has_pending_type && member->has_pending_value && member->is_comptime_known);

	const TypeId member_type_id = independent(member->type.complete);

	const TypeMetrics metrics = type_metrics_from_id(interp->types, member_type_id);

	GlobalValueId value_id = alloc_global_value(interp->globals, member_type_id, metrics.size, metrics.align);

	MutRange<byte> value_bytes = global_value_get_mut(interp->globals, value_id);

	Location value_loc = Location{ value_bytes, LocationHeader{ false, member->is_mut } };

	AstNode* const value = ast_node_from_id(interp->asts, member->value.pending);

	if (value->type == DependentTypeId::INVALID)
	{
		typecheck_independent_expr(interp, value, member->completion_context);

		set_load_only(interp, value, TypeKind::Value);
	}

	if (static_cast<ArecId>(member->completion_arec) == ArecId::INVALID)
	{
		Location mapped_value_loc = prepare_load_and_convert(interp, value, value_loc);

		evaluate_independent_expr(interp, value, member->completion_context, mapped_value_loc);

		load_and_convert(interp, value->source_id, value_loc, member_type_id, mapped_value_loc, independent(value->type), value->flags);
	}
	else
	{
		const ArecRestoreInfo restore_info = activate_arec_id(interp, static_cast<ArecId>(member->completion_arec));

		evaluate_dependent_expr(interp, value, value_loc);

		restore_arec(interp, restore_info);
	}

	member->value.complete = value_id;
	member->has_pending_value = false;

	set_incomplete_type_member_value_by_rank(interp->types, member->surrounding_type_id, member->rank, value_id);

	return true;
}



static void evaluate_dependent_expr(Interpreter* interp, AstNode* node, Location into) noexcept
{
	(void) interp;

	(void) node;

	(void) into;

	TODO("Implement evaluate_dependent_expr");
}

static void evaluate_independent_expr(Interpreter* interp, AstNode* node, TypeId context, Location into) noexcept
{
	ASSERT_OR_IGNORE(is_dependent(node->type) || (independent(node->type) != TypeId::INVALID && independent(node->type) != TypeId::CHECKING));

	if (has_flag(node, AstFlag::Any_SkipEvaluation))
		return;

	switch (node->tag)
	{
	case AstTag::Builtin:
	{
		const u8 ordinal = static_cast<u8>(node->flags) & 0x7F;

		ASSERT_OR_IGNORE(ordinal < static_cast<u8>(Builtin::MAX));

		Callable result{};
		result.is_builtin = true;
		result.func_type_id_bits = static_cast<u32>(interp->builtin_type_ids[ordinal]);
		result.code.ordinal = ordinal;

		store_loc(into, result);

		return;
	}

	case AstTag::Func:
	{
		FuncInfo info = get_func_info(node);

		if (is_some(info.body))
		{
			Callable result{};
			result.is_builtin = false;
			result.func_type_id_bits = static_cast<u32>(independent(node->type));
			result.code.ast = id_from_ast_node(interp->asts, get_ptr(info.body));

			store_loc(into, result);
		}
		else
		{
			TODO("(Later) Implement evaluation of function signatures to their type");
		}

		return;
	}

	case AstTag::Identifier:
	{
		ASSERT_OR_IGNORE(has_flag(node, AstFlag::Any_IsComptimeKnown));

		MemberInfo member;

		if (!lookup_identifier(interp, context, attachment_of<AstIdentifierData>(node)->identifier_id, node->source_id, &member))
			ASSERT_UNREACHABLE;

		if (member.has_pending_type)
			complete_independent_member_type(interp, &member);

		if (member.has_pending_value)
			complete_independent_member_value(interp, &member);

		store_loc(into, Location{ global_value_get_mut(interp->globals, member.value.complete), LocationHeader{ false, false } });

		return;
	}

	case AstTag::LitInteger:
	{
		store_loc(into, attachment_of<AstLitIntegerData>(node)->value);

		return;
	}

	case AstTag::LitString:
	{
		const GlobalValueId global_value_id = attachment_of<AstLitStringData>(node)->string_value_id;

		store_loc(into, Location{ global_value_get_mut(interp->globals, global_value_id), LocationHeader{ false, false } });

		return;
	}

	case AstTag::Call:
	{
		AstNode* const callee = first_child_of(node);

		Callable callee_value;

		Location callee_loc = make_loc(&callee_value);

		Location mapped_callee_loc = prepare_load_and_convert(interp, callee, callee_loc);

		evaluate_independent_expr(interp, callee, context, mapped_callee_loc);

		load_and_convert(interp, callee->source_id, callee_loc, independent(callee->type), mapped_callee_loc, independent(callee->type), callee->flags);

		const FuncType* callee_structure = static_cast<const FuncType*>(simple_type_structure_from_id(interp->types, static_cast<TypeId>(callee_value.func_type_id_bits)));

		const ArecId signature_arec_id = push_arec(interp, callee_structure->signature_type_id, ArecId::INVALID);

		Arec* const signature_arec = arec_from_id(interp, signature_arec_id);

		u16 arg_rank = 0;

		AstNode* arg = callee;

		while (has_next_sibling(arg))
		{
			arg = next_sibling_of(arg);

			if (arg->tag == AstTag::OpSet)
			{
				TODO("Implement evaluation of named arguments");
			}
			else
			{
				Location param_loc;

				TypeId param_type_id;

				lookup_location_local_by_rank(interp, signature_arec, arg_rank, &param_loc, &param_type_id);

				Location mapped_param_loc = prepare_load_and_convert(interp, arg, param_loc);

				evaluate_independent_expr(interp, arg, context, mapped_param_loc);

				load_and_convert(interp, arg->source_id, param_loc, param_type_id, mapped_param_loc, independent(arg->type), arg->flags);

				arg_rank += 1;
			}
		}

		if (callee_value.is_builtin)
		{
			interp->builtin_values[callee_value.code.ordinal](interp, signature_arec, node, into);
		}
		else
		{
			AstNode* const body = ast_node_from_id(interp->asts, callee_value.code.ast);

			Location mapped_into = prepare_load_and_convert(interp, body, into);

			evaluate_dependent_expr(interp, body, mapped_into);

			load_and_convert(interp, body->source_id, into, independent(node->type), mapped_into, independent(callee_structure->return_type_id), body->flags);
		}

		pop_arec(interp, signature_arec_id);

		return;
	}

	case AstTag::OpMember:
	{
		AstNode* const lhs = first_child_of(node);

		const TypeTag lhs_type_tag = type_tag_from_id(interp->types, independent(lhs->type));

		AstNode* const rhs = next_sibling_of(lhs);

		ASSERT_OR_IGNORE(rhs->tag == AstTag::Identifier);

		const IdentifierId member_name = attachment_of<AstIdentifierData>(rhs)->identifier_id;

		if (lhs_type_tag == TypeTag::Composite)
		{
			const TypeKind lhs_type_kind = type_kind_of(lhs);

			if (lhs_type_kind == TypeKind::Value)
			{
				TODO("Implement member operator for values as left-hand-side");
			}
			else
			{
				Location evaluated_lhs;

				Location evaluated_lhs_loc = make_loc(&evaluated_lhs);

				Location mapped_evaluated_lhs_loc = prepare_load_and_convert(interp, lhs, evaluated_lhs_loc);

				evaluate_independent_expr(interp, lhs, context, mapped_evaluated_lhs_loc);

				load_and_convert(interp, lhs->source_id, evaluated_lhs_loc, independent(lhs->type), mapped_evaluated_lhs_loc, independent(lhs->type), lhs->flags);

				MemberInfo member;

				if (!type_member_info_by_name(interp->types, independent(lhs->type), member_name, node->source_id, &member))
					ASSERT_UNREACHABLE;

				if (member.has_pending_type)
					complete_independent_member_type(interp, &member);

				if (member.is_global)
				{
					if (member.has_pending_value)
						complete_independent_member_value(interp, &member);

					store_loc(into, Location{ global_value_get_mut(interp->globals, member.value.complete), LocationHeader{ false, member.is_mut } });
				}
				else
				{
					const TypeMetrics member_metrics = type_metrics_from_id(interp->types, independent(member.type.complete)); 

					store_loc(into, Location{ evaluated_lhs.as_mut_byte_range().mut_subrange(member.offset, member_metrics.size), LocationHeader{ false, evaluated_lhs.attachment().is_mut && member.is_mut } });
				}
			}
		}
		else
		{
			DependentTypeId evaluated_lhs_type;

			Location evaluated_lhs_type_loc = make_loc(&evaluated_lhs_type);

			Location mapped_evaluated_lhs_type_loc = prepare_load_and_convert(interp, lhs, evaluated_lhs_type_loc);

			evaluate_independent_expr(interp, lhs, context, mapped_evaluated_lhs_type_loc);

			load_and_convert(interp, lhs->source_id, evaluated_lhs_type_loc, independent(lhs->type), mapped_evaluated_lhs_type_loc, independent(lhs->type), lhs->flags);

			MemberInfo member;

			if (!type_member_info_by_name(interp->types, independent(evaluated_lhs_type), member_name, node->source_id, &member))
				ASSERT_UNREACHABLE;

			ASSERT_OR_IGNORE(member.is_global);

			if (member.has_pending_type)
				complete_independent_member_type(interp, &member);

			if (member.has_pending_value)
				complete_independent_member_value(interp, &member);

			store_loc(into, Location{ global_value_get_mut(interp->globals, member.value.complete), LocationHeader{ false, member.is_mut } });
		}

		return;
	}

	case AstTag::OpCmpEQ:
	{
		// TODO: Properly implement.
		store_loc(into, true);

		return;
	}

	case AstTag::File:
	case AstTag::CompositeInitializer:
	case AstTag::ArrayInitializer:
	case AstTag::Wildcard:
	case AstTag::Where:
	case AstTag::Expects:
	case AstTag::Ensures:
	case AstTag::Definition:
	case AstTag::Parameter:
	case AstTag::Block:
	case AstTag::If:
	case AstTag::For:
	case AstTag::ForEach:
	case AstTag::Switch:
	case AstTag::Case:
	case AstTag::Trait:
	case AstTag::Impl:
	case AstTag::Catch:
	case AstTag::LitFloat:
	case AstTag::LitChar:
	case AstTag::Return:
	case AstTag::Leave:
	case AstTag::Yield:
	case AstTag::ParameterList:
	case AstTag::UOpTypeTailArray:
	case AstTag::UOpTypeSlice:
	case AstTag::UOpTypeMultiPtr:
	case AstTag::UOpTypeOptMultiPtr:
	case AstTag::UOpEval:
	case AstTag::UOpTry:
	case AstTag::UOpDefer:
	case AstTag::UOpDistinct:
	case AstTag::UOpAddr:
	case AstTag::UOpDeref:
	case AstTag::UOpBitNot:
	case AstTag::UOpLogNot:
	case AstTag::UOpTypeOptPtr:
	case AstTag::UOpTypeVar:
	case AstTag::UOpImpliedMember:
	case AstTag::UOpTypePtr:
	case AstTag::UOpNegate:
	case AstTag::UOpPos:
	case AstTag::OpAdd:
	case AstTag::OpSub:
	case AstTag::OpMul:
	case AstTag::OpDiv:
	case AstTag::OpAddTC:
	case AstTag::OpSubTC:
	case AstTag::OpMulTC:
	case AstTag::OpMod:
	case AstTag::OpBitAnd:
	case AstTag::OpBitOr:
	case AstTag::OpBitXor:
	case AstTag::OpShiftL:
	case AstTag::OpShiftR:
	case AstTag::OpLogAnd:
	case AstTag::OpLogOr:
	case AstTag::OpCmpLT:
	case AstTag::OpCmpGT:
	case AstTag::OpCmpLE:
	case AstTag::OpCmpGE:
	case AstTag::OpCmpNE:
	case AstTag::OpSet:
	case AstTag::OpSetAdd:
	case AstTag::OpSetSub:
	case AstTag::OpSetMul:
	case AstTag::OpSetDiv:
	case AstTag::OpSetAddTC:
	case AstTag::OpSetSubTC:
	case AstTag::OpSetMulTC:
	case AstTag::OpSetMod:
	case AstTag::OpSetBitAnd:
	case AstTag::OpSetBitOr:
	case AstTag::OpSetBitXor:
	case AstTag::OpSetShiftL:
	case AstTag::OpSetShiftR:
	case AstTag::OpTypeArray:
	case AstTag::OpArrayIndex:
	case AstTag::INVALID:
	case AstTag::MAX:
		; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

static TypeId typecheck_dependent_expr(Interpreter* interp, AstNode* node) noexcept
{
	(void) interp;

	(void) node;

	TODO("Implement typecheck_dependent_expr.");
}

static void typecheck_independent_expr(Interpreter* interp, AstNode* node, TypeId context) noexcept
{
	const DependentTypeId prev_type_id = node->type;

	if (!is_dependent(prev_type_id) && independent(prev_type_id) == TypeId::CHECKING)
		source_error(interp->errors, node->source_id, "Cyclic type dependency detected during typechecking.\n");

	if (prev_type_id != DependentTypeId::INVALID)
		return;

	node->type = independent_type_id(TypeId::CHECKING);

	switch (node->tag)
	{
	case AstTag::Builtin:
	{
		const u8 ordinal = static_cast<u8>(node->flags) & 0x7F;

		const DependentTypeId result_type = independent_type_id(interp->builtin_type_ids[ordinal]);

		store_typecheck_result(node, result_type, TypeKind::Value, true);

		return;
	}

	case AstTag::Definition:
	case AstTag::Parameter:
	{
		DefinitionInfo info = get_definition_info(node);

		// Some tomfoolery to forego checking of `tag` by `attachment_of`.
		// Since `AstDefinitionData` and `AstParameterData` share the same
		// layout, this is fine.
		AstDefinitionData* const attach = node->tag == AstTag::Definition
			? attachment_of<AstDefinitionData>(node)
			: reinterpret_cast<AstDefinitionData*>(attachment_of<AstParameterData>(node));

		if (is_some(info.type))
		{
			AstNode* const type = get_ptr(info.type);

			typecheck_independent_expr(interp, type, context);

			if (!has_flag(type, AstFlag::Any_IsComptimeKnown))
				source_error(interp->errors, type->source_id, "Explicit type annotation must have compile-time known value.\n");

			if (!is_dependent(type->type) && type_tag_from_id(interp->types, independent(type->type)) != TypeTag::Type)
				source_error(interp->errors, type->source_id, "Explicit type annotation must be of type `Type`\n");

			set_load_only(interp, type, TypeKind::Value);

			Location defined_type_loc = make_loc(&attach->defined_type);

			Location mapped_defined_type_loc = prepare_load_and_convert(interp, type, defined_type_loc);

			evaluate_independent_expr(interp, type, context, mapped_defined_type_loc);

			load_and_convert(interp, type->source_id, defined_type_loc, simple_type(interp->types, TypeTag::Type, {}), mapped_defined_type_loc, independent(type->type), type->flags);

			if (is_some(info.value))
			{
				AstNode* const value = get_ptr(info.value);

				typecheck_independent_expr(interp, value, context);

				set_load_and_convert(interp, value, TypeKind::Value, attach->defined_type);
			}
		}
		else
		{
			AstNode* const value = get_ptr(info.value);

			typecheck_independent_expr(interp, value, context);

			set_load_only(interp, value, TypeKind::Value);

			attach->defined_type = value->type;
		}

		const bool is_comptime_known = (node->tag == AstTag::Parameter && has_flag(node, AstFlag::Parameter_IsEval))
		                            || is_none(info.value)
									|| has_flag(get_ptr(info.value), AstFlag::Any_IsComptimeKnown);

		store_typecheck_result(node, independent_type_id(simple_type(interp->types, TypeTag::Definition, {})), TypeKind::Value, is_comptime_known);

		return;
	}

	case AstTag::Block:
	{
		const TypeId block_type_id = create_open_type(interp->types, context, node->source_id, TypeDisposition::Block);

		u64 block_member_offset = 0;

		u32 block_align = 1;

		bool is_comptime_known = true;

		u16 definition_rank = 0;

		AstNode* stmt = nullptr;

		AstDirectChildIterator stmts = direct_children_of(node);

		while (has_next(&stmts))
		{
			stmt = next(&stmts);

			typecheck_independent_expr(interp, stmt, block_type_id);

			is_comptime_known &= has_flag(stmt, AstFlag::Any_IsComptimeKnown);

			if (has_next_sibling(stmt) && stmt->tag == AstTag::Definition)
			{
				const AstDefinitionData* const attach = attachment_of<AstDefinitionData>(stmt);

				DefinitionInfo info = get_definition_info(stmt);

				TypeMetrics metrics;

				if (is_dependent(attach->defined_type))
				{
					metrics = { sizeof(DependentValue), sizeof(DependentValue), alignof(DependentValue) };
				}
				else
				{
					metrics = type_metrics_from_id(interp->types, independent(attach->defined_type));
				}

				if (metrics.align > block_align)
					block_align = metrics.align;

				const bool include_value = is_some(info.value) && has_flag(get_ptr(info.value), AstFlag::Any_IsComptimeKnown);

				block_member_offset = next_multiple(block_member_offset, static_cast<u64>(metrics.align));

				MemberInit init{};
				init.name = attach->identifier_id;
				init.source = stmt->source_id;
				init.type.complete = attach->defined_type;
				init.value.pending = include_value ? id_from_ast_node(interp->asts, get_ptr(info.value)) : AstNodeId::INVALID;
				init.completion_context = block_type_id;
				init.completion_arec = static_cast<s32>(ArecId::INVALID);
				init.is_global = has_flag(node, AstFlag::Definition_IsGlobal);
				init.is_pub = has_flag(node, AstFlag::Definition_IsPub);
				init.is_use = has_flag(node, AstFlag::Definition_IsUse);
				init.is_mut = has_flag(node, AstFlag::Definition_IsMut);
				init.is_comptime_known = has_flag(node, AstFlag::Any_IsComptimeKnown);
				init.has_pending_type = false;
				init.has_pending_value = include_value;
				init.offset = block_member_offset;

				block_member_offset += metrics.size;

				add_open_type_member(interp->types, block_type_id, init);

				if (include_value)
				{
					MemberInfo member;

					type_member_info_by_rank(interp->types, block_type_id, definition_rank, &member);

					complete_independent_member_value(interp, &member);
				}

				definition_rank += 1;
			}
		}

		close_open_type(interp->types, block_type_id, block_member_offset, block_align, next_multiple(block_member_offset, static_cast<u64>(block_align)));

		DependentTypeId result_type;

		if (stmt != nullptr)
		{
			result_type = stmt->type;

			set_load_only(interp, stmt, TypeKind::Value);
		}
		else
		{
			result_type = independent_type_id(simple_type(interp->types, TypeTag::Void, {}));
		}

		store_typecheck_result(node, result_type, TypeKind::Value, is_comptime_known);

		return;
	}

	case AstTag::Func:
	{
		FuncInfo info = get_func_info(node);

		u16 param_count = 0;

		const TypeId unsized_signature_type_id = create_open_type(interp->types, context, node->source_id, TypeDisposition::Signature);

		AstDirectChildIterator params = direct_children_of(info.parameters);

		// Add all parameters to a dummy type `unsized_signature_type_id`.
		// This is necessary as parameters' types may depend upon each other in
		// any order, meaning that they cannot be typechecked yet, implying
		// that their sizes are unknown at this point.  
		while (has_next(&params))
		{
			AstNode* const param = next(&params);

			AstParameterData* const param_attach = attachment_of<AstParameterData>(param);

			DefinitionInfo param_info = get_definition_info(param);

			MemberInit init{};
			init.name = param_attach->identifier_id;
			init.source = param->source_id;
			init.type.pending = is_some(param_info.type) ? id_from_ast_node(interp->asts, get_ptr(param_info.type)) : AstNodeId::INVALID;
			init.value.pending = is_some(param_info.value) ? id_from_ast_node(interp->asts, get_ptr(param_info.value)) : AstNodeId::INVALID;
			init.completion_context = unsized_signature_type_id;
			init.completion_arec = static_cast<s32>(ArecId::INVALID);
			init.is_global = false;
			init.is_pub = false;
			init.is_use = has_flag(param, AstFlag::Parameter_IsMut);
			init.is_mut = has_flag(param, AstFlag::Parameter_IsMut);
			init.is_comptime_known = has_flag(param, AstFlag::Parameter_IsEval);
			init.has_pending_type = is_some(param_info.type);
			init.has_pending_value = is_some(param_info.value);
			init.offset = 0;

			add_open_type_member(interp->types, unsized_signature_type_id, init);

			if (param_count == 63)
				source_error(interp->errors, param->source_id, "Exceeded maximum of 63 parameters in function definition.\n");

			param_count += 1;
		}

		close_open_type(interp->types, unsized_signature_type_id, 0, 1, 0);

		const TypeId signature_type_id = create_open_type(interp->types, context, node->source_id, TypeDisposition::Signature);

		u64 signature_member_offset = 0;

		u32 signature_align = 1;

		params = direct_children_of(info.parameters);

		while (has_next(&params))
		{
			AstNode* const param = next(&params);

			typecheck_independent_expr(interp, param, unsized_signature_type_id);

			AstParameterData* const param_attach = attachment_of<AstParameterData>(param);

			DefinitionInfo param_info = get_definition_info(param);

			TypeMetrics metrics;

			if (is_dependent(param_attach->defined_type))
			{
				metrics = { sizeof(DependentValue), sizeof(DependentValue), alignof(DependentValue) };
			}
			else
			{
				metrics = type_metrics_from_id(interp->types, independent(param_attach->defined_type));
			}

			signature_member_offset = next_multiple(signature_member_offset, static_cast<u64>(metrics.align));

			MemberInit init{};
			init.name = param_attach->identifier_id;
			init.source = param->source_id;
			init.type.complete = param_attach->defined_type;
			init.value.pending = is_some(param_info.value) ? id_from_ast_node(interp->asts, get_ptr(param_info.value)) : AstNodeId::INVALID;
			init.completion_context = unsized_signature_type_id;
			init.completion_arec = static_cast<s32>(ArecId::INVALID);
			init.is_global = false;
			init.is_pub = false;
			init.is_use = has_flag(param, AstFlag::Parameter_IsUse);
			init.is_mut = has_flag(param, AstFlag::Parameter_IsMut);
			init.is_comptime_known = has_flag(param, AstFlag::Parameter_IsEval);
			init.has_pending_type = false;
			init.has_pending_value = is_some(param_info.value);
			init.offset = signature_member_offset;

			signature_member_offset += metrics.size;

			add_open_type_member(interp->types, signature_type_id, init);
		}

		close_open_type(interp->types, signature_type_id, signature_member_offset, signature_align, next_multiple(signature_member_offset, static_cast<u64>(signature_align)));

		if (is_some(info.expects))
		{
			TODO("(Later) Implement expects");
		}

		if (is_some(info.ensures))
		{
			TODO("(Later) Implement ensures");
		}

		DependentTypeId evaluated_return_type;

		if (is_some(info.return_type))
		{
			AstNode* const return_type = get_ptr(info.return_type);

			typecheck_independent_expr(interp, return_type, signature_type_id);

			set_load_only(interp, return_type, TypeKind::Value);

			if (!has_flag(return_type, AstFlag::Any_IsComptimeKnown))
				source_error(interp->errors, return_type->source_id, "Return type annotation must have compile-time known value.\n");

			if (type_tag_from_id(interp->types, independent(return_type->type)) != TypeTag::Type)
				source_error(interp->errors, return_type->source_id, "Return type annotation must be of type `Type`\n");

			Location evaluated_return_type_loc = make_loc(&evaluated_return_type);
			
			Location mapped_evaluated_return_type_loc = prepare_load_and_convert(interp, return_type, evaluated_return_type_loc);

			evaluate_independent_expr(interp, return_type, signature_type_id, mapped_evaluated_return_type_loc);

			load_and_convert(interp, return_type->source_id, evaluated_return_type_loc, independent(return_type->type), mapped_evaluated_return_type_loc, independent(return_type->type), return_type->flags);
		}
		else
		{
			TODO("(Later) Implement return type deduction");
		}

		FuncType func_type{};
		func_type.return_type_id = evaluated_return_type;
		func_type.signature_type_id = signature_type_id;
		func_type.param_count = param_count;
		func_type.is_proc = has_flag(node, AstFlag::Func_IsProc);

		const TypeId func_type_id = simple_type(interp->types, TypeTag::Func, range::from_object_bytes(&func_type));

		attachment_of<AstFuncData>(node)->func_type_id = func_type_id;

		DependentTypeId result_type;

		if (is_some(info.body))
		{
			AstNode* const body = get_ptr(info.body);

			typecheck_independent_expr(interp, body, signature_type_id);

			set_load_and_convert(interp, body, TypeKind::Value, evaluated_return_type);

			result_type = independent_type_id(func_type_id);
		}
		else
		{
			result_type = independent_type_id(simple_type(interp->types, TypeTag::Type, {}));
		}

		store_typecheck_result(node, result_type, TypeKind::Value, true);

		return;
	}

	case AstTag::Identifier:
	{
		const AstIdentifierData* const attach = attachment_of<AstIdentifierData>(node);

		MemberInfo info;

		if (!lookup_identifier(interp, context, attach->identifier_id, node->source_id, &info))
		{
			const Range<char8> name = identifier_name_from_id(interp->identifiers, attach->identifier_id);

			source_error(interp->errors, node->source_id, "Cannot find definition of identifier %.*s.\n", static_cast<s32>(name.count()), name.begin());
		}

		if (info.has_pending_type)
			complete_independent_member_type(interp, &info);

		store_typecheck_result(node, info.type.complete, info.is_mut ? TypeKind::MutLocation : TypeKind::ImmutLocation, info.is_comptime_known);

		return;
	}

	case AstTag::LitInteger:
	{
		const DependentTypeId result_type = independent_type_id(simple_type(interp->types, TypeTag::CompInteger, {}));
		
		store_typecheck_result(node, result_type, TypeKind::Value, true);

		return;
	}

	case AstTag::LitString:
	{
		const DependentTypeId result_type = independent_type_id(global_value_type(interp->globals, attachment_of<AstLitStringData>(node)->string_value_id));

		store_typecheck_result(node, result_type, TypeKind::ImmutLocation, true);

		return;
	}

	case AstTag::Call:
	{
		AstNode* const callee = first_child_of(node);

		typecheck_independent_expr(interp, callee, context);

		set_load_only(interp, callee, TypeKind::Value);

		if (is_dependent(callee->type))
			TODO("Implement calls to dependently typed callees");

		const TypeId callee_type_id = independent(callee->type);

		const TypeTag callee_type_tag = type_tag_from_id(interp->types, callee_type_id);

		if (callee_type_tag != TypeTag::Func && callee_type_tag != TypeTag::Builtin)
			source_error(interp->errors, callee->source_id, "Left-hand-side of call must be of `func`, `proc` or `builtin` type.\n");

		const FuncType callee_structure = *static_cast<const FuncType*>(simple_type_structure_from_id(interp->types, callee_type_id));

		bool is_comptime_known = has_flag(callee, AstFlag::Any_IsComptimeKnown);

		u16 arg_rank = 0;

		AstNode* arg = callee;

		while (has_next_sibling(arg))
		{
			arg = next_sibling_of(arg);

			if (arg->tag == AstTag::OpSet)
			{
				TODO("Implement named arguments");
			}
			else
			{
				typecheck_independent_expr(interp, arg, context);
			}

			is_comptime_known &= has_flag(arg, AstFlag::Any_IsComptimeKnown);

			if (arg_rank == callee_structure.param_count)
			{
				while (has_next_sibling(arg))
				{
					arg = next_sibling_of(arg);

					arg_rank += 1;
				}

				source_error(interp->errors, arg->source_id, "Too many arguments in call (Expected %u, found %u).\n", callee_structure.param_count, arg_rank + 1);
			}

			MemberInfo param_info;

			type_member_info_by_rank(interp->types, callee_structure.signature_type_id, arg_rank, &param_info);

			ASSERT_OR_IGNORE(!param_info.has_pending_type);

			arg_rank += 1;

			set_load_and_convert(interp, arg, TypeKind::Value, param_info.type.complete);
		}

		if (arg_rank != callee_structure.param_count)
			source_error(interp->errors, node->source_id, "Too few arguments in call (Expected %u, found %u).\n", callee_structure.param_count, arg_rank);

		store_typecheck_result(node, callee_structure.return_type_id, TypeKind::Value, is_comptime_known);

		return;
	}

	case AstTag::OpMember:
	{
		AstNode* const lhs = first_child_of(node);

		AstNode* const rhs = next_sibling_of(lhs);

		typecheck_independent_expr(interp, lhs, context);

		if (is_dependent(lhs->type))
			TODO("Implement members of dependent types");

		const TypeId lhs_type_id = independent(lhs->type);

		const TypeTag lhs_type_tag = type_tag_from_id(interp->types, lhs_type_id);

		if (rhs->tag != AstTag::Identifier)
			source_error(interp->errors, rhs->source_id, "Right-hand-side of `.` must be an identifier.\n");

		const IdentifierId identifier = attachment_of<AstIdentifierData>(rhs)->identifier_id;

		bool is_comptime_known;

		TypeKind type_kind;

		MemberInfo member;

		if (lhs_type_tag == TypeTag::Composite)
		{
			set_load_and_convert(interp, lhs, type_kind_of(lhs), lhs->type);

			if (!type_member_info_by_name(interp->types, lhs_type_id, identifier, rhs->source_id, &member))
			{
				const Range<char8> name = identifier_name_from_id(interp->identifiers, identifier);

				source_error(interp->errors, node->source_id, "Left-hand-side of `.` does not have a member named '%.*s'.\n", static_cast<s32>(name.count()), name.begin());
			}

			is_comptime_known = has_flag(lhs, AstFlag::Any_IsComptimeKnown);

			type_kind = type_kind_of(lhs) == TypeKind::Value ? TypeKind::Value : member.is_mut ? TypeKind::MutLocation : TypeKind::ImmutLocation;
		}
		else if (lhs_type_tag == TypeTag::Type)
		{
			set_load_and_convert(interp, lhs, TypeKind::Value, lhs->type);

			TypeId evaluated_lhs_type_id;

			Location evaluated_lhs_type_id_loc = make_loc(&evaluated_lhs_type_id);

			Location mapped_evaluated_lhs_type_id_loc = prepare_load_and_convert(interp, lhs, evaluated_lhs_type_id_loc);

			evaluate_independent_expr(interp, lhs, context, mapped_evaluated_lhs_type_id_loc);

			load_and_convert(interp, lhs->source_id, evaluated_lhs_type_id_loc, simple_type(interp->types, TypeTag::Type, {}), mapped_evaluated_lhs_type_id_loc, independent(lhs->type), lhs->flags);

			if (!type_member_info_by_name(interp->types, evaluated_lhs_type_id, identifier, rhs->source_id, &member))
			{
				const Range<char8> name = identifier_name_from_id(interp->identifiers, identifier);

				source_error(interp->errors, node->source_id, "Left-hand-side of `.` does not have a member named '%.*s'.\n", static_cast<s32>(name.count()), name.begin());
			}

			if (!member.is_global)
			{
				const Range<char8> name = identifier_name_from_id(interp->identifiers, identifier);

				source_error(interp->errors, node->source_id, "Cannot access non-global member '%.*s' from type.\n", static_cast<s32>(name.count()), name.begin());
			}

			is_comptime_known = true;

			type_kind = member.is_mut ? TypeKind::MutLocation : TypeKind::ImmutLocation;
		}
		else
		{
			source_error(interp->errors, lhs->source_id, "Left-hand-side of `.` must be of either of composite type of type `Type`.\n");
		}

		if (member.has_pending_type)
			complete_independent_member_type(interp, &member);

		store_typecheck_result(node, member.type.complete, type_kind, is_comptime_known);

		return;
	}

	case AstTag::OpCmpEQ:
	{
		AstNode* const lhs = first_child_of(node);

		AstNode* const rhs = next_sibling_of(lhs);

		typecheck_independent_expr(interp, lhs, context);

		typecheck_independent_expr(interp, rhs, context);

		const TypeId common_type_id = common_type(interp->types, independent(lhs->type), independent(rhs->type));

		if (common_type_id == TypeId::INVALID)
			source_error(interp->errors, node->source_id, "Operands of `==` have incompatible types.\n");

		set_load_and_convert(interp, lhs, TypeKind::Value, independent_type_id(common_type_id));

		set_load_and_convert(interp, rhs, TypeKind::Value, independent_type_id(common_type_id));

		const bool is_comptime_known = has_flag(lhs, AstFlag::Any_IsComptimeKnown) && has_flag(rhs, AstFlag::Any_IsComptimeKnown);

		store_typecheck_result(node, independent_type_id(simple_type(interp->types, TypeTag::Boolean, {})), TypeKind::Value, is_comptime_known);

		return;
	}

	case AstTag::File:
	case AstTag::CompositeInitializer:
	case AstTag::ArrayInitializer:
	case AstTag::Wildcard:
	case AstTag::Where:
	case AstTag::Expects:
	case AstTag::Ensures:
	case AstTag::If:
	case AstTag::For:
	case AstTag::ForEach:
	case AstTag::Switch:
	case AstTag::Case:
	case AstTag::Trait:
	case AstTag::Impl:
	case AstTag::Catch:
	case AstTag::LitFloat:
	case AstTag::LitChar:
	case AstTag::Return:
	case AstTag::Leave:
	case AstTag::Yield:
	case AstTag::ParameterList:
	case AstTag::UOpTypeTailArray:
	case AstTag::UOpTypeSlice:
	case AstTag::UOpTypeMultiPtr:
	case AstTag::UOpTypeOptMultiPtr:
	case AstTag::UOpEval:
	case AstTag::UOpTry:
	case AstTag::UOpDefer:
	case AstTag::UOpDistinct:
	case AstTag::UOpAddr:
	case AstTag::UOpDeref:
	case AstTag::UOpBitNot:
	case AstTag::UOpLogNot:
	case AstTag::UOpTypeOptPtr:
	case AstTag::UOpTypeVar:
	case AstTag::UOpImpliedMember:
	case AstTag::UOpTypePtr:
	case AstTag::UOpNegate:
	case AstTag::UOpPos:
	case AstTag::OpAdd:
	case AstTag::OpSub:
	case AstTag::OpMul:
	case AstTag::OpDiv:
	case AstTag::OpAddTC:
	case AstTag::OpSubTC:
	case AstTag::OpMulTC:
	case AstTag::OpMod:
	case AstTag::OpBitAnd:
	case AstTag::OpBitOr:
	case AstTag::OpBitXor:
	case AstTag::OpShiftL:
	case AstTag::OpShiftR:
	case AstTag::OpLogAnd:
	case AstTag::OpLogOr:
	case AstTag::OpCmpLT:
	case AstTag::OpCmpGT:
	case AstTag::OpCmpLE:
	case AstTag::OpCmpGE:
	case AstTag::OpCmpNE:
	case AstTag::OpSet:
	case AstTag::OpSetAdd:
	case AstTag::OpSetSub:
	case AstTag::OpSetMul:
	case AstTag::OpSetDiv:
	case AstTag::OpSetAddTC:
	case AstTag::OpSetSubTC:
	case AstTag::OpSetMulTC:
	case AstTag::OpSetMod:
	case AstTag::OpSetBitAnd:
	case AstTag::OpSetBitOr:
	case AstTag::OpSetBitXor:
	case AstTag::OpSetShiftL:
	case AstTag::OpSetShiftR:
	case AstTag::OpTypeArray:
	case AstTag::OpArrayIndex:
	case AstTag::INVALID:
	case AstTag::MAX:
		; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}



static TypeId type_from_file_ast(Interpreter* interp, AstNode* file, SourceId file_type_source_id) noexcept
{
	ASSERT_OR_IGNORE(file->tag == AstTag::File);

	// Note that `interp->prelude_type_id` is `INVALID_TYPE_ID` if we are
	// called from `init_prelude_type`, so the prelude itself has no lexical
	// parent.
	const TypeId file_type_id = create_open_type(interp->types, interp->prelude_type_id, file_type_source_id, TypeDisposition::User);

	AstDirectChildIterator ast_it = direct_children_of(file);

	while (has_next(&ast_it))
	{
		AstNode* const node = next(&ast_it);

		if (node->tag != AstTag::Definition)
			source_error(interp->errors, node->source_id, "Currently only definitions are supported on a file's top-level.\n");

		if (has_flag(node, AstFlag::Definition_IsGlobal))
			source_warning(interp->errors, node->source_id, "Redundant 'global' modifier. Top-level definitions are implicitly global.\n");

		const AstDefinitionData* const attachment = attachment_of<AstDefinitionData>(node);

		const DefinitionInfo info = get_definition_info(node);

		MemberInit init{};
		init.name = attachment->identifier_id;
		init.source = node->source_id;
		init.type.pending = is_some(info.type) ? id_from_ast_node(interp->asts, get_ptr(info.type)) : AstNodeId::INVALID;
		init.value.pending = is_some(info.value) ? id_from_ast_node(interp->asts, get_ptr(info.value)) : AstNodeId::INVALID;
		init.completion_context = file_type_id;
		init.completion_arec = static_cast<s32>(ArecId::INVALID);
		init.is_global = true;
		init.is_pub = has_flag(node, AstFlag::Definition_IsPub);
		init.is_use = has_flag(node, AstFlag::Definition_IsUse);
		init.is_mut = has_flag(node, AstFlag::Definition_IsMut);
		init.is_comptime_known = true;
		init.has_pending_type = true;
		init.has_pending_value = is_some(info.value);
		init.offset = 0;

		add_open_type_member(interp->types, file_type_id, init);
	}

	close_open_type(interp->types, file_type_id, 0, 1, 0);

	const ArecId file_arec_id = push_arec(interp, file_type_id, ArecId::INVALID);

	ast_it = direct_children_of(file);

	while (has_next(&ast_it))
	{
		AstNode* const node = next(&ast_it);

		typecheck_independent_expr(interp, node, file_type_id);

		trim_arec(interp, file_arec_id);
	}

	IncompleteMemberIterator member_it = incomplete_members_of(interp->types, file_type_id);

	while (has_next(&member_it))
	{
		MemberInfo member = next(&member_it);

		if (member.has_pending_type)
		{
			complete_independent_member_type(interp, &member);

			trim_arec(interp, file_arec_id);
		}

		if (member.has_pending_value)
		{
			complete_independent_member_value(interp, &member);

			trim_arec(interp, file_arec_id);
		}
	}

	pop_arec(interp, file_arec_id);

	return file_type_id;
}





static TypeId make_func_type_from_array(TypePool* types, TypeId return_type_id, u16 param_count, const BuiltinParamInfo* params) noexcept
{
	const TypeId signature_type_id = create_open_type(types, TypeId::INVALID, SourceId::INVALID, TypeDisposition::Signature);

	u64 offset = 0;

	u32 max_align = 1;

	for (u16 i = 0; i != param_count; ++i)
	{
		const TypeMetrics metrics = type_metrics_from_id(types, params[i].type);

		offset = next_multiple(offset, static_cast<u64>(metrics.align));

		MemberInit init{};
		init.name = params[i].name;
		init.type.complete = independent_type_id(params[i].type);
		init.value.complete = GlobalValueId::INVALID;
		init.source = SourceId::INVALID;
		init.is_global = false;
		init.is_pub = false;
		init.is_use = false;
		init.is_mut = false;
		init.is_comptime_known = params[i].is_comptime_known;
		init.has_pending_type = false;
		init.has_pending_value = false;
		init.offset = offset;


		offset += metrics.size;

		if (metrics.align > max_align)
			max_align = metrics.align;

		add_open_type_member(types, signature_type_id, init);
	}

	close_open_type(types, signature_type_id, offset, max_align, next_multiple(offset, static_cast<u64>(max_align)));

	FuncType func_type{};
	func_type.return_type_id = independent_type_id(return_type_id);
	func_type.param_count = param_count;
	func_type.is_proc = false;
	func_type.signature_type_id = signature_type_id;

	return simple_type(types, TypeTag::Func, range::from_object_bytes(&func_type));
}

template<typename... Params>
static TypeId make_func_type(TypePool* types, TypeId return_type_id, Params... params) noexcept
{
	if constexpr (sizeof...(params) == 0)
	{
		return make_func_type_from_array(types, return_type_id, 0, nullptr);
	}
	else
	{
		const BuiltinParamInfo params_array[] = { params... };

		return make_func_type_from_array(types, return_type_id, sizeof...(params), params_array);
	}
}



template<typename T>
static T get_builtin_arg(Interpreter* interp, Arec* arec, IdentifierId name) noexcept
{
	Location loc;

	TypeId unused;

	lookup_location_local(interp, arec, name, SourceId::INVALID, &loc, &unused);

	return load_loc<T>(loc);
}

static void builtin_integer(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, Location into) noexcept
{
	const u8 bits = get_builtin_arg<u8>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("bits")));

	const bool is_signed = get_builtin_arg<bool>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("is_signed")));

	NumericType integer_type{};
	integer_type.bits = bits;
	integer_type.is_signed = is_signed;

	store_loc(into, simple_type(interp->types, TypeTag::Integer, range::from_object_bytes(&integer_type)));
}

static void builtin_float(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, Location into) noexcept
{
	const u8 bits = get_builtin_arg<u8>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("bits")));

	NumericType float_type{};
	float_type.bits = bits;
	float_type.is_signed = true;

	store_loc(into, simple_type(interp->types, TypeTag::Float, range::from_object_bytes(&float_type)));
}

static void builtin_type(Interpreter* interp, [[maybe_unused]] Arec* arec, [[maybe_unused]] AstNode* call_node, Location into) noexcept
{
	store_loc(into, simple_type(interp->types, TypeTag::Type, {}));
}

static void builtin_typeof(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, Location into) noexcept
{
	store_loc(into, get_builtin_arg<TypeId>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("arg"))));
}

static void builtin_returntypeof(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, Location into) noexcept
{
	const TypeId arg = get_builtin_arg<TypeId>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("arg")));

	ASSERT_OR_IGNORE(type_tag_from_id(interp->types, arg) == TypeTag::Func || type_tag_from_id(interp->types, arg) == TypeTag::Builtin);

	const FuncType* const func_type = static_cast<const FuncType*>(simple_type_structure_from_id(interp->types, arg));

	store_loc(into, func_type->return_type_id);
}

static void builtin_sizeof(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, Location into) noexcept
{
	const TypeId arg = get_builtin_arg<TypeId>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("arg")));

	const TypeMetrics metrics = type_metrics_from_id(interp->types, arg);

	store_loc(into, comp_integer_from_u64(metrics.size));
}

static void builtin_alignof(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, Location into) noexcept
{
	const TypeId arg = get_builtin_arg<TypeId>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("arg")));

	const TypeMetrics metrics = type_metrics_from_id(interp->types, arg);

	store_loc(into, comp_integer_from_u64(metrics.align));
}

static void builtin_strideof(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, Location into) noexcept
{
	const TypeId arg = get_builtin_arg<TypeId>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("arg")));

	const TypeMetrics metrics = type_metrics_from_id(interp->types, arg);

	store_loc(into, comp_integer_from_u64(metrics.align));
}

static void builtin_offsetof(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, Location into) noexcept
{
	(void) interp;

	(void) arec;

	(void) into;

	TODO("Implement.");
}

static void builtin_nameof(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, Location into) noexcept
{
	(void) interp;

	(void) arec;

	(void) into;

	TODO("Implement.");
}

static void builtin_import(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, Location into) noexcept
{
	const Range<char8> path = get_builtin_arg<Range<char8>>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("path")));

	const bool is_std = get_builtin_arg<bool>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("is_std")));

	const SourceId from = get_builtin_arg<SourceId>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("from")));

	char8 absolute_path_buf[8192];

	Range<char8> absolute_path;

	if (from != SourceId::INVALID)
	{
		const Range<char8> path_base = source_file_path_from_source_id(interp->reader, from);

		char8 path_base_parent_buf[8192];

		const u32 path_base_parent_chars = minos::path_to_absolute_directory(path_base, MutRange{ path_base_parent_buf });

		if (path_base_parent_chars == 0 || path_base_parent_chars > array_count(path_base_parent_buf))
			source_error(interp->errors, call_node->source_id, "Failed to make get parent directory from `from` source file (0x%X).\n", minos::last_error());

		const u32 absolute_path_chars = minos::path_to_absolute_relative_to(path, Range{ path_base_parent_buf , path_base_parent_chars }, MutRange{ absolute_path_buf });

		if (absolute_path_chars == 0 || absolute_path_chars > array_count(absolute_path_buf))
			source_error(interp->errors, call_node->source_id, "Failed to make `path` %.*s absolute relative to `from` %.*s (0x%X).\n", static_cast<s32>(path.count()), path.begin(), static_cast<s32>(path_base.count()), path_base.begin(), minos::last_error());

		absolute_path = Range{ absolute_path_buf, absolute_path_chars };
	}
	else
	{
		// This makes the prelude import of the configured standard library
		// (which is an absolute path) work.
		absolute_path = path;
	}

	store_loc(into, import_file(interp, absolute_path, is_std));
}

static void builtin_create_type_builder(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, Location into) noexcept
{
	(void) interp;

	(void) arec;

	(void) into;

	TODO("Implement.");
}

static void builtin_add_type_member(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, Location into) noexcept
{
	(void) interp;

	(void) arec;

	(void) into;

	TODO("Implement.");
}

static void builtin_complete_type(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, Location into) noexcept
{
	(void) interp;

	(void) arec;

	(void) into;

	TODO("Implement.");
}

static void builtin_source_id([[maybe_unused]] Interpreter* interp, [[maybe_unused]] Arec* arec, AstNode* call_node, Location into) noexcept
{
	store_loc(into, call_node->source_id);
}



static void init_builtin_types(Interpreter* interp) noexcept
{
	const TypeId type_type_id = simple_type(interp->types, TypeTag::Type, {});

	const TypeId comp_integer_type_id = simple_type(interp->types, TypeTag::CompInteger, {});

	const TypeId bool_type_id = simple_type(interp->types, TypeTag::Boolean, {});

	const TypeId definition_type_id = simple_type(interp->types, TypeTag::Definition, {});

	const TypeId type_builder_type_id = simple_type(interp->types, TypeTag::TypeBuilder, {});

	const TypeId void_type_id = simple_type(interp->types, TypeTag::Void, {});

	const TypeId type_info_type_id = simple_type(interp->types, TypeTag::TypeInfo, {});

	ReferenceType ptr_to_type_builder_type{};
	ptr_to_type_builder_type.is_opt = false;
	ptr_to_type_builder_type.is_multi = false;
	ptr_to_type_builder_type.is_mut = true;
	ptr_to_type_builder_type.referenced_type_id = type_builder_type_id;

	const TypeId ptr_to_mut_type_builder_type_id = simple_type(interp->types, TypeTag::Ptr, range::from_object_bytes(&ptr_to_type_builder_type));

	NumericType s64_type{};
	s64_type.bits = 64;
	s64_type.is_signed = true;

	const TypeId s64_type_id = simple_type(interp->types, TypeTag::Integer, range::from_object_bytes(&s64_type));

	NumericType u8_type{};
	u8_type.bits = 8;
	u8_type.is_signed = false;

	const TypeId u8_type_id = simple_type(interp->types, TypeTag::Integer, range::from_object_bytes(&u8_type));

	ReferenceType slice_of_u8_type{};
	slice_of_u8_type.is_opt = false;
	slice_of_u8_type.is_multi = false;
	slice_of_u8_type.is_mut = false;
	slice_of_u8_type.referenced_type_id = u8_type_id;

	const TypeId slice_of_u8_type_id = simple_type(interp->types, TypeTag::Slice, range::from_object_bytes(&slice_of_u8_type));

	NumericType u32_type{};
	u32_type.bits = 32;
	u32_type.is_signed = false;

	const TypeId u32_type_id = simple_type(interp->types, TypeTag::Integer, range::from_object_bytes(&u32_type));



	interp->builtin_type_ids[static_cast<u8>(Builtin::Integer)] = make_func_type(interp->types, type_type_id,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("bits")), u8_type_id, true },
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("is_signed")), bool_type_id, true }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::Float)] = make_func_type(interp->types, type_type_id,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("bits")), u8_type_id, true }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::Type)] = make_func_type(interp->types, type_type_id);

	interp->builtin_type_ids[static_cast<u8>(Builtin::Typeof)] = make_func_type(interp->types, type_type_id,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("arg")), type_info_type_id, true }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::Returntypeof)] = make_func_type(interp->types, type_type_id,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("arg")), type_info_type_id, true }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::Sizeof)] = make_func_type(interp->types, comp_integer_type_id,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("arg")), type_info_type_id, true }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::Alignof)] = make_func_type(interp->types, comp_integer_type_id,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("arg")), type_info_type_id, true }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::Strideof)] = make_func_type(interp->types, comp_integer_type_id,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("arg")), type_info_type_id, true }
	);

	// TODO: Figure out what type this takes as its argument. A member? If so,
	//       how do you effectively get that?
	interp->builtin_type_ids[static_cast<u8>(Builtin::Offsetof)] = make_func_type(interp->types, comp_integer_type_id);

	interp->builtin_type_ids[static_cast<u8>(Builtin::Nameof)] = make_func_type(interp->types, slice_of_u8_type_id,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("arg")), type_info_type_id, true }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::Import)] = make_func_type(interp->types, type_type_id,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("path")), slice_of_u8_type_id, true },
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("is_std")), bool_type_id, true },
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("from")), u32_type_id, true }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::CreateTypeBuilder)] = make_func_type(interp->types, type_builder_type_id);

	interp->builtin_type_ids[static_cast<u8>(Builtin::AddTypeMember)] = make_func_type(interp->types, void_type_id,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("builder")), ptr_to_mut_type_builder_type_id, true },
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("definition")), definition_type_id, true },
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("offset")), s64_type_id, true }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::CompleteType)] = make_func_type(interp->types, type_type_id,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("arg")), type_builder_type_id, true }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::SourceId)] = make_func_type(interp->types, u32_type_id);
}

static void init_builtin_values(Interpreter* interp) noexcept
{
	interp->builtin_values[static_cast<u8>(Builtin::Integer)] = &builtin_integer;
	interp->builtin_values[static_cast<u8>(Builtin::Float)] = &builtin_float;
	interp->builtin_values[static_cast<u8>(Builtin::Type)] = &builtin_type;
	interp->builtin_values[static_cast<u8>(Builtin::Typeof)] = &builtin_typeof;
	interp->builtin_values[static_cast<u8>(Builtin::Returntypeof)] = &builtin_returntypeof;
	interp->builtin_values[static_cast<u8>(Builtin::Sizeof)] = &builtin_sizeof;
	interp->builtin_values[static_cast<u8>(Builtin::Alignof)] = &builtin_alignof;
	interp->builtin_values[static_cast<u8>(Builtin::Strideof)] = &builtin_strideof;
	interp->builtin_values[static_cast<u8>(Builtin::Offsetof)] = &builtin_offsetof;
	interp->builtin_values[static_cast<u8>(Builtin::Nameof)] = &builtin_nameof;
	interp->builtin_values[static_cast<u8>(Builtin::Import)] = &builtin_import;
	interp->builtin_values[static_cast<u8>(Builtin::CreateTypeBuilder)] = &builtin_create_type_builder;
	interp->builtin_values[static_cast<u8>(Builtin::AddTypeMember)] = &builtin_add_type_member;
	interp->builtin_values[static_cast<u8>(Builtin::CompleteType)] = &builtin_complete_type;
	interp->builtin_values[static_cast<u8>(Builtin::SourceId)] = &builtin_source_id;
}

static void init_prelude_type(Interpreter* interp, Config* config, IdentifierPool* identifiers, AstPool* asts) noexcept
{
	NumericType u8_type{};
	u8_type.bits = 8;
	u8_type.is_signed = false;

	const TypeId u8_type_id = simple_type(interp->types, TypeTag::Integer, range::from_object_bytes(&u8_type));

	ArrayType array_of_u8_type{};
	array_of_u8_type.element_type = u8_type_id;
	array_of_u8_type.element_count = config->std.filepath.count();

	const TypeId array_of_u8_type_id = simple_type(interp->types, TypeTag::Array, range::from_object_bytes(&array_of_u8_type));

	const GlobalValueId std_filepath_value_id = alloc_global_value(interp->globals, array_of_u8_type_id, config->std.filepath.count(), 1);

	global_value_set(interp->globals, std_filepath_value_id, 0, config->std.filepath.as_byte_range());

	const AstBuilderToken import_builtin = push_node(asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, static_cast<AstFlag>(Builtin::Import), AstTag::Builtin);

	push_node(asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, AstFlag::EMPTY, AstLitStringData{ std_filepath_value_id });

	const AstBuilderToken literal_zero = push_node(asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, AstFlag::EMPTY, AstLitIntegerData{ comp_integer_from_u64(0) });

	push_node(asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, AstFlag::EMPTY, AstLitIntegerData{ comp_integer_from_u64(0) });

	push_node(asts, literal_zero, SourceId::INVALID, AstFlag::EMPTY, AstTag::OpCmpEQ);

	const AstBuilderToken source_id_builtin = push_node(asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, static_cast<AstFlag>(Builtin::SourceId), AstTag::Builtin);

	push_node(asts, source_id_builtin, SourceId::INVALID, AstFlag::EMPTY, AstTag::Call);

	const AstBuilderToken import_call = push_node(asts, import_builtin, SourceId::INVALID, AstFlag::EMPTY, AstTag::Call);

	const AstBuilderToken std_definition = push_node(asts, import_call, SourceId::INVALID, AstFlag::EMPTY, AstDefinitionData{ id_from_identifier(identifiers, range::from_literal_string("std")), DependentTypeId::INVALID });

	const AstBuilderToken std_identifier = push_node(asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, AstFlag::EMPTY, AstIdentifierData{ id_from_identifier(identifiers, range::from_literal_string("std")) });

	push_node(asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, AstFlag::EMPTY, AstIdentifierData{ id_from_identifier(identifiers, range::from_literal_string("prelude")) });

	const AstBuilderToken prelude_member = push_node(asts, std_identifier, SourceId::INVALID, AstFlag::EMPTY, AstTag::OpMember);

	push_node(asts, prelude_member, SourceId::INVALID, AstFlag::Definition_IsUse, AstDefinitionData{ id_from_identifier(identifiers, range::from_literal_string("prelude")), DependentTypeId::INVALID });

	push_node(asts, std_definition, SourceId::INVALID, AstFlag::EMPTY, AstTag::File);

	AstNode* const prelude_ast = complete_ast(asts);

	interp->prelude_type_id = type_from_file_ast(interp, prelude_ast, SourceId::INVALID);

	if (interp->log_file.m_rep != nullptr && interp->log_prelude)
	{
		const SourceId file_type_source = type_source_from_id(interp->types, interp->prelude_type_id);

		const SourceLocation file_type_location = source_location_from_source_id(interp->reader, file_type_source);

		diag::print_type(interp->log_file, interp->identifiers, interp->types, independent_type_id(interp->prelude_type_id), &file_type_location);
	}
}



Interpreter* create_interpreter(AllocPool* alloc, Config* config, SourceReader* reader, Parser* parser, TypePool* types, AstPool* asts, IdentifierPool* identifiers, GlobalValuePool* globals, ErrorSink* errors, minos::FileHandle log_file, bool log_prelude) noexcept
{
	Interpreter* const interp = static_cast<Interpreter*>(alloc_from_pool(alloc, sizeof(Interpreter), alignof(Interpreter)));

	interp->reader = reader;
	interp->parser = parser;
	interp->types = types;
	interp->asts = asts;
	interp->identifiers = identifiers;
	interp->globals = globals;
	interp->errors = errors;
	interp->arecs.init(1 << 20, 1 << 9);
	interp->active_arec_id = ArecId::INVALID;
	interp->top_arec_id = ArecId::INVALID;
	interp->prelude_type_id = TypeId::INVALID;
	interp->log_file = log_file;
	interp->log_prelude = log_prelude;

	init_builtin_types(interp);

	init_builtin_values(interp);

	init_prelude_type(interp, config, identifiers, asts);

	return interp;
}

void release_interpreter(Interpreter* interp) noexcept
{
	interp->arecs.release();
}

TypeId import_file(Interpreter* interp, Range<char8> filepath, bool is_std) noexcept
{
	SourceFileRead read = read_source_file(interp->reader, filepath);

	AstNode* root;

	if (read.source_file->root_type != TypeId::INVALID)
	{
		return read.source_file->root_type;
	}
	else if (read.source_file->root_ast == AstNodeId::INVALID)
	{
		root = parse(interp->parser, read.content, read.source_file->source_id_base, is_std, filepath);

		read.source_file->root_ast = id_from_ast_node(interp->asts, root);
	}
	else
	{
		root = ast_node_from_id(interp->asts, read.source_file->root_ast);
	}

	const TypeId file_type_id = type_from_file_ast(interp, root, read.source_file->source_id_base);

	if (interp->log_file.m_rep != nullptr)
	{
		const SourceId file_type_source = type_source_from_id(interp->types, file_type_id);

		const SourceLocation file_type_location = source_location_from_source_id(interp->reader, file_type_source);

		diag::print_type(interp->log_file, interp->identifiers, interp->types, independent_type_id(file_type_id), &file_type_location);
	}

	read.source_file->root_type = file_type_id;

	return file_type_id;
}

const char8* tag_name(Builtin builtin) noexcept
{
	static constexpr const char8* BUILTIN_NAMES[] = {
		"[Unknown]",
		"_integer",
		"_float",
		"_type",
		"_definition",
		"_typeof",
		"_returntypeof",
		"_sizeof",
		"_alignof",
		"_strideof",
		"_offsetof",
		"_nameof",
		"_import",
		"_create_type_builder",
		"_add_type_member",
		"_complete_type",
		"_source_id",
	};

	u8 ordinal = static_cast<u8>(builtin);

	if (ordinal >= array_count(BUILTIN_NAMES))
		ordinal = 0;

	return BUILTIN_NAMES[ordinal];
}

const char8* tag_name(TypeKind type_kind) noexcept
{
	static constexpr const char8* TYPE_KIND_NAMES[] = {
		"[Unknown]",
		"Value",
		"MutLocation",
		"ImmutLocation",
	};

	u8 ordinal = static_cast<u8>(type_kind);

	if (ordinal >= array_count(TYPE_KIND_NAMES))
		ordinal = 0;

	return TYPE_KIND_NAMES[ordinal];
}
