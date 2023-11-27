#include "global_data.hpp"

#include "minwin.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstddef>

/*
enum class CompletionKey
{
	Read,
	Lex,
	Parse,
};

struct Arguments
{
	const char8** paths;
	
	u32 path_count;

	u32 thread_count;

	u32 concurrent_read_count;

	u32 read_buffer_bytes;

	u32 max_token_count;
};

static bool open_file(const char8* path, void** out_handle) noexcept
{
	char16* wide_path_buf = nullptr;

	char16* full_path_buf = nullptr;

	const s32 wide_path_chars = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);

	if (wide_path_chars == 0)
		goto ERROR;

	wide_path_buf = static_cast<char16*>(malloc(wide_path_chars * sizeof(char16)));

	if (wide_path_buf == nullptr)
		goto ERROR;

	if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wide_path_buf, wide_path_chars) == 0)
		goto ERROR;

	const u32 full_path_chars = GetFullPathNameW(wide_path_buf, 0, nullptr, nullptr);

	if (full_path_chars == 0)
		goto ERROR;

	full_path_buf = static_cast<char16*>(malloc((full_path_chars + 4) * sizeof(char16)));

	if (full_path_buf == nullptr)
		goto ERROR;

	full_path_buf[0] = '\\';
	full_path_buf[1] = '\\';
	full_path_buf[2] = '?';
	full_path_buf[3] = '\\';

	if (const u32 n = GetFullPathNameW(wide_path_buf, full_path_chars, full_path_buf, nullptr); n == 0 || n >= full_path_chars)
		goto ERROR;

	HANDLE file_handle = CreateFileW(full_path_buf, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);

	if (file_handle == INVALID_HANDLE_VALUE)
		goto ERROR;

	*out_handle = file_handle;

	return true;

ERROR:
	
	free(wide_path_buf);

	free(full_path_buf);

	return false;
}

static bool push_token(Token token, ReadData* read_data, u32 reserved_count) noexcept
{
	if (read_data->token_used_count == read_data->token_commit_count)
	{
		if (read_data->token_used_count == reserved_count)
			return false;

		if (!VirtualAlloc(read_data->tokens + read_data->token_commit_count, 65536, MEM_COMMIT, PAGE_READWRITE))
			return false;
	}

	read_data->tokens[read_data->token_used_count] = token;

	read_data->token_used_count += 1;

	return true;
}

static bool skip_comment(ReadData* read_data, const char8** inout_c, u32 comment_nesting) noexcept
{
	const char8* c = *inout_c;

	while (comment_nesting != 0)
	{
		const char curr = *c;

		if (curr == '\0')
		{
			if (c != read_data->buffer + read_data->buffer_beg_offset)
				read_data->comment_last_char = c[-1];
			else
				read_data->comment_last_char = '\0';

			read_data->comment_nesting = comment_nesting;

			read_data->buffer_beg_offset = 0;

			read_data->buffer_valid_bytes = 0;

			return false;
		}
		else if (curr == '/' && c[1] == '*')
		{
			comment_nesting += 1;

			c += 2;
		}
		else if (curr == '*' && c[1] == '/')
		{
			comment_nesting -= 1;

			c += 2;
		}
	}

	*inout_c = c;

	return true;
}

static bool lex(ReadData* read_data, GlobalData* global_data) noexcept
{
	// Install sentinel
	read_data->buffer[read_data->buffer_beg_offset + read_data->buffer_valid_bytes] = '\0';

	const char8* c = read_data->buffer + read_data->buffer_beg_offset;

	if (read_data->comment_nesting == ~0u)
	{
		while (*c != '\n')
			c += 1;

		if (*c == '\0')
		{
			read_data->buffer_beg_offset = 0;

			read_data->buffer_valid_bytes = 0;

			return true;
		}

		c += 1;

		read_data->comment_nesting = 0;
	}
	else if (read_data->comment_nesting != 0)
	{
		u32 comment_nesting = read_data->comment_nesting;

		const char prev_char = read_data->comment_last_char;

		if (prev_char == '/' && *c == '*')
		{
			comment_nesting += 1;

			c += 1;			
		}
		else if (prev_char == '*' && *c == '/')
		{
			comment_nesting -= 1;

			c += 1;
		}

		if (!skip_comment(read_data, &c, comment_nesting))
			return true;

		read_data->comment_nesting = 0;
	}

	const char* token_beg = c;

	while (true)
	{
		Token token{};

		token_beg = c;

		const char curr = c[0];

		// Safe, since read_data->buffer is overallocated by one page.
		const char next = c[1];

		if (next == '\0')
		{
			c += 1;

			goto UNFINISHED;
		}

		switch (curr)
		{
		case '0': {

			if (next == 'x' || next == 'o' || next == 'b')
			{
				c += 2;

				if (next == 'x')
				{
					while ((*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'f') || (*c >= 'A' && *c <= 'F'))
						c += 1;
				}
				else if (next == 'o')
				{
					while (*c >= '0' && *c <= '7')
						c += 1;
				}
				else
				{
					while (*c == '0' || *c == '1')
						c += 1;
				}

				if (*c == '\0')
					goto UNFINISHED;

				const s32 index = global_data->m_strings.index_from(Range{ token_beg, c });

				if (index < 0)
					return false;

				token = Token{ Token::Tag::LitInteger, index };

				break;
			}

			// fallthrough to normal numeric literal processing
		}
		
		case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9': {

			Token::Tag tag;

			c += 1;

			while (*c >= '0' && *c <= '9')
				c += 1;

			if (*c == '\0')
			{
				goto UNFINISHED;
			}
			if (*c == '.')
			{
				if (c[1] == '\0')
				{
					c += 1;

					goto UNFINISHED;
				}
				else if (c[1] >= '0' && c[1] <= '9')
				{
					c += 2;

					while (*c >= '0' && *c <= '9')
						c += 1;

					tag = Token::Tag::LitFloat;
				}
				else
				{
					tag = Token::Tag::LitInteger;
				}
			}
			else
			{
				tag = Token::Tag::LitInteger;
			}

			const s32 index = global_data->m_strings.index_from(Range{ token_beg, c });

			if (index < 0)
				return false;

			token = Token{ tag, index };

			break;
		}

		case 'a': case 'b': case 'c': case 'd': case 'e':
		case 'f': case 'g': case 'h': case 'i': case 'j':
		case 'k': case 'l': case 'm': case 'n': case 'o':
		case 'p': case 'q': case 'r': case 's': case 't':
		case 'u': case 'v': case 'w': case 'x': case 'y':
		case 'z':
		case 'A': case 'B': case 'C': case 'D': case 'E':
		case 'F': case 'G': case 'H': case 'I': case 'J':
		case 'K': case 'L': case 'M': case 'N': case 'O':
		case 'P': case 'Q': case 'R': case 'S': case 'T':
		case 'U': case 'V': case 'W': case 'X': case 'Y':
		case 'Z': {

			static constexpr const char8 keyword_map[][8] {
				"",
				"if",
				"auto",
				"",
				"for",
				"pub",
				"proc",
				"switch",
				"",
				"",
				"catch",
				"let",
				"case",
				"",
				"mut",
				"then",
				"impl",
				"global",
				"do",
				"",
				"func",
				"",
				"",
				"",
				"defer",
				"finally",
				"try",
				"",
				"else",
				"where",
				"trait",
				"",
			};

			static constexpr const Token::Tag tag_map[] {
				Token::Tag::EMPTY,
				Token::Tag::KwdIf,
				Token::Tag::KwdAuto,
				Token::Tag::EMPTY,
				Token::Tag::KwdFor,
				Token::Tag::KwdPub,
				Token::Tag::KwdProc,
				Token::Tag::KwdSwitch,
				Token::Tag::EMPTY,
				Token::Tag::EMPTY,
				Token::Tag::KwdCatch,
				Token::Tag::KwdLet,
				Token::Tag::KwdCase,
				Token::Tag::EMPTY,
				Token::Tag::KwdMut,
				Token::Tag::KwdThen,
				Token::Tag::KwdImpl,
				Token::Tag::KwdGlobal,
				Token::Tag::KwdDo,
				Token::Tag::EMPTY,
				Token::Tag::KwdFunc,
				Token::Tag::EMPTY,
				Token::Tag::EMPTY,
				Token::Tag::EMPTY,
				Token::Tag::KwdDefer,
				Token::Tag::KwdFinally,
				Token::Tag::KwdTry,
				Token::Tag::EMPTY,
				Token::Tag::KwdElse,
				Token::Tag::KwdWhere,
				Token::Tag::KwdTrait,
				Token::Tag::EMPTY,
			};

			c += 1;

			while ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '_')
				c += 1;

			if (*c == '\0')
				goto UNFINISHED;

			const Range string{ token_beg, c };

			const u32 hash = fnv1a(string);

			const u32 kwd_hash = ((hash >> 5) & 0b00011) | ((hash >> 12) & 0b01100) | ((hash >> 20) & 0b10000);

			if (tag_map[kwd_hash] != Token::Tag::EMPTY)
			{
				const char8* const kwd = keyword_map[kwd_hash];

				for (uint i = 0; i != string.count(); ++i)
				{
					if (string[i] != kwd[i])
						goto NO_KEYWORD;
				}

				token = Token{ tag_map[kwd_hash] };
			}
			else
			{
			NO_KEYWORD:

				const s32 index = global_data->m_strings.index_from(string, hash);

				if (index < 0)
					return false;

				token = Token{ Token::Tag::Ident, index };
			}

			break;
		}

		case '+': {

			if (next == '=')
			{
				token = Token{ Token::Tag::OpSetAdd };

				c += 2;
			}
			else
			{
				token = Token{ Token::Tag::OpAdd };

				c += 1;
			}

			break;
		}

		case '-': {

			if (next == '=')
			{
				token = Token{ Token::Tag::OpSetSub };

				c += 2;
			}
			else if (next == '>')
			{
				token = Token{ Token::Tag::ArrowR };

				c += 1;
			}
			else
			{
				token = Token{ Token::Tag::OpSub };

				c += 1;
			}

			break;
		}

		case '*': {

			if (next == '=')
			{
				token = Token{ Token::Tag::OpSetMul };

				c += 2;
			}
			else
			{
				token = Token{ Token::Tag::OpMul };

				c += 1;
			}

			break;
		}

		case '/': {

			if (next == '=')
			{
				token = Token{ Token::Tag::OpSetDiv };

				c += 2;
			}
			else if (next == '/')
			{
				c += 2;

				while (*c != '\n')
					c += 1;

				if (*c == '\0')
				{
					read_data->buffer_beg_offset = 0;

					read_data->buffer_valid_bytes = 0;

					read_data->comment_nesting = ~0u;

					return true;
				}

				c += 1;
			}
			else if (next == '*')
			{
				c += 2;

				if (!skip_comment(read_data, &c, 1))
					return true;
			}
			else
			{
				token = Token{ Token::Tag::OpDiv };

				c += 1;
			}

			break;
		}

		case '%': {

			if (next == '=')
			{
				token = Token{ Token::Tag::OpSetMod };

				c += 2;
			}
			else
			{
				token = Token{ Token::Tag::OpMod };

				c += 1;
			}

			break;
		}

		case '&': {

			if (next == '=')
			{
				token = Token{ Token::Tag::OpSetAnd };

				c += 2;
			}
			else if (next == '&')
			{
				token = Token{ Token::Tag::OpLogAnd };

				c += 2;
			}
			else
			{
				token = Token{ Token::Tag::OpAnd };

				c += 1;
			}

			break;
		}

		case '|': {

			if (next == '=')
			{
				token = Token{ Token::Tag::OpSetOr };

				c += 2;
			}
			else if (next == '|')
			{
				token = Token{ Token::Tag::OpLogOr };

				c += 2;
			}
			else
			{
				token = Token{ Token::Tag::OpOr };

				c += 1;
			}

			break;
		}

		case '^': {

			if (next == '=')
			{
				token = Token{ Token::Tag::OpSetXor };

				c += 2;
			}
			else
			{
				token = Token{ Token::Tag::OpXor };

				c += 1;
			}

			break;
		}

		case '<': {

			if (next == '=')
			{
				token = Token{ Token::Tag::OpLe };

				c += 2;
			}
			else if (next == '-')
			{
				token = Token{ Token::Tag::ArrowL };

				c += 2;
			}
			else if (next == '<')
			{
				if (c[2] == '\0')
				{
					c += 2;

					goto UNFINISHED;
				}
				else if (c[2] == '=')
				{
					token = Token{ Token::Tag::OpSetShl };

					c += 3;
				}
				else
				{
					token = Token{ Token::Tag::OpShl };

					c += 2;
				}
			}
			else
			{
				token = Token{ Token::Tag::OpLt };

				c += 1;
			}

			break;
		}

		case '>': {

			if (next == '=')
			{
				token = Token{ Token::Tag::OpGe };

				c += 2;
			}
			else if (next == '>')
			{
				if (c[2] == '\0')
				{
					c += 2;

					goto UNFINISHED;
				}
				else if (c[2] == '=')
				{
					token = Token{ Token::Tag::OpSetShr };

					c += 3;
				}
				else
				{
					token = Token{ Token::Tag::OpShr };

					c += 2;
				}
			}
			else
			{
				token = Token{ Token::Tag::OpGt };

				c += 1;
			}

			break;
		}

		case '.': {

			if (next == '*')
			{
				token = Token{ Token::Tag::UOpDeref };

				c += 2;
			}
			else if (next == '.')
			{
				if (c[2] == '\0')
				{
					c += 2;

					goto UNFINISHED;
				}
				else if (c[2] == '.')
				{
					token = Token{ Token::Tag::TypVar };

					c += 2;
				}
				else
				{
					// @TODO: Should this really fall back from '..' to '.'?

					token = Token{ Token::Tag::OpMember };

					c += 1;
				}
			}
			else
			{
				token = Token{ Token::Tag::OpMember };

				c += 1;
			}

			break;
		}

		case '!': {

			if (next == '=')
			{
				token = Token{ Token::Tag::OpNe };

				c += 2;
			}
			else
			{
				token = Token{ Token::Tag::UOpLogNot };

				c += 1;
			}

			break;
		}

		case '=': {

			if (next == '=')
			{
				token = Token{ Token::Tag::OpEq };

				c += 2;
			}
			else
			{
				token = Token{ Token::Tag::OpSet };

				c += 1;
			}

			break;
		}

		case '~': {

			token = Token{ Token::Tag::UOpNot };

			break;
		}

		case '?': {

			token = Token{ Token::Tag::TypOptPtr };

			break;
		}

		case ':': {

			token = Token{ Token::Tag::Colon };

			break;
		}

		case ',': {

			token = Token{ Token::Tag::Comma };

			break;
		}

		case '[': {

			token = Token{ Token::Tag::BracketL };

			break;
		}

		case ']': {

			token = Token{ Token::Tag::BracketR };

			break;
		}

		case '{': {

			token = Token{ Token::Tag::SquiggleL };

			break;
		}

		case '}': {

			token = Token{ Token::Tag::SquiggleR };

			break;
		}

		case '(': {

			token = Token{ Token::Tag::ParenL };

			break;
		}

		case ')': {

			token = Token{ Token::Tag::ParenR };

			break;
		}

		case '\'': {

			c += 1;

			while (true)
			{
				if (*c == '\0')
					goto UNFINISHED;
				if (*c == '\'')
					break;
				else if (*c == '\\' && c[1] == '\'')
					c += 2;
				else
					c += 1;
			}

			c += 1;

			const s32 index = global_data->m_strings.index_from(Range{ token_beg + 1, c - 1 });

			if (index < 0)
				return false;

			token = Token{ Token::Tag::LitChar, index };

			break;
		}

		case '"': {

			c += 1;

			const char8* const lit_beg = c;

			while (true)
			{
				if (*c == '\0')
					goto UNFINISHED;
				if (*c == '"')
					break;
				else if (*c == '\\' && c[1] == '"')
					c += 2;
				else
					c += 1;
			}

			const s32 index = global_data->m_strings.index_from(Range{ lit_beg, c });

			if (index < 0)
				return false;

			token = Token{ Token::Tag::LitString, index };

			break;
		}
		
		case ' ': case '\t': case '\r': case '\n': {

			c += 1;

			break;
		}

		case '\0': {

			goto UNFINISHED;

			break;
		}

		default: {

			return false;
		}
		}

		if (!push_token(token, read_data, global_data->m_max_token_count))
			return false;
	}

	return true;

UNFINISHED:

	// Check for invalid '\0' character in file.
	if (c < read_data->buffer + read_data->buffer_beg_offset + read_data->buffer_valid_bytes)
		return false;

	const u32 unfinished_bytes = static_cast<u32>(c - token_beg);

	const u32 next_page_boundary = (unfinished_bytes + global_data->m_page_bytes - 1) & ~(global_data->m_page_bytes - 1);

	const u32 beg_offset = next_page_boundary - unfinished_bytes;

	memcpy(read_data->buffer + beg_offset, token_beg, unfinished_bytes);

	read_data->buffer_beg_offset = beg_offset;

	read_data->buffer_valid_bytes = unfinished_bytes;

	return true;
}

static DWORD worker_thread_proc(void* parameter) noexcept
{
	GlobalData* global_data = static_cast<GlobalData*>(parameter);

	while (true)
	{
		DWORD transferred_bytes;

		ULONG_PTR raw_completion_key;

		OVERLAPPED* overlapped;

		if (!GetQueuedCompletionStatus(global_data->m_completion_port, &transferred_bytes, &raw_completion_key, &overlapped, INFINITE))
		{
			fprintf(stderr, "[%s] GetQueuedCompletionStatus failed with %d. Aborting.\n", global_data->m_program_name, GetLastError());

			exit(1);
		}

		const CompletionKey completion_key = static_cast<CompletionKey>(raw_completion_key);

		if (completion_key == CompletionKey::Read)
		{
			ReadData* read_data = reinterpret_cast<ReadData*>(overlapped);

			if (!lex(read_data, global_data))
			{
				fprintf(stderr, "[%s] lex failed. Aborting.\n", global_data->m_program_name);

				// @TODO: Maybe just break and continue with other files?
				exit(1);
			}

			if (read_data->remaining_file_bytes == 0)
			{
				// @TODO: Read next file or do something (?) if none are available.
				break;
			}

			const u32 buffer_offset = read_data->buffer_beg_offset + read_data->buffer_valid_bytes;

			u32 bytes_to_read;

			if (global_data->m_read_buffer_bytes - buffer_offset < read_data->remaining_file_bytes)
				bytes_to_read = global_data->m_read_buffer_bytes - buffer_offset;
			else
				bytes_to_read = static_cast<u32>(read_data->remaining_file_bytes);

			read_data->remaining_file_bytes -= bytes_to_read;

			read_data->buffer_valid_bytes += bytes_to_read;

			const u64 new_file_offset = read_data->overlapped.Offset + (static_cast<u64>(read_data->overlapped.OffsetHigh) << 32) + transferred_bytes;

			read_data->overlapped.Offset = static_cast<u32>(new_file_offset);

			read_data->overlapped.OffsetHigh = static_cast<u32>(new_file_offset >> 32);

			if (!ReadFile(read_data->file_handle, read_data->buffer + buffer_offset, bytes_to_read, nullptr, &read_data->overlapped) && GetLastError() != ERROR_IO_PENDING)
			{
				fprintf(stderr, "[%s] ReadFile failed with %d. Aborting.\n", global_data->m_program_name, GetLastError());

				exit(1);
			}
		}
		else if (completion_key == CompletionKey::Lex)
		{
			// @TODO
		}
		else if (completion_key == CompletionKey::Parse)
		{
			// @TODO
		}
		else
		{
			fprintf(stderr, "[%s] Unexpected value for completion_key (%llu). Aborting.\n", global_data->m_program_name, raw_completion_key);

			exit(1);
		}
	}

	if (InterlockedDecrement(&global_data->m_running_thread_count) == 0)
	{
		if (!SetEvent(global_data->m_thread_completion_event))
		{
			fprintf(stderr, "[%s] SetEvent failed with %d. Aborting.\n", global_data->m_program_name, GetLastError());

			exit(1);
		}
	}

	return 0;
}
*/

