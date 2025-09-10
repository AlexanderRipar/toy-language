#include "core.hpp"

#include "../infra/hash.hpp"
#include "../infra/container.hpp"
#include "../infra/inplace_sort.hpp"

#include <cstdlib>
#include <cstring>

static constexpr u32 MIN_STRUCTURE_SIZE_LOG2 = 4;

static constexpr u32 MAX_STRUCTURE_SIZE_LOG2 = 12;



struct TypeStructure;

static TypeStructure* structure_from_id(TypePool* types, TypeId id) noexcept;

static TypeStructure* make_structure(TypePool* types, TypeTag tag, Range<byte> attach, u64 reserve_size, bool is_fixed, SourceId distinct_source_id) noexcept;

static TypeId id_from_structure(const TypePool* types, const TypeStructure* structure) noexcept;


enum class TypeEq : u8
{
	Equal,
	Unequal,
	MaybeEqual,
};

struct HolotypeInfo
{
	TypeId a;

	TypeId b;
};

struct EqualityState
{
	u16 stack_used;

	u16 delayed_used;

	TypeId stack[128];

	HolotypeInfo delayed[256];
};

struct alignas(8) CompositeTypeHeader
{
	u64 size;

	u64 stride;

	u8 align_log2;

	bool is_open;

	u16 member_count;

	u16 incomplete_member_count;

	TypeDisposition disposition;

	u8 unused_1;

	TypeId lexical_parent_type_id;

	u32 unused_2;
};

struct CompositeType
{
	CompositeTypeHeader header;

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
	Member members[];
	#if COMPILER_MSVC
	#pragma warning(pop)
	#elif COMPILER_CLANG
	#pragma clang diagnostic pop
	#elif COMPILER_GCC
	#pragma GCC diagnostic pop
	#endif
};

struct alignas(8) TypeStructure
{
	u16 size;

	u16 capacity_log2 : 7;

	u8 is_fixed : 1;

	TypeTag tag;

	TypeId holotype_id;

	union
	{
		SourceId distinct_source_id;

		TypeId indirection_type_id;
	};

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
	alignas(8) byte attach[];
	#if COMPILER_MSVC
	#pragma warning(pop)
	#elif COMPILER_CLANG
	#pragma clang diagnostic pop
	#elif COMPILER_GCC
	#pragma GCC diagnostic pop
	#endif
};

struct DeduplicatedTypeInit
{
	AttachmentRange<byte, TypeTag> tag_and_attach;

	SourceId distinct_source_id;

	TypePool* types;
};

struct alignas(8) DeduplicatedTypeInfo
{
	TypeId type_id;

	u32 m_hash;

	static constexpr u32 stride() noexcept
	{
		return sizeof(DeduplicatedTypeInfo);
	}

	static u32 required_strides([[maybe_unused]] DeduplicatedTypeInit key) noexcept
	{
		return 1;
	}

	u32 used_strides() const noexcept
	{
		return 1;
	}

	u32 hash() const noexcept
	{
		return m_hash;
	}

	bool equal_to_key(DeduplicatedTypeInit key, u32 key_hash) const noexcept
	{
		if (m_hash != key_hash)
			return false;

		const TypeStructure* const structure = structure_from_id(key.types, type_id);

		return structure->tag == key.tag_and_attach.attachment()
		    && structure->distinct_source_id == key.distinct_source_id
		    && structure->size == sizeof(TypeStructure) + key.tag_and_attach.count()
			&& memcmp(structure->attach, key.tag_and_attach.begin(), key.tag_and_attach.count()) == 0;
	}

	void init(DeduplicatedTypeInit key, u32 key_hash) noexcept
	{
		TypeStructure* const structure = make_structure(key.types, key.tag_and_attach.attachment(), key.tag_and_attach.as_byte_range(), key.tag_and_attach.count(), true, key.distinct_source_id);

		type_id = id_from_structure(key.types, structure);
		m_hash = key_hash;
	}
};

struct TypePool
{
	IndexMap<DeduplicatedTypeInit, DeduplicatedTypeInfo> dedup;

	ReservedHeap<MIN_STRUCTURE_SIZE_LOG2, MAX_STRUCTURE_SIZE_LOG2> structures;

	MutRange<byte> memory;
};





static TypeId id_from_structure(const TypePool* types, const TypeStructure* structure) noexcept
{
	return static_cast<TypeId>(reinterpret_cast<const u64*>(structure) - reinterpret_cast<const u64*>(types->structures.begin()));
}

static TypeStructure* structure_from_id(TypePool* types, TypeId id) noexcept
{
	return reinterpret_cast<TypeStructure*>(reinterpret_cast<u64*>(types->structures.begin()) + static_cast<u32>(id));
}

static TypeStructure* make_structure(TypePool* types, TypeTag tag, Range<byte> attach, u64 reserve_size, bool is_fixed, SourceId distinct_source_id) noexcept
{
	ASSERT_OR_IGNORE(reserve_size <= UINT16_MAX && reserve_size >= attach.count());

	ASSERT_OR_IGNORE(is_fixed || tag == TypeTag::Composite);

	MutRange<byte> memory = types->structures.alloc(static_cast<u32>(sizeof(TypeStructure) + reserve_size));

	TypeStructure* const structure = reinterpret_cast<TypeStructure*>(memory.begin());
	structure->size = static_cast<u16>(sizeof(TypeStructure) + attach.count());
	structure->capacity_log2 = count_trailing_zeros_assume_one(memory.count());
	structure->is_fixed = is_fixed;
	structure->tag = tag;
	structure->holotype_id = id_from_structure(types, structure);
	structure->distinct_source_id = distinct_source_id;
	structure->indirection_type_id = TypeId::INVALID;

	memcpy(structure->attach, attach.begin(), attach.count());

	return structure;
}

