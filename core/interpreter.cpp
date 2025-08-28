#include "core.hpp"

#include "../diag/diag.hpp"
#include "../infra/container.hpp"

enum class ArecKind
{
	INVALID = 0,
	Normal,
	Unbound,
};

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

	u32 size : 30;

	u32 kind : 2;

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
	alignas(8) byte attachment[];
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

using BuiltinFunc = void (*) (Interpreter* interp, Arec* arec, AstNode* call_node, MutRange<byte> into) noexcept;

// Representation of a callable, meaning either a builtin or a user-defined
// function or procedure.
struct alignas(8) CallableValue
{
	#if COMPILER_CLANG
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Wnested-anon-types" // anonymous types declared in an anonymous union are an extension
	#pragma clang diagnostic ignored "-Wgnu-anonymous-struct" // anonymous structs are a GNU extension
	#elif COMPILER_GCC
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wpedantic" // ISO C++ prohibits anonymous structs
	#endif
	union
	{
		struct
		{
			u32 is_builtin : 1;

			u32 unused_ : 31;
		};

		struct
		{
			u32 is_builtin : 1;

			u32 ordinal : 31;
		} builtin;

		struct
		{
			u32 is_builtin : 1;

			u32 ast_node_id_bits : 31;
		} function;
	};
	#if COMPILER_CLANG
	#pragma clang diagnostic pop
	#elif COMPILER_GCC
	#pragma GCC diagnostic pop
	#endif

	TypeId signature_type_id;

	static CallableValue from_builtin(TypeId builtin_type_id, u8 ordinal) noexcept
	{
		return { builtin_type_id, ordinal };
	}

	static CallableValue from_function(TypeId signature_type_id, AstNodeId ast_node_id) noexcept
	{
		return { signature_type_id, ast_node_id };
	}

	CallableValue() noexcept = default;

private:

	CallableValue(TypeId signature_type_id, u8 ordinal) noexcept :
		builtin{ true, ordinal },
		signature_type_id{ signature_type_id }
	{}

	CallableValue(TypeId signature_type_id, AstNodeId ast_node_id) noexcept :
		function{ false, static_cast<u32>(ast_node_id) },
		signature_type_id{ signature_type_id }
	{}
};

// Utility for creating built-in functions types.
struct BuiltinParamInfo
{
	IdentifierId name;

	TypeId type;

	bool is_comptime_known;
};

enum class EvalTag : u8
{
	Success,
	Unbound,
};

struct EvalSpec
{
	EvalTag tag;

	#if COMPILER_CLANG
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Wnested-anon-types" // anonymous types declared in an anonymous union are an extension
	#elif COMPILER_GCC
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wpedantic" // ISO C++ prohibits anonymous structs
	#endif
	union
	{
		struct
		{
			MutRange<byte> location;

			TypeId type_id;

			ValueKind kind;
		} success;

		struct
		{
			Arec* source;
		} unbound;
	};
	#if COMPILER_CLANG
	#pragma clang diagnostic pop
	#elif COMPILER_GCC
	#pragma GCC diagnostic pop
	#endif

	EvalSpec() noexcept : tag{}, success{} {}

	EvalSpec(ValueKind kind) noexcept :
		tag{ EvalTag::Success },
		success{ {}, TypeId::INVALID, kind }
	{}

	EvalSpec(ValueKind kind, MutRange<byte> location) noexcept :
		tag{ EvalTag::Success },
		success{ location, TypeId::INVALID, kind }
	{}

	EvalSpec(ValueKind kind, MutRange<byte> location, TypeId type_id) noexcept :
		tag{ EvalTag::Success },
		success{ location, type_id, kind }
	{}

	EvalSpec(Arec* unbound_source) noexcept :
		tag{ EvalTag::Unbound },
		unbound{ unbound_source }
	{}
};

enum class IdentifierInfoTag : u8
{
	Found,
	Unbound,
	Missing,
};

struct IdentifierInfo
{
	IdentifierInfoTag tag;

	#if COMPILER_CLANG
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Wnested-anon-types" // anonymous types declared in an anonymous union are an extension
	#elif COMPILER_GCC
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wpedantic" // ISO C++ prohibits anonymous structs
	#endif
	union
	{
		struct
		{
			MutRange<byte> location;

			TypeId type_id;

			bool is_mut;
		} found;

		struct
		{
			Arec* source;
		} unbound;
	};
	#if COMPILER_CLANG
	#pragma clang diagnostic pop
	#elif COMPILER_GCC
	#pragma GCC diagnostic pop
	#endif

	IdentifierInfo() noexcept : tag{}, found{} {}

	static IdentifierInfo make_found(MutRange<byte> location, TypeId type_id, bool is_mut) noexcept
	{
		IdentifierInfo info;
		info.tag = IdentifierInfoTag::Found;
		info.found.location = location;
		info.found.type_id = type_id;
		info.found.is_mut = is_mut;

		return info;
	}

	static IdentifierInfo make_unbound(Arec* source) noexcept
	{
		IdentifierInfo info;
		info.tag = IdentifierInfoTag::Unbound;
		info.unbound.source = source;

		return info;
	}

	static IdentifierInfo make_missing() noexcept
	{
		IdentifierInfo info;
		info.tag = IdentifierInfoTag::Missing;

		return info;
	}
};

struct CallInfo
{
	TypeId return_type_id;

	ArecId parameter_list_arec_id;
};

struct PeekablePartialValueIterator
{
	PartialValueIterator it;

	PartialValue curr;
};

struct Interpreter
{
	SourceReader* reader;

	Parser* parser;

	TypePool* types;

	AstPool* asts;

	IdentifierPool* identifiers;

	GlobalValuePool* globals;

	PartialValuePool* partials;

	ErrorSink* errors;

	ReservedVec<u64> arecs;

	ArecId top_arec_id;

	ArecId active_arec_id;

	ReservedVec<byte> temps;

	ReservedVec<PartialValueBuilderId> partial_value_builders;

	ReservedVec<PeekablePartialValueIterator> active_partial_values;

	TypeId prelude_type_id;

	TypeId builtin_type_ids[static_cast<u8>(Builtin::MAX)];

	BuiltinFunc builtin_values[static_cast<u8>(Builtin::MAX)];

	minos::FileHandle log_file;

	bool log_prelude;

	MutRange<byte> memory;
};





static EvalSpec evaluate(Interpreter* interp, AstNode* node, EvalSpec into) noexcept;

static TypeId typeinfer(Interpreter* interp, AstNode* node) noexcept;



static void copy_loc(MutRange<byte> dst, Range<byte> src) noexcept
{
	ASSERT_OR_IGNORE(dst.count() == src.count());

	memcpy(dst.begin(), src.begin(), dst.count());
}

template<typename T>
static T load_loc(MutRange<byte> src) noexcept
{
	ASSERT_OR_IGNORE(src.count() == sizeof(T));

	return *reinterpret_cast<T*>(src.begin());
}

template<typename T>
static void store_loc(MutRange<byte> dst, T src) noexcept
{
	ASSERT_OR_IGNORE(dst.count() == sizeof(T));

	memcpy(dst.begin(), &src, sizeof(T));
}



static ArecRestoreInfo set_active_arec_id(Interpreter* interp, ArecId arec_id) noexcept
{
	ASSERT_OR_IGNORE(arec_id != ArecId::INVALID && arec_id <= interp->top_arec_id);

	const ArecId old_selected = interp->active_arec_id;

	const u32 old_used = interp->arecs.used();

	interp->active_arec_id = arec_id;

	return { old_selected, old_used };
}

static Arec* active_arec(Interpreter* interp) noexcept
{
	ASSERT_OR_IGNORE(interp->active_arec_id != ArecId::INVALID);

	return reinterpret_cast<Arec*>(interp->arecs.begin() + static_cast<u32>(interp->active_arec_id));
}

static ArecId active_arec_id(Interpreter* interp) noexcept
{
	ASSERT_OR_IGNORE(interp->active_arec_id != ArecId::INVALID);

	return interp->active_arec_id;
}

static TypeId active_arec_type_id(Interpreter* interp) noexcept
{
	return active_arec(interp)->type_id;
}

static void arec_restore(Interpreter* interp, ArecRestoreInfo info) noexcept
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

static ArecId arec_push(Interpreter* interp, TypeId record_type_id, u64 size, u32 align, ArecId lookup_parent, ArecKind kind) noexcept
{
	ASSERT_OR_IGNORE(type_tag_from_id(interp->types, record_type_id) == TypeTag::Composite);

	ASSERT_OR_IGNORE(kind != ArecKind::INVALID);

	if (size >= (static_cast<u64>(1) << 31))
		panic("Arec too large.\n");

	if (align > 8)
		TODO("Implement overaligned Arecs");

	Arec* const arec = static_cast<Arec*>(interp->arecs.reserve_padded(static_cast<u32>(sizeof(Arec) + size)));
	arec->prev_top_id = interp->top_arec_id;
	arec->surrounding_arec_id = lookup_parent;
	arec->type_id = record_type_id;
	arec->size = static_cast<u32>(size);
	arec->kind = static_cast<u32>(kind);

	const ArecId arec_id = static_cast<ArecId>(reinterpret_cast<const u64*>(arec) - interp->arecs.begin());

	interp->top_arec_id = arec_id;

	ASSERT_OR_IGNORE(lookup_parent == ArecId::INVALID || interp->active_arec_id == lookup_parent);

	interp->active_arec_id = arec_id;

	return arec_id;
}

static void arec_pop(Interpreter* interp, ArecId arec_id) noexcept
{
	ASSERT_OR_IGNORE(arec_id != ArecId::INVALID && interp->top_arec_id == arec_id && interp->active_arec_id == arec_id);

	const Arec* const popped = reinterpret_cast<Arec*>(interp->arecs.begin() + static_cast<s32>(arec_id));

	interp->active_arec_id = popped->surrounding_arec_id == ArecId::INVALID ? popped->prev_top_id : popped->surrounding_arec_id;

	interp->top_arec_id = popped->prev_top_id;

	interp->arecs.pop_to(static_cast<u32>(arec_id));
}

