#include "core.hpp"

#include <cmath>

#include "../diag/diag.hpp"
#include "../infra/container/reserved_vec.hpp"

enum class ArecKind : bool
{
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

	u32 size : 31;

	u32 kind : 1;

	u32 attach_index;

	TypeId global_scope_type_id;

	SourceId source_id;

	ArecId caller_arec_id;
};

struct ArecRestoreInfo
{
	ArecId old_selected;

	ArecId old_top;
};

using BuiltinFunc = void (*) (Interpreter* interp, Arec* arec, MutRange<byte> into) noexcept;

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

enum class ShiftAmountResult : u8
{
	InRange,
	TooLarge,
	Negative,
	UnsupportedType,
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

enum class EvalTag : u8
{
	Success,
	Unbound,
};

struct EvalValue
{
	MutRange<byte> bytes;

	TypeId type_id;

	bool is_location;

	bool is_mut;
};

struct EvalRst
{
	EvalTag tag;

	union
	{
		EvalValue success;

		Arec* unbound;
	};

	// TODO: Try and remove this.
	EvalRst() noexcept :
		tag{},
		success{}
	{}
};

struct IdentifierInfo
{
	EvalTag tag;

	EvalValue success;

	OptPtr<Arec> arec;
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

	LexicalAnalyser* lex;

	ErrorSink* errors;

	ReservedVec<Arec> arec_headers;

	ReservedVec<u64> arec_attachs;

	ArecId top_arec_id;

	ArecId active_arec_id;

	ReservedVec<byte> temps;

	ReservedVec<ClosureInstance> active_closures;

	ReservedVec<PartialValueBuilderId> partial_value_builders;

	ReservedVec<PeekablePartialValueIterator> active_partial_values;

	TypeId prelude_type_id;

	TypeId builtin_type_ids[static_cast<u8>(Builtin::MAX)];

	BuiltinFunc builtin_values[static_cast<u8>(Builtin::MAX)];

	minos::FileHandle type_log_file;

	minos::FileHandle ast_log_file;

	bool log_prelude;

