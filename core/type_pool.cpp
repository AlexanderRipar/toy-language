#include "pass_data.hpp"

#include "../infra/hash.hpp"
#include "../infra/inplace_sort.hpp"

#include <cstdlib>

struct TypeStructure
{
	TypeTag tag;

	u16 bytes;

	u32 m_hash;

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
	u64 data[];
	#if COMPILER_MSVC
	#pragma warning(pop)
	#elif COMPILER_CLANG
	#pragma clang diagnostic pop
	#elif COMPILER_GCC
	#pragma GCC diagnostic pop
	#endif

	static constexpr u32 stride() noexcept
	{
		return 8;
	}

	static u32 required_strides(AttachmentRange<byte, TypeTag> key) noexcept
	{
		return static_cast<u32>((offsetof(TypeStructure, data) + key.count() + stride() - 1) / stride());
	}

	u32 used_strides() const noexcept
	{
		return static_cast<u32>((offsetof(TypeStructure, data) + bytes + stride() - 1) / stride());
	}

	u32 hash() const noexcept
	{
		return m_hash;
	}

	bool equal_to_key(AttachmentRange<byte, TypeTag> key, u32 key_hash) const noexcept
	{
		return m_hash == key_hash && key.attachment() == tag && key.count() == bytes && memcmp(key.begin(), data, key.count()) == 0;
	}

	void init(AttachmentRange<byte, TypeTag> key, u32 key_hash) noexcept
	{
		ASSERT_OR_IGNORE(key.count() <= UINT16_MAX);

		m_hash = key_hash;

		bytes = static_cast<u16>(key.count());

		tag = key.attachment();

		memcpy(data, key.begin(), key.count());
	}
};

struct TypeMember
{
	u64 offset_or_global_value : 59;

	u64 is_global : 1;

	u64 is_use : 1;

	u64 is_pub : 1;

	u64 has_pending_value : 1;

	u64 has_pending_type : 1;

	union
	{
		TypeId id;

		TypecheckerResumptionId resumption_id;
	} type;

	SourceId source;

	IdentifierId name;
};

struct TypeMemberAst
{
	AstNodeId opt_type;

	AstNodeId opt_value;
};

struct FindByNameResult
{
	TypeMember* member;

	u16 rank;

	TypeId surrounding_type_id;

	TypeMemberAst ast;
};

struct FindByRankResult
{
	TypeMember* member;

	IdentifierId name;

	TypeMemberAst ast;
};

struct CompositeTypeHeader
{
	u64 size;

	u64 stride;

	u32 align;

	u32 member_count;
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
	IdentifierId names[];
	#if COMPILER_MSVC
	#pragma warning(pop)
	#elif COMPILER_CLANG
	#pragma clang diagnostic pop
	#elif COMPILER_GCC
	#pragma GCC diagnostic pop
	#endif
};

struct TypeName
{
	static constexpr u32 STRUCTURE_INDEX_NORMAL = 0;
	static constexpr u32 STRUCTURE_INDEX_BUILDER = 1;
	static constexpr u32 STRUCTURE_INDEX_INDIRECT = 2;

	// This is used to initialize a `TypeName` entry that will never match any
	// other in `create_type_pool`. It must not be used as the value of the
	// `structure_index_kind` of any other `TypeName`.
	static constexpr u32 INVALID_STRUCTURE_INDEX = 3;

	// `TypeId` of the type from which this type was derived.
	//
	// E.g. in `let T = U`, `T`'s `parent_id` will be set to the `TypeId` of
	// `U`.
	//
	// If the type is not derived from another one, `parent_id` is set to
	// `INVALID_TYPE_ID`.
	TypeId parent_type_id;

	// `TypeId` of the first type in the parent hierarchy which is `distinct`,
	// or the root type if there is no such type.
	//
	// If this type is itself `distinct` or not derived from another type,
	// `distinct_root_id` is set to `INVALID_TYPE_ID`.
	TypeId distinct_root_type_id;

	// One of the following, depending on the value of `structure_index_kind`:
	//
	// - `STRUCTURE_INDEX_NORMAL`: An index into `TypePool.structural_types`
	//   from which this type's structure can be retrieved.
	//
	// - `STRUCTURE_INDEX_BUILDER` A qword-strided index into
	//   `TypePool.builders`. This indicates that the type is not yet
	//   complete, and could thus not be hashed into
	//   `TypePool.structural_types` yet.
	//
	// - `STRUCTURE_INDEX_INDIRECT`: An index into `TypePool.named_types`. This
	//   indicates that this type's parent type was not complete
	//   (`STRUCTURE_INDEX_BUILDER` or `STRUCTURE_INDEX_INDIRECT`) at the time
	//   this type was created. Since the parent's structural type index will
	//   be updated to a reference of type `STRUCTURE_INDEX_NORMAL` later, this
	// indirection is necessary to avoid a dangling reference to the
	// left-behind `TypeBuilder`.
	u32 structure_index : 30;

	// See `structure_index`
	u32 structure_index_kind : 2;

	// Location at which the type was defined. If the type is a built-in (i.e.,
	// defined in the prelude) this is set to `INVALID_SOURCE_ID`.
	//
	// This is needed for diagnostics.
	SourceId source_id;

	// Name under which the type is created. If the type has no name,
	// `INVALID_IDENTIFIER_ID`.
	//
	// This is needed for diagnostics.
	IdentifierId name_id;

	static constexpr u32 stride() noexcept
	{
		return sizeof(TypeName);
	}

	static u32 required_strides([[maybe_unused]] TypeName key) noexcept
	{
		return 1;
	}

	u32 used_strides() const noexcept
	{
		return 1;
	}