static void arec_grow(Interpreter* interp, ArecId arec_id, u64 new_size) noexcept
{
	ASSERT_OR_IGNORE(arec_id != ArecId::INVALID && interp->top_arec_id == arec_id);

	if (new_size >= (static_cast<u64>(1) << 31))
		panic("Arec too large.\n");

	Arec* const arec = reinterpret_cast<Arec*>(interp->arecs.begin() + static_cast<s32>(arec_id));

	ASSERT_OR_IGNORE(reinterpret_cast<u64>(interp->arecs.end()) == (reinterpret_cast<u64>(arec->attachment + arec->size + 7) & ~static_cast<u64>(7)));

	ASSERT_OR_IGNORE(arec->size <= new_size);

	arec->size = new_size;
}



static void push_partial_value_builder(Interpreter* interp, AstNode* root) noexcept
{
	const PartialValueBuilderId builder_id = create_partial_value_builder(interp->partials, root);

	interp->partial_value_builders.append(builder_id);
}

static PartialValueId pop_partial_value_builder(Interpreter* interp) noexcept
{
	ASSERT_OR_IGNORE(interp->partial_value_builders.used() != 0);

	const PartialValueBuilderId builder_id = interp->partial_value_builders.end()[-1];

	interp->partial_value_builders.pop_by(1);

	return complete_partial_value_builder(interp->partials, builder_id);
}

static MutRange<byte> add_partial_value_to_builder(Interpreter* interp, AstNode* node, TypeId type_id, u64 size, u32 align) noexcept
{
	ASSERT_OR_IGNORE(interp->partial_value_builders.used() != 0);

	const PartialValueBuilderId builder_id = interp->partial_value_builders.end()[-1];

	return partial_value_builder_add_value(interp->partials, builder_id, node, type_id, size, align);
}

static void push_partial_value(Interpreter* interp, PartialValueId id) noexcept
{
	PeekablePartialValueIterator* const it = interp->active_partial_values.reserve();

	it->it = values_of(interp->partials, id);

	if (has_next(&it->it))
		it->curr = next(&it->it);
	else
		it->curr.node = nullptr;
}

static void pop_partial_value(Interpreter* interp) noexcept
{
	interp->active_partial_values.pop_by(1);
}

static bool has_applicable_partial_value(Interpreter* interp, AstNode* node, PartialValue* out) noexcept
{
	if (interp->active_partial_values.used() == 0)
		return false;

	PeekablePartialValueIterator* const it = &interp->active_partial_values.end()[-1];

	if (it->curr.node != node)
		return false;

	*out = it->curr;

	if (has_next(&it->it))
		it->curr = next(&it->it);

	return true;
}



static MutRange<byte> stack_push(Interpreter* interp, u64 size, u32 align) noexcept
{
	if (size > UINT32_MAX)
		panic("Exceeded maximum size of %u bytes for interpreter stack allocation.\n", UINT32_MAX);

	interp->temps.pad_to_alignment(align);

	return { static_cast<byte*>(interp->temps.reserve_exact(static_cast<u32>(size))), size };
}

static u32 stack_mark(Interpreter* interp) noexcept
{
	return interp->temps.used();
}

static void stack_shrink(Interpreter* interp, u32 mark) noexcept
{
	interp->temps.pop_to(mark);
}



static void complete_member(Interpreter* interp, TypeId surrounding_type_id, const Member* member) noexcept
{
	if (!member->has_pending_type && !member->has_pending_value)
		return;

	const u32 mark = stack_mark(interp);

	TypeId member_type_id = TypeId::INVALID;

	GlobalValueId member_value_id = GlobalValueId::INVALID;

	if (!member->has_pending_type)
	{
		member_type_id = member->type.complete;
	}
	else if (member->type.pending != AstNodeId::INVALID)
	{
		const ArecRestoreInfo restore = set_active_arec_id(interp, member->type_completion_arec_id);

		AstNode* const type = ast_node_from_id(interp->asts, member->type.pending);

		const EvalSpec type_spec = evaluate(interp, type, EvalSpec{
			ValueKind::Value,
			range::from_object_bytes_mut(&member_type_id),
			simple_type(interp->types, TypeTag::Type, {})
		});

		ASSERT_OR_IGNORE(type_spec.tag == EvalTag::Success && member_type_id != TypeId::INVALID);

		arec_restore(interp, restore);
	}

	if (member->has_pending_value)
	{
		AstNode* const value = ast_node_from_id(interp->asts, member->value.pending);

		const ArecRestoreInfo restore = set_active_arec_id(interp, member->value_completion_arec_id);

		if (member_type_id != TypeId::INVALID)
		{
			const TypeMetrics metrics = type_metrics_from_id(interp->types, member_type_id);

			member_value_id = alloc_global_value(interp->globals, metrics.size, metrics.align);

			const EvalSpec value_spec = evaluate(interp, value, EvalSpec{
				ValueKind::Value,
				global_value_get_mut(interp->globals, member_value_id),
				member_type_id
			});

			ASSERT_OR_IGNORE(value_spec.tag == EvalTag::Success);
		}
		else
		{
			const EvalSpec value_spec = evaluate(interp, value, EvalSpec{ ValueKind::Value });

			ASSERT_OR_IGNORE(value_spec.tag == EvalTag::Success);

			const TypeMetrics metrics = type_metrics_from_id(interp->types, value_spec.success.type_id);

			member_type_id = value_spec.success.type_id;

			member_value_id = alloc_global_value(interp->globals, metrics.size, metrics.align);

			copy_loc(global_value_get_mut(interp->globals, member_value_id), value_spec.success.location.immut());
		}

		arec_restore(interp, restore);
	}

	stack_shrink(interp, mark);

	set_incomplete_type_member_info_by_rank(interp->types, surrounding_type_id, member->rank, MemberCompletionInfo{
		member->has_pending_type,
		member->has_pending_value,
		member_type_id,
		member_value_id
	});
}



static IdentifierInfo location_info_from_global_member(Interpreter* interp, TypeId surrounding_type_id, const Member* member) noexcept
{
	complete_member(interp, surrounding_type_id, member);

	return IdentifierInfo::make_found(
		global_value_get_mut(interp->globals, member->value.complete),
		member->type.complete,
		member->is_mut
	);
}

static IdentifierInfo location_info_from_arec_and_member(Interpreter* interp, Arec* arec, const Member* member) noexcept
{
	if (member->is_global)
		return location_info_from_global_member(interp, arec->type_id, member);

	const u64 size = type_metrics_from_id(interp->types, member->type.complete).size;

	if (size > UINT32_MAX)
		source_error(interp->errors, member->source, "Size of stack-based location must not exceed 2^32 - 1 bytes.\n");

	const ArecKind kind = static_cast<ArecKind>(arec->kind);

	if (kind == ArecKind::Unbound)
		return IdentifierInfo::make_unbound(arec);

	return IdentifierInfo::make_found(
		MutRange<byte>{ arec->attachment + member->offset, static_cast<u32>(size) },
		member->type.complete,
		member->is_mut);
}

static bool lookup_identifier_arec_and_member(Interpreter* interp, IdentifierId name, SourceId source, OptPtr<Arec>* out_arec, TypeId* out_surrounding_type_id, const Member** out_member) noexcept
{
	ASSERT_OR_IGNORE(interp->active_arec_id != ArecId::INVALID);

	ArecId curr = interp->active_arec_id;

	Arec* arec = arec_from_id(interp, curr);

	while (true)
	{
		if (type_member_by_name(interp->types, arec->type_id, name, source, out_member))
		{
			*out_arec = some(arec);

			*out_surrounding_type_id = arec->type_id;

			return true;
		}

		if (arec->surrounding_arec_id == ArecId::INVALID)
			break;

		curr = arec->surrounding_arec_id;

		arec = arec_from_id(interp, curr);
	}

	TypeId lex_scope = lexical_parent_type_from_id(interp->types, arec->type_id);

	while (lex_scope != TypeId::INVALID)
	{
		if (type_member_by_name(interp->types, lex_scope, name, source, out_member))
		{
			*out_arec = none<Arec>();

			*out_surrounding_type_id = lex_scope;

			return true;
		}

		lex_scope = lexical_parent_type_from_id(interp->types, lex_scope);
	}

	return false;	
}

static IdentifierInfo lookup_local_identifier(Interpreter* interp, Arec* arec, IdentifierId name, SourceId source) noexcept
{
	const Member* member;

	if (!type_member_by_name(interp->types, arec->type_id, name, source, &member))
		ASSERT_UNREACHABLE;

	return location_info_from_arec_and_member(interp, arec, member);
}

static IdentifierInfo lookup_identifier(Interpreter* interp, IdentifierId name, SourceId source) noexcept
{
	OptPtr<Arec> arec;

	TypeId surrounding_type_id;

	const Member* member;

	if (!lookup_identifier_arec_and_member(interp, name, source, &arec, &surrounding_type_id, &member))
		ASSERT_UNREACHABLE;

	if (is_some(arec))
	{
		const ArecKind kind = static_cast<ArecKind>(get_ptr(arec)->kind);

		if (kind == ArecKind::Unbound)
			return IdentifierInfo::make_unbound(get_ptr(arec));

		return location_info_from_arec_and_member(interp, get_ptr(arec), member);
	}
	else if (member->is_global)
	{
		return location_info_from_global_member(interp, surrounding_type_id, member);
	}

	return IdentifierInfo::make_missing();
}



