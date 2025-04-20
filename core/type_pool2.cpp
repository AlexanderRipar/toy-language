#include "pass_data.hpp"

#include "../infra/hash.hpp"
#include "../infra/inplace_sort.hpp"

#include <cstdlib>

///////////////////////////////////// TODO ////////////////////////////////////
//
// Create mechanism for managing dependencies on incomplete types.
// This will likely take the form of a linked list of some form.
//

struct CompositeTypeBuffer
{
	CompositeTypeHeader2 header;

	Member2 members[32];
};

struct TypeName
{
	static constexpr u32 STRUCTURE_INDEX_NORMAL = 0;
	static constexpr u32 STRUCTURE_INDEX_BUILDER = 1;
	static constexpr u32 STRUCTURE_INDEX_INDIRECT = 2;

	// This is used to initialize a `TypeName` entry that will never match any
	// other in `create_type_pool2`. It must not be used as the value of the
	// `structure_index_kind` of any other `TypeName`.
	static constexpr u32 INVALID_STRUCTURE_INDEX = 3;

	// `TypeId` of the type from which this type was derived.
	//
	// E.g. in `let T = U`, `T`'s `parent_id` will be set to the `TypeId` of
	// `U`.
	//
	// If the type is not derived from another one, `parent_id` is set to
	// `INVALID_TYPE_ID_2`.
	TypeId2 parent_type_id;

	// `TypeId` of the first type in the parent hierarchy which is `distinct`,
	// or the root type if there is no such type.
	//
	// If this type is itself `distinct` or not derived from another type,
	// `distinct_root_id` is set to `INVALID_TYPE_ID_2`.
	TypeId2 distinct_root_type_id;

	// One of the following, depending on the value of `structure_index_kind`:
	//
	// - `STRUCTURE_INDEX_NORMAL`: An index into `TypePool2.structural_types`
	//   from which this type's structure can be retrieved.
	//
	// - `STRUCTURE_INDEX_BUILDER` A qword-strided index into
	//   `TypePool2.builders`. This indicates that the type is not yet
	//   complete, and could thus not be hashed into
	//   `TypePool2.structural_types` yet.
	//
	// - `STRUCTURE_INDEX_INDIRECT`: An index into `TypePool2.named_types`. This
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

struct TypePool2
{
	IndexMap<AttachmentRange<byte, TypeTag>, TypeStructure2> structural_types;

	IndexMap<TypeName, TypeName> named_types;

	ReservedVec<u64> builders;

	s32 first_free_builder_index;

	ErrorSink* errors;
};

struct TypeBuilder2
{
	union
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
			s32 next_offset;

			s32 tail_offset;

			u8 used;

			bool is_completed;

			u32 total_used;

			u32 incomplete_member_count;

			SourceId source_id;
		};
		#if COMPILER_CLANG
			#pragma clang diagnostic pop
		#elif COMPILER_GCC
			#pragma GCC diagnostic pop
		#endif

		Member2 unused_align_;
	};

	Member2 members[7];
};

static_assert(sizeof(TypeBuilder2) == 8 * sizeof(Member2));



static s32 index_from_type_builder(const TypePool2* types, const TypeBuilder2* builder) noexcept
{
	return static_cast<s32>(reinterpret_cast<const u64*>(builder) - types->builders.begin());
}

static TypeBuilder2* type_builder_from_index(TypePool2* types, s32 index) noexcept
{
	return reinterpret_cast<TypeBuilder2*>(types->builders.begin() + index);
}

static TypeBuilder2* type_builder_at_offset(TypeBuilder2* builder, s32 offset) noexcept
{
	return reinterpret_cast<TypeBuilder2*>(reinterpret_cast<u64*>(builder) + offset);
}

static const TypeBuilder2* type_builder_at_offset(const TypeBuilder2* builder, s32 offset) noexcept
{
	return type_builder_at_offset(const_cast<TypeBuilder2*>(builder), offset);
}

static s32 type_builder_difference(const TypeBuilder2* from, const TypeBuilder2* to) noexcept
{
	return static_cast<s32>(reinterpret_cast<const u64*>(to) - reinterpret_cast<const u64*>(from));
}

