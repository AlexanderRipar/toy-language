#include "core.hpp"

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

struct FindResult
{
	Member* member;
	
	TypeId surrounding_type_id;

	TypeDisposition disposition;

	bool surrounding_type_is_complete;
};

struct CompositeTypeHeader
{
	u64 size;

	u64 stride;

	u32 align;

	u16 member_count;

	TypeDisposition disposition;
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

struct TypeBuilder
{
	s32 next_offset;

	u32 used;

	Member members[8];
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

		TypeDisposition disposition;

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
	// `IdentifierId::INVALID`.
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

struct TypePool
{
	IndexMap<AttachmentRange<byte, TypeTag>, TypeStructure> structural_types;

	IndexMap<TypeName, TypeName> named_types;

	ReservedVec2<u64> builders;

	s32 first_free_builder_index;

	GlobalValuePool* globals;

	ErrorSink* errors;

	MutRange<byte> memory;
};

static_assert(sizeof(TypeBuilder) == sizeof(TypeBuilderHeader));



static bool find_member_by_name(TypePool* types, TypeId type_id, IdentifierId name, SourceId source, FindResult* out) noexcept;

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

static constexpr u32 composite_type_alloc_size(u32 member_count) noexcept
{
	return sizeof(CompositeTypeHeader) + member_count * sizeof(Member);
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
	composite->header.member_count = static_cast<u16>(header->total_used);
	composite->header.disposition = header->disposition;

	// Initialize `composite->members`

	const TypeBuilder* curr = type_builder_at_offset(&header->unused_, header->head_offset);

	u32 i = 0;

	while (true)
	{
		ASSERT_OR_IGNORE(i + curr->used <= header->total_used);

		memcpy(composite->members + i, curr->members, curr->used * sizeof(Member));

		i += curr->used;

		if (curr->next_offset == 0)
			break;

		curr = type_builder_at_offset(curr, curr->next_offset);
	}

	ASSERT_OR_IGNORE(i == header->total_used);

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

static bool find_builder_member_by_name(TypeBuilderHeader* header, IdentifierId name, SourceId source, TypeId type_id, FindResult* out) noexcept
{
	if (header->head_offset == 0)
		return false;

	TypeBuilder* const head = type_builder_at_offset(&header->unused_, header->head_offset);

	TypeBuilder* curr = head;

	while (true)
	{
		for (u32 i = 0; i != curr->used; ++i)
		{
			if (curr->members[i].name == name)
			{
				if (header->disposition == TypeDisposition::Block && curr->members[i].source > source)
					return false;

				out->member = curr->members + i;
				out->surrounding_type_id = type_id;
				out->disposition = header->disposition;
				out->surrounding_type_is_complete = false;

				return true;
			}
		}

		if (curr->next_offset == 0)
			return false;

		curr = type_builder_at_offset(curr, curr->next_offset);
	}
}

static bool find_composite_member_by_name(TypePool* types, CompositeType* composite, IdentifierId name, SourceId source, TypeId type_id, FindResult* out) noexcept
{
	for (u32 i = 0; i != composite->header.member_count; ++i)
	{
		if (composite->members[i].name == name)
		{
			if (composite->header.disposition == TypeDisposition::Block && composite->members[i].source > source)
				return false;

			out->member = composite->members + i;
			out->surrounding_type_id = type_id;
			out->disposition = composite->header.disposition;
			out->surrounding_type_is_complete = true;

			return true;
		}
	}

	// Handle `use`.

	for (u32 i = 0; i != composite->header.member_count; ++i)
	{
		if (!composite->members[i].is_use)
			continue;

		if (composite->header.disposition == TypeDisposition::Block && composite->members[i].source > source)
			return false;

		const TypeId use_type_id = composite->members[i].type.complete;

		ASSERT_OR_IGNORE(use_type_id != TypeId::INVALID);

		const TypeTag use_type_tag = type_tag_from_id(types, use_type_id);

		if (use_type_tag == TypeTag::Type)
		{
			Range<byte> global_value = global_value_get(types->globals, composite->members[i].value.complete);

			ASSERT_OR_IGNORE(global_value.count() == sizeof(TypeId));

			const TypeId defined_type_id = *reinterpret_cast<const TypeId*>(global_value.begin());

			// We can pass on source here, as we know `defined_type_id`, being
			// a member, cannot refer to a block type.
			if (find_member_by_name(types, defined_type_id, name, source, out))
				return true;
		}
		else
		{
			ASSERT_OR_IGNORE(use_type_tag == TypeTag::Composite);

			// Again, just pass on source, we know it'll get ignored.
			if (find_member_by_name(types, use_type_id, name, source, out))
				return true;
		}
	}

	return false;
}

static bool find_member_by_name(TypePool* types, TypeId type_id, IdentifierId name, SourceId source, FindResult* out) noexcept
{
	TypeName* const type_name = types->named_types.value_from(static_cast<u32>(type_id));

	if (!resolve_name_structure(types, type_name))
	{
		TypeBuilderHeader* const header = get_deferred_type_builder(types, type_name);

		return find_builder_member_by_name(header, name, source, type_id, out);
	}
	else
	{
		TypeStructure* const structure = types->structural_types.value_from(type_name->structure_index);

		if (structure->tag != TypeTag::Composite)
			panic("Tried getting member of non-composite type by name.\n");

		return find_composite_member_by_name(types, data<CompositeType>(structure), name, source, type_id, out);
	}
}

static void find_builder_member_by_rank(TypeBuilderHeader* header, u16 rank, FindResult* out) noexcept
{
	ASSERT_OR_IGNORE(header->head_offset != 0);

	TypeBuilder* curr = type_builder_at_offset(&header->unused_, header->head_offset);

	const u16 builder_index = rank / static_cast<u16>(array_count(curr->members));

	for (u16 i = 0; i != builder_index; ++i)
	{
		ASSERT_OR_IGNORE(curr->next_offset != 0);

		curr = type_builder_at_offset(curr, curr->next_offset);
	}

	const u16 index_in_builder = rank % array_count(curr->members);

	ASSERT_OR_IGNORE(index_in_builder < curr->used);

	out->member = curr->members + index_in_builder;
	out->surrounding_type_is_complete = false;
	out->disposition = header->disposition;
}

static void find_composite_member_by_rank(CompositeType* composite, u16 rank, FindResult* out) noexcept
{
	ASSERT_OR_IGNORE(rank < composite->header.member_count);

	out->member = composite->members + rank;
	out->surrounding_type_is_complete = true;
	out->disposition = composite->header.disposition;
}

static void find_member_by_rank(TypePool* types, TypeId type_id, u16 rank, FindResult* out) noexcept
{
	TypeName* const name = types->named_types.value_from(static_cast<u32>(type_id));

	out->surrounding_type_id = type_id;

	if (!resolve_name_structure(types, name))
	{
		TypeBuilderHeader* const header = get_deferred_type_builder(types, name);

		find_builder_member_by_rank(header, rank, out);
	}
	else
	{
		TypeStructure* const structure = types->structural_types.value_from(name->structure_index);

		if (structure->tag != TypeTag::Composite)
			panic("Tried getting member of non-composite type by rank.\n");

		find_composite_member_by_rank(data<CompositeType>(structure), rank, out);
	}
}



TypePool* create_type_pool(AllocPool* alloc, GlobalValuePool* globals, ErrorSink* errors) noexcept
{
	static constexpr u64 BUILDERS_SIZE = (static_cast<u64>(1) << 15) * sizeof(u64);

	byte* const memory = static_cast<byte*>(minos::mem_reserve(BUILDERS_SIZE));

	if (memory == nullptr)
		panic("Could not reserve memory for TypePool (0x%X).\n", minos::last_error());

	TypePool* const types = static_cast<TypePool*>(alloc_from_pool(alloc, sizeof(TypePool), alignof(TypePool)));

	types->structural_types.init(1 << 24, 1 << 10, 1 << 24, 1 << 9);
	types->named_types.init(1 << 26, 1 << 10, 1 << 26, 1 << 13);
	types->builders.init(MutRange<byte>{ memory, BUILDERS_SIZE }, 1 << 11);
	types->first_free_builder_index = -1;
	types->globals = globals;
	types->errors = errors;
	types->memory = MutRange<byte>{ memory, BUILDERS_SIZE };

	// Reserve `0` as `TypeId::INVALID` and `1` as `TypeId::CHECKING`

	TypeName dummy_name{};
	dummy_name.structure_index_kind = TypeName::INVALID_STRUCTURE_INDEX;

	(void) types->named_types.index_from(dummy_name, fnv1a(range::from_object_bytes(&dummy_name)));

	dummy_name.parent_type_id = static_cast<TypeId>(1);

	(void) types->named_types.index_from(dummy_name, fnv1a(range::from_object_bytes(&dummy_name)));

	dummy_name.parent_type_id = static_cast<TypeId>(2);

	(void) types->named_types.index_from(dummy_name, fnv1a(range::from_object_bytes(&dummy_name)));

	return types;
}

void release_type_pool(TypePool* types) noexcept
{
	types->named_types.release();

	types->structural_types.release();

	minos::mem_unreserve(types->memory.begin(), types->memory.count());
}


TypeId simple_type(TypePool* types, TypeTag tag, Range<byte> data) noexcept
{
	ASSERT_OR_IGNORE(tag != TypeTag::Composite);

	const u32 structure_index = types->structural_types.index_from(AttachmentRange{ data, tag }, fnv1a_step(fnv1a(data), static_cast<byte>(tag)));

	TypeName name;
	name.parent_type_id = TypeId::INVALID;
	name.distinct_root_type_id = TypeId::INVALID;
	name.structure_index = structure_index;
	name.structure_index_kind = TypeName::STRUCTURE_INDEX_NORMAL;
	name.source_id = SourceId::INVALID;
	name.name_id = IdentifierId::INVALID;
	name.lexical_parent_type_id = TypeId::INVALID;

	return static_cast<TypeId>(types->named_types.index_from(name, fnv1a(range::from_object_bytes(&name))));
}

TypeId alias_type(TypePool* types, TypeId aliased_type_id, bool is_distinct, SourceId source_id, IdentifierId name_id) noexcept
{
	ASSERT_OR_IGNORE(aliased_type_id != TypeId::INVALID);

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

	return static_cast<TypeId>(types->named_types.index_from(name, fnv1a(range::from_object_bytes(&name))));
}

TypeId create_open_type(TypePool* types, TypeId lexical_parent_type_id, SourceId source_id, TypeDisposition disposition) noexcept
{
	ASSERT_OR_IGNORE(disposition != TypeDisposition::INVALID);

	TypeBuilderHeader* const header = static_cast<TypeBuilderHeader*>(alloc_type_builder(types));
	header->head_offset = 0;
	header->tail_offset = 0;
	header->total_used = 0;
	header->incomplete_member_count = 0;
	header->is_closed = false;
	header->disposition = disposition;
	header->source_id = source_id;
	header->align = 1;
	header->size = 0;

	TypeName name{};
	name.parent_type_id = TypeId::INVALID;
	name.distinct_root_type_id = TypeId::INVALID;
	name.structure_index = type_builder_difference(reinterpret_cast<TypeBuilder*>(types->builders.begin()), &header->unused_);
	name.structure_index_kind = TypeName::STRUCTURE_INDEX_BUILDER;
	name.source_id = source_id;
	name.name_id = IdentifierId::INVALID;
	name.lexical_parent_type_id = lexical_parent_type_id;

	return static_cast<TypeId>(types->named_types.index_from(name, fnv1a(range::from_object_bytes(&name))));
}

void add_open_type_member(TypePool* types, TypeId open_type_id, Member member) noexcept
{
	ASSERT_OR_IGNORE(open_type_id != TypeId::INVALID);

	ASSERT_OR_IGNORE(member.name != IdentifierId::INVALID);

	ASSERT_OR_IGNORE(member.rank == 0);

	ASSERT_OR_IGNORE(!member.is_param);

	// There is either a pending type (derived from type or value), or a
	// complete one.
	ASSERT_OR_IGNORE(member.has_pending_type || member.type.complete != TypeId::INVALID);

	TypeName* const builder_name = types->named_types.value_from(static_cast<u32>(open_type_id));

	if (resolve_name_structure(types, builder_name))
		panic("Passed completed type to `add_open_type_member`.\n");

	TypeBuilderHeader* const header = get_deferred_type_builder(types, builder_name);

	if (header->disposition == TypeDisposition::Signature || header->disposition == TypeDisposition::Block)
	{
		ASSERT_OR_IGNORE(member.offset == 0);

		if (!member.has_pending_type)
		{
			TypeMetrics metrics = type_metrics_from_id(types, member.type.complete);

			const u64 offset = next_multiple(header->size, static_cast<u64>(metrics.align));

			member.offset = offset;

			header->size = offset + metrics.size;

			if (header->align < metrics.align)
				header->align = metrics.align;
		}
	}

	member.rank = static_cast<u16>(header->total_used);
	member.is_param = header->disposition == TypeDisposition::Signature;

	if (header->is_closed)
		panic("Passed non-open type to `add_open_type_member`.\n");

	if (header->total_used + 1 == UINT16_MAX)
		panic("Exceeded maximum of %u members in composite type.\n");

	FindResult found;

	if (find_builder_member_by_name(header, member.name, SourceId::INVALID, open_type_id, &found))
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

	tail->members[tail->used] = member;

	tail->used += 1;

	header->total_used += 1;

	if (member.has_pending_type || member.has_pending_value)
		header->incomplete_member_count += 1;
}

void close_open_type(TypePool* types, TypeId open_type_id, u64 size, u32 align, u64 stride) noexcept
{
	ASSERT_OR_IGNORE(open_type_id != TypeId::INVALID);

	TypeName* const builder_name = types->named_types.value_from(static_cast<u32>(open_type_id));

	if (resolve_name_structure(types, builder_name))
		panic("Passed completed type to `add_open_type_member`.\n");

	TypeBuilderHeader* const header = get_deferred_type_builder(types, builder_name);

	if (header->is_closed)
		panic("Passed non-open type to `close_open_type`\n");

	if (header->disposition == TypeDisposition::Signature || header->disposition == TypeDisposition::Block)
	{
		ASSERT_OR_IGNORE(size == 0 && align == 0 && stride == 0);

		header->stride = next_multiple(header->size, static_cast<u64>(header->align));
	}
	else
	{
		ASSERT_OR_IGNORE(align != 0);

		header->size = size;
		header->stride = stride;
		header->align = align;
	}

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

void set_incomplete_type_member_info_by_rank(TypePool* types, TypeId open_type_id, u16 rank, MemberCompletionInfo info) noexcept
{
	ASSERT_OR_IGNORE(open_type_id != TypeId::INVALID);

	TypeName* const builder_name = types->named_types.value_from(static_cast<u32>(open_type_id));

	if (resolve_name_structure(types, builder_name))
		panic("Passed completed type to `add_open_type_member`.\n");

	TypeBuilderHeader* const header = get_deferred_type_builder(types, builder_name);

	FindResult found;

	find_builder_member_by_rank(header, rank, &found);

	if (info.has_type_id)
	{
		ASSERT_OR_IGNORE(found.member->has_pending_type);

		ASSERT_OR_IGNORE(info.type_id != TypeId::INVALID);

		found.member->type.complete = info.type_id;
		found.member->has_pending_type = false;

		if (header->disposition == TypeDisposition::Signature)
		{
			const TypeMetrics metrics = type_metrics_from_id(types, info.type_id);

			const u64 member_offset = next_multiple(header->size, static_cast<u64>(metrics.align));

			found.member->offset = member_offset;

			header->size = member_offset + metrics.size;

			if (metrics.align > header->align)
				header->align = metrics.align;
		}
	}

	if (info.has_value_id)
	{
		ASSERT_OR_IGNORE(found.member->has_pending_value);

		found.member->value.complete = info.value_id;
		found.member->has_pending_value = false;
	}

	ASSERT_OR_IGNORE(header->incomplete_member_count != 0);

	if (!found.member->has_pending_type && !found.member->has_pending_value)
	{
		header->incomplete_member_count -= 1;

		if (header->is_closed && header->incomplete_member_count == 0)
		{
			builder_name->structure_index = structure_index_from_complete_type_builder(types, header);
			builder_name->structure_index_kind = TypeName::STRUCTURE_INDEX_NORMAL;
		}
	}
}

TypeId copy_incomplete_type(TypePool* types, TypeId incomplete_type_id) noexcept
{
	ASSERT_OR_IGNORE(incomplete_type_id != TypeId::INVALID);

	TypeName* const old_name = types->named_types.value_from(static_cast<u32>(incomplete_type_id));

	if (resolve_name_structure(types, old_name))
		panic("Passed completed type to `add_open_type_member`.\n");

	const TypeBuilderHeader* const old_header = get_deferred_type_builder(types, old_name);

	TypeBuilderHeader* const new_header = static_cast<TypeBuilderHeader*>(alloc_type_builder(types));

	memcpy(new_header, old_header, sizeof(*new_header));

	const TypeBuilder* old_curr = type_builder_at_offset(&old_header->unused_, old_header->head_offset);

	TypeBuilder* new_curr = static_cast<TypeBuilder*>(alloc_type_builder(types));

	new_header->head_offset = type_builder_difference(&new_header->unused_, new_curr);

	while (true)
	{
		memcpy(new_curr, old_curr, sizeof(*new_curr));

		if (old_curr->next_offset == 0)
			break;

		TypeBuilder* const new_next = static_cast<TypeBuilder*>(alloc_type_builder(types));

		new_curr->next_offset = type_builder_difference(new_curr, new_next);

		new_curr = new_next;
	}

	new_curr->next_offset = 0;

	new_header->tail_offset = type_builder_difference(&new_header->unused_, new_curr);

	TypeName new_name{};
	new_name.parent_type_id = old_name->parent_type_id;
	new_name.distinct_root_type_id = old_name->distinct_root_type_id;
	new_name.structure_index = type_builder_difference(reinterpret_cast<TypeBuilder*>(types->builders.begin()), &new_header->unused_);
	new_name.structure_index_kind = TypeName::STRUCTURE_INDEX_BUILDER;
	new_name.source_id = old_name->source_id;
	new_name.name_id = old_name->name_id;
	new_name.lexical_parent_type_id = old_name->lexical_parent_type_id;

	return static_cast<TypeId>(types->named_types.index_from(new_name, fnv1a(range::from_object_bytes(&new_name))));
}


bool is_same_type(TypePool* types, TypeId type_id_a, TypeId type_id_b) noexcept
{
	ASSERT_OR_IGNORE(type_id_a != TypeId::INVALID && type_id_b != TypeId::INVALID);

	if (type_id_a == type_id_b)
		return true;

	TypeName* const name_a = types->named_types.value_from(static_cast<u32>(type_id_a));

	TypeName* const name_b = types->named_types.value_from(static_cast<u32>(type_id_b));

	if (!resolve_name_structure(types, name_a) || !resolve_name_structure(types, name_b))
		TODO("Figure out how to handle equality of incomple types.\n");

	// TODO: This doesn't work when there are different but same-type member
	// types, as that will lead to distinct hashes. Which is silly. This
	// *should* be fixable in `close_open_type`, by somehow "canonicalizing"
	// member types before hashing.
	if (name_a->structure_index != name_b->structure_index)
		return false;

	SourceId distinct_root_source_a;

	if (name_a->distinct_root_type_id != TypeId::INVALID)
	{
		TypeName* const root_name = types->named_types.value_from(static_cast<u32>(name_a->distinct_root_type_id));

		distinct_root_source_a = root_name->source_id;
	}
	else
	{
		distinct_root_source_a = name_a->source_id;
	}

	SourceId distinct_root_source_b;

	if (name_b->distinct_root_type_id != TypeId::INVALID)
	{
		TypeName* const root_name = types->named_types.value_from(static_cast<u32>(name_b->distinct_root_type_id));

		distinct_root_source_b = root_name->source_id;
	}
	else
	{
		distinct_root_source_b = name_a->source_id;
	}

	return distinct_root_source_a == distinct_root_source_b;
}

bool type_can_implicitly_convert_from_to(TypePool* types, TypeId from_type_id, TypeId to_type_id) noexcept
{
	ASSERT_OR_IGNORE(from_type_id != TypeId::INVALID && to_type_id != TypeId::INVALID);

	if (is_same_type(types, from_type_id, to_type_id))
		return true;

	const TypeTag from_type_tag = type_tag_from_id(types, from_type_id);

	const TypeTag to_type_tag = type_tag_from_id(types, to_type_id);

	if (from_type_tag == TypeTag::Divergent)
		return true;

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
	case TypeTag::TailArray:
	{
		return false;
	}

	case TypeTag::INVALID:
	case TypeTag::Divergent:
		; // Fallthrough to unreachable
	}

	ASSERT_UNREACHABLE;
}

TypeId common_type(TypePool* types, TypeId type_id_a, TypeId type_id_b) noexcept
{
	ASSERT_OR_IGNORE(type_id_a != TypeId::INVALID && type_id_b != TypeId::INVALID);

	// Check the common case. If the ids themselves are equal, we have a match
	// already.

	if (type_id_a == type_id_b)
		return type_id_a;

	// Since the ids were not equal, there are a few cases left:
	// 1. The types are aliases with the same `distinct_root_type_id`; In this
	//    case, we have a match.
	// 2. The type ids have different `distinct_root_type_id`s, but the two
	//    roots are really one. This happens when a partially complete type is
	//    constructed twice before being completed and is a rare case.
	// 3. The type ids do not refer to different types but one can be
	//    implicitly converted to the other.
	// 4. The type ids do not refer to different types and there are no
	//    applicable implicit conversions.

	// Check for case 1.

	TypeName* const name_a = types->named_types.value_from(static_cast<u32>(type_id_a));

	if (!resolve_name_structure(types, name_a))
		panic("Tried comparing incomplete type\n"); // TODO: Figure out what to do here

	TypeName* const name_b = types->named_types.value_from(static_cast<u32>(type_id_b));

	if (!resolve_name_structure(types, name_b))
		panic("Tried comparing incomplete type\n"); // TODO: Figure out what to do here

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

	if (root_name_a->structure_index == root_name_b->structure_index && root_name_a->source_id == root_name_b->source_id)
	{
		// Just select the "minimal" id as the canonical one.
		const TypeId min_id = static_cast<u32>(root_type_id_a) < static_cast<u32>(root_type_id_b) ? root_type_id_a : root_type_id_b;

		return min_id;
	}

	// Check for case 3.

	if (type_can_implicitly_convert_from_to(types, type_id_a, type_id_b))
		return type_id_b;

	if (type_can_implicitly_convert_from_to(types, type_id_b, type_id_a))
		return type_id_a;

	// We're in case 4. (Incompatible types)

	return TypeId::INVALID;
}


IdentifierId type_name_from_id(const TypePool* types, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeName* const name = types->named_types.value_from(static_cast<u32>(type_id));

	return name->name_id;
}

SourceId type_source_from_id(const TypePool* types, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeName* const name = types->named_types.value_from(static_cast<u32>(type_id));

	return name->source_id;
}

TypeDisposition type_disposition_from_id(TypePool* types, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	TypeName* const name = types->named_types.value_from(static_cast<u32>(type_id));

	if (!resolve_name_structure(types, name))
		return type_builder_header_at_index(types, name->structure_index)->disposition;

	TypeStructure* const structure = types->structural_types.value_from(name->structure_index);

	ASSERT_OR_IGNORE(structure->tag == TypeTag::Composite);

	return data<CompositeType>(structure)->header.disposition;
}

TypeId lexical_parent_type_from_id(const TypePool* types, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	const TypeName* const name = types->named_types.value_from(static_cast<u32>(type_id));

	return name->lexical_parent_type_id;
}

TypeMetrics type_metrics_from_id(TypePool* types, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

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
	case TypeTag::TailArray:
		panic("`type_metrics_from_id` is not yet implemented for `%s`.\n", tag_name(structure->tag));

	case TypeTag::INVALID:
		; // fallthrough to unreachable
	}

	ASSERT_UNREACHABLE;
}

TypeTag type_tag_from_id(TypePool* types, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	TypeName* const name = types->named_types.value_from(static_cast<u32>(type_id));

	if (!resolve_name_structure(types, name))
		return TypeTag::Composite;

	return types->structural_types.value_from(name->structure_index)->tag;
}

Member* get_open_type_member(TypePool* types, TypeId open_type_id, u16 rank) noexcept
{
	ASSERT_OR_IGNORE(open_type_id != TypeId::INVALID);

	TypeName* const builder_name = types->named_types.value_from(static_cast<u32>(open_type_id));

	if (resolve_name_structure(types, builder_name))
		panic("Passed completed type to `add_open_type_member`.\n");

	TypeBuilderHeader* const header = get_deferred_type_builder(types, builder_name);

	if (header->is_closed)
		panic("Passed non-open type to `get_open_type_member`\n");

	FindResult found;

	find_builder_member_by_rank(header, rank, &found);

	return found.member;
}

const Member* type_member_by_rank(TypePool* types, TypeId type_id, u16 rank)
{
	FindResult found;

	find_member_by_rank(types, type_id, rank, &found);

	return found.member;
}

bool type_member_by_name(TypePool* types, TypeId type_id, IdentifierId name, SourceId source, const Member** out) noexcept
{
	FindResult found;

	if (!find_member_by_name(types, type_id, name, source, &found))
		return false;

	if (found.disposition == TypeDisposition::Block && found.member->source > source)
		return false;

	*out = found.member;

	return true;
}

const void* simple_type_structure_from_id(TypePool* types, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

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
		"TailArray",
		"Dependent",
	};