	MutRange<byte> memory;
};





static EvalRst evaluate(Interpreter* interp, AstNode* node, EvalSpec spec) noexcept;

static TypeId typeinfer(Interpreter* interp, AstNode* node) noexcept;



static EvalRst eval_unbound(Arec* unbound_in) noexcept
{
	EvalRst rst;
	rst.tag = EvalTag::Unbound;
	rst.unbound = unbound_in;

	return rst;
}



template<typename T>
static const T* value_as(EvalValue val) noexcept
{
	ASSERT_OR_IGNORE(val.bytes.count() == sizeof(T));

	return reinterpret_cast<const T*>(val.bytes.begin());
}

static Range<byte> value_bytes(EvalValue val) noexcept
{
	return val.bytes.immut();
}

static void value_set(EvalValue* val, MutRange<byte> bytes) noexcept
{
	if (val->is_location)
	{
		ASSERT_OR_IGNORE(val->bytes.begin() == nullptr);

		val->bytes = bytes;
	}
	else
	{
		ASSERT_OR_IGNORE(val->bytes.count() == bytes.count());

		memcpy(val->bytes.begin(), bytes.begin(), val->bytes.count());
	}
}

static EvalValue make_value(MutRange<byte> bytes, bool is_location, bool is_mut, TypeId type_id) noexcept
{
	EvalValue rst;
	rst.bytes = bytes;
	rst.type_id = type_id;
	rst.is_location = is_location;
	rst.is_mut = is_mut;

	return rst;
}

static IdentifierInfo make_identifier_info(MutRange<byte> location, TypeId type_id, bool is_mut, OptPtr<Arec> arec) noexcept
{
	IdentifierInfo info;
	info.tag = EvalTag::Success;
	info.success = make_value(location, true, is_mut, type_id);
	info.arec = arec;

	return info;
}

static IdentifierInfo make_unbound_identifier_info(Arec* arec) noexcept
{
	IdentifierInfo info;
	info.tag = EvalTag::Unbound;
	info.arec = some(arec);

	return info;
}


MutRange<byte> arec_attach(Interpreter* interp, Arec* arec) noexcept
{
	ASSERT_OR_IGNORE(arec->attach_index + (arec->size + 7) / 8 <= interp->arec_attachs.used());

	byte* const begin = reinterpret_cast<byte*>(interp->arec_attachs.begin() + arec->attach_index);

	return { begin, arec->size };
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

	return interp->arec_headers.begin() + static_cast<u32>(interp->active_arec_id);
}

static ArecId active_arec_id(Interpreter* interp) noexcept
{
	ASSERT_OR_IGNORE(interp->active_arec_id != ArecId::INVALID);

	return interp->active_arec_id;
}

static TypeId active_arec_global_scope_type_id(Interpreter* interp) noexcept
{
	return active_arec(interp)->global_scope_type_id;
}

static Arec* arec_from_id(Interpreter* interp, ArecId arec_id) noexcept
{
	ASSERT_OR_IGNORE(arec_id != ArecId::INVALID);

	return interp->arec_headers.begin() + static_cast<s32>(arec_id);
}

static void arec_restore(Interpreter* interp, ArecRestoreInfo info) noexcept
{
	ASSERT_OR_IGNORE(info.old_top <= interp->top_arec_id);

	ASSERT_OR_IGNORE(info.old_selected <= static_cast<ArecId>(info.old_top));

	const Arec* const old_top_arec = arec_from_id(interp, info.old_top);

	const u32 old_top_arec_qwords = (old_top_arec->size + 7) / 8;

	const u32 old_used = old_top_arec->attach_index + old_top_arec_qwords;

	interp->arec_headers.pop_to(static_cast<u32>(info.old_top) + 1);

	interp->arec_attachs.pop_to(old_used);

	interp->active_arec_id = info.old_selected;

	interp->top_arec_id = info.old_top;
}

static ArecId arec_push(Interpreter* interp, TypeId record_type_id, u64 size, u32 align, ArecId lookup_parent, ArecKind kind, TypeId global_scope_type_id, SourceId source_id, ArecId caller_arec_id) noexcept
{
	ASSERT_OR_IGNORE(type_tag_from_id(interp->types, record_type_id) == TypeTag::Composite);

	if (size >= UINT32_MAX)
		panic("Arec too large.\n");

	if (align > 8)
		TODO("Implement overaligned Arecs");

	Arec* const arec = interp->arec_headers.reserve();
	arec->prev_top_id = interp->top_arec_id;
	arec->surrounding_arec_id = lookup_parent;
	arec->type_id = record_type_id;
	arec->size = static_cast<u32>(size);
	arec->kind = static_cast<u32>(kind);
	arec->attach_index = interp->arec_attachs.used();
	arec->global_scope_type_id = global_scope_type_id;
	arec->source_id = source_id;
	arec->caller_arec_id = caller_arec_id;

	(void) interp->arec_attachs.reserve(static_cast<u32>((size + 7) / 8));

	const ArecId arec_id = static_cast<ArecId>(arec - interp->arec_headers.begin());

	interp->top_arec_id = arec_id;

	ASSERT_OR_IGNORE(lookup_parent == ArecId::INVALID || interp->active_arec_id == lookup_parent);

	interp->active_arec_id = arec_id;

	return arec_id;
}

static void arec_pop(Interpreter* interp, ArecId arec_id) noexcept
{
	ASSERT_OR_IGNORE(arec_id != ArecId::INVALID && interp->top_arec_id == arec_id && interp->active_arec_id == arec_id);

	const Arec* const popped = interp->arec_headers.begin() + static_cast<s32>(arec_id);

	interp->active_arec_id = popped->surrounding_arec_id == ArecId::INVALID ? popped->prev_top_id : popped->surrounding_arec_id;

	interp->top_arec_id = popped->prev_top_id;

	const u32 new_attach_top = interp->arec_headers.used() == 1 ? 0 : popped[-1].attach_index + (popped[-1].size + 7) / 8;

	interp->arec_headers.pop_by(1);

	interp->arec_attachs.pop_to(new_attach_top);
}

static void arec_grow(Interpreter* interp, ArecId arec_id, u64 new_size) noexcept
{
	ASSERT_OR_IGNORE(arec_id != ArecId::INVALID && interp->top_arec_id == arec_id);

	if (new_size > UINT32_MAX)
		panic("Arec too large.\n");

	Arec* const arec = interp->arec_headers.begin() + static_cast<s32>(arec_id);

	ASSERT_OR_IGNORE(arec->size <= new_size && interp->arec_attachs.used() == arec->attach_index + (arec->size + 7) / 8);

	// This overallocates due to `interp->arecs` rounding to 8 bytes.
	// However, that's not really a problem, as we're in the top arec anyways,
	// and it'll get popped soon enough.
	(void) interp->arec_attachs.reserve_padded(static_cast<u32>(new_size - arec->size));

	arec->size = static_cast<u32>(new_size);
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

static void add_partial_value_to_builder_sized(Interpreter* interp, AstNode* node, TypeId type_id, Range<byte> bytes, u64 size, u32 align) noexcept
{
	ASSERT_OR_IGNORE(interp->partial_value_builders.used() != 0);

	const PartialValueBuilderId builder_id = interp->partial_value_builders.end()[-1];

	MutRange<byte> dst = partial_value_builder_add_value(interp->partials, builder_id, node, type_id, size, align);

	range::mem_copy(dst, bytes);
}

static void add_partial_value_to_builder(Interpreter* interp, AstNode* node, TypeId type_id, Range<byte> bytes) noexcept
{
	const TypeMetrics metrics = type_metrics_from_id(interp->types, type_id);

	add_partial_value_to_builder_sized(interp, node, type_id, bytes, metrics.size, metrics.align);
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

			range::mem_copy(global_value_get_mut(interp->globals, member_value_id), value_bytes(value_rst.success));
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



static IdentifierInfo identifier_info_from_global_member(Interpreter* interp, TypeId surrounding_type_id, const Member* member) noexcept
{
	complete_member(interp, surrounding_type_id, member);

	return make_identifier_info(
		global_value_get_mut(interp->globals, member->value.complete),
		member->type.complete,
		member->is_mut,
		none<Arec>()
	);
}

static IdentifierInfo identifier_info_from_arec_and_member(Interpreter* interp, Arec* arec, const Member* member) noexcept
{
	if (member->is_global)
		return identifier_info_from_global_member(interp, arec->type_id, member);

	const u64 size = type_metrics_from_id(interp->types, member->type.complete).size;

	if (size > UINT32_MAX)
		source_error(interp->errors, SourceId::INVALID, "Size of stack-based location must not exceed 2^32 - 1 bytes.\n");

	const ArecKind kind = static_cast<ArecKind>(arec->kind);

	if (kind == ArecKind::Unbound)
		return make_unbound_identifier_info(arec);

	MutRange<byte> attach = arec_attach(interp, arec);

	return make_identifier_info(
		attach.mut_subrange(member->offset, size),
		member->type.complete,
		member->is_mut,
		some(arec)
	);
}

static IdentifierInfo lookup_identifier(Interpreter* interp, IdentifierId name, NameBinding binding) noexcept
{
	if (binding.is_closed_over)
	{
		ASSERT_OR_IGNORE(interp->active_closures.used() != 0);

		ClosureInstance* instance = interp->active_closures.end() - 1;

		const Member* member;

		if (!type_member_by_name(interp->types, instance->type_id, name, &member))
			ASSERT_UNREACHABLE;

		ASSERT_OR_IGNORE(!member->is_global && !member->has_pending_type);

		const TypeMetrics metrics = type_metrics_from_id(interp->types, member->type.complete);

		return make_identifier_info(
			instance->values.mut_subrange(member->offset, metrics.size),
			member->type.complete,
			false,
			none<Arec>()
		);
	}

	Arec* arec = active_arec(interp);

	if (binding.is_global)
	{
		TypeId global_scope_type_id = arec->global_scope_type_id;

		u16 out = binding.out;

		while (out != 0)
		{
			ASSERT_OR_IGNORE(global_scope_type_id != TypeId::INVALID);

			global_scope_type_id = type_global_scope_from_id(interp->types, global_scope_type_id);

			out -= 1;
		}

		ASSERT_OR_IGNORE(global_scope_type_id != TypeId::INVALID);

		const Member* const member = type_member_by_rank(interp->types, global_scope_type_id, binding.rank);

		ASSERT_OR_IGNORE(member->name == name);

		// Since we are in a global context, the member has to be global.
		return identifier_info_from_global_member(interp, global_scope_type_id, member);
	}
	else
	{
		u16 out = binding.out;

		while (out != 0)
		{
			ASSERT_OR_IGNORE(arec->surrounding_arec_id != ArecId::INVALID);

			arec = arec_from_id(interp, arec->surrounding_arec_id);

			out -= 1;
		}

		const Member* const member = type_member_by_rank(interp->types, arec->type_id, binding.rank);

		ASSERT_OR_IGNORE(member->name == name);

		if (static_cast<ArecKind>(arec->kind) == ArecKind::Unbound)
			return make_unbound_identifier_info(arec);

		return identifier_info_from_arec_and_member(interp, arec, member);
	}
}



static void print_stack_trace(Interpreter* interp) noexcept
{
	Arec* arec = active_arec(interp);

	while (true)
	{
		SourceLocation loc = source_location_from_source_id(interp->reader, arec->source_id);

		error_diagnostic(interp->errors, "    %.*s:%u:%u\n", static_cast<s32>(loc.filepath.count()), loc.filepath.begin(), loc.line_number, loc.column_number);

		if (arec->caller_arec_id == ArecId::INVALID)
			break;

		arec = arec_from_id(interp, arec->caller_arec_id);
	}
}

static ClosureBuilderId close_over_rec(Interpreter* interp, ClosureBuilderId builder_id, AstNode* node, u16 out_adjustment) noexcept
{
	if (node->tag == AstTag::Identifier)
	{
		const AstIdentifierData attach = *attachment_of<AstIdentifierData>(node);

		// If the identifier is statically known not to be captured - i.e., if
		// it is at global scope - we must not capture it.
		if (!attach.binding.is_closed_over)
			return builder_id;

		ASSERT_OR_IGNORE(!attach.binding.is_global);

		// If the identifier is bound to a definition inside the function body,
		// we must not capture it.
		// Note that the reason it is still marked as `is_closed_over` is that
		// it is captured by a nested function.
		if (attach.binding.out < out_adjustment)
			return builder_id;

		NameBinding adjusted_binding = attach.binding;
		adjusted_binding.out -= out_adjustment;
		adjusted_binding.is_closed_over = attach.binding.is_closed_over_closure;

		IdentifierInfo info = lookup_identifier(interp, attach.identifier_id, adjusted_binding);

		ASSERT_OR_IGNORE(info.tag == EvalTag::Success);

		return closure_add_value(interp->closures, builder_id, attach.identifier_id, info.success.type_id, info.success.bytes.immut());
	}
	else
	{
		if (node->tag == AstTag::Block || node->tag == AstTag::Func)
			out_adjustment += 1;

		AstDirectChildIterator it = direct_children_of(node);

		while (has_next(&it))
		{
			AstNode* const child = next(&it);

			builder_id = close_over_rec(interp, builder_id, child, out_adjustment);
		}

		return builder_id;
	}
}

static ClosureId close_over_func_body(Interpreter* interp, AstNode* body) noexcept
{
	ClosureBuilderId builder_id = closure_create(interp->closures);

	builder_id = close_over_rec(interp, builder_id, body, 1);

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

static ShiftAmountResult shift_amount(Interpreter* interp, Range<byte> value, TypeId type_id, u16 max_shift_log2_ceil, u16* out) noexcept
{
	const TypeTag type_tag = type_tag_from_id(interp->types, type_id);

	if (type_tag == TypeTag::CompInteger)
	{
		ASSERT_OR_IGNORE(value.count() == sizeof(CompIntegerValue));

		const CompIntegerValue comp_integer_value = *reinterpret_cast<const CompIntegerValue*>(value.begin());

		if (comp_integer_compare(comp_integer_value, comp_integer_from_u64(0)) == StrongCompareOrdering::LessThan)
			return ShiftAmountResult::Negative;

		u64 shift;

		if (!u64_from_comp_integer(comp_integer_value, 16, &shift))
			return ShiftAmountResult::TooLarge;

		ASSERT_OR_IGNORE(shift <= UINT16_MAX);

		if (shift >= max_shift_log2_ceil)
			return ShiftAmountResult::TooLarge;

		*out = static_cast<u16>(shift);

		return ShiftAmountResult::InRange;
	}
	else if (type_tag == TypeTag::Integer)
	{
		const NumericType type = *type_attachment_from_id<NumericType>(interp->types, type_id);

		const u64 size = type.bits / 8;

		const u8 extra_bits = type.bits % 8;

		if (type.is_signed)
		{
			if (extra_bits == 0)
			{
				if ((value[size - 1] & 0x80) != 0)
					return ShiftAmountResult::Negative;
			}
			else
			{
				const u8 msb_mask = static_cast<u8>(1 << (extra_bits - 1));

				if ((value[size] & msb_mask) != 0)
					return ShiftAmountResult::Negative;
			}
		}

		for (u64 i = 8; i < size; ++i)
		{
			if (value[i] != 0)
				return ShiftAmountResult::TooLarge;
		}

		if (size >= 8 && extra_bits != 0)
		{
			const u8 bits = value[size] & static_cast<u8>((1 << extra_bits) - 1);

			if (bits != 0)
				return ShiftAmountResult::TooLarge;
		}

		u64 shift = 0;

		memcpy(&shift, value.begin(), size < 8 ? size : 8);

		if (extra_bits != 0)
		{
			const u8 bits = value[size] & static_cast<u8>((1 << extra_bits) - 1);

			shift |= bits << (size * 8);
		}

		if (shift >= max_shift_log2_ceil)
			return ShiftAmountResult::TooLarge;

		*out = static_cast<u16>(shift);

		return ShiftAmountResult::InRange;
	}
	else
	{
		return ShiftAmountResult::UnsupportedType;
	}
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

static void convert_comp_integer_to_integer(Interpreter* interp, const AstNode* error_source, EvalValue* dst, CompIntegerValue src) noexcept
{
	ASSERT_OR_IGNORE(!dst->is_location && type_tag_from_id(interp->types, dst->type_id) == TypeTag::Integer);

	const NumericType dst_type = *type_attachment_from_id<NumericType>(interp->types, dst->type_id);

	if (dst_type.is_signed)
	{
		s64 signed_value;

		if (!s64_from_comp_integer(src, static_cast<u8>(dst_type.bits), &signed_value))
			source_error(interp->errors, source_id_of(interp->asts, error_source), "Value of integer literal exceeds bounds of signed %u-bit integer.\n", dst_type.bits);

		const MutRange<byte> value = { reinterpret_cast<byte*>(&signed_value), static_cast<u64>((dst_type.bits + 7) / 8) };

		value_set(dst, value);
	}
	else
	{
		u64 unsigned_value;

		if (!u64_from_comp_integer(src, static_cast<u8>(dst_type.bits), &unsigned_value))
			source_error(interp->errors, source_id_of(interp->asts, error_source), "Value of integer literal exceeds bounds of unsigned %u-bit integer.\n", dst_type.bits);

		const MutRange<byte> value = { reinterpret_cast<byte*>(&unsigned_value), static_cast<u64>((dst_type.bits + 7) / 8) };

		value_set(dst, value);
	}
}

static void convert_comp_float_to_float(Interpreter* interp, EvalValue* dst, CompFloatValue src) noexcept
{
	ASSERT_OR_IGNORE(!dst->is_location && type_tag_from_id(interp->types, dst->type_id) == TypeTag::Integer);

	const NumericType dst_type = *type_attachment_from_id<NumericType>(interp->types, dst->type_id);

	ASSERT_OR_IGNORE(!dst_type.is_signed);

	if (dst_type.bits == 32)
	{
		f32 f32_value = f32_from_comp_float(src);

		value_set(dst, range::from_object_bytes_mut(&f32_value));
	}
	else
	{
		ASSERT_OR_IGNORE(dst_type.bits == 64);

		f64 f64_value = f64_from_comp_float(src);

		value_set(dst, range::from_object_bytes_mut(&f64_value));
	}
}

static void convert_array_to_slice(Interpreter* interp, const AstNode* error_source, EvalValue* dst, EvalValue src) noexcept
{
	ASSERT_OR_IGNORE(dst->type_id != TypeId::INVALID
				  && dst->bytes.count() == sizeof(MutRange<byte>)
				  && type_tag_from_id(interp->types, dst->type_id) == TypeTag::Slice
	);

	const ArrayType src_type = *type_attachment_from_id<ArrayType>(interp->types, src.type_id);

	const ReferenceType dst_type = *type_attachment_from_id<ReferenceType>(interp->types, dst->type_id);

	if (!type_is_equal(interp->types, src_type.element_type, dst_type.referenced_type_id))
		source_error(interp->errors, source_id_of(interp->asts, error_source), "Cannot implicitly convert array to slice with different element type");

	value_set(dst, range::from_object_bytes_mut(&src.bytes));
}

static void convert(Interpreter* interp, const AstNode* error_source, EvalValue* dst, EvalValue src) noexcept
{
	ASSERT_OR_IGNORE(dst->type_id != TypeId::INVALID
				  && dst->bytes.begin() != nullptr
				  && src.type_id != TypeId::INVALID
				  && src.bytes.begin() != nullptr
	);

	const TypeTag src_type_tag = type_tag_from_id(interp->types, src.type_id);

	switch (src_type_tag)
	{
	case TypeTag::CompInteger:
	{
		convert_comp_integer_to_integer(interp, error_source, dst, *value_as<CompIntegerValue>(src));

		return;
	}

	case TypeTag::CompFloat:
	{
		convert_comp_float_to_float(interp, dst, *value_as<CompFloatValue>(src));

		return;
	}

	case TypeTag::Array:
	{
		convert_array_to_slice(interp, error_source, dst, src);

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

static CompareResult compare(Interpreter* interp, TypeId common_type_id, Range<byte> lhs, Range<byte> rhs, const AstNode* error_source) noexcept
{
	ASSERT_OR_IGNORE(lhs.count() == rhs.count());

	const TypeTag common_type_tag = type_tag_from_id(interp->types, common_type_id);

	switch (common_type_tag)
	{
	case TypeTag::Void:
	{
		ASSERT_OR_IGNORE(lhs.count() == 0);

		return CompareResult{ CompareEquality::Equal };
	}

	case TypeTag::Type:
	case TypeTag::TypeInfo:
	case TypeTag::TypeBuilder:
	{
		ASSERT_OR_IGNORE(lhs.count() == sizeof(TypeId));

		const TypeId lhs_value = *reinterpret_cast<const TypeId*>(lhs.begin());

		const TypeId rhs_value = *reinterpret_cast<const TypeId*>(rhs.begin());

		return CompareResult{ type_is_equal(interp->types, lhs_value, rhs_value) ? CompareEquality::Equal : CompareEquality::Unequal };
	}

	case TypeTag::CompInteger:
	{
		ASSERT_OR_IGNORE(lhs.count() == sizeof(CompIntegerValue));

		const CompIntegerValue lhs_value = *reinterpret_cast<const CompIntegerValue*>(lhs.begin());

		const CompIntegerValue rhs_value = *reinterpret_cast<const CompIntegerValue*>(rhs.begin());

		return CompareResult{ static_cast<WeakCompareOrdering>(comp_integer_compare(lhs_value, rhs_value)) };
	}

	case TypeTag::CompFloat:
	{
		ASSERT_OR_IGNORE(lhs.count() == sizeof(CompFloatValue));

		const CompFloatValue lhs_value = *reinterpret_cast<const CompFloatValue*>(lhs.begin());

		const CompFloatValue rhs_value = *reinterpret_cast<const CompFloatValue*>(rhs.begin());

		return CompareResult{ comp_float_compare(lhs_value, rhs_value) };
	}

	case TypeTag::Boolean:
	{
		ASSERT_OR_IGNORE(lhs.count() == sizeof(bool));

		const bool lhs_value = *reinterpret_cast<const bool*>(lhs.begin());

		const bool rhs_value = *reinterpret_cast<const bool*>(rhs.begin());

		return CompareResult{ lhs_value == rhs_value ? CompareEquality::Equal : CompareEquality::Unequal };
	}

	case TypeTag::Integer:
	{
		const NumericType type = *type_attachment_from_id<NumericType>(interp->types, common_type_id);

		ASSERT_OR_IGNORE(lhs.count() == (static_cast<u64>(type.bits) + 7) >> 3);

		const s64 compare_size = static_cast<s64>(type.bits >> 3);

		const u8 extra_bits = type.bits & 7;

		if (extra_bits != 0)
		{
			const u8 mask = static_cast<u8>((1 << extra_bits) - 1);

			const u8 lhs_masked = lhs[compare_size] & mask;

			const u8 rhs_masked = rhs[compare_size] & mask;

			if (lhs_masked != rhs_masked)
			{
				if (type.is_signed)
				{
					const u8 msb_mask = static_cast<u8>(1 << (extra_bits - 1));

					const bool lhs_is_negative = (lhs_masked & msb_mask) != 0;

					const bool rhs_is_negative = (rhs_masked & msb_mask) != 0;

					if (lhs_is_negative && rhs_is_negative)
					{
						// Flip it.
						// And reverse it.
						return CompareResult{ lhs_masked < rhs_masked ? WeakCompareOrdering::GreaterThan : WeakCompareOrdering::LessThan };
					}
					else if (lhs_is_negative)
					{
						return CompareResult{ WeakCompareOrdering::LessThan };
					}
					else if (rhs_is_negative)
					{
						return CompareResult{ WeakCompareOrdering::GreaterThan };
					}
					else
					{
						return CompareResult{ lhs_masked < rhs_masked ? WeakCompareOrdering::LessThan : WeakCompareOrdering::GreaterThan };
					}
				}
				else
				{
					if (lhs_masked < rhs_masked)
						return CompareResult{ WeakCompareOrdering::LessThan };
					else
						return CompareResult{ WeakCompareOrdering::GreaterThan };
				}
			}
		}

		if (compare_size == 0)
			return CompareResult{ WeakCompareOrdering::Equal };

		s64 i = compare_size - 1;

		bool negate_comparison = false;

		if (type.is_signed)
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
		const NumericType type = *type_attachment_from_id<NumericType>(interp->types, common_type_id);

		if (type.bits == 32)
		{
			ASSERT_OR_IGNORE(lhs.count() == sizeof(f32));

			const f32 lhs_value = *reinterpret_cast<const f32*>(lhs.begin());

			const f32 rhs_value = *reinterpret_cast<const f32*>(rhs.begin());

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
			ASSERT_OR_IGNORE(lhs.count() == sizeof(f64) && type.bits == 64);

			const f64 lhs_value = *reinterpret_cast<const f64*>(lhs.begin());

			const f64 rhs_value = *reinterpret_cast<const f64*>(rhs.begin());

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
		ASSERT_OR_IGNORE(lhs.count() == sizeof(void*) * 2);

		return CompareResult{ range::mem_equal(lhs, rhs) ? CompareEquality::Equal : CompareEquality::Unequal };
	}

	case TypeTag::Ptr:
	{
		ASSERT_OR_IGNORE(lhs.count() == sizeof(void*));

		return CompareResult{ range::mem_equal(lhs, rhs) ? CompareEquality::Equal : CompareEquality::Unequal };
	}

	case TypeTag::Array:
	{
		const ArrayType type = *type_attachment_from_id<ArrayType>(interp->types, common_type_id);

		if (type.element_type == TypeId::INVALID)
		{
			ASSERT_OR_IGNORE(type.element_count == 0 && lhs.count() == 0);

			return CompareResult{ CompareEquality::Equal };
		}

		const TypeMetrics metrics = type_metrics_from_id(interp->types, type.element_type);

		for (u64 i = 0; i != type.element_count; ++i)
		{
			const Range<byte> lhs_elem{ lhs.begin() + i * metrics.stride, metrics.size };

			const Range<byte> rhs_elem{ rhs.begin() + i * metrics.stride, metrics.size };

			const CompareResult result = compare(interp, type.element_type, lhs_elem, rhs_elem, error_source);

			if (result.tag == CompareTag::INVALID)
				return CompareResult{};
			else if (result.equality != CompareEquality::Equal)
				return CompareResult{ CompareEquality::Unequal };
		}

		return CompareResult{ CompareEquality::Equal };
	}

	case TypeTag::Composite:
	{
		ASSERT_OR_IGNORE(lhs.count() == type_metrics_from_id(interp->types, common_type_id).size);

		MemberIterator it = members_of(interp->types, common_type_id);

		while (has_next(&it))
		{
			const Member* const member = next(&it);

			if (member->has_pending_type || member->has_pending_value)
				source_error(interp->errors, source_id_of(interp->asts, error_source), "Tried comparing values of incomplete composite type.\n");

			if (member->is_global)
				continue;

			const TypeMetrics metrics = type_metrics_from_id(interp->types, member->type.complete);

			const Range<byte> lhs_member{ lhs.begin() + member->offset, metrics.size };

			const Range<byte> rhs_member{ rhs.begin() + member->offset, metrics.size };

			const CompareResult result = compare(interp, member->type.complete, lhs_member, rhs_member, error_source);

			if (result.tag == CompareTag::INVALID)
				return CompareResult{};
			else if (result.equality != CompareEquality::Equal)
				return CompareResult{ CompareEquality::Unequal };
		}

		return CompareResult{ CompareEquality::Equal };
	}
	case TypeTag::Definition:
	case TypeTag::Func:
	case TypeTag::Builtin:
	case TypeTag::TailArray:
		return CompareResult{};

	case TypeTag::CompositeLiteral:
	case TypeTag::ArrayLiteral:
	case TypeTag::Variadic:
	case TypeTag::Divergent:
	case TypeTag::Trait:
	case TypeTag::INVALID:
	case TypeTag::INDIRECTION:
		; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

static EvalRst fill_spec(Interpreter* interp, EvalSpec spec, const AstNode* error_source, bool is_location, bool is_mut, TypeId inferred_type_id) noexcept
{
	ASSERT_OR_IGNORE(is_location || is_mut);

	if (spec.kind == ValueKind::Location && !is_location)
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
			bytes = {};
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
	rst.success.type_id = type_id;
	rst.success = make_value(bytes, spec.kind == ValueKind::Location, is_mut, type_id);

	return rst;
}

static EvalRst fill_spec_sized(Interpreter* interp, EvalSpec spec, const AstNode* error_source, bool is_location, bool is_mut, TypeId inferred_type_id, u64 size, u32 align) noexcept
{
	ASSERT_OR_IGNORE(is_location || is_mut);

	if (spec.kind == ValueKind::Location && !is_location)
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
			bytes = {};
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
	rst.success = make_value(bytes, spec.kind == ValueKind::Location, is_mut, type_id);

	return rst;
}

static EvalRst evaluate_global_member(Interpreter* interp, AstNode* node, EvalSpec spec, TypeId surrounding_type_id, const Member* member) noexcept
{
	ASSERT_OR_IGNORE(member->is_global);

	complete_member(interp, surrounding_type_id, member);

	EvalRst rst = fill_spec(interp, spec, node, true, member->is_mut, member->type.complete);

	MutRange<byte> value = global_value_get_mut(interp->globals, member->value.complete);

	if (type_is_equal(interp->types, rst.success.type_id, member->type.complete))
	{
		value_set(&rst.success, value);
	}
	else if (!rst.success.is_location)
	{
		convert(interp, node, &rst.success, make_value(value, true, member->is_mut, member->type.complete));
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

	EvalRst rst = fill_spec_sized(interp, spec, node, true, member->is_mut, member->type.complete, metrics.size, metrics.align);

	const MutRange<byte> member_bytes = MutRange<byte>{ lhs_value.begin() + member->offset, metrics.size };

	if (type_is_equal(interp->types, rst.success.type_id, member->type.complete))
	{
		value_set(&rst.success, member_bytes);
	}
	else if (!rst.success.is_location)
	{
		convert(interp, node, &rst.success, make_value(member_bytes, true, member->is_mut, member->type.complete));
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

static Arec* check_binary_expr_for_unbound(Interpreter* interp, AstNode* lhs, AstNode* rhs, const EvalRst* lhs_rst, const EvalRst* rhs_rst) noexcept
{
	if (lhs_rst->tag == EvalTag::Success && rhs_rst->tag == EvalTag::Success)
	{
		return nullptr;
	}
	else if (lhs_rst->tag == EvalTag::Success)
	{
		add_partial_value_to_builder(interp, lhs, lhs_rst->success.type_id, lhs_rst->success.bytes.immut());

		return rhs_rst->unbound;
	}
	else if (rhs_rst->tag == EvalTag::Success)
	{
		add_partial_value_to_builder(interp, rhs, rhs_rst->success.type_id, rhs_rst->success.bytes.immut());

		return lhs_rst->unbound;
	}
	else
	{
		return lhs_rst->unbound < rhs_rst->unbound ? lhs_rst->unbound : rhs_rst->unbound;
	}
}

static bool evaluate_commonly_typed_binary_expr(Interpreter* interp, AstNode* node, EvalValue* out_lhs, EvalValue* out_rhs, TypeId* out_common_type_id, Arec** out_unbound) noexcept
{
	AstNode* const lhs = first_child_of(node);

	const EvalRst lhs_rst = evaluate(interp, lhs, EvalSpec{ ValueKind::Value });

	AstNode* const rhs = next_sibling_of(lhs);

	const EvalRst rhs_rst = evaluate(interp, rhs, EvalSpec{ ValueKind::Value });

	Arec* const unbound = check_binary_expr_for_unbound(interp, lhs, rhs, &lhs_rst, &rhs_rst);

	if (unbound != nullptr)
	{
		*out_unbound = unbound;

		return false;
	}

	const TypeId common_type_id = type_unify(interp->types, lhs_rst.success.type_id, rhs_rst.success.type_id);

	if (common_type_id == TypeId::INVALID)
		source_error(interp->errors, source_id_of(interp->asts, node), "Could not unify argument types of `%s`.\n", tag_name(node->tag));

	if (!type_is_equal(interp->types, common_type_id, lhs_rst.success.type_id))
	{
		const TypeMetrics metrics = type_metrics_from_id(interp->types, common_type_id);

		*out_lhs = make_value(stack_push(interp, metrics.size, metrics.align), false, true, common_type_id);

		convert(interp, lhs, out_lhs, lhs_rst.success);
	}
	else
	{
		*out_lhs = lhs_rst.success;
	}

	if (!type_is_equal(interp->types, common_type_id, rhs_rst.success.type_id))
	{
		const TypeMetrics metrics = type_metrics_from_id(interp->types, common_type_id);

		*out_rhs = make_value(stack_push(interp, metrics.size, metrics.align), false, true, common_type_id);

		convert(interp, rhs, out_rhs, rhs_rst.success);
	}
	else
	{
		*out_rhs = rhs_rst.success;
	}

	*out_common_type_id = common_type_id;

	return true;
}

static CallInfo setup_call_args(Interpreter* interp, const SignatureType* signature_type, AstNode* callee) noexcept
{
	if (signature_type->parameter_list_is_unbound || signature_type->return_type_is_unbound)
		push_partial_value(interp, signature_type->partial_value_id);

	const TypeId parameter_list_type_id = signature_type->parameter_list_is_unbound
		? type_copy_composite(interp->types, signature_type->parameter_list_type_id, signature_type->param_count, true)
		: signature_type->parameter_list_type_id;

	const TypeId global_scope_type_id = type_global_scope_from_id(interp->types, parameter_list_type_id);

	const ArecId caller_arec_id = active_arec_id(interp);

	const TypeMetrics parameter_list_metrics = type_metrics_from_id(interp->types, parameter_list_type_id);

	const ArecId parameter_list_arec_id = arec_push(interp, parameter_list_type_id, parameter_list_metrics.size, parameter_list_metrics.align, ArecId::INVALID, ArecKind::Normal, global_scope_type_id, source_id_of(interp->asts, callee), caller_arec_id);

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
		else if (type_tag_from_id(interp->types, param->type.complete) == TypeTag::Func)
			TODO("Add binding of nested signatures");

		const TypeMetrics param_metrics = type_metrics_from_id(interp->types, param->type.complete);

		if (has_pending_type)
			arec_grow(interp, parameter_list_arec_id, param->offset + param_metrics.size);

		MutRange<byte> attach = arec_attach(interp, parameter_list_arec);

		const EvalRst arg_rst = evaluate(interp, arg, EvalSpec{
			ValueKind::Value,
			attach.mut_subrange(param->offset, param_metrics.size),
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
		EvalRst rst = fill_spec(interp, spec, node, true, false, applicable_partial.type_id);

		const MutRange<byte> mutified = MutRange<byte>{ const_cast<byte*>(applicable_partial.data.begin()), applicable_partial.data.count() };

		if (type_is_equal(interp->types, applicable_partial.type_id, rst.success.type_id))
		{
			value_set(&rst.success, mutified);
		}
		else
		{
			convert(interp, node, &rst.success, make_value(mutified, true, false, applicable_partial.type_id));
		}

		return rst;
	}
	else if (spec.type_id != TypeId::INVALID && type_tag_from_id(interp->types, spec.type_id) == TypeTag::TypeInfo)
	{
		const TypeId type_type_id = type_create_simple(interp->types, TypeTag::Type);

		EvalRst rst = fill_spec_sized(interp, spec, node, false, true, type_type_id, sizeof(TypeId), alignof(TypeId));

		TypeId expression_type = typeinfer(interp, node);

		value_set(&rst.success, range::from_object_bytes_mut(&expression_type));

		return rst;
	}
	else switch (node->tag)
	{
	case AstTag::Builtin:
	{
		const u8 ordinal = static_cast<u8>(node->flags);

		ASSERT_OR_IGNORE(ordinal < array_count(interp->builtin_type_ids));

		const TypeId builtin_type_id = interp->builtin_type_ids[ordinal];

		EvalRst rst = fill_spec_sized(interp, spec, node, false, true, builtin_type_id, sizeof(CallableValue), alignof(CallableValue));

		CallableValue callable = CallableValue::from_builtin(builtin_type_id, ordinal);

		value_set(&rst.success, range::from_object_bytes_mut(&callable));

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

		EvalValue* const values = reinterpret_cast<EvalValue*>(stack_push(interp, elem_count * sizeof(EvalValue), alignof(EvalValue)).begin());

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
				values[i] = elem_rst.success;

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
				if (unbound_in == nullptr || unbound_in > elem_rst.unbound)
					unbound_in = elem_rst.unbound;

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

				EvalValue value = values[i];

				if (value.bytes.begin() == nullptr)
					continue;

				add_partial_value_to_builder(interp, elem, value.type_id, value.bytes.immut());
			}

			ASSERT_OR_IGNORE(!has_next(&elems));

			stack_shrink(interp, mark);

			return eval_unbound(unbound_in);
		}
		else
		{
			const TypeId inferred_type_id = type_create_array(interp->types, TypeTag::ArrayLiteral, ArrayType{ elem_count, unified_elem_type_id });

			EvalRst rst = fill_spec(interp, spec, node, false, true, inferred_type_id);

			const ArrayType* const rst_type = type_attachment_from_id<ArrayType>(interp->types, rst.success.type_id);

			unified_elem_type_id = rst_type->element_type;

			const TypeMetrics elem_metrics = unified_elem_type_id == TypeId::INVALID
				? TypeMetrics{ 0, 0, 1 }
				: type_metrics_from_id(interp->types, unified_elem_type_id);

			elems = direct_children_of(node);

			for (u32 i = 0; i != elem_count; ++i)
			{
				AstNode* const elem = next(&elems);

				EvalValue value = values[i];

				MutRange<byte> elem_value_dst = rst.success.bytes.mut_subrange(i * elem_metrics.stride, elem_metrics.size);

				if (type_is_equal(interp->types, value.type_id, unified_elem_type_id))
				{
					range::mem_copy(elem_value_dst, value.bytes.immut());
				}
				else
				{
					EvalValue dst_value = make_value(elem_value_dst, false, true, unified_elem_type_id);

					convert(interp, elem, &dst_value, value);
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

		EvalRst rst = fill_spec_sized(interp, spec, node, false, true, definition_type_id, sizeof(Definition), alignof(Definition));

		const DefinitionInfo info = get_definition_info(node);

		const AstDefinitionData attach = *attachment_of<AstDefinitionData>(node);

		Definition* const definition = reinterpret_cast<Definition*>(rst.success.bytes.begin());
		memset(definition, 0, sizeof(*definition));
		definition->name = attach.identifier_id;
		definition->type.pending = is_some(info.type) ? id_from_ast_node(interp->asts, get_ptr(info.type)) : AstNodeId::INVALID;
		definition->value.pending = is_some(info.value) ? id_from_ast_node(interp->asts, get_ptr(info.value)) : AstNodeId::INVALID;
		definition->is_global = has_flag(node, AstFlag::Definition_IsGlobal);
		definition->is_pub = has_flag(node, AstFlag::Definition_IsPub);
		definition->is_mut = has_flag(node, AstFlag::Definition_IsMut);
		definition->has_pending_type = true;
		definition->has_pending_value = is_some(info.value);
		definition->type_completion_arec_id = is_some(info.type) ? active_arec_id(interp) : ArecId::INVALID;
		definition->value_completion_arec_id = is_some(info.value) ? active_arec_id(interp) : ArecId::INVALID;

		return rst;
	}

	case AstTag::Block:
	{
		const TypeId global_scope_type_id = active_arec_global_scope_type_id(interp);

		const TypeId block_type_id = type_create_composite(interp->types, global_scope_type_id, TypeDisposition::Block, SourceId::INVALID, 0, false);

		const ArecId block_arec_id = arec_push(interp, block_type_id, 0, 1, active_arec_id(interp), ArecKind::Normal, global_scope_type_id, source_id_of(interp->asts, node), active_arec(interp)->caller_arec_id);

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

					MutRange<byte> attach = arec_attach(interp, block_arec);

					const EvalRst value_rst = evaluate(interp, get_ptr(info.value), EvalSpec{
						ValueKind::Value,
						attach.mut_subrange(member->offset, metrics.size),
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

					MutRange<byte> attach = arec_attach(interp, block_arec);

					range::mem_copy(attach.mut_subrange(member->offset, metrics.size), value_rst.success.bytes.immut());
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

				if (stmt_rst.tag == EvalTag::Unbound)
					source_error(interp->errors, source_id_of(interp->asts, node), "Cannot use block in unbound context.\n");

				if (!has_next_sibling(stmt))
				{
					EvalRst rst = fill_spec_sized(interp, spec, node, false, true, stmt_rst.success.type_id, metrics.size, metrics.align);

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

		return fill_spec_sized(interp, spec, node, false, true, type_create_simple(interp->types, TypeTag::Void), 0, 1);
	}

	case AstTag::If:
	{
		IfInfo info = get_if_info(node);

		bool cond_val;

		const EvalRst cond_rst = evaluate(interp, info.condition, EvalSpec{
			ValueKind::Value,
			range::from_object_bytes_mut(&cond_val),
			type_create_simple(interp->types, TypeTag::Boolean)
		});

		if (cond_rst.tag == EvalTag::Unbound)
			TODO("Implement unbound if alternative");

		AstNode* taken;

		if (cond_val)
		{
			taken = info.consequent;
		}
		else if (is_some(info.alternative))
		{
			taken = get_ptr(info.alternative);
		}
		else
		{
			return fill_spec_sized(interp, spec, node, false, true, type_create_simple(interp->types, TypeTag::Void), 0, 1);
		}

		const EvalRst rst = evaluate(interp, taken, EvalSpec{
			ValueKind::Value,
			spec.dst,
			spec.type_id
		});

		if (rst.tag == EvalTag::Unbound)
			TODO("Implement unbound if branch.");

		if (spec.kind == ValueKind::Location)
			source_error(interp->errors, source_id_of(interp->asts, node), "Cannot convert value to location.\n");

		return rst;
	}

	case AstTag::For:
	{
		ForInfo info = get_for_info(node);

		if (is_some(info.where))
			TODO("Implement where on for loops");

		if (is_some(info.finally))
			TODO("Implement finally on for loops.");

		while (true)
		{
			bool cond_val;

			const EvalRst cond_rst = evaluate(interp, info.condition, EvalSpec{
				ValueKind::Value,
				range::from_object_bytes_mut(&cond_val),
				type_create_simple(interp->types, TypeTag::Boolean)
			});

			if (cond_rst.tag != EvalTag::Success)
				source_error(interp->errors, source_id_of(interp->asts, node), "Cannot use `for` in an unbound context.\n");

			if (!cond_val)
				break;

			byte unused_body_val;

			const EvalRst body_rst = evaluate(interp, info.body, EvalSpec{
				ValueKind::Value,
				{ &unused_body_val, static_cast<u64>(0) },
				type_create_simple(interp->types, TypeTag::Void)				
			});

			if (body_rst.tag != EvalTag::Success)
				source_error(interp->errors, source_id_of(interp->asts, node), "Cannot use `for` in an unbound context.\n");

			if (is_some(info.step))
			{
				byte unused_step_val;

				const EvalRst step_rst = evaluate(interp, get_ptr(info.step), EvalSpec{
					ValueKind::Value,
					{ &unused_step_val, static_cast<u64>(0) },
					type_create_simple(interp->types, TypeTag::Void)
				});

				if (step_rst.tag != EvalTag::Success)
					source_error(interp->errors, source_id_of(interp->asts, node), "Cannot use `for` in an unbound context.\n");
			}
		}

		return fill_spec_sized(interp, spec, node, false, true, type_create_simple(interp->types, TypeTag::Void), 0, 1);
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

		EvalRst rst = fill_spec_sized(interp, spec, node, false, true, signature_type_id, sizeof(CallableValue), alignof(CallableValue));

		AstNode* const body = next_sibling_of(signature);

		const AstNodeId body_id = id_from_ast_node(interp->asts, body);

		const ClosureId closure_id = close_over_func_body(interp, body);

		CallableValue callable = CallableValue::from_function(signature_type_id, body_id, closure_id);

		value_set(&rst.success, range::from_object_bytes_mut(&callable));

		return rst;
	}

	case AstTag::Signature:
	{
		const TypeId type_type_id = type_create_simple(interp->types, TypeTag::Type);

		EvalRst rst = fill_spec_sized(interp, spec, node, false, true, type_type_id, sizeof(TypeId), alignof(TypeId));

		SignatureInfo info = get_signature_info(node);

		const TypeId global_scope_type_id = active_arec_global_scope_type_id(interp);

		const TypeId parameter_list_type_id = type_create_composite(interp->types, global_scope_type_id, TypeDisposition::Signature, SourceId::INVALID, 0, false);

		const ArecId parameter_list_arec_id = arec_push(interp, parameter_list_type_id, 0, 1, active_arec_id(interp), ArecKind::Unbound, global_scope_type_id, source_id_of(interp->asts, node), active_arec(interp)->caller_arec_id);

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

					if (type_rst.unbound < outermost_unbound)
						outermost_unbound = type_rst.unbound;
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
						ASSERT_OR_IGNORE(value_rst.unbound != parameter_list_arec);

						has_unbound_parameter = true;

						if (value_rst.unbound < outermost_unbound)
							outermost_unbound = value_rst.unbound;
					}
					else if (type_is_unbound)
					{
						add_partial_value_to_builder(interp, value, value_rst.success.type_id, value_rst.success.bytes.immut());
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

			if (has_unbound_return_type && return_type_rst.unbound < outermost_unbound)
				outermost_unbound = return_type_rst.unbound;
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
					add_partial_value_to_builder_sized(interp, get_ptr(param_info.type), type_type_id, range::from_object_bytes(&member->type.complete), sizeof(TypeId), alignof(TypeId));
				}

				if (is_some(param_info.value) && !member->has_pending_value)
				{
					ASSERT_OR_IGNORE(!member->has_pending_type);

					Range<byte> value_src = global_value_get(interp->globals, member->value.complete);

					add_partial_value_to_builder(interp, get_ptr(param_info.value), member->type.complete, value_src);
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

			TypeId signature_type_id = type_create_signature(interp->types, TypeTag::Func, signature_type);

			value_set(&rst.success, range::from_object_bytes_mut(&signature_type_id));

			return rst;
		}
	}

	case AstTag::Unreachable:
	{
		source_error_nonfatal(interp->errors, source_id_of(interp->asts, node), "Reached `unreachable`.\n");

		print_stack_trace(interp);

		error_exit(interp->errors);
	}

	case AstTag::Undefined:
	{
		if (spec.type_id == TypeId::INVALID)
			source_error(interp->errors, source_id_of(interp->asts, node), "Type of `undefined` must be inferrable from context.\n");

		EvalRst rst = fill_spec(interp, spec, node, false, true, spec.type_id);

		// We are returning uninitialized memory, so we might as well add a
		// nice debug pattern <3
		range::mem_set(rst.success.bytes, 0x5A);

		return rst;
	}

	case AstTag::Identifier:
	{
		const AstIdentifierData attach = *attachment_of<AstIdentifierData>(node);

		const IdentifierInfo info = lookup_identifier(interp, attach.identifier_id, attach.binding);

		if (info.tag == EvalTag::Unbound)
			return eval_unbound(get_ptr(info.arec));

		EvalRst rst = fill_spec(interp, spec, node, true, info.success.is_mut, info.success.type_id);

		if (type_is_equal(interp->types, info.success.type_id, rst.success.type_id))
		{
			value_set(&rst.success, info.success.bytes);
		}
		else if (!rst.success.is_location)
		{
			convert(interp, node, &rst.success, info.success);
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

		EvalRst rst = fill_spec_sized(interp, spec, node, false, true, comp_integer_type_id, sizeof(CompIntegerValue), alignof(CompIntegerValue));

		CompIntegerValue value = attachment_of<AstLitIntegerData>(node)->value;

		if (type_is_equal(interp->types, rst.success.type_id, comp_integer_type_id))
			value_set(&rst.success, range::from_object_bytes_mut(&value));
		else
			convert_comp_integer_to_integer(interp, node, &rst.success, value);

		return rst;
	}

	case AstTag::LitFloat:
	{
		const TypeId comp_float_type_id = type_create_simple(interp->types, TypeTag::CompFloat);

		EvalRst rst = fill_spec_sized(interp, spec, node, false, true, comp_float_type_id, sizeof(CompFloatValue), alignof(CompFloatValue));

		CompFloatValue value = attachment_of<AstLitFloatData>(node)->value;

		if (type_is_equal(interp->types, rst.success.type_id, comp_float_type_id))
		{
			value_set(&rst.success, range::from_object_bytes_mut(&value));
		}
		else
		{
			ASSERT_OR_IGNORE(type_tag_from_id(interp->types, rst.success.type_id) == TypeTag::Float);

			const NumericType type = *type_attachment_from_id<NumericType>(interp->types, rst.success.type_id);

			if (type.bits == 32)
			{
				f32 f32_value = f32_from_comp_float(value);

				value_set(&rst.success, range::from_object_bytes_mut(&f32_value));
			}
			else
			{
				ASSERT_OR_IGNORE(type.bits == 64);

				f64 f64_value = f64_from_comp_float(value);

				value_set(&rst.success, range::from_object_bytes_mut(&f64_value));
			}
		}

		return rst;
	}

	case AstTag::LitString:
	{
		const AstLitStringData attach = *attachment_of<AstLitStringData>(node);

		EvalRst rst = fill_spec(interp, spec, node, false, true, attach.string_type_id);

		MutRange<byte> value = global_value_get_mut(interp->globals, attach.string_value_id);

		if (type_is_equal(interp->types, rst.success.type_id, attach.string_type_id))
			value_set(&rst.success, value);
		else if (!rst.success.is_location)
			convert_array_to_slice(interp, node, &rst.success, make_value(value, false, true, attach.string_type_id));
		else
			source_error(interp->errors, source_id_of(interp->asts, node), "Cannot treat string litearal as location as it requires an implicit conversion to conform to the desired type.\n");

		return rst;
	}

	case AstTag::Call:
	{
		const u32 callee_mark = stack_mark(interp);

		AstNode* const callee = first_child_of(node);

		CallableValue callee_value;

		const EvalRst callee_rst = evaluate(interp, callee, EvalSpec{
			ValueKind::Value,
			range::from_object_bytes_mut(&callee_value)
		});

		stack_shrink(interp, callee_mark);

		EvalRst rst;

		if (callee_rst.tag == EvalTag::Unbound)
		{
			TODO("Implement unbound callees");
		}

		const TypeTag callee_type_tag = type_tag_from_id(interp->types, callee_rst.success.type_id);

		if (callee_type_tag != TypeTag::Func && callee_type_tag != TypeTag::Builtin)
			source_error(interp->errors, source_id_of(interp->asts, callee), "Cannot implicitly convert callee to callable type.\n");

		const SignatureType* const signature_type = type_attachment_from_id<SignatureType>(interp->types, callee_value.signature_type_id);

		const CallInfo call_info = setup_call_args(interp, signature_type, callee);

		rst = fill_spec(interp, spec, node, false, true, call_info.return_type_id);

		const u32 mark = stack_mark(interp);

		const bool needs_conversion = !type_is_equal(interp->types, call_info.return_type_id, rst.success.type_id);

		MutRange<byte> temp_location;

		if (needs_conversion)
		{
			const TypeMetrics return_type_metrics = type_metrics_from_id(interp->types, call_info.return_type_id);

			temp_location = stack_push(interp, return_type_metrics.size, return_type_metrics.align);
		}
		else
		{
			temp_location = rst.success.bytes;
		}

		if (callee_value.is_builtin)
		{
			Arec* const parameter_list_arec = arec_from_id(interp, call_info.parameter_list_arec_id);

			interp->builtin_values[callee_value.ordinal](interp, parameter_list_arec, temp_location);
		}
		else
		{
			if (callee_value.closure_id != ClosureId::INVALID)
			{
				ClosureInstance instance = closure_instance(interp->closures, callee_value.closure_id);

				interp->active_closures.append(instance);
			}

			AstNode* const body = ast_node_from_id(interp->asts, static_cast<AstNodeId>(callee_value.body_ast_node_id));

			const EvalRst call_rst = evaluate(interp, body, EvalSpec{
				ValueKind::Value,
				temp_location,
				rst.success.type_id
			});

			if (callee_value.closure_id != ClosureId::INVALID)
				interp->active_closures.pop_by(1);

			ASSERT_OR_IGNORE(call_rst.tag == EvalTag::Success);
		}

		if (needs_conversion)
			convert(interp, node, &rst.success, make_value(temp_location, false, true, call_info.return_type_id));

		arec_pop(interp, call_info.parameter_list_arec_id);

		stack_shrink(interp, mark);

		return rst;
	}

	case AstTag::UOpTypeSlice:
	case AstTag::UOpTypeMultiPtr:
	case AstTag::UOpTypeOptMultiPtr:
	case AstTag::UOpTypeOptPtr:
	case AstTag::UOpTypeVarArgs:
	case AstTag::UOpTypePtr:
	{
		const TypeId type_type_id = type_create_simple(interp->types, TypeTag::Type);

		EvalRst rst = fill_spec_sized(interp, spec, node, false, true, type_type_id, sizeof(TypeId), alignof(TypeId));

		const u32 mark = stack_mark(interp);

		AstNode* const referenced = first_child_of(node);

		TypeId referenced_type_id;

		EvalRst referenced_rst = evaluate(interp, referenced, EvalSpec{
			ValueKind::Value,
			range::from_object_bytes_mut(&referenced_type_id),
			type_type_id
		});

		if (referenced_rst.tag == EvalTag::Unbound)
			return eval_unbound(referenced_rst.unbound);

		ReferenceType reference_type{};
		reference_type.referenced_type_id = referenced_type_id;
		reference_type.is_opt = node->tag == AstTag::UOpTypeOptPtr || node->tag == AstTag::UOpTypeOptMultiPtr;
		reference_type.is_multi = node->tag == AstTag::UOpTypeMultiPtr || node->tag == AstTag::UOpTypeOptMultiPtr;
		reference_type.is_mut = has_flag(node, AstFlag::Type_IsMut);

		const TypeTag reference_type_tag = node->tag == AstTag::UOpTypeSlice ? TypeTag::Slice : node->tag == AstTag::UOpTypeVarArgs ? TypeTag::Variadic : TypeTag::Ptr;

		TypeId reference_type_id = type_create_reference(interp->types, reference_type_tag, reference_type);

		value_set(&rst.success, range::from_object_bytes_mut(&reference_type_id));

		stack_shrink(interp, mark);

		return rst;
	}

	case AstTag::UOpAddr:
	{
		const u32 mark = stack_mark(interp);

		AstNode* const operand = first_child_of(node);

		EvalRst operand_rst = evaluate(interp, operand, EvalSpec{
			ValueKind::Location
		});

		if (operand_rst.tag == EvalTag::Unbound)
		{
			stack_shrink(interp, mark);

			return eval_unbound(operand_rst.unbound);
		}
		
		// Create and initialize pointer type.
		ReferenceType ptr_type{};
		ptr_type.referenced_type_id = operand_rst.success.type_id;
		ptr_type.is_multi = true;
		ptr_type.is_mut = operand_rst.success.is_mut;
		ptr_type.is_opt = false;

		const TypeId ptr_type_id = type_create_reference(interp->types, TypeTag::Ptr, ptr_type);

		EvalRst rst = fill_spec_sized(interp, spec, node, false, true, ptr_type_id, sizeof(void*), alignof(void*));
		
		byte* address = operand_rst.success.bytes.begin();
		
		value_set(&rst.success, range::from_object_bytes_mut(&address));

		rst.success.bytes = stack_copy_down(interp, mark, rst.success.bytes);

		return rst;
	}

	case AstTag::UOpDeref:
	{
		const u32 mark = stack_mark(interp);

		AstNode* const operand = first_child_of(node);

		EvalRst operand_rst = evaluate(interp, operand, EvalSpec{
			ValueKind::Value
		});

		if (operand_rst.tag == EvalTag::Unbound)
		{
			stack_shrink(interp, mark);

			return eval_unbound(operand_rst.unbound);
		}

		// type if of the reference, contains type info of the derefed type
		TypeId reference_type_id = operand_rst.success.type_id;

		if (type_tag_from_id(interp->types, reference_type_id) != TypeTag::Ptr)
			source_error(interp->errors, source_id_of(interp->asts, operand), "Operand of `.*` must be a pointer.\n");

		const ReferenceType* reference_type = type_attachment_from_id<ReferenceType>(interp->types, reference_type_id);
		
		TypeId dereferenced_type = reference_type->referenced_type_id;
		
		EvalRst rst = fill_spec(interp, spec, node, true, reference_type->is_mut, dereferenced_type);

		byte* const ptr = *value_as<byte*>(operand_rst.success);

		const TypeMetrics metrics = type_metrics_from_id(interp->types, dereferenced_type);
		
		value_set(&rst.success, { ptr, metrics.size });

		stack_shrink(interp, mark);

		return rst;
	}

	case AstTag::UOpLogNot:
	{
		const TypeId bool_type_id = type_create_simple(interp->types, TypeTag::Boolean);

		EvalRst rst = fill_spec_sized(interp, spec, node, false, true, bool_type_id, sizeof(bool), alignof(bool));

		const u32 mark = stack_mark(interp);

		AstNode* const operand = first_child_of(node);

		bool operand_value;

		const EvalRst operand_rst = evaluate(interp, operand, EvalSpec{
			ValueKind::Value,
			range::from_object_bytes_mut(&operand_value),
			bool_type_id
		});

		if (operand_rst.tag == EvalTag::Unbound)
		{
			stack_shrink(interp, mark);

			return eval_unbound(operand_rst.unbound);
		}

		operand_value = !operand_value;

		value_set(&rst.success, range::from_object_bytes_mut(&operand_value));

		stack_shrink(interp, mark);

		return rst;
	}

	case AstTag::OpAdd:
	case AstTag::OpSub:
	case AstTag::OpMul:
	case AstTag::OpDiv:
	case AstTag::OpMod:
	{
		const u32 mark = stack_mark(interp);

		AstNode* const lhs = first_child_of(node);

		EvalRst lhs_rst = evaluate(interp, lhs, EvalSpec{
			ValueKind::Value
		});

		AstNode* const rhs = next_sibling_of(lhs);

		EvalRst rhs_rst = evaluate(interp, rhs, EvalSpec{
			ValueKind::Value
		});

		const TypeId unified_type_id = type_unify(interp->types, lhs_rst.success.type_id, rhs_rst.success.type_id);

		if (unified_type_id == TypeId::INVALID)
			source_error(interp->errors, source_id_of(interp->asts, node), "Incompatible operand types passed to `%s` operator.\n", tag_name(node->tag));

		const TypeTag unified_type_tag = type_tag_from_id(interp->types, unified_type_id);

		if (unified_type_tag != TypeTag::Integer
		 && unified_type_tag != TypeTag::Float
		 && unified_type_tag != TypeTag::CompInteger
		 && unified_type_tag != TypeTag::CompFloat)
			source_error(interp->errors, source_id_of(interp->asts, lhs), "The `%s` operator is only supported for Integer and Float values!\n", tag_name(node->tag));

		const TypeMetrics metrics = type_metrics_from_id(interp->types, unified_type_id);

		EvalValue lhs_casted;

		if (type_is_equal(interp->types, unified_type_id, lhs_rst.success.type_id))
		{
			lhs_casted = lhs_rst.success;
		}
		else
		{
			lhs_casted = make_value(stack_push(interp, metrics.size, metrics.align), false, true, unified_type_id);

			convert(interp, lhs, &lhs_casted, lhs_rst.success);
		}

		EvalValue rhs_casted;

		if (type_is_equal(interp->types, unified_type_id, rhs_rst.success.type_id))
		{
			rhs_casted = rhs_rst.success;
		}
		else
		{
			rhs_casted = make_value(stack_push(interp, metrics.size, metrics.align), false, true, unified_type_id);

			convert(interp, rhs, &rhs_casted, rhs_rst.success);
		}

		EvalRst rst = fill_spec(interp, spec, node, false, true, unified_type_id);

		if (unified_type_tag == TypeTag::Float)
		{
			if (node->tag == AstTag::OpMod)
				source_error(interp->errors, source_id_of(interp->asts, node), "Operator `%s` does not support floating-point operands.\n", tag_name(node->tag));

			const NumericType* const type = type_attachment_from_id<NumericType>(interp->types, unified_type_id);

			if (type->bits == 32)
			{
				const f32 lhs_value = *value_as<f32>(lhs_casted);

				const f32 rhs_value = *value_as<f32>(rhs_casted);

				f32 rst_value;

				if (node->tag == AstTag::OpAdd)
					rst_value = lhs_value + rhs_value;
				else if (node->tag == AstTag::OpSub)
					rst_value = lhs_value - rhs_value;
				else if (node->tag == AstTag::OpMul)
					rst_value = lhs_value * rhs_value;
				else if (node->tag == AstTag::OpDiv)
					rst_value = lhs_value / rhs_value;
				else
					ASSERT_UNREACHABLE;
				
				value_set(&rst.success, range::from_object_bytes_mut(&rst_value));
			}
			else
			{
				const f64 lhs_value = *value_as<f64>(lhs_casted);

				const f64 rhs_value = *value_as<f64>(rhs_casted);

				f64 rst_value;

				if (node->tag == AstTag::OpAdd)
					rst_value = lhs_value + rhs_value;
				else if (node->tag == AstTag::OpSub)
					rst_value = lhs_value - rhs_value;
				else if (node->tag == AstTag::OpMul)
					rst_value = lhs_value * rhs_value;
				else if (node->tag == AstTag::OpDiv)
					rst_value = lhs_value / rhs_value;
				else
					ASSERT_UNREACHABLE;

				value_set(&rst.success, range::from_object_bytes_mut(&rst_value));
			}
		}
		else if (unified_type_tag == TypeTag::CompFloat)
		{
			if (node->tag == AstTag::OpMod)
				source_error(interp->errors, source_id_of(interp->asts, node), "Operator `%s` does not support floating-point operands.\n", tag_name(node->tag));

			const CompFloatValue lhs_value = *value_as<CompFloatValue>(lhs_casted);

			const CompFloatValue rhs_value = *value_as<CompFloatValue>(rhs_casted);
			
			CompFloatValue rst_value;

			if (node->tag == AstTag::OpAdd)
				rst_value = comp_float_add(lhs_value, rhs_value);
			else if (node->tag == AstTag::OpSub)
				rst_value = comp_float_sub(lhs_value, rhs_value);
			else if (node->tag == AstTag::OpMul)
				rst_value = comp_float_mul(lhs_value, rhs_value);
			else if (node->tag == AstTag::OpDiv)
				rst_value = comp_float_div(lhs_value, rhs_value);
			else
				ASSERT_UNREACHABLE;

			const TypeTag rst_type_tag = type_tag_from_id(interp->types, rst.success.type_id);

			if (rst_type_tag == TypeTag::CompFloat)
			{
				value_set(&rst.success, range::from_object_bytes_mut(&rst_value));
			}
			else
			{
				ASSERT_OR_IGNORE(rst_type_tag == TypeTag::Float);

				convert(interp, node, &rst.success, make_value(range::from_object_bytes_mut(&rst_value), false, true, unified_type_id));
			}
		}
		else if (unified_type_tag == TypeTag::Integer)
		{
			const NumericType* const type = type_attachment_from_id<NumericType>(interp->types, unified_type_id);

			bool rst_ok;

			if (node->tag == AstTag::OpAdd)
				rst_ok = bitwise_add(type->bits, type->is_signed, rst.success.bytes, lhs_casted.bytes.immut(), rhs_casted.bytes.immut());
			else if (node->tag == AstTag::OpSub)
				rst_ok = bitwise_sub(type->bits, type->is_signed, rst.success.bytes, lhs_casted.bytes.immut(), rhs_casted.bytes.immut());
			else if (node->tag == AstTag::OpMul)
				rst_ok = bitwise_mul(type->bits, type->is_signed, rst.success.bytes, lhs_casted.bytes.immut(), rhs_casted.bytes.immut());
			else if (node->tag == AstTag::OpDiv)
				rst_ok = bitwise_div(type->bits, type->is_signed, rst.success.bytes, lhs_casted.bytes.immut(), rhs_casted.bytes.immut());
			else if (node->tag == AstTag::OpMod)
				rst_ok = bitwise_mod(type->bits, type->is_signed, rst.success.bytes, lhs_casted.bytes.immut(), rhs_casted.bytes.immut());
			else
				ASSERT_UNREACHABLE;

			if (!rst_ok)
				source_error(interp->errors, source_id_of(interp->asts, node), "Overflow encountered while evaluating operator `%s`.\n", tag_name(node->tag));
		}
		else if (unified_type_tag == TypeTag::CompInteger)
		{
			const CompIntegerValue lhs_value = *value_as<CompIntegerValue>(lhs_casted);

			const CompIntegerValue rhs_value = *value_as<CompIntegerValue>(rhs_casted);
			CompIntegerValue rst_value;
			
			if (node->tag == AstTag::OpAdd)
			{
				rst_value = comp_integer_add(lhs_value, rhs_value);
			}
			else if (node->tag == AstTag::OpSub)
			{
				rst_value = comp_integer_sub(lhs_value, rhs_value);
			}
			else if (node->tag == AstTag::OpMul)
			{
				rst_value = comp_integer_mul(lhs_value, rhs_value);
			}
			else if (node->tag == AstTag::OpDiv)
			{
				if (!comp_integer_div(lhs_value, rhs_value, &rst_value))
					source_error(interp->errors, source_id_of(interp->asts, node), "Tried dividing by 0.\n");
			}
			else
			{
				ASSERT_UNREACHABLE;
			}

			const TypeTag rst_type_tag = type_tag_from_id(interp->types, rst.success.type_id);

			if (rst_type_tag == TypeTag::CompInteger)
			{
				value_set(&rst.success, range::from_object_bytes_mut(&rst_value));
			}
			else
			{
				ASSERT_OR_IGNORE(rst_type_tag == TypeTag::Integer);

				convert(interp, node, &rst.success, make_value(range::from_object_bytes_mut(&rst_value), false, true, unified_type_id));
			}
		}

		if (spec.dst.begin() == nullptr)
			rst.success.bytes = stack_copy_down(interp, mark, rst.success.bytes);
		else
			stack_shrink(interp, mark);

		return rst;
	}

	case AstTag::OpBitAnd:
	{
		const u32 mark = stack_mark(interp);

		EvalValue lhs_casted;

		EvalValue rhs_casted;

		TypeId common_type_id;

		Arec* unbound;

		if (!evaluate_commonly_typed_binary_expr(interp, node, &lhs_casted, &rhs_casted, &common_type_id, &unbound))
		{
			stack_shrink(interp, mark);

			return eval_unbound(unbound);
		}

		const TypeTag common_type_tag = type_tag_from_id(interp->types, common_type_id);

		EvalRst rst;

		if (common_type_tag == TypeTag::CompInteger)
		{
			rst = fill_spec_sized(interp, spec, node, false, true, common_type_id, sizeof(CompIntegerValue), alignof(CompIntegerValue));

			CompIntegerValue value;

			if (!comp_integer_bit_and(*value_as<CompIntegerValue>(lhs_casted), *value_as<CompIntegerValue>(rhs_casted), &value))
				source_error(interp->errors, source_id_of(interp->asts, node), "Cannot apply operator `&` to negative compile-time integer value.\n");

			value_set(&rst.success, range::from_object_bytes_mut(&value));
		}
		else if (common_type_tag == TypeTag::Boolean)
		{
			rst = fill_spec_sized(interp, spec, node, false, true, common_type_id, sizeof(bool), alignof(bool));

			bool value = *value_as<bool>(lhs_casted) && *value_as<bool>(rhs_casted);

			value_set(&rst.success, range::from_object_bytes_mut(&value));
		}
		else if (common_type_tag == TypeTag::Integer)
		{
			rst = fill_spec(interp, spec, node, false, true, common_type_id);

			for (u32 i = 0; i != rst.success.bytes.count(); ++i)
				rst.success.bytes[i] = lhs_casted.bytes[i] & rhs_casted.bytes[i];
		}
		else
		{
			source_error(interp->errors, source_id_of(interp->asts, node), "Operator `&` only works on integer or boolean typed values.\n");
		}

		rst.success.bytes = stack_copy_down(interp, mark, rst.success.bytes);

		return rst;
	}

	case AstTag::OpBitOr:
	{
		const u32 mark = stack_mark(interp);

		EvalValue lhs_casted;

		EvalValue rhs_casted;

		TypeId common_type_id;

		Arec* unbound;

		if (!evaluate_commonly_typed_binary_expr(interp, node, &lhs_casted, &rhs_casted, &common_type_id, &unbound))
		{
			stack_shrink(interp, mark);

			return eval_unbound(unbound);
		}

		const TypeTag common_type_tag = type_tag_from_id(interp->types, common_type_id);

		EvalRst rst;

		if (common_type_tag == TypeTag::CompInteger)
		{
			rst = fill_spec_sized(interp, spec, node, false, true, common_type_id, sizeof(CompIntegerValue), alignof(CompIntegerValue));

			CompIntegerValue value;

			if (!comp_integer_bit_or(*value_as<CompIntegerValue>(lhs_casted), *value_as<CompIntegerValue>(rhs_casted), &value))
				source_error(interp->errors, source_id_of(interp->asts, node), "Cannot apply operator `|` to negative compile-time integer value.\n");

			value_set(&rst.success, range::from_object_bytes_mut(&value));
		}
		else if (common_type_tag == TypeTag::Boolean)
		{
			rst = fill_spec_sized(interp, spec, node, false, true, common_type_id, sizeof(bool), alignof(bool));

			bool value = *value_as<bool>(lhs_casted) || *value_as<bool>(rhs_casted);

			value_set(&rst.success, range::from_object_bytes_mut(&value));
		}
		else if (common_type_tag == TypeTag::Integer)
		{
			rst = fill_spec(interp, spec, node, false, true, common_type_id);

			for (u32 i = 0; i != rst.success.bytes.count(); ++i)
				rst.success.bytes[i] = lhs_casted.bytes[i] | rhs_casted.bytes[i];
		}
		else
		{
			source_error(interp->errors, source_id_of(interp->asts, node), "Operator `|` only works on integer or boolean typed values.\n");
		}

		rst.success.bytes = stack_copy_down(interp, mark, rst.success.bytes);

		return rst;
	}

	case AstTag::OpBitXor:
	{
		const u32 mark = stack_mark(interp);

		EvalValue lhs_casted;

		EvalValue rhs_casted;

		TypeId common_type_id;

		Arec* unbound;

		if (!evaluate_commonly_typed_binary_expr(interp, node, &lhs_casted, &rhs_casted, &common_type_id, &unbound))
		{
			stack_shrink(interp, mark);

			return eval_unbound(unbound);
		}

		const TypeTag common_type_tag = type_tag_from_id(interp->types, common_type_id);

		EvalRst rst;

		if (common_type_tag == TypeTag::CompInteger)
		{
			rst = fill_spec_sized(interp, spec, node, false, true, common_type_id, sizeof(CompIntegerValue), alignof(CompIntegerValue));

			CompIntegerValue value;

			if (!comp_integer_bit_xor(*value_as<CompIntegerValue>(lhs_casted), *value_as<CompIntegerValue>(rhs_casted), &value))
				source_error(interp->errors, source_id_of(interp->asts, node), "Cannot apply operator `^` to negative compile-time integer value.\n");

			value_set(&rst.success, range::from_object_bytes_mut(&value));
		}
		else if (common_type_tag == TypeTag::Boolean)
		{
			rst = fill_spec_sized(interp, spec, node, false, true, common_type_id, sizeof(bool), alignof(bool));

			bool value = *value_as<bool>(lhs_casted) ^ *value_as<bool>(rhs_casted);

			value_set(&rst.success, range::from_object_bytes_mut(&value));
		}
		else if (common_type_tag == TypeTag::Integer)
		{
			rst = fill_spec(interp, spec, node, false, true, common_type_id);

			for (u32 i = 0; i != rst.success.bytes.count(); ++i)
				rst.success.bytes[i] = lhs_casted.bytes[i] ^ rhs_casted.bytes[i];
		}
		else
		{
			source_error(interp->errors, source_id_of(interp->asts, node), "Operator `^` only works on integer or boolean typed values.\n");
		}

		rst.success.bytes = stack_copy_down(interp, mark, rst.success.bytes);

		return rst;
	}

	case AstTag::OpShiftL:
	case AstTag::OpShiftR:
	{
		const u32 mark = stack_mark(interp);

		AstNode* const lhs = first_child_of(node);

		EvalRst lhs_rst = evaluate(interp, lhs, spec);

		AstNode* const rhs = next_sibling_of(lhs);

		EvalRst rhs_rst = evaluate(interp, rhs, EvalSpec{
			ValueKind::Value
		});

		Arec* const unbound = check_binary_expr_for_unbound(interp, lhs, rhs, &lhs_rst, &rhs_rst);

		if (unbound != nullptr)
		{
			stack_shrink(interp, mark);

			return eval_unbound(unbound);
		}

		const TypeTag lhs_type_tag = type_tag_from_id(interp->types, lhs_rst.success.type_id);

		u16 max_shift_log2_ceil;

		NumericType lhs_type;

		if (lhs_type_tag == TypeTag::CompInteger)
		{
			max_shift_log2_ceil = 16;
		}
		else if (lhs_type_tag == TypeTag::Integer)
		{
			lhs_type = *type_attachment_from_id<NumericType>(interp->types, lhs_rst.success.type_id);

			max_shift_log2_ceil = lhs_type.bits;
		}
		else
		{
			source_error(interp->errors, source_id_of(interp->asts, rhs), "Right-hand-side of `%s` must be of integer type.\n", node->tag == AstTag::OpShiftL ? "<<" : ">>");
		}

		u16 shift;

		const ShiftAmountResult shift_rst = shift_amount(interp, rhs_rst.success.bytes.immut(), rhs_rst.success.type_id, max_shift_log2_ceil, &shift);

		if (shift_rst == ShiftAmountResult::Negative)
			source_error(interp->errors, source_id_of(interp->asts, rhs), "Right-hand-side of `%s` must not be negative.\n", node->tag == AstTag::OpShiftL ? "<<" : ">>");
		else if (shift_rst == ShiftAmountResult::TooLarge)
			source_error(interp->errors, source_id_of(interp->asts, rhs), "%s-shifting by 2^16 or more is not supported.\n", node->tag == AstTag::OpShiftL ? "Left" : "Right");
		else if (shift_rst == ShiftAmountResult::UnsupportedType)
			source_error(interp->errors, source_id_of(interp->asts, rhs), "Right-hand-side of `%s` must be of integer type.\n", node->tag == AstTag::OpShiftL ? "<<" : ">>");

		ASSERT_OR_IGNORE(shift_rst == ShiftAmountResult::InRange);

		EvalRst rst = fill_spec(interp, spec, node, false, true, lhs_rst.success.type_id);

		if (lhs_type_tag == TypeTag::CompInteger)
		{
			const CompIntegerValue lhs_value = *value_as<CompIntegerValue>(lhs_rst.success);

			const CompIntegerValue rhs_value = comp_integer_from_u64(shift);

			CompIntegerValue shifted;

			if (node->tag == AstTag::OpShiftL)
			{
				if (!comp_integer_shift_left(lhs_value, rhs_value, &shifted))
					source_error(interp->errors, source_id_of(interp->asts, rhs), "Right-hand-side of `%s` must not be negative.\n", node->tag == AstTag::OpShiftL ? "<<" : ">>");
			}
			else
			{
				ASSERT_OR_IGNORE(node->tag == AstTag::OpShiftR);

				if (!comp_integer_shift_right(lhs_value, rhs_value, &shifted))
					source_error(interp->errors, source_id_of(interp->asts, rhs), "Right-hand-side of `%s` must not be negative.\n", node->tag == AstTag::OpShiftL ? "<<" : ">>");
			}

			const TypeTag rst_type_tag = type_tag_from_id(interp->types, rst.success.type_id);

			if (rst_type_tag == TypeTag::CompInteger)
			{
				value_set(&rst.success, range::from_object_bytes_mut(&shifted));
			}
			else
			{
				ASSERT_OR_IGNORE(rst_type_tag == TypeTag::Integer);

				convert(interp, node, &rst.success, make_value(range::from_object_bytes_mut(&shifted), false, true, lhs_rst.success.type_id));
			}
		}
		else
		{
			ASSERT_OR_IGNORE(lhs_type_tag == TypeTag::Integer);

			if (node->tag == AstTag::OpShiftL)
			{
				bitwise_shift_left(lhs_type.bits, rst.success.bytes, lhs_rst.success.bytes.immut(), shift);
			}
			else
			{
				ASSERT_OR_IGNORE(node->tag == AstTag::OpShiftR);

				bitwise_shift_right(lhs_type.bits, rst.success.bytes, lhs_rst.success.bytes.immut(), shift, lhs_type.is_signed);
			}
		}

		rst.success.bytes = stack_copy_down(interp, mark, rst.success.bytes);

		return rst;
	}

	case AstTag::OpLogAnd:
	case AstTag::OpLogOr:
	{
		const TypeId bool_type_id = type_create_simple(interp->types, TypeTag::Boolean);

		EvalRst rst = fill_spec_sized(interp, spec, node, false, true, bool_type_id, sizeof(bool), alignof(bool));

		const u32 mark = stack_mark(interp);

		AstNode* const lhs = first_child_of(node);

		bool lhs_value;

		const EvalRst lhs_rst = evaluate(interp, lhs, EvalSpec{
			ValueKind::Value,
			range::from_object_bytes_mut(&lhs_value),
			bool_type_id
		});

		// Since we might short-circuit here we can't look at `rhs` to see
		// whether it should be put into a partial value, so just return right
		// away.
		if (lhs_rst.tag == EvalTag::Unbound)
		{
			stack_shrink(interp, mark);

			return eval_unbound(lhs_rst.unbound);
		}

		// Short-circuit. If we are performing and `and`, short-circuit iff lhs
		// is `false`. Otherwise (i.e. if we are performing an `or`)
		// short-circuit iff lhs is `true`. 
		if (lhs_value != (node->tag == AstTag::OpLogAnd))
		{
			value_set(&rst.success, lhs_rst.success.bytes);

			stack_shrink(interp, mark);

			return rst;
		}

		AstNode* const rhs = next_sibling_of(lhs);

		bool rhs_value;

		const EvalRst rhs_rst = evaluate(interp, rhs, EvalSpec{
			ValueKind::Value,
			range::from_object_bytes_mut(&rhs_value),
			bool_type_id
		});

		if (rhs_rst.tag == EvalTag::Unbound)
		{
			add_partial_value_to_builder_sized(interp, lhs, bool_type_id, lhs_rst.success.bytes.immut(), sizeof(bool), alignof(bool));

			stack_shrink(interp, mark);

			return eval_unbound(rhs_rst.unbound);
		}

		// Since we're here, we know that `lhs` did not short-circuit, so it
		// has no further bearing on the result. Thus we can just use `rhs` as
		// the result of the operation.
		value_set(&rst.success, rhs_rst.success.bytes);

		stack_shrink(interp, mark);

		return rst;
	}

	case AstTag::Member:
	{
		const u32 mark = stack_mark(interp);

		AstNode* const lhs = first_child_of(node);

		const AstMemberData attach = *attachment_of<AstMemberData>(node);

		EvalRst lhs_rst = evaluate(interp, lhs, EvalSpec{ ValueKind::Location });

		if (lhs_rst.tag == EvalTag::Unbound)
		{
			stack_shrink(interp, mark);

			return eval_unbound(lhs_rst.unbound);
		}

		const TypeTag lhs_type_tag = type_tag_from_id(interp->types, lhs_rst.success.type_id);

		EvalRst rst;

		if (lhs_type_tag == TypeTag::Composite)
		{
			const Member* member;

			if (!type_member_by_name(interp->types, lhs_rst.success.type_id, attach.identifier_id, &member))
			{
				const Range<char8> name = identifier_name_from_id(interp->identifiers, attach.identifier_id);

				source_error(interp->errors, source_id_of(interp->asts, node), "Left-hand-side of `.` has no member named `%.*s`.\n", static_cast<s32>(name.count()), name.begin());
			}

			if (member->is_global)
				rst = evaluate_global_member(interp, node, spec, lhs_rst.success.type_id, member);
			else
				rst = evaluate_local_member(interp, node, spec, lhs_rst.success.type_id, member, lhs_rst.success.bytes);
		}
		else if (lhs_type_tag == TypeTag::Type)
		{
			const TypeId type_id = *value_as<TypeId>(lhs_rst.success);

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

			rst = evaluate_global_member(interp, node, spec, type_id, member);
		}
		else
		{
			source_error(interp->errors, source_id_of(interp->asts, node), "Left-hand-side of `.` must be either a composite value or a composite type.\n");
		}

		stack_shrink(interp, mark);

		return rst;
	}

	case AstTag::OpCmpLT:
	case AstTag::OpCmpGT:
	case AstTag::OpCmpLE:
	case AstTag::OpCmpGE:
	case AstTag::OpCmpNE:
	case AstTag::OpCmpEQ:
	{
		const TypeId bool_type_id = type_create_simple(interp->types, TypeTag::Boolean);

		EvalRst rst = fill_spec_sized(interp, spec, node, false, true, bool_type_id, sizeof(bool), alignof(bool));

		const u32 mark = stack_mark(interp);

		EvalValue lhs_casted;

		EvalValue rhs_casted;

		TypeId common_type_id;

		Arec* unbound;

		if (!evaluate_commonly_typed_binary_expr(interp, node, &lhs_casted, &rhs_casted, &common_type_id, &unbound))
		{
			stack_shrink(interp, mark);

			return eval_unbound(unbound);
		}

		const CompareResult result = compare(interp, common_type_id, lhs_casted.bytes.immut(), rhs_casted.bytes.immut(), node);

		if (result.tag == CompareTag::INVALID)
			source_error(interp->errors, source_id_of(interp->asts, node), "Cannot compare values of the given type.\n");
		else if (result.tag == CompareTag::Equality && node->tag != AstTag::OpCmpNE && node->tag != AstTag::OpCmpEQ)
			source_error(interp->errors, source_id_of(interp->asts, node), "Cannot order values of the given type.\n");

		bool bool_result;

		if (node->tag == AstTag::OpCmpLT)
			bool_result = result.ordering == WeakCompareOrdering::LessThan;
		else if (node->tag == AstTag::OpCmpGT)
			bool_result = result.ordering == WeakCompareOrdering::GreaterThan;
		else if (node->tag == AstTag::OpCmpLE)
			bool_result = result.ordering == WeakCompareOrdering::LessThan || result.ordering == WeakCompareOrdering::Equal;
		else if (node->tag == AstTag::OpCmpGE)
			bool_result = result.ordering == WeakCompareOrdering::GreaterThan || result.ordering == WeakCompareOrdering::Equal;
		else if (node->tag == AstTag::OpCmpNE)
			bool_result = result.ordering != WeakCompareOrdering::Equal && result.ordering != WeakCompareOrdering::Unordered;
		else if (node->tag == AstTag::OpCmpEQ)
			bool_result = result.ordering == WeakCompareOrdering::Equal;

		// No need for implicit conversion here, as bool is not convertible to
		// anything else.
		value_set(&rst.success, range::from_object_bytes_mut(&bool_result));

		stack_shrink(interp, mark);

		return rst;
	}

	case AstTag::OpSet:
	{
		const u32 mark = stack_mark(interp);

		AstNode* const lhs = first_child_of(node);

		const EvalRst lhs_rst = evaluate(interp, lhs, EvalSpec{
			ValueKind::Location
		});

		if (lhs_rst.tag == EvalTag::Unbound)
			source_error(interp->errors, source_id_of(interp->asts, node), "Cannot use `=` operator in unbound context.\n");

		if (!lhs_rst.success.is_mut)
			source_error(interp->errors, source_id_of(interp->asts, node), "Left-hand-side of `=` operator must be mutable.\n");

		AstNode* const rhs = next_sibling_of(lhs);

		const EvalRst rhs_rst = evaluate(interp, rhs, EvalSpec{
			ValueKind::Value,
			lhs_rst.success.bytes,
			lhs_rst.success.type_id
		});

		if (rhs_rst.tag == EvalTag::Unbound)
			source_error(interp->errors, source_id_of(interp->asts, node), "Cannot use `=` operator in unbound context.\n");

		stack_shrink(interp, mark);

		return fill_spec_sized(interp, spec, node, false, true, type_create_simple(interp->types, TypeTag::Void), 0, 1);
	}

	case AstTag::OpTypeArray:
	{
		const TypeId type_type_id = type_create_simple(interp->types, TypeTag::Type);

		EvalRst rst = fill_spec_sized(interp, spec, node, false, true, type_type_id, sizeof(TypeId), alignof(TypeId));

		const u32 mark = stack_mark(interp);

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
				const CompIntegerValue count_value = *value_as<CompIntegerValue>(count_rst.success);

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

		TypeId elem_type_id;

		const EvalRst elem_type_rst = evaluate(interp, elem_type, EvalSpec{
			ValueKind::Value,
			range::from_object_bytes_mut(&elem_type_id),
			type_type_id
		});

		Arec* const unbound = check_binary_expr_for_unbound(interp, count, elem_type, &count_rst, &elem_type_rst);

		if (unbound != nullptr)
		{
			stack_shrink(interp, mark);

			return eval_unbound(unbound);
		}

		TypeId array_type_id = type_create_array(interp->types, TypeTag::Array, ArrayType{ count_u64, elem_type_id });

		value_set(&rst.success, range::from_object_bytes_mut(&array_type_id));

		stack_shrink(interp, mark);

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

		bool elem_is_mut = false;

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

				elem_is_mut = arrayish_rst.success.is_mut;

				arrayish_size = array_type->element_count * elem_metrics.size;

				arrayish_begin = arrayish_rst.success.bytes.begin();
			}
			else if (arrayish_type_tag == TypeTag::Slice)
			{
				const ReferenceType* const slice_type = type_attachment_from_id<ReferenceType>(interp->types, arrayish_rst.success.type_id);

				MutRange<byte> slice = *value_as<MutRange<byte>>(arrayish_rst.success);

				elem_metrics = type_metrics_from_id(interp->types, slice_type->referenced_type_id);

				ASSERT_OR_IGNORE(slice.count() % elem_metrics.stride == 0);

				elem_type_id = slice_type->referenced_type_id;

				elem_is_mut = slice_type->is_mut;

				arrayish_size = slice.count();

				arrayish_begin = slice.begin();
			}
			else if (arrayish_type_tag == TypeTag::Ptr)
			{
				const ReferenceType* const ptr_type = type_attachment_from_id<ReferenceType>(interp->types, arrayish_rst.success.type_id);

				if (!ptr_type->is_multi || ptr_type->is_opt)
					source_error(interp->errors, source_id_of(interp->asts, arrayish), "Left-hand-side of index operator must have array, slice, or multi-pointer type.\n");

				void* const ptr = *value_as<void*>(arrayish_rst.success);

				ASSERT_OR_IGNORE(ptr != nullptr);

				elem_metrics = type_metrics_from_id(interp->types, ptr_type->referenced_type_id);

				elem_type_id = ptr_type->referenced_type_id;

				elem_is_mut = ptr_type->is_mut;

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
				const CompIntegerValue index_value = *value_as<CompIntegerValue>(index_rst.success);

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

		if (arrayish_rst.tag == EvalTag::Unbound && index_rst.tag == EvalTag::Unbound)
		{
			return eval_unbound(arrayish_rst.unbound < index_rst.unbound ? arrayish_rst.unbound : index_rst.unbound);
		}
		else if (arrayish_rst.tag == EvalTag::Unbound)
		{
			add_partial_value_to_builder(interp, index, index_rst.success.type_id, index_rst.success.bytes.immut());

			return eval_unbound(arrayish_rst.unbound);
		}
		else if (index_rst.tag == EvalTag::Unbound)
		{
			add_partial_value_to_builder(interp, arrayish, arrayish_rst.success.type_id, arrayish_rst.success.bytes.immut());

			return eval_unbound(index_rst.unbound);
		}
		else
		{
			const u64 offset = index_u64 * elem_metrics.stride;

			if (offset + elem_metrics.size > arrayish_size)
				source_error(interp->errors, source_id_of(interp->asts, node), "Index %" PRIu64 " exceeds element count of %" PRIu64 ".\n", index_u64, arrayish_size / elem_metrics.stride);

			EvalRst rst;

			if (rst.success.is_location && !arrayish_rst.success.is_location)
				source_error(interp->errors, source_id_of(interp->asts, node), "Cannot use index operator with non-location left-hand-side as a location.\n");

			rst = fill_spec_sized(interp, spec, node, arrayish_rst.success.is_location, elem_is_mut || !arrayish_rst.success.is_location, elem_type_id, elem_metrics.size, elem_metrics.align);

			value_set(&rst.success, MutRange<byte>{ arrayish_begin + offset, elem_metrics.size });

			return rst;
		}
	}

	case AstTag::CompositeInitializer:
	case AstTag::Wildcard:
	case AstTag::Where:
	case AstTag::Expects:
	case AstTag::Ensures:
	case AstTag::ForEach:
	case AstTag::Switch:
	case AstTag::Trait:
	case AstTag::Impl:
	case AstTag::Catch:
	case AstTag::LitChar:
	case AstTag::Return:
	case AstTag::Leave:
	case AstTag::Yield:
	case AstTag::UOpTypeTailArray:
	case AstTag::UOpEval:
	case AstTag::UOpTry:
	case AstTag::UOpDefer:
	case AstTag::UOpDistinct:
	case AstTag::UOpBitNot:
	case AstTag::UOpNegate:
	case AstTag::UOpPos:
	case AstTag::OpAddTC:
	case AstTag::OpSubTC:
	case AstTag::OpMulTC:
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
	case AstTag::Case:
	case AstTag::ParameterList:
	case AstTag::UOpImpliedMember:
	case AstTag::MAX:
		; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

static TypeId typeinfer(Interpreter* interp, AstNode* node) noexcept
{
	switch (node->tag)
	{
	case AstTag::Builtin:
	{
		const u8 ordinal = static_cast<u8>(node->flags);

		ASSERT_OR_IGNORE(ordinal < array_count(interp->builtin_type_ids));

		return interp->builtin_type_ids[ordinal];
	}

	case AstTag::Block:
	{
		if (!has_children(node))
			return type_create_simple(interp->types, TypeTag::Void);

		panic("typeinfer(%s) for non-empty blocks not yet implemented.\n", tag_name(node->tag));
	}

	case AstTag::Identifier:
	{
		AstIdentifierData attach = *attachment_of<AstIdentifierData>(node);

		IdentifierInfo info = lookup_identifier(interp, attach.identifier_id, attach.binding);

		if (info.tag != EvalTag::Success)
		{
			const Range<char8> name = identifier_name_from_id(interp->identifiers, attach.identifier_id);

			source_error(interp->errors, source_id_of(interp->asts, node), "Identifier '%.*s' is not bound yet, so its type cannot be inferred.\n", static_cast<s32>(name.count()), name.begin());
		}

		const TypeTag type_tag = type_tag_from_id(interp->types, info.success.type_id);

		if (type_tag == TypeTag::TypeInfo)
			return *value_as<TypeId>(info.success);

		return info.success.type_id;
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
	case AstTag::Unreachable:
	case AstTag::Undefined:
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
	case AstTag::UOpTypeVarArgs:
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

static void type_from_file_ast(Interpreter* interp, AstNode* file, SourceId file_type_source_id, TypeId* out) noexcept
{
	ASSERT_OR_IGNORE(file->tag == AstTag::File);

	resolve_names(interp->lex, file);

	if (interp->ast_log_file.m_rep != nullptr && (file_type_source_id != SourceId::INVALID || interp->log_prelude))
	{
		const Range<char8> filepath = file_type_source_id == SourceId::INVALID
			?	range::from_literal_string("<prelude>")
			: source_file_path_from_source_id(interp->reader, file_type_source_id);

		diag::print_ast(interp->ast_log_file, interp->identifiers, file, filepath);
	}

	// Note that `interp->prelude_type_id` is `INVALID_TYPE_ID` if we are
	// called from `init_prelude_type`, so the prelude itself has no lexical
	// parent.
	const TypeId file_type_id = type_create_composite(interp->types, interp->prelude_type_id, TypeDisposition::User, file_type_source_id, 0, false);

	const ArecId file_arec_id = arec_push(interp, file_type_id, 0, 1, ArecId::INVALID, ArecKind::Normal, file_type_id, file_type_source_id, ArecId::INVALID);

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

	*out = file_type_id;

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
			member->has_pending_type,
			member->has_pending_value,
			value_rst.success.type_id,
			member_value_id
		});
	}

	arec_pop(interp, file_arec_id);
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
	const Member* member;

	if (!type_member_by_name(interp->types, arec->type_id, name, &member))
		ASSERT_UNREACHABLE;

	ASSERT_OR_IGNORE(!member->is_global && !member->has_pending_type);

	const TypeMetrics metrics = type_metrics_from_id(interp->types, member->type.complete);

	MutRange<byte> value = arec_attach(interp, arec).mut_subrange(member->offset, metrics.size);

	ASSERT_OR_IGNORE(value.count() == sizeof(T));

	return *reinterpret_cast<T*>(value.begin());
}

static void builtin_integer(Interpreter* interp, Arec* arec, MutRange<byte> into) noexcept
{
	const u8 bits = get_builtin_arg<u8>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("bits")));

	const bool is_signed = get_builtin_arg<bool>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("is_signed")));

	const TypeId rst = type_create_numeric(interp->types, TypeTag::Integer, NumericType{ bits, is_signed });

	range::mem_copy(into, range::from_object_bytes(&rst));
}

static void builtin_float(Interpreter* interp, Arec* arec, MutRange<byte> into) noexcept
{
	const u8 bits = get_builtin_arg<u8>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("bits")));

	const TypeId rst = type_create_numeric(interp->types, TypeTag::Float, NumericType{ bits, true });

	range::mem_copy(into, range::from_object_bytes(&rst));
}

static void builtin_type(Interpreter* interp, [[maybe_unused]] Arec* arec, MutRange<byte> into) noexcept
{
	const TypeId rst = type_create_simple(interp->types, TypeTag::Type);

	range::mem_copy(into, range::from_object_bytes(&rst));
}

static void builtin_definition(Interpreter* interp, [[maybe_unused]] Arec* arec, MutRange<byte> into) noexcept
{
	const TypeId rst = type_create_simple(interp->types, TypeTag::Definition);

	range::mem_copy(into, range::from_object_bytes(&rst));
}

static void builtin_type_info(Interpreter* interp, [[maybe_unused]] Arec* arec, MutRange<byte> into) noexcept
{
	const TypeId rst = type_create_simple(interp->types, TypeTag::TypeInfo);

	range::mem_copy(into, range::from_object_bytes(&rst));
}

static void builtin_typeof(Interpreter* interp, Arec* arec, MutRange<byte> into) noexcept
{
	const TypeId rst = get_builtin_arg<TypeId>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("arg")));

	range::mem_copy(into, range::from_object_bytes(&rst));
}

static void builtin_returntypeof(Interpreter* interp, Arec* arec, MutRange<byte> into) noexcept
{
	const TypeId arg = get_builtin_arg<TypeId>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("arg")));

	ASSERT_OR_IGNORE(type_tag_from_id(interp->types, arg) == TypeTag::Func || type_tag_from_id(interp->types, arg) == TypeTag::Builtin);

	const SignatureType* const signature_type = type_attachment_from_id<SignatureType>(interp->types, arg);

	if (signature_type->return_type_is_unbound)
		TODO("Implement `_returntypeof` for unbound return types");

	range::mem_copy(into, range::from_object_bytes(&signature_type->return_type.complete));
}

static void builtin_sizeof(Interpreter* interp, Arec* arec, MutRange<byte> into) noexcept
{
	const TypeId arg = get_builtin_arg<TypeId>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("arg")));

	const TypeMetrics metrics = type_metrics_from_id(interp->types, arg);

	const CompIntegerValue rst = comp_integer_from_u64(metrics.size);

	range::mem_copy(into, range::from_object_bytes(&rst));
}

static void builtin_alignof(Interpreter* interp, Arec* arec, MutRange<byte> into) noexcept
{
	const TypeId arg = get_builtin_arg<TypeId>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("arg")));

	const TypeMetrics metrics = type_metrics_from_id(interp->types, arg);

	const CompIntegerValue rst = comp_integer_from_u64(metrics.align);

	range::mem_copy(into, range::from_object_bytes(&rst));
}

static void builtin_strideof(Interpreter* interp, Arec* arec, MutRange<byte> into) noexcept
{
	const TypeId arg = get_builtin_arg<TypeId>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("arg")));

	const TypeMetrics metrics = type_metrics_from_id(interp->types, arg);

	const CompIntegerValue rst = comp_integer_from_u64(metrics.align);

	range::mem_copy(into, range::from_object_bytes(&rst));
}

static void builtin_offsetof(Interpreter* interp, Arec* arec, MutRange<byte> into) noexcept
{
	(void) interp;

	(void) arec;

	(void) into;

	TODO("Implement.");
}

static void builtin_nameof(Interpreter* interp, Arec* arec, MutRange<byte> into) noexcept
{
	(void) interp;

	(void) arec;

	(void) into;

	TODO("Implement.");
}

static void builtin_import(Interpreter* interp, Arec* arec, MutRange<byte> into) noexcept
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
			source_error(interp->errors, arec_from_id(interp, arec->caller_arec_id)->source_id, "Failed to get parent directory from `from` source file (0x%X).\n", minos::last_error());

		const u32 absolute_path_chars = minos::path_to_absolute_relative_to(path, Range{ path_base_parent_buf , path_base_parent_chars }, MutRange{ absolute_path_buf });

		if (absolute_path_chars == 0 || absolute_path_chars > array_count(absolute_path_buf))
			source_error(interp->errors, arec_from_id(interp, arec->caller_arec_id)->source_id, "Failed to make `path` %.*s absolute relative to `from` %.*s (0x%X).\n", static_cast<s32>(path.count()), path.begin(), static_cast<s32>(path_base.count()), path_base.begin(), minos::last_error());

		absolute_path = Range{ absolute_path_buf, absolute_path_chars };
	}
	else
	{
		// This makes the prelude import of the configured standard library
		// (which is an absolute path) work.
		absolute_path = path;
	}

	const TypeId rst = import_file(interp, absolute_path, is_std);

	range::mem_copy(into, range::from_object_bytes(&rst));
}

static void builtin_create_type_builder(Interpreter* interp, Arec* arec, MutRange<byte> into) noexcept
{
	const SourceId source_id = get_builtin_arg<SourceId>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("source_id")));

	const TypeId rst = type_create_composite(interp->types, TypeId::INVALID, TypeDisposition::User, source_id, 0, false);

	range::mem_copy(into, range::from_object_bytes(&rst));
}

static void builtin_add_type_member(Interpreter* interp, Arec* arec, [[maybe_unused]] MutRange<byte> into) noexcept
{
	const TypeId builder = get_builtin_arg<TypeId>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("builder")));

	const Definition definition = get_builtin_arg<Definition>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("definition")));

	const s64 offset = get_builtin_arg<s64>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("offset")));

	Member member{};
	member.name = definition.name;
	member.type = definition.type;
	member.value = definition.value;
	member.is_global = definition.is_global;
	member.is_pub = definition.is_pub;
	member.is_mut = definition.is_mut;
	member.is_param = false;
	member.has_pending_type = definition.has_pending_type;
	member.has_pending_value = definition.has_pending_value;
	member.is_comptime_known = false;
	member.is_arg_independent = false;
	member.rank = 0;
	member.type_completion_arec_id = definition.type_completion_arec_id;
	member.value_completion_arec_id = definition.value_completion_arec_id;
	member.offset = offset;

	type_add_composite_member(interp->types, builder, member);
}

