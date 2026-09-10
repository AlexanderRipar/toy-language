#include "core.hpp"
#include "structure.hpp"

struct alignas(8) TypecheckDefinition
{
	TypeId type;

	u32 size;

	u8 align_log2;

	bool is_type_evaluated;

	bool is_mut;

	u32 value_offset;
};

struct alignas(8) TypecheckScope
{
	u32 definition_count;

	u32 prev_scope_index;

	TypecheckDefinition definitions[];
};

struct alignas(8) TypecheckValue
{
	void* begin;

	u32 size;

	u8 align_log2;

	bool is_mut;
};



static void push_chunk(CoreData* core, Code2Header* header) noexcept
{
	const Code2Id id = id_from_code2_chunk(core, header);

	core->typecheck.active_chunks.append(id);
}

static void pop_chunk(CoreData* core) noexcept
{
	ASSERT_OR_IGNORE(core->typecheck.active_chunks.used() != 0);

	core->typecheck.active_chunks.pop_by(1);
}

static Code2Header* chunk_at(CoreData* core, u32 out) noexcept
{
	ASSERT_OR_IGNORE(out + 1 < core->typecheck.active_chunks.used());

	const Code2Id id = core->typecheck.active_chunks.end()[-(out + 1)];

	return code2_chunk_from_id(core, id);
}



static void push_scope(CoreData* core, u16 definition_count) noexcept
{
	const u64 size = sizeof(TypecheckScope) + definition_count * sizeof(TypecheckDefinition);

	TypecheckScope* const scope = static_cast<TypecheckScope*>(core->typecheck.scopes.reserve_exact(size));
	scope->definition_count = definition_count;
	scope->prev_scope_index = is_some(core->typecheck.top_scope)
		? get(core->typecheck.top_scope) - core->typecheck.scopes.begin()
		: 0;

	core->typecheck.top_scope = some(scope);
}

static void pop_scope(CoreData* core) noexcept
{
	const TypecheckScope* const old_top = get(core->typecheck.top_scope);

	const u32 new_top_index = old_top->prev_scope_index;

	const Maybe<TypecheckScope*> new_top = new_top_index == 0
		? none<TypecheckScope*>()
		: some(core->typecheck.scopes.begin() + new_top_index);

	core->typecheck.scopes.pop_by(old_top - core->typecheck.scopes.begin());
}

static TypecheckScope* scope_at(CoreData* core, u8 out) noexcept
{
	TypecheckScope* scope = get(core->typecheck.top_scope);

	for (u8 i = 0; i != out; ++i)
	{
		ASSERT_OR_IGNORE(scope->prev_scope_index != 0);

		scope = core->typecheck.scopes.begin() + scope->prev_scope_index;
	}

	return scope;
}



static void push_value(CoreData* core, TypecheckValue value) noexcept
{
	core->typecheck.values.append(value);
}

static void pop_value(CoreData* core, u32 count = 1) noexcept
{
	ASSERT_OR_IGNORE(count != 0);

	core->typecheck.values.pop_by(count);
}

static TypecheckValue value_at(CoreData* core, u32 index) noexcept
{
	ASSERT_OR_IGNORE(index + 1 < core->typecheck.values.used());

	return core->typecheck.values.end()[-(index + 1)];
}



static void typecheck_error(CoreData* core, Code2* code, CompileError error) noexcept
{
	TODO("Implement");
}



static Maybe<Code2*> as_next(void* ptr) noexcept
{
	return some(static_cast<Code2*>(ptr));
}

template<typename T>
static T* access_value(TypecheckValue value) noexcept
{
	ASSERT_OR_IGNORE(1 << value.align_log2 == alignof(T));

	ASSERT_OR_IGNORE(value.size == sizeof(T));

	return static_cast<T*>(value.begin);
}