	u8 ordinal = static_cast<u8>(tag);

	if (ordinal >= array_count(TYPE_TAG_NAMES))
		ordinal = 0;

	return TYPE_TAG_NAMES[ordinal];
}


static void next_incomplete_member(IncompleteMemberIterator* it) noexcept
{
	TypeBuilder* builder = static_cast<TypeBuilder*>(it->structure);

	u16 rank = it->rank;

	while (true)
	{
		u16 index = rank & static_cast<u16>(array_count(builder->members) - 1);

		while (index != builder->used)
		{
			if (builder->members[index].has_pending_type || builder->members[index].has_pending_value)
			{
				it->rank = rank;

				it->structure = builder;
				
				return;
			}

			index += 1;

			rank += 1;
		}

		index = 0;

		if (builder->next_offset == 0)
		{
			it->structure = nullptr;

			return;
		}

		builder = type_builder_at_offset(builder, builder->next_offset);
	}
}

IncompleteMemberIterator incomplete_members_of(TypePool* types, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	TypeName* name = types->named_types.value_from(static_cast<u32>(type_id));

	if (resolve_name_structure(types, name))
		return { nullptr, nullptr, 0, TypeDisposition::INVALID, TypeId::INVALID };

	if (name->structure_index_kind == TypeName::STRUCTURE_INDEX_INDIRECT)
		name = types->named_types.value_from(name->structure_index);

	ASSERT_OR_IGNORE(name->structure_index_kind == TypeName::STRUCTURE_INDEX_BUILDER);

	TypeBuilderHeader* const header = type_builder_header_at_index(types, name->structure_index);

	if (!header->is_closed)
		panic("Passed open type to `incomplete_members_of`\n");

	if (header->head_offset == 0)
		return { nullptr, nullptr, 0, TypeDisposition::INVALID, TypeId::INVALID };

	TypeBuilder* builder = type_builder_at_offset(&header->unused_, header->head_offset);

	IncompleteMemberIterator it{ builder, name, 0, header->disposition, type_id };

	next_incomplete_member(&it);

	return it;
}

const Member* next(IncompleteMemberIterator* it) noexcept
{
	ASSERT_OR_IGNORE(it->structure != nullptr);

	ASSERT_OR_IGNORE(static_cast<TypeName*>(it->name)->structure_index_kind == TypeName::STRUCTURE_INDEX_BUILDER);

	const u16 rank = it->rank;

	TypeBuilder* const builder = static_cast<TypeBuilder*>(it->structure);

	const u16 index = rank & static_cast<u16>(array_count(builder->members) - 1);

	it->rank = rank + 1;

	if (((rank + 1) & (array_count(builder->members) - 1)) == 0)
	{
		if (builder->next_offset == 0)
			return builder->members + index;

		it->structure = type_builder_at_offset(builder, builder->next_offset);
	}

	next_incomplete_member(it);

	return builder->members + index;
}

bool has_next(const IncompleteMemberIterator* it) noexcept
{
	return it->structure != nullptr
	    && static_cast<TypeName*>(it->name)->structure_index_kind == TypeName::STRUCTURE_INDEX_BUILDER;
}

MemberIterator members_of(TypePool* types, TypeId type_id) noexcept
{
	ASSERT_OR_IGNORE(type_id != TypeId::INVALID);

	TypeName* name = types->named_types.value_from(static_cast<u32>(type_id));

	if (resolve_name_structure(types, name))
	{
		ASSERT_OR_IGNORE(name->structure_index_kind == TypeName::STRUCTURE_INDEX_NORMAL);

		TypeStructure* const structure = types->structural_types.value_from(name->structure_index);

		ASSERT_OR_IGNORE(structure->tag == TypeTag::Composite);

		CompositeType* const composite = data<CompositeType>(structure);

		if (composite->header.member_count == 0)
			return { nullptr, nullptr, 0, TypeDisposition::INVALID, TypeId::INVALID };

		return { composite, nullptr, 0, composite->header.disposition, type_id };
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
			return { nullptr, nullptr, 0, TypeDisposition::INVALID, TypeId::INVALID };

		return { type_builder_at_offset(&header->unused_, header->tail_offset), name, 0, header->disposition, type_id };
	}

}

const Member* next(MemberIterator* it) noexcept
{
	ASSERT_OR_IGNORE(it->structure != nullptr);

	const u16 rank = it->rank;

	it->rank = rank + 1;

	if (it->name != nullptr)
	{
		TypeBuilder* const builder = static_cast<TypeBuilder*>(it->structure);

		const u32 index = rank & (array_count(builder->members) - 1);

		if (index + 1 == builder->used)
		{
			if (builder->next_offset == 0)
				it->structure = nullptr;
			else
				it->structure = type_builder_at_offset(builder, builder->next_offset);
		}

		return builder->members + index;
	}
	else
	{
		CompositeType* const composite = static_cast<CompositeType*>(it->structure);

		if (static_cast<u16>(rank + 1) == composite->header.member_count)
			it->structure = nullptr;

		return composite->members + rank;
	}
}

bool has_next(const MemberIterator* it) noexcept
{
	return it->structure != nullptr;
}
