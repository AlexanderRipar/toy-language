#include "pass_data.hpp"

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

// Utility for creating built-in functions types.
struct BuiltinParamInfo
{
	IdentifierId name;

	TypeId type;
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

struct DeferredTypeValue
{
	TypeRef type;

	u32 unused_;

	void* actual_value;
};




static bool force_member_type(Interpreter* interp, MemberInfo* member) noexcept; 

static bool force_member_value(Interpreter* interp, MemberInfo* member) noexcept; 



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

static void stack_free_temporary(Interpreter* interp, MutRange<byte> temporary)
{
	ASSERT_OR_IGNORE(reinterpret_cast<u64*>(temporary.begin()) > interp->arecs.begin() + interp->arec_top || (reinterpret_cast<u64*>(temporary.begin()) == interp->arecs.begin() && interp->arec_top == 0));

	interp->arecs.pop_to(static_cast<u32>(reinterpret_cast<u64*>(temporary.begin()) - interp->arecs.begin()));
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
		return global_value_get_mut(interp->globals, info.value.complete);
	else
		return MutRange{ arec->attachment + info.offset, type_metrics_from_id(interp->types, info.type.complete).size };
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
	else if (is_some(arec))
		return MutRange{ get_ptr(arec)->attachment + info.offset, type_metrics_from_id(interp->types, info.type.complete).size };
	else
		ASSERT_UNREACHABLE;
}



static MemberInit member_init_from_definition(Interpreter* interp, TypeId completion_context, s32 completion_arec, AstNode* definition, DefinitionInfo info, u64 offset) noexcept
{
	ASSERT_OR_IGNORE(definition->tag == AstTag::Definition);

	ASSERT_OR_IGNORE(type_id(definition->type) != TypeId::CHECKING);

	const TypeId defined_type_id = attachment_of<AstDefinitionData>(definition)->defined_type_id;

	const bool has_pending_type = defined_type_id == TypeId::INVALID;

	// TODO
	const bool has_pending_value = is_some(info.value);

	MemberInit member;
	member.name = attachment_of<AstDefinitionData>(definition)->identifier_id;
	member.source = definition->source_id;
	member.completion_context = completion_context;
	member.completion_arec = completion_arec;
	member.is_global = has_flag(definition, AstFlag::Definition_IsGlobal);
	member.is_pub = has_flag(definition, AstFlag::Definition_IsPub);
	member.is_use = has_flag(definition, AstFlag::Definition_IsUse);
	member.is_mut = has_flag(definition, AstFlag::Definition_IsMut);
	member.has_pending_type = has_pending_type;
	member.has_pending_value = has_pending_value;
	member.offset = offset;

	if (has_pending_type)
		member.type.pending = is_some(info.type) ? id_from_ast_node(interp->asts, get_ptr(info.type)) : AstNodeId::INVALID;
	else
		member.type.complete = defined_type_id;

	if (is_none(info.value))
		member.value.complete = GlobalValueId::INVALID;
	else if (has_pending_value)
		member.value.pending = is_some(info.value) ? id_from_ast_node(interp->asts, get_ptr(info.value)) : AstNodeId::INVALID;
	else
		TODO("Implement passing `ValueId` to `member_init_from_definition`.");

	return member;
}



template<typename T>
static T* bytes_as(MutRange<byte> bytes) noexcept
{
	ASSERT_OR_IGNORE(bytes.count() == sizeof(T));

	return reinterpret_cast<T*>(bytes.begin());
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

void store(MutRange<byte> dst, Range<byte> src) noexcept
{
	ASSERT_OR_IGNORE(dst.count() == src.count());

	memcpy(dst.begin(), src.begin(), dst.count());
}



static void impconv_load(Interpreter* interp, MutRange<byte> src, MutRange<byte> dst, TypeId dst_type_id) noexcept
{
	ASSERT_OR_IGNORE(src.count() == sizeof(MutRange<byte>));

	ASSERT_OR_IGNORE(dst.count() == type_metrics_from_id(interp->types, dst_type_id).size);

	MutRange<byte> loaded_src = *bytes_as<MutRange<byte>>(src);

	ASSERT_OR_IGNORE(loaded_src.count() == dst.count());

	memcpy(dst.begin(), loaded_src.begin(), dst.count());
}

static void impconv(Interpreter* interp, MutRange<byte> src, TypeId src_type_id, MutRange<byte> dst, TypeId dst_type_id, bool load, SourceId source_id) noexcept
{
	const TypeTag src_type_tag = type_tag_from_id(interp->types, src_type_id);

	const TypeTag dst_type_tag = type_tag_from_id(interp->types, dst_type_id);

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
			MutRange<byte> src_value = load ? *bytes_as<MutRange<byte>>(src) : src;

			ASSERT_OR_IGNORE(src_value.count() == type_metrics_from_id(interp->types, src_type_id).size);

			*bytes_as<Range<byte>>(dst) = src_value.as_byte_range();
		}
		else
		{
			ASSERT_OR_IGNORE(src_type_tag == TypeTag::Slice);

			ASSERT_OR_IGNORE(load);

			impconv_load(interp, src, dst, dst_type_id);
		}

		break;
	}

	case TypeTag::Integer:
	{
		if (src_type_tag == TypeTag::CompInteger)
		{
			const CompIntegerValue src_value = load_as<CompIntegerValue>(load, src);

			const NumericType dst_type = *static_cast<const NumericType*>(simple_type_structure_from_id(interp->types, dst_type_id));

			ASSERT_OR_IGNORE((dst_type.bits & 7) == 0 && dst_type.bits <= 64 && dst_type.bits != 0);

			if (dst_type.is_signed)
			{
				s64 signed_value;

				if (!s64_from_comp_integer(src_value, static_cast<u8>(dst_type.bits), &signed_value))
					source_error(interp->errors, source_id, "Compile-time integer constant exceeds range of signed %u bit integer.\n", dst_type.bits);

				ASSERT_OR_IGNORE(dst_type.bits / 8 == dst.count());

				store(dst, Range{ reinterpret_cast<byte*>(&signed_value), static_cast<u64>(dst_type.bits / 8) });
			}
			else
			{
				u64 unsigned_value;

				if (!u64_from_comp_integer(src_value, static_cast<u8>(dst_type.bits), &unsigned_value))
					source_error(interp->errors, source_id, "Compile-time integer constant exceeds range of unsigned %u bit integer.\n", dst_type.bits);

				ASSERT_OR_IGNORE(dst_type.bits / 8 == dst.count());

				store(dst, Range{ reinterpret_cast<byte*>(&unsigned_value), static_cast<u64>(dst_type.bits / 8) });
			}
		}
		else
		{
			ASSERT_OR_IGNORE(src_type_tag == TypeTag::Integer);

			ASSERT_OR_IGNORE(load);

			impconv_load(interp, src, dst, dst_type_id);
		}

		break;
	}

	case TypeTag::Float:
	{
		if (src_type_tag == TypeTag::CompFloat)
		{
			const CompFloatValue src_value = load_as<CompFloatValue>(load, src);

			const NumericType dst_type = *static_cast<const NumericType*>(simple_type_structure_from_id(interp->types, dst_type_id));

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

			impconv_load(interp, src, dst, dst_type_id);
		}

		break;
	}

	case TypeTag::TypeInfo:
	{
		store(dst, range::from_object_bytes(&src_type_id));

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
			impconv_load(interp, src, dst, dst_type_id);

		break;
	}

	case TypeTag::INVALID:
		ASSERT_UNREACHABLE;
	}

	stack_free_temporary(interp, src);
}

static MutRange<byte> map_into_for_impconv(Interpreter* interp, TypeRef type, MutRange<byte> unmapped_into) noexcept
{
	if (!requires_implicit_conversion(type))
		return unmapped_into;

	u64 required_bytes;

	u32 required_align;

	if (kind(type) == TypeKind::Value)
	{
		const TypeMetrics metrics = type_metrics_from_id(interp->types, type_id(type));

		required_bytes = metrics.size;

		required_align = metrics.align;
	}
	else
	{
		ASSERT_OR_IGNORE(kind(type) == TypeKind::MutLocation || kind(type) == TypeKind::ImmutLocation);

		required_bytes = 16;

		required_align = 16;
	}

	return stack_alloc_temporary(interp, required_bytes, required_align);
}