static TypeStructure* make_indirection(TypePool* types, TypeId indirected_type_id) noexcept
{
	MutRange<byte> memory = types->structures.alloc(sizeof(TypeStructure));

	TypeStructure* const structure = reinterpret_cast<TypeStructure*>(memory.begin());
	structure->size = sizeof(TypeStructure);
	structure->capacity_log2 = count_trailing_zeros_assume_one(memory.count());
	structure->is_fixed = true;
	structure->tag = TypeTag::INDIRECTION;
	structure->holotype_id = indirected_type_id;
	structure->distinct_source_id = SourceId::INVALID;
	structure->indirection_type_id = indirected_type_id;

	return structure;
}

static TypeId type_create_deduplicated(TypePool* types, TypeTag tag, Range<byte> attach_bytes) noexcept
{
	DeduplicatedTypeInit init;
	init.tag_and_attach = { attach_bytes, tag };
	init.types = types;
	init.distinct_source_id = SourceId::INVALID;

	const u32 hash = fnv1a_step(fnv1a(attach_bytes), static_cast<byte>(tag));

	return types->dedup.value_from(init, hash)->type_id;
}

static void unify_holotype(TypeStructure* a, TypeStructure* b) noexcept
{
	const TypeId h_a = a->holotype_id;

	const TypeId h_b = b->holotype_id;

	ASSERT_OR_IGNORE(h_a != h_b && h_a != TypeId::INVALID && h_b != TypeId::INVALID);

	if (h_a > h_b)
		a->holotype_id = h_b;
	else
		b->holotype_id = h_b;
}

static void unify_holotype_with_indirection(TypeStructure* a, TypeStructure* indirect_a, TypeStructure* b, TypeStructure* indirect_b) noexcept
{
	const TypeId h_a = indirect_a->holotype_id;

	const TypeId h_b = indirect_b->holotype_id;

	ASSERT_OR_IGNORE(h_a != h_b && h_a != TypeId::INVALID && h_b != TypeId::INVALID);

	if (h_a > h_b)
	{
		indirect_a->holotype_id = h_b;
		a->holotype_id = h_b;
	}
	else
	{
		indirect_b->holotype_id = h_a;
		b->holotype_id = h_a;
	}
}

static bool type_can_implicitly_convert_from_to_assume_unequal(TypePool* types, TypeId from_type_id, TypeId to_type_id) noexcept
{
	const TypeStructure* const from = structure_from_id(types, from_type_id);

	const TypeStructure* const to = structure_from_id(types, to_type_id);

	if (to->tag == TypeTag::TypeInfo)
		return true;

	switch (from->tag)
	{
	case TypeTag::Void:
	case TypeTag::Type:
	case TypeTag::Definition:
	case TypeTag::Boolean:
	case TypeTag::TypeBuilder:
	case TypeTag::TypeInfo:
	case TypeTag::Integer:
	case TypeTag::Float:
	case TypeTag::Func:
	case TypeTag::Builtin:
	case TypeTag::Composite:
	case TypeTag::TailArray:
	case TypeTag::Variadic:
	case TypeTag::Trait:
	case TypeTag::CompositeLiteral:
	case TypeTag::ArrayLiteral:
	{
		return false;
	}

	case TypeTag::CompInteger:
	{
		return to->tag == TypeTag::Integer;
	}

	case TypeTag::CompFloat:
	{
		return to->tag == TypeTag::Float;
	}

	case TypeTag::Divergent:
	{
		return true;
	}

	case TypeTag::Slice:
	{
		if (to->tag != TypeTag::Slice)
			return false;

		const ReferenceType* const from_attach = reinterpret_cast<const ReferenceType*>(from->attach);

		const ReferenceType* const to_attach = reinterpret_cast<const ReferenceType*>(to->attach);

		return from_attach->is_mut || !to_attach->is_mut;
	}

	case TypeTag::Ptr:
	{
		if (to->tag != TypeTag::Ptr)
			return false;

		const ReferenceType* const from_attach = reinterpret_cast<const ReferenceType*>(from->attach);

		const ReferenceType* const to_attach = reinterpret_cast<const ReferenceType*>(to->attach);

		return (from_attach->is_mut || !to_attach->is_mut)
		    && (!from_attach->is_opt || to_attach->is_opt)
			&& (from_attach->is_multi || !to_attach->is_multi);
	}

	case TypeTag::Array:
	{
		if (to->tag != TypeTag::Slice)
			return false;

		const ArrayType* const from_attach = reinterpret_cast<const ArrayType*>(from->attach);

		const ReferenceType* const to_attach = reinterpret_cast<const ReferenceType*>(to->attach);

		return type_is_equal(types, from_attach->element_type, to_attach->referenced_type_id);
	}

	case TypeTag::INVALID:
	case TypeTag::INDIRECTION:
		break; // Fallthrough to unreachable. 
	}

	ASSERT_UNREACHABLE;
}

static void eq_state_init(EqualityState* out) noexcept
{
	out->stack_used = 0;

	out->delayed_used = 0;
}

static bool eq_state_push(EqualityState* state, TypeId a, TypeId b) noexcept
{
	ASSERT_OR_IGNORE(a != TypeId::INVALID && b != TypeId::INVALID && a != b);

	for (u16 i = 0; i != state->stack_used; ++i)
	{
		const TypeId seen = state->stack[i];

		if (seen == a)
			a = TypeId::INVALID;

		if (seen == b)
			b = TypeId::INVALID;
	}

	if (a == TypeId::INVALID || b == TypeId::INVALID)
		return false;

	if (static_cast<u32>(state->stack_used + 2) > array_count(state->stack))
		panic("Maximum depth %u of type equality check exceeded.\n", array_count(state->stack));

	state->stack[state->stack_used] = a;
	state->stack[state->stack_used + 1] = b;

	state->stack_used += 2;

	return true;
}

static void eq_state_pop(EqualityState* state) noexcept
{
	ASSERT_OR_IGNORE(state->stack_used >= 2);

	state->stack_used -= 2;
}

