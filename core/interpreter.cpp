#include "core.hpp"

#include "../diag/diag.hpp"
#include "../infra/container/reserved_vec.hpp"

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

	ArecId old_top;
};

using BuiltinFunc = void (*) (Interpreter* interp, Arec* arec, AstNode* call_node, MutRange<byte> into) noexcept;

// Representation of a callable, meaning either a builtin or a user-defined
// function or procedure.
struct alignas(8) CallableValue
{
	bool is_builtin;

	union
	{
		u8 ordinal;

		AstNodeId body_ast_node_id;
	};

	TypeId signature_type_id;

	ClosureId closure_id;

	static CallableValue from_builtin(TypeId signature_type_id, u8 ordinal) noexcept
	{
		CallableValue value;
		value.is_builtin = true;
		value.ordinal = ordinal;
		value.signature_type_id = signature_type_id;
		value.closure_id = ClosureId::INVALID;

		return value;
	}

	static CallableValue from_function(TypeId signature_type_id, AstNodeId body_ast_node_id, ClosureId closure_id) noexcept
	{
		CallableValue value;
		value.is_builtin = false;
		value.body_ast_node_id = body_ast_node_id;
		value.signature_type_id = signature_type_id;
		value.closure_id = closure_id;

		return value;
	}
};

// Utility for creating built-in functions types.
struct BuiltinParamInfo
{
	IdentifierId name;

	TypeId type;

	bool is_comptime_known;
};

struct EvalSpec
{
	ValueKind kind;

	TypeId type_id;

	MutRange<byte> dst;

	EvalSpec() noexcept = default;

	EvalSpec(ValueKind kind) noexcept :
		kind{ kind },
		type_id{ TypeId::INVALID },
		dst{}
	{}

	EvalSpec(ValueKind kind, MutRange<byte> dst) noexcept :
		kind{ kind },
		type_id{ TypeId::INVALID },
		dst{ dst }
	{}

	EvalSpec(ValueKind kind, MutRange<byte> dst, TypeId type_id) noexcept :
		kind{ kind },
		type_id{ type_id },
		dst{ dst }
	{}
};

enum class EvalTag : u8
{
	Success,
	Unbound,
};

struct EvalRst
{
	EvalTag tag;

	#if COMPILER_CLANG
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Wnested-anon-types" // anonymous types declared in an anonymous union are an extension
	#endif
	union
	{
		struct
		{
			ValueKind kind;

			TypeId type_id;

			MutRange<byte> bytes;
		} success;

		struct
		{
			Arec* in;
		} unbound;
	};
	#if COMPILER_CLANG
	#pragma clang diagnostic pop
	#endif

	EvalRst() noexcept :
		tag{},
		success{}
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

			OptPtr<Arec> arec; 
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

