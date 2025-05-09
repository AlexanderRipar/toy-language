#ifndef PARSEDATA_INCLUDE_GUARD
#define PARSEDATA_INCLUDE_GUARD

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



struct ValueId
{
	u32 rep;
};

struct TypeId2
{
	u32 rep;
};

struct SourceId
{
	u32 m_rep;
};

struct ScopePool;

struct SourceReader;

struct SourceLocation;

struct Scope;

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

IdentifierEntry* identifier_entry_from_identifier(IdentifierPool* identifiers, Range<char8> identifier) noexcept;

IdentifierId id_from_identifier(IdentifierPool* identifiers, Range<char8> identifier) noexcept;

IdentifierEntry* identifier_entry_from_id(IdentifierPool* identifiers, IdentifierId id) noexcept;



struct ErrorSink;

[[nodiscard]] ErrorSink* create_error_sink(AllocPool* pool, SourceReader* reader) noexcept;

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

	TypeId2 type_id;

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

struct CompositeTypeBuilder;

struct FuncTypeBuilder;

struct TypeId
{
	u32 rep;
};

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

enum class TypeFlag : u8
{
	EMPTY            = 0,
	SliceOrPtr_IsMut = 0x01,
	Ptr_IsOpt        = 0x02,
	Ptr_IsMulti      = 0x04,
	Integer_IsSigned = 0x02,
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

	u16 parameter_count;

	bool is_proc : 1;
};

struct FuncTypeParam
{
	u32 internal_flags : 28;

	u32 unused_ : 3;

	u32 is_mut : 1;

	IdentifierId name;

	TypeId type;

	ValueId default_value;
};

struct FuncType
{
	FuncTypeHeader header;

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
	FuncTypeParam params[];
	#if COMPILER_MSVC
	#pragma warning(pop)
	#elif COMPILER_CLANG
	#pragma clang diagnostic pop
	#elif COMPILER_GCC
	#pragma GCC diagnostic pop
	#endif
};

struct CompositeTypeHeader
{
	u32 size;

	u32 alignment;

	u32 stride;

	u32 member_count;

	Scope* scope;
};

struct CompositeTypeMember
{
	u32 internal_flags : 28;

	u32 is_mut : 1;

	u32 is_pub : 1;

	u32 is_global : 1;

	u32 is_use : 1;

	IdentifierId identifier_id;

	TypeId type_id;

	ValueId default_value;

	s64 offset; // when is_global: offset into global data segment; otherwise offset inside instances of type.
};

struct CompositeType
{
	CompositeTypeHeader header;

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
	CompositeTypeMember members[];
	#if COMPILER_MSVC
	#pragma warning(pop)
	#elif COMPILER_CLANG
	#pragma clang diagnostic pop
	#elif COMPILER_GCC
	#pragma GCC diagnostic pop
	#endif
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
	byte m_value[];
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
		return m_hash == key_hash && key.tag == tag && key.flags == flags && key.bytes.count() == size && memcmp(key.bytes.begin(), m_value, size) == 0;
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

TypeId dealias_type_id(TypePool* types, TypeEntry* entry) noexcept;

TypeId dealias_type_id(TypePool* types, TypeId id) noexcept;

TypeEntry* dealias_type_entry(TypePool* types, TypeEntry* entry) noexcept;

TypeEntry* dealias_type_entry(TypePool* types, TypeId id) noexcept;

bool can_implicity_convert_from_to(TypePool* types, TypeId from, TypeId to) noexcept;

OptPtr<TypeEntry> find_common_type_entry(TypePool* types, TypeEntry* a, TypeEntry* b) noexcept;

CompositeTypeBuilder* alloc_composite_type_builder(TypePool* types) noexcept;

void add_composite_type_member(TypePool* types, CompositeTypeBuilder* composite_builder, CompositeTypeMember member) noexcept;

TypeId complete_composite_type(TypePool* types, ScopePool* scopes, CompositeTypeBuilder* composite_builder, u32 size, u32 alignment, u32 stride) noexcept;

FuncTypeBuilder* alloc_func_type_builder(TypePool* types) noexcept;

void add_func_type_param(TypePool* types, FuncTypeBuilder* func_builder, FuncTypeParam param) noexcept;

TypeId complete_func_type(TypePool* types, FuncTypeBuilder* func_builder, TypeId return_type, bool is_proc) noexcept;



struct TypePool2;

struct TypeBuilder2;

struct Definition2
{
	IdentifierId name;

