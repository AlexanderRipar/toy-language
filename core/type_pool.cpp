#include "core.hpp"
#include "structure.hpp"

#include "../infra/types.hpp"
#include "../infra/assert.hpp"
#include "../infra/panic.hpp"
#include "../infra/math.hpp"
#include "../infra/range.hpp"
#include "../infra/hash.hpp"
#include "../infra/container/index_map.hpp"
#include "../infra/container/reserved_heap.hpp"
#include "../infra/container/reserved_vec.hpp"

#include <cstring>

struct TypeStructure;

static TypeStructure* structure_from_id(CoreData* core, TypeId id) noexcept;

static TypeId id_from_structure(const CoreData* core, const TypeStructure* structure) noexcept;

static TypeStructure* make_structure_nohash(CoreData* core, TypeTag tag, Range<byte> attach, u64 reserve_size, SourceId distinct_source_id) noexcept;

static TypeStructure* make_structure_hash(CoreData* core, TypeTag tag, Range<byte> attach, u64 reserve_size, SourceId distinct_source_id) noexcept;



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

struct TypeIdAndFlags
{
	u32 type_id_bits : 28;

	u32 is_pending : 1;

	u32 is_pub : 1;

	u32 is_mut : 1;

	u32 is_eval : 1;
};

// Used for Composite Literal types.
struct alignas(8) InitializerMemberData
{
	TypeIdAndFlags type_and_flags;

	u32 offset;
};

struct alignas(8) FileMemberData
{
	TypeIdAndFlags type_and_flags;

	union
	{
		ForeverValueId value_id;

		OpcodeId completion_id;
	};
};

struct alignas(8) ParameterListOrUserMemberData
{
	TypeIdAndFlags type_and_flags;

	union
	{
		Maybe<ForeverValueId> default_id;

		OpcodeId parameter_completion_id;
	};

	s64 offset;
};

struct CompositeMemberInfo
{
	TypeDisposition disposition;

	u8 member_stride;

	u32 count;

	IdentifierId* names;

	void* members;
};

struct CompositeType
{
	u64 size;

	u64 stride;

	u8 align_log2;

	bool is_open;

	bool is_fixed;

	TypeDisposition disposition;

	u16 member_used;

	u16 member_capacity;

	// IdentifierId[header->member_count];

	// Depending on header->disposition:
	// ::Initializer           -> InitializerMemberData[header->member_count];
	// ::File                  -> FileMemberData[header->member_count];
	// ::ParameterList, ::User -> ParameterListOrUserMemberData[header->member_count];
};

struct IndirectionType
{
	TypeId indirection_type_id;
};

struct alignas(8) TypeStructure
{
	TypeTag tag;

	TypeId holotype_id;

	SourceId distinct_source_id;

	u32 hash;

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

	static constexpr u32 stride() noexcept
	{
		return sizeof(Holotype);
	}

	static u32 required_strides([[maybe_unused]] HolotypeInit key) noexcept
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

	bool equal_to_key(HolotypeInit key, u32 key_hash) const noexcept
	{
		ASSERT_OR_IGNORE(key_hash != 0);

		if (m_hash != key_hash)
			return false;

		ASSERT_OR_IGNORE(structure_from_id(key.core, m_holotype_id)->tag != TypeTag::INDIRECTION);

		const TypeId key_type_id = id_from_structure(key.core, key.structure);

		return type_is_equal(key.core, m_holotype_id, key_type_id);
	}

	void init(HolotypeInit key, u32 key_hash) noexcept
	{
		ASSERT_OR_IGNORE(key.structure->hash == key_hash);

		ASSERT_OR_IGNORE(key_hash != 0);

		m_holotype_id = TypeId::INVALID;
		m_hash = key.structure->hash;
	}
};

struct CompositeTypeAllocInfo
{
	u16 alloc_size;

	u16 member_capacity;
};



static constexpr u32 STRUCTURES_CAPACITIES[MAX_STRUCTURE_SIZE_LOG2 - MIN_STRUCTURE_SIZE_LOG2 + 1] = {
	131072, 65536, 65536, 32768, 16384,
		4096,  1024,   256,    64,
};

static constexpr u32 STRUCTURES_COMMITS[MAX_STRUCTURE_SIZE_LOG2 - MIN_STRUCTURE_SIZE_LOG2 + 1] = {
	1024, 512, 256, 128, 64,
		16,   4,   1,   1,
};

static constexpr u64 STRUCTURES_RESERVE = decltype(TypePool::structures)::memory_size(Range{ STRUCTURES_CAPACITIES });

static constexpr u64 HOLOTYPES_LOOKUPS_RESERVE = decltype(TypePool::holotypes)::lookups_memory_size(1 << 21);

static constexpr u32 HOLOTYPES_LOOKUPS_INITIAL_COMMIT_COUNT = static_cast<u32>(1) << 10;

static constexpr u32 HOLOTYPES_VALUES_RESERVE = (static_cast<u32>(1) << 20) * Holotype::stride();

static constexpr u32 HOLOTYPES_VALUES_COMMIT_INCREMENT_COUNT = static_cast<u32>(1) << 12;

static constexpr u64 SCRATCH_RESERVE = static_cast<u32>(1) << 16;

static constexpr u64 SCRATCH_COMMIT_INCREMENT_COUNT = static_cast<u32>(1) << 12;



static constexpr u16 min_composite_structure_alloc_size_log2(u32 min_alloc_size_log2) noexcept
{
	const u32 header_size = sizeof(TypeStructure) + sizeof(CompositeType);

	while ((static_cast<u32>(1) << min_alloc_size_log2) < header_size)
		min_alloc_size_log2 += 1;
		
	return static_cast<u16>(min_alloc_size_log2);
}

static constexpr u16 MIN_COMPOSITE_STRUCTURE_ALLOC_SIZE_LOG2 = min_composite_structure_alloc_size_log2(MIN_STRUCTURE_SIZE_LOG2);

static constexpr u16 MIN_MEMBER_STRIDE = sizeof(ParameterListOrUserMemberData) < sizeof(FileMemberData)
	? sizeof(ParameterListOrUserMemberData) < sizeof(InitializerMemberData) ? sizeof(ParameterListOrUserMemberData) : sizeof(InitializerMemberData)
	: sizeof(FileMemberData) < sizeof(InitializerMemberData) ? sizeof(FileMemberData) : sizeof(InitializerMemberData);

static constexpr u16 MAX_MEMBER_STRIDE = sizeof(ParameterListOrUserMemberData) > sizeof(FileMemberData)
	? sizeof(ParameterListOrUserMemberData) > sizeof(InitializerMemberData) ? sizeof(ParameterListOrUserMemberData) : sizeof(InitializerMemberData)
	: sizeof(FileMemberData) > sizeof(InitializerMemberData) ? sizeof(FileMemberData) : sizeof(InitializerMemberData);


struct MemberCapacities
{
	u16 capacities[1 + (MAX_MEMBER_STRIDE - MIN_MEMBER_STRIDE) / 8][1 + MAX_STRUCTURE_SIZE_LOG2 - MIN_COMPOSITE_STRUCTURE_ALLOC_SIZE_LOG2];
};

static constexpr u16 member_capacity(u32 alloc_size, u32 member_stride) noexcept
{
	const u32 header_size = sizeof(TypeStructure) + sizeof(CompositeType);

	const u32 remaining_size = alloc_size - header_size;

	const u16 approx_capacity = static_cast<u16>(remaining_size / (member_stride + sizeof(IdentifierId)));

	// If the number of members is even, we're good to go. Otherwise we need to
	// insert an extra `IdentifierId` of padding, which might reduce our actual
	// capacity by one.
	if ((approx_capacity & 1) == 0)
		return approx_capacity;

	// Add an extra `IdentifierId` of padding to the size.
	const u32 used_size = header_size + approx_capacity * (member_stride + sizeof(IdentifierId)) + sizeof(IdentifierId);

	// All members fit into the allocation size, even with the extra padding.
	if (used_size < alloc_size)
		return approx_capacity;

	// Since not all members fit, we reduce the number of members by 1, which
	// will always fit.
	return approx_capacity - 1;
}

