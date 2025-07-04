#include "core.hpp"

#include "../diag/diag.hpp"
#include "../infra/container.hpp"

struct Arec;

using BuiltinFunc = void (*) (Interpreter* interp, Arec* arec, AstNode* call_node, MutRange<byte> into) noexcept;

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

	u32 arec_top;

	s32 selected_arec_index;

	TypeId prelude_type_id;

	s32 context_top;

	TypeId contexts[256];

	TypeId builtin_type_ids[static_cast<u8>(Builtin::MAX)];

	BuiltinFunc builtin_values[static_cast<u8>(Builtin::MAX)];

	minos::FileHandle log_file;

	bool log_prelude;
};

// Activation record.
// This is allocated in `Interpreter.arecs`, and acts somewhat like a stack
// frame. However, an activation record is created not just for every function
// invocation, but for every scope containing definitions. This includes
// blocks, function signature types instantiated which are instantiated on each
// call, and `where` clauses.
struct alignas(8) Arec
{
	// Index of the `Arec` that is the lexical parent of this
	// one.
	// Note that this differs from `prev_stack_index` in two cases:
	// Firstly, when there are other activation records between this one and
	// its parent, as is the case when there is a block inside a call, feeding
	// its result into the call's signature record.
	// Secondly, when this is a root activation record, meaning it has no
	// lexical predecessor. In this case, `parent_index` is set to `-1`.
	s32 parent_index;

	// Index of the `Arec` preceding this one on the stack, in
	// qwords. If there is no previous record on the stack, this is set to `0`.
	// While this is ambiguous with the case when there is only a single
	// record, this is fine, as debug assertions can instead check
	// `Interpreter.arecs.used()` when wanting to ensure there
	// further records.
	u32 prev_stack_index;

	// id of the type of this activation record's `attachment`. This is always
	// a valid `TypeId` referencing a composite type.
	TypeId type_id;

	// Padding to align `attachment` on an 8-byte boundary.
	u32 unused_;

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

struct alignas(8) DependentValue
{
	TypeId resolved_type_id;

	u32 value_offset;
};

// Utility for creating built-in functions types.
struct BuiltinParamInfo
{
	IdentifierId name;

