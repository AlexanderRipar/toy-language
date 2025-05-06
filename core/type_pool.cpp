#include "pass_data.hpp"

#include "../infra/hash.hpp"
#include "../infra/container.hpp"
#include "../infra/inplace_sort.hpp"

#include <cstdlib>
#include <cstring>

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

struct BuilderMember
{
	// Offset in the parent type. 0 for global members.
	s64 offset : 60;

	// `true` if this is a global member, `false` otherwise.
	s64 is_global : 1;

	// `true` if this is a public member, `false` otherwise.
	s64 is_pub : 1;

	// `true` if this member is defined with the `use` modifier, `false`
	// otherwise.
	s64 is_use : 1;

	// `true` if this member is mutable, `false` otherwise.
	s64 is_mut : 1;

	// Either a `TypeId` or an `AstNodeId` from which a type can be determined.
	// See `has_pending_type`.
	DelayableTypeId type;

	// Either a `GlobalValueId` or an `AstNodeId` from which a value can be
	// determined. See `has_pending_value`.
	DelayableValueId value;

	// Source of the Definition from which this member is derived.
	SourceId source;

	// Name of this member.
	IdentifierId name;

	// `true` if `type` holds an `AstNodeId` to be typechecked (with
	// `lexical_parent_type_id` as the context), `false` if it holds a
	// `TypeId`.
	bool has_pending_type : 1;

	// `true` if `type` holds an `AstNodeId` to be evaluated (with
	// `lexical_parent_type_id` as the context), `false` if it holds a
	// `ValueId`.
	bool has_pending_value : 1;

	// `TypeId´ of the type in which the Definition from which this member is
	// derived is located.
	TypeId lexical_parent_type_id;
};

struct CompositeMember
{
	// Offset in the parent type. 0 for global members.
	s64 offset : 60;

	// `true` if this is a global member, `false` otherwise.
	s64 is_global : 1;

	// `true` if this is a public member, `false` otherwise.
	s64 is_pub : 1;

	// `true` if this member is defined with the `use` modifier, `false`
	// otherwise.
	s64 is_use : 1;

	// `true` if this member is mutable, `false` otherwise.
	s64 is_mut : 1;

	// `TypeId` of this member.
	TypeId type_id;

	// `GlobalValueId` of the (default) value of this member.
	GlobalValueId value_id;

	// Source of the Definition from which this member is derived.
	SourceId source;

	// Name of this member.
	IdentifierId name;
};

struct FindByNameResult
{
	union
	{
		BuilderMember* incomplete;

		CompositeMember* complete;
	} member;

	bool is_complete;

	u16 rank;

	TypeId surrounding_type_id;
};

struct FindByRankResult
{
	union
	{
		BuilderMember* incomplete;

		CompositeMember* complete;
	} member;

	bool is_complete;

	IdentifierId name;
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

	// `TypeId` of the type this type is defined in.
	// For non-composite types this is `INVALID_TYPE_ID`, as the information is
	// not important and leads to meaningless duplication of `TypeName`s.
	// For composite types, if there is no surrounding type - this is only the
	// case for the signature types of builtins as well as the prelude type
	// defined in `Interpreter` and wrapped around every `_import`ed type -
	// this also is `INVALID_TYPE_ID`.
	// For type aliases, this is copied from the aliased type.
	TypeId lexical_parent_type_id;

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

	BuilderMember members[8];
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

	GlobalValuePool* globals;

	ErrorSink* errors;
};

static_assert(sizeof(TypeBuilder) == sizeof(TypeBuilderHeader));



static bool find_member_by_name(TypePool* types, TypeId type_id, IdentifierId name, bool include_use, FindByNameResult* out) noexcept;