template<typename DataEntry>
struct DataEntryAndIndex
{
	DataEntry* entry;

	s32 index;
};

static u16 robin_hood_hash(u32 raw_hash, u32 hash_mask) noexcept
{
	const u16 h = (raw_hash >> 16) & hash_mask;

	return h == 0 ? 0x8000 : h;
}

template<typename T>
static u32 data_entry_bytes(u16 tail_bytes)
{
	return (offsetof(T, tail) + tail_bytes + alignof(T) - 1) & ~(alignof(T) - 1);
}

template<typename T>
static T* next_data_entry(T* e) noexcept
{
	return reinterpret_cast<T*>(reinterpret_cast<byte*>(e) + data_entry_bytes<T>(e->tail_bytes));
}

template<typename T>
static const T* next_data_entry(const T* e) noexcept
{
	return reinterpret_cast<const T*>(reinterpret_cast<const byte*>(e) + data_entry_bytes<T>(e->tail_bytes));
}

template<typename T>
static void map_create_ind_for_data_entry(RobinHoodMap<T>* map, const T* e) noexcept
{
	u16* const inds = static_cast<u16*>(map->inds);

	u32* const offs = reinterpret_cast<u32*>(inds + map->inds_committed_count);

	const u32 hash = e->hash;

	const u32 index_mask = map->inds_committed_count - 1;

	u32 i = hash & index_mask;

	u16 ind_to_insert = robin_hood_hash(hash, map->index_hash_mask);

	u32 off_to_insert = static_cast<u32>((reinterpret_cast<const byte*>(e) - static_cast<const byte*>(map->data)) / alignof(T));

	while (true)
	{
		const u16 ind = inds[i];

		if (ind == 0)
		{
			inds[i] = ind_to_insert;

			offs[i] = off_to_insert;

			return;
		}
		else if (static_cast<u16>(ind & map->index_psl_mask) < (ind_to_insert & map->index_psl_mask))
		{
			const u16 new_ind_to_insert = inds[i];

			const u32 new_off_to_insert = offs[i];

			inds[i] = ind_to_insert;

			offs[i] = off_to_insert;

			ind_to_insert = new_ind_to_insert;

			off_to_insert = new_off_to_insert;
		}

		assert((ind_to_insert & map->index_psl_mask) != map->index_psl_mask);

		ind_to_insert += 1;

		if (i == index_mask)
			i = 0;
		else
			i += 1;
	}
}