	TypeId type;
};





static bool force_member_type(Interpreter* interp, MemberInfo* member) noexcept; 

static bool force_member_value(Interpreter* interp, MemberInfo* member) noexcept; 

static TypeId resolve_type_id(Interpreter* interp, DependentTypeId type) noexcept;

static TypeId typecheck_dependent_expr(Interpreter* interp, AstNode* node) noexcept;



static TypeId curr_typechecker_context(Interpreter* interp) noexcept
{
	ASSERT_OR_IGNORE(interp->context_top >= 0);

	return interp->contexts[interp->context_top];
}

static void push_typechecker_context(Interpreter* interp, TypeId context) noexcept
{
	ASSERT_OR_IGNORE(interp->context_top >= 0);

	ASSERT_OR_IGNORE(context != TypeId::INVALID);

	ASSERT_OR_IGNORE(interp->contexts[interp->context_top] == lexical_parent_type_from_id(interp->types, context));

	interp->contexts[interp->context_top] = context;
}

static void pop_typechecker_context(Interpreter* interp) noexcept
{
	ASSERT_OR_IGNORE(interp->context_top >= 0);

	const TypeId old_context = interp->contexts[interp->context_top];

	ASSERT_OR_IGNORE(old_context != TypeId::INVALID);

	const TypeId new_context = lexical_parent_type_from_id(interp->types, old_context);

	ASSERT_OR_IGNORE(new_context != TypeId::INVALID);

	interp->contexts[interp->context_top] = new_context;
}

static void set_typechecker_context(Interpreter* interp, TypeId context) noexcept
{
	if (interp->context_top + 1 == static_cast<s32>(array_count(interp->contexts)))
		panic("Maximum active interpreter context limit of %u exceeded.\n", array_count(interp->contexts));

	interp->context_top += 1;

	interp->contexts[interp->context_top] = context;
}

static void unset_typechecker_context(Interpreter* interp) noexcept
{
	ASSERT_OR_IGNORE(interp->context_top >= 0);

	interp->context_top -= 1;
}



static bool has_arec(const Interpreter* interp) noexcept
{
	return interp->selected_arec_index >= 0;
}

static s32 select_arec(Interpreter* interp, s32 index) noexcept
{
	// Note that `-1` is explicitly allowed here to select no arec.
	ASSERT_OR_IGNORE(index >= -1 && index <= static_cast<s32>(interp->arec_top));

	const s32 old_selected = interp->selected_arec_index;

	interp->selected_arec_index = index;

	return old_selected;
}

static Arec* selected_arec(Interpreter* interp) noexcept
{
	ASSERT_OR_IGNORE(has_arec(interp));

	ASSERT_OR_IGNORE(interp->selected_arec_index >= 0 && interp->selected_arec_index <= static_cast<s32>(interp->arec_top));

	return reinterpret_cast<Arec*>(interp->arecs.begin() + interp->selected_arec_index);
}

static s32 selected_arec_index(Interpreter* interp) noexcept
{
	return interp->selected_arec_index;
}

static Arec* push_arec(Interpreter* interp, TypeId record_type_id, s32 parent_index) noexcept
{
	ASSERT_OR_IGNORE(type_tag_from_id(interp->types, record_type_id) == TypeTag::Composite);

	const TypeMetrics record_metrics = type_metrics_from_id(interp->types, record_type_id);

	// TODO: Make this properly aligned for over-aligned types.
	// That also needs to account for the 8-byte skew created by the
	// `Arec`.

	Arec* const arec = static_cast<Arec*>(interp->arecs.reserve_padded(static_cast<u32>(sizeof(Arec) + record_metrics.size)));
	arec->prev_stack_index = interp->arec_top;
	arec->parent_index = parent_index;
	arec->type_id = record_type_id;

	const u32 arec_index = static_cast<u32>(reinterpret_cast<const u64*>(arec) - interp->arecs.begin());

	interp->arec_top = arec_index;

	if (parent_index >= 0)
		interp->selected_arec_index = static_cast<s32>(arec_index);

	return arec;
}

static void pop_arec(Interpreter* interp) noexcept
{
	ASSERT_OR_IGNORE(has_arec(interp));

	const u32 new_top = selected_arec(interp)->prev_stack_index;

	interp->arecs.pop_to(interp->arec_top);

	interp->arec_top = new_top;
}

static MutRange<byte> stack_alloc_temporary(Interpreter* interp, u64 size, u32 align) noexcept
{
	if (size > UINT32_MAX)
		panic("Tried allocating local storage exceeding allowed maximum size in activation record.\n");

	interp->arecs.pad_to_alignment(align);

	return MutRange{ static_cast<byte*>(interp->arecs.reserve_padded(static_cast<u32>(size))), size };
}

static void stack_free_temporary(Interpreter* interp, Range<byte> temporary)
{
	ASSERT_OR_IGNORE(reinterpret_cast<const u64*>(temporary.begin()) > interp->arecs.begin() + interp->arec_top || (reinterpret_cast<const u64*>(temporary.begin()) == interp->arecs.begin() && interp->arec_top == 0));

	interp->arecs.pop_to(static_cast<u32>(reinterpret_cast<const u64*>(temporary.begin()) - interp->arecs.begin()));
}

static bool has_parent_arec(Arec* arec) noexcept
{
	return arec->parent_index >= 0;
}

static Arec* parent_arec(Interpreter* interp, Arec* arec) noexcept
{
	ASSERT_OR_IGNORE(has_parent_arec(arec));

	return reinterpret_cast<Arec*>(interp->arecs.begin() + arec->prev_stack_index);
}

static s32 arec_index(Interpreter* interp, Arec* arec) noexcept
{
	ASSERT_OR_IGNORE(reinterpret_cast<u64*>(arec) >= interp->arecs.begin() && reinterpret_cast<u64*>(arec) < interp->arecs.end());

	return static_cast<s32>(reinterpret_cast<u64*>(arec) - interp->arecs.begin());
}



static bool lookup_identifier_info_in_arec(Interpreter* interp, Arec* arec, IdentifierId identifer, MemberInfo* out) noexcept
{
	return type_member_info_by_name(interp->types, arec->type_id, identifer, out);
}

static MutRange<byte> lookup_identifier_location_in_arec(Interpreter* interp, Arec* arec, IdentifierId identifier) noexcept
{
	MemberInfo info;

	if (!lookup_identifier_info_in_arec(interp, arec, identifier, &info))
		ASSERT_UNREACHABLE;

	ASSERT_OR_IGNORE(!info.has_pending_type);

	if (info.is_global)
	{
		return global_value_get_mut(interp->globals, info.value.complete);
	}
	else
	{
		const TypeId type_id = resolve_type_id(interp, info.type.complete);

		return MutRange{ arec->attachment + info.offset, type_metrics_from_id(interp->types, type_id).size };
	}
}

static bool lookup_identifier_info_and_arec(Interpreter* interp, IdentifierId identifier, MemberInfo* out_info, OptPtr<Arec>* out_arec) noexcept
{
	if (has_arec(interp))
	{
		Arec* arec = selected_arec(interp);

		while (true)
		{
			if (lookup_identifier_info_in_arec(interp, arec, identifier, out_info))
			{
				*out_arec = some(arec);

				return true;
			}

			if (!has_parent_arec(arec))
				break;

			arec = parent_arec(interp, arec);
		}
	}

	TypeId context = curr_typechecker_context(interp);

	while (true)
	{
		if (type_member_info_by_name(interp->types, context, identifier, out_info))
		{
			*out_arec = none<Arec>();

			return true;
		}

		context = lexical_parent_type_from_id(interp->types, context);

		if (context == TypeId::INVALID)
			return false;
	}
}

static bool lookup_identifier_info(Interpreter* interp, IdentifierId identifier, MemberInfo* out) noexcept
{
	OptPtr<Arec> unused;

	return lookup_identifier_info_and_arec(interp, identifier, out, &unused);
}

static MutRange<byte> lookup_identifier_location(Interpreter* interp, IdentifierId identifier) noexcept
{
	MemberInfo info;

	OptPtr<Arec> arec;

	if (!lookup_identifier_info_and_arec(interp, identifier, &info, &arec))
		ASSERT_UNREACHABLE;

	if (info.is_global)
	{
		force_member_value(interp, &info);

		return global_value_get_mut(interp->globals, info.value.complete);
	}
	else
	{
		ASSERT_OR_IGNORE(is_some(arec));

		ASSERT_OR_IGNORE(!info.has_pending_type);

		return MutRange{ get_ptr(arec)->attachment + info.offset, type_metrics_from_id(interp->types, resolve_type_id(interp, info.type.complete)).size };
	}
}





template<typename T>
static T* bytes_as(MutRange<byte> bytes) noexcept
{
	ASSERT_OR_IGNORE(bytes.count() == sizeof(T));

	return reinterpret_cast<T*>(bytes.begin());
}

template<typename T>
static const T* bytes_as(Range<byte> bytes) noexcept
{
	ASSERT_OR_IGNORE(bytes.count() == sizeof(T));

	return reinterpret_cast<const T*>(bytes.begin());
}

template<typename T>
static T load_as(bool load, MutRange<byte> src) noexcept
{
	if (load)
	{
		MutRange<byte> loaded_src = *bytes_as<MutRange<byte>>(src);

		return *bytes_as<T>(loaded_src);
	}
	else
	{
		ASSERT_OR_IGNORE(src.count() == sizeof(T));

		return *bytes_as<T>(src);
	}
}

template<typename T>
static T load_as(bool load, Range<byte> src) noexcept
{
	if (load)
	{
		Range<byte> loaded_src = *bytes_as<Range<byte>>(src);

		return *bytes_as<T>(loaded_src);
	}
	else
	{
		ASSERT_OR_IGNORE(src.count() == sizeof(T));

		return *bytes_as<T>(src);
	}
}

void store(MutRange<byte> dst, Range<byte> src) noexcept
{
	ASSERT_OR_IGNORE(dst.count() == src.count());

	memcpy(dst.begin(), src.begin(), dst.count());
}



static TypeMetrics dependent_member_metrics(Interpreter* interp, DependentTypeId type) noexcept
{
	return is_dependent(type)
		? TypeMetrics{ 8, 8, 8 }
		: type_metrics_from_id(interp->types, completed(type));
}

static TypeId resolve_type_id(Interpreter* interp, DependentTypeId type) noexcept
{
	if (is_dependent(type))
	{
		AstNode* const type_node = ast_node_from_id(interp->asts, delayed(type));
	
		return typecheck_dependent_expr(interp, type_node);
	}
	else
	{
		return completed(type);
	}
}

static bool kind_is_convertible_to_from(TypeKind dst, TypeKind src) noexcept
{
	ASSERT_OR_IGNORE(dst != TypeKind::INVALID && src != TypeKind::INVALID);

	return static_cast<u8>(dst) <= static_cast<u8>(src);
}

static bool any_integer_to_u64(Interpreter* interp, TypeId type_id, const void* value, u64* out) noexcept
{
	const TypeTag type_tag = type_tag_from_id(interp->types, type_id);

	if (type_tag == TypeTag::CompInteger)
	{
		const CompIntegerValue count_value = *static_cast<const CompIntegerValue*>(value);

		if (!u64_from_comp_integer(count_value, 64, out))
			return false;
	}
	else
	{
		ASSERT_OR_IGNORE(type_tag == TypeTag::Integer);

		const u64 n = *static_cast<const u64*>(value);

		const NumericType* const integer_type = static_cast<const NumericType*>(simple_type_structure_from_id(interp->types, type_id));

		if (integer_type->is_signed && ((n >> (integer_type->bits - 1)) & 1) != 0)
			return false;
	}

	return true;
}

static void impconv_load(Interpreter* interp, MutRange<byte> dst, Range<byte> src, TypeId resolved_dst_type_id) noexcept
{
	ASSERT_OR_IGNORE(src.count() == sizeof(MutRange<byte>));

	ASSERT_OR_IGNORE(dst.count() == type_metrics_from_id(interp->types, resolved_dst_type_id).size);

	Range<byte> loaded_src = *bytes_as<Range<byte>>(src);

	ASSERT_OR_IGNORE(loaded_src.count() == dst.count());

	memcpy(dst.begin(), loaded_src.begin(), dst.count());
}

static void impconv(Interpreter* interp, MutRange<byte> dst, TypeId resolved_dst_type_id, Range<byte> src, TypeId resolved_src_type_id, const AstNode* src_node) noexcept
{
	const TypeTag dst_type_tag = type_tag_from_id(interp->types, resolved_dst_type_id);

	const TypeTag src_type_tag = type_tag_from_id(interp->types, resolved_src_type_id);

	const bool load = has_flag(src_node, AstFlag::Any_LoadResult);

	switch (dst_type_tag)
	{
	case TypeTag::Slice:
	{
		if (src_type_tag == TypeTag::Array)
		{
			// Only dereference arrays once, instead of twice as would be done
			// on a normal load. This allows us to directly get a range over
			// the array, which is already a slice, so the conversion is done
			// at this point.
			Range<byte> src_value = load ? *bytes_as<Range<byte>>(src) : src;

			ASSERT_OR_IGNORE(src_value.count() == type_metrics_from_id(interp->types, resolved_src_type_id).size);

			*bytes_as<Range<byte>>(dst) = src_value.as_byte_range();
		}
		else
		{
			ASSERT_OR_IGNORE(src_type_tag == TypeTag::Slice);

			ASSERT_OR_IGNORE(load);

			impconv_load(interp, dst, src, resolved_dst_type_id);
		}

		break;
	}

	case TypeTag::Integer:
	{
		if (src_type_tag == TypeTag::CompInteger)
		{
			const CompIntegerValue src_value = load_as<CompIntegerValue>(load, src);

			const NumericType dst_type = *static_cast<const NumericType*>(simple_type_structure_from_id(interp->types, resolved_dst_type_id));

			ASSERT_OR_IGNORE((dst_type.bits & 7) == 0 && dst_type.bits <= 64 && dst_type.bits != 0);

			if (dst_type.is_signed)
			{
				s64 signed_value;

				if (!s64_from_comp_integer(src_value, static_cast<u8>(dst_type.bits), &signed_value))
					source_error(interp->errors, src_node->source_id, "Compile-time integer constant exceeds range of signed %u bit integer.\n", dst_type.bits);

				ASSERT_OR_IGNORE(dst_type.bits / 8 == dst.count());

				store(dst, Range{ reinterpret_cast<byte*>(&signed_value), static_cast<u64>(dst_type.bits / 8) });
			}
			else
			{
				u64 unsigned_value;

				if (!u64_from_comp_integer(src_value, static_cast<u8>(dst_type.bits), &unsigned_value))
					source_error(interp->errors, src_node->source_id, "Compile-time integer constant exceeds range of unsigned %u bit integer.\n", dst_type.bits);

				ASSERT_OR_IGNORE(dst_type.bits / 8 == dst.count());

				store(dst, Range{ reinterpret_cast<byte*>(&unsigned_value), static_cast<u64>(dst_type.bits / 8) });
			}
		}
		else
		{
			ASSERT_OR_IGNORE(src_type_tag == TypeTag::Integer);

			ASSERT_OR_IGNORE(load);

			impconv_load(interp, dst, src, resolved_dst_type_id);
		}

		break;
	}

	case TypeTag::Float:
	{
		if (src_type_tag == TypeTag::CompFloat)
		{
			const CompFloatValue src_value = load_as<CompFloatValue>(load, src);

			const NumericType dst_type = *static_cast<const NumericType*>(simple_type_structure_from_id(interp->types, resolved_dst_type_id));

			if (dst_type.bits == 32)
			{
				const f32 f32_value = f32_from_comp_float(src_value);

				store(dst, range::from_object_bytes(&f32_value));
			}
			else
			{
				ASSERT_OR_IGNORE(dst_type.bits == 64);

				const f64 f64_value = f64_from_comp_float(src_value);

				store(dst, range::from_object_bytes(&f64_value));
			}
		}
		else
		{
			ASSERT_OR_IGNORE(src_type_tag == TypeTag::Float);

			ASSERT_OR_IGNORE(load);

			impconv_load(interp, dst, src, resolved_dst_type_id);
		}

		break;
	}

	case TypeTag::TypeInfo:
	{
		store(dst, range::from_object_bytes(&resolved_src_type_id));

		break;
	}

	case TypeTag::Void:
	case TypeTag::Type:
	case TypeTag::Definition:
	case TypeTag::CompInteger:
	case TypeTag::CompFloat:
	case TypeTag::Boolean:
	case TypeTag::Ptr:
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
	{
		if (load)
			impconv_load(interp, dst, src, resolved_dst_type_id);

		break;
	}

	case TypeTag::INVALID:
	case TypeTag::Dependent:
		ASSERT_UNREACHABLE;
	}

	stack_free_temporary(interp, src);
}

static MutRange<byte> map_into_for_impconv(Interpreter* interp, const AstNode* node, TypeId resolved_type_id, MutRange<byte> unmapped_into) noexcept
{
	if (has_flag(node, AstFlag::Any_SkipConversion))
		return unmapped_into;

	const TypeKind kind = static_cast<TypeKind>(node->type_kind);

	u64 required_bytes;

	u32 required_align;

	if (kind == TypeKind::Value)
	{
		const TypeMetrics metrics = type_metrics_from_id(interp->types, resolved_type_id);

		required_bytes = metrics.size;

		required_align = metrics.align;
	}
	else
	{
		ASSERT_OR_IGNORE(kind == TypeKind::MutLocation || kind == TypeKind::ImmutLocation);

		required_bytes = 16;

		required_align = 16;
	}

	return stack_alloc_temporary(interp, required_bytes, required_align);
}

static void ensure_load(AstNode* node) noexcept
{
	ASSERT_OR_IGNORE(static_cast<TypeKind>(node->type_kind) != TypeKind::INVALID);

	if (static_cast<TypeKind>(node->type_kind) != TypeKind::Value)
		node->flags |= AstFlag::Any_LoadResult;
}

// Checks whether `node`'s type requires an implicit conversion to conform to
// `expected_type_id` and `expected_kind`, and stores this information in
// `node`'s flags (`AstFlag::Any_NoConversion`).
// In case `expected_type` is dependent, `TypeId::INVALID` is passed for
// `expected_type_id`.
// Returns `false` if the conversion is illegal, `true` otherwise.
// In particular, even when `true` is returned, the conversion may still be
// illegal if dependent types are involved.
static bool check_and_set_impconv(Interpreter* interp, AstNode* node, DependentTypeId expected_type, TypeKind expected_kind) noexcept
{
	const TypeKind kind = static_cast<TypeKind>(node->type_kind);

	ASSERT_OR_IGNORE(kind != TypeKind::INVALID && expected_kind != TypeKind::INVALID);

	ASSERT_OR_IGNORE(is_dependent(expected_type) || completed(expected_type) != TypeId::CHECKING);

	if (!kind_is_convertible_to_from(expected_kind, kind))
		return false;

	bool skip_conversion = kind == TypeKind::Value || expected_kind != TypeKind::Value;

	if (is_dependent(node->type) || is_dependent(expected_type))
		skip_conversion = false;
	else if (type_can_implicitly_convert_from_to(interp->types, completed(node->type), completed(expected_type)))
		skip_conversion &= is_same_type(interp->types, completed(node->type), completed(expected_type));
	else
		return false;

	if (skip_conversion)
		node->flags |= AstFlag::Any_SkipConversion;
	
	if (kind != TypeKind::Value && expected_kind == TypeKind::Value)
		node->flags |= AstFlag::Any_LoadResult;

	return true;
}



static void evaluate_expr(Interpreter* interp, AstNode* node, TypeId resolved_type_id, MutRange<byte> into) noexcept
{
	ASSERT_OR_IGNORE(node->type != DependentTypeId::INVALID && static_cast<TypeKind>(node->type_kind) != TypeKind::INVALID);

	switch (node->tag)
	{
	case AstTag::Builtin:
	{
		const u8 builtin_ordinal = static_cast<u8>(node->flags);

		Callable result{};
		result.is_builtin = true;
		result.func_type_id_bits = static_cast<u32>(interp->builtin_type_ids[builtin_ordinal]);
		result.code.ordinal = builtin_ordinal;

		store(into, range::from_object_bytes(&result));

		return;
	}

	case AstTag::Definition:
	{
		const DefinitionInfo info = get_definition_info(node);

		const AstDefinitionData* const attachment = attachment_of<AstDefinitionData>(node);

		MemberInit init{};
		init.name = attachment->identifier_id;
		init.source = node->source_id;
		init.type.complete = attachment->defined_type;
		init.value.pending = is_some(info.value) ? id_from_ast_node(interp->asts, get_ptr(info.value)) : AstNodeId::INVALID;
		init.completion_context = curr_typechecker_context(interp);
		init.completion_arec = selected_arec_index(interp);
		init.is_global = has_flag(node, AstFlag::Definition_IsGlobal);
		init.is_pub = has_flag(node, AstFlag::Definition_IsPub);
		init.is_use = has_flag(node, AstFlag::Definition_IsUse);
		init.is_mut = has_flag(node, AstFlag::Definition_IsMut);
		init.has_pending_type = attachment->defined_type == DependentTypeId::INVALID;
		init.has_pending_value = is_some(info.value);
		init.offset = 0;

		store(into, range::from_object_bytes(&init));

		return;
	}

	case AstTag::Block:
	{
		Arec* const block_arec = push_arec(interp, attachment_of<AstBlockData>(node)->scope_type_id, selected_arec_index(interp));

		const s32 restore_arec = select_arec(interp, arec_index(interp, block_arec));

		AstDirectChildIterator stmts = direct_children_of(node);

		while (has_next(&stmts))
		{
			AstNode* const stmt = next(&stmts);

			if (!has_next_sibling(stmt))
			{
				const TypeId resolved_stmt_type_id = resolve_type_id(interp, stmt->type);

				evaluate_expr(interp, stmt, resolved_stmt_type_id, into);
			}
			else if (stmt->tag == AstTag::Definition)
			{
				DefinitionInfo info = get_definition_info(stmt);

				ASSERT_OR_IGNORE(is_some(info.value));

				AstNode* const value = get_ptr(info.value);

				MutRange<byte> unmapped_location = lookup_identifier_location_in_arec(interp, block_arec, attachment_of<AstDefinitionData>(stmt)->identifier_id);

				const DependentTypeId defined_type = attachment_of<AstDefinitionData>(stmt)->defined_type;

				const TypeId resolved_value_type_id = resolve_type_id(interp, stmt->type);

				MutRange<byte> mapped_location = map_into_for_impconv(interp, value, resolved_value_type_id, unmapped_location);

				evaluate_expr(interp, value, resolved_value_type_id, mapped_location);

				if (mapped_location.begin() != unmapped_location.begin())
					impconv(interp, unmapped_location, resolve_type_id(interp, defined_type), mapped_location.immut(), resolved_value_type_id, value);
			}
			else
			{
				const TypeId resolved_stmt_type_id = resolve_type_id(interp, stmt->type);

				ASSERT_OR_IGNORE(type_tag_from_id(interp->types, resolved_stmt_type_id) == TypeTag::Void);

				evaluate_expr(interp, stmt, resolved_stmt_type_id, MutRange<byte>{});
			}
		}

		pop_arec(interp);

		select_arec(interp, restore_arec);

		return;
	}

	case AstTag::Func:
	{
		FuncInfo info = get_func_info(node);

		if (is_some(info.body))
		{
			Callable callable{};
			callable.is_builtin = false;
			callable.func_type_id_bits = static_cast<u32>(resolve_type_id(interp, node->type));
			callable.code.ast = id_from_ast_node(interp->asts, get_ptr(info.body));

			store(into, range::from_object_bytes(&callable));
		}
		else
		{
			TODO("Implement function-signature-to-type evaluation. This is probably best done by modifying the AST to have a `FuncSignature` node.");
		}

		return;
	}

	case AstTag::Identifer:
	{
		Range<byte> location = lookup_identifier_location(interp, attachment_of<AstIdentifierData>(node)->identifier_id).immut();

		store(into, range::from_object_bytes(&location));

		return;
	}

	case AstTag::LitInteger:
	{
		store(into, range::from_object_bytes(&attachment_of<AstLitIntegerData>(node)->value));

		return;
	}

	case AstTag::LitString:
	{
		const Range<byte> global_value = global_value_get(interp->globals, attachment_of<AstLitStringData>(node)->string_value_id);

		store(into, range::from_object_bytes(&global_value));

		return;
	}

	case AstTag::Call:
	{
		AstDirectChildIterator args = direct_children_of(node);

		const bool has_callee = has_next(&args);

		ASSERT_OR_IGNORE(has_callee);

		AstNode* const callee = next(&args);

		const TypeKind kind = static_cast<TypeKind>(callee->type_kind);

		const TypeId resolved_callee_type_id = resolve_type_id(interp, callee->type);

		Callable callee_value;

		// We sidestep impconv here, as callables cannot undergo implicit
		// conversions other than loads, which are directly implemented here.
		if (kind == TypeKind::Value)
		{
			evaluate_expr(interp, callee, resolved_callee_type_id, MutRange{ &callee_value, 1 }.as_mut_byte_range());
		}
		else
		{
			ASSERT_OR_IGNORE(kind == TypeKind::ImmutLocation || kind == TypeKind::MutLocation);

			MutRange<byte> callee_location{};

			evaluate_expr(interp, callee, resolved_callee_type_id, MutRange{ &callee_location, 1 }.as_mut_byte_range());

			callee_value = *bytes_as<Callable>(callee_location);
		}

		const FuncType* const callee_structure = static_cast<const FuncType*>(simple_type_structure_from_id(interp->types, resolved_callee_type_id));

		ASSERT_OR_IGNORE(callee_structure->param_count < 64);

		const TypeId signature_type_id = callee_structure->signature_type_id;

		Arec* const signature_arec = push_arec(interp, signature_type_id, -1);

		u16 arg_rank = 0;

		u64 seen_args = 0;

		while (has_next(&args))
		{
			AstNode* arg = next(&args);

			MemberInfo arg_info;

			if (arg->tag == AstTag::OpSet)
			{
				AstNode* const implied_member = first_child_of(arg);

				ASSERT_OR_IGNORE(implied_member->tag == AstTag::UOpImpliedMember);

				AstNode* const arg_name = first_child_of(implied_member);

				if (!type_member_info_by_name(interp->types, signature_type_id, attachment_of<AstIdentifierData>(arg_name)->identifier_id, &arg_info))
				{
					const Range<char8> name = identifier_name_from_id(interp->identifiers, attachment_of<AstIdentifierData>(arg_name)->identifier_id);

					source_error(interp->errors, arg->source_id, "Called function has no argument named `%.*s`\n", static_cast<s32>(name.count()), name.begin());
				}

				const u64 arg_mask = static_cast<u64>(1) << arg_info.rank;

				if ((seen_args & arg_mask) != 0)
				{
					const Range<char8> name = identifier_name_from_id(interp->identifiers, attachment_of<AstIdentifierData>(arg_name)->identifier_id);

					source_error(interp->errors, arg->source_id, "Specified function argument `%.*s` more than once\n", static_cast<s32>(name.count()), name.begin());
				}

				seen_args |= arg_mask;

				arg = next_sibling_of(implied_member);
			}
			else
			{
				if (!type_member_info_by_rank(interp->types, signature_type_id, arg_rank, &arg_info))
					source_error(interp->errors, arg->source_id, "Too many arguments in function call, expected at most %u.\n", callee_structure->param_count);

				ASSERT_OR_IGNORE(arg_rank < 64);

				seen_args |= static_cast<u64>(1) << arg_rank;

				arg_rank += 1;
			}

			ASSERT_OR_IGNORE(!arg_info.is_global && !arg_info.has_pending_type && !arg_info.has_pending_value);

			const TypeId resolved_arg_slot_type_id = resolve_type_id(interp, arg_info.type.complete);

			const TypeId resolved_arg_value_type_id = resolve_type_id(interp, arg->type);

			const u64 arg_slot_size = type_metrics_from_id(interp->types, resolved_arg_slot_type_id).size;

			MutRange<byte> unmapped_arg_slot = MutRange{ signature_arec->attachment + arg_info.offset, arg_slot_size };

			MutRange<byte> mapped_arg_slot = map_into_for_impconv(interp, arg, resolved_arg_value_type_id, unmapped_arg_slot);

			evaluate_expr(interp, arg, resolved_arg_value_type_id, mapped_arg_slot);

			if (mapped_arg_slot.begin() != unmapped_arg_slot.begin())
				impconv(interp, unmapped_arg_slot, resolved_arg_slot_type_id, mapped_arg_slot.immut(), resolved_arg_value_type_id, arg);
		}

		const u64 expected_args = (static_cast<u64>(1) << (callee_structure->param_count & 63)) - 1;

		while (seen_args != expected_args)
		{
			const u16 missing_arg_rank = count_trailing_ones(seen_args);

			MemberInfo arg_info;

			if (!type_member_info_by_rank(interp->types, signature_type_id, missing_arg_rank, &arg_info))
				ASSERT_UNREACHABLE;

			ASSERT_OR_IGNORE(!arg_info.is_global && !arg_info.has_pending_type && !arg_info.has_pending_value);

			if (arg_info.value.complete == GlobalValueId::INVALID)
			{
				const Range<char8> name = identifier_name_from_id(interp->identifiers, arg_info.name);

				source_error(interp->errors, node->source_id, "Missing value for mandatory parameter `%.*s` at position `%u`\n", static_cast<s32>(name.count()), name.begin(), arg_info.rank + 1);
			}

			const TypeId resolved_arg_slot_type_id = resolve_type_id(interp, arg_info.type.complete);

			Range<byte> default_value = global_value_get(interp->globals, arg_info.value.complete);

			const u64 arg_size = type_metrics_from_id(interp->types, resolved_arg_slot_type_id).size;

			store(MutRange{ signature_arec->attachment + arg_info.offset, arg_size }, default_value);

			seen_args |= static_cast<u64>(1) << missing_arg_rank;
		}

		const s32 restore_arec = select_arec(interp, arec_index(interp, signature_arec));

		if (callee_value.is_builtin)
		{
			interp->builtin_values[callee_value.code.ordinal](interp, signature_arec, node, into);
		}
		else
		{
			AstNode* const body = ast_node_from_id(interp->asts, callee_value.code.ast);

			const TypeId resolved_body_type_id = resolve_type_id(interp, body->type);

			// We need to map `into` here, to model the conversion from the
			// body's type to the declared return type.
			MutRange<byte> mapped_into = map_into_for_impconv(interp, body, resolved_body_type_id, into);

			evaluate_expr(interp, body, resolved_type_id, mapped_into);

			if (mapped_into.begin() != into.begin())
				impconv(interp, into, resolved_type_id, mapped_into.immut(), resolved_body_type_id, body);
		}

		pop_arec(interp);

		select_arec(interp, restore_arec);

		return;
	}

	case AstTag::OpMember:
	{
		AstNode* const lhs = first_child_of(node);

		const TypeKind lhs_kind = static_cast<TypeKind>(lhs->type_kind);

		const TypeId resolved_lhs_type_id = resolve_type_id(interp, lhs->type);

		const TypeTag lhs_type_tag = type_tag_from_id(interp->types, resolved_lhs_type_id);

		AstNode* const rhs = next_sibling_of(lhs);

		ASSERT_OR_IGNORE(rhs->tag == AstTag::Identifer);

		const IdentifierId member_identifier = attachment_of<AstIdentifierData>(rhs)->identifier_id;

		if (lhs_type_tag == TypeTag::Type)
		{
			TypeId lhs_defined_type_id;

			// Load without impconv, since no other conversion can happen here.
			if (lhs_kind == TypeKind::Value)
			{
				evaluate_expr(interp, lhs, resolved_lhs_type_id, range::from_object_bytes_mut(&lhs_defined_type_id));
			}
			else
			{
				MutRange<byte> lhs_defined_type_id_location;

				evaluate_expr(interp, lhs, resolved_lhs_type_id, range::from_object_bytes_mut(&lhs_defined_type_id_location));

				ASSERT_OR_IGNORE(lhs_defined_type_id_location.count() == sizeof(lhs_defined_type_id));

				lhs_defined_type_id = *bytes_as<TypeId>(lhs_defined_type_id_location);
			}

			MemberInfo info;

			const bool member_found = type_member_info_by_name(interp->types, lhs_defined_type_id, member_identifier, &info);

			ASSERT_OR_IGNORE(member_found);

			ASSERT_OR_IGNORE(info.is_global);

			const bool value_is_dependent = force_member_value(interp, &info);

			ASSERT_OR_IGNORE(value_is_dependent);

			Range<byte> global_value = global_value_get(interp->globals, info.value.complete);

			store(into, range::from_object_bytes(&global_value));
		}
		else
		{
			ASSERT_OR_IGNORE(lhs_type_tag == TypeTag::Composite);

			MemberInfo info;

			const bool member_found = type_member_info_by_name(interp->types, resolved_lhs_type_id, member_identifier, &info);

			ASSERT_OR_IGNORE(member_found);

			if (info.is_global)
			{
				// TODO: Should `lhs` still be evaluated here? It's not
				//       strictly necessary, but might make for more consistent
				//       semantics.

				const bool member_is_dependent = force_member_value(interp, &info);

				ASSERT_OR_IGNORE(!member_is_dependent);

				Range<byte> global_value = global_value_get(interp->globals, info.value.complete);

				store(into, range::from_object_bytes(&global_value));
			}
			else
			{
				u64 lhs_into_size;

				u32 lhs_into_align;

				if (lhs_kind == TypeKind::Value)
				{
					const TypeMetrics metrics = type_metrics_from_id(interp->types, resolved_lhs_type_id);

					lhs_into_size = metrics.size;

					lhs_into_align = metrics.align;
					
					TODO("Somehow extend lifetime of left-hand-side of member operator if it is a value and not a location.");
				}
				else
				{
					lhs_into_size = 8;

					lhs_into_align = 8;
				}

				MutRange<byte> lhs_into = stack_alloc_temporary(interp, lhs_into_size, lhs_into_align);

				evaluate_expr(interp, lhs, resolved_lhs_type_id, lhs_into);

				MutRange<byte> lhs_value;

				if (lhs_kind == TypeKind::Value)
				{
					lhs_value = lhs_into;
				}
				else
				{
					lhs_value = *bytes_as<MutRange<byte>>(lhs_value);
				}

				store(into, lhs_value.subrange(info.offset, type_metrics_from_id(interp->types, resolved_type_id).size));

				stack_free_temporary(interp, lhs_into.immut());
			}
		}

		return;
	}

	case AstTag::OpCmpEQ:
	{
		// TODO: Implement.
		const bool dummy_true = true;

		store(into, range::from_object_bytes(&dummy_true));

		return;
	}

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

	case AstTag::File:
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

static bool typecheck_expr(Interpreter* interp, AstNode* node) noexcept
{
	const DependentTypeId prev_type_id = node->type;

	if (!is_dependent(prev_type_id) && completed(prev_type_id) == TypeId::CHECKING)
		source_error(interp->errors, node->source_id, "Cyclic type dependency detected during typechecking.\n");
	
	if (prev_type_id != DependentTypeId::INVALID)
		return has_flag(node, AstFlag::Any_HasDependentValue) || is_dependent(node->type);

	node->type = completed_type_id(TypeId::CHECKING);

	switch (node->tag)
	{
	case AstTag::Builtin:
	{
		node->type = completed_type_id(interp->builtin_type_ids[static_cast<u8>(node->flags)]);
		node->type_kind = static_cast<u8>(TypeKind::Value);

		return false;
	}

	case AstTag::Definition:
	{
		DefinitionInfo info = get_definition_info(node);

		DependentTypeId defined_type = DependentTypeId::INVALID;

		bool dependent = false;

		if (is_some(info.type))
		{
			AstNode* const type = get_ptr(info.type);

			const bool type_is_dependent = typecheck_expr(interp, type);

			const TypeKind kind = static_cast<TypeKind>(type->type_kind);

			const TypeId type_type_id = simple_type(interp->types, TypeTag::Type, {});

			if (!check_and_set_impconv(interp, type, completed_type_id(type_type_id), TypeKind::Value))
				source_error(interp->errors, type->source_id, "Type annotation must be of type `Type`.\n");

			if (type_is_dependent)
			{
				defined_type = dependent_type_id(id_from_ast_node(interp->asts, type), DependentTypePosition::Type);
			}
			else if (kind == TypeKind::Value)
			{
				evaluate_expr(interp, type, type_type_id, range::from_object_bytes_mut(&defined_type));
			}
			else
			{
				MutRange<byte> annotated_type_id_location{};

				evaluate_expr(interp, type, type_type_id, range::from_object_bytes_mut(&annotated_type_id_location));

				defined_type = *bytes_as<DependentTypeId>(annotated_type_id_location);
			}

			attachment_of<AstDefinitionData>(node)->defined_type = defined_type;

			dependent |= type_is_dependent;
		}

		if (is_some(info.value))
		{
			AstNode* const value = get_ptr(info.value);

			const bool value_is_dependent = typecheck_expr(interp, value);

			if (is_none(info.type))
				attachment_of<AstDefinitionData>(node)->defined_type = value->type;
			else if (!check_and_set_impconv(interp, value, defined_type, TypeKind::Value))
				source_error(interp->errors, value->source_id, "Cannot convert definition value to explicitly annotated type.\n");

			dependent |= value_is_dependent;
		}

		node->type = completed_type_id(simple_type(interp->types, TypeTag::Definition, {}));
		node->type_kind = static_cast<u8>(TypeKind::Value);

		if (dependent)
			node->flags |= AstFlag::Any_HasDependentValue;

		return dependent;
	}

	case AstTag::Block:
	{
		bool dependent = false;

		const TypeId scope_type_id = create_open_type(interp->types, curr_typechecker_context(interp), node->source_id, false);

		attachment_of<AstBlockData>(node)->scope_type_id = scope_type_id;

		u64 offset_in_scope = 0;

		// Initial alignment is 8, since scopes are allocated in
		// `Interpreter.arecs`, where they are always aligned to 8 bytes.
		// This does not currently have any effect, but if there are future
		// optimizations relying on alignment, this is better than
		// underspecifying the minimal scope alignment as 1.
		u32 scope_align = 8;

		u16 scope_rank = 0;

		push_typechecker_context(interp, scope_type_id);

		AstDirectChildIterator stmts = direct_children_of(node);

		AstNode* last_stmt = nullptr;

		while (has_next(&stmts))
		{
			AstNode* const stmt = next(&stmts);

			dependent |= typecheck_expr(interp, stmt);

			if (!has_next_sibling(stmt))
			{
				last_stmt = stmt;

				break;
			}

			if (stmt->tag == AstTag::Definition)
			{
				const AstDefinitionData* const attachment = attachment_of<AstDefinitionData>(stmt);

				ASSERT_OR_IGNORE(attachment->defined_type != DependentTypeId::INVALID);

				const TypeMetrics metrics = dependent_member_metrics(interp, attachment->defined_type);

				offset_in_scope = next_multiple(offset_in_scope, static_cast<u64>(metrics.align));

				MemberInit init{};
				init.name = attachment->identifier_id;
				init.source = node->source_id;
				init.type.complete = is_dependent(attachment->defined_type)
					? completed_type_id(simple_type(interp->types, TypeTag::Dependent, {}))
					: attachment->defined_type;
				init.value.complete = GlobalValueId::INVALID;
				init.completion_context = curr_typechecker_context(interp);
				init.completion_arec = selected_arec_index(interp);
				init.is_global = has_flag(node, AstFlag::Definition_IsGlobal);
				init.is_pub = has_flag(node, AstFlag::Definition_IsPub);
				init.is_use = has_flag(node, AstFlag::Definition_IsUse);
				init.is_mut = has_flag(node, AstFlag::Definition_IsMut);
				init.has_pending_type = false;
				init.has_pending_value = false;
				init.offset = offset_in_scope;

				if (init.is_global)
					source_error(interp->errors, stmt->source_id, "Block-level globals are not currently supported.\n");

				ASSERT_OR_IGNORE(!init.has_pending_type && !init.has_pending_value);

				add_open_type_member(interp->types, scope_type_id, init);

				offset_in_scope += metrics.size;

				if (metrics.align > scope_align)
					scope_align = metrics.align;

				if (scope_rank == UINT16_MAX)
					source_error(interp->errors, stmt->source_id, "Exceeded maximum of %u definitions in a single block.\n", UINT16_MAX);

				scope_rank += 1;
			}

			if (!is_dependent(stmt->type))
			{
				const TypeTag stmt_result_tag = type_tag_from_id(interp->types, completed(stmt->type));

				if (stmt_result_tag != TypeTag::Void && stmt_result_tag != TypeTag::Definition)
					source_error(interp->errors, stmt->source_id, "All but the last expression in a block must be of type `Void` or `Definition`.\n");
			}
		}

		close_open_type(interp->types, scope_type_id, offset_in_scope, scope_align, next_multiple(offset_in_scope, static_cast<u64>(scope_align)));

		if (last_stmt == nullptr)
		{
			node->type = completed_type_id(simple_type(interp->types, TypeTag::Void, {}));
		}
		else
		{
			node->type = last_stmt->type;

			ensure_load(last_stmt);
		}

		node->type_kind = static_cast<u8>(TypeKind::Value);

		if (dependent)
			node->flags |= AstFlag::Any_HasDependentValue;

		pop_typechecker_context(interp);

		return dependent;
	}

	case AstTag::Func:
	{
		bool dependent = false;

		FuncInfo info = get_func_info(node);

		if (is_some(info.ensures) || is_some(info.expects))
			TODO("Implement typechecking of function-level ensures and expects.");

		AstDirectChildIterator params = direct_children_of(info.parameters);

		const TypeId unsized_signature_type_id = create_open_type(interp->types, curr_typechecker_context(interp), info.parameters->source_id, false);

		push_typechecker_context(interp, unsized_signature_type_id);

		u16 param_count = 0;

		while (has_next(&params))
		{
			AstNode* const param = next(&params);

			ASSERT_OR_IGNORE(param->tag == AstTag::Definition);

			if (has_flag(param, AstFlag::Definition_IsGlobal))
				source_error(interp->errors, param->source_id, "Function parameter cannot be `global`.\n");

			if (has_flag(param, AstFlag::Definition_IsPub))
				source_error(interp->errors, param->source_id, "Function parameter cannot be `pub`.\n");

			if (has_flag(param, AstFlag::Definition_IsUse))
				source_error(interp->errors, param->source_id, "Function parameter cannot be `use`.\n");

			const AstDefinitionData* const attachment = attachment_of<AstDefinitionData>(param);

			const DefinitionInfo param_info = get_definition_info(param);

			MemberInit init{};
			init.name = attachment->identifier_id;
			init.source = param->source_id;
			init.type.pending = is_some(param_info.type) ? id_from_ast_node(interp->asts, get_ptr(param_info.type)) : AstNodeId::INVALID;
			init.value.pending = is_some(param_info.value) ? id_from_ast_node(interp->asts, get_ptr(param_info.value)) : AstNodeId::INVALID;
			init.completion_context = curr_typechecker_context(interp);
			init.completion_arec = selected_arec_index(interp);
			init.is_global = false;
			init.is_pub = false;
			init.is_use = false;
			init.is_mut = has_flag(param, AstFlag::Definition_IsMut);
			init.has_pending_type = true;
			init.has_pending_value = is_some(param_info.value);
			init.offset = 0;

			add_open_type_member(interp->types, unsized_signature_type_id, init);

			if (param_count == 63)
				source_error(interp->errors, param->source_id, "Exceeded maximum of 63 parameters in function definition.\n");

			param_count += 1;
		}

		close_open_type(interp->types, unsized_signature_type_id, 0, 1, 0);

		pop_typechecker_context(interp);

		const TypeId signature_type_id = create_open_type(interp->types, curr_typechecker_context(interp), info.parameters->source_id, false);

		push_typechecker_context(interp, signature_type_id);

		u64 signature_offset = 0;

		u32 signature_align = 1;

		params = direct_children_of(info.parameters);

		while (has_next(&params))
		{
			AstNode* const param = next(&params);

			dependent |= typecheck_expr(interp, param);

			const TypeMetrics metrics = dependent_member_metrics(interp, param->type);

			signature_offset = next_multiple(signature_offset, static_cast<u64>(metrics.align));

			const AstDefinitionData* const attachment = attachment_of<AstDefinitionData>(param);

			const DefinitionInfo param_info = get_definition_info(param);

			MemberInit init{};
			init.name = attachment->identifier_id;
			init.source = param->source_id;
			init.type.complete = param->type;
			init.value.pending = is_some(param_info.value) ? id_from_ast_node(interp->asts, get_ptr(param_info.value)) : AstNodeId::INVALID;
			init.completion_context = curr_typechecker_context(interp);
			init.completion_arec = selected_arec_index(interp);
			init.is_global = false;
			init.is_pub = false;
			init.is_use = false;
			init.is_mut = has_flag(param, AstFlag::Definition_IsMut);
			init.has_pending_type = false;
			init.has_pending_value = is_some(param_info.value);
			init.offset = signature_offset;

			signature_offset += metrics.size;

			add_open_type_member(interp->types, signature_type_id, init);
		}

		close_open_type(interp->types, signature_type_id, signature_offset, signature_align, next_multiple(signature_offset, static_cast<u64>(signature_align)));

		DependentTypeId return_type_id;

		if (is_some(info.return_type))
		{
			AstNode* const return_type = get_ptr(info.return_type);

			const bool return_type_is_dependent = typecheck_expr(interp, return_type);

			if (return_type_is_dependent)
			{
				return_type_id = dependent_type_id(id_from_ast_node(interp->asts, return_type), DependentTypePosition::Type);
			}
			else if (type_tag_from_id(interp->types, completed(return_type->type)) != TypeTag::Type)
			{
				source_error(interp->errors, return_type->source_id, "Return type declaration must be of type `Type`.\n");
			}
			else
			{
				const TypeKind kind = static_cast<TypeKind>(return_type->type_kind);

				if (kind == TypeKind::Value)
				{
					evaluate_expr(interp, return_type, completed(return_type->type), range::from_object_bytes_mut(&return_type_id));
				}
				else
				{
					ASSERT_OR_IGNORE(kind == TypeKind::ImmutLocation || kind == TypeKind::MutLocation);

					MutRange<byte> return_type_id_location{};

					evaluate_expr(interp, return_type, completed(return_type->type), range::from_object_bytes_mut(&return_type_id_location));

					return_type_id = *bytes_as<DependentTypeId>(return_type_id_location);
				}
			}
		}
		else
		{
			TODO("Implement return type deduction.");
		}

		pop_typechecker_context(interp);

		if (is_some(info.body))
		{
			AstNode* const body = get_ptr(info.body);

			push_typechecker_context(interp, signature_type_id);

			// The body being dependent does not make the function itself
			// dependent, so the return value of typechecking the body is
			// simply discarded instead of being incorporated into
			// `is_dependent`.
			(void) typecheck_expr(interp, body);

			pop_typechecker_context(interp);

			if (!check_and_set_impconv(interp, body, return_type_id, TypeKind::Value))
				source_error(interp->errors, body->source_id, "Cannot convert type of function body to declared return type.\n");

			FuncType func_type{};
			func_type.is_proc = has_flag(node, AstFlag::Func_IsProc);
			func_type.param_count = param_count;
			func_type.return_type_id = return_type_id;
			func_type.signature_type_id = signature_type_id;

			node->type = completed_type_id(simple_type(interp->types, TypeTag::Func, range::from_object_bytes(&func_type)));
		}
		else
		{
			node->type = completed_type_id(simple_type(interp->types, TypeTag::Type, {}));
		}

		node->type_kind = static_cast<u8>(TypeKind::Value);

		if (dependent)
			node->flags |= AstFlag::Any_HasDependentValue;

		return dependent;
	}

	case AstTag::Identifer:
	{
		const IdentifierId identifier = attachment_of<AstIdentifierData>(node)->identifier_id;

		MemberInfo info;

		if (!lookup_identifier_info(interp, identifier, &info))
		{
			const Range<char8> name = identifier_name_from_id(interp->identifiers, identifier);
			
			source_error(interp->errors, node->source_id, "Could not find definition for identifier `%.*s`\n", static_cast<s32>(name.count()), name.begin());
		}

		const bool member_is_dependent = force_member_type(interp, &info);

		const bool dependent = member_is_dependent || !info.is_global;

		node->type = info.type.complete;
		node->type_kind = static_cast<u8>(info.is_mut ? TypeKind::MutLocation : TypeKind::ImmutLocation);

		if (dependent)
			node->flags |= AstFlag::Any_HasDependentValue;

		return dependent;
	}

	case AstTag::LitInteger:
	{
		node->type = completed_type_id(simple_type(interp->types, TypeTag::CompInteger, {}));
		node->type_kind = static_cast<u8>(TypeKind::Value);

		return false;
	}

	case AstTag::LitString:
	{
		node->type = completed_type_id(global_value_type(interp->globals, attachment_of<AstLitStringData>(node)->string_value_id));
		node->type_kind = static_cast<u8>(TypeKind::ImmutLocation);

		return false;
	}

	case AstTag::Call:
	{
		AstDirectChildIterator args = direct_children_of(node);

		const bool unused_has_callee = has_next(&args);

		ASSERT_OR_IGNORE(unused_has_callee);

		AstNode* const callee = next(&args);

		bool dependent = typecheck_expr(interp, callee);

		if (is_dependent(callee->type))
		{
			TODO("Implement typechecking for calls on callee of dependent type");
		}
		else
		{
			const TypeTag callee_type_tag = type_tag_from_id(interp->types, completed(callee->type));

			if (callee_type_tag != TypeTag::Func && callee_type_tag != TypeTag::Builtin)
				source_error(interp->errors, node->source_id, "Expected `Function` or `Builtin` as left-hand-side of call.\n");

			const FuncType* const callee_structure = static_cast<const FuncType*>(simple_type_structure_from_id(interp->types, completed(callee->type)));
	
			u16 arg_rank = 0;

			u64 seen_args = 0;

			bool allow_positional_args = true;

			while (has_next(&args))
			{
				AstNode* arg = next(&args);

				MemberInfo param_info;

				if (arg->tag == AstTag::UOpImpliedMember)
				{
					AstNode* const arg_name = first_child_of(arg);

					const IdentifierId arg_identifier = attachment_of<AstIdentifierData>(node)->identifier_id;

					if (!type_member_info_by_name(interp->types, callee_structure->signature_type_id, arg_identifier, &param_info))
					{
						const Range<char8> name = identifier_name_from_id(interp->identifiers, arg_identifier);

						source_error(interp->errors, arg->source_id, "Called function has no parameter `%.*s`\n", static_cast<s32>(name.count()), name.begin());
					}
					
					const u64 arg_bit = static_cast<u64>(1) << param_info.rank;

					if ((seen_args & arg_bit) != 0)
					{
						const Range<char8> name = identifier_name_from_id(interp->identifiers, arg_identifier);

						source_error(interp->errors, arg->source_id, "Function parameter `%.*s` at position %u specified more than once.\n", static_cast<s32>(name.count()), name.begin(), param_info.rank);
					}

					seen_args |= arg_bit;

					allow_positional_args = false;

					arg = next_sibling_of(arg_name);
				}
				else if (allow_positional_args)
				{
					if (arg_rank == callee_structure->param_count)
						source_error(interp->errors, arg->source_id, "Too many arguments in function call. Expected %u.\n", callee_structure->param_count);

					if (!type_member_info_by_rank(interp->types, callee_structure->signature_type_id, arg_rank, &param_info))
						ASSERT_UNREACHABLE;

					seen_args |= static_cast<u64>(1) << arg_rank;

					arg_rank += 1;
				}
				else
				{
					source_error(interp->errors, arg->source_id, "Positional function arguments must not follow named ones.\n");
				}

				const bool param_is_dependent = force_member_type(interp, &param_info);

				const bool arg_is_dependent = typecheck_expr(interp, arg);

				dependent |= param_is_dependent | arg_is_dependent;

				if (!check_and_set_impconv(interp, arg, param_info.type.complete, TypeKind::Value))
					source_error(interp->errors, arg->source_id, "Cannot convert function argument to declared parameter type.\n");
			}

			node->type = callee_structure->return_type_id;
		}

		node->type_kind = static_cast<u8>(TypeKind::Value);

		if (dependent)
			node->flags |= AstFlag::Any_HasDependentValue;

		return dependent;
	}

	case AstTag::OpMember:
	{
		AstNode* const lhs = first_child_of(node);

		const bool lhs_is_dependent = typecheck_expr(interp, lhs);

		AstNode* const rhs = next_sibling_of(lhs);

		if (rhs->tag != AstTag::Identifer)
			source_error(interp->errors, rhs->source_id, "Right-hand-side of `.` must be an identifier.\n");

		if (is_dependent(lhs->type))
		{
			node->type = dependent_type_id(id_from_ast_node(interp->asts, node), DependentTypePosition::Value);
			node->type_kind = static_cast<u8>(TypeKind::MutLocation);
			node->flags |= AstFlag::Any_HasDependentValue;

			return true;
		}

		bool dependent = lhs_is_dependent;

		const TypeId lhs_type_id = completed(lhs->type);

		const TypeTag lhs_type_tag = type_tag_from_id(interp->types, lhs_type_id);

		const IdentifierId member_identifier = attachment_of<AstIdentifierData>(rhs)->identifier_id;

		if (lhs_type_tag == TypeTag::Type)
		{
			if (lhs_is_dependent)
			{
				node->type = dependent_type_id(id_from_ast_node(interp->asts, node), DependentTypePosition::Value);
				node->type_kind = static_cast<u8>(TypeKind::MutLocation);
				node->flags |= AstFlag::Any_HasDependentValue;

				return true;
			}
			else
			{
				const TypeId type_type_id = simple_type(interp->types, TypeTag::Type, {});

				TypeId lhs_defined_type_id;

				if (static_cast<TypeKind>(lhs->type_kind) == TypeKind::Value)
				{
					evaluate_expr(interp, lhs, type_type_id, range::from_object_bytes_mut(&lhs_defined_type_id));
				}
				else
				{
					MutRange<byte> lhs_defined_type_id_location;

					evaluate_expr(interp, lhs, type_type_id, range::from_object_bytes_mut(&lhs_defined_type_id_location));

					lhs_defined_type_id = *bytes_as<TypeId>(lhs_defined_type_id_location);
				}

				MemberInfo info;

				if (!type_member_info_by_name(interp->types, lhs_defined_type_id, member_identifier, &info))
				{
					const Range<char8> name = identifier_name_from_id(interp->identifiers, member_identifier);

					source_error(interp->errors, node->source_id, "Left-hand-side of `.` has no member `%.*s`", static_cast<s32>(name.count()), name.begin());
				}

				if (!info.is_global)
				{
					const Range<char8> name = identifier_name_from_id(interp->identifiers, member_identifier);

					source_error(interp->errors, node->source_id, "Member `%.*s` cannot be accessed through type, as it is a non-global.\n", static_cast<s32>(name.count()), name.begin());
				}

				const bool member_is_dependent = force_member_type(interp, &info);

				dependent |= member_is_dependent;

				node->type = info.type.complete;
				node->type_kind = static_cast<u8>(info.is_mut ? TypeKind::MutLocation : TypeKind::ImmutLocation);

				if (dependent)
					node->flags |= AstFlag::Any_HasDependentValue;
			}
		}
		else if (lhs_type_tag == TypeTag::Composite)
		{
			MemberInfo info;

			if (!type_member_info_by_name(interp->types, completed(lhs->type), member_identifier, &info))
			{
				const Range<char8> name = identifier_name_from_id(interp->identifiers, member_identifier);

				source_error(interp->errors, node->source_id, "Left-hand-side of `.` has no member `%.*s`", static_cast<s32>(name.count()), name.begin());
			}

			const bool member_is_dependent = force_member_type(interp, &info);

			dependent |= member_is_dependent;

			node->type = info.type.complete;
			node->type_kind = static_cast<u8>(info.is_mut ? TypeKind::MutLocation : TypeKind::ImmutLocation);
			
			if (dependent)
				node->flags |= AstFlag::Any_HasDependentValue;
		}
		else
		{
			source_error(interp->errors, lhs->source_id, "Left-hand-side of `.` must be of `Type` or `Composite `type.\n");
		}

		return dependent;
	}

	case AstTag::OpCmpEQ:
	{
		bool dependent = false;

		AstNode* const lhs = first_child_of(node);

		dependent |= typecheck_expr(interp, lhs);

		AstNode* const rhs = next_sibling_of(lhs);

		dependent |= typecheck_expr(interp, rhs);

		if (!is_dependent(lhs->type) && !is_dependent(rhs->type))
		{
			const TypeId common_type_id = common_type(interp->types, completed(lhs->type), completed(rhs->type));

			if (common_type_id == TypeId::INVALID)
				source_error(interp->errors, node->source_id, "Incompatible operands for `==`\n");
		}

		// TODO: Check if there is an `impl CmpEq(common_type_id)`.

		node->type = completed_type_id(simple_type(interp->types, TypeTag::Boolean, {}));
		node->type_kind = static_cast<u8>(TypeKind::Value);
		
		if (dependent)
			node->flags |= AstFlag::Any_HasDependentValue;

		return dependent;
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
	const TypeId file_type_id = create_open_type(interp->types, interp->prelude_type_id, file_type_source_id, true);

	set_typechecker_context(interp, file_type_id);

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
		init.completion_context = curr_typechecker_context(interp);
		init.completion_arec = -1;
		init.is_global = true;
		init.is_pub = has_flag(node, AstFlag::Definition_IsPub);
		init.is_use = false;
		init.is_mut = has_flag(node, AstFlag::Definition_IsMut);
		init.has_pending_type = true;
		init.has_pending_value = is_some(info.value);
		init.offset = 0;

		add_open_type_member(interp->types, file_type_id, init);
	}

	close_open_type(interp->types, file_type_id, 0, 1, 0);

	IncompleteMemberIterator member_it = incomplete_members_of(interp->types, file_type_id);

	while (has_next(&member_it))
	{
		MemberInfo member = next(&member_it);

		(void) force_member_value(interp, &member);
	}

	unset_typechecker_context(interp);

	return file_type_id;
}



static bool force_member_type(Interpreter* interp, MemberInfo* member) noexcept
{
	if (!member->has_pending_type)
		return is_dependent(member->type.complete);

	ASSERT_OR_IGNORE(member->has_pending_value);

	set_typechecker_context(interp, member->completion_context);

	const s32 restore_arec = select_arec(interp, member->completion_arec);

	DependentTypeId defined_type;

	bool is_dependent = false;

	if (member->type.pending != AstNodeId::INVALID)
	{
		AstNode* const type = ast_node_from_id(interp->asts, member->type.pending);

		const bool type_is_dependent = typecheck_expr(interp, type);

		is_dependent |= type_is_dependent;

		if (!check_and_set_impconv(interp, type, completed_type_id(simple_type(interp->types, TypeTag::Type, {})), TypeKind::Value))
			source_error(interp->errors, type->source_id, "Explicit type annotation of definition must be of type `Type`.\n");

		if (type_is_dependent)
		{
			defined_type = dependent_type_id(id_from_ast_node(interp->asts, type), DependentTypePosition::Type);
		}
		else if (static_cast<TypeKind>(type->type_kind) == TypeKind::Value)
		{
			evaluate_expr(interp, type, completed(type->type), range::from_object_bytes_mut(&defined_type));
		}
		else
		{
			MutRange<byte> defined_type_location;

			evaluate_expr(interp, type, completed(type->type), range::from_object_bytes_mut(&defined_type_location));

			defined_type = *bytes_as<DependentTypeId>(defined_type_location);
		}

		if (member->value.pending != AstNodeId::INVALID)
		{
			AstNode* const value = ast_node_from_id(interp->asts, member->value.pending);

			is_dependent |= typecheck_expr(interp, value);

			if (!check_and_set_impconv(interp, value, defined_type, TypeKind::Value))
				source_error(interp->errors, value->source_id, "Definition value cannot be implicitly converted to type of explicit type annotation.\n");
		}
	}
	else
	{
		ASSERT_OR_IGNORE(member->value.pending != AstNodeId::INVALID);

		AstNode* const value = ast_node_from_id(interp->asts, member->value.pending);

		is_dependent |= typecheck_expr(interp, value);

		defined_type = value->type;
	}

	select_arec(interp, restore_arec);

	unset_typechecker_context(interp);

	set_incomplete_type_member_type_by_rank(interp->types, member->surrounding_type_id, member->rank, defined_type);

	member->has_pending_type = false;
	member->type.complete = defined_type;

	return is_dependent;
}

static bool force_member_value(Interpreter* interp, MemberInfo* member) noexcept
{
	if (!member->has_pending_value)
		return true;

	const bool type_is_dependent = force_member_type(interp, member);

	if (type_is_dependent)
		TODO("Handle (default) values of dependently typed members.");

	const TypeId member_type_id = completed(member->type.complete);

	AstNode* const value = ast_node_from_id(interp->asts, member->value.pending);

	const bool value_is_dependent = typecheck_expr(interp, value);

	ASSERT_OR_IGNORE(!value_is_dependent);

	if (!check_and_set_impconv(interp, value, member->type.complete, TypeKind::Value))
		source_error(interp->errors, member->source, "Definition value cannot be implicitly converted to type of explicit type annotation.\n");

	const TypeMetrics metrics = type_metrics_from_id(interp->types, member_type_id);

	const GlobalValueId value_id = alloc_global_value(interp->globals, member_type_id, metrics.size, metrics.align);

	MutRange<byte> global_value = global_value_get_mut(interp->globals, value_id);

	MutRange<byte> value_into = map_into_for_impconv(interp, value, member_type_id, global_value);



	set_typechecker_context(interp, member->completion_context);

	const s32 restore_arec = select_arec(interp, member->completion_arec);

	if (has_flag(value, AstFlag::Any_HasDependentValue))
		TODO("Handle (default) values of dependently valued members.");
	else
		evaluate_expr(interp, value, member_type_id, value_into);

	select_arec(interp, restore_arec);

	unset_typechecker_context(interp);



	if (value_into.begin() != global_value.begin())
		impconv(interp, global_value, member_type_id, value_into.immut(), completed(value->type), value);

	set_incomplete_type_member_value_by_rank(interp->types, member->surrounding_type_id, member->rank, value_id);

	member->has_pending_value = false;
	member->value.complete = value_id;

	return true;
}



static TypeId make_func_type_from_array(TypePool* types, TypeId return_type_id, u16 param_count, const BuiltinParamInfo* params) noexcept
{
	const TypeId signature_type_id = create_open_type(types, TypeId::INVALID, SourceId::INVALID, false);

	u64 offset = 0;

	u32 max_align = 1;

	for (u16 i = 0; i != param_count; ++i)
	{
		const TypeMetrics metrics = type_metrics_from_id(types, params[i].type);

		offset = next_multiple(offset, static_cast<u64>(metrics.align));

		MemberInit init{};
		init.name = params[i].name;
		init.type.complete = completed_type_id(params[i].type);
		init.value.complete = GlobalValueId::INVALID;
		init.source = SourceId::INVALID;
		init.is_global = false;
		init.is_pub = false;
		init.is_use = false;
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
	func_type.return_type_id = completed_type_id(return_type_id);
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
static T get_builtin_arg(Interpreter* interp, Arec* arec, IdentifierId identfier) noexcept
{
	MemberInfo info;

	if (!lookup_identifier_info_in_arec(interp, arec, identfier, &info))
		ASSERT_UNREACHABLE;

	ASSERT_OR_IGNORE(!info.is_global);

	ASSERT_OR_IGNORE(type_metrics_from_id(interp->types, completed(info.type.complete)).size == sizeof(T));

	return *reinterpret_cast<T*>(arec->attachment + info.offset);
}

static void builtin_integer(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, MutRange<byte> into) noexcept
{
	const u8 bits = get_builtin_arg<u8>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("bits")));

	const bool is_signed = get_builtin_arg<bool>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("is_signed")));

	NumericType integer_type{};
	integer_type.bits = bits;
	integer_type.is_signed = is_signed;

	const TypeId result = simple_type(interp->types, TypeTag::Integer, range::from_object_bytes(&integer_type));

	store(into, range::from_object_bytes(&result));
}

static void builtin_float(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, MutRange<byte> into) noexcept
{
	const u8 bits = get_builtin_arg<u8>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("bits")));

