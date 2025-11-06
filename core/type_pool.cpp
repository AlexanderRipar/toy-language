#include "core.hpp"

#include "../infra/hash.hpp"
#include "../infra/container/index_map.hpp"
#include "../infra/container/reserved_heap.hpp"

#include <cstdlib>
#include <cstring>

static constexpr u32 MIN_STRUCTURE_SIZE_LOG2 = 4;

static constexpr u32 MAX_STRUCTURE_SIZE_LOG2 = 12;



struct TypeStructure;

static TypeStructure* structure_from_id(TypePool* types, TypeId id) noexcept;

static TypeStructure* make_structure(TypePool* types, TypeTag tag, Range<byte> attach, u64 reserve_size, SourceId distinct_source_id) noexcept;

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

struct alignas(8) CommonMemberData
{
	DelayableTypeId type;

	DelayableValueId value;

	union
	{
		#if COMPILER_CLANG
		#pragma clang diagnostic push
		#pragma clang diagnostic ignored "-Wnested-anon-types" // anonymous types declared in an anonymous union are an extension
		#elif COMPILER_GCC
		#pragma GCC diagnostic push
		#pragma GCC diagnostic ignored "-Wpedantic" // ISO C++ prohibits anonymous structs
		#endif
		struct
		{
			IdentifierId name;

			bool has_pending_type : 1;

			bool has_pending_value : 1;

			bool is_pub : 1;

			bool is_mut : 1;

			bool is_global : 1;

			bool is_comptime_known : 1;

			bool unused_1_ : 2;

			u8 unused_2_[3];
		};
		#if COMPILER_CLANG
		#pragma clang diagnostic pop
		#elif COMPILER_GCC
		#pragma GCC diagnostic pop
		#endif
		
		u64 bitwise_comparable;
	};
};

struct alignas(8) FileMemberData : CommonMemberData {};

struct alignas(8) BlockOrSignatureOrLiteralMemberData : CommonMemberData
{
	s64 offset;
};

struct alignas(8) UserMemberData : CommonMemberData
{
	s64 offset;

	ArecId type_completion_arec_id;

	ArecId value_completion_arec_id;
};

using FullMemberData = UserMemberData;

struct CompositeType
{
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
			u64 size;
		} general;

		struct
		{
			ArecId completion_arec_id;

			u32 unused_;
		} file;
	};
	#if COMPILER_CLANG
	#pragma clang diagnostic pop
	#elif COMPILER_GCC
	#pragma GCC diagnostic pop
	#endif

	u64 stride;

	u8 align_log2;

	bool is_open : 1;

	bool is_fixed : 1;

	u16 member_count;

	u16 incomplete_member_count;

	TypeDisposition disposition;

	u8 member_stride;

	TypeId global_scope_type_id;

	u16 capacity;

	u16 used;

	// IdentifierId[header->member_count];

	// Depending on header->disposition:
	// TypeDisposition::File -> FileMemberData[header->member_count];
	// TypeDisposition::User -> UserMemberData[header->member_count];
	// otherwise             -> BlockOrSignatureOrLiteralMemberData[header->member_count];
};

struct alignas(8) TypeStructure
{
	u32 tag_bits : 5;

	u32 holotype_id_bits : 27;

	union
	{
		SourceId distinct_source_id;

		TypeId indirection_type_id;
	};

	#if COMPILER_GCC
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wpedantic" // ISO C++ forbids flexible array member
	#endif
	alignas(8) byte attach[];
	#if COMPILER_GCC
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

		ASSERT_OR_IGNORE(static_cast<TypeTag>(structure->tag_bits) != TypeTag::INDIRECTION && static_cast<TypeTag>(structure->tag_bits) != TypeTag::Composite);