static TypeBuilder2* alloc_type_builder(TypePool2* types) noexcept
{
	const s32 first_free_index = types->first_free_builder_index;

	if (first_free_index < 0)
		return static_cast<TypeBuilder2*>(types->builders.reserve_exact(sizeof(TypeBuilder2)));

	TypeBuilder2* const builder = type_builder_from_index(types, first_free_index);

	types->first_free_builder_index = builder->next_offset == 0 ? -1 : builder->next_offset + first_free_index;

	return builder;
}

static void free_type_builder(TypePool2* types, TypeBuilder2* builder) noexcept
{
	const s32 old_first_free_index = types->first_free_builder_index;

	if (old_first_free_index >= 0)
	{
		TypeBuilder2* const tail_builder = type_builder_at_offset(builder, builder->tail_offset);

		const s32 tail_index = index_from_type_builder(types, tail_builder);

		tail_builder->next_offset = old_first_free_index - tail_index;
	}

	types->first_free_builder_index = index_from_type_builder(types, builder);
}

static u32 structure_index_from_complete_type_builder(TypePool2* types, const TypeBuilder2* builder, u64 size, u32 align, u64 stride) noexcept
{
	// Use a stack-based buffer if there are only a few members in the builder;
	// Otherwise allocate a buffer from the heap.
	CompositeTypeBuffer stack_buffer;

	CompositeType2* composite;

	if (builder->total_used > array_count(stack_buffer.members))
	{
		composite = static_cast<CompositeType2*>(malloc(sizeof(CompositeType2::header) + builder->total_used * sizeof(CompositeType2::members[0])));

		if (composite == nullptr)
			panic("malloc failed (0x%X)\n", minos::last_error());
	}
	else
	{
		composite = reinterpret_cast<CompositeType2*>(&stack_buffer);
	}

	// Initialize `composite->header`

	composite->header.size = size;
	composite->header.stride = stride;
	composite->header.align = align;
	composite->header.member_count = builder->total_used;

	// Initialize `composite->members`

	const TypeBuilder2* curr = builder;

	u32 curr_member_index = 0;

	while (true)
	{
		memcpy(composite->members + curr_member_index, curr->members, curr->used * sizeof(curr->members[0]));

		if (curr->next_offset == 0)
			break;

		curr = type_builder_at_offset(curr, curr->next_offset);
	}

	// Sort members by their names. This fulfills two purposes:
	// 1. Minimizing the number of types by avoiding differences caused by the
	//    order in which their members were added.
	// 2. Simplify checking for member name collisions.

	struct MemberComparator
	{
		static u32 compare(const Member2& lhs, const Member2& rhs) noexcept
		{
			return lhs.definition.name.rep - rhs.definition.name.rep;
		}
	};

	inplace_sort<Member2, MemberComparator>(MutRange{ composite->members, composite->header.member_count });

	// Check for member name collisions.

	IdentifierId prev_name = composite->header.member_count == 0 ? INVALID_IDENTIFIER_ID : composite->members[0].definition.name;

	for (u32 i = 1; i < composite->header.member_count; ++i)
	{
		const IdentifierId curr_name = composite->members[i].definition.name;

		if (curr_name == prev_name)
			source_error(types->errors, builder->source_id, "Cannot create type with more than one member with the name %.*s\n", 1, "?" /* TODO */);
	}

	// Hash the created composite into `TypeBuilder2.structural_types`.

	const Range<byte> data{ reinterpret_cast<byte*>(composite), sizeof(composite->header) + builder->total_used * sizeof(composite->members[0]) };

	const u32 structural_index = types->structural_types.index_from(AttachmentRange{ data, TypeTag::Composite }, fnv1a_step(fnv1a(data), static_cast<byte>(TypeTag::Composite)));

	// If we allocated the composite buffer from the heap, free it.
	if (composite != reinterpret_cast<CompositeType2*>(&stack_buffer))
		free(composite);

	return structural_index;
}

static bool resolve_name_structure(TypePool2* types, TypeName* name) noexcept
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

