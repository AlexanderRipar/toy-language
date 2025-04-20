#ifndef PASS_DATA_INCLUDE_GUARD
#define PASS_DATA_INCLUDE_GUARD

#include "../infra/common.hpp"
#include "../infra/container.hpp"
#include "../infra/threading.hpp"
#include "../infra/alloc_pool.hpp"
#include "../infra/optptr.hpp"

enum class Builtin : u8
{
	Integer,
	Type,
	CompInteger,
	CompFloat,
	CompString,
	TypeBuilder,
	True,
	Typeof,
	Sizeof,
	Alignof,
	Strideof,
	Offsetof,
	Nameof,
	Import,
	CreateTypeBuilder,
	AddTypeMember,
	CompleteType,
};



struct TypeId
{
	u32 rep;
};

struct SourceId
{
	u32 m_rep;
};

struct SourceReader;

struct SourceLocation;

struct AstNode;



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
		KwdDistinct,          // distinct
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
		Builtin,              // '_' ( 'a' - 'z' | 'A' - 'Z' | '0' - '9' | '_' )+    --- Only if is_std == true
		Wildcard,             // _
		END_OF_SOURCE,
		MAX,
};

const char8* token_name(Token token) noexcept;



struct Config
{
	struct
	{
		Range<char8> filepath = range::from_literal_string("main.evl");

		Range<char8> symbol = range::from_literal_string("main");
	} entrypoint;

	struct
	{
		Range<char8> filepath = range::from_literal_string("std.evl");
	} std;

	void* m_heap_ptr;
};

Config* create_config(AllocPool* alloc, Range<char8> filepath) noexcept;

void release_config(Config* config) noexcept;

void print_config(const Config* config) noexcept;

void print_config_help(u32 depth = 0) noexcept;



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
	char8 m_chars[];
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

void release_identifier_pool(IdentifierPool* identifiers) noexcept;

IdentifierEntry* identifier_entry_from_identifier(IdentifierPool* identifiers, Range<char8> identifier) noexcept;

IdentifierId id_from_identifier(IdentifierPool* identifiers, Range<char8> identifier) noexcept;

IdentifierEntry* identifier_entry_from_id(IdentifierPool* identifiers, IdentifierId id) noexcept;



struct ErrorSink;

[[nodiscard]] ErrorSink* create_error_sink(AllocPool* pool, SourceReader* reader, IdentifierPool* identifiers) noexcept;

void release_error_sink(ErrorSink* errors) noexcept;

NORETURN void source_error(ErrorSink* errors, SourceId source_id, const char8* format, ...) noexcept;

NORETURN void vsource_error(ErrorSink* errors, SourceId source_id, const char8* format, va_list args) noexcept;

void print_error(const SourceLocation* location, const char8* format, va_list args) noexcept; 



static constexpr s32 MAX_AST_DEPTH = 128;

struct AstPool;

struct AstNodeId
{
	u32 rep;
};

struct AstNodeOffset
{
	s32 rep;
};

enum class AstTag : u8
{
	INVALID = 0,
	Builtin,
	File,
	CompositeInitializer,
	ArrayInitializer,
	Wildcard,
	Where,
	Expects,
	Ensures,
	Definition,
	Block,
	If,
	For,
	ForEach,
	Switch,
	Case,
	Func,
	Trait,
	Impl,
	Catch,
	ValIdentifer,
	ValInteger,
	ValFloat,
	ValChar,
	ValString,
	Return,
	Leave,
	Yield,
	ParameterList,
	Call,
	UOpTypeTailArray,
	UOpTypeSlice,
	UOpTypeMultiPtr,
	UOpTypeOptMultiPtr,
	UOpEval,
	UOpTry,
	UOpDefer,
	UOpDistinct,
	UOpAddr,
	UOpDeref,
	UOpBitNot,
	UOpLogNot,
	UOpTypeOptPtr,
	UOpTypeVar,
	UOpImpliedMember,
	UOpTypePtr,
	UOpNegate,
	UOpPos,
	OpAdd,
	OpSub,
	OpMul,
	OpDiv,
	OpAddTC,
	OpSubTC,
	OpMulTC,
	OpMod,
	OpBitAnd,
	OpBitOr,
	OpBitXor,
	OpShiftL,
	OpShiftR,
	OpLogAnd,
	OpLogOr,
	OpMember,
	OpCmpLT,
	OpCmpGT,
	OpCmpLE,
	OpCmpGE,
	OpCmpNE,
	OpCmpEQ,
	OpSet,
	OpSetAdd,
	OpSetSub,
	OpSetMul,
	OpSetDiv,
	OpSetAddTC,
	OpSetSubTC,
	OpSetMulTC,
	OpSetMod,
	OpSetBitAnd,
	OpSetBitOr,
	OpSetBitXor,
	OpSetShiftL,
	OpSetShiftR,
	OpTypeArray,
	OpArrayIndex,
	MAX,
};