		return static_cast<TypeTag>(structure->tag_bits) == key.tag_and_attach.attachment()
		    && structure->distinct_source_id == key.distinct_source_id
			&& memcmp(structure->attach, key.tag_and_attach.begin(), key.tag_and_attach.count()) == 0;
	}

	void init(DeduplicatedTypeInit key, u32 key_hash) noexcept
	{
		TypeStructure* const structure = make_structure(key.types, key.tag_and_attach.attachment(), key.tag_and_attach.as_byte_range(), key.tag_and_attach.count(), key.distinct_source_id);

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




static CommonMemberData* member_at(CompositeType* composite, u32 index) noexcept
{
	byte* const base = reinterpret_cast<byte*>(composite + 1);

	return reinterpret_cast<CommonMemberData*>(base + composite->member_stride * index);
}

static const CommonMemberData* member_at(const CompositeType* composite, u32 index) noexcept
{
	return member_at(const_cast<CompositeType*>(composite), index);
}

static bool find_member_by_name(const CompositeType* composite, IdentifierId name, u16* out_rank, const CommonMemberData** out_data) noexcept
{
	for (u32 i = 0; i != composite->member_count; ++i)
	{
		const CommonMemberData* const member = member_at(composite, i);

		if (member->name == name)
		{
			*out_rank = static_cast<u16>(i);

			*out_data = member;

			return true;
		}
	}

	return false;
}

static MemberInfo fill_member_info(const CompositeType* composite, const CommonMemberData* member, u16 rank) noexcept
{
	MemberInfo info{};
	info.type = member->type;
	info.value = member->value;
	info.name = member->name;
	info.is_pub = member->is_pub;
	info.is_mut = member->is_mut;
	info.is_global = member->is_global;
	info.is_comptime_known = member->is_comptime_known;
	info.has_pending_type = member->has_pending_type;
	info.has_pending_value = member->has_pending_value;
	info.rank = rank;

	if (composite->member_stride >= sizeof(UserMemberData))
	{
		const UserMemberData* const full = static_cast<const UserMemberData*>(member);

		info.offset = full->offset;
		info.type_completion_arec_id = full->type_completion_arec_id;
		info.value_completion_arec_id = full->value_completion_arec_id;
	}
	else if (composite->member_stride >= sizeof(BlockOrSignatureOrLiteralMemberData))
	{
		ASSERT_OR_IGNORE(composite->disposition == TypeDisposition::Block || composite->disposition == TypeDisposition::Signature || composite->disposition == TypeDisposition::Literal);

		const BlockOrSignatureOrLiteralMemberData* const full = static_cast<const BlockOrSignatureOrLiteralMemberData*>(member);

		info.offset = full->offset;
		info.type_completion_arec_id = ArecId::INVALID;
		info.value_completion_arec_id = ArecId::INVALID;
	}
	else
	{
		ASSERT_OR_IGNORE(composite->disposition == TypeDisposition::File && composite->member_stride == sizeof(FileMemberData));

		info.offset = 0;
		info.type_completion_arec_id = composite->file.completion_arec_id;
		info.value_completion_arec_id = composite->file.completion_arec_id;
	}

	return info;
}

static TypeId id_from_structure(const TypePool* types, const TypeStructure* structure) noexcept
{
	const TypeId id = static_cast<TypeId>(reinterpret_cast<const u64*>(structure) - reinterpret_cast<const u64*>(types->structures.begin()));

	ASSERT_OR_IGNORE(id != TypeId::INVALID);

	return id;
}

static TypeStructure* structure_from_id(TypePool* types, TypeId id) noexcept
{
	ASSERT_OR_IGNORE(id != TypeId::INVALID);

	return reinterpret_cast<TypeStructure*>(reinterpret_cast<u64*>(types->structures.begin()) + static_cast<u32>(id));
}

static TypeStructure* follow_indirection(TypePool* types, TypeStructure* indir) noexcept
{
	if (static_cast<TypeTag>(indir->tag_bits) != TypeTag::INDIRECTION)
		return indir;

	TypeStructure* const dir = structure_from_id(types, indir->indirection_type_id);

	ASSERT_OR_IGNORE(static_cast<TypeTag>(dir->tag_bits) == TypeTag::Composite || static_cast<TypeTag>(dir->tag_bits) == TypeTag::CompositeLiteral);

	return dir;
}

static const TypeStructure* follow_indirection(TypePool* types, const TypeStructure* indir) noexcept
{
	if (static_cast<TypeTag>(indir->tag_bits) != TypeTag::INDIRECTION)
		return indir;

	TypeStructure* const dir = structure_from_id(types, indir->indirection_type_id);

	ASSERT_OR_IGNORE(static_cast<TypeTag>(dir->tag_bits) == TypeTag::Composite || static_cast<TypeTag>(dir->tag_bits) == TypeTag::CompositeLiteral);

	return dir;
}

static TypeStructure* make_structure(TypePool* types, TypeTag tag, Range<byte> attach, u64 reserve_size, SourceId distinct_source_id) noexcept
{
	ASSERT_OR_IGNORE(reserve_size <= UINT16_MAX && reserve_size >= attach.count());

	MutRange<byte> memory = types->structures.alloc(static_cast<u32>(sizeof(TypeStructure) + reserve_size));

	TypeStructure* const structure = reinterpret_cast<TypeStructure*>(memory.begin());
	structure->tag_bits = static_cast<u32>(tag);
	structure->holotype_id_bits = static_cast<u32>(id_from_structure(types, structure));
	structure->distinct_source_id = distinct_source_id;

	memcpy(structure->attach, attach.begin(), attach.count());

	return structure;
}

static TypeStructure* make_indirection(TypePool* types, TypeId indirected_type_id) noexcept
{
	ASSERT_OR_IGNORE(indirected_type_id != TypeId::INVALID);

	MutRange<byte> memory = types->structures.alloc(sizeof(TypeStructure));

	TypeStructure* const structure = reinterpret_cast<TypeStructure*>(memory.begin());
	structure->tag_bits = static_cast<u32>(TypeTag::INDIRECTION);
	structure->holotype_id_bits = static_cast<u32>(id_from_structure(types, structure));
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
	const TypeId h_a = static_cast<TypeId>(a->holotype_id_bits);

	const TypeId h_b = static_cast<TypeId>(b->holotype_id_bits);

	ASSERT_OR_IGNORE(h_a != h_b && h_a != TypeId::INVALID && h_b != TypeId::INVALID);

	if (h_a > h_b)
		a->holotype_id_bits = static_cast<u32>(h_b);
	else
		b->holotype_id_bits = static_cast<u32>(h_b);
}

static void unify_holotype_with_indirection(TypeStructure* a, TypeStructure* indirect_a, TypeStructure* b, TypeStructure* indirect_b) noexcept
{
	const TypeId h_a = static_cast<TypeId>(indirect_a->holotype_id_bits);

	const TypeId h_b = static_cast<TypeId>(indirect_b->holotype_id_bits);

	ASSERT_OR_IGNORE(h_a != h_b && h_a != TypeId::INVALID && h_b != TypeId::INVALID);

	if (h_a > h_b)
	{
		indirect_a->holotype_id_bits = static_cast<u32>(h_b);
		a->holotype_id_bits = static_cast<u32>(h_b);
	}
	else
	{
		indirect_b->holotype_id_bits = static_cast<u32>(h_a);
		b->holotype_id_bits = static_cast<u32>(h_a);
	}
}

static bool type_can_implicitly_convert_from_to_assume_unequal(TypePool* types, TypeId from_type_id, TypeId to_type_id) noexcept
{
	const TypeStructure* const from = structure_from_id(types, from_type_id);

	const TypeStructure* const to = structure_from_id(types, to_type_id);

	const TypeTag to_tag = static_cast<TypeTag>(to->tag_bits);

	const TypeTag from_tag = static_cast<TypeTag>(from->tag_bits);

	if (to_tag == TypeTag::TypeInfo)
		return true;

	switch (from_tag)
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
	{
		return false;
	}

	case TypeTag::CompInteger:
	{
		return to_tag == TypeTag::Integer;
	}

	case TypeTag::CompFloat:
	{
		return to_tag == TypeTag::Float;
	}

	case TypeTag::Divergent:
	{
		return true;
	}

	case TypeTag::Slice:
	{
		if (to_tag != TypeTag::Slice)
			return false;

		const ReferenceType* const from_attach = reinterpret_cast<const ReferenceType*>(from->attach);

		const ReferenceType* const to_attach = reinterpret_cast<const ReferenceType*>(to->attach);

		return from_attach->is_mut || !to_attach->is_mut;
	}

	case TypeTag::Ptr:
	{
		if (to_tag != TypeTag::Ptr)
			return false;

		const ReferenceType* const from_attach = reinterpret_cast<const ReferenceType*>(from->attach);

		const ReferenceType* const to_attach = reinterpret_cast<const ReferenceType*>(to->attach);

		return (from_attach->is_mut || !to_attach->is_mut)
		    && (!from_attach->is_opt || to_attach->is_opt)
			&& (from_attach->is_multi || !to_attach->is_multi);
	}

	case TypeTag::CompositeLiteral:
	{
		if (to_tag != TypeTag::Composite)
			return false;

		const CompositeType* const from_attach = reinterpret_cast<const CompositeType*>(from->attach);

		const CompositeType* const to_attach = reinterpret_cast<const CompositeType*>(to->attach);

		u32 rank = 0;

		for (u32 i = 0; i != from_attach->member_count; ++i)
		{
			// TODO: Implement member-by-member comparison
		}

		return true;
	}

	case TypeTag::ArrayLiteral:
	{
		if (to_tag == TypeTag::Array || to_tag == TypeTag::ArrayLiteral)
		{
			const ArrayType* const from_attach = reinterpret_cast<const ArrayType*>(from->attach);

			const ArrayType* const to_attach = reinterpret_cast<const ArrayType*>(to->attach);

			if (from_attach->element_count != to_attach->element_count)
				return false;

			// An empty array literal with no element type can be converted to
			// an empty array or array literal with any other element type.
			if (from_attach->element_type == TypeId::INVALID)
				return true;

			return type_can_implicitly_convert_from_to(types, from_attach->element_type, to_attach->element_type);
		}

		#if COMPILER_GCC
		#pragma GCC diagnostic push
		#pragma GCC diagnostic ignored "-Wimplicit-fallthrough" // this statement may fall through
		#endif
	}
		#if COMPILER_GCC
		#pragma GCC diagnostic pop
		#endif

	// Fallthrough from `ArrayLiteral` to `Array`.

	case TypeTag::Array:
	{
		if (to_tag != TypeTag::Slice)
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

		const TypeTag a_tag = static_cast<TypeTag>(a->tag_bits);

		const TypeTag b_tag = static_cast<TypeTag>(b->tag_bits);

		if (a_tag == TypeTag::INDIRECTION || b_tag == TypeTag::INDIRECTION)
		{
			TypeStructure* const direct_a = a_tag == TypeTag::INDIRECTION
				? structure_from_id(types, a->indirection_type_id)
				: a;

			TypeStructure* const direct_b = b_tag == TypeTag::INDIRECTION
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

	TypeStructure* const a = follow_indirection(types, structure_from_id(types, type_id_a));

	TypeStructure* const b = follow_indirection(types, structure_from_id(types, type_id_b));

	// This is assumed to be the common case, so we check for it as early as
	// possible.
	if (a->holotype_id_bits == b->holotype_id_bits)
		return TypeEq::Equal;

	// Types with differing tags can never be equal.
	if (a->tag_bits != b->tag_bits)
		return TypeEq::Unequal;

	// Since these types do not reference any other types and are deduplicated,
	// comparing their `TypeId`s is actually sufficient.
	// If they differ, we cannot be dealing with equal types.
	if (static_cast<TypeTag>(a->tag_bits) <= TypeTag::Float)
		return TypeEq::Unequal;

	// Types from different sources can definitionally never be equal. 
	if (a->distinct_source_id != b->distinct_source_id)
		return TypeEq::Unequal;

	const TypeTag tag = static_cast<TypeTag>(a->tag_bits);

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

		ASSERT_OR_IGNORE(a_attach->disposition != TypeDisposition::Block && b_attach->disposition != TypeDisposition::Block);

		if (a_attach->disposition != b_attach->disposition
		 || a_attach->stride != b_attach->stride
		 || a_attach->align_log2 != b_attach->align_log2
		 || a_attach->general.size != b_attach->general.size
		 || a_attach->member_count != b_attach->member_count)
		{
			eq_state_pop(seen);

			return TypeEq::Unequal;
		}

		ASSERT_OR_IGNORE(a_attach->member_stride == b_attach->member_stride);

		TypeEq result = TypeEq::Equal;

		for (u16 rank = 0; rank != a_attach->member_count; ++rank)
		{
			const CommonMemberData* const a_member = member_at(a_attach, rank);
			
			const CommonMemberData* const b_member = member_at(b_attach, rank);

			if (a_member->bitwise_comparable != b_member->bitwise_comparable)
			{
				eq_state_pop(seen);
			
				return TypeEq::Unequal;
			}

			if (a_attach->member_stride > sizeof(CommonMemberData))
			{
				const FullMemberData* const a_full_member = static_cast<const FullMemberData*>(a_member);

				const FullMemberData* const b_full_member = static_cast<const FullMemberData*>(b_member);

				if (a_full_member->offset != b_full_member->offset)
				{
					eq_state_pop(seen);

					return TypeEq::Unequal;
				}
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
	case TypeTag::ArrayLiteral:
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

		TypeEq element_result;

		// Array literals may have `TypeId::INVALID` as their element type if
		// they have no elements, because no element type can be inferred in
		// that case. This needs to be special cased to avoid recursing on a
		// `TypeId::INVALID`.
		if (tag == TypeTag::ArrayLiteral && (a_next == TypeId::INVALID || b_next == TypeId::INVALID))
		{
			element_result = a_next == TypeId::INVALID && b_next == TypeId::INVALID
				? TypeEq::Equal
				: TypeEq::Unequal;
		}
		else
		{
			element_result = type_is_equal_noloop(types, a_next, b_next, seen, false);
		}

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



TypePool* create_type_pool(HandlePool* alloc) noexcept
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

	TypePool* const types = alloc_handle_from_pool<TypePool>(alloc);
	types->dedup.init(1 << 21, 1 << 8, 1 << 20, 1 << 10);
	types->structures.init({ memory, structures_size }, Range{ STRUCTURES_CAPACITIES }, Range{ STRUCTURES_COMMITS });
	types->memory = { memory, structures_size };

	// Reserve `0` as `TypeId::INVALID`.
	types->structures.alloc(sizeof(TypeStructure));

	// Reserve simple types for use with `type_create_simple`.
	for (u8 ordinal = static_cast<u8>(TypeTag::Void); ordinal != static_cast<u8>(TypeTag::Divergent) + 1; ++ordinal)
		(void) make_structure(types, static_cast<TypeTag>(ordinal), {}, 0, SourceId::INVALID);

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
	ASSERT_OR_IGNORE(tag == TypeTag::Ptr || tag == TypeTag::Slice || tag == TypeTag::TailArray || tag == TypeTag::Variadic);

	return type_create_deduplicated(types, tag, range::from_object_bytes(&attach));
}

TypeId type_create_array(TypePool* types, TypeTag tag, ArrayType attach) noexcept
{
	ASSERT_OR_IGNORE(tag == TypeTag::Array || tag == TypeTag::ArrayLiteral);

	// Carve out exception for array literal with no elements having no
	// intrinsic element type.
	ASSERT_OR_IGNORE(attach.element_type != TypeId::INVALID || (tag == TypeTag::ArrayLiteral && attach.element_count == 0));

	return type_create_deduplicated(types, tag, range::from_object_bytes(&attach));
}

TypeId type_create_signature(TypePool* types, TypeTag tag, SignatureType attach) noexcept
{
	ASSERT_OR_IGNORE(tag == TypeTag::Builtin || tag == TypeTag::Func);

	return type_create_deduplicated(types, tag, range::from_object_bytes(&attach));
}

TypeId type_create_composite(TypePool* types, TypeTag tag, TypeId global_scope_type_id, TypeDisposition disposition, SourceId distinct_source_id, u32 initial_member_capacity, bool is_fixed_member_capacity) noexcept
{
	ASSERT_OR_IGNORE((tag == TypeTag::Composite || tag == TypeTag::CompositeLiteral)
	              && disposition != TypeDisposition::INVALID);

	const u8 member_stride = disposition == TypeDisposition::File
		? sizeof(FileMemberData)
		: disposition == TypeDisposition::User
		? sizeof(UserMemberData)
		: sizeof(BlockOrSignatureOrLiteralMemberData);

	const u64 reserve_size = sizeof(CompositeType) + static_cast<u64>(initial_member_capacity) * member_stride;

	CompositeType composite{};
	composite.general.size = 0;
	composite.stride = 0;
	composite.align_log2 = 0;
	composite.is_open = true;
	composite.is_fixed = is_fixed_member_capacity;
	composite.member_count = 0;
	composite.incomplete_member_count = 0;
	composite.disposition = disposition;
	composite.member_stride = member_stride;
	composite.global_scope_type_id = global_scope_type_id;
	composite.capacity = static_cast<u16>(next_pow2(sizeof(TypeStructure) + reserve_size, static_cast<u64>(32)));
	composite.used = sizeof(TypeStructure) + sizeof(CompositeType);

	if (disposition == TypeDisposition::File)
		composite.file.completion_arec_id = ArecId::INVALID;

	TypeStructure* const structure = make_structure(types, tag, range::from_object_bytes(&composite), reserve_size, distinct_source_id);

	const TypeId structure_type_id = id_from_structure(types, structure);

	if (is_fixed_member_capacity)
		return structure_type_id;

	TypeStructure* const indirection = make_indirection(types, structure_type_id);

	const TypeId indirection_type_id = id_from_structure(types, indirection);

	structure->holotype_id_bits = static_cast<u32>(indirection_type_id);

	return indirection_type_id;
}

void type_set_composite_file_completion_arec_id(TypePool* types, TypeId type_id, ArecId completion_arec_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID && completion_arec_id != ArecId::INVALID);

	TypeStructure* const structure = follow_indirection(types, structure_from_id(types, type_id));

	ASSERT_OR_IGNORE(static_cast<TypeTag>(structure->tag_bits) == TypeTag::Composite || static_cast<TypeTag>(structure->tag_bits) == TypeTag::CompositeLiteral);

	CompositeType* const composite = reinterpret_cast<CompositeType*>(structure->attach);

	ASSERT_OR_IGNORE(composite->is_open && composite->member_count == 0 && composite->file.completion_arec_id == ArecId::INVALID);

	composite->file.completion_arec_id = completion_arec_id;
}

TypeId type_seal_composite(TypePool* types, TypeId type_id, u64 size, u32 align, u64 stride) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	ASSERT_OR_IGNORE(align <= UINT16_MAX);

	TypeStructure* const structure = follow_indirection(types, structure_from_id(types, type_id));

	ASSERT_OR_IGNORE(static_cast<TypeTag>(structure->tag_bits) == TypeTag::Composite || static_cast<TypeTag>(structure->tag_bits) == TypeTag::CompositeLiteral);

	CompositeType* const composite = reinterpret_cast<CompositeType*>(structure->attach);

	ASSERT_OR_IGNORE(composite->is_open);

	if (composite->disposition == TypeDisposition::User)
	{
		ASSERT_OR_IGNORE(align != 0 && is_pow2(align));

		composite->general.size = size;
		composite->align_log2 = count_trailing_zeros_assume_one(align);
		composite->stride = stride;
	}
	else if (composite->disposition != TypeDisposition::File)
	{
		ASSERT_OR_IGNORE(size == 0 && align == 0 && stride == 0);

		composite->stride = next_multiple(composite->general.size, static_cast<u64>(1) << composite->align_log2);
	}

	composite->is_open = false;

	return id_from_structure(types, structure);
}

bool type_add_composite_member(TypePool* types, TypeId type_id, MemberInfo init) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID && init.rank == 0);

	TypeStructure* const structure = structure_from_id(types, type_id);

	TypeStructure* direct_structure = follow_indirection(types, structure);

	ASSERT_OR_IGNORE(static_cast<TypeTag>(direct_structure->tag_bits) == TypeTag::Composite || static_cast<TypeTag>(direct_structure->tag_bits) == TypeTag::CompositeLiteral);

	CompositeType* composite = reinterpret_cast<CompositeType*>(direct_structure->attach);

	ASSERT_OR_IGNORE(composite->is_open);

	ASSERT_OR_IGNORE(composite->disposition == TypeDisposition::User || init.offset == 0);

	ASSERT_OR_IGNORE(composite->disposition == TypeDisposition::Literal || init.name != IdentifierId::INVALID);

	if (init.name != IdentifierId::INVALID)
	{
		for (u16 i = 0; i != composite->member_count; ++i)
		{
			const CommonMemberData* const existing = member_at(composite, i);

			if (existing->name == init.name)
				return false;
		}
	}

	if (composite->capacity < composite->used + composite->member_stride)
	{
		ASSERT_OR_IGNORE(!composite->is_fixed);

		const MutRange<byte> to_dealloc = { reinterpret_cast<byte*>(direct_structure), composite->capacity };

		const Range<byte> composite_bytes = { reinterpret_cast<byte*>(composite), composite->used };

		direct_structure = make_structure(types, TypeTag::Composite, composite_bytes, composite_bytes.count() + composite->member_stride, direct_structure->distinct_source_id);

		types->structures.dealloc(to_dealloc);

		composite = reinterpret_cast<CompositeType*>(direct_structure->attach);

		structure->indirection_type_id = id_from_structure(types, direct_structure);

		// This can only ever increase by a factor of 2 due to how the
		// `TypePool.structures` heap is specified.
		composite->capacity *= 2;
	}

	if (composite->disposition != TypeDisposition::User && composite->disposition != TypeDisposition::File && !init.has_pending_type)
	{
		const TypeMetrics metrics = type_metrics_from_id(types, init.type.complete);

		const u64 member_begin = next_multiple(composite->general.size, static_cast<u64>(metrics.align));

		init.offset = member_begin;

		composite->general.size = member_begin + metrics.size;

		const u8 align_log2 = count_trailing_zeros_assume_one(metrics.align);

		if (composite->align_log2 < align_log2)
			composite->align_log2 = align_log2;
	}

	CommonMemberData* const raw_dst = member_at(composite, composite->member_count);
	raw_dst->type = init.type;
	raw_dst->value = init.value;
	raw_dst->name = init.name;
	raw_dst->has_pending_type = init.has_pending_type;
	raw_dst->has_pending_value = init.has_pending_value;
	raw_dst->is_pub = init.is_pub;
	raw_dst->is_mut = init.is_mut;
	raw_dst->is_global = init.is_global;
	raw_dst->is_comptime_known = init.is_comptime_known;
	raw_dst->unused_1_ = {};
	raw_dst->unused_2_[0] = {};
	raw_dst->unused_2_[1] = {};
	raw_dst->unused_2_[2] = {};

	ASSERT_OR_IGNORE(composite->disposition != TypeDisposition::File
	             || (init.has_pending_type
	              && init.has_pending_value
	              && init.is_global
	              && init.type_completion_arec_id == ArecId::INVALID
	              && init.value_completion_arec_id == ArecId::INVALID
	              && init.offset == 0));

	if (composite->disposition == TypeDisposition::User)
	{
		ASSERT_OR_IGNORE(init.name != IdentifierId::INVALID
		              && init.has_pending_type != (init.type_completion_arec_id == ArecId::INVALID)
		              && init.has_pending_value != (init.value_completion_arec_id == ArecId::INVALID)
		              && (!init.has_pending_value || init.value.pending != AstNodeId::INVALID));

		UserMemberData* const dst = static_cast<UserMemberData*>(raw_dst);
		dst->offset = init.offset;
		dst->type_completion_arec_id = init.type_completion_arec_id;
		dst->value_completion_arec_id = init.value_completion_arec_id;
	}
	else if (composite->disposition != TypeDisposition::File)
	{
		ASSERT_OR_IGNORE(init.type_completion_arec_id == ArecId::INVALID);
		ASSERT_OR_IGNORE(init.value_completion_arec_id == ArecId::INVALID);
		ASSERT_OR_IGNORE(composite->disposition != TypeDisposition::Block || (!init.has_pending_type && !init.has_pending_value && init.value.complete == GlobalValueId::INVALID));

		BlockOrSignatureOrLiteralMemberData* const dst = static_cast<BlockOrSignatureOrLiteralMemberData*>(raw_dst);
		dst->offset = init.offset;
	}

	if (init.has_pending_type || init.has_pending_value)
		composite->incomplete_member_count += 1;

	composite->member_count += 1;

	composite->used += composite->member_stride;

	return true;
}

void type_set_composite_member_info(TypePool* types, TypeId type_id, u16 rank, TypeId member_type_id, GlobalValueId member_value_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	TypeStructure* const structure = follow_indirection(types, structure_from_id(types, type_id));

	ASSERT_OR_IGNORE(static_cast<TypeTag>(structure->tag_bits) == TypeTag::Composite || static_cast<TypeTag>(structure->tag_bits) == TypeTag::CompositeLiteral);

	CompositeType* const composite = reinterpret_cast<CompositeType*>(structure->attach);

	ASSERT_OR_IGNORE(rank < composite->member_count);

	CommonMemberData* const member = member_at(composite, rank);

	if (!member->has_pending_type && !member->has_pending_value)
		return;

	ASSERT_OR_IGNORE(composite->incomplete_member_count != 0);

	if (member->has_pending_type)
	{
		ASSERT_OR_IGNORE(member_type_id != TypeId::INVALID);

		member->type.complete = member_type_id;
		member->has_pending_type = false;

		if (composite->disposition != TypeDisposition::User && composite->disposition != TypeDisposition::File)
		{
			const TypeMetrics metrics = type_metrics_from_id(types, member_type_id);

			const u64 member_begin = next_multiple(composite->general.size, static_cast<u64>(metrics.align));

			static_cast<BlockOrSignatureOrLiteralMemberData*>(member)->offset = member_begin;

			composite->general.size = member_begin + metrics.size;

			const u8 align_log2 = count_trailing_zeros_assume_one(metrics.align);

			if (composite->align_log2 < align_log2)
				composite->align_log2 = align_log2;
		}
	}

	if (member->has_pending_value)
	{
		ASSERT_OR_IGNORE(member_value_id != GlobalValueId::INVALID);

		member->value.complete = member_value_id;
		member->has_pending_value = false;

		if (composite->member_stride == sizeof(UserMemberData))
			static_cast<UserMemberData*>(member)->value_completion_arec_id = ArecId::INVALID;
	}
	
	if (composite->member_stride == sizeof(UserMemberData))
	{
		UserMemberData* const full = static_cast<UserMemberData*>(member);

		full->type_completion_arec_id = ArecId::INVALID;
		full->value_completion_arec_id = ArecId::INVALID;
	}

	composite->incomplete_member_count -= 1;
}

TypeId type_copy_composite(TypePool* types, TypeId type_id, u32 initial_member_capacity, bool is_fixed_member_capacity) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeStructure* const old_structure = follow_indirection(types, structure_from_id(types, type_id));

	ASSERT_OR_IGNORE(static_cast<TypeTag>(old_structure->tag_bits) == TypeTag::Composite || static_cast<TypeTag>(old_structure->tag_bits) == TypeTag::CompositeLiteral);

	const CompositeType* const old_composite = reinterpret_cast<const CompositeType*>(old_structure->attach);

	if (old_composite->member_count > initial_member_capacity)
		initial_member_capacity = old_composite->incomplete_member_count;

	const u32 adj_member_capacity = !old_composite->is_open || old_composite->member_count > initial_member_capacity
		? old_composite->member_count
		: initial_member_capacity;

	const u64 reserve_size = sizeof(CompositeType) + adj_member_capacity * old_composite->member_stride;

	const Range<byte> to_copy{ reinterpret_cast<const byte*>(old_composite), sizeof(CompositeType) + old_composite->member_count * old_composite->member_stride };

	TypeStructure* const new_structure = make_structure(types, TypeTag::Composite, to_copy, reserve_size, old_structure->distinct_source_id);

	CompositeType* const new_composite = reinterpret_cast<CompositeType*>(new_structure->attach);

	new_composite->is_fixed = is_fixed_member_capacity;

	const TypeId new_structure_type_id = id_from_structure(types, new_structure);

	if (is_fixed_member_capacity)
		return new_structure_type_id;

	TypeStructure* const indirection = make_indirection(types, new_structure_type_id);

	const TypeId indirection_type_id = id_from_structure(types, indirection);

	return indirection_type_id;
}

