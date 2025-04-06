#include "pass_data.hpp"

#include "../infra/container.hpp"
#include "../infra/hash.hpp"

static constexpr u32 TYPE_SCRATCH_CAPACITY = 4096;

union TypeBuilder
{
	CompositeTypeMember members[8];

	FuncTypeParam params[12];
};

static_assert(sizeof(TypeBuilder::members) == sizeof(TypeBuilder::params));

struct TypePool
{
	IndexMap<TypeKey, TypeEntry> map;

	ReservedVec<TypeBuilder> builders;

	s32 first_free_builder_index;

	u32 scratch_capacity;

	void* scratch_begin;
};

static u32 hash_type(TypeKey key) noexcept
{
	return fnv1a_step(fnv1a_step(fnv1a(key.bytes), static_cast<u8>(key.tag)), static_cast<u8>(key.flags));
}

TypePool* create_type_pool(AllocPool* pool) noexcept
{
	TypePool* const types = static_cast<TypePool*>(alloc_from_pool(pool, sizeof(TypePool), alignof(TypePool)));

	types->map.init(1u << 24, 1u << 15, 1u << 31, 1u << 18);

	// Reserve id 0, so it can serve as an invalid value
	(void) type_entry_from_type(types, TypeTag::INVALID, TypeFlag::EMPTY, {});

	types->builders.init(1u << 16, 1u << 7);

	types->first_free_builder_index = -1;

	types->scratch_begin = types->builders.reserve_padded(TYPE_SCRATCH_CAPACITY);

	types->scratch_capacity = TYPE_SCRATCH_CAPACITY;

	return types;
}

void release_type_pool(TypePool* types) noexcept
{
	types->map.release();

	types->builders.release();
}

TypeEntry* type_entry_from_type(TypePool* types, TypeTag tag, TypeFlag flags, Range<byte> bytes) noexcept
{
	TypeKey key{ tag, flags, bytes };

	return types->map.value_from(key, hash_type(key));
}

TypeId id_from_type(TypePool* types, TypeTag tag, TypeFlag flags, Range<byte> bytes) noexcept
{
	TypeKey key{ tag, flags, bytes };

	return TypeId{ types->map.index_from(key, hash_type(key)) };
}

TypeEntry* type_entry_from_id(TypePool* types, TypeId id) noexcept
{
	return types->map.value_from(id.rep);
}

TypeId id_from_type_entry(TypePool* types, TypeEntry* entry) noexcept
{
	return TypeId{ types->map.index_from(entry) };
}

TypeId dealias_type_id(TypePool* types, TypeEntry* entry) noexcept
{
	while (entry->tag == TypeTag::Alias)
		entry = type_entry_from_id(types, entry->data<AliasType>()->aliased_id);

	return id_from_type_entry(types, entry);
}

TypeId dealias_type_id(TypePool* types, TypeId id) noexcept
{
	TypeEntry* const entry = type_entry_from_id(types, id);

	return dealias_type_id(types, entry);
}

TypeEntry* dealias_type_entry(TypePool* types, TypeEntry* entry) noexcept
{
	while (entry->tag == TypeTag::Alias)
		entry = type_entry_from_id(types, entry->data<AliasType>()->aliased_id);

	return entry;
}

TypeEntry* dealias_type_entry(TypePool* types, TypeId id) noexcept
{
	TypeEntry* const entry = type_entry_from_id(types, id);

	return dealias_type_entry(types, entry);
}