enum class AstFlag : u8
{
	EMPTY                = 0,

	Definition_IsPub     = 0x01,
	Definition_IsMut     = 0x02,
	Definition_IsGlobal  = 0x04,
	Definition_IsAuto    = 0x08,
	Definition_IsUse     = 0x10,
	Definition_HasType   = 0x20,

	If_HasWhere          = 0x20,
	If_HasElse           = 0x01,

	For_HasWhere         = 0x20,
	For_HasStep          = 0x01,
	For_HasFinally       = 0x02,

	ForEach_HasWhere     = 0x20,
	ForEach_HasIndex     = 0x01,
	ForEach_HasFinally   = 0x02,

	Switch_HasWhere      = 0x20,

	Func_HasExpects      = 0x01,
	Func_HasEnsures      = 0x02,
	Func_IsProc          = 0x04,
	Func_HasReturnType   = 0x08,
	Func_HasBody         = 0x10,

	Trait_HasExpects     = 0x01,

	Impl_HasExpects      = 0x01,

	Catch_HasDefinition  = 0x01,

	Type_IsMut           = 0x02,
};

struct AstNode
{
	static constexpr u8 FLAG_LAST_SIBLING  = 0x01;
	static constexpr u8 FLAG_FIRST_SIBLING = 0x02;
	static constexpr u8 FLAG_NO_CHILDREN   = 0x04;

	AstTag tag;

	AstFlag flags;

	u8 data_dwords;

	u8 internal_flags;

	u32 next_sibling_offset;

	TypeId type_id;

	SourceId source_id;
};

struct AstBuilderToken
{
	u32 rep;
};

struct AstBuilder
{
	static constexpr AstBuilderToken NO_CHILDREN = { ~0u };

	ReservedVec<u32> scratch;
};

struct AstIterationResult
{
	AstNode* node;

	u32 depth;
};

struct AstDirectChildIterator
{
	AstNode* curr;
};

struct AstPreorderIterator
{
	AstNode* curr;

	u8 depth;

	s32 top;

	u8 prev_depths[MAX_AST_DEPTH];

	static_assert(MAX_AST_DEPTH <= UINT8_MAX);
};

struct AstPostorderIterator
{
	AstNode* base;

	s32 depth;

	u32 offsets[MAX_AST_DEPTH];
};

inline AstFlag operator|(AstFlag lhs, AstFlag rhs) noexcept
{
	return static_cast<AstFlag>(static_cast<u8>(lhs) | static_cast<u8>(rhs));
}

inline AstFlag operator&(AstFlag lhs, AstFlag rhs) noexcept
{
	return static_cast<AstFlag>(static_cast<u8>(lhs) & static_cast<u8>(rhs));
}

inline AstFlag& operator|=(AstFlag& lhs, AstFlag rhs) noexcept
{
	lhs = lhs | rhs;

	return lhs;
}

inline AstFlag& operator&=(AstFlag& lhs, AstFlag rhs) noexcept
{
	lhs = lhs & rhs;

	return lhs;
}

inline bool operator==(AstNodeId lhs, AstNodeId rhs) noexcept
{
	return lhs.rep == rhs.rep;
}

inline bool operator!=(AstNodeId lhs, AstNodeId rhs) noexcept
{
	return lhs.rep != rhs.rep;
}

static constexpr AstNodeId INVALID_AST_NODE_ID = { 0 };

AstPool* create_ast_pool(AllocPool* pool) noexcept;

void release_ast_pool(AstPool* asts) noexcept;

AstNode* alloc_ast(AstPool* asts, u32 dwords) noexcept;

AstNodeId id_from_ast_node(AstPool* asts, AstNode* node) noexcept;

AstNode* ast_node_from_id(AstPool* asts, AstNodeId id) noexcept;

