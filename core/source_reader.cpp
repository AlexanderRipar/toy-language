#include "core.hpp"

#include "../infra/common.hpp"
#include "../infra/container/index_map.hpp"
#include "../infra/threading.hpp"
#include "../infra/minos/minos.hpp"
#include "../infra/hash.hpp"

#include <cstdlib>
#include <cstddef>
#include <atomic>

static u32 hash_file_identity(u64 file_id, u32 device_id) noexcept
{
	struct
	{
		u64 file_id;

		u32 device_id;
	} id { file_id, device_id };

	return fnv1a(range::from_object_bytes(&id));
}



struct SourceFileByPathEntry
{
	u32 m_hash;

	u32 path_bytes;

	u32 id_entry_index;

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
	char8 path[];
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

	static u32 required_strides(Range<char8> key) noexcept
	{
		return static_cast<u32>((offsetof(SourceFileByPathEntry, path) + key.count() + stride() - 1) / stride());
	}

	u32 used_strides() const noexcept
	{
		return (offsetof(SourceFileByPathEntry, path) + path_bytes + stride() - 1) / stride();
	}

	u32 hash() const noexcept
	{
		return m_hash;
	}

	bool equal_to_key(Range<char8> key, u32 key_hash) noexcept
	{
		return m_hash == key_hash && key.count() == path_bytes && memcmp(key.begin(), path, key.count()) == 0;
	}

	void init(Range<char8> key, u32 key_hash) noexcept
	{
		ASSERT_OR_IGNORE(key.count() < UINT32_MAX);

		m_hash = key_hash;

		path_bytes = static_cast<u32>(key.count());

		id_entry_index = 0;

		memcpy(path, key.begin(), key.count());
	}
};

struct SourceFileByIdEntry
{
	u64 file_id;

	u32 device_id;

	u32 path_entry_index;

	SourceFile data;

	static constexpr u32 stride() noexcept
	{
		return sizeof(SourceFileByIdEntry);
	}

	static u32 required_strides([[maybe_unused]] minos::FileIdentity key) noexcept
	{
		return 1;
	}

	u32 used_strides() const noexcept
	{
		return sizeof(SourceFileByIdEntry);
	}

	u32 hash() const noexcept
	{
		return hash_file_identity(file_id, device_id);
	}

	bool equal_to_key(minos::FileIdentity key, [[maybe_unused]] u32 key_hash) noexcept
	{
		return device_id == key.volume_serial && file_id == key.index;
	}

	void init(minos::FileIdentity key, [[maybe_unused]] u32 key_hash) noexcept
	{
		device_id = key.volume_serial;

		file_id = key.index;
	}
};

struct SourceReader
{
	IndexMap<Range<char8>, SourceFileByPathEntry> known_files_by_path;

	IndexMap<minos::FileIdentity, SourceFileByIdEntry> known_files_by_identity;

	u32 curr_source_id_base;

	u32 source_file_count;
};



static SourceFile* source_file_from_source_id(SourceReader* reader, SourceId source_id) noexcept
{
	ASSERT_OR_IGNORE(source_id != SourceId::INVALID);

	ASSERT_OR_IGNORE(reader->source_file_count != 0);

	ASSERT_OR_IGNORE(static_cast<u32>(source_id) < reader->curr_source_id_base);

	SourceFileByIdEntry* const entries = reader->known_files_by_identity.value_from(0);;

	// By handling the last entry as a special case, we can always index into
	// `mid + 1`. This is necessary since `SourceFileByIdEntry` only stores the
	// lowest source id present in the file. However, since entries are
	// effectively ordered by their source id, the effective end index is the
	// start id of the next entry.
	if (static_cast<u32>(entries[reader->source_file_count - 1].data.source_id_base) <= static_cast<u32>(source_id))
		return &entries[reader->source_file_count - 1].data;

	u32 lo = 0;

	// Ignore last entry, as described above.
	u32 hi = reader->source_file_count - 2;

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

	// We cannot have lo == hi == reader->source_file_count - 1, as we have
	// already checked that we do not exceed the last entry's beginning before
	// entering the search loop.
	ASSERT_OR_IGNORE(lo < reader->source_file_count - 1);

	return &entries[lo].data;
}