template<typename T>
static bool map_init(RobinHoodMap<T>* map) noexcept
{
	void* inds = VirtualAlloc(nullptr, map->inds_reserved_count * 6, MEM_RESERVE, PAGE_READWRITE);

	void* data = VirtualAlloc(nullptr, map->data_reserved_bytes, MEM_RESERVE, PAGE_READWRITE);

	if (inds == nullptr || data == nullptr)
		goto ERROR;

	if (VirtualAlloc(inds, map->inds_initial_commit_count * 6, MEM_COMMIT, PAGE_READWRITE) == nullptr)
		goto ERROR;

	if (VirtualAlloc(data, map->data_initial_commit_bytes, MEM_COMMIT, PAGE_READWRITE) == nullptr)
		goto ERROR;

	map->lock = SRWLOCK_INIT;

	map->inds = inds;

	map->inds_used_count = 0;

	map->inds_committed_count = map->inds_initial_commit_count;

	map->data = data;

	map->data_used_bytes = 0;

	map->data_committed_bytes = map->data_initial_commit_bytes;

	return true;

ERROR:
	if (inds != nullptr)
		VirtualFree(inds, 0, MEM_RELEASE);

	if (data != nullptr)
		VirtualFree(data, 0, MEM_RELEASE);

	return false;
}

