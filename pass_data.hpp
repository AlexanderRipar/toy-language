#ifndef PARSEDATA_INCLUDE_GUARD
#define PARSEDATA_INCLUDE_GUARD

#include "infra/common.hpp"
#include "infra/container.hpp"
#include "infra/threading.hpp"
#include "infra/alloc_pool.hpp"
#include "ast2.hpp"

static constexpr u32 MAX_FUNC_PARAMETER_COUNT = 255;



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



struct CompIntegerValue
{
	s64 value;
};

inline CompIntegerValue create_comp_integer(s64 value) noexcept
{
	return CompIntegerValue{ value };
}

inline bool comp_integer_as_u64(CompIntegerValue* comp_integer, u64* out) noexcept
{
	if (comp_integer->value < 0)
		return false;

	*out = static_cast<u64>(comp_integer->value);

	return true;
}



struct IdentifierPool;

struct IdentifierId
{
	u32 rep;
};

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

static constexpr IdentifierId INVALID_IDENTIFIER_ID = { 0 };

static inline bool operator==(IdentifierId lhs, IdentifierId rhs) noexcept
{
	return lhs.rep == rhs.rep;
}

static inline bool operator!=(IdentifierId lhs, IdentifierId rhs) noexcept
{
	return lhs.rep != rhs.rep;
}

IdentifierPool* create_identifier_pool(AllocPool* pool) noexcept;

IdentifierEntry* identifier_entry_from_identifier(IdentifierPool* identifiers, Range<char8> identifier) noexcept;

IdentifierId id_from_identifier(IdentifierPool* identifiers, Range<char8> identifier) noexcept;

IdentifierEntry* identifier_entry_from_id(IdentifierPool* identifiers, IdentifierId id) noexcept;



struct TypePool;

struct TypeId
{
	u32 rep;
};

struct BuiltinTypeIds
{
	TypeId comp_integer_type_id;

	TypeId comp_float_type_id;

	TypeId comp_string_type_id;

	TypeId type_type_id;

	TypeId void_type_id;

	TypeId bool_type_id;
};

enum class TypeTag : u8
{
	INVALID = 0,
	Void,
	Type,
	CompInteger,
	CompFloat,
	CompString,
	Integer,
	Float,
	Boolean,
	Slice,
	Ptr,
	Alias,
	Array,
	Func,
	Composite,
	CompositeLiteral,
	ArrayLiteral,
};

enum class TypeFlag : u8
{
	EMPTY            = 0,
	SliceOrPtr_IsMut = 0x01,
	Ptr_IsOpt        = 0x02,
	Ptr_IsMulti      = 0x04,
	Integer_IsSigned = 0x02,
	Func_IsProc      = 0x01,
};

static inline TypeFlag operator&(TypeFlag lhs, TypeFlag rhs) noexcept
{
	return static_cast<TypeFlag>(static_cast<u8>(lhs) & static_cast<u8>(rhs));
}

static inline TypeFlag operator|(TypeFlag lhs, TypeFlag rhs) noexcept
{
	return static_cast<TypeFlag>(static_cast<u8>(lhs) | static_cast<u8>(rhs));
}

static inline TypeFlag operator&=(TypeFlag& lhs, TypeFlag rhs) noexcept
{
	lhs = static_cast<TypeFlag>(static_cast<u8>(lhs) & static_cast<u8>(rhs));

	return lhs;
}

static inline TypeFlag operator|=(TypeFlag& lhs, TypeFlag rhs) noexcept
{
	lhs = static_cast<TypeFlag>(static_cast<u8>(lhs) | static_cast<u8>(rhs));

	return lhs;
}

struct IntegerType
{
	u8 bits;
};

struct FloatType
{
	u8 bits;
};

struct SliceType
{
	TypeId element_id;
};

struct PtrType
{
	TypeId pointee_id;
};

struct AliasType
{
	TypeId aliased_id;
};

struct ArrayType
{
	TypeId element_id;

	u64 count;
};

struct FuncTypeHeader
{
	TypeId return_type_id;

	u32 parameter_count;
};

struct FuncType
{
	FuncTypeHeader header;

	#pragma warning(push)
	#pragma warning(disable : 4200) // nonstandard extension used: zero-sized array in struct/union
	TypeId parameter_type_ids[];
	#pragma warning(pop)
};

struct FuncTypeBuffer
{
	FuncTypeHeader header;

	TypeId parameter_type_ids[MAX_FUNC_PARAMETER_COUNT];
};