u32 type_get_composite_member_count(TypePool* types, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	TypeStructure* const structure = follow_indirection(types, structure_from_id(types, type_id));

	ASSERT_OR_IGNORE(static_cast<TypeTag>(structure->tag_bits) == TypeTag::Composite || static_cast<TypeTag>(structure->tag_bits) == TypeTag::CompositeLiteral);

	CompositeType* const composite = reinterpret_cast<CompositeType*>(structure->attach);

	ASSERT_OR_IGNORE(!composite->is_open);

	return composite->member_count;
}

void type_discard(TypePool* types, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	TypeStructure* const structure = follow_indirection(types, structure_from_id(types, type_id));

	ASSERT_OR_IGNORE(static_cast<TypeTag>(structure->tag_bits) == TypeTag::Composite || static_cast<TypeTag>(structure->tag_bits) == TypeTag::CompositeLiteral);

	CompositeType* const composite = reinterpret_cast<CompositeType*>(structure->attach);

	types->structures.dealloc({ reinterpret_cast<byte*>(structure), composite->capacity });
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

	const TypeStructure* const structure = follow_indirection(types, structure_from_id(types, type_id));

	const CompositeType* const composite = reinterpret_cast<const CompositeType*>(structure->attach);

	return composite->disposition;
}

TypeId type_global_scope_from_id(TypePool* types, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeStructure* structure = follow_indirection(types, structure_from_id(types, type_id));

	ASSERT_OR_IGNORE(static_cast<TypeTag>(structure->tag_bits) == TypeTag::Composite || static_cast<TypeTag>(structure->tag_bits) == TypeTag::CompositeLiteral);

	const CompositeType* const composite = reinterpret_cast<const CompositeType*>(structure->attach);

	return composite->global_scope_type_id;
}

