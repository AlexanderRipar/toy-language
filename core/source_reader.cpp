#include "core.hpp"
#include "structure.hpp"

#include "../infra/types.hpp"
#include "../infra/assert.hpp"
#include "../infra/panic.hpp"
#include "../infra/range.hpp"
#include "../infra/hash.hpp"
#include "../infra/container/id_map.hpp"
#include "../infra/minos/minos.hpp"

#include <cstdlib>
#include <cstddef>
#include <atomic>

static u32 hash_file_identity(u64 file_id, u32 device_id) noexcept
{
	return fnv1a_step(fnv1a(range::from_object_bytes(&file_id)), range::from_object_bytes(&device_id));
}

#ifdef COMPILER_MSVC
	#pragma warning(push)
	#pragma warning(disable : 4324) // structure was padded due to alignment specifier
#endif
struct alignas(8) SourceFileByPathEntry
{
	u32 m_hash;

	u32 path_bytes;

	u32 id_entry_index;

	#if COMPILER_GCC
		#pragma GCC diagnostic push
		#pragma GCC diagnostic ignored "-Wpedantic" // ISO C++ forbids flexible array member
	#endif
	char8 path[];
	#if COMPILER_GCC
		#pragma GCC diagnostic pop
	#endif

	u32 hash() const noexcept
	{
		return m_hash;
	}

	bool is_equal_to_key(Range<char8> key, u32 key_hash) const noexcept
	{
		return m_hash == key_hash && key.count() == path_bytes && memcmp(key.begin(), path, key.count()) == 0;
	}
};
#ifdef COMPILER_MSVC
	#pragma warning(pop)
#endif

struct alignas(8) SourceFileByIdEntry
{
	u64 file_id;

	u32 device_id;

	u32 path_entry_index;

	SourceFile data;

	u32 hash() const noexcept
	{
		return hash_file_identity(file_id, device_id);
	}

	bool is_equal_to_key(minos::FileIdentity key, [[maybe_unused]] u32 key_hash) const noexcept
	{
		return device_id == key.volume_serial && file_id == key.index;
	}
};

static constexpr u64 KNOWN_FILES_BY_PATH_LOOKUP_RESERVE = decltype(SourceReader::known_files_by_path)::lookups_memory_size(1 << 20);

static constexpr u32 KNOWN_FILES_BY_PATH_LOOKUP_INITIAL_COMMIT_COUNT = static_cast<u32>(1) << 10;

static constexpr u32 KNOWN_FILES_BY_PATH_VALUES_RESERVE = (static_cast<u32>(1) << 19) * alignof(SourceFileByPathEntry);

static constexpr u32 KNOWN_FILES_BY_PATH_VALUES_COMMIT_INCREMENT_COUNT = (static_cast<u32>(1) << 11) * alignof(SourceFileByPathEntry);

static constexpr u64 KNOWN_FILES_BY_IDENTITY_LOOKUP_RESERVE = decltype(SourceReader::known_files_by_identity)::lookups_memory_size(1 << 19);

static constexpr u32 KNOWN_FILES_BY_IDENTITY_LOOKUP_INITIAL_COMMIT_COUNT = static_cast<u32>(1) << 10;

static constexpr u32 KNOWN_FILES_BY_IDENTITY_VALUES_RESERVE = (static_cast<u32>(1) << 18) * sizeof(SourceFileByIdEntry);

static constexpr u32 KNOWN_FILES_BY_IDENTITY_VALUES_COMMIT_INCREMENT_COUNT = static_cast<u32>(1) << 11;


bool SourceFileByPathIterator::has_next() const noexcept
{
	ASSERT_OR_IGNORE(curr <= end);

	return curr != end;
}

SourceFileByPathEntry* SourceFileByPathIterator::next() noexcept
{
	ASSERT_OR_IGNORE(curr < end);

	SourceFileByPathEntry* const result = reinterpret_cast<SourceFileByPathEntry*>(core->reader.path_entries.begin() + curr * alignof(SourceFileByPathEntry));

	const u32 raw_size = static_cast<u32>(offsetof(SourceFileByPathEntry, path) + result->path_bytes);

	const u32 size = (raw_size + alignof(SourceFileByPathEntry) - 1) & ~(alignof(SourceFileByPathEntry) - 1);

	curr += size / alignof(SourceFileByPathEntry);

	return result;
}



SourceFileByPathEntry* SourceFileByPathAlloc::value_from_id(u32 id) noexcept
{
	ASSERT_OR_IGNORE(id != 0);

	ASSERT_OR_IGNORE(id * alignof(SourceFileByPathEntry) < core->reader.path_entries.used());

	return reinterpret_cast<SourceFileByPathEntry*>(core->reader.path_entries.begin() + id * alignof(SourceFileByPathEntry));
}