static Member delayed_member_from(Interpreter* interp, AstNode* definition) noexcept
{
	ASSERT_OR_IGNORE(definition->tag == AstTag::Definition || definition->tag == AstTag::Parameter);

	const AstDefinitionData attach = definition->tag == AstTag::Definition
		? *attachment_of<AstDefinitionData>(definition)
		: *reinterpret_cast<AstDefinitionData*>(attachment_of<AstParameterData>(definition));

	const DefinitionInfo info = get_definition_info(definition);

	Member member{};
	member.name = attach.identifier_id;
	member.source = source_id_of(interp->asts, definition);
	member.type.pending = is_some(info.type) ? id_from_ast_node(interp->asts, get_ptr(info.type)) : AstNodeId::INVALID;
	member.value.pending = is_some(info.value) ? id_from_ast_node(interp->asts, get_ptr(info.value)) : AstNodeId::INVALID;
	member.is_global = has_flag(definition, AstFlag::Definition_IsGlobal);
	member.is_pub = has_flag(definition, AstFlag::Definition_IsPub);
	member.is_use = has_flag(definition, AstFlag::Definition_IsUse);
	member.is_mut = has_flag(definition, AstFlag::Definition_IsMut);
	member.is_param = false;
	member.has_pending_type = true;
	member.has_pending_value = is_some(info.value);
	member.rank = 0;
	member.type_completion_arec_id = active_arec_id(interp);
	member.value_completion_arec_id = active_arec_id(interp);
	member.offset = 0;

	return member;
}

static void convert_comp_integer_to_integer(Interpreter* interp, const AstNode* error_source, EvalSpec dst, CompIntegerValue src_value) noexcept
{
	ASSERT_OR_IGNORE(dst.tag == EvalTag::Success
	              && dst.success.kind == ValueKind::Value
	              && type_tag_from_id(interp->types, dst.success.type_id) == TypeTag::Integer);

	const NumericType dst_type = *static_cast<const NumericType*>(simple_type_structure_from_id(interp->types, dst.success.type_id));

	if (dst_type.is_signed)
	{
		s64 signed_value;

		if (!s64_from_comp_integer(src_value, static_cast<u8>(dst_type.bits), &signed_value))
			source_error(interp->errors, source_id_of(interp->asts, error_source), "Value of integer literal exceeds bounds of signed %u-bit integer.\n", dst_type.bits);

		const Range<byte> value_range = Range<byte>{ reinterpret_cast<byte*>(&signed_value), static_cast<u64>((dst_type.bits + 7) / 8) };

		copy_loc(dst.success.location, value_range);
	}
	else
	{
		u64 unsigned_value;

		if (!u64_from_comp_integer(src_value, static_cast<u8>(dst_type.bits), &unsigned_value))
			source_error(interp->errors, source_id_of(interp->asts, error_source), "Value of integer literal exceeds bounds of unsigned %u-bit integer.\n", dst_type.bits);

		const Range<byte> value_range = Range<byte>{ reinterpret_cast<byte*>(&unsigned_value), static_cast<u64>((dst_type.bits + 7) / 8) };

		copy_loc(dst.success.location, value_range);
	}
}

static void convert_comp_float_to_float(Interpreter* interp, EvalSpec dst, CompFloatValue src_value) noexcept
{
	ASSERT_OR_IGNORE(dst.tag == EvalTag::Success
	              && dst.success.kind == ValueKind::Value
	              && type_tag_from_id(interp->types, dst.success.type_id) == TypeTag::Integer);

	const NumericType dst_type = *static_cast<const NumericType*>(simple_type_structure_from_id(interp->types, dst.success.type_id));

	ASSERT_OR_IGNORE(!dst_type.is_signed);

	if (dst_type.bits == 32)
	{
		store_loc(dst.success.location, f32_from_comp_float(src_value));
	}
	else
	{
		ASSERT_OR_IGNORE(dst_type.bits == 64);

		store_loc(dst.success.location, f64_from_comp_float(src_value));
	}
}

static void convert_array_to_slice(Interpreter* interp, const AstNode* error_source, EvalSpec dst, EvalSpec src) noexcept
{
	ASSERT_OR_IGNORE(
		dst.tag == EvalTag::Success &&
		dst.success.type_id != TypeId::INVALID &&
		dst.success.location.begin() != nullptr &&
		dst.success.location.count() == sizeof(MutRange<byte>) &&
		type_tag_from_id(interp->types, dst.success.type_id) == TypeTag::Slice &&
		src.tag == EvalTag::Success &&
		src.success.type_id != TypeId::INVALID &&
		src.success.location.begin() != nullptr &&
		type_tag_from_id(interp->types, src.success.type_id) == TypeTag::Array
	);

	const ArrayType src_type = *static_cast<const ArrayType*>(simple_type_structure_from_id(interp->types, src.success.type_id));

	const ReferenceType dst_type = *static_cast<const ReferenceType*>(simple_type_structure_from_id(interp->types, dst.success.type_id));

	if (!is_same_type(interp->types, src_type.element_type, dst_type.referenced_type_id))
		source_error(interp->errors, source_id_of(interp->asts, error_source), "Cannot implicitly convert array to slice with different element type");

	store_loc(dst.success.location, MutRange<byte>{ src.success.location.begin(), src_type.element_count });
}