	static IdentifierInfo make_found(MutRange<byte> location, TypeId type_id, bool is_mut, OptPtr<Arec> arec) noexcept
	{
		IdentifierInfo info;
		info.tag = IdentifierInfoTag::Found;
		info.found.location = location;
		info.found.type_id = type_id;
		info.found.is_mut = is_mut;
		info.found.arec = arec;

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

struct TypeIdAndValueKind
{
	u32 type_id_bits : 30;

	u32 value_kind_bits : 2;
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

	ClosurePool* closures;

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





static EvalRst evaluate(Interpreter* interp, AstNode* node, EvalSpec spec) noexcept;

static TypeId typeinfer(Interpreter* interp, AstNode* node) noexcept;



static EvalRst eval_unbound(Arec* unbound_in) noexcept
{
	EvalRst rst;
	rst.tag = EvalTag::Unbound;
	rst.unbound.in = unbound_in;

	return rst;
}



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

	const ArecId old_top = interp->top_arec_id;

	interp->active_arec_id = arec_id;

	return { old_selected, old_top };
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

static Arec* arec_from_id(Interpreter* interp, ArecId arec_id) noexcept
{
	ASSERT_OR_IGNORE(arec_id != ArecId::INVALID);

	return reinterpret_cast<Arec*>(interp->arecs.begin() + static_cast<s32>(arec_id));
}

static void arec_restore(Interpreter* interp, ArecRestoreInfo info) noexcept
{
	ASSERT_OR_IGNORE(info.old_top <= interp->top_arec_id);

	ASSERT_OR_IGNORE(info.old_selected <= static_cast<ArecId>(info.old_top));

	const Arec* const old_top_arec = arec_from_id(interp, info.old_top);

	const u32 old_top_arec_qwords = (sizeof(Arec) + old_top_arec->size + 7) / 8;

	const u32 old_top_arec_base = static_cast<u32>(reinterpret_cast<const u64*>(old_top_arec) - interp->arecs.begin());

	const u32 old_used = old_top_arec_base + old_top_arec_qwords;

	interp->arecs.pop_to(old_used);

	interp->active_arec_id = info.old_selected;
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

	// This overallocates due to `interp->arecs` rounding to 8 bytes.
	// However, that's not really a problem, as we're in the top arec anyways,
	// and it'll get popped soon enough.
	(void) interp->arecs.reserve_padded(static_cast<u32>(new_size - arec->size));

	arec->size = new_size;
}



static void push_partial_value_builder(Interpreter* interp, AstNode* root) noexcept
{
	const PartialValueBuilderId builder_id = create_partial_value_builder(interp->partials, root);

	interp->partial_value_builders.append(builder_id);
}

static void pop_and_merge_partial_value_builder(Interpreter* interp) noexcept
{
	ASSERT_OR_IGNORE(interp->partial_value_builders.used() >= 2);

	const PartialValueBuilderId inner_builder_id = interp->partial_value_builders.end()[-1];

	const PartialValueBuilderId outer_builder_id = interp->partial_value_builders.end()[-2];

	merge_partial_value_builders(interp->partials, outer_builder_id, inner_builder_id);

	interp->partial_value_builders.pop_by(1);
}

static PartialValueId pop_and_complete_partial_value_builder(Interpreter* interp) noexcept
{
	ASSERT_OR_IGNORE(interp->partial_value_builders.used() != 0);

	const PartialValueBuilderId builder_id = interp->partial_value_builders.end()[-1];

	interp->partial_value_builders.pop_by(1);

	return complete_partial_value_builder(interp->partials, builder_id);
}

void pop_and_discard_partial_value_builder(Interpreter* interp) noexcept
{
	ASSERT_OR_IGNORE(interp->partial_value_builders.used() != 0);

	const PartialValueBuilderId builder_id = interp->partial_value_builders.end()[-1];

	discard_partial_value_builder(interp->partials, builder_id);

	interp->partial_value_builders.pop_by(1);
}

static MutRange<byte> add_partial_value_to_builder(Interpreter* interp, AstNode* node, TypeId type_id, u64 size, u32 align) noexcept
{
	ASSERT_OR_IGNORE(interp->partial_value_builders.used() != 0);

	const PartialValueBuilderId builder_id = interp->partial_value_builders.end()[-1];

	return partial_value_builder_add_value(interp->partials, builder_id, node, type_id, size, align);
}

static void push_partial_value(Interpreter* interp, PartialValueId id) noexcept
{
	ASSERT_OR_IGNORE(id != PartialValueId::INVALID);

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
	ASSERT_OR_IGNORE(mark <= interp->temps.used());

	interp->temps.pop_to(mark);
}

static MutRange<byte> stack_copy_down(Interpreter* interp, u32 mark, MutRange<byte> to_copy) noexcept
{
	ASSERT_OR_IGNORE(mark <= interp->temps.used());

	ASSERT_OR_IGNORE(to_copy.begin() >= interp->temps.begin() + mark && to_copy.end() <= interp->temps.end());

	byte* const new_begin = interp->temps.begin() + mark;

	memmove(new_begin, to_copy.begin(), to_copy.count());

	interp->temps.pop_to(static_cast<u32>(mark + to_copy.count()));

	return { new_begin, to_copy.count() };
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

		const EvalRst type_rst = evaluate(interp, type, EvalSpec{
			ValueKind::Value,
			range::from_object_bytes_mut(&member_type_id),
			type_create_simple(interp->types, TypeTag::Type)
		});

		ASSERT_OR_IGNORE(type_rst.tag == EvalTag::Success && member_type_id != TypeId::INVALID);

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

			const EvalRst value_rst = evaluate(interp, value, EvalSpec{
				ValueKind::Value,
				global_value_get_mut(interp->globals, member_value_id),
				member_type_id
			});

			ASSERT_OR_IGNORE(value_rst.tag == EvalTag::Success);
		}
		else
		{
			const EvalRst value_rst = evaluate(interp, value, EvalSpec{ ValueKind::Value });

			ASSERT_OR_IGNORE(value_rst.tag == EvalTag::Success);

			const TypeMetrics metrics = type_metrics_from_id(interp->types, value_rst.success.type_id);

			member_type_id = value_rst.success.type_id;

			member_value_id = alloc_global_value(interp->globals, metrics.size, metrics.align);

			copy_loc(global_value_get_mut(interp->globals, member_value_id), value_rst.success.bytes.immut());
		}

		arec_restore(interp, restore);
	}

	stack_shrink(interp, mark);

	type_set_composite_member_info(interp->types, surrounding_type_id, member->rank, MemberCompletionInfo{
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
		member->is_mut,
		none<Arec>()
	);
}

static IdentifierInfo location_info_from_arec_and_member(Interpreter* interp, Arec* arec, const Member* member) noexcept
{
	if (member->is_global)
		return location_info_from_global_member(interp, arec->type_id, member);

	const u64 size = type_metrics_from_id(interp->types, member->type.complete).size;

	if (size > UINT32_MAX)
		source_error(interp->errors, SourceId::INVALID, "Size of stack-based location must not exceed 2^32 - 1 bytes.\n");

	const ArecKind kind = static_cast<ArecKind>(arec->kind);

	if (kind == ArecKind::Unbound)
		return IdentifierInfo::make_unbound(arec);

	return IdentifierInfo::make_found(
		MutRange<byte>{ arec->attachment + member->offset, static_cast<u32>(size) },
		member->type.complete,
		member->is_mut,
		some(arec)
	);
}

static bool lookup_identifier_helper(Interpreter* interp, IdentifierId name, OptPtr<Arec>* out_arec, TypeId* out_surrounding_type_id, const Member** out_member) noexcept
{
	ASSERT_OR_IGNORE(interp->active_arec_id != ArecId::INVALID);

	ArecId curr = interp->active_arec_id;

	Arec* arec = arec_from_id(interp, curr);

	while (true)
	{
		if (type_member_by_name(interp->types, arec->type_id, name, out_member))
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
		if (type_member_by_name(interp->types, lex_scope, name, out_member))
		{
			*out_arec = none<Arec>();

			*out_surrounding_type_id = lex_scope;

			return true;
		}

		lex_scope = lexical_parent_type_from_id(interp->types, lex_scope);
	}

	return false;
}

static IdentifierInfo lookup_local_identifier(Interpreter* interp, Arec* arec, IdentifierId name) noexcept
{
	const Member* member;

	if (!type_member_by_name(interp->types, arec->type_id, name, &member))
		ASSERT_UNREACHABLE;

	return location_info_from_arec_and_member(interp, arec, member);
}

static IdentifierInfo lookup_identifier(Interpreter* interp, IdentifierId name) noexcept
{
	OptPtr<Arec> arec;

	TypeId surrounding_type_id;

	const Member* member;

	if (!lookup_identifier_helper(interp, name, &arec, &surrounding_type_id, &member))
		return IdentifierInfo::make_missing();

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



static ClosureId close_over_func_body(Interpreter* interp, TypeId parameter_list_type_id, AstNode* body) noexcept
{
	ClosureBuilderId builder_id = closure_create(interp->closures);

	AstFlatIterator it = flat_ancestors_of(body);

	while (has_next(&it))
	{
		AstNode* const curr = next(&it);

		if (curr->tag != AstTag::Identifier)
			continue;

		const IdentifierId name = attachment_of<AstIdentifierData>(curr)->identifier_id;

		const Member* unused_member;

		if (type_member_by_name(interp->types, parameter_list_type_id, name, &unused_member))
			continue;

		IdentifierInfo info = lookup_identifier(interp, name);

		if (info.tag != IdentifierInfoTag::Found)
			continue;

		if (is_none(info.found.arec))
			continue;

		builder_id = closure_add_value(interp->closures, builder_id, name, info.found.type_id, info.found.location.immut());
	}

	return closure_seal(interp->closures, builder_id);
}

static bool u64_from_integer(Range<byte> value, NumericType type, u64* out) noexcept
{
	u32 count_size = (type.bits + 7) / 8;

	ASSERT_OR_IGNORE(count_size != 0);

	if (type.is_signed && (value[count_size - 1] & 0x80) != 0)
		return false;

	// Check `count`'s value fits into 64 bits if its type is larger than that.
	if (count_size > sizeof(u64))
	{
		while (count_size > sizeof(u64))
		{
			count_size -= 1;

			if (value[count_size] != 0)
				return false;
		}
	}

	u64 result = 0;

	ASSERT_OR_IGNORE(count_size <= sizeof(u64));

	memcpy(&result, value.begin(), count_size);

	*out = result;

	return true;
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
	member.type.pending = is_some(info.type) ? id_from_ast_node(interp->asts, get_ptr(info.type)) : AstNodeId::INVALID;
	member.value.pending = is_some(info.value) ? id_from_ast_node(interp->asts, get_ptr(info.value)) : AstNodeId::INVALID;
	member.is_global = has_flag(definition, AstFlag::Definition_IsGlobal);
	member.is_pub = has_flag(definition, AstFlag::Definition_IsPub);
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

static void convert_comp_integer_to_integer(Interpreter* interp, const AstNode* error_source, EvalSpec spec, CompIntegerValue src_value) noexcept
{
	ASSERT_OR_IGNORE(spec.kind == ValueKind::Value && type_tag_from_id(interp->types, spec.type_id) == TypeTag::Integer);

	const NumericType dst_type = *type_attachment_from_id<NumericType>(interp->types, spec.type_id);

	if (dst_type.is_signed)
	{
		s64 signed_value;

		if (!s64_from_comp_integer(src_value, static_cast<u8>(dst_type.bits), &signed_value))
			source_error(interp->errors, source_id_of(interp->asts, error_source), "Value of integer literal exceeds bounds of signed %u-bit integer.\n", dst_type.bits);

		const Range<byte> value_range = Range<byte>{ reinterpret_cast<byte*>(&signed_value), static_cast<u64>((dst_type.bits + 7) / 8) };

		copy_loc(spec.dst, value_range);
	}
	else
	{
		u64 unsigned_value;

		if (!u64_from_comp_integer(src_value, static_cast<u8>(dst_type.bits), &unsigned_value))
			source_error(interp->errors, source_id_of(interp->asts, error_source), "Value of integer literal exceeds bounds of unsigned %u-bit integer.\n", dst_type.bits);

		const Range<byte> value_range = Range<byte>{ reinterpret_cast<byte*>(&unsigned_value), static_cast<u64>((dst_type.bits + 7) / 8) };

		copy_loc(spec.dst, value_range);
	}
}

static void convert_comp_float_to_float(Interpreter* interp, EvalSpec spec, CompFloatValue src_value) noexcept
{
	ASSERT_OR_IGNORE(spec.kind == ValueKind::Value && type_tag_from_id(interp->types, spec.type_id) == TypeTag::Integer);

	const NumericType dst_type = *type_attachment_from_id<NumericType>(interp->types, spec.type_id);

	ASSERT_OR_IGNORE(!dst_type.is_signed);

	if (dst_type.bits == 32)
	{
		store_loc(spec.dst, f32_from_comp_float(src_value));
	}
	else
	{
		ASSERT_OR_IGNORE(dst_type.bits == 64);

		store_loc(spec.dst, f64_from_comp_float(src_value));
	}
}

static void convert_array_to_slice(Interpreter* interp, const AstNode* error_source, EvalSpec dst_spec, EvalSpec src_spec) noexcept
{
	ASSERT_OR_IGNORE(dst_spec.type_id != TypeId::INVALID
				  && dst_spec.dst.begin() != nullptr
				  && dst_spec.dst.count() == sizeof(MutRange<byte>)
				  && type_tag_from_id(interp->types, dst_spec.type_id) == TypeTag::Slice
	);

	const ArrayType src_type = *type_attachment_from_id<ArrayType>(interp->types, src_spec.type_id);

	const ReferenceType dst_type = *type_attachment_from_id<ReferenceType>(interp->types, dst_spec.type_id);

	if (!type_is_equal(interp->types, src_type.element_type, dst_type.referenced_type_id))
		source_error(interp->errors, source_id_of(interp->asts, error_source), "Cannot implicitly convert array to slice with different element type");

	store_loc(dst_spec.dst, src_spec.dst);
}

static void convert(Interpreter* interp, const AstNode* error_source, EvalSpec dst_spec, EvalSpec src_spec) noexcept
{
	ASSERT_OR_IGNORE(dst_spec.type_id != TypeId::INVALID
				  && dst_spec.dst.begin() != nullptr
				  && src_spec.type_id != TypeId::INVALID
				  && src_spec.dst.begin() != nullptr
	);

	const TypeTag src_type_tag = type_tag_from_id(interp->types, src_spec.type_id);

	if (dst_spec.kind == ValueKind::Value && src_spec.kind == ValueKind::Location)
	{
		src_spec.kind = ValueKind::Value;
		src_spec.dst = load_loc<MutRange<byte>>(src_spec.dst);
	}

	switch (src_type_tag)
	{
	case TypeTag::CompInteger:
	{
		convert_comp_integer_to_integer(interp, error_source, dst_spec, load_loc<CompIntegerValue>(src_spec.dst));

		return;
	}

	case TypeTag::CompFloat:
	{
		convert_comp_float_to_float(interp, dst_spec, load_loc<CompFloatValue>(src_spec.dst));

		return;
	}

	case TypeTag::Array:
	{
		convert_array_to_slice(interp, error_source, dst_spec, src_spec);

		return;
	}

	case TypeTag::INVALID:
	case TypeTag::INDIRECTION:
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

static EvalRst fill_spec(Interpreter* interp, EvalSpec spec, const AstNode* error_source, ValueKind inferred_kind, TypeId inferred_type_id) noexcept
{
	if (spec.kind == ValueKind::Location && inferred_kind == ValueKind::Value)
		source_error(interp->errors, source_id_of(interp->asts, error_source), "Cannot convert value to location.\n");

	TypeId type_id;

	MutRange<byte> bytes;

	if (spec.type_id == TypeId::INVALID)
	{
		type_id = inferred_type_id;

		if (spec.dst.begin() != nullptr)
		{
			bytes = spec.dst;
		}
		else if (spec.kind == ValueKind::Location)
		{
			bytes = stack_push(interp, sizeof(MutRange<byte>), alignof(MutRange<byte>));
		}
		else
		{
			const TypeMetrics metrics = type_metrics_from_id(interp->types, type_id);

			bytes = stack_push(interp, metrics.size, metrics.align);
		}
	}
	else if (type_can_implicitly_convert_from_to(interp->types, inferred_type_id, spec.type_id))
	{
		type_id = spec.type_id;

		bytes = spec.dst;
	}
	else
	{
		source_error(interp->errors, source_id_of(interp->asts, error_source), "Cannot convert to desired type.\n");
	}

	EvalRst rst;
	rst.tag = EvalTag::Success;
	rst.success.kind = spec.kind;
	rst.success.type_id = type_id;
	rst.success.bytes = bytes;

	return rst;
}

static EvalRst fill_spec_sized(Interpreter* interp, EvalSpec spec, const AstNode* error_source, ValueKind inferred_kind, TypeId inferred_type_id, u64 size, u32 align) noexcept
{
	if (spec.kind == ValueKind::Location && inferred_kind == ValueKind::Value)
		source_error(interp->errors, source_id_of(interp->asts, error_source), "Cannot convert value to location.\n");

	TypeId type_id;

	MutRange<byte> bytes;

	if (spec.type_id == TypeId::INVALID)
	{
		type_id = inferred_type_id;

		if (spec.dst.begin() != nullptr)
		{
			bytes = spec.dst;
		}
		else if (spec.kind == ValueKind::Location)
		{
			bytes = stack_push(interp, sizeof(MutRange<byte>), alignof(MutRange<byte>));
		}
		else
		{
			bytes = stack_push(interp, size, align);
		}
	}
	else if (type_can_implicitly_convert_from_to(interp->types, inferred_type_id, spec.type_id))
	{
		type_id = spec.type_id;

		bytes = spec.dst;
	}
	else
	{
		source_error(interp->errors, source_id_of(interp->asts, error_source), "Cannot convert to desired type.\n");
	}

	EvalRst rst;
	rst.tag = EvalTag::Success;
	rst.success.kind = spec.kind;
	rst.success.type_id = type_id;
	rst.success.bytes = bytes;

	return rst;
}

static EvalRst evaluate_global_member(Interpreter* interp, AstNode* node, EvalSpec spec, TypeId surrounding_type_id, const Member* member) noexcept
{
	ASSERT_OR_IGNORE(member->is_global);

	complete_member(interp, surrounding_type_id, member);

	EvalRst rst = fill_spec(interp, spec, node, ValueKind::Location, member->type.complete);

	MutRange<byte> value = global_value_get_mut(interp->globals, member->value.complete);

	if (type_is_equal(interp->types, rst.success.type_id, member->type.complete))
	{
		if (rst.success.kind == ValueKind::Location)
		{
			store_loc(rst.success.bytes, value);
		}
		else
		{
			copy_loc(rst.success.bytes, value.immut());
		}
	}
	else if (rst.success.kind == ValueKind::Value)
	{
		convert(interp, node, EvalSpec{ rst.success.kind, rst.success.bytes, rst.success.type_id }, EvalSpec{ ValueKind::Location, value, member->type.complete });
	}
	else
	{
		const Range<char8> name = identifier_name_from_id(interp->identifiers, attachment_of<AstMemberData>(node)->identifier_id);

		source_error(interp->errors, source_id_of(interp->asts, node), "Cannot use member `%.*s` as a location of the desired type, as it requires implicit conversion.\n", static_cast<s32>(name.count()), name.begin());
	}

	// TODO: This should probably carry out some sort of information from
	// `complete_member`.
	return rst;
}

static EvalRst evaluate_local_member(Interpreter* interp, AstNode* node, EvalSpec spec, TypeId surrounding_type_id, const Member* member, MutRange<byte> lhs_value) noexcept
{
	ASSERT_OR_IGNORE(!member->is_global);

	complete_member(interp, surrounding_type_id, member);

	const TypeMetrics metrics = type_metrics_from_id(interp->types, member->type.complete);

	EvalRst rst = fill_spec_sized(interp, spec, node, ValueKind::Location, member->type.complete, metrics.size, metrics.align);

	if (type_is_equal(interp->types, rst.success.type_id, member->type.complete))
	{
		if (rst.success.kind == ValueKind::Location)
		{
			store_loc(rst.success.bytes, MutRange<byte>{ lhs_value.begin() + member->offset, metrics.size });
		}
		else
		{
			copy_loc(rst.success.bytes, Range<byte>{ lhs_value.begin() + member->offset, metrics.size });
		}
	}
	else if (rst.success.kind == ValueKind::Value)
	{
		convert(interp, node, EvalSpec{ rst.success.kind, rst.success.bytes, rst.success.type_id }, EvalSpec{ ValueKind::Location, lhs_value, member->type.complete });
	}
	else
	{
		const Range<char8> name = identifier_name_from_id(interp->identifiers, attachment_of<AstMemberData>(node)->identifier_id);

		source_error(interp->errors, source_id_of(interp->asts, node), "Cannot use member `%.*s` as a location of the desired type, as it requires implicit conversion.\n", static_cast<s32>(name.count()), name.begin());
	}

	// TODO: This should probably carry out some sort of information from
	// `complete_member`.
	return rst;
}

static CallInfo setup_call_args(Interpreter* interp, const SignatureType* signature_type, AstNode* callee) noexcept
{
	if (signature_type->parameter_list_is_unbound || signature_type->return_type_is_unbound)
		push_partial_value(interp, signature_type->partial_value_id);

	const TypeId parameter_list_type_id = signature_type->parameter_list_is_unbound
		? type_copy_composite(interp->types, signature_type->parameter_list_type_id, signature_type->param_count, true)
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
		{
			complete_member(interp, parameter_list_type_id, param);
		}
		else if (type_tag_from_id(interp->types, param->type.complete) == TypeTag::Func)
		{
			TODO("Add binding of nested signatures");
		}


		const TypeMetrics param_metrics = type_metrics_from_id(interp->types, param->type.complete);

		if (has_pending_type)
			arec_grow(interp, parameter_list_arec_id, param->offset + param_metrics.size);

		const EvalRst arg_rst = evaluate(interp, arg, EvalSpec{
			ValueKind::Value,
			MutRange<byte>{ parameter_list_arec->attachment + param->offset, param_metrics.size },
			param->type.complete
		});

		ASSERT_OR_IGNORE(arg_rst.tag == EvalTag::Success);

		arg_rank += 1;
	}

	arec_restore(interp, restore);

	TypeId return_type_id;

	if (signature_type->return_type_is_unbound)
	{
		AstNode* const return_type = ast_node_from_id(interp->asts, signature_type->return_type.partial_root);

		EvalRst return_type_rst = evaluate(interp, return_type, EvalSpec{
			ValueKind::Value,
			range::from_object_bytes_mut(&return_type_id),
			type_create_simple(interp->types, TypeTag::Type)
		});

		ASSERT_OR_IGNORE(return_type_rst.tag == EvalTag::Success);
	}
	else
	{
		return_type_id = signature_type->return_type.complete;
	}

	if (signature_type->parameter_list_is_unbound || signature_type->return_type_is_unbound)
		pop_partial_value(interp);

	return { return_type_id, parameter_list_arec_id };
}

static EvalRst evaluate(Interpreter* interp, AstNode* node, EvalSpec spec) noexcept
{
	PartialValue applicable_partial;

	if (has_applicable_partial_value(interp, node, &applicable_partial))
	{
		EvalRst rst = fill_spec(interp, spec, node, ValueKind::Value, applicable_partial.type_id);

		if (type_is_equal(interp->types, applicable_partial.type_id, rst.success.type_id))
		{
			copy_loc(rst.success.bytes, applicable_partial.data);
		}
		else
		{
			convert(interp, node, EvalSpec{ rst.success.kind, rst.success.bytes, rst.success.type_id }, EvalSpec{ ValueKind::Value,	{ const_cast<byte*>(applicable_partial.data.begin()), applicable_partial.data.count() }, applicable_partial.type_id	});
		}

		return rst;
	}
	else if (spec.type_id != TypeId::INVALID && type_tag_from_id(interp->types, spec.type_id) == TypeTag::TypeInfo)
	{
		const TypeId type_type_id = type_create_simple(interp->types, TypeTag::Type);

		EvalRst rst = fill_spec_sized(interp, spec, node, ValueKind::Value, type_type_id, sizeof(TypeId), alignof(TypeId));

		const TypeId expression_type = typeinfer(interp, node);

		store_loc(rst.success.bytes, expression_type);

		return rst;
	}
	else switch (node->tag)
	{
	case AstTag::Builtin:
	{
		const u8 ordinal = static_cast<u8>(node->flags);

		ASSERT_OR_IGNORE(ordinal < array_count(interp->builtin_type_ids));

		const TypeId builtin_type_id = interp->builtin_type_ids[ordinal];

		EvalRst rst = fill_spec_sized(interp, spec, node, ValueKind::Value, builtin_type_id, sizeof(CallableValue), alignof(CallableValue));

		store_loc(rst.success.bytes, CallableValue::from_builtin(builtin_type_id, ordinal));

		return rst;
	}

	case AstTag::ArrayInitializer:
	{
		AstDirectChildIterator elems = direct_children_of(node);

		u32 elem_count = 0;

		while (has_next(&elems))
		{
			(void) next(&elems);

			elem_count += 1;
		}

		const u32 mark = stack_mark(interp);

		MutAttachmentRange<byte, TypeIdAndValueKind>* const values = reinterpret_cast<MutAttachmentRange<byte, TypeIdAndValueKind>*>(stack_push(interp, elem_count * sizeof(MutRange<byte>), alignof(MutRange<byte>)).begin());

		elems = direct_children_of(node);

		Arec* unbound_in = nullptr;

		TypeId unified_elem_type_id = TypeId::INVALID;

		for (u32 i = 0; i != elem_count; ++i)
		{
			AstNode* const elem = next(&elems);

			const EvalRst elem_rst = evaluate(interp, elem, EvalSpec{
				ValueKind::Value
			});

			if (elem_rst.tag == EvalTag::Success)
			{
				values[i] = { elem_rst.success.bytes, TypeIdAndValueKind{ static_cast<u32>(elem_rst.success.type_id), static_cast<u32>(elem_rst.success.kind) } };

				if (unified_elem_type_id == TypeId::INVALID)
				{
					unified_elem_type_id = elem_rst.success.type_id;
				}
				else
				{
					unified_elem_type_id = type_unify(interp->types, unified_elem_type_id, elem_rst.success.type_id);

					if (unified_elem_type_id == TypeId::INVALID)
						source_error(interp->errors, source_id_of(interp->asts, elem), "Array literal element cannot be implicitly converted to type of preceding elements.\n");
				}
			}
			else
			{
				if (unbound_in == nullptr || unbound_in > elem_rst.unbound.in)
					unbound_in = elem_rst.unbound.in;

				values[i] = {};
			}
		}

		ASSERT_OR_IGNORE(!has_next(&elems));

		if (unbound_in != nullptr)
		{
			elems = direct_children_of(node);

			for (u32 i = 0; i != elem_count; ++i)
			{
				AstNode* const elem = next(&elems);

				MutAttachmentRange<byte, TypeIdAndValueKind> value = values[i];

				if (value.begin() == nullptr)
					continue;

				const TypeId value_type_id = static_cast<TypeId>(value.attachment().type_id_bits);

				const TypeMetrics value_metrics = type_metrics_from_id(interp->types, value_type_id);

				const MutRange<byte> value_dst = add_partial_value_to_builder(interp, elem, value_type_id, value_metrics.size, value_metrics.align);

				const Range<byte> value_src = static_cast<ValueKind>(value.attachment().value_kind_bits) == ValueKind::Location
					? load_loc<Range<byte>>(value.mut_range())
					: value.range();

				copy_loc(value_dst, value_src);
			}

			ASSERT_OR_IGNORE(!has_next(&elems));

			stack_shrink(interp, mark);

			return eval_unbound(unbound_in);
		}
		else
		{
			const TypeId inferred_type_id = type_create_array(interp->types, TypeTag::ArrayLiteral, ArrayType{ elem_count, unified_elem_type_id });

			EvalRst rst = fill_spec(interp, spec, node, ValueKind::Value, inferred_type_id);

			const ArrayType* const rst_type = type_attachment_from_id<ArrayType>(interp->types, rst.success.type_id);

			unified_elem_type_id = rst_type->element_type;

			const TypeMetrics elem_metrics = unified_elem_type_id == TypeId::INVALID
				? TypeMetrics{ 0, 0, 1 }
				: type_metrics_from_id(interp->types, unified_elem_type_id);

			elems = direct_children_of(node);

			for (u32 i = 0; i != elem_count; ++i)
			{
				AstNode* const elem = next(&elems);

				const MutAttachmentRange<byte, TypeIdAndValueKind> value = values[i];

				const TypeId value_type_id = static_cast<TypeId>(value.attachment().type_id_bits);

				MutRange<byte> elem_value_dst = rst.success.bytes.mut_subrange(i * elem_metrics.stride, elem_metrics.size);

				if (type_is_equal(interp->types, value_type_id, unified_elem_type_id))
				{
					copy_loc(elem_value_dst, value.range());
				}
				else
				{
					convert(interp, elem, EvalSpec{
						ValueKind::Value,
						elem_value_dst,
						unified_elem_type_id
					}, EvalSpec{
						static_cast<ValueKind>(value.attachment().value_kind_bits),
						value.mut_range(),
						value_type_id
					});
				}
			}

			ASSERT_OR_IGNORE(!has_next(&elems));

			if (spec.dst.begin() == nullptr)
				rst.success.bytes = stack_copy_down(interp, mark, rst.success.bytes);
			else
				stack_shrink(interp, mark);

			return rst;
		}
	}

	case AstTag::Definition:
	{
		const TypeId definition_type_id = type_create_simple(interp->types, TypeTag::Definition);

		EvalRst rst = fill_spec_sized(interp, spec, node, ValueKind::Value, definition_type_id, sizeof(Definition), alignof(Definition));

		const DefinitionInfo info = get_definition_info(node);

		const AstDefinitionData attach = *attachment_of<AstDefinitionData>(node);

		Definition* const definition = reinterpret_cast<Definition*>(rst.success.bytes.begin());
		memset(definition, 0, sizeof(*definition));
		definition->name = attach.identifier_id;
		definition->type.pending = is_some(info.type) ? id_from_ast_node(interp->asts, get_ptr(info.type)) : AstNodeId::INVALID;
		definition->default_or_global_value.pending = is_some(info.value) ? id_from_ast_node(interp->asts, get_ptr(info.value)) : AstNodeId::INVALID;
		definition->is_global = has_flag(node, AstFlag::Definition_IsGlobal);
		definition->is_pub = has_flag(node, AstFlag::Definition_IsPub);
		definition->is_mut = has_flag(node, AstFlag::Definition_IsMut);

		return rst;
	}

	case AstTag::Block:
	{
		const TypeId block_type_id = type_create_composite(interp->types, active_arec_type_id(interp), TypeDisposition::Block, SourceId::INVALID, 0, false);

		const ArecId block_arec_id = arec_push(interp, block_type_id, 0, 1, active_arec_id(interp), ArecKind::Normal);

		Arec* const block_arec = active_arec(interp);

		const u32 mark = stack_mark(interp);

		u16 definition_count = 0;

		AstDirectChildIterator stmts = direct_children_of(node);

		while (has_next(&stmts))
		{
			AstNode* const stmt = next(&stmts);

			if (stmt->tag == AstTag::Definition && has_next_sibling(stmt))
			{
				DefinitionInfo info = get_definition_info(stmt);

				if (is_none(info.value))
					source_error(interp->errors, source_id_of(interp->asts, stmt), "Block-level definition must have a value.\n");

				if (has_flag(stmt, AstFlag::Definition_IsGlobal))
					source_error(interp->errors, source_id_of(interp->asts, stmt), "`global` is not (yet) supported on block-level definitions.\n");

				if (has_flag(stmt, AstFlag::Definition_IsPub))
					source_error(interp->errors, source_id_of(interp->asts, stmt), "`pub` is not supported on block-level definitions.\n");

				if (is_some(info.type))
				{
					TypeId type_id;

					const EvalRst type_rst = evaluate(interp, get_ptr(info.type), EvalSpec{
						ValueKind::Value,
						range::from_object_bytes_mut(&type_id),
						type_create_simple(interp->types, TypeTag::Type)
					});

					ASSERT_OR_IGNORE(type_rst.tag == EvalTag::Success);

					Member member_init;
					member_init.name = attachment_of<AstDefinitionData>(stmt)->identifier_id;
					member_init.type.complete = type_id;
					member_init.value.complete = GlobalValueId::INVALID;
					member_init.is_global = false;
					member_init.is_pub = false;
					member_init.is_mut = has_flag(stmt, AstFlag::Definition_IsMut);
					member_init.is_param = false;
					member_init.has_pending_type = false;
					member_init.has_pending_value = false;
					member_init.is_comptime_known = false;
					member_init.is_arg_independent = false;
					member_init.rank = 0;
					member_init.type_completion_arec_id = ArecId::INVALID;
					member_init.value_completion_arec_id = ArecId::INVALID;
					member_init.offset = 0;

					if (!type_add_composite_member(interp->types, block_type_id, member_init))
					{
						const Range<char8> name = identifier_name_from_id(interp->identifiers, attachment_of<AstDefinitionData>(stmt)->identifier_id);

						source_error(interp->errors, source_id_of(interp->asts, stmt), "Variable with name `%.*s` is already defined.\n", static_cast<s32>(name.count()), name.begin());
					}

					const Member* member = type_member_by_rank(interp->types, block_type_id, definition_count);

					const TypeMetrics metrics = type_metrics_from_id(interp->types, type_id);

					arec_grow(interp, block_arec_id, member->offset + metrics.size);

					const EvalRst value_rst = evaluate(interp, get_ptr(info.value), EvalSpec{
						ValueKind::Value,
						MutRange<byte>{ block_arec->attachment + member->offset, metrics.size },
						type_id
					});

					ASSERT_OR_IGNORE(value_rst.tag == EvalTag::Success);
				}
				else
				{
					const EvalRst value_rst = evaluate(interp, get_ptr(info.value), EvalSpec{ ValueKind::Value });

					ASSERT_OR_IGNORE(value_rst.tag == EvalTag::Success);

					Member member_init;
					member_init.name = attachment_of<AstDefinitionData>(stmt)->identifier_id;
					member_init.type.complete = value_rst.success.type_id;
					member_init.value.complete = GlobalValueId::INVALID;
					member_init.is_global = false;
					member_init.is_pub = false;
					member_init.is_mut = has_flag(stmt, AstFlag::Definition_IsMut);
					member_init.is_param = false;
					member_init.has_pending_type = false;
					member_init.has_pending_value = false;
					member_init.is_comptime_known = false;
					member_init.is_arg_independent = false;
					member_init.rank = 0;
					member_init.type_completion_arec_id = ArecId::INVALID;
					member_init.value_completion_arec_id = ArecId::INVALID;
					member_init.offset = 0;

					if (!type_add_composite_member(interp->types, block_type_id, member_init))
					{
						const Range<char8> name = identifier_name_from_id(interp->identifiers, attachment_of<AstDefinitionData>(stmt)->identifier_id);

						source_error(interp->errors, source_id_of(interp->asts, stmt), "Variable with name `%.*s` is already defined.\n", static_cast<s32>(name.count()), name.begin());
					}

					const Member* member = type_member_by_rank(interp->types, block_type_id, definition_count);

					const TypeMetrics metrics = type_metrics_from_id(interp->types, value_rst.success.type_id);

					arec_grow(interp, block_arec_id, member->offset + metrics.size);

					copy_loc(MutRange<byte>{ block_arec->attachment + member->offset, metrics.size }, value_rst.success.bytes.immut());
				}

				definition_count += 1;
			}
			else
			{
				EvalSpec stmt_spec;

				if (has_next_sibling(stmt))
				{
					// Just take some old non-null address here for `dst`.
					// It won't be written to, due to its size being 0.
					stmt_spec = EvalSpec{
						ValueKind::Value,
						MutRange<byte>{ reinterpret_cast<byte*>(&stmt_spec), static_cast<u64>(0) },
						type_create_simple(interp->types, TypeTag::Void)
					};
				}
				else
				{
					stmt_spec = EvalSpec{ ValueKind::Value, spec.dst, spec.type_id };
				}

				const EvalRst stmt_rst = evaluate(interp, stmt, stmt_spec);

				ASSERT_OR_IGNORE(stmt_rst.tag == EvalTag::Success);

				const TypeMetrics metrics = type_metrics_from_id(interp->types, stmt_rst.success.type_id);

				EvalRst rst = fill_spec_sized(interp, spec, node, ValueKind::Value, stmt_rst.success.type_id, metrics.size, metrics.align);

				if (rst.tag == EvalTag::Unbound)
					source_error(interp->errors, source_id_of(interp->asts, node), "Cannot use block in unbound context.\n");

				if (!has_next_sibling(stmt))
				{
					// This is super hacky; We basically forget about the stack
					// memory just allocated for `rst`, move our result down to the
					// shrunken stack's top, and copy the resulting location into
					// `rst`.
					if (spec.dst.begin() == nullptr)
						rst.success.bytes = stack_copy_down(interp, mark, stmt_rst.success.bytes);
					else
						stack_shrink(interp, mark);

					arec_pop(interp, block_arec_id);

					type_discard(interp->types, block_type_id);

					return rst;
				}
			}
		}

		stack_shrink(interp, mark);

		arec_pop(interp, block_arec_id);

		type_discard(interp->types, block_type_id);

		return fill_spec_sized(interp, spec, node, ValueKind::Value, type_create_simple(interp->types, TypeTag::Void), 0, 1);
	}

	case AstTag::Func:
	{
		AstNode* const signature = first_child_of(node);

		TypeId signature_type_id;

		const EvalRst signature_rst = evaluate(interp, signature, EvalSpec{
			ValueKind::Value,
			range::from_object_bytes_mut(&signature_type_id),
			type_create_simple(interp->types, TypeTag::Type)
		});

		if (signature_rst.tag == EvalTag::Unbound)
			return signature_rst;

		if (type_tag_from_id(interp->types, signature_type_id) != TypeTag::Func)
			source_error(interp->errors, source_id_of(interp->asts, signature), "Function signature must be of type `Signature`.\n");

		EvalRst rst = fill_spec_sized(interp, spec, node, ValueKind::Value, signature_type_id, sizeof(CallableValue), alignof(CallableValue));

		AstNode* const body = next_sibling_of(signature);

		const AstNodeId body_id = id_from_ast_node(interp->asts, body);

		const SignatureType* const signature_type = type_attachment_from_id<SignatureType>(interp->types, signature_type_id);

		const ClosureId closure_id = close_over_func_body(interp, signature_type->parameter_list_type_id, body);

		const CallableValue callable = CallableValue::from_function(signature_type_id, body_id, closure_id);

		store_loc(rst.success.bytes, callable);

		return rst;
	}

	case AstTag::Signature:
	{
		const TypeId type_type_id = type_create_simple(interp->types, TypeTag::Type);

		EvalRst rst = fill_spec_sized(interp, spec, node, ValueKind::Value, type_type_id, sizeof(TypeId), alignof(TypeId));

		SignatureInfo info = get_signature_info(node);

		const TypeId parameter_list_type_id = type_create_composite(interp->types, active_arec_type_id(interp), TypeDisposition::Signature, SourceId::INVALID, 0, false);

		const ArecId parameter_list_arec_id = arec_push(interp, parameter_list_type_id, 0, 1, active_arec_id(interp), ArecKind::Unbound);

		Arec* const parameter_list_arec = arec_from_id(interp, parameter_list_arec_id);

		u8 param_count = 0;

		AstDirectChildIterator params = direct_children_of(info.parameters);

		while (has_next(&params))
		{
			AstNode* const param = next(&params);

			ASSERT_OR_IGNORE(param->tag == AstTag::Parameter);

			if (param_count == 63)
				source_error(interp->errors, source_id_of(interp->asts, param), "Exceeded maximum of 64 function parameters.\n");

			Member param_member = delayed_member_from(interp, param);
			param_member.is_global = false;

			if (!type_add_composite_member(interp->types, parameter_list_type_id, param_member))
			{
				const Range<char8> name = identifier_name_from_id(interp->identifiers, attachment_of<AstParameterData>(param)->identifier_id);
				
				source_error(interp->errors, source_id_of(interp->asts, param), "More than one parameter with name `%.*s`.\n", static_cast<s32>(name.count()), name.begin());
			}

			param_count += 1;
		}

		type_seal_composite(interp->types, parameter_list_type_id, 0, 0, 0);

		push_partial_value_builder(interp, node);

		Arec* outermost_unbound = parameter_list_arec + 1;

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

				const EvalRst type_rst = evaluate(interp, type, EvalSpec{
					ValueKind::Value,
					range::from_object_bytes_mut(&member_type_id),
					type_create_simple(interp->types, TypeTag::Type)
				});

				if (type_rst.tag == EvalTag::Unbound)
				{
					type_is_unbound = true;

					has_unbound_parameter = true;

					if (type_rst.unbound.in < outermost_unbound)
						outermost_unbound = type_rst.unbound.in;
				}
			}

			if (member->has_pending_value)
			{
				AstNode* const value = ast_node_from_id(interp->asts, member->value.pending);

				if (member_type_id == TypeId::INVALID)
				{
					const u32 mark = stack_mark(interp);

					const EvalRst value_rst = evaluate(interp, value, EvalSpec{
						ValueKind::Value,
					});

					if (value_rst.tag == EvalTag::Unbound)
					{
						ASSERT_OR_IGNORE(value_rst.unbound.in != parameter_list_arec);

						has_unbound_parameter = true;

						if (value_rst.unbound.in < outermost_unbound)
							outermost_unbound = value_rst.unbound.in;
					}
					else if (type_is_unbound)
					{
						const TypeMetrics metrics = type_metrics_from_id(interp->types, value_rst.success.type_id);

						add_partial_value_to_builder(interp, value, value_rst.success.type_id, metrics.size, metrics.align);
					}
					else
					{
						const TypeMetrics metrics = type_metrics_from_id(interp->types, value_rst.success.type_id);

						member_value_id = alloc_global_value(interp->globals, metrics.size, metrics.align);

						MutRange<byte> member_value = global_value_get_mut(interp->globals, member_value_id);

						ASSERT_OR_IGNORE(member_value.count() == value_rst.success.bytes.count());

						memcpy(member_value.begin(), value_rst.success.bytes.begin(), member_value.count());

						member_type_id = value_rst.success.type_id;
					}

					stack_shrink(interp, mark);
				}
				else
				{
					const TypeMetrics metrics = type_metrics_from_id(interp->types, member_type_id);

					member_value_id = alloc_global_value(interp->globals, metrics.size, metrics.align);

					MutRange<byte> member_value = global_value_get_mut(interp->globals, member_value_id);

					const EvalRst value_rst = evaluate(interp, value, EvalSpec{
						ValueKind::Value,
						member_value,
						member_type_id
					});

					if (value_rst.tag == EvalTag::Unbound)
						TODO("Handle unbound implicitly typed default values.");
				}
			}

			type_set_composite_member_info(interp->types, parameter_list_type_id, member->rank, MemberCompletionInfo{
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

			EvalRst return_type_rst = evaluate(interp, return_type, EvalSpec{
				ValueKind::Value,
				range::from_object_bytes_mut(&return_type_id),
				type_create_simple(interp->types, TypeTag::Type)
			});

			has_unbound_return_type = return_type_rst.tag == EvalTag::Unbound;

			if (has_unbound_return_type && return_type_rst.unbound.in < outermost_unbound)
				outermost_unbound = return_type_rst.unbound.in;
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

		if (outermost_unbound < parameter_list_arec)
		{
			// Merge the inner (current) `PartialValueBuilder` into the outer
			// one. Since completed members aren't stored in the current
			// builder, add them as well.
			// Note that we add them directly to the outer builder instead of
			// the inner to avoid redundant copies from inner to outer.

			pop_and_merge_partial_value_builder(interp);

			MemberIterator complete_members = members_of(interp->types, parameter_list_type_id);

			AstDirectChildIterator complete_params = direct_children_of(info.parameters);

			while (has_next(&complete_members))
			{
				const Member* const member = next(&complete_members);

				AstNode* const param = next(&complete_params);

				DefinitionInfo param_info = get_definition_info(param);

				if (is_some(param_info.type) && !member->has_pending_type)
				{
					MutRange<byte> type_dst = add_partial_value_to_builder(interp, get_ptr(param_info.type), type_type_id, sizeof(TypeId), alignof(TypeId));

					store_loc(type_dst, member->type.complete);
				}

				if (is_some(param_info.value) && !member->has_pending_value)
				{
					ASSERT_OR_IGNORE(!member->has_pending_type);

					const TypeMetrics metrics = type_metrics_from_id(interp->types, member->type.complete);

					MutRange<byte> value_dst = add_partial_value_to_builder(interp, get_ptr(param_info.value), member->type.complete, metrics.size, metrics.align);

					Range<byte> value_src = global_value_get(interp->globals, member->value.complete);

					copy_loc(value_dst, value_src);
				}
			}

			ASSERT_OR_IGNORE(!has_next(&complete_params));

			return eval_unbound(outermost_unbound);
		}
		else
		{
			PartialValueId partial_value_id;

			if (has_unbound_parameter || has_unbound_return_type)
			{
				partial_value_id = pop_and_complete_partial_value_builder(interp);
			}
			else
			{
				pop_and_discard_partial_value_builder(interp);

				partial_value_id = PartialValueId::INVALID;
			}

			ASSERT_OR_IGNORE(outermost_unbound == parameter_list_arec || outermost_unbound == parameter_list_arec + 1);

			SignatureType signature_type{};
			signature_type.parameter_list_type_id = parameter_list_type_id;
			signature_type.partial_value_id = partial_value_id;
			signature_type.param_count = param_count;
			signature_type.is_proc = has_flag(node, AstFlag::Signature_IsProc);
			signature_type.parameter_list_is_unbound = has_unbound_parameter;
			signature_type.return_type_is_unbound = has_unbound_return_type;

			if (has_unbound_return_type)
				signature_type.return_type.partial_root = id_from_ast_node(interp->asts, get_ptr(info.return_type));
			else
				signature_type.return_type.complete = return_type_id;

			const TypeId signature_type_id = type_create_signature(interp->types, TypeTag::Func, signature_type);

			store_loc(rst.success.bytes, signature_type_id);

			return rst;
		}
	}

	case AstTag::Identifier:
	{
		const AstIdentifierData attach = *attachment_of<AstIdentifierData>(node);

		const IdentifierInfo info = lookup_identifier(interp, attach.identifier_id);

		if (info.tag == IdentifierInfoTag::Missing)
		{
			const Range<char8> name = identifier_name_from_id(interp->identifiers, attach.identifier_id);

			source_error(interp->errors, source_id_of(interp->asts, node), "Identifier '%.*s' is not defined.\n", static_cast<s32>(name.count()), name.begin());
		}
		else if (info.tag == IdentifierInfoTag::Unbound)
		{
			return eval_unbound(info.unbound.source);
		}

		ASSERT_OR_IGNORE(info.tag == IdentifierInfoTag::Found);

		EvalRst rst = fill_spec(interp, spec, node, ValueKind::Location, info.found.type_id);

		if (type_is_equal(interp->types, info.found.type_id, rst.success.type_id))
		{
			if (rst.success.kind == ValueKind::Location)
				store_loc(rst.success.bytes, info.found.location);
			else
				copy_loc(rst.success.bytes, info.found.location.immut());
		}
		else if (rst.success.kind == ValueKind::Value)
		{
			TODO("Implement implicit conversion of identifier values");
		}
		else
		{
			const Range<char8> name = identifier_name_from_id(interp->identifiers, attach.identifier_id);

			source_error(interp->errors, source_id_of(interp->asts, node), "Cannot treat identifier '%.*s' as location as it requires an implicit conversion to conform to the desired type.\n", static_cast<s32>(name.count()), name.begin());
		}

		return rst;
	}

	case AstTag::LitInteger:
	{
		const TypeId comp_integer_type_id = type_create_simple(interp->types, TypeTag::CompInteger);

		EvalRst rst = fill_spec_sized(interp, spec, node, ValueKind::Value, comp_integer_type_id, sizeof(CompIntegerValue), alignof(CompIntegerValue));

		const AstLitIntegerData attach = *attachment_of<AstLitIntegerData>(node);

		if (type_is_equal(interp->types, rst.success.type_id, comp_integer_type_id))
			store_loc(rst.success.bytes, attach.value);
		else
			convert_comp_integer_to_integer(interp, node, EvalSpec{ rst.success.kind, rst.success.bytes, rst.success.type_id }, attach.value);

		return rst;
	}

	case AstTag::LitString:
	{
		const AstLitStringData attach = *attachment_of<AstLitStringData>(node);

		EvalRst rst = fill_spec(interp, spec, node, ValueKind::Location, attach.string_type_id);

		MutRange<byte> value = global_value_get_mut(interp->globals, attach.string_value_id);

		if (type_is_equal(interp->types, rst.success.type_id, attach.string_type_id))
		{
			if (rst.success.kind == ValueKind::Value)
				copy_loc(rst.success.bytes, value.immut());
			else
				store_loc(rst.success.bytes, value.immut());
		}
		else if (rst.success.kind == ValueKind::Value)
		{
			ASSERT_OR_IGNORE(type_tag_from_id(interp->types, rst.success.type_id) == TypeTag::Slice);

			convert_array_to_slice(interp, node, EvalSpec{ rst.success.kind, rst.success.bytes, rst.success.type_id }, EvalSpec{ ValueKind::Value, value, attach.string_type_id });
		}
		else
		{
			source_error(interp->errors, source_id_of(interp->asts, node), "Cannot treat string litearal as location as it requires an implicit conversion to conform to the desired type.\n");
		}

		return rst;
	}

	case AstTag::Call:
	{
		AstNode* const callee = first_child_of(node);

		CallableValue callee_value;

		const EvalRst callee_rst = evaluate(interp, callee, EvalSpec{
			ValueKind::Value,
			range::from_object_bytes_mut(&callee_value)
		});

		EvalRst rst;

		if (callee_rst.tag == EvalTag::Unbound)
		{
			TODO("Implement unbound callees");
		}
		else
		{
			const TypeTag callee_type_tag = type_tag_from_id(interp->types, callee_rst.success.type_id);

			if (callee_type_tag != TypeTag::Func && callee_type_tag != TypeTag::Builtin)
				source_error(interp->errors, source_id_of(interp->asts, callee), "Cannot implicitly convert callee to callable type.\n");

			const SignatureType* const signature_type = type_attachment_from_id<SignatureType>(interp->types, callee_value.signature_type_id);

			const CallInfo call_info = setup_call_args(interp, signature_type, callee);

			rst = fill_spec(interp, spec, node, ValueKind::Value, call_info.return_type_id);

			const u32 mark = stack_mark(interp);

			const bool needs_conversion = !type_is_equal(interp->types, call_info.return_type_id, rst.success.type_id);

			MutRange<byte> temp_location;

			if (needs_conversion)
			{
				const TypeMetrics return_type_metrics = type_metrics_from_id(interp->types, call_info.return_type_id);

				stack_push(interp, return_type_metrics.size, return_type_metrics.align);
			}
			else
			{
				temp_location = rst.success.bytes;
			}

			if (callee_value.is_builtin)
			{
				Arec* const parameter_list_arec = arec_from_id(interp, call_info.parameter_list_arec_id);

				interp->builtin_values[callee_value.ordinal](interp, parameter_list_arec, node, temp_location);
			}
			else
			{
				ArecId closure_arec_id = ArecId::INVALID;

				if (callee_value.closure_id != ClosureId::INVALID)
				{
					ClosureInstance instance = closure_instance(interp->closures, callee_value.closure_id);
					
					closure_arec_id = arec_push(interp, instance.type_id, instance.values.count(), instance.align, active_arec_id(interp), ArecKind::Normal);

					Arec* const closure_arec = arec_from_id(interp, closure_arec_id);

					memcpy(closure_arec->attachment, instance.values.begin(), instance.values.count());
				}

				AstNode* const body = ast_node_from_id(interp->asts, static_cast<AstNodeId>(callee_value.body_ast_node_id));

				const EvalRst call_rst = evaluate(interp, body, EvalSpec{
					ValueKind::Value,
					temp_location,
					rst.success.type_id
				});

				if (closure_arec_id != ArecId::INVALID)
					arec_pop(interp, closure_arec_id);

				ASSERT_OR_IGNORE(call_rst.tag == EvalTag::Success);
			}

			if (needs_conversion)
				convert(interp, node, EvalSpec{ rst.success.kind, rst.success.bytes, rst.success.type_id }, EvalSpec{ ValueKind::Value, temp_location, call_info.return_type_id });

			arec_pop(interp, call_info.parameter_list_arec_id);

			stack_shrink(interp, mark);
		}

		return rst;
	}

	case AstTag::Member:
	{
		AstNode* const lhs = first_child_of(node);

		const AstMemberData attach = *attachment_of<AstMemberData>(node);

		EvalRst lhs_rst = evaluate(interp, lhs, EvalSpec{ ValueKind::Location });

		if (lhs_rst.tag == EvalTag::Unbound)
			return eval_unbound(lhs_rst.unbound.in);

		const TypeTag lhs_type_tag = type_tag_from_id(interp->types, lhs_rst.success.type_id);

		if (lhs_type_tag == TypeTag::Composite)
		{
			const Member* member;

			if (!type_member_by_name(interp->types, lhs_rst.success.type_id, attach.identifier_id, &member))
			{
				const Range<char8> name = identifier_name_from_id(interp->identifiers, attach.identifier_id);

				source_error(interp->errors, source_id_of(interp->asts, node), "Left-hand-side of `.` has no member named `%.*s`.\n", static_cast<s32>(name.count()), name.begin());
			}

			if (member->is_global)
				return evaluate_global_member(interp, node, spec, lhs_rst.success.type_id, member);

			return evaluate_local_member(interp, node, spec, lhs_rst.success.type_id, member, lhs_rst.success.bytes);
		}
		else if (lhs_type_tag == TypeTag::Type)
		{
			const TypeId type_id = load_loc<TypeId>(lhs_rst.success.kind == ValueKind::Value
				? lhs_rst.success.bytes
				: load_loc<MutRange<byte>>(lhs_rst.success.bytes)
			);

			if (type_tag_from_id(interp->types, type_id) != TypeTag::Composite)
				source_error(interp->errors, source_id_of(interp->asts, node), "Left-hand-side of `.` cannot be a non-composite type.\n");;

			const Member* member;

			if (!type_member_by_name(interp->types, type_id, attach.identifier_id, &member))
			{
				const Range<char8> name = identifier_name_from_id(interp->identifiers, attach.identifier_id);

				source_error(interp->errors, source_id_of(interp->asts, node), "Left-hand-side of `.` has no member named `%.*s`.\n", static_cast<s32>(name.count()), name.begin());
			}

			if (!member->is_global)
			{
				const Range<char8> name = identifier_name_from_id(interp->identifiers, attach.identifier_id);

				source_error(interp->errors, source_id_of(interp->asts, node), "Member `%.*s` cannot be accessed through a type, as it is not global.\n", static_cast<s32>(name.count()), name.begin());
			}

			return evaluate_global_member(interp, node, spec, type_id, member);
		}
		else
		{
			source_error(interp->errors, source_id_of(interp->asts, node), "Left-hand-side of `.` must be either a composite value or a composite type.\n");
		}
	}

	case AstTag::OpCmpEQ:
	{
		const TypeId bool_type_id = type_create_simple(interp->types, TypeTag::Boolean);

		const EvalRst rst = fill_spec_sized(interp, spec, node, ValueKind::Value, bool_type_id, sizeof(bool), alignof(bool));

		AstNode* const lhs = first_child_of(node);

		const EvalRst lhs_rst = evaluate(interp, lhs, EvalSpec{ ValueKind::Value });

		AstNode* const rhs = next_sibling_of(lhs);

		const EvalRst rhs_rst = evaluate(interp, rhs, EvalSpec{ ValueKind::Value });

		if (lhs_rst.tag == EvalTag::Unbound && rhs_rst.tag == EvalTag::Unbound)
		{
			TODO("Treat unbound parameters to OpCmpEq");
		}
		else if (lhs_rst.tag == EvalTag::Unbound)
		{
			TODO("Treat unbound parameters to OpCmpEq");
		}
		else if (rhs_rst.tag == EvalTag::Unbound)
		{
			TODO("Treat unbound parameters to OpCmpEq");
		}

		const TypeId common_type_id = type_unify(interp->types, lhs_rst.success.type_id, rhs_rst.success.type_id);

		if (common_type_id == TypeId::INVALID)
			source_error(interp->errors, source_id_of(interp->asts, node), "Could not unify argument types of `%s`.\n", tag_name(node->tag));

		const u32 mark = stack_mark(interp);

		MutRange<byte> lhs_casted;

		if (!type_is_equal(interp->types, common_type_id, lhs_rst.success.type_id))
		{
			const TypeMetrics metrics = type_metrics_from_id(interp->types, common_type_id);

			lhs_casted = stack_push(interp, metrics.size, metrics.align);

			convert(interp, lhs, EvalSpec{ ValueKind::Value, lhs_casted, common_type_id }, EvalSpec{ lhs_rst.success.kind, lhs_rst.success.bytes, lhs_rst.success.type_id });
		}
		else
		{
			lhs_casted = lhs_rst.success.bytes;
		}

		MutRange<byte> rhs_casted;

		if (!type_is_equal(interp->types, common_type_id, rhs_rst.success.type_id))
		{
			const TypeMetrics metrics = type_metrics_from_id(interp->types, common_type_id);

			rhs_casted = stack_push(interp, metrics.size, metrics.align);

			convert(interp, rhs, EvalSpec{ ValueKind::Value, rhs_casted, common_type_id }, EvalSpec{ lhs_rst.success.kind, lhs_rst.success.bytes, lhs_rst.success.type_id });
		}
		else
		{
			rhs_casted = rhs_rst.success.bytes;
		}

		ASSERT_OR_IGNORE(lhs_casted.count() == rhs_casted.count());

		const bool result = memcmp(lhs_casted.begin(), rhs_casted.begin(), lhs_casted.count()) == 0;

		stack_shrink(interp, mark);

		// No need for implicit conversion here, as bool is not convertible to
		// anything else.
		store_loc(rst.success.bytes, result);

		return rst;
	}

	case AstTag::OpTypeArray:
	{
		AstNode* const count = first_child_of(node);

		const EvalRst count_rst = evaluate(interp, count, EvalSpec{
			ValueKind::Value
		});

		u64 count_u64 = 0;

		if (count_rst.tag == EvalTag::Success)
		{
			const TypeTag count_type_tag = type_tag_from_id(interp->types, count_rst.success.type_id);

			if (count_type_tag == TypeTag::CompInteger)
			{
				const CompIntegerValue count_value = load_loc<CompIntegerValue>(count_rst.success.bytes);

				if (!u64_from_comp_integer(count_value, 64, &count_u64))
					source_error(interp->errors, source_id_of(interp->asts, count), "Array element count must fit into unsigned 64-bit integer.\n");
			}
			else if (count_type_tag == TypeTag::Integer)
			{
				const NumericType count_type = *type_attachment_from_id<NumericType>(interp->types, count_rst.success.type_id);

				if (!u64_from_integer(count_rst.success.bytes.immut(), count_type, &count_u64))
					source_error(interp->errors, source_id_of(interp->asts, count), "Array element count must fit into unsigned 64-bit integer.\n");
			}
			else
			{
				source_error(interp->errors, source_id_of(interp->asts, count), "Array element count must have an integer type.\n");
			}
		}

		AstNode* const elem_type = next_sibling_of(count);

		const TypeId type_type_id = type_create_simple(interp->types, TypeTag::Type);

		TypeId elem_type_id;

		const EvalRst elem_type_rst = evaluate(interp, elem_type, EvalSpec{
			ValueKind::Value,
			range::from_object_bytes_mut(&elem_type_id),
			type_type_id
		});

		if (count_rst.tag == EvalTag::Unbound && elem_type_rst.tag == EvalTag::Unbound)
			return eval_unbound(count_rst.unbound.in < elem_type_rst.unbound.in ? count_rst.unbound.in : elem_type_rst.unbound.in);
		else if (count_rst.tag == EvalTag::Unbound)
			return eval_unbound(count_rst.unbound.in);
		else if (elem_type_rst.tag == EvalTag::Unbound)
			return eval_unbound(elem_type_rst.unbound.in);

		const EvalRst rst = fill_spec_sized(interp, spec, node, ValueKind::Value, type_type_id, sizeof(TypeId), alignof(TypeId));

		const TypeId array_type_id = type_create_array(interp->types, TypeTag::Array, ArrayType{ count_u64, elem_type_id });

		store_loc(rst.success.bytes, array_type_id);

		return rst;
	}

	case AstTag::OpArrayIndex:
	{
		AstNode* const arrayish = first_child_of(node);

		EvalRst arrayish_rst = evaluate(interp, arrayish, EvalSpec{
			ValueKind::Location
		});

		TypeId elem_type_id = TypeId::INVALID;

		TypeMetrics elem_metrics = {};

		u64 arrayish_size = 0;

		byte* arrayish_begin = nullptr;

		if (arrayish_rst.tag == EvalTag::Success)
		{
			const TypeTag arrayish_type_tag = type_tag_from_id(interp->types, arrayish_rst.success.type_id);

			if (arrayish_type_tag == TypeTag::Array || arrayish_type_tag == TypeTag::ArrayLiteral)
			{
				const ArrayType* const array_type = type_attachment_from_id<ArrayType>(interp->types, arrayish_rst.success.type_id);

				elem_metrics = type_metrics_from_id(interp->types, array_type->element_type);

				elem_type_id = array_type->element_type;

				arrayish_size = array_type->element_count * elem_metrics.size;

				arrayish_begin = arrayish_rst.success.kind == ValueKind::Location
					? load_loc<MutRange<byte>>(arrayish_rst.success.bytes).begin()
					: arrayish_rst.success.bytes.begin();
			}
			else if (arrayish_type_tag == TypeTag::Slice)
			{
				const ReferenceType* const slice_type = type_attachment_from_id<ReferenceType>(interp->types, arrayish_rst.success.type_id);

				MutRange<byte> slice = load_loc<MutRange<byte>>(arrayish_rst.success.bytes);

				if (arrayish_rst.success.kind == ValueKind::Location)
					slice = load_loc<MutRange<byte>>(slice);

				elem_metrics = type_metrics_from_id(interp->types, slice_type->referenced_type_id);

				ASSERT_OR_IGNORE(slice.count() % elem_metrics.stride == 0);

				elem_type_id = slice_type->referenced_type_id;

				arrayish_size = slice.count();

				arrayish_begin = slice.begin();
			}
			else if (arrayish_type_tag == TypeTag::Ptr)
			{
				const ReferenceType* const ptr_type = type_attachment_from_id<ReferenceType>(interp->types, arrayish_rst.success.type_id);

				if (!ptr_type->is_multi)
					source_error(interp->errors, source_id_of(interp->asts, arrayish), "Left-hand-side of index operator must have array, slice, or multi-pointer type.\n");

				MutRange<byte> ptr_loc = arrayish_rst.success.kind == ValueKind::Location
					? load_loc<MutRange<byte>>(arrayish_rst.success.bytes)
					: arrayish_rst.success.bytes;

				void* const ptr = load_loc<void*>(ptr_loc);

				if (ptr_type->is_opt && ptr == nullptr)
					source_error(interp->errors, source_id_of(interp->asts, node), "Left-hand-side of index operator was `null`.\n");

				ASSERT_OR_IGNORE(ptr != nullptr);

				elem_metrics = type_metrics_from_id(interp->types, ptr_type->referenced_type_id);

				elem_type_id = ptr_type->referenced_type_id;

				arrayish_size = UINT64_MAX;

				arrayish_begin = static_cast<byte*>(ptr);
			}
			else
			{
				source_error(interp->errors, source_id_of(interp->asts, arrayish), "Left-hand-side of index operator must have array, slice, or multi-pointer type.\n");
			}
		}

		AstNode* const index = next_sibling_of(arrayish);

		const EvalRst index_rst = evaluate(interp, index, EvalSpec{
			ValueKind::Value
		});

		u64 index_u64 = 0;

		if (index_rst.tag == EvalTag::Success)
		{
			const TypeTag index_type_tag = type_tag_from_id(interp->types, index_rst.success.type_id);

			if (index_type_tag == TypeTag::CompInteger)
			{
				const CompIntegerValue index_value = load_loc<CompIntegerValue>(index_rst.success.bytes);

				if (!u64_from_comp_integer(index_value, 64, &index_u64))
					source_error(interp->errors, source_id_of(interp->asts, index), "Right-hand-side of index operator must fit into unsigned 64-bit integer.\n");
			}
			else if (index_type_tag == TypeTag::Integer)
			{
				const NumericType index_type = *type_attachment_from_id<NumericType>(interp->types, index_rst.success.type_id);

				if (!u64_from_integer(index_rst.success.bytes.immut(), index_type, &index_u64))
					source_error(interp->errors, source_id_of(interp->asts, index), "Right-hand-side of index operator must fit into unsigned 64-bit integer.\n");
			}
			else
			{
				source_error(interp->errors, source_id_of(interp->asts, arrayish), "Right-hand-side of index operator must have integer type.\n");
			}
		}

		if (arrayish_rst.tag == EvalTag::Unbound || index_rst.tag == EvalTag::Unbound)
		{
			Arec* unbound_in;

			if (arrayish_rst.tag == EvalTag::Success)
			{
				const TypeMetrics metrics = type_metrics_from_id(interp->types, arrayish_rst.success.type_id);

				const MutRange<byte> dst = add_partial_value_to_builder(interp, arrayish, arrayish_rst.success.type_id, metrics.size, metrics.align);

				const Range<byte> src = arrayish_rst.success.kind == ValueKind::Location
					? load_loc<Range<byte>>(arrayish_rst.success.bytes)
					: arrayish_rst.success.bytes.immut();

				copy_loc(dst, src);

				ASSERT_OR_IGNORE(index_rst.tag == EvalTag::Unbound);

				unbound_in = index_rst.unbound.in;
			}
			else if (index_rst.tag == EvalTag::Success)
			{
				const TypeMetrics metrics = type_metrics_from_id(interp->types, index_rst.success.type_id);

				const MutRange<byte> dst = add_partial_value_to_builder(interp, index, index_rst.success.type_id, metrics.size, metrics.align);

				const Range<byte> src = arrayish_rst.success.kind == ValueKind::Location
					? load_loc<Range<byte>>(arrayish_rst.success.bytes)
					: arrayish_rst.success.bytes.immut();

				copy_loc(dst, src);

				ASSERT_OR_IGNORE(arrayish_rst.tag == EvalTag::Unbound);

				unbound_in = arrayish_rst.unbound.in;
			}
			else
			{
				unbound_in = arrayish_rst.unbound.in < index_rst.unbound.in ? arrayish_rst.unbound.in : index_rst.unbound.in;
			}

			return eval_unbound(unbound_in);
		}
		else
		{
			const u64 offset = index_u64 * elem_metrics.stride;

			if (offset + elem_metrics.size > arrayish_size)
				source_error(interp->errors, source_id_of(interp->asts, node), "Index %" PRIu64 " exceeds element count of %" PRIu64 ".\n", index_u64, arrayish_size / elem_metrics.stride);

			EvalRst rst;

			if (spec.kind == ValueKind::Location)
			{
				if (arrayish_rst.success.kind != ValueKind::Location)
					source_error(interp->errors, source_id_of(interp->asts, node), "Cannot use index operator with non-location left-hand-side as a location.\n");

				rst = fill_spec_sized(interp, spec, node, ValueKind::Location, elem_type_id, sizeof(MutRange<byte>), alignof(MutRange<byte>));

				store_loc(rst.success.bytes, MutRange<byte>{ arrayish_begin + offset, elem_metrics.size });
			}
			else
			{
				rst = fill_spec_sized(interp, spec, node, ValueKind::Value, elem_type_id, elem_metrics.size, elem_metrics.align);

				copy_loc(rst.success.bytes, Range<byte>{ arrayish_begin + offset, elem_metrics.size });
			}

			return rst;
		}
	}

	case AstTag::CompositeInitializer:
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
			return type_create_simple(interp->types, TypeTag::Void);

		panic("typeinfer(%s) for non-empty blocks not yet implemented.\n", tag_name(node->tag));
	}

	case AstTag::Identifier:
	{
		AstIdentifierData attach = *attachment_of<AstIdentifierData>(node);

		IdentifierInfo info = lookup_identifier(interp, attach.identifier_id);

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
		return type_create_simple(interp->types, TypeTag::CompInteger);
	}

	case AstTag::OpCmpEQ:
	{
		AstNode* const lhs = first_child_of(node);

		const TypeId lhs_type_id = typeinfer(interp, lhs);

		AstNode* const rhs = next_sibling_of(lhs);

		const TypeId rhs_type_id = typeinfer(interp, rhs);

		if (type_unify(interp->types, lhs_type_id, rhs_type_id) == TypeId::INVALID)
			source_error(interp->errors, source_id_of(interp->asts, node), "Could not unify argument types of `%s`.\n", tag_name(node->tag));

		return type_create_simple(interp->types, TypeTag::Boolean);
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
	const TypeId file_type_id = type_create_composite(interp->types, interp->prelude_type_id, TypeDisposition::User, file_type_source_id, 0, false);

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

		if (!type_add_composite_member(interp->types, file_type_id, member))
		{
			const Range<char8> name = identifier_name_from_id(interp->identifiers, attachment_of<AstDefinitionData>(node)->identifier_id);

			source_error(interp->errors, source_id_of(interp->asts, node), "More than one top-level definition with name `%.*s`.\n", static_cast<s32>(name.count()), name.begin());
		}
	}

	type_seal_composite(interp->types, file_type_id, 0, 1, 0);

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

			const EvalRst member_type_rst = evaluate(interp, type, EvalSpec{
				ValueKind::Value,
				range::from_object_bytes_mut(&member_type_id),
				type_create_simple(interp->types, TypeTag::Type)
			});

			// This must succeed as we are on the top level.
			ASSERT_OR_IGNORE(member_type_rst.tag == EvalTag::Success);

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

		const EvalRst value_rst = evaluate(interp, value, value_spec);

		ASSERT_OR_IGNORE(value_rst.tag == EvalTag::Success);

		if (member_value_id == GlobalValueId::INVALID)
		{
			const TypeMetrics metrics = type_metrics_from_id(interp->types, value_rst.success.type_id);

			member_value_id = alloc_global_value(interp->globals, metrics.size, metrics.align);

			MutRange<byte> value_bytes = global_value_get_mut(interp->globals, member_value_id);

			ASSERT_OR_IGNORE(value_bytes.count() == value_rst.success.bytes.count());

			memcpy(value_bytes.begin(), value_rst.success.bytes.begin(), value_bytes.count());
		}

		type_set_composite_member_info(interp->types, file_type_id, member->rank, MemberCompletionInfo{
			true,
			true,
			value_rst.success.type_id,
			member_value_id
		});
	}

	arec_pop(interp, file_arec_id);

	return file_type_id;
}





static TypeId make_func_type_from_array(TypePool* types, TypeId return_type_id, u8 param_count, const BuiltinParamInfo* params) noexcept
{
	const TypeId parameter_list_type_id = type_create_composite(types, TypeId::INVALID, TypeDisposition::Signature, SourceId::INVALID, param_count, true);

	for (u8 i = 0; i != param_count; ++i)
	{
		Member member{};
		member.name = params[i].name;
		member.type.complete = params[i].type;
		member.value.complete = GlobalValueId::INVALID;
		member.is_global = false;
		member.is_pub = false;
		member.is_mut = false;
		member.is_comptime_known = params[i].is_comptime_known;
		member.is_arg_independent = true;
		member.has_pending_type = false;
		member.has_pending_value = false;
		member.offset = 0;

		if (!type_add_composite_member(types, parameter_list_type_id, member))
			ASSERT_UNREACHABLE;
	}

	type_seal_composite(types, parameter_list_type_id, 0, 0, 0);

	SignatureType signature_type{};
	signature_type.parameter_list_type_id = parameter_list_type_id;
	signature_type.return_type.complete = return_type_id;
	signature_type.partial_value_id = PartialValueId::INVALID;
	signature_type.param_count = param_count;
	signature_type.is_proc = false;
	signature_type.parameter_list_is_unbound = false;
	signature_type.return_type_is_unbound = false;

	return type_create_signature(types, TypeTag::Func, signature_type);
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
	IdentifierInfo info = lookup_local_identifier(interp, arec, name);

	ASSERT_OR_IGNORE(info.tag == IdentifierInfoTag::Found);

	return load_loc<T>(info.found.location);
}

static void builtin_integer(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, MutRange<byte> into) noexcept
{
	const u8 bits = get_builtin_arg<u8>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("bits")));

	const bool is_signed = get_builtin_arg<bool>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("is_signed")));

	store_loc(into, type_create_numeric(interp->types, TypeTag::Integer, NumericType{ bits, is_signed }));
}

static void builtin_float(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, MutRange<byte> into) noexcept
{
	const u8 bits = get_builtin_arg<u8>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("bits")));

	store_loc(into, type_create_numeric(interp->types, TypeTag::Float, NumericType{ bits, true }));
}

