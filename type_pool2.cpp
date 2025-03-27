#include "pass_data.hpp"

#include "infra/hash.hpp"

struct PrimitiveTypeKey
{
	TypeTag tag;

	TypePool2* types;

	Range<byte> data;
};

struct PrimitiveTypeEntry
{
	TypeEntry2* entry;

	static constexpr u32 stride() noexcept
	{
		return 8;
	}

	static u32 required_strides([[maybe_unused]] PrimitiveTypeKey key) noexcept
	{
		return 1;
	}

	u32 used_strides() const noexcept
	{
		return 1;
	}

	u32 hash() const noexcept
	{
		const u32 bytes = entry->tag == TypeTag::Array ? 12 : 4;

		return fnv1a_step(fnv1a(Range<byte>{ reinterpret_cast<const byte*>(&entry->inline_data), bytes }), static_cast<byte>(entry->tag));
	}

	bool equal_to_key(PrimitiveTypeKey key, [[maybe_unused]] u32 key_hash) noexcept
	{
		return key.data.count() == entry->bytes + 4 && memcmp(key.data.begin(), &entry->inline_data, key.data.count()) == 0;
	}

	void init(PrimitiveTypeKey key, u32 key_hash) noexcept;
};

struct TypePool2
{
	IndexMap<PrimitiveTypeKey, PrimitiveTypeEntry> primitive_types;

	ReservedVec<u64> types;

	ReservedVec<u64> builders;

	s32 first_free_builder_index;
};

struct TypeBuilder2
{
	union
	{
		#if COMPILER_CLANG
			#pragma clang diagnostic push
			#pragma clang diagnostic ignored "-Wgnu-anonymous-struct" // anonymous structs are a GNU extension
			#pragma clang diagnostic ignored "-Wnested-anon-types" // anonymous types declared in an anonymous union are an extension
		#endif
		struct
		{
			s32 next_offset;

			s32 tail_offset;

			u32 used;

			u32 total_used;

			TypePool2* types;
		};
		#if COMPILER_CLANG
			#pragma clang diagnostic pop
		#endif

		Member2 unused_align_;
	};

	Member2 members[7];
};

static_assert(sizeof(TypeBuilder2) == 8 * sizeof(Member2));

void PrimitiveTypeEntry::init(PrimitiveTypeKey key, [[maybe_unused]] u32 key_hash) noexcept
{
	const u16 extra_bytes = key.tag == TypeTag::Array ? 8 : 0;

	ASSERT_OR_IGNORE(key.data.count() == extra_bytes + 4);

	TypeEntry2* const new_entry = static_cast<TypeEntry2*>(key.types->types.reserve_exact(sizeof(TypeEntry2) + extra_bytes));

	new_entry->tag = key.tag;

	new_entry->bytes = extra_bytes;

	memcpy(&new_entry->inline_data, key.data.begin(), key.data.count());

	entry = new_entry;
}

static TypeEntry2* alloc_type(TypePool2* types, TypeTag tag, u32 bytes) noexcept
{
	ASSERT_OR_IGNORE(bytes <= UINT16_MAX);

	TypeEntry2* const type = static_cast<TypeEntry2*>(types->types.reserve_padded(sizeof(TypeEntry2) + bytes));

	type->tag = tag;

	type->bytes = static_cast<u16>(bytes);

	return type;
}

TypePool2* create_type_pool2(AllocPool* alloc) noexcept
{
	TypePool2* const types = static_cast<TypePool2*>(alloc_from_pool(alloc, sizeof(TypePool2), alignof(TypePool2)));

	types->types.init(1 << 26, 1 << 13);
	types->primitive_types.init(1 << 24, 1 << 9, 1 << 24, 1 << 9);
	types->builders.init(1 << 15, 1 << 11);
	types->first_free_builder_index = -1;

	types->types.append(0);

	return types;
}

void release_type_pool2(TypePool2* types) noexcept
{
	types->types.release();

	types->primitive_types.release();

	types->builders.release();
}

