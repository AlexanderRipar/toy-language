#include "core.hpp"
#include "structure.hpp"

#include "../infra/inplace_sort.hpp"

#include <cmath>

enum class ContState : u32
{
	INITIAL = 0,
	ERROR,
	If_Alternative,
	If_SetType,
	LoopOrJump_SetType,
	InitGlobal_LockType,
	InitGlobal_LockValueUntyped,
	InitGlobal_LockValueTyped,
	Call_LockTemplatedParameterType,
	Call_LockTemplatedParameterValue,
	Call_LockArgumentValue,
	Call_LockTemplatedReturnType,
};

struct alignas(8) TypecheckValue
{
	Maybe<void*> begin;

	u32 size;

	u8 align_log2;

	bool is_mut;
};

#ifdef COMPILER_MSVC
	#pragma warning(push)
	#pragma warning(disable : 4324) // structure was padded due to alignment specifier
#endif
struct alignas(8) TypecheckActivation
{
	Code2Block* block;

	Maybe<Code2Offset> cont_code;

	ContState cont_state;

	bool is_dynamic_control_flow;

	u16 definition_count;

	u32 prev_activation;

	u32 prev_locations_used;

	Code2Location prev_leave;

	#if COMPILER_GCC
		#pragma GCC diagnostic push
		#pragma GCC diagnostic ignored "-Wpedantic" // ISO C++ forbids anonymous strcuts
	#endif
	TypecheckDefinition definitions[];
	#if COMPILER_GCC
		#pragma GCC diagnostic pop
	#endif
};

struct alignas(8) TypecheckClosure
{
	u32 count;

	#if COMPILER_GCC
		#pragma GCC diagnostic push
		#pragma GCC diagnostic ignored "-Wpedantic" // ISO C++ forbids anonymous strcuts
	#endif
	TypecheckDefinition definitions[];
	#if COMPILER_GCC
		#pragma GCC diagnostic pop
	#endif

	// byte allocation[];
};
#ifdef COMPILER_MSVC
	#pragma warning(pop)
#endif

enum class PopTypeResult
{
	Ok,
	NoValue,
	Failure,
};

enum class LockTypeCallResult : u8
{
	OkTypeEval,
	OkNonTypeEval,
	Failure,
};

struct TypecheckNext
{
	Code2* code;

	Code2Block* block;

	ContState state = ContState::INITIAL;
};





static bool lock_type_single(CoreData* core, Code2Block* block, Code2* code, Maybe<TypeId> expected_type) noexcept;

static TypecheckNext typecheck_block(CoreData* core, Maybe<Code2*> cont_code, ContState cont_state, Code2Block* block, bool preserve_locations, bool is_dynamic_control_flow) noexcept;

static bool lock_type_block(CoreData* core, Code2Block* block, Maybe<TypeId> expected_type) noexcept;

static TypecheckNext initialize_global_cont(CoreData* core, Code2Block* block, Code2* code, ContState state, TypeId file_type, const MemberInfo* info) noexcept;





static bool record_lock_type_error(CoreData* core, Code2* code) noexcept
{
	(void) core;

	(void) code;

	TODO("Implement.");
}

static TypecheckNext record_typecheck_error(CoreData* core, Code2* code) noexcept
{
	(void) core;

	(void) code;

	TODO("Implement.");
}





static u32 push_location(CoreData* core, const void* begin, u64 size, u64 align) noexcept
{
	pad_to_alignment(&core->typecheck.locations, align);

	void* const location = push(&core->typecheck.locations, size);

	memcpy(location, begin, size);

	return used(&core->typecheck.locations);
}

static TypecheckValue* peek_value(CoreData* core, u64 n = 0) noexcept
{
	return static_cast<TypecheckValue*>(from_top(&core->typecheck.values, n * sizeof(TypecheckValue)));
}

template<typename T>
static bool peek_value_as(CoreData* core, T* out) noexcept
{
	TypecheckValue* const value = peek_value(core, 0);

	if (is_none(value->begin))
		return false;

	ASSERT_OR_IGNORE(sizeof(T) == value->size);

	ASSERT_OR_IGNORE(alignof(T) == static_cast<u64>(1) << value->align_log2);

	*out = *static_cast<T*>(get(value->begin));

	return true;
}

static void pop_value(CoreData* core) noexcept
{
	pop_by(&core->typecheck.values, sizeof(TypecheckValue));
}

template<typename T>
static bool pop_value_as(CoreData* core, T* out) noexcept
{
	const bool result = peek_value_as(core, out);

	pop_value(core);

	return result;
}

static Maybe<CoreId> pop_value_into_core_id(CoreData* core) noexcept
{
	TypecheckValue* const value = peek_value(core, 0);

	if (is_none(value->begin))
		return none<CoreId>();

	Maybe<void*> const dst = comp_heap_alloc(core, value->size, static_cast<u64>(1) << value->align_log2);

	if (is_none(dst))
		TODO("Implement GC.");

	memcpy(get(dst), get(value->begin), value->size);

	pop_value(core);

	return some(core_id_from_address(core, get(dst)));
}

static bool pop_value_into_definition(CoreData* core, TypecheckDefinition* dst, TypeId type, bool is_mut) noexcept
{
	TypecheckValue* const value = peek_value(core, 0);

	const bool is_type_eval = is_some(value->begin);

	dst->type = type;
	dst->size = value->size;
	dst->align_log2 = value->align_log2;
	dst->is_type_eval = is_type_eval;
	dst->is_mut = is_mut;
	dst->location_offset = is_type_eval
		? push_location(core, get(value->begin), value->size, static_cast<u64>(1) << value->align_log2)
		: 0;

	pop_value(core);

	return is_type_eval;
}

static Maybe<ClosureId> pop_values_into_closure(CoreData* core, Code2Block* block, Range<Code2Offset> closed_codes) noexcept
{
	ASSERT_OR_IGNORE(closed_codes.count() != 0);

	const u64 count = closed_codes.count();

	const u64 header_size = sizeof(TypecheckClosure) + count * sizeof(TypecheckDefinition);

	u64 size = header_size;

	u64 align = alignof(TypecheckDefinition);

	for (u64 i = 0; i != count; ++i)
	{
		Code2* const code = code2_from_offset(block, closed_codes[count - 1 - i]);

		TypeMetrics metrics;

		if (!type_metrics_from_id(core, code->type, &metrics))
			ASSERT_UNREACHABLE;

		if (size + metrics.align < size)
		{
			// TODO: Record Error.

			return none<ClosureId>();
		}

		size = (size + metrics.align - 1) & ~static_cast<u64>(metrics.align - 1);

		if (size + metrics.size < size)
		{
			// TODO: Record Error.

			return none<ClosureId>();
		}

		size += metrics.size;

		if (align < metrics.align)
			align = metrics.align;
	}

	const Maybe<void*> allocation = comp_heap_alloc(core, size, align);

	if (is_none(allocation))
		TODO("Implement GC.");

	TypecheckClosure* const closure = static_cast<TypecheckClosure*>(get(allocation));

	byte* const value_base = static_cast<byte*>(get(allocation));

	u64 value_offset = header_size;
	
	for (u64 i = 0; i != count; ++i)
	{
		Code2* const code = code2_from_offset(block, closed_codes[count - 1 - i]);

		TypecheckValue* const value = peek_value(core, 0);

		if (is_none(value->begin))
		{
			// TODO: Record Error.

			return none<ClosureId>();
		}

		const u32 value_size = value->size;

		const u8 value_align_log2 = value->align_log2;

		const u32 value_align = static_cast<u32>(1) << value_align_log2;

		ASSERT_OR_IGNORE(value_align <= align);

		#ifndef _NDEBUG
		{
			TypeMetrics metrics;

			if (!type_metrics_from_id(core, code->type, &metrics))
				ASSERT_UNREACHABLE;

			ASSERT_OR_IGNORE(metrics.size == value->size && metrics.align == value_align);
		}
		#endif

		value_offset = (value_offset + value_align - 1) & ~static_cast<u64>(value_align - 1);

		memcpy(value_base + value_offset, get(value->begin), value_size);

		value_offset += value_size;

		TypecheckDefinition* const definition = closure->definitions + count - 1 - i;
		definition->type = code->type;
		definition->size = value_size;
		definition->align_log2 = value_align_log2;
		definition->is_type_eval = true;
		definition->is_mut = false;
		definition->location_offset = static_cast<u32>(value_offset - header_size);

		pop_value(core);
	}

	ASSERT_OR_IGNORE(value_offset == size);

	return some(static_cast<ClosureId>(core_id_from_address(core, closure)));
}

static bool pop_value_into_address(CoreData* core, MutRange<byte> dst) noexcept
{
	TypecheckValue* const value = peek_value(core, 0);

	if (is_none(value->begin))
		return false;

	ASSERT_OR_IGNORE(static_cast<u64>(value->size) == dst.count());

	memcpy(dst.begin(), get(value->begin), dst.count());

	pop_value(core);

	return true;
}

static void* push_value_raw_uninit(CoreData* core, u64 size, u64 align, bool is_mut) noexcept
{
	pad_to_alignment(&core->typecheck.locations, align);

	void* const dst = push(&core->typecheck.locations, size);

	TypecheckValue* const value = static_cast<TypecheckValue*>(push(&core->typecheck.values, sizeof(TypecheckValue)));
	value->begin = some(dst);
	value->size = static_cast<u32>(size);
	value->align_log2 = count_trailing_zeros_assume_one(align);
	value->is_mut = is_mut;

	return dst;
}

static void push_value_raw(CoreData* core, const void* begin, u64 size, u64 align, bool is_mut) noexcept
{
	void* const dst = push_value_raw_uninit(core, size, align, is_mut);

	memcpy(dst, begin, size);
}

static void push_value_location(CoreData* core, void* begin, u64 size, u64 align, bool is_mut) noexcept
{
	TypecheckValue* const value = static_cast<TypecheckValue*>(push(&core->typecheck.values, sizeof(TypecheckValue)));
	value->begin = some(begin);
	value->size = static_cast<u32>(size);
	value->align_log2 = count_trailing_zeros_assume_one(align);
	value->is_mut = is_mut;
}

static void push_value_empty(CoreData* core, u64 size, u64 align, bool is_mut) noexcept
{
	TypecheckValue* const value = static_cast<TypecheckValue*>(push(&core->typecheck.values, sizeof(TypecheckValue)));
	value->begin = none<void*>();
	value->size = static_cast<u32>(size);
	value->align_log2 = count_trailing_zeros_assume_one(align);
	value->is_mut = is_mut;
}

static void promote_value_to_location(CoreData* core) noexcept
{
	TypecheckValue* const value = peek_value(core);

	// If the value is not type-evaluable we can just leave it in place; It
	// already has all the same characteristics a location would have.
	if (is_none(value->begin))
		return;

	const u32 location = push_location(core, get(value->begin), value->size, static_cast<u64>(1) << value->align_log2);

	value->begin = some(from_base(&core->typecheck.locations, location));
}

template<typename T>
static void push_value(CoreData* core, T value, bool is_mut) noexcept
{
	push_value_raw(core, &value, sizeof(T), alignof(T), is_mut);
}



static TypecheckActivation* top_activation(CoreData* core) noexcept
{
	ASSERT_OR_IGNORE(used(&core->typecheck.activations) != 0);

	return static_cast<TypecheckActivation*>(top(&core->typecheck.activations));
}

static TypecheckActivation* next_activation(CoreData* core, TypecheckActivation* activation) noexcept
{
	ASSERT_OR_IGNORE(activation->prev_activation != 0);

	return static_cast<TypecheckActivation*>(from_top(&core->typecheck.activations, activation->prev_activation));
}

static TypecheckActivation* get_activation(CoreData* core, u32 out) noexcept
{
	TypecheckActivation* activation = top_activation(core);

	for (u32 i = 0; i != out; ++i)
		activation = next_activation(core, activation);

	return activation;
}

static void push_activation(CoreData* core, Code2Block* block, Maybe<Code2*> cont_code, ContState cont_state, bool preserve_locations, bool is_dynamic_control_flow) noexcept
{
	if (is_some(cont_code))
	{
		TypecheckActivation* const prev_activation = top_activation(core);

		prev_activation->cont_code = some(offset_from_code2(prev_activation->block, get(cont_code)));
		prev_activation->cont_state = cont_state;
	}

	const u32 prev_locations_used = preserve_locations
		? UINT32_MAX
		: used(&core->typecheck.locations);

	const u32 prev_activation = used(&core->typecheck.activations);

	const u64 size = sizeof(TypecheckActivation) + block->definition_count * sizeof(TypecheckDefinition);

	TypecheckActivation* const activation = static_cast<TypecheckActivation*>(push(&core->typecheck.activations, size));
	activation->block = block;
	activation->cont_code = none<Code2Offset>();
	activation->cont_state = ContState::INITIAL;
	activation->prev_activation = prev_activation;
	activation->prev_locations_used = prev_locations_used;
	activation->definition_count = 0;
	activation->is_dynamic_control_flow = is_dynamic_control_flow;
}

static bool unify_leaves(CoreData* core, TypecheckActivation* activation) noexcept
{
	// `Divergent` can be implicitly converted to every other type, so it's a
	// nice initial value here.
	// It is also the type of a block that does not get targetted by any
	// leaves, i.e. a block that gets left from an inner block.
	TypeId common_type = type_create_simple(core, TypeTag::Divergent);

	Code2Location curr = activation->prev_leave;

	while (is_some(curr.block))
	{
		Code2Block* const block = code2_block_from_id(core, get(curr.block));

		Code2* const code = code2_from_offset(block, curr.offset);

		ASSERT_OR_IGNORE(code->op == Code2Op::Leave);

		Code2AttLeave* const attachment = attachment_of<Code2AttLeave>(code);

		Code2* const operand = code2_from_offset(block, attachment->operand);

		const TypeRelation relation = type_relation(core, common_type, operand->type);

		if (relation == TypeRelation::FirstConvertsToSecond)
			common_type = operand->type;
		else if (relation == TypeRelation::Unrelated || relation == TypeRelation::Incomplete)
			return record_lock_type_error(core, code);
		else
			ASSERT_OR_IGNORE(relation == TypeRelation::Equal || relation == TypeRelation::SecondConvertsToFirst);
	}

	curr = activation->prev_leave;

	while (is_some(curr.block))
	{
		Code2Block* const block = code2_block_from_id(core, get(curr.block));

		Code2* const code = code2_from_offset(block, curr.offset);

		ASSERT_OR_IGNORE(code->op == Code2Op::Leave);

		Code2AttLeave* const attachment = attachment_of<Code2AttLeave>(code);

		Code2* const operand = code2_from_offset(block, attachment->operand);

		if (!lock_type_single(core, block, operand, some(common_type)))
			return false;
	}

	activation->block->type = common_type;

	return true;
}

static void register_leave(CoreData* core, Code2Block* block, Code2* code, u32 out) noexcept
{
	Code2AttLeave* const attachment = attachment_of<Code2AttLeave>(code);

	TypecheckActivation* const target_activation = get_activation(core, out);

	attachment->prev_leave = target_activation->prev_leave;

	target_activation->prev_leave.block = some(id_from_code2_block(core, block));
	target_activation->prev_leave.offset = offset_from_code2(block, code);
}

static Maybe<TypecheckActivation*> pop_non_control_flow_activations(CoreData* core, u32 out) noexcept
{
	TypecheckActivation* popped = top_activation(core);

	u32 new_locations_used = UINT32_MAX;

	for (u32 i = 0; i != out; ++i)
	{
		unify_leaves(core, popped);

		if (new_locations_used > popped->prev_locations_used)
			new_locations_used = popped->prev_locations_used;

		popped = next_activation(core, popped);
	}

	unify_leaves(core, popped);

	if (new_locations_used > popped->prev_locations_used)
		new_locations_used = popped->prev_locations_used;

	if (new_locations_used != UINT32_MAX)
		pop_to(&core->typecheck.locations, popped->prev_locations_used);

	const u32 new_activations_top = popped->prev_activation;

	pop_to(&core->typecheck.activations, new_activations_top);

	return new_activations_top == 0
		? none<TypecheckActivation*>()
		: some(top_activation(core));
}



static PopTypeResult pop_int_to_u64(CoreData* core, TypeId type, u64* out) noexcept
{
	const TypeTag type_tag = type_tag_from_id(core, type);

	if (type_tag == TypeTag::CompInteger)
	{
		CompIntegerValue value;

		if (!pop_value_as(core, &value))
			return PopTypeResult::NoValue;

		if (!u64_from_comp_integer(value, 64, out))
			return PopTypeResult::Failure;

		return PopTypeResult::Ok;
	}
	else
	{
		ASSERT_OR_IGNORE(type_tag == TypeTag::Integer);

		const NumericType type_attachment = *type_attachment_from_id<NumericType>(core, type);

		ASSERT_OR_IGNORE(type_attachment.bits >= 8 && type_attachment.bits <= 64 && is_pow2(type_attachment.bits));

		u64 bits = 0;

		TypecheckValue* const top = peek_value(core);

		if (is_none(top->begin))
			return PopTypeResult::NoValue;

		ASSERT_OR_IGNORE(static_cast<u32>(type_attachment.bits) / 8 == top->size);

		memcpy(&bits, get(top->begin), type_attachment.bits / 8);

		*out = bits;

		return PopTypeResult::Ok;
	}
}