static void builtin_type(Interpreter* interp, [[maybe_unused]] Arec* arec, [[maybe_unused]] AstNode* call_node, MutRange<byte> into) noexcept
{
	store_loc(into, type_create_simple(interp->types, TypeTag::Type));
}

static void builtin_typeof(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, MutRange<byte> into) noexcept
{
	store_loc(into, get_builtin_arg<TypeId>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("arg"))));
}

static void builtin_returntypeof(Interpreter* interp, Arec* arec, [[maybe_unused]] AstNode* call_node, MutRange<byte> into) noexcept
{
	const TypeId arg = get_builtin_arg<TypeId>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("arg")));

	ASSERT_OR_IGNORE(type_tag_from_id(interp->types, arg) == TypeTag::Func || type_tag_from_id(interp->types, arg) == TypeTag::Builtin);

	const SignatureType* const signature_type = type_attachment_from_id<SignatureType>(interp->types, arg);

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
	const TypeId type_type_id = type_create_simple(interp->types, TypeTag::Type);

	const TypeId comp_integer_type_id = type_create_simple(interp->types, TypeTag::CompInteger);

	const TypeId bool_type_id = type_create_simple(interp->types, TypeTag::Boolean);

	const TypeId definition_type_id = type_create_simple(interp->types, TypeTag::Definition);

	const TypeId type_builder_type_id = type_create_simple(interp->types, TypeTag::TypeBuilder);

	const TypeId void_type_id = type_create_simple(interp->types, TypeTag::Void);

	const TypeId type_info_type_id = type_create_simple(interp->types, TypeTag::TypeInfo);

	ReferenceType ptr_to_type_builder_type{};
	ptr_to_type_builder_type.is_opt = false;
	ptr_to_type_builder_type.is_multi = false;
	ptr_to_type_builder_type.is_mut = true;
	ptr_to_type_builder_type.referenced_type_id = type_builder_type_id;

	const TypeId ptr_to_mut_type_builder_type_id = type_create_reference(interp->types, TypeTag::Ptr, ptr_to_type_builder_type);

	const TypeId s64_type_id = type_create_numeric(interp->types, TypeTag::Integer, NumericType{ 64, true });

	const TypeId u8_type_id = type_create_numeric(interp->types, TypeTag::Integer, NumericType{ 8, false });

	ReferenceType slice_of_u8_type{};
	slice_of_u8_type.is_opt = false;
	slice_of_u8_type.is_multi = false;
	slice_of_u8_type.is_mut = false;
	slice_of_u8_type.referenced_type_id = u8_type_id;

	const TypeId slice_of_u8_type_id = type_create_reference(interp->types, TypeTag::Slice, slice_of_u8_type);

	const TypeId u32_type_id = type_create_numeric(interp->types, TypeTag::Integer, NumericType{ 32, false });



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