static constexpr MemberCapacities member_capacities(u32 min_alloc_size_log2) noexcept
{
	MemberCapacities capacities{};

	for (u16 i = 0; i != array_count(capacities.capacities); ++i)
	{
		for (u16 j = 0; j != array_count(capacities.capacities[0]); ++j)
			capacities.capacities[i][j] = member_capacity(1 << (min_alloc_size_log2 + j), MIN_MEMBER_STRIDE + 8 * i);
	}

	return capacities;
}



static u8 members_stride_for(TypeDisposition disposition) noexcept
{
	static constexpr u8 MEMBER_STRIDES[] = {
		sizeof(InitializerMemberData),
		sizeof(FileMemberData),
		sizeof(ParameterListOrUserMemberData),
		sizeof(ParameterListOrUserMemberData),
	};

	ASSERT_OR_IGNORE(disposition == TypeDisposition::Initializer || disposition == TypeDisposition::File || disposition == TypeDisposition::ParameterList || disposition == TypeDisposition::User);

	return MEMBER_STRIDES[static_cast<u8>(disposition) - 1];
}

static CompositeTypeAllocInfo composite_type_alloc_info(TypeDisposition disposition, u64 member_count) noexcept
{
	static constexpr MemberCapacities CAPACITIES = member_capacities(MIN_COMPOSITE_STRUCTURE_ALLOC_SIZE_LOG2);

	const u8 member_stride = members_stride_for(disposition);

	ASSERT_OR_IGNORE(member_stride == 8 || member_stride == 16);

	const u8 stride_index = (member_stride >> 3) - 1;

	for (u32 i = 0; i != array_count(CAPACITIES.capacities[0]); ++i)
	{
		const u16 member_capacity = CAPACITIES.capacities[stride_index][i];

		if (member_capacity >= member_count)
		{
			const u16 alloc_size = static_cast<u16>(1) << (i + MIN_COMPOSITE_STRUCTURE_ALLOC_SIZE_LOG2);

			CompositeTypeAllocInfo rst;
			rst.alloc_size = static_cast<u16>(alloc_size - sizeof(TypeStructure));
			rst.member_capacity = static_cast<u16>(member_capacity);

			return rst;
		}
	}

	ASSERT_UNREACHABLE;
}

static CompositeMemberInfo composite_members(CompositeType* composite) noexcept
{
	CompositeMemberInfo result;
	result.disposition = composite->disposition;
	result.member_stride = members_stride_for(composite->disposition);
	result.count = composite->member_used;
	result.names = reinterpret_cast<IdentifierId*>(composite + 1);
	result.members = result.names + composite->member_capacity;

	return result;
}

static CompositeMemberInfo composite_members(const CompositeType* composite) noexcept
{
	return composite_members(const_cast<CompositeType*>(composite));
}

static void composite_realloc(CoreData* core, TypeStructure* indirect_structure, TypeStructure** inout_direct_structure, CompositeType** inout_composite, CompositeMemberInfo* inout_members) noexcept
{
	ASSERT_OR_IGNORE(!(*inout_composite)->is_fixed);

	ASSERT_OR_IGNORE(indirect_structure->tag == TypeTag::INDIRECTION);

	TypeStructure* const old_direct_structure = *inout_direct_structure;

	CompositeType* const old_composite = *inout_composite;

	const u16 old_member_capacity = old_composite->member_capacity;

	const CompositeTypeAllocInfo alloc_info = composite_type_alloc_info(old_composite->disposition, old_member_capacity + 1);

	const MutRange<byte> to_dealloc = { reinterpret_cast<byte*>(old_direct_structure), (alloc_info.alloc_size + sizeof(TypeStructure)) / 2 };

	const Range<byte> to_copy = range::from_object_bytes(old_composite);

	TypeStructure* const new_direct_structure = make_structure_nohash(core, TypeTag::Composite, to_copy, alloc_info.alloc_size, old_direct_structure->distinct_source_id);

	IndirectionType* const indirection = reinterpret_cast<IndirectionType*>(indirect_structure->attach);

	indirection->indirection_type_id = id_from_structure(core, new_direct_structure);

	CompositeType* const new_composite = reinterpret_cast<CompositeType*>(new_direct_structure->attach);

	new_composite->member_capacity = alloc_info.member_capacity;

	const CompositeMemberInfo old_members = *inout_members;

	const CompositeMemberInfo new_members = composite_members(new_composite);

	memcpy(new_members.names, old_members.names, old_member_capacity * sizeof(IdentifierId));

	memcpy(new_members.members, old_members.members, old_member_capacity * old_members.member_stride);

	*inout_direct_structure = new_direct_structure;

	*inout_composite = new_composite;

	*inout_members = new_members;

	core->types.structures.dealloc(to_dealloc);
}

static void* member_at(const CompositeMemberInfo* members, u32 index) noexcept
{
	return static_cast<byte*>(members->members) + static_cast<u64>(members->member_stride) * index;
}

static bool find_member_by_name(const CompositeMemberInfo* members, IdentifierId name, u16* out_rank, const void** out_data) noexcept
{
	for (u32 i = 0; i != members->count; ++i)
	{
		if (members->names[i] == name)
		{
			*out_rank = static_cast<u16>(i);

			*out_data = member_at(members, i);

			return true;
		}
	}

	return false;
}

static bool fill_member_info(const CompositeType* composite, u16 rank, const void* data, MemberInfo* out_info, OpcodeId* out_completion_id) noexcept
{
	switch (composite->disposition)
	{
	case TypeDisposition::Initializer:
	{
		const InitializerMemberData* const member = static_cast<const InitializerMemberData*>(data);

		ASSERT_OR_IGNORE(
			!member->type_and_flags.is_pending
		 && !member->type_and_flags.is_pub
		 &&  member->type_and_flags.is_mut
		 && !member->type_and_flags.is_eval
		);

		out_info->type_id = static_cast<TypeId>(member->type_and_flags.type_id_bits);
		out_info->value_or_default_id = none<ForeverValueId>();
		out_info->is_pub = false;
		out_info->is_mut = true;
		out_info->is_eval = false;
		out_info->is_global = false;
		out_info->rank = rank;
		out_info->offset = static_cast<s64>(static_cast<u64>(member->offset));

		return true;
	}

	case TypeDisposition::File:
	{
		const FileMemberData* const member = static_cast<const FileMemberData*>(data);

		ASSERT_OR_IGNORE(!member->type_and_flags.is_eval);

		if (member->type_and_flags.is_pending)
		{
			*out_completion_id = member->completion_id;

			out_info->rank = rank;
			out_info->is_pub = member->type_and_flags.is_pub;
			out_info->is_mut = member->type_and_flags.is_mut;
			out_info->is_eval = member->type_and_flags.is_eval;
			out_info->is_global = true;

			return false;
		}

		out_info->type_id = static_cast<TypeId>(member->type_and_flags.type_id_bits);
		out_info->value_or_default_id = some(member->value_id);
		out_info->is_pub = member->type_and_flags.is_pub;
		out_info->is_mut = member->type_and_flags.is_mut;
		out_info->is_eval = member->type_and_flags.is_eval;
		out_info->is_global = true;
		out_info->rank = rank;
		out_info->offset = 0;

		return true;
	}

	case TypeDisposition::ParameterList:
	case TypeDisposition::User:
	{
		const ParameterListOrUserMemberData* const member = static_cast<const ParameterListOrUserMemberData*>(data);

		if (member->type_and_flags.is_pending)
		{
			*out_completion_id = member->parameter_completion_id;

			out_info->rank = rank;
			out_info->is_pub = member->type_and_flags.is_pub;
			out_info->is_mut = member->type_and_flags.is_mut;
			out_info->is_eval = member->type_and_flags.is_eval;
			out_info->is_global = false;

			return false;
		}

		out_info->type_id = static_cast<TypeId>(member->type_and_flags.type_id_bits);
		out_info->value_or_default_id = member->default_id;
		out_info->is_pub = member->type_and_flags.is_pub;
		out_info->is_mut = member->type_and_flags.is_mut;
		out_info->is_eval = member->type_and_flags.is_eval;
		out_info->is_global = false;
		out_info->rank = rank;
		out_info->offset = member->offset;

		return true;
	}

	case TypeDisposition::INVALID:
		; // Fallthrough to unreachable
	}

	ASSERT_UNREACHABLE;
}



static TypeId id_from_structure(const CoreData* core, const TypeStructure* structure) noexcept
{
	const TypeId id = static_cast<TypeId>(reinterpret_cast<const u64*>(structure) - reinterpret_cast<const u64*>(core->types.structures.begin()));

	ASSERT_OR_IGNORE(id != TypeId::INVALID);

	return id;
}