static void eq_state_add_delay_unify(EqualityState* state, TypeId a, TypeId b) noexcept
{
	if (static_cast<u32>(state->delayed_used + 1) > array_count(state->delayed))
	{
		warn("Maximum number %u of delayed holotype unifications exceeded. This is not fatal, but might slow down future equality checks, and means that the number of delay slots in `TypeEqualityState` might need to be increased\n", array_count(state->stack));

		return;
	}

	state->delayed[state->delayed_used] = { a, b };
}

static void eq_state_unify_delayed(TypePool* types, EqualityState* state) noexcept
{
	for (u16 i = 0; i != state->delayed_used; ++i)
	{
		const HolotypeInfo to_unify = state->delayed[i];

		TypeStructure* a = structure_from_id(types, to_unify.a);

		TypeStructure* b = structure_from_id(types, to_unify.b);

		if (a->tag == TypeTag::INDIRECTION || b->tag == TypeTag::INDIRECTION)
		{
			TypeStructure* const direct_a = a->tag == TypeTag::INDIRECTION
				? structure_from_id(types, a->indirection_type_id)
				: a;

			TypeStructure* const direct_b = b->tag == TypeTag::INDIRECTION
				? structure_from_id(types, b->indirection_type_id)
				: b;

			unify_holotype_with_indirection(direct_a, a, direct_b, b);
		}
		else
		{
			unify_holotype(a, b);
		}
	}
}