// Pushes `let std = _import(<std_filepath>, _source_id())` into `asts`'s
// builder.
static AstBuilderToken std_import(AstPool* asts, IdentifierId std_name, GlobalValueId std_filepath_value_id, TypeId std_filepath_type_id) noexcept
{
	const AstBuilderToken import_builtin = push_node(asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, static_cast<AstFlag>(Builtin::Import), AstTag::Builtin);

	push_node(asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, AstFlag::EMPTY, AstLitStringData{ std_filepath_value_id, std_filepath_type_id });

	const AstBuilderToken literal_zero = push_node(asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, AstFlag::EMPTY, AstLitIntegerData{ comp_integer_from_u64(0) });

	push_node(asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, AstFlag::EMPTY, AstLitIntegerData{ comp_integer_from_u64(0) });

	push_node(asts, literal_zero, SourceId::INVALID, AstFlag::EMPTY, AstTag::OpCmpEQ);

	const AstBuilderToken source_id_builtin = push_node(asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, static_cast<AstFlag>(Builtin::SourceId), AstTag::Builtin);

	push_node(asts, source_id_builtin, SourceId::INVALID, AstFlag::EMPTY, AstTag::Call);

	const AstBuilderToken import_call = push_node(asts, import_builtin, SourceId::INVALID, AstFlag::EMPTY, AstTag::Call);

	return push_node(asts, import_call, SourceId::INVALID, AstFlag::EMPTY, AstDefinitionData{ std_name });
}