	NumericType float_type{};
	float_type.bits = bits;
	float_type.is_signed = true;

	const TypeId result = simple_type(interp->types, TypeTag::Float, range::from_object_bytes(&float_type));

	store(into, range::from_object_bytes(&result));
}

static void builtin_type(Interpreter* interp, [[maybe_unused]] Arec* arec, [[maybe_unused]] AstNode* call_node, MutRange<byte> into) noexcept
{
	const TypeId result = simple_type(interp->types, TypeTag::Type, {});

	store(into, range::from_object_bytes(&result));
}

static void builtin_typeof(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, MutRange<byte> into) noexcept
{
	const TypeId arg = get_builtin_arg<TypeId>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("arg")));

	store(into, range::from_object_bytes(&arg));
}

static void builtin_returntypeof(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, MutRange<byte> into) noexcept
{
	const TypeId arg = get_builtin_arg<TypeId>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("arg")));

	ASSERT_OR_IGNORE(type_tag_from_id(interp->types, arg) == TypeTag::Func || type_tag_from_id(interp->types, arg) == TypeTag::Builtin);

	const FuncType* const func_type = static_cast<const FuncType*>(simple_type_structure_from_id(interp->types, arg));

	store(into, range::from_object_bytes(&func_type->return_type_id));
}

static void builtin_sizeof(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, MutRange<byte> into) noexcept
{
	const TypeId arg = get_builtin_arg<TypeId>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("arg")));

	const TypeMetrics metrics = type_metrics_from_id(interp->types, arg);

	const CompIntegerValue size = comp_integer_from_u64(metrics.size);

	store(into, range::from_object_bytes(&size));
}