static TypeStructure* structure_from_id(CoreData* core, TypeId id) noexcept
{
	ASSERT_OR_IGNORE(id != TypeId::INVALID);

	return reinterpret_cast<TypeStructure*>(reinterpret_cast<u64*>(core->types.structures.begin()) + static_cast<u32>(id));
}

static TypeStructure* follow_indirection(CoreData* core, TypeStructure* indir) noexcept
{
	if (indir->tag != TypeTag::INDIRECTION)
		return indir;

	IndirectionType* const attach = reinterpret_cast<IndirectionType*>(indir->attach);

	TypeStructure* const dir = structure_from_id(core, attach->indirection_type_id);

	ASSERT_OR_IGNORE(dir->tag == TypeTag::Composite || dir->tag == TypeTag::CompositeLiteral);

	return dir;
}

static const TypeStructure* follow_indirection(CoreData* core, const TypeStructure* indir) noexcept
{
	if (indir->tag != TypeTag::INDIRECTION)
		return indir;

	const IndirectionType* const attach = reinterpret_cast<const IndirectionType*>(indir->attach);

	TypeStructure* const dir = structure_from_id(core, attach->indirection_type_id);

	ASSERT_OR_IGNORE(dir->tag == TypeTag::Composite || dir->tag == TypeTag::CompositeLiteral);

	return dir;
}



static TypeId holotype_id_from_interned_type_structure(CoreData* core, TypeStructure* structure) noexcept
{
	ASSERT_OR_IGNORE(structure->holotype_id == TypeId::INVALID);

	HolotypeInit init;
	init.structure = structure;
	init.core = core;

	Holotype* const holotype = core->types.holotypes.value_from(init, structure->hash);

	if (holotype->m_holotype_id != TypeId::INVALID)
	{
		structure->holotype_id = holotype->m_holotype_id;

		return holotype->m_holotype_id;
	}

	const TypeId type_id = id_from_structure(core, structure);

	structure->holotype_id = type_id;

	holotype->m_holotype_id = type_id;

	return type_id;
}

static TypeId holotype_id_from_attachment(CoreData* core, TypeTag tag, Range<byte> attach) noexcept
{
	TypeStructure* const structure = make_structure_hash(core, tag, attach, attach.count(), SourceId::INVALID);

	ASSERT_OR_IGNORE(structure->holotype_id == TypeId::INVALID);

	if (structure->hash == 0)
		return id_from_structure(core, structure);

	HolotypeInit init;
	init.structure = structure;
	init.core = core;

	Holotype* const holotype = core->types.holotypes.value_from(init, structure->hash);

	if (holotype->m_holotype_id == TypeId::INVALID)
	{
		const TypeId id = id_from_structure(core, structure);

		holotype->m_holotype_id = id;

		structure->holotype_id = id;

		return id;
	}
	else
	{
		core->types.structures.dealloc(MutRange{ reinterpret_cast<byte*>(structure), sizeof(TypeStructure) + attach.count() });

		return holotype->m_holotype_id;
	}
}



static SeenSet seen_set_create_with_capacity(CoreData* core, u32 capacity) noexcept
{
	ASSERT_OR_IGNORE(capacity != 0 && is_pow2(capacity));

	MutRange<byte> memory = core->types.structures.alloc(capacity * 2 * sizeof(SeenSetEntry));
	range::mem_set(memory.mut_subrange(0, capacity * sizeof(SeenSetEntry)), 0);

	SeenSet set;
	set.memory = reinterpret_cast<SeenSetEntry*>(memory.begin());
	set.table_used = 0;
	set.stack_top = capacity - 1;
	set.capacity = capacity;

	return set;
}

static SeenSet seen_set_create(CoreData* core) noexcept
{
	return seen_set_create_with_capacity(core, 16);
}