bool type_has_metrics(TypePool* types, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeStructure* structure = follow_indirection(types, structure_from_id(types, type_id));

	if (static_cast<TypeTag>(structure->tag_bits) != TypeTag::Composite)
		return true;

	const CompositeType* const composite = reinterpret_cast<const CompositeType*>(structure->attach);

	return composite->disposition != TypeDisposition::User || !composite->is_open;
}

TypeMetrics type_metrics_from_id(TypePool* types, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeStructure* const structure = follow_indirection(types, structure_from_id(types, type_id));

	switch (static_cast<TypeTag>(structure->tag_bits))
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
	case TypeTag::Variadic:
	{
		return { 16, 16, 8 };
	}

	case TypeTag::Ptr:
	{
		return { 8, 8, 8 };
	}

	case TypeTag::Array:
	case TypeTag::ArrayLiteral:
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
		return { 16, 16, 8 };
	}

	case TypeTag::Composite:
	case TypeTag::CompositeLiteral:
	{
		const CompositeType* const composite = reinterpret_cast<const CompositeType*>(structure->attach);

		ASSERT_OR_IGNORE(!composite->is_open);

		return composite->disposition == TypeDisposition::File
			? TypeMetrics{ 0, 0, 1}
			: TypeMetrics{ composite->general.size, composite->stride, static_cast<u32>(1) << composite->align_log2 };
	}

	case TypeTag::TailArray:
	case TypeTag::Trait:
		TODO("Implement `type_metrics_from_id` for TailArray and Trait.");

	case TypeTag::INVALID:
	case TypeTag::INDIRECTION:
		break; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