	u32 is_pub : 1;

	u32 is_mut : 1;

	u32 is_global : 1;

	u32 type_id_bits : 29;

	AstNodeId opt_type;

	AstNodeId opt_value;
};

struct Member2
{
	Definition2 definition;

	s64 offset;
};

struct alignas(u64) TypeEntry2
{
	TypeTag tag;

	u16 bytes;

	u32 inline_data;

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
};

struct SimpleType2
{
	static constexpr bool IS_INLINED = true;

	u32 unused_ = 0;
};

struct ReferenceType2
{
	static constexpr bool IS_INLINED = true;

	u32 is_mut : 1;

	u32 is_opt : 1;

	u32 is_multi : 1;

	u32 referenced_type_id : 29;
};

struct IntegerType2
{
	static constexpr bool IS_INLINED = true;

	u16 bits;
	
	bool is_signed;

	u8 unused_ = 0;
};

struct FloatType2
{
	static constexpr bool IS_INLINED = true;

	u16 bits;

	u16 unused_ = 0;
};

struct ArrayType2
{
	static constexpr bool IS_INLINED = false;

	// TypeId element_type_id stored in inline_data

	u64 element_count;
};

struct ArrayTypeInitializer2
{
	u32 unused_;

	TypeId2 element_type_id;

	u64 element_count;
};

struct CompositeTypeHeader2
{
	u64 size;

	u64 stride;

	u32 align;

	u16 member_count;

	bool is_complete;
};

struct CompositeType2
{
	static constexpr bool IS_INLINED = false;

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
	Member2 members[];
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
	TypeId2 return_type_id;

	u16 param_count;

	bool is_complete;

	bool is_proc;
};

struct FuncType2
{
	static constexpr bool IS_INLINED = false;

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
	Member2 param[];
	#if COMPILER_MSVC
	#pragma warning(pop)
	#elif COMPILER_CLANG
	#pragma clang diagnostic pop
	#elif COMPILER_GCC
	#pragma GCC diagnostic pop
	#endif
};

static constexpr TypeId2 INVALID_TYPE_ID_2 = { 0 };

static inline bool operator==(TypeId2 lhs, TypeId2 rhs) noexcept
{
	return lhs.rep == rhs.rep;
}

static inline bool operator!=(TypeId2 lhs, TypeId2 rhs) noexcept
{
	return lhs.rep != rhs.rep;
}

static inline Range<byte> array_type_initializer_bytes(const ArrayTypeInitializer2* initializer) noexcept
{
	return Range<byte>{ reinterpret_cast<const byte*>(&initializer->element_type_id), 12 };
}

template<typename T>
static inline T* data(TypeEntry2* entry) noexcept
{
	static_assert(!T::IS_INLINED || sizeof(T) <= sizeof(entry->inline_data));

	if constexpr (T::IS_INLINED)
		return reinterpret_cast<T*>(&entry->inline_data);
	else
		return reinterpret_cast<T*>(entry->data);
}

TypePool2* create_type_pool2(AllocPool* alloc) noexcept;

void release_type_pool2(TypePool2* types) noexcept;

TypeEntry2* type_entry_from_primitive_type(TypePool2* types, TypeTag tag, Range<byte> bytes) noexcept;

TypeEntry2* type_entry_from_id(TypePool2* types, TypeId id) noexcept;

TypeId2 id_from_type_entry(TypePool2* types, TypeEntry2* entry) noexcept;

TypeBuilder2* create_type_builder(TypePool2* types) noexcept;

void add_type_member(TypeBuilder2* builder, Member2 member) noexcept;

TypeEntry2* complete_type(TypeBuilder2* builder, u64 size, u32 align, u64 stride) noexcept;





struct ValuePool;

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
	byte value[];
	#if COMPILER_MSVC
	#pragma warning(pop)
	#elif COMPILER_CLANG
	#pragma clang diagnostic pop
	#elif COMPILER_GCC
	#pragma GCC diagnostic pop
	#endif
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
static inline T* data(Value* value) noexcept
{
	if (value->header.is_ref)
		value = reinterpret_cast<ReferenceValue*>(value->value)->referenced;

	return reinterpret_cast<T*>(value->value);
}

ValuePool* create_value_pool(AllocPool* alloc) noexcept;

void release_value_pool(ValuePool* values) noexcept;

ValueLocation alloc_value(ValuePool* values, u32 bytes) noexcept;

Value* value_from_id(ValuePool* values, ValueId id) noexcept;



struct Parser;

[[nodiscard]] Parser* create_parser(AllocPool* pool, IdentifierPool* identifiers, ErrorSink* errors) noexcept;

[[nodiscard]] AstNode* parse(Parser* parser, SourceFileRead read, bool is_std, AstPool* out) noexcept;

[[nodiscard]] AstBuilder* get_ast_builder(Parser* parser) noexcept;



struct ScopePool;

struct Scope;

struct ScopeHeader
{
	AstNode* root;