	u32 hash() const noexcept
	{
		return fnv1a(range::from_object_bytes(this));
	}

	bool equal_to_key(TypeName key, [[maybe_unused]] u32 key_hash) const noexcept
	{
		return memcmp(&key, this, sizeof(*this)) == 0;
	}

	void init(TypeName key, [[maybe_unused]] u32 key_hash) noexcept
	{
		memcpy(this, &key, sizeof(*this));
	}
};

struct TypeBuilder
{
	s32 next_offset;

	u32 used;

	IdentifierId names[8];

	TypeMember members[8];

	TypeMemberAst asts[8];
};

union TypeBuilderHeader
{
	#if COMPILER_CLANG
		#pragma clang diagnostic push
		#pragma clang diagnostic ignored "-Wgnu-anonymous-struct" // anonymous structs are a GNU extension
		#pragma clang diagnostic ignored "-Wnested-anon-types" // anonymous types declared in an anonymous union are an extension
	#elif COMPILER_GCC
		#pragma GCC diagnostic push
		#pragma GCC diagnostic ignored "-Wpedantic" // ISO C++ prohibits anonymous structs
	#endif
	struct
	{
		s32 head_offset;

		s32 tail_offset;

		u32 total_used;

		u32 incomplete_member_count;

		bool is_closed;

		SourceId source_id;

		u32 align;

		u64 size;

		u64 stride;
	};
	#if COMPILER_CLANG
		#pragma clang diagnostic pop
	#elif COMPILER_GCC
		#pragma GCC diagnostic pop
	#endif

	TypeBuilder unused_;
};

struct TypePool
{
	IndexMap<AttachmentRange<byte, TypeTag>, TypeStructure> structural_types;

	IndexMap<TypeName, TypeName> named_types;

	ReservedVec<u64> builders;

	s32 first_free_builder_index;

	ErrorSink* errors;
};

static_assert(sizeof(TypeBuilder) == sizeof(TypeBuilderHeader));



static bool find_member_by_name(TypePool* types, TypeId type_id, IdentifierId name, bool include_use, FindByNameResult* out) noexcept;

template<typename T>
[[nodiscard]] static T* data(TypeStructure* entry) noexcept
{
	return reinterpret_cast<T*>(&entry->data);
}

template<typename T>
[[nodiscard]] static const T* data(const TypeStructure* entry) noexcept
{
	return reinterpret_cast<const T*>(&entry->data);
}

static TypeBuilderHeader* type_builder_header_at_index(TypePool* types, s32 index) noexcept
{
	return reinterpret_cast<TypeBuilderHeader*>(types->builders.begin() + index);
}

static TypeBuilder* type_builder_at_offset(TypeBuilder* builder, s32 offset) noexcept
{
	return reinterpret_cast<TypeBuilder*>(reinterpret_cast<u64*>(builder) + offset);
}

static const TypeBuilder* type_builder_at_offset(const TypeBuilder* builder, s32 offset) noexcept
{
	return type_builder_at_offset(const_cast<TypeBuilder*>(builder), offset);
}

static s32 type_builder_difference(const TypeBuilder* from, const TypeBuilder* to) noexcept
{
	return static_cast<s32>(reinterpret_cast<const u64*>(to) - reinterpret_cast<const u64*>(from));
}

static void* alloc_type_builder(TypePool* types) noexcept
{
	const s32 first_free_index = types->first_free_builder_index;

	if (first_free_index < 0)
		return static_cast<TypeBuilder*>(types->builders.reserve_exact(sizeof(TypeBuilder)));

	TypeBuilder* const builder = type_builder_at_offset(reinterpret_cast<TypeBuilder*>(types->builders.begin()), first_free_index);

	types->first_free_builder_index = builder->next_offset == 0 ? -1 : builder->next_offset + first_free_index;

	return builder;
}

static void free_type_builder(TypePool* types, TypeBuilderHeader* header) noexcept
{
	const s32 old_first_free_index = types->first_free_builder_index;

	if (old_first_free_index >= 0)
	{
		TypeBuilder* const tail_builder = type_builder_at_offset(&header->unused_, header->tail_offset);

		const s32 tail_index = type_builder_difference(reinterpret_cast<TypeBuilder*>(types->builders.begin()), tail_builder);

		tail_builder->next_offset = old_first_free_index - tail_index;
	}

	types->first_free_builder_index = type_builder_difference(reinterpret_cast<TypeBuilder*>(types->builders.begin()), &header->unused_);
}

static TypeMember* member_array_from_name_array(IdentifierId* names, u32 count) noexcept
{
	ASSERT_OR_IGNORE((reinterpret_cast<u64>(names) & 7) == 0);

	return reinterpret_cast<TypeMember*>(names + ((count + 1) & ~1));
}

static constexpr u32 composite_type_alloc_size(u32 member_count) noexcept
{
	return sizeof(CompositeTypeHeader) + ((member_count + 1) & ~1) * sizeof(IdentifierId) + member_count * sizeof(TypeMember);
}

