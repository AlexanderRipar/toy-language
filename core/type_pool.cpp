#include "core.hpp"
#include "structure.hpp"

#include "../infra/types.hpp"
#include "../infra/assert.hpp"
#include "../infra/panic.hpp"
#include "../infra/math.hpp"
#include "../infra/range.hpp"
#include "../infra/hash.hpp"
#include "../infra/inplace_sort.hpp"
#include "../infra/container/id_map.hpp"
#include "../infra/container/reserved_vec.hpp"

#include <cstring>

struct TypeStructure;

static TypeStructure* structure_from_id_direct(CoreData* core, TypeId type_id) noexcept;

static TypeId id_from_structure(CoreData* core, TypeStructure* structure) noexcept;

static TypeStructure* make_structure_nohash(CoreData* core, TypeTag tag, Range<byte> attach, u64 reserve_size) noexcept;

static TypeStructure* make_structure_hash(CoreData* core, TypeTag tag, Range<byte> attach, u64 reserve_size) noexcept;



struct alignas(8) SeenSetEntry
{
	TypeId id;

	u32 index;
};

struct SeenSet
{
	// First, an open-addressed hashtable of `capacity` elements, each
	// containing their `TypeId` and index into the stack, relative to
	// `memory`.
	// Following this is a stack with each element containing its `TypeId` and
	// index in the hashtable.
	SeenSetEntry* memory;

	u32 table_used;

	u32 stack_top;

	u32 capacity;
};



enum class CompositeKind : u8
{
	INVALID = 0,
	File,
	Impl,
	Signature,
	User,
	COUNT = User,
};

enum class CompositeFlags : u8
{
	EMPTY = 0,

	Signature_IsFunc                 = 0x01,

	User_IsOpen                      = 0x01,

	Any_IsShadow                     = 0x02,
};

static constexpr CompositeFlags operator&(CompositeFlags lhs, CompositeFlags rhs) noexcept
{
	return static_cast<CompositeFlags>(static_cast<u8>(lhs) & static_cast<u8>(rhs));
}

static constexpr CompositeFlags& operator&=(CompositeFlags& lhs, CompositeFlags rhs) noexcept
{
	lhs = lhs & rhs;

	return lhs;
}

static constexpr CompositeFlags operator~(CompositeFlags f) noexcept
{
	return static_cast<CompositeFlags>(~static_cast<u8>(f));
}

struct alignas(8) CompositeFileExtraData
{
	SourceId definition_site;

	u32 unused_ = 0;
};

struct alignas(8) CompositeImplExtraData
{
	TypeId self_type_id;

	u32 unused_ = 0;
};

struct alignas(8) CompositeSignatureExtraData
{
	union
	{
		TypeId type_id;

		OpcodeId completion_id;
	} return_type;

	Maybe<ClosureId> closure_id;

	bool is_func;

	u8 templated_parameter_count;

	bool has_templated_return_type;

	bool is_variadic;

	u32 unused_ = 0;
};

struct alignas(8) CompositeUserExtraData
{
	u64 size;

	u64 stride;

	u32 align;

	SourceId definition_site;
};

struct alignas(8) CompositeMember
{
	TypeId type_id;

	union
	{
		Maybe<CoreId> value_or_default;

		OpcodeId completion_id;
	};

	bool is_pending : 1;

	bool is_pub : 1;

	bool is_mut : 1;

	bool is_eval : 1;

	bool is_initializing : 1;

	bool is_impl_member_from_trait_default : 1;

	u16 shadow_rank;

	Maybe<ShadowLayoutId> shadow_id;
};

struct alignas(8) CompositeType
{
	u16 member_used;

	u16 member_capacity;

	CompositeKind kind;

	CompositeFlags flags;

	bool may_realloc;

	u8 extra_data_size;

	// Depending on `kind`:
	// File => Nothing
	// Trait => Nothing
	// Impl => `CompositeImplExtraData`
	// Signature => `CompositeSignatureExtraData`
	// User => `CompositeUserExtraData`

	// @sizeas(IdentifierId[member_capacity])
	// IdentifierId member_names[member_count];

	// @sizeas(CompositeGenericMember[member_capacity])
	// CompositeGenericMember members_types[member_count];

	// Depending on `kind`:
	// File, Trait, Impl, Signature => Nothing
	// User => @sizeas(s64[member_capacity]) s64 member_offsets[member_count];
};



struct CompositeAllocInfo
{
	u64 alloc_size;

	u16 member_capacity;

	u8 extra_data_size;
};

struct CompositeInfo
{
	CompositeKind kind;

	u32 member_count;

	IdentifierId* member_names;

	CompositeMember* member_types;

	Maybe<s64*> member_offsets;
};



struct alignas(8) IndirectionType
{
	Maybe<TypeId> indirection_type_id;

	u32 unused_ = 0;
};

struct alignas(8) ImplKeyType
{
	TypeId base_type_id;

	TypeId trait_type_id;

	Maybe<TypeId> impl_type_id;

	u32 unused_ = 0;
};



struct alignas(8) TypeStructure
{
	TypeTag tag;

	TypeId holotype_id;

	u32 hash;

	u32 unused_ = 0;

	#if COMPILER_GCC
		#pragma GCC diagnostic push
		#pragma GCC diagnostic ignored "-Wpedantic" // ISO C++ forbids flexible array member
	#endif
	alignas(8) byte attach[];
	#if COMPILER_GCC
		#pragma GCC diagnostic pop
	#endif
};



struct alignas(8) HolotypeInit
{
	TypeStructure* structure;

	CoreData* core;
};

struct alignas(8) Holotype
{
	TypeId m_holotype_id;

	u32 m_hash;

	u32 hash() const noexcept
	{
		return m_hash;
	}

	bool is_equal_to_key(HolotypeInit key, u32 key_hash) const noexcept
	{
		ASSERT_OR_IGNORE(key_hash != 0);

		if (m_hash != key_hash)
			return false;

		ASSERT_OR_IGNORE(structure_from_id_direct(key.core, m_holotype_id)->tag != TypeTag::INDIRECTION);

		const TypeId key_type_id = id_from_structure(key.core, key.structure);

		return type_is_equal(key.core, m_holotype_id, key_type_id) == TypeEquality::Equal;
	}
};



static constexpr u64 HOLOTYPES_LOOKUPS_RESERVE = decltype(TypePool::holotypes)::lookups_memory_size(1 << 21);

static constexpr u32 HOLOTYPES_LOOKUPS_INITIAL_COMMIT_COUNT = static_cast<u32>(1) << 10;

static constexpr u32 HOLOTYPES_VALUES_RESERVE = (static_cast<u32>(1) << 20) * sizeof(Holotype);

static constexpr u32 HOLOTYPES_VALUES_COMMIT_INCREMENT_COUNT = static_cast<u32>(1) << 12;



static CompositeInfo composite_info(CompositeType* composite) noexcept
{
	byte* const extra_data = reinterpret_cast<byte*>(composite + 1);

	IdentifierId* const member_names = reinterpret_cast<IdentifierId*>(extra_data + composite->extra_data_size);

	CompositeMember* const member_types = reinterpret_cast<CompositeMember*>(member_names + composite->member_capacity);

	const Maybe<s64*> member_offsets = composite->kind == CompositeKind::User
		? some(reinterpret_cast<s64*>(member_types + composite->member_capacity))
		: none<s64*>();

	CompositeInfo result;
	result.kind = composite->kind;
	result.member_count = composite->member_used;
	result.member_names = member_names;
	result.member_types = member_types;
	result.member_offsets = member_offsets;

	return result;
}

static CompositeInfo composite_info(const CompositeType* composite) noexcept
{
	return composite_info(const_cast<CompositeType*>(composite));
}



static CompositeAllocInfo composite_alloc_info(CompositeKind kind, u64 member_count) noexcept
{
	const u16 member_capacity = (member_count + 1) & ~1;

	u64 alloc_size;

	u8 extra_data_size;

	switch (kind)
	{
	case CompositeKind::File:
		alloc_size = sizeof(CompositeType) + sizeof(CompositeFileExtraData) + member_capacity * (sizeof(IdentifierId) + sizeof(CompositeMember));
		extra_data_size = sizeof(CompositeFileExtraData);
		break;

	case CompositeKind::Impl:
		alloc_size = sizeof(CompositeType) + sizeof(CompositeImplExtraData) + member_capacity * (sizeof(IdentifierId) + sizeof(CompositeMember));
		extra_data_size = sizeof(CompositeImplExtraData);
		break;

	case CompositeKind::Signature:
		alloc_size = sizeof(CompositeType) + sizeof(CompositeSignatureExtraData) + member_capacity * (sizeof(IdentifierId) + sizeof(CompositeMember));
		extra_data_size = sizeof(CompositeSignatureExtraData);
		break;

	case CompositeKind::User:
		alloc_size = sizeof(CompositeType) + sizeof(CompositeUserExtraData) + member_capacity * (sizeof(IdentifierId) + sizeof(CompositeMember) + sizeof(u64));
		extra_data_size = sizeof(CompositeUserExtraData);
		break;

	default:
		ASSERT_UNREACHABLE;
	}

	return CompositeAllocInfo{ alloc_size, member_capacity, extra_data_size };
}

static TypeStructure* composite_alloc(CoreData* core, TypeTag tag, CompositeKind kind, CompositeFlags flags, u32 member_count, bool may_realloc) noexcept
{
	const CompositeAllocInfo alloc_info = composite_alloc_info(kind, member_count);

	TypeStructure* const structure = make_structure_nohash(core, tag, {}, alloc_info.alloc_size);

	CompositeType* const composite = reinterpret_cast<CompositeType*>(structure + 1);
	composite->member_used = 0;
	composite->member_capacity = alloc_info.member_capacity;
	composite->kind = kind;
	composite->flags = flags;
	composite->may_realloc = may_realloc;
	composite->extra_data_size = alloc_info.extra_data_size;

	return structure;
}

static TypeStructure* composite_realloc(CoreData* core, TypeStructure* indirection_structure, TypeStructure* old_composite_structure) noexcept
{
	ASSERT_OR_IGNORE(indirection_structure->tag == TypeTag::INDIRECTION);

	// Make sure we are only reallocating user-defined `Composites`, for which
	// `TypeTag::Composite` is sufficient.
	ASSERT_OR_IGNORE(old_composite_structure->tag == TypeTag::Composite);

	const CompositeType* const old_composite = reinterpret_cast<CompositeType*>(old_composite_structure + 1);

	ASSERT_OR_IGNORE(old_composite->kind == CompositeKind::User);

	const u32 old_member_capacity = old_composite->member_capacity;

	TypeStructure* const new_composite_structure = composite_alloc(core, old_composite_structure->tag, old_composite->kind, old_composite->flags, old_member_capacity + 1, true);

	IndirectionType* const indirection = reinterpret_cast<IndirectionType*>(indirection_structure->attach);
	indirection->indirection_type_id = some(id_from_structure(core, new_composite_structure));

	CompositeType* const new_composite = reinterpret_cast<CompositeType*>(new_composite_structure->attach);

	const CompositeInfo old_info = composite_info(old_composite);

	const CompositeInfo new_info = composite_info(new_composite);

	memcpy(new_composite, old_composite, sizeof(CompositeType) + old_composite->extra_data_size + old_member_capacity * sizeof(IdentifierId));

	memcpy(new_info.member_types, old_info.member_types, old_member_capacity * sizeof(CompositeMember));

	if (is_some(old_info.member_offsets))
		memcpy(get(new_info.member_offsets), get(old_info.member_offsets), old_member_capacity * sizeof(s64));

	return new_composite_structure;
}

static bool find_member_by_name(CompositeInfo info, IdentifierId name, u16* out_rank) noexcept
{
	for (u32 i = 0; i != info.member_count; ++i)
	{
		if (info.member_names[i] == name)
		{
			*out_rank = static_cast<u16>(i);

			return true;
		}
	}

	return false;
}

static bool fill_member_info(CompositeInfo info, u16 rank, MemberInfo* out_info, OpcodeId* out_completion_id) noexcept
{
	ASSERT_OR_IGNORE(rank < info.member_count);

	const CompositeMember type = info.member_types[rank];

	if (type.is_pending)
	{
		*out_completion_id = type.completion_id;

		out_info->type_id = TypeId::INVALID;
		out_info->value_or_default = none<CoreId>();
		out_info->rank = rank;
		out_info->is_pub = type.is_pub;
		out_info->is_mut = type.is_mut;
		out_info->is_eval = type.is_eval;
		out_info->is_global = info.kind != CompositeKind::User && info.kind != CompositeKind::Signature;
		out_info->is_impl_member_from_trait_default = type.is_impl_member_from_trait_default;

		return false;
	}
	else
	{
		*out_completion_id = OpcodeId::INVALID;

		out_info->type_id = type.type_id;
		out_info->value_or_default = type.value_or_default;
		out_info->is_pub = type.is_pub;
		out_info->is_mut = type.is_mut;
		out_info->is_eval = type.is_eval;
		out_info->is_global = info.kind != CompositeKind::User && info.kind != CompositeKind::Signature;
		out_info->rank = rank;
		out_info->shadow_rank = type.shadow_rank;
		out_info->shadow_id = type.shadow_id;
		out_info->offset = is_some(info.member_offsets) ? get(info.member_offsets)[rank] : 0;

		return true;
	}
}



