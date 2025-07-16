#include "core.hpp"

#include "../infra/container.hpp"

struct TypeListHeader
{
	u32 count;

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
	TypeId ids[];
	#if COMPILER_MSVC
	#pragma warning(pop)
	#elif COMPILER_CLANG
	#pragma clang diagnostic pop
	#elif COMPILER_GCC
	#pragma GCC diagnostic pop
	#endif
};

struct TypeListPool
{
	ReservedVec<TypeListHeader> permanent;

	ReservedHeap<3, 16> transient;
};



static TypeListHeader* get_type_list_header(TypeList list) noexcept
{
	TypeListHeader* const header = reinterpret_cast<TypeListHeader*>(reinterpret_cast<u32*>(list.begin()) - 1);

	ASSERT_OR_IGNORE(header->count == list.count());

	return header;
}

static void dealloc_type_list_header(TypeListPool* lists, TypeListHeader* header) noexcept
{
	MutRange<byte> memory = MutRange<byte>{ reinterpret_cast<byte*>(header), sizeof(TypeListHeader) + header->count * sizeof(TypeId) };

	lists->transient.dealloc(memory);
}



TypeListPool* create_type_list_pool(AllocPool* alloc) noexcept
{
	TypeListPool* const lists = static_cast<TypeListPool*>(alloc_from_pool(alloc, sizeof(TypeListPool), alignof(TypeListPool)));

	lists->permanent.init(1 << 28, 1 << 14);

	static constexpr u32 transient_capacities[14] = {
		16384, 8192, 4096, 2048,
		 1024,  512,  512,  512,
		  512,  256,  256,  256,
		  128,   64,
	};

	static constexpr u32 transient_commits[14] = {
		1024, 512, 256, 128,
		  64,  32,  16,   8,
		   4,   2,   1,   1,
		   1,   1,
	};

	lists->transient.init(Range{ transient_capacities }, Range{ transient_commits });

	lists->permanent.reserve_exact(sizeof(TypeListHeader));

	return lists;
}

void release_type_list_pool(TypeListPool* lists) noexcept
{
	lists->permanent.release();

	lists->transient.release();
}

TypeList create_permanent_type_list(TypeListPool* lists, u32 count) noexcept
{
	TypeListHeader* const header = static_cast<TypeListHeader*>(lists->permanent.reserve_exact(sizeof(TypeListHeader) + count * sizeof(TypeId)));

	header->count = count;

	return MutAttachmentRange<TypeId, bool>{ header->ids, count, true };
}

TypeList create_transient_type_list(TypeListPool* lists, u32 count) noexcept
{
	MutRange<byte> bytes = lists->transient.alloc(sizeof(TypeListHeader) + count * sizeof(TypeId));

	TypeListHeader* const header = reinterpret_cast<TypeListHeader*>(bytes.begin());

	return MutAttachmentRange<TypeId, bool>{ header->ids, count, false };
}

TypeList make_type_list_permanent(TypeListPool* lists,TypeList transient) noexcept
{
	ASSERT_OR_IGNORE(!transient.attachment());

	TypeListHeader* const permanent_header = static_cast<TypeListHeader*>(lists->permanent.reserve_exact(static_cast<u32>(sizeof(TypeListHeader) + transient.count() * sizeof(TypeId))));

	TypeListHeader* const transient_header = get_type_list_header(transient);

	memcpy(permanent_header, transient_header, sizeof(TypeListHeader) + transient.count() * sizeof(TypeId));

	dealloc_type_list_header(lists, transient_header);

	return MutAttachmentRange<TypeId, bool>{ permanent_header->ids, static_cast<u32>(transient.count()), true };
}

void release_transient_type_list(TypeListPool* lists, TypeList transient) noexcept
{
	ASSERT_OR_IGNORE(!transient.attachment());

	TypeListHeader* const header = get_type_list_header(transient);

	dealloc_type_list_header(lists, header);
}

TypeList type_list_from_id(TypeListPool* lists, TypeListId id) noexcept
{
	ASSERT_OR_IGNORE(id != TypeListId::INVALID);

	const bool is_permanent = static_cast<s32>(id) > 0;

	TypeListHeader* const header = is_permanent
		? lists->permanent.begin() - static_cast<u32>(id)
		: static_cast<TypeListHeader*>(lists->transient.begin()) - static_cast<s32>(id);

	return TypeList{ header->ids, header->count, is_permanent };
}

TypeListId id_from_type_list(TypeListPool* lists, TypeList list) noexcept
{
	TypeListHeader* const header = get_type_list_header(list);

	if (list.attachment())
	{
		return static_cast<TypeListId>(header - lists->permanent.begin());
	}
	else
	{
		return static_cast<TypeListId>(-static_cast<s32>(header - static_cast<TypeListHeader*>(lists->transient.begin())));
	}
}