template<typename T>
static bool map_deinit(RobinHoodMap<T>* map) noexcept
{
	bool is_ok = true;

	if (map->inds != nullptr)
		is_ok &= static_cast<bool>(VirtualFree(map->inds, 0, MEM_RELEASE));

	if (map->data != nullptr)
		is_ok &= static_cast<bool>(VirtualFree(map->data, 0, MEM_RELEASE));

	memset(map, 0, sizeof(*map));

	return is_ok;
}

template<typename T>
static bool map_grow_data(RobinHoodMap<T>* map, u32 extra_bytes) noexcept
{
	if (map->data_reserved_bytes == map->data_committed_bytes)
		return false;

	const u32 actual_extra_bytes = (extra_bytes + map->data_commit_increment_bytes - 1) & ~(map->data_commit_increment_bytes - 1);

	if (VirtualAlloc(static_cast<byte*>(map->data) + map->data_committed_bytes, actual_extra_bytes, MEM_COMMIT, PAGE_READWRITE) == nullptr)
		return false;

	map->data_committed_bytes += actual_extra_bytes;

	return true;
}

template<typename T>
static bool map_grow_inds(RobinHoodMap<T>* map) noexcept
{
	if (map->inds_committed_count == map->inds_reserved_count)
		return false;

	if (VirtualAlloc(static_cast<byte*>(map->inds) + map->inds_committed_count * 6, map->inds_committed_count * 6, MEM_COMMIT, PAGE_READWRITE) == nullptr)
		return false;

	memset(map->inds, 0, map->inds_committed_count * 6);

	map->inds_committed_count *= 2;

	for (T* e = static_cast<T*>(map->data); e != reinterpret_cast<T*>(static_cast<byte*>(map->data) + map->data_used_bytes); e = next_data_entry(e))
		map_create_ind_for_data_entry(map, e);

	return true;
}