static TypeBuilder2* get_deferred_type_builder(TypePool2* types, TypeName* name) noexcept
{
	ASSERT_OR_IGNORE(name->structure_index_kind == TypeName::STRUCTURE_INDEX_BUILDER || name->structure_index_kind == TypeName::STRUCTURE_INDEX_INDIRECT);

	if (name->structure_index_kind == TypeName::STRUCTURE_INDEX_INDIRECT)
		name = types->named_types.value_from(name->structure_index);

	ASSERT_OR_IGNORE(name->structure_index_kind == TypeName::STRUCTURE_INDEX_BUILDER);

	return type_builder_from_index(types, name->structure_index);
}



TypePool2* create_type_pool2(AllocPool* alloc, ErrorSink* errors) noexcept
{
	TypePool2* const types = static_cast<TypePool2*>(alloc_from_pool(alloc, sizeof(TypePool2), alignof(TypePool2)));

	types->structural_types.init(1 << 24, 1 << 10, 1 << 24, 1 << 9);
	types->named_types.init(1 << 26, 1 << 10, 1 << 26, 1 << 13);
	types->builders.init(1 << 15, 1 << 11);
	types->first_free_builder_index = -1;
	types->errors = errors;

	// Reserve 0 as INVALID_TYPE_ID_2

	TypeName dummy_name{};
	dummy_name.structure_index_kind = TypeName::INVALID_STRUCTURE_INDEX;

	(void) types->named_types.index_from(dummy_name, fnv1a(range::from_object_bytes(&dummy_name)));

	return types;
}

void release_type_pool2(TypePool2* types) noexcept
{
	types->named_types.release();

	types->structural_types.release();

	types->builders.release();
}

TypeId2 primitive_type(TypePool2* types, TypeTag tag, Range<byte> data) noexcept
{
	const u32 structure_index = types->structural_types.index_from(AttachmentRange{ data, tag }, fnv1a_step(fnv1a(data), static_cast<byte>(tag)));

	TypeName name;
	name.parent_type_id = INVALID_TYPE_ID_2;
	name.distinct_root_type_id = INVALID_TYPE_ID_2;
	name.structure_index = structure_index;
	name.structure_index_kind = TypeName::STRUCTURE_INDEX_NORMAL;
	name.source_id = INVALID_SOURCE_ID;
	name.name_id = INVALID_IDENTIFIER_ID;

	return TypeId2{ types->named_types.index_from(name, fnv1a(range::from_object_bytes(&name))) };
}

TypeId2 alias_type(TypePool2* types, TypeId2 aliased_type_id, bool is_distinct, SourceId source_id, IdentifierId name_id) noexcept
{
	TypeName* const aliased_ref = types->named_types.value_from(aliased_type_id.rep);

	TypeName name;
	name.parent_type_id = aliased_type_id;
	name.distinct_root_type_id = is_distinct ? INVALID_TYPE_ID_2 : aliased_ref->distinct_root_type_id;
	name.source_id = source_id;
	name.name_id = name_id;

	if (aliased_ref->structure_index_kind == TypeName::STRUCTURE_INDEX_BUILDER)
	{
		name.structure_index = aliased_type_id.rep;
		name.structure_index_kind = TypeName::STRUCTURE_INDEX_INDIRECT;
	}
	else
	{
		name.structure_index = aliased_ref->structure_index;
		name.structure_index_kind = aliased_ref->structure_index_kind;
	}

	return TypeId2{ types->named_types.index_from(name, fnv1a(range::from_object_bytes(&name))) };
}

OptPtr<TypeStructure2> type_structure_from_id(TypePool2* types, TypeId2 type_id) noexcept
{
	TypeName* const name = types->named_types.value_from(type_id.rep);

	if (!resolve_name_structure(types, name))
		return none<TypeStructure2>();

	return some(types->structural_types.value_from(name->structure_index));
}

TypeBuilder2* create_type_builder(TypePool2* types, SourceId source_id) noexcept
{
	TypeBuilder2* const builder = alloc_type_builder(types);

	builder->next_offset = 0;
	builder->tail_offset = 0;
	builder->used = 0;
	builder->is_completed = false;
	builder->total_used = 0;
	builder->incomplete_member_count = 0;
	builder->source_id = source_id;

	return builder;
}