static Range<char8> source_file_path(SourceReader* reader, SourceFile* source_file) noexcept
{
	SourceFileByIdEntry* const id_entry = reinterpret_cast<SourceFileByIdEntry*>(reinterpret_cast<byte*>(source_file) - offsetof(SourceFileByIdEntry, data));

	SourceFileByPathEntry* const path_entry = reader->known_files_by_path.value_from(id_entry->path_entry_index);

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


	const u32 column_number = offset - line_begin;

	const u32 context_begin = line_begin + (column_number < 200 ? 0 : column_number - 200);

	const u32 context_chars = line_end - context_begin < sizeof(SourceLocation::context) ? line_end - context_begin : sizeof(SourceLocation::context);

	SourceLocation location;
	location.filepath = filepath;
	location.line_number = line_number;
	location.column_number = column_number + 1;
	location.context_offset = context_begin - line_begin;
	location.context_chars = context_chars;
	memcpy(location.context, content.begin() + context_begin, context_chars);

	return location;
}

static SourceLocation source_location_from_source_file_and_source_id(SourceReader* reader, SourceFile* source_file, SourceId source_id) noexcept
{
	minos::FileInfo fileinfo;

	Range<char8> filepath = source_file_path(reader, source_file);

	if (!minos::file_get_info(source_file->file, &fileinfo))
		panic("Could not get info on source file %.*s while trying to re-read it for error reporting (0x%X)\n", static_cast<s32>(filepath.count()), filepath.begin(), minos::last_error());

	char8* const buffer = static_cast<char8*>(malloc(fileinfo.bytes));

	u32 bytes_read;

	if (!minos::file_read(source_file->file, MutRange{ buffer, fileinfo.bytes }.as_mut_byte_range(), 0, &bytes_read))
		panic("Could not read source file %.*s while trying to re-read it for error reporting (0x%X)\n", static_cast<s32>(filepath.count()), filepath.begin(), minos::last_error());

	if (bytes_read != fileinfo.bytes)
		panic("Could only read %u out of %" PRIu64 " bytes from source file %.*s while trying to re-read it for error reporting (0x%X)\n", bytes_read, fileinfo.bytes, static_cast<s32>(filepath.count()), filepath.begin(), minos::last_error());

	SourceLocation location = build_source_location(filepath, Range{ buffer, fileinfo.bytes }, static_cast<u32>(source_id) - static_cast<u32>(source_file->source_id_base));

	free(buffer);

	return location;
}



SourceReader* create_source_reader(HandlePool* pool) noexcept
{
	SourceReader* const reader = static_cast<SourceReader*>(alloc_handle_from_pool(pool, sizeof(SourceReader), alignof(SourceReader)));

	reader->known_files_by_path.init(1 << 24, 1 << 10, 1 << 23, 1 << 13);

	reader->known_files_by_identity.init(1 << 24, 1 << 10, 1 << 23, 1 << 12);

	reader->curr_source_id_base = 1;

	reader->source_file_count = 0;

	return reader;
}

void release_source_reader(SourceReader* reader) noexcept
{
	reader->known_files_by_path.release();

	reader->known_files_by_identity.release();
}