static TypeEq type_is_equal_noloop(TypePool* types, TypeId type_id_a, TypeId type_id_b, EqualityState* seen, bool treat_loop_as_maybe_equal) noexcept
{
	if (type_id_a == type_id_b)
		return TypeEq::Equal;

	TypeStructure* a = structure_from_id(types, type_id_a);

	if (a->tag == TypeTag::INDIRECTION)
	{
		a = structure_from_id(types, a->indirection_type_id);

		ASSERT_OR_IGNORE(a->tag == TypeTag::Composite);
	}

	TypeStructure* b = structure_from_id(types, type_id_b);

	if (b->tag == TypeTag::INDIRECTION)
	{
		b = structure_from_id(types, b->indirection_type_id);

		ASSERT_OR_IGNORE(b->tag == TypeTag::Composite);
	}

	// This is assumed to be the common case, so we check for it as early as
	// possible.
	if (a->holotype_id == b->holotype_id)
		return TypeEq::Equal;

	// Types with differing tags can never be equal.
	if (a->tag != b->tag)
		return TypeEq::Unequal;

	// Since these types do not reference any other types and are deduplicated,
	// comparing their `TypeId`s is actually sufficient.
	// If they differ, we cannot be dealing with equal types.
	if (a->tag <= TypeTag::Float)
		return TypeEq::Unequal;

	// Types from different sources can definitionally never be equal. 
	if (a->distinct_source_id != b->distinct_source_id)
		return TypeEq::Unequal;

	const TypeTag tag = a->tag;

	if (!eq_state_push(seen, type_id_a, type_id_b))
	{
		if (treat_loop_as_maybe_equal)
			return TypeEq::MaybeEqual;

		panic("Type loop detected.\n");
	}

	switch (tag)
	{
	case TypeTag::Composite:
	{
		const CompositeType* const a_attach = reinterpret_cast<CompositeType*>(a->attach);

		const CompositeType* const b_attach = reinterpret_cast<CompositeType*>(b->attach);

		if (a_attach->header.size != b_attach->header.size
		 || a_attach->header.stride != b_attach->header.stride
		 || a_attach->header.align_log2 != b_attach->header.align_log2
		 || a_attach->header.disposition != b_attach->header.disposition
		 || a_attach->header.member_count != b_attach->header.member_count)
		{
			eq_state_pop(seen);

			return TypeEq::Unequal;
		}

		TypeEq result = TypeEq::Equal;

		for (u16 rank = 0; rank != a_attach->header.member_count; ++rank)
		{
			const Member* const a_member = a_attach->members + rank;
			
			const Member* const b_member = b_attach->members + rank;

			if (a_member->name != b_member->name
			 || a_member->offset != b_member->offset
			 || a_member->is_global != b_member->is_global
			 || a_member->is_mut != b_member->is_mut
			 || a_member->is_param != b_member->is_param
			 || a_member->is_pub != b_member->is_pub)
			{
				eq_state_pop(seen);
			
				return TypeEq::Unequal;
			}

			if (a_member->has_pending_type || b_member->has_pending_type)
				TODO("Handle pending composite member types in `type_is_equal_noloop`.");

			const TypeId a_member_type_id = a_member->type.complete;

			const TypeId b_member_type_id = b_member->type.complete;

			const TypeEq member_result = type_is_equal_noloop(types, a_member_type_id, b_member_type_id, seen, false);

			if (member_result == TypeEq::Unequal)
			{
				eq_state_pop(seen);

				return TypeEq::Unequal;
			}

			if (result == TypeEq::Equal)
				result = member_result;

			if (a_member->has_pending_value || b_member->has_pending_value)
				TODO("Handle pending composite member default values in `type_is_equal_noloop`.");

			if (a_member->value.complete != GlobalValueId::INVALID && b_member->value.complete != GlobalValueId::INVALID)
				TODO("Handle composite member default values in `type_is_equal_noloop`.");

			if (a_member->value.complete != GlobalValueId::INVALID || b_member->value.complete != GlobalValueId::INVALID)
			{
				eq_state_pop(seen);

				return TypeEq::Unequal;
			}
		}

		if (result == TypeEq::Equal)
			unify_holotype_with_indirection(a, structure_from_id(types, type_id_a), b, structure_from_id(types, type_id_b));
		else if (result == TypeEq::MaybeEqual)
			eq_state_add_delay_unify(seen, type_id_a, type_id_b);

		eq_state_pop(seen);


		return result;
	}

	case TypeTag::TailArray:
	case TypeTag::Slice:
	case TypeTag::Ptr:
	{
		const ReferenceType* const a_attach = reinterpret_cast<ReferenceType*>(a->attach);
		
		const ReferenceType* const b_attach = reinterpret_cast<ReferenceType*>(b->attach);

		if (a_attach->is_multi != b_attach->is_multi || a_attach->is_mut != b_attach->is_mut || a_attach->is_opt != b_attach->is_opt)
		{
			eq_state_pop(seen);

			return TypeEq::Unequal;
		}

		const TypeId a_next = a_attach->referenced_type_id;

		const TypeId b_next = b_attach->referenced_type_id;

		const TypeEq reference_result = type_is_equal_noloop(types, a_next, b_next, seen, true);

		if (reference_result == TypeEq::Equal)
			unify_holotype(a, b);
		else if (reference_result == TypeEq::MaybeEqual)
			eq_state_add_delay_unify(seen, type_id_a, type_id_b);

		eq_state_pop(seen);

		return reference_result;
	}

	case TypeTag::Array:
	{
		const ArrayType* const a_attach = reinterpret_cast<ArrayType*>(a->attach);
		
		const ArrayType* const b_attach = reinterpret_cast<ArrayType*>(b->attach);

		if (a_attach->element_count != b_attach->element_count)
		{
			eq_state_pop(seen);

			return TypeEq::Unequal;
		}

		const TypeId a_next = a_attach->element_type;

		const TypeId b_next = b_attach->element_type;

		const TypeEq element_result = type_is_equal_noloop(types, a_next, b_next, seen, false);

		if (element_result == TypeEq::Equal)
			unify_holotype(a, b);
		else if (element_result == TypeEq::MaybeEqual)
			eq_state_add_delay_unify(seen, type_id_a, type_id_b);

		eq_state_pop(seen);

		return element_result;
	}

	case TypeTag::Func:
	case TypeTag::Builtin:
	{
		const SignatureType* const a_attach = reinterpret_cast<SignatureType*>(a->attach);

		const SignatureType* const b_attach = reinterpret_cast<SignatureType*>(b->attach);

		if (a_attach->parameter_list_is_unbound || b_attach->parameter_list_is_unbound || a_attach->return_type_is_unbound || b_attach->return_type_is_unbound)
			TODO("Handle unbound signatures in `type_is_equal_noloop`.");

		if (a_attach->is_proc != b_attach->is_proc || a_attach->param_count != b_attach->param_count)
		{
			eq_state_pop(seen);

			return TypeEq::Unequal;
		}

		const TypeId a_return_type_id = a_attach->return_type.complete;

		const TypeId b_return_type_id = b_attach->return_type.complete;

		const TypeEq return_type_result = type_is_equal_noloop(types, a_return_type_id, b_return_type_id, seen, false);

		if (return_type_result == TypeEq::Unequal)
		{
			eq_state_pop(seen);

			return TypeEq::Unequal;
		}

		const TypeId a_params_type_id = a_attach->parameter_list_type_id;

		const TypeId b_params_type_id = b_attach->parameter_list_type_id;

		const TypeEq params_type_result = type_is_equal_noloop(types, a_params_type_id, b_params_type_id, seen, false);

		if (params_type_result == TypeEq::Unequal)
		{
			eq_state_pop(seen);

			return TypeEq::Unequal;
		}

		eq_state_pop(seen);

		if (params_type_result == TypeEq::Equal && return_type_result == TypeEq::Equal)
		{
			unify_holotype(a, b);

			return TypeEq::Equal;
		}
		else
		{
			eq_state_add_delay_unify(seen, type_id_a, type_id_b);

			return TypeEq::MaybeEqual;
		}
	}

	case TypeTag::CompositeLiteral:
	case TypeTag::ArrayLiteral:
	case TypeTag::Variadic:
	case TypeTag::Trait:
		TODO("Implement `type_is_equal_noloop` for CompositeLiteral, ArrayLiteral, Variadic and Trait");

	case TypeTag::INVALID:
	case TypeTag::INDIRECTION:
	case TypeTag::Void:
	case TypeTag::Type:
	case TypeTag::Definition:
	case TypeTag::CompInteger:
	case TypeTag::CompFloat:
	case TypeTag::Boolean:
	case TypeTag::TypeInfo:
	case TypeTag::TypeBuilder:
	case TypeTag::Divergent:
	case TypeTag::Integer:
	case TypeTag::Float:
		break; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}



TypePool* create_type_pool(AllocPool* alloc) noexcept
{
	static constexpr u32 STRUCTURES_CAPACITIES[MAX_STRUCTURE_SIZE_LOG2 - MIN_STRUCTURE_SIZE_LOG2 + 1] = {
		131072, 65536, 65536, 32768, 16384,
		  4096,  1024,   256,    64,
	};

	static constexpr u32 STRUCTURES_COMMITS[MAX_STRUCTURE_SIZE_LOG2 - MIN_STRUCTURE_SIZE_LOG2 + 1] = {
		1024, 512, 256, 128, 64,
		  16,   4,   1,   1,
	};

	u64 structures_size = 0;

	for (u32 i = 0; i != array_count(STRUCTURES_CAPACITIES); ++i)
		structures_size += static_cast<u64>(STRUCTURES_CAPACITIES[i]) << (i + MIN_STRUCTURE_SIZE_LOG2);

	byte* const memory = static_cast<byte*>(minos::mem_reserve(structures_size));

	if (memory == nullptr)
		panic("Could not reserve memory for TypePool (0x%X).\n", minos::last_error());

	TypePool* const types = static_cast<TypePool*>(alloc_from_pool(alloc, sizeof(TypePool), alignof(TypePool)));
	types->dedup.init(1 << 21, 1 << 8, 1 << 20, 1 << 10);
	types->structures.init({ memory, structures_size }, Range{ STRUCTURES_CAPACITIES }, Range{ STRUCTURES_COMMITS });
	types->memory = { memory, structures_size };

	// Reserve `0` as `TypeId::INVALID`.
	types->structures.alloc(sizeof(TypeStructure));

	// Reserve simple types for use with `type_create_simple`.
	for (u8 ordinal = static_cast<u8>(TypeTag::Void); ordinal != static_cast<u8>(TypeTag::Divergent) + 1; ++ordinal)
		(void) make_structure(types, static_cast<TypeTag>(ordinal), {}, 0, true, SourceId::INVALID);

	return types;
}

void release_type_pool(TypePool* types) noexcept
{
	types->dedup.release();

	minos::mem_unreserve(types->memory.begin(), types->memory.count());
}



TypeId type_create_simple(TypePool* types, TypeTag tag) noexcept
{
	ASSERT_OR_IGNORE(tag >= TypeTag::Void && tag <= TypeTag::Divergent);

	const TypeId type_id = static_cast<TypeId>((static_cast<u32>(tag) - 1) * 2);

	ASSERT_OR_IGNORE(type_tag_from_id(types, type_id) == tag);

	return type_id;
}

TypeId type_create_numeric(TypePool* types, TypeTag tag, NumericType attach) noexcept
{
	ASSERT_OR_IGNORE(tag == TypeTag::Integer || tag == TypeTag::Float);

	return type_create_deduplicated(types, tag, range::from_object_bytes(&attach));
}

TypeId type_create_reference(TypePool* types, TypeTag tag, ReferenceType attach) noexcept
{
	ASSERT_OR_IGNORE(tag == TypeTag::Ptr || tag == TypeTag::Slice || tag == TypeTag::TailArray);

	return type_create_deduplicated(types, tag, range::from_object_bytes(&attach));
}

TypeId type_create_array(TypePool* types, TypeTag tag, ArrayType attach) noexcept
{
	ASSERT_OR_IGNORE(tag == TypeTag::Array);

	return type_create_deduplicated(types, tag, range::from_object_bytes(&attach));
}

TypeId type_create_signature(TypePool* types, TypeTag tag, SignatureType attach) noexcept
{
	ASSERT_OR_IGNORE(tag == TypeTag::Builtin || tag == TypeTag::Func);

	return type_create_deduplicated(types, tag, range::from_object_bytes(&attach));
}

TypeId type_create_composite(TypePool* types, TypeId lexical_parent_type_id, TypeDisposition disposition, SourceId distinct_source_id, u32 initial_member_capacity, bool is_fixed_member_capacity) noexcept
{
	CompositeTypeHeader header{};
	header.size = 0;
	header.stride = 0;
	header.align_log2 = 0;
	header.member_count = 0;
	header.incomplete_member_count = 0;
	header.disposition = disposition;
	header.is_open = true;
	header.lexical_parent_type_id = lexical_parent_type_id;

	const u64 reserve_size = sizeof(CompositeType) + initial_member_capacity * sizeof(Member);

	TypeStructure* const structure = make_structure(types, TypeTag::Composite, range::from_object_bytes(&header), reserve_size, is_fixed_member_capacity, distinct_source_id);

	const TypeId structure_type_id = id_from_structure(types, structure);

	if (is_fixed_member_capacity)
		return structure_type_id;

	TypeStructure* const indirection = make_indirection(types, structure_type_id);

	const TypeId indirection_type_id = id_from_structure(types, indirection);

	structure->holotype_id = indirection_type_id;

	return indirection_type_id;
}

TypeId type_seal_composite(TypePool* types, TypeId type_id, u64 size, u32 align, u64 stride) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	ASSERT_OR_IGNORE(align <= UINT16_MAX);

	TypeStructure* structure = structure_from_id(types, type_id);

	if (structure->tag == TypeTag::INDIRECTION)
	{
		structure = structure_from_id(types, structure->indirection_type_id);

		structure->is_fixed = true;
	}

	ASSERT_OR_IGNORE(structure->tag == TypeTag::Composite);

	CompositeType* const composite = reinterpret_cast<CompositeType*>(structure->attach);

	ASSERT_OR_IGNORE(composite->header.is_open);

	if (composite->header.disposition == TypeDisposition::User)
	{
		ASSERT_OR_IGNORE(align != 0 && is_pow2(align));

		composite->header.size = size;
		composite->header.align_log2 = count_trailing_zeros_assume_one(align);
		composite->header.stride = stride;
	}
	else
	{
		ASSERT_OR_IGNORE(size == 0 && align == 0 && stride == 0);

		composite->header.stride = next_multiple(composite->header.size, static_cast<u64>(1) << composite->header.align_log2);
	}

	composite->header.is_open = false;

	return id_from_structure(types, structure);
}