// Evaluates the given `Code2`, assuming that all of its dependencies have
// already been evaluated.
// "Dependencies" refers to subexpressions (present on the value stack), local
// definitions (present on the scope stack), and typechecking (present in
// `code->type`). The result of evaluating `code` is put onto the value stack.
static bool evaluate(CoreData* core, Code2Header* header, Code2* code) noexcept
{
	switch (code->op)
	{
	case Code2Op::Leave:
	case Code2Op::ScopeBegin:
	case Code2Op::ScopeEnd:
	case Code2Op::Builtin:
	case Code2Op::Composite:
	case Code2Op::Array:
	case Code2Op::Definition:
	case Code2Op::MakeLocation:
	case Code2Op::WithType:
	case Code2Op::DefineLocal:
	case Code2Op::ConsumeVoid:
	case Code2Op::Void:
	case Code2Op::If:
	case Code2Op::Loop:
	case Code2Op::Func:
	case Code2Op::SignatureBegin:
	case Code2Op::SignatureEnd:
	case Code2Op::Parameter:
	case Code2Op::Trait:
	case Code2Op::Impl:
	case Code2Op::ImplTraitCall:
	case Code2Op::Self:
	case Code2Op::Unreachable:
	case Code2Op::Undefined:
	case Code2Op::TemplatedParamValue:
	case Code2Op::TemplatedParamLocation:
	case Code2Op::LocalValue:
	case Code2Op::LocalLocation:
	case Code2Op::GlobalValue:
	case Code2Op::GlobalLocation:
	case Code2Op::ClosedValue:
	case Code2Op::ClosedLocation:
	case Code2Op::TraitArgumentValue:
	case Code2Op::TraitArgumentLocation:
	case Code2Op::Slice:
	case Code2Op::Integer16:
	case Code2Op::Integer64:
	case Code2Op::Float:
	case Code2Op::String:
	case Code2Op::Call:
	case Code2Op::TypeTailArray:
	case Code2Op::TypeSlice:
	case Code2Op::TypePtr:
	case Code2Op::Eval:
	case Code2Op::Defer:
	case Code2Op::Distinct:
	case Code2Op::Address:
	case Code2Op::Dereference:
	case Code2Op::BitNot:
	case Code2Op::LogicalNot:
	case Code2Op::Negate:
	case Code2Op::UnaryPlus:
	case Code2Op::Add:
	case Code2Op::Sub:
	case Code2Op::Mul:
	case Code2Op::Div:
	case Code2Op::AddTC:
	case Code2Op::SubTC:
	case Code2Op::MulTC:
	case Code2Op::Mod:
	case Code2Op::BitAnd:
	case Code2Op::BitOr:
	case Code2Op::BitXor:
	case Code2Op::ShiftLeft:
	case Code2Op::ShiftRight:
	case Code2Op::TypeArray:
	case Code2Op::Index:
	case Code2Op::LogicalAnd:
	case Code2Op::LogicalOr:
	case Code2Op::MemberValue:
	case Code2Op::MemberLocation:
	case Code2Op::Compare:
	case Code2Op::Set:
	case Code2Op::LoadLocation:

	case Code2Op::INVALID:
		; // Fallthrough to unreachable.
	}
}

// Locks in the preliminary types set by `typecheck`, unifying or promoting them
// to proper types as necessary.
static Maybe<TypeId> natural_type(CoreData* core, Code2Header* header, Code2* code) noexcept
{
	ASSERT_OR_IGNORE(is_some(code->type));

	switch (code->op)
	{
	case Code2Op::Builtin:
	{
		ASSERT_OR_IGNORE(static_cast<u8>(code->builtin.builtin) < array_count(core->typecheck.builtin_types));

		ASSERT_OR_IGNORE(get(code->type) == core->typecheck.builtin_types[static_cast<u8>(code->builtin.builtin)]);

		return code->type;
	}

	case Code2Op::Composite:
	case Code2Op::Array:
	case Code2Op::Definition:
	case Code2Op::MakeLocation:
	case Code2Op::WithType:
	case Code2Op::DefineLocal:
	case Code2Op::ConsumeVoid:
	case Code2Op::Void:
	case Code2Op::If:
	case Code2Op::Loop:
	case Code2Op::Func:
	case Code2Op::SignatureBegin:
	case Code2Op::SignatureEnd:
	case Code2Op::Parameter:
	case Code2Op::Trait:
	case Code2Op::Impl:
	case Code2Op::ImplTraitCall:
	case Code2Op::Self:
	case Code2Op::Unreachable:
	case Code2Op::Undefined:
	case Code2Op::TemplatedParamValue:
	case Code2Op::TemplatedParamLocation:
	case Code2Op::LocalValue:
	case Code2Op::LocalLocation:
	case Code2Op::GlobalValue:
	case Code2Op::GlobalLocation:
	case Code2Op::ClosedValue:
	case Code2Op::ClosedLocation:
	case Code2Op::TraitArgumentValue:
	case Code2Op::TraitArgumentLocation:
	case Code2Op::Slice:
	case Code2Op::Integer16:
	case Code2Op::Integer64:
	case Code2Op::Float:
	case Code2Op::String:
	case Code2Op::Call:
	case Code2Op::TypeTailArray:
	case Code2Op::TypeSlice:
	case Code2Op::TypePtr:
	case Code2Op::Eval:
	case Code2Op::Defer:
	case Code2Op::Distinct:
	case Code2Op::Address:
	case Code2Op::Dereference:
	case Code2Op::BitNot:
	case Code2Op::LogicalNot:
	case Code2Op::Negate:
	case Code2Op::UnaryPlus:
	case Code2Op::Add:
	case Code2Op::Sub:
	case Code2Op::Mul:
	case Code2Op::Div:
	case Code2Op::AddTC:
	case Code2Op::SubTC:
	case Code2Op::MulTC:
	case Code2Op::Mod:
	case Code2Op::BitAnd:
	case Code2Op::BitOr:
	case Code2Op::BitXor:
	case Code2Op::ShiftLeft:
	case Code2Op::ShiftRight:
	case Code2Op::TypeArray:
	case Code2Op::Index:
	case Code2Op::LogicalAnd:
	case Code2Op::LogicalOr:
	case Code2Op::MemberValue:
	case Code2Op::MemberLocation:
	case Code2Op::Compare:
	case Code2Op::Set:
	case Code2Op::LoadLocation:

	case Code2Op::INVALID:
	case Code2Op::Leave:
	case Code2Op::ScopeBegin:
	case Code2Op::ScopeEnd:
		; // Fallthrough to unreachable.
	}
}