SourceFileRead read_source_file(SourceReader* reader, Range<char8> filepath) noexcept
{
	// Try lookup via path. This is just approximate, but conservative, meaning
	// that there *might* be a match here if the file has already been seen,
	// but there will never be a match if it has not been seen.

	// TODO: Normalize path to optimize hit rate?

	SourceFileByPathEntry* const path_entry = reader->known_files_by_path.value_from(filepath, fnv1a(filepath.as_byte_range()));

	if (path_entry->id_entry_index != 0)
		return { &reader->known_files_by_identity.value_from(path_entry->id_entry_index)->data, {} };

	// Try lookup via file identity. This is exact, meaning there is a match
	// here if and only if the file has already been seen.

	minos::FileHandle file;

	if (!minos::file_create(filepath, minos::Access::Read, minos::ExistsMode::Open, minos::NewMode::Fail, minos::AccessPattern::Sequential, nullptr, false, &file))
		panic("Could not open source file %.*s for reading (0x%X)\n", static_cast<u32>(filepath.count()), filepath.begin(), minos::last_error());

	minos::FileInfo fileinfo;

	if (!minos::file_get_info(file, &fileinfo))
		panic("Could not get info on source file %.*s (0x%X)\n", static_cast<u32>(filepath.count()), filepath.begin(), minos::last_error());

	if (fileinfo.bytes > UINT32_MAX)
		panic("Could not read source file %.*s as its size %llu exceeds the supported maximum of %u bytes (< 4gb)\n", static_cast<u32>(filepath.count()), filepath.begin(), fileinfo.bytes, UINT32_MAX);

	SourceFileByIdEntry* const id_entry = reader->known_files_by_identity.value_from(fileinfo.identity, hash_file_identity(fileinfo.identity.index, fileinfo.identity.volume_serial));

	path_entry->id_entry_index = reader->known_files_by_identity.index_from(id_entry);

	if (id_entry->data.file.m_rep != nullptr)
		return { &id_entry->data, {} };

	// File has not been read in yet. Do so.

	id_entry->path_entry_index = reader->known_files_by_path.index_from(path_entry);
	id_entry->data.file = file;
	id_entry->data.root_ast = AstNodeId::INVALID;
	id_entry->data.source_id_base = SourceId{ reader->curr_source_id_base };

	if (fileinfo.bytes + reader->curr_source_id_base > UINT32_MAX)
		panic("Could not read source file %.*s as the maximum total capacity of 4gb of source code was exceeded.\n", static_cast<s32>(filepath.count()), filepath.begin());

	// Allow for one extra byte so `parse` can use one-past-end for
	// `Token::END_OF_FILE` without extra work.
	reader->curr_source_id_base += static_cast<u32>(fileinfo.bytes) + 1;

	reader->source_file_count += 1;

	char8* const content = static_cast<char8*>(malloc(fileinfo.bytes + 1));

	if (content == nullptr)
		panic("Could not allocate buffer for reading source file %.*s (0x%X)\n", static_cast<s32>(filepath.count()), filepath.begin(), minos::last_error());

	content[fileinfo.bytes] = '\0';

	u32 bytes_read;

	if (!minos::file_read(file, MutRange{ content, fileinfo.bytes }.as_mut_byte_range(), 0, &bytes_read))
		panic("Could not read source file %.*s (0x%X)\n", static_cast<s32>(filepath.count()), filepath.begin(), minos::last_error());

	if (bytes_read != fileinfo.bytes)
		panic("Could only read %u out of %" PRIu64 " bytes from source file %.*s (0x%X)\n", bytes_read, fileinfo.bytes, static_cast<s32>(filepath.count()), filepath.begin(), minos::last_error());

	return { &id_entry->data, Range{ content, fileinfo.bytes + 1 } };
}

void release_read([[maybe_unused]] SourceReader* reader, SourceFileRead read) noexcept
{
	free(const_cast<char8*>(read.content.begin()));
}

SourceLocation source_location_from_source_id(SourceReader* reader, SourceId source_id) noexcept
{
	if (source_id == SourceId::INVALID)
	{
		return build_source_location(range::from_literal_string("<prelude>"), {}, 0);
	}
	else
	{
		SourceFile* const source_file = source_file_from_source_id(reader, source_id);

		return source_location_from_source_file_and_source_id(reader, source_file, source_id);
	}
}

Range<char8> source_file_path_from_source_id(SourceReader* reader, SourceId source_id) noexcept
{
	ASSERT_OR_IGNORE(source_id != SourceId::INVALID);

	SourceFile* const source_file = source_file_from_source_id(reader, source_id);

	SourceFileByIdEntry* const id_entry = reinterpret_cast<SourceFileByIdEntry*>(reinterpret_cast<byte*>(source_file) - offsetof(SourceFileByIdEntry, data));

	SourceFileByPathEntry* const path_entry = reader->known_files_by_path.value_from(id_entry->path_entry_index);

	return Range{ path_entry->path, path_entry->path_bytes };
}
