#include "pass_data.hpp"

#include "infra/container.hpp"
#include "infra/hash.hpp"

struct TypePool
{
	IndexMap<TypeKey, TypeEntry> map;

	BuiltinTypeIds builtin_type_ids;
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

	types->builtin_type_ids.comp_integer_type_id = id_from_type(types, TypeTag::CompInteger, TypeFlag::EMPTY, {});

	types->builtin_type_ids.comp_float_type_id = id_from_type(types, TypeTag::CompFloat, TypeFlag::EMPTY, {});

	types->builtin_type_ids.comp_string_type_id = id_from_type(types, TypeTag::CompString, TypeFlag::EMPTY, {});

	types->builtin_type_ids.type_type_id = id_from_type(types, TypeTag::Type, TypeFlag::EMPTY, {});

	types->builtin_type_ids.void_type_id = id_from_type(types, TypeTag::Void, TypeFlag::EMPTY, {});

	types->builtin_type_ids.bool_type_id = id_from_type(types, TypeTag::Boolean, TypeFlag::EMPTY, {});

	return types;
}

void release_type_pool(TypePool* types) noexcept
{
	types->map.release();
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

const BuiltinTypeIds* get_builtin_type_ids(const TypePool* types) noexcept
{
	return &types->builtin_type_ids;
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

OptPtr<TypeEntry> find_common_type_entry(TypePool* types, TypeEntry* a, TypeEntry* b) noexcept
{
	a = dealias_type_entry(types, a);

	b = dealias_type_entry(types, b);

	if (a == b)
		return some(a);

	// TODO: Handle e.g. CompInteger-to-u64 conversion
	return none<TypeEntry>();
}