static u32 structure_index_from_complete_type_builder(TypePool* types, const TypeBuilderHeader* header) noexcept
{
	ASSERT_OR_IGNORE(header->incomplete_member_count == 0);

	// Use a stack-based buffer if there are only a few members in the builder;
	// Otherwise allocate a buffer from the heap.
	byte stack_buffer[composite_type_alloc_size(32)];

	CompositeType* composite;

	const u32 alloc_size = composite_type_alloc_size(header->total_used);

	if (alloc_size > sizeof(stack_buffer))
	{
		composite = static_cast<CompositeType*>(malloc(alloc_size));

		if (composite == nullptr)
			panic("malloc failed (0x%X)\n", minos::last_error());
	}
	else
	{
		composite = reinterpret_cast<CompositeType*>(&stack_buffer);
	}

	// Initialize `composite->header`

	composite->header.size = header->size;
	composite->header.stride = header->stride;
	composite->header.align = header->align;
	composite->header.member_count = header->total_used;

	// Initialize `composite->members`

	const TypeBuilder* curr = type_builder_at_offset(&header->unused_, header->head_offset);

	u32 curr_index = 0;

	IdentifierId* const names = composite->names;

	TypeMember* const members = member_array_from_name_array(names, composite->header.member_count);

	while (true)
	{
		ASSERT_OR_IGNORE(curr_index + curr->used <= header->total_used);

		memcpy(names + curr_index, curr->names, curr->used * sizeof(curr->names[0]));

		memcpy(members + curr_index, curr->members, curr->used * sizeof(curr->members[0]));

		curr_index += curr->used;

		if (curr->next_offset == 0)
			break;

		curr = type_builder_at_offset(curr, curr->next_offset);
	}

	ASSERT_OR_IGNORE(curr_index == header->total_used);

	// In case there is an odd number of members, there is a padding
	// `IdentifierId` before the `TypeMembers`.
	// Set this to `INVALID_IDENTIFIER_ID` for consistent hashing.
	if ((header->total_used & 1) != 0)
		composite->names[header->total_used] = INVALID_IDENTIFIER_ID;

	// Hash the created composite into `TypeBuilder.structural_types`.

	const Range<byte> data{ reinterpret_cast<byte*>(composite), sizeof(composite->header) + ((header->total_used + 1) & ~1) * sizeof(IdentifierId) + header->total_used * sizeof(TypeMember) };

	const u32 structural_index = types->structural_types.index_from(AttachmentRange{ data, TypeTag::Composite }, fnv1a_step(fnv1a(data), static_cast<byte>(TypeTag::Composite)));

	// If we allocated the composite buffer from the heap, free it.
	if (composite != reinterpret_cast<CompositeType*>(&stack_buffer))
		free(composite);

	return structural_index;
}

static bool resolve_name_structure(TypePool* types, TypeName* name) noexcept
{
	if (name->structure_index_kind == TypeName::STRUCTURE_INDEX_NORMAL)
	{
		return true;
	}
	else if (name->structure_index_kind == TypeName::STRUCTURE_INDEX_BUILDER)
	{
		return false; // TODO: Try to complete builder (?)
	}
	else
	{
		ASSERT_OR_IGNORE(name->structure_index_kind == TypeName::STRUCTURE_INDEX_INDIRECT);

		TypeName* const indirect = types->named_types.value_from(name->structure_index);

		if (indirect->structure_index_kind == TypeName::STRUCTURE_INDEX_BUILDER)
			return false; // TODO: Try to complete builder (?)

		ASSERT_OR_IGNORE(indirect->structure_index_kind == TypeName::STRUCTURE_INDEX_NORMAL);

		name->structure_index = indirect->structure_index;
		name->structure_index_kind = indirect->structure_index_kind;

		return true;
	}
}

static TypeBuilderHeader* get_deferred_type_builder(TypePool* types, TypeName* name) noexcept
{
	ASSERT_OR_IGNORE(name->structure_index_kind == TypeName::STRUCTURE_INDEX_BUILDER || name->structure_index_kind == TypeName::STRUCTURE_INDEX_INDIRECT);

	if (name->structure_index_kind == TypeName::STRUCTURE_INDEX_INDIRECT)
		name = types->named_types.value_from(name->structure_index);

	ASSERT_OR_IGNORE(name->structure_index_kind == TypeName::STRUCTURE_INDEX_BUILDER);

	return type_builder_header_at_index(types, name->structure_index);
}

static TypeId common_type_id_with_masked_assignability(TypePool* types, TypeId type_id_a, TypeId type_id_b) noexcept
{
	ASSERT_OR_IGNORE(type_id_a.rep != INVALID_TYPE_ID.rep);

	ASSERT_OR_IGNORE(type_id_b.rep != INVALID_TYPE_ID.rep);

	const u32 index_a = type_id_a.rep >> 1;

	const u32 index_b = type_id_b.rep >> 1;

	// Check the common case. If the ids themselves are equal, we have a match
	// already.

	if (index_a == index_b)
	{
		// Mask assignability by ANDing together type ids. This works because
		// they are otherwise equal.
		return TypeId{ type_id_a.rep & type_id_b.rep };
	}

	// Since the ids were not equal, there are a few cases left:
	// 1. The types are aliases with the same `distinct_root_type_id`; In this
	//    case, we have a match.
	// 2. The types refer to different `distinct_root_type_id`s, but the two
	//    roots are really one. This happens when a partially complete type is
	//    constructed twice before being completed and is a rare case.
	// 3. The types are not compatible.

	// Check for case 1.

	TypeName* const name_a = types->named_types.value_from(index_a);

	if (!resolve_name_structure(types, name_a))
		panic("Tried comparing incomplete type for compatibility\n"); // TODO: Figure out what to do here

	TypeName* const name_b = types->named_types.value_from(index_b);

	if (!resolve_name_structure(types, name_b))
		panic("Tried comparing incomplete type for compatibility\n"); // TODO: Figure out what to do here

	const TypeId root_type_id_a = name_a->distinct_root_type_id.rep == INVALID_TYPE_ID.rep ? type_id_a : name_a->distinct_root_type_id;

	const TypeId root_type_id_b = name_b->distinct_root_type_id.rep == INVALID_TYPE_ID.rep ? type_id_b : name_b->distinct_root_type_id;

	if (root_type_id_a.rep == root_type_id_b.rep)
		return set_assignability(root_type_id_a, is_assignable(type_id_a) && is_assignable(type_id_b));

	// Check for case 2.

	TypeName* root_name_a;

	if (name_a->distinct_root_type_id.rep == INVALID_TYPE_ID.rep)
	{
		root_name_a = name_a;
	}
	else
	{
		root_name_a = types->named_types.value_from(name_a->distinct_root_type_id.rep >> 1);

		if (!resolve_name_structure(types, root_name_a))
			panic("Tried comparing incomplete type for compatibility\n"); // TODO: Figure out what to do here
	}

	TypeName* root_name_b;

	if (name_b->distinct_root_type_id.rep == INVALID_TYPE_ID.rep)
	{
		root_name_b = name_b;
	}
	else
	{
		root_name_b = types->named_types.value_from(name_b->distinct_root_type_id.rep >> 1);

		if (!resolve_name_structure(types, root_name_b))
			panic("Tried comparing incomplete type for compatibility\n"); // TODO: Figure out what to do here
	}

	// If this is `false` we are in case 3. (incompatible types)

	if (root_name_a->structure_index == root_name_b->structure_index && root_name_a->source_id == root_name_b->source_id)
	{
		// Just select the "minimal" id as the canonical one.
		const TypeId min_id = root_type_id_a.rep < root_type_id_b.rep ? root_type_id_a : root_type_id_b;

		return set_assignability(min_id, is_assignable(type_id_a) && is_assignable(type_id_b));
	}

	// We're in case 3. (Incompatible types)

	return INVALID_TYPE_ID;
}

