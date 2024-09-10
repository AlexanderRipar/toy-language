#ifndef AST_SERVER_INCLUDE_GUARD
#define AST_SERVER_INCLUDE_GUARD

#include "common.hpp"
#include "range.hpp"
#include "minos.hpp"
#include "threading.hpp"
#include "memory.hpp"
#include "append_buffer.hpp"
#include "job_server.hpp"

struct AstHandle
{
	// TODO
};

using file_processing_proc = bool (*) (Range<byte> input, AppendBuffer* output) noexcept;

struct PathMapping
{
	u32 hash;

	u32 next;

	u32 filedata_index;

	u16 path_chars;

	#pragma warning(push)
	#pragma warning(disable : 4200) // warning C4200: nonstandard extension used: zero-sized array in struct/union
	u8 path[];
	#pragma warning(pop)

	void init(Range<char8> key, u32 key_hash) noexcept
	{
		hash = key_hash;

		filedata_index = ~0u;

		path_chars = static_cast<u16>(key.count());

		memcpy(path, key.begin(), key.count());
	}

	static constexpr u32 stride() noexcept
	{
		return 16;
	}

	static u32 get_required_strides(Range<char8> key) noexcept
	{
		return static_cast<u32>((offsetof(PathMapping, path) + key.count() + stride() - 1) / stride());
	}

	u32 get_used_strides() const noexcept
	{
		return static_cast<u32>((offsetof(PathMapping, path) + path_chars + stride() - 1) / stride());
	}

	u32 get_hash() const noexcept
	{
		return hash;
	}

	bool equal_to_key(Range<char8> key, u32 key_hash) const noexcept
	{
		return hash == key_hash && path_chars == key.count() && memcmp(key.begin(), path, key.count()) == 0;
	}

	void set_next(u32 index) noexcept
	{
		next = index;
	}

	u32 get_next() const noexcept
	{
		return next;
	}
};

struct FileKey
{
	minos::FileInfo info;

	minos::FileHandle handle;
};

struct FileData
{
	u32 hash;

	u32 volume_serial_id;

	u64 file_local_identifer;

	u32 next;

	minos::FileHandle filehandle;

	void init(FileKey key, u32 key_hash) noexcept
	{
		hash = key_hash;

		volume_serial_id = key.info.identity.volume_serial;

		file_local_identifer = key.info.identity.index;

		filehandle = key.handle;
	}

	static constexpr u32 stride() noexcept
	{
		return static_cast<u32>(next_pow2(sizeof(FileData)));
	}

	static u32 get_required_strides([[maybe_unused]] FileKey key) noexcept
	{
		return 1;
	}

	u32 get_used_strides() const noexcept
	{
		return 1;
	}

	u32 get_hash() const noexcept
	{
		return hash;
	}

	bool equal_to_key(FileKey key, [[maybe_unused]] u32 key_hash) const noexcept
	{
		return volume_serial_id == key.info.identity.volume_serial && file_local_identifer == key.info.identity.index;
	}

	void set_next(u32 index) noexcept
	{
		next = index;
	}

	u32 get_next() const noexcept
	{
		return next;
	}
};

struct ProcessingJob
{
	Range<byte> input;

	AppendBuffer* output;

	file_processing_proc processing_proc;

	u32 key;
};

struct AstServer
{
private:

	static constexpr u32 lex_and_parse_priority = 0;

	union ReadData
	{
		struct
		{
			minos::Overlapped overlapped;

			byte* content;

			u64 bytes;

			FileData* filedata;
		};

		u32 next;
	};

	using PathMap = ThreadsafeMap2<Range<char8>, PathMapping>;

	using FileMap = ThreadsafeMap2<FileKey, FileData>;

	PathMap m_path_map;

	FileMap m_file_map;

	minos::ThreadHandle m_completion_thread;

	minos::CompletionHandle m_completion;

	JobServer* m_job_server;

	u32 m_read_capacity;

	ReadData* m_reads;

	ThreadsafeIndexStackListHeader<ReadData, offsetof(ReadData, next)> m_ready_reads;

	ThreadsafeIndexStackListHeader<ReadData, offsetof(ReadData, next)> m_read_freelist;

	static u32 completion_thread_proc(void* param) noexcept;

public:

	struct InitInfo
	{
		u32 thread_count;

		u32 concurrent_read_capacity;

		struct
		{
			PathMap::InitInfo::MapInitInfo map;

			PathMap::InitInfo::StoreInitInfo store;
		} paths;

		struct
		{
			FileMap::InitInfo::MapInitInfo map;

			FileMap::InitInfo::StoreInitInfo store;
		} files;
	};

	static MemoryRequirements get_memory_requirements(const InitInfo& info) noexcept;

	bool init(const InitInfo& info, byte* memory, JobServer* job_server) noexcept;

	bool request_ast_from_file(u32 thread_id, Range<char8> path, AstHandle* out) noexcept;

	bool get_job(job_proc* out_proc, void* out_param) noexcept;

	void notify_job_complete(u32 key) noexcept;
};

#endif // AST_SERVER_INCLUDE_GUARD