bool can_implicity_convert_from_to(TypePool* types, TypeId from, TypeId to) noexcept
{
	TypeEntry* const from_entry = dealias_type_entry(types, from);

	TypeEntry* const to_entry = dealias_type_entry(types, to);

	if (from == to)
		return true;

	switch (from_entry->tag)
	{
	case TypeTag::Array:
	{
		const TypeEntry* const from_element_entry = dealias_type_entry(types, from_entry->data<ArrayType>()->element_id);

		if (to_entry->tag == TypeTag::Slice)
		{
			const TypeEntry* const to_element_entry = dealias_type_entry(types, to_entry->data<SliceType>()->element_id);

			return from_element_entry == to_element_entry;
		}
		else if (to_entry->tag == TypeTag::Ptr && (to_entry->flags & TypeFlag::Ptr_IsMulti) != TypeFlag::EMPTY)
		{
			const TypeEntry* const to_element_entry = dealias_type_entry(types, to_entry->data<PtrType>()->pointee_id);

			return from_element_entry == to_element_entry;
		}
		else if (to_entry->tag == TypeTag::Array)
		{
			if (from_entry->data<ArrayType>()->count != to_entry->data<ArrayType>()->count)
				return false;

			const TypeEntry* const to_element_entry = dealias_type_entry(types, to_entry->data<ArrayType>()->element_id);

			return from_element_entry == to_element_entry;
		}
		else
		{
			return false;
		}
	}

	case TypeTag::Slice:
	{
		const TypeEntry* to_element_entry;

		if (to_entry->tag == TypeTag::Ptr && (to_entry->flags & TypeFlag::Ptr_IsMulti) != TypeFlag::EMPTY)
			to_element_entry = dealias_type_entry(types, to_entry->data<PtrType>()->pointee_id);
		else if (to_entry->tag == TypeTag::Slice)
			to_element_entry = dealias_type_entry(types, to_entry->data<SliceType>()->element_id);
		else
			return false;

		const TypeEntry* const from_element_entry = dealias_type_entry(types, from_entry->data<SliceType>()->element_id);
		
		return from_element_entry == to_element_entry;
	}

	case TypeTag::CompInteger:
		return to_entry->tag == TypeTag::Integer;

	case TypeTag::CompFloat:
		return to_entry->tag == TypeTag::Float;

	case TypeTag::CompString:
	{
		const TypeEntry* to_element_entry;

		if (to_entry->tag == TypeTag::Array)
			to_element_entry = dealias_type_entry(types, to_entry->data<ArrayType>()->element_id);
		else if (to_entry->tag == TypeTag::Slice)
		to_element_entry = dealias_type_entry(types, to_entry->data<SliceType>()->element_id);
		else if (to_entry->tag == TypeTag::Ptr && (to_entry->flags & TypeFlag::Ptr_IsMulti) != TypeFlag::EMPTY)
		to_element_entry = dealias_type_entry(types, to_entry->data<PtrType>()->pointee_id);
		else
			return false;

		return to_element_entry->tag == TypeTag::Integer
			&& to_element_entry->data<IntegerType>()->bits == 8
			&& (to_element_entry->flags & TypeFlag::Integer_IsSigned) == TypeFlag::EMPTY;
	}

	default:
		return false;
	}
}

OptPtr<TypeEntry> find_common_type_entry(TypePool* types, TypeEntry* a_orig, TypeEntry* b_orig) noexcept
{
	const TypeEntry* const a = dealias_type_entry(types, a_orig);

	const TypeEntry* const b = dealias_type_entry(types, b_orig);

	if (a == b)
	{
		return some(a_orig);
	}
	else if ((a->tag == TypeTag::CompInteger && b->tag == TypeTag::Integer) || (a->tag == TypeTag::Integer && b->tag == TypeTag::CompInteger))
	{
		return some(a->tag == TypeTag::CompInteger ? b_orig : a_orig);
	}
	else if ((a->tag == TypeTag::CompString && b->tag == TypeTag::Slice) || (a->tag == TypeTag::Slice && b->tag == TypeTag::CompString))
	{
		return some(a->tag == TypeTag::CompString ? b_orig : a_orig);
	}
	else if ((a->tag == TypeTag::CompFloat && b->tag == TypeTag::Float) || (a->tag == TypeTag::Float && b->tag == TypeTag::CompFloat))
	{
		return some(a->tag == TypeTag::CompFloat ? b_orig : a_orig);
	}
	else
	{
		return none<TypeEntry>();
	}
}