static MemberInfo member_info_from_type_member(const TypeMember* member, TypeMemberAst ast, TypeId surrounding_type_id, IdentifierId name, u16 rank) noexcept
{
	MemberInfo info;
	info.name = name;
	info.source = member->source;
	info.is_global = member->is_global;
	info.is_pub = member->is_pub;
	info.is_use = member->is_use;
	info.rank = rank;
	info.offset_or_global_value = member->offset_or_global_value;
	info.surrounding_type_id = surrounding_type_id;

	if (member->has_pending_type)
	{
		info.opt_type_resumption_id = member->type.resumption_id;
		info.opt_type_node_id = ast.opt_type;
		info.opt_value_node_id = ast.opt_value;
		info.opt_type = INVALID_TYPE_ID;
	}
	else
	{
		info.opt_type = member->type.id;
	}

	return info;
}

static bool find_builder_member_by_name(TypeBuilderHeader* header, IdentifierId name, TypeId type_id, FindByNameResult* out) noexcept
{
	if (header->head_offset == 0)
		return false;

	TypeBuilder* const head = type_builder_at_offset(&header->unused_, header->head_offset);

	TypeBuilder* curr = head;

	u16 rank = 0;

	while (true)
	{
		for (u32 i = 0; i != curr->used; ++i)
		{
			if (curr->names[i] == name)
			{
				out->member = curr->members + i;
				out->rank = static_cast<u16>(rank + i);
				out->surrounding_type_id = type_id;
				out->ast = curr->asts[i];

				return true;
			}
		}

		if (curr->next_offset == 0)
			return false;

		rank += static_cast<u16>(array_count(curr->names));

		curr = type_builder_at_offset(curr, curr->next_offset);
	}
}

static bool find_composite_member_by_name(TypePool* types, CompositeType* composite, IdentifierId name, TypeId type_id, bool include_use, FindByNameResult* out) noexcept
{
	TypeMember* const members = member_array_from_name_array(composite->names, composite->header.member_count);

	for (u32 i = 0; i != composite->header.member_count; ++i)
	{
		if (composite->names[i] == name)
		{
			out->member = members + i;
			out->rank = static_cast<u16>(i);
			out->surrounding_type_id = type_id;

			return true;
		}
	}

	if (!include_use)
		return false;

	// Handle `use`.

	for (u32 i = 0; i != composite->header.member_count; ++i)
	{
		if (!members[i].is_use)
			continue;

		const TypeId use_type_id = members[i].type.id;

		ASSERT_OR_IGNORE(use_type_id.rep != INVALID_TYPE_ID.rep);

		if (find_member_by_name(types, use_type_id, name, include_use, out))
			return true;
	}

	return false;
}

static bool find_member_by_name(TypePool* types, TypeId type_id, IdentifierId name, bool include_use, FindByNameResult* out) noexcept
{
	TypeName* const type_name = types->named_types.value_from(type_id.rep >> 1);

	if (!resolve_name_structure(types, type_name))
	{
		TypeBuilderHeader* const header = get_deferred_type_builder(types, type_name);

		return find_builder_member_by_name(header, name, type_id, out);
	}
	else
	{
		TypeStructure* const structure = types->structural_types.value_from(type_name->structure_index);

		if (structure->tag != TypeTag::Composite)
			panic("Tried getting member of non-composite type by name.\n");

		return find_composite_member_by_name(types, data<CompositeType>(structure), name, type_id, include_use, out);
	}
}

static bool find_builder_member_by_rank(TypeBuilderHeader* header, u16 rank, FindByRankResult* out) noexcept
{
	if (header->head_offset == 0)
		return false;

	TypeBuilder* curr = type_builder_at_offset(&header->unused_, header->head_offset);

	const u16 builder_index = rank / static_cast<u16>(array_count(curr->names));

	for (u16 i = 0; i != builder_index; ++i)
	{
		if (curr->next_offset == 0)
			return false;

		curr = type_builder_at_offset(curr, curr->next_offset);
	}

	const u16 index_in_builder = rank % array_count(curr->names);

	if (curr->used <= index_in_builder)
		return false;

	out->member = curr->members + index_in_builder;
	out->name = curr->names[index_in_builder];
	out->ast = curr->asts[index_in_builder];

	return true;
}