struct CompositeTypeMember
{
	IdentifierId name;

	TypeId type;

	u32 offset : 28;

	u32 is_mut : 1;

	u32 is_pub : 1;

	u32 is_global : 1;

	u32 is_use : 1;
};

struct CompositeTypeHeader
{
	u32 size;

	u32 alignment;

	u32 member_count;
};

struct CompositeType
{
	CompositeTypeHeader header;

	#pragma warning(push)
	#pragma warning(disable : 4200) // nonstandard extension used: zero-sized array in struct/union
	CompositeTypeMember members[];
	#pragma warning(pop)
};

struct TypeKey
{
	TypeTag tag;

	TypeFlag flags;

	Range<byte> bytes;
};

struct alignas(8) TypeEntry
{
	u32 m_hash;

	u16 size;

	TypeTag tag;

	TypeFlag flags;

	#pragma warning(push)
	#pragma warning(disable : 4200) // C4200: nonstandard extension used: zero-sized array in struct/union
	byte m_value[];
	#pragma warning(pop)

	static constexpr u32 stride() noexcept
	{
		return 8;
	}

	static u32 required_strides(TypeKey key) noexcept
	{
		return static_cast<u32>((offsetof(TypeEntry, m_value) + key.bytes.count() + stride() - 1) / stride());
	}

	u32 used_strides() const noexcept
	{
		return static_cast<u32>((offsetof(TypeEntry, m_value) + size + stride() - 1) / stride());
	}

	u32 hash() const noexcept
	{
		return m_hash;
	}

	bool equal_to_key(TypeKey key, u32 key_hash) const noexcept
	{
		return m_hash == key_hash && key.bytes.count() == size && memcmp(key.bytes.begin(), m_value, size) == 0;
	}

	void init(TypeKey key, u32 key_hash) noexcept
	{
		m_hash = key_hash;

		size = static_cast<u16>(key.bytes.count());

		tag = key.tag;

		flags = key.flags;

		memcpy(m_value, key.bytes.begin(), key.bytes.count());
	}

	template<typename T>
	T* data() noexcept
	{
		return reinterpret_cast<T*>(m_value);
	}

	template<typename T>
	const T* data() const noexcept
	{
		return reinterpret_cast<const T*>(m_value);
	}
};

static constexpr TypeId INVALID_TYPE_ID = { 0 };

static inline bool operator==(TypeId lhs, TypeId rhs) noexcept
{
	return lhs.rep == rhs.rep;
}

static inline bool operator!=(TypeId lhs, TypeId rhs) noexcept
{
	return lhs.rep != rhs.rep;
}

TypePool* create_type_pool(AllocPool* pool) noexcept;

TypeEntry* type_entry_from_type(TypePool* types, TypeTag tag, TypeFlag flags, Range<byte> bytes) noexcept;

TypeId id_from_type(TypePool* types, TypeTag tag, TypeFlag flags, Range<byte> bytes) noexcept;

TypeEntry* type_entry_from_id(TypePool* types, TypeId id) noexcept;

TypeId id_from_type_entry(TypePool* types, TypeEntry* entry) noexcept;

const BuiltinTypeIds* get_builtin_type_ids(const TypePool* types) noexcept;

TypeId dealias_type_id(TypePool* types, TypeEntry* entry) noexcept;

TypeId dealias_type_id(TypePool* types, TypeId id) noexcept;

TypeEntry* dealias_type_entry(TypePool* types, TypeEntry* entry) noexcept;

TypeEntry* dealias_type_entry(TypePool* types, TypeId id) noexcept;

bool can_implicity_convert_from_to(TypePool* types, TypeId from, TypeId to) noexcept;

OptPtr<TypeEntry> find_common_type_entry(TypePool* types, TypeEntry* a, TypeEntry* b) noexcept;



struct ValuePool;

struct ValueId
{
	u32 rep;
};

struct alignas(u64) ValueHeader
{
	TypeId type_id;

	u32 prev_offset : 30; // Only valid for values kept in a stack; 0 when created using alloc_value

	u32 is_ref : 1;

	u32 is_undefined : 1;
};

struct Value
{
	ValueHeader header;

	#pragma warning(push)
	#pragma warning(disable: 4200) // C4200: nonstandard extension used: zero-sized array in struct/union
	byte value[];
	#pragma warning(pop)
};

struct ReferenceValue
{
	Value* referenced;
};

struct ValueLocation
{
	Value* ptr;