static s32 get_builder_next_offset(const TypeBuilder* builder) noexcept
{
	return static_cast<s32>(builder->members[0].internal_flags) >> 4;
}

static void set_builder_next_offset_destructive(TypeBuilder* builder, s32 next_offset) noexcept
{
	builder->members[0].internal_flags = next_offset << 4;
}

static void reset_builder_internal_flags(TypeBuilder* builder) noexcept
{
	builder->members[0].internal_flags = 0;
}

static void set_builder_next_offset(TypeBuilder* builder, s32 next_offset) noexcept
{
	const u32 internal_flags = builder->members[0].internal_flags;

	builder->members[0].internal_flags = (internal_flags & 0xF) | (next_offset << 4);
}

static u32 get_builder_used(const TypeBuilder* builder) noexcept
{
	return builder->members[0].internal_flags & 0xF;
}

static void set_builder_used(TypeBuilder* builder, u32 used) noexcept
{
	ASSERT_OR_IGNORE(used <= 0xF);

	builder->members[0].internal_flags = (builder->members[0].internal_flags & ~0xF) | used;
}

static void release_type_builder_internal(TypePool* types, TypeBuilder* head, TypeBuilder* tail) noexcept
{
	const s32 old_first = types->first_free_builder_index;

	set_builder_next_offset_destructive(tail, old_first < 0 ? 0 : static_cast<s32>(types->builders.begin() + old_first - tail));

	types->first_free_builder_index = static_cast<s32>(head - types->builders.begin());
}

static TypeBuilder* alloc_type_builder_internal(TypePool* types) noexcept
{
	TypeBuilder* builder;

	if (types->first_free_builder_index < 0)
	{
		builder = static_cast<TypeBuilder*>(types->builders.reserve_exact(sizeof(TypeBuilder)));
	}
	else
	{
		builder = types->builders.begin() + types->first_free_builder_index;

		const s32 next_free_offset = get_builder_next_offset(builder);

		if (next_free_offset == 0)
			types->first_free_builder_index = -1;
		else
			types->first_free_builder_index += next_free_offset;
	}

	return builder;
}


CompositeTypeBuilder* alloc_composite_type_builder(TypePool* types) noexcept
{
	TypeBuilder* const builder = alloc_type_builder_internal(types);

	reset_builder_internal_flags(builder);

	return reinterpret_cast<CompositeTypeBuilder*>(builder);
}

void add_composite_type_member(TypePool* types, CompositeTypeBuilder* composite_builder, CompositeTypeMember member) noexcept
{
	TypeBuilder* builder = reinterpret_cast<TypeBuilder*>(composite_builder);

	TypeBuilder* const head = builder;

	const s32 next_offset = get_builder_next_offset(builder);

	builder += next_offset;

	u32 used = get_builder_used(builder);

	if (used == array_count(builder->members))
	{
		TypeBuilder* const next = alloc_type_builder_internal(types);

		reset_builder_internal_flags(next);

		set_builder_next_offset(next, static_cast<s32>(builder - next));

		set_builder_next_offset(head, static_cast<s32>(next - head));

		builder = next;

		used = 0;
	}

	builder->members[used] = member;

	set_builder_used(builder, used + 1);
}

