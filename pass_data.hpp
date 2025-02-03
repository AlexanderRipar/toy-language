#ifndef PARSEDATA_INCLUDE_GUARD
#define PARSEDATA_INCLUDE_GUARD

#include "infra/common.hpp"
#include "infra/container.hpp"
#include "infra/threading.hpp"
#include "infra/alloc_pool.hpp"
#include "ast2.hpp"



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
		KwdGlobal,            // global
		KwdAuto,              // auto
		KwdUse,               // use
		KwdReturn,            // return
		KwdLeave,             // leave
		KwdYield,             // yield
		ArrayInitializer,     // .[
		CompositeInitializer, // .{
		BracketR,             // ]
		BracketL,             // [
		CurlyR,               // }
		CurlyL,               // {
		ParenR,               // )
		ParenL,               // (
		KwdMut,               // mut
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

const char8* token_name(Token token) noexcept;



struct alignas(8) IdentifierEntry
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
		return static_cast<u32>((offsetof(IdentifierEntry, m_chars) + key.count() + stride() - 1) / stride());
	}

	u32 used_strides() const noexcept
	{
		return static_cast<u32>((offsetof(IdentifierEntry, m_chars) + m_length + stride() - 1) / stride());
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

struct IdentifierPool;

struct IdentifierId
{
	u32 rep;
};

IdentifierPool* create_identifier_pool(AllocPool* pool) noexcept;

IdentifierEntry* entry_from_identifier(IdentifierPool* identifiers, Range<char8> identifier) noexcept;

IdentifierId id_from_identifier(IdentifierPool* identifiers, Range<char8> identifier) noexcept;

IdentifierEntry* entry_from_id(IdentifierPool* identifiers, IdentifierId id) noexcept;



struct SourceFile
{
private:

	MutAttachmentRange<char8, IdentifierId> m_content_and_filepath;

public:

	SourceFile() noexcept : m_content_and_filepath{ nullptr, nullptr } {}

	SourceFile(char8* begin, u32 bytes, IdentifierId filepath_id) noexcept : m_content_and_filepath{ begin, bytes, filepath_id } {}

	Range<char8> content() const noexcept
	{
		return m_content_and_filepath.range();
	}

	char8* raw_begin() noexcept
	{
		return m_content_and_filepath.begin();
	}

	IdentifierId filepath_id() const noexcept
	{
		return m_content_and_filepath.attachment();
	}
};

struct SourceReader;

SourceReader* create_source_reader(AllocPool* pool) noexcept;

void request_read(SourceReader* reader, Range<char8> filepath, IdentifierId filepath_id) noexcept;

[[nodiscard]] bool poll_completed_read(SourceReader* reader, SourceFile* out) noexcept;

[[nodiscard]] bool await_completed_read(SourceReader* reader, SourceFile* out) noexcept;

void release_read(SourceReader* reader, SourceFile file) noexcept;



struct Parser;

[[nodiscard]] Parser* create_parser(AllocPool* pool, IdentifierPool* identifiers) noexcept;

[[nodiscard]] a2::Node* parse(Parser* parser, SourceFile source, ReservedVec<u32>* out) noexcept;



struct DefinitionDesc
{
	IdentifierId identifier_id;

	u32 definition_offset;
};

struct Namespace
{
	u16 definition_count;

	u16 use_count;

	u32 block_index;

	#pragma warning(push)
	#pragma warning(disable : 4200) // nonstandard extension used: zero-sized array in struct/union
	DefinitionDesc definitions[];
	#pragma warning(pop)
};

static inline Range<DefinitionDesc> definitions(Namespace* ns) noexcept
{
	return { ns->definitions, ns->definition_count };
}

static inline Range<u16> use_indices(Namespace* ns) noexcept
{
	return { reinterpret_cast<u16*>(ns->definitions + ns->definition_count), ns->use_count };
}

struct NameResolver;

[[nodiscard]] NameResolver* create_name_resolver(AllocPool* pool, IdentifierPool* identifiers) noexcept;

[[nodiscard]] a2::Node* resolve_names(NameResolver* resolver, a2::Node* root, ReservedVec<u32>* out) noexcept;

#endif // PARSEDATA_INCLUDE_GUARD