void add_type_builder_member(TypePool2* types, TypeBuilder2* builder, Member2 member) noexcept
{
	ASSERT_OR_IGNORE(member.definition.name != INVALID_IDENTIFIER_ID);

	ASSERT_OR_IGNORE(member.definition.opt_type != INVALID_AST_NODE_ID || member.definition.opt_value != INVALID_AST_NODE_ID);

	ASSERT_OR_IGNORE(builder->incomplete_member_count == ~0u);

	TypeBuilder2* tail = type_builder_at_offset(builder, builder->tail_offset);

	ASSERT_OR_IGNORE(tail->next_offset == 0);

	if (tail->used == array_count(builder->members))
	{
		TypeBuilder2* const new_tail = alloc_type_builder(types);

		new_tail->next_offset = 0;
		new_tail->tail_offset = 0;
		new_tail->used = 0;

		tail->next_offset = type_builder_difference(tail, new_tail);

		builder->tail_offset = type_builder_difference(builder, new_tail);

		tail = new_tail;
	}

	ASSERT_OR_IGNORE(tail->used < array_count(builder->members));

	tail->members[tail->used] = member;

	tail->used += 1;

	builder->total_used += 1;

	if (member.definition.type_id_bits == 0)
		builder->incomplete_member_count += 1;
}

TypeId2 complete_type_builder(TypePool2* types, TypeBuilder2* builder, u64 size, u32 align, u64 stride) noexcept
{
	TypeName name;
	name.parent_type_id = INVALID_TYPE_ID_2;
	name.distinct_root_type_id = INVALID_TYPE_ID_2;
	name.source_id = builder->source_id;
	name.name_id = INVALID_IDENTIFIER_ID;

	if (builder->incomplete_member_count == 0)
	{
		name.structure_index = structure_index_from_complete_type_builder(types, builder, size, align, stride);
		name.structure_index_kind = TypeName::STRUCTURE_INDEX_NORMAL;

		free_type_builder(types, builder);
	}
	else
	{
		name.structure_index = static_cast<u32>(reinterpret_cast<u64*>(builder) - reinterpret_cast<u64*>(types->builders.begin()));
		name.structure_index_kind = TypeName::STRUCTURE_INDEX_BUILDER;

		builder->is_completed = true;
	}

	return TypeId2{ types->named_types.index_from(name, fnv1a(range::from_object_bytes(&name))) };
}

bool type_compatible(TypePool2* types, TypeId2 type_id_a, TypeId2 type_id_b) noexcept
{
	ASSERT_OR_IGNORE(type_id_a != INVALID_TYPE_ID_2);

	ASSERT_OR_IGNORE(type_id_b != INVALID_TYPE_ID_2);

	// First, check the common case. If the ids themselves are equal, we have a
	// match already.

	if (type_id_a == type_id_b)
		return true;

	// Since the ids were not equal, there are a few cases left:
	// 1. The types are aliases with the same `distinct_root_type_id`; In this
	//    case, we have a match.
	// 2. The types refer to different `distinct_root_type_id`s, but the two
	//    roots are really one. This happens when a partially complete type is
	//    constructed twice before being completed and is a rare case.
	// 3. The types are not compatible.

	// Check for case 1.

	TypeName* const name_a = types->named_types.value_from(type_id_a.rep);

	if (!resolve_name_structure(types, name_a))
		panic("Tried comparing incomplete type for compatibility\n"); // TODO: Figure out what to do here

	TypeName* const name_b = types->named_types.value_from(type_id_b.rep);

	if (!resolve_name_structure(types, name_b))
		panic("Tried comparing incomplete type for compatibility\n"); // TODO: Figure out what to do here

	const TypeId2 root_type_id_a = name_a->distinct_root_type_id == INVALID_TYPE_ID_2 ? type_id_a : name_a->distinct_root_type_id;

	const TypeId2 root_type_id_b = name_b->distinct_root_type_id == INVALID_TYPE_ID_2 ? type_id_b : name_b->distinct_root_type_id;

	if (root_type_id_a == root_type_id_b)
		return true;

	// Check for case 2.

	TypeName* root_name_a;

	if (name_a->distinct_root_type_id == INVALID_TYPE_ID_2)
	{
		root_name_a = name_a;
	}
	else
	{
		root_name_a = types->named_types.value_from(name_a->distinct_root_type_id.rep);

		if (!resolve_name_structure(types, root_name_a))
			panic("Tried comparing incomplete type for compatibility\n"); // TODO: Figure out what to do here
	}

	TypeName* root_name_b;

	if (name_b->distinct_root_type_id == INVALID_TYPE_ID_2)
	{
		root_name_b = name_b;
	}
	else
	{
		root_name_b = types->named_types.value_from(name_b->distinct_root_type_id.rep);

		if (!resolve_name_structure(types, root_name_b))
			panic("Tried comparing incomplete type for compatibility\n"); // TODO: Figure out what to do here
	}

	// If this is `false` we are in case 3. (incompatible types)

	return root_name_a->structure_index == root_name_b->structure_index && root_name_a->source_id == root_name_b->source_id;
}