static void seen_set_release(CoreData* core, SeenSet set) noexcept
{
	const MutRange<byte> memory = MutRange{ set.memory, set.capacity * 2 }.as_mut_byte_range();

	core->types.structures.dealloc(memory);
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

		seen_set_release(core, *set);

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



static u32 hash_type_id(CoreData* core, u32 hash, SeenSet* seen, TypeId type) noexcept;

static u32 hash_composite_type_members(CoreData* core, u32 hash, SeenSet* seen, const CompositeType* composite, u32 count, const void* data) noexcept
{
	switch (composite->disposition)
	{
	case TypeDisposition::Initializer:
	{
		const InitializerMemberData* const members = static_cast<const InitializerMemberData*>(data);

		for (u32 i = 0; i != count; ++i)
		{
			const TypeId member_type = static_cast<TypeId>(members[i].type_and_flags.type_id_bits);

			hash = hash_type_id(core, hash, seen, member_type);

			if (hash == 0)
				return 0;
		}

		return hash;
	}

	case TypeDisposition::File:
	{
		// Since the hash of the `distinct_source_id` of the enclosing
		// `TypeStructure` is already unique for file types, there is no need
		// to add more data to the mix.
		return hash;
	}

	case TypeDisposition::ParameterList:
	{
		const ParameterListOrUserMemberData* const members = static_cast<const ParameterListOrUserMemberData*>(data);

		for (u32 i = 0; i != count; ++i)
		{
			// Skip templated members.
			if (members[i].type_and_flags.is_pending)
				continue;

			const TypeId member_type = static_cast<TypeId>(members[i].type_and_flags.type_id_bits);

			hash = hash_type_id(core, hash, seen, member_type);

			if (hash == 0)
				return 0;
		}

		return hash;
	}

	case TypeDisposition::User:
	{
		const ParameterListOrUserMemberData* const members = static_cast<const ParameterListOrUserMemberData*>(data);

		for (u32 i = 0; i != count; ++i)
		{
			ASSERT_OR_IGNORE(!members[i].type_and_flags.is_pending);

			const TypeId member_type = static_cast<TypeId>(members[i].type_and_flags.type_id_bits);

			hash = hash_type_id(core, hash, seen, member_type);

			if (hash == 0)
				return 0;
		}

		return hash;
	}

	case TypeDisposition::INVALID:
		; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

static u32 hash_composite_type(CoreData* core, u32 hash, SeenSet* seen, const CompositeType* attach) noexcept
{
	if (attach->is_open)
		return 0;

	CompositeMemberInfo members = composite_members(attach);

	for (u32 i = 0; i != members.count; ++i)
		hash = fnv1a_step(hash, range::from_object_bytes(&members.names[i])) | 1;

	return hash_composite_type_members(core, hash, seen, attach, members.count, members.members);
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

static u32 hash_signature_type(CoreData* core, u32 hash, SeenSet* seen, const SignatureType2* attach) noexcept
{
	const TypeStructure* const parameter_list_structure = structure_from_id(core, attach->parameter_list_type_id);

	ASSERT_OR_IGNORE(parameter_list_structure->tag == TypeTag::Composite);

	const CompositeType* const parameter_list_type = reinterpret_cast<const CompositeType*>(parameter_list_structure->attach);

	ASSERT_OR_IGNORE(parameter_list_type->disposition == TypeDisposition::ParameterList);

	hash = hash_composite_type(core, hash, seen, parameter_list_type);

	if (hash == 0)
		return 0;

	if (!attach->has_templated_return_type)
	{
		hash = hash_type_id(core, hash, seen, attach->return_type.type_id);

		if (hash == 0)
			return 0;
	}

	const byte flags = (static_cast<byte>(attach->is_func))
	                 | (static_cast<byte>(attach->has_templated_parameter_list) << 1)
	                 | (static_cast<byte>(attach->has_templated_return_type) << 2);

	return fnv1a_step(fnv1a_step(hash, flags), static_cast<byte>(attach->parameter_count)) | 1;
}

static u32 hash_type_structure_preseen(CoreData* core, u32 hash, SeenSet* seen, const TypeStructure* structure) noexcept
{
	if (structure->hash != 0)
		return structure->hash;

	const TypeTag tag = structure->tag;

	hash = fnv1a_step(hash, static_cast<byte>(tag));

	hash = fnv1a_step(hash, range::from_object_bytes(&structure->distinct_source_id));

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

	case TypeTag::Func:
		return hash_signature_type(core, hash, seen, reinterpret_cast<const SignatureType2*>(structure->attach));

	case TypeTag::Composite:
	case TypeTag::CompositeLiteral:
		return hash_composite_type(core, hash, seen, reinterpret_cast<const CompositeType*>(structure->attach));

	case TypeTag::Variadic:
	case TypeTag::Definition:
	case TypeTag::Trait:
		TODO("Implement `type_hash` for Variadic, Definition and Trait");

	case TypeTag::INVALID:
	case TypeTag::INDIRECTION:
		; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

static u32 hash_type_structure(CoreData* core, u32 hash, SeenSet* seen, const TypeStructure* structure) noexcept
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
	const TypeStructure* const structure = follow_indirection(core, structure_from_id(core, id));

	hash = hash_type_structure(core, hash, seen, structure);

	return hash;
}



static bool type_can_implicitly_convert_from_to_assume_unequal(CoreData* core, TypeId from_type_id, TypeId to_type_id) noexcept
{
	const TypeStructure* const from = structure_from_id(core, from_type_id);

	const TypeStructure* const to = structure_from_id(core, to_type_id);

	const TypeTag to_tag = to->tag;

	const TypeTag from_tag = from->tag;

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
	case TypeTag::Array:
	case TypeTag::Func:
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

	case TypeTag::Undefined:
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

		ASSERT_OR_IGNORE(from_attach->disposition == TypeDisposition::Initializer && to_attach->disposition == TypeDisposition::User);

		const u32 required_seen_qwords = to_attach->member_used / 64 + 63;

		u64* seen_member_bits = static_cast<u64*>(core->types.scratch.reserve(required_seen_qwords));

		memset(seen_member_bits, 0, required_seen_qwords * sizeof(u64));

		u32 to_rank = 0;

		const CompositeMemberInfo from_members = composite_members(from_attach);

		const CompositeMemberInfo to_members = composite_members(to_attach);

		// Check whether all members in `from` have a corresponding non-global
		// member in `to` of a type they can be converted to.
		for (u32 i = 0; i != from_attach->member_used; ++i)
		{
			InitializerMemberData* const from_member = static_cast<InitializerMemberData*>(member_at(&from_members, i));

			const IdentifierId from_name = from_members.names[i];

			const ParameterListOrUserMemberData* to_member;

			if (from_name == IdentifierId::INVALID)
			{
				to_member = static_cast<const ParameterListOrUserMemberData*>(member_at(&to_members, to_rank));
			}
			else
			{
				u16 found_rank;

				const void* raw_to_member;

				if (!find_member_by_name(&to_members, from_name, &found_rank, &raw_to_member))
				{
					core->types.scratch.pop_by(required_seen_qwords);

					return false;
				}

				to_rank = found_rank;

				to_member = static_cast<const ParameterListOrUserMemberData*>(raw_to_member);
			}

			if (to_member->type_and_flags.is_pending)
				TODO("Implement implicit convertability check from composite literal to incomplete type");

			const TypeId from_member_type_id = static_cast<TypeId>(from_member->type_and_flags.type_id_bits);

			const TypeId to_member_type_id = static_cast<TypeId>(to_member->type_and_flags.type_id_bits);

			if (!type_can_implicitly_convert_from_to(core, from_member_type_id, to_member_type_id))
			{
				core->types.scratch.pop_by(required_seen_qwords);

				return false;
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

			const ParameterListOrUserMemberData* const to_member = static_cast<const ParameterListOrUserMemberData*>(member_at(&to_members, i));

			if (is_none(to_member->default_id))
			{
				core->types.scratch.pop_by(required_seen_qwords);

				return false;
			}
		}

		core->types.scratch.pop_by(required_seen_qwords);

		return true;
	}

	case TypeTag::ArrayLiteral:
	{
		if (to_tag != TypeTag::Array && to_tag != TypeTag::ArrayLiteral)
			return false;

		const ArrayType* const from_attach = reinterpret_cast<const ArrayType*>(from->attach);

		const ArrayType* const to_attach = reinterpret_cast<const ArrayType*>(to->attach);

		if (from_attach->element_count != to_attach->element_count)
			return false;

		// An empty array literal with no element type can be converted to
		// an empty array or array literal with any other element type.
		if (is_none(from_attach->element_type))
			return true;

		return type_can_implicitly_convert_from_to(core, get(from_attach->element_type), get(to_attach->element_type));
	}

	case TypeTag::INVALID:
	case TypeTag::INDIRECTION:
		break; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}



static bool type_is_equal_noloop(CoreData* core, TypeId type_id_a, TypeId type_id_b, SeenSet* a_seen, SeenSet* b_seen) noexcept
{
	if (type_id_a == type_id_b)
		return true;

	TypeStructure* const a = follow_indirection(core, structure_from_id(core, type_id_a));

	TypeStructure* const b = follow_indirection(core, structure_from_id(core, type_id_b));

	if (a == b)
		return true;

	if (a->hash == 0)
	{
		SeenSet hash_seen = seen_set_create(core);

		const u32 hash = hash_type_structure(core, FNV1A_SEED, &hash_seen, a);

		seen_set_release(core, hash_seen);

		if (hash != 0)
		{
			a->hash = hash;

			(void) holotype_id_from_interned_type_structure(core, a);
		}
	}

	if (b->hash == 0)
	{
		SeenSet hash_seen = seen_set_create(core);

		const u32 hash = hash_type_structure(core, FNV1A_SEED, &hash_seen, b);

		seen_set_release(core, hash_seen);

		if (hash != 0)
		{
			b->hash = hash;

			(void) holotype_id_from_interned_type_structure(core, b);
		}
	}

	// This is assumed to be the common case, so we check for it as early as
	// possible.
	if (a->holotype_id != TypeId::INVALID && b->holotype_id != TypeId::INVALID)
		return a->holotype_id == b->holotype_id;

	// From here on we perform a "deep" equality check. This is only necessary
	// if either `a` or `b` are open composite types or reference an open
	// composite type. Otherwise, they will both already be hashed into
	// `holotypes` when we are called, or the attempts to hash them inside the
	// call will have succeeded. In both cases, they will have their holotypes
	// set, meaning that the above test will succeed, leading to an early-out.

	// Types with differing tags can never be equal.
	if (a->tag != b->tag)
		return false;

	// Types from different sources can definitionally never be equal.
	if (a->distinct_source_id != b->distinct_source_id)
		return false;

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
		return false;

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
		return true;

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

		return true;
	}

	case TypeTag::Integer:
	case TypeTag::Float:
	{
		const NumericType* const a_attach = reinterpret_cast<NumericType*>(a->attach);

		const NumericType* const b_attach = reinterpret_cast<NumericType*>(b->attach);

		seen_set_pop(a_seen);

		seen_set_pop(b_seen);

		return a_attach->bits == b_attach->bits && a_attach->is_signed == b_attach->is_signed;
	}

	case TypeTag::Composite:
	{
		const CompositeType* const a_attach = reinterpret_cast<CompositeType*>(a->attach);

		const CompositeType* const b_attach = reinterpret_cast<CompositeType*>(b->attach);

		if (a_attach->disposition != b_attach->disposition
		 || a_attach->stride != b_attach->stride
		 || a_attach->align_log2 != b_attach->align_log2
		 || a_attach->size != b_attach->size
		 || a_attach->member_used != b_attach->member_used)
		{
			seen_set_pop(a_seen);

			seen_set_pop(b_seen);

			return false;
		}

		const CompositeMemberInfo a_members = composite_members(a_attach);

		const CompositeMemberInfo b_members = composite_members(b_attach);

		for (u16 rank = 0; rank != a_attach->member_used; ++rank)
		{
			void* const a_member = member_at(&a_members, rank);

			void* const b_member = member_at(&b_members, rank);

			const TypeIdAndFlags a_type_and_flags = *static_cast<const TypeIdAndFlags*>(a_member);

			const TypeIdAndFlags b_type_and_flags = *static_cast<const TypeIdAndFlags*>(b_member);

			if (a_type_and_flags.is_pending != b_type_and_flags.is_pending)
				TODO("Handle pending composite member types in `type_is_equal_noloop`");

			if (a_type_and_flags.is_pub != b_type_and_flags.is_pub
			 || a_type_and_flags.is_mut != b_type_and_flags.is_mut
			 || a_type_and_flags.is_eval != b_type_and_flags.is_eval
			) {
				seen_set_pop(a_seen);

				seen_set_pop(b_seen);

				return false;
			}

			if (a_attach->disposition == TypeDisposition::Initializer)
			{
				const InitializerMemberData* const a_typed_member = static_cast<const InitializerMemberData*>(a_member);

				const InitializerMemberData* const b_typed_member = static_cast<const InitializerMemberData*>(b_member);

				if (a_typed_member->offset != b_typed_member->offset)
				{
					seen_set_pop(a_seen);

					seen_set_pop(b_seen);

					return false;
				}
			}
			else if (a_attach->disposition == TypeDisposition::File)
			{
				FileMemberData* const a_typed_member = static_cast<FileMemberData*>(a_member);

				FileMemberData* const b_typed_member = static_cast<FileMemberData*>(b_member);

				if (a_typed_member->value_id != b_typed_member->value_id)
				{
					seen_set_pop(a_seen);

					seen_set_pop(b_seen);

					return false;
				}
			}
			else
			{
				ASSERT_OR_IGNORE(a_attach->disposition == TypeDisposition::ParameterList || a_attach->disposition == TypeDisposition::User);

				const ParameterListOrUserMemberData* const a_typed_member = static_cast<const ParameterListOrUserMemberData*>(a_member);

				const ParameterListOrUserMemberData* const b_typed_member = static_cast<const ParameterListOrUserMemberData*>(b_member);

				if (a_typed_member->offset != b_typed_member->offset
				 || a_typed_member->default_id != b_typed_member->default_id
				) {
					seen_set_pop(a_seen);

					seen_set_pop(b_seen);

					return false;
				}
			}

			const TypeId a_member_type_id = static_cast<TypeId>(a_type_and_flags.type_id_bits);

			const TypeId b_member_type_id = static_cast<TypeId>(b_type_and_flags.type_id_bits);

			if (!type_is_equal_noloop(core, a_member_type_id, b_member_type_id, a_seen, b_seen))
			{
				seen_set_pop(a_seen);

				seen_set_pop(b_seen);

				return false;
			}
		}

		seen_set_pop(a_seen);

		seen_set_pop(b_seen);

		return true;
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

			return false;
		}

		const TypeId a_next = a_attach->referenced_type_id;

		const TypeId b_next = b_attach->referenced_type_id;

		const bool reference_result = type_is_equal_noloop(core, a_next, b_next, a_seen, b_seen);

		seen_set_pop(a_seen);

		seen_set_pop(b_seen);

		return reference_result;
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

			return false;
		}

		const Maybe<TypeId> a_next = a_attach->element_type;

		const Maybe<TypeId> b_next = b_attach->element_type;

		bool element_result;

		// Array literals may have `TypeId::INVALID` as their element type if
		// they have no elements, because no element type can be inferred in
		// that case. This needs to be special cased to avoid recursing on a
		// `TypeId::INVALID`.
		if (tag == TypeTag::ArrayLiteral && (is_none(a_next) || is_none(b_next)))
			element_result = is_none(a_next) && is_none(b_next);
		else
			element_result = type_is_equal_noloop(core, get(a_next), get(b_next), a_seen, b_seen);

		seen_set_pop(a_seen);

		seen_set_pop(b_seen);

		return element_result;
	}

	case TypeTag::Func:
	{
		const SignatureType2* const a_attach = reinterpret_cast<SignatureType2*>(a->attach);

		const SignatureType2* const b_attach = reinterpret_cast<SignatureType2*>(b->attach);

		if (a_attach->is_func                      != b_attach->is_func
		 || a_attach->parameter_count              != b_attach->parameter_count
		 || a_attach->has_templated_parameter_list != b_attach->has_templated_parameter_list
		 || a_attach->has_templated_return_type    != b_attach->has_templated_return_type
		) {
			seen_set_pop(a_seen);

			seen_set_pop(b_seen);

			return false;
		}

		if (a_attach->has_templated_parameter_list || a_attach->has_templated_return_type)
			TODO("Handle comparison of template signature types.\n");

		const TypeId a_return_type_id = a_attach->return_type.type_id;

		const TypeId b_return_type_id = b_attach->return_type.type_id;

		if (!type_is_equal_noloop(core, a_return_type_id, b_return_type_id, a_seen, b_seen))
		{
			seen_set_pop(a_seen);

			seen_set_pop(b_seen);

			return false;
		}

		const TypeId a_params_type_id = a_attach->parameter_list_type_id;

		const TypeId b_params_type_id = b_attach->parameter_list_type_id;

		const bool result = type_is_equal_noloop(core, a_params_type_id, b_params_type_id, a_seen, b_seen);

		seen_set_pop(a_seen);

		seen_set_pop(b_seen);

		return result;
	}

	case TypeTag::CompositeLiteral:
	case TypeTag::Variadic:
	case TypeTag::Trait:
		TODO("Implement `type_is_equal_noloop` for CompositeLiteral, ArrayLiteral, Variadic and Trait");

	case TypeTag::INVALID:
	case TypeTag::INDIRECTION:
		break; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}



static TypeStructure* make_structure_nohash(CoreData* core, TypeTag tag, Range<byte> attach, u64 reserve_size, SourceId distinct_source_id) noexcept
{
	ASSERT_OR_IGNORE(reserve_size <= UINT16_MAX && reserve_size >= attach.count());

	MutRange<byte> memory = core->types.structures.alloc(static_cast<u32>(sizeof(TypeStructure) + reserve_size));

	TypeStructure* const structure = reinterpret_cast<TypeStructure*>(memory.begin());
	structure->tag = tag;
	structure->holotype_id = TypeId::INVALID;
	structure->distinct_source_id = distinct_source_id;
	structure->hash = 0;

	if (attach.count() != 0)
		memcpy(structure->attach, attach.begin(), attach.count());

	return structure;
}

static TypeStructure* make_structure_hash(CoreData* core, TypeTag tag, Range<byte> attach, u64 reserve_size, SourceId distinct_source_id) noexcept
{
	TypeStructure* const structure = make_structure_nohash(core, tag, attach, reserve_size, distinct_source_id);

	SeenSet seen = seen_set_create(core);

	structure->hash = hash_type_structure(core, FNV1A_SEED, &seen, structure);

	seen_set_release(core, seen);

	return structure;
}

static TypeStructure* make_indirection(CoreData* core, TypeId indirected_type_id) noexcept
{
	ASSERT_OR_IGNORE(indirected_type_id != TypeId::INVALID);

	MutRange<byte> memory = core->types.structures.alloc(sizeof(TypeStructure) + sizeof(IndirectionType));

	TypeStructure* const structure = reinterpret_cast<TypeStructure*>(memory.begin());
	structure->tag = TypeTag::INDIRECTION;
	structure->holotype_id = TypeId::INVALID;
	structure->distinct_source_id = SourceId::INVALID;
	structure->hash = 0;

	IndirectionType* const attach = reinterpret_cast<IndirectionType*>(structure->attach);
	attach->indirection_type_id = indirected_type_id;

	return structure;
}



MemoryRequirements type_pool_memory_requirements([[maybe_unused]] const Config* config) noexcept
{
	MemoryRequirements reqs;
	reqs.private_reserve = HOLOTYPES_LOOKUPS_RESERVE + HOLOTYPES_VALUES_RESERVE + SCRATCH_RESERVE;
	reqs.id_requirements_count = 1;
	reqs.id_requirements[0].reserve = STRUCTURES_RESERVE;
	reqs.id_requirements[0].alignment = 1 << MIN_STRUCTURE_SIZE_LOG2;

	return reqs;
}

void type_pool_init(CoreData* core, MemoryAllocation allocation) noexcept
{
	ASSERT_OR_IGNORE(allocation.ids[0].count() == STRUCTURES_RESERVE);

	TypePool* const types = &core->types;

	u64 offset = 0;

	const MutRange<byte> dedup_lookups_memory = allocation.private_data.mut_subrange(offset, HOLOTYPES_LOOKUPS_RESERVE);
	offset += HOLOTYPES_LOOKUPS_RESERVE;

	const MutRange<byte> dedup_values_memory = allocation.private_data.mut_subrange(offset, HOLOTYPES_VALUES_RESERVE);
	offset += HOLOTYPES_VALUES_RESERVE;

	types->holotypes.init(
		dedup_lookups_memory, HOLOTYPES_LOOKUPS_INITIAL_COMMIT_COUNT,
		dedup_values_memory, HOLOTYPES_VALUES_COMMIT_INCREMENT_COUNT
	);

	const MutRange<byte> scratch_memory = allocation.private_data.mut_subrange(offset, SCRATCH_RESERVE);
	offset += SCRATCH_RESERVE;

	ASSERT_OR_IGNORE(allocation.private_data.count() == offset);

	types->scratch.init(scratch_memory, SCRATCH_COMMIT_INCREMENT_COUNT);

	const MutRange<byte> structures_memory = allocation.ids[0];

	types->structures.init(structures_memory, Range{ STRUCTURES_CAPACITIES }, Range{ STRUCTURES_COMMITS });

	// Reserve `0` as `TypeId::INVALID`.
	types->structures.alloc(sizeof(TypeStructure));

	// Reserve simple types for use with `type_create_simple`.
	for (u8 ordinal = static_cast<u8>(TypeTag::Void); ordinal != static_cast<u8>(TypeTag::Undefined) + 1; ++ordinal)
	{
		// TODO: Once hashing is implemented for all types, this pseudo-hashing
		// can be removed.
		TypeStructure* const structure = make_structure_nohash(core, static_cast<TypeTag>(ordinal), {}, 0, SourceId::INVALID);
		structure->hash = ordinal;

		(void) holotype_id_from_interned_type_structure(core, structure);
	}
}



TypeId type_create_simple([[maybe_unused]] CoreData* core, TypeTag tag) noexcept
{
	ASSERT_OR_IGNORE(tag >= TypeTag::Void && tag <= TypeTag::Undefined);

	const TypeId type_id = static_cast<TypeId>((static_cast<u32>(tag) - 1) * 2);

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

TypeId type_create_signature(CoreData* core, TypeTag tag, SignatureType2 attach) noexcept
{
	ASSERT_OR_IGNORE(tag == TypeTag::Func);

	return holotype_id_from_attachment(core, tag, range::from_object_bytes(&attach));
}

TypeId type_create_composite(CoreData* core, TypeTag tag, TypeDisposition disposition, SourceId distinct_source_id, u32 initial_member_capacity, bool is_fixed_member_capacity) noexcept
{
	ASSERT_OR_IGNORE(tag == TypeTag::Composite || tag == TypeTag::CompositeLiteral);

	ASSERT_OR_IGNORE(disposition != TypeDisposition::INVALID);

	const CompositeTypeAllocInfo alloc_info = composite_type_alloc_info(disposition, initial_member_capacity);

	CompositeType composite{};
	composite.size = 0;
	composite.stride = 0;
	composite.align_log2 = 0;
	composite.is_open = true;
	composite.is_fixed = is_fixed_member_capacity;
	composite.disposition = disposition;
	composite.member_used = 0;
	composite.member_capacity = alloc_info.member_capacity;

	TypeStructure* const structure = make_structure_nohash(core, tag, range::from_object_bytes(&composite), alloc_info.alloc_size, distinct_source_id);

	const TypeId structure_type_id = id_from_structure(core, structure);

	if (is_fixed_member_capacity)
		return structure_type_id;

	TypeStructure* const indirection = make_indirection(core, structure_type_id);

	const TypeId indirection_type_id = id_from_structure(core, indirection);

	return indirection_type_id;
}

TypeId type_seal_composite(CoreData* core, TypeId type_id, u64 size, u32 align, u64 stride) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	ASSERT_OR_IGNORE(align <= UINT16_MAX);

	TypeStructure* const structure = follow_indirection(core, structure_from_id(core, type_id));

	ASSERT_OR_IGNORE(structure->tag == TypeTag::Composite || structure->tag == TypeTag::CompositeLiteral);

	CompositeType* const composite = reinterpret_cast<CompositeType*>(structure->attach);

	ASSERT_OR_IGNORE(composite->is_open);

	if (composite->disposition == TypeDisposition::User)
	{
		ASSERT_OR_IGNORE(align != 0 && is_pow2(align));

		composite->size = size;
		composite->align_log2 = count_trailing_zeros_assume_one(align);
		composite->stride = stride;
	}
	else
	{
		ASSERT_OR_IGNORE(size == 0 && align == 0 && stride == 0);

		composite->stride = next_multiple(composite->size, static_cast<u64>(1) << composite->align_log2);
	}

	composite->is_open = false;

	SeenSet seen = seen_set_create(core);

	const u32 hash = hash_type_structure(core, FNV1A_SEED, &seen, structure);

	seen_set_release(core, seen);

	if (hash == 0)
		return type_id;

	structure->hash = hash;

	// TODO: Clean up references to indirection and potentially abandoned
	// structure.
	return holotype_id_from_interned_type_structure(core, structure);
}

bool type_add_composite_member(CoreData* core, TypeId type_id, MemberInit init) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	TypeStructure* const structure = structure_from_id(core, type_id);

	TypeStructure* direct_structure = follow_indirection(core, structure);

	ASSERT_OR_IGNORE(direct_structure->tag == TypeTag::Composite || direct_structure->tag == TypeTag::CompositeLiteral);

	CompositeType* composite = reinterpret_cast<CompositeType*>(direct_structure->attach);

	ASSERT_OR_IGNORE(composite->is_open);

	// File members go through `type_add_file_member`.
	ASSERT_OR_IGNORE(composite->disposition != TypeDisposition::File);

	ASSERT_OR_IGNORE(init.name != IdentifierId::INVALID || composite->disposition == TypeDisposition::Initializer);

	CompositeMemberInfo members = composite_members(composite);

	if (composite->member_capacity == composite->member_used)
		composite_realloc(core, structure, &direct_structure, &composite, &members);

	if (init.name != IdentifierId::INVALID)
	{
		u16 unused_rank;

		const void* unused_data;

		if (find_member_by_name(&members, init.name, &unused_rank, &unused_data))
			return false;
	}

	if (composite->disposition != TypeDisposition::User)
	{
		ASSERT_OR_IGNORE(init.offset == 0);

		const TypeMetrics metrics = type_metrics_from_id(core, init.type_id);

		const u64 member_begin = next_multiple(composite->size, static_cast<u64>(metrics.align));

		init.offset = member_begin;

		composite->size = member_begin + metrics.size;

		const u8 align_log2 = count_trailing_zeros_assume_one(metrics.align);

		if (composite->align_log2 < align_log2)
			composite->align_log2 = align_log2;
	}

	members.names[composite->member_used] = init.name;

	void* const member_data = member_at(&members, composite->member_used);

	if (composite->disposition == TypeDisposition::Initializer)
	{
		ASSERT_OR_IGNORE(!init.is_eval
		              && !init.is_pub
		              && init.offset <= UINT32_MAX
		              && is_none(init.default_id)
		);

		InitializerMemberData* const member = static_cast<InitializerMemberData*>(member_data);
		member->type_and_flags.type_id_bits = static_cast<u32>(init.type_id);
		member->type_and_flags.is_pending = false;
		member->type_and_flags.is_pub = false;
		member->type_and_flags.is_mut = init.is_mut;
		member->type_and_flags.is_eval = false;
		member->offset = static_cast<u32>(init.offset);
	}
	else
	{
		ASSERT_OR_IGNORE(
			(composite->disposition == TypeDisposition::ParameterList && !init.is_pub) ||
			(composite->disposition == TypeDisposition::User && !init.is_pub && !init.is_eval)
		);

		ParameterListOrUserMemberData* const member = static_cast<ParameterListOrUserMemberData*>(member_data);
		member->type_and_flags.type_id_bits = static_cast<u32>(init.type_id);
		member->type_and_flags.is_pending = false;
		member->type_and_flags.is_pub = false;
		member->type_and_flags.is_mut = init.is_mut;
		member->type_and_flags.is_eval = init.is_eval;
		member->offset = init.offset;
		member->default_id = init.default_id;
	}

	composite->member_used += 1;

	return true;
}