void type_add_composite_member(TypePool* types, TypeId type_id, Member member) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID && member.rank == 0);

	TypeStructure* const structure = structure_from_id(types, type_id);

	TypeStructure* direct_structure = structure->tag == TypeTag::INDIRECTION
		? structure_from_id(types, structure->indirection_type_id)
		: structure;

	ASSERT_OR_IGNORE(direct_structure->tag == TypeTag::Composite);

	CompositeType* composite = reinterpret_cast<CompositeType*>(direct_structure->attach);

	ASSERT_OR_IGNORE(composite->header.is_open);

	ASSERT_OR_IGNORE(composite->header.disposition == TypeDisposition::User || member.offset == 0);

	if (direct_structure->size + sizeof(Member) > static_cast<u64>(1) << direct_structure->capacity_log2)
	{
		ASSERT_OR_IGNORE(!direct_structure->is_fixed);

		const Range<byte> composite_bytes = { reinterpret_cast<byte*>(composite), sizeof(CompositeTypeHeader) + composite->header.member_count * sizeof(Member) };

		direct_structure = make_structure(types, TypeTag::Composite, composite_bytes, composite_bytes.count() + sizeof(Member), false, direct_structure->distinct_source_id);

		composite = reinterpret_cast<CompositeType*>(direct_structure->attach);

		structure->indirection_type_id = id_from_structure(types, direct_structure);
	}

	if (composite->header.disposition != TypeDisposition::User && !member.has_pending_type)
	{
		const TypeMetrics metrics = type_metrics_from_id(types, member.type.complete);

		const u64 member_begin = next_multiple(composite->header.size, static_cast<u64>(metrics.align));

		member.offset = member_begin;

		composite->header.size = member_begin + metrics.size;

		const u8 align_log2 = count_trailing_zeros_assume_one(metrics.align);

		if (composite->header.align_log2 < align_log2)
			composite->header.align_log2 = align_log2;
	}

	composite->members[composite->header.member_count] = member;
	composite->members[composite->header.member_count].rank = composite->header.member_count;

	if (member.has_pending_type || member.has_pending_value)
		composite->header.incomplete_member_count += 1;

	composite->header.member_count += 1;

	direct_structure->size += sizeof(Member);
}