TypeTag type_tag_from_id(TypePool* types, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeStructure* const structure = structure_from_id(types, type_id);

	if (static_cast<TypeTag>(structure->tag_bits) == TypeTag::INDIRECTION)
		return TypeTag::Composite;
		
	return static_cast<TypeTag>(structure->tag_bits);
}

const MemberInfo type_member_by_rank(TypePool* types, TypeId type_id, u16 rank)
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeStructure* const structure = follow_indirection(types, structure_from_id(types, type_id));

	ASSERT_OR_IGNORE(static_cast<TypeTag>(structure->tag_bits) == TypeTag::Composite || static_cast<TypeTag>(structure->tag_bits) == TypeTag::CompositeLiteral);

	const CompositeType* const composite = reinterpret_cast<const CompositeType*>(structure->attach);

	ASSERT_OR_IGNORE(composite->member_count > rank);

	const CommonMemberData* const member = member_at(composite, rank);

	return fill_member_info(composite, member, rank);
}

bool type_member_by_name(TypePool* types, TypeId type_id, IdentifierId name, MemberInfo* out) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeStructure* const structure = follow_indirection(types, structure_from_id(types, type_id));

	ASSERT_OR_IGNORE(static_cast<TypeTag>(structure->tag_bits) == TypeTag::Composite || static_cast<TypeTag>(structure->tag_bits) == TypeTag::CompositeLiteral);

	const CompositeType* const composite = reinterpret_cast<const CompositeType*>(structure->attach);

	u16 rank;

	const CommonMemberData* member;

	if (!find_member_by_name(composite, name, &rank, &member))
		return false;

	*out = fill_member_info(composite, member, rank);

	return true;
}