static bool evaluate_binary_arithmetic_comp_integer_op(CoreData* core, Code2* code, CompIntegerValue lhs, CompIntegerValue rhs, CompIntegerValue* out) noexcept
{
	switch(code->binary_arithmetic.kind)
	{
	case Code2AttBinaryArithmetic_Kind::Add:
	case Code2AttBinaryArithmetic_Kind::AddTC:
	{
		*out = comp_integer_add(lhs, rhs);

		return true;
	}

	case Code2AttBinaryArithmetic_Kind::Sub:
	case Code2AttBinaryArithmetic_Kind::SubTC:
	{
		*out = comp_integer_sub(lhs, rhs);

		return true;
	}

	case Code2AttBinaryArithmetic_Kind::Mul:
	case Code2AttBinaryArithmetic_Kind::MulTC:
	{
		*out = comp_integer_mul(lhs, rhs);

		return true;
	}

	case Code2AttBinaryArithmetic_Kind::Div:
	{
		if (!comp_integer_div(lhs, rhs, out))
			return record_lock_type_error(core, code);

		return true;
	}

	case Code2AttBinaryArithmetic_Kind::Mod:
	{
		if (!comp_integer_mod(lhs, rhs, out))
			return record_lock_type_error(core, code);

		return true;
	}

	case Code2AttBinaryArithmetic_Kind::BitAnd:
	{
		*out = comp_integer_bit_and(lhs, rhs);

		return true;
	}

	case Code2AttBinaryArithmetic_Kind::BitOr:
	{
		*out = comp_integer_bit_or(lhs, rhs);

		return true;
	}

	case Code2AttBinaryArithmetic_Kind::BitXor:
	{
		*out = comp_integer_bit_xor(lhs, rhs);

		return true;
	}
	}

	ASSERT_UNREACHABLE;
}

static bool evaluate_binary_arithmetic_integer_op(CoreData* core, Code2* code, u16 bits, bool is_signed, const void* lhs, const void* rhs, u64* out) noexcept
{
	ASSERT_OR_IGNORE(bits >= 8 && bits <= 64 && is_pow2(bits));

	const u8 size = static_cast<u8>(bits / 8);

	const Range<byte> lhs_value{ static_cast<const byte*>(lhs), size };

	const Range<byte> rhs_value{ static_cast<const byte*>(rhs), size };

	MutRange<byte> dst{ reinterpret_cast<byte*>(out), size }; 

	switch(code->binary_arithmetic.kind)
	{
	case Code2AttBinaryArithmetic_Kind::Add:
	{
		if (!bitwise_add(bits, is_signed, dst, lhs_value, rhs_value))
			return record_lock_type_error(core, code);

		return true;
	}

	case Code2AttBinaryArithmetic_Kind::Sub:
	{
		if (!bitwise_sub(bits, is_signed, dst, lhs_value, rhs_value))
			return record_lock_type_error(core, code);

		return true;
	}

	case Code2AttBinaryArithmetic_Kind::Mul:
	{
		if (!bitwise_mul(bits, is_signed, dst, lhs_value, rhs_value))
			return record_lock_type_error(core, code);

		return true;
	}

	case Code2AttBinaryArithmetic_Kind::Div:
	{
		if (!bitwise_div(bits, is_signed, dst, lhs_value, rhs_value))
			return record_lock_type_error(core, code);

		return true;
	}

	case Code2AttBinaryArithmetic_Kind::AddTC:
	{
		(void) bitwise_add(bits, is_signed, dst, lhs_value, rhs_value);

		return true;
	}

	case Code2AttBinaryArithmetic_Kind::SubTC:
	{
		(void) bitwise_sub(bits, is_signed, dst, lhs_value, rhs_value);

		return true;
	}

	case Code2AttBinaryArithmetic_Kind::MulTC:
	{
		(void) bitwise_mul(bits, is_signed, dst, lhs_value, rhs_value);

		return true;
	}

	case Code2AttBinaryArithmetic_Kind::Mod:
	{
		if (!bitwise_mod(bits, is_signed, dst, lhs_value, rhs_value))
			return record_lock_type_error(core, code);

		return true;
	}

	case Code2AttBinaryArithmetic_Kind::BitAnd:
	{
		for (u8 i = 0; i != size; ++i)
			dst[i] = lhs_value[i] & rhs_value[i];

		return true;
	}

	case Code2AttBinaryArithmetic_Kind::BitOr:
	{
		for (u8 i = 0; i != size; ++i)
			dst[i] = lhs_value[i] | rhs_value[i];

		return true;
	}

	case Code2AttBinaryArithmetic_Kind::BitXor:
	{
		for (u8 i = 0; i != size; ++i)
			dst[i] = lhs_value[i] ^ rhs_value[i];

		return true;
	}
	}

	ASSERT_UNREACHABLE;
}