TypeId complete_composite_type(TypePool* types, ScopePool* scopes, CompositeTypeBuilder* composite_builder, u32 size, u32 alignment, u32 stride) noexcept
{
	TypeBuilder* builder = reinterpret_cast<TypeBuilder*>(composite_builder);

	CompositeType* const composite = static_cast<CompositeType*>(types->scratch_begin);

	TypeBuilder* const head = builder;

	u32 member_index = 0;

	while (true)
	{
		const u32 used = get_builder_used(builder);

		if (sizeof(CompositeTypeHeader) + (member_index + used) * sizeof(CompositeTypeMember) > types->scratch_capacity)
			panic("Type with %u members exceeds the supported maximum of %u members\n", member_index, (types->scratch_capacity - sizeof(CompositeTypeHeader)) / sizeof(CompositeTypeMember));

		memcpy(composite->members + member_index, builder->members, used * sizeof(CompositeTypeMember));

		composite->members[member_index].internal_flags = 0;

		member_index += used;

		const s32 next_offset = get_builder_next_offset(builder);

		if (next_offset == 0)
			break;

		builder += next_offset;
	}

	release_type_builder_internal(types, head, builder);

	composite->header.member_count = member_index;
	composite->header.size = size;
	composite->header.alignment = alignment;
	composite->header.stride = stride;
	composite->header.scope = nullptr;

	const TypeId result = id_from_type(types, TypeTag::Composite, TypeFlag::EMPTY, Range<byte>{ reinterpret_cast<byte*>(composite), sizeof(composite->header) + member_index * sizeof(composite->members[0]) });

	TypeEntry* const entry = type_entry_from_id(types, result);

	if (entry->data<CompositeType>()->header.scope == nullptr)
		init_composite_scope(scopes, composite);

	return result;
}


FuncTypeBuilder* alloc_func_type_builder(TypePool* types) noexcept
{
	TypeBuilder* const builder = alloc_type_builder_internal(types);

	reset_builder_internal_flags(builder);

	return reinterpret_cast<FuncTypeBuilder*>(builder);
}

void add_func_type_param(TypePool* types, FuncTypeBuilder* func_builder, FuncTypeParam param) noexcept
{
	TypeBuilder* builder = reinterpret_cast<TypeBuilder*>(func_builder);

	TypeBuilder* const head = builder;

	const s32 next_offset = get_builder_next_offset(builder);

	builder += next_offset;

	u32 used = get_builder_used(builder);

	if (used == array_count(builder->members))
	{
		TypeBuilder* const next = alloc_type_builder_internal(types);

		reset_builder_internal_flags(next);

		set_builder_next_offset(next, static_cast<s32>(builder - next));

		set_builder_next_offset(head, static_cast<s32>(next - head));

		builder = next;

		used = 0;
	}

	builder->params[used] = param;

	set_builder_used(builder, used + 1);
}

TypeId complete_func_type(TypePool* types, FuncTypeBuilder* func_builder, TypeId return_type, bool is_proc) noexcept
{
	FuncType* const func = static_cast<FuncType*>(types->scratch_begin);

	TypeBuilder* builder = reinterpret_cast<TypeBuilder*>(func_builder);

	TypeBuilder* const head = builder;

	u32 param_index = 0;

	while (true)
	{
		const u32 used = get_builder_used(builder);

		if (sizeof(FuncTypeHeader) + (param_index + used) * sizeof(FuncTypeParam) > types->scratch_capacity)
			panic("Type with %u members exceeds the supported maximum of %u members\n", param_index, (types->scratch_capacity - sizeof(FuncTypeHeader)) / sizeof(FuncTypeParam));

		memcpy(func->params + param_index, builder->params, used * sizeof(FuncTypeParam));

		func->params[param_index].internal_flags = 0;

		param_index += used;

		const s32 next_offset = get_builder_next_offset(builder);

		if (next_offset == 0)
			break;

		builder += next_offset;
	}

	release_type_builder_internal(types, head, builder);

	ASSERT_OR_IGNORE(param_index <= UINT16_MAX);

	func->header.parameter_count = static_cast<u16>(param_index);
	func->header.return_type_id = return_type;
	func->header.is_proc = is_proc;

	return id_from_type(types, TypeTag::Func, TypeFlag::EMPTY, Range<byte>{ reinterpret_cast<byte*>(func), sizeof(func->header) + param_index * sizeof(func->params[0]) });
}