	ValueId id;
};

static constexpr ValueId INVALID_VALUE_ID = { 0 };

static inline bool operator==(ValueId lhs, ValueId rhs) noexcept
{
	return lhs.rep == rhs.rep;
}

static inline bool operator!=(ValueId lhs, ValueId rhs) noexcept
{
	return lhs.rep != rhs.rep;
}

template<typename T>
static inline T* access_value(Value* value) noexcept
{
	if (value->header.is_ref)
		value = reinterpret_cast<ReferenceValue*>(value->value)->referenced;

	return reinterpret_cast<T*>(value->value);
}

ValuePool* create_value_pool(AllocPool* alloc) noexcept;

void release_value_pool(ValuePool* values) noexcept;

ValueLocation alloc_value(ValuePool* values, u32 bytes) noexcept;

Value* value_from_id(ValuePool* values, ValueId id) noexcept;



struct AstPool;

AstPool* create_ast_pool(AllocPool* pool) noexcept;

void release_ast_pool(AstPool* asts) noexcept;

a2::Node* alloc_ast(AstPool* asts, u32 dwords) noexcept;

a2::Node* create_builtin_definitions(AstPool* asts, IdentifierPool* identifiers, TypePool* types, ValuePool* values, a2::Builder* builder) noexcept;



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

[[nodiscard]] a2::Node* parse(Parser* parser, SourceFile source, AstPool* out) noexcept;

[[nodiscard]] a2::Builder* get_ast_builder(Parser* parser) noexcept;



struct ScopePool;

struct Scope;

struct ScopeHeader
{
	a2::Node* root;

	Scope* parent_scope;

	u32 capacity;

	u32 used;
};

struct ScopeEntry
{
	IdentifierId identifier_id;

	u32 node_offset;
};

struct Scope
{
	ScopeHeader header;

	#pragma warning(push)
	#pragma warning(disable : 4200) // nonstandard extension used: zero-sized array in struct/union
	ScopeEntry definitions[];
	#pragma warning(pop)
};

struct ScopeId
{
	u32 rep;
};

struct ScopeLocation
{
	Scope* ptr;

	ScopeId id;
};

static constexpr ScopeId INVALID_SCOPE_ID = { 0 };

static inline bool operator==(ScopeId lhs, ScopeId rhs) noexcept
{
	return lhs.rep == rhs.rep;
}

static inline bool operator!=(ScopeId lhs, ScopeId rhs) noexcept
{
	return lhs.rep != rhs.rep;
}

ScopePool* create_scope_pool(AllocPool* alloc, a2::Node* builtins) noexcept;

void release_scope_pool(ScopePool* scopes) noexcept;

ScopeLocation alloc_static_scope(ScopePool* scopes, Scope* parent_scope, a2::Node* root, u32 capacity) noexcept;

ScopeLocation alloc_top_level_scope(ScopePool* scopes, a2::Node* root) noexcept;

ScopeId id_from_scope(ScopePool* scopes, Scope* scope) noexcept;

Scope* scope_from_id(ScopePool* scopes, ScopeId id) noexcept;

void add_definition_to_scope(Scope* scope, a2::Node* definition) noexcept;

OptPtr<a2::Node> lookup_identifier_recursive(Scope* scope, IdentifierId identifier_id) noexcept;

OptPtr<a2::Node> lookup_identifier_local(Scope* scope, IdentifierId identifier_id) noexcept;



struct Interpreter;

struct Typechecker;

Interpreter* create_interpreter(AllocPool* alloc, ScopePool* scopes, TypePool* types, ValuePool* values, IdentifierPool* identifiers) noexcept;

Value* interpret_expr(Interpreter* interpreter, Scope* enclosing_scope, a2::Node* expr) noexcept;

void release_interpretation_result(Interpreter* interpreter, Value* result) noexcept;

void set_interpreter_typechecker(Interpreter* interpreter, Typechecker* typechecker) noexcept;



Typechecker* create_typechecker(AllocPool* alloc, Interpreter* Interpreter, ScopePool* scopes, TypePool* types, IdentifierPool* identifiers) noexcept;

void release_typechecker(Typechecker* typechecker) noexcept;

TypeId typecheck_expr(Typechecker* typechecker, Scope* enclosing_scope, a2::Node* expr) noexcept;

TypeId typecheck_definition(Typechecker* typechecker, a2::Node* definition) noexcept;

#endif // PARSEDATA_INCLUDE_GUARD