template<typename T>
static u32 map_find_entry(const volatile RobinHoodMap<T>* map, Range<char8> name, u32 hash) noexcept
{
	const u16* const inds = static_cast<const u16*>(map->inds);

	const u32* const offs = reinterpret_cast<const u32*>(inds + map->inds_committed_count);

	const byte* const data = static_cast<byte*>(map->data);

	const u32 index_mask = map->inds_committed_count - 1;

	u16 ind_to_find = robin_hood_hash(hash, map->index_hash_mask);

	u32 i = hash & index_mask;

	while (true)
	{
		const u16 ind = inds[i];

		if (ind == ind_to_find)
		{
			const u32 off = offs[i];

			const T* e = reinterpret_cast<const T*>(data + off * alignof(T));

			if (e->tail_bytes == name.count() && memcmp(e->tail, name.begin(), name.count()) == 0)
				return off;
		}
		else if (ind == 0 || (ind & map->index_psl_mask) < (ind_to_find & map->index_psl_mask))
		{
			return ~0u;
		}

		ind_to_find += 1;

		if (i == index_mask)
			i = 0;
		else
			i += 1;
	}
}

template<typename T>
static u32 map_insert_or_find_data_entry(RobinHoodMap<T>* map, Range<char8> name, u32 hash) noexcept
{
	// Optimistally assume that name is likely already present in the map.
	// For this case, a shared lock is sufficient, since we aren't writing
	// anything.
	AcquireSRWLockShared(&map->lock);

	assert(name.count() < 0x10000);

	const u16 name_bytes = static_cast<u16>(name.count());

	const u32 shared_found_ind = map_find_entry(map, name, hash);

	if (shared_found_ind != ~0u)
	{
		ReleaseSRWLockShared(&map->lock);

		return shared_found_ind;
	}

	// Our optimistic assumption was wrong.
	// Release shared lock and acquire exclusive. Sadly, SRWLocks cannot be
	// upgraded, so this little dance and the re-checking for matches and
	// possible rehashing are all necessary.
	ReleaseSRWLockShared(&map->lock);
	AcquireSRWLockExclusive(&map->lock);

	// Check whether name was inserted while we did not hold the lock.
	// Since map_find_entry takes map as volatile, this cannot accidentally
	// be optimized away by reusing shared_found_ind.
	const u32 exlusive_found_ind = map_find_entry(map, name, hash);

	if (exlusive_found_ind != ~0u)
	{
		ReleaseSRWLockExclusive(&map->lock);

		return exlusive_found_ind;
	}

	const u32 extra_bytes = data_entry_bytes<T>(name_bytes);

	if (map->data_used_bytes + extra_bytes > map->data_committed_bytes)
	{
		if (!map_grow_data(map, extra_bytes))
		{
			ReleaseSRWLockExclusive(&map->lock);

			return ~0u;
		}
	}

	T* e = reinterpret_cast<T*>(static_cast<byte*>(map->data) + map->data_used_bytes);

	e->hash = hash;

	e->tail_bytes = name_bytes;

	memcpy(e->tail, name.begin(), name_bytes);

	map->data_used_bytes += extra_bytes;

	// Since inds never gets full, inserting into it without checking
	// inds_committed_count is greater than inds_used_count is fine. This
	// is ensured by the below check, which grows inds 'prematurely', thus
	// also ensuring our load factor does not get out of hand.
	map_create_ind_for_data_entry(map, e);

	map->inds_used_count += 1;

	if (map->inds_used_count * 6 > map->inds_committed_count * 5)
	{
		if (!map_grow_inds(map))
		{
			ReleaseSRWLockExclusive(&map->lock);

			return ~0u;
		}
	}

	ReleaseSRWLockExclusive(&map->lock);

	return static_cast<u32>((reinterpret_cast<byte*>(e) - static_cast<byte*>(map->data)) / alignof(T));
}