const SourceFileByPathEntry* SourceFileByPathAlloc::value_from_id(u32 id) const noexcept
{
	ASSERT_OR_IGNORE(id != 0);

	ASSERT_OR_IGNORE(id * alignof(SourceFileByPathEntry) < core->reader.path_entries.used());

	return reinterpret_cast<SourceFileByPathEntry*>(core->reader.path_entries.begin() + id * alignof(SourceFileByPathEntry));
}

u32 SourceFileByPathAlloc::id_from_value(const SourceFileByPathEntry* value) const noexcept
{
	ASSERT_OR_IGNORE(reinterpret_cast<const byte*>(value) > core->reader.path_entries.begin());

	ASSERT_OR_IGNORE(reinterpret_cast<const byte*>(value) < core->reader.path_entries.end());

	return static_cast<u32>((reinterpret_cast<const byte*>(value) - core->reader.path_entries.begin()) / alignof(SourceFileByPathEntry));
}

SourceFileByPathIterator SourceFileByPathAlloc::values() noexcept
{
	SourceFileByPathIterator it;
	it.core = core;
	it.curr = 1;
	it.end = core->reader.path_entries.used() / alignof(SourceFileByPathEntry);

	return it;
}

SourceFileByPathEntry* SourceFileByPathAlloc::alloc(Range<char8> key, u32 key_hash) noexcept
{
	ASSERT_OR_IGNORE(key.count() < UINT32_MAX);

	const u32 raw_size = static_cast<u32>(offsetof(SourceFileByPathEntry, path) + key.count());

	const u32 size = (raw_size + alignof(SourceFileByPathEntry) - 1) & ~(alignof(SourceFileByPathEntry) - 1);

	SourceFileByPathEntry* const result = reinterpret_cast<SourceFileByPathEntry*>(core->reader.path_entries.reserve(size));
	result->m_hash = key_hash;
	result->path_bytes = static_cast<u32>(key.count());
	result->id_entry_index = 0;
	memcpy(result->path, key.begin(), key.count());

	return result;
}

void SourceFileByPathAlloc::dealloc([[maybe_unused]] u32 id) noexcept
{
	ASSERT_UNREACHABLE;
}



bool SourceFileByIdIterator::has_next() const noexcept
{
	ASSERT_OR_IGNORE(curr <= end);

	return curr != end;
}

SourceFileByIdEntry* SourceFileByIdIterator::next() noexcept
{
	ASSERT_OR_IGNORE(curr < end);

	SourceFileByIdEntry* const result = core->reader.id_entries.begin() + curr;

	curr += 1;

	return result;
}



SourceFileByIdEntry* SourceFileByIdAlloc::value_from_id(u32 id) noexcept
{
	ASSERT_OR_IGNORE(id != 0);

	ASSERT_OR_IGNORE(id < core->reader.id_entries.used());

	return core->reader.id_entries.begin() + id;
}

const SourceFileByIdEntry* SourceFileByIdAlloc::value_from_id(u32 id) const noexcept
{
	ASSERT_OR_IGNORE(id != 0);

	ASSERT_OR_IGNORE(id < core->reader.id_entries.used());

	return core->reader.id_entries.begin() + id;
}

u32 SourceFileByIdAlloc::id_from_value(const SourceFileByIdEntry* value) const noexcept
{
	ASSERT_OR_IGNORE(value > core->reader.id_entries.begin());

	ASSERT_OR_IGNORE(value < core->reader.id_entries.end());

	return static_cast<u32>(value - core->reader.id_entries.begin());
}

SourceFileByIdIterator SourceFileByIdAlloc::values() noexcept
{
	SourceFileByIdIterator it;
	it.core = core;
	it.curr = 1;
	it.end = core->reader.id_entries.used();

	return it;
}

SourceFileByIdEntry* SourceFileByIdAlloc::alloc(minos::FileIdentity key, [[maybe_unused]] u32 key_hash) noexcept
{
	SourceFileByIdEntry* const result = core->reader.id_entries.reserve();
	result->device_id = key.volume_serial;
	result->file_id = key.index;

	return result;
}

void SourceFileByIdAlloc::dealloc([[maybe_unused]] u32 id) noexcept
{
	ASSERT_UNREACHABLE;
}