static void builtin_complete_type(Interpreter* interp, Arec* arec, MutRange<byte> into) noexcept
{
	const TypeId builder = get_builtin_arg<TypeId>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("builder")));

	const u64 size = get_builtin_arg<u64>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("size")));

	const u64 align = get_builtin_arg<u64>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("align")));

	const u64 stride = get_builtin_arg<u64>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("stride")));

	if (align > UINT32_MAX)
		source_error(interp->errors, arec_from_id(interp, arec->caller_arec_id)->source_id, "Alignment %" PRIu64 " passed to `_complete_type` must not exceed the maximum supported value of %u.\n", align, static_cast<u32>(1) << 31);

	if (align == 0)
		source_error(interp->errors, arec_from_id(interp, arec->caller_arec_id)->source_id, "Alignment %" PRIu64 " passed to `_complete_type` must not be 0.\n", align);

	if (!is_pow2(align))
		source_error(interp->errors, arec_from_id(interp, arec->caller_arec_id)->source_id, "Alignment %" PRIu64 " passed to `_complete_type` must be a power of two.\n", align);

	const TypeId direct_type_id = type_seal_composite(interp->types, builder, size, static_cast<u32>(align), stride);

	IncompleteMemberIterator it = incomplete_members_of(interp->types, direct_type_id);

	while (has_next(&it))
	{
		const Member* const member = next(&it);

		complete_member(interp, direct_type_id, member);
	}

	range::mem_copy(into, range::from_object_bytes(&direct_type_id));
}