static void convert(Interpreter* interp, const AstNode* error_source, EvalSpec dst, EvalSpec src) noexcept
{
	ASSERT_OR_IGNORE(
		dst.tag == EvalTag::Success &&
		dst.success.type_id != TypeId::INVALID &&
		dst.success.location.begin() != nullptr &&
		src.tag == EvalTag::Success &&
		src.success.type_id != TypeId::INVALID &&
		src.success.location.begin() != nullptr
	);

	const TypeTag src_type_tag = type_tag_from_id(interp->types, src.success.type_id);

	if (dst.success.kind == ValueKind::Value && src.success.kind == ValueKind::Location)
		src.success.location = load_loc<MutRange<byte>>(src.success.location);

	switch (src_type_tag)
	{
	case TypeTag::CompInteger:
	{
		convert_comp_integer_to_integer(interp, error_source, dst, load_loc<CompIntegerValue>(src.success.location));

		return;
	}

	case TypeTag::CompFloat:
	{
		convert_comp_float_to_float(interp, dst, load_loc<CompFloatValue>(src.success.location));

		return;
	}

	case TypeTag::Array:
	{
		convert_array_to_slice(interp, error_source, dst, src);

		return;
	}

	case TypeTag::INVALID:
	case TypeTag::Void:
	case TypeTag::Type:
	case TypeTag::Definition:
	case TypeTag::Integer:
	case TypeTag::Float:
	case TypeTag::Boolean:
	case TypeTag::Slice:
	case TypeTag::Ptr:
	case TypeTag::Func:
	case TypeTag::Builtin:
	case TypeTag::Composite:
	case TypeTag::CompositeLiteral:
	case TypeTag::ArrayLiteral:
	case TypeTag::TypeBuilder:
	case TypeTag::Variadic:
	case TypeTag::Divergent:
	case TypeTag::Trait:
	case TypeTag::TypeInfo:
	case TypeTag::TailArray:
		; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

static EvalSpec evaluate_global_member(Interpreter* interp, AstNode* node, EvalSpec into, TypeId surrounding_type_id, const Member* member) noexcept
{
	ASSERT_OR_IGNORE(member->is_global);

	complete_member(interp, surrounding_type_id, member);

	const TypeMetrics metrics = type_metrics_from_id(interp->types, member->type.complete);

	if (into.success.type_id == TypeId::INVALID)
	{
		into.success.type_id = member->type.complete;

		if (into.success.location.begin() == nullptr)
		{
			if (into.success.kind == ValueKind::Value)
			{
				into.success.location = stack_push(interp, metrics.size, metrics.align);
			}
			else
			{
				into.success.location = stack_push(interp, sizeof(MutRange<byte>), alignof(MutRange<byte>));
			}
		}
	}
	else if (!type_can_implicitly_convert_from_to(interp->types, member->type.complete, into.success.type_id))
	{
		const Range<char8> name = identifier_name_from_id(interp->identifiers, attachment_of<AstMemberData>(node)->identifier_id);

		source_error(interp->errors, source_id_of(interp->asts, node), "Cannot convert type of member `%.*s` to the desired type.\n", static_cast<s32>(name.count()), name.begin());
	}

	MutRange<byte> value = global_value_get_mut(interp->globals, member->value.complete);

	if (is_same_type(interp->types, into.success.type_id, member->type.complete))
	{
		if (into.success.kind == ValueKind::Location)
		{
			store_loc(into.success.location, value);
		}
		else
		{
			copy_loc(into.success.location, value.immut());
		}
	}
	else if (into.success.kind == ValueKind::Value)
	{
		convert(interp, node, into, EvalSpec{ ValueKind::Location, value, member->type.complete });
	}
	else
	{
		const Range<char8> name = identifier_name_from_id(interp->identifiers, attachment_of<AstMemberData>(node)->identifier_id);

		source_error(interp->errors, source_id_of(interp->asts, node), "Cannot use member `%.*s` as a location of the desired type, as it requires implicit conversion.\n", static_cast<s32>(name.count()), name.begin());
	}

	return into;
}

static EvalSpec evaluate_local_member(Interpreter* interp, AstNode* node, EvalSpec into, TypeId surrounding_type_id, const Member* member, MutRange<byte> lhs_value) noexcept
{
	ASSERT_OR_IGNORE(!member->is_global);

	complete_member(interp, surrounding_type_id, member);

	const TypeMetrics metrics = type_metrics_from_id(interp->types, member->type.complete);

	if (into.success.type_id == TypeId::INVALID)
	{
		into.success.type_id = member->type.complete;

		if (into.success.location.begin() == nullptr)
		{
			if (into.success.kind == ValueKind::Value)
			{
				into.success.location = stack_push(interp, metrics.size, metrics.align);
			}
			else
			{
				into.success.location = stack_push(interp, sizeof(MutRange<byte>), alignof(MutRange<byte>));
			}
		}
	}
	else if (!type_can_implicitly_convert_from_to(interp->types, member->type.complete, into.success.type_id))
	{
		const Range<char8> name = identifier_name_from_id(interp->identifiers, attachment_of<AstMemberData>(node)->identifier_id);

		source_error(interp->errors, source_id_of(interp->asts, node), "Cannot convert type of member `%.*s` to the desired type.\n", static_cast<s32>(name.count()), name.begin());
	}

	if (is_same_type(interp->types, into.success.type_id, member->type.complete))
	{
		if (into.success.kind == ValueKind::Location)
		{
			store_loc(into.success.location, MutRange<byte>{ lhs_value.begin() + member->offset, metrics.size });
		}
		else
		{
			copy_loc(into.success.location, Range<byte>{ lhs_value.begin() + member->offset, metrics.size });
		}
	}
	else if (into.success.kind == ValueKind::Value)
	{
		convert(interp, node, into, EvalSpec{ ValueKind::Location, lhs_value, member->type.complete });
	}
	else
	{
		const Range<char8> name = identifier_name_from_id(interp->identifiers, attachment_of<AstMemberData>(node)->identifier_id);

		source_error(interp->errors, source_id_of(interp->asts, node), "Cannot use member `%.*s` as a location of the desired type, as it requires implicit conversion.\n", static_cast<s32>(name.count()), name.begin());
	}

	return into;
}

static CallInfo setup_call_args(Interpreter* interp, const SignatureType* signature_type, AstNode* callee) noexcept
{
	if (signature_type->parameter_list_is_unbound || signature_type->return_type_is_unbound)
		push_partial_value(interp, signature_type->partial_value_id);

	const TypeId parameter_list_type_id = signature_type->parameter_list_is_unbound
		? copy_incomplete_type(interp->types, signature_type->parameter_list_type_id)
		: signature_type->parameter_list_type_id;

	const ArecId caller_arec_id = active_arec_id(interp);

	const TypeMetrics parameter_list_metrics = type_metrics_from_id(interp->types, parameter_list_type_id);

	const ArecId parameter_list_arec_id = arec_push(interp, parameter_list_type_id, parameter_list_metrics.size, parameter_list_metrics.align, ArecId::INVALID, ArecKind::Normal);

	Arec* const parameter_list_arec = arec_from_id(interp, parameter_list_arec_id);

	const ArecRestoreInfo restore = set_active_arec_id(interp, caller_arec_id);

	u16 arg_rank = 0;

	AstNode* arg = callee;

	while (has_next_sibling(arg))
	{
		arg = next_sibling_of(arg);

		if (arg->tag == AstTag::OpSet)
			TODO("Implement named arguments");

		const Member* const param = type_member_by_rank(interp->types, parameter_list_type_id, arg_rank);

		const bool has_pending_type = param->has_pending_type;

		if (has_pending_type)
			complete_member(interp, parameter_list_type_id, param);

		const TypeMetrics param_metrics = type_metrics_from_id(interp->types, param->type.complete);

		if (has_pending_type)
			arec_grow(interp, parameter_list_arec_id, param->offset + param_metrics.size);

		const EvalSpec arg_spec = evaluate(interp, arg, EvalSpec{
			ValueKind::Value,
			MutRange<byte>{ parameter_list_arec->attachment + param->offset, param_metrics.size },
			param->type.complete
		});

		if (arg_spec.tag == EvalTag::Unbound)
			TODO("Implement unbound late-bound arguments (?)");

		arg_rank += 1;
	}

	arec_restore(interp, restore);

	TypeId return_type_id;

	if (signature_type->return_type_is_unbound)
	{
		AstNode* const return_type = ast_node_from_id(interp->asts, signature_type->return_type.partial_root);

		EvalSpec return_type_spec = evaluate(interp, return_type, EvalSpec{
			ValueKind::Value,
			range::from_object_bytes_mut(&return_type_id),
			simple_type(interp->types, TypeTag::Type, {})
		});

		if (return_type_spec.tag == EvalTag::Unbound)
			TODO("Implement unbound late-bound return types (?)");
	}
	else
	{
		return_type_id = signature_type->return_type.complete;
	}

	if (signature_type->parameter_list_is_unbound || signature_type->return_type_is_unbound)
		pop_partial_value(interp);

	return { return_type_id, parameter_list_arec_id };
}

static EvalSpec evaluate(Interpreter* interp, AstNode* node, EvalSpec into) noexcept
{
	ASSERT_OR_IGNORE(into.tag == EvalTag::Success);

	PartialValue applicable_partial;

	if (has_applicable_partial_value(interp, node, &applicable_partial))
	{
		ASSERT_OR_IGNORE(into.success.kind == ValueKind::Value);

		if (into.success.type_id == TypeId::INVALID)
		{
			into.success.type_id = applicable_partial.type_id;

			if (into.success.location.begin() == nullptr)
			{
				const TypeMetrics metrics = type_metrics_from_id(interp->types, applicable_partial.type_id);

				into.success.location = stack_push(interp, metrics.size, metrics.align);
			}
		}
		else if (!type_can_implicitly_convert_from_to(interp->types, applicable_partial.type_id, into.success.type_id))
		{
			source_error(interp->errors, source_id_of(interp->asts, node), "Cannot convert previous partial value to desired type.\n");
		}

		if (is_same_type(interp->types, applicable_partial.type_id, into.success.type_id))
		{
			copy_loc(into.success.location, applicable_partial.data);
		}
		else
		{
			convert(interp, node, into, EvalSpec{
				ValueKind::Value,
				{ const_cast<byte*>(applicable_partial.data.begin()), applicable_partial.data.count() },
				applicable_partial.type_id
			});
		}

		return into;
	}
	else if (into.success.type_id != TypeId::INVALID && type_tag_from_id(interp->types, into.success.type_id) == TypeTag::TypeInfo)
	{
		ASSERT_OR_IGNORE(into.success.kind == ValueKind::Value);

		const TypeId type_type_id = simple_type(interp->types, TypeTag::Type, {});

		if (into.success.type_id == TypeId::INVALID)
		{
			into.success.type_id = type_type_id;

			if (into.success.location.begin() == nullptr)
				into.success.location = stack_push(interp, sizeof(TypeId), alignof(TypeId));
		}
		else if (!type_can_implicitly_convert_from_to(interp->types, type_type_id, into.success.type_id))
		{
			source_error(interp->errors, source_id_of(interp->asts, node), "Cannot implicitly convert meta-type to desired type.\n");
		}

		store_loc(into.success.location, typeinfer(interp, node));

		return into;
	}
	else switch (node->tag)
	{
	case AstTag::Builtin:
	{
		ASSERT_OR_IGNORE(into.success.kind == ValueKind::Value);

		const u8 ordinal = static_cast<u8>(node->flags);

		ASSERT_OR_IGNORE(ordinal < array_count(interp->builtin_type_ids));

		const TypeId builtin_type_id = interp->builtin_type_ids[ordinal];

		if (into.success.type_id == TypeId::INVALID)
		{
			into.success.type_id = builtin_type_id;

			if (into.success.location.begin() == nullptr)
				into.success.location = stack_push(interp, sizeof(CallableValue), alignof(CallableValue));
		}
		else if (!is_same_type(interp->types, builtin_type_id, into.success.type_id))
		{
			source_error(interp->errors, source_id_of(interp->asts, node), "Cannot convert builtin function to desired type.\n");
		}

		store_loc(into.success.location, CallableValue::from_builtin(builtin_type_id, ordinal));

		return into;
	}

	case AstTag::Definition:
	{
		ASSERT_OR_IGNORE(into.success.kind == ValueKind::Value);

		const TypeId definition_type_id = simple_type(interp->types, TypeTag::Definition, {});

		if (into.success.type_id == TypeId::INVALID)
		{
			into.success.type_id = definition_type_id;

			if (into.success.location.begin() == nullptr)
				into.success.location = stack_push(interp, sizeof(Definition), alignof(Definition));
		}
		else if (!type_can_implicitly_convert_from_to(interp->types, definition_type_id, into.success.type_id))
		{
			source_error(interp->errors, source_id_of(interp->asts, node), "Cannot implicitly convert definition to desired type.\n");
		}

		const DefinitionInfo info = get_definition_info(node);

		const AstDefinitionData attach = *attachment_of<AstDefinitionData>(node);

		Definition* const definition = reinterpret_cast<Definition*>(into.success.location.begin());
		memset(definition, 0, sizeof(*definition));

		definition->name = attach.identifier_id;
		definition->source = source_id_of(interp->asts, node);
		definition->type.pending = is_some(info.type) ? id_from_ast_node(interp->asts, get_ptr(info.type)) : AstNodeId::INVALID;
		definition->default_or_global_value.pending = is_some(info.value) ? id_from_ast_node(interp->asts, get_ptr(info.value)) : AstNodeId::INVALID;
		definition->is_global = has_flag(node, AstFlag::Definition_IsGlobal);
		definition->is_pub = has_flag(node, AstFlag::Definition_IsPub);
		definition->is_use = has_flag(node, AstFlag::Definition_IsUse);
		definition->is_mut = has_flag(node, AstFlag::Definition_IsMut);

		return into;
	}

	case AstTag::Func:
	{
		ASSERT_OR_IGNORE(into.success.kind == ValueKind::Value);

		AstNode* const signature = first_child_of(node);

		TypeId signature_type_id;

		const EvalSpec signature_spec = evaluate(interp, signature, EvalSpec{
			ValueKind::Value,
			range::from_object_bytes_mut(&signature_type_id),
			simple_type(interp->types, TypeTag::Type, {})
		});

		if (signature_spec.tag == EvalTag::Unbound)
			return EvalSpec{ signature_spec.unbound.source };

		if (type_tag_from_id(interp->types, signature_type_id) != TypeTag::Func)
			source_error(interp->errors, source_id_of(interp->asts, signature), "Function signature must be of type `Signature`.\n");

		if (into.success.type_id == TypeId::INVALID)
		{
			into.success.type_id = signature_type_id;

			if (into.success.location.begin() == nullptr)
				into.success.location = stack_push(interp, sizeof(CallableValue), alignof(CallableValue));
		}
		else if (!type_can_implicitly_convert_from_to(interp->types, signature_type_id, into.success.type_id))
		{
			source_error(interp->errors, source_id_of(interp->asts, node), "Cannot implicitly convert function to desired type.\n");
		}

		AstNode* const body = next_sibling_of(signature);

		const AstNodeId body_id = id_from_ast_node(interp->asts, body);

		const CallableValue callable = CallableValue::from_function(signature_type_id, body_id);

		store_loc(into.success.location, callable);

		return into;
	}

	case AstTag::Signature:
	{
		ASSERT_OR_IGNORE(into.success.kind == ValueKind::Value);

		const TypeId type_type_id = simple_type(interp->types, TypeTag::Type, {});

		if (into.success.type_id == TypeId::INVALID)
		{
			into.success.type_id = type_type_id;

			if (into.success.location.begin() == nullptr)
				into.success.location = stack_push(interp, sizeof(TypeId), alignof(TypeId));
		}
		else if (!type_can_implicitly_convert_from_to(interp->types, type_type_id, into.success.type_id))
		{
			source_error(interp->errors, source_id_of(interp->asts, node), "Cannot implicitly convert signature to desired type.\n");
		}

		SignatureInfo info = get_signature_info(node);

		AstDirectChildIterator params = direct_children_of(info.parameters);

		const TypeId parameter_list_type_id = create_open_type(interp->types, active_arec_type_id(interp), source_id_of(interp->asts, info.parameters), TypeDisposition::Signature);

		const ArecId parameter_list_arec_id = arec_push(interp, parameter_list_type_id, 0, 1, active_arec_id(interp), ArecKind::Unbound);

		u8 param_count = 0;

		while (has_next(&params))
		{
			AstNode* const param = next(&params);

			ASSERT_OR_IGNORE(param->tag == AstTag::Parameter);

			Member param_member = delayed_member_from(interp, param);
			param_member.is_global = false;
			param_member.is_use = false;

			add_open_type_member(interp->types, parameter_list_type_id, param_member);

			param_count += 1;
		}

		close_open_type(interp->types, parameter_list_type_id, 0, 0, 0);

		push_partial_value_builder(interp, node);

		bool has_unbound_parameter = false;

		IncompleteMemberIterator members = incomplete_members_of(interp->types, parameter_list_type_id);

		while (has_next(&members))
		{
			const Member* const member = next(&members);

			ASSERT_OR_IGNORE(member->has_pending_type || member->has_pending_value);

			TypeId member_type_id = TypeId::INVALID;

			GlobalValueId member_value_id = GlobalValueId::INVALID;

			bool type_is_unbound = false;

			if (!member->has_pending_type)
			{
				member_type_id = member->type.complete;
			}
			else if (member->type.pending != AstNodeId::INVALID)
			{
				AstNode* const type = ast_node_from_id(interp->asts, member->type.pending);

				const EvalSpec type_spec = evaluate(interp, type, EvalSpec{
					ValueKind::Value,
					range::from_object_bytes_mut(&member_type_id),
					simple_type(interp->types, TypeTag::Type, {})
				});

				if (type_spec.tag == EvalTag::Unbound)
				{
					type_is_unbound = true;

					has_unbound_parameter = true;
				}
			}

			if (member->has_pending_value)
			{
				AstNode* const value = ast_node_from_id(interp->asts, member->value.pending);

				if (type_is_unbound)
				{
					TODO("Handle default value for unbound parameter");
				}
				else if (member_type_id == TypeId::INVALID)
				{
					const u32 mark = stack_mark(interp);

					const EvalSpec value_spec = evaluate(interp, value, EvalSpec{
						ValueKind::Value,
					});

					if (value_spec.tag == EvalTag::Unbound)
					{
						TODO("Handle unbound explicitly typed default values");
					}
					else
					{
						const TypeMetrics metrics = type_metrics_from_id(interp->types, value_spec.success.type_id);

						member_value_id = alloc_global_value(interp->globals, metrics.size, metrics.align);

						MutRange<byte> member_value = global_value_get_mut(interp->globals, member_value_id);

						ASSERT_OR_IGNORE(member_value.count() == value_spec.success.location.count());

						memcpy(member_value.begin(), value_spec.success.location.begin(), member_value.count());

						member_type_id = value_spec.success.type_id;
					}

					stack_shrink(interp, mark);
				}
				else
				{
					const TypeMetrics metrics = type_metrics_from_id(interp->types, member_type_id);

					member_value_id = alloc_global_value(interp->globals, metrics.size, metrics.align);

					MutRange<byte> member_value = global_value_get_mut(interp->globals, member_value_id);

					const EvalSpec value_spec = evaluate(interp, value, EvalSpec{
						ValueKind::Value,
						member_value,
						member_type_id
					});

					if (value_spec.tag == EvalTag::Unbound)
						TODO("Handle unbound implicitly typed default values.");
				}
			}

			set_incomplete_type_member_info_by_rank(interp->types, parameter_list_type_id, member->rank, MemberCompletionInfo{
				member->has_pending_type && member_type_id != TypeId::INVALID,
				member->has_pending_value && member_value_id != GlobalValueId::INVALID,
				member_type_id,
				member_value_id
			});
		}

		TypeId return_type_id = TypeId::INVALID;

		bool has_unbound_return_type;

		if (is_some(info.return_type))
		{
			AstNode* const return_type = get_ptr(info.return_type);

			EvalSpec return_type_spec = evaluate(interp, return_type, EvalSpec{
				ValueKind::Value,
				range::from_object_bytes_mut(&return_type_id),
				simple_type(interp->types, TypeTag::Type, {})
			});

			has_unbound_return_type = return_type_spec.tag == EvalTag::Unbound;
		}
		else
		{
			TODO("(Later) Implement return type deduction");
		}

		if (is_some(info.expects))
		{
			TODO("(Later) Implement expects clause on signatures");
		}

		if (is_some(info.ensures))
		{
			TODO("(Later) Implement ensures clause on signatures");
		}

		arec_pop(interp, parameter_list_arec_id);

		const PartialValueId partial_value_id = pop_partial_value_builder(interp);

		SignatureType signature_type{};
		signature_type.parameter_list_type_id = parameter_list_type_id;
		if (has_unbound_return_type)
			signature_type.return_type.partial_root = id_from_ast_node(interp->asts, get_ptr(info.return_type));
		else
			signature_type.return_type.complete = return_type_id;
		signature_type.partial_value_id = has_unbound_parameter || has_unbound_return_type
			? partial_value_id
			: PartialValueId::INVALID;
		signature_type.param_count = param_count;
		signature_type.is_proc = has_flag(node, AstFlag::Signature_IsProc);
		signature_type.parameter_list_is_unbound = has_unbound_parameter;
		signature_type.return_type_is_unbound = has_unbound_return_type;

		const TypeId signature_type_id = simple_type(interp->types, TypeTag::Func, range::from_object_bytes(&signature_type));

		store_loc(into.success.location, signature_type_id);

		return into;
	}

	case AstTag::Identifier:
	{
		const AstIdentifierData attach = *attachment_of<AstIdentifierData>(node);

		const IdentifierInfo info = lookup_identifier(interp, attach.identifier_id, source_id_of(interp->asts, node));

		if (info.tag == IdentifierInfoTag::Missing)
		{
			const Range<char8> name = identifier_name_from_id(interp->identifiers, attach.identifier_id);

			source_error(interp->errors, source_id_of(interp->asts, node), "Identifier '%.*s' is not defined.\n", static_cast<s32>(name.count()), name.begin());
		}
		else if (info.tag == IdentifierInfoTag::Unbound)
		{
			return EvalSpec{ info.unbound.source };
		}

		ASSERT_OR_IGNORE(info.tag == IdentifierInfoTag::Found);

		if (into.success.type_id == TypeId::INVALID)
		{
			into.success.type_id = info.found.type_id;

			if (into.success.location.begin() == nullptr)
			{
				if (into.success.kind == ValueKind::Value)
				{
					const TypeMetrics metrics = type_metrics_from_id(interp->types, info.found.type_id);

					into.success.location = stack_push(interp, metrics.size, metrics.align);
				}
				else
				{
					into.success.location = stack_push(interp, sizeof(MutRange<byte>), alignof(MutRange<byte>));
				}
			}
		}
		else if (!type_can_implicitly_convert_from_to(interp->types, info.found.type_id, into.success.type_id))
		{
			const Range<char8> name = identifier_name_from_id(interp->identifiers, attach.identifier_id);

			source_error(interp->errors, source_id_of(interp->asts, node), "Cannot convert value of identifier '%.*s' to desired type.\n", static_cast<s32>(name.count()), name.begin());
		}

		if (is_same_type(interp->types, info.found.type_id, into.success.type_id))
		{
			if (into.success.kind == ValueKind::Location)
				store_loc(into.success.location, info.found.location);
			else
				copy_loc(into.success.location, info.found.location.immut());
		}
		else if (into.success.kind == ValueKind::Value)
		{
			TODO("Implement implicit conversion of identifier values");
		}
		else
		{
			const Range<char8> name = identifier_name_from_id(interp->identifiers, attach.identifier_id);

			source_error(interp->errors, source_id_of(interp->asts, node), "Cannot treat identifier '%.*s' as location as it requires an implicit conversion to conform to the desired type.\n", static_cast<s32>(name.count()), name.begin());
		}

		return into;
	}

	case AstTag::LitInteger:
	{
		ASSERT_OR_IGNORE(into.success.kind == ValueKind::Value);

		const TypeId comp_integer_type_id = simple_type(interp->types, TypeTag::CompInteger, {});

		if (into.success.type_id == TypeId::INVALID)
		{
			into.success.type_id = comp_integer_type_id;

			if (into.success.location.begin() == nullptr)
				into.success.location = stack_push(interp, sizeof(CompIntegerValue), alignof(CompIntegerValue));
		}
		else if (!type_can_implicitly_convert_from_to(interp->types, comp_integer_type_id, into.success.type_id))
		{
			source_error(interp->errors, source_id_of(interp->asts, node), "Cannot implicitly convert integer literal to desired type.\n");
		}

		const AstLitIntegerData attach = *attachment_of<AstLitIntegerData>(node);

		if (is_same_type(interp->types, into.success.type_id, comp_integer_type_id))
			store_loc(into.success.location, attach.value);
		else
			convert_comp_integer_to_integer(interp, node, into, attach.value);

		return into;
	}

	case AstTag::LitString:
	{
		ASSERT_OR_IGNORE(into.success.kind == ValueKind::Value);

		const AstLitStringData attach = *attachment_of<AstLitStringData>(node);

		if (into.success.type_id == TypeId::INVALID)
		{
			into.success.type_id = attach.string_type_id;

			if (into.success.location.begin() == nullptr)
			{
				const TypeMetrics metrics = type_metrics_from_id(interp->types, attach.string_type_id);

				into.success.location = stack_push(interp, metrics.size, metrics.align);
			}
		}
		else if (!type_can_implicitly_convert_from_to(interp->types, attach.string_type_id, into.success.type_id))
		{
			source_error(interp->errors, source_id_of(interp->asts, node), "Cannot implicitly convert string literal to desired type.\n");
		}

		MutRange<byte> value = global_value_get_mut(interp->globals, attach.string_value_id);

		if (is_same_type(interp->types, into.success.type_id, attach.string_type_id))
		{
			if (into.success.kind == ValueKind::Value)
				copy_loc(into.success.location, value.immut());
			else
				store_loc(into.success.location, value.immut());
		}
		else if (into.success.kind == ValueKind::Value)
		{
			ASSERT_OR_IGNORE(type_tag_from_id(interp->types, into.success.type_id) == TypeTag::Slice);

			convert_array_to_slice(interp, node, into, EvalSpec{ ValueKind::Value, value, attach.string_type_id });
		}
		else
		{
			source_error(interp->errors, source_id_of(interp->asts, node), "Cannot treat string litearal as location as it requires an implicit conversion to conform to the desired type.\n");
		}

		return into;
	}

	case AstTag::Call:
	{
		ASSERT_OR_IGNORE(into.success.kind == ValueKind::Value);

		AstNode* const callee = first_child_of(node);

		CallableValue callee_value;

		const EvalSpec callee_spec = evaluate(interp, callee, EvalSpec{
			ValueKind::Value,
			range::from_object_bytes_mut(&callee_value)
		});

		if (callee_spec.tag == EvalTag::Unbound)
		{
			TODO("Implement unbound callees");
		}
		else
		{
			const TypeTag callee_type_tag = type_tag_from_id(interp->types, callee_spec.success.type_id);

			if (callee_type_tag != TypeTag::Func && callee_type_tag != TypeTag::Builtin)
				source_error(interp->errors, source_id_of(interp->asts, callee), "Cannot implicitly convert callee to callable type.\n");

			const SignatureType* const signature_type = static_cast<const SignatureType*>(simple_type_structure_from_id(interp->types, callee_value.signature_type_id));

			const CallInfo call_info = setup_call_args(interp, signature_type, callee);

			if (into.success.type_id == TypeId::INVALID)
			{
				into.success.type_id = call_info.return_type_id;

				if (into.success.location.begin() == nullptr)
				{
					const TypeMetrics return_type_metrics = type_metrics_from_id(interp->types, call_info.return_type_id);

					into.success.location = stack_push(interp, return_type_metrics.size, return_type_metrics.align);
				}
			}
			else if (!type_can_implicitly_convert_from_to(interp->types, call_info.return_type_id, into.success.type_id))
			{
				source_error(interp->errors, source_id_of(interp->asts, node), "Cannot implicitly convert returned value to desired type.\n");
			}

			const u32 mark = stack_mark(interp);

			const bool needs_conversion = !is_same_type(interp->types, call_info.return_type_id, into.success.type_id);

			MutRange<byte> temp_location;

			if (needs_conversion)
			{
				const TypeMetrics return_type_metrics = type_metrics_from_id(interp->types, call_info.return_type_id);

				stack_push(interp, return_type_metrics.size, return_type_metrics.align);
			}
			else
			{
				temp_location = into.success.location;
			}

			if (callee_value.is_builtin)
			{
				Arec* const parameter_list_arec = arec_from_id(interp, call_info.parameter_list_arec_id);

				interp->builtin_values[callee_value.builtin.ordinal](interp, parameter_list_arec, node, temp_location);
			}
			else
			{
				AstNode* const body = ast_node_from_id(interp->asts, static_cast<AstNodeId>(callee_value.function.ast_node_id_bits));

				const EvalSpec call_spec = evaluate(interp, body, EvalSpec{
					ValueKind::Value,
					temp_location,
					into.success.type_id
				});

				if (call_spec.tag == EvalTag::Unbound)
					TODO("Implement unbound returns. I don't think this can actually reasonably happen, since we are by definition binding everything for the call here\n");
			}

			if (needs_conversion)
			{
				convert(interp, node, into, EvalSpec{
					ValueKind::Value,
					temp_location,
					call_info.return_type_id
				});
			}

			arec_pop(interp, call_info.parameter_list_arec_id);

			stack_shrink(interp, mark);
		}

		return into;
	}

	case AstTag::Member:
	{
		AstNode* const lhs = first_child_of(node);

		const AstMemberData attach = *attachment_of<AstMemberData>(node);

		EvalSpec lhs_spec = evaluate(interp, lhs, EvalSpec{ ValueKind::Location });

		if (lhs_spec.tag == EvalTag::Unbound)
			return EvalSpec{ lhs_spec.unbound.source };

		ASSERT_OR_IGNORE(lhs_spec.tag == EvalTag::Success);

		const TypeTag lhs_type_tag = type_tag_from_id(interp->types, lhs_spec.success.type_id);

		if (lhs_type_tag == TypeTag::Composite)
		{
			const Member* member;

			if (!type_member_by_name(interp->types, lhs_spec.success.type_id, attach.identifier_id, source_id_of(interp->asts, node), &member))
			{
				const Range<char8> name = identifier_name_from_id(interp->identifiers, attach.identifier_id);

				source_error(interp->errors, source_id_of(interp->asts, node), "Left-hand-side of `.` has no member named `%.*s`.\n", static_cast<s32>(name.count()), name.begin());
			}

			if (member->is_global)
			{
				into = evaluate_global_member(interp, node, into, lhs_spec.success.type_id, member);
			}
			else
			{
				into = evaluate_local_member(interp, node, into, lhs_spec.success.type_id, member, lhs_spec.success.location);
			}
		}
		else if (lhs_type_tag == TypeTag::Type)
		{
			const TypeId type_id = load_loc<TypeId>(lhs_spec.success.kind == ValueKind::Value
				? lhs_spec.success.location
				: load_loc<MutRange<byte>>(lhs_spec.success.location)
			);

			if (type_tag_from_id(interp->types, type_id) != TypeTag::Composite)
				source_error(interp->errors, source_id_of(interp->asts, node), "Left-hand-side of `.` cannot be a non-composite type.\n");;

			const Member* member;

			if (!type_member_by_name(interp->types, type_id, attach.identifier_id, source_id_of(interp->asts, node), &member))
			{
				const Range<char8> name = identifier_name_from_id(interp->identifiers, attach.identifier_id);

				source_error(interp->errors, source_id_of(interp->asts, node), "Left-hand-side of `.` has no member named `%.*s`.\n", static_cast<s32>(name.count()), name.begin());
			}

			if (!member->is_global)
			{
				const Range<char8> name = identifier_name_from_id(interp->identifiers, attach.identifier_id);

				source_error(interp->errors, source_id_of(interp->asts, node), "Member `%.*s` cannot be accessed through a type, as it is not global.\n", static_cast<s32>(name.count()), name.begin());
			}

			into = evaluate_global_member(interp, node, into, type_id, member);
		}
		else
		{
			source_error(interp->errors, source_id_of(interp->asts, node), "Left-hand-side of `.` must be either a composite value or a composite type.\n");
		}

		return into;
	}

	case AstTag::OpCmpEQ:
	{
		ASSERT_OR_IGNORE(into.success.kind == ValueKind::Value);

		const TypeId bool_type_id = simple_type(interp->types, TypeTag::Boolean, {});

		if (into.success.type_id == TypeId::INVALID)
		{
			into.success.type_id = bool_type_id;

			if (into.success.location.begin() == nullptr)
				into.success.location = stack_push(interp, sizeof(bool), alignof(bool));
		}
		else if (!type_can_implicitly_convert_from_to(interp->types, bool_type_id, into.success.type_id))
		{
			source_error(interp->errors, source_id_of(interp->asts, node), "Cannot implicitly convert boolean to desired type.\n");
		}

		AstNode* const lhs = first_child_of(node);

		const EvalSpec lhs_spec = evaluate(interp, lhs, EvalSpec{ ValueKind::Value });

		AstNode* const rhs = next_sibling_of(lhs);

		const EvalSpec rhs_spec = evaluate(interp, rhs, EvalSpec{ ValueKind::Value });

		if (lhs_spec.tag == EvalTag::Unbound && rhs_spec.tag == EvalTag::Unbound)
		{
			TODO("Treat unbound parameters to OpCmpEq");
		}
		else if (lhs_spec.tag == EvalTag::Unbound)
		{
			TODO("Treat unbound parameters to OpCmpEq");
		}
		else if (rhs_spec.tag == EvalTag::Unbound)
		{
			TODO("Treat unbound parameters to OpCmpEq");
		}

		const TypeId common_type_id = common_type(interp->types, lhs_spec.success.type_id, rhs_spec.success.type_id);

		if (common_type_id == TypeId::INVALID)
			source_error(interp->errors, source_id_of(interp->asts, node), "Could not unify argument types of `%s`.\n", tag_name(node->tag));

		const u32 mark = stack_mark(interp);

		MutRange<byte> lhs_casted;

		if (!is_same_type(interp->types, common_type_id, lhs_spec.success.type_id))
		{
			const TypeMetrics metrics = type_metrics_from_id(interp->types, common_type_id);

			lhs_casted = stack_push(interp, metrics.size, metrics.align);

			convert(interp, lhs, EvalSpec{ ValueKind::Value, lhs_casted, common_type_id}, lhs_spec);
		}
		else
		{
			lhs_casted = lhs_spec.success.location;
		}

		MutRange<byte> rhs_casted;

		if (!is_same_type(interp->types, common_type_id, rhs_spec.success.type_id))
		{
			const TypeMetrics metrics = type_metrics_from_id(interp->types, common_type_id);

			rhs_casted = stack_push(interp, metrics.size, metrics.align);

			convert(interp, rhs, EvalSpec{ ValueKind::Value, rhs_casted, common_type_id}, rhs_spec);
		}
		else
		{
			rhs_casted = rhs_spec.success.location;
		}

		ASSERT_OR_IGNORE(lhs_casted.count() == rhs_casted.count());

		const bool result = memcmp(lhs_casted.begin(), rhs_casted.begin(), lhs_casted.count()) == 0;

		stack_shrink(interp, mark);

		// No need for implicit conversion here, as bool is not convertible to
		// anything else.
		store_loc(into.success.location, result);

		return into;
	}

	case AstTag::CompositeInitializer:
	case AstTag::ArrayInitializer:
	case AstTag::Wildcard:
	case AstTag::Where:
	case AstTag::Expects:
	case AstTag::Ensures:
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
		panic("evaluate(%s) not yet implemented.\n", tag_name(node->tag));

	case AstTag::INVALID:
	case AstTag::File:
	case AstTag::Parameter:
	case AstTag::ParameterList:
	case AstTag::MAX:
		; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

static TypeId typeinfer(Interpreter* interp, AstNode* node) noexcept
{
	switch (node->tag)
	{
	case AstTag::Block:
	{
		if (!has_children(node))
			return simple_type(interp->types, TypeTag::Void, {});

		panic("typeinfer(%s) for non-empty blocks not yet implemented.\n", tag_name(node->tag));
	}

	case AstTag::Identifier:
	{
		AstIdentifierData attach = *attachment_of<AstIdentifierData>(node);

		IdentifierInfo info = lookup_identifier(interp, attach.identifier_id, source_id_of(interp->asts, node));

		if (info.tag == IdentifierInfoTag::Found)
			return info.found.type_id;

		const Range<char8> name = identifier_name_from_id(interp->identifiers, attach.identifier_id);

		ASSERT_OR_IGNORE(info.tag == IdentifierInfoTag::Unbound || info.tag == IdentifierInfoTag::Missing);

		if (info.tag == IdentifierInfoTag::Unbound)
			source_error(interp->errors, source_id_of(interp->asts, node), "Identifier '%.*s' is not bound yet, so its type cannot be inferred.\n", static_cast<s32>(name.count()), name.begin());

		source_error(interp->errors, source_id_of(interp->asts, node), "Identifier '%.*s' is not defined.\n", static_cast<s32>(name.count()), name.begin());
	}

	case AstTag::LitInteger:
	{
		return simple_type(interp->types, TypeTag::CompInteger, {});
	}

	case AstTag::OpCmpEQ:
	{
		AstNode* const lhs = first_child_of(node);

		const TypeId lhs_type_id = typeinfer(interp, lhs);

		AstNode* const rhs = next_sibling_of(lhs);

		const TypeId rhs_type_id = typeinfer(interp, rhs);

		if (common_type(interp->types, lhs_type_id, rhs_type_id) == TypeId::INVALID)
			source_error(interp->errors, source_id_of(interp->asts, node), "Could not unify argument types of `%s`.\n", tag_name(node->tag));

		return simple_type(interp->types, TypeTag::Boolean, {});
	}

	case AstTag::Builtin:
	case AstTag::CompositeInitializer:
	case AstTag::ArrayInitializer:
	case AstTag::Wildcard:
	case AstTag::Where:
	case AstTag::Expects:
	case AstTag::Ensures:
	case AstTag::Definition:
	case AstTag::If:
	case AstTag::For:
	case AstTag::ForEach:
	case AstTag::Switch:
	case AstTag::Case:
	case AstTag::Func:
	case AstTag::Signature:
	case AstTag::Trait:
	case AstTag::Impl:
	case AstTag::Catch:
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
	case AstTag::Member:
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
		panic("typeinfer(%s) not yet implemented.\n", tag_name(node->tag));
	
	case AstTag::INVALID:
	case AstTag::File:
	case AstTag::Parameter:
	case AstTag::ParameterList:
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

	const ArecId file_arec_id = arec_push(interp, file_type_id, 0, 1, ArecId::INVALID, ArecKind::Normal);

	AstDirectChildIterator ast_it = direct_children_of(file);

	while (has_next(&ast_it))
	{
		AstNode* const node = next(&ast_it);

		if (node->tag != AstTag::Definition)
			source_error(interp->errors, source_id_of(interp->asts, node), "Currently only definitions are supported on a file's top-level.\n");

		if (has_flag(node, AstFlag::Definition_IsGlobal))
			source_warning(interp->errors, source_id_of(interp->asts, node), "Redundant 'global' modifier. Top-level definitions are implicitly global.\n");

		Member member = delayed_member_from(interp, node);
		member.is_global = true;

		add_open_type_member(interp->types, file_type_id, member);
	}

	close_open_type(interp->types, file_type_id, 0, 1, 0);

	IncompleteMemberIterator members = incomplete_members_of(interp->types, file_type_id);

	while (has_next(&members))
	{
		const Member* member = next(&members);

		ASSERT_OR_IGNORE(member->has_pending_type && member->has_pending_value);

		AstNode* const value = ast_node_from_id(interp->asts, member->value.pending);

		EvalSpec value_spec;

		GlobalValueId member_value_id;

		if (member->type.pending != AstNodeId::INVALID)
		{
			AstNode* const type = ast_node_from_id(interp->asts, member->type.pending);

			TypeId member_type_id;

			const EvalSpec member_type_spec = evaluate(interp, type, EvalSpec{
				ValueKind::Value,
				range::from_object_bytes_mut(&member_type_id),
				simple_type(interp->types, TypeTag::Type, {})
			});

			// This must succeed as we are on the top level.
			ASSERT_OR_IGNORE(member_type_spec.tag == EvalTag::Success);

			const TypeMetrics metrics = type_metrics_from_id(interp->types, member_type_id);

			member_value_id = alloc_global_value(interp->globals, metrics.size, metrics.align);

			value_spec = EvalSpec{
				ValueKind::Value,
				global_value_get_mut(interp->globals, member_value_id),
				member_type_id
			};
		}
		else
		{
			value_spec = EvalSpec{
				ValueKind::Value
			};

			member_value_id = GlobalValueId::INVALID;
		}

		value_spec = evaluate(interp, value, value_spec);

		if (member_value_id == GlobalValueId::INVALID)
		{
			const TypeMetrics metrics = type_metrics_from_id(interp->types, value_spec.success.type_id);

			member_value_id = alloc_global_value(interp->globals, metrics.size, metrics.align);

			MutRange<byte> value_bytes = global_value_get_mut(interp->globals, member_value_id);

			ASSERT_OR_IGNORE(value_bytes.count() == value_spec.success.location.count());

			memcpy(value_bytes.begin(), value_spec.success.location.begin(), value_bytes.count());
		}

		const MemberCompletionInfo completion = {
			true,
			true,
			value_spec.success.type_id,
			member_value_id
		};

		set_incomplete_type_member_info_by_rank(interp->types, file_type_id, member->rank, completion);
	}

	arec_pop(interp, file_arec_id);

	return file_type_id;
}





static TypeId make_func_type_from_array(TypePool* types, TypeId return_type_id, u8 param_count, const BuiltinParamInfo* params) noexcept
{
	const TypeId parameter_list_type_id = create_open_type(types, TypeId::INVALID, SourceId::INVALID, TypeDisposition::Signature);

	for (u8 i = 0; i != param_count; ++i)
	{
		Member member{};
		member.name = params[i].name;
		member.type.complete = params[i].type;
		member.value.complete = GlobalValueId::INVALID;
		member.source = SourceId::INVALID;
		member.is_global = false;
		member.is_pub = false;
		member.is_use = false;
		member.is_mut = false;
		member.is_comptime_known = params[i].is_comptime_known;
		member.is_arg_independent = true;
		member.has_pending_type = false;
		member.has_pending_value = false;
		member.offset = 0;

		add_open_type_member(types, parameter_list_type_id, member);
	}

	close_open_type(types, parameter_list_type_id, 0, 0, 0);

	SignatureType signature_type{};
	signature_type.parameter_list_type_id = parameter_list_type_id;
	signature_type.return_type.complete = return_type_id;
	signature_type.partial_value_id = PartialValueId::INVALID;
	signature_type.param_count = param_count;
	signature_type.is_proc = false;
	signature_type.parameter_list_is_unbound = false;
	signature_type.return_type_is_unbound = false;

	return simple_type(types, TypeTag::Func, range::from_object_bytes(&signature_type));
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
	IdentifierInfo info = lookup_local_identifier(interp, arec, name, SourceId::INVALID);

	ASSERT_OR_IGNORE(info.tag == IdentifierInfoTag::Found);

	return load_loc<T>(info.found.location);
}

static void builtin_integer(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, MutRange<byte> into) noexcept
{
	const u8 bits = get_builtin_arg<u8>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("bits")));

	const bool is_signed = get_builtin_arg<bool>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("is_signed")));

	NumericType integer_type{};
	integer_type.bits = bits;
	integer_type.is_signed = is_signed;

	store_loc(into, simple_type(interp->types, TypeTag::Integer, range::from_object_bytes(&integer_type)));
}

static void builtin_float(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, MutRange<byte> into) noexcept
{
	const u8 bits = get_builtin_arg<u8>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("bits")));

	NumericType float_type{};
	float_type.bits = bits;
	float_type.is_signed = true;

	store_loc(into, simple_type(interp->types, TypeTag::Float, range::from_object_bytes(&float_type)));
}