static SourceFile* source_file_from_source_id(CoreData* core, SourceId source_id) noexcept
{
	ASSERT_OR_IGNORE(source_id != SourceId::INVALID);

	ASSERT_OR_IGNORE(core->reader.source_file_count != 0);

	ASSERT_OR_IGNORE(static_cast<u32>(source_id) < core->reader.curr_source_id_base);

	SourceFileByIdEntry* const entries = core->reader.id_entries.begin() + 1;

	// By handling the last entry as a special case, we can always index into
	// `mid + 1`. This is necessary since `SourceFileByIdEntry` only stores the
	// lowest source id present in the file. However, since entries are
	// effectively ordered by their source id, the effective end index is the
	// start id of the next entry.
	if (static_cast<u32>(entries[core->reader.source_file_count - 1].data.source_id_base) <= static_cast<u32>(source_id))
		return &entries[core->reader.source_file_count - 1].data;

	u32 lo = 0;

	// Ignore last entry, as described above.
	u32 hi = core->reader.source_file_count - 2;

	while (lo < hi)
	{
		// If we ever get to more than 2^31 source files, we should really
		// already be over the 4gb source code limit, so no need to worry about
		// arithmetic overflow here.
		const u32 mid = (lo + hi) >> 1;

		SourceFileByIdEntry* const curr = entries + mid;

		SourceFileByIdEntry* const next = entries + mid + 1;

		if (static_cast<u32>(source_id) < static_cast<u32>(curr->data.source_id_base))
		{
			hi = mid - 1;
		}
		else if (static_cast<u32>(source_id) >= static_cast<u32>(next->data.source_id_base))
		{
			lo = mid + 1;
		}
		else
		{
			return &curr->data;
		}
	}

	// We cannot have lo == hi == core->reader.source_file_count - 1, as we have
	// already checked that we do not exceed the last entry's beginning before
	// entering the search loop.
	ASSERT_OR_IGNORE(lo < core->reader.source_file_count - 1);

	return &entries[lo].data;
}

static Range<char8> source_file_path(CoreData* core, SourceFile* source_file) noexcept
{
	SourceFileByIdEntry* const id_entry = reinterpret_cast<SourceFileByIdEntry*>(reinterpret_cast<byte*>(source_file) - offsetof(SourceFileByIdEntry, data));

	SourceFileByPathEntry* const path_entry = core->reader.known_files_by_path.value_from(id_entry->path_entry_index);

	return Range{ path_entry->path, path_entry->path_bytes };
}

static SourceLocation build_source_location(Range<char8> filepath, Range<char8> content, u32 offset) noexcept
{
	ASSERT_OR_IGNORE(offset <= content.count());

	u32 line_begin = 0;

	u32 line_number = 1;

	for (u32 i = 0; i != offset; ++i)
	{
		if (content[i] == '\n')
		{
			line_begin = i + 1;

			line_number += 1;
		}
	}

	u32 line_end = line_begin;

	while (line_end < content.count() && content[line_end] != '\n' && content[line_end] != '\r')
		line_end += 1;

	u32 tabs_before_offset = 0;

	for (u32 i = line_begin; i != offset; ++i)
	{
		if (content[i] == '\t')
			tabs_before_offset += 1;
	}

	const u32 column_number = offset - line_begin;

	const u32 context_begin = line_begin + (column_number < 200 ? 0 : column_number - 200);

	const u32 context_chars = line_end - context_begin < sizeof(SourceLocation::context) ? line_end - context_begin : sizeof(SourceLocation::context);

	SourceLocation location;
	location.filepath = filepath;
	location.line_number = line_number;
	location.column_number = column_number + 1;
	location.context_offset = context_begin - line_begin;
	location.context_chars = context_chars;
	location.tabs_before_column_number = tabs_before_offset;
	memcpy(location.context, content.begin() + context_begin, context_chars);

	return location;
}

static SourceLocation source_location_from_source_file_and_source_id(CoreData* core, SourceFile* source_file, SourceId source_id) noexcept
{
	minos::FileInfo fileinfo;

	Range<char8> filepath = source_file_path(core, source_file);

	if (!minos::file_get_info(get(source_file->file), &fileinfo))
		panic("Could not get info on source file % while trying to re-read it for error reporting (0x%[|X])\n", filepath, minos::last_error());

	char8* const buffer = static_cast<char8*>(malloc(fileinfo.bytes));

	u32 bytes_read;

	if (!minos::file_read(get(source_file->file), MutRange{ buffer, fileinfo.bytes }.as_mut_byte_range(), 0, &bytes_read))
		panic("Could not read source file % while trying to re-read it for error reporting (0x%[|X])\n", filepath, minos::last_error());

	if (bytes_read != fileinfo.bytes)
		panic("Could only read % out of % bytes from source file % while trying to re-read it for error reporting (0x%[|X])\n", bytes_read, fileinfo.bytes, filepath, minos::last_error());

	SourceLocation location = build_source_location(filepath, Range{ buffer, fileinfo.bytes }, static_cast<u32>(source_id) - static_cast<u32>(source_file->source_id_base));

	free(buffer);

	return location;
}