static void builtin_source_id(Interpreter* interp, Arec* arec, MutRange<byte> into) noexcept
{
	const Arec* const calling_arec = arec_from_id(interp, arec->caller_arec_id);

	const SourceId rst = calling_arec->source_id;

	range::mem_copy(into, range::from_object_bytes(&rst));
}

static void builtin_caller_source_id(Interpreter* interp, Arec* arec, MutRange<byte> into) noexcept
{
	const Arec* const calling_arec = arec_from_id(interp, arec->caller_arec_id);

	const Arec* const caller_arec = arec_from_id(interp, calling_arec->caller_arec_id);

	const SourceId rst = caller_arec->source_id;

	range::mem_copy(into, range::from_object_bytes(&rst));
}

static void builtin_definition_typeof(Interpreter* interp, Arec* arec, MutRange<byte> into) noexcept
{
	// TODO: Change `Definition` into a reference type so that its members can
	//       be meaningfully set here, to avoid multiple evaluations.

	const Definition definition = get_builtin_arg<Definition>(interp, arec, id_from_identifier(interp->identifiers, range::from_literal_string("definition")));

	TypeId type_id;

	if (!definition.has_pending_type)
	{
		type_id = definition.type.complete;
	}
	else if (definition.type.pending != AstNodeId::INVALID)
	{
		ASSERT_OR_IGNORE(definition.type.pending != AstNodeId::INVALID && definition.type_completion_arec_id != ArecId::INVALID);

		AstNode* const type = ast_node_from_id(interp->asts, definition.type.pending);

		const ArecRestoreInfo restore = set_active_arec_id(interp, definition.type_completion_arec_id);

		const EvalRst rst = evaluate(interp, type, EvalSpec{
			ValueKind::Value,
			range::from_object_bytes_mut(&type_id),
			type_create_simple(interp->types, TypeTag::Type)
		});

		arec_restore(interp, restore);

		if (rst.tag == EvalTag::Unbound)
			source_error(interp->errors, arec_from_id(interp, arec->caller_arec_id)->source_id, "Cannot use `_definition_typeof` in unbound context.\n");
	}
	else
	{
		ASSERT_OR_IGNORE(definition.has_pending_value && definition.value.pending != AstNodeId::INVALID && definition.value_completion_arec_id != ArecId::INVALID);

		AstNode* const value = ast_node_from_id(interp->asts, definition.value.pending);

		const ArecRestoreInfo restore = set_active_arec_id(interp, definition.type_completion_arec_id);

		const EvalRst rst = evaluate(interp, value, EvalSpec{
			ValueKind::Value
		});

		arec_restore(interp, restore);

		if (rst.tag == EvalTag::Unbound)
			source_error(interp->errors, arec_from_id(interp, arec->caller_arec_id)->source_id, "Cannot use `_definition_typeof` in unbound context.\n");

		type_id = rst.success.type_id;
	}

	range::mem_copy(into, range::from_object_bytes(&type_id));
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

	const TypeId s64_type_id = type_create_numeric(interp->types, TypeTag::Integer, NumericType{ 64, true });

	const TypeId u64_type_id = type_create_numeric(interp->types, TypeTag::Integer, NumericType{ 64, false });

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

	interp->builtin_type_ids[static_cast<u8>(Builtin::Definition)] = make_func_type(interp->types, type_type_id);

	interp->builtin_type_ids[static_cast<u8>(Builtin::TypeInfo)] = make_func_type(interp->types, type_type_id);

	interp->builtin_type_ids[static_cast<u8>(Builtin::Typeof)] = make_func_type(interp->types, type_type_id,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("arg")), type_info_type_id, true }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::Returntypeof)] = make_func_type(interp->types, type_type_id,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("arg")), type_info_type_id, true }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::Sizeof)] = make_func_type(interp->types, comp_integer_type_id,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("arg")), type_type_id, true }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::Alignof)] = make_func_type(interp->types, comp_integer_type_id,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("arg")), type_type_id, true }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::Strideof)] = make_func_type(interp->types, comp_integer_type_id,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("arg")), type_type_id, true }
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

	interp->builtin_type_ids[static_cast<u8>(Builtin::CreateTypeBuilder)] = make_func_type(interp->types, type_builder_type_id,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("source_id")), u32_type_id, true }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::AddTypeMember)] = make_func_type(interp->types, void_type_id,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("builder")), type_builder_type_id, true },
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("definition")), definition_type_id, true },
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("offset")), s64_type_id, true }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::CompleteType)] = make_func_type(interp->types, type_type_id,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("builder")), type_builder_type_id, true },
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("size")), u64_type_id, true },
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("align")), u64_type_id, true },
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("stride")), u64_type_id, true }
	);

	interp->builtin_type_ids[static_cast<u8>(Builtin::SourceId)] = make_func_type(interp->types, u32_type_id);

	interp->builtin_type_ids[static_cast<u8>(Builtin::CallerSourceId)] = make_func_type(interp->types, u32_type_id);

	interp->builtin_type_ids[static_cast<u8>(Builtin::DefinitionTypeof)] = make_func_type(interp->types, type_type_id,
		BuiltinParamInfo{ id_from_identifier(interp->identifiers, range::from_literal_string("definition")), definition_type_id, true }
	);
}