static Maybe<TypeId> unified_type(CoreData* core, Code2Header* header, Code2* code, TypeId type) noexcept
{
	ASSERT_OR_IGNORE(is_some(code->type));

	switch (code->op)
	{
	case Code2Op::Builtin:
	{
		const u8 ordinal = static_cast<u8>(code->builtin.builtin);

		ASSERT_OR_IGNORE(ordinal != 0 && ordinal < array_count(core->typecheck.builtin_types));

		const TypeRelation relation = type_relation(core, get(code->type), type);

		if (relation == TypeRelation::Equal || relation == TypeRelation::SecondConvertsToFirst)
		{
			return code->type;
		}
		else if (relation == TypeRelation::FirstConvertsToSecond)
		{
			code->type = some(type);

			return some(type);
		}
		else
		{
			ASSERT_OR_IGNORE(relation == TypeRelation::Incomplete || relation == TypeRelation::Unrelated);

			const CompileError error = relation == TypeRelation::Incomplete
				? CompileError::IncompleteType
				: CompileError::TypesCannotConvert;

			typecheck_error(core, code, error);

			return none<TypeId>();
		}
	}

	case Code2Op::Composite:
	case Code2Op::Array:
	case Code2Op::Definition:
	case Code2Op::MakeLocation:
	case Code2Op::WithType:
	case Code2Op::DefineLocal:
	case Code2Op::ConsumeVoid:
	case Code2Op::Void:
	case Code2Op::If:
	case Code2Op::Loop:
	case Code2Op::Func:
	case Code2Op::SignatureBegin:
	case Code2Op::SignatureEnd:
	case Code2Op::Parameter:
	case Code2Op::Trait:
	case Code2Op::Impl:
	case Code2Op::ImplTraitCall:
	case Code2Op::Self:
	case Code2Op::Unreachable:
	case Code2Op::Undefined:
	case Code2Op::TemplatedParamValue:
	case Code2Op::TemplatedParamLocation:
	case Code2Op::LocalValue:
	case Code2Op::LocalLocation:
	case Code2Op::GlobalValue:
	case Code2Op::GlobalLocation:
	case Code2Op::ClosedValue:
	case Code2Op::ClosedLocation:
	case Code2Op::TraitArgumentValue:
	case Code2Op::TraitArgumentLocation:
	case Code2Op::Slice:
	case Code2Op::Integer16:
	case Code2Op::Integer64:
	case Code2Op::Float:
	case Code2Op::String:
	case Code2Op::Call:
	case Code2Op::TypeTailArray:
	case Code2Op::TypeSlice:
	case Code2Op::TypePtr:
	case Code2Op::Eval:
	case Code2Op::Defer:
	case Code2Op::Distinct:
	case Code2Op::Address:
	case Code2Op::Dereference:
	case Code2Op::BitNot:
	case Code2Op::LogicalNot:
	case Code2Op::Negate:
	case Code2Op::UnaryPlus:
	case Code2Op::Add:
	case Code2Op::Sub:
	case Code2Op::Mul:
	case Code2Op::Div:
	case Code2Op::AddTC:
	case Code2Op::SubTC:
	case Code2Op::MulTC:
	case Code2Op::Mod:
	case Code2Op::BitAnd:
	case Code2Op::BitOr:
	case Code2Op::BitXor:
	case Code2Op::ShiftLeft:
	case Code2Op::ShiftRight:
	case Code2Op::TypeArray:
	case Code2Op::Index:
	case Code2Op::LogicalAnd:
	case Code2Op::LogicalOr:
	case Code2Op::MemberValue:
	case Code2Op::MemberLocation:
	case Code2Op::Compare:
	case Code2Op::Set:
	case Code2Op::LoadLocation:

	case Code2Op::INVALID:
	case Code2Op::Leave:
	case Code2Op::ScopeBegin:
	case Code2Op::ScopeEnd:
		; // Fallthrough to unreachable.
	}
}