bool type_can_cast_from_to(TypePool2* types, TypeId2 from_type_id, TypeId2 to_type_id) noexcept
{
	if (type_compatible(types, from_type_id, to_type_id))
		return true;

	// TODO: Check for applicable implicit conversion rules from `from` to `to`.

	return false;
}

TypeId2 common_type(TypePool2* types, TypeId2 type_id_a, TypeId2 type_id_b) noexcept
{
	if (type_id_a == type_id_b)
		return type_id_a;

	if (!type_compatible(types, type_id_a, type_id_b))
		return INVALID_TYPE_ID_2;

	// TODO: Look if a is an alias of b (or the other way around). In this
	//       case, return the aliased of the two (the one closer to the root).
	//       If they are not directly related, return their first common
	//       ancestor.
	//       Note that all of this is only a niceity for diagnostics, and does
	//       not otherwise affect the compiler's correctness.

	return type_id_a;
}

Member2* type_get_member(TypePool2* types, TypeId2 type_id, IdentifierId member_name) noexcept
{
	TypeName* const name = types->named_types.value_from(type_id.rep);

	if (!resolve_name_structure(types, name))
	{
		TypeBuilder2* deferred_builder = get_deferred_type_builder(types, name);

		while (true)
		{
			for (u32 i = 0; i != deferred_builder->used; ++i)
			{
				if (deferred_builder->members[i].definition.name == member_name)
				{
					return deferred_builder->members + i;
				}
			}

			if (deferred_builder->next_offset == 0)
				break;

			deferred_builder = type_builder_at_offset(deferred_builder, deferred_builder->next_offset);
		}

		panic("Tried getting nonexistent member of type\n"); // TODO: Report type
	}

	TypeStructure2* const structure = types->structural_types.value_from(name->structure_index);

	if (structure->tag != TypeTag::Composite)
		panic("Tried getting member of non-composite type\n"); // TODO: Report type

	CompositeType2* const composite = data<CompositeType2>(structure);

	for (u32 i = 0; i != composite->header.member_count; ++i)
	{
		if (composite->members[i].definition.name == member_name)
			return composite->members + i;
	}

	panic("Tried getting nonexistent member of type\n"); // TODO: Report type
}

IncompleteMemberIterator incomplete_members_of(TypePool2* types, TypeId2 type_id) noexcept
{
	TypeName* name = types->named_types.value_from(type_id.rep);

	if (name->structure_index_kind == TypeName::STRUCTURE_INDEX_INDIRECT)
		name = types->named_types.value_from(name->structure_index);

	if (name->structure_index_kind == TypeName::STRUCTURE_INDEX_NORMAL)
		return { nullptr, 0 };

	ASSERT_OR_IGNORE(name->structure_index_kind == TypeName::STRUCTURE_INDEX_BUILDER);

	return { type_builder_from_index(types, name->structure_index), 0 };
}

OptPtr<Member2> next(IncompleteMemberIterator* it) noexcept
{
	if (it->builder == nullptr)
		return none<Member2>();

	TypeBuilder2* builder = it->builder;

	u32 curr = it->curr;

	while (true)
	{
		while (curr != builder->used)
		{
			if (builder->members[curr].definition.type_id_bits == 0)
				break;

			curr += 1;
		}

		if (builder->next_offset == 0)
		{
			it->builder = nullptr;

			return none<Member2>();
		}

		builder = type_builder_at_offset(builder, builder->next_offset);

		curr = 0;
	}

	it->builder = builder;

	it->curr = curr + 1;

	return some(builder->members + curr);
}
