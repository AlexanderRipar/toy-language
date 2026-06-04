#include "tree_schema.hpp"

#include "../math.hpp"
#include "../inplace_sort.hpp"
#include "../minos/minos.hpp"

static constexpr u16 INITIAL_TABLE_CAPACITY = 2;

static constexpr u16 MIN_TABLE_SORT_SIZE = 16;

struct TreeSchemaTable
{
	u16 used;
	
	u16 capacity;

	u16 sorted;

	TreeSchemaValue* entries;
};

struct TreeSchemaArray
{
	u32 used;
	
	u32 capacity;

	TreeSchemaValue* entries;
};



static s32 string_compare(Range<char8> a, Range<char8> b) noexcept
{
	const u64 a_count = a.count();

	const u64 b_count = b.count();

	const u64 min_count = a_count < b_count ? a_count : b_count;

	const s32 rst = memcmp(a.begin(), b.begin(), min_count);

	if (rst != 0)
		return rst;
	else if (a_count < b_count)
		return -1;
	else if (a_count > b_count)
		return 1;
	else
		return 0;
}



static bool heap_ensure_commit(TreeSchemaAllocator* alloc, byte* new_curr) noexcept
{
	if (new_curr <= alloc->commit_end)
		return true;

	byte* const new_commit_end = reinterpret_cast<byte*>(reinterpret_cast<u64>(new_curr + alloc->commit_increment - 1) & ~static_cast<u64>(alloc->commit_increment - 1));

	if (alloc->reserve_end < new_commit_end)
		return false;

	if (!minos::mem_commit(alloc->commit_end, new_commit_end - alloc->commit_end))
		return false;

	alloc->commit_end = new_commit_end;

	return true;
}

static Maybe<void*> heap_alloc(TreeSchemaAllocator* alloc, u64 size) noexcept
{
	size = (size + 7) & ~static_cast<u64>(7);

	byte* const new_curr = alloc->curr + size;

	if (!heap_ensure_commit(alloc, new_curr))
		return none<void*>();

	ASSERT_OR_IGNORE(new_curr < alloc->commit_end);

	void* const allocation = alloc->curr;

	alloc->curr = new_curr;

	return some(allocation);
}

static Maybe<void*> heap_realloc(TreeSchemaAllocator* alloc, void* old_allocation, u64 old_size, u64 new_size) noexcept
{
	ASSERT_OR_IGNORE(old_size < new_size);

	byte* const old_allocation_end = reinterpret_cast<byte*>((reinterpret_cast<u64>(old_allocation) + old_size + 7) & ~static_cast<u64>(7));

	ASSERT_OR_IGNORE(alloc->curr >= old_allocation_end);

	if (alloc->curr != old_allocation_end)
	{
		const Maybe<void*> new_allocation = heap_alloc(alloc, new_size);

		if (is_none(new_allocation))
			return none<void*>();

		memcpy(get(new_allocation), old_allocation, old_size);

		return new_allocation;
	}

	byte* const new_curr = reinterpret_cast<byte*>((reinterpret_cast<u64>(old_allocation) + new_size - old_size + 7) & ~static_cast<u64>(7));

	if (!heap_ensure_commit(alloc, new_curr))
		return none<void*>();

	alloc->curr = new_curr;

	return some(old_allocation);
}



TreeSchemaAllocator ts_allocator_create(u32 reserve, u32 commit_increment) noexcept
{
	ASSERT_OR_IGNORE(reserve != 0);

	ASSERT_OR_IGNORE(commit_increment != 0);

	reserve = next_pow2(reserve);

	commit_increment = next_pow2(commit_increment);

	byte* const memory = static_cast<byte*>(minos::mem_reserve(reserve));

	TreeSchemaAllocator rst{};
	rst.curr = memory;
	rst.commit_end = memory;
	rst.reserve_end = memory + reserve;
	rst.commit_increment = commit_increment;
	rst.reserve = reserve;

	return rst;
}

void ts_allocator_release(TreeSchemaAllocator alloc) noexcept
{
	minos::mem_unreserve(alloc.reserve_end - alloc.reserve, alloc.reserve);
}