static void builtin_type(Interpreter* interp, [[maybe_unused]] Arec* arec, [[maybe_unused]] AstNode* call_node, MutRange<byte> into) noexcept
{
	store_loc(into, simple_type(interp->types, TypeTag::Type, {}));
}

static void builtin_typeof(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, MutRange<byte> into) noexcept
{
	store_loc(into, get_builtin_arg<TypeId>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("arg"))));
}

static void builtin_returntypeof(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, MutRange<byte> into) noexcept
{
	const TypeId arg = get_builtin_arg<TypeId>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("arg")));

	ASSERT_OR_IGNORE(type_tag_from_id(interp->types, arg) == TypeTag::Func || type_tag_from_id(interp->types, arg) == TypeTag::Builtin);

	const SignatureType* const signature_type = static_cast<const SignatureType*>(simple_type_structure_from_id(interp->types, arg));

	if (signature_type->return_type_is_unbound)
		TODO("Implement `_returntypeof` for unbound return types");

	store_loc(into, signature_type->return_type.complete);
}

static void builtin_sizeof(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, MutRange<byte> into) noexcept
{
	const TypeId arg = get_builtin_arg<TypeId>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("arg")));

	const TypeMetrics metrics = type_metrics_from_id(interp->types, arg);

	store_loc(into, comp_integer_from_u64(metrics.size));
}