void type_set_composite_member_info(TypePool* types, TypeId type_id, u16 rank, MemberCompletionInfo info) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	TypeStructure* structure = structure_from_id(types, type_id);

	if (structure->tag == TypeTag::INDIRECTION)
		structure = structure_from_id(types, structure->indirection_type_id);

	ASSERT_OR_IGNORE(structure->tag == TypeTag::Composite);

	CompositeType* const composite = reinterpret_cast<CompositeType*>(structure->attach);

	ASSERT_OR_IGNORE(rank < composite->header.member_count);

	Member* const member = composite->members + rank;

	if (info.has_type_id)
	{
		ASSERT_OR_IGNORE(member->has_pending_type && info.type_id != TypeId::INVALID);

		member->type.complete = info.type_id;
		member->has_pending_type = false;
		member->type_completion_arec_id = ArecId::INVALID;

		if (!member->has_pending_value)
		{
			ASSERT_OR_IGNORE(composite->header.incomplete_member_count != 0);

			composite->header.incomplete_member_count -= 1;
		}

		if (composite->header.disposition != TypeDisposition::User)
		{
			const TypeMetrics metrics = type_metrics_from_id(types, info.type_id);

			const u64 member_begin = next_multiple(composite->header.size, static_cast<u64>(metrics.align));

			member->offset = member_begin;

			composite->header.size = member_begin + metrics.size;

			const u8 align_log2 = count_trailing_zeros_assume_one(metrics.align);

			if (composite->header.align_log2 < align_log2)
				composite->header.align_log2 = align_log2;
		}
	}

	if (info.has_value_id)
	{
		ASSERT_OR_IGNORE(!member->has_pending_type && member->has_pending_value && info.value_id != GlobalValueId::INVALID);

		member->value.complete = info.value_id;
		member->has_pending_value = false;
		member->value_completion_arec_id = ArecId::INVALID;

		ASSERT_OR_IGNORE(composite->header.incomplete_member_count != 0);

		composite->header.incomplete_member_count -= 1;
	}
}

TypeId type_copy_composite(TypePool* types, TypeId type_id, u32 initial_member_capacity, bool is_fixed_member_capacity) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeStructure* old_structure = structure_from_id(types, type_id);

	if (old_structure->tag == TypeTag::INDIRECTION)
		old_structure = structure_from_id(types, old_structure->indirection_type_id);

	ASSERT_OR_IGNORE(old_structure->tag == TypeTag::Composite);

	const CompositeType* const old_composite = reinterpret_cast<const CompositeType*>(old_structure->attach);

	if (old_composite->header.member_count > initial_member_capacity)
		initial_member_capacity = old_composite->header.incomplete_member_count;

	const u32 adj_member_capacity = !old_composite->header.is_open || old_composite->header.member_count > initial_member_capacity
		? old_composite->header.member_count
		: initial_member_capacity;

	const u64 reserve_size = sizeof(CompositeTypeHeader) + adj_member_capacity * sizeof(Member);

	const Range<byte> to_copy{ reinterpret_cast<const byte*>(old_composite), sizeof(CompositeTypeHeader) + old_composite->header.member_count * sizeof(Member) };

	TypeStructure* const new_structure = make_structure(types, TypeTag::Composite, to_copy, reserve_size, is_fixed_member_capacity, old_structure->distinct_source_id);

	const TypeId new_structure_type_id = id_from_structure(types, new_structure);

	if (is_fixed_member_capacity)
		return new_structure_type_id;

	TypeStructure* const indirection = make_indirection(types, new_structure_type_id);

	const TypeId indirection_type_id = id_from_structure(types, indirection);

	return indirection_type_id;
}



bool type_is_equal(TypePool* types, TypeId type_id_a, TypeId type_id_b) noexcept
{
	ASSERT_OR_IGNORE(type_id_a != TypeId::INVALID && type_id_b != TypeId::INVALID);

	// Equal `TypeId`s imply equal types. This is checked here redundantly even
	// though it is also checked in `type_is_equal_noloop`. This is to possibly
	// let the compiler optimize this case better, as it is quite common.
	if (type_id_a == type_id_b)
		return true;

	EqualityState seen;
	eq_state_init(&seen);

	const TypeEq result = type_is_equal_noloop(types, type_id_a, type_id_b, &seen, false);
	
	if (result == TypeEq::Unequal)
		return false;
	
	if (result == TypeEq::MaybeEqual)
		eq_state_unify_delayed(types, &seen);

	return true;
}

bool type_can_implicitly_convert_from_to(TypePool* types, TypeId from_type_id, TypeId to_type_id) noexcept
{
	ASSERT_OR_IGNORE(from_type_id != TypeId::INVALID && to_type_id != TypeId::INVALID);

	if (type_is_equal(types, from_type_id, to_type_id))
		return true;

	return type_can_implicitly_convert_from_to_assume_unequal(types, from_type_id, to_type_id);
}