template<typename T>
static void map_get_diagnostics(const RobinHoodMap<T>* map, SimpleMapDiagnostics* out) noexcept
{
	out->indices_used_count = map->inds_used_count;

	out->indices_committed_count = map->inds_committed_count;

	out->data_used_bytes = map->data_used_bytes;

	out->data_committed_bytes = map->data_committed_bytes;

	out->data_overhead = offsetof(T, tail);

	out->data_stride = alignof(T);
}

template<typename T>
static void map_get_diagnostics(const RobinHoodMap<T>* map, FullMapDiagnostics* out) noexcept
{
	const u16* const inds = static_cast<const u16*>(map->inds);

	map_get_diagnostics(map, &out->simple);

	memset(out->probe_seq_len_counts, 0, sizeof(out->probe_seq_len_counts));

	u16 max_psl = 0;

	for (u32 i = 0; i != map->inds_committed_count; ++i)
	{
		if (inds[i] == 0)
			continue;

		const u16 psl = inds[i] & map->index_psl_mask;

		out->probe_seq_len_counts[psl] += 1;

		if (psl > max_psl)
			max_psl = psl;
	}

	u32 total_string_bytes = 0;

	u16 max_string_bytes = 0;

	for (T* e = static_cast<T*>(map->data); e != reinterpret_cast<T*>(static_cast<byte*>(map->data) + map->data_used_bytes); e = next_data_entry(e))
	{
		const u16 string_bytes = e->tail_bytes;

		total_string_bytes += string_bytes;

		if (string_bytes > max_string_bytes)
			max_string_bytes = string_bytes;
	}

	out->max_probe_seq_len = max_psl + 1;

	out->max_string_bytes = max_string_bytes;

	out->total_string_bytes = total_string_bytes;
}