static bool find_composite_member_by_rank(CompositeType* composite, u16 rank, FindByRankResult* out) noexcept
{
	if (rank >= composite->header.member_count)
		return false;

	out->member = member_array_from_name_array(composite->names, composite->header.member_count) + rank;
	out->name = composite->names[rank];

	return true;
}

static bool find_member_by_rank(TypePool* types, TypeId type_id, u16 rank, FindByRankResult* out) noexcept
{
	TypeName* const name = types->named_types.value_from(type_id.rep >> 1);

	if (!resolve_name_structure(types, name))
	{
		TypeBuilderHeader* const header = get_deferred_type_builder(types, name);

		return find_builder_member_by_rank(header, rank, out);
	}
	else
	{
		TypeStructure* const structure = types->structural_types.value_from (name->structure_index);

		if (structure->tag != TypeTag::Composite)
			panic("Tried getting member of non-composite type by rank.\n");

		return find_composite_member_by_rank(data<CompositeType>(structure), rank, out);
	}
}



TypePool* create_type_pool(AllocPool* alloc, ErrorSink* errors) noexcept
{
	TypePool* const types = static_cast<TypePool*>(alloc_from_pool(alloc, sizeof(TypePool), alignof(TypePool)));

	types->structural_types.init(1 << 24, 1 << 10, 1 << 24, 1 << 9);
	types->named_types.init(1 << 26, 1 << 10, 1 << 26, 1 << 13);
	types->builders.init(1 << 15, 1 << 11);
	types->first_free_builder_index = -1;
	types->errors = errors;

	// Reserve `0 << 1` as `INVALID_TYPE_ID`,
	// `1 << 1` as `CHECKING_TYPE_ID` and
	// `2 << 1` as `NO_TYPE_TYPE_ID`.

	TypeName dummy_name{};
	dummy_name.structure_index_kind = TypeName::INVALID_STRUCTURE_INDEX;

	(void) types->named_types.index_from(dummy_name, fnv1a(range::from_object_bytes(&dummy_name)));

	dummy_name.parent_type_id = TypeId{ 1 };

	(void) types->named_types.index_from(dummy_name, fnv1a(range::from_object_bytes(&dummy_name)));

	dummy_name.parent_type_id = TypeId{ 2 };

	(void) types->named_types.index_from(dummy_name, fnv1a(range::from_object_bytes(&dummy_name)));

	return types;
}

void release_type_pool(TypePool* types) noexcept
{
	types->named_types.release();

	types->structural_types.release();

	types->builders.release();
}

const char8* tag_name(TypeTag tag) noexcept
{
	static constexpr const char8* TYPE_TAG_NAMES[] = {
		"[Unknown]",
		"Void",
		"Type",
		"Definition",
		"CompInteger",
		"CompFloat",
		"CompString",
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
	};

	u8 index = static_cast<u8>(tag);

	if (index > array_count(TYPE_TAG_NAMES))
		index = 0;

	return TYPE_TAG_NAMES[index];
}

TypeId primitive_type(TypePool* types, TypeTag tag, Range<byte> data) noexcept
{
	const u32 structure_index = types->structural_types.index_from(AttachmentRange{ data, tag }, fnv1a_step(fnv1a(data), static_cast<byte>(tag)));

	TypeName name;
	name.parent_type_id = INVALID_TYPE_ID;
	name.distinct_root_type_id = INVALID_TYPE_ID;
	name.structure_index = structure_index;
	name.structure_index_kind = TypeName::STRUCTURE_INDEX_NORMAL;
	name.source_id = INVALID_SOURCE_ID;
	name.name_id = INVALID_IDENTIFIER_ID;

	return TypeId{ types->named_types.index_from(name, fnv1a(range::from_object_bytes(&name))) << 1 };
}

TypeId alias_type(TypePool* types, TypeId aliased_type_id, bool is_distinct, SourceId source_id, IdentifierId name_id) noexcept
{
	TypeName* const aliased_ref = types->named_types.value_from(aliased_type_id.rep >> 1);

	TypeName name;
	name.parent_type_id = aliased_type_id;
	name.distinct_root_type_id = is_distinct ? INVALID_TYPE_ID : aliased_ref->distinct_root_type_id;
	name.source_id = source_id;
	name.name_id = name_id;

	if (aliased_ref->structure_index_kind == TypeName::STRUCTURE_INDEX_BUILDER)
	{
		// Right-shift by one here to preserve 29 bits of information.
		name.structure_index = aliased_type_id.rep >> 1;
		name.structure_index_kind = TypeName::STRUCTURE_INDEX_INDIRECT;
	}
	else
	{
		name.structure_index = aliased_ref->structure_index;
		name.structure_index_kind = aliased_ref->structure_index_kind;
	}

	return TypeId{ types->named_types.index_from(name, fnv1a(range::from_object_bytes(&name))) << 1 };
}

IdentifierId type_name_from_id(const TypePool* types, TypeId type_id) noexcept
{
	const TypeName* const name = types->named_types.value_from(type_id.rep >> 1);

	return name->name_id;
}

SourceId type_source_from_id(const TypePool* types, TypeId type_id) noexcept
{
	const TypeName* const name = types->named_types.value_from(type_id.rep >> 1);

	return name->source_id;
}

