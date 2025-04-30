#ifndef PASS_DATA_INCLUDE_GUARD
#define PASS_DATA_INCLUDE_GUARD

#include "../infra/common.hpp"
#include "../infra/alloc_pool.hpp"
#include "../infra/optptr.hpp"
#include "../infra/minos.hpp"



// Forward declarations.
// These are necessary because the modules defining them would otherwise appear
// after those using them.

// Id used to refer to a type in the `TypePool`. See `TypePool` for further
// information.
struct TypeId
{
	u32 rep;
};

// Id used to identify a particular source code location.
// This encodes the location's file, line and column. See `SourceReader` for
// further information.
struct SourceId
{
	u32 m_rep;
};

// Id that identifies a typechecker context so that it can be resumed to
// complete typechecking.
// A resumption only remains valid as long as typechecking has not
// concluded for the context it identifies has not completed. Since these
// context scoping rules are internal to `Interpreter`, this type can only be
// meaningfully used therein, and only stored for later use by an `Interpreter`
// otherwise.
struct TypecheckerResumptionId
{
	u32 rep;
};



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

	struct
	{
		struct
		{
			bool enable = false;

			Range<char8> log_filepath = {};
		} asts;

		struct
		{
			bool enable = false;

			Range<char8> log_filepath = {};
		} config;
	} logging;


	void* m_heap_ptr;

	Range<char8> m_config_filepath;
};

Config* create_config(AllocPool* alloc, Range<char8> filepath) noexcept;

void release_config(Config* config) noexcept;

void print_config(minos::FileHandle out, const Config* config) noexcept;

void print_config_help(u32 depth = 0) noexcept;



struct IdentifierPool;

struct IdentifierId
{
	u32 rep;
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

IdentifierId id_from_identifier(IdentifierPool* identifiers, Range<char8> identifier) noexcept;

IdentifierId id_from_identifier_with_token(IdentifierPool* identifiers, Range<char8> identifier, Token* out_token) noexcept;

Range<char8> identifier_name_from_id(const IdentifierPool* identifiers, IdentifierId id) noexcept;



struct CompIntegerValue
{
	u64 rep;
};

struct CompFloatValue
{
	f64 rep;
};

[[nodiscard]] CompIntegerValue comp_integer_from_u64(u64 value) noexcept;

[[nodiscard]] CompIntegerValue comp_integer_from_s64(s64 value) noexcept;

[[nodiscard]] bool comp_integer_from_comp_float(CompFloatValue value, CompIntegerValue* out) noexcept;

[[nodiscard]] bool u64_from_comp_integer(CompIntegerValue value, u64* out) noexcept;

[[nodiscard]] bool s64_from_comp_integer(CompIntegerValue value, s64* out) noexcept;

[[nodiscard]] CompIntegerValue comp_integer_add(CompIntegerValue lhs, CompIntegerValue rhs) noexcept;

[[nodiscard]] CompIntegerValue comp_integer_sub(CompIntegerValue lhs, CompIntegerValue rhs) noexcept;

[[nodiscard]] CompIntegerValue comp_integer_mul(CompIntegerValue lhs, CompIntegerValue rhs) noexcept;

[[nodiscard]] bool comp_integer_div(CompIntegerValue lhs, CompIntegerValue rhs, CompIntegerValue* out) noexcept;

[[nodiscard]] bool comp_integer_mod(CompIntegerValue lhs, CompIntegerValue rhs, CompIntegerValue* out) noexcept;

[[nodiscard]] CompIntegerValue comp_integer_neg(CompIntegerValue value) noexcept;

[[nodiscard]] CompIntegerValue comp_integer_shift_left(CompIntegerValue lhs, CompIntegerValue rhs) noexcept;

[[nodiscard]] CompIntegerValue comp_integer_shift_right(CompIntegerValue lhs, CompIntegerValue rhs) noexcept;

[[nodiscard]] bool comp_integer_bit_and(CompIntegerValue lhs, CompIntegerValue rhs, CompIntegerValue* out) noexcept;

[[nodiscard]] bool comp_integer_bit_or(CompIntegerValue lhs, CompIntegerValue rhs, CompIntegerValue* out) noexcept;

[[nodiscard]] bool comp_integer_bit_xor(CompIntegerValue lhs, CompIntegerValue rhs, CompIntegerValue* out) noexcept;

[[nodiscard]] bool comp_float_from_literal(Range<char8> literal, CompIntegerValue* out) noexcept;

[[nodiscard]] CompFloatValue comp_float_from_f64(f64 value) noexcept;

[[nodiscard]] CompFloatValue comp_float_from_f32(f32 value) noexcept;

[[nodiscard]] bool comp_float_from_u64(u64 value, CompFloatValue* out) noexcept;

[[nodiscard]] bool comp_float_from_s64(s64 value, CompFloatValue* out) noexcept;

[[nodiscard]] bool comp_float_from_comp_integer(CompIntegerValue value, CompFloatValue* out) noexcept;

[[nodiscard]] f64 f64_from_comp_float(CompFloatValue value) noexcept;

[[nodiscard]] f32 f32_from_comp_float(CompFloatValue value) noexcept;

[[nodiscard]] CompFloatValue comp_float_add(CompFloatValue lhs, CompFloatValue rhs) noexcept;

[[nodiscard]] CompFloatValue comp_float_sub(CompFloatValue lhs, CompFloatValue rhs) noexcept;

[[nodiscard]] CompFloatValue comp_float_mul(CompFloatValue lhs, CompFloatValue rhs) noexcept;

[[nodiscard]] CompFloatValue comp_float_div(CompFloatValue lhs, CompFloatValue rhs) noexcept;

[[nodiscard]] CompFloatValue comp_float_neg(CompFloatValue value) noexcept;



static constexpr s32 MAX_AST_DEPTH = 128;

struct AstPool;

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
	Identifer,
	LitInteger,
	LitFloat,
	LitChar,
	LitString,
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