static inline AstNode* apply_offset_(AstNode* node, ureg offset) noexcept
{
	static_assert(sizeof(AstNode) % sizeof(u32) == 0 && alignof(AstNode) % sizeof(u32) == 0);

	return reinterpret_cast<AstNode*>(reinterpret_cast<u32*>(node) + offset);
}

static inline AstNode* apply_offset_(AstNode* node, AstNodeOffset offset) noexcept
{
	static_assert(sizeof(AstNode) % sizeof(u32) == 0 && alignof(AstNode) % sizeof(u32) == 0);

	return reinterpret_cast<AstNode*>(reinterpret_cast<u32*>(node) + offset.rep);
}

static inline AstNodeOffset get_offset(AstNode* from, AstNode* to) noexcept
{
	return { static_cast<s32>(reinterpret_cast<u32*>(to) - reinterpret_cast<u32*>(from)) };
}

static inline bool has_children(const AstNode* node) noexcept
{
	return (node->internal_flags & AstNode::FLAG_NO_CHILDREN) == 0;
}

static inline bool has_next_sibling(const AstNode* node) noexcept
{
	return (node->internal_flags & AstNode::FLAG_LAST_SIBLING) == 0;
}

static inline bool has_flag(AstNode* node, AstFlag flag) noexcept
{
	return (static_cast<u8>(node->flags) & static_cast<u8>(flag)) != 0;
}

static inline AstNode* next_sibling_of(AstNode* node) noexcept
{
	ASSERT_OR_IGNORE(has_next_sibling(node));

	return apply_offset_(node, node->next_sibling_offset);
}

static inline AstNode* first_child_of(AstNode* node) noexcept
{
	ASSERT_OR_IGNORE(has_children(node));

	return apply_offset_(node, node->data_dwords);
}

template<typename T>
static inline T* attachment_of(AstNode* node) noexcept
{
	ASSERT_OR_IGNORE(T::TAG == node->tag);

	ASSERT_OR_IGNORE(sizeof(T) + sizeof(AstNode) == node->data_dwords * sizeof(u32));

	return reinterpret_cast<T*>(node + 1);
}

template<typename T>
static inline const T* attachment_of(const AstNode* node) noexcept
{
	ASSERT_OR_IGNORE(T::TAG == node->tag);

	ASSERT_OR_IGNORE(sizeof(T) + sizeof(AstNode) == node->data_dwords * sizeof(u32));

	return reinterpret_cast<const T*>(node + 1);
}

static inline bool is_valid(AstIterationResult result) noexcept
{
	return result.node != nullptr;
}

static inline AstDirectChildIterator direct_children_of(AstNode* node) noexcept
{
	return { has_children(node) ? first_child_of(node) : nullptr };
}

static inline OptPtr<AstNode> next(AstDirectChildIterator* iterator) noexcept
{
	if (iterator->curr == nullptr)
		return none<AstNode>();

	AstNode* const curr = iterator->curr;

	iterator->curr = has_next_sibling(curr) ? next_sibling_of(curr) : nullptr;

	return some(curr);
}

static inline OptPtr<AstNode> peek(const AstDirectChildIterator* iterator) noexcept
{
	return maybe(iterator->curr);
}

static inline AstPreorderIterator preorder_ancestors_of(AstNode* node) noexcept
{
	AstPreorderIterator iterator;

	if (has_children(node))
	{
		iterator.curr = first_child_of(node);
		iterator.depth = 0;
		iterator.top = -1;
	}
	else
	{
		iterator.curr = nullptr;
		iterator.depth = 0;
		iterator.top = -1;
	}

	return iterator;
}

static inline AstIterationResult next(AstPreorderIterator* iterator) noexcept
{
	if (iterator->curr == nullptr)
		return { nullptr, 0 };

	AstIterationResult result = { iterator->curr, iterator->depth };

	AstNode* const curr = iterator->curr;

	iterator->curr = apply_offset_(curr, curr->data_dwords);

	if ((curr->internal_flags & AstNode::FLAG_NO_CHILDREN) == 0)
	{
		if ((curr->internal_flags & AstNode::FLAG_LAST_SIBLING) == 0)
		{
			ASSERT_OR_IGNORE(iterator->top + 1 < MAX_AST_DEPTH);

			iterator->top += 1;

			iterator->prev_depths[iterator->top] = iterator->depth;
		}

		ASSERT_OR_IGNORE(iterator->depth + 1 < MAX_AST_DEPTH);

		iterator->depth += 1;
	}
	else if ((curr->internal_flags & AstNode::FLAG_LAST_SIBLING) == AstNode::FLAG_LAST_SIBLING)
	{
		if (iterator->top == -1)
		{
			iterator->curr = nullptr;
		}
		else
		{
			iterator->depth = iterator->prev_depths[iterator->top];

			iterator->top -= 1;
		}
	}

	return result;
}