Maybe<TreeSchemaTable*> ts_table_create(TreeSchemaAllocator* alloc) noexcept
{
	const Maybe<void*> allocation = heap_alloc(alloc, sizeof(TreeSchemaTable) + INITIAL_TABLE_CAPACITY * sizeof(TreeSchemaValue));

	if (is_none(allocation))
		return none<TreeSchemaTable*>();

	TreeSchemaTable* const table = static_cast<TreeSchemaTable*>(get(allocation));
	table->used = 0;
	table->capacity = INITIAL_TABLE_CAPACITY;
	table->entries = reinterpret_cast<TreeSchemaValue*>(table + 1);

	return some(table);
}

TreeSchemaTableAddResult ts_table_add_value(TreeSchemaAllocator* alloc, TreeSchemaTable* table, TreeSchemaValue value) noexcept
{
	ASSERT_OR_IGNORE(value.name_and_tag.count() != 0);

	const u32 used = table->used;

	if (used == table->capacity)
	{
		if (static_cast<u32>(used) * 2 > UINT16_MAX)
			return TreeSchemaTableAddResult::NoMemory;

		const Maybe<void*> allocation = heap_realloc(alloc, table->entries, used * sizeof(TreeSchemaValue), 2 * used * sizeof(TreeSchemaValue));

		if (is_none(allocation))
			return TreeSchemaTableAddResult::NoMemory;

		TreeSchemaValue* const new_entries = static_cast<TreeSchemaValue*>(get(allocation));
		
		memcpy(new_entries, table->entries, used * sizeof(TreeSchemaValue));

		table->capacity = static_cast<u16>(used * 2);
		table->entries = new_entries;
	}

	const Maybe<TreeSchemaValue*> existing_value = ts_table_find_value(table, value.name_and_tag.range());

	if (is_some(existing_value))
		return TreeSchemaTableAddResult::DuplicateName;

	ASSERT_OR_IGNORE(table->used < table->capacity);

	table->entries[table->used] = value;
	table->used += 1;

	return TreeSchemaTableAddResult::Ok;
}

Maybe<TreeSchemaValue*> ts_table_find_value(TreeSchemaTable* table, Range<char8> name) noexcept
{
	const Maybe<const TreeSchemaValue*> rst = ts_table_find_value(static_cast<const TreeSchemaTable*>(table), name);

	if (is_none(rst))
		return none<TreeSchemaValue*>();

	return some(const_cast<TreeSchemaValue*>(get(rst)));
}

Maybe<const TreeSchemaValue*> ts_table_find_value(const TreeSchemaTable* table, Range<char8> name) noexcept
{
	ASSERT_OR_IGNORE(name.count() != 0);

	const u64 used = table->used;

	const u64 sorted = table->sorted;

	if (table->sorted != 0)
	{
		u64 lo = 0;

		u64 hi = sorted - 1;

		while (lo <= hi)
		{
			const u64 mid = (lo + hi) >> 1;

			const s32 ordering = string_compare(name, table->entries[mid].name_and_tag.range());

			if (ordering < 0)
				hi = mid - 1;
			else if (ordering > 0)
				lo = mid + 1;
			else
				return some<const TreeSchemaValue*>(table->entries + mid);
		}
	}

	for (u64 i = sorted; i != used; ++i)
	{
		const Range<char8> existing_name = table->entries[i].name_and_tag.range();

		if (name.count() != existing_name.count() || !range::mem_equal(name, existing_name))
			continue;

		return some<const TreeSchemaValue*>(table->entries + i);
	}

	return none<const TreeSchemaValue*>();
}

void ts_table_optimize_for_lookup(const TreeSchemaTable* table) noexcept
{
	struct TreeSchemaValueByNameComparator
	{
		static s32 compare(const TreeSchemaValue& a, const TreeSchemaValue& b)
		{
			return string_compare(a.name_and_tag.range(), b.name_and_tag.range());
		}
	};

	if (table->sorted == table->used || table->used < MIN_TABLE_SORT_SIZE)
		return;

	inplace_sort<TreeSchemaValue, TreeSchemaValueByNameComparator>({ table->entries, table->used });
}

u32 ts_table_count(const TreeSchemaTable* table) noexcept
{
	return table->used;
}