bool StringSet::init() noexcept
{
	return map_init(&m_map);
}

bool StringSet::deinit() noexcept
{
	return map_deinit(&m_map);
}

s32 StringSet::index_from(Range<char8> string) noexcept
{
	const u32 hash = fnv1a(string);

	return index_from(string, hash);
}

s32 StringSet::index_from(Range<char8> string, u32 hash) noexcept
{
	return map_insert_or_find_data_entry(&m_map, string, hash);
}

Range<char8> StringSet::string_from(s32 index) const noexcept
{
	assert(index >= 0 && static_cast<u32>(index) < (m_map.data_used_bytes / 4));

	const DataEntry* entry = reinterpret_cast<const DataEntry*>(static_cast<const byte*>(m_map.data) + index * alignof(DataEntry));

	return Range{ entry->tail, entry->tail_bytes };
}

void StringSet::get_diagnostics(SimpleMapDiagnostics* out) const noexcept
{
	map_get_diagnostics(&m_map, out);
}

void StringSet::get_diagnostics(FullMapDiagnostics* out) const noexcept
{
	map_get_diagnostics(&m_map, out);
}



bool InputFileSet::init() noexcept
{
	return map_init(&m_map);
}

bool InputFileSet::deinit() noexcept
{
	return map_deinit(&m_map);
}