const void* type_attachment_from_id_raw(TypePool* types, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeStructure* const structure = follow_indirection(types, structure_from_id(types, type_id));

	return structure->attach;
}



const char8* tag_name(TypeTag tag) noexcept
{
	static constexpr const char8* TYPE_TAG_NAMES[] = {
		"[Unknown]",
		"[Indirection]",
		"Void",
		"Type",
		"Definition",
		"CompInteger",
		"CompFloat",
		"Boolean",
		"TypeInfo",
		"TypeBuilder",
		"Divergent",
		"Integer",
		"Float",
		"Slice",
		"Ptr",
		"Array",
		"Func",
		"Builtin",
		"Composite",
		"TailArray",
		"CompositeLiteral",
		"ArrayLiteral",
		"Variadic",
		"Trait",
	};

	u8 ordinal = static_cast<u8>(tag);

	if (ordinal >= array_count(TYPE_TAG_NAMES))
		ordinal = 0;

	return TYPE_TAG_NAMES[ordinal];
}



MemberIterator members_of(TypePool* types, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeStructure* const structure = structure_from_id(types, type_id);

	const TypeStructure* const direct_structure = follow_indirection(types, structure);

	ASSERT_OR_IGNORE(static_cast<TypeTag>(direct_structure->tag_bits) == TypeTag::Composite || static_cast<TypeTag>(structure->tag_bits) == TypeTag::CompositeLiteral);

	const CompositeType* const composite = reinterpret_cast<const CompositeType*>(direct_structure->attach);

	MemberIterator it;
	it.structure = composite->member_count == 0 ? nullptr : composite->is_fixed ? direct_structure : structure;
	it.types = types;
	it.rank = 0;

	return it;
}

MemberInfo next(MemberIterator* it) noexcept
{
	ASSERT_OR_IGNORE(has_next(it));

	const TypeStructure* const structure = follow_indirection(it->types, static_cast<const TypeStructure*>(it->structure));

	const CompositeType* const composite = reinterpret_cast<const CompositeType*>(structure->attach);

	const u16 curr_rank = it->rank;

	const CommonMemberData* const curr_member = member_at(composite, curr_rank);

	it->rank = curr_rank + 1;

	if (composite->member_count == it->rank)
		it->structure = nullptr;

	return fill_member_info(composite, curr_member, curr_rank);
}

bool has_next(const MemberIterator* it) noexcept
{
	return it->structure != nullptr;
}