void type_add_file_member(CoreData* core, TypeId type_id, IdentifierId name, OpcodeId completion_opcode, bool is_pub, bool is_mut) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID && name != IdentifierId::INVALID && completion_opcode != OpcodeId::INVALID);

	TypeStructure* const structure = structure_from_id(core, type_id);

	TypeStructure* direct_structure = follow_indirection(core, structure);

	ASSERT_OR_IGNORE(direct_structure->tag == TypeTag::Composite);

	CompositeType* composite = reinterpret_cast<CompositeType*>(direct_structure->attach);

	ASSERT_OR_IGNORE(composite->is_open);

	ASSERT_OR_IGNORE(composite->disposition == TypeDisposition::File);

	CompositeMemberInfo members = composite_members(composite);

	if (composite->member_capacity == composite->member_used)
		composite_realloc(core, structure, &direct_structure, &composite, &members);

	members.names[composite->member_used] = name;

	FileMemberData* const member = static_cast<FileMemberData*>(member_at(&members, composite->member_used));
	member->type_and_flags.type_id_bits = static_cast<u32>(TypeId::INVALID);
	member->type_and_flags.is_pending = true;
	member->type_and_flags.is_pub = is_pub;
	member->type_and_flags.is_mut = is_mut;
	member->type_and_flags.is_eval = false;
	member->completion_id = completion_opcode;

	composite->member_used += 1;
}