static void builtin_alignof(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, MutRange<byte> into) noexcept
{
	const TypeId arg = get_builtin_arg<TypeId>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("arg")));

	const TypeMetrics metrics = type_metrics_from_id(interp->types, arg);

	store_loc(into, comp_integer_from_u64(metrics.align));
}

static void builtin_strideof(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, MutRange<byte> into) noexcept
{
	const TypeId arg = get_builtin_arg<TypeId>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("arg")));

	const TypeMetrics metrics = type_metrics_from_id(interp->types, arg);

	store_loc(into, comp_integer_from_u64(metrics.align));
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
			source_error(interp->errors, source_id_of(interp->asts, call_node), "Failed to make get parent directory from `from` source file (0x%X).\n", minos::last_error());

		const u32 absolute_path_chars = minos::path_to_absolute_relative_to(path, Range{ path_base_parent_buf , path_base_parent_chars }, MutRange{ absolute_path_buf });

		if (absolute_path_chars == 0 || absolute_path_chars > array_count(absolute_path_buf))
			source_error(interp->errors, source_id_of(interp->asts, call_node), "Failed to make `path` %.*s absolute relative to `from` %.*s (0x%X).\n", static_cast<s32>(path.count()), path.begin(), static_cast<s32>(path_base.count()), path_base.begin(), minos::last_error());

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
	store_loc(into, source_id_of(interp->asts, call_node));
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

	const TypeId std_filepath_type_id = simple_type(interp->types, TypeTag::Array, range::from_object_bytes(&array_of_u8_type));

	const GlobalValueId std_filepath_value_id = alloc_global_value(interp->globals, config->std.filepath.count(), 1);

	global_value_set(interp->globals, std_filepath_value_id, 0, config->std.filepath.as_byte_range());

	const AstBuilderToken import_builtin = push_node(asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, static_cast<AstFlag>(Builtin::Import), AstTag::Builtin);

	push_node(asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, AstFlag::EMPTY, AstLitStringData{ std_filepath_value_id, std_filepath_type_id });

	const AstBuilderToken literal_zero = push_node(asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, AstFlag::EMPTY, AstLitIntegerData{ comp_integer_from_u64(0) });

	push_node(asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, AstFlag::EMPTY, AstLitIntegerData{ comp_integer_from_u64(0) });

	push_node(asts, literal_zero, SourceId::INVALID, AstFlag::EMPTY, AstTag::OpCmpEQ);

	const AstBuilderToken source_id_builtin = push_node(asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, static_cast<AstFlag>(Builtin::SourceId), AstTag::Builtin);

	push_node(asts, source_id_builtin, SourceId::INVALID, AstFlag::EMPTY, AstTag::Call);

	const AstBuilderToken import_call = push_node(asts, import_builtin, SourceId::INVALID, AstFlag::EMPTY, AstTag::Call);

	const AstBuilderToken std_definition = push_node(asts, import_call, SourceId::INVALID, AstFlag::EMPTY, AstDefinitionData{ id_from_identifier(identifiers, range::from_literal_string("std")) });

	const AstBuilderToken std_identifier = push_node(asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, AstFlag::EMPTY, AstIdentifierData{ id_from_identifier(identifiers, range::from_literal_string("std")) });

	const AstBuilderToken prelude_member = push_node(asts, std_identifier, SourceId::INVALID, AstFlag::EMPTY, AstMemberData{ id_from_identifier(identifiers, range::from_literal_string("prelude")) });

	push_node(asts, prelude_member, SourceId::INVALID, AstFlag::Definition_IsUse, AstDefinitionData{ id_from_identifier(identifiers, range::from_literal_string("prelude")) });

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