static inline AstIterationResult peek(const AstPreorderIterator* iterator) noexcept
{
	return { iterator->curr, iterator->depth };
}

static inline AstPostorderIterator postorder_ancestors_of(AstNode* node) noexcept
{
	AstPostorderIterator iterator;

	iterator.base = node;
	iterator.depth = -1;

	while (has_children(node))
	{
		ASSERT_OR_IGNORE(iterator.depth < MAX_AST_DEPTH);

		node = first_child_of(node);

		iterator.depth += 1;

		iterator.offsets[iterator.depth] = static_cast<u32>(reinterpret_cast<u32*>(node) - reinterpret_cast<u32*>(iterator.base));
	}

	return iterator;
}

static inline AstIterationResult next(AstPostorderIterator* iterator) noexcept
{
	if (iterator->depth < 0)
		return { nullptr, 0 };

	AstNode* const ret_node = reinterpret_cast<AstNode*>(reinterpret_cast<u32*>(iterator->base) + iterator->offsets[iterator->depth]);

	const u32 ret_depth = static_cast<u32>(iterator->depth);

	AstNode* curr = ret_node;

	if (has_next_sibling(curr))
	{
		curr = next_sibling_of(curr);

		iterator->offsets[iterator->depth] = static_cast<u32>(reinterpret_cast<u32*>(curr) - reinterpret_cast<u32*>(iterator->base));

		while (has_children(curr))
		{
			curr = first_child_of(curr);

			iterator->depth += 1;

			ASSERT_OR_IGNORE(iterator->depth < MAX_AST_DEPTH);

			iterator->offsets[iterator->depth] = static_cast<u32>(reinterpret_cast<u32*>(curr) - reinterpret_cast<u32*>(iterator->base));
		}
	}
	else
	{
		iterator->depth -= 1;

		if (iterator->depth >= 0)
			curr = reinterpret_cast<AstNode*>(reinterpret_cast<u32*>(iterator->base) + iterator->offsets[iterator->depth]);
	}

	return { ret_node, ret_depth };
}

static inline AstIterationResult peek(const AstPostorderIterator* iterator) noexcept
{
	if (iterator->depth == -1)
		return { nullptr, 0 };

	return { apply_offset_(iterator->base, iterator->offsets[iterator->depth]), static_cast<u32>(iterator->depth) };
}

static inline bool operator==(AstBuilderToken lhs, AstBuilderToken rhs) noexcept
{
	return lhs.rep == rhs.rep;
}

static inline bool operator!=(AstBuilderToken lhs, AstBuilderToken rhs) noexcept
{
	return lhs.rep != rhs.rep;
}

static inline AstBuilder create_ast_builder() noexcept
{
	AstBuilder builder;

	builder.scratch.init(1u << 31, 1u << 18);

	return builder;
}

static inline AstBuilderToken push_node(AstBuilder* builder, AstBuilderToken first_child, SourceId source_id, AstFlag flags, AstTag tag) noexcept
{
	static_assert(sizeof(AstNode) % sizeof(u32) == 0);

	AstNode* const node = reinterpret_cast<AstNode*>(builder->scratch.reserve_exact(sizeof(AstNode)));

	node->next_sibling_offset = first_child.rep;
	node->tag = tag;
	node->flags = flags;
	node->data_dwords = sizeof(AstNode) / sizeof(u32);
	node->internal_flags = first_child == AstBuilder::NO_CHILDREN ? AstNode::FLAG_NO_CHILDREN : 0;
	node->source_id = source_id;

	return { static_cast<u32>(reinterpret_cast<u32*>(node) - builder->scratch.begin()) };
}