bool source_reader_validate_config([[maybe_unused]] const Config* config, [[maybe_unused]] PrintSink sink) noexcept
{
	return true;
}

MemoryRequirements source_reader_memory_requirements([[maybe_unused]] const Config* config) noexcept
{
	MemoryRequirements reqs;
	reqs.count = 1;
	reqs.ranges[0].size = KNOWN_FILES_BY_PATH_LOOKUP_RESERVE
	                    + KNOWN_FILES_BY_PATH_VALUES_RESERVE
	                    + KNOWN_FILES_BY_IDENTITY_LOOKUP_RESERVE
	                    + KNOWN_FILES_BY_IDENTITY_VALUES_RESERVE;
	reqs.ranges[0].max_offset = UINT64_MAX;

	return reqs;
}

void source_reader_init(CoreData* core, MemoryAllocation allocation) noexcept
{
	ASSERT_OR_IGNORE(allocation.ranges[0].count() == KNOWN_FILES_BY_PATH_LOOKUP_RESERVE + KNOWN_FILES_BY_PATH_VALUES_RESERVE + KNOWN_FILES_BY_IDENTITY_LOOKUP_RESERVE + KNOWN_FILES_BY_IDENTITY_VALUES_RESERVE);

	SourceReader* const reader = &core->reader;

	u64 offset = 0;

	const MutRange<byte> by_path_lookup_memory = allocation.ranges[0].mut_subrange(offset, KNOWN_FILES_BY_PATH_LOOKUP_RESERVE);
	offset += KNOWN_FILES_BY_PATH_LOOKUP_RESERVE;

	const MutRange<byte> by_path_values_memory = allocation.ranges[0].mut_subrange(offset, KNOWN_FILES_BY_PATH_VALUES_RESERVE);
	offset += KNOWN_FILES_BY_PATH_VALUES_RESERVE;

	const MutRange<byte> by_identity_lookup_memory = allocation.ranges[0].mut_subrange(offset, KNOWN_FILES_BY_IDENTITY_LOOKUP_RESERVE);
	offset += KNOWN_FILES_BY_IDENTITY_LOOKUP_RESERVE;

	const MutRange<byte> by_identity_values_memory = allocation.ranges[0].mut_subrange(offset, KNOWN_FILES_BY_IDENTITY_VALUES_RESERVE);
	offset += KNOWN_FILES_BY_IDENTITY_VALUES_RESERVE;

	ASSERT_OR_IGNORE(allocation.ranges[0].count() == offset);

	reader->known_files_by_path.init(by_path_lookup_memory, KNOWN_FILES_BY_PATH_LOOKUP_INITIAL_COMMIT_COUNT, SourceFileByPathAlloc{ core });
	reader->known_files_by_identity.init(by_identity_lookup_memory, KNOWN_FILES_BY_IDENTITY_LOOKUP_INITIAL_COMMIT_COUNT, SourceFileByIdAlloc{ core });
	reader->path_entries.init(by_path_values_memory, KNOWN_FILES_BY_PATH_VALUES_COMMIT_INCREMENT_COUNT);
	reader->id_entries.init(by_identity_values_memory, KNOWN_FILES_BY_IDENTITY_VALUES_COMMIT_INCREMENT_COUNT);
	reader->curr_source_id_base = 1;
	reader->source_file_count = 0;

	(void) reader->path_entries.reserve(alignof(SourceFileByPathEntry));
	(void) reader->id_entries.reserve();
}



