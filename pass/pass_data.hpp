#ifndef PARSEDATA_INCLUDE_GUARD
#define PARSEDATA_INCLUDE_GUARD

#include "../infra/common.hpp"
#include "../infra/container.hpp"
#include "../infra/threading.hpp"
#include "ast.hpp"

// Lexer Tokens
enum class Token : u8
{
		EMPTY = 0,
		KwdIf,                // if
		KwdThen,              // then
		KwdElse,              // else
		KwdFor,               // for
		KwdDo,                // do
		KwdFinally,           // finally
		KwdSwitch,            // switch
		KwdCase,              // case
		KwdFunc,              // func
		KwdProc,              // proc
		KwdTrait,             // trait
		KwdImpl,              // impl
		KwdWhere,             // where
		KwdExpects,           // expects
		KwdEnsures,           // ensures
		KwdCatch,             // catch
		KwdLet,               // let
		KwdPub,               // pub
		KwdMut,               // mut
		KwdGlobal,            // global
		KwdAuto,              // auto
		KwdUse,               // use
		ArrayInitializer,     // .[
		CompositeInitializer, // .{
		BracketR,             // ]
		BracketL,             // [
		CurlyR,               // }
		CurlyL,               // {
		ParenR,               // )
		ParenL,               // (
		KwdEval,              // eval
		KwdTry,               // try
		KwdDefer,             // defer
		UOpAddr,              // $
		UOpNot,               // ~
		UOpLogNot,            // !
		TypOptPtr,            // ?
		TypVar,               // ...
		TypTailArray,         // [...]
		TypMultiPtr,          // [*]
		TypOptMultiPtr,       // [?]
		TypSlice,             // []
		OpMemberOrRef,        // .
		OpMulOrTypPtr,        // *
		OpSub,                // -
		OpAdd,                // +
		OpDiv,                // /
		OpAddTC,              // +:
		OpSubTC,              // -:
		OpMulTC,              // *:
		OpMod,                // %
		UOpDeref,             // .*
		OpAnd,                // &
		OpOr,                 // |
		OpXor,                // ^
		OpShl,                // <<
		OpShr,                // >>
		OpLogAnd,             // &&
		OpLogOr,              // ||
		OpLt,                 // <
		OpGt,                 // >
		OpLe,                 // <=
		OpGe,                 // >=
		OpNe,                 // !=
		OpEq,                 // ==
		OpSet,                // =
		OpSetAdd,             // +=
		OpSetSub,             // -=
		OpSetMul,             // *=
		OpSetDiv,             // /=
		OpSetAddTC,           // +:=
		OpSetSubTC,           // -:=
		OpSetMulTC,           // *:=
		OpSetMod,             // %=
		OpSetAnd,             // &=
		OpSetOr,              // |=
		OpSetXor,             // ^=
		OpSetShl,             // <<=
		OpSetShr,             // >>=
		Colon,                // :
		Comma,                // ,
		ThinArrowL,           // <-
		ThinArrowR,           // ->
		WideArrowR,           // =>
		Pragma,               // #
		LitInteger,           // ( '0' - '9' )+
		LitFloat,             // ( '0' - '9' )+ '.' ( '0' - '9' )+
		LitChar,              // '\'' .* '\''
		LitString,            // '"' .* '"'
		Ident,                // ( 'a' - 'z' | 'A' - 'Z' ) ( 'a' - 'z' | 'A' - 'Z' | '0' - '9' | '_' )*
		Wildcard,           // _
		END_OF_SOURCE,
		MAX,
};



// Used to keep track of a single asynchronous file being read by
// read::request_read.
struct Read
{
	minos::Overlapped overlapped;

	minos::FileHandle filehandle;

	char8* content;

	u32 bytes;

	u32 next;

	u32 filepath_id;
};

// Used by read::* to keep track of all resources required for reading files. 
struct ReadData
{
	thd::IndexStackListHeader<Read, offsetof(Read, next)> completed_reads;

	thd::IndexStackListHeader<Read, offsetof(Read, next)> unused_reads;

	thd::Semaphore available_read_count;

	std::atomic<u32> pending_read_count;

	Read reads[512];

	minos::CompletionHandle completion_handle;

	minos::ThreadHandle completion_thread;
};

// Returned by read::poll_completed_read and read::await_completed_read to
// return the read file's content.
struct SourceFile
{
private:

	MutAttachmentRange<char8, u32> m_content_and_filepath;

public:

	SourceFile() noexcept : m_content_and_filepath{ nullptr, nullptr } {}

	SourceFile(char8* begin, u32 bytes, u32 filepath_id) noexcept : m_content_and_filepath{ begin, bytes, filepath_id } {}

	Range<char8> content() const noexcept
	{
		return m_content_and_filepath.range();
	}

	char8* raw_begin() noexcept
	{
		return m_content_and_filepath.begin();
	}

	u32 filepath_id() const noexcept
	{
		return m_content_and_filepath.attachment();
	}
};



struct alignas(8) IdentifierMapEntry
{
	u32 m_hash;

	u16 m_length;

	Token m_token;

	#pragma warning(push)
	#pragma warning(disable : 4200) // C4200: nonstandard extension used: zero-sized array in struct/union
	char8 m_chars[];
	#pragma warning(pop)

	static constexpr u32 stride() noexcept
	{
		return 8;
	}

	static u32 required_strides(Range<char8> key) noexcept
	{
		return static_cast<u32>((offsetof(IdentifierMapEntry, m_chars) + key.count() + stride() - 1) / stride());
	}

	u32 used_strides() const noexcept
	{
		return static_cast<u32>((offsetof(IdentifierMapEntry, m_chars) + m_length + stride() - 1) / stride());
	}

	u32 hash() const noexcept
	{
		return m_hash;
	}

	bool equal_to_key(Range<char8> key, u32 key_hash) const noexcept
	{
		return m_hash == key_hash && key.count() == m_length && memcmp(key.begin(), m_chars, m_length) == 0;
	}

	void init(Range<char8> key, u32 key_hash) noexcept
	{
		m_hash = key_hash;

		m_length = static_cast<u16>(key.count());

		m_token = Token::Ident;

		memcpy(m_chars, key.begin(), key.count());
	}

	Range<char8> range() const noexcept
	{
		return Range<char8>{ m_chars, m_length };
	}

	Token token() const noexcept
	{
		return m_token;
	}

	void set_token(Token token) noexcept
	{
		m_token = token;
	}
};

using IdentifierMap = IndexMap<Range<char8>, IdentifierMapEntry>;



struct Globals
{
	IdentifierMap identifiers;

	ReservedVec<u32> asts;

	ReservedVec<byte> values;

	ReservedVec<u32> ast_scratch;

	ReservedVec<u32> stack_scratch;

	ReadData read;

	Globals() noexcept;
};



const char8* token_name(Token token) noexcept;

namespace read
{
	void request_read(Globals* data, Range<char8> filepath, u32 filepath_id) noexcept;

	[[nodiscard]] bool poll_completed_read(Globals* data, Range<char8>* out) noexcept;

	[[nodiscard]] bool await_completed_read(Globals* data, SourceFile* out) noexcept;

	void release_read(Globals* data, SourceFile file) noexcept;
}

[[nodiscard]] ast::Tree parse(Globals* data, SourceFile source) noexcept;

#endif // PARSEDATA_INCLUDE_GUARD