Interpreter* create_interpreter(AllocPool* alloc, Config* config, SourceReader* reader, Parser* parser, TypePool* types, AstPool* asts, IdentifierPool* identifiers, GlobalValuePool* globals, PartialValuePool* partials, ErrorSink* errors, minos::FileHandle log_file, bool log_prelude) noexcept
{
	Interpreter* const interp = static_cast<Interpreter*>(alloc_from_pool(alloc, sizeof(Interpreter), alignof(Interpreter)));

	static constexpr u64 ARECS_RESERVE_SIZE = (1 << 20) * sizeof(Arec);

	static constexpr u64 TEMPS_RESERVE_SIZE = (1 << 26) * sizeof(byte);

	static constexpr u64 PARTIAL_VALUE_BUILDER_RESERVE_SIZE = (1 << 16) * sizeof(PartialValueBuilderId);

	static constexpr u64 ACTIVE_PARTIAL_VALUE_RESERVE_SIZE = (1 << 16) * sizeof(PeekablePartialValueIterator);

	const u64 total_reserve_size = ARECS_RESERVE_SIZE
	                             + TEMPS_RESERVE_SIZE
	                             + PARTIAL_VALUE_BUILDER_RESERVE_SIZE
	                             + ACTIVE_PARTIAL_VALUE_RESERVE_SIZE;

	byte* const memory = static_cast<byte*>(minos::mem_reserve(total_reserve_size));

	if (memory == nullptr)
		panic("Could not reserve memory for interpreter (0x%X).\n", minos::last_error());

	interp->reader = reader;
	interp->parser = parser;
	interp->types = types;
	interp->asts = asts;
	interp->identifiers = identifiers;
	interp->globals = globals;
	interp->partials = partials;
	interp->errors = errors;
	interp->top_arec_id = ArecId::INVALID;
	interp->active_arec_id = ArecId::INVALID;
	interp->prelude_type_id = TypeId::INVALID;
	interp->log_file = log_file;
	interp->log_prelude = log_prelude;

	u64 offset = 0;

	interp->arecs.init({ memory + offset, ARECS_RESERVE_SIZE}, 1 << 9);
	offset += ARECS_RESERVE_SIZE;

	interp->temps.init({ memory + offset, TEMPS_RESERVE_SIZE }, 1 << 9);
	offset += TEMPS_RESERVE_SIZE;

	interp->partial_value_builders.init({ memory + offset, PARTIAL_VALUE_BUILDER_RESERVE_SIZE }, 1 << 10);
	offset += PARTIAL_VALUE_BUILDER_RESERVE_SIZE;

	interp->active_partial_values.init({ memory + offset, ACTIVE_PARTIAL_VALUE_RESERVE_SIZE }, 1 << 10);
	offset += ACTIVE_PARTIAL_VALUE_RESERVE_SIZE;

	interp->memory = { memory, offset };

	init_builtin_types(interp);

	init_builtin_values(interp);

	init_prelude_type(interp, config, identifiers, asts);

	return interp;
}

void release_interpreter(Interpreter* interp) noexcept
{
	minos::mem_unreserve(interp->memory.begin(), interp->memory.count());
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

const char8* tag_name(ValueKind type_kind) noexcept
{
	static constexpr const char8* TYPE_KIND_NAMES[] = {
		"Value",
		"Location",
	};

	u8 ordinal = static_cast<u8>(type_kind);

	if (ordinal >= array_count(TYPE_KIND_NAMES))
		ordinal = 0;

	return TYPE_KIND_NAMES[ordinal];
}