TreeSchemaTableIterator ts_table_values(const TreeSchemaTable* table) noexcept
{
	return TreeSchemaTableIterator{ table->entries, table->entries + table->used };
}



bool has_next(const TreeSchemaTableIterator* it) noexcept
{
	ASSERT_OR_IGNORE(it->curr <= it->end);

	return it->curr != it->end;
}

const TreeSchemaValue* next(TreeSchemaTableIterator* it) noexcept
{
	ASSERT_OR_IGNORE(has_next(it));

	const TreeSchemaValue* const result = it->curr;

	it->curr += 1;

	return result;
}



Maybe<TreeSchemaArray*> ts_array_create(TreeSchemaAllocator* alloc) noexcept
{
	const Maybe<void*> allocation = heap_alloc(alloc, sizeof(TreeSchemaTable) + INITIAL_TABLE_CAPACITY * sizeof(TreeSchemaValue));

	if (is_none(allocation))
		return none<TreeSchemaArray*>();

	TreeSchemaArray* const table = static_cast<TreeSchemaArray*>(get(allocation));
	table->used = 0;
	table->capacity = INITIAL_TABLE_CAPACITY;
	table->entries = reinterpret_cast<TreeSchemaValue*>(table + 1);

	return some(table);
}

bool ts_array_add_value(TreeSchemaAllocator* alloc, TreeSchemaArray* array, TreeSchemaValue value) noexcept
{
	ASSERT_OR_IGNORE(value.name_and_tag.count() == 0);

	const u32 used = array->used;

	if (used == array->capacity)
	{
		const Maybe<void*> allocation = heap_realloc(alloc, array->entries, used * sizeof(TreeSchemaValue), 2 * used * sizeof(TreeSchemaValue));

		if (is_none(allocation))
			return false;

		TreeSchemaValue* const new_entries = static_cast<TreeSchemaValue*>(get(allocation));
		
		memcpy(new_entries, array->entries, used * sizeof(TreeSchemaValue));

		array->capacity = used * 2;
		array->entries = new_entries;
	}

	ASSERT_OR_IGNORE(array->used < array->capacity);

	array->entries[array->used] = value;
	array->used += 1;

	return true;
}

u32 ts_array_count(const TreeSchemaArray* array) noexcept
{
	return array->used;
}

TreeSchemaValue* ts_array_at(TreeSchemaArray* array, u32 index) noexcept
{
	ASSERT_OR_IGNORE(index < array->used);

	return array->entries + index;
}

const TreeSchemaValue* ts_array_at(const TreeSchemaArray* array, u32 index) noexcept
{
	ASSERT_OR_IGNORE(index < array->used);

	return array->entries + index;
}



bool ts_string_create(TreeSchemaAllocator* alloc, Range<char8> data, Range<char8>* out) noexcept
{
	const Maybe<void*> allocation = heap_alloc(alloc, data.count());

	if (is_none(allocation))
		return false;

	char8* const begin = static_cast<char8*>(get(allocation));

	memcpy(begin, data.begin(), data.count());

	*out = Range<char8>{ begin, data.count() };

	return true;
}

bool ts_string_append(TreeSchemaAllocator* alloc, Range<char8> data, Range<char8>* inout_string) noexcept
{
	const Maybe<void*> allocation = heap_realloc(alloc, const_cast<char8*>(inout_string->begin()), inout_string->count(), inout_string->count() + data.count());

	if (is_none(allocation))
		return false;

	char8* const begin = static_cast<char8*>(get(allocation));

	memcpy(begin + inout_string->count(), data.begin(), data.count());

	*inout_string = Range<char8>{ begin, inout_string->count() + data.count() };

	return true;
}



bool ts_value_from_table(TreeSchemaAllocator* alloc, Range<char8> name, u32 source_line, u32 source_column, TreeSchemaTable* table, TreeSchemaValue* out) noexcept
{
	Range<char8> interned_name;

	if (!ts_string_create(alloc, name, &interned_name))
		return false;

	TreeSchemaValue rst{};
	rst.name_and_tag = { interned_name, TreeSchemaValueTag::Table };
	rst.source_line = source_line;
	rst.source_column = source_column;
	rst.value.table = table;

	*out = rst;

	return true;
}