TypeId create_open_type(TypePool* types, SourceId source_id) noexcept
{
	TypeBuilderHeader* const header = static_cast<TypeBuilderHeader*>(alloc_type_builder(types));
	header->head_offset = 0;
	header->tail_offset = 0;
	header->total_used = 0;
	header->incomplete_member_count = 0;
	header->is_closed = false;
	header->source_id = source_id;

	TypeName name{};
	name.parent_type_id = INVALID_TYPE_ID;
	name.distinct_root_type_id = INVALID_TYPE_ID;
	name.source_id = source_id;
	name.name_id = INVALID_IDENTIFIER_ID;
	name.structure_index = type_builder_difference(reinterpret_cast<TypeBuilder*>(types->builders.begin()), &header->unused_);
	name.structure_index_kind = TypeName::STRUCTURE_INDEX_BUILDER;

	return TypeId{ types->named_types.index_from(name, fnv1a(range::from_object_bytes(&name))) << 1 };
}

void add_open_type_member(TypePool* types, TypeId open_type_id, MemberInit member) noexcept
{
	ASSERT_OR_IGNORE(member.name != INVALID_IDENTIFIER_ID);

	ASSERT_OR_IGNORE(member.offset_or_global_value < (static_cast<u64>(1) << 60));

	ASSERT_OR_IGNORE(member.type.id.rep != INVALID_TYPE_ID.rep);

	ASSERT_OR_IGNORE(!member.has_pending_type || member.opt_type_node_id != INVALID_AST_NODE_ID || member.opt_value_node_id != INVALID_AST_NODE_ID);

	TypeName* const builder_name = types->named_types.value_from(open_type_id.rep >> 1);

	if (resolve_name_structure(types, builder_name))
		panic("Passed completed type to `add_open_type_member`.\n");

	TypeBuilderHeader* const header = get_deferred_type_builder(types, builder_name);

	if (header->is_closed)
		panic("Passed non-open type to `add_open_type_member`.\n");

	if (header->total_used + 1 == UINT16_MAX)
		panic("Exceeded maximum of %u members in composite type.\n");

	FindByNameResult unused_found;

	if (find_builder_member_by_name(header, member.name, open_type_id, &unused_found))
		source_error(types->errors, member.source, "Type already has a member with the same name.\n");

	TypeBuilder* tail;

	if (header->tail_offset == 0)
	{
		tail = static_cast<TypeBuilder*>(alloc_type_builder(types));
		tail->next_offset = 0;
		tail->used = 0;

		const s32 tail_offset = type_builder_difference(&header->unused_, tail);

		header->tail_offset = tail_offset;

		header->head_offset = tail_offset;
	}
	else
	{
		tail = type_builder_at_offset(&header->unused_, header->tail_offset);

		if (tail->used == array_count(tail->members))
		{
			TypeBuilder* const new_tail = static_cast<TypeBuilder*>(alloc_type_builder(types));
			new_tail->next_offset = 0;
			new_tail->used = 0;

			tail->next_offset = type_builder_difference(tail, new_tail);

			header->tail_offset = type_builder_difference(&header->unused_, new_tail);

			tail = new_tail;
		}
	}

	ASSERT_OR_IGNORE(tail->next_offset == 0);

	ASSERT_OR_IGNORE(tail->used < array_count(tail->members));

	tail->names[tail->used] = member.name;

	TypeMember* const type_member = tail->members + tail->used;
	type_member->offset_or_global_value = member.offset_or_global_value;
	type_member->is_global = member.is_global;
	type_member->is_pub = member.is_pub;
	type_member->is_use = member.is_use;
	type_member->has_pending_type = member.has_pending_type;
	type_member->type.id = member.type.id;
	type_member->source = member.source;

	TypeMemberAst* const type_member_ast = tail->asts + tail->used;
	type_member_ast->opt_type = member.opt_type_node_id;
	type_member_ast->opt_value = member.opt_value_node_id;

	tail->used += 1;

	header->total_used += 1;

	if (member.has_pending_type)
		header->incomplete_member_count += 1;
}

void close_open_type(TypePool* types, TypeId open_type_id, u64 size, u32 align, u64 stride) noexcept
{
	TypeName* const builder_name = types->named_types.value_from(open_type_id.rep >> 1);

	if (resolve_name_structure(types, builder_name))
		panic("Passed completed type to `add_open_type_member`.\n");

	TypeBuilderHeader* const header = get_deferred_type_builder(types, builder_name);

	if (header->is_closed)
		panic("Passed non-open type to `close_open_type`\n");

	header->size = size;
	header->stride = stride;
	header->align = align;

	if (header->incomplete_member_count == 0)
	{
		builder_name->structure_index = structure_index_from_complete_type_builder(types, header);
		builder_name->structure_index_kind = TypeName::STRUCTURE_INDEX_NORMAL;

		free_type_builder(types, header);
	}
	else
	{
		header->is_closed = true;
	}
}

void set_incomplete_type_member_type_by_name(TypePool* types, TypeId open_type_id, IdentifierId member_name_id, TypeId member_type_id) noexcept
{
	ASSERT_OR_IGNORE(open_type_id.rep != INVALID_TYPE_ID.rep);

	ASSERT_OR_IGNORE(member_type_id.rep != INVALID_TYPE_ID.rep);

	TypeName* const builder_name = types->named_types.value_from(open_type_id.rep >> 1);

	if (resolve_name_structure(types, builder_name))
		panic("Passed completed type to `add_open_type_member`.\n");

	TypeBuilderHeader* const header = get_deferred_type_builder(types, builder_name);

	FindByNameResult found;

	if (!find_builder_member_by_name(header, member_name_id, open_type_id, &found))
		panic("Tried setting type of non-existent member.\n");

	if (!found.member->has_pending_type)
		panic("Tried setting type of already typed member.\n");

	found.member->has_pending_type = false;
	found.member->type.id = member_type_id;
}