TypeId type_unify(TypePool* types, TypeId type_id_a, TypeId type_id_b) noexcept
{
	ASSERT_OR_IGNORE(type_id_a != TypeId::INVALID && type_id_b != TypeId::INVALID);

	if (type_is_equal(types, type_id_a, type_id_b))
		return type_id_a < type_id_b ? type_id_a : type_id_b;

	if (type_can_implicitly_convert_from_to_assume_unequal(types, type_id_a, type_id_b))
		return type_id_b;

	if (type_can_implicitly_convert_from_to_assume_unequal(types, type_id_b, type_id_a))
		return type_id_a;

	return TypeId::INVALID;
}



TypeDisposition type_disposition_from_id(TypePool* types, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeStructure* structure = structure_from_id(types, type_id);

	if (structure->tag == TypeTag::INDIRECTION)
		structure = structure_from_id(types, structure->indirection_type_id);

	ASSERT_OR_IGNORE(structure->tag == TypeTag::Composite);

	const CompositeType* const composite = reinterpret_cast<const CompositeType*>(structure->attach);

	return composite->header.disposition;
}

TypeId lexical_parent_type_from_id(TypePool* types, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeStructure* structure = structure_from_id(types, type_id);

	if (structure->tag == TypeTag::INDIRECTION)
		structure = structure_from_id(types, structure->indirection_type_id);

	ASSERT_OR_IGNORE(structure->tag == TypeTag::Composite);

	const CompositeType* const composite = reinterpret_cast<const CompositeType*>(structure->attach);

	return composite->header.lexical_parent_type_id;
}

bool type_has_metrics(TypePool* types, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeStructure* structure = structure_from_id(types, type_id);

	if (structure->tag == TypeTag::INDIRECTION)
	{
		structure = structure_from_id(types, structure->indirection_type_id);

		ASSERT_OR_IGNORE(structure->tag == TypeTag::Composite);
	}
	else if (structure->tag != TypeTag::Composite)
	{
		return true;
	}

	const CompositeType* const composite = reinterpret_cast<const CompositeType*>(structure->attach);

	return composite->header.disposition != TypeDisposition::User || !composite->header.is_open;
}

TypeMetrics type_metrics_from_id(TypePool* types, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeStructure* structure = structure_from_id(types, type_id);

	if (structure->tag == TypeTag::INDIRECTION)
	{
		structure = structure_from_id(types, structure->indirection_type_id);

		ASSERT_OR_IGNORE(structure->tag == TypeTag::Composite);
	}

	switch (structure->tag)
	{
	case TypeTag::Void:
	{
		return { 0, 0, 1 };
	}

	case TypeTag::Type:
	case TypeTag::TypeInfo:
	case TypeTag::TypeBuilder:
	{
		return { 4, 4, 4 };
	}

	case TypeTag::Definition:
	{
		return { sizeof(Definition), sizeof(Definition), alignof(Definition) };
	}

	case TypeTag::CompInteger:
	{
		return { sizeof(CompIntegerValue), sizeof(CompIntegerValue), alignof(CompIntegerValue) };
	}

	case TypeTag::CompFloat:
	{
		return { sizeof(CompFloatValue), sizeof(CompFloatValue), alignof(CompFloatValue) };
	}

	case TypeTag::Boolean:
	{
		return { 1, 1, 1 };
	}

	case TypeTag::Divergent:
	{
		return { 0, 0, 1 };
	}

	case TypeTag::Integer:
	case TypeTag::Float:
	{
		const NumericType* const numeric = reinterpret_cast<const NumericType*>(structure->attach);

		const u32 bytes = (numeric->bits + 7) / 8;

		const u32 pow2_bytes = next_pow2(bytes);

		return { pow2_bytes, pow2_bytes, pow2_bytes };
	}

	case TypeTag::Slice:
	{
		return { 16, 16, 8 };
	}

	case TypeTag::Ptr:
	{
		return { 8, 8, 8 };
	}

	case TypeTag::Array:
	{
		const ArrayType* const array = reinterpret_cast<const ArrayType*>(structure->attach);

		if (array->element_count == 0)
			return { 0, 0, 1 };

		const TypeMetrics element_metrics = type_metrics_from_id(types, array->element_type);

		return { element_metrics.stride * (array->element_count - 1) + element_metrics.size, (element_metrics.stride * array->element_count), element_metrics.align };
	}

	case TypeTag::Func:
	case TypeTag::Builtin:
	{
		return { 8, 8, 8 };
	}

	case TypeTag::Composite:
	{
		const CompositeType* const composite = reinterpret_cast<const CompositeType*>(structure->attach);

		ASSERT_OR_IGNORE(!composite->header.is_open);

		return { composite->header.size, composite->header.stride, static_cast<u32>(1) << composite->header.align_log2 };
	}

	case TypeTag::TailArray:
	case TypeTag::CompositeLiteral:
	case TypeTag::ArrayLiteral:
	case TypeTag::Variadic:
	case TypeTag::Trait:
		TODO("Implement `type_metrics_from_id` for TailArray, CompositeLiteral, ArrayLiteral, Variadic and Trait.");

	case TypeTag::INVALID:
	case TypeTag::INDIRECTION:
		break; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

TypeTag type_tag_from_id(TypePool* types, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeStructure* structure = structure_from_id(types, type_id);

	if (structure->tag == TypeTag::INDIRECTION)
		return TypeTag::Composite;
		
	return structure->tag;
}

const Member* type_member_by_rank(TypePool* types, TypeId type_id, u16 rank)
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeStructure* structure = structure_from_id(types, type_id);

	if (structure->tag == TypeTag::INDIRECTION)
		structure = structure_from_id(types, structure->indirection_type_id);

	ASSERT_OR_IGNORE(structure->tag == TypeTag::Composite);

	const CompositeType* const composite = reinterpret_cast<const CompositeType*>(structure->attach);

	ASSERT_OR_IGNORE(composite->header.member_count > rank);

	return composite->members + rank;
}

bool type_member_by_name(TypePool* types, TypeId type_id, IdentifierId name, const Member** out) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeStructure* structure = structure_from_id(types, type_id);

	if (structure->tag == TypeTag::INDIRECTION)
		structure = structure_from_id(types, structure->indirection_type_id);

	ASSERT_OR_IGNORE(structure->tag == TypeTag::Composite);

	const CompositeType* const composite = reinterpret_cast<const CompositeType*>(structure->attach);

	for (u16 rank = 0; rank != composite->header.member_count; ++rank)
	{
		if (composite->members[rank].name == name)
		{
			*out = composite->members + rank;

			return true;
		}
	}

	return false;
}