bool ts_value_from_array(TreeSchemaAllocator* alloc, Range<char8> name, u32 source_line, u32 source_column, TreeSchemaArray* array, TreeSchemaValue* out) noexcept
{
	Range<char8> interned_name;

	if (!ts_string_create(alloc, name, &interned_name))
		return false;

	TreeSchemaValue rst{};
	rst.name_and_tag = { interned_name, TreeSchemaValueTag::Array };
	rst.source_line = source_line;
	rst.source_column = source_column;
	rst.value.array = array;

	*out = rst;

	return true;
}

bool ts_value_from_integer(TreeSchemaAllocator* alloc, Range<char8> name, u32 source_line, u32 source_column, s64 integer, TreeSchemaValue* out) noexcept
{
	Range<char8> interned_name;

	if (!ts_string_create(alloc, name, &interned_name))
		return false;

	TreeSchemaValue rst{};
	rst.name_and_tag = { interned_name, TreeSchemaValueTag::Integer };
	rst.source_line = source_line;
	rst.source_column = source_column;
	rst.value.integer = integer;

	*out = rst;

	return true;
}

bool ts_value_from_string(TreeSchemaAllocator* alloc, Range<char8> name, u32 source_line, u32 source_column, Range<char8> string, TreeSchemaValue* out) noexcept
{
	Range<char8> interned_name;

	if (!ts_string_create(alloc, name, &interned_name))
		return false;

	TreeSchemaValue rst{};
	rst.name_and_tag = { interned_name, TreeSchemaValueTag::String };
	rst.source_line = source_line;
	rst.source_column = source_column;
	rst.value.string = string;

	*out = rst;

	return true;
}

bool ts_value_from_boolean(TreeSchemaAllocator* alloc, Range<char8> name, u32 source_line, u32 source_column, bool boolean, TreeSchemaValue* out) noexcept
{
	Range<char8> interned_name;

	if (!ts_string_create(alloc, name, &interned_name))
		return false;

	TreeSchemaValue rst{};
	rst.name_and_tag = { interned_name, TreeSchemaValueTag::Boolean };
	rst.source_line = source_line;
	rst.source_column = source_column;
	rst.value.boolean = boolean;

	*out = rst;

	return true;
}



TreeSchemaValue ts_value_from_table_unnamed(u32 source_line, u32 source_column, TreeSchemaTable* table) noexcept
{
	TreeSchemaValue rst{};
	rst.name_and_tag = { Range<char8>{}, TreeSchemaValueTag::Table };
	rst.source_line = source_line;
	rst.source_column = source_column;
	rst.value.table = table;

	return rst;
}

TreeSchemaValue ts_value_from_array_unnamed(u32 source_line, u32 source_column, TreeSchemaArray* array) noexcept
{
	TreeSchemaValue rst{};
	rst.name_and_tag = { Range<char8>{}, TreeSchemaValueTag::Array };
	rst.source_line = source_line;
	rst.source_column = source_column;
	rst.value.array = array;

	return rst;
}

TreeSchemaValue ts_value_from_integer_unnamed(u32 source_line, u32 source_column, s64 integer) noexcept
{
	TreeSchemaValue rst{};
	rst.name_and_tag = { Range<char8>{}, TreeSchemaValueTag::Integer };
	rst.source_line = source_line;
	rst.source_column = source_column;
	rst.value.integer = integer;

	return rst;
}

TreeSchemaValue ts_value_from_string_unnamed(u32 source_line, u32 source_column, Range<char8> string) noexcept
{
	TreeSchemaValue rst{};
	rst.name_and_tag = { Range<char8>{}, TreeSchemaValueTag::String };
	rst.source_line = source_line;
	rst.source_column = source_column;
	rst.value.string = string;

	return rst;
}

TreeSchemaValue ts_value_from_boolean_unnamed(u32 source_line, u32 source_column, bool boolean) noexcept
{
	TreeSchemaValue rst{};
	rst.name_and_tag = { Range<char8>{}, TreeSchemaValueTag::Boolean };
	rst.source_line = source_line;
	rst.source_column = source_column;
	rst.value.boolean = boolean;

	return rst;
}