void set_incomplete_type_member_type_by_rank(TypePool* types, TypeId open_type_id, u16 rank, TypeId member_type_id) noexcept
{
	ASSERT_OR_IGNORE(open_type_id.rep != INVALID_TYPE_ID.rep);

	ASSERT_OR_IGNORE(member_type_id.rep != INVALID_TYPE_ID.rep);

	TypeName* const builder_name = types->named_types.value_from(open_type_id.rep >> 1);

	if (resolve_name_structure(types, builder_name))
		panic("Passed completed type to `add_open_type_member`.\n");

	TypeBuilderHeader* const header = get_deferred_type_builder(types, builder_name);

	FindByRankResult found;

	if (!find_builder_member_by_rank(header, rank, &found))
		panic("Tried setting type of non-existent member.\n");

	if (!found.member->has_pending_type)
		panic("Tried setting type of already typed member.\n");

	found.member->has_pending_type = false;
	found.member->type.id = member_type_id;
}

TypeMetrics type_metrics_from_id(TypePool* types, TypeId type_id) noexcept
{
	TypeName* const name = types->named_types.value_from(type_id.rep >> 1);

	if (!resolve_name_structure(types, name))
		panic("Tried getting align of type that was not complete.\n");

	ASSERT_OR_IGNORE(name->structure_index_kind == TypeName::STRUCTURE_INDEX_NORMAL);

	const TypeStructure* const structure = types->structural_types.value_from(name->structure_index);

	switch (structure->tag)
	{
	case TypeTag::Void:
	case TypeTag::Func:
	case TypeTag::Builtin:
		return { 0, 0, 1 };

	case TypeTag::Boolean:
		return { 1, 1, 1 };

	case TypeTag::TypeBuilder:
	case TypeTag::Type:
	case TypeTag::TypeInfo:
		return { 4, 4, 4 };

	case TypeTag::CompInteger:
	case TypeTag::CompFloat:
	case TypeTag::Ptr:
	case TypeTag::Slice:
	case TypeTag::CompString:
		return { 8, 8, 8 };

	case TypeTag::Integer:
	case TypeTag::Float:
	{
		const u32 all = next_pow2((data<NumericType>(structure)->bits + 7) / 8);

		return { all, all, all };
	}

	case TypeTag::Composite:
	{
		const CompositeTypeHeader* const header = &data<CompositeType>(structure)->header;

		return { header->size, header->stride, header->align };
	}

	case TypeTag::Array:
	{
		const ArrayType* const array = data<ArrayType>(structure);

		const TypeMetrics element = type_metrics_from_id(types, array->element_type);

		const u64 size = array->element_count == 0 ? 0 : (array->element_count - 1) * element.stride + element.size;

		const u64 stride = array->element_count * element.stride;

		return { size, stride, element.align };
	}

	case TypeTag::Definition:
	case TypeTag::CompositeLiteral:
	case TypeTag::ArrayLiteral:
	case TypeTag::Variadic:
	case TypeTag::Divergent:
	case TypeTag::Trait:
		panic("`type_metrics_from_id` is not yet implemented for `%s`.\n", tag_name(structure->tag));
	}

	ASSERT_UNREACHABLE;
}

TypeTag type_tag_from_id(TypePool* types, TypeId type_id) noexcept
{
	TypeName* const name = types->named_types.value_from(type_id.rep >> 1);

	if (!resolve_name_structure(types, name))
		return TypeTag::Composite;

	return types->structural_types.value_from(name->structure_index)->tag;
}

bool type_can_implicitly_convert_from_to(TypePool* types, TypeId from_type_id, TypeId to_type_id) noexcept
{
	if (common_type_id_with_masked_assignability(types, from_type_id, to_type_id).rep != INVALID_TYPE_ID.rep)
		return true;

	// TODO: Check for applicable implicit conversion rules from `from` to `to`.

	return false;
}

TypeId common_type(TypePool* types, TypeId type_id_a, TypeId type_id_b) noexcept
{
	return common_type_id_with_masked_assignability(types, type_id_a, type_id_b);
}

bool type_member_info_by_name(TypePool* types, TypeId type_id, IdentifierId name, MemberInfo* out) noexcept
{
	FindByNameResult found;

	if (!find_member_by_name(types, type_id, name, true, &found))
		return false;

	*out = member_info_from_type_member(found.member, found.ast, found.surrounding_type_id, name, found.rank);

	return true;
}

bool type_member_info_by_rank(TypePool* types, TypeId type_id, u16 rank, MemberInfo* out) noexcept
{
	FindByRankResult found;

	if (!find_member_by_rank(types, type_id, rank, &found))
		return false;

	*out = member_info_from_type_member(found.member, found.ast, type_id, found.name, rank);

	return true;
}

bool signature_member_by_rank(TypePool* types, TypeId signature_type_id, u16 rank, MemberInfo* out) noexcept
{
	FindByRankResult found;

	if (!find_member_by_rank(types, signature_type_id, rank, &found))
		return false;

	*out = member_info_from_type_member(found.member, found.ast, signature_type_id, found.name, rank);

	return true;
}

const void* primitive_type_structure(TypePool* types, TypeId type_id) noexcept
{
	TypeName* const name = types->named_types.value_from(type_id.rep >> 1);

	if (resolve_name_structure(types, name))
	{
		const TypeStructure* structure = types->structural_types.value_from(name->structure_index);

		if (structure->tag != TypeTag::Composite)
			return &structure->data;
	}

	panic("Called `primitive_type_structure` with non-primitive type.\n");
}