static CompFloatValue evaluate_binary_arithmetic_comp_float_op(Code2* code, CompFloatValue lhs, CompFloatValue rhs) noexcept
{
	switch (code->binary_arithmetic.kind)
	{
	case Code2AttBinaryArithmetic_Kind::Add:
		return comp_float_add(lhs, rhs);

	case Code2AttBinaryArithmetic_Kind::Sub:
		return comp_float_sub(lhs, rhs);

	case Code2AttBinaryArithmetic_Kind::Mul:
		return comp_float_mul(lhs, rhs);

	case Code2AttBinaryArithmetic_Kind::Div:
		return comp_float_div(lhs, rhs);

	case Code2AttBinaryArithmetic_Kind::AddTC:
	case Code2AttBinaryArithmetic_Kind::SubTC:
	case Code2AttBinaryArithmetic_Kind::MulTC:
	case Code2AttBinaryArithmetic_Kind::Mod:
	case Code2AttBinaryArithmetic_Kind::BitAnd:
	case Code2AttBinaryArithmetic_Kind::BitOr:
	case Code2AttBinaryArithmetic_Kind::BitXor:
		; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

template<typename T>
static T evaluate_binary_arithmetic_float_op(Code2* code, T lhs, T rhs) noexcept
{
	switch (code->binary_arithmetic.kind)
	{
	case Code2AttBinaryArithmetic_Kind::Add:
		return lhs + rhs;

	case Code2AttBinaryArithmetic_Kind::Sub:
		return lhs - rhs;

	case Code2AttBinaryArithmetic_Kind::Mul:
		return lhs * rhs;

	case Code2AttBinaryArithmetic_Kind::Div:
		return lhs / rhs;

	default:
		; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

static bool evaluate_binary_arithmetic_op(CoreData* core, Code2* code, TypeId type, TypecheckValue* lhs, TypecheckValue* rhs) noexcept
{
	const TypeTag type_tag = type_tag_from_id(core, type);

	if (type_tag == TypeTag::CompInteger)
	{
		const CompIntegerValue lhs_value = *static_cast<CompIntegerValue*>(get(lhs->begin));

		const CompIntegerValue rhs_value = *static_cast<CompIntegerValue*>(get(rhs->begin));

		CompIntegerValue result;

		if (!evaluate_binary_arithmetic_comp_integer_op(core, code, lhs_value, rhs_value, &result))
			return false;

		pop_value(core);
		pop_value(core);

		push_value(core, result, false);
	}
	else if (type_tag == TypeTag::Integer)
	{
		const NumericType type_attachment = *type_attachment_from_id<NumericType>(core, type);

		const u16 bits = type_attachment.bits;

		const bool is_signed = type_attachment.is_signed;

		u64 result;

		if (!evaluate_binary_arithmetic_integer_op(core, code, bits, is_signed, lhs, rhs, &result))
			return false;

		pop_value(core);
		pop_value(core);

		push_value_raw(core, &result, bits / 8, bits / 8, false);
	}
	else if (type_tag == TypeTag::CompFloat)
	{
		const CompFloatValue lhs_value = *static_cast<CompFloatValue*>(get(lhs->begin));

		const CompFloatValue rhs_value = *static_cast<CompFloatValue*>(get(rhs->begin));

		const CompFloatValue result = evaluate_binary_arithmetic_comp_float_op(code, lhs_value, rhs_value);

		pop_value(core);
		pop_value(core);

		push_value(core, result, false);
	}
	else if (type_tag == TypeTag::Float)
	{
		const NumericType type_attachment = *type_attachment_from_id<NumericType>(core, type);

		const u16 bits = type_attachment.bits;

		if (bits == 32)
		{
			const f32 lhs_value = *static_cast<f32*>(get(lhs->begin));

			const f32 rhs_value = *static_cast<f32*>(get(rhs->begin));

			const f32 result = evaluate_binary_arithmetic_float_op<f32>(code, lhs_value, rhs_value);

			pop_value(core);
			pop_value(core);

			push_value(core, result, false);
		}
		else
		{
			ASSERT_OR_IGNORE(bits == 64);

			const f64 lhs_value = *static_cast<f64*>(get(lhs->begin));

			const f64 rhs_value = *static_cast<f64*>(get(rhs->begin));

			const f64 result = evaluate_binary_arithmetic_float_op<f64>(code, lhs_value, rhs_value);

			pop_value(core);
			pop_value(core);

			push_value(core, result, false);
		}
	}

	return true;
}



static bool evaluate_shift(CoreData* core, Code2* code, TypeId type, TypeId rhs_type, TypecheckValue* lhs, TypecheckValue* rhs) noexcept
{
	const TypeTag type_tag = type_tag_from_id(core, type);

	const TypeTag rhs_type_tag = type_tag_from_id(core, rhs_type);

	if (type_tag == TypeTag::CompInteger)
	{
		const CompIntegerValue lhs_value = *reinterpret_cast<CompIntegerValue*>(get(lhs->begin));

		CompIntegerValue rhs_value;

		if (rhs_type_tag == TypeTag::CompInteger)
		{
			rhs_value = *reinterpret_cast<CompIntegerValue*>(get(rhs->begin));
		}
		else
		{
			ASSERT_OR_IGNORE(rhs_type_tag == TypeTag::Integer);

			const NumericType rhs_type_attachment = *type_attachment_from_id<NumericType>(core, rhs_type);

			const u16 bits = rhs_type_attachment.bits;

			ASSERT_OR_IGNORE(bits >= 8 && bits <= 64 && is_pow2(bits));

			u64 rhs_integer_value = 0;

			memcpy(&rhs_integer_value, get(rhs->begin), bits / 8);

			if (rhs_type_attachment.is_signed && bits != 64 && (rhs_integer_value & ((static_cast<u64>(1) << bits) - 1)) != 0)
				return record_lock_type_error(core, code);

			rhs_value = comp_integer_from_u64(rhs_integer_value);
		}

		pop_value(core);
		pop_value(core);

		CompIntegerValue result;

		const bool ok = code->bit_shift.kind == Code2AttBitShift_Kind::Left
			? comp_integer_shift_left(lhs_value, rhs_value, &result)
			: comp_integer_shift_right(lhs_value, rhs_value, &result);

		if (!ok)
			return record_lock_type_error(core, code);

		push_value(core, result, false);
	}
	else
	{
		ASSERT_OR_IGNORE(type_tag == TypeTag::Integer);

		const u64 lhs_size = lhs->size;

		const NumericType lhs_type_attachment = *type_attachment_from_id<NumericType>(core, type);

		ASSERT_OR_IGNORE(lhs_type_attachment.bits >= 8 && lhs_type_attachment.bits <= 64 && is_pow2(lhs_type_attachment.bits));

		ASSERT_OR_IGNORE(lhs_size * 8 == lhs_type_attachment.bits);

		const bool lhs_is_signed = lhs_type_attachment.is_signed;

		u64 lhs_value = 0;

		memcpy(&lhs_value, get(lhs->begin), lhs_size);

		// Sign extend if necessary.
		if (lhs_is_signed && lhs_size != 8 && (lhs_value & (static_cast<u64>(1) << lhs->size * 8)) != 0)
			lhs_value |= (~static_cast<u64>(0)) << lhs_size * 8;

		u64 rhs_value;

		if (rhs_type_tag == TypeTag::CompInteger)
		{
			const CompIntegerValue rhs_comp_integer_value = *reinterpret_cast<CompIntegerValue*>(get(lhs->begin));

			if (!u64_from_comp_integer(rhs_comp_integer_value, 64, &rhs_value))
				return record_lock_type_error(core, code);
		}
		else
		{
			ASSERT_OR_IGNORE(rhs_type_tag == TypeTag::Integer);

			const NumericType rhs_type_attachment = *type_attachment_from_id<NumericType>(core, rhs_type);

			ASSERT_OR_IGNORE(rhs_type_attachment.bits >= 8 && rhs_type_attachment.bits <= 64 && is_pow2(rhs_type_attachment.bits));

			ASSERT_OR_IGNORE(rhs->size * 8 == rhs_type_attachment.bits);

			const bool rhs_is_signed = rhs_type_attachment.is_signed;

			rhs_value = 0;

			memcpy(&rhs_value, get(rhs->begin), rhs->size);

			if (rhs_is_signed && rhs->size != 8 && (rhs_value & (static_cast<u64>(1) << rhs->size * 8)) != 0)
				return record_lock_type_error(core, code);
		}

		if (rhs_value > lhs->size * 8)
			return record_lock_type_error(core, code);

		pop_value(core);
		pop_value(core);

		u64 result;

		if (code->bit_shift.kind == Code2AttBitShift_Kind::Left)
			result = lhs_value << rhs_value;
		else if (lhs_is_signed)
			result = static_cast<u64>(static_cast<s64>(lhs_value) >> static_cast<s64>(rhs_value));
		else
			result = lhs_value >> rhs_value;

		push_value_raw(core, &result, lhs_size, lhs_size, false);
	}

	return true;
}



static bool evaluate_ordering(CoreData* core, Code2* code, TypeId type, TypecheckValue* lhs, TypecheckValue* rhs, WeakCompareOrdering* out) noexcept
{
	ASSERT_OR_IGNORE(lhs->size == rhs->size);

	const TypeTag type_tag = type_tag_from_id(core, type);

	const Code2AttCompare_Kind kind = code->compare.kind;

	switch (type_tag)
	{
	case TypeTag::Void:
	case TypeTag::Definition:
	case TypeTag::TypeInfo:
	case TypeTag::TypeBuilder:
	case TypeTag::Divergent:
	case TypeTag::Slice:
	case TypeTag::Array:
	case TypeTag::Composite:
	case TypeTag::TailArray:
	case TypeTag::Trait:
	case TypeTag::Self:
	{
		return record_lock_type_error(core, code);
	}

	case TypeTag::Type:
	{
		if (kind != Code2AttCompare_Kind::Equal && kind != Code2AttCompare_Kind::NotEqual)
			return record_lock_type_error(core, code);

		const TypeId lhs_value = *static_cast<TypeId*>(get(lhs->begin));

		const TypeId rhs_value = *static_cast<TypeId*>(get(rhs->begin));

		const TypeEquality equality = type_is_equal(core, lhs_value, rhs_value);

		if (equality == TypeEquality::Incomplete)
			return record_lock_type_error(core, code);

		ASSERT_OR_IGNORE(equality == TypeEquality::Equal || equality == TypeEquality::Unequal);

		// Bogus value for inequality, which is fine since we restrict the
		// allowed ops to unordered comparisons.
		*out = equality == TypeEquality::Equal
			? WeakCompareOrdering::Equal
			: WeakCompareOrdering::GreaterThan;

		return true;
	}

	case TypeTag::CompInteger:
	{
		const CompIntegerValue lhs_value = *static_cast<CompIntegerValue*>(get(lhs->begin));

		const CompIntegerValue rhs_value = *static_cast<CompIntegerValue*>(get(rhs->begin));

		*out = static_cast<WeakCompareOrdering>(comp_integer_compare(lhs_value, rhs_value));

		return true;
	}

	case TypeTag::CompFloat:
	{
		const CompFloatValue lhs_value = *static_cast<CompFloatValue*>(get(lhs->begin));

		const CompFloatValue rhs_value = *static_cast<CompFloatValue*>(get(rhs->begin));

		*out = comp_float_compare(lhs_value, rhs_value);

		return true;
	}

	case TypeTag::Boolean:
	{
		if (kind != Code2AttCompare_Kind::Equal && kind != Code2AttCompare_Kind::NotEqual)
			return record_lock_type_error(core, code);

		const bool lhs_value = *static_cast<bool*>(get(lhs->begin));

		const bool rhs_value = *static_cast<bool*>(get(rhs->begin));

		// Bogus value for inequality, which is fine since we restrict the
		// allowed ops to unordered comparisons.
		*out = lhs_value == rhs_value
			? WeakCompareOrdering::Equal
			: WeakCompareOrdering::GreaterThan;

		return true;
	}

	case TypeTag::Integer:
	{
		const byte* const lhs_value = static_cast<byte*>(get(lhs->begin));

		const byte* const rhs_value = static_cast<byte*>(get(rhs->begin));

		const u32 size = lhs->size;

		for (u32 i = 0; i != size; ++i)
		{
			const byte lhs_byte = lhs_value[size - 1  - i];

			const byte rhs_byte = rhs_value[size - 1  - i];

			if (lhs_byte < rhs_byte)
			{
				*out = WeakCompareOrdering::LessThan;

				return true;
			}
			else if (lhs_byte > rhs_byte)
			{
				*out = WeakCompareOrdering::GreaterThan;

				return true;
			}
		}

		*out = WeakCompareOrdering::Equal;

		return true;
	}

	case TypeTag::Float:
	{
		if (lhs->size == 4)
		{
			const f32 lhs_value = *static_cast<f32*>(get(lhs->begin));

			const f32 rhs_value = *static_cast<f32*>(get(rhs->begin));

			if (lhs_value < rhs_value)
				*out = WeakCompareOrdering::LessThan;
			else if (lhs_value > rhs_value)
				*out = WeakCompareOrdering::GreaterThan;
			else if (lhs_value == rhs_value)
				*out = WeakCompareOrdering::Equal;
			else
				*out = WeakCompareOrdering::Unordered;
		}
		else
		{
			ASSERT_OR_IGNORE(lhs->size == 8);

			const f64 lhs_value = *static_cast<f64*>(get(lhs->begin));

			const f64 rhs_value = *static_cast<f64*>(get(rhs->begin));

			if (lhs_value < rhs_value)
				*out = WeakCompareOrdering::LessThan;
			else if (lhs_value > rhs_value)
				*out = WeakCompareOrdering::GreaterThan;
			else if (lhs_value == rhs_value)
				*out = WeakCompareOrdering::Equal;
			else
				*out = WeakCompareOrdering::Unordered;
		}

		return true;
	}

	case TypeTag::Ptr:
	{
		const void* lhs_value = *static_cast<void**>(get(lhs->begin));

		const void* rhs_value = *static_cast<void**>(get(rhs->begin));

		if (lhs_value < rhs_value)
			*out = WeakCompareOrdering::LessThan;
		else if (lhs_value > rhs_value)
			*out = WeakCompareOrdering::GreaterThan;
		else
			*out = WeakCompareOrdering::Equal;

		return true;
	}

	case TypeTag::INVALID:
	case TypeTag::INDIRECTION:
	case TypeTag::Distinct:
	case TypeTag::Undefined:
	case TypeTag::Signature:
		; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

static bool evaluate_compare(CoreData* core, Code2* code, TypeId type, TypecheckValue* lhs, TypecheckValue* rhs, bool* out) noexcept
{
	WeakCompareOrdering ordering;

	if (!evaluate_ordering(core, code, type, lhs, rhs, &ordering))
		return false;

	switch (code->compare.kind)
	{
	case Code2AttCompare_Kind::LessThan:
	{
		*out = ordering == WeakCompareOrdering::LessThan;

		return true;
	}
	case Code2AttCompare_Kind::GreaterThan:
	{
		*out = ordering == WeakCompareOrdering::GreaterThan;

		return true;
	}

	case Code2AttCompare_Kind::LessThanOrEqual:
	{
		*out = ordering == WeakCompareOrdering::LessThan || ordering == WeakCompareOrdering::Equal;

		return true;
	}

	case Code2AttCompare_Kind::GreaterThanOrEqual:
	{
		*out = ordering == WeakCompareOrdering::GreaterThan || ordering == WeakCompareOrdering::Equal;

		return true;
	}

	case Code2AttCompare_Kind::NotEqual:
	{
		*out = ordering == WeakCompareOrdering::LessThan || ordering == WeakCompareOrdering::GreaterThan;

		return true;
	}

	case Code2AttCompare_Kind::Equal:
	{
		*out = ordering == WeakCompareOrdering::Equal;

		return true;
	}
	}

	ASSERT_UNREACHABLE;
}







static void lock_type_call_argument_default(CoreData* core, TypeId signature, u16 rank, LockTypeCallResult status) noexcept
{
	// If we already know that the call won't be evaluated, we can skip setting
	// up the argument default values.
	if (status == LockTypeCallResult::OkNonTypeEval)
		return;

	MemberInfo info;

	if (!type_member_info_by_rank(core, signature, rank, &info))
		ASSERT_UNREACHABLE;

	// Skip setting up `eval` parameters. They are baked into the de-templated
	// callee already anyways.
	if (info.is_eval)
		return;

	// This has already been ensured by `typecheck_call_arguments`.
	ASSERT_OR_IGNORE(is_some(info.type_and_value.complete.value));

	TypeMetrics metrics;

	if (!type_metrics_from_id(core, info.type_and_value.complete.type, &metrics))
		ASSERT_UNREACHABLE;

	TypecheckActivation* const activation = top_activation(core);

	ASSERT_OR_IGNORE(rank < activation->definition_count);

	TypecheckDefinition* const definition = activation->definitions + rank;
	definition->type = info.type_and_value.complete.type;
	definition->size = static_cast<u32>(metrics.size);
	definition->align_log2 = count_trailing_zeros_assume_one(metrics.align);
	definition->is_type_eval = info.is_eval;
	definition->is_mut = info.is_mut;
	definition->location_offset = 0;

	const void* const default_value = address_from_core_id(core, get(info.type_and_value.complete.value));

	definition->location_offset = push_location(core, default_value, metrics.size, metrics.align);
}

static LockTypeCallResult lock_type_call_argument_value(CoreData* core, TypeId signature, u16 rank, Code2Id argument_value_id) noexcept
{
	MemberInfo info;

	if (!type_member_info_by_rank(core, signature, rank, &info))
		ASSERT_UNREACHABLE;

	TypeMetrics metrics;

	// Skip setting up `eval` parameters. They are baked into the de-templated
	// callee already anyways and have already been locked by
	// `typecheck_call_argument_value`.
	if (info.is_eval)
		return LockTypeCallResult::OkTypeEval;

	if (!type_metrics_from_id(core, info.type_and_value.complete.type, &metrics))
		ASSERT_UNREACHABLE;

	TypecheckActivation* const activation = top_activation(core);

	ASSERT_OR_IGNORE(rank < activation->definition_count);

	TypecheckDefinition* const definition = activation->definitions + rank;
	definition->type = info.type_and_value.complete.type;
	definition->size = static_cast<u32>(metrics.size);
	definition->align_log2 = count_trailing_zeros_assume_one(metrics.align);
	definition->is_type_eval = info.is_eval;
	definition->is_mut = info.is_mut;

	Code2Block* const argument_value = code2_block_from_id(core, argument_value_id);

	if (!lock_type_block(core, argument_value, some(info.type_and_value.complete.type)))
		return LockTypeCallResult::Failure;

	return pop_value_into_definition(core, definition, info.type_and_value.complete.type, info.is_mut)
		? LockTypeCallResult::OkTypeEval
		: LockTypeCallResult::OkNonTypeEval;
}

static LockTypeCallResult lock_type_call_arguments(CoreData* core, Code2* code, TypeId signature, bool callee_is_type_evaluated) noexcept
{
	ASSERT_OR_IGNORE(code->op == Code2Op::Call || code->op == Code2Op::ImplTraitCall);

	Code2AttCall* const attachment = attachment_of<Code2AttCall>(code);

	const SignatureTypeInfo signature_info = type_signature_info_from_id(core, signature);

	ASSERT_OR_IGNORE(!signature_info.has_templated_return_type);

	ASSERT_OR_IGNORE(signature_info.templated_parameter_count == 0);

	const u32 argument_count = code->call.argument_count;

	const u32 parameter_count = signature_info.parameter_count;

	LockTypeCallResult rst = callee_is_type_evaluated && signature_info.is_func
		? LockTypeCallResult::OkTypeEval
		: LockTypeCallResult::OkNonTypeEval;

	u32 prev_rank = 0;

	for (u32 i = 0; i != argument_count; ++i)
	{
		const u32 rank = attachment->arguments[i].rank;

		for (u32 j = prev_rank; j != rank; ++j)
			lock_type_call_argument_default(core, signature, static_cast<u16>(j), rst);

		prev_rank = rank + 1;

		const LockTypeCallResult argument_rst = lock_type_call_argument_value(core, signature, static_cast<u16>(rank), attachment->arguments[i].value);

		if (argument_rst == LockTypeCallResult::Failure)
		{
			Code2Block* const value_block = code2_block_from_id(core, attachment->arguments[i].value);

			Code2* const value = code2_block_begin(value_block);

			(void) record_lock_type_error(core, value);

			return LockTypeCallResult::Failure;
		}
		else if (argument_rst == LockTypeCallResult::OkNonTypeEval)
			rst = LockTypeCallResult::OkNonTypeEval;
	}

	for (u32 i = prev_rank; i != parameter_count; ++i)
		lock_type_call_argument_default(core, signature, static_cast<u16>(i), rst);

	return rst;
}



static bool lock_type_update_to_desired(CoreData* core, Code2* code, Maybe<TypeId> expected_type) noexcept
{
	if (is_none(expected_type))
		return true;

	const TypeRelation relation = type_relation(core, code->type, get(expected_type));

	if (relation == TypeRelation::FirstConvertsToSecond)
		code->type = get(expected_type);
	else if (relation == TypeRelation::Incomplete)
		return record_lock_type_error(core, code);
	else if (relation == TypeRelation::Unrelated || relation == TypeRelation::SecondConvertsToFirst)
		return record_lock_type_error(core, code);

	ASSERT_OR_IGNORE(relation == TypeRelation::FirstConvertsToSecond || relation == TypeRelation::Equal);

	return true;
}

static bool lock_type_check_is_desired(CoreData* core, Code2* code, Maybe<TypeId> expected_type) noexcept
{
	if (is_none(expected_type))
		return true;

	const TypeRelation relation = type_relation(core, code->type, get(expected_type));

	if (relation == TypeRelation::Equal)
	{
		return true;
	}
	else if (relation == TypeRelation::FirstConvertsToSecond)
	{
		return record_lock_type_error(core, code);
	}
	else if (relation == TypeRelation::Incomplete)
	{
		return record_lock_type_error(core, code);
	}
	else
	{
		ASSERT_OR_IGNORE(relation == TypeRelation::Unrelated || relation == TypeRelation::SecondConvertsToFirst);

		return record_lock_type_error(core, code);
	}
}

static bool lock_type_single(CoreData* core, Code2Block* block, Code2* code, Maybe<TypeId> expected_type) noexcept
{
	switch (code->op)
	{
	case Code2Op::Jump:
	case Code2Op::Loop:
	{
		Code2AttJump* const attachment = attachment_of<Code2AttJump>(code);

		if (!lock_type_update_to_desired(core, code, expected_type))
			return false;

		if (code->loop.is_type_eval)
		{
			TODO("Implement type-evaluated loop conditions");
		}
		else
		{
			Code2Block* const body = code2_block_from_id(core, attachment->body);

			if (!lock_type_block(core, body, expected_type))
				return false;

			TypeMetrics metrics;

			if (!type_metrics_from_id(core, code->type, &metrics))
				return record_lock_type_error(core, code);

			push_value_empty(core, metrics.size, metrics.align, false);
		}

		return true;
	}

	case Code2Op::Builtin:
	{
		const u8 ordinal = static_cast<u8>(code->builtin.builtin);

		ASSERT_OR_IGNORE(ordinal < array_count(core->typecheck.builtin_functions));

		push_value(core, core->typecheck.builtin_functions[ordinal], false);

		if (!lock_type_update_to_desired(core, code, expected_type))
			return false;

		return true;
	}

	case Code2Op::Composite:
	{
		const Code2AttComposite* const attachment = attachment_of<Code2AttComposite>(code);

		if (is_none(expected_type))
			return record_lock_type_error(core, code);

		const TypeTag type_tag = type_tag_from_id(core, get(expected_type));

		if (type_tag != TypeTag::Composite)
			return record_lock_type_error(core, code);



		u32 dst_member_count;

		if (!type_member_count(core, get(expected_type), &dst_member_count))
			return record_lock_type_error(core, code);



		TypeMetrics metrics;

		// This can never fail, since we already check for the type being
		// incomplete in the above call to `type_member_count`.
		if (type_metrics_from_id(core, get(expected_type), &metrics))
			ASSERT_UNREACHABLE;

		byte* const composite = static_cast<byte*>(push_value_raw_uninit(core, metrics.size, metrics.align, false));

		// Initialize to some recognizable debug pattern.
		memset(composite, 0x4A, metrics.size);



		const u64 mark = temp_stack_mark(core);

		const u64 bits_word_count = (dst_member_count + 63) / 64;

		u64* const bits = static_cast<u64*>(temp_stack_alloc(core, bits_word_count * sizeof(u64), alignof(u64)));

		bool is_type_evaluable = true;

		const u32 src_member_count = attachment->header.composite.member_count;

		u32 dst_rank = 0;

		for (u32 i = 0; i != src_member_count; ++i)
		{
			if (dst_rank >= dst_member_count)
				return record_lock_type_error(core, code);

			const Code2AttComposite_Member member = attachment->members[i];

			MemberInfo info;

			if (is_some(member.name))
			{
				const MemberByNameRst rst = type_member_info_by_name(core, get(expected_type), get(member.name), &info);

				if (rst == MemberByNameRst::NotFound)
					return record_lock_type_error(core, code);

				// The result can never be `MemberByNameRst::Incomplete`, since
				// we already check for that in the above call to
				// `type_member_count`.
				ASSERT_OR_IGNORE(rst == MemberByNameRst::Ok);

				dst_rank = info.rank;
			}
			else
			{
				// This can never fail, since we already check for the type
				// being incomplete in the above call to `type_member_count`.
				if (!type_member_info_by_rank(core, get(expected_type), static_cast<u16>(dst_rank), &info))
					ASSERT_UNREACHABLE;
			}

			if (info.is_global)
				return record_lock_type_error(core, code);

			TypeMetrics member_metrics;

			if (!type_metrics_from_id(core, info.type_and_value.complete.type, &member_metrics))
				ASSERT_UNREACHABLE;

			Code2* const value = code2_from_offset(block, member.value);

			if (!lock_type_single(core, block, value, some(info.type_and_value.complete.type)))
				return false;

			if (!pop_value_into_address(core, MutRange{ composite + info.offset, member_metrics.size }))
				is_type_evaluable = false;

			u64* const bits_word = bits + (dst_rank / 64);

			const u64 bits_mask = static_cast<u64>(1) << (dst_rank % 64);

			if ((*bits_word & bits_mask) != 0)
				return record_lock_type_error(core, code);

			*bits_word |= bits_mask;

			dst_rank += 1;
		}

		for (u32 i = 0; i != bits_word_count; ++i)
		{
			u64 bits_word = bits[i];

			// Fill out-of-range bits in the last word to ignore them.
			if (i == bits_word_count - 1 && (dst_member_count % 64) != 0)
				bits_word |= (~static_cast<u64>(0)) << (dst_member_count % 64);

			while (bits_word != ~static_cast<u64>(0))
			{
				const u8 bit_index = count_trailing_ones_assume_zero(bits_word);

				bits_word |= static_cast<u64>(1) << bit_index;

				MemberInfo info;

				// This can never fail, since we already check for the type
				// being incomplete in the above call to `type_member_count`.
				if (!type_member_info_by_rank(core, get(expected_type), static_cast<u16>(i * 64 + bit_index), &info))
					ASSERT_UNREACHABLE;

				if (info.is_global)
					continue;

				if (is_none(info.type_and_value.complete.value))
					return record_lock_type_error(core, code);

				if (is_type_evaluable)
				{
					TypeMetrics member_metrics;

					if (!type_metrics_from_id(core, info.type_and_value.complete.type, &member_metrics))
						ASSERT_UNREACHABLE;

					const void* const default_value = address_from_core_id(core, get(info.type_and_value.complete.value));

					memcpy(composite + info.offset, default_value, member_metrics.size);
				}
			}
		}

		temp_stack_release(core, mark);

		if (!is_type_evaluable)
			peek_value(core)->begin = none<void*>();

		code->type = get(expected_type);

		return true;
	}

	case Code2Op::Array:
	{
		const Code2AttArray* const attachment = attachment_of<Code2AttArray>(code);

		if (is_none(expected_type))
			return record_lock_type_error(core, code);

		const TypeTag type_tag = type_tag_from_id(core, get(expected_type));

		if (type_tag != TypeTag::Array)
			return record_lock_type_error(core, code);

		const ArrayType array_type = *type_attachment_from_id<ArrayType>(core, get(expected_type));

		if (array_type.element_count != code->array.element_count)
			return record_lock_type_error(core, code);

		TypeMetrics element_metrics;

		if (!type_metrics_from_id(core, array_type.element_type, &element_metrics))
			return record_lock_type_error(core, code);

		const u32 element_count = code->array.element_count;

		byte* const array = static_cast<byte*>(push_value_raw_uninit(core, element_metrics.stride * element_count, element_metrics.size, false));

		bool is_type_evaluable = true;

		for (u32 i = 0; i != element_count; ++i)
		{
			Code2* const value = code2_from_offset(block, attachment->elements[i].value);

			if (!lock_type_single(core, block, value, some(array_type.element_type)))
				return false;

			if (!pop_value_into_address(core, MutRange{ array + i * element_metrics.stride, element_metrics.size }))
				is_type_evaluable = false;
		}

		if (!is_type_evaluable)
			peek_value(core)->begin = none<void*>();

		code->type = get(expected_type);

		return true;
	}

	case Code2Op::Definition:
	{
		Code2AttDefinition* const attachment = attachment_of<Code2AttDefinition>(code);

		TypeId defined_type;

		Maybe<CoreId> defined_value;

		bool is_type_evaluable = true;

		if (is_some(attachment->type))
		{
			const TypeId type_type = type_create_simple(core, TypeTag::Type);

			Code2* const type = code2_from_offset(block, get(attachment->type));

			if (!lock_type_single(core, block, type, some(type_type)))
				return false;

			if (!pop_value_as(core, &defined_type))
				return record_lock_type_error(core, code);

			if (is_some(attachment->value))
			{
				Code2* const value = code2_from_offset(block, get(attachment->value));

				if (!lock_type_single(core, block, value, some(defined_type)))
					return false;

				defined_value = pop_value_into_core_id(core);

				if (is_none(defined_value))
					is_type_evaluable = false;
			}
			else
			{
				defined_value = none<CoreId>();
			}
		}
		else
		{
			ASSERT_OR_IGNORE(is_some(attachment->value));

			Code2* const value = code2_from_offset(block, get(attachment->value));

			if (!lock_type_single(core, block, value, none<TypeId>()))
				return false;

			defined_type = value->type;

			defined_value = pop_value_into_core_id(core);

			if (is_none(defined_value))
				is_type_evaluable = false;
		}

		if (is_type_evaluable)
		{
			DefinitionValue result{};
			result.name = attachment->name;
			result.is_mut = attachment->header.definition.is_mut;
			result.is_pub = attachment->header.definition.is_pub;
			result.type = defined_type;
			result.value = defined_value;

			push_value(core, result, false);
		}
		else
		{
			push_value_empty(core, sizeof(DefinitionValue), alignof(DefinitionValue), false);
		}

		if (!lock_type_update_to_desired(core, code, expected_type))
			return false;

		return true;
	}

	case Code2Op::MakeLocation:
	{
		Code2AttUnaryOp* const attachment = attachment_of<Code2AttUnaryOp>(code);

		Code2* const operand = code2_from_offset(block, attachment->operand);

		if (!lock_type_single(core, block, operand, expected_type))
			return false;

		promote_value_to_location(core);

		code->type = operand->type;

		return true;
	}

	case Code2Op::Void:
	{
		push_value_raw_uninit(core, 0, 1, false);

		if (!lock_type_update_to_desired(core, code, expected_type))
			return false;

		return true;
	}

	case Code2Op::If:
	{
		Code2AttIf* const attachment = attachment_of<Code2AttIf>(code);

		if (!lock_type_update_to_desired(core, code, expected_type))
			return false;

		if (code->if_.is_type_eval)
		{
			const Code2Id taken_branch_id = code->if_.evaluated_condition
				? attachment->consequent
				: attachment->alternative;

			Code2Block* const taken_branch = code2_block_from_id(core, taken_branch_id);

			if (!lock_type_block(core, taken_branch, some(code->type)))
				return false;
		}
		else
		{
			Code2* const condition = code2_from_offset(block, attachment->condition);

			const TypeId bool_type = type_create_simple(core, TypeTag::Boolean);

			if (!lock_type_single(core, block, condition, some(bool_type)))
				return false;

			Code2Block* const consequent = code2_block_from_id(core, attachment->consequent);

			if (!lock_type_block(core, consequent, some(code->type)))
				return false;

			Code2Block* const alternative = code2_block_from_id(core, attachment->alternative);

			if (!lock_type_block(core, alternative, some(code->type)))
				return false;
		}

		return true;
	}

	case Code2Op::Func:
	{
		Code2AttFunc* const attachment = attachment_of<Code2AttFunc>(code);

		if (!lock_type_update_to_desired(core, code, expected_type))
			return false;

		const TypeId signature_type = code->type;

		CallableValue value{};
		value.signature_type = signature_type;
		value.body = attachment->body;

		push_value(core, value, false);

		return true;
	}

	case Code2Op::Signature:
	{
		Code2AttSignature* const attachment = attachment_of<Code2AttSignature>(code);

		if (!lock_type_update_to_desired(core, code, expected_type))
			return false;

		const u8 parameter_count = code->signature.parameter_count;

		const u32 closed_count = code->signature.closed_count;

		Code2Offset* const closed = reinterpret_cast<Code2Offset*>(attachment->parameters + parameter_count);

		const TypeId signature = type_create_signature(core, code->signature.is_func, parameter_count);

		const TypeId type_type = type_create_simple(core, TypeTag::Type);

		bool is_type_evaluable = true;

		// Evaluate the parameters if possible, and handle templated
		// parameters.

		for (u32 i = 0; i != parameter_count; ++i)
		{
			const Code2AttSignature_Parameter* const parameter = attachment->parameters + i;

			SignatureParameterInit init{};
			init.name = parameter->name;
			init.is_templated = parameter->is_templated;
			init.is_eval = parameter->is_eval;
			init.is_mut = parameter->is_mut;

			if (parameter->is_templated)
			{
				init.type_and_value.pending.type = some(parameter->templated.type);
				init.type_and_value.pending.value = parameter->templated.default_value;
			}
			else
			{
				Code2* const type = code2_from_offset(block, parameter->complete.type);

				if (!lock_type_single(core, block, type, some(type_type)))
					return false;

				TypeId parameter_type = TypeId::INVALID;

				if (!pop_value_as(core, &parameter_type))
				{
					if (is_some(parameter->templated.default_value))
						return record_lock_type_error(core, code);
					else
						is_type_evaluable = false;
				}

				Maybe<CoreId> default_value_id;

				if (is_some(parameter->templated.default_value))
				{
					Code2* const default_value = code2_from_offset(block, get(parameter->complete.default_value));

					if (!lock_type_single(core, block, default_value, some(parameter_type)))
						return false;

					default_value_id = pop_value_into_core_id(core);

					if (is_none(default_value_id))
						is_type_evaluable = false;
				}
				else
				{
					default_value_id = none<CoreId>();
				}

				init.type_and_value.complete.type = parameter_type;
				init.type_and_value.complete.value = default_value_id;
			}

			if (is_type_evaluable)
				type_add_signature_parameter(core, signature, init);
		}

		// Evaluate the return type if possible, or handle the templated return
		// type.

		const bool has_templated_return_type = code->signature.has_templated_return_type;

		SignatureSealInfo seal{};
		seal.has_templated_return_type = has_templated_return_type;
		seal.is_variadic = code->signature.is_variadic;

		if (has_templated_return_type)
		{
			seal.return_type.templated.initializer = attachment->return_type.templated;
		}
		else
		{
			Code2* const return_type = code2_from_offset(block, attachment->return_type.complete);

			if (!lock_type_single(core, block, return_type, some(type_type)))
				return false;

			TypeId return_type_type;

			if (pop_value_as(core, &return_type_type))
				seal.return_type.complete.type_id = return_type_type;
			else
				is_type_evaluable = false;
		}

		// Evaluate closed-over values and create a closure if necessary.

		for (u32 i = 0; i != closed_count; ++i)
		{
			Code2* const curr = code2_from_offset(block, closed[i]);

			if (!lock_type_single(core, block, curr, none<TypeId>()))
				return false;

			TypecheckValue* const closed_value = peek_value(core);

			if (is_none(closed_value->begin))
				is_type_evaluable = false;
		}

		if (closed_count != 0 && is_type_evaluable)
		{
			const Maybe<ClosureId> closure = pop_values_into_closure(core, block, Range{ closed, closed_count });

			if (is_none(closure))
				return false;

			seal.closure_id = closure;
		}
		else
		{
			for (u32 i = 0; i != closed_count; ++i)
				pop_value(core);

			seal.closure_id = none<ClosureId>();
		}

		// Create the actual signature value (if possible) and push it

		if (is_type_evaluable)
		{
			const TypeId sealed_signature = type_seal_signature(core, signature, seal);

			push_value(core, sealed_signature, false);
		}
		else
		{
			push_value_empty(core, sizeof(TypeId), alignof(TypeId), false);
		}

		return true;
	}

	case Code2Op::Trait:
	{
		TODO("Implement `lock_type_single(%)`.", tag_name(code->op));
	}

	case Code2Op::Impl:
	{
		TODO("Implement `lock_type_single(%)`.", tag_name(code->op));
	}

	case Code2Op::ImplTraitCall:
	{
		TODO("Implement `lock_type_single(%)`.", tag_name(code->op));
	}

	case Code2Op::Self:
	{
		TODO("Implement `lock_type_single(%)`.", tag_name(code->op));
	}

	case Code2Op::Unreachable:
	case Code2Op::Undefined:
	{
		if (!lock_type_update_to_desired(core, code, expected_type))
			return false;

		TypeMetrics metrics;

		if (!type_metrics_from_id(core, code->type, &metrics))
			return record_lock_type_error(core, code);

		void* const value = push_value_raw_uninit(core, metrics.size, metrics.align, false);

		memset(value, 0x4A, metrics.size);

		return true;
	}

	case Code2Op::LocalValue:
	case Code2Op::LocalLocation:
	{
		if (code->op == Code2Op::LocalValue)
		{
			if (!lock_type_update_to_desired(core, code, expected_type))
				return false;
		}
		else
		{
			if (!lock_type_check_is_desired(core, code, expected_type))
				return false;
		}

		const u8 out = code->local_ref.out;

		const u16 rank = code->local_ref.rank;

		TypecheckActivation* const activation = get_activation(core, out);

		ASSERT_OR_IGNORE(rank < activation->definition_count);

		TypecheckDefinition* const definition = activation->definitions + rank;

		TypeMetrics metrics;

		if (!type_metrics_from_id(core, code->type, &metrics))
			return record_lock_type_error(core, code);

		void* const begin = static_cast<byte*>(base(&core->typecheck.locations)) + definition->location_offset;

		if (!definition->is_type_eval)
			push_value_empty(core, metrics.size, metrics.align, definition->is_mut);
		else if (code->op == Code2Op::LocalValue)
			push_value_raw(core, begin, metrics.size, metrics.align, false);
		else
			push_value_location(core, begin, metrics.size, metrics.align, definition->is_mut);

		return true;
	}

	case Code2Op::GlobalValue:
	case Code2Op::GlobalLocation:
	{
		if (code->op == Code2Op::GlobalValue)
		{
			if (!lock_type_update_to_desired(core, code, expected_type))
				return false;
		}
		else
		{
			if (!lock_type_check_is_desired(core, code, expected_type))
				return false;
		}

		MemberInfo info;

		if (!type_member_info_by_rank(core, core->typecheck.active_file_type, code->global_ref.rank, &info))
			ASSERT_UNREACHABLE;

		TypeMetrics metrics;

		if (!type_metrics_from_id(core, code->type, &metrics))
			return record_lock_type_error(core, code);

		void* const begin = address_from_core_id(core, get(info.type_and_value.complete.value));

		if (code->op == Code2Op::GlobalValue)
			push_value_raw(core, begin, metrics.size, metrics.align, false);
		else
			push_value_location(core, begin, metrics.size, metrics.align, info.is_mut);

		return true;
	}

	case Code2Op::ClosedValue:
	case Code2Op::ClosedLocation:
	{
		TODO("Implement `lock_type_single(%)`.", tag_name(code->op));
	}

	case Code2Op::TraitArgumentValue:
	case Code2Op::TraitArgumentLocation:
	{
		TODO("Implement `lock_type_single(%)`.", tag_name(code->op));
	}

	case Code2Op::Slice:
	{
		Code2AttSlice* const attachment = attachment_of<Code2AttSlice>(code);

		if (!lock_type_update_to_desired(core, code, expected_type))
			return false;

		Code2* const sliced = code2_from_offset(block, attachment->sliced);

		if (!lock_type_single(core, block, sliced, none<TypeId>()))
			return false;

		TypecheckValue* const sliced_value = peek_value(core);

		bool is_type_evaluable = is_some(sliced_value->begin);

		const TypeId sliced_type = sliced->type;

		const TypeTag sliced_type_tag = type_tag_from_id(core, sliced_type);

		void* base_ptr = nullptr;
		bool has_max_index = false;
		u64 max_index = 0;

		if (sliced_type_tag == TypeTag::Array)
		{
			const ArrayType sliced_type_attachment = *type_attachment_from_id<ArrayType>(core, sliced_type);

			if (is_type_evaluable)
				base_ptr = get(sliced_value->begin);

			has_max_index = true;
			max_index = sliced_type_attachment.element_count;
		}
		else if (sliced_type_tag == TypeTag::Slice)
		{
			if (is_type_evaluable)
			{
				const SliceValue slice_value = *reinterpret_cast<SliceValue*>(get(sliced_value->begin));

				base_ptr = slice_value.begin;
				has_max_index = true;
				max_index = slice_value.count;
			}
		}
		else if (sliced_type_tag == TypeTag::Ptr)
		{
			const ReferenceType sliced_type_attachment = *type_attachment_from_id<ReferenceType>(core, sliced_type);

			if (is_none(attachment->end))
				return record_lock_type_error(core, code);

			if (!sliced_type_attachment.is_multi)
				return record_lock_type_error(core, code);

			if (sliced_type_attachment.is_opt)
				return record_lock_type_error(core, code);

			if (is_type_evaluable)
				base_ptr = *reinterpret_cast<void**>(get(sliced_value->begin));
		}

		u64 begin_value;

		if (is_some(attachment->begin))
		{
			Code2* const begin = code2_from_offset(block, get(attachment->begin));

			if (!lock_type_single(core, block, begin, none<TypeId>()))
				return false;

			const TypeTag begin_type_tag = type_tag_from_id(core, begin->type);

			if (begin_type_tag != TypeTag::CompInteger && begin_type_tag != TypeTag::Integer)
				return record_lock_type_error(core, code);

			const PopTypeResult rst = pop_int_to_u64(core, begin->type, &begin_value);

			if (rst == PopTypeResult::NoValue)
				is_type_evaluable = false;
			else if (rst == PopTypeResult::Failure)
				return false;
			else
				ASSERT_OR_IGNORE(rst == PopTypeResult::Ok);
		}
		else
		{
			begin_value = 0;
		}

		u64 end_value;

		if (is_some(attachment->end))
		{
			Code2* const end = code2_from_offset(block, get(attachment->end));

			if (!lock_type_single(core, block, end, none<TypeId>()))
				return false;

			const TypeTag end_type_tag = type_tag_from_id(core, end->type);

			if (end_type_tag != TypeTag::CompInteger && end_type_tag != TypeTag::Integer)
				return record_lock_type_error(core, code);

			const PopTypeResult rst = pop_int_to_u64(core, end->type, &end_value);

			if (rst == PopTypeResult::NoValue)
			{
				is_type_evaluable = false;
			}
			else if (rst == PopTypeResult::Failure)
			{
				return false;
			}
			else
			{
				ASSERT_OR_IGNORE(rst == PopTypeResult::Ok);

				if (has_max_index && max_index < end_value)
					return record_lock_type_error(core, code);
			}
		}
		else if (sliced_type_tag == TypeTag::Array || sliced_type_tag == TypeTag::Slice)
		{
			end_value = max_index;
		}
		else
		{
			ASSERT_OR_IGNORE(sliced_type_tag == TypeTag::Ptr);

			return record_lock_type_error(core, code);
		}

		if (is_type_evaluable)
		{
			const ReferenceType* const slice_type_attach = type_attachment_from_id<ReferenceType>(core, code->type);

			TypeMetrics element_metrics;

			if (!type_metrics_from_id(core, slice_type_attach->referenced_type_id, &element_metrics))
				return record_lock_type_error(core, code);

			SliceValue result{};
			result.begin = static_cast<byte*>(base_ptr) + element_metrics.stride * begin_value;
			result.count = end_value - begin_value;

			push_value(core, result, false);
		}
		else
		{
			push_value_empty(core, sizeof(SliceValue), alignof(SliceValue), false);
		}

		return true;
	}

	case Code2Op::Integer16:
	{
		if (!lock_type_update_to_desired(core, code, expected_type))
			return false;

		const TypeId type = code->type;

		const TypeTag type_tag = type_tag_from_id(core, type);

		if (type_tag == TypeTag::CompInteger)
		{
			const CompIntegerValue value = comp_integer_from_u64(code->integer16.value);

			push_value(core, value, false);
		}
		else
		{
			ASSERT_OR_IGNORE(type_tag == TypeTag::Integer);

			const NumericType type_attachment = *type_attachment_from_id<NumericType>(core, type);

			ASSERT_OR_IGNORE(type_attachment.bits >= 8 && type_attachment.bits <= 64 && is_pow2(type_attachment.bits));

			const u64 value = code->integer16.value;

			const u64 mask = (~static_cast<u64>(0)) >> (64 - type_attachment.bits - static_cast<u8>(type_attachment.is_signed));

			if ((value & ~mask) != 0)
				return record_lock_type_error(core, code);

			push_value_raw(core, &value, type_attachment.bits / 8, type_attachment.bits / 8, false);
		}

		return true;
	}

	case Code2Op::Integer64:
	{
		Code2AttInteger64* const attachment = attachment_of<Code2AttInteger64>(code);

		if (!lock_type_update_to_desired(core, code, expected_type))
			return false;

		const TypeId type = code->type;

		const TypeTag type_tag = type_tag_from_id(core, type);

		CompIntegerValue raw_value;

		memcpy(&raw_value, &attachment->value_lo, sizeof(CompIntegerValue));

		if (type_tag == TypeTag::CompInteger)
		{
			push_value(core, raw_value, false);
		}
		else
		{
			ASSERT_OR_IGNORE(type_tag == TypeTag::Integer);

			const NumericType type_attachment = *type_attachment_from_id<NumericType>(core, type);

			ASSERT_OR_IGNORE(type_attachment.bits >= 8 && type_attachment.bits <= 64 && is_pow2(type_attachment.bits));

			u64 value;

			if (!u64_from_comp_integer(raw_value, 64, &value))
				return record_lock_type_error(core, code);

			const u64 mask = (~static_cast<u64>(0)) >> (64 + static_cast<u8>(type_attachment.is_signed) - type_attachment.bits);

			if ((value & ~mask) != 0)
				return record_lock_type_error(core, code);

			push_value_raw(core, &value, type_attachment.bits / 8, type_attachment.bits / 8, false);
		}

		return true;
	}

	case Code2Op::Float:
	{
		TODO("Implement `lock_type_single(%)`.", tag_name(code->op));
	}

	case Code2Op::String:
	{
		Code2AttString* const attachment = attachment_of<Code2AttString>(code);

		if (!lock_type_update_to_desired(core, code, expected_type))
			return false;

		void* const begin = address_from_core_id(core, attachment->data);

		push_value_raw(core, begin, attachment->size, 1, false);

		return true;
	}

	case Code2Op::Call:
	{
		Code2AttCall* const attachment = attachment_of<Code2AttCall>(code);

		if (!lock_type_update_to_desired(core, code, expected_type))
			return false;

		Code2* const callee = code2_from_offset(block, attachment->callee);

		if (!lock_type_single(core, block, callee, none<TypeId>()))
			return false;

		CallableValue callee_value;

		const bool callee_is_type_evaluated = pop_value_as(core, &callee_value);

		const TypeId callee_type = callee->type;

		const LockTypeCallResult setup_result = lock_type_call_arguments(core, code, callee_type, callee_is_type_evaluated);

		if (setup_result == LockTypeCallResult::Failure)
		{
			return false;
		}
		else if (setup_result == LockTypeCallResult::OkTypeEval)
		{
			TODO("Call type-evaluable functions through a typecheck cache.");
		}
		else
		{
			TypeMetrics metrics;

			if (!type_metrics_from_id(core, code->type, &metrics))
				return record_lock_type_error(core, code);

			push_value_empty(core, metrics.size, metrics.align, false);
		}

		return true;
	}

	case Code2Op::TypeTailArray:
	{
		TODO("Implement `lock_type_single(%)`.", tag_name(code->op));
	}

	case Code2Op::TypeSlice:
	case Code2Op::TypePtr:
	{
		Code2AttUnaryOp* const attachment = attachment_of<Code2AttUnaryOp>(code);

		if (!lock_type_update_to_desired(core, code, expected_type))
			return false;

		Code2* const operand = code2_from_offset(block, attachment->operand);

		const TypeId type_type = type_create_simple(core, TypeTag::Type);

		if (!lock_type_single(core, block, operand, some(type_type)))
			return false;

		TypeId element_type;
		
		if (pop_value_as(core, &element_type))
		{
			ReferenceType result_attachment{};
			result_attachment.referenced_type_id = element_type;
			result_attachment.is_opt = code->reference_type.is_opt;
			result_attachment.is_multi = code->reference_type.is_multi;
			result_attachment.is_mut = code->reference_type.is_mut;

			TypeTag result_tag;
			
			if (code->op == Code2Op::TypeSlice)
				result_tag = TypeTag::Slice;
			else if (code->op == Code2Op::TypePtr)
				result_tag = TypeTag::Ptr;
			else if (code->op == Code2Op::Distinct)
				result_tag = TypeTag::Distinct;
			else
				ASSERT_UNREACHABLE;

			const TypeId result = type_create_reference(core, result_tag, result_attachment);

			push_value(core, result, false);
		}
		else
		{
			push_value_empty(core, sizeof(TypeId), alignof(TypeId), false);
		}

		return true;
	}

	case Code2Op::Eval:
	{
		TODO("Implement `lock_type_single(%)`.", tag_name(code->op));
	}

	case Code2Op::Defer:
	{
		TODO("Implement `lock_type_single(%)`.", tag_name(code->op));
	}

	case Code2Op::Distinct:
	{
		TODO("Implement `lock_type_single(%)`.", tag_name(code->op));
	}

	case Code2Op::Address:
	{
		Code2AttUnaryOp* const attachment = attachment_of<Code2AttUnaryOp>(code);

		if (!lock_type_update_to_desired(core, code, expected_type))
			return false;

		Code2* const operand = code2_from_offset(block, attachment->operand);

		const ReferenceType* const type_attachment = type_attachment_from_id<ReferenceType>(core, code->type);

		if (!lock_type_single(core, block, operand, some(type_attachment->referenced_type_id)))
			return false;

		const Maybe<void*> address = peek_value(core)->begin;

		pop_value(core);

		if (is_some(address))
			push_value(core, get(address), false);
		else
			push_value_empty(core, sizeof(void*), alignof(void*), false);

		return true;
	}

	case Code2Op::Dereference:
	{
		Code2AttUnaryOp* const attachment = attachment_of<Code2AttUnaryOp>(code);

		if (!lock_type_update_to_desired(core, code, expected_type))
			return false;

		Code2* const operand = code2_from_offset(block, attachment->operand);

		const TypeId dereferenced_type = operand->type;

		ReferenceType dereferenced_type_attachment = *type_attachment_from_id<ReferenceType>(core, dereferenced_type);

		dereferenced_type_attachment.referenced_type_id = code->type;

		const TypeId expected_dereferenced_type = type_create_reference(core, TypeTag::Ptr, dereferenced_type_attachment);

		if (!lock_type_single(core, block, operand, some(expected_dereferenced_type)))
			return false;

		TypeMetrics metrics;

		if (!type_metrics_from_id(core, code->type, &metrics))
			return record_lock_type_error(core, code);

		void* address;

		if (pop_value_as(core, &address))
			push_value_raw(core, address, metrics.size, metrics.align, dereferenced_type_attachment.is_mut);
		else
			push_value_empty(core, metrics.size, metrics.align, dereferenced_type_attachment.is_mut);

		return true;
	}

	case Code2Op::BitNot:
	case Code2Op::Negate:
	case Code2Op::UnaryPlus:
	{
		Code2AttUnaryOp* const attachment = attachment_of<Code2AttUnaryOp>(code);

		if (!lock_type_update_to_desired(core, code, expected_type))
			return false;

		const TypeId type = code->type;

		const TypeTag type_tag = type_tag_from_id(core, type);

		if (type_tag != TypeTag::CompInteger && type_tag != TypeTag::Integer && type_tag != TypeTag::CompFloat && type_tag != TypeTag::Float)
			return record_lock_type_error(core, code);

		if (type_tag != TypeTag::CompInteger && type_tag != TypeTag::Integer && code->op == Code2Op::BitNot)
			return record_lock_type_error(core, code);

		Code2* const operand = code2_from_offset(block, attachment->operand);

		if (!lock_type_single(core, block, operand, some(type)))
			return false;

		TypecheckValue* const operand_value = peek_value(core);

		if (is_none(operand_value->begin))
		{
			// We aren't modifying the type at all, so we can just pass the
			// unevaluated value straight through.
		}
		if (type_tag == TypeTag::CompInteger)
		{
			CompIntegerValue comp_integer_value;

			if (!pop_value_as(core, &comp_integer_value))
				ASSERT_UNREACHABLE;

			if (code->op == Code2Op::BitNot)
				comp_integer_value = comp_integer_bit_not(comp_integer_value);
			else if (code->op == Code2Op::Negate)
				comp_integer_value = comp_integer_neg(comp_integer_value);
			else
				ASSERT_OR_IGNORE(code->op == Code2Op::UnaryPlus);

			push_value(core, comp_integer_value, false);
		}
		else if (type_tag == TypeTag::Integer)
		{
			u64 bits = 0;

			const u64 size = operand_value->size;

			ASSERT_OR_IGNORE(size >= 1 && size <= 8 && is_pow2(size));

			memcpy(&bits, get(operand_value->begin), size);

			if (code->op == Code2Op::BitNot)
				bits = ~bits;
			else if (code->op == Code2Op::Negate)
				bits = static_cast<u64>(-static_cast<s64>(bits));
			else
				ASSERT_OR_IGNORE(code->op == Code2Op::UnaryPlus);

			push_value_raw(core, &bits, size, size, false);
		}
		else if (type_tag == TypeTag::CompFloat)
		{
			CompFloatValue comp_float_value;

			if (!pop_value_as(core, &comp_float_value))
				ASSERT_UNREACHABLE;

			if (code->op == Code2Op::Negate)
				comp_float_value = comp_float_neg(comp_float_value);
			else
				ASSERT_OR_IGNORE(code->op == Code2Op::UnaryPlus);

			push_value(core, comp_float_value, false);
		}
		else if (type_tag == TypeTag::Float)
		{
			const u64 size = operand_value->size;

			if (size == 4)
			{
				f32 f32_value;

				if (!pop_value_as(core, &f32_value))
					ASSERT_UNREACHABLE;

				if (code->op == Code2Op::Negate)
					f32_value = -f32_value;
				else
					ASSERT_OR_IGNORE(code->op == Code2Op::UnaryPlus);

				push_value(core, f32_value, false);
			}
			else
			{
				ASSERT_OR_IGNORE(size == 8);

				f64 f64_value;

				if (!pop_value_as(core, &f64_value))
					ASSERT_UNREACHABLE;

				if (code->op == Code2Op::Negate)
					f64_value = -f64_value;
				else
					ASSERT_OR_IGNORE(code->op == Code2Op::UnaryPlus);

				push_value(core, f64_value, false);
			}
		}

		return true;
	}

	case Code2Op::LogicalNot:
	{
		Code2AttUnaryOp* const attachment = attachment_of<Code2AttUnaryOp>(code);

		if (!lock_type_update_to_desired(core, code, expected_type))
			return false;

		ASSERT_OR_IGNORE(type_tag_from_id(core, code->type) == TypeTag::Boolean);

		Code2* const operand = code2_from_offset(block, attachment->operand);

		if (!lock_type_single(core, block, operand, some(code->type)))
			return false;

		bool operand_value;

		if (pop_value_as(core, &operand_value))
			push_value(core, !operand_value, false);
		else
			push_value_empty(core, 1, 1, false);

		return true;
	}

	case Code2Op::BinaryArithmetic:
	{
		Code2AttBinaryOp* const attachment = attachment_of<Code2AttBinaryOp>(code);

		if (!lock_type_update_to_desired(core, code, expected_type))
			return false;

		const TypeId type = code->type;

		Code2* const lhs = code2_from_offset(block, attachment->lhs);

		if (!lock_type_single(core, block, lhs, some(type)))
			return false;

		TypecheckValue* const lhs_value = peek_value(core);

		Code2* const rhs = code2_from_offset(block, attachment->rhs);

		if (!lock_type_single(core, block, rhs, some(type)))
			return false;

		TypecheckValue* const rhs_value = peek_value(core);

		const bool is_type_evaluable = is_some(lhs_value->begin) && is_some(rhs_value->begin);

		if (is_type_evaluable)
		{
			if (!evaluate_binary_arithmetic_op(core, code, type, lhs_value, rhs_value))
				return false;
		}
		else
		{
			const u64 size = lhs_value->size;

			const u64 align = static_cast<u64>(1) << lhs_value->align_log2;

			pop_value(core);
			pop_value(core);

			push_value_empty(core, size, align, false);
		}

		return true;
	}

	case Code2Op::BitShift:
	{
		Code2AttBinaryOp* const attachment = attachment_of<Code2AttBinaryOp>(code);

		if (!lock_type_update_to_desired(core, code, expected_type))
			return false;

		const TypeId type = code->type;

		const TypeTag type_tag = type_tag_from_id(core, type);

		if (type_tag != TypeTag::CompInteger && type_tag != TypeTag::Integer)
			return record_lock_type_error(core, code);

		Code2* const lhs = code2_from_offset(block, attachment->lhs);

		if (!lock_type_single(core, block, lhs, some(type)))
			return false;

		TypecheckValue* const lhs_value = peek_value(core);

		Code2* const rhs = code2_from_offset(block, attachment->rhs);

		// Do not constrain the right-hand-side of shifts too much.
		if (!lock_type_single(core, block, rhs, none<TypeId>()))
			return false;

		const TypeId shift_type = rhs->type;

		const TypeTag shift_type_tag = type_tag_from_id(core, shift_type);

		if (shift_type_tag != TypeTag::CompInteger && shift_type_tag != TypeTag::Integer)
			return record_lock_type_error(core, rhs);

		TypecheckValue* const rhs_value = peek_value(core);

		const bool is_type_evaluable = is_some(lhs_value->begin) && is_some(rhs_value->begin);

		if (is_type_evaluable)
		{
			if (!evaluate_shift(core, code, type, shift_type, lhs_value, rhs_value))
				return false;
		}
		else
		{
			const u64 size = lhs_value->size;

			const u64 align = static_cast<u64>(1) << lhs_value->align_log2;

			pop_value(core);
			pop_value(core);

			push_value_empty(core, size, align, false);
		}

		return true;
	}

	case Code2Op::TypeArray:
	{
		Code2AttBinaryOp* const attachment = attachment_of<Code2AttBinaryOp>(code);

		if (!lock_type_update_to_desired(core, code, expected_type))
			return false;

		Code2* const lhs = code2_from_offset(block, attachment->lhs);

		if (!lock_type_single(core, block, lhs, none<TypeId>()))
			return false;

		const TypeTag lhs_type_tag = type_tag_from_id(core, lhs->type);

		if (lhs_type_tag != TypeTag::CompInteger && lhs_type_tag != TypeTag::Integer)
			return record_lock_type_error(core, lhs);

		u64 element_count;

		const PopTypeResult rst = pop_int_to_u64(core, lhs->type, &element_count);

		if (rst == PopTypeResult::Failure)
			return false;

		ASSERT_OR_IGNORE(rst == PopTypeResult::Ok || rst == PopTypeResult::NoValue);

		bool is_type_evaluable = rst == PopTypeResult::Ok;

		Code2* const rhs = code2_from_offset(block, attachment->rhs);

		const TypeId type_type = type_create_simple(core, TypeTag::Type);

		if (!lock_type_single(core, block, rhs, some(type_type)))
			return false;

		TypeId element_type;

		if (!pop_value_as(core, &element_type))
			is_type_evaluable = false;

		if (is_type_evaluable)
		{
			const TypeId array_type = type_create_array(core, ArrayType{ element_count, element_type });

			push_value(core, array_type, false);
		}
		else
		{
			push_value_empty(core, sizeof(TypeId), alignof(TypeId), false);
		}

		return true;
	}

	case Code2Op::Index:
	{
		Code2AttBinaryOp* const attachment = attachment_of<Code2AttBinaryOp>(code);

		if (!lock_type_update_to_desired(core, code, expected_type))
			return false;

		Code2* const lhs = code2_from_offset(block, attachment->lhs);

		if (!lock_type_single(core, block, lhs, none<TypeId>()))
			return false;

		const TypeId lhs_type = lhs->type;

		const TypeTag lhs_type_tag = type_tag_from_id(core, lhs_type);

		TypecheckValue* const lhs_value = peek_value(core);

		bool lhs_is_type_evaluable = is_some(lhs_value->begin);

		bool has_max_index;
		u64 max_index;
		TypeId element_type;
		Maybe<void*> base_address = none<void*>();
		bool is_mut;

		if (lhs_type_tag == TypeTag::Array)
		{
			const ArrayType lhs_type_attachment = *type_attachment_from_id<ArrayType>(core, lhs_type);

			has_max_index = true;
			max_index = lhs_type_attachment.element_count;
			element_type = lhs_type_attachment.element_type;
			base_address = lhs_value->begin;
			is_mut = lhs_value->is_mut;
		}
		else if (lhs_type_tag == TypeTag::Slice)
		{
			const ReferenceType lhs_type_attachment = *type_attachment_from_id<ReferenceType>(core, lhs_type);

			if (lhs_is_type_evaluable)
			{
				const SliceValue slice = *reinterpret_cast<SliceValue*>(get(lhs_value->begin));

				has_max_index = true;
				max_index = slice.count;
				element_type = lhs_type_attachment.referenced_type_id;
				base_address = some(slice.begin);
				is_mut = lhs_type_attachment.is_mut;
			}
			else
			{
				has_max_index = false;
				max_index = 0;
				element_type = lhs_type_attachment.referenced_type_id;
				base_address = none<void*>();
				is_mut = lhs_type_attachment.is_mut;
			}
		}
		else if (lhs_type_tag == TypeTag::Ptr)
		{
			const ReferenceType lhs_type_attachment = *type_attachment_from_id<ReferenceType>(core, lhs_type);

			has_max_index = false;
			max_index = 0;
			element_type = lhs_type_attachment.referenced_type_id;
			base_address = is_some(lhs_value->begin) ? some(*static_cast<void**>(get(lhs_value->begin))) : none<void*>();
			is_mut = lhs_type_attachment.is_mut;
		}
		else
		{
			return record_lock_type_error(core, code);
		}

		Code2* const rhs = code2_from_offset(block, attachment->rhs);

		if (!lock_type_single(core, block, rhs, none<TypeId>()))
			return false;

		const TypeTag rhs_type_tag = type_tag_from_id(core, rhs->type);

		if (rhs_type_tag != TypeTag::CompInteger && rhs_type_tag != TypeTag::Integer)
			return record_lock_type_error(core, code);

		u64 index;

		const PopTypeResult rst = pop_int_to_u64(core, rhs->type, &index);

		if (rst == PopTypeResult::Failure)
			return false;

		ASSERT_OR_IGNORE(rst == PopTypeResult::Ok || rst == PopTypeResult::NoValue);

		TypeMetrics element_metrics;

		if (!type_metrics_from_id(core, element_type, &element_metrics))
			ASSERT_UNREACHABLE;

		if (lhs_is_type_evaluable && rst == PopTypeResult::Ok)
		{
			if (has_max_index && max_index <= index)
				return record_lock_type_error(core, code);

			u64 offset;

			if (!mul_checked_u64(index, element_metrics.stride, &offset))
				return record_lock_type_error(core, code);

			u64 element_address_bits;

			if (!add_checked_u64(reinterpret_cast<u64>(get(base_address)), offset, &element_address_bits))
				return record_lock_type_error(core, code);

			push_value_location(core, reinterpret_cast<void*>(element_address_bits), element_metrics.size, element_metrics.align, is_mut);
		}
		else
		{
			push_value_empty(core, element_metrics.size, element_metrics.align, false);
		}

		return true;
	}

	case Code2Op::LogicalAnd:
	case Code2Op::LogicalOr:
	{
		Code2AttLogicalOp* const attachment = attachment_of<Code2AttLogicalOp>(code);

		if (!lock_type_update_to_desired(core, code, expected_type))
			return false;

		Code2* const lhs = code2_from_offset(block, attachment->lhs);

		const TypeId bool_type = type_create_simple(core, TypeTag::Boolean);

		if (!lock_type_single(core, block, lhs, some(bool_type)))
			return false;

		bool lhs_result;

		bool is_type_evaluable = pop_value_as(core, &lhs_result);

		const bool positive_lhs_result = code->op == Code2Op::LogicalAnd;

		Code2Block* const rhs = code2_block_from_id(core, attachment->rhs);

		if (!lock_type_block(core, rhs, some(bool_type)))
			return false;

		bool rhs_result;

		if (is_type_evaluable)
			is_type_evaluable = pop_value_as(core, &rhs_result);
		else
			rhs_result = false;

		if (is_type_evaluable && lhs_result == positive_lhs_result)
			push_value(core, rhs_result, false);
		else
			push_value_empty(core, sizeof(bool), alignof(bool), false);

		return true;
	}

	case Code2Op::MemberValue:
	case Code2Op::MemberLocation:
	{
		Code2AttMember* const attachment = attachment_of<Code2AttMember>(code);

		if (!lock_type_update_to_desired(core, code, expected_type))
			return false;

		TypeMetrics member_metrics;
		
		// We can assume that this succeeds, because it would already have
		// failed in `typecheck_single`.
		if (!type_metrics_from_id(core, code->type, &member_metrics))
			ASSERT_UNREACHABLE;

		if (code->member_ref.composite_is_type)
		{
			void* const address = address_from_core_id(core, attachment->composite.type_member_data);

			if (code->op == Code2Op::MemberValue)
				push_value_raw(core, address, member_metrics.size, member_metrics.align, code->member_ref.is_mut);
			else
				push_value_location(core, address, member_metrics.size, member_metrics.align, code->member_ref.is_mut);
		}
		else
		{
			Code2* const composite = code2_from_offset(block, attachment->composite.offset);

			if (!lock_type_single(core, block, composite, none<TypeId>()))
				return false;

			TypecheckValue* const composite_value = peek_value(core);

			if (is_none(composite_value->begin))
			{
				push_value_empty(core, member_metrics.size, member_metrics.align, code->member_ref.is_mut);
			}
			else
			{
				MemberInfo member_info;

				[[maybe_unused]] const MemberByNameRst rst = type_member_info_by_name(core, composite->type, attachment->name, &member_info);

				// We know this can't be `MemberByNameRst::Incomplete` since we have
				// already initialized global members during forward typechecking.
				// It also can't be `MemberByNameRst::NotFound` since that would also
				// have shown up at the same point already.
				ASSERT_OR_IGNORE(rst == MemberByNameRst::Ok);

				void* const address = member_info.is_global
					? address_from_core_id(core, get(member_info.type_and_value.complete.value))
					: static_cast<byte*>(get(composite_value->begin)) + member_info.offset;

				pop_value(core);

				if (code->op == Code2Op::MemberValue)
					push_value_raw(core, address, member_metrics.size, member_metrics.align, code->member_ref.is_mut);
				else
					push_value_location(core, address, member_metrics.size, member_metrics.align, code->member_ref.is_mut);
			}
		}

		return true;
	}

	case Code2Op::Compare:
	{
		Code2AttBinaryOp* const attachment = attachment_of<Code2AttBinaryOp>(code);

		if (!lock_type_update_to_desired(core, code, expected_type))
			return false;

		Code2* const lhs = code2_from_offset(block, attachment->lhs);

		Code2* const rhs = code2_from_offset(block, attachment->rhs);

		const TypeId lhs_type = lhs->type;

		const TypeId rhs_type = rhs->type;

		const TypeRelation relation = type_relation(core, lhs_type, rhs_type);

		TypeId common_type;

		if (relation == TypeRelation::Equal || relation == TypeRelation::SecondConvertsToFirst)
		{
			common_type = lhs_type;
		}
		else if (relation == TypeRelation::FirstConvertsToSecond)
		{
			common_type = rhs_type;
		}
		else if (relation == TypeRelation::Unrelated)
		{
			return record_lock_type_error(core, code);
		}
		else
		{
			ASSERT_OR_IGNORE(relation == TypeRelation::Incomplete);

			return record_lock_type_error(core, code);
		}

		if (!lock_type_single(core, block, lhs, some(common_type)))
			return false;

		TypecheckValue* const lhs_value = peek_value(core);

		if (!lock_type_single(core, block, rhs, some(common_type)))
			return false;

		TypecheckValue* const rhs_value = peek_value(core);

		if (is_some(lhs_value->begin) && is_some(rhs_value->begin))
		{
			bool result;

			if (!evaluate_compare(core, code, common_type, lhs_value, rhs_value, &result))
				return false;

			push_value(core, result, false);
		}
		else
		{
			push_value_empty(core, sizeof(bool), alignof(bool), false);
		}

		return true;
	}

	case Code2Op::Store:
	{
		Code2AttBinaryOp* const attachment = attachment_of<Code2AttBinaryOp>(code);

		if (!lock_type_update_to_desired(core, code, expected_type))
			return false;

		Code2* const lhs = code2_from_offset(block, attachment->lhs);

		if (!lock_type_single(core, block, lhs, none<TypeId>()))
			return false;

		TypecheckValue* const lhs_value = peek_value(core);

		if (!lhs_value->is_mut)
			return record_lock_type_error(core, code);

		const TypeId lhs_type = lhs->type;

		Code2* const rhs = code2_from_offset(block, attachment->rhs);

		if (!lock_type_single(core, block, rhs, some(lhs_type)))
			return false;

		TypecheckValue* const rhs_value = peek_value(core);

		pop_value(core);
		pop_value(core);

		if (is_some(lhs_value->begin) && is_some(rhs_value->begin))
		{
			memcpy(get(lhs_value->begin), get(rhs_value->begin), lhs_value->size);

			push_value_empty(core, 0, 1, false);
		}
		else
		{
			push_value_raw_uninit(core, 0, 1, false);
		}

		return true;
	}

	case Code2Op::DupAndLoadLocation:
	{
		Code2AttUnaryOp* const attachment = attachment_of<Code2AttUnaryOp>(code);

		if (!lock_type_update_to_desired(core, code, expected_type))
			return false;

		Code2* const operand = code2_from_offset(block, attachment->operand);

		if (!lock_type_single(core, block, operand, expected_type))
			return false;

		// The type of `operand` has already been locked by the other reference
		// to it, so do not call `lock_type_single` on it again.

		TypecheckValue* const operand_value = peek_value(core);

		if (is_some(operand_value->begin))
			push_value_raw(core, get(operand_value->begin), operand_value->size, static_cast<u64>(1) << operand_value->align_log2, false);
		else
			push_value_empty(core, operand_value->size, static_cast<u64>(1) << operand_value->align_log2, false);

		return true;
	}

	case Code2Op::Leave:
	case Code2Op::Repeat:
	case Code2Op::DefineUntyped:
	case Code2Op::DefineTyped:
	case Code2Op::ConsumeVoid:
	case Code2Op::INVALID:
		; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

static bool lock_type_block(CoreData* core, Code2Block* block, Maybe<TypeId> expected_type) noexcept
{
	(void) core;

	(void) block;

	(void) expected_type;

	TODO("Implement.");
}



static bool typecheck_call_reorder_arguments(CoreData* core, Code2AttCall* attachment, TypeId signature, u32 argument_count, u32 parameter_count, bool is_variadic) noexcept
{
	ASSERT_OR_IGNORE(parameter_count <= 64);

	ASSERT_OR_IGNORE(argument_count <= 256);

	struct alignas(2) ArgumentParameterPosition
	{
		u8 argument_rank;

		u8 parameter_rank;
	};

	bool has_inversion = false;

	u64 seen_parameters = 0;

	u32 rank = 0;

	for (u32 i = 0; i != argument_count; ++i)
	{
		const Maybe<IdentifierId> name = attachment->arguments[i].name;

		if (is_some(name))
		{
			MemberInfo info;

			const MemberByNameRst rst = type_member_info_by_name(core, signature, get(name), &info);

			if (rst == MemberByNameRst::NotFound)
			{
				// TODO: Record Error.

				return false;
			}

			// An `Incomplete` info still contains the rank, so we can proceed
			// either way.
			ASSERT_OR_IGNORE(rst == MemberByNameRst::Ok || rst == MemberByNameRst::Incomplete);

			if (info.rank < rank)
				has_inversion = true;

			rank = info.rank;
		}
		else if (rank + 1 < parameter_count)
		{
			rank += 1;
		}
		else if (is_variadic)
		{
			TODO("Implement variadic arguments.");
		}
		else
		{
			// TODO: Record Error.

			return false;
		}

		const u64 seen_bit = static_cast<u64>(1) << rank;

		if ((seen_parameters & seen_bit) != 0)
		{
			// TODO: Record Error.

			return false;
		}

		seen_parameters |= seen_bit;

		attachment->arguments[i].rank = static_cast<u8>(rank);
	}

	// If all arguments are already in the same order as the parameters, we're
	// good to go, no sort required. This should be the exceedingly common
	// case, so optimizing for it should pay of.
	// TODO: Test whether this actually speeds anything up. 
	if (!has_inversion)
		return true;

	struct CompareByRank
	{
		static s32 compare(const Code2AttCall_Argument& a, const Code2AttCall_Argument& b) noexcept
		{
			return static_cast<s32>(a.rank) - static_cast<s32>(b.rank);
		}
	};

	inplace_sort<Code2AttCall_Argument, CompareByRank>(MutRange{ attachment->arguments, argument_count });

	return true;
}

static ContState typecheck_call_state(ContState actual_state, u32 parameter_count, u32 parameter_rank, u32 argument_index) noexcept
{
	ASSERT_OR_IGNORE(static_cast<u32>(actual_state) < 64 && parameter_count < 64 && parameter_rank < 64 && argument_index < 64);

	return static_cast<ContState>(static_cast<u32>(actual_state)
	     | parameter_count <<  6
	     | parameter_rank  << 12
	     | argument_index   << 18
	);
}

static TypecheckNext typecheck_call_arguments(CoreData* core, Code2Block* block, Code2* code, ContState state) noexcept
{
	ASSERT_OR_IGNORE(code->op == Code2Op::Call || code->op == Code2Op::ImplTraitCall);

	Code2AttCall* const attachment = attachment_of<Code2AttCall>(code);

	Code2* const callee = code2_from_offset(block, attachment->callee);

	// Functions are of type `Signature`, which cannot be cast to anything
	// else. As such, we can assume that the callee's type will not be
	// changed by `lock_type_single` and we can use it already.
	TypeId signature = callee->type;

	u8 parameter_count;
	u8 parameter_rank;
	u8 argument_index;

	if (state == ContState::INITIAL)
	{
		const TypeTag signature_tag = type_tag_from_id(core, signature);

		if (signature_tag != TypeTag::Signature)
			return record_typecheck_error(core, code);

		const SignatureTypeInfo signature_info = type_signature_info_from_id(core, signature);

		// Duplicate the signature if it is a template so it can be
		// de-templatized without affecting the original template.
		if (signature_info.has_templated_return_type || signature_info.templated_parameter_count != 0)
		{
			signature = type_instantiate_templated_signature(core, signature);

			callee->type = signature;
		}

		// Reorder around named arguments. This happens by mutating the code's
		// `attachment` in-place, since it will stay constant after
		// typechecking and we are working on a mutable copy for that exact
		// phase anyways.
		if (!typecheck_call_reorder_arguments(core, attachment, signature, code->call.argument_count, signature_info.parameter_count, signature_info.is_variadic))
			return record_typecheck_error(core, code);

		parameter_count = signature_info.parameter_count;
		parameter_rank = 0;
		argument_index = 0;
	}
	else
	{
		const u32 raw_state = static_cast<u32>(state);

		state = static_cast<ContState>(raw_state & 63);

		parameter_count = (raw_state >>  6) & 63;
		parameter_rank =  (raw_state >> 12) & 63;
		argument_index =   (raw_state >> 18) & 63;
	}

	const u8 argument_count = attachment->header.call.argument_count;

	while (parameter_rank != parameter_count)
	{
		MemberInfo info;

		if (!type_member_info_by_rank(core, signature, parameter_rank, &info))
		{
			Code2Block* const type_block = code2_block_from_id(core, get(info.type_and_value.pending.type));

			if (state == ContState::INITIAL)
			{
				const ContState next_state = typecheck_call_state(ContState::Call_LockTemplatedParameterType, parameter_count, parameter_rank, argument_index);

				return typecheck_block(core, some(code), next_state, type_block, false, false);
			}
			else if (state == ContState::Call_LockTemplatedParameterType)
			{
				const TypeId type_type = type_create_simple(core, TypeTag::Type);

				if (!lock_type_block(core, type_block, some(type_type)))
					return TypecheckNext{};

				if (is_some(info.type_and_value.pending.value))
				{
					const ContState next_state = typecheck_call_state(ContState::Call_LockTemplatedParameterValue, parameter_count, parameter_rank, argument_index);

					Code2Block* const value_block = code2_block_from_id(core, get(info.type_and_value.pending.value));

					// Do not yet pop the type block's value. Keep it safe on
					// the stack, popping it only when we need it to complete
					// the parameter.
					return typecheck_block(core, some(code), next_state, value_block, false, false);
				}
			}
			else
			{
				ASSERT_OR_IGNORE(state == ContState::Call_LockTemplatedParameterValue);
			}


			TypeId parameter_type;

			if (!pop_value_as(core, &parameter_type))
				return record_typecheck_error(core, code);

			Maybe<CoreId> parameter_default;

			if (state == ContState::Call_LockTemplatedParameterValue)
			{
				Code2Block* const value_block = code2_block_from_id(core, get(info.type_and_value.pending.value));

				if (!lock_type_block(core, value_block, some(parameter_type)))
					return TypecheckNext{};

				parameter_default = pop_value_into_core_id(core);

				if (is_none(parameter_default))
					return record_typecheck_error(core, code);
			}
			else
			{
				parameter_default = none<CoreId>();
			}

			type_complete_templated_signature_parameter(core, signature, parameter_rank, CompleteTypeAndValue{ parameter_type, parameter_default });

			state = ContState::INITIAL;
		}

		const u8 argument_rank = argument_index == argument_count
			? parameter_count
			: attachment->arguments[argument_index].rank;

		if (argument_rank == parameter_rank)
		{
			// Complete the argument with the parameter's type.

			Code2Block* const value_block = code2_block_from_id(core, attachment->arguments[argument_index].value);

			if (state == ContState::INITIAL)
			{
				const ContState next_state = typecheck_call_state(ContState::Call_LockArgumentValue, parameter_count, parameter_rank, argument_index);

				return typecheck_block(core, some(code), next_state, value_block, false, false);
			}
			else
			{
				ASSERT_OR_IGNORE(state == ContState::Call_LockArgumentValue);

				if (!lock_type_block(core, value_block, some(info.type_and_value.complete.type)))
					return TypecheckNext{};

				TypecheckActivation* const activation = top_activation(core);

				ASSERT_OR_IGNORE(parameter_rank < activation->definition_count);

				if (info.is_eval)
				{
					if (!pop_value_into_definition(core, activation->definitions + parameter_rank, info.type_and_value.complete.type, info.is_mut))
						return record_typecheck_error(core, code);
				}
				else
				{
					pop_value(core);
				}

				argument_index += 1;
			}
		}
		else if (is_some(info.type_and_value.complete.value))
		{
			if (info.is_eval)
			{
				// Copy over the default value.

				const void* const src_begin = address_from_core_id(core, get(info.type_and_value.complete.value));

				TypeMetrics metrics;

				if (!type_metrics_from_id(core, info.type_and_value.complete.type, &metrics))
					return record_typecheck_error(core, code);

				TypecheckActivation* const activation = top_activation(core);

				ASSERT_OR_IGNORE(parameter_rank < activation->definition_count);

				TypecheckDefinition* const definition = activation->definitions + parameter_rank;
				definition->type = info.type_and_value.complete.type;
				definition->size = static_cast<u32>(metrics.size);
				definition->align_log2 = count_trailing_zeros_assume_one(metrics.align);
				definition->is_type_eval = info.is_eval;
				definition->is_mut = info.is_mut;
				definition->location_offset = push_location(core, src_begin, metrics.size, metrics.align);
			}
		}
		else
		{
			return record_typecheck_error(core, code);
		}

		parameter_rank += 1;

		state = ContState::INITIAL;
	}

	const SignatureTypeInfo signature_info = type_signature_info_from_id(core, signature);

	TypeId return_type;

	if (state == ContState::INITIAL)
	{
		if (signature_info.has_templated_return_type)
		{
			const ContState next_state = typecheck_call_state(ContState::Call_LockTemplatedReturnType, parameter_count, parameter_rank, argument_index);

			Code2Block* const return_type_block = code2_block_from_id(core, signature_info.return_type.templated.completion_id);

			return typecheck_block(core, some(code), next_state, return_type_block, false, false);
		}
		else
		{
			return_type = signature_info.return_type.complete.type_id;
		}
	}
	else
	{
		ASSERT_OR_IGNORE(state == ContState::Call_LockTemplatedReturnType);

		Code2Block* const return_type_block = code2_block_from_id(core, signature_info.return_type.templated.completion_id);

		const TypeId type_type = type_create_simple(core, TypeTag::Type);

		if (!lock_type_block(core, return_type_block, some(type_type)))
			return TypecheckNext{};

		if (!pop_value_as(core, &return_type))
			return record_typecheck_error(core, code);

		type_complete_templated_signature_return_type(core, signature, return_type);
	}

	code->type = return_type;

	return TypecheckNext{ reinterpret_cast<Code2*>(attachment->arguments + code->call.argument_count), block };
}

static TypecheckNext typecheck_single(CoreData* core, Code2Block* block, Code2* code, ContState state) noexcept
{
	ASSERT_OR_IGNORE(code->type == TypeId::INVALID);

	switch (code->op)
	{
	case Code2Op::Leave:
	{
		Code2AttUnaryOp* const attach = attachment_of<Code2AttUnaryOp>(code);

		Code2* const operand = code2_from_offset(block, attach->operand);

		const u16 out = code->leave.out;

		register_leave(core, block, operand, out);

		code->type = type_create_simple(core, TypeTag::Divergent);

		const Maybe<TypecheckActivation*> opt_new_activation = pop_non_control_flow_activations(core, out);

		if (is_none(opt_new_activation))
			return TypecheckNext{};

		TypecheckActivation* const new_activation = get(opt_new_activation);

		Code2Block* const new_block = new_activation->block;

		Code2* const new_code = code2_from_offset(new_block, get(new_activation->cont_code));

		return TypecheckNext{ new_code, new_block, new_activation->cont_state };
	}

	case Code2Op::Repeat:
	{
		code->type = type_create_simple(core, TypeTag::Divergent);

		const Maybe<TypecheckActivation*> opt_new_activation = code->leave.out == 0
			? some(get_activation(core, 0))
			: pop_non_control_flow_activations(core, code->leave.out - 1);

		if (is_none(opt_new_activation))
			return TypecheckNext{};

		TypecheckActivation* const new_activation = get(opt_new_activation);

		Code2Block* const new_block = new_activation->block;

		Code2* const new_code = code2_block_begin(new_block);

		return TypecheckNext{ new_code, new_block };
	}

	case Code2Op::Jump:
	case Code2Op::Loop:
	{
		Code2AttJump* const attachment = attachment_of<Code2AttJump>(code);

		if (state == ContState::INITIAL)
		{
			Code2Block* const body = code2_block_from_id(core, attachment->body);

			return typecheck_block(core, some(code), ContState::LoopOrJump_SetType, body, false, true);
		}
		else
		{
			ASSERT_OR_IGNORE(state == ContState::LoopOrJump_SetType);

			Code2Block* const body = code2_block_from_id(core, attachment->body);

			code->type = body->type;

			// The body's type can only be locked once we know the desired type of
			// this `loop`, which is in `lock_type_single`.

			return TypecheckNext{ reinterpret_cast<Code2*>(attachment + 1), block };
		}
	}

	case Code2Op::Builtin:
	{
		const u8 ordinal = static_cast<u8>(code->builtin.builtin);

		ASSERT_OR_IGNORE(ordinal < array_count(core->typecheck.builtin_types));

		code->type = core->typecheck.builtin_types[ordinal];

		return TypecheckNext{ code + 1, block };
	}

	case Code2Op::Composite:
	{
		Code2AttComposite* const attach = attachment_of<Code2AttComposite>(code);

		const u16 member_count = attach->header.composite.member_count;

		TypeId composite_type = type_create_approx_composite(core, member_count);

		for (u32 i = 0; i != member_count; ++i)
		{
			const Code2AttComposite_Member* const member = attach->members + i;

			const Code2* const member_value = code2_from_offset(block, member->value);

			type_add_approx_composite_member(core, composite_type, static_cast<u16>(i), member->name, member_value->type);
		}

		code->type = composite_type;

		return TypecheckNext{ reinterpret_cast<Code2*>(attach->members + member_count), block };
	}

	case Code2Op::Array:
	{
		Code2AttArray* const attach = attachment_of<Code2AttArray>(code);

		const u32 element_count = attach->header.composite.member_count;

		TypeId common_element_type = TypeId::INVALID;

		for (u32 i = 0; i != element_count; ++i)
		{
			const Code2AttArray_Element* const element = attach->elements + element_count;

			Code2* const element_value = code2_from_offset(block, element->value);

			if (i == 0)
			{
				common_element_type = element_value->type;
			}
			else
			{
				const TypeRelation relation = type_relation(core, common_element_type, element_value->type);

				if (relation == TypeRelation::FirstConvertsToSecond)
					common_element_type = element_value->type;
				else if (relation == TypeRelation::Unrelated)
					return record_typecheck_error(core, code);
				else if (relation == TypeRelation::Incomplete)
					return record_typecheck_error(core, code);
				else
					ASSERT_OR_IGNORE(relation == TypeRelation::Equal || relation == TypeRelation::SecondConvertsToFirst);
			}
		}

		code->type = type_create_array(core, ArrayType{ element_count, common_element_type });

		return TypecheckNext{ reinterpret_cast<Code2*>(attach->elements + element_count), block };
	}

	case Code2Op::Definition:
	{
		Code2AttDefinition* const attachment = attachment_of<Code2AttDefinition>(code);

		code->type = type_create_simple(core, TypeTag::Definition);

		return TypecheckNext{ reinterpret_cast<Code2*>(attachment + 1), block };
	}

	case Code2Op::MakeLocation:
	{
		Code2AttUnaryOp* const attachment = attachment_of<Code2AttUnaryOp>(code);

		const Code2* const operand = code2_from_offset(block, attachment->operand);

		code->type = operand->type;

		return TypecheckNext{ reinterpret_cast<Code2*>(attachment + 1), block };
	}

	case Code2Op::DefineUntyped:
	{
		Code2AttUnaryOp* const attachment = attachment_of<Code2AttUnaryOp>(code);

		Code2* const value = code2_from_offset(block, attachment->operand);

		if (!lock_type_single(core, block, value, none<TypeId>()))
			return TypecheckNext{};

		TypecheckActivation* const activation = top_activation(core);

		ASSERT_OR_IGNORE(code->define.definition_index < activation->definition_count);

		TypecheckDefinition* const definition = activation->definitions + code->define.definition_index;

		(void) pop_value_into_definition(core, definition, value->type, code->define.is_mut);

		return TypecheckNext{ reinterpret_cast<Code2*>(attachment + 1), block };
	}

	case Code2Op::DefineTyped:
	{
		Code2AttBinaryOp* const attachment = attachment_of<Code2AttBinaryOp>(code);

		Code2* const type_operand = code2_from_offset(block, attachment->lhs);

		const TypeId type_type = type_create_simple(core, TypeTag::Type);

		if (!lock_type_single(core, block, type_operand, some(type_type)))
			return TypecheckNext{};

		TypeId value_type;

		if (!pop_value_as(core, &value_type))
			return record_typecheck_error(core, code);

		Code2* const value_operand = code2_from_offset(block, attachment->rhs);

		if (!lock_type_single(core, block, value_operand, some(value_type)))
			return TypecheckNext{};

		TypecheckActivation* const activation = top_activation(core);

		ASSERT_OR_IGNORE(code->define.definition_index < activation->definition_count);

		TypecheckDefinition* const definition = activation->definitions + code->define.definition_index;

		(void) pop_value_into_definition(core, definition, value_type, code->define.is_mut);

		return TypecheckNext{ reinterpret_cast<Code2*>(attachment + 1), block };
	}

	case Code2Op::ConsumeVoid:
	{
		Code2AttUnaryOp* const attachment = attachment_of<Code2AttUnaryOp>(code);

		Code2* const operand = code2_from_offset(block, attachment->operand);

		const TypeId void_type = type_create_simple(core, TypeTag::Void);

		if (!lock_type_single(core, block, operand, some(void_type)))
			return TypecheckNext{};

		pop_value(core);

		return TypecheckNext{ reinterpret_cast<Code2*>(attachment + 1), block };
	}

	case Code2Op::Void:
	{
		code->type = type_create_simple(core, TypeTag::Void);

		return TypecheckNext{ code + 1, block };
	}

	case Code2Op::If:
	{
		Code2AttIf* const attachment = attachment_of<Code2AttIf>(code);

		if (state == ContState::INITIAL)
		{
			if (code->if_.is_type_eval)
			{
				Code2* const condition = code2_from_offset(block, attachment->condition);

				const TypeId bool_type = type_create_simple(core, TypeTag::Boolean);

				if (!lock_type_single(core, block, condition, some(bool_type)))
					return TypecheckNext{};

				bool evaluated_condition;

				if (!pop_value_as(core, &evaluated_condition))
					return record_typecheck_error(core, code);

				code->if_.evaluated_condition = evaluated_condition;

				const Code2Id chosen_branch_id = evaluated_condition
					? attachment->consequent
					: attachment->alternative;

				Code2Block* const chosen_branch = code2_block_from_id(core, chosen_branch_id);

				return typecheck_block(core, some(code), ContState::If_SetType, chosen_branch, true, false);
			}
			else
			{
				Code2Block* const consequent = code2_block_from_id(core, attachment->consequent);

				return typecheck_block(core, some(code), ContState::If_Alternative, consequent, false, true);
			}
		}
		else if (state == ContState::If_Alternative)
		{
			Code2Block* const alternative = code2_block_from_id(core, attachment->alternative);

			return typecheck_block(core, some(code), ContState::If_SetType, alternative, false, true);
		}
		else
		{
			ASSERT_OR_IGNORE(state == ContState::If_SetType);

			if (code->if_.is_type_eval)
			{
				const Code2Id chosen_branch_id = code->if_.evaluated_condition
					? attachment->consequent
					: attachment->alternative;

				Code2Block* const chosen_branch = code2_block_from_id(core, chosen_branch_id);

				code->type = chosen_branch->type;

				// The chosen branch's type can only be locked once we know the
				// desired type of this `if`, which is in `lock_type_single`.
			}
			else
			{
				Code2Block* const consequent = code2_block_from_id(core, attachment->consequent);

				Code2Block* const alternative = code2_block_from_id(core, attachment->alternative);

				const TypeRelation relation = type_relation(core, consequent->type, alternative->type);

				if (relation == TypeRelation::Equal || relation == TypeRelation::FirstConvertsToSecond)
				{
					code->type = consequent->type;
				}
				else if (relation == TypeRelation::SecondConvertsToFirst)
				{
					code->type = alternative->type;
				}
				else
				{
					ASSERT_OR_IGNORE(relation == TypeRelation::Incomplete || relation == TypeRelation::Unrelated);

					return record_typecheck_error(core, code);
				}

				// The individual branches' types can only be locked once we know
				// the desired type of this `if`, which is in `lock_type_single`.
			}

			return TypecheckNext{ reinterpret_cast<Code2*>(attachment + 1), block };
		}
	}

	case Code2Op::Func:
	{
		Code2AttFunc* const attachment = attachment_of<Code2AttFunc>(code);

		Code2* const signature = code2_from_offset(block, attachment->signature);

		if (!lock_type_single(core, block, signature, none<TypeId>()))
			return TypecheckNext{};

		// This is guaranteed by the syntax, since the `signature` is always an
		// in-place-defined signature, whose type is type.
		ASSERT_OR_IGNORE(type_is_equal(core, signature->type, type_create_simple(core, TypeTag::Type)) == TypeEquality::Equal);

		TypeId signature_type;

		if (!pop_value_as(core, &signature_type))
			return record_typecheck_error(core, code);

		code->type = signature_type;

		return TypecheckNext{ reinterpret_cast<Code2*>(attachment + 1), block };
	}

	case Code2Op::Signature:
	{
		Code2AttSignature* const attachment = attachment_of<Code2AttSignature>(code);

		const u32 parameter_count = code->signature.parameter_count;

		const u32 closed_count = code->signature.closed_count;

		Code2Offset* const closed = reinterpret_cast<Code2Offset*>(attachment->parameters + parameter_count);

		code->type = type_create_simple(core, TypeTag::Type);

		return TypecheckNext{ reinterpret_cast<Code2*>(closed + closed_count), block };
	}

	case Code2Op::Trait:
	{
		TODO("Implement `typecheck_single(%).`", tag_name(code->op));
	}

	case Code2Op::Impl:
	{
		TODO("Implement `typecheck_single(%).`", tag_name(code->op));
	}

	case Code2Op::ImplTraitCall:
	{
		TODO("Implement `typecheck_single(%).`", tag_name(code->op));
	}

	case Code2Op::Self:
	{
		TODO("Implement `typecheck_single(%).`", tag_name(code->op));
	}

	case Code2Op::Unreachable:
	{
		code->type = type_create_simple(core, TypeTag::Divergent);

		return TypecheckNext{ code + 1, block };
	}

	case Code2Op::Undefined:
	{
		code->type = type_create_simple(core, TypeTag::Undefined);

		return TypecheckNext{ code + 1, block };
	}

	case Code2Op::LocalValue:
	case Code2Op::LocalLocation:
	{
		TypecheckActivation* const activation = get_activation(core, code->local_ref.out);

		ASSERT_OR_IGNORE(code->local_ref.rank < activation->definition_count);

		const TypecheckDefinition* const definition = activation->definitions + code->local_ref.rank;

		code->type = definition->type;

		return TypecheckNext{ code + 1, block };
	}

	case Code2Op::GlobalValue:
	case Code2Op::GlobalLocation:
	{
		MemberInfo info;

		if (type_member_info_by_rank(core, core->typecheck.active_file_type, code->global_ref.rank, &info))
		{
			code->type = info.type_and_value.complete.type;

			return TypecheckNext{ code + 1, block };
		}
		else
		{
			return initialize_global_cont(core, block, code, state, core->typecheck.active_file_type, &info);
		}
	}

	case Code2Op::ClosedValue:
	case Code2Op::ClosedLocation:
	{
		TODO("Implement `typecheck_single(%).`", tag_name(code->op));
	}

	case Code2Op::TraitArgumentValue:
	case Code2Op::TraitArgumentLocation:
	{
		TODO("Implement `typecheck_single(%).`", tag_name(code->op));
	}

	case Code2Op::Slice:
	{
		Code2AttSlice* const attachment = attachment_of<Code2AttSlice>(code);

		Code2* const sliced = code2_from_offset(block, attachment->sliced);

		const TypeId sliced_type = sliced->type;

		const TypeTag sliced_type_tag = type_tag_from_id(core, sliced_type);

		if (sliced_type_tag == TypeTag::Array)
		{
			const ArrayType sliced_type_attachment = *type_attachment_from_id<ArrayType>(core, sliced_type);

			ReferenceType slice_type{};
			slice_type.referenced_type_id = sliced_type_attachment.element_type;
			slice_type.is_multi = false;
			slice_type.is_opt = false;
			slice_type.is_mut = false; // TODO: Properly determine mutability.

			code->type = type_create_reference(core, TypeTag::Slice, slice_type);
		}
		else if (sliced_type_tag == TypeTag::Slice)
		{
			code->type = sliced_type;
		}
		else if (sliced_type_tag == TypeTag::Ptr)
		{
			const ReferenceType sliced_type_attachment = *type_attachment_from_id<ReferenceType>(core, sliced_type);

			ReferenceType slice_type{};
			slice_type.referenced_type_id = sliced_type_attachment.referenced_type_id;
			slice_type.is_multi = false;
			slice_type.is_opt = false;
			slice_type.is_mut = false; // TODO: Properly determine mutability.

			code->type = type_create_reference(core, TypeTag::Slice, slice_type);
		}
		else
		{
			return record_typecheck_error(core, code);
		}

		return TypecheckNext{ reinterpret_cast<Code2*>(attachment + 1), block };
	}

	case Code2Op::Integer16:
	{
		code->type = type_create_simple(core, TypeTag::CompInteger);

		return TypecheckNext{ code + 1, block };
	}

	case Code2Op::Integer64:
	{
		Code2AttInteger64* const attachment = attachment_of<Code2AttInteger64>(code);

		code->type = type_create_simple(core, TypeTag::CompInteger);

		return TypecheckNext{ reinterpret_cast<Code2*>(attachment + 1), block };
	}

	case Code2Op::Float:
	{
		Code2AttFloat* const attachment = attachment_of<Code2AttFloat>(code);

		code->type = type_create_simple(core, TypeTag::CompFloat);

		return TypecheckNext{ reinterpret_cast<Code2*>(attachment + 1), block };
	}

	case Code2Op::String:
	{
		Code2AttString* const attachment = attachment_of<Code2AttString>(code);

		const TypeId u8_type = type_create_numeric(core, TypeTag::Integer, NumericType{ 8, false });

		code->type = type_create_array(core, ArrayType{ attachment->size, u8_type });

		return TypecheckNext{ reinterpret_cast<Code2*>(attachment + 1), block };
	}

	case Code2Op::Call:
	{
		return typecheck_call_arguments(core, block, code, state);
	}

	case Code2Op::TypeTailArray:
	{
		TODO("Implement `typecheck_single(%).`", tag_name(code->op));
	}

	case Code2Op::TypeSlice:
	case Code2Op::TypePtr:
	case Code2Op::Distinct:
	{
		Code2AttUnaryOp* const attachment = attachment_of<Code2AttUnaryOp>(code);

		code->type = type_create_simple(core, TypeTag::Type);

		return TypecheckNext{ reinterpret_cast<Code2*>(attachment + 1), block };
	}

	case Code2Op::Eval:
	{
		TODO("Implement `typecheck_single(%).`", tag_name(code->op));
	}

	case Code2Op::Defer:
	{
		Code2AttUnaryOp* const attachment = attachment_of<Code2AttUnaryOp>(code);

		code->type = type_create_simple(core, TypeTag::Void);

		return TypecheckNext{ reinterpret_cast<Code2*>(attachment + 1), block };
	}

	case Code2Op::Address:
	{
		Code2AttUnaryOp* const attachment = attachment_of<Code2AttUnaryOp>(code);

		Code2* const operand = code2_from_offset(block, attachment->operand);

		ReferenceType ptr_type{};
		ptr_type.referenced_type_id = operand->type;
		ptr_type.is_multi = false; // TODO: Set Properly.
		ptr_type.is_mut = false;   // TODO: Set Properly.
		ptr_type.is_opt = false;   // TODO: Set Properly.

		code->type = type_create_reference(core, TypeTag::Ptr, ptr_type);

		return TypecheckNext{ reinterpret_cast<Code2*>(attachment + 1), block };
	}

	case Code2Op::Dereference:
	{
		Code2AttUnaryOp* const attachment = attachment_of<Code2AttUnaryOp>(code);

		Code2* const operand = code2_from_offset(block, attachment->operand);

		const TypeTag type_tag = type_tag_from_id(core, operand->type);

		if (type_tag != TypeTag::Ptr)
			return record_typecheck_error(core, code);

		const ReferenceType* const ptr_type = type_attachment_from_id<ReferenceType>(core, operand->type);

		code->type = ptr_type->referenced_type_id;

		return TypecheckNext{ reinterpret_cast<Code2*>(attachment + 1), block };
	}

	case Code2Op::BitNot:
	{
		Code2AttUnaryOp* const attachment = attachment_of<Code2AttUnaryOp>(code);

		Code2* const operand = code2_from_offset(block, attachment->operand);

		const TypeTag type_tag = type_tag_from_id(core, operand->type);

		if (type_tag != TypeTag::CompInteger && type_tag != TypeTag::Integer)
			return record_typecheck_error(core, code);

		code->type = operand->type;

		return TypecheckNext{ reinterpret_cast<Code2*>(attachment + 1), block };
	}

	case Code2Op::LogicalNot:
	{
		Code2AttUnaryOp* const attachment = attachment_of<Code2AttUnaryOp>(code);

		Code2* const operand = code2_from_offset(block, attachment->operand);

		const TypeTag type_tag = type_tag_from_id(core, operand->type);

		if (type_tag != TypeTag::Boolean)
			return record_typecheck_error(core, code);

		code->type = operand->type;

		return TypecheckNext{ reinterpret_cast<Code2*>(attachment + 1), block };
	}

	case Code2Op::Negate:
	case Code2Op::UnaryPlus:
	{
		Code2AttUnaryOp* const attachment = attachment_of<Code2AttUnaryOp>(code);

		Code2* const operand = code2_from_offset(block, attachment->operand);

		const TypeTag type_tag = type_tag_from_id(core, operand->type);

		if (type_tag != TypeTag::CompInteger && type_tag != TypeTag::Integer && type_tag != TypeTag::CompFloat && type_tag != TypeTag::Float)
			return record_typecheck_error(core, code);

		code->type = operand->type;

		return TypecheckNext{ reinterpret_cast<Code2*>(attachment + 1), block };
	}

	case Code2Op::BinaryArithmetic:
	{
		Code2AttBinaryOp* const attachment = attachment_of<Code2AttBinaryOp>(code);

		Code2* const lhs = code2_from_offset(block, attachment->lhs);

		Code2* const rhs = code2_from_offset(block, attachment->rhs);

		const TypeId lhs_type = lhs->type;

		const TypeId rhs_type = rhs->type;

		const TypeRelation relation = type_relation(core, lhs_type, rhs_type);

		TypeId common_type;

		if (relation == TypeRelation::Equal || relation == TypeRelation::SecondConvertsToFirst)
		{
			common_type = lhs_type;
		}
		else if (relation == TypeRelation::FirstConvertsToSecond)
		{
			common_type = rhs_type;
		}
		else
		{
			ASSERT_OR_IGNORE(relation == TypeRelation::Unrelated || relation == TypeRelation::Incomplete);

			return record_typecheck_error(core, code);
		}

		const TypeTag type_tag = type_tag_from_id(core, common_type);

		const bool accept_float = code->binary_arithmetic.kind <= Code2AttBinaryArithmetic_Kind::Div;

		const bool accept_bool = code->binary_arithmetic.kind >= Code2AttBinaryArithmetic_Kind::BitAnd;

		const bool is_integer = type_tag == TypeTag::CompInteger || type_tag == TypeTag::Integer;

		const bool is_float = type_tag == TypeTag::CompFloat || type_tag == TypeTag::Float;

		const bool is_bool = type_tag == TypeTag::Boolean;

		if (!is_integer && (!accept_float || !is_float) && (!accept_bool || !is_bool))
			return record_typecheck_error(core, code);

		code->type = common_type;

		return TypecheckNext{ reinterpret_cast<Code2*>(attachment + 1), block };
	}

	case Code2Op::BitShift:
	{
		Code2AttBinaryOp* const attachment = attachment_of<Code2AttBinaryOp>(code);

		Code2* const lhs = code2_from_offset(block, attachment->lhs);

		code->type = lhs->type;

		return TypecheckNext{ reinterpret_cast<Code2*>(attachment + 1), block };
	}

	case Code2Op::TypeArray:
	{
		Code2AttBinaryOp* const attachment = attachment_of<Code2AttBinaryOp>(code);

		code->type = type_create_simple(core, TypeTag::Type);

		return TypecheckNext{ reinterpret_cast<Code2*>(attachment + 1), block };
	}

	case Code2Op::Index:
	{
		Code2AttBinaryOp* const attachment = attachment_of<Code2AttBinaryOp>(code);

		Code2* const lhs = code2_from_offset(block, attachment->lhs);

		const TypeId lhs_type = lhs->type;

		const TypeTag lhs_type_tag = type_tag_from_id(core, lhs_type);

		TypeId element_type;

		if (lhs_type_tag == TypeTag::Array)
		{
			const ArrayType* const array_type = type_attachment_from_id<ArrayType>(core, lhs_type);

			element_type = array_type->element_type;
		}
		else if (lhs_type_tag == TypeTag::Slice || lhs_type_tag == TypeTag::Ptr)
		{
			const ReferenceType* const reference_type = type_attachment_from_id<ReferenceType>(core, lhs_type);

			element_type = reference_type->referenced_type_id;

			if (lhs_type_tag == TypeTag::Ptr && reference_type->is_opt)
			return record_typecheck_error(core, code);

			if (lhs_type_tag == TypeTag::Ptr && !reference_type->is_multi)
			return record_typecheck_error(core, code);
		}
		else
		{
			return record_typecheck_error(core, code);
		}

		code->type = element_type;

		return TypecheckNext{ reinterpret_cast<Code2*>(attachment + 1), block };
	}

	case Code2Op::LogicalAnd:
	case Code2Op::LogicalOr:
	{
		Code2AttLogicalOp* const attachment = attachment_of<Code2AttLogicalOp>(code);

		code->type = type_create_simple(core, TypeTag::Boolean);

		Code2Block* const rhs = code2_block_from_id(core, attachment->rhs);

		return typecheck_block(core, some(reinterpret_cast<Code2*>(attachment + 1)), ContState::INITIAL, rhs, true, true);
	}

	case Code2Op::MemberValue:
	case Code2Op::MemberLocation:
	{
		Code2AttMember* const attachment = attachment_of<Code2AttMember>(code);

		if (state == ContState::INITIAL)
		{
			Code2* const composite = code2_from_offset(block, attachment->composite.offset);

			const TypeId composite_type = composite->type;

			const TypeTag composite_type_tag = type_tag_from_id(core, composite_type);

			if (composite_type_tag == TypeTag::Composite)
			{
				MemberInfo info;

				const MemberByNameRst find_rst = type_member_info_by_name(core, composite_type, attachment->name, &info);

				if (find_rst == MemberByNameRst::NotFound)
					return record_typecheck_error(core, code);

				// We can assume that the member is complete since it is part of a
				// proper composite.
				ASSERT_OR_IGNORE(find_rst == MemberByNameRst::Ok);

				// It's ok to use the details of the composite member before
				// locking it, since composites cannot be implicitly converted
				// anyways.
				code->type = info.type_and_value.complete.type;
				code->member_ref.is_mut = code->op == Code2Op::MemberLocation && info.is_mut;
				code->member_ref.is_pub = info.is_pub;
				code->member_ref.composite_is_type = false;
				code->member_ref.rank = info.rank;

				return TypecheckNext{ reinterpret_cast<Code2*>(attachment + 1), block };
			}
			else if (composite_type_tag == TypeTag::Type)
			{
				const TypeId type_type = type_create_simple(core, TypeTag::Type);

				if (!lock_type_single(core, block, composite, some(type_type)))
					ASSERT_UNREACHABLE;

				TypeId type_value;

				if (!pop_value_as(core, &type_value))
					return record_typecheck_error(core, code);

				MemberInfo info;

				const MemberByNameRst rst = type_member_info_by_name(core, type_value, attachment->name, &info);

				if (rst == MemberByNameRst::NotFound)
					return record_typecheck_error(core, code);

				code->type = info.type_and_value.complete.type;
				code->member_ref.is_mut = info.is_mut;
				code->member_ref.is_pub = info.is_pub;
				code->member_ref.composite_is_type = true;
				code->member_ref.rank = info.rank;

				if (rst == MemberByNameRst::Ok)
				{
					attachment->composite.type_member_data = get(info.type_and_value.complete.value);

					return TypecheckNext{ reinterpret_cast<Code2*>(attachment + 1), block };
				}
				else
				{
					// Temporarily stash the type we are completing a global
					// member of so that we can access it in later completion
					// iterations.
					attachment->composite.type = type_value;

					return initialize_global_cont(core, block, code, state, type_value, &info);
				}
			}
			else
			{
				return record_typecheck_error(core, code);
			}
		}
		else
		{
			MemberInfo info;

			if (type_member_info_by_rank(core, attachment->composite.type, code->member_ref.rank, &info))
				ASSERT_UNREACHABLE;

			return initialize_global_cont(core, block, code, state, attachment->composite.type, &info);
		}
	}

	case Code2Op::Compare:
	{
		Code2AttBinaryOp* const attachment = attachment_of<Code2AttBinaryOp>(code);

		Code2* const lhs = code2_from_offset(block, attachment->lhs);

		Code2* const rhs = code2_from_offset(block, attachment->rhs);

		const TypeId lhs_type = lhs->type;

		const TypeId rhs_type = rhs->type;

		const TypeRelation relation = type_relation(core, lhs_type, rhs_type);

		TypeId common_type;

		if (relation == TypeRelation::Equal || relation == TypeRelation::SecondConvertsToFirst)
		{
			common_type = lhs_type;
		}
		else if (relation == TypeRelation::FirstConvertsToSecond)
		{
			common_type = rhs_type;
		}
		else
		{
			ASSERT_OR_IGNORE(relation == TypeRelation::Unrelated || relation == TypeRelation::Incomplete);

			return record_typecheck_error(core, code);
		}

		// TODO: Check whether common_type actually supports comparisons.

		if (!lock_type_single(core, block, lhs, some(common_type)))
			return TypecheckNext{};

		if (!lock_type_single(core, block, rhs, some(common_type)))
			return TypecheckNext{};

		code->type = type_create_simple(core, TypeTag::Boolean);

		return TypecheckNext{ reinterpret_cast<Code2*>(attachment + 1), block };
	}

	case Code2Op::Store:
	{
		Code2AttBinaryOp* const attachment = attachment_of<Code2AttBinaryOp>(code);

		Code2* const lhs = code2_from_offset(block, attachment->lhs);

		if (!lock_type_single(core, block, lhs, none<TypeId>()))
			return TypecheckNext{};

		const TypeId lhs_type = lhs->type;

		Code2* const rhs = code2_from_offset(block, attachment->rhs);

		if (!lock_type_single(core, block, rhs, some(lhs_type)))
			return TypecheckNext{};

		code->type = type_create_simple(core, TypeTag::Void);

		return TypecheckNext{ reinterpret_cast<Code2*>(attachment + 1), block };
	}

	case Code2Op::DupAndLoadLocation:
	{
		Code2AttUnaryOp* const attachment = attachment_of<Code2AttUnaryOp>(code);

		Code2* const lhs = code2_from_offset(block, attachment->operand);

		code->type = lhs->type;

		return TypecheckNext{ reinterpret_cast<Code2*>(attachment + 1), block };
	}

	case Code2Op::INVALID:
		; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

static TypecheckNext typecheck_block(CoreData* core, Maybe<Code2*> cont_code, ContState cont_state, Code2Block* block, bool preserve_locations, bool is_dynamic_control_flow) noexcept
{
	push_activation(core, block, cont_code, cont_state, preserve_locations, is_dynamic_control_flow);

	return TypecheckNext{ code2_block_begin(block), block };
}



static TypecheckNext initialize_global_cont(CoreData* core, Code2Block* block, Code2* code, ContState state, TypeId file_type, const MemberInfo* info) noexcept
{
	TypeId value_type;

	if (state == ContState::INITIAL)
	{
		type_file_member_init_begin(core, file_type, info->rank);

		const Maybe<Code2Id> type_initializer_id = info->type_and_value.pending.type;

		if (is_some(type_initializer_id))
		{
			Code2Block* const type_initializer = code2_block_from_id(core, get(type_initializer_id));

			return typecheck_block(core, some(code), ContState::InitGlobal_LockType, type_initializer, false, false);
		}
		else
		{
			const Code2Id value_initializer_id = get(info->type_and_value.pending.value);

			Code2Block* const value_initializer = code2_block_from_id(core, value_initializer_id);

			return typecheck_block(core, some(code), ContState::InitGlobal_LockValueUntyped, value_initializer, false, false);
		}
	}
	else if (state == ContState::InitGlobal_LockType)
	{
		const Maybe<Code2Id> type_initializer_id = info->type_and_value.pending.type;

		Code2Block* const type_initializer = code2_block_from_id(core, get(type_initializer_id));

		const TypeId type_type = type_create_simple(core, TypeTag::Type);

		// This is popped below in `InitGlobal_LockValueTyped`.
		if (!lock_type_block(core, type_initializer, some(type_type)))
			return TypecheckNext{};

		return typecheck_block(core, some(code), ContState::InitGlobal_LockValueTyped, type_initializer, false, false);

	}
	else if (state == ContState::InitGlobal_LockValueUntyped)
	{
		const Code2Id value_initializer_id = get(info->type_and_value.pending.value);

		Code2Block* const value_initializer = code2_block_from_id(core, value_initializer_id);

		if (!lock_type_block(core, value_initializer, none<TypeId>()))
			return TypecheckNext{};

		value_type = value_initializer->type;

		// Fallthrough to completion.
	}
	else
	{
		ASSERT_OR_IGNORE(state == ContState::InitGlobal_LockValueTyped);

		const Code2Id value_initializer_id = get(info->type_and_value.pending.value);

		Code2Block* const value_initializer = code2_block_from_id(core, value_initializer_id);

		// This was put on the stack in `InitGlobal_LockType` and kept there
		// safely until right now.
		if (!pop_value_as(core, &value_type))
			return record_typecheck_error(core, code);

		type_file_member_init_set_type(core, file_type, info->rank, value_type);

		if (!lock_type_block(core, value_initializer, some(value_type)))
			return TypecheckNext{};

		// Fallthrough to completion.
	}

	const Maybe<CoreId> type_evaluated_value = pop_value_into_core_id(core);

	CoreId value;

	if (is_some(type_evaluated_value))
		value = get(type_evaluated_value);
	else
		TODO("Properly evaluate global initializer that isn't fully type-evaluable");

	type_file_member_init_complete(core, file_type, info->rank, value_type, value);

	if (code->op == Code2Op::GlobalLocation || code->op == Code2Op::GlobalValue)
	{
		code->type = value_type;

		return TypecheckNext{ code + 1, block };
	}
	else
	{
		ASSERT_OR_IGNORE(code->op == Code2Op::MemberLocation || code->op == Code2Op::MemberValue);

		Code2AttMember* const attachment = attachment_of<Code2AttMember>(code);

		code->type = value_type;

		attachment->composite.type_member_data = value;

		return TypecheckNext{ reinterpret_cast<Code2*>(attachment + 1), block };
	}
}



bool run_typechecker(CoreData* core, Code2Id block_id) noexcept
{
	Code2Block* const block = code2_block_from_id(core, block_id);

	TypecheckNext curr = typecheck_block(core, none<Code2*>(), ContState::INITIAL, block, false, false);

	ASSERT_OR_IGNORE(curr.code != nullptr);

	do
		curr = typecheck_single(core, curr.block, curr.code, curr.state);
	while (curr.code != nullptr);

	return core->typecheck.is_ok;
}