// Locks in the preliminary types set by `typecheck`, casting them to `type` as
// necessary.
static bool specific_type(CoreData* core, Code2Header* header, Code2* code, TypeId type) noexcept
{
	ASSERT_OR_IGNORE(is_some(code->type));

	switch (code->op)
	{
	case Code2Op::Builtin:
	{
		Code2AttBuiltin* const attach = attachment_of<Code2AttBuiltin>(code);

		const TypeRelation relation = type_relation(core, get(attach->header.type), type);

		if (relation != TypeRelation::Equal && relation != TypeRelation::FirstConvertsToSecond)
		{
			const CompileError error = relation == TypeRelation::Incomplete
				? CompileError::IncompleteType
				: CompileError::TypesCannotConvert;

			typecheck_error(core, code, error);

			return false;
		}

		attach->header.type = some(type);

		return true;
	}

	case Code2Op::Composite:
	case Code2Op::Array:
	case Code2Op::Definition:
	case Code2Op::WithType:
	case Code2Op::MakeLocation:
	case Code2Op::DefineLocal:
	case Code2Op::ConsumeVoid:
	case Code2Op::Void:
	case Code2Op::If:
	case Code2Op::Loop:
	case Code2Op::Func:
	case Code2Op::SignatureBegin:
	case Code2Op::SignatureEnd:
	case Code2Op::Parameter:
	case Code2Op::Trait:
	case Code2Op::Impl:
	case Code2Op::ImplTraitCall:
	case Code2Op::Self:
	case Code2Op::Unreachable:
	case Code2Op::Undefined:
	case Code2Op::TemplatedParamValue:
	case Code2Op::TemplatedParamLocation:
	case Code2Op::LocalValue:
	case Code2Op::LocalLocation:
	case Code2Op::GlobalValue:
	case Code2Op::GlobalLocation:
	case Code2Op::ClosedValue:
	case Code2Op::ClosedLocation:
	case Code2Op::TraitArgumentValue:
	case Code2Op::TraitArgumentLocation:
	case Code2Op::Slice:
	case Code2Op::Integer16:
	case Code2Op::Integer64:
	case Code2Op::Float:
	case Code2Op::String:
	case Code2Op::Call:
	case Code2Op::TypeTailArray:
	case Code2Op::TypeSlice:
	case Code2Op::TypePtr:
	case Code2Op::Eval:
	case Code2Op::Defer:
	case Code2Op::Distinct:
	case Code2Op::Address:
	case Code2Op::Dereference:
	case Code2Op::BitNot:
	case Code2Op::LogicalNot:
	case Code2Op::Negate:
	case Code2Op::UnaryPlus:
	case Code2Op::Add:
	case Code2Op::Sub:
	case Code2Op::Mul:
	case Code2Op::Div:
	case Code2Op::AddTC:
	case Code2Op::SubTC:
	case Code2Op::MulTC:
	case Code2Op::Mod:
	case Code2Op::BitAnd:
	case Code2Op::BitOr:
	case Code2Op::BitXor:
	case Code2Op::ShiftLeft:
	case Code2Op::ShiftRight:
	case Code2Op::TypeArray:
	case Code2Op::Index:
	case Code2Op::LogicalAnd:
	case Code2Op::LogicalOr:
	case Code2Op::MemberValue:
	case Code2Op::MemberLocation:
	case Code2Op::Compare:
	case Code2Op::Set:
	case Code2Op::LoadLocation:

	case Code2Op::INVALID:
	case Code2Op::Leave:
	case Code2Op::ScopeBegin:
	case Code2Op::ScopeEnd:
		; // Fallthrough to unreachable.
	}
}