IncompleteMemberIterator incomplete_members_of(TypePool* types, TypeId type_id) noexcept
{
	TypeName* name = types->named_types.value_from(type_id.rep >> 1);

	if (resolve_name_structure(types, name))
		return { nullptr, nullptr, 0 };

	if (name->structure_index_kind == TypeName::STRUCTURE_INDEX_INDIRECT)
		name = types->named_types.value_from(name->structure_index);

	ASSERT_OR_IGNORE(name->structure_index_kind == TypeName::STRUCTURE_INDEX_BUILDER);

	TypeBuilderHeader* const header = type_builder_header_at_index(types, name->structure_index);

	if (!header->is_closed)
		panic("Passed open type to `incomplete_members_of`\n");

	if (header->head_offset == 0)
		return { nullptr, nullptr, 0 };

	return { type_builder_at_offset(&header->unused_, header->head_offset), name, 0, type_id };
}

MemberInfo next(IncompleteMemberIterator* it) noexcept
{
	ASSERT_OR_IGNORE(it->structure != nullptr);

	ASSERT_OR_IGNORE(static_cast<TypeName*>(it->name)->structure_index_kind == TypeName::STRUCTURE_INDEX_BUILDER);

	const u16 rank = it->rank;

	it->rank += rank + 1;

	const TypeBuilder* const builder = static_cast<const TypeBuilder*>(it->structure);

	const u16 index = rank & (static_cast<u16>(array_count(builder->names)) - 1);

	return member_info_from_type_member(builder->members + index, builder->asts[index], it->type_id, builder->names[index], rank);
}

bool has_next(IncompleteMemberIterator* it) noexcept
{
	if (it->structure == nullptr)
		return false;

	if (static_cast<TypeName*>(it->name)->structure_index_kind != TypeName::STRUCTURE_INDEX_BUILDER)
	{
		it->structure = nullptr;

		return false;
	}

	TypeBuilder* builder = static_cast<TypeBuilder*>(it->structure);

	u32 curr = it->rank;

	while (true)
	{
		while (curr != builder->used)
		{
			if (builder->members[curr].has_pending_type)
				return true;

			curr += 1;
		}

		if (builder->next_offset == 0)
		{
			it->structure = nullptr;

			return false;
		}

		builder = type_builder_at_offset(builder, builder->next_offset);
	}
}

MemberIterator members_of(TypePool* types, TypeId type_id) noexcept
{
	TypeName* name = types->named_types.value_from(type_id.rep >> 1);

	if (resolve_name_structure(types, name))
	{
		ASSERT_OR_IGNORE(name->structure_index_kind == TypeName::STRUCTURE_INDEX_NORMAL);

		TypeStructure* const structure = types->structural_types.value_from(name->structure_index);

		ASSERT_OR_IGNORE(structure->tag == TypeTag::Composite);

		return { data<CompositeType>(structure), nullptr, 0, type_id };
	}
	else
	{
		if (name->structure_index_kind == TypeName::STRUCTURE_INDEX_INDIRECT)
			name = types->named_types.value_from(name->structure_index);

		ASSERT_OR_IGNORE(name->structure_index_kind == TypeName::STRUCTURE_INDEX_BUILDER);

		TypeBuilderHeader* const header = type_builder_header_at_index(types, name->structure_index);

		if (!header->is_closed)
			panic("Passed open type to `incomplete_members_of`\n");

		if (header->head_offset == 0)
			return { nullptr, nullptr, 0 };

		return { type_builder_at_offset(&header->unused_, header->tail_offset), name, 0, type_id };
	}

}

MemberInfo next(MemberIterator* it) noexcept
{
	ASSERT_OR_IGNORE(it->structure != nullptr);

	const u16 rank = it->rank;

	it->rank = rank + 1;

	if (it->name != nullptr)
	{
		TypeBuilder* const builder = static_cast<TypeBuilder*>(it->structure);

		const u32 index = rank & (array_count(builder->names) - 1);

		return member_info_from_type_member(builder->members + index, builder->asts[index], it->type_id, builder->names[index], rank);
	}
	else
	{
		CompositeType* const composite = static_cast<CompositeType*>(it->structure);

		const TypeMember* const members = member_array_from_name_array(composite->names, composite->header.member_count);

		return member_info_from_type_member(members + rank, { INVALID_AST_NODE_ID, INVALID_AST_NODE_ID }, it->type_id, composite->names[rank], rank);
	}
}

bool has_next(MemberIterator* it) noexcept
{
	if (it->structure == nullptr)
		return false;

	if (it->name != nullptr)
	{
		if (static_cast<TypeName*>(it->name)->structure_index_kind == TypeName::STRUCTURE_INDEX_NORMAL)
		{
			// TODO: Switch to newly completed TypeStructure. This is
			//       problematic due to `curr` becoming basically meaningless,
			//       meaning that member sorting might have to happen either in
			//       `complete_type_builder` or even in
			//       `add_open_type_member` using insertion sort.
			panic("TODO");
		}

		TypeBuilder* builder = static_cast<TypeBuilder*>(it->structure);

		if (it->rank == builder->used)
		{
			if (builder->next_offset == 0)
			{
				it->structure = nullptr;

				return false;
			}

			it->structure = type_builder_at_offset(builder, builder->next_offset);

			it->rank = 0;
		}

		return true;
	}
	else
	{
		CompositeType* const structure = static_cast<CompositeType*>(it->structure);

		const u16 rank = it->rank;

		ASSERT_OR_IGNORE(structure->header.member_count <= rank);

		if (structure->header.member_count == rank)
		{
			it->structure = nullptr;

			return false;
		}

		it->rank = rank;

		return true;
	}
}