static void init_builtin_values(Interpreter* interp) noexcept
{
	interp->builtin_values[static_cast<u8>(Builtin::Integer)] = &builtin_integer;
	interp->builtin_values[static_cast<u8>(Builtin::Float)] = &builtin_float;
	interp->builtin_values[static_cast<u8>(Builtin::Type)] = &builtin_type;
	interp->builtin_values[static_cast<u8>(Builtin::Definition)] = &builtin_definition;
	interp->builtin_values[static_cast<u8>(Builtin::TypeInfo)] = &builtin_type_info;
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
	interp->builtin_values[static_cast<u8>(Builtin::CallerSourceId)] = &builtin_caller_source_id;
	interp->builtin_values[static_cast<u8>(Builtin::DefinitionTypeof)] = &builtin_definition_typeof;
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
	const AstBuilderToken std_identifer = push_node(asts, AstBuilderToken::NO_CHILDREN, SourceId::INVALID, AstFlag::EMPTY, AstIdentifierData{ std_name, NameBinding{ 0, false, false, false, 0 } });

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

	std_prelude_use(asts, std_name, prelude_name, id_from_identifier(identifiers, range::from_literal_string("Definition")));

	std_prelude_use(asts, std_name, prelude_name, id_from_identifier(identifiers, range::from_literal_string("true")));

	std_prelude_use(asts, std_name, prelude_name, id_from_identifier(identifiers, range::from_literal_string("false")));

	std_prelude_use(asts, std_name, prelude_name, id_from_identifier(identifiers, range::from_literal_string("import")));

	std_prelude_use(asts, std_name, prelude_name, id_from_identifier(identifiers, range::from_literal_string("typeof")));

	std_prelude_use(asts, std_name, prelude_name, id_from_identifier(identifiers, range::from_literal_string("sizeof")));

	std_prelude_use(asts, std_name, prelude_name, id_from_identifier(identifiers, range::from_literal_string("alignof")));

	std_prelude_use(asts, std_name, prelude_name, id_from_identifier(identifiers, range::from_literal_string("strideof")));

	push_node(asts, first_token, SourceId::INVALID, AstFlag::EMPTY, AstTag::File);

	AstNode* const prelude_ast = complete_ast(asts);

	set_prelude_scope(interp->lex, prelude_ast);

	type_from_file_ast(interp, prelude_ast, SourceId::INVALID, &interp->prelude_type_id);

	if (interp->type_log_file.m_rep != nullptr && interp->log_prelude)
	{
		const SourceLocation file_type_location = source_location_from_source_id(interp->reader, SourceId::INVALID);

		diag::print_type(interp->type_log_file, interp->identifiers, interp->types, interp->prelude_type_id, &file_type_location);
	}
}