void type_add_templated_parameter_list_member(CoreData* core, TypeId type_id, IdentifierId name, OpcodeId completion_opcode, bool is_eval, bool is_mut) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID && name != IdentifierId::INVALID);

	TypeStructure* const structure = structure_from_id(core, type_id);

	TypeStructure* direct_structure = follow_indirection(core, structure);

	ASSERT_OR_IGNORE(direct_structure->tag == TypeTag::Composite);

	CompositeType* composite = reinterpret_cast<CompositeType*>(direct_structure->attach);

	ASSERT_OR_IGNORE(composite->is_open);

	ASSERT_OR_IGNORE(composite->disposition == TypeDisposition::ParameterList);

	CompositeMemberInfo members = composite_members(composite);

	if (composite->member_capacity == composite->member_used)
		composite_realloc(core, structure, &direct_structure, &composite, &members);

	members.names[composite->member_used] = name;

	ParameterListOrUserMemberData* const member = static_cast<ParameterListOrUserMemberData*>(member_at(&members, composite->member_used));
	member->type_and_flags.type_id_bits = static_cast<u32>(TypeId::INVALID);
	member->type_and_flags.is_pending = true;
	member->type_and_flags.is_pub = false;
	member->type_and_flags.is_mut = is_mut;
	member->type_and_flags.is_eval = is_eval;
	member->parameter_completion_id = completion_opcode;
	member->offset = 0;

	composite->member_used += 1;
}