template<typename T>
static inline AstBuilderToken push_node(AstBuilder* builder, AstBuilderToken first_child, SourceId source_id, AstFlag flags, T attachment) noexcept
{
	static_assert(sizeof(AstNode) % sizeof(u32) == 0);

	static_assert(sizeof(T) % sizeof(u32) == 0);

	const u32 required_dwords = (sizeof(AstNode) + sizeof(T)) / sizeof(u32);

	AstNode* const node = reinterpret_cast<AstNode*>(builder->scratch.reserve_exact(required_dwords * sizeof(u32)));

	node->next_sibling_offset = first_child.rep;
	node->tag = T::TAG;
	node->flags = flags;
	node->data_dwords = required_dwords;
	node->internal_flags = first_child == AstBuilder::NO_CHILDREN ? AstNode::FLAG_NO_CHILDREN : 0;
	node->source_id = source_id;

	memcpy(node + 1, &attachment, sizeof(T));

	return { static_cast<u32>(reinterpret_cast<u32*>(node) - builder->scratch.begin()) };
}

AstNode* complete_ast(AstBuilder* builder, AstPool* dst) noexcept;

const char8* ast_tag_name(AstTag tag) noexcept;



struct SourceFile
{
	minos::FileHandle file;

	AstNodeId ast_root;

	u32 source_id_base;
};

struct SourceFileRead
{
	SourceFile* source_file;

	Range<char8> content;
};

struct SourceLocation
{
	Range<char8> filepath;

	u32 line_number;

	u32 column_number;

	u32 context_offset;

	u32 context_chars;

	char8 context[512];
};

struct SourceReader;

static inline bool operator==(SourceId lhs, SourceId rhs) noexcept
{
	return lhs.m_rep == rhs.m_rep;
}

static inline bool operator!=(SourceId lhs, SourceId rhs) noexcept
{
	return lhs.m_rep != rhs.m_rep;
}

static constexpr SourceId INVALID_SOURCE_ID = { 0 };

[[nodiscard]] SourceReader* create_source_reader(AllocPool* pool) noexcept;

void release_source_reader(SourceReader* reader) noexcept;

[[nodiscard]] SourceFileRead read_source_file(SourceReader* reader, Range<char8> filepath) noexcept;

void release_read(SourceReader* reader, SourceFileRead read) noexcept;

[[nodiscard]] SourceLocation source_location_from_ast_node(SourceReader* reader, AstNode* node) noexcept;

[[nodiscard]] SourceLocation source_location_from_source_id(SourceReader* reader, SourceId source_id) noexcept;

[[nodiscard]] SourceFile* source_file_from_source_id(SourceReader* reader, SourceId source_id) noexcept;

[[nodiscard]] Range<char8> source_file_path(SourceReader* reader, SourceFile* source_file) noexcept;



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



struct TypePool;

enum class TypeTag : u8
{
	INVALID = 0,
	Void,
	Type,
	Definition,
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
	Builtin,
	Composite,
	CompositeLiteral,
	ArrayLiteral,
	CallFrame,
	TypeBuilder,
};

struct TypeBuilder;

struct IncompleteMemberIterator
{
	TypeBuilder* builder;

	u32 curr;
};

struct Definition
{
	IdentifierId name;

	u32 is_pub : 1;

	u32 is_mut : 1;

	u32 is_global : 1;

	u32 type_id_bits : 29;

	AstNodeId opt_type;

	AstNodeId opt_value;
};

struct Member
{
	Definition definition;

	s64 offset;
};

struct alignas(u64) TypeStructure
{
	TypeTag tag;

	u16 bytes;

	u32 m_hash;

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
	u64 data[];
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

	static u32 required_strides(AttachmentRange<byte, TypeTag> key) noexcept
	{
		return static_cast<u32>((offsetof(TypeStructure, data) + key.count() + stride() - 1) / stride());
	}

	u32 used_strides() const noexcept
	{
		return static_cast<u32>((offsetof(TypeStructure, data) + bytes + stride() - 1) / stride());
	}

	u32 hash() const noexcept
	{
		return m_hash;
	}

	bool equal_to_key(AttachmentRange<byte, TypeTag> key, u32 key_hash) const noexcept
	{
		return m_hash == key_hash && key.attachment() == tag && key.count() == bytes && memcmp(key.begin(), data, key.count()) == 0;
	}

