#ifndef TREE_SCHEMA_INCLUDE_GUARD
#define TREE_SCHEMA_INCLUDE_GUARD

#include "../types.hpp"
#include "../range.hpp"
#include "../opt.hpp"

struct TreeSchemaAllocator
{
	byte* curr;

	byte* commit_end;

	byte* reserve_end;

	u32 commit_increment;

	u32 reserve;
};



struct TreeSchemaTable;

struct TreeSchemaArray;

enum class TreeSchemaValueTag : u8
{
	INVALID = 0,
	Table,
	Array,
	Integer,
	String,
	Boolean,
};

struct TreeSchemaValue
{
	AttachmentRange<char8, TreeSchemaValueTag> name_and_tag;

	u32 source_line;

	u32 source_column;

	union
	{
		s64 integer;

		bool boolean;

		Range<char8> string;

		TreeSchemaTable* table;

		TreeSchemaArray* array;
	} value;
};

enum class TreeSchemaTableAddResult : u8
{
	Ok,
	NoMemory,
	DuplicateName,
};

struct TreeSchemaTableIterator
{
	const TreeSchemaValue* curr;

	const TreeSchemaValue* end;
};



TreeSchemaAllocator ts_allocator_create(u32 reserve, u32 commit_increment) noexcept;

void ts_allocator_release(TreeSchemaAllocator alloc) noexcept;



Maybe<TreeSchemaTable*> ts_table_create(TreeSchemaAllocator* alloc) noexcept;

TreeSchemaTableAddResult ts_table_add_value(TreeSchemaAllocator* alloc, TreeSchemaTable* table, TreeSchemaValue value) noexcept;

Maybe<TreeSchemaValue*> ts_table_find_value(TreeSchemaTable* table, Range<char8> name) noexcept;

Maybe<const TreeSchemaValue*> ts_table_find_value(const TreeSchemaTable* table, Range<char8> name) noexcept;

void ts_table_optimize_for_lookup(const TreeSchemaTable* table) noexcept;

u32 ts_table_count(const TreeSchemaTable* table) noexcept;

TreeSchemaTableIterator ts_table_values(const TreeSchemaTable* table) noexcept;



bool has_next(const TreeSchemaTableIterator* it) noexcept;

const TreeSchemaValue* next(TreeSchemaTableIterator* it) noexcept;



Maybe<TreeSchemaArray*> ts_array_create(TreeSchemaAllocator* alloc) noexcept;

bool ts_array_add_value(TreeSchemaAllocator* alloc, TreeSchemaArray* array, TreeSchemaValue value) noexcept;

u32 ts_array_count(const TreeSchemaArray* array) noexcept;

TreeSchemaValue* ts_array_at(TreeSchemaArray* array, u32 index) noexcept;

const TreeSchemaValue* ts_array_at(const TreeSchemaArray* array, u32 index) noexcept;



bool ts_string_create(TreeSchemaAllocator* alloc, Range<char8> data, Range<char8>* out) noexcept;

bool ts_string_append(TreeSchemaAllocator* alloc, Range<char8> data, Range<char8>* inout_string) noexcept;



bool ts_value_from_table(TreeSchemaAllocator* alloc, Range<char8> name, u32 source_line, u32 source_column, TreeSchemaTable* table, TreeSchemaValue* out) noexcept;

bool ts_value_from_array(TreeSchemaAllocator* alloc, Range<char8> name, u32 source_line, u32 source_column, TreeSchemaArray* array, TreeSchemaValue* out) noexcept;

bool ts_value_from_integer(TreeSchemaAllocator* alloc, Range<char8> name, u32 source_line, u32 source_column, s64 integer, TreeSchemaValue* out) noexcept;

bool ts_value_from_string(TreeSchemaAllocator* alloc, Range<char8> name, u32 source_line, u32 source_column, Range<char8> string, TreeSchemaValue* out) noexcept;

bool ts_value_from_boolean(TreeSchemaAllocator* alloc, Range<char8> name, u32 source_line, u32 source_column, bool boolean, TreeSchemaValue* out) noexcept;



TreeSchemaValue ts_value_from_table_unnamed(u32 source_line, u32 source_column, TreeSchemaTable* table) noexcept;

TreeSchemaValue ts_value_from_array_unnamed(u32 source_line, u32 source_column, TreeSchemaArray* array) noexcept;

TreeSchemaValue ts_value_from_integer_unnamed(u32 source_line, u32 source_column, s64 integer) noexcept;

TreeSchemaValue ts_value_from_string_unnamed(u32 source_line, u32 source_column, Range<char8> string) noexcept;

TreeSchemaValue ts_value_from_boolean_unnamed(u32 source_line, u32 source_column, bool boolean) noexcept;



void ts_value_mark_visited(TreeSchemaValue* value) noexcept;

#endif // TREE_SCHEMA_INCLUDE_GUARD
