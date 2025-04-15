#include <cstdlib>
#include <atomic>

#include "../infra/common.hpp"
#include "../infra/threading.hpp"
#include "../infra/minos.hpp"
#include "../infra/hash.hpp"
#include "pass_data.hpp"

struct SourceFileByPathEntry
{
	u32 m_hash;

	u32 path_bytes;

	u32 source_file_index;

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

		source_file_index = 0;

		memcpy(path, key.begin(), key.count());
	}
};

struct SourceFileByIdEntry
{
	u32 m_hash;

	u32 device_id;

	u64 file_id;

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
		return m_hash;
	}

	bool equal_to_key(minos::FileIdentity key, [[maybe_unused]] u32 key_hash) noexcept
	{
		return device_id == key.volume_serial && file_id == key.index;
	}

	void init(minos::FileIdentity key, u32 key_hash) noexcept
	{
		m_hash = key_hash;

		device_id = key.volume_serial;

		file_id = key.index;
	}
};

struct SourceReader
{
	IndexMap<Range<char8>, SourceFileByPathEntry> known_files_by_path;

	IndexMap<minos::FileIdentity, SourceFileByIdEntry> known_files_by_identity;
};


SourceReader* create_source_reader(AllocPool* pool) noexcept
{
	SourceReader* const reader = static_cast<SourceReader*>(alloc_from_pool(pool, sizeof(SourceReader), alignof(SourceReader)));

	reader->known_files_by_path.init(1 << 24, 1 << 10, 1 << 26, 1 << 13);

	reader->known_files_by_identity.init(1 << 24, 1 << 10, 1 << 26, 1 << 12);

	minos::FileIdentity dummy_identity{};

	// Reserve index 0.
	(void) reader->known_files_by_identity.value_from(dummy_identity, fnv1a(range::from_object_bytes(&dummy_identity)));

	return reader;
}

SourceFileRead read_source_file(SourceReader* reader, Range<char8> filepath, IdentifierId filepath_id) noexcept
{
	// Try lookup via path. This is just approximate, but conservative, meaning
	// that there *might* be a match here if the file has already been seen,
	// but there will never be a match if it has not been seen.

	// TODO: Normalize path to optimize hit rate?

	SourceFileByPathEntry* const path_entry = reader->known_files_by_path.value_from(filepath, fnv1a(filepath.as_byte_range()));

	if (path_entry->source_file_index != 0)
		return { &reader->known_files_by_identity.value_from(path_entry->source_file_index)->data, {} };

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

	SourceFileByIdEntry* const id_entry = reader->known_files_by_identity.value_from(fileinfo.identity, fnv1a(range::from_object_bytes(&fileinfo.identity)));

	path_entry->source_file_index = reader->known_files_by_identity.index_from(id_entry);

	if (id_entry->data.file.m_rep != nullptr)
		return { &id_entry->data, {} };

	// File has not been read in yet. Do so.

	id_entry->data.file = file;
	id_entry->data.filepath_id = filepath_id;
	id_entry->data.ast_root = INVALID_AST_NODE_ID;

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

void release_read([[maybe_unused]] SourceReader* reader, SourceFileRead file) noexcept
{
	free(const_cast<char8*>(file.content.begin()));
}