	Scope* parent_scope;

	u16 capacity;

	u16 used;

	bool is_composite : 1;
};

struct ScopeEntry
{
	IdentifierId identifier_id;

	AstNodeOffset node_offset;
};

struct Scope
{
	ScopeHeader header;

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
	ScopeEntry definitions[];
	#if COMPILER_MSVC
	#pragma warning(pop)
	#elif COMPILER_CLANG
	#pragma clang diagnostic pop
	#elif COMPILER_GCC
	#pragma GCC diagnostic pop
	#endif
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

struct ScopeLookupResult
{
	AstNode* definition;

	Scope* enclosing_scope;
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

static inline bool is_valid(ScopeLookupResult result) noexcept
{
	return result.definition != nullptr;
}

ScopePool* create_scope_pool(AllocPool* alloc) noexcept;

void release_scope_pool(ScopePool* scopes) noexcept;

Scope* alloc_scope(ScopePool* scopes, Scope* parent_scope, AstNode* root, u32 capacity) noexcept;

Scope* alloc_builtins_scope(ScopePool* scopes, AstNode* root, u32 capacity) noexcept;

void init_composite_scope(ScopePool* scopes, CompositeType* composite) noexcept;

ScopeId id_from_scope(ScopePool* scopes, Scope* scope) noexcept;

Scope* scope_from_id(ScopePool* scopes, ScopeId id) noexcept;

[[nodiscard]] bool add_definition_to_scope(Scope* scope, AstNode* definition) noexcept;

ScopeLookupResult lookup_identifier_recursive(Scope* scope, IdentifierId identifier_id) noexcept;

OptPtr<AstNode> lookup_identifier_local(Scope* scope, IdentifierId identifier_id) noexcept;





struct Interpreter;

struct Typechecker;

Interpreter* create_interpreter(AllocPool* alloc, SourceReader* reader, Parser* parser, AstPool* asts, ScopePool* scopes, TypePool* types, ValuePool* values, IdentifierPool* identifiers) noexcept;

Value* interpret_expr(Interpreter* interpreter, Scope* enclosing_scope, AstNode* expr) noexcept;

void release_interpretation_result(Interpreter* interpreter, Value* result) noexcept;

void set_interpreter_typechecker(Interpreter* interpreter, Typechecker* typechecker) noexcept;

TypeId import_file(Interpreter* interpreter, Range<char8> filepath, bool is_std) noexcept;



Typechecker* create_typechecker(AllocPool* alloc, Interpreter* Interpreter, ScopePool* scopes, TypePool* types, IdentifierPool* identifiers, AstPool* asts, AstBuilder* builder) noexcept;

void release_typechecker(Typechecker* typechecker) noexcept;

TypeId typecheck_expr(Typechecker* typechecker, Scope* enclosing_scope, AstNode* expr) noexcept;

TypeId typecheck_builtin(Typechecker* typechecker, Builtin builtin) noexcept;

void typecheck_definition(Typechecker* typechecker, Scope* enclosing_scope, AstNode* definition) noexcept;

TypeId typecheck_file(Typechecker* typechecker, AstNode* root) noexcept;

#endif // PARSEDATA_INCLUDE_GUARD