static TypeId id_from_structure(CoreData* core, TypeStructure* structure) noexcept
{
	return static_cast<TypeId>(core_id_from_address(core, structure));
}

static TypeStructure* structure_from_id_direct(CoreData* core, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	TypeStructure* const structure = static_cast<TypeStructure*>(address_from_core_id(core, static_cast<CoreId>(type_id)));

	ASSERT_OR_IGNORE(structure->tag != TypeTag::INVALID);

	return structure;
}

static Maybe<TypeStructure*> structure_from_id_follow(CoreData* core, TypeId type_id) noexcept
{
	TypeStructure* structure = structure_from_id_direct(core, type_id);

	// INDIRECTIONs can only be nested up to two times. As such, we "unroll"
	// the indireciton following code instead of using a loop, allowing us to
	// easily assert that this invariant holds.

	if (structure->tag == TypeTag::INDIRECTION)
	{
		IndirectionType* const indirection = reinterpret_cast<IndirectionType*>(structure + 1);

		if (is_none(indirection->indirection_type_id))
			return none<TypeStructure*>();

		structure = structure_from_id_direct(core, get(indirection->indirection_type_id));
	}

	if (structure->tag == TypeTag::INDIRECTION)
	{
		IndirectionType* const indirection = reinterpret_cast<IndirectionType*>(structure + 1);

		if (is_none(indirection->indirection_type_id))
			return none<TypeStructure*>();

		structure = structure_from_id_direct(core, get(indirection->indirection_type_id));
	}

	ASSERT_OR_IGNORE(structure->tag != TypeTag::INDIRECTION);

	return some(structure);
}

static TypeStructure* structure_from_id_follow_with_last_indirection(CoreData* core, TypeId type_id, TypeStructure** out_last_indirection) noexcept
{
	TypeStructure* structure = structure_from_id_direct(core, type_id);

	ASSERT_OR_IGNORE(structure->tag == TypeTag::INDIRECTION);

	TypeStructure* last_indirection = structure;

	IndirectionType* const indirection = reinterpret_cast<IndirectionType*>(structure + 1);

	ASSERT_OR_IGNORE(is_some(indirection->indirection_type_id));

	structure = structure_from_id_direct(core, get(indirection->indirection_type_id));

	if (structure->tag == TypeTag::INDIRECTION)
	{
		last_indirection = structure;

		IndirectionType* const second_indirection = reinterpret_cast<IndirectionType*>(structure + 1);

		ASSERT_OR_IGNORE(is_some(second_indirection->indirection_type_id));

		structure = structure_from_id_direct(core, get(second_indirection->indirection_type_id));
	}

	ASSERT_OR_IGNORE(structure->tag != TypeTag::INDIRECTION);

	*out_last_indirection = last_indirection;

	return structure;
}

