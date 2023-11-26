#ifndef GLOBAL_DATA_INCLUDE_GUARD
#define GLOBAL_DATA_INCLUDE_GUARD

#include "common.hpp"
#include "range.hpp"
#include "token.hpp"
#include "minwin.hpp"

static inline u32 fnv1a(Range<char8> string) noexcept
{
	u32 hash = 2166136261;

	for (const char8 c : string)
		hash = (hash * 16777619) ^ c;

	return hash;
}

struct SimpleMapDiagnostics
{
	u32 indices_used_count;

	u32 indices_committed_count;

	u32 data_used_bytes;

	u32 data_committed_bytes;

	u32 data_overhead;

	u32 data_stride;
};

struct FullMapDiagnostics
{
	SimpleMapDiagnostics simple;

	u32 max_probe_seq_len;

	u32 probe_seq_len_counts[128];

	u32 total_string_bytes;

	u32 max_string_bytes;
};

template<typename DataEntry>
struct GenericMapData
{
	static constexpr u64 data_reserved_bytes = 1ui64 << 31;

	static constexpr u64 indices_reserved_bytes = 1ui64 << 29;

	static constexpr u32 data_commit_increment_bytes = 1024 * 1024;

	static constexpr u32 initial_indices_commit_log2 = 15;

	static constexpr u32 inline_hash_bits = DataEntry::inline_hash_bits;

	static constexpr u32 inline_hash_mask = ~(~0u >> inline_hash_bits);

	SRWLOCK lock;

	void* data;

	u32 data_used_bytes;

	u32 data_committed_bytes;

	void* indices;

	u32 indices_committed_bytes_log2;

	u32 indices_used_bytes;
};

struct StringSet
{
private:

	struct DataEntry
	{
		static constexpr u32 inline_hash_bits = Token::MAX_TAG_BITS;

		u32 hash;

		u16 tail_bytes;

		#pragma warning(push)
		#pragma warning(disable : 4200)
		char8 tail[];
		#pragma warning(pop)
	};

	GenericMapData<DataEntry> m_map;

public:

	bool init() noexcept;

	bool deinit() noexcept;

	s32 index_from(Range<char8> string) noexcept;

	s32 index_from(Range<char8> string, u32 hash) noexcept;

	Range<char8> string_from(s32 index) const noexcept;

	void get_diagnostics(SimpleMapDiagnostics* out) const noexcept;

	void get_diagnostics(FullMapDiagnostics* out) const noexcept;
};

struct InputFileSet
{
private:
	struct DataEntry
	{
		static constexpr u32 inline_hash_bits = 5;

		u32 hash;

		u32 next_index;

		HANDLE handle;

		u16 tail_bytes;

		#pragma warning(push)
		#pragma warning(disable : 4200)
		char8 tail[];
		#pragma warning(pop)
	};

	DataEntry* m_head;

	GenericMapData<DataEntry> m_map;

public:

	bool init() noexcept;

	bool deinit() noexcept;

	bool add_file(Range<char8> path, HANDLE handle) noexcept;

	HANDLE get_file() noexcept;

	void get_diagnostics(SimpleMapDiagnostics* out) const noexcept;

	void get_diagnostics(FullMapDiagnostics* out) const noexcept;
};

struct PerReadData
{
	// Padding after the end of the preceding buffer. This might have null
	// characters and/or spaces written into as buffer sentinels in case
	// the buffer is completely used.
	byte reserved[64];

	union
	{
		// Used for asynchronous reading. This must be zeroed before
		// beginning to read a new file.
		OVERLAPPED overlapped;

		// Bookkeeping for list of inactive PerReadDatas (i.e.,
		// PerReadDatas with no outstanding reads).
		// This can share space with overlapped, as, per definition it
		// is only active when overlapped is inactive (there are no
		// outstanding reads that would use it).
		SLIST_ENTRY free_list_entry;
	};

	// Handle to the file being read.
	HANDLE file_handle;

	// Number of bytes that have yet to be read from the file.
	u64 remaining_file_bytes;

	// Byte offset into the buffer from which valid data starts. When lex
	// completes sucessfully, buffer_start_offset + buffer_valid_bytes is
	// an integer multiple of the page size.
	u32 buffer_start_offset;

	// Number of valid bytes in the buffer starting at buffer_start_offset.
	// On entry to lex, this contains the bytes recycled from the previous
	// lex plus the number of newly read bytes. It does not include the
	// null characters inserted after the end of the buffer.
	// When lex completes successfully, this contains the number of bytes
	// that were recycled.
	u32 buffer_valid_bytes;

	// Nesting depth of multiline comments after the end of the previous
	// lex. If this is ~0u, it indicates an unfinished one-line comment
	u32 comment_nesting;

	// Last character of unfinished comment. This is used to cover the case
	// when the last character from the previous read was either '/' or
	// '*', and could thus be part of a comment start or end tag.
	char8 comment_prev_char;
};

struct ReadList
{
private:

	void* m_buffers;

	u32 m_stride;

	u32 m_buffer_count;

	u32 m_per_buffer_bytes;

	#pragma warning(push)
	#pragma warning(disable : 4324)

	SLIST_HEADER m_free_list;

	#pragma warning(pop)

public:

	bool init(u32 buffer_count, u32 per_buffer_bytes) noexcept;

	bool deinit() noexcept;

	void* buffer_from(const PerReadData* ptr) noexcept;

	u32 buffer_bytes() const noexcept;

	PerReadData* claim_read_data() noexcept;

	void free_read_data(PerReadData* data) noexcept;
};

struct LexList
{
private:

public:

	bool init() noexcept;

	bool deinit() noexcept;
};

struct AstList
{
private:

public:

	bool init() noexcept;

	bool deinit() noexcept;
};

struct GlobalData
{
	// Equal to argv[0]
	const char8* program_name;

	// Set of unique strings encountered during tokenization. A Token's
	// string representation can be retrieved via
	// m_strings.string_from(token.index()). Note that this is only the
	// case for Ident Tokens, as all other Tokens have a 1:1 correspondence
	// between their tag and string representation.
	StringSet strings;

	// Buffer of input files. These can be added to during the compilation
	// process, as includes are found.
	InputFileSet input_files;

	// Per-operation data for asynchronous reads of input files.
	ReadList reads;

	LexList lexes;

	AstList asts;

	// Completion port used by worker threads.
	// This receives read, lexer, and parser completions.
	HANDLE completion_port;

	// Event that is signalled by the last worker thread to complete.
	// It is waited upon by the main thread so that the process is not
	// terminated prematurely.
	HANDLE thread_completion_event;

	// Number of currently running work items being processed by worker
	// threads. Workers increment this for every work item that is queued
	// (such as adding a file to ) 
	u32 pending_work_count;

	// Number of worker threads that have not exited yet. When a worker
	// thread exits, it decrements this *atomically*. When the result is
	// is zero, the worker then sets thread_completion_event to indicate
	// that the main thread can proceed.
	u32 running_worker_thread_count;
};

#endif // GLOBAL_DATA_INCLUDE_GUARD