SourceFileRead read_source_file(CoreData* core, Range<char8> filepath) noexcept
{
	// Try lookup via path. This is just approximate, but conservative, meaning
	// that there *might* be a match here if the file has already been seen,
	// but there will never be a match if it has not been seen.

	// TODO: Normalize path to optimize hit rate?

	SourceFileByPathEntry* const path_entry = core->reader.known_files_by_path.value_from(filepath, fnv1a(filepath.as_byte_range()));

	if (path_entry->id_entry_index != 0)
		return SourceFileRead{ &core->reader.known_files_by_identity.value_from(path_entry->id_entry_index)->data, {} };

	// Try lookup via file identity. This is exact, meaning there is a match
	// here if and only if the file has already been seen.

	minos::FileHandle file;

	if (!minos::file_create(filepath, minos::Access::Read, minos::ExistsMode::Open, minos::NewMode::Fail, minos::AccessPattern::Sequential, none<const minos::CompletionInitializer*>(), false, &file))
		panic("Could not open source file % for reading (0x%[|X])\n", filepath, minos::last_error());

	minos::FileInfo fileinfo;

	if (!minos::file_get_info(file, &fileinfo))
		panic("Could not get info on source file % (0x%[|X])\n", filepath, minos::last_error());

	if (fileinfo.bytes > UINT32_MAX)
		panic("Could not read source file % as its size % exceeds the supported maximum of % bytes (< 4gb)\n", filepath, fileinfo.bytes, UINT32_MAX);

	SourceFileByIdEntry* const id_entry = core->reader.known_files_by_identity.value_from(fileinfo.identity, hash_file_identity(fileinfo.identity.index, fileinfo.identity.volume_serial));

	path_entry->id_entry_index = core->reader.known_files_by_identity.id_from(id_entry);

	if (is_some(id_entry->data.file))
		return SourceFileRead{ &id_entry->data, {} };

	// File has not been read in yet. Do so.

	id_entry->path_entry_index = core->reader.known_files_by_path.id_from(path_entry);
	id_entry->data.file = some(file);
	id_entry->data.ast = AstNodeId::INVALID;
	id_entry->data.type = TypeId::INVALID;
	id_entry->data.source_id_base = SourceId{ core->reader.curr_source_id_base };
	id_entry->data.has_error = false;

	if (fileinfo.bytes + core->reader.curr_source_id_base > UINT32_MAX)
		panic("Could not read source file % as the maximum total capacity of 4gb of source code was exceeded.\n", filepath);

	// Allow for one extra byte so `parse` can use one-past-end for
	// `Token::END_OF_FILE` without extra work.
	core->reader.curr_source_id_base += static_cast<u32>(fileinfo.bytes) + 1;

	core->reader.source_file_count += 1;

	char8* const content = static_cast<char8*>(malloc(fileinfo.bytes + 1));

	if (content == nullptr)
		panic("Could not allocate buffer for reading source file % (0x%[|X])\n", filepath, minos::last_error());

	content[fileinfo.bytes] = '\0';

	u32 bytes_read;

	if (!minos::file_read(file, MutRange{ content, fileinfo.bytes }.as_mut_byte_range(), 0, &bytes_read))
		panic("Could not read source file % (0x%[|X])\n", filepath, minos::last_error());

	if (bytes_read != fileinfo.bytes)
		panic("Could only read % out of % bytes from source file % (0x%[|X])\n", bytes_read, fileinfo.bytes, filepath, minos::last_error());

	return SourceFileRead{ &id_entry->data, Range{ content, fileinfo.bytes + 1 } };
}

void release_read([[maybe_unused]] CoreData* core, SourceFileRead read) noexcept
{
	free(const_cast<char8*>(read.content.begin()));
}

SourceLocation source_location_from_source_id(CoreData* core, SourceId source_id) noexcept
{
	if (source_id == SourceId::INVALID)
	{
		return build_source_location(range::from_literal_string("<prelude>"), {}, 0);
	}
	else
	{
		SourceFile* const source_file = source_file_from_source_id(core, source_id);

		return source_location_from_source_file_and_source_id(core, source_file, source_id);
	}
}

Range<char8> source_file_path_from_source_id(CoreData* core, SourceId source_id) noexcept
{
	ASSERT_OR_IGNORE(source_id != SourceId::INVALID);

	SourceFile* const source_file = source_file_from_source_id(core, source_id);

	SourceFileByIdEntry* const id_entry = reinterpret_cast<SourceFileByIdEntry*>(reinterpret_cast<byte*>(source_file) - offsetof(SourceFileByIdEntry, data));

	SourceFileByPathEntry* const path_entry = core->reader.known_files_by_path.value_from(id_entry->path_entry_index);

	return Range{ path_entry->path, path_entry->path_bytes };
}



SourceFileId id_from_source_file(CoreData* core, SourceFile* file) noexcept
{
	const SourceFileByIdEntry* const key = reinterpret_cast<const SourceFileByIdEntry*>(reinterpret_cast<byte*>(file) - offsetof(SourceFileByIdEntry, data));

	return static_cast<SourceFileId>(core->reader.known_files_by_identity.id_from(key));
}

SourceFile* source_file_from_id(CoreData* core, SourceFileId file_id) noexcept
{
	return &core->reader.known_files_by_identity.value_from(static_cast<u32>(file_id))->data;
}