static void builtin_alignof(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, MutRange<byte> into) noexcept
{
	const TypeId arg = get_builtin_arg<TypeId>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("arg")));

	const TypeMetrics metrics = type_metrics_from_id(interp->types, arg);

	const CompIntegerValue align = comp_integer_from_u64(metrics.align);

	store(into, range::from_object_bytes(&align));
}

static void builtin_strideof(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, MutRange<byte> into) noexcept
{
	const TypeId arg = get_builtin_arg<TypeId>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("arg")));

	const TypeMetrics metrics = type_metrics_from_id(interp->types, arg);

	const CompIntegerValue align = comp_integer_from_u64(metrics.align);

	store(into, range::from_object_bytes(&align));
}

static void builtin_offsetof(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, MutRange<byte> into) noexcept
{
	(void) interp;

	(void) arec;

	(void) into;

	TODO("Implement.");
}

static void builtin_nameof(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, MutRange<byte> into) noexcept
{
	(void) interp;

	(void) arec;

	(void) into;

	TODO("Implement.");
}

static void builtin_import(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, MutRange<byte> into) noexcept
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

	const TypeId result = import_file(interp, absolute_path, is_std);

	store(into, range::from_object_bytes(&result));
}

static void builtin_create_type_builder(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, MutRange<byte> into) noexcept
{
	(void) interp;

	(void) arec;

	(void) into;

	TODO("Implement.");
}