template<typename T>
[[nodiscard]] static T* data(TypeStructure* entry) noexcept
{
	return reinterpret_cast<T*>(&entry->data);
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

static CompositeMember* composite_members(CompositeType* composite) noexcept
{
	return reinterpret_cast<CompositeMember*>(composite->names + ((composite->header.member_count + 1) & ~1));
}

static constexpr u32 composite_type_alloc_size(u32 member_count) noexcept
{
	return sizeof(CompositeTypeHeader) + ((member_count + 1) & ~1) * sizeof(IdentifierId) + member_count * sizeof(CompositeMember);
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

	CompositeMember* const members = composite_members(composite);

	while (true)
	{
		ASSERT_OR_IGNORE(curr_index + curr->used <= header->total_used);

		memcpy(names + curr_index, curr->names, curr->used * sizeof(curr->names[0]));

		for (u32 i = 0; i != curr->used; ++i)
		{
			ASSERT_OR_IGNORE(!curr->members[i].has_pending_type);

			ASSERT_OR_IGNORE(!curr->members[i].has_pending_value);

			members[curr_index  + i].offset = curr->members[i].offset;
			members[curr_index  + i].is_global = curr->members[i].is_global;
			members[curr_index  + i].is_pub = curr->members[i].is_pub;
			members[curr_index  + i].is_use = curr->members[i].is_use;
			members[curr_index  + i].is_mut = curr->members[i].is_mut;
			members[curr_index  + i].type_id = curr->members[i].type.complete;
			members[curr_index  + i].value_id = curr->members[i].value.complete;
			members[curr_index  + i].source = curr->members[i].source;
			members[curr_index  + i].name = curr->members[i].name;
		}

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

	const Range<byte> data{ reinterpret_cast<byte*>(composite), alloc_size };

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
		return false;
	}
	else
	{
		ASSERT_OR_IGNORE(name->structure_index_kind == TypeName::STRUCTURE_INDEX_INDIRECT);

		TypeName* const indirect = types->named_types.value_from(name->structure_index);

		if (indirect->structure_index_kind == TypeName::STRUCTURE_INDEX_BUILDER)
			return false;

		ASSERT_OR_IGNORE(indirect->structure_index_kind == TypeName::STRUCTURE_INDEX_NORMAL);

		name->structure_index = indirect->structure_index;
		name->structure_index_kind = indirect->structure_index_kind; // Equal to `TypeName::STRUCTURE_INDEX_NORMAL`

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

static MemberInfo member_info_from_composite_member(const CompositeMember* member, TypeId surrounding_type_id, IdentifierId name, u16 rank) noexcept
{
	MemberInfo info;
	info.name = name;
	info.source = member->source;
	info.type.complete = member->type_id;
	info.value.complete = member->value_id;
	info.is_global = member->is_global;
	info.is_pub = member->is_pub;
	info.is_use = member->is_use;
	info.is_mut = member->is_mut;
	info.has_pending_type = false;
	info.has_pending_value = false;
	info.rank = rank;
	info.surrounding_type_id = surrounding_type_id;
	info.completion_context_type_id = TypeId::INVALID;
	info.offset = member->offset;

	return info;
}

static MemberInfo member_info_from_builder_member(const BuilderMember* member, TypeId surrounding_type_id, IdentifierId name, u16 rank) noexcept
{
	MemberInfo info;
	info.name = name;
	info.source = member->source;
	info.type = member->type;
	info.value = member->value;
	info.is_global = member->is_global;
	info.is_pub = member->is_pub;
	info.is_use = member->is_use;
	info.is_mut = member->is_mut;
	info.has_pending_type = member->has_pending_type;
	info.has_pending_value = member->has_pending_value;
	info.rank = rank;
	info.surrounding_type_id = surrounding_type_id;
	info.completion_context_type_id = member->lexical_parent_type_id;
	info.offset = member->offset;

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
				out->member.incomplete = curr->members + i;
				out->is_complete = false;
				out->rank = static_cast<u16>(rank + i);
				out->surrounding_type_id = type_id;

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
	CompositeMember* const members = composite_members(composite);

	for (u32 i = 0; i != composite->header.member_count; ++i)
	{
		if (composite->names[i] == name)
		{
			out->member.complete = members + i;
			out->is_complete = true;
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

		const TypeId use_type_id = members[i].type_id;

		ASSERT_OR_IGNORE(use_type_id != TypeId::INVALID);

		const TypeTag use_type_tag = type_tag_from_id(types, use_type_id);

		if (use_type_tag == TypeTag::Type)
		{
			const TypeId defined_type_id = *static_cast<TypeId*>(global_value_from_id(types->globals, members[i].value_id).address);

			if (find_member_by_name(types, defined_type_id, name, true, out))
				return true;
		}
		else
		{
			ASSERT_OR_IGNORE(use_type_tag == TypeTag::Composite);

			if (find_member_by_name(types, use_type_id, name, true, out))
				return true;
		}
	}

	return false;
}

static bool find_member_by_name(TypePool* types, TypeId type_id, IdentifierId name, bool include_use, FindByNameResult* out) noexcept
{
	TypeName* const type_name = types->named_types.value_from(static_cast<u32>(type_id));

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

	out->member.incomplete = curr->members + index_in_builder;
	out->is_complete = false;
	out->name = curr->names[index_in_builder];

	return true;
}

static bool find_composite_member_by_rank(CompositeType* composite, u16 rank, FindByRankResult* out) noexcept
{
	if (rank >= composite->header.member_count)
		return false;

	out->member.complete = composite_members(composite) + rank;
	out->is_complete = true;
	out->name = composite->names[rank];

	return true;
}

static bool find_member_by_rank(TypePool* types, TypeId type_id, u16 rank, FindByRankResult* out) noexcept
{
	TypeName* const name = types->named_types.value_from(static_cast<u32>(type_id));

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



TypePool* create_type_pool(AllocPool* alloc, GlobalValuePool* globals, ErrorSink* errors) noexcept
{
	TypePool* const types = static_cast<TypePool*>(alloc_from_pool(alloc, sizeof(TypePool), alignof(TypePool)));

	types->structural_types.init(1 << 24, 1 << 10, 1 << 24, 1 << 9);
	types->named_types.init(1 << 26, 1 << 10, 1 << 26, 1 << 13);
	types->builders.init(1 << 15, 1 << 11);
	types->first_free_builder_index = -1;
	types->globals = globals;
	types->errors = errors;

	// Reserve `0` as `TypeId::INVALID`, `1` as `TypeId::CHECKING` and `2` as
	// `TypeId::NO_TYPE`.

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


TypeId simple_type(TypePool* types, TypeTag tag, Range<byte> data) noexcept
{
	const u32 structure_index = types->structural_types.index_from(AttachmentRange{ data, tag }, fnv1a_step(fnv1a(data), static_cast<byte>(tag)));

	TypeName name;
	name.parent_type_id = TypeId::INVALID;
	name.distinct_root_type_id = TypeId::INVALID;
	name.structure_index = structure_index;
	name.structure_index_kind = TypeName::STRUCTURE_INDEX_NORMAL;
	name.source_id = SourceId::INVALID;
	name.name_id = INVALID_IDENTIFIER_ID;
	name.lexical_parent_type_id = TypeId::INVALID;

	return TypeId{ types->named_types.index_from(name, fnv1a(range::from_object_bytes(&name))) };
}

TypeId alias_type(TypePool* types, TypeId aliased_type_id, bool is_distinct, SourceId source_id, IdentifierId name_id) noexcept
{
	TypeName* const aliased_ref = types->named_types.value_from(static_cast<u32>(aliased_type_id));

	TypeName name;
	name.parent_type_id = aliased_type_id;
	name.distinct_root_type_id = is_distinct ? TypeId::INVALID : aliased_ref->distinct_root_type_id;
	name.source_id = source_id;
	name.name_id = name_id;
	name.lexical_parent_type_id = aliased_ref->lexical_parent_type_id;

	if (aliased_ref->structure_index_kind == TypeName::STRUCTURE_INDEX_BUILDER)
	{
		name.structure_index = static_cast<u32>(aliased_type_id);
		name.structure_index_kind = TypeName::STRUCTURE_INDEX_INDIRECT;
	}
	else
	{
		name.structure_index = aliased_ref->structure_index;
		name.structure_index_kind = aliased_ref->structure_index_kind;
	}

	return TypeId{ types->named_types.index_from(name, fnv1a(range::from_object_bytes(&name))) };
}

TypeId create_open_type(TypePool* types, TypeId lexical_parent_type_id, SourceId source_id) noexcept
{
	TypeBuilderHeader* const header = static_cast<TypeBuilderHeader*>(alloc_type_builder(types));
	header->head_offset = 0;
	header->tail_offset = 0;
	header->total_used = 0;
	header->incomplete_member_count = 0;
	header->is_closed = false;
	header->source_id = source_id;

	TypeName name{};
	name.parent_type_id = TypeId::INVALID;
	name.distinct_root_type_id = TypeId::INVALID;
	name.structure_index = type_builder_difference(reinterpret_cast<TypeBuilder*>(types->builders.begin()), &header->unused_);
	name.structure_index_kind = TypeName::STRUCTURE_INDEX_BUILDER;
	name.source_id = source_id;
	name.name_id = INVALID_IDENTIFIER_ID;
	name.lexical_parent_type_id = lexical_parent_type_id;

	return TypeId{ types->named_types.index_from(name, fnv1a(range::from_object_bytes(&name))) };
}

void add_open_type_member(TypePool* types, TypeId open_type_id, MemberInit init) noexcept
{
	ASSERT_OR_IGNORE(init.name != INVALID_IDENTIFIER_ID);

	ASSERT_OR_IGNORE(init.offset < (static_cast<s64>(1) << 59) && init.offset >= -(static_cast<s64>(1) << 59));

	ASSERT_OR_IGNORE(init.type.pending != AstNodeId::INVALID || init.value.pending != AstNodeId::INVALID);

	TypeName* const builder_name = types->named_types.value_from(static_cast<u32>(open_type_id));

	if (resolve_name_structure(types, builder_name))
		panic("Passed completed type to `add_open_type_member`.\n");

	TypeBuilderHeader* const header = get_deferred_type_builder(types, builder_name);

	if (header->is_closed)
		panic("Passed non-open type to `add_open_type_member`.\n");

	if (header->total_used + 1 == UINT16_MAX)
		panic("Exceeded maximum of %u members in composite type.\n");

	FindByNameResult unused_found;

	if (find_builder_member_by_name(header, init.name, open_type_id, &unused_found))
		source_error(types->errors, init.source, "Type already has a member with the same name.\n");

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

	tail->names[tail->used] = init.name;

	BuilderMember* const member = tail->members + tail->used;
	member->offset = init.offset;
	member->is_global = init.is_global;
	member->is_pub = init.is_pub;
	member->is_use = init.is_use;
	member->is_mut = init.is_mut;
	member->type = init.type;
	member->value = init.value;
	member->source = init.source;
	member->name = init.name;
	member->has_pending_type = init.has_pending_type;
	member->has_pending_value = init.has_pending_value;
	member->lexical_parent_type_id = init.lexical_parent_type_id;

	tail->used += 1;

	header->total_used += 1;

	if (init.has_pending_type || init.has_pending_value)
		header->incomplete_member_count += 1;
}

void close_open_type(TypePool* types, TypeId open_type_id, u64 size, u32 align, u64 stride) noexcept
{
	TypeName* const builder_name = types->named_types.value_from(static_cast<u32>(open_type_id));

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

void set_incomplete_type_member_type_by_rank(TypePool* types, TypeId open_type_id, u16 rank, TypeId member_type_id) noexcept
{
	ASSERT_OR_IGNORE(open_type_id != TypeId::INVALID);

	ASSERT_OR_IGNORE(member_type_id != TypeId::INVALID);

	TypeName* const builder_name = types->named_types.value_from(static_cast<u32>(open_type_id));

	if (resolve_name_structure(types, builder_name))
		panic("Passed completed type to `add_open_type_member`.\n");

	TypeBuilderHeader* const header = get_deferred_type_builder(types, builder_name);

	FindByRankResult found;

	if (!find_builder_member_by_rank(header, rank, &found))
		panic("Tried setting type of non-existent member.\n");

	if (!found.member.incomplete->has_pending_type)
		panic("Tried setting type of already typed member.\n");

	found.member.incomplete->has_pending_type = false;
	found.member.incomplete->type.complete = member_type_id;

	ASSERT_OR_IGNORE(header->incomplete_member_count != 0);

	if (!found.member.incomplete->has_pending_value)
	{
		header->incomplete_member_count -= 1;

		if (header->is_closed && header->incomplete_member_count == 0)
		{
			builder_name->structure_index = structure_index_from_complete_type_builder(types, header);
			builder_name->structure_index_kind = TypeName::STRUCTURE_INDEX_NORMAL;
		}
	}
}

void set_incomplete_type_member_value_by_rank(TypePool* types, TypeId open_type_id, u16 rank, GlobalValueId member_value_id) noexcept
{
	ASSERT_OR_IGNORE(open_type_id != TypeId::INVALID);

	ASSERT_OR_IGNORE(member_value_id != GlobalValueId::INVALID);

	TypeName* const builder_name = types->named_types.value_from(static_cast<u32>(open_type_id));

	if (resolve_name_structure(types, builder_name))
		panic("Passed completed type to `add_open_type_member`.\n");

	TypeBuilderHeader* const header = get_deferred_type_builder(types, builder_name);

	FindByRankResult found;

	if (!find_builder_member_by_rank(header, rank, &found))
		panic("Tried setting type of non-existent member.\n");

	if (!found.member.incomplete->has_pending_value)
		panic("Tried setting value of already evaluated member.\n");

	found.member.incomplete->has_pending_value = false;
	found.member.incomplete->value.complete = member_value_id;

	ASSERT_OR_IGNORE(header->incomplete_member_count != 0);

	header->incomplete_member_count -= 1;

	if (header->is_closed && header->incomplete_member_count == 0)
	{
		builder_name->structure_index = structure_index_from_complete_type_builder(types, header);
		builder_name->structure_index_kind = TypeName::STRUCTURE_INDEX_NORMAL;
	}
}


bool is_same_type(TypePool* types, TypeId type_id_a, TypeId type_id_b) noexcept
{
	if (type_id_a == type_id_b)
		return true;

	TypeName* const name_a = types->named_types.value_from(static_cast<u32>(type_id_a));

	TypeName* const name_b = types->named_types.value_from(static_cast<u32>(type_id_b));

	if (!resolve_name_structure(types, name_a) || !resolve_name_structure(types, name_b))
		return false; // TODO: In this case we actually just don't know.

	if (name_a->structure_index_kind != name_b->structure_index_kind && name_a->structure_index != name_b->structure_index && name_a->source_id == name_b->source_id)
		return false;

	if (name_a->distinct_root_type_id == TypeId::INVALID && name_b->distinct_root_type_id == TypeId::INVALID)
		return true;

	if (name_a->distinct_root_type_id == TypeId::INVALID || name_b->distinct_root_type_id == TypeId::INVALID)
		return false;

	return is_same_type(types, name_a->distinct_root_type_id, name_b->distinct_root_type_id);
}

bool type_can_implicitly_convert_from_to(TypePool* types, TypeId from_type_id, TypeId to_type_id) noexcept
{
	if (common_type(types, from_type_id, to_type_id) != TypeId::INVALID)
		return true;

	const TypeTag from_type_tag = type_tag_from_id(types, from_type_id);

	const TypeTag to_type_tag = type_tag_from_id(types, to_type_id);

	switch (to_type_tag)
	{
	case TypeTag::Integer:
	{
		return from_type_tag == TypeTag::CompInteger;
	}

	case TypeTag::Float:
	{
		return from_type_tag == TypeTag::CompFloat;
	}

	case TypeTag::Slice:
	{
		const ReferenceType* const to_type = static_cast<const ReferenceType*>(simple_type_structure_from_id(types, to_type_id));

		if (from_type_tag == TypeTag::Array)
		{
			const ArrayType* const from_type = static_cast<const ArrayType*>(simple_type_structure_from_id(types, from_type_id));

			return common_type(types, to_type->referenced_type_id, from_type->element_type) != TypeId::INVALID;
		}
		else if (from_type_tag == TypeTag::Slice)
		{
			const ReferenceType* const from_type = static_cast<const ReferenceType*>(simple_type_structure_from_id(types, from_type_id));

			if (to_type->is_mut && !from_type->is_mut)
				return false;

			return common_type(types, to_type->referenced_type_id, from_type->referenced_type_id) != TypeId::INVALID;
		}
		else
		{
			return false;
		}
	}

	case TypeTag::Ptr:
	{
		if (from_type_tag != TypeTag::Ptr)
			return false;

		const ReferenceType* const to_type = static_cast<const ReferenceType*>(simple_type_structure_from_id(types, to_type_id));

		const ReferenceType* const from_type = static_cast<const ReferenceType*>(simple_type_structure_from_id(types, from_type_id));

		if (to_type->is_mut && !from_type->is_mut)
			return false;

		if (!to_type->is_opt && from_type->is_opt)
			return false;

		if (to_type->is_multi && !from_type->is_multi)
			return false;

		return common_type(types, to_type->referenced_type_id, from_type->referenced_type_id) != TypeId::INVALID;
	}

	case TypeTag::Divergent:
	case TypeTag::TypeInfo:
	{
		return true;
	}

	case TypeTag::Void:
	case TypeTag::Type:
	case TypeTag::Definition:
	case TypeTag::CompInteger:
	case TypeTag::CompFloat:
	case TypeTag::Boolean:
	case TypeTag::Array:
	case TypeTag::Func:
	case TypeTag::Builtin:
	case TypeTag::Composite:
	case TypeTag::CompositeLiteral:
	case TypeTag::ArrayLiteral:
	case TypeTag::TypeBuilder:
	case TypeTag::Variadic:
	case TypeTag::Trait:
	{
		return false;
	}

	default:
		ASSERT_UNREACHABLE;
	}
}

TypeId common_type(TypePool* types, TypeId type_id_a, TypeId type_id_b) noexcept
{
	ASSERT_OR_IGNORE(type_id_a != TypeId::INVALID);

	ASSERT_OR_IGNORE(type_id_b != TypeId::INVALID);

	// Check the common case. If the ids themselves are equal, we have a match
	// already.

	if (type_id_a == type_id_b)
		return type_id_a;

	// Since the ids were not equal, there are a few cases left:
	// 1. The types are aliases with the same `distinct_root_type_id`; In this
	//    case, we have a match.
	// 2. The types refer to different `distinct_root_type_id`s, but the two
	//    roots are really one. This happens when a partially complete type is
	//    constructed twice before being completed and is a rare case.
	// 3. The types are not compatible.

	// Check for case 1.

	TypeName* const name_a = types->named_types.value_from(static_cast<u32>(type_id_a));

	if (!resolve_name_structure(types, name_a))
		panic("Tried comparing incomplete type for compatibility\n"); // TODO: Figure out what to do here

	TypeName* const name_b = types->named_types.value_from(static_cast<u32>(type_id_b));

	if (!resolve_name_structure(types, name_b))
		panic("Tried comparing incomplete type for compatibility\n"); // TODO: Figure out what to do here

	const TypeId root_type_id_a = name_a->distinct_root_type_id == TypeId::INVALID ? type_id_a : name_a->distinct_root_type_id;

	const TypeId root_type_id_b = name_b->distinct_root_type_id == TypeId::INVALID ? type_id_b : name_b->distinct_root_type_id;

	if (root_type_id_a == root_type_id_b)
		return root_type_id_a;

	// Check for case 2.

	TypeName* root_name_a;

	if (name_a->distinct_root_type_id == TypeId::INVALID)
	{
		root_name_a = name_a;
	}
	else
	{
		root_name_a = types->named_types.value_from(static_cast<u32>(name_a->distinct_root_type_id));

		if (!resolve_name_structure(types, root_name_a))
			panic("Tried comparing incomplete type for compatibility\n"); // TODO: Figure out what to do here
	}

	TypeName* root_name_b;

	if (name_b->distinct_root_type_id == TypeId::INVALID)
	{
		root_name_b = name_b;
	}
	else
	{
		root_name_b = types->named_types.value_from(static_cast<u32>(name_b->distinct_root_type_id));

		if (!resolve_name_structure(types, root_name_b))
			panic("Tried comparing incomplete type for compatibility\n"); // TODO: Figure out what to do here
	}

	// If this is `false` we are in case 3. (incompatible types)

	if (root_name_a->structure_index == root_name_b->structure_index && root_name_a->source_id == root_name_b->source_id)
	{
		// Just select the "minimal" id as the canonical one.
		const TypeId min_id = static_cast<u32>(root_type_id_a) < static_cast<u32>(root_type_id_b) ? root_type_id_a : root_type_id_b;

		return min_id;
	}

	// We're in case 3. (Incompatible types)

	return TypeId::INVALID;
}


IdentifierId type_name_from_id(const TypePool* types, TypeId type_id) noexcept
{
	const TypeName* const name = types->named_types.value_from(static_cast<u32>(type_id));

	return name->name_id;
}

SourceId type_source_from_id(const TypePool* types, TypeId type_id) noexcept
{
	const TypeName* const name = types->named_types.value_from(static_cast<u32>(type_id));

	return name->source_id;
}

TypeId lexical_parent_type_from_id(const TypePool* types, TypeId type_id) noexcept
{
	const TypeName* const name = types->named_types.value_from(static_cast<u32>(type_id));

	return name->lexical_parent_type_id;
}

TypeMetrics type_metrics_from_id(TypePool* types, TypeId type_id) noexcept
{
	TypeName* const name = types->named_types.value_from(static_cast<u32>(type_id));

	if (!resolve_name_structure(types, name))
	{
		const TypeBuilderHeader* const header = get_deferred_type_builder(types, name);

		if (!header->is_closed)
			panic("Tried getting align of type that was not closed.\n");

		return { header->size, header->stride, header->align };
	}

	ASSERT_OR_IGNORE(name->structure_index_kind == TypeName::STRUCTURE_INDEX_NORMAL);

	TypeStructure* const structure = types->structural_types.value_from(name->structure_index);

	switch (structure->tag)
	{
	case TypeTag::Void:
	case TypeTag::Builtin:
		return { 0, 0, 1 };

	case TypeTag::Boolean:
		return { 1, 1, 1 };

	case TypeTag::TypeBuilder:
	case TypeTag::Type:
	case TypeTag::TypeInfo:
	case TypeTag::Definition:
		return { 4, 4, 4 };

	case TypeTag::CompInteger:
	case TypeTag::CompFloat:
	case TypeTag::Ptr:
	case TypeTag::Func:
		return { 8, 8, 8 };

	case TypeTag::Slice:
		return { 16, 16, 8 };

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

	case TypeTag::CompositeLiteral:
	case TypeTag::ArrayLiteral:
	case TypeTag::Variadic:
	case TypeTag::Divergent:
	case TypeTag::Trait:
		panic("`type_metrics_from_id` is not yet implemented for `%s`.\n", tag_name(structure->tag));

	case TypeTag::INVALID:
		ASSERT_UNREACHABLE;
	}

	ASSERT_UNREACHABLE;
}

TypeTag type_tag_from_id(TypePool* types, TypeId type_id) noexcept
{
	TypeName* const name = types->named_types.value_from(static_cast<u32>(type_id));

	if (!resolve_name_structure(types, name))
		return TypeTag::Composite;

	return types->structural_types.value_from(name->structure_index)->tag;
}

bool type_member_info_by_name(TypePool* types, TypeId type_id, IdentifierId name, MemberInfo* out) noexcept
{
	FindByNameResult found;

	if (!find_member_by_name(types, type_id, name, true, &found))
		return false;

	if (found.is_complete)
		*out = member_info_from_composite_member(found.member.complete, found.surrounding_type_id, name, found.rank);
	else
		*out = member_info_from_builder_member(found.member.incomplete, found.surrounding_type_id, name, found.rank);

	return true;
}

bool type_member_info_by_rank(TypePool* types, TypeId type_id, u16 rank, MemberInfo* out) noexcept
{
	FindByRankResult found;

	if (!find_member_by_rank(types, type_id, rank, &found))
		return false;

	if (found.is_complete)
		*out = member_info_from_composite_member(found.member.complete, type_id, found.name, rank);
	else
		*out = member_info_from_builder_member(found.member.incomplete, type_id, found.name, rank);

	return true;
}

const void* simple_type_structure_from_id(TypePool* types, TypeId type_id) noexcept
{
	TypeName* const name = types->named_types.value_from(static_cast<u32>(type_id));

	if (resolve_name_structure(types, name))
	{
		const TypeStructure* structure = types->structural_types.value_from(name->structure_index);

		if (structure->tag != TypeTag::Composite)
			return &structure->data;
	}

	panic("Called `simple_type_structure_from_id` with non-primitive type.\n");
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
	};

	u8 index = static_cast<u8>(tag);

	if (index > array_count(TYPE_TAG_NAMES))
		index = 0;

	return TYPE_TAG_NAMES[index];
}


IncompleteMemberIterator incomplete_members_of(TypePool* types, TypeId type_id) noexcept
{
	TypeName* name = types->named_types.value_from(static_cast<u32>(type_id));

	if (resolve_name_structure(types, name))
		return { nullptr, nullptr, 0, TypeId::INVALID };

	if (name->structure_index_kind == TypeName::STRUCTURE_INDEX_INDIRECT)
		name = types->named_types.value_from(name->structure_index);

	ASSERT_OR_IGNORE(name->structure_index_kind == TypeName::STRUCTURE_INDEX_BUILDER);

	TypeBuilderHeader* const header = type_builder_header_at_index(types, name->structure_index);

	if (!header->is_closed)
		panic("Passed open type to `incomplete_members_of`\n");

	if (header->head_offset == 0)
		return { nullptr, nullptr, 0, TypeId::INVALID };

	u16 rank = 0;

	TypeBuilder* builder = type_builder_at_offset(&header->unused_, header->head_offset);

	while (rank != header->total_used)
	{
		const u16 index = rank & (array_count(builder->members) - 1);

		const BuilderMember* const member = builder->members + index;

		if (member->has_pending_type || member->has_pending_value)
			break;

		rank += 1;

		if (static_cast<u16>(index + 1) == array_count(builder->members) - 1)
		{
			if (builder->next_offset == 0)
				return { nullptr, nullptr, 0, TypeId::INVALID };
			else
				builder = type_builder_at_offset(builder, builder->next_offset);
		}
	}

	return { builder, name, rank, type_id };
}

MemberInfo next(IncompleteMemberIterator* it) noexcept
{
	ASSERT_OR_IGNORE(it->structure != nullptr);

	ASSERT_OR_IGNORE(static_cast<TypeName*>(it->name)->structure_index_kind == TypeName::STRUCTURE_INDEX_BUILDER);

	const u16 rank = it->rank;

	it->rank = rank + 1;

	TypeBuilder* const builder = static_cast<TypeBuilder*>(it->structure);

	const u16 index = rank & static_cast<u16>(array_count(builder->names) - 1);

	if (static_cast<u16>(index + 1) == builder->used)
	{
		if (builder->next_offset == 0)
			it->structure = nullptr;
		else
			it->structure = type_builder_at_offset(builder, builder->next_offset);
	}
	return member_info_from_builder_member(builder->members + index, it->type_id, builder->names[index], rank);
}

bool has_next(const IncompleteMemberIterator* it) noexcept
{
	return static_cast<TypeName*>(it->name)->structure_index_kind == TypeName::STRUCTURE_INDEX_BUILDER
	    && it->structure != nullptr;
}

MemberIterator members_of(TypePool* types, TypeId type_id) noexcept
{
	TypeName* name = types->named_types.value_from(static_cast<u32>(type_id));

	if (resolve_name_structure(types, name))
	{
		ASSERT_OR_IGNORE(name->structure_index_kind == TypeName::STRUCTURE_INDEX_NORMAL);

		TypeStructure* const structure = types->structural_types.value_from(name->structure_index);

		ASSERT_OR_IGNORE(structure->tag == TypeTag::Composite);

		CompositeType* const composite = data<CompositeType>(structure);

		if (composite->header.member_count == 0)
			return { nullptr, nullptr, 0, TypeId::INVALID };

		return { composite, nullptr, 0, type_id };
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
			return { nullptr, nullptr, 0, TypeId::INVALID };

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

		const BuilderMember* const member = builder->members + index;

		const IdentifierId name = builder->names[index];

		if (index + 1 == builder->used)
		{
			if (builder->next_offset == 0)
				it->structure = nullptr;
			else
				it->structure = type_builder_at_offset(builder, builder->next_offset);
		}

		return member_info_from_builder_member(member, it->type_id, name, rank);
	}
	else
	{
		CompositeType* const composite = static_cast<CompositeType*>(it->structure);

		const CompositeMember* const members = composite_members(composite);

		const IdentifierId name = composite->names[rank];

		if (static_cast<u16>(rank + 1) == composite->header.member_count)
			it->structure = nullptr;

		return member_info_from_composite_member(members + rank, it->type_id, name, rank);
	}
}

bool has_next(const MemberIterator* it) noexcept
{
	return it->structure != nullptr;
}