	For_HasCondition     = 0x01,
	For_HasWhere         = 0x20,
	For_HasStep          = 0x02,
	For_HasFinally       = 0x04,

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

struct AstNodeId
{
	u32 rep;
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

struct AstLitIntegerData
{
	static constexpr AstTag TAG = AstTag::LitInteger;

	#pragma pack(push)
	#pragma pack(4)
	CompIntegerValue value;
	#pragma pack(pop)
};

struct AstLitFloatData
{
	static constexpr AstTag TAG = AstTag::LitFloat;

	#pragma pack(push)
	#pragma pack(4)
	CompFloatValue value;
	#pragma pack(pop)
};

struct AstLitCharData
{
	static constexpr AstTag TAG = AstTag::LitChar;

	u32 codepoint;
};

struct AstIdentifierData
{
	static constexpr AstTag TAG = AstTag::Identifer;

	IdentifierId identifier_id;
};

struct AstLitStringData
{
	static constexpr AstTag TAG = AstTag::LitString;

	IdentifierId string_id;
};

struct AstDefinitionData
{
	static constexpr AstTag TAG = AstTag::Definition;

	IdentifierId identifier_id;
};

struct AstBlockData
{
	static constexpr AstTag TAG = AstTag::Block;

	TypeId scope_type_id;
};

struct FuncInfo
{
	AstNode* parameters;

	OptPtr<AstNode> return_type;

	OptPtr<AstNode> expects;

	OptPtr<AstNode> ensures;

	OptPtr<AstNode> body;
};

struct DefinitionInfo
{
	OptPtr<AstNode> type;

	OptPtr<AstNode> value;
};

struct IfInfo
{
	AstNode* condition;

	AstNode* consequent;

	OptPtr<AstNode> alternative;

	OptPtr<AstNode> where;
};

struct ForInfo
{
	AstNode* condition;

	OptPtr<AstNode> step;

	OptPtr<AstNode> where;

	AstNode* body;

	OptPtr<AstNode> finally;
};

struct ForEachInfo
{
	AstNode* element;

	OptPtr<AstNode> index;

	AstNode* iterated;

	OptPtr<AstNode> where;

	AstNode* body;