// Pushes `let <use_name> = std.prelude.<use_name>` into `asts`'s builder.
static void std_prelude_use(AstPool* asts, IdentifierId std_name, IdentifierId prelude_name, IdentifierId use_name) noexcept
{
	const AstBuilderToken std_identifer = push_node(asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, AstFlag::EMPTY, AstIdentifierData{ std_name });

	const AstBuilderToken prelude_member = push_node(asts, std_identifer, SourceId::INVALID, AstFlag::EMPTY, AstMemberData{ prelude_name });

	const AstBuilderToken used_member = push_node(asts, prelude_member, SourceId::INVALID, AstFlag::EMPTY, AstMemberData{ use_name });

	push_node(asts, used_member, SourceId::INVALID, AstFlag::EMPTY, AstDefinitionData{ use_name });
}

static void init_prelude_type(Interpreter* interp, Config* config, IdentifierPool* identifiers, AstPool* asts) noexcept
{
	const TypeId u8_type_id = type_create_numeric(interp->types, TypeTag::Integer, NumericType{ 8, false });

	const TypeId std_filepath_type_id = type_create_array(interp->types, TypeTag::Array, ArrayType{ config->std.filepath.count(), u8_type_id });

	const GlobalValueId std_filepath_value_id = alloc_global_value(interp->globals, config->std.filepath.count(), 1);

	global_value_set(interp->globals, std_filepath_value_id, 0, config->std.filepath.as_byte_range());

	const IdentifierId std_name = id_from_identifier(identifiers, range::from_literal_string("std"));

	const IdentifierId prelude_name = id_from_identifier(identifiers, range::from_literal_string("prelude"));

	const AstBuilderToken first_token = std_import(asts, std_name, std_filepath_value_id, std_filepath_type_id);

	std_prelude_use(asts, std_name, prelude_name, id_from_identifier(identifiers, range::from_literal_string("u8")));

	std_prelude_use(asts, std_name, prelude_name, id_from_identifier(identifiers, range::from_literal_string("u16")));

	std_prelude_use(asts, std_name, prelude_name, id_from_identifier(identifiers, range::from_literal_string("u32")));

	std_prelude_use(asts, std_name, prelude_name, id_from_identifier(identifiers, range::from_literal_string("u64")));

	std_prelude_use(asts, std_name, prelude_name, id_from_identifier(identifiers, range::from_literal_string("s8")));

	std_prelude_use(asts, std_name, prelude_name, id_from_identifier(identifiers, range::from_literal_string("s16")));

	std_prelude_use(asts, std_name, prelude_name, id_from_identifier(identifiers, range::from_literal_string("s32")));

	std_prelude_use(asts, std_name, prelude_name, id_from_identifier(identifiers, range::from_literal_string("s64")));

	std_prelude_use(asts, std_name, prelude_name, id_from_identifier(identifiers, range::from_literal_string("f32")));

	std_prelude_use(asts, std_name, prelude_name, id_from_identifier(identifiers, range::from_literal_string("f64")));

	std_prelude_use(asts, std_name, prelude_name, id_from_identifier(identifiers, range::from_literal_string("Type")));

	std_prelude_use(asts, std_name, prelude_name, id_from_identifier(identifiers, range::from_literal_string("Bool")));

	std_prelude_use(asts, std_name, prelude_name, id_from_identifier(identifiers, range::from_literal_string("Void")));

	std_prelude_use(asts, std_name, prelude_name, id_from_identifier(identifiers, range::from_literal_string("true")));

	std_prelude_use(asts, std_name, prelude_name, id_from_identifier(identifiers, range::from_literal_string("false")));

	push_node(asts, first_token, SourceId::INVALID, AstFlag::EMPTY, AstTag::File);

	AstNode* const prelude_ast = complete_ast(asts);

	interp->prelude_type_id = type_from_file_ast(interp, prelude_ast, SourceId::INVALID);

	if (interp->log_file.m_rep != nullptr && interp->log_prelude)
	{
		const SourceLocation file_type_location = source_location_from_source_id(interp->reader, SourceId::INVALID);

		diag::print_type(interp->log_file, interp->identifiers, interp->types, interp->prelude_type_id, &file_type_location);
	}
}



Interpreter* create_interpreter(AllocPool* alloc, Config* config, SourceReader* reader, Parser* parser, TypePool* types, AstPool* asts, IdentifierPool* identifiers, GlobalValuePool* globals, PartialValuePool* partials, ClosurePool* closures, ErrorSink* errors, minos::FileHandle log_file, bool log_prelude) noexcept
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
	interp->closures = closures;
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
		const SourceLocation file_type_location = source_location_from_source_id(interp->reader, read.source_file->source_id_base);

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