TypeEntry2* type_entry_from_primitive_type(TypePool2* types, TypeTag tag, Range<byte> bytes) noexcept
{
	return types->primitive_types.value_from(PrimitiveTypeKey{ tag, types, bytes }, fnv1a_step(fnv1a(bytes), static_cast<byte>(tag)))->entry;
}

TypeEntry2* type_entry_from_id(TypePool2* types, TypeId id) noexcept
{
	return reinterpret_cast<TypeEntry2*>(reinterpret_cast<u32*>(types->types.begin()) + id.rep);
}

TypeId2 id_from_type_entry(TypePool2* types, TypeEntry2* entry) noexcept
{
	return { static_cast<u32>(reinterpret_cast<u32*>(entry) - reinterpret_cast<u32*>(types->types.begin())) };
}

TypeBuilder2* create_type_builder(TypePool2* types) noexcept
{
	TypeBuilder2* builder;

	if (types->first_free_builder_index < 0)
	{
		builder = static_cast<TypeBuilder2*>(types->builders.reserve_exact(sizeof(TypeBuilder2)));

		builder->types = types;
	}
	else
	{
		builder = reinterpret_cast<TypeBuilder2*>(types->builders.begin() + types->first_free_builder_index);

		types->first_free_builder_index = builder->next_offset;
	}

	builder->next_offset = 0;
	builder->tail_offset = 0;
	builder->used = 0;
	builder->total_used = 0;

	return builder;
}

void add_type_member(TypeBuilder2* builder, Member2 member) noexcept
{
	ASSERT_OR_IGNORE(member.definition.name != INVALID_IDENTIFIER_ID);

	ASSERT_OR_IGNORE(member.definition.opt_type != INVALID_AST_NODE_ID || member.definition.opt_value != INVALID_AST_NODE_ID);

	TypeBuilder2* tail = reinterpret_cast<TypeBuilder2*>(reinterpret_cast<u64*>(builder) + builder->tail_offset);

	ASSERT_OR_IGNORE(tail->next_offset == 0);

	if (tail->used == array_count(builder->members))
	{
		TypeBuilder2* const new_tail = create_type_builder(builder->types);

		tail->next_offset = static_cast<s32>(reinterpret_cast<u64*>(new_tail) - reinterpret_cast<u64*>(tail));

		builder->tail_offset = static_cast<s32>(reinterpret_cast<u64*>(new_tail) - reinterpret_cast<u64*>(builder));

		tail = new_tail;
	}

	ASSERT_OR_IGNORE(tail->used < array_count(builder->members));

	tail->members[tail->used] = member;

	tail->used += 1;

	builder->total_used += 1;
}

TypeEntry2* complete_type(TypeBuilder2* builder, u64 size, u32 align, u64 stride) noexcept
{
	TypeEntry2* const type = alloc_type(builder->types, TypeTag::Composite, sizeof(CompositeTypeHeader2) + sizeof(Member2) * builder->total_used);

	CompositeType2* const composite = data<CompositeType2>(type);
	composite->header.size = size;
	composite->header.stride = stride;
	composite->header.align = align;
	composite->header.member_count = static_cast<u16>(builder->total_used);
	composite->header.is_complete = false;

	const TypeBuilder2* curr = builder;

	u32 member_index = 0;

	while (true)
	{
		ASSERT_OR_IGNORE(member_index + curr->used <= builder->total_used);

		memcpy(composite->members + member_index, curr->members, curr->used * sizeof(Member2));

		member_index += curr->used;

		if (curr->next_offset == 0)
			break;

		curr = reinterpret_cast<const TypeBuilder2*>(reinterpret_cast<const u64*>(curr) + curr->next_offset);
	}

	ASSERT_OR_IGNORE(member_index == builder->total_used);

	ASSERT_OR_IGNORE(curr == reinterpret_cast<TypeBuilder2*>(reinterpret_cast<u64*>(builder) + builder->tail_offset));

	return type;
}