const void* type_attachment_from_id_raw(TypePool* types, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeStructure* structure = structure_from_id(types, type_id);

	if (structure->tag == TypeTag::INDIRECTION)
	{
		structure = structure_from_id(types, structure->indirection_type_id);

		ASSERT_OR_IGNORE(structure->tag == TypeTag::Composite);
	}

	return structure->attach;
}



const char8* tag_name(TypeTag tag) noexcept
{
	static constexpr const char8* TYPE_TAG_NAMES[] = {
		"[Unknown]",
		"[FIXEDPTR_INDIRECTION]",
		"Void",
		"Type",
		"Definition",
		"CompInteger",
		"CompFloat",
		"Integer",
		"Float",
		"Boolean",
		"Slice",
		"Ptr",
		"Array",
		"Func",
		"Builtin",
		"Composite",
		"CompositeLiteral",
		"ArrayLiteral",
		"TypeBuilder",
		"Variadic",
		"Divergent",
		"Trait",
		"TypeInfo",
		"TailArray",
		"Dependent",
	};

	u8 ordinal = static_cast<u8>(tag);

	if (ordinal >= array_count(TYPE_TAG_NAMES))
		ordinal = 0;

	return TYPE_TAG_NAMES[ordinal];
}



IncompleteMemberIterator incomplete_members_of(TypePool* types, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeStructure* const structure = structure_from_id(types, type_id);

	ASSERT_OR_IGNORE(structure->tag == TypeTag::INDIRECTION || structure->tag == TypeTag::Composite);

	const TypeStructure* const direct_structure = structure->tag == TypeTag::INDIRECTION
		? structure_from_id(types, structure->indirection_type_id)
		: structure;

	const CompositeType* const composite = reinterpret_cast<const CompositeType*>(direct_structure->attach);

	u32 first_incomplete_rank = UINT32_MAX;

	if (composite->header.incomplete_member_count != 0)
	{
		for (u16 rank = 0; rank != composite->header.member_count; ++rank)
		{
			if (composite->members[rank].has_pending_type || composite->members[rank].has_pending_value)
			{
				first_incomplete_rank = rank;

				break;
			}
		}
	}

	IncompleteMemberIterator it;
	it.structure = first_incomplete_rank == UINT32_MAX ? nullptr : structure;
	it.types = types;
	it.rank = static_cast<u16>(first_incomplete_rank);
	it.is_indirect = structure->tag == TypeTag::INDIRECTION;

	return it;
}

const Member* next(IncompleteMemberIterator* it) noexcept
{
	ASSERT_OR_IGNORE(has_next(it));

	const TypeStructure* const structure = static_cast<const TypeStructure*>(it->structure);

	ASSERT_OR_IGNORE(structure->tag == TypeTag::INDIRECTION || structure->tag == TypeTag::Composite);

	const TypeStructure* const direct_structure = it->is_indirect
		? structure_from_id(it->types, structure->indirection_type_id)
		: structure;

	const CompositeType* const composite = reinterpret_cast<const CompositeType*>(direct_structure->attach);

	const Member* const result = composite->members + it->rank;

	for (u16 rank = it->rank + 1; rank != composite->header.member_count; ++rank)
	{
		if (composite->members[rank].has_pending_type || composite->members[rank].has_pending_value)
		{
			it->rank = rank;

			return result;
		}
	}

	it->structure = nullptr;

	return result;
}

bool has_next(const IncompleteMemberIterator* it) noexcept
{
	return it->structure != nullptr;
}

MemberIterator members_of(TypePool* types, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeStructure* const structure = structure_from_id(types, type_id);

	ASSERT_OR_IGNORE(structure->tag == TypeTag::INDIRECTION || structure->tag == TypeTag::Composite);

	const TypeStructure* const direct_structure = structure->tag == TypeTag::INDIRECTION
		? structure_from_id(types, structure->indirection_type_id)
		: structure;

	const CompositeType* const composite = reinterpret_cast<const CompositeType*>(direct_structure->attach);

	MemberIterator it;
	it.structure = composite->header.member_count == 0 ? nullptr : structure;
	it.types = types;
	it.rank = 0;
	it.is_indirect = structure->tag == TypeTag::INDIRECTION;

	return it;
}

const Member* next(MemberIterator* it) noexcept
{
	ASSERT_OR_IGNORE(has_next(it));

	const TypeStructure* const structure = static_cast<const TypeStructure*>(it->structure);

	ASSERT_OR_IGNORE(structure->tag == TypeTag::INDIRECTION || structure->tag == TypeTag::Composite);

	const TypeStructure* const direct_structure = it->is_indirect
		? structure_from_id(it->types, structure->indirection_type_id)
		: structure;

	const CompositeType* const composite = reinterpret_cast<const CompositeType*>(direct_structure->attach);

	const Member* const result = composite->members + it->rank;

	it->rank += 1;

	if (composite->header.member_count == it->rank)
		it->structure = nullptr;

	return result;
}

bool has_next(const MemberIterator* it) noexcept
{
	return it->structure != nullptr;
}