	void init(AttachmentRange<byte, TypeTag> key, u32 key_hash) noexcept
	{
		ASSERT_OR_IGNORE(key.count() <= UINT16_MAX);

		m_hash = key_hash;

		bytes = static_cast<u16>(key.count());

		tag = key.attachment();

		memcpy(data, key.begin(), key.count());
	}
};

struct ReferenceType2
{
	u32 is_mut : 1;

	u32 is_opt : 1;

	u32 is_multi : 1;

	u32 referenced_type_id : 29;
};

struct IntegerType2
{
	u16 bits;

	bool is_signed;

	u8 unused_ = 0;
};

struct FloatType2
{
	u16 bits;

	u16 unused_ = 0;
};

struct ArrayType2
{
	TypeId element_type;

	u64 element_count;
};

struct CompositeTypeHeader2
{
	u64 size;

	u64 stride;

	u32 align;

	u32 member_count;
};

struct CompositeType2
{
	CompositeTypeHeader2 header;

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
	Member members[];
	#if COMPILER_MSVC
	#pragma warning(pop)
	#elif COMPILER_CLANG
	#pragma clang diagnostic pop
	#elif COMPILER_GCC
	#pragma GCC diagnostic pop
	#endif
};

struct FuncTypeHeader2
{
	TypeId return_type_id;

	u16 param_count;

	bool is_complete;

	bool is_proc;
};

struct FuncType2
{
	FuncTypeHeader2 header;

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
	Member param[];
	#if COMPILER_MSVC
	#pragma warning(pop)
	#elif COMPILER_CLANG
	#pragma clang diagnostic pop
	#elif COMPILER_GCC
	#pragma GCC diagnostic pop
	#endif
};



static constexpr TypeId INVALID_TYPE_ID_2 = { 0 };

static inline bool operator==(TypeId lhs, TypeId rhs) noexcept
{
	return lhs.rep == rhs.rep;
}

static inline bool operator!=(TypeId lhs, TypeId rhs) noexcept
{
	return lhs.rep != rhs.rep;
}

template<typename T>
[[nodiscard]] static inline T* data(TypeStructure* entry) noexcept
{
	return reinterpret_cast<T*>(&entry->data);
}

template<typename T>
[[nodiscard]] static inline const T* data(const TypeStructure* entry) noexcept
{
	return reinterpret_cast<const T*>(&entry->data);
}

[[nodiscard]] TypePool* create_type_pool2(AllocPool* alloc, ErrorSink* errors) noexcept;

void release_type_pool2(TypePool* types) noexcept;

[[nodiscard]] TypeId primitive_type(TypePool* types, TypeTag tag, Range<byte> data) noexcept;

[[nodiscard]] TypeId alias_type(TypePool* types, TypeId aliased_type_id, bool is_distinct, SourceId source_id, IdentifierId name_id) noexcept;

[[nodiscard]] OptPtr<TypeStructure> type_structure_from_id(TypePool* types, TypeId type_id) noexcept;

[[nodiscard]] TypeBuilder* create_type_builder(TypePool* types, SourceId source_id) noexcept;

void add_type_builder_member(TypeBuilder* builder, Member member) noexcept;

[[nodiscard]] TypeId complete_type_builder(TypeBuilder* builder, u64 size, u32 align, u64 stride) noexcept;

[[nodiscard]] bool type_compatible(TypePool* types, TypeId type_id_a, TypeId type_id_b) noexcept;

[[nodiscard]] bool type_can_cast_from_to(TypePool* types, TypeId from_type_id, TypeId to_type_id) noexcept;

[[nodiscard]] TypeId common_type(TypePool* types, TypeId type_id_a, TypeId type_id_b) noexcept;

[[nodiscard]] Member* type_get_member(TypePool* types, TypeId type_id, IdentifierId member_name) noexcept;

[[nodiscard]] IncompleteMemberIterator incomplete_members_of(TypePool* types, TypeId type_id) noexcept;

[[nodiscard]] OptPtr<Member> next(IncompleteMemberIterator* it) noexcept;



struct Parser;

[[nodiscard]] Parser* create_parser(AllocPool* pool, IdentifierPool* identifiers, ErrorSink* errors) noexcept;

[[nodiscard]] AstNode* parse(Parser* parser, SourceFileRead read, bool is_std, AstPool* out) noexcept;

[[nodiscard]] AstBuilder* get_ast_builder(Parser* parser) noexcept;

#endif // PASS_DATA_INCLUDE_GUARD