static void builtin_add_type_member(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, MutRange<byte> into) noexcept
{
	(void) interp;

	(void) arec;

	(void) into;

	TODO("Implement.");
}

static void builtin_complete_type(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, MutRange<byte> into) noexcept
{
	(void) interp;

	(void) arec;

	(void) into;

	TODO("Implement.");
}

static void builtin_source_id([[maybe_unused]] Interpreter* interp, [[maybe_unused]] Arec* arec, AstNode* call_node, MutRange<byte> into) noexcept
{
	store(into, range::from_object_bytes(&call_node->source_id));
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
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("bits")), u8_type_id },
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("is_signed")), bool_type_id }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::Float)] = make_func_type(interp->types, type_type_id,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("bits")), u8_type_id }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::Type)] = make_func_type(interp->types, type_type_id);

	interp->builtin_type_ids[static_cast<u8>(Builtin::Typeof)] = make_func_type(interp->types, type_type_id,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("arg")), type_info_type_id }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::Returntypeof)] = make_func_type(interp->types, type_type_id,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("arg")), type_info_type_id }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::Sizeof)] = make_func_type(interp->types, comp_integer_type_id,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("arg")), type_info_type_id }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::Alignof)] = make_func_type(interp->types, comp_integer_type_id,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("arg")), type_info_type_id }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::Strideof)] = make_func_type(interp->types, comp_integer_type_id,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("arg")), type_info_type_id }
	);

	// TODO: Figure out what type this takes as its argument. A member? If so,
	//       how do you effectively get that?
	interp->builtin_type_ids[static_cast<u8>(Builtin::Offsetof)] = make_func_type(interp->types, comp_integer_type_id);

	interp->builtin_type_ids[static_cast<u8>(Builtin::Nameof)] = make_func_type(interp->types, slice_of_u8_type_id,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("arg")), type_info_type_id }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::Import)] = make_func_type(interp->types, type_type_id,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("path")), slice_of_u8_type_id },
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("is_std")), bool_type_id },
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("from")), u32_type_id }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::CreateTypeBuilder)] = make_func_type(interp->types, type_builder_type_id);

	interp->builtin_type_ids[static_cast<u8>(Builtin::AddTypeMember)] = make_func_type(interp->types, void_type_id,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("builder")), ptr_to_mut_type_builder_type_id },
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("definition")), definition_type_id },
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("offset")), s64_type_id }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::CompleteType)] = make_func_type(interp->types, type_type_id,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("arg")), type_builder_type_id }
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

		diag::print_type(interp->log_file, interp->identifiers, interp->types, completed_type_id(interp->prelude_type_id), &file_type_location);
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
	interp->selected_arec_index = -1;
	interp->arec_top = 0;
	interp->prelude_type_id = TypeId::INVALID;
	interp->context_top = -1;
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

		diag::print_type(interp->log_file, interp->identifiers, interp->types, completed_type_id(file_type_id), &file_type_location);
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