	OptPtr<AstNode> finally;
};

static constexpr AstBuilderToken AST_BUILDER_NO_CHILDREN = { ~0u };

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

static inline bool operator==(AstBuilderToken lhs, AstBuilderToken rhs) noexcept
{
	return lhs.rep == rhs.rep;
}

static inline bool operator!=(AstBuilderToken lhs, AstBuilderToken rhs) noexcept
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

AstDirectChildIterator direct_children_of(AstNode* node) noexcept;

OptPtr<AstNode> next(AstDirectChildIterator* iterator) noexcept;

AstPreorderIterator preorder_ancestors_of(AstNode* node) noexcept;

AstIterationResult next(AstPreorderIterator* iterator) noexcept;

AstPostorderIterator postorder_ancestors_of(AstNode* node) noexcept;

AstIterationResult next(AstPostorderIterator* iterator) noexcept;

AstBuilderToken push_node(AstPool* asts, AstBuilderToken first_child, SourceId source_id, AstFlag flags, AstTag tag) noexcept;

AstBuilderToken push_node(AstPool* asts, AstBuilderToken first_child, SourceId source_id, AstFlag flags, AstTag tag, u8 attachment_dwords, const void* attachment) noexcept;

template<typename T>
static inline AstBuilderToken push_node(AstPool* asts, AstBuilderToken first_child, SourceId source_id, AstFlag flags, T attachment) noexcept
{
	static_assert(sizeof(T) % sizeof(u32) == 0);

	return push_node(asts, first_child, source_id, flags, T::TAG, sizeof(attachment) / sizeof(u32), &attachment);
}

AstNode* complete_ast(AstPool* asts) noexcept;

const char8* tag_name(AstTag tag) noexcept;

FuncInfo get_func_info(AstNode* func) noexcept;

DefinitionInfo get_definition_info(AstNode* definition) noexcept;

IfInfo get_if_info(AstNode* node) noexcept;

ForInfo get_for_info(AstNode* node) noexcept;

ForEachInfo get_foreach_info(AstNode* node) noexcept;



struct SourceReader;

struct SourceFile
{
	minos::FileHandle file;

	AstNodeId ast_root;

	SourceId source_id_base;
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



struct ErrorSink;

[[nodiscard]] ErrorSink* create_error_sink(AllocPool* pool, SourceReader* reader, IdentifierPool* identifiers) noexcept;

void release_error_sink(ErrorSink* errors) noexcept;

NORETURN void source_error(ErrorSink* errors, SourceId source_id, const char8* format, ...) noexcept;

NORETURN void vsource_error(ErrorSink* errors, SourceId source_id, const char8* format, va_list args) noexcept;

void source_warning(ErrorSink* errors, SourceId source_id, const char8* format, ...) noexcept;

void vsource_warning(ErrorSink* errors, SourceId source_id, const char8* format, va_list args) noexcept;

void print_error(const SourceLocation* location, const char8* format, va_list args) noexcept;



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
	Array,
	Func,
	Builtin,
	Composite,
	CompositeLiteral,
	ArrayLiteral,
	TypeBuilder,
	Variadic,
	Divergent,
	Trait,
	TypeInfo,
};

struct TypeMetrics
{
	u64 size;

	u64 stride;

	u32 align;
};

struct IncompleteMemberIterator
{
	void* structure;

	void* name;

	u16 rank;

	TypeId type_id;
};

struct MemberIterator
{
	void* structure;

	void* name;

	u16 rank;

	TypeId type_id;
};

struct MemberInfo
{
	IdentifierId name;

	TypeId opt_type;

	SourceId source;

	bool is_global : 1;

	bool is_pub : 1;

	bool is_use : 1;

	u16 rank;

	u64 offset_or_global_value;

	TypeId surrounding_type_id;

	AstNodeId opt_type_node_id;

	AstNodeId opt_value_node_id;

	TypecheckerResumptionId opt_type_resumption_id;
};

struct MemberInit
{
	IdentifierId name;

	union
	{
		TypeId id;

		TypecheckerResumptionId resumption_id;
	} type;

	SourceId source;

	bool is_global : 1;

	bool is_pub : 1;

	bool is_use : 1;

	bool has_pending_type : 1;

	u64 offset_or_global_value;

	AstNodeId opt_type_node_id;

	AstNodeId opt_value_node_id;
};

struct ReferenceType
{
	TypeId referenced_type_id;

	bool is_opt;

	bool is_multi;

	u64 unused_ = 0;
};

struct NumericType
{
	u16 bits;

	bool is_signed;

	u8 unused_ = 0;
};

struct ArrayType
{
	u64 element_count;

	TypeId element_type;

	u32 unused_ = 0;
};

struct FuncType
{
	TypeId return_type_id;

	u16 param_count;

	bool is_proc;