Interpreter* create_interpreter(AllocPool* alloc, Config* config, SourceReader* reader, Parser* parser, TypePool* types, AstPool* asts, IdentifierPool* identifiers, GlobalValuePool* globals, PartialValuePool* partials, ClosurePool* closures, LexicalAnalyser* lex, ErrorSink* errors, minos::FileHandle type_log_file, minos::FileHandle ast_log_file, bool log_prelude) noexcept
{
	Interpreter* const interp = static_cast<Interpreter*>(alloc_from_pool(alloc, sizeof(Interpreter), alignof(Interpreter)));

	static constexpr u64 AREC_HEADERS_RESERVE_SIZE = (1 << 20) * sizeof(Arec);

	static constexpr u64 AREC_ATTACHS_RESERVE_SIZE = (1 << 26);

	static constexpr u64 TEMPS_RESERVE_SIZE = (1 << 26) * sizeof(byte);

	static constexpr u64 ACTIVE_CLOSURES_RESERVE_SIZE = 512 * sizeof(ClosureInstance);

	static constexpr u64 PARTIAL_VALUE_BUILDER_RESERVE_SIZE = (1 << 16) * sizeof(PartialValueBuilderId);

	static constexpr u64 ACTIVE_PARTIAL_VALUE_RESERVE_SIZE = (1 << 16) * sizeof(PeekablePartialValueIterator);

	const u64 total_reserve_size = AREC_HEADERS_RESERVE_SIZE
	                             + AREC_ATTACHS_RESERVE_SIZE
	                             + TEMPS_RESERVE_SIZE
	                             + ACTIVE_CLOSURES_RESERVE_SIZE
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
	interp->lex = lex;
	interp->errors = errors;
	interp->top_arec_id = ArecId::INVALID;
	interp->active_arec_id = ArecId::INVALID;
	interp->prelude_type_id = TypeId::INVALID;
	interp->type_log_file = type_log_file;
	interp->ast_log_file = ast_log_file;
	interp->log_prelude = log_prelude;

	u64 offset = 0;

	interp->arec_headers.init({ memory + offset, AREC_HEADERS_RESERVE_SIZE }, 1 << 9);
	offset += AREC_HEADERS_RESERVE_SIZE;

	interp->arec_attachs.init({ memory + offset, AREC_ATTACHS_RESERVE_SIZE }, 1 << 9);
	offset += AREC_ATTACHS_RESERVE_SIZE;

	interp->temps.init({ memory + offset, TEMPS_RESERVE_SIZE }, 1 << 9);
	offset += TEMPS_RESERVE_SIZE;

	interp->active_closures.init({ memory + offset, ACTIVE_CLOSURES_RESERVE_SIZE }, 512);
	offset += ACTIVE_CLOSURES_RESERVE_SIZE;

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
		root = parse(interp->parser, read.content, read.source_file->source_id_base, is_std);

		read.source_file->root_ast = id_from_ast_node(interp->asts, root);
	}
	else
	{
		root = ast_node_from_id(interp->asts, read.source_file->root_ast);
	}

	type_from_file_ast(interp, root, read.source_file->source_id_base, &read.source_file->root_type);

	const TypeId file_type_id = read.source_file->root_type;

	if (interp->type_log_file.m_rep != nullptr)
	{
		const SourceLocation file_type_location = source_location_from_source_id(interp->reader, read.source_file->source_id_base);

		diag::print_type(interp->type_log_file, interp->identifiers, interp->types, file_type_id, &file_type_location);
	}

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
		"_type_info",
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
		"_caller_source_id",
		"_definition_typeof",
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