void type_set_file_member_info(CoreData* core, TypeId type_id, u16 rank, TypeId member_type_id, ForeverValueId value_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	ASSERT_OR_IGNORE(member_type_id != TypeId::INVALID);

	TypeStructure* const structure = follow_indirection(core, structure_from_id(core, type_id));

	ASSERT_OR_IGNORE(structure->tag == TypeTag::Composite);

	CompositeType* const composite = reinterpret_cast<CompositeType*>(structure->attach);

	ASSERT_OR_IGNORE(composite->disposition == TypeDisposition::File);

	ASSERT_OR_IGNORE(rank < composite->member_used);

	const CompositeMemberInfo members = composite_members(composite);

	FileMemberData* const member = static_cast<FileMemberData*>(member_at(&members, rank));

	ASSERT_OR_IGNORE(member->type_and_flags.is_pending);

	member->type_and_flags.type_id_bits = static_cast<u32>(member_type_id);
	member->type_and_flags.is_pending = false;
	member->value_id = value_id;
}

void type_set_templated_parameter_list_member_info(CoreData* core, TypeId type_id, u16 rank, TypeId member_type_id, Maybe<ForeverValueId> member_default_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	ASSERT_OR_IGNORE(member_type_id != TypeId::INVALID);

	TypeStructure* const structure = follow_indirection(core, structure_from_id(core, type_id));

	ASSERT_OR_IGNORE(structure->tag == TypeTag::Composite || structure->tag == TypeTag::CompositeLiteral);

	CompositeType* const composite = reinterpret_cast<CompositeType*>(structure->attach);

	ASSERT_OR_IGNORE(composite->disposition == TypeDisposition::ParameterList);

	ASSERT_OR_IGNORE(rank < composite->member_used);

	const CompositeMemberInfo members = composite_members(composite);

	const TypeMetrics metrics = type_metrics_from_id(core, member_type_id);

	const u64 member_offset = (composite->size + metrics.align - 1) & ~static_cast<u64>(metrics.align - 1);

	ParameterListOrUserMemberData* const member = static_cast<ParameterListOrUserMemberData*>(member_at(&members, rank));

	ASSERT_OR_IGNORE(member->type_and_flags.is_pending);

	member->type_and_flags.type_id_bits = static_cast<u32>(member_type_id);
	member->type_and_flags.is_pending = false;
	member->default_id = member_default_id;
	member->offset = member_offset;

	composite->size = member_offset + metrics.size;

	const u8 align_log2 = count_trailing_zeros_assume_one(metrics.align);

	if (composite->align_log2 < align_log2)
		composite->align_log2 = align_log2;
}

TypeId type_copy_templated_parameter_list(CoreData* core, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeStructure* const old_structure = follow_indirection(core, structure_from_id(core, type_id));

	ASSERT_OR_IGNORE(old_structure->tag == TypeTag::Composite || old_structure->tag == TypeTag::CompositeLiteral);

	const CompositeType* const old_composite = reinterpret_cast<const CompositeType*>(old_structure->attach);

	ASSERT_OR_IGNORE(old_composite->disposition == TypeDisposition::ParameterList && !old_composite->is_open);

	const CompositeTypeAllocInfo alloc_info = composite_type_alloc_info(old_composite->disposition, old_composite->member_used);

	const Range<byte> to_copy{ reinterpret_cast<const byte*>(old_composite), sizeof(CompositeType) + old_composite->member_used * sizeof(IdentifierId) };

	TypeStructure* const new_structure = make_structure_nohash(core, TypeTag::Composite, to_copy, alloc_info.alloc_size, old_structure->distinct_source_id);
	new_structure->holotype_id = TypeId::INVALID;

	CompositeType* const new_composite = reinterpret_cast<CompositeType*>(new_structure->attach);
	new_composite->is_fixed = true;
	new_composite->member_capacity = alloc_info.member_capacity;

	const CompositeMemberInfo old_members = composite_members(old_composite);

	const CompositeMemberInfo new_members = composite_members(new_composite);

	memcpy(new_members.members, old_members.members, old_members.count * old_members.member_stride);

	const TypeId new_structure_type_id = id_from_structure(core, new_structure);

	return new_structure_type_id;
}

u32 type_get_composite_member_count(CoreData* core, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	TypeStructure* const structure = follow_indirection(core, structure_from_id(core, type_id));

	ASSERT_OR_IGNORE(structure->tag == TypeTag::Composite || structure->tag == TypeTag::CompositeLiteral);

	CompositeType* const composite = reinterpret_cast<CompositeType*>(structure->attach);

	ASSERT_OR_IGNORE(!composite->is_open);

	return composite->member_used;
}

void type_discard(CoreData* core, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	TypeStructure* const structure = follow_indirection(core, structure_from_id(core, type_id));

	ASSERT_OR_IGNORE(structure->tag == TypeTag::Composite || structure->tag == TypeTag::CompositeLiteral);

	CompositeType* const composite = reinterpret_cast<CompositeType*>(structure->attach);

	const CompositeTypeAllocInfo dealloc_info = composite_type_alloc_info(composite->disposition, composite->member_capacity);

	core->types.structures.dealloc({ reinterpret_cast<byte*>(structure), dealloc_info.alloc_size + sizeof(TypeStructure) });
}



TypeRelation type_relation(CoreData* core, TypeId first_type_id, TypeId second_type_id) noexcept
{
	ASSERT_OR_IGNORE(first_type_id != TypeId::INVALID && second_type_id != TypeId::INVALID);

	if (type_is_equal(core, first_type_id, second_type_id))
		return TypeRelation::Equal;

	if (type_can_implicitly_convert_from_to_assume_unequal(core, first_type_id, second_type_id))
		return TypeRelation::FirstConvertsToSecond;

	if (type_can_implicitly_convert_from_to_assume_unequal(core, second_type_id, first_type_id))
		return TypeRelation::SecondConvertsToFirst;

	return TypeRelation::Unrelated;
}