static bool check_and_set_impconv(Interpreter* interp, TypeRef* inout_type, TypeId expected_type_id, TypeKind expected_kind) noexcept
{
	ASSERT_OR_IGNORE(kind(*inout_type) != TypeKind::INVALID && expected_kind != TypeKind::INVALID);

	ASSERT_OR_IGNORE(type_id(*inout_type) != TypeId::INVALID && type_id(*inout_type) != TypeId::CHECKING && expected_type_id != TypeId::INVALID && expected_type_id != TypeId::CHECKING);

	const TypeRef type = *inout_type;

	if (static_cast<u8>(kind(type)) < static_cast<u32>(expected_kind))
		return false;

	bool requires_conversion = kind(type) != TypeKind::Value && expected_kind == TypeKind::Value;

	if (type_id(type) == TypeId::DEPENDENT || expected_type_id == TypeId::DEPENDENT)
		requires_conversion = true;
	else if (type_can_implicitly_convert_from_to(interp->types, type_id(type), expected_type_id))
		requires_conversion |= !is_same_type(interp->types, type_id(type), expected_type_id);
	else
		return false;

	const bool skip_evaluation = type_tag_from_id(interp->types, expected_type_id) == TypeTag::TypeInfo;

	*inout_type = type_ref(type_id(type), kind(type), has_dependent_value(type), requires_conversion, skip_evaluation);

	return true;
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



static void evaluate_expr(Interpreter* interp, AstNode* node, MutRange<byte> into) noexcept
{
	ASSERT_OR_IGNORE(type_id(node->type) != TypeId::INVALID && type_id(node->type) != TypeId::CHECKING);

	if (type_id(node->type) == TypeId::DEPENDENT)
	{
		ASSERT_UNREACHABLE;
	}

	if (skip_evaluation(node->type))
		return;

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
		const MemberInit init = member_init_from_definition(
			interp,
			curr_typechecker_context(interp),
			selected_arec_index(interp),
			node,
			get_definition_info(node),
			0
		);

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
				MutRange<byte> mapped_into = map_into_for_impconv(interp, stmt->type, into);

				evaluate_expr(interp, stmt, mapped_into);

				if (mapped_into.begin() != into.begin())
					impconv(interp, mapped_into, type_id(stmt->type), into, type_id(node->type), kind(stmt->type) != TypeKind::Value && kind(node->type) == TypeKind::Value, stmt->source_id);
			}
			else if (stmt->tag == AstTag::Definition)
			{
				DefinitionInfo info = get_definition_info(stmt);

				ASSERT_OR_IGNORE(is_some(info.value));

				AstNode* const value = get_ptr(info.value);

				MutRange<byte> location = lookup_identifier_location_in_arec(interp, block_arec, attachment_of<AstDefinitionData>(stmt)->identifier_id);

				const TypeId defined_type_id = attachment_of<AstDefinitionData>(stmt)->defined_type_id;

				const TypeRef defined_type_ref = type_ref(defined_type_id, TypeKind::Value, false, is_same_type(interp->types, defined_type_id, type_id(value->type)), type_tag_from_id(interp->types, defined_type_id) == TypeTag::TypeInfo);

				MutRange<byte> definition_into = map_into_for_impconv(interp, defined_type_ref, location);

				evaluate_expr(interp, value, definition_into);

				if (definition_into.begin() != location.begin())
					impconv(interp, definition_into, type_id(value->type), location, defined_type_id, kind(value->type) != TypeKind::Value, value->source_id);
			}
			else
			{
				ASSERT_OR_IGNORE(type_tag_from_id(interp->types, type_id(stmt->type)) == TypeTag::Void || type_tag_from_id(interp->types, type_id(stmt->type)) == TypeTag::Definition);

				MemberInit unused;

				evaluate_expr(interp, stmt, MutRange{ &unused, 1 }.as_mut_byte_range());
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
			callable.func_type_id_bits = static_cast<u32>(type_id(node->type));
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
		MutRange<byte> location = lookup_identifier_location(interp, attachment_of<AstIdentifierData>(node)->identifier_id);

		ASSERT_OR_IGNORE(type_metrics_from_id(interp->types, type_id(node->type)).size == location.count());

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

		Callable callee_value;

		if (kind(callee->type) == TypeKind::Value)
		{
			evaluate_expr(interp, callee, MutRange{ &callee_value, 1 }.as_mut_byte_range());
		}
		else
		{
			ASSERT_OR_IGNORE(kind(callee->type) == TypeKind::ImmutLocation || kind(callee->type) == TypeKind::MutLocation);

			MutRange<byte> callee_location{};

			evaluate_expr(interp, callee, MutRange{ &callee_location, 1 }.as_mut_byte_range());

			callee_value = *bytes_as<Callable>(callee_location);
		}

		const FuncType* const callee_structure = static_cast<const FuncType*>(simple_type_structure_from_id(interp->types, type_id(callee->type)));

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

			const u64 arg_size = type_metrics_from_id(interp->types, arg_info.type.complete).size;

			MutRange<byte> arg_slot = MutRange{ signature_arec->attachment + arg_info.offset, arg_size };

			MutRange<byte> arg_into = map_into_for_impconv(interp, arg->type, arg_slot);

			evaluate_expr(interp, arg, arg_into);

			if (arg_into.begin() != arg_slot.begin())
				impconv(interp, arg_into, type_id(arg->type), arg_slot, arg_info.type.complete, kind(arg->type) != TypeKind::Value, arg->source_id);
		}

		if (seen_args != (static_cast<u64>(1) << (callee_structure->param_count & 63)) - 1)
		{
			for (u16 i = 0; i != callee_structure->param_count; ++i)
			{
				if ((seen_args & (static_cast<u64>(1) << i)) != 0)
					continue;

				MemberInfo arg_info;
				
				if (!type_member_info_by_rank(interp->types, signature_type_id, i, &arg_info))
					ASSERT_UNREACHABLE;

				ASSERT_OR_IGNORE(!arg_info.has_pending_value && !arg_info.is_global);

				if (arg_info.value.complete == GlobalValueId::INVALID)
				{
					const Range<char8> name = identifier_name_from_id(interp->identifiers, arg_info.name);

					source_error(interp->errors, node->source_id, "Missing value for mandatory parameter `%.*s` at position `%u`\n", static_cast<s32>(name.count()), name.begin(), arg_info.rank + 1);
				}

				Range<byte> default_value = global_value_get(interp->globals, arg_info.value.complete);

				const u64 arg_size = type_metrics_from_id(interp->types, arg_info.type.complete).size;

				store(MutRange{ signature_arec->attachment + arg_info.offset, arg_size }, default_value);
			}
		}

		const s32 restore_arec = select_arec(interp, arec_index(interp, signature_arec));

		if (callee_value.is_builtin)
		{
			// TODO: This is missing e.g. conversion from `CompInteger` to
			//       other integer types.
			interp->builtin_values[callee_value.code.ordinal](interp, signature_arec, node, into);
		}
		else
		{
			AstNode* const body = ast_node_from_id(interp->asts, callee_value.code.ast);

			TypeRef mapped_type = body->type;

			// TODO: This is a really weird use of `check_and_set_impconv`. It
			//       is really intended to run during typechking, but this is a
			//       bit of a weird case. Maybe `AstTag::Call` nodes should
			//       have an attachment storing this? That would also make it
			//       safe with builtin calls, which are currently scuffed.
			if (!check_and_set_impconv(interp, &mapped_type, type_id(node->type), kind(node->type)))
				ASSERT_UNREACHABLE;

			MutRange<byte> mapped_into = map_into_for_impconv(interp, mapped_type, into);

			evaluate_expr(interp, body, mapped_into);

			if (mapped_into.begin() != into.begin())
				impconv(interp, mapped_into, type_id(body->type), into, type_id(node->type), kind(node->type) == TypeKind::Value && kind(body->type) != TypeKind::Value, node->source_id);
		}

		pop_arec(interp);

		select_arec(interp, restore_arec);

		return;
	}

	case AstTag::OpMember:
	{
		AstNode* const lhs = first_child_of(node);

		const TypeTag lhs_type_tag = type_tag_from_id(interp->types, type_id(lhs->type));

		AstNode* const rhs = next_sibling_of(lhs);

		ASSERT_OR_IGNORE(rhs->tag == AstTag::Identifer);

		const IdentifierId member_identifier = attachment_of<AstIdentifierData>(rhs)->identifier_id;

		if (lhs_type_tag == TypeTag::Type)
		{
			TypeId lhs_defined_type_id;

			if (kind(lhs->type) == TypeKind::Value)
			{
				evaluate_expr(interp, lhs, MutRange{ &lhs_defined_type_id, 1 }.as_mut_byte_range());
			}
			else
			{
				MutRange<byte> lhs_defined_type_id_location;

				evaluate_expr(interp, lhs, MutRange{ &lhs_defined_type_id_location, 1 }.as_mut_byte_range());

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

			const bool member_found = type_member_info_by_name(interp->types, type_id(lhs->type), member_identifier, &info);

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

				if (kind(lhs->type) == TypeKind::Value)
				{
					const TypeMetrics metrics = type_metrics_from_id(interp->types, type_id(lhs->type));

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

				evaluate_expr(interp, lhs, lhs_into);

				MutRange<byte> lhs_value;

				if (kind(lhs->type) == TypeKind::Value)
				{
					lhs_value = lhs_into;
				}
				else
				{
					lhs_value = *bytes_as<MutRange<byte>>(lhs_value);
				}

				store(into, lhs_value.subrange(info.offset, type_metrics_from_id(interp->types, info.type.complete).size));

				stack_free_temporary(interp, lhs_into);
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



// Sets the `type` field of `node` and all its children.
// Returns `true` if `node` or any of its children has a dependent type or
// value, `false` otherwise.
static bool typecheck_expr(Interpreter* interp, AstNode* node) noexcept
{
	const TypeId prev_type_id = type_id(node->type);

	if (prev_type_id == TypeId::CHECKING)
		source_error(interp->errors, node->source_id, "Cyclic type dependency detected during typechecking.\n");
	else if (prev_type_id != TypeId::INVALID)
		return has_dependent_value(node->type) || type_id(node->type) == TypeId::DEPENDENT;

	node->type = type_ref(TypeId::CHECKING, TypeKind::INVALID, false, false, false);

	switch (node->tag)
	{
	case AstTag::Builtin:
	{
		node->type = type_ref(interp->builtin_type_ids[static_cast<u8>(node->flags)], TypeKind::Value, false, false, false);

		return false;
	}

	case AstTag::Definition:
	{
		DefinitionInfo info = get_definition_info(node);

		TypeId annotated_type_id = TypeId::INVALID;

		bool is_dependent = false;

		if (is_some(info.type))
		{
			AstNode* const type = get_ptr(info.type);

			const bool type_is_dependent = typecheck_expr(interp, type);

			if (!check_and_set_impconv(interp, &type->type, simple_type(interp->types, TypeTag::Type, {}), TypeKind::Value))
				source_error(interp->errors, type->source_id, "Type annotation must be of type `Type`.\n");

			if (type_is_dependent)
			{
				annotated_type_id = TypeId::DEPENDENT;
			}
			else if (kind(type->type) == TypeKind::Value)
			{
				evaluate_expr(interp, type, MutRange{ &annotated_type_id, 1 }.as_mut_byte_range());
			}
			else
			{
				MutRange<byte> annotated_type_id_location{};

				evaluate_expr(interp, type, MutRange{ &annotated_type_id_location, 1 }.as_mut_byte_range());

				annotated_type_id = *bytes_as<TypeId>(annotated_type_id_location);
			}

			attachment_of<AstDefinitionData>(node)->defined_type_id = annotated_type_id;

			is_dependent |= type_is_dependent;
		}

		if (is_some(info.value))
		{
			AstNode* const value = get_ptr(info.value);

			const bool value_is_dependent = typecheck_expr(interp, value);

			if (is_none(info.type))
				attachment_of<AstDefinitionData>(node)->defined_type_id = type_id(value->type);

			else if (!check_and_set_impconv(interp, &value->type, annotated_type_id, TypeKind::Value))
				source_error(interp->errors, value->source_id, "Cannot convert definition value to explicitly annotated type.\n");

			is_dependent |= value_is_dependent;
		}

		node->type = type_ref(simple_type(interp->types, TypeTag::Definition, {}), TypeKind::Value, is_dependent, false, false);

		return is_dependent;
	}

	case AstTag::Block:
	{
		bool is_dependent = false;

		AstNode* last_stmt = nullptr;

		const TypeId scope_type_id = create_open_type(interp->types, curr_typechecker_context(interp), node->source_id, false);

		attachment_of<AstBlockData>(node)->scope_type_id = scope_type_id;

		u64 scope_offset = 0;

		// Initial alignment is 8, since scopes are allocated in
		// `Interpreter.arecs`, where they are always aligned to 8 bytes.
		// This does not currently have any effect, but if there are future
		// optimizations relying on alignment, this is better than
		// underspecifying the minimal scope alignment as 1.
		u32 scope_align = 8;

		u16 scope_rank = 0;

		push_typechecker_context(interp, scope_type_id);

		AstDirectChildIterator stmts = direct_children_of(node);

		while (has_next(&stmts))
		{
			AstNode* const stmt = next(&stmts);

			is_dependent |= typecheck_expr(interp, stmt);

			if (stmt->tag == AstTag::Definition)
			{
				const TypeId defined_type_id = attachment_of<AstDefinitionData>(stmt)->defined_type_id;

				const TypeMetrics metrics = type_metrics_from_id(interp->types, defined_type_id);

				scope_offset = next_multiple(scope_offset, static_cast<u64>(metrics.align));

				add_open_type_member(interp->types, scope_type_id, member_init_from_definition(interp, curr_typechecker_context(interp), -1, stmt, get_definition_info(stmt), scope_offset));

				set_incomplete_type_member_type_by_rank(interp->types, scope_type_id, scope_rank, defined_type_id);

				scope_offset += metrics.size;

				if (metrics.align > scope_align)
					scope_align = metrics.align;

				if (scope_rank == UINT16_MAX)
					source_error(interp->errors, stmt->source_id, "Exceeded maximum of %u definitions in a single block.\n", UINT16_MAX);

				scope_rank += 1;
			}

			if (has_next_sibling(stmt))
			{
				const TypeTag stmt_result_tag = type_tag_from_id(interp->types, type_id(stmt->type));

				if (stmt_result_tag != TypeTag::Void && stmt_result_tag != TypeTag::Definition)
					source_error(interp->errors, stmt->source_id, "Non-trailing expression in block must be of type `Void` or `Definition`.\n");
			}

			last_stmt = stmt;
		}

		close_open_type(interp->types, scope_type_id, scope_offset, scope_align, next_multiple(scope_offset, static_cast<u64>(scope_align)));

		if (last_stmt == nullptr)
		{
			node->type = type_ref(simple_type(interp->types, TypeTag::Void, {}), TypeKind::Value, false, false, false);
		}
		else
		{
			const TypeRef last_stmt_type = last_stmt->type;

			node->type = type_ref(type_id(last_stmt_type), TypeKind::Value, has_dependent_value(last_stmt_type), false, false);

			// This will always succeed, since we are casting to the same type,
			// only potentially weakening it from a location to a value.
			if (!check_and_set_impconv(interp, &last_stmt->type, type_id(last_stmt->type), TypeKind::Value))
				ASSERT_UNREACHABLE;
		}

		pop_typechecker_context(interp);

		return is_dependent;
	}

	case AstTag::Func:
	{
		bool is_dependent = false;

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

			MemberInit init = member_init_from_definition(interp, curr_typechecker_context(interp), selected_arec_index(interp), param, get_definition_info(param), 0);

			add_open_type_member(interp->types, unsized_signature_type_id, init);

			if (param_count == 64)
				source_error(interp->errors, param->source_id, "Exceeded maximum of 64 parameters in function definition.\n");

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

			is_dependent |= typecheck_expr(interp, param);

			const TypeMetrics metrics = type_metrics_from_id(interp->types, type_id(param->type));

			signature_offset = next_multiple(signature_offset, static_cast<u64>(metrics.align));

			MemberInit init = member_init_from_definition(interp, curr_typechecker_context(interp), selected_arec_index(interp), param, get_definition_info(param), signature_offset);

			signature_offset += metrics.size;

			add_open_type_member(interp->types, signature_type_id, init);
		}

		close_open_type(interp->types, signature_type_id, signature_offset, signature_align, next_multiple(signature_offset, static_cast<u64>(signature_align)));

		pop_typechecker_context(interp);

		TypeId return_type_id;

		if (is_some(info.return_type))
		{
			AstNode* const return_type = get_ptr(info.return_type);

			const bool return_type_is_dependent = typecheck_expr(interp, return_type);

			if (return_type_is_dependent)
			{
				return_type_id = TypeId::DEPENDENT;
			}
			else if (type_tag_from_id(interp->types, type_id(return_type->type)) != TypeTag::Type)
			{
				source_error(interp->errors, return_type->source_id, "Return type declaration must be of type `Type`.\n");
			}
			else if (kind(return_type->type) == TypeKind::Value)
			{
				evaluate_expr(interp, return_type, MutRange{ &return_type_id, 1 }.as_mut_byte_range());
			}
			else
			{
				MutRange<byte> return_type_id_location{};

				evaluate_expr(interp, return_type, MutRange{ &return_type_id_location, 1}.as_mut_byte_range());

				return_type_id = *bytes_as<TypeId>(return_type_id_location);
			}
		}
		else
		{
			TODO("Implement return type deduction.");
		}

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

			if (!check_and_set_impconv(interp, &body->type, return_type_id, TypeKind::Value))
				source_error(interp->errors, body->source_id, "Cannot convert type of function body to declared return type.\n");

			FuncType func_type{};
			func_type.is_proc = has_flag(node, AstFlag::Func_IsProc);
			func_type.param_count = param_count;
			func_type.return_type_id = return_type_id;
			func_type.signature_type_id = signature_type_id;

			node->type = type_ref(simple_type(interp->types, TypeTag::Func, range::from_object_bytes(&func_type)), TypeKind::Value, is_dependent, false, false);
		}
		else
		{
			node->type = type_ref(simple_type(interp->types, TypeTag::Type, {}), TypeKind::Value, is_dependent, false, false);
		}

		return is_dependent;
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

		const bool is_dependent = member_is_dependent || !info.is_global;

		node->type = type_ref(info.type.complete, info.is_mut ? TypeKind::MutLocation : TypeKind::ImmutLocation, is_dependent, false, false);

		return is_dependent;
	}

	case AstTag::LitInteger:
	{
		node->type = type_ref(simple_type(interp->types, TypeTag::CompInteger, {}), TypeKind::Value, false, false, false);

		return false;
	}

	case AstTag::LitString:
	{
		node->type = type_ref(global_value_type(interp->globals, attachment_of<AstLitStringData>(node)->string_value_id), TypeKind::ImmutLocation, false, false, false);

		return false;
	}

	case AstTag::Call:
	{
		AstDirectChildIterator args = direct_children_of(node);

		const bool unused_has_callee = has_next(&args);

		ASSERT_OR_IGNORE(unused_has_callee);

		AstNode* const callee = next(&args);

		bool is_dependent = typecheck_expr(interp, callee);

		if (is_dependent)
		{
			TODO("Implement typechecking for calls on dependent callee.");
		}
		else
		{
			const TypeTag callee_type_tag = type_tag_from_id(interp->types, type_id(callee->type));

			if (callee_type_tag != TypeTag::Func && callee_type_tag != TypeTag::Builtin)
				source_error(interp->errors, node->source_id, "Expected `Function` or `Builtin` as left-hand-side of call.\n");

			const FuncType* const callee_structure = static_cast<const FuncType*>(simple_type_structure_from_id(interp->types, type_id(callee->type)));

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

				is_dependent |= param_is_dependent | arg_is_dependent;

				if (!check_and_set_impconv(interp, &arg->type, param_info.type.complete, TypeKind::Value))
					source_error(interp->errors, arg->source_id, "Cannot convert function argument to declared parameter type.\n");
			}

			node->type = type_ref(callee_structure->return_type_id, TypeKind::Value, is_dependent, false, false);
		}

		return is_dependent;
	}

	case AstTag::OpMember:
	{
		AstNode* const lhs = first_child_of(node);

		const bool lhs_is_dependent = typecheck_expr(interp, lhs);

		bool is_dependent = lhs_is_dependent;

		const TypeTag lhs_type_tag = type_tag_from_id(interp->types, type_id(lhs->type));

		AstNode* const rhs = next_sibling_of(lhs);

		if (rhs->tag != AstTag::Identifer)
			source_error(interp->errors, rhs->source_id, "Right-hand-side of `.` must be an identifier.\n");

		const IdentifierId member_identifier = attachment_of<AstIdentifierData>(rhs)->identifier_id;

		if (lhs_type_tag == TypeTag::Type)
		{
			if (lhs_is_dependent)
			{
				node->type = type_ref(TypeId::DEPENDENT, TypeKind::MutLocation, true, false, false);
			}
			else
			{
				TypeId lhs_defined_type_id;

				if (kind(lhs->type) == TypeKind::Value)
				{
					evaluate_expr(interp, lhs, MutRange{ &lhs_defined_type_id, 1 }.as_mut_byte_range());
				}
				else
				{
					MutRange<byte> lhs_defined_type_id_location;

					evaluate_expr(interp, lhs, MutRange{ &lhs_defined_type_id_location, 1 }.as_mut_byte_range());

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

				is_dependent |= member_is_dependent;

				node->type = type_ref(info.type.complete, info.is_mut ? TypeKind::MutLocation : TypeKind::ImmutLocation, member_is_dependent, false, false);
			}
		}
		else if (lhs_type_tag == TypeTag::Composite)
		{
			MemberInfo info;

			if (!type_member_info_by_name(interp->types, type_id(lhs->type), member_identifier, &info))
			{
				const Range<char8> name = identifier_name_from_id(interp->identifiers, member_identifier);

				source_error(interp->errors, node->source_id, "Left-hand-side of `.` has no member `%.*s`", static_cast<s32>(name.count()), name.begin());
			}

			const bool member_is_dependent = force_member_type(interp, &info);

			is_dependent |= member_is_dependent;

			node->type = type_ref(info.type.complete, info.is_mut ? TypeKind::MutLocation : TypeKind::ImmutLocation, member_is_dependent, false, false);
		}
		else
		{
			source_error(interp->errors, lhs->source_id, "Left-hand-side of `.` must be of `Type` or `Composite `type.\n");
		}

		return is_dependent;
	}

	case AstTag::OpCmpEQ:
	{
		bool is_dependent = false;

		AstNode* const lhs = first_child_of(node);

		is_dependent |= typecheck_expr(interp, lhs);

		AstNode* const rhs = next_sibling_of(lhs);

		is_dependent |= typecheck_expr(interp, rhs);

		const TypeId common_type_id = common_type(interp->types, type_id(lhs->type), type_id(rhs->type));

		if (common_type_id == TypeId::INVALID)
			source_error(interp->errors, node->source_id, "Incompatible operands for `==`\n");

		// TODO: Check if there is an `impl CmpEq(common_type_id)`.

		node->type = type_ref(simple_type(interp->types, TypeTag::Boolean, {}), TypeKind::Value, is_dependent, false, false);

		return is_dependent;
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

		MemberInit member = member_init_from_definition(interp, file_type_id, -1, node, get_definition_info(node), 0);

		if (member.is_global)
			source_warning(interp->errors, node->source_id, "Redundant 'global' modifier. Top-level definitions are implicitly global.\n");
		else
			member.is_global = true;

		add_open_type_member(interp->types, file_type_id, member);
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
		return member->type.complete == TypeId::DEPENDENT;

	ASSERT_OR_IGNORE(member->has_pending_value);

	set_typechecker_context(interp, member->completion_context);

	const s32 restore_arec = select_arec(interp, member->completion_arec);

	TypeId defined_type_id;

	bool is_dependent = false;

	if (member->type.pending != AstNodeId::INVALID)
	{
		AstNode* const type = ast_node_from_id(interp->asts, member->type.pending);

		const bool type_is_dependent = typecheck_expr(interp, type);

		is_dependent |= type_is_dependent;

		if (!check_and_set_impconv(interp, &type->type, simple_type(interp->types, TypeTag::Type, {}), TypeKind::Value))
			source_error(interp->errors, type->source_id, "Explicit type annotation of definition must be of type `Type`.\n");

		if (type_is_dependent)
		{
			defined_type_id = TypeId::DEPENDENT;
		}
		else if (kind(type->type) == TypeKind::Value)
		{
			evaluate_expr(interp, type, MutRange{ &defined_type_id, 1 }.as_mut_byte_range());
		}
		else
		{
			MutRange<byte> defined_type_location;

			evaluate_expr(interp, type, MutRange{ &defined_type_location, 1 }.as_mut_byte_range());

			defined_type_id = *bytes_as<TypeId>(defined_type_location);
		}

		if (member->value.pending != AstNodeId::INVALID)
		{
			AstNode* const value = ast_node_from_id(interp->asts, member->value.pending);

			is_dependent |= typecheck_expr(interp, value);

			if (!check_and_set_impconv(interp, &value->type, defined_type_id, TypeKind::Value))
				source_error(interp->errors, value->source_id, "Definition value cannot be implicitly converted to type of explicit type annotation.\n");
		}
	}
	else
	{
		ASSERT_OR_IGNORE(member->value.pending != AstNodeId::INVALID);

		AstNode* const value = ast_node_from_id(interp->asts, member->value.pending);

		is_dependent |= typecheck_expr(interp, value);

		defined_type_id = type_id(value->type);
	}

	select_arec(interp, restore_arec);

	unset_typechecker_context(interp);

	set_incomplete_type_member_type_by_rank(interp->types, member->surrounding_type_id, member->rank, defined_type_id);

	member->has_pending_type = false;
	member->type.complete = defined_type_id;

	return is_dependent;
}

static bool force_member_value(Interpreter* interp, MemberInfo* member) noexcept
{
	if (!member->has_pending_value)
		return true;

	const bool type_is_dependent = force_member_type(interp, member);

	ASSERT_OR_IGNORE(!type_is_dependent);

	AstNode* const value = ast_node_from_id(interp->asts, member->value.pending);

	const bool value_is_dependent = typecheck_expr(interp, value);

	ASSERT_OR_IGNORE(!value_is_dependent);

	if (!check_and_set_impconv(interp, &value->type, member->type.complete, TypeKind::Value))
		source_error(interp->errors, member->source, "Definition value cannot be implicitly converted to type of explicit type annotation.\n");

	const TypeMetrics metrics = type_metrics_from_id(interp->types, member->type.complete);

	const GlobalValueId value_id = alloc_global_value(interp->globals, member->type.complete, metrics.size, metrics.align);

	MutRange<byte> global_value = global_value_get_mut(interp->globals, value_id);

	MutRange<byte> value_into = map_into_for_impconv(interp, value->type, global_value);



	set_typechecker_context(interp, member->completion_context);

	const s32 restore_arec = select_arec(interp, member->completion_arec);

	if (has_dependent_value(value->type))
		TODO("Handle dependent default member values.");
	else
		evaluate_expr(interp, value, value_into);

	select_arec(interp, restore_arec);

	unset_typechecker_context(interp);



	if (value_into.begin() != global_value.begin())
		impconv(interp, value_into, type_id(value->type), global_value, member->type.complete, kind(value->type) != TypeKind::Value, value->source_id);

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
		init.type.complete = params[i].type;
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
	func_type.return_type_id = return_type_id;
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

	ASSERT_OR_IGNORE(type_metrics_from_id(interp->types, info.type.complete).size == sizeof(T));

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

	const AstBuilderToken std_definition = push_node(asts, import_call, SourceId::INVALID, AstFlag::EMPTY, AstDefinitionData{ id_from_identifier(identifiers, range::from_literal_string("std")) });

	const AstBuilderToken std_identifier = push_node(asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, AstFlag::EMPTY, AstIdentifierData{ id_from_identifier(identifiers, range::from_literal_string("std")) });

	push_node(asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, AstFlag::EMPTY, AstIdentifierData{ id_from_identifier(identifiers, range::from_literal_string("prelude")) });

	const AstBuilderToken prelude_member = push_node(asts, std_identifier, SourceId::INVALID, AstFlag::EMPTY, AstTag::OpMember);

	push_node(asts, prelude_member, SourceId::INVALID, AstFlag::Definition_IsUse, AstDefinitionData{ id_from_identifier(identifiers, range::from_literal_string("prelude"))} );

	push_node(asts, std_definition, SourceId::INVALID, AstFlag::EMPTY, AstTag::File);

	AstNode* const prelude_ast = complete_ast(asts);

	interp->prelude_type_id = type_from_file_ast(interp, prelude_ast, SourceId::INVALID);

	if (interp->log_file.m_rep != nullptr && interp->log_prelude)
	{
		const SourceId file_type_source = type_source_from_id(interp->types, interp->prelude_type_id);

		const SourceLocation file_type_location = source_location_from_source_id(interp->reader, file_type_source);

		diag::print_type(interp->log_file, interp->identifiers, interp->types, interp->prelude_type_id, &file_type_location);
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

		diag::print_type(interp->log_file, interp->identifiers, interp->types, file_type_id, &file_type_location);
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

TypeRef type_ref(TypeId id, TypeKind kind, bool is_dependent_value, bool requires_conversion, bool skip_evaluation) noexcept
{
	ASSERT_OR_IGNORE(static_cast<u32>(id) < (static_cast<u32>(1) << (32 - 5)));

	return static_cast<TypeRef>(
		static_cast<u32>(id) << 5
	  | static_cast<u32>(skip_evaluation) << 4
	  | static_cast<u32>(is_dependent_value) << 3
	  | static_cast<u32>(requires_conversion) << 2
	  | static_cast<u32>(kind)
	);
}

TypeId type_id(TypeRef type) noexcept
{
	return TypeId{ static_cast<u32>(type) >> 5 };
}

TypeKind kind(TypeRef type) noexcept
{
	return static_cast<TypeKind>(static_cast<u32>(type) & 3);
}

bool has_dependent_value(TypeRef type) noexcept
{
	return (static_cast<u32>(type) & 8) != 0;
}

bool requires_implicit_conversion(TypeRef type) noexcept
{
	return (static_cast<u32>(type) & 4) != 0;
}

bool skip_evaluation(TypeRef type) noexcept
{
	return (static_cast<u32>(type) & 16) != 0;
}










/*
static void implicit_convert(Interpreter* interp, void* value, TypeId source_type_id, TypeId target_type_id, SourceId source_id) noexcept
{
	switch (target_type_tag)
	{
	case TypeTag::Type:
	case TypeTag::Definition:
	case TypeTag::CompInteger:
	case TypeTag::CompFloat:
	case TypeTag::Boolean:
	case TypeTag::Ptr:
	case TypeTag::Func:
	case TypeTag::Builtin:
	case TypeTag::CompositeLiteral:
	case TypeTag::ArrayLiteral:
	case TypeTag::TypeBuilder:
	case TypeTag::Variadic:
	case TypeTag::Trait:
	case TypeTag::TailArray:
	{
		ASSERT_OR_IGNORE(target_type_tag == type_tag_from_id(interp->types, source_type_id));

		return stack_top;
	}

	case TypeTag::Void:
	{
		if (source_type_id == target_type_id)
			return stack_top;

		const TypeTag source_type_tag = type_tag_from_id(interp->types, source_type_id);

		if (source_type_tag == TypeTag::Void)
			return stack_top;

		ASSERT_OR_IGNORE(source_type_tag == TypeTag::Definition);

		trim_activation_record(interp, trim_to);

		return alloc_in_activation_record(interp, 0, 1);
	}

	case TypeTag::Integer:
	{
		const TypeTag source_type_tag = type_tag_from_id(interp->types, source_type_id);

		if (source_type_tag == TypeTag::CompInteger)
		{
			const CompIntegerValue v = *static_cast<CompIntegerValue*>(stack_top);

			memset(stack_top, 0, sizeof(v));

			const NumericType* const target_type = static_cast<const NumericType*>(simple_type_structure_from_id(interp->types, target_type_id));

			if (target_type->is_signed)
			{
				if (!s64_from_comp_integer(v, static_cast<u8>(target_type->bits), static_cast<s64*>(stack_top)))
					source_error(interp->errors, source_id, "`CompInteger` value does not fit into signed %u bit integer.\n", target_type->bits);
			}
			else
			{
				if (!u64_from_comp_integer(v, static_cast<u8>(target_type->bits), static_cast<u64*>(stack_top)))
					source_error(interp->errors, source_id, "`CompInteger` value does not fit into unsigned %u bit integer.\n", target_type->bits);
			}
		}
		#ifndef _NDEBUG
		else
		{
			ASSERT_OR_IGNORE(source_type_tag == TypeTag::Integer);

			const NumericType* const target_type = static_cast<const NumericType*>(simple_type_structure_from_id(interp->types, target_type_id));

			const NumericType* const source_type = static_cast<const NumericType*>(simple_type_structure_from_id(interp->types, source_type_id));

			ASSERT_OR_IGNORE(target_type->bits == source_type->bits && target_type->is_signed == source_type->is_signed);
		}
		#endif

		return stack_top;
	}

	case TypeTag::Float:
	{
		const TypeTag source_type_tag = type_tag_from_id(interp->types, source_type_id);

		if (source_type_tag == TypeTag::CompFloat)
		{
			const CompFloatValue v = *static_cast<CompFloatValue*>(stack_top);

			memset(stack_top, 0, sizeof(v));

			const NumericType* const target_type = static_cast<const NumericType*>(simple_type_structure_from_id(interp->types, target_type_id));

			if (target_type->bits == 32)
			{
				*static_cast<f32*>(stack_top) = f32_from_comp_float(v);
			}
			else
			{
				ASSERT_OR_IGNORE(target_type->bits == 64);

				*static_cast<f64*>(stack_top) = f64_from_comp_float(v);
			}
		}
		#ifndef _NDEBUG
		else
		{
			ASSERT_OR_IGNORE(source_type_tag == TypeTag::Float);

			const NumericType* const target_type = static_cast<const NumericType*>(simple_type_structure_from_id(interp->types, target_type_id));

			const NumericType* const source_type = static_cast<const NumericType*>(simple_type_structure_from_id(interp->types, source_type_id));

			ASSERT_OR_IGNORE(target_type->bits == source_type->bits);
		}
		#endif

		return stack_top;
	}

	case TypeTag::Slice:
	{
		const TypeTag source_type_tag = type_tag_from_id(interp->types, source_type_id);

		if (source_type_tag == TypeTag::Array)
		{
			const ArrayType* const source_type = static_cast<const ArrayType*>(simple_type_structure_from_id(interp->types, source_type_id));

			const ArrayValue v = *static_cast<ArrayValue*>(stack_top);

			if ((v.padding_qwords & 1) == 1)
				TODO("Implement implicit conversion of non-global");

			pop_temporary(interp);

			Range<byte>* const new_stack_value = static_cast<Range<byte>*>(push_temporary(interp, 16, 8));

			*new_stack_value = Range{ static_cast<byte*>(v.value_ptr), source_type->element_count };

			return new_stack_value;
		}
		#ifndef _NDEBUG
		else
		{
			ASSERT_OR_IGNORE(source_type_tag == TypeTag::Slice);

			const ReferenceType* const target_type = static_cast<const ReferenceType*>(simple_type_structure_from_id(interp->types, target_type_id));

			const ReferenceType* const source_type = static_cast<const ReferenceType*>(simple_type_structure_from_id(interp->types, source_type_id));

			ASSERT_OR_IGNORE(is_same_type(interp->types, target_type->referenced_type_id, source_type->referenced_type_id));

			ASSERT_OR_IGNORE(!target_type->is_mut || source_type->is_mut);

			ASSERT_OR_IGNORE(!target_type->is_multi || source_type->is_multi);

			ASSERT_OR_IGNORE(target_type->is_opt || !source_type->is_opt);
		}
		#endif
		
		return stack_top;
	}

	case TypeTag::Array:
	{
		const TypeTag source_type_tag = type_tag_from_id(interp->types, source_type_id);

		if (source_type_tag == TypeTag::ArrayLiteral)
		{
			TODO("Implement literal-to-array conversion");
		}
		#ifndef _NDEBUG
		else
		{
			ASSERT_OR_IGNORE(source_type_tag == TypeTag::Array);

			const ArrayType* const target_type = static_cast<const ArrayType*>(simple_type_structure_from_id(interp->types, target_type_id));

			const ArrayType* const source_type = static_cast<const ArrayType*>(simple_type_structure_from_id(interp->types, source_type_id));

			ASSERT_OR_IGNORE(is_same_type(interp->types, target_type->element_type, source_type->element_type));

			ASSERT_OR_IGNORE(target_type->element_count == source_type->element_count);
		}
		#endif

		return stack_top;
	}

	case TypeTag::Composite:
	{
		TODO("Implement literal-to-composite conversion");
	}

	case TypeTag::INVALID:
	case TypeTag::Divergent:
	case TypeTag::TypeInfo:
		ASSERT_UNREACHABLE;
	}

	ASSERT_UNREACHABLE;
}

static TypeRef typecheck_expr_impl(Interpreter* interp, AstNode* node, TypeId into_type_id) noexcept
{
	switch (node->tag)
	{
	case AstTag::CompositeInitializer:
	case AstTag::ArrayInitializer:
	case AstTag::Wildcard:
	case AstTag::Where:
	case AstTag::Expects:
	case AstTag::Ensures:
	case AstTag::Definition:
	case AstTag::ForEach:
	case AstTag::Switch:
	case AstTag::Trait:
	case AstTag::Impl:
	case AstTag::Catch:
	case AstTag::Return:
	case AstTag::Leave:
	case AstTag::Yield:
	case AstTag::UOpTry:
	case AstTag::UOpDefer:
	case AstTag::UOpImpliedMember:
		source_error(interp->errors, node->source_id, "Typechecking of AST node type %s is not yet implemented (%s:%u).\n", tag_name(node->tag), __FILE__, __LINE__);

	case AstTag::Builtin:
	{
		const Builtin builtin = static_cast<Builtin>(node->flags);

		if (builtin == Builtin::Offsetof)
			panic("Typechecking for builtin %s not yet supported.\n", tag_name(builtin));

		const u8 ordinal = static_cast<u8>(builtin);

		ASSERT_OR_IGNORE(ordinal < array_count(interp->builtin_type_ids) && interp->builtin_type_ids[ordinal] != TypeId::INVALID);

		const TypeId type_id = interp->builtin_type_ids[ordinal];

		return type_ref(type_id, false, !is_same_type(interp->types, type_id, into_type_id));
	}

	case AstTag::Block:
	{
		ASSERT_OR_IGNORE(attachment_of<AstBlockData>(node)->scope_type_id == TypeId::INVALID);

		const TypeId scope_type_id = create_open_type(interp->types, curr_typechecker_context(interp), node->source_id);

		attachment_of<AstBlockData>(node)->scope_type_id = scope_type_id;

		push_typechecker_context(interp, scope_type_id);

		AstDirectChildIterator it = direct_children_of(node);

		u64 offset = 0;

		u32 max_align = 1;

		TypeRef result_type_id = TypeRef::INVALID;

		while (has_next(&it))
		{
			AstNode* const child = next(&it);

			if (child->tag == AstTag::Definition)
			{
				DefinitionInfo info = get_definition_info(child);

				ASSERT_OR_IGNORE(is_some(info.type) || is_some(info.value));

				TypeRef defined_type_ref = TypeRef::INVALID;

				if (is_some(info.type))
				{
					AstNode* const type = get_ptr(info.type);

					const TypeRef type_type_ref = typecheck_expr(interp, type);

					const TypeTag type_type_tag = type_tag_from_id(interp->types, type_id(type_type_ref));

					if (type_type_tag != TypeTag::Type)
						source_error(interp->errors, type->source_id, "Explicit type annotation of definition must be of type `Type`.\n");

					TypeId defined_type_id;

					if (!evaluate_static_expr(interp, type, type_id(type_type_ref), &defined_type_id))
						defined_type_id = simple_type(interp->types, TypeTag::Dependent, {});

					defined_type_ref = type_ref(defined_type_id, has_flag(node, AstFlag::Definition_IsMut), is_same_type(interp->types, defined_type_id, into_type_id));
				}

				if (is_some(info.value))
				{
					AstNode* const value = get_ptr(info.value);

					const TypeId value_type_id = type_id(typecheck_expr(interp, value));

					if (defined_type_ref == TypeRef::INVALID)
					{
						defined_type_ref = type_ref(value_type_id, has_flag(node, AstFlag::Definition_IsMut), is_same_type(interp->types, value_type_id, into_type_id));
					}
					else if (!type_can_implicitly_convert_from_to(interp->types, value_type_id, type_id(defined_type_ref)))
					{
						source_error(interp->errors, node->source_id, "Definition value cannot be implicitly converted to type of explicit type annotation.\n");
					}
				}

				child->type = concrete_type_ref(defined_type_id, has_flag(child, AstFlag::Definition_IsMut));

				const TypeMetrics metrics = type_metrics_from_id(interp->types, defined_type_id);

				offset = next_multiple(offset, static_cast<u64>(metrics.align));

				MemberInit member = member_init_from_definition(interp, scope_type_id, child, info, offset);

				offset += metrics.size;

				if (metrics.align > max_align)
					max_align = metrics.align;

				add_open_type_member(interp->types, scope_type_id, member);

				if (is_some(info.type) && is_some(info.value))
				{
					AstNode* const value = get_ptr(info.value);

					const TypeId value_type_id = type_id(typecheck_expr(interp, value));

					if (!type_can_implicitly_convert_from_to(interp->types, value_type_id, defined_type_id))
						source_error(interp->errors, value->source_id, "Definition value cannot be implicitly converted to type of explicit type annotation.\n");
				}

				if (!has_next_sibling(child))
					result_type_id = concrete_type_ref(simple_type(interp->types, TypeTag::Definition, {}), false);
			}
			else
			{
				const TypeRef expr_type_id = typecheck_expr(interp, child);

				if (!has_next_sibling(node))
				{
					result_type_id = expr_type_id;
				}
				else
				{
					const TypeTag expr_type_tag = type_tag_from_id(interp->types, type_id(expr_type_id));

					if (expr_type_tag != TypeTag::Void && expr_type_tag != TypeTag::Definition)
						source_error(interp->errors, child->source_id, "Expression in non-terminal position in block must be a definition or of void type.\n");
				}
			}
		}

		pop_typechecker_context(interp);

		close_open_type(interp->types, scope_type_id, offset, max_align, next_multiple(offset, static_cast<u64>(max_align)));

		// Empty blocks are of type `Void`.
		if (type_id(result_type_id) == TypeId::INVALID)
			result_type_id = concrete_type_ref(simple_type(interp->types, TypeTag::Void, {}), false);

		return result_type_id;
	}

	case AstTag::If:
	{
		IfInfo info = get_if_info(node);

		const TypeId condition_type_id = type_id(typecheck_expr(interp, info.condition));

		const TypeTag condition_type_tag = type_tag_from_id(interp->types, condition_type_id);

		if (condition_type_tag != TypeTag::Boolean)
			source_error(interp->errors, info.condition->source_id, "Condition of `if` expression must be of boolean type.\n");

		if (is_some(info.where))
			typecheck_where(interp, get_ptr(info.where));

		const TypeRef consequent_type_id = typecheck_expr(interp, info.consequent);

		if (is_some(info.alternative))
		{
			AstNode* const alternative = get_ptr(info.alternative);

			const TypeRef alternative_type_id = typecheck_expr(interp, alternative);

			const TypeId common_type_id = common_type(interp->types, type_id(consequent_type_id), type_id(alternative_type_id));

			if (common_type_id == TypeId::INVALID)
				source_error(interp->errors, node->source_id, "Consequent and alternative of `if` have incompatible types.\n");

			return concrete_type_ref(common_type_id, is_assignable(consequent_type_id) && is_assignable(alternative_type_id));
		}
		else
		{
			const TypeTag consequent_type_tag = type_tag_from_id(interp->types, type_id(consequent_type_id));

			if (consequent_type_tag != TypeTag::Void)
				source_error(interp->errors, node->source_id, "Consequent of `if` must be of void type if no alternative is provided.\n");

			return consequent_type_id;
		}
	}

	case AstTag::For:
	{
		ForInfo info = get_for_info(node);

		const TypeId condition_type_id = type_id(typecheck_expr(interp, info.condition));

		const TypeTag condition_type_tag = type_tag_from_id(interp->types, condition_type_id);

		if (condition_type_tag != TypeTag::Boolean)
			source_error(interp->errors, info.condition->source_id, "Condition of `for` must be of boolean type.\n");

		if (is_some(info.step))
		{
			AstNode* const step = get_ptr(info.step);

			const TypeId step_type_id = type_id(typecheck_expr(interp, step));

			const TypeTag step_type_tag = type_tag_from_id(interp->types, step_type_id);

			if (step_type_tag != TypeTag::Void)
				source_error(interp->errors, step->source_id, "Step of `for` must be of void type.\n");
		}

		if (is_some(info.where))
		typecheck_where(interp, get_ptr(info.where));

		const TypeRef body_type_id = typecheck_expr(interp, info.body);

		if (is_some(info.finally))
		{
			AstNode* const finally = get_ptr(info.finally);

			const TypeRef finally_type_id = typecheck_expr(interp, finally);

			const TypeId common_type_id = common_type(interp->types, type_id(body_type_id), type_id(finally_type_id));

			if (common_type_id == TypeId::INVALID)
				source_error(interp->errors, node->source_id, "Body and finally of `for` have incompatible types.\n");

			return concrete_type_ref(common_type_id, is_assignable(body_type_id) && is_assignable(finally_type_id));
		}
		else
		{
			const TypeTag body_type_tag = type_tag_from_id(interp->types, type_id(body_type_id));

			if (body_type_tag != TypeTag::Void)
				source_error(interp->errors, node->source_id, "Consequent of `if` must be of void type if no alternative is provided.\n");

			return body_type_id;
		}
	}

	case AstTag::Func:
	{
		FuncInfo info = get_func_info(node);

		// TODO: This is a throwaway type only used as a scope for type
		//       checking, which is a bit silly. Maybe a version of
		//       `create_open_type` that deletes the type as soon as it is
		//       instructed to do so through an additional function could be
		//       nice. In this case the type should also never be put into
		//       `structural_types`, instead staying in `builders`.
		const TypeId pseudo_signature_type_id = create_open_type(interp->types, curr_typechecker_context(interp), node->source_id);

		AstDirectChildIterator parameters = direct_children_of(info.parameters);

		u16 param_count = 0;

		while (has_next(&parameters))
		{
			AstNode* const parameter = next(&parameters);

			if (param_count == 64)
				source_error(interp->errors, parameter->source_id, "Exceeded maximum of 64 function parameters.\n");

			param_count += 1;

			ASSERT_OR_IGNORE(parameter->tag == AstTag::Definition);

			MemberInit init = member_init_from_definition(interp, pseudo_signature_type_id, parameter, get_definition_info(parameter), 0);

			add_open_type_member(interp->types, pseudo_signature_type_id, init);
		}

		close_open_type(interp->types, pseudo_signature_type_id, 0, 1, 0);

		IncompleteMemberIterator incomplete_members = incomplete_members_of(interp->types, pseudo_signature_type_id);

		push_typechecker_context(interp, pseudo_signature_type_id);

		while (has_next(&incomplete_members))
		{
			MemberInfo member = next(&incomplete_members);

			force_member_type(interp, &member);
		}

		pop_typechecker_context(interp);

		const TypeId signature_type_id = create_open_type(interp->types, curr_typechecker_context(interp), node->source_id);

		MemberIterator members = members_of(interp->types, pseudo_signature_type_id);

		u64 offset = 0;

		u32 max_align = 1;

		while (has_next(&members))
		{
			MemberInfo member = next(&members);

			ASSERT_OR_IGNORE(!member.has_pending_type);

			const TypeMetrics member_metrics = type_metrics_from_id(interp->types, member.type.complete);

			offset = next_multiple(offset, static_cast<u64>(member_metrics.align));

			MemberInit init;
			init.name = member.name;
			init.source = member.source;
			init.type = member.type;
			init.value = member.value;
			init.completion_context = signature_type_id;
			init.is_global = member.is_global;
			init.is_pub = member.is_pub;
			init.is_use = member.is_use;
			init.is_mut = member.is_mut;
			init.has_pending_type = member.has_pending_type;
			init.has_pending_value = member.has_pending_value;
			init.offset = offset;

			offset += member_metrics.size;

			if (max_align < member_metrics.align)
				max_align = member_metrics.align;

			add_open_type_member(interp->types, signature_type_id, init);
		}

		close_open_type(interp->types, signature_type_id, offset, max_align, next_multiple(offset, static_cast<u64>(max_align)));

		if (is_none(info.return_type))
			source_error(interp->errors, node->source_id, "Function definitions without an explicit return type are not yet supported.\n");

		AstNode* const return_type = get_ptr(info.return_type);

		push_typechecker_context(interp, signature_type_id);

		const TypeId return_type_id = type_id(typecheck_expr(interp, return_type));

		if (type_tag_from_id(interp->types, return_type_id) != TypeTag::Type)
			source_error(interp->errors, node->source_id, "Return type expression of function must be of type `Type`.\n");

		void* defined_return_type_id_ptr;

		if (!evaluate_expr(interp, return_type, type_id(return_type->type), &defined_return_type_id_ptr))
			TODO("Implement.");

		const TypeId defined_return_type_id = *static_cast<TypeId*>(defined_return_type_id_ptr);

		pop_temporary(interp);

		FuncType func_type{};
		func_type.return_type_id = defined_return_type_id;
		func_type.param_count = param_count;
		func_type.is_proc = has_flag(node, AstFlag::Func_IsProc);
		func_type.signature_type_id = signature_type_id;

		const TypeId func_type_id = simple_type(interp->types, TypeTag::Func, range::from_object_bytes(&func_type));

		attachment_of<AstFuncData>(node)->func_type_id = func_type_id;

		if (is_some(info.body))
		{
			AstNode* const body = get_ptr(info.body);

			const TypeId body_type_id = type_id(typecheck_expr(interp, body));

			if (!type_can_implicitly_convert_from_to(interp->types, body_type_id, defined_return_type_id))
				source_error(interp->errors, body->source_id, "Cannot implicitly convert type of function body to declared return type.\n");

			pop_typechecker_context(interp);

			return concrete_type_ref(func_type_id, false);
		}
		else
		{
			pop_typechecker_context(interp);

			return concrete_type_ref(simple_type(interp->types, TypeTag::Type, {}), false);
		}
	}

	case AstTag::Identifer:
	{
		const IdentifierId identifier_id = attachment_of<AstIdentifierData>(node)->identifier_id;

		MemberInfo member = lookup_identifier_definition(interp, identifier_id, node->source_id);

		force_member_type(interp, &member);

		return concrete_type_ref(member.type.complete, member.is_mut);
	}

	case AstTag::LitInteger:
	{
		return concrete_type_ref(simple_type(interp->types, TypeTag::CompInteger, {}), false);
	}

	case AstTag::LitFloat:
	{
		return concrete_type_ref(simple_type(interp->types, TypeTag::CompFloat, {}), false);
	}

	case AstTag::LitChar:
	{
		return concrete_type_ref(simple_type(interp->types, TypeTag::CompInteger, {}), false);
	}

	case AstTag::LitString:
	{
		const GlobalValueId string_value_id = attachment_of<AstLitStringData>(node)->string_value_id;

		const GlobalValue string_value = global_value_from_id(interp->globals, string_value_id);

		ASSERT_OR_IGNORE(type_tag_from_id(interp->types, type_id(string_value.type)) == TypeTag::Array);

		return string_value.type;
	}

	case AstTag::Call:
	{
		// TODO: Variadics

		AstNode* const callee = first_child_of(node);

		const TypeId callee_type_id = type_id(typecheck_expr(interp, callee));

		const TypeTag callee_type_tag = type_tag_from_id(interp->types, callee_type_id);

		if (callee_type_tag != TypeTag::Func && callee_type_tag != TypeTag::Builtin)
			source_error(interp->errors, callee->source_id, "Left-hand-side of call operator must be of function or builtin type.\n");

		const FuncType* const func_type = static_cast<const FuncType*>(simple_type_structure_from_id(interp->types, callee_type_id));

		const TypeId signature_type_id = func_type->signature_type_id;

		bool expect_named = false;

		u64 seen_argument_mask = 0;

		u16 seen_argument_count = 0;

		AstNode* argument = callee;

		while (has_next_sibling(argument))
		{
			argument = next_sibling_of(argument);

			MemberInfo argument_member;

			TypeRef argument_type_id;

			if (argument->tag == AstTag::OpSet)
			{
				if (!expect_named)
				{
					seen_argument_mask = (static_cast<u64>(1) << seen_argument_count) - 1;

					expect_named = true;
				}

				AstNode* const argument_lhs = first_child_of(argument);

				AstNode* const argument_rhs = next_sibling_of(argument_lhs);

				// TODO: Enforce this in parser
				ASSERT_OR_IGNORE(argument_lhs->tag == AstTag::UOpImpliedMember);

				AstNode* const argument_name = first_child_of(argument_lhs);

				// TODO: Enforce this in parser
				ASSERT_OR_IGNORE(argument_name->tag == AstTag::Identifer);

				const IdentifierId argument_identifier = attachment_of<AstIdentifierData>(argument_name)->identifier_id;

				if (!type_member_info_by_name(interp->types, callee_type_id, argument_identifier, &argument_member))
				{
					const Range<char8> name = identifier_name_from_id(interp->identifiers, argument_identifier);

					source_error(interp->errors, argument_lhs->source_id, "`%.*s` is not an argument of the called function.\n", static_cast<s32>(name.count()), name.begin());
				}

				ASSERT_OR_IGNORE(argument_member.rank < 64);

				const u64 curr_argument_bit = static_cast<u64>(1) << argument_member.rank;

				if ((seen_argument_mask & curr_argument_bit) != 0)
				{
					const Range<char8> name = identifier_name_from_id(interp->identifiers, argument_identifier);

					source_error(interp->errors, argument_lhs->source_id, "Function argument `%.*s` set more than once.\n", static_cast<s32>(name.count()), name.begin());
				}

				seen_argument_mask |= curr_argument_bit;

				argument_type_id = typecheck_expr(interp, argument_rhs);
			}
			else
			{
				ASSERT_OR_IGNORE(seen_argument_count < 64);

				if (expect_named)
					source_error(interp->errors, argument->source_id, "Positional arguments must not follow named arguments.\n");

				if (seen_argument_count >= func_type->param_count)
					source_error(interp->errors, argument->source_id, "Call supplies more than the expeceted %d arguments.\n", func_type->param_count);

				if (!type_member_info_by_rank(interp->types, signature_type_id, seen_argument_count, &argument_member))
					source_error(interp->errors, argument->source_id, "Too many arguments in function call.\n");

				argument_type_id = typecheck_expr(interp, argument);

				seen_argument_count += 1;
			}

			ASSERT_OR_IGNORE(!argument_member.has_pending_type);

			if (!type_can_implicitly_convert_from_to(interp->types, type_id(argument_type_id), argument_member.type.complete))
				source_error(interp->errors, argument->source_id, "Cannot implicitly convert to expected argument type.\n");
		}

		if (!expect_named)
			seen_argument_mask = (static_cast<u64>(1) << seen_argument_count) - 1;

		for (u16 i = 0; i != func_type->param_count; ++i)
		{
			const u64 curr_argument_mask = static_cast<u64>(1) << i;

			if ((seen_argument_mask & curr_argument_mask) == 0)
			{
				MemberInfo member;

				if (!type_member_info_by_rank(interp->types, signature_type_id, i, &member))
					ASSERT_UNREACHABLE;

				if (member.value.pending == AstNodeId::INVALID)
				{
					const Range<char8> name = identifier_name_from_id(interp->identifiers, member.name);

					source_error(interp->errors, node->source_id, "Missing value for argument %.*s in call.\n", static_cast<s32>(name.count()), name.begin());
				}
			}
		}

		return concrete_type_ref(func_type->return_type_id, true);
	}

	case AstTag::UOpEval:
	{
		AstNode* const operand = first_child_of(node);

		return typecheck_expr(interp, operand);
	}

	case AstTag::UOpDistinct:
	{
		AstNode* const operand = first_child_of(node);

		const TypeId operand_type_id = type_id(typecheck_expr(interp, operand));

		const TypeTag operand_type_tag = type_tag_from_id(interp->types, operand_type_id);

		if (operand_type_tag != TypeTag::Type)
			source_error(interp->errors, operand->source_id, "Operand of `%s` must be of type `Type`.\n", tag_name(node->tag));

		return concrete_type_ref(alias_type(interp->types, operand_type_id, true, operand->source_id, IdentifierId::INVALID), false);
	}

	case AstTag::UOpAddr:
	{
		AstNode* const operand = first_child_of(node);

		const TypeRef operand_type_id = typecheck_expr(interp, operand);

		ReferenceType ptr_type{};
		ptr_type.is_multi = false;
		ptr_type.is_opt = false;
		ptr_type.is_mut = is_assignable(operand_type_id);
		ptr_type.referenced_type_id = type_id(operand_type_id);

		return concrete_type_ref(simple_type(interp->types, TypeTag::Ptr, range::from_object_bytes(&ptr_type)), false);
	}

	case AstTag::UOpDeref:
	{
		AstNode* const operand = first_child_of(node);

		const TypeId operand_type_id = type_id(typecheck_expr(interp, operand));

		const TypeTag operand_type_tag = type_tag_from_id(interp->types, operand_type_id);

		if (operand_type_tag != TypeTag::Ptr)
			source_error(interp->errors, operand->source_id, "Operand of `%s` must be of pointer type.\n", tag_name(node->tag));

		const ReferenceType* const reference = static_cast<const ReferenceType*>(simple_type_structure_from_id(interp->types, operand_type_id));

		return concrete_type_ref(reference->referenced_type_id, reference->is_mut);
	}

	case AstTag::UOpBitNot:
	{
		AstNode* const operand = first_child_of(node);

		const TypeId operand_type_id = type_id(typecheck_expr(interp, operand));

		const TypeTag operand_type_tag = type_tag_from_id(interp->types, operand_type_id);

		if (operand_type_tag != TypeTag::Integer && operand_type_tag != TypeTag::CompInteger)
			source_error(interp->errors, operand->source_id, "Operand of `%s` must be of integral type.\n", tag_name(node->tag));

		return concrete_type_ref(operand_type_id, false);
	}

	case AstTag::UOpLogNot:
	{
		AstNode* const operand = first_child_of(node);

		const TypeId operand_type_id = type_id(typecheck_expr(interp, operand));

		const TypeTag operand_type_tag = type_tag_from_id(interp->types, operand_type_id);

		if (operand_type_tag != TypeTag::Boolean)
			source_error(interp->errors, operand->source_id, "Operand of `%s` must be of boolean type.\n", tag_name(node->tag));

		return concrete_type_ref(operand_type_id, false);
	}

	case AstTag::UOpTypeTailArray:
	case AstTag::UOpTypeVar:
	case AstTag::UOpTypeSlice:
	case AstTag::UOpTypePtr:
	case AstTag::UOpTypeOptPtr:
	case AstTag::UOpTypeMultiPtr:
	case AstTag::UOpTypeOptMultiPtr:
	{
		AstNode* const operand = first_child_of(node);

		const TypeId operand_type_id = type_id(typecheck_expr(interp, operand));

		const TypeTag operand_type_tag = type_tag_from_id(interp->types, operand_type_id);

		if (operand_type_tag != TypeTag::Type)
			source_error(interp->errors, operand->source_id, "Operand of `%s` must be of type `Type`.\n", tag_name(node->tag));

		return concrete_type_ref(simple_type(interp->types, TypeTag::Type, {}), false);
	}

	case AstTag::UOpNegate:
	case AstTag::UOpPos:
	{
		AstNode* const operand = first_child_of(node);

		const TypeId operand_type_id = type_id(typecheck_expr(interp, node));

		const TypeTag operand_type_tag = type_tag_from_id(interp->types, operand_type_id);

		if (operand_type_tag != TypeTag::Integer && operand_type_tag != TypeTag::CompInteger && operand_type_tag != TypeTag::Float && operand_type_tag != TypeTag::CompFloat)
			source_error(interp->errors, operand->source_id, "Operand of unary `%s` must be of integral or floating point type.\n");

		if (node->tag == AstTag::UOpNegate && (operand_type_tag == TypeTag::Integer || operand_type_tag == TypeTag::CompInteger))
		{
			const NumericType* const integer_type = static_cast<const NumericType*>(simple_type_structure_from_id(interp->types, operand_type_id));

			if (!integer_type->is_signed)
				source_error(interp->errors, operand->source_id, "Operand of unary `%s` must be signed.\n");
		}

		return concrete_type_ref(operand_type_id, false);
	}

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
	{
		AstNode* const lhs = first_child_of(node);

		const TypeId lhs_type_id = type_id(typecheck_expr(interp, lhs));

		const TypeTag lhs_type_tag = type_tag_from_id(interp->types, lhs_type_id);

		if (node->tag != AstTag::OpSet && lhs_type_tag != TypeTag::Integer && lhs_type_tag != TypeTag::CompInteger)
		{
			if (node->tag != AstTag::OpSetAdd && node->tag != AstTag::OpSetSub && node->tag != AstTag::OpSetMul && node->tag != AstTag::OpSetDiv)
				source_error(interp->errors, lhs->source_id, "Left-hand-side of `%s` must be of integral type.\n", tag_name(node->tag));

			if (lhs_type_tag != TypeTag::Float && lhs_type_tag != TypeTag::CompFloat)
				source_error(interp->errors, lhs->source_id, "Left-hand-side of `%s` must be of integral or floating point type.\n", tag_name(node->tag));
		}

		AstNode* const rhs = next_sibling_of(lhs);

		const TypeId rhs_type_id = type_id(typecheck_expr(interp, rhs));

		const TypeTag rhs_type_tag = type_tag_from_id(interp->types, rhs_type_id);

		if (node->tag != AstTag::OpSet && rhs_type_tag != TypeTag::Integer && rhs_type_tag != TypeTag::CompInteger)
		{
			if (node->tag != AstTag::OpSetAdd && node->tag != AstTag::OpSetSub && node->tag != AstTag::OpSetMul && node->tag != AstTag::OpSetDiv)
				source_error(interp->errors, rhs->source_id, "Right-hand-side of `%s` must be of integral type.\n", tag_name(node->tag));

			if (rhs_type_tag != TypeTag::Float && rhs_type_tag != TypeTag::CompFloat)
				source_error(interp->errors, rhs->source_id, "Right-hand-side of `%s` must be of integral or floating point type.\n", tag_name(node->tag));
		}

		const TypeId common_type_id = common_type(interp->types, lhs_type_id, rhs_type_id);

		if (common_type_id == TypeId::INVALID)
			source_error(interp->errors, node->source_id, "Incompatible left-hand and right-hand side operands for `%s`.\n", tag_name(node->tag));

		return concrete_type_ref(common_type_id, false);
	}

	case AstTag::OpShiftL:
	case AstTag::OpShiftR:
	{
		AstNode* const lhs = first_child_of(node);

		const TypeId lhs_type_id = type_id(typecheck_expr(interp, lhs));

		const TypeTag lhs_type_tag = type_tag_from_id(interp->types, lhs_type_id);

		if (lhs_type_tag != TypeTag::Integer && lhs_type_tag != TypeTag::CompInteger)
			source_error(interp->errors, lhs->source_id, "Left-hand-side of `%s` must be of integral type.\n", tag_name(node->tag));

		AstNode* const rhs = next_sibling_of(lhs);

		const TypeId rhs_type_id = type_id(typecheck_expr(interp, rhs));

		const TypeTag rhs_type_tag = type_tag_from_id(interp->types, rhs_type_id);

		if (rhs_type_tag != TypeTag::Integer && rhs_type_tag != TypeTag::CompInteger)
			source_error(interp->errors, lhs->source_id, "Right-hand-side of `%s` must be of integral type.\n", tag_name(node->tag));

		return concrete_type_ref(lhs_type_id, false);
	}

	case AstTag::OpLogAnd:
	case AstTag::OpLogOr:
	{
		AstNode* const lhs = first_child_of(node);

		const TypeId lhs_type_id = type_id(typecheck_expr(interp, lhs));

		const TypeTag lhs_type_tag = type_tag_from_id(interp->types, lhs_type_id);

		if (lhs_type_tag != TypeTag::Boolean)
			source_error(interp->errors, lhs->source_id, "Left-hand-side of `%s` must be of boolean type.\n", tag_name(node->tag));

		AstNode* const rhs = next_sibling_of(lhs);

		const TypeId rhs_type_id = type_id(typecheck_expr(interp, rhs));

		const TypeTag rhs_type_tag = type_tag_from_id(interp->types, rhs_type_id);

		if (rhs_type_tag != TypeTag::Boolean)
			source_error(interp->errors, lhs->source_id, "Right-hand-side of `%s` must be of boolean type.\n", tag_name(node->tag));

		const TypeId common_type_id = common_type(interp->types, lhs_type_id, rhs_type_id);

		if (common_type_id == TypeId::INVALID)
			source_error(interp->errors, node->source_id, "Operands of `%s` are incompatible.\n", tag_name(node->tag));

		return concrete_type_ref(common_type_id, false);
	}

	case AstTag::OpMember:
	{
		AstNode* const lhs = first_child_of(node);

		const TypeRef lhs_type_id = typecheck_expr(interp, lhs);

		const TypeTag lhs_type_tag = type_tag_from_id(interp->types, type_id(lhs_type_id));

		if (lhs_type_tag != TypeTag::Composite && lhs_type_tag != TypeTag::Type)
			source_error(interp->errors, lhs->source_id, "Left-hand-side of `.` must be of type `Type` or a composite type.\n");

		AstNode* const rhs = next_sibling_of(lhs);

		if (rhs->tag != AstTag::Identifer)
			source_error(interp->errors, rhs->source_id, "Right-hand-side of `.` must be an identifier\n");

		rhs->type = concrete_type_ref(TypeId::NO_TYPE, false);

		const IdentifierId identifier_id = attachment_of<AstIdentifierData>(rhs)->identifier_id;

		if (lhs_type_tag == TypeTag::Composite)
		{
			MemberInfo member;

			if (!type_member_info_by_name(interp->types, type_id(lhs_type_id), identifier_id, &member))
			{
				const Range<char8> name = identifier_name_from_id(interp->identifiers, identifier_id);

				source_error(interp->errors, node->source_id, "Left-hand-side of `.` has no member \"%.*s\"", static_cast<s32>(name.count()), name.begin());
			}

			force_member_type(interp, &member);

			return concrete_type_ref(member.type.complete, member.is_mut && is_assignable(lhs_type_id));
		}
		else
		{
			ASSERT_OR_IGNORE(lhs_type_tag == TypeTag::Type);

			void* defined_type_id_ptr;

			if (!evaluate_expr(interp, lhs, type_id(lhs_type_id), &defined_type_id_ptr))
				TODO("Implement.");

			const TypeId defined_type_id = *static_cast<TypeId*>(defined_type_id_ptr);

			pop_temporary(interp);

			MemberInfo member;

			if (!type_member_info_by_name(interp->types, defined_type_id, identifier_id, &member))
			{
				const Range<char8> name = identifier_name_from_id(interp->identifiers, identifier_id);

				source_error(interp->errors, node->source_id, "Left-hand-side of `.` has no member \"%.*s\"", static_cast<s32>(name.count()), name.begin());
			}

			if (!member.is_global)
			{
				const Range<char8> name = identifier_name_from_id(interp->identifiers, identifier_id);

				source_error(interp->errors, node->source_id, "Non-global member %.*s cannot be referenced by its parent type.\n", static_cast<s32>(name.count()), name.begin());
			}

			force_member_type(interp, &member);

			return concrete_type_ref(member.type.complete, member.is_mut && is_assignable(lhs_type_id));
		}
	}

	case AstTag::OpCmpLT:
	case AstTag::OpCmpGT:
	case AstTag::OpCmpLE:
	case AstTag::OpCmpGE:
	case AstTag::OpCmpNE:
	case AstTag::OpCmpEQ:
	{
		AstNode* const lhs = first_child_of(node);

		const TypeId lhs_type_id = type_id(typecheck_expr(interp, lhs));

		const TypeTag lhs_type_tag = type_tag_from_id(interp->types, lhs_type_id);

		if (lhs_type_tag == TypeTag::Array || lhs_type_tag == TypeTag::ArrayLiteral || lhs_type_tag == TypeTag::Composite || lhs_type_tag == TypeTag::CompositeLiteral)
			source_error(interp->errors, lhs->source_id, "Left-hand-side of %s must not be of composite or array type.\n");

		AstNode* const rhs = next_sibling_of(lhs);

		const TypeId rhs_type_id = type_id(typecheck_expr(interp, rhs));

		const TypeTag rhs_type_tag = type_tag_from_id(interp->types, rhs_type_id);

		if (rhs_type_tag == TypeTag::Array || rhs_type_tag == TypeTag::ArrayLiteral || rhs_type_tag == TypeTag::Composite || rhs_type_tag == TypeTag::CompositeLiteral)
			source_error(interp->errors, rhs->source_id, "Right-hand-side of %s must not be of composite or array type.\n");

		const TypeId common_type_id = common_type(interp->types, lhs_type_id, rhs_type_id);

		if (common_type_id == TypeId::INVALID)
			source_error(interp->errors, node->source_id, "Incompatible left-hand and right-hand side operands for `%s`.\n", tag_name(node->tag));

		return concrete_type_ref(simple_type(interp->types, TypeTag::Boolean, {}), false);
	}

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
	{
		AstNode* const lhs = first_child_of(node);

		const TypeRef lhs_type_id = typecheck_expr(interp, lhs);

		const TypeTag lhs_type_tag = type_tag_from_id(interp->types, type_id(lhs_type_id));

		if (node->tag != AstTag::OpSet && lhs_type_tag != TypeTag::Integer && lhs_type_tag != TypeTag::CompInteger)
		{
			if (node->tag != AstTag::OpSetAdd && node->tag != AstTag::OpSetSub && node->tag != AstTag::OpSetMul && node->tag != AstTag::OpSetDiv)
				source_error(interp->errors, lhs->source_id, "Left-hand-side of `%s` must be of integral type.\n", tag_name(node->tag));

			if (lhs_type_tag != TypeTag::Float && lhs_type_tag != TypeTag::CompFloat)
				source_error(interp->errors, lhs->source_id, "Left-hand-side of `%s` must be of integral or floating point type.\n", tag_name(node->tag));
		}

		if (!is_assignable(lhs_type_id))
			source_error(interp->errors, lhs->source_id, "Left-hand-side of `%s` must be assignable.\n", tag_name(node->tag));

		AstNode* const rhs = next_sibling_of(lhs);

		const TypeId rhs_type_id = type_id(typecheck_expr(interp, rhs));

		const TypeTag rhs_type_tag = type_tag_from_id(interp->types, rhs_type_id);

		if (node->tag != AstTag::OpSet && rhs_type_tag != TypeTag::Integer && rhs_type_tag != TypeTag::CompInteger)
		{
			if (node->tag != AstTag::OpSetAdd && node->tag != AstTag::OpSetSub && node->tag != AstTag::OpSetMul && node->tag != AstTag::OpSetDiv)
				source_error(interp->errors, rhs->source_id, "Right-hand-side of `%s` must be of integral type.\n", tag_name(node->tag));

			if (rhs_type_tag != TypeTag::Float && rhs_type_tag != TypeTag::CompFloat)
				source_error(interp->errors, rhs->source_id, "Right-hand-side of `%s` must be of integral or floating point type.\n", tag_name(node->tag));
		}

		const TypeId common_type_id = common_type(interp->types, type_id(lhs_type_id), rhs_type_id);

		if (common_type_id == TypeId::INVALID)
			source_error(interp->errors, node->source_id, "Incompatible left-hand and right-hand side operands for `%s`.\n", tag_name(node->tag));

		return concrete_type_ref(simple_type(interp->types, TypeTag::Void, {}), false);
	}

	case AstTag::OpSetShiftL:
	case AstTag::OpSetShiftR:
	{
		AstNode* const lhs = first_child_of(node);

		const TypeRef lhs_type_id = typecheck_expr(interp, lhs);

		const TypeTag lhs_type_tag = type_tag_from_id(interp->types, type_id(lhs_type_id));

		if (lhs_type_tag != TypeTag::Integer && lhs_type_tag != TypeTag::CompInteger)
			source_error(interp->errors, lhs->source_id, "Left-hand-side of `%s` must be of integral type.\n", tag_name(node->tag));

		if (!is_assignable(lhs_type_id))
			source_error(interp->errors, lhs->source_id, "Left-hand-side of `%s` must be assignable.\n", tag_name(node->tag));

		AstNode* const rhs = next_sibling_of(lhs);

		const TypeId rhs_type_id = type_id(typecheck_expr(interp, rhs));

		const TypeTag rhs_type_tag = type_tag_from_id(interp->types, rhs_type_id);

		if (rhs_type_tag != TypeTag::Integer && rhs_type_tag != TypeTag::CompInteger)
			source_error(interp->errors, lhs->source_id, "Right-hand-side of `%s` must be of integral type.\n", tag_name(node->tag));

		return concrete_type_ref(simple_type(interp->types, TypeTag::Void, {}), false);
	}

	case AstTag::OpTypeArray:
	{
		AstNode* const count = first_child_of(node);

		const TypeId count_type_id = type_id(typecheck_expr(interp, count));

		const TypeTag count_type_tag = type_tag_from_id(interp->types, count_type_id);

		if (count_type_tag != TypeTag::Integer && count_type_tag != TypeTag::CompInteger)
			source_error(interp->errors, count->source_id, "Expected array count expression to be of integral type.\n");

		AstNode* const type = next_sibling_of(count);

		const TypeId type_type_id = type_id(typecheck_expr(interp, type));

		const TypeTag type_type_tag = type_tag_from_id(interp->types, type_type_id);

		if (type_type_tag != TypeTag::Type)
			source_error(interp->errors, type->source_id, "Expected array type expression of be of type `Type`.\n");

		return concrete_type_ref(simple_type(interp->types, TypeTag::Type, {}), false);
	}

	case AstTag::OpArrayIndex:
	{
		AstNode* const arrayish = first_child_of(node);

		const TypeId arrayish_type_id = type_id(typecheck_expr(interp, arrayish));

		const TypeTag array_type_tag = type_tag_from_id(interp->types, arrayish_type_id);

		const void* const structure = simple_type_structure_from_id(interp->types, arrayish_type_id);

		TypeId element_type_id;

		bool result_is_assignable;

		if (array_type_tag == TypeTag::Array)
		{
			const ArrayType* const array = static_cast<const ArrayType*>(structure);

			element_type_id = array->element_type;

			result_is_assignable = true;
		}
		else if (array_type_tag == TypeTag::Slice || array_type_tag == TypeTag::Ptr)
		{
			const ReferenceType* const  reference = static_cast<const ReferenceType*>(structure);

			element_type_id = reference->referenced_type_id;

			result_is_assignable = reference->is_mut;
		}
		else
			source_error(interp->errors, arrayish->source_id, "Left-hand-side of array dereference operator must be of array-, slice- or multi-pointer type.\n");

		AstNode* const index = next_sibling_of(arrayish);

		const TypeId index_type_id = type_id(typecheck_expr(interp, index));

		const TypeTag index_type_tag = type_tag_from_id(interp->types, index_type_id);

		if (index_type_tag != TypeTag::Integer && index_type_tag != TypeTag::CompInteger)
			source_error(interp->errors, index->source_id, "Index operand of array dereference operator must be of integral type.\n");

		return concrete_type_ref(element_type_id, result_is_assignable);
	}

	case AstTag::INVALID:
	case AstTag::MAX:
	case AstTag::File:
	case AstTag::Case:
	case AstTag::ParameterList:
		; // fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

static bool evaluate_expr_impl(Interpreter* interp, AstNode* node, void** out) noexcept
{
	ASSERT_OR_IGNORE(is_valid(node->type_id));

	switch (node->tag)
	{
	case AstTag::Builtin:
	{
		const u8 ordinal = static_cast<u8>(node->flags);

		Callable* const dst = static_cast<Callable*>(push_temporary(interp, 8, 8));
		dst->func_type_id_bits = static_cast<u32>(interp->builtin_type_ids[ordinal]);
		dst->is_builtin = true;
		dst->code.ordinal = ordinal;

		*out = dst;

		return true;
	}

	case AstTag::Block:
	{
		const TypeId scope_type_id = attachment_of<AstBlockData>(node)->scope_type_id;

		AstDirectChildIterator statements = direct_children_of(node);

		if (!has_next(&statements))
		{
			*out = push_temporary(interp, 0, 1); // Void.

			return true;
		}

		void* const activation_record = push_activation_record(interp, scope_type_id, false);

		void* statement_value = nullptr;

		while (has_next(&statements))
		{
			AstNode* const statement = next(&statements);

			if (statement->tag == AstTag::Definition)
			{
				const DefinitionInfo info = get_definition_info(statement);

				void* value_address = lookup_identifier_address_immediate(interp, attachment_of<AstDefinitionData>(statement)->identifier_id);

				AstNode* const rhs = get_ptr(info.value);

				void* stack_value;

				if (!evaluate_expr(interp, rhs, type_id(rhs->type_id), &stack_value))
					TODO("Implement.");

				const TypeMetrics metrics = type_metrics_from_id(interp->types, type_id(rhs->type_id));

				memcpy(value_address, stack_value, metrics.size);

				pop_temporary(interp);

				if (!has_next(&statements))
					TODO("Implement returning of trailing definition.\n");
			}
			else
			{
				if (!evaluate_expr(interp, statement, type_id(statement->type_id), &statement_value))
					TODO("Implement.");

				if (has_next(&statements))
					pop_temporary(interp);
			}
		}

		pop_activation_record(interp);

		*out = statement_value;

		return true;
	}

	case AstTag::If:
	{
		IfInfo info = get_if_info(node);

		ASSERT_OR_IGNORE(is_some(info.alternative) || type_tag_from_id(interp->types, type_id(node->type_id)) == TypeTag::Void);

		void* condition_value_ptr;
		
		if (!evaluate_expr(interp, info.condition, type_id(info.condition->type_id), &condition_value_ptr))
			return false;

		const bool condition_value = *static_cast<bool*>(condition_value_ptr);

		pop_temporary(interp);

		if (condition_value)
		{
			return evaluate_expr(interp, info.consequent, type_id(node->type_id), out);
		}
		else if (is_some(info.alternative))
		{
			return evaluate_expr(interp, get_ptr(info.alternative), type_id(node->type_id), out);
		}
		else
		{
			*out = push_temporary(interp, 0, 1); // Void

			return true;
		}
	}

	case AstTag::Func:
	{
		FuncInfo info = get_func_info(node);

		const TypeId func_type_id = attachment_of<AstFuncData>(node)->func_type_id;

		if (is_some(info.body))
		{
			Callable* const stack_value = static_cast<Callable*>(push_temporary(interp, sizeof(Callable), alignof(Callable)));
	
			stack_value->func_type_id_bits = static_cast<u32>(func_type_id);
			stack_value->is_builtin = false;
			stack_value->code.ast = id_from_ast_node(interp->asts, get_ptr(info.body));

			*out = stack_value;

			return true;
		}
		else
		{
			TypeId* const stack_value = static_cast<TypeId*>(push_temporary(interp, 4, 4));

			*stack_value = func_type_id;

			*out = stack_value;

			return true;
		}
	}

	case AstTag::Identifer:
	{
		void* identifier_value;
		
		if (!lookup_identifier_address(interp, attachment_of<AstIdentifierData>(node)->identifier_id, node->source_id, &identifier_value))
			return false;

		const TypeMetrics metrics = type_metrics_from_id(interp->types, type_id(node->type_id));

		void* const stack_value = push_temporary(interp, metrics.size, metrics.align);

		memcpy(stack_value, identifier_value, metrics.size);

		*out = stack_value;

		return true;
	}

	case AstTag::LitInteger:
	{
		const CompIntegerValue value = attachment_of<AstLitIntegerData>(node)->value;

		CompIntegerValue* const stack_value = static_cast<CompIntegerValue*>(push_temporary(interp, 8, 8));

		*stack_value = value;

		*out = stack_value;

		return true;
	}

	case AstTag::LitFloat:
	{
		const CompFloatValue value = attachment_of<AstLitFloatData>(node)->value;

		CompFloatValue* const stack_value = static_cast<CompFloatValue*>(push_temporary(interp, 8, 8));

		*stack_value = value;

		*out = stack_value;

		return true;
	}

	case AstTag::LitChar:
	{
		const u32 value = attachment_of<AstLitCharData>(node)->codepoint;

		CompIntegerValue* const stack_value = static_cast<CompIntegerValue*>(push_temporary(interp, 8, 8));

		*stack_value = comp_integer_from_u64(value);

		*out = stack_value;

		return true;
	}

	case AstTag::LitString:
	{
		ArrayValue* const stack_value = static_cast<ArrayValue*>(push_temporary(interp, sizeof(ArrayValue), alignof(ArrayValue)));

		stack_value->value_ptr = global_value_from_id(interp->globals, attachment_of<AstLitStringData>(node)->string_value_id).address;

		ASSERT_OR_IGNORE((reinterpret_cast<u64>(stack_value->value_ptr) & 1) == 0);

		*out = stack_value;

		return true;
	}

	case AstTag::Call:
	{
		AstNode* const callee = first_child_of(node);

		void* callable_ptr;

		if (!evaluate_expr(interp, callee, type_id(callee->type_id), &callable_ptr))
			return false;

		const Callable callable = *static_cast<Callable*>(callable_ptr);

		pop_temporary(interp);

		const TypeId func_type_id = TypeId{ callable.func_type_id_bits };

		const FuncType* const func_type = static_cast<const FuncType*>(simple_type_structure_from_id(interp->types, func_type_id));

		const TypeId signature_type_id = func_type->signature_type_id;

		const TypeMetrics signature_metrics = type_metrics_from_id(interp->types, signature_type_id);

		void* const temp_activation_record = push_temporary(interp, signature_metrics.size, signature_metrics.align);

		AstNode* argument = callee;

		u16 rank = 0;

		u64 seen_argument_mask = 0;

		while (has_next_sibling(argument))
		{
			argument = next_sibling_of(argument);

			MemberInfo member;

			void* member_value;

			if (argument->tag == AstTag::OpSet)
			{
				AstNode* const argument_name = first_child_of(argument);

				AstNode* const argument_value = next_sibling_of(argument_name);

				if (!type_member_info_by_name(interp->types, signature_type_id, attachment_of<AstIdentifierData>(argument_name)->identifier_id, &member))
					ASSERT_UNREACHABLE;

				if (!evaluate_expr(interp, argument_value, member.type.complete, &member_value))
					return false;
			}
			else
			{
				if (!type_member_info_by_rank(interp->types, signature_type_id, rank, &member))
					ASSERT_UNREACHABLE;

				if (!evaluate_expr(interp, argument, member.type.complete, &member_value))
					return false;

				rank += 1;
			}

			ASSERT_OR_IGNORE(member.is_global == false);

			const TypeMetrics member_metrics = type_metrics_from_id(interp->types, member.type.complete);

			memcpy(static_cast<byte*>(temp_activation_record) + member.offset, member_value, member_metrics.size);

			pop_temporary(interp);

			seen_argument_mask |= static_cast<u64>(1) << member.rank;
		}

		for (u16 i = 0; i != func_type->param_count; ++i)
		{
			const u64 curr_argument_bit = static_cast<u64>(1) << i;

			if ((seen_argument_mask & curr_argument_bit) != 0)
				continue;

			TODO("Copy default value to activation record.");
		}

		void* const activation_record = push_activation_record(interp, signature_type_id, true);

		memcpy(activation_record, temp_activation_record, signature_metrics.size);

		if (callable.is_builtin)
			*out = interp->builtin_values[callable.code.ordinal](interp, node);
		else if (!evaluate_expr(interp, ast_node_from_id(interp->asts, callable.code.ast), type_id(node->type_id), out))
			return false;

		pop_activation_record(interp);

		return true;
	}

	case AstTag::UOpTypeTailArray:
	{
		AstNode* const operand = first_child_of(node);

		ASSERT_OR_IGNORE(type_tag_from_id(interp->types, type_id(operand->type_id)) == TypeTag::Type);

		void* element_type_id_ptr;

		if (!evaluate_expr(interp, operand, type_id(operand->type_id), &element_type_id_ptr))
			return false;

		const TypeId element_type_id = *static_cast<TypeId*>(element_type_id_ptr);

		pop_temporary(interp);

		ReferenceType tail_array_type{};
		tail_array_type.is_multi = false;
		tail_array_type.is_opt = false;
		tail_array_type.is_mut = true;
		tail_array_type.referenced_type_id = element_type_id;

		const TypeId defined_type_id = simple_type(interp->types, TypeTag::TailArray, range::from_object_bytes(&tail_array_type));

		TypeId* const stack_value = static_cast<TypeId*>(push_temporary(interp, 4, 4));

		*stack_value = defined_type_id;

		*out = stack_value;

		return true;
	}

	case AstTag::UOpTypeSlice:
	{
		AstNode* const operand = first_child_of(node);

		ASSERT_OR_IGNORE(type_tag_from_id(interp->types, type_id(operand->type_id)) == TypeTag::Type);

		void* element_type_id_ptr;

		if (!evaluate_expr(interp, operand, type_id(operand->type_id), &element_type_id_ptr))
			return false;

		const TypeId element_type_id = *static_cast<TypeId*>(element_type_id_ptr);

		pop_temporary(interp);

		ReferenceType slice_type{};
		slice_type.is_multi = false;
		slice_type.is_opt = false;
		slice_type.is_mut = has_flag(node, AstFlag::Type_IsMut);
		slice_type.referenced_type_id = element_type_id;

		const TypeId defined_type_id = simple_type(interp->types, TypeTag::Slice, range::from_object_bytes(&slice_type));

		TypeId* const stack_value = static_cast<TypeId*>(push_temporary(interp, 4, 4));

		*stack_value = defined_type_id;

		*out = stack_value;

		return true;
	}

	case AstTag::UOpLogNot:
	{
		AstNode* const operand = first_child_of(node);

		void* operand_value_ptr;

		if (!evaluate_expr(interp, operand, type_id(node->type_id), &operand_value_ptr))
			return false;

		bool* const operand_value = static_cast<bool*>(operand_value_ptr);

		*operand_value = !*operand_value;

		*out = operand_value;

		return true;
	}

	case AstTag::UOpTypeVar:
	{
		AstNode* const operand = first_child_of(node);

		ASSERT_OR_IGNORE(type_tag_from_id(interp->types, type_id(operand->type_id)) == TypeTag::Type);

		void* element_type_id_ptr;

		if (!evaluate_expr(interp, operand, type_id(operand->type_id), &element_type_id_ptr))
			return false;

		const TypeId element_type_id = *static_cast<TypeId*>(element_type_id_ptr);

		pop_temporary(interp);

		ReferenceType variadic_type{};
		variadic_type.is_multi = false;
		variadic_type.is_opt = false;
		variadic_type.is_mut = false;
		variadic_type.referenced_type_id = element_type_id;

		const TypeId defined_type_id = simple_type(interp->types, TypeTag::Variadic, range::from_object_bytes(&variadic_type));

		TypeId* const stack_value = static_cast<TypeId*>(push_temporary(interp, 4, 4));

		*stack_value = defined_type_id;

		*out = stack_value;

		return true;
	}

	case AstTag::UOpTypePtr:
	case AstTag::UOpTypeOptPtr:
	case AstTag::UOpTypeMultiPtr:
	case AstTag::UOpTypeOptMultiPtr:
	{
		AstNode* const operand = first_child_of(node);

		ASSERT_OR_IGNORE(type_tag_from_id(interp->types, type_id(operand->type_id)) == TypeTag::Type);

		void* element_type_id_ptr;

		if (!evaluate_expr(interp, operand, type_id(operand->type_id), &element_type_id_ptr))
			return false;

		const TypeId element_type_id = *static_cast<TypeId*>(element_type_id_ptr);

		pop_temporary(interp);

		ReferenceType ptr_type{};
		ptr_type.is_multi = node->tag == AstTag::UOpTypeMultiPtr || node->tag == AstTag::UOpTypeOptMultiPtr;
		ptr_type.is_opt = node->tag == AstTag::UOpTypeOptPtr || node->tag == AstTag::UOpTypeOptMultiPtr;
		ptr_type.is_mut = has_flag(node, AstFlag::Type_IsMut);
		ptr_type.referenced_type_id = element_type_id;

		const TypeId defined_type_id = simple_type(interp->types, TypeTag::Ptr, range::from_object_bytes(&ptr_type));

		TypeId* const stack_value = static_cast<TypeId*>(push_temporary(interp, 4, 4));

		*stack_value = defined_type_id;

		*out = stack_value;

		return true;
	}

	case AstTag::OpMember:
	{
		AstNode* const lhs = first_child_of(node);

		const TypeTag lhs_type_tag = type_tag_from_id(interp->types, type_id(lhs->type_id));

		AstNode* const rhs = next_sibling_of(lhs);

		ASSERT_OR_IGNORE(rhs->tag == AstTag::Identifer);

		const IdentifierId member_name = attachment_of<AstIdentifierData>(rhs)->identifier_id;

		const void* member_address;

		TypeMetrics member_metrics;

		if (lhs_type_tag == TypeTag::Type)
		{
			void* lhs_type_id_ptr;

			if (!evaluate_expr(interp, lhs, type_id(lhs->type_id), &lhs_type_id_ptr))
				return false;

			const TypeId lhs_type_id = *static_cast<TypeId*>(lhs_type_id_ptr);

			pop_temporary(interp);

			MemberInfo member;
	
			if (!type_member_info_by_name(interp->types, lhs_type_id, member_name, &member))
			{
				const Range<char8> name = identifier_name_from_id(interp->identifiers, member_name);

				source_error(interp->errors, node->source_id, "Left-hand-side of member operator has member named '%.*s'", static_cast<s32>(name.count()), name.begin());
			}
	
			if (!member.is_global)
			{
				const Range<char8> name = identifier_name_from_id(interp->identifiers, member_name);

				source_error(interp->errors, node->source_id, "'%.*s' is not a global member of type-valued left-hand-side of member operator\n", static_cast<s32>(name.count()), name.begin());
			}

			force_member_value(interp, &member);

			member_metrics = type_metrics_from_id(interp->types, member.type.complete);

			member_address = global_value_from_id(interp->globals, member.value.complete).address;
		}
		else
		{
			MemberInfo member;

			if (!type_member_info_by_name(interp->types, type_id(lhs->type_id), member_name, &member))
			{
				const Range<char8> name = identifier_name_from_id(interp->identifiers, member_name);

				source_error(interp->errors, node->source_id, "Left-hand-side of member operator has member named '%.*s'", static_cast<s32>(name.count()), name.begin());
			}

			member_metrics = type_metrics_from_id(interp->types, member.type.complete);

			if (member.is_global)
			{
				force_member_value(interp, &member);

				member_address = global_value_from_id(interp->globals, member.value.complete).address;
			}
			else
			{
				void* const base_address = address_expr(interp, lhs);
	
				member_address = static_cast<byte*>(base_address) + member.offset;
			}
		}

		void* const stack_value = push_temporary(interp, member_metrics.size, member_metrics.align);

		memcpy(stack_value, member_address, member_metrics.size);

		*out = stack_value;

		return true;
	}

	case AstTag::OpCmpEQ:
	{
		AstNode* const lhs = first_child_of(node);

		AstNode* const rhs = next_sibling_of(lhs);

		const TypeId common_type_id = common_type(interp->types, type_id(lhs->type_id), type_id(rhs->type_id));

		ASSERT_OR_IGNORE(common_type_id != TypeId::INVALID);

		void* lhs_value;

		if (!evaluate_expr(interp, lhs, common_type_id, &lhs_value))
			return false;

		void* rhs_value;

		if (!evaluate_expr(interp, rhs, common_type_id, &rhs_value))
		{
			// Clean up `lhs_value`.
			pop_temporary(interp);

			return false;
		}

		const TypeTag common_type_tag = type_tag_from_id(interp->types, common_type_id);

		bool result;

		if (common_type_tag == TypeTag::CompInteger)
		{
			result = comp_integer_equal(*static_cast<const CompIntegerValue*>(lhs_value), *static_cast<const CompIntegerValue*>(rhs_value));
		}
		else
		{
			TODO("Implement `OpCmpEq` for non-`CompInteger` types.\n");
		}

		pop_temporary(interp);

		pop_temporary(interp);

		bool *const stack_value = static_cast<bool*>(push_temporary(interp, 1, 1));

		*stack_value = result;

		*out = stack_value;

		return true;
	}
	
	case AstTag::OpTypeArray:
	{
		AstNode* const count = first_child_of(node);

		const TypeTag count_type_tag = type_tag_from_id(interp->types, type_id(count->type_id));

		u64 element_count;

		void* count_value_ptr;

		if (!evaluate_expr(interp, count, type_id(count->type_id), &count_value_ptr))
			return false;

		if (!any_integer_to_u64(interp, type_id(count->type_id), count_value_ptr, &element_count))
			source_error(interp->errors, count->source_id, "Array element count must fit into unsigned 64-bit integer.\n");

		pop_temporary(interp);

		AstNode* const type = next_sibling_of(count);

		void* element_type_id_ptr;

		if (!evaluate_expr(interp, type, type_id(type->type_id), &element_type_id_ptr))
			return false;

		const TypeId element_type_id = *static_cast<TypeId*>(element_type_id_ptr);

		pop_temporary(interp);

		ArrayType array_type{};
		array_type.element_type = element_type_id;
		array_type.element_count = element_count;

		const TypeId defined_type_id = simple_type(interp->types, TypeTag::Array, range::from_object_bytes(&array_type));

		TypeId* const stack_value = static_cast<TypeId*>(push_temporary(interp, 4, 4));

		*stack_value = defined_type_id;

		*out = stack_value;

		return true;
	}

	case AstTag::CompositeInitializer:
	case AstTag::ArrayInitializer:
	case AstTag::Wildcard:
	case AstTag::Where:
	case AstTag::Expects:
	case AstTag::Ensures:
	case AstTag::Definition:
	case AstTag::For:
	case AstTag::ForEach:
	case AstTag::Switch:
	case AstTag::Trait:
	case AstTag::Impl:
	case AstTag::Catch:
	case AstTag::Return:
	case AstTag::Leave:
	case AstTag::Yield:
	case AstTag::UOpEval:
	case AstTag::UOpTry:
	case AstTag::UOpDefer:
	case AstTag::UOpDistinct:
	case AstTag::UOpAddr:
	case AstTag::UOpDeref:
	case AstTag::UOpBitNot:
	case AstTag::UOpImpliedMember:
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
	case AstTag::OpArrayIndex:
		source_error(interp->errors, node->source_id, "Evaluation of AST node type %s is not yet implemented (%s:%u).\n", tag_name(node->tag), __FILE__, __LINE__);

	case AstTag::INVALID:
	case AstTag::MAX:
	case AstTag::File:
	case AstTag::Case:
	case AstTag::ParameterList:
		; // fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

static bool evaluate_expr(Interpreter* interp, AstNode* node, TypeId target_type_id, void** out) noexcept
{
	ASSERT_OR_IGNORE(is_valid(target_type_id));

	const TypeTag target_type_tag = type_tag_from_id(interp->types, target_type_id);

	if (target_type_tag == TypeTag::TypeInfo)
	{
		TypeId* stack_top = static_cast<TypeId*>(push_temporary(interp, 4, 4));

		*stack_top = type_id(node->type_id);

		*out = stack_top;
	}
	else
	{
		void* stack_top;
		
		if (!evaluate_expr_impl(interp, node, &stack_top))
			return false;

		*out = implicit_convert(interp, stack_top, type_id(node->type_id), target_type_id, target_type_tag, node->source_id);
	}

	return true;
}

static void* address_expr(Interpreter* interp, AstNode* node) noexcept
{
	ASSERT_OR_IGNORE(is_valid(node->type_id));

	switch (node->tag)
	{
	case AstTag::Identifer:
	{
		void* result;

		if (!lookup_identifier_address(interp, attachment_of<AstIdentifierData>(node)->identifier_id, node->source_id, &result))
			TODO("Addressing deferred identifier is not yet implemented.\n");
	}
	
	case AstTag::OpMember:
	{
		AstNode* const lhs = first_child_of(node);

		void* const base_address = address_expr(interp, lhs);

		AstNode* const rhs = next_sibling_of(lhs);

		ASSERT_OR_IGNORE(rhs->tag == AstTag::Identifer);

		const IdentifierId member_name = attachment_of<AstIdentifierData>(rhs)->identifier_id;

		MemberInfo member;

		if (!type_member_info_by_name(interp->types, type_id(lhs->type_id), member_name, &member))
		{
			const Range<char8> name = identifier_name_from_id(interp->identifiers, member_name);
			
			source_error(interp->errors, node->source_id, "Left-hand-side of member operator has no member named '%.*s'.\n", static_cast<s32>(name.count()), name.begin());
		}

		if (member.is_global)
		{
			force_member_value(interp, &member);

			return global_value_from_id(interp->globals, member.value.complete).address;
		}
		else
		{
			return static_cast<byte*>(base_address) + member.offset;
		}
	}

	case AstTag::OpArrayIndex:
	{
		AstNode* const lhs = first_child_of(node);

		const TypeId lhs_type_id = type_id(lhs->type_id);

		const TypeTag lhs_type_tag = type_tag_from_id(interp->types, lhs_type_id);

		void* base_address;

		u64 stride;

		u64 index_limit;

		if (lhs_type_tag == TypeTag::Array)
		{
			const ArrayType* const array_type = static_cast<const ArrayType*>(simple_type_structure_from_id(interp->types, lhs_type_id));

			base_address = address_expr(interp, lhs);

			stride = type_metrics_from_id(interp->types, array_type->element_type).stride;

			index_limit = array_type->element_count;
		}
		else if (lhs_type_tag == TypeTag::Ptr)
		{
			const ReferenceType* const ptr_type = static_cast<const ReferenceType*>(simple_type_structure_from_id(interp->types, lhs_type_id));

			ASSERT_OR_IGNORE(ptr_type->is_multi && !ptr_type->is_opt);

			if (!evaluate_expr(interp, lhs, type_id(lhs->type_id), &base_address))
				TODO("Addressing deferred pointers is not yet implemented");

			pop_temporary(interp);

			stride = type_metrics_from_id(interp->types, ptr_type->referenced_type_id).stride;

			index_limit = UINT64_MAX;
		}
		else
		{
			ASSERT_OR_IGNORE(lhs_type_tag == TypeTag::Slice);

			const ReferenceType* const slice_type = static_cast<const ReferenceType*>(simple_type_structure_from_id(interp->types, lhs_type_id));

			void* slice_value_ptr;

			if (!evaluate_expr(interp, lhs, type_id(lhs->type_id), &slice_value_ptr))
				TODO("Addressing deferred slices is not yet implemented.");

			MutRange<byte> slice_value = *static_cast<MutRange<byte>*>(slice_value_ptr);

			pop_temporary(interp);
			
			base_address = slice_value.begin();

			stride = type_metrics_from_id(interp->types, slice_type->referenced_type_id).stride;

			const u64 slice_bytes = (reinterpret_cast<u64>(slice_value.end()) - reinterpret_cast<u64>(slice_value.begin()));

			ASSERT_OR_IGNORE(slice_bytes % stride == 0);

			index_limit = slice_bytes / stride;
		}

		AstNode* const rhs = next_sibling_of(lhs);

		void* index_ptr;

		if (!evaluate_expr(interp, rhs, type_id(rhs->type_id), &index_ptr))
			TODO("Addressing deferred index is not yet implemented.");

		u64 index;
		
		if (!any_integer_to_u64(interp, type_id(rhs->type_id), index_ptr, &index))
			source_error(interp->errors, rhs->source_id, "Index must fit into unsigned 64-bit integer.\n");

		pop_temporary(interp);

		if (index >= index_limit)
			source_error(interp->errors, node->source_id, "Index %" PRIu64 " exceeds maximum of %" PRIu64 ".\n", index, index_limit);

		u64 byte_offset;

		u64 address;

		if (!mul_checked(stride, index, &byte_offset) || add_checked(reinterpret_cast<u64>(base_address), byte_offset, &address))
			source_error(interp->errors, node->source_id, "Address calculation overflowed.\n");

		return reinterpret_cast<void*>(address);
	}

	case AstTag::Builtin:
	case AstTag::CompositeInitializer:
	case AstTag::ArrayInitializer:
	case AstTag::Wildcard:
	case AstTag::Where:
	case AstTag::Expects:
	case AstTag::Ensures:
	case AstTag::Definition:
	case AstTag::Block:
	case AstTag::If:
	case AstTag::For:
	case AstTag::ForEach:
	case AstTag::Switch:
	case AstTag::Func:
	case AstTag::Trait:
	case AstTag::Impl:
	case AstTag::Catch:
	case AstTag::LitInteger:
	case AstTag::LitFloat:
	case AstTag::LitChar:
	case AstTag::LitString:
	case AstTag::Return:
	case AstTag::Leave:
	case AstTag::Yield:
	case AstTag::Call:
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
	case AstTag::OpCmpEQ:
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
		source_error(interp->errors, node->source_id, "Addressation of AST node type %s is not yet implemented (%s:%u).\n", tag_name(node->tag), __FILE__, __LINE__);

	case AstTag::INVALID:
	case AstTag::MAX:
	case AstTag::File:
	case AstTag::Case:
	case AstTag::ParameterList:
		; // fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}
*/