// Sets the `type` field of `code`.
static Maybe<Code2*> typecheck(CoreData* core, Code2Header* header, Code2* code) noexcept
{
	ASSERT_OR_IGNORE(is_none(code->type));

	switch (code->op)
	{
	case Code2Op::Leave:
	{
		Code2AttLeave* const attach = attachment_of<Code2AttLeave>(code);

		const TypeId divergent_type = type_create_simple(core, TypeTag::Divergent);

		code->type = some(divergent_type);

		return as_next(attach + 1);
	}

	case Code2Op::ScopeBegin:
	{
		Code2AttScopeBegin* const attach = attachment_of<Code2AttScopeBegin>(code);

		push_scope(core, attach->header.scope_begin.definition_count);

		return as_next(attach + 1);
	}

	case Code2Op::ScopeEnd:
	{
		Code2AttNonaryOp* const attach = attachment_of<Code2AttNonaryOp>(code);

		pop_scope(core);

		return as_next(attach + 1);
	}

	case Code2Op::Builtin:
	{
		Code2AttBuiltin* const attach = attachment_of<Code2AttBuiltin>(code);

		const u8 ordinal = static_cast<u8>(attach->header.builtin.builtin);

		ASSERT_OR_IGNORE(ordinal != 0 && ordinal < array_count(core->typecheck.builtin_types));

		attach->header.type = some(core->typecheck.builtin_types[ordinal]);

		return as_next(attach + 1);
	}

	case Code2Op::Composite:
	case Code2Op::Array:
		TODO("Impement `typecheck(%).`", tag_name(code->op));

	case Code2Op::Definition:
	{
		Code2AttDefinition* const attach = attachment_of<Code2AttDefinition>(code);

		if (is_some(attach->type))
		{
			Code2* const type_code = code2_from_offset(header, get(attach->type));

			const TypeId type_type = type_create_simple(core, TypeTag::Type);

			if (!specific_type(core, header, type_code, type_type))
				return none<Code2*>();

			if (is_some(attach->value))
			{
				Code2* const value = code2_from_offset(header, get(attach->value));

				if (!evaluate(core, header, type_code))
					return none<Code2*>();

				const TypecheckValue type_value = value_at(core, 0);

				const TypeId annotated_type = *access_value<TypeId>(type_value);

				if (!specific_type(core, header, value, annotated_type))
					return none<Code2*>();
			}
		}
		else
		{
			Code2* const value = code2_from_offset(header, get(attach->value));

			const Maybe<TypeId> type = natural_type(core, header, value);

			if (is_none(type))
				return none<Code2*>();
		}

		const TypeId definition_type = type_create_simple(core, TypeTag::Definition);

		code->type = some(definition_type);

		return as_next(attach + 1);
	}

	case Code2Op::MakeLocation:
	{
		Code2AttMakeLocation* const attach = attachment_of<Code2AttMakeLocation>(code);

		Code2* const operand = code2_from_offset(header, attach->value);

		const TypeId operand_type = get(operand->type);

		code->type = some(operand_type);

		return as_next(attach + 1);
	}

	case Code2Op::WithType:
	{
		Code2AttBinaryOp* const attach = attachment_of<Code2AttBinaryOp>(code);

		Code2* const lhs = code2_from_offset(header, attach->lhs);

		Code2* const rhs = code2_from_offset(header, attach->rhs);

		const TypeId type_type = type_create_simple(core, TypeTag::Type);

		if (!specific_type(core, header, lhs, type_type))
			return none<Code2*>();

		if (!evaluate(core, header, lhs))
			return none<Code2*>();

		const TypecheckValue value = value_at(core, 0);

		const TypeId annotated_type = *access_value<TypeId>(value);

		if (!specific_type(core, header, rhs, annotated_type))
			return none<Code2*>();

		return as_next(attach + 1);
	}

	case Code2Op::DefineLocal:
	{
		Code2AttDefineLocal* const attach = attachment_of<Code2AttDefineLocal>(code);

		Code2* const value = code2_from_offset(header, attach->value);

		const Maybe<TypeId> type = natural_type(core, header, value);

		if (is_none(type))
			return none<Code2*>();

		TypeMetrics metrics;

		if (!type_metrics_from_id(core, get(type), &metrics))
		{
			typecheck_error(core, code, CompileError::IncompleteType);

			return none<Code2*>();
		}

		TypecheckScope* const scope = scope_at(core, 0);

		ASSERT_OR_IGNORE(is_pow2(metrics.align));

		TypecheckDefinition* const definition = scope->definitions + attach->header.define_local.definition_index;
		definition->type = get(type);
		definition->size = metrics.size;
		definition->align_log2 = count_trailing_zeros_assume_one(metrics.align);
		definition->is_type_evaluated = false; // TODO
		definition->is_mut = attach->header.define_local.is_mut;
		definition->value_offset = 0; // TODO

		return as_next(attach + 1);
	}

	case Code2Op::ConsumeVoid:
	{
		Code2AttUnaryOp* const attach = attachment_of<Code2AttUnaryOp>(code);

		Code2* const operand = code2_from_offset(header, attach->operand);

		const TypeId void_type = type_create_simple(core, TypeTag::Void);

		if (!specific_type(core, header, operand, void_type))
			return none<Code2*>();

		return as_next(attach + 1);
	}

	case Code2Op::Void:
	{
		Code2AttNonaryOp* const attach = attachment_of<Code2AttNonaryOp>(code);

		const TypeId void_type = type_create_simple(core, TypeTag::Void);

		code->type = some(void_type);

		return as_next(attach + 1);
	}

	case Code2Op::If:
	{
		Code2AttIf* const attach = attachment_of<Code2AttIf>(code);

		Code2* const condition = code2_from_offset(header, attach->condition);

		const TypeId bool_type = type_create_simple(core, TypeTag::Boolean);

		if (!specific_type(core, header, condition, bool_type))
			return none<Code2*>();

		Code2Header* const consequent_header = code2_chunk_from_id(core, attach->consequent);

		if (!typecheck_chunk(core, consequent_header))
			return none<Code2*>();

		Code2Header* const alternative_header = code2_chunk_from_id(core, attach->alternative);

		if (!typecheck_chunk(core, alternative_header))
			return none<Code2*>();

		// TODO: Unify types of consequent and alternative.

		return as_next(attach + 1);
	}

	case Code2Op::Loop:
	case Code2Op::Func:
	case Code2Op::SignatureBegin:
	case Code2Op::SignatureEnd:
	case Code2Op::Parameter:
	case Code2Op::Trait:
	case Code2Op::Impl:
	case Code2Op::ImplTraitCall:
	case Code2Op::Self:
	case Code2Op::Unreachable:
	case Code2Op::Undefined:
	case Code2Op::TemplatedParamValue:
	case Code2Op::TemplatedParamLocation:
	case Code2Op::LocalValue:
	case Code2Op::LocalLocation:
	case Code2Op::GlobalValue:
	case Code2Op::GlobalLocation:
	case Code2Op::ClosedValue:
	case Code2Op::ClosedLocation:
	case Code2Op::TraitArgumentValue:
	case Code2Op::TraitArgumentLocation:
	case Code2Op::Slice:
	case Code2Op::Integer16:
	case Code2Op::Integer64:
	case Code2Op::Float:
	case Code2Op::String:
	case Code2Op::Call:
	case Code2Op::TypeTailArray:
	case Code2Op::TypeSlice:
	case Code2Op::TypePtr:
	case Code2Op::Eval:
	case Code2Op::Defer:
	case Code2Op::Distinct:
	case Code2Op::Address:
	case Code2Op::Dereference:
	case Code2Op::BitNot:
	case Code2Op::LogicalNot:
	case Code2Op::Negate:
	case Code2Op::UnaryPlus:
	case Code2Op::Add:
	case Code2Op::Sub:
	case Code2Op::Mul:
	case Code2Op::Div:
	case Code2Op::AddTC:
	case Code2Op::SubTC:
	case Code2Op::MulTC:
	case Code2Op::Mod:
	case Code2Op::BitAnd:
	case Code2Op::BitOr:
	case Code2Op::BitXor:
	case Code2Op::ShiftLeft:
	case Code2Op::ShiftRight:
	case Code2Op::TypeArray:
	case Code2Op::Index:
	case Code2Op::LogicalAnd:
	case Code2Op::LogicalOr:
	case Code2Op::MemberValue:
	case Code2Op::MemberLocation:
	case Code2Op::Compare:
	case Code2Op::Set:
	case Code2Op::LoadLocation:

	case Code2Op::INVALID:
		; // Fallthrough to unreachable.
	}
}

// Modifies the chunk identified by `header` by setting its codes' `type`
// fields.
// If `header` is a function body, an argument scope containing all
// type-evaluated arguments must first be set up by the caller. 
bool typecheck_chunk(CoreData* core, Code2Header* header) noexcept;