static u32 structure_size(TypeStructure* structure) noexcept
{
	switch (structure->tag)
	{
	case TypeTag::INDIRECTION:
	{
		return sizeof(TypeStructure) + sizeof(IndirectionType);
	}

	case TypeTag::Void:
	case TypeTag::Type:
	case TypeTag::CompInteger:
	case TypeTag::CompFloat:
	case TypeTag::Boolean:
	case TypeTag::TypeInfo:
	case TypeTag::TypeBuilder:
	case TypeTag::Divergent:
	case TypeTag::Undefined:
	{
		return sizeof(TypeStructure);
	}

	case TypeTag::Integer:
	case TypeTag::Float:
	{
		return sizeof(TypeStructure) + sizeof(NumericType);
	}

	case TypeTag::Slice:
	case TypeTag::Ptr:
	case TypeTag::TailArray:
	{
		return sizeof(TypeStructure) + sizeof(ReferenceType);
	}

	case TypeTag::Array:
	case TypeTag::ArrayLiteral:
	{
		return sizeof(TypeStructure) + sizeof(ArrayType);
	}

	case TypeTag::Signature:
	{
		const CompositeType* const composite = reinterpret_cast<const CompositeType*>(structure + 1);

		ASSERT_OR_IGNORE((composite->member_capacity & 1) == 0);

		return sizeof(TypeStructure)
		     + sizeof(CompositeType)
		     + sizeof(CompositeSignatureExtraData)
		     + composite->member_capacity * (sizeof(IdentifierId) + sizeof(CompositeMember));
	}

	case TypeTag::Composite:
	case TypeTag::CompositeLiteral:
	case TypeTag::Trait:
	{
		const CompositeType* const composite = reinterpret_cast<const CompositeType*>(structure + 1);

		ASSERT_OR_IGNORE((composite->member_capacity & 1) == 0);

		const u32 offsets_size = composite->kind == CompositeKind::User
			? composite->member_capacity * sizeof(s64)
			: 0;

		return sizeof(TypeStructure)
		     + sizeof(CompositeType)
		     + sizeof(CompositeSignatureExtraData)
		     + composite->member_capacity * (sizeof(IdentifierId) + sizeof(CompositeMember))
		     + offsets_size;
	}

	case TypeTag::Self:
	{
		return sizeof(TypeStructure) + sizeof(SelfType);
	}
	
	case TypeTag::Definition:
	case TypeTag::Variadic:
		TODO("Implement `structure_size(%)`.", tag_name(structure->tag));

	case TypeTag::INVALID:
		; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}



static TypeId holotype_id_from_interned_type_structure(CoreData* core, TypeStructure* structure, bool delete_duplicate) noexcept
{
	ASSERT_OR_IGNORE(structure->holotype_id == TypeId::INVALID);

	ASSERT_OR_IGNORE(structure->hash != 0);

	HolotypeInit init;
	init.structure = structure;
	init.core = core;

	Holotype* const holotype = core->types.holotypes.value_from(init, structure->hash);

	const TypeId structure_id = id_from_structure(core, structure);

	const TypeId holotype_id = holotype->m_holotype_id;

	structure->holotype_id = holotype_id;

	if (holotype_id != structure_id && delete_duplicate)
	{
		const u32 size = structure_size(structure);

		comp_heap_dealloc(core, MutRange<byte>{ reinterpret_cast<byte*>(structure), size });
	}

	return holotype_id;
}

static TypeId holotype_id_from_attachment(CoreData* core, TypeTag tag, Range<byte> attach) noexcept
{
	TypeStructure* const structure = make_structure_hash(core, tag, attach, attach.count());

	ASSERT_OR_IGNORE(structure->holotype_id == TypeId::INVALID);

	if (structure->hash == 0)
		return id_from_structure(core, structure);

	HolotypeInit init;
	init.structure = structure;
	init.core = core;

	Holotype* const holotype = core->types.holotypes.value_from(init, structure->hash);

	const TypeId structure_id = id_from_structure(core, structure);

	const TypeId holotype_id = holotype->m_holotype_id;

	if (holotype_id == structure_id)
	{
		structure->holotype_id = holotype_id;
	}
	else
	{
		const u32 size = sizeof(TypeStructure) + static_cast<u32>(attach.count());

		comp_heap_dealloc(core, MutRange<byte>{ reinterpret_cast<byte*>(structure), size });
	}

	return holotype_id;
}



static SeenSet seen_set_create_with_capacity(CoreData* core, u32 capacity) noexcept
{
	ASSERT_OR_IGNORE(capacity != 0 && is_pow2(capacity));

	void* memory = temp_stack_alloc(core, capacity * 2 * sizeof(SeenSetEntry), alignof(SeenSetEntry));

	memset(memory, 0, capacity * sizeof(SeenSetEntry));

	SeenSet set;
	set.memory = static_cast<SeenSetEntry*>(memory);
	set.table_used = 0;
	set.stack_top = capacity - 1;
	set.capacity = capacity;

	return set;
}

static SeenSet seen_set_create(CoreData* core) noexcept
{
	return seen_set_create_with_capacity(core, 16);
}

static u32 seen_set_push_assume_capacity(SeenSet* set, TypeId type_id) noexcept
{
	const u32 hash = fnv1a(range::from_object_bytes(&type_id));

	u32 index = hash & (set->capacity - 1);

	u32 earliest_gravestone = set->capacity;

	// Use the entry just before the stack as a sentinel. This simplifies
	// calculating the function's return value.
	u32 highest_seen_stack_index = set->capacity - 1;

	while (true)
	{
		const SeenSetEntry entry = set->memory[index];

		if (entry.id == TypeId::INVALID)
		{
			break;
		}
		if (entry.id == type_id)
		{
			if (entry.index > highest_seen_stack_index)
				highest_seen_stack_index = entry.index;
		}
		else if (entry.id == static_cast<TypeId>(1) && earliest_gravestone == set->capacity)
		{
			earliest_gravestone = index;
		}

		index = (index + 1) & (set->capacity - 1);

		// Make sure we don't loop around. This should never happen, since
		// we make sure to allocate enough space before we get more than
		// two-thirds full, but making sure won't hurt.
		ASSERT_OR_IGNORE(index != (hash & (set->capacity - 1)));
	}

	const u32 insertion_index = earliest_gravestone == set->capacity
		? index
		: earliest_gravestone;

	SeenSetEntry* const table_entry = set->memory + insertion_index;

	ASSERT_OR_IGNORE(table_entry->id == TypeId::INVALID || table_entry->id == static_cast<TypeId>(1));

	set->stack_top += 1;

	ASSERT_OR_IGNORE(set->stack_top >= set->capacity && set->stack_top < set->capacity * 2);

	table_entry->id = type_id;
	table_entry->index = set->stack_top;

	SeenSetEntry* const stack_entry = set->memory + set->stack_top;

	stack_entry->id = type_id;
	stack_entry->index = insertion_index;

	// Only increment `table_used` if we didn't reuse a gravestone slot.
	if (earliest_gravestone == set->capacity)
		set->table_used += 1;

	return highest_seen_stack_index - (set->capacity - 1);
}

static u32 seen_set_push(CoreData* core, SeenSet* set, TypeId type_id) noexcept
{
	if (set->capacity * 3 < set->table_used * 4)
	{
		const SeenSetEntry* const stack = set->memory + set->capacity;

		SeenSet new_set = seen_set_create_with_capacity(core, set->capacity * 2);

		for (u32 i = 0; i != set->stack_top - set->capacity; ++i)
			(void) seen_set_push_assume_capacity(&new_set, stack[i].id);

		*set = new_set;
	}

	return seen_set_push_assume_capacity(set, type_id);
}

static void seen_set_pop(SeenSet* set) noexcept
{
	ASSERT_OR_IGNORE(set->stack_top >= set->capacity && set->stack_top < set->capacity * 2);

	const u32 table_index = set->memory[set->stack_top].index;

	set->memory[table_index].id = static_cast<TypeId>(1);
	
	set->stack_top -= 1;
}



static void init_user_composite_shadow_data(CoreData* core, CompositeType* composite) noexcept
{
	struct OffsetComparator
	{
		static s64 compare(const ShadowLayoutMemberInitializer& a, const ShadowLayoutMemberInitializer& b) noexcept
		{
			if (a.offset_in_type < b.offset_in_type)
				return -1;
			else if (a.offset_in_type > b.offset_in_type)
				return 1;
			else if (a.rank_in_type < b.rank_in_type)
				return -1;
			else if (a.rank_in_type > b.rank_in_type)
				return 1;
			else
				return 0;
		}
	};

	const CompositeInfo info = composite_info(composite);

	ASSERT_OR_IGNORE(info.kind == CompositeKind::User);

	const u64 mark = temp_stack_mark(core);

	ShadowLayoutMemberInitializer* const by_offset = static_cast<ShadowLayoutMemberInitializer*>(temp_stack_alloc(core, info.member_count * sizeof(ShadowLayoutMemberInitializer), alignof(ShadowLayoutMemberInitializer)));

	u16 shadow_member_count = 0;

	for (u16 i = 0; i != info.member_count; ++i)
	{
		TypeMetrics metrics;
		
		// Since we are only being called because the user composite has just
		// been successfully hashed, all of its members must themselves also be
		// complete already, making `type_metrics_from_id` safe to call.
		if (!type_metrics_from_id(core, info.member_types[i].type_id, &metrics))
			ASSERT_UNREACHABLE;

		if (!metrics.is_shadow)
			continue;

		by_offset[shadow_member_count].offset_in_type = get(info.member_offsets)[i];
		by_offset[shadow_member_count].type_id = info.member_types[i].type_id;
		by_offset[shadow_member_count].rank_in_type = i;

		shadow_member_count += 1;
	}

	inplace_sort<ShadowLayoutMemberInitializer, OffsetComparator>(MutRange{ by_offset, shadow_member_count });

	u16 i = 0;

	while (i != shadow_member_count)
	{
		const s64 offset = by_offset[i].offset_in_type;

		const u16 begin = i;

		i += 1;

		while (i != shadow_member_count && by_offset[i].offset_in_type == offset)
			i += 1;

		const u16 end = i;

		const ShadowLayoutId shadow_id = shadow_create_layout(core, Range{ by_offset + begin, by_offset + end });

		for (u16 j = 0; j != end - begin; ++j)
		{
			const u16 rank = by_offset[begin + j].rank_in_type;

			info.member_types[rank].shadow_id = some(shadow_id);
			info.member_types[rank].shadow_rank = j;
		}
	}

	temp_stack_release(core, mark);
}



static u32 hash_type_id(CoreData* core, u32 hash, SeenSet* seen, TypeId type) noexcept;

static u32 hash_composite_type_extra_data(CoreData* core, u32 hash, SeenSet* seen, const CompositeType* attach) noexcept
{
	switch (attach->kind)
	{
	case CompositeKind::File:
	{
		const CompositeFileExtraData* const file = reinterpret_cast<const CompositeFileExtraData*>(attach + 1);

		return fnv1a_step(hash, range::from_object_bytes(&file->definition_site)) | 1;
	}

	case CompositeKind::Impl:
	{
		const CompositeImplExtraData* impl = reinterpret_cast<const CompositeImplExtraData*>(attach + 1);

		return hash_type_id(core, hash, seen, impl->self_type_id);
	}

	case CompositeKind::Signature:
	{
		const CompositeSignatureExtraData* const signature = reinterpret_cast<const CompositeSignatureExtraData*>(attach + 1);

		if (!signature->has_templated_return_type)
		{
			hash = hash_type_id(core, hash, seen, signature->return_type.type_id);

			if (hash == 0)
				return 0;
		}

		const byte flags = (static_cast<byte>(signature->has_templated_return_type))
		                 | (static_cast<byte>(signature->is_func) << 1)
						 | (static_cast<byte>(signature->templated_parameter_count) << 2)
						 | (static_cast<byte>(signature->is_variadic) << 3);

		return fnv1a_step(hash, flags) | 1;
	}

	case CompositeKind::User:
	{
		const CompositeUserExtraData* const user = reinterpret_cast<const CompositeUserExtraData*>(attach + 1);

		if ((attach->flags & CompositeFlags::User_IsOpen) != CompositeFlags::EMPTY)
			return 0;

		return fnv1a_step(hash, range::from_object_bytes(user)) | 1;
	}

	case CompositeKind::INVALID:
		; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

static u32 hash_composite_type(CoreData* core, u32 hash, SeenSet* seen, const CompositeType* attach) noexcept
{
	hash = hash_composite_type_extra_data(core, hash, seen, attach);

	if (hash == 0)
		return hash;

	CompositeInfo info = composite_info(attach);

	hash = fnv1a_step(hash, Range{ info.member_names, info.member_count }.as_byte_range()) | 1;

	if (info.kind == CompositeKind::File || info.kind == CompositeKind::Impl)
		return hash;

	for (u32 i = 0; i != info.member_count; ++i)
	{
		const CompositeMember member_type = info.member_types[i];

		if (member_type.is_pending)
		{
			if (info.kind == CompositeKind::Signature)
				continue;
			else
				return 0;
		}

		ASSERT_OR_IGNORE(!member_type.is_pending);

		const byte flags = (static_cast<byte>(member_type.is_pub))
		                 | (static_cast<byte>(member_type.is_mut) << 1)
		                 | (static_cast<byte>(member_type.is_eval) << 2);

		hash = fnv1a_step(hash, flags) | 1;

		hash = hash_type_id(core, hash, seen, member_type.type_id);

		if (hash == 0)
			return 0;
	}

	return hash;
}

static u32 hash_self_type(CoreData* core, u32 hash, SeenSet* seen, const SelfType* attach) noexcept
{
	if (!attach->has_arguments)
		return 0;

	for (u32 i = 0; i != attach->argument_count; ++i)
	{
		hash = hash_type_id(core, hash, seen, attach->argument_type_ids[i]);

		if (hash == 0)
			return 0;
	}

	hash = hash_type_id(core, hash, seen, attach->base_type_id);

	if (hash == 0)
		return 0;

	return fnv1a_step(hash, range::from_object_bytes(&attach->trait_body_opcode_id)) | 1;
}

static u32 hash_numeric_type([[maybe_unused]] CoreData* core, u32 hash, const NumericType* attach) noexcept
{
	return fnv1a_step(fnv1a_step(hash, static_cast<byte>(attach->is_signed)), range::from_object_bytes(&attach->bits)) | 1;
}

static u32 hash_reference_type([[maybe_unused]] CoreData* core, u32 hash, SeenSet* seen, const ReferenceType* attach) noexcept
{
	const byte flags = (static_cast<byte>(attach->is_multi))
	                 | (static_cast<byte>(attach->is_mut) << 1)
	                 | (static_cast<byte>(attach->is_opt) << 2);

	hash = fnv1a_step(hash, flags) | 1;

	return hash_type_id(core, hash, seen, attach->referenced_type_id);
}

static u32 hash_array_type(CoreData* core, u32 hash, SeenSet* seen, const ArrayType* attach) noexcept
{
	hash = fnv1a_step(hash, range::from_object_bytes(&attach->element_count)) | 1;

	if (is_some(attach->element_type))
		return hash_type_id(core, hash, seen, get(attach->element_type));
	else
		return hash;
}

static u32 hash_type_structure_preseen(CoreData* core, u32 hash, SeenSet* seen, TypeStructure* structure) noexcept
{
	if (structure->hash != 0)
		return structure->hash;

	const TypeTag tag = structure->tag;

	hash = fnv1a_step(hash, static_cast<byte>(tag)) | 1;

	switch (tag)
	{
	case TypeTag::Void:
	case TypeTag::Type:
	case TypeTag::CompInteger:
	case TypeTag::CompFloat:
	case TypeTag::Boolean:
	case TypeTag::TypeInfo:
	case TypeTag::TypeBuilder:
	case TypeTag::Divergent:
	case TypeTag::Undefined:
		return hash;

	case TypeTag::Integer:
	case TypeTag::Float:
		return hash_numeric_type(core, hash, reinterpret_cast<const NumericType*>(structure->attach));

	case TypeTag::Slice:
	case TypeTag::Ptr:
	case TypeTag::TailArray:
		return hash_reference_type(core, hash, seen, reinterpret_cast<const ReferenceType*>(structure->attach));

	case TypeTag::Array:
	case TypeTag::ArrayLiteral:
		return hash_array_type(core, hash, seen, reinterpret_cast<const ArrayType*>(structure->attach));

	case TypeTag::Signature:
	case TypeTag::Composite:
	case TypeTag::CompositeLiteral:
	case TypeTag::Trait:
		return hash_composite_type(core, hash, seen, reinterpret_cast<const CompositeType*>(structure->attach));

	case TypeTag::Self:
		return hash_self_type(core, hash, seen, reinterpret_cast<const SelfType*>(structure->attach));

	case TypeTag::Variadic:
	case TypeTag::Definition:
		TODO("Implement `hash_type_structure_preseen(%)`.", tag_name(tag));

	case TypeTag::INVALID:
	case TypeTag::INDIRECTION:
		; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

static u32 hash_type_structure(CoreData* core, u32 hash, SeenSet* seen, TypeStructure* structure) noexcept
{
	const TypeId id = id_from_structure(core, structure);

	// If we have already encountered this type we cannot recurse into it
	// again, since that would lead to infinite recursion. As such, we just
	// return the unmodified hash value.
	const u32 seen_index = seen_set_push(core, seen, id);

	if (seen_index == 0)
		hash = hash_type_structure_preseen(core, hash, seen, structure);
	else
		hash = fnv1a_step(hash, range::from_object_bytes(&seen_index)) | 1;

	seen_set_pop(seen);

	return hash;
}

static u32 hash_type_id(CoreData* core, u32 hash, SeenSet* seen, TypeId id) noexcept
{
	const Maybe<TypeStructure*> structure = structure_from_id_follow(core, id);

	if (is_none(structure))
		return 0;

	return hash_type_structure(core, hash, seen, get(structure));
}

static bool init_structure_hash(CoreData* core, TypeStructure* structure) noexcept
{
	ASSERT_OR_IGNORE(structure->hash == 0);

	const u64 mark = temp_stack_mark(core);

	SeenSet seen = seen_set_create(core);

	const u32 hash = hash_type_structure(core, FNV1A_SEED, &seen, structure);

	temp_stack_release(core, mark);

	if (hash == 0)
		return false;

	structure->hash = hash;

	if (structure->tag != TypeTag::Composite && structure->tag != TypeTag::CompositeLiteral)
		return true;

	CompositeType* const composite = reinterpret_cast<CompositeType*>(structure + 1);

	if (composite->kind == CompositeKind::User)
		init_user_composite_shadow_data(core, composite);

	return true;
}



static TypeRelation type_can_implicitly_convert_from_to(CoreData* core, TypeId from_type_id, TypeId to_type_id) noexcept;

static TypeRelation type_can_implicitly_convert_from_to_assume_unequal(CoreData* core, TypeId from_type_id, TypeId to_type_id) noexcept
{
	const Maybe<TypeStructure*> opt_from = structure_from_id_follow(core, from_type_id);

	const Maybe<TypeStructure*> opt_to = structure_from_id_follow(core, to_type_id);

	if (is_none(opt_from) || is_none(opt_to))
		return TypeRelation::Incomplete;

	const TypeStructure* const from = get(opt_from);

	const TypeStructure* const to = get(opt_to);

	const TypeTag to_tag = to->tag;

	const TypeTag from_tag = from->tag;

	if (to_tag == TypeTag::TypeInfo)
		return TypeRelation::FirstConvertsToSecond;

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
	case TypeTag::Array:
	case TypeTag::Signature:
	case TypeTag::Composite:
	case TypeTag::TailArray:
	case TypeTag::Variadic:
	case TypeTag::Trait:
	{
		return TypeRelation::Unrelated;
	}

	case TypeTag::CompInteger:
	{
		return to_tag == TypeTag::Integer
			? TypeRelation::FirstConvertsToSecond
			: TypeRelation::Unrelated;
	}

	case TypeTag::CompFloat:
	{
		return to_tag == TypeTag::Float
			? TypeRelation::FirstConvertsToSecond
			: TypeRelation::Unrelated;
	}

	case TypeTag::Divergent:
	{
		return TypeRelation::FirstConvertsToSecond;
	}

	case TypeTag::Undefined:
	{
		return TypeRelation::FirstConvertsToSecond;
	}

	case TypeTag::Slice:
	{
		if (to_tag != TypeTag::Slice)
			return TypeRelation::Unrelated;

		const ReferenceType* const from_attach = reinterpret_cast<const ReferenceType*>(from->attach);

		const ReferenceType* const to_attach = reinterpret_cast<const ReferenceType*>(to->attach);

		return from_attach->is_mut || !to_attach->is_mut
			? TypeRelation::FirstConvertsToSecond
			: TypeRelation::Unrelated;
	}

	case TypeTag::Ptr:
	{
		if (to_tag != TypeTag::Ptr)
			return TypeRelation::Unrelated;

		const ReferenceType* const from_attach = reinterpret_cast<const ReferenceType*>(from->attach);

		const ReferenceType* const to_attach = reinterpret_cast<const ReferenceType*>(to->attach);

		const bool is_qualifier_convertible = (from_attach->is_mut || !to_attach->is_mut)
		                                   && (!from_attach->is_opt || to_attach->is_opt)
			                               && (from_attach->is_multi || !to_attach->is_multi);

		return is_qualifier_convertible
			? TypeRelation::FirstConvertsToSecond
			: TypeRelation::Unrelated;
	}

	case TypeTag::CompositeLiteral:
	{
		if (to_tag != TypeTag::Composite)
			return TypeRelation::Unrelated;

		const CompositeType* const from_attach = reinterpret_cast<const CompositeType*>(from->attach);

		const CompositeType* const to_attach = reinterpret_cast<const CompositeType*>(to->attach);

		ASSERT_OR_IGNORE(from_attach->kind == CompositeKind::User);

		if (to_attach->kind != CompositeKind::User)
			return TypeRelation::Unrelated;

		const u32 required_seen_qwords = to_attach->member_used / 64 + 63;

		const u64 mark = temp_stack_mark(core);

		u64* seen_member_bits = static_cast<u64*>(temp_stack_alloc(core, required_seen_qwords, sizeof(u64)));

		memset(seen_member_bits, 0, required_seen_qwords * sizeof(u64));

		u32 to_rank = 0;

		const CompositeInfo from_info = composite_info(from_attach);

		const CompositeInfo to_info = composite_info(to_attach);

		// Check whether all members in `from` have a corresponding non-global
		// member in `to` of a type they can be converted to.
		for (u32 i = 0; i != from_info.member_count; ++i)
		{
			const CompositeMember from_member = from_info.member_types[i];

			const IdentifierId from_name = from_info.member_names[i];

			if (from_name != IdentifierId::INVALID)
			{
				u16 found_rank;

				if (!find_member_by_name(to_info, from_name, &found_rank))
				{
					temp_stack_release(core, mark);

					return TypeRelation::Unrelated;
				}

				to_rank = found_rank;
			}

			const CompositeMember to_member = to_info.member_types[to_rank];

			if (to_member.is_pending)
				TODO("Implement implicit convertability check from composite literal to incomplete type");

			const TypeRelation member_relation = type_can_implicitly_convert_from_to(core, from_member.type_id, to_member.type_id);

			ASSERT_OR_IGNORE(member_relation != TypeRelation::SecondConvertsToFirst);

			if (member_relation != TypeRelation::Equal && member_relation != TypeRelation::FirstConvertsToSecond)
			{
				temp_stack_release(core, mark);

				return member_relation;
			}

			seen_member_bits[to_rank >> 6] |= static_cast<u64>(1) << (to_rank & 63);

			to_rank += 1;
		}

		// Check whether all members of `to` that are not contained in `from`
		// have a default value.
		for (u32 i = 0; i != to_attach->member_used; ++i)
		{
			if ((seen_member_bits[i >> 6] & (static_cast<u64>(1) << (i & 63))) != 0)
				continue;

			const CompositeMember to_member = to_info.member_types[i];

			if (is_none(to_member.value_or_default))
			{
				temp_stack_release(core, mark);

				return TypeRelation::Unrelated;
			}
		}

		temp_stack_release(core, mark);

		return TypeRelation::FirstConvertsToSecond;
	}

	case TypeTag::ArrayLiteral:
	{
		if (to_tag != TypeTag::Array && to_tag != TypeTag::ArrayLiteral)
			return TypeRelation::Unrelated;

		const ArrayType* const from_attach = reinterpret_cast<const ArrayType*>(from->attach);

		const ArrayType* const to_attach = reinterpret_cast<const ArrayType*>(to->attach);

		if (from_attach->element_count != to_attach->element_count)
			return TypeRelation::Unrelated;

		// An empty array literal with no element type can be converted to
		// an empty array or array literal with any other element type.
		if (is_none(from_attach->element_type))
			return TypeRelation::FirstConvertsToSecond;

		const TypeRelation element_relation = type_can_implicitly_convert_from_to(core, get(from_attach->element_type), get(to_attach->element_type));

		ASSERT_OR_IGNORE(element_relation != TypeRelation::SecondConvertsToFirst);

		return element_relation == TypeRelation::Equal
			? TypeRelation::FirstConvertsToSecond
			: element_relation;
	}

	case TypeTag::Self:
	{
		const SelfType* const attach = reinterpret_cast<const SelfType*>(from + 1);

		const TypeRelation base_relation = type_can_implicitly_convert_from_to(core, attach->base_type_id, to_type_id);

		return base_relation == TypeRelation::Equal
			? TypeRelation::FirstConvertsToSecond
			: base_relation;
	}

	case TypeTag::INVALID:
	case TypeTag::INDIRECTION:
		break; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

static TypeRelation type_can_implicitly_convert_from_to(CoreData* core, TypeId from_type_id, TypeId to_type_id) noexcept
{
	ASSERT_OR_IGNORE(from_type_id != TypeId::INVALID && to_type_id != TypeId::INVALID);

	const TypeEquality equality = type_is_equal(core, from_type_id, to_type_id);

	if (equality != TypeEquality::Unequal)
		return static_cast<TypeRelation>(equality);

	return type_can_implicitly_convert_from_to_assume_unequal(core, from_type_id, to_type_id);
}

static TypeEquality type_is_equal_noloop(CoreData* core, TypeId type_id_a, TypeId type_id_b, SeenSet* a_seen, SeenSet* b_seen) noexcept
{
	if (type_id_a == type_id_b)
		return TypeEquality::Equal;

	Maybe<TypeStructure*> const opt_a = structure_from_id_follow(core, type_id_a);

	Maybe<TypeStructure*> const opt_b = structure_from_id_follow(core, type_id_b);

	if (is_none(opt_a) || is_none(opt_b))
		return TypeEquality::Incomplete;

	TypeStructure* const a = get(opt_a);

	TypeStructure* const b = get(opt_b);

	if (a == b)
		return TypeEquality::Equal;

	if (a->hash == 0 && init_structure_hash(core, a))
		(void) holotype_id_from_interned_type_structure(core, a, false);

	if (b->hash == 0 && init_structure_hash(core, b))
		(void) holotype_id_from_interned_type_structure(core, b, false);

	// This is assumed to be the common case, so we check for it as early as
	// possible.
	if (a->holotype_id != TypeId::INVALID && b->holotype_id != TypeId::INVALID)
		return a->holotype_id == b->holotype_id ? TypeEquality::Equal : TypeEquality::Unequal;

	// From here on we perform a "deep" equality check. This is only necessary
	// if either `a` or `b` are open composite types or reference an open
	// composite type. Otherwise, they will both already be hashed into
	// `holotypes` when we are called, or the attempts to hash them inside the
	// call will have succeeded. In both cases, they will have their holotypes
	// set, meaning that the above test will succeed, leading to an early-out.

	// Types with differing tags can never be equal.
	if (a->tag != b->tag)
		return TypeEquality::Unequal;

	const TypeTag tag = a->tag;

	const u32 a_seen_index = seen_set_push(core, a_seen, type_id_a);

	const u32 b_seen_index = seen_set_push(core, b_seen, type_id_b);

	// If the returned back-loop indices are unequal, the types we are
	// currently comparing are also unequal.
	//
	// TODO: This does not actually necessarily hold, if the back-loop at the
	// lower non-zero index would compare equal to the type at that index in
	// the other `SeenSet`. This is however super-duper exotic, and I'm not
	// even sure how it could be made to happen right now, even though it seems
	// possible to concoct something in theory.
	if (a_seen_index != b_seen_index)
		return TypeEquality::Unequal;

	// If both types reference a type we have already seen, and that type is
	// the same number of steps removed from both types, we consider the types
	// equal. If the types in question turn out to be different at a later
	// point, e.g. by having different later members, the comparison as a whole
	// will still return `false`. Otherwise, our assumption that the
	// back-references were equal holds and we can correctly return `true`.
	//
	// Note also that we cannot recurse here, since this would lead to an
	// infinite loop (or rather an overflow of our `SeenSet`s).
	if (a_seen_index != 0)
		return TypeEquality::Equal;

	switch (tag)
	{
	case TypeTag::Void:
	case TypeTag::Type:
	case TypeTag::Definition:
	case TypeTag::CompInteger:
	case TypeTag::CompFloat:
	case TypeTag::Boolean:
	case TypeTag::TypeInfo:
	case TypeTag::TypeBuilder:
	case TypeTag::Divergent:
	case TypeTag::Undefined:
	{
		seen_set_pop(a_seen);

		seen_set_pop(b_seen);

		return TypeEquality::Equal;
	}

	case TypeTag::Integer:
	case TypeTag::Float:
	{
		const NumericType* const a_attach = reinterpret_cast<NumericType*>(a->attach);

		const NumericType* const b_attach = reinterpret_cast<NumericType*>(b->attach);

		seen_set_pop(a_seen);

		seen_set_pop(b_seen);

		return a_attach->bits == b_attach->bits && a_attach->is_signed == b_attach->is_signed
			? TypeEquality::Equal
			: TypeEquality::Unequal;
	}

	case TypeTag::Signature:
	case TypeTag::Composite:
	case TypeTag::CompositeLiteral:
	case TypeTag::Trait:
	{
		const CompositeType* const a_attach = reinterpret_cast<CompositeType*>(a->attach);

		const CompositeType* const b_attach = reinterpret_cast<CompositeType*>(b->attach);

		if (a_attach->kind != b_attach->kind || a_attach->member_used != b_attach->member_used)
		{
			seen_set_pop(a_seen);

			seen_set_pop(b_seen);

			return TypeEquality::Unequal;
		}

		const CompositeInfo a_info = composite_info(a_attach);

		const CompositeInfo b_info = composite_info(b_attach);

		switch (a_info.kind)
		{
		case CompositeKind::Impl:
		{
			// `impl` bodies are only equal if their `TypeId`s are equal.
			// If they were equal, we would have returned `true` already, so we
			// can safely return `false` instead.

			seen_set_pop(a_seen);

			seen_set_pop(b_seen);

			return TypeEquality::Unequal;
		}

		case CompositeKind::Signature:
		{
			const CompositeSignatureExtraData* const a_signature = reinterpret_cast<const CompositeSignatureExtraData*>(a_attach + 1);

			const CompositeSignatureExtraData* const b_signature = reinterpret_cast<const CompositeSignatureExtraData*>(b_attach + 1);

			if (a_signature->has_templated_return_type != b_signature->has_templated_return_type
			 || a_signature->templated_parameter_count != b_signature->templated_parameter_count
			 || a_signature->is_func != b_signature->is_func
			 || a_signature->is_variadic != b_signature->is_variadic
			 || is_some(a_signature->closure_id) != is_some(b_signature->closure_id)
			) {
				seen_set_pop(a_seen);

				seen_set_pop(b_seen);

				return TypeEquality::Unequal;
			}

			if (a_signature->has_templated_return_type)
			{
				TODO("Handle comparison of signature types with templated return types.");
			}
			else
			{
				const TypeEquality return_type_equality = type_is_equal_noloop(core, a_signature->return_type.type_id, b_signature->return_type.type_id, a_seen, b_seen);

				if (return_type_equality != TypeEquality::Equal)
				{
					seen_set_pop(a_seen);

					seen_set_pop(b_seen);

					return return_type_equality;
				}
			}

			if (is_some(a_signature->closure_id))
				TODO("Handle comparison of signatures that hold a closure.");

			break;
		}

		case CompositeKind::User:
		{
			const CompositeUserExtraData* const a_user = reinterpret_cast<const CompositeUserExtraData*>(a_attach + 1);

			const CompositeUserExtraData* const b_user = reinterpret_cast<const CompositeUserExtraData*>(b_attach + 1);

			if (
				a_user->size != b_user->size
			 || a_user->stride != b_user->stride
			 || a_user->align != b_user->align
			 || a_user->definition_site != b_user->definition_site
			) {
				seen_set_pop(a_seen);

				seen_set_pop(b_seen);

				return TypeEquality::Unequal;
			}

			break;
		}

		default:
			break;
		}

		for (u16 rank = 0; rank != a_attach->member_used; ++rank)
		{
			const CompositeMember a_member = a_info.member_types[rank];

			const CompositeMember b_member = b_info.member_types[rank];

			if (a_member.is_pending || b_member.is_pending)
				TODO("Handle pending composite member types in `type_is_equal_noloop`");

			if (a_member.is_pub != b_member.is_pub
			 || a_member.is_mut != b_member.is_mut
			 || a_member.is_eval != b_member.is_eval
			) {
				seen_set_pop(a_seen);

				seen_set_pop(b_seen);

				return TypeEquality::Unequal;
			}

			if (a_member.value_or_default != b_member.value_or_default)
			{
				seen_set_pop(a_seen);

				seen_set_pop(b_seen);

				return TypeEquality::Unequal;
			}

			ASSERT_OR_IGNORE((a_member.type_id == TypeId::INVALID) == (b_member.type_id == TypeId::INVALID));

			if (a_member.type_id != TypeId::INVALID && b_member.type_id != TypeId::INVALID)
			{
				const TypeEquality member_equality = type_is_equal_noloop(core, a_member.type_id, b_member.type_id, a_seen, b_seen);

				if (member_equality != TypeEquality::Equal)
				{
					seen_set_pop(a_seen);

					seen_set_pop(b_seen);

					return member_equality;
				}
			}
		}

		ASSERT_OR_IGNORE(is_some(a_info.member_offsets) == is_some(b_info.member_offsets));

		if (is_some(a_info.member_offsets))
		{
			if (memcmp(get(a_info.member_offsets), get(b_info.member_offsets), a_info.member_count * sizeof(s64)) != 0)
			{
				seen_set_pop(a_seen);

				seen_set_pop(b_seen);

				return TypeEquality::Unequal;
			}
		}

		seen_set_pop(a_seen);

		seen_set_pop(b_seen);

		return TypeEquality::Equal;
	}

	case TypeTag::TailArray:
	case TypeTag::Slice:
	case TypeTag::Ptr:
	{
		const ReferenceType* const a_attach = reinterpret_cast<ReferenceType*>(a->attach);

		const ReferenceType* const b_attach = reinterpret_cast<ReferenceType*>(b->attach);

		if (a_attach->is_multi != b_attach->is_multi || a_attach->is_mut != b_attach->is_mut || a_attach->is_opt != b_attach->is_opt)
		{
			seen_set_pop(a_seen);

			seen_set_pop(b_seen);

			return TypeEquality::Unequal;
		}

		const TypeId a_next = a_attach->referenced_type_id;

		const TypeId b_next = b_attach->referenced_type_id;

		const TypeEquality referenced_equality = type_is_equal_noloop(core, a_next, b_next, a_seen, b_seen);

		seen_set_pop(a_seen);

		seen_set_pop(b_seen);

		return referenced_equality;
	}

	case TypeTag::Array:
	case TypeTag::ArrayLiteral:
	{
		const ArrayType* const a_attach = reinterpret_cast<ArrayType*>(a->attach);

		const ArrayType* const b_attach = reinterpret_cast<ArrayType*>(b->attach);

		if (a_attach->element_count != b_attach->element_count)
		{
			seen_set_pop(a_seen);

			seen_set_pop(b_seen);

			return TypeEquality::Unequal;
		}

		const Maybe<TypeId> a_next = a_attach->element_type;

		const Maybe<TypeId> b_next = b_attach->element_type;

		TypeEquality element_result;

		// Array literals may have `TypeId::INVALID` as their element type if
		// they have no elements, because no element type can be inferred in
		// that case. This needs to be special cased to avoid recursing on a
		// `TypeId::INVALID`.
		if (tag == TypeTag::ArrayLiteral && (is_none(a_next) || is_none(b_next)))
			element_result = is_none(a_next) && is_none(b_next) ? TypeEquality::Equal : TypeEquality::Unequal;
		else
			element_result = type_is_equal_noloop(core, get(a_next), get(b_next), a_seen, b_seen);

		seen_set_pop(a_seen);

		seen_set_pop(b_seen);

		return element_result;
	}

	case TypeTag::Self:
	{
		const SelfType* const a_attach = reinterpret_cast<SelfType*>(a->attach);

		const SelfType* const b_attach = reinterpret_cast<SelfType*>(b->attach);

		ASSERT_OR_IGNORE(a_attach->has_arguments && b_attach->has_arguments);

		if (a_attach->argument_count != b_attach->argument_count
		 || a_attach->definition_source_id != b_attach->definition_source_id
		 || a_attach->trait_body_opcode_id != b_attach->trait_body_opcode_id
		) {
			seen_set_pop(a_seen);

			seen_set_pop(b_seen);

			return TypeEquality::Unequal;
		}

		const TypeEquality trait_equality = type_is_equal_noloop(core, a_attach->trait_type_id, b_attach->trait_type_id, a_seen, b_seen);

		if (trait_equality != TypeEquality::Equal)
		{
			seen_set_pop(a_seen);

			seen_set_pop(b_seen);

			return TypeEquality::Unequal;
		}

		for (u8 i = 0; i != a_attach->argument_count; ++i)
		{
			const TypeEquality argument_equality = type_is_equal_noloop(core, a_attach->argument_type_ids[i], b_attach->argument_type_ids[i], a_seen, b_seen);

			if (argument_equality != TypeEquality::Equal)
			{
				seen_set_pop(a_seen);

				seen_set_pop(b_seen);

				return argument_equality;
			}
		}

		if (is_some(a_attach->trait_closure_id) != is_some(b_attach->trait_closure_id)
		 || is_some(a_attach->impl_closure_id) != is_some(b_attach->impl_closure_id)
		) {
			seen_set_pop(a_seen);

			seen_set_pop(b_seen);

			return TypeEquality::Unequal;
		}

		if (is_some(a_attach->trait_closure_id)
		 && !closure_equal(core, get(a_attach->trait_closure_id), get(b_attach->trait_closure_id))
		) {
			seen_set_pop(a_seen);

			seen_set_pop(b_seen);

			return TypeEquality::Unequal;
		}

		if (is_some(a_attach->impl_closure_id)
		 && !closure_equal(core, get(a_attach->impl_closure_id), get(b_attach->impl_closure_id))
		) {
			seen_set_pop(a_seen);

			seen_set_pop(b_seen);

			return TypeEquality::Unequal;
		}

		seen_set_pop(a_seen);

		seen_set_pop(b_seen);

		return TypeEquality::Equal;
	}

	case TypeTag::Variadic:
		TODO("Implement `type_is_equal_noloop(%)`.", tag_name(tag));

	case TypeTag::INVALID:
	case TypeTag::INDIRECTION:
		break; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}



static TypeStructure* make_structure_nohash(CoreData* core, TypeTag tag, Range<byte> attach, u64 reserve_size) noexcept
{
	ASSERT_OR_IGNORE(reserve_size <= UINT16_MAX && reserve_size >= attach.count());

	const Maybe<void*> allocation = comp_heap_alloc(core, sizeof(TypeStructure) + reserve_size, 16);

	if (is_none(allocation))
		TODO("Implement GC traversal.");

	TypeStructure* const structure = static_cast<TypeStructure*>(get(allocation));
	structure->tag = tag;
	structure->holotype_id = TypeId::INVALID;
	structure->hash = 0;
	structure->unused_ = 0;

	if (attach.count() != 0)
		memcpy(structure->attach, attach.begin(), attach.count());

	return structure;
}

static TypeStructure* make_structure_hash(CoreData* core, TypeTag tag, Range<byte> attach, u64 reserve_size) noexcept
{
	TypeStructure* const structure = make_structure_nohash(core, tag, attach, reserve_size);

	(void) init_structure_hash(core, structure);

	return structure;
}



bool type_pool_validate_config([[maybe_unused]] const Config* config, [[maybe_unused]] PrintSink sink) noexcept
{
	return true;
}

MemoryRequirements type_pool_memory_requirements([[maybe_unused]] const Config* config) noexcept
{
	MemoryRequirements reqs;
	reqs.count = 1;
	reqs.ranges[0].size = HOLOTYPES_LOOKUPS_RESERVE + HOLOTYPES_VALUES_RESERVE;
	reqs.ranges[0].max_offset = UINT64_MAX;

	return reqs;
}

void type_pool_init(CoreData* core, MemoryAllocation allocation) noexcept
{
	TypePool* const types = &core->types;

	u64 offset = 0;

	const MutRange<byte> dedup_lookups_memory = allocation.ranges[0].mut_subrange(offset, HOLOTYPES_LOOKUPS_RESERVE);
	offset += HOLOTYPES_LOOKUPS_RESERVE;

	const MutRange<byte> dedup_values_memory = allocation.ranges[0].mut_subrange(offset, HOLOTYPES_VALUES_RESERVE);
	offset += HOLOTYPES_VALUES_RESERVE;

	types->holotypes.init(dedup_lookups_memory, HOLOTYPES_LOOKUPS_INITIAL_COMMIT_COUNT, HolotypeAlloc{ core });

	types->holotype_entries.init(dedup_values_memory, HOLOTYPES_VALUES_COMMIT_INCREMENT_COUNT);

	// Reserve simple types for use with `type_create_simple`.
	
	TypeStructure* const first_simple_structure = make_structure_nohash(core, TypeTag::Void, {}, 0);

	core->types.simple_type_base_id = core_id_from_address(core, first_simple_structure);

	for (u8 ordinal = static_cast<u8>(TypeTag::Void) + 1; ordinal != static_cast<u8>(TypeTag::Undefined) + 1; ++ordinal)
	{
		// TODO: Once hashing is implemented for all types, this pseudo-hashing
		// can be removed.
		TypeStructure* const simple_structure = make_structure_nohash(core, static_cast<TypeTag>(ordinal), {}, 0);
		simple_structure->hash = ordinal;

		(void) holotype_id_from_interned_type_structure(core, simple_structure, false);
	}
}



TypeId type_create_simple([[maybe_unused]] CoreData* core, TypeTag tag) noexcept
{
	ASSERT_OR_IGNORE(tag >= TypeTag::Void && tag <= TypeTag::Undefined);

	const TypeId type_id = static_cast<TypeId>(static_cast<u32>(core->types.simple_type_base_id) + static_cast<u32>(tag) - static_cast<u32>(TypeTag::Void));

	ASSERT_OR_IGNORE(type_tag_from_id(core, type_id) == tag);

	return type_id;
}

TypeId type_create_numeric(CoreData* core, TypeTag tag, NumericType attach) noexcept
{
	ASSERT_OR_IGNORE(tag == TypeTag::Integer || tag == TypeTag::Float);

	return holotype_id_from_attachment(core, tag, range::from_object_bytes(&attach));
}

TypeId type_create_reference(CoreData* core, TypeTag tag, ReferenceType attach) noexcept
{
	ASSERT_OR_IGNORE(tag == TypeTag::Ptr || tag == TypeTag::Slice || tag == TypeTag::TailArray || tag == TypeTag::Variadic);

	return holotype_id_from_attachment(core, tag, range::from_object_bytes(&attach));
}

TypeId type_create_array(CoreData* core, TypeTag tag, ArrayType attach) noexcept
{
	ASSERT_OR_IGNORE(tag == TypeTag::Array || tag == TypeTag::ArrayLiteral);

	// Carve out exception for array literal with no elements having no
	// intrinsic element type.
	ASSERT_OR_IGNORE(is_some(attach.element_type) || (tag == TypeTag::ArrayLiteral && attach.element_count == 0));

	return holotype_id_from_attachment(core, tag, range::from_object_bytes(&attach));
}



TypeId type_create_indirection(CoreData* core) noexcept
{
	IndirectionType attach{};
	attach.indirection_type_id = none<TypeId>();

	TypeStructure* const structure = make_structure_nohash(core, TypeTag::INDIRECTION, range::from_object_bytes(&attach), sizeof(attach));

	return id_from_structure(core, structure);
}

void type_set_indirection_target(CoreData* core, TypeId indirection_type_id, TypeId target_type_id) noexcept
{
	TypeStructure* const structure = structure_from_id_direct(core, indirection_type_id);

	IndirectionType* const indirection = reinterpret_cast<IndirectionType*>(structure + 1);

	ASSERT_OR_IGNORE(is_none(indirection->indirection_type_id));

	indirection->indirection_type_id = some(target_type_id);
}



TypeId type_create_signature(CoreData* core, bool is_func, u8 parameter_count) noexcept
{
	ASSERT_OR_IGNORE(parameter_count <= 64);

	TypeStructure* const structure = composite_alloc(core, TypeTag::Signature, CompositeKind::Signature, is_func ? CompositeFlags::Signature_IsFunc : CompositeFlags::EMPTY, parameter_count, false);

	CompositeType* const composite = reinterpret_cast<CompositeType*>(structure + 1);

	CompositeSignatureExtraData* const signature = reinterpret_cast<CompositeSignatureExtraData*>(composite + 1);
	signature->return_type.type_id = TypeId::INVALID;
	signature->closure_id = none<ClosureId>();
	signature->is_func = is_func;
	signature->templated_parameter_count = 0;
	signature->has_templated_return_type = false;
	signature->is_variadic = false;
	signature->unused_ = 0;

	return id_from_structure(core, structure);
}

void type_add_signature_parameter(CoreData* core, TypeId type_id, SignatureParameterInit init) noexcept
{
	TypeStructure* const structure = structure_from_id_direct(core, type_id);

	ASSERT_OR_IGNORE(structure->tag == TypeTag::Signature);

	CompositeType* const composite = reinterpret_cast<CompositeType*>(structure + 1);

	ASSERT_OR_IGNORE(composite->kind == CompositeKind::Signature);

	ASSERT_OR_IGNORE(composite->member_used < composite->member_capacity);

	CompositeInfo info = composite_info(composite);

	info.member_names[info.member_count] = init.name;

	info.member_types[info.member_count].is_pending = init.is_templated;
	info.member_types[info.member_count].is_initializing = false;
	info.member_types[info.member_count].is_pub = false;
	info.member_types[info.member_count].is_mut = init.is_mut;
	info.member_types[info.member_count].is_eval = init.is_eval;

	if (init.is_templated)
	{
		info.member_types[info.member_count].type_id = TypeId::INVALID;
		info.member_types[info.member_count].completion_id = init.templated.completion_id;

		CompositeSignatureExtraData* const signature = reinterpret_cast<CompositeSignatureExtraData*>(composite + 1);

		signature->templated_parameter_count += 1;
	}
	else
	{
		info.member_types[info.member_count].type_id = init.complete.type_id;
		info.member_types[info.member_count].value_or_default = init.complete.default_value;
	}

	composite->member_used += 1;
}

TypeId type_seal_signature(CoreData* core, TypeId type_id, SignatureSealInfo seal_info) noexcept
{
	TypeStructure* const structure = structure_from_id_direct(core, type_id);

	ASSERT_OR_IGNORE(structure->tag == TypeTag::Signature);

	CompositeType* const composite = reinterpret_cast<CompositeType*>(structure + 1);

	CompositeSignatureExtraData* const signature = reinterpret_cast<CompositeSignatureExtraData*>(composite + 1);

	if (seal_info.has_templated_return_type)
	{
		signature->has_templated_return_type = true;
		signature->return_type.completion_id = seal_info.return_type.templated.completion_id;
	}
	else
	{
		signature->has_templated_return_type = false;
		signature->return_type.type_id = seal_info.return_type.complete.type_id;
	}

	signature->closure_id = seal_info.closure_id;
	signature->is_variadic = seal_info.is_variadic;

	if (!init_structure_hash(core, structure))
		ASSERT_UNREACHABLE;

	return holotype_id_from_interned_type_structure(core, structure, false);
}

TypeId type_instantiate_templated_signature(CoreData* core, TypeId type_id) noexcept
{
	TypeStructure* const original_structure = structure_from_id_direct(core, type_id);

	ASSERT_OR_IGNORE(original_structure->tag == TypeTag::Signature);

	CompositeType* const original_composite = reinterpret_cast<CompositeType*>(original_structure + 1);

	ASSERT_OR_IGNORE(
		reinterpret_cast<CompositeSignatureExtraData*>(original_composite + 1)->templated_parameter_count != 0
	 || reinterpret_cast<CompositeSignatureExtraData*>(original_composite + 1)->has_templated_return_type
	);

	TypeStructure* const copied_structure = composite_alloc(core, TypeTag::Signature, CompositeKind::Signature, original_composite->flags, original_composite->member_used, false);

	CompositeType* const copied_composite = reinterpret_cast<CompositeType*>(copied_structure + 1);
	copied_composite->member_used = original_composite->member_used;

	ASSERT_OR_IGNORE(original_composite->member_capacity == copied_composite->member_capacity);

	const u64 tail_size = sizeof(CompositeSignatureExtraData) + original_composite->member_capacity * (sizeof(IdentifierId) + sizeof(CompositeMember));

	memcpy(copied_composite + 1, original_composite + 1, tail_size);

	return id_from_structure(core, copied_structure);
}

void type_complete_templated_signature_parameter(CoreData* core, TypeId type_id, u16 rank, TypeId member_type_id, Maybe<CoreId> default_value) noexcept
{
	TypeStructure* const structure = structure_from_id_direct(core, type_id);

	ASSERT_OR_IGNORE(structure->tag == TypeTag::Signature);

	CompositeType* const composite = reinterpret_cast<CompositeType*>(structure + 1);

	ASSERT_OR_IGNORE(rank < composite->member_used);

	CompositeInfo info = composite_info(composite);

	ASSERT_OR_IGNORE(info.member_types[rank].is_pending);

	info.member_types[rank].type_id = member_type_id;
	info.member_types[rank].is_pending = false;
	info.member_types[rank].value_or_default = default_value;

	CompositeSignatureExtraData* const signature = reinterpret_cast<CompositeSignatureExtraData*>(composite + 1);

	ASSERT_OR_IGNORE(signature->templated_parameter_count != 0);

	signature->templated_parameter_count -= 1;
}



TypeId type_create_trait(CoreData* core, Range<IdentifierId> parameter_names) noexcept
{
	ASSERT_OR_IGNORE(parameter_names.count() >= 1 && parameter_names.count() <= 64);

	TypeStructure* const structure = composite_alloc(core, TypeTag::Trait, CompositeKind::Signature, CompositeFlags::EMPTY, static_cast<u32>(parameter_names.count()), false);

	CompositeType* const composite = reinterpret_cast<CompositeType*>(structure + 1);

	const TypeId type_type_id = type_create_simple(core, TypeTag::Type);

	composite->member_used = static_cast<u16>(parameter_names.count());

	CompositeSignatureExtraData* const signature = reinterpret_cast<CompositeSignatureExtraData*>(composite + 1);
	signature->return_type.type_id = type_type_id;
	signature->closure_id = none<ClosureId>();
	signature->is_func = true;
	signature->templated_parameter_count = 0;
	signature->has_templated_return_type = false;
	signature->is_variadic = false;
	signature->unused_ = 0;

	CompositeInfo info = composite_info(composite);

	for (u8 i = 0; i != static_cast<u8>(parameter_names.count()); ++i)
	{
		info.member_names[i] = parameter_names[i];

		info.member_types[i].type_id = type_type_id;
		info.member_types[i].is_pending = false;
		info.member_types[i].is_initializing = false;
		info.member_types[i].is_pub = false;
		info.member_types[i].is_mut = false;
		info.member_types[i].is_eval = true;
		info.member_types[i].value_or_default = none<CoreId>();
	}

	if (!init_structure_hash(core, structure))
		ASSERT_UNREACHABLE;

	return holotype_id_from_interned_type_structure(core, structure, true);
}



TypeId type_create_self(CoreData* core, TypeId base_type_id, TypeId trait_type_id, u8 trait_argument_count, OpcodeId trait_body_opcode_id, SourceId definition_source_id, Maybe<ClosureId> impl_closure_id, Maybe<ClosureId> trait_closure_id) noexcept
{
	byte attach_buffer[sizeof(SelfType)];

	SelfType* const attach = reinterpret_cast<SelfType*>(attach_buffer);
	attach->base_type_id = base_type_id;
	attach->trait_type_id = trait_type_id;
	attach->body_type_id = none<TypeId>();
	attach->definition_source_id = definition_source_id;
	attach->trait_body_opcode_id = trait_body_opcode_id;
	attach->impl_closure_id = impl_closure_id;
	attach->trait_closure_id = trait_closure_id;
	attach->argument_count = trait_argument_count;
	attach->has_arguments = false;

	TypeStructure* const structure = make_structure_nohash(core, TypeTag::Self, range::from_object_bytes(attach), sizeof(SelfType) + trait_argument_count * sizeof(TypeId));

	return id_from_structure(core, structure);
}

TypeId type_set_self_trait_arguments(CoreData* core, TypeId type_id, const TypeId* arguments) noexcept
{
	TypeStructure* const structure = structure_from_id_direct(core, type_id);

	ASSERT_OR_IGNORE(structure->tag == TypeTag::Self);

	SelfType* const self = reinterpret_cast<SelfType*>(structure + 1);

	self->has_arguments = true;

	memcpy(self->argument_type_ids, arguments, self->argument_count * sizeof(TypeId));

	if (!init_structure_hash(core, structure))
		ASSERT_UNREACHABLE;

	return holotype_id_from_interned_type_structure(core, structure, false);
}

void type_set_self_impl_body(CoreData* core, TypeId type_id, TypeId impl_body_type_id) noexcept
{
	TypeStructure* const structure = structure_from_id_direct(core, type_id);

	ASSERT_OR_IGNORE(structure->tag == TypeTag::Self);

	SelfType* const self = reinterpret_cast<SelfType*>(structure + 1);

	self->body_type_id = some(impl_body_type_id);
}



TypeId type_create_impl_body(CoreData* core, TypeId self_type_id, u16 trait_member_count) noexcept
{
	TypeStructure* const structure = composite_alloc(core, TypeTag::Composite, CompositeKind::Impl, CompositeFlags::EMPTY, trait_member_count, false);

	CompositeType* const composite = reinterpret_cast<CompositeType*>(structure + 1);

	CompositeImplExtraData* const impl = reinterpret_cast<CompositeImplExtraData*>(composite + 1);
	impl->self_type_id = self_type_id;
	impl->unused_ = 0;

	return id_from_structure(core, structure);
}

void type_add_impl_body_member(CoreData* core, TypeId type_id, ImplMemberInit init) noexcept
{
	TypeStructure* const structure = structure_from_id_direct(core, type_id);

	ASSERT_OR_IGNORE(structure->tag == TypeTag::Composite);

	CompositeType* const composite = reinterpret_cast<CompositeType*>(structure + 1);

	ASSERT_OR_IGNORE(composite->member_used < composite->member_capacity);

	const u16 rank = composite->member_used;

	ASSERT_OR_IGNORE(composite->kind == CompositeKind::Impl);

	CompositeInfo info = composite_info(composite);

	info.member_names[rank] = init.name;

	info.member_types[rank].type_id = static_cast<TypeId>(init.type_completion_id);
	info.member_types[rank].is_pending = true;
	info.member_types[rank].is_initializing = false;
	info.member_types[rank].is_pub = true;
	info.member_types[rank].is_mut = init.is_mut;
	info.member_types[rank].is_eval = false;
	info.member_types[rank].is_impl_member_from_trait_default = init.is_from_trait_default;
	info.member_types[rank].completion_id = init.value_completion_id;

	composite->member_used += 1;
}

OpcodeId type_impl_body_member_type_initializer(CoreData* core, TypeId type_id, u16 rank) noexcept
{
	TypeStructure* const structure = structure_from_id_direct(core, type_id);

	ASSERT_OR_IGNORE(structure->tag == TypeTag::Composite);

	CompositeType* const composite = reinterpret_cast<CompositeType*>(structure + 1);

	ASSERT_OR_IGNORE(rank < composite->member_used);

	ASSERT_OR_IGNORE(composite->kind == CompositeKind::Impl);

	CompositeInfo info = composite_info(composite);

	ASSERT_OR_IGNORE(info.member_types[rank].is_pending);

	return static_cast<OpcodeId>(info.member_types[rank].type_id);
}

void type_seal_impl_body(CoreData* core, TypeId type_id) noexcept
{
	TypeStructure* const structure = structure_from_id_direct(core, type_id);

	ASSERT_OR_IGNORE(structure->tag == TypeTag::Composite);

	ASSERT_OR_IGNORE(reinterpret_cast<CompositeType*>(structure + 1)->kind == CompositeKind::Impl);

	#ifndef NDEBUG
		CompositeInfo info = composite_info(reinterpret_cast<CompositeType*>(structure + 1));

		for (u16 i = 0; i != info.member_count; ++i)
			ASSERT_OR_IGNORE(!info.member_types[i].is_pending);
	#endif // !NDEBUG

	if (!init_structure_hash(core, structure))
		ASSERT_UNREACHABLE;

	(void) holotype_id_from_interned_type_structure(core, structure, false);
}



TypeId type_create_user_composite(CoreData* core, TypeTag tag, SourceId definition_site) noexcept
{
	ASSERT_OR_IGNORE(tag == TypeTag::Composite || tag == TypeTag::CompositeLiteral);

	ASSERT_OR_IGNORE(definition_site != SourceId::INVALID);

	TypeStructure* const structure = composite_alloc(core, tag, CompositeKind::User, CompositeFlags::User_IsOpen, 4, false);

	CompositeType* const composite = reinterpret_cast<CompositeType*>(structure + 1);

	CompositeUserExtraData* const user = reinterpret_cast<CompositeUserExtraData*>(composite + 1);
	user->size = 0;
	user->stride = 0;
	user->align = 0;
	user->definition_site = definition_site;

	const TypeId user_type_id = id_from_structure(core, structure);

	IndirectionType indirection_attach{};
	indirection_attach.indirection_type_id = some(user_type_id);

	TypeStructure* const indirection_structure = make_structure_nohash(core, TypeTag::INDIRECTION, range::from_object_bytes(&indirection_attach), sizeof(IndirectionType));

	return id_from_structure(core, indirection_structure);
}

bool type_add_user_composite_member(CoreData* core, TypeId type_id, UserCompositeMemberInit init) noexcept
{
	TypeStructure* indirection_structure;

	TypeStructure* structure = structure_from_id_follow_with_last_indirection(core, type_id, &indirection_structure);

	ASSERT_OR_IGNORE(structure->tag == TypeTag::Composite || structure->tag == TypeTag::CompositeLiteral);

	ASSERT_OR_IGNORE(init.name != IdentifierId::INVALID || structure->tag == TypeTag::CompositeLiteral);

	CompositeType* composite = reinterpret_cast<CompositeType*>(structure + 1);

	ASSERT_OR_IGNORE(composite->kind == CompositeKind::User);

	if (composite->member_used == composite->member_capacity)
	{
		structure = composite_realloc(core, indirection_structure, structure);

		composite = reinterpret_cast<CompositeType*>(structure + 1);
	}

	ASSERT_OR_IGNORE(composite->member_used < composite->member_capacity);

	CompositeInfo info = composite_info(composite);

	u16 unused_rank;

	if (init.name != IdentifierId::INVALID && find_member_by_name(info, init.name, &unused_rank))
		return false;

	info.member_names[info.member_count] = init.name;

	info.member_types[info.member_count].type_id = init.type_id;
	info.member_types[info.member_count].is_pending = false;
	info.member_types[info.member_count].is_pub = init.is_pub;
	info.member_types[info.member_count].is_mut = init.is_mut;
	info.member_types[info.member_count].is_eval = false;
	info.member_types[info.member_count].is_initializing = false;
	info.member_types[info.member_count].is_impl_member_from_trait_default = false;
	info.member_types[info.member_count].value_or_default = init.default_value;
	info.member_types[info.member_count].shadow_id = none<ShadowLayoutId>();
	info.member_types[info.member_count].shadow_rank = 0;

	get(info.member_offsets)[info.member_count] = init.offset;

	composite->member_used += 1;

	return true;
}

TypeId type_seal_user_composite(CoreData* core, TypeId type_id, UserCompositeSealInfo seal_info) noexcept
{
	ASSERT_OR_IGNORE(seal_info.align != 0 && is_pow2(seal_info.align));

	TypeStructure* indirection_structure;

	TypeStructure* structure = structure_from_id_follow_with_last_indirection(core, type_id, &indirection_structure);

	ASSERT_OR_IGNORE(structure->tag == TypeTag::Composite || structure->tag == TypeTag::CompositeLiteral);

	CompositeType* composite = reinterpret_cast<CompositeType*>(structure + 1);

	ASSERT_OR_IGNORE(composite->kind == CompositeKind::User);

	ASSERT_OR_IGNORE((composite->flags & CompositeFlags::User_IsOpen) != CompositeFlags::EMPTY);

	composite->flags &= ~CompositeFlags::User_IsOpen;

	CompositeUserExtraData* const user = reinterpret_cast<CompositeUserExtraData*>(composite + 1);
	user->size = seal_info.size;
	user->stride = seal_info.stride;
	user->align = seal_info.align;

	if (!init_structure_hash(core, structure))
		return type_id;

	return holotype_id_from_interned_type_structure(core, structure, false);
}



TypeId type_create_file_composite(CoreData* core, u16 member_count, SourceId definition_site) noexcept
{
	ASSERT_OR_IGNORE(definition_site != SourceId::INVALID);

	TypeStructure* const structure = composite_alloc(core, TypeTag::Composite, CompositeKind::File, CompositeFlags::EMPTY, member_count, false);

	CompositeType* const composite = reinterpret_cast<CompositeType*>(structure + 1);

	CompositeFileExtraData* const file = reinterpret_cast<CompositeFileExtraData*>(composite + 1);
	file->definition_site = definition_site;
	file->unused_ = 0;

	return id_from_structure(core, structure);
}

void type_add_file_composite_member(CoreData* core, TypeId type_id, FileCompositeMemberInit init) noexcept
{
	TypeStructure* const structure = structure_from_id_direct(core, type_id);

	ASSERT_OR_IGNORE(structure->tag == TypeTag::Composite);

	CompositeType* const composite = reinterpret_cast<CompositeType*>(structure + 1);

	ASSERT_OR_IGNORE(composite->kind == CompositeKind::File);

	ASSERT_OR_IGNORE(composite->member_used < composite->member_capacity);

	CompositeInfo info = composite_info(composite);

	info.member_names[info.member_count] = init.name;

	info.member_types[info.member_count].type_id = TypeId::INVALID;
	info.member_types[info.member_count].is_pending = true;
	info.member_types[info.member_count].is_initializing = false;
	info.member_types[info.member_count].is_pub = init.is_pub;
	info.member_types[info.member_count].is_mut = init.is_mut;
	info.member_types[info.member_count].is_eval = false;
	info.member_types[info.member_count].completion_id = init.completion_id;

	composite->member_used += 1;
}



bool type_member_begin_initialization_by_rank(CoreData* core, TypeId type_id, u16 rank) noexcept
{
	TypeStructure* const structure = structure_from_id_direct(core, type_id);

	ASSERT_OR_IGNORE(structure->tag == TypeTag::Composite);

	CompositeType* const composite = reinterpret_cast<CompositeType*>(structure + 1);

	ASSERT_OR_IGNORE(composite->kind == CompositeKind::File || composite->kind == CompositeKind::Impl);

	ASSERT_OR_IGNORE(rank < composite->member_used);

	CompositeInfo info = composite_info(composite);

	ASSERT_OR_IGNORE(info.member_types[rank].is_pending);

	if (info.member_types[rank].is_initializing)
		return false;

	info.member_types[rank].is_initializing = true;

	return true;
}

bool type_member_begin_initialization_by_name(CoreData* core, TypeId type_id, IdentifierId name, u16* out_rank) noexcept
{
	TypeStructure* const structure = structure_from_id_direct(core, type_id);

	ASSERT_OR_IGNORE(structure->tag == TypeTag::Composite);

	CompositeType* const composite = reinterpret_cast<CompositeType*>(structure + 1);

	ASSERT_OR_IGNORE(composite->kind == CompositeKind::File || composite->kind == CompositeKind::Impl);

	CompositeInfo info = composite_info(composite);

	u16 rank;

	if (!find_member_by_name(info, name, &rank))
		ASSERT_UNREACHABLE;

	ASSERT_OR_IGNORE(info.member_types[rank].is_pending);

	if (info.member_types[rank].is_initializing)
		return false;

	info.member_types[rank].is_initializing = true;

	*out_rank = rank;

	return true;
}

void type_member_complete(CoreData* core, TypeId type_id, u16 rank, TypeId member_type_id, CoreId value_id, bool end_initialization) noexcept
{
	TypeStructure* const structure = structure_from_id_direct(core, type_id);

	ASSERT_OR_IGNORE(structure->tag == TypeTag::Composite);

	CompositeType* const composite = reinterpret_cast<CompositeType*>(structure + 1);

	ASSERT_OR_IGNORE(composite->kind == CompositeKind::File || composite->kind == CompositeKind::Impl);

	ASSERT_OR_IGNORE(rank < composite->member_used);

	CompositeInfo info = composite_info(composite);

	ASSERT_OR_IGNORE(info.member_types[rank].is_pending);

	ASSERT_OR_IGNORE(info.member_types[rank].is_initializing);

	info.member_types[rank].type_id = member_type_id;
	info.member_types[rank].is_pending = !end_initialization;
	info.member_types[rank].is_initializing = !end_initialization;
	info.member_types[rank].value_or_default = some(value_id);
}

void type_member_end_initialization(CoreData* core, TypeId type_id, u16 rank) noexcept
{
	TypeStructure* const structure = structure_from_id_direct(core, type_id);

	ASSERT_OR_IGNORE(structure->tag == TypeTag::Composite);

	CompositeType* const composite = reinterpret_cast<CompositeType*>(structure + 1);

	ASSERT_OR_IGNORE(composite->kind == CompositeKind::File || composite->kind == CompositeKind::Impl);

	ASSERT_OR_IGNORE(rank < composite->member_used);

	CompositeInfo info = composite_info(composite);

	// Note that `is_initializing` and `is_pending` may already be `false` from
	// a previous call to `type_member_relax_initialization`.

	info.member_types[rank].is_pending = false;
	info.member_types[rank].is_initializing = false;
}



TypeRelation type_relation(CoreData* core, TypeId first_type_id, TypeId second_type_id) noexcept
{
	ASSERT_OR_IGNORE(first_type_id != TypeId::INVALID && second_type_id != TypeId::INVALID);

	const TypeEquality equality = type_is_equal(core, first_type_id, second_type_id);

	if (equality != TypeEquality::Unequal)
		return static_cast<TypeRelation>(equality);



	const TypeRelation first_to_second_relation = type_can_implicitly_convert_from_to_assume_unequal(core, first_type_id, second_type_id);

	ASSERT_OR_IGNORE(first_to_second_relation != TypeRelation::Equal && first_to_second_relation != TypeRelation::SecondConvertsToFirst);

	if (first_to_second_relation != TypeRelation::Unrelated)
		return first_to_second_relation;



	const TypeRelation second_to_first_relation = type_can_implicitly_convert_from_to_assume_unequal(core, second_type_id, first_type_id);

	ASSERT_OR_IGNORE(second_to_first_relation != TypeRelation::Equal && second_to_first_relation != TypeRelation::SecondConvertsToFirst);

	return second_to_first_relation == TypeRelation::FirstConvertsToSecond
		? TypeRelation::SecondConvertsToFirst
		: second_to_first_relation;
}

TypeEquality type_is_equal(CoreData* core, TypeId type_id_a, TypeId type_id_b) noexcept
{
	ASSERT_OR_IGNORE(type_id_a != TypeId::INVALID && type_id_b != TypeId::INVALID);

	// Equal `TypeId`s imply equal types. This is checked here redundantly even
	// though it is also checked in `type_is_equal_noloop` to avoid having to
	// create a `SeenSet` in the simplest case.
	if (type_id_a == type_id_b)
		return TypeEquality::Equal;

	const Maybe<TypeStructure*> opt_a = structure_from_id_follow(core, type_id_a);

	const Maybe<TypeStructure*> opt_b = structure_from_id_follow(core, type_id_b);

	if (is_none(opt_a) || is_none(opt_b))
		return TypeEquality::Incomplete;

	TypeStructure* const a = get(opt_a);

	TypeStructure* const b = get(opt_b);

	// ... Same goes for equal holotypes.
	if (a->holotype_id != TypeId::INVALID && b->holotype_id != TypeId::INVALID)
		return a->holotype_id == b->holotype_id ? TypeEquality::Equal : TypeEquality::Unequal;

	const u64 mark = temp_stack_mark(core);

	SeenSet a_seen = seen_set_create(core);

	SeenSet b_seen = seen_set_create(core);

	const TypeEquality result = type_is_equal_noloop(core, type_id_a, type_id_b, &a_seen, &b_seen);

	temp_stack_release(core, mark);

	return result;
}



bool type_member_count(CoreData* core, TypeId type_id, u32* out) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const Maybe<TypeStructure*> opt_structure = structure_from_id_follow(core, type_id);

	if (is_none(opt_structure))
		return false;

	TypeStructure* const structure = get(opt_structure);

	ASSERT_OR_IGNORE(structure->tag == TypeTag::Composite || structure->tag == TypeTag::CompositeLiteral);

	CompositeType* const composite = reinterpret_cast<CompositeType*>(structure->attach);

	if (composite->kind == CompositeKind::User && (composite->flags & CompositeFlags::User_IsOpen) != CompositeFlags::EMPTY)
		return false;

	*out = composite->member_used;

	return true;
}

bool type_composite_is_impl_body(CoreData* core, TypeId type_id) noexcept
{
	const Maybe<TypeStructure*> opt_structure = structure_from_id_follow(core, type_id);

	ASSERT_OR_IGNORE(is_some(opt_structure));

	TypeStructure* const structure = get(opt_structure);

	ASSERT_OR_IGNORE(structure->tag == TypeTag::Composite);

	CompositeType* const composite = reinterpret_cast<CompositeType*>(structure + 1);

	return composite->kind == CompositeKind::Impl;
}

bool type_implements_trait(CoreData* core, TypeId type_id, OpcodeId trait_body_opcode_id, Range<TypeId> argument_types) noexcept
{
	const Maybe<TypeStructure*> opt_structure = structure_from_id_follow(core, type_id);

	if (is_none(opt_structure))
		return false;

	const TypeStructure* const structure = get(opt_structure);

	if (structure->tag != TypeTag::Self)
		return false;

	const SelfType* const attach = reinterpret_cast<const SelfType*>(structure + 1);

	ASSERT_OR_IGNORE(attach->argument_count == argument_types.count());

	if (!attach->has_arguments)
		return false;

	if (attach->trait_body_opcode_id != trait_body_opcode_id)
		return false;

	for (u8 i = 0; i != attach->argument_count; ++i)
	{
		if (type_is_equal(core, argument_types[i], attach->argument_type_ids[i]) != TypeEquality::Equal)
			return false;
	}

	return true;
}

bool type_get_holotype(CoreData* core, TypeId type_id, TypeId* out_holotype_id) noexcept
{
	const Maybe<TypeStructure*> opt_structure = structure_from_id_follow(core, type_id);

	if (is_none(opt_structure))
		return false;

	TypeStructure* const structure = get(opt_structure);

	if (structure->holotype_id == TypeId::INVALID)
		(void) holotype_id_from_interned_type_structure(core, structure, false);

	*out_holotype_id = structure->holotype_id;

	return structure->holotype_id != TypeId::INVALID;
}

SignatureTypeInfo type_signature_info_from_id(CoreData* core, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeStructure* const structure = structure_from_id_direct(core, type_id);

	ASSERT_OR_IGNORE(structure->tag == TypeTag::Signature || structure->tag == TypeTag::Trait);

	const CompositeType* const composite = reinterpret_cast<const CompositeType*>(structure + 1);

	ASSERT_OR_IGNORE(composite->kind == CompositeKind::Signature);

	const CompositeSignatureExtraData* const signature = reinterpret_cast<const CompositeSignatureExtraData*>(composite + 1);

	SignatureTypeInfo info{};
	info.templated_parameter_count = signature->templated_parameter_count;
	info.parameter_count = static_cast<u8>(composite->member_used);
	info.is_func = signature->is_func;
	info.has_templated_return_type = signature->has_templated_return_type;
	info.is_variadic = signature->is_variadic;
	info.closure_id = signature->closure_id;

	if (signature->has_templated_return_type)
		info.return_type.templated.completion_id = signature->return_type.completion_id;
	else
		info.return_type.complete.type_id = signature->return_type.type_id;

	return info;
}

bool type_has_metrics(CoreData* core, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const Maybe<TypeStructure*> opt_structure = structure_from_id_follow(core, type_id);

	if (is_none(opt_structure) || get(opt_structure)->tag != TypeTag::Composite)
		return true;

	const CompositeType* const composite = reinterpret_cast<const CompositeType*>(get(opt_structure) + 1);

	return composite->kind != CompositeKind::User || (composite->flags & CompositeFlags::User_IsOpen) == CompositeFlags::EMPTY;
}

bool type_metrics_from_id(CoreData* core, TypeId type_id, TypeMetrics* out) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const Maybe<TypeStructure*> opt_structure = structure_from_id_follow(core, type_id);

	if (is_none(opt_structure))
		return false;

	const TypeStructure* const structure = get(opt_structure);

	switch (structure->tag)
	{
	case TypeTag::Void:
	case TypeTag::Divergent:
	case TypeTag::Undefined:
	{
		*out = TypeMetrics{ 0, 0, 1, false };

		return true;
	}

	case TypeTag::Type:
	case TypeTag::TypeInfo:
	case TypeTag::TypeBuilder:
	{
		*out = TypeMetrics{ 4, 4, 4, true };

		return true;
	}

	case TypeTag::Definition:
	{
		*out = TypeMetrics{ sizeof(DefinitionValue), sizeof(DefinitionValue), alignof(DefinitionValue), true };

		return true;
	}

	case TypeTag::CompInteger:
	{
		*out = TypeMetrics{ sizeof(CompIntegerValue), sizeof(CompIntegerValue), alignof(CompIntegerValue), true };

		return true;
	}

	case TypeTag::CompFloat:
	{
		*out = TypeMetrics{ sizeof(CompFloatValue), sizeof(CompFloatValue), alignof(CompFloatValue), true };

		return true;
	}

	case TypeTag::Boolean:
	{
		*out = TypeMetrics{ 1, 1, 1, false };

		return true;
	}

	case TypeTag::Integer:
	case TypeTag::Float:
	{
		const NumericType* const numeric = reinterpret_cast<const NumericType*>(structure->attach);

		const u32 bytes = (numeric->bits + 7) / 8;

		const u32 pow2_bytes = next_pow2(bytes);

		*out = TypeMetrics{ pow2_bytes, pow2_bytes, pow2_bytes, false };

		return true;
	}

	case TypeTag::Slice:
	{
		*out = TypeMetrics{ 16, 16, 8, false };

		return true;
	}

	case TypeTag::Signature:
	{
		*out = TypeMetrics{ sizeof(CallableValue), sizeof(CallableValue), alignof(CallableValue), true };

		return true;
	}

	case TypeTag::Ptr:
	{
		*out = TypeMetrics{ 8, 8, 8, false };

		return true;
	}

	case TypeTag::Array:
	case TypeTag::ArrayLiteral:
	{
		const ArrayType* const array = reinterpret_cast<const ArrayType*>(structure->attach);

		if (is_some(array->element_type))
		{
			TypeMetrics element_metrics;
			
			if (!type_metrics_from_id(core, get(array->element_type), &element_metrics))
				return false;

			*out = TypeMetrics{ element_metrics.stride * (array->element_count - 1) + element_metrics.size, (element_metrics.stride * array->element_count), element_metrics.align, element_metrics.is_shadow };
		}
		else
		{
			*out = TypeMetrics{ 0, 0, 1, false };
		}

		return true;
	}

	case TypeTag::Composite:
	case TypeTag::CompositeLiteral:
	{
		const CompositeType* const composite = reinterpret_cast<const CompositeType*>(structure->attach);

		const bool is_shadow = (composite->flags & CompositeFlags::Any_IsShadow) == CompositeFlags::Any_IsShadow;

		if (composite->kind == CompositeKind::User)
		{
			ASSERT_OR_IGNORE((composite->flags & CompositeFlags::User_IsOpen) == CompositeFlags::EMPTY);

			const CompositeUserExtraData* const user = reinterpret_cast<const CompositeUserExtraData*>(composite + 1);

			*out = TypeMetrics{ user->size, user->stride, user->align, is_shadow };
		}
		else
		{
			*out = TypeMetrics{ 0, 0, 1, is_shadow };
		}

		return true;
	}

	case TypeTag::Trait:
	{
		*out = TypeMetrics{ sizeof(TraitValue), sizeof(TraitValue), alignof(TraitValue), true };

		return true;
	}

	case TypeTag::Self:
	{
		const SelfType* const attach = reinterpret_cast<const SelfType*>(structure + 1);

		return type_metrics_from_id(core, attach->base_type_id, out);
	}

	case TypeTag::Variadic:
	case TypeTag::TailArray:
		TODO("Implement `type_metrics_from_id(%)`.", tag_name(structure->tag));

	case TypeTag::INVALID:
	case TypeTag::INDIRECTION:
		break; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

TypeTag type_tag_from_id(CoreData* core, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const Maybe<TypeStructure*> structure = structure_from_id_follow(core, type_id);

	if (is_none(structure))
		return TypeTag::Type;

	return get(structure)->tag;
}

bool type_member_info_by_rank(CoreData* core, TypeId type_id, u16 rank, MemberInfo* out_info, OpcodeId* out_completion_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeStructure* const structure = get(structure_from_id_follow(core, type_id));

	ASSERT_OR_IGNORE(
		structure->tag == TypeTag::Composite
	 || structure->tag == TypeTag::CompositeLiteral
	 || structure->tag == TypeTag::Signature
	 || structure->tag == TypeTag::Trait
	);

	const CompositeType* const composite = reinterpret_cast<const CompositeType*>(structure + 1);

	ASSERT_OR_IGNORE(rank < composite->member_used);

	CompositeInfo info = composite_info(composite);

	return fill_member_info(info, rank, out_info, out_completion_id);
}

MemberByNameRst type_member_info_by_name(CoreData* core, TypeId type_id, IdentifierId name, MemberInfo* out_info, OpcodeId* out_completion_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeStructure* const structure = get(structure_from_id_follow(core, type_id));

	ASSERT_OR_IGNORE(
		structure->tag == TypeTag::Composite
	 || structure->tag == TypeTag::CompositeLiteral
	 || structure->tag == TypeTag::Signature
	 || structure->tag == TypeTag::Trait
	);

	const CompositeType* const composite = reinterpret_cast<const CompositeType*>(structure->attach);

	const CompositeInfo info = composite_info(composite);

	u16 rank;

	if (!find_member_by_name(info, name, &rank))
		return MemberByNameRst::NotFound;

	return fill_member_info(info, rank, out_info, out_completion_id)
		? MemberByNameRst::Ok
		: MemberByNameRst::Incomplete;
}

IdentifierId type_member_name_by_rank(CoreData* core, TypeId type_id, u16 rank) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeStructure* const structure = get(structure_from_id_follow(core, type_id));

	ASSERT_OR_IGNORE(structure->tag == TypeTag::Composite || structure->tag == TypeTag::CompositeLiteral);

	const CompositeType* const composite = reinterpret_cast<const CompositeType*>(structure->attach);

	ASSERT_OR_IGNORE(rank < composite->member_used);

	const CompositeInfo info = composite_info(composite);

	return info.member_names[rank];
}

const void* type_attachment_from_id_raw(CoreData* core, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeStructure* const structure = get(structure_from_id_follow(core, type_id));

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
		"Undefined",
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



MemberIterator members_of(CoreData* core, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const Maybe<TypeStructure*> opt_structure = structure_from_id_follow(core, type_id);

	if (is_none(opt_structure))
	{
		MemberIterator it;
		it.core = core;
		it.names = nullptr;
		it.types = nullptr;
		it.offsets = none<s64*>();
		it.count = 0;
		it.rank = 0;
		it.kind_bits = static_cast<u8>(CompositeKind::INVALID);

		return it;
	}

	const TypeStructure* const structure = get(opt_structure);

	ASSERT_OR_IGNORE(structure->tag == TypeTag::Composite || structure->tag == TypeTag::CompositeLiteral);

	const CompositeType* const composite = reinterpret_cast<const CompositeType*>(structure + 1);

	ASSERT_OR_IGNORE((composite->flags & CompositeFlags::User_IsOpen) == CompositeFlags::EMPTY);

	const CompositeInfo info = composite_info(composite);

	MemberIterator it;
	it.core = core;
	it.names = info.member_names;
	it.types = info.member_types;
	it.offsets = info.member_offsets;
	it.count = info.member_count;
	it.rank = 0;
	it.kind_bits = static_cast<u8>(composite->kind);

	return it;
}

bool next(MemberIterator* it, MemberInfo* out_info, OpcodeId* out_completion_id) noexcept
{
	ASSERT_OR_IGNORE(has_next(it));

	const u16 rank = it->rank;

	it->rank += 1;

	CompositeInfo info;
	info.kind = static_cast<CompositeKind>(it->kind_bits);
	info.member_count = it->count;
	info.member_names = const_cast<IdentifierId*>(it->names);
	info.member_types = const_cast<CompositeMember*>(static_cast<const CompositeMember*>(it->types));
	info.member_offsets = is_some(it->offsets) ? some(const_cast<s64*>(get(it->offsets))) : none<s64*>();

	return fill_member_info(info, rank, out_info, out_completion_id);
}

bool has_next(const MemberIterator* it) noexcept
{
	ASSERT_OR_IGNORE(it->rank <= it->count);

	return it->rank != it->count;
}



bool HolotypeIterator::has_next() const noexcept
{
	ASSERT_OR_IGNORE(curr <= end);

	return curr != end;
}

Holotype* HolotypeIterator::next() noexcept
{
	ASSERT_OR_IGNORE(has_next());

	Holotype* const entry = core->types.holotype_entries.begin() + curr;

	curr += 1;

	return entry;
}



Holotype* HolotypeAlloc::value_from_id(u32 id) noexcept
{
	ASSERT_OR_IGNORE(id < core->types.holotype_entries.used());

	return core->types.holotype_entries.begin() + id;
}

const Holotype* HolotypeAlloc::value_from_id(u32 id) const noexcept
{
	ASSERT_OR_IGNORE(id < core->types.holotype_entries.used());

	return core->types.holotype_entries.begin() + id;
}

u32 HolotypeAlloc::id_from_value(const Holotype* value) const noexcept
{
	ASSERT_OR_IGNORE(value >= core->types.holotype_entries.begin());

	ASSERT_OR_IGNORE(value < core->types.holotype_entries.end());

	return static_cast<u32>(value - core->types.holotype_entries.begin());
}

HolotypeIterator HolotypeAlloc::values() noexcept
{
	HolotypeIterator it;
	it.core = core;
	it.curr = 0;
	it.end = core->types.holotype_entries.used();

	return it;
}

Holotype* HolotypeAlloc::alloc(HolotypeInit key, u32 key_hash) noexcept
{
	Holotype* const holotype = core->types.holotype_entries.reserve();
	holotype->m_hash = key_hash;
	holotype->m_holotype_id = id_from_structure(core, key.structure);

	return holotype;
}

void HolotypeAlloc::dealloc([[maybe_unused]] u32 id) noexcept
{
	ASSERT_UNREACHABLE;
}