bool InputFileSet::add_file(Range<char8> path, HANDLE handle) noexcept
{
	const u32 hash = fnv1a(path);

	const u32 index = map_insert_or_find_data_entry(&m_map, path, hash);

	if (index == ~0u)
		return false;

	DataEntry* entry = reinterpret_cast<DataEntry*>(static_cast<byte*>(m_map.data) + index * alignof(DataEntry));

	if (entry->handle != nullptr)
		return true;

	AcquireSRWLockExclusive(&m_map.lock);

	entry->handle = handle;

	if (m_head == nullptr)
		entry->next_index = ~0u;
	else
		entry->next_index = static_cast<u32>((reinterpret_cast<byte*>(m_head) - static_cast<byte*>(m_map.data)) / alignof(DataEntry));

	m_head = entry;

	ReleaseSRWLockExclusive(&m_map.lock);

	return true;
}

HANDLE InputFileSet::get_file() noexcept
{
	AcquireSRWLockExclusive(&m_map.lock);

	if (m_head == nullptr)
		return nullptr;

	HANDLE ret = m_head->handle;

	u32 next_index = m_head->next_index;

	if (next_index == ~0u)
		m_head = nullptr;
	else
		m_head = reinterpret_cast<DataEntry*>(reinterpret_cast<byte*>(m_map.data) + next_index * alignof(DataEntry));

	ReleaseSRWLockExclusive(&m_map.lock);

	return ret;
}

void InputFileSet::get_diagnostics(SimpleMapDiagnostics* out) const noexcept
{
	map_get_diagnostics(&m_map, out);
}

void InputFileSet::get_diagnostics(FullMapDiagnostics* out) const noexcept
{
	map_get_diagnostics(&m_map, out);
}



bool ReadList::init(u32 buffer_count, u32 per_buffer_bytes) noexcept
{
	SYSTEM_INFO sysinfo;

	GetSystemInfo(&sysinfo);

	const u32 padded_per_buffer_bytes = (per_buffer_bytes + sysinfo.dwAllocationGranularity - 1) & ~(sysinfo.dwAllocationGranularity - 1);

	const u32 per_read_extra_bytes = (static_cast<u32>(sizeof(PerReadData)) + sysinfo.dwAllocationGranularity - 1) & ~(sysinfo.dwAllocationGranularity - 1);

	const u32 stride = padded_per_buffer_bytes + per_read_extra_bytes;

	const u64 total_bytes = static_cast<u64>(buffer_count) * stride;

	if ((m_buffers = VirtualAlloc(nullptr, total_bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) == nullptr)
		return false;

	m_buffer_count = buffer_count;

	m_per_buffer_bytes = per_buffer_bytes;

	m_stride = stride;

	InitializeSListHead(&m_free_list);

	for (u32 i = 0; i != m_buffer_count; ++i)
	{
		PerReadData* data = reinterpret_cast<PerReadData*>(static_cast<byte*>(m_buffers) + static_cast<u64>(i) * m_stride + m_per_buffer_bytes);

		InterlockedPushEntrySList(&m_free_list, &data->free_list_entry);
	}

	return true;
}

bool ReadList::deinit() noexcept
{
	bool is_ok = true;

	if (m_buffers != nullptr)
		is_ok &= static_cast<bool>(VirtualFree(m_buffers, 0, MEM_RELEASE));

	memset(this, 0, sizeof(*this));

	return true;
}

void* ReadList::buffer_from(const PerReadData* ptr) noexcept
{
	return reinterpret_cast<byte*>(const_cast<PerReadData*>(ptr)) - m_per_buffer_bytes;
}

u32 ReadList::buffer_bytes() const noexcept
{
	return m_per_buffer_bytes;
}

PerReadData* ReadList::claim_read_data() noexcept
{
	SLIST_ENTRY* entry = InterlockedPopEntrySList(&m_free_list);

	if (entry == nullptr)
		return nullptr;

	PerReadData* data = reinterpret_cast<PerReadData*>(reinterpret_cast<byte*>(entry) - offsetof(PerReadData, free_list_entry));

	memset(data, 0, sizeof(*data));

	return data;
}

void ReadList::free_read_data(PerReadData* data) noexcept
{
	InterlockedPushEntrySList(&m_free_list, &data->free_list_entry);
}