	TypeId signature_type_id;
};

static constexpr TypeId INVALID_TYPE_ID = { 0 };

static constexpr TypeId CHECKING_TYPE_ID = { 2 };

static constexpr TypeId NO_TYPE_TYPE_ID = { 4 };

[[nodiscard]] static inline bool is_assignable(TypeId type_id) noexcept
{
	return (type_id.rep & 1) == 1;
}

[[nodiscard]] static inline TypeId set_assignability(TypeId type_id, bool assignable) noexcept
{
	if (assignable)
		return TypeId{ type_id.rep | 1 };

	return TypeId{ type_id.rep & ~1 };
}

[[nodiscard]] static inline TypeId mask_assignability(TypeId type_id, bool assignable) noexcept
{
	if (assignable)
		return type_id;

	return TypeId{ type_id.rep & ~1 };
}

[[nodiscard]] TypePool* create_type_pool(AllocPool* alloc, ErrorSink* errors) noexcept;

void release_type_pool(TypePool* types) noexcept;

[[nodiscard]] const char8* tag_name(TypeTag tag) noexcept;

[[nodiscard]] TypeId primitive_type(TypePool* types, TypeTag tag, Range<byte> data) noexcept;

[[nodiscard]] TypeId alias_type(TypePool* types, TypeId aliased_type_id, bool is_distinct, SourceId source_id, IdentifierId name_id) noexcept;

[[nodiscard]] IdentifierId type_name_from_id(const TypePool* types, TypeId type_id) noexcept;

[[nodiscard]] SourceId type_source_from_id(const TypePool* types, TypeId type_id) noexcept;

[[nodiscard]] TypeId create_open_type(TypePool* types, SourceId source_id) noexcept;

void add_open_type_member(TypePool* types, TypeId open_type_id, MemberInit member) noexcept;

void close_open_type(TypePool* types, TypeId open_type_id, u64 size, u32 align, u64 stride) noexcept;

void set_incomplete_type_member_type_by_name(TypePool* types, TypeId open_type_id, IdentifierId member_name_id, TypeId member_type_id) noexcept;

void set_incomplete_type_member_type_by_rank(TypePool* types, TypeId open_type_id, u16 rank, TypeId member_type_id) noexcept;

[[nodiscard]] TypeMetrics type_metrics_from_id(TypePool* types, TypeId type_id) noexcept;

[[nodiscard]] TypeTag type_tag_from_id(TypePool* types, TypeId type_id) noexcept;

[[nodiscard]] bool type_can_implicitly_convert_from_to(TypePool* types, TypeId from_type_id, TypeId to_type_id) noexcept;

[[nodiscard]] TypeId common_type(TypePool* types, TypeId type_id_a, TypeId type_id_b) noexcept;

[[nodiscard]] bool type_member_info_by_name(TypePool* types, TypeId type_id, IdentifierId member_name_id, MemberInfo* out) noexcept;

[[nodiscard]] bool type_member_info_by_rank(TypePool* types, TypeId type_id, u16 rank, MemberInfo* out) noexcept;

[[nodiscard]] const void* primitive_type_structure(TypePool* types, TypeId type_id) noexcept;

[[nodiscard]] IncompleteMemberIterator incomplete_members_of(TypePool* types, TypeId type_id) noexcept;

[[nodiscard]] MemberInfo next(IncompleteMemberIterator* it) noexcept;

[[nodiscard]] bool has_next(IncompleteMemberIterator* it) noexcept;

[[nodiscard]] MemberIterator members_of(TypePool* types, TypeId type_id) noexcept;

[[nodiscard]] MemberInfo next(MemberIterator* it) noexcept;

[[nodiscard]] bool has_next(MemberIterator* it) noexcept;



struct Parser;

[[nodiscard]] Parser* create_parser(AllocPool* pool, IdentifierPool* identifiers, ErrorSink* errors, minos::FileHandle log_file) noexcept;

[[nodiscard]] AstNode* parse(Parser* parser, Range<char8> content, SourceId base_source_id, bool is_std, Range<char8> filepath) noexcept;



struct Interpreter;

enum class Builtin : u8
{
	INVALID = 0,
	Integer,
	Type,
	Definition,
	CompInteger,
	CompFloat,
	CompString,
	TypeBuilder,
	True,
	Typeof,
	Returntypeof,
	Sizeof,
	Alignof,
	Strideof,
	Offsetof,
	Nameof,
	Import,
	CreateTypeBuilder,
	AddTypeMember,
	CompleteType,
	MAX,
};

static constexpr TypecheckerResumptionId INVALID_RESUMPTION_ID = { 0 };

Interpreter* create_interpreter(AllocPool* alloc, Config* config, SourceReader* reader, Parser* parser, TypePool* types, AstPool* asts, IdentifierPool* identifiers, ErrorSink* errors) noexcept;

void release_interpreter([[maybe_unused]] Interpreter* interp) noexcept;

TypeId import_file(Interpreter* interp, Range<char8> filepath, bool is_std) noexcept;

const char8* tag_name(Builtin builtin) noexcept;

#endif // PASS_DATA_INCLUDE_GUARD