bool type_is_equal(CoreData* core, TypeId type_id_a, TypeId type_id_b) noexcept
{
	ASSERT_OR_IGNORE(type_id_a != TypeId::INVALID && type_id_b != TypeId::INVALID);

	// Equal `TypeId`s imply equal types. This is checked here redundantly even
	// though it is also checked in `type_is_equal_noloop` to avoid having to
	// create a `SeenSet` in the simplest case.
	if (type_id_a == type_id_b)
		return true;

	// ... Same goes for equal holotypes.
	TypeStructure* const a = follow_indirection(core, structure_from_id(core, type_id_a));

	TypeStructure* const b = follow_indirection(core, structure_from_id(core, type_id_b));

	if (a->holotype_id != TypeId::INVALID && b->holotype_id != TypeId::INVALID)
		return a->holotype_id == b->holotype_id;

	SeenSet a_seen = seen_set_create(core);

	SeenSet b_seen = seen_set_create(core);

	const bool result = type_is_equal_noloop(core, type_id_a, type_id_b, &a_seen, &b_seen);

	seen_set_release(core, a_seen);

	seen_set_release(core, b_seen);

	return result;
}

bool type_can_implicitly_convert_from_to(CoreData* core, TypeId from_type_id, TypeId to_type_id) noexcept
{
	ASSERT_OR_IGNORE(from_type_id != TypeId::INVALID && to_type_id != TypeId::INVALID);

	if (type_is_equal(core, from_type_id, to_type_id))
		return true;

	return type_can_implicitly_convert_from_to_assume_unequal(core, from_type_id, to_type_id);
}

Maybe<TypeId> type_unify(CoreData* core, TypeId type_id_a, TypeId type_id_b) noexcept
{
	ASSERT_OR_IGNORE(type_id_a != TypeId::INVALID && type_id_b != TypeId::INVALID);

	if (type_is_equal(core, type_id_a, type_id_b))
		return some(type_id_a < type_id_b ? type_id_a : type_id_b);

	if (type_can_implicitly_convert_from_to_assume_unequal(core, type_id_a, type_id_b))
		return some(type_id_b);

	if (type_can_implicitly_convert_from_to_assume_unequal(core, type_id_b, type_id_a))
		return some(type_id_a);

	return none<TypeId>();
}



TypeDisposition type_disposition_from_id(CoreData* core, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeStructure* const structure = follow_indirection(core, structure_from_id(core, type_id));

	const CompositeType* const composite = reinterpret_cast<const CompositeType*>(structure->attach);

	return composite->disposition;
}

bool type_has_metrics(CoreData* core, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeStructure* structure = follow_indirection(core, structure_from_id(core, type_id));

	if (structure->tag != TypeTag::Composite)
		return true;

	const CompositeType* const composite = reinterpret_cast<const CompositeType*>(structure->attach);

	return composite->disposition != TypeDisposition::User || !composite->is_open;
}

TypeMetrics type_metrics_from_id(CoreData* core, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeStructure* const structure = follow_indirection(core, structure_from_id(core, type_id));

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

	case TypeTag::Undefined:
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

		const TypeMetrics element_metrics = type_metrics_from_id(core, get(array->element_type));

		return { element_metrics.stride * (array->element_count - 1) + element_metrics.size, (element_metrics.stride * array->element_count), element_metrics.align };
	}

	case TypeTag::Func:
	{
		return { 16, 16, 8 };
	}

	case TypeTag::Composite:
	case TypeTag::CompositeLiteral:
	{
		const CompositeType* const composite = reinterpret_cast<const CompositeType*>(structure->attach);

		ASSERT_OR_IGNORE(!composite->is_open);

		return TypeMetrics{ composite->size, composite->stride, static_cast<u32>(1) << composite->align_log2 };
	}

	case TypeTag::Definition:
	case TypeTag::TailArray:
	case TypeTag::Trait:
		TODO("Implement `type_metrics_from_id` for TailArray and Trait.");

	case TypeTag::INVALID:
	case TypeTag::INDIRECTION:
		break; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;
}

TypeTag type_tag_from_id(CoreData* core, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeStructure* const structure = structure_from_id(core, type_id);

	if (structure->tag == TypeTag::INDIRECTION)
		return TypeTag::Composite;

	return structure->tag;
}

bool type_member_info_by_rank(CoreData* core, TypeId type_id, u16 rank, MemberInfo* out_info, OpcodeId* out_completion_id)
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeStructure* const structure = follow_indirection(core, structure_from_id(core, type_id));

	ASSERT_OR_IGNORE(structure->tag == TypeTag::Composite || structure->tag == TypeTag::CompositeLiteral);

	const CompositeType* const composite = reinterpret_cast<const CompositeType*>(structure->attach);

	ASSERT_OR_IGNORE(composite->member_used > rank);

	const CompositeMemberInfo members = composite_members(composite);

	return fill_member_info(composite, rank, member_at(&members, rank), out_info, out_completion_id);
}

MemberByNameRst type_member_info_by_name(CoreData* core, TypeId type_id, IdentifierId name, MemberInfo* out_info, OpcodeId* out_completion_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeStructure* const structure = follow_indirection(core, structure_from_id(core, type_id));

	ASSERT_OR_IGNORE(structure->tag == TypeTag::Composite || structure->tag == TypeTag::CompositeLiteral);

	const CompositeType* const composite = reinterpret_cast<const CompositeType*>(structure->attach);

	const CompositeMemberInfo members = composite_members(composite);

	u16 rank;

	const void* member;

	if (!find_member_by_name(&members, name, &rank, &member))
		return MemberByNameRst::NotFound;

	return fill_member_info(composite, rank, member, out_info, out_completion_id) ? MemberByNameRst::Ok : MemberByNameRst::Incomplete;
}

IdentifierId type_member_name_by_rank(CoreData* core, TypeId type_id, u16 rank) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeStructure* const structure = follow_indirection(core, structure_from_id(core, type_id));

	ASSERT_OR_IGNORE(structure->tag == TypeTag::Composite || structure->tag == TypeTag::CompositeLiteral);

	const CompositeType* const composite = reinterpret_cast<const CompositeType*>(structure->attach);

	ASSERT_OR_IGNORE(composite->member_used > rank);

	const CompositeMemberInfo members = composite_members(composite);

	return members.names[rank];
}

const void* type_attachment_from_id_raw(CoreData* core, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeStructure* const structure = follow_indirection(core, structure_from_id(core, type_id));

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

	const TypeStructure* const structure = structure_from_id(core, type_id);

	const TypeStructure* const direct_structure = follow_indirection(core, structure);

	ASSERT_OR_IGNORE(direct_structure->tag == TypeTag::Composite || structure->tag == TypeTag::CompositeLiteral);

	const CompositeType* const composite = reinterpret_cast<const CompositeType*>(direct_structure->attach);

	const CompositeMemberInfo members = composite_members(composite);

	MemberIterator it;
	it.structure = composite->member_used == 0 ? nullptr : composite->is_fixed ? direct_structure : structure;
	it.names = members.names;
	it.members = members.members;
	it.core = core;
	it.member_stride = members.member_stride;
	it.is_indirect = !composite->is_fixed;
	it.rank = 0;

	return it;
}

bool next(MemberIterator* it, MemberInfo* out_info, OpcodeId* out_completion_id) noexcept
{
	ASSERT_OR_IGNORE(has_next(it));

	const TypeStructure* const structure = follow_indirection(it->core, static_cast<const TypeStructure*>(it->structure));

	const CompositeType* const composite = reinterpret_cast<const CompositeType*>(structure->attach);

	CompositeMemberInfo members;

	if (it->names == reinterpret_cast<const IdentifierId*>(composite + 1))
	{
		members.disposition = composite->disposition;
		members.member_stride = it->member_stride;
		members.count = composite->member_used;
		members.names = const_cast<IdentifierId*>(it->names);
		members.members = const_cast<void*>(it->members);
	}
	else
	{
		members = composite_members(composite);

		it->names = members.names;
		it->members = members.members;
	}

	const u16 curr_rank = it->rank;

	const void* const curr_member = member_at(&members, curr_rank);

	it->rank = curr_rank + 1;

	if (composite->member_used == it->rank)
		it->structure = nullptr;

	return fill_member_info(composite, curr_rank, curr_member, out_info, out_completion_id);
}

bool has_next(const MemberIterator* it) noexcept
{
	return it->structure != nullptr;
}
