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

const BuiltinTypeIds* get_builtin_type_ids(const TypePool* types) noexcept
{
	return &types->builtin_type_ids;
}
